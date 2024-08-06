// 3D World - lighting code, incuding static and dynamic lights, profile generation, and flow calculation
// by Frank Gennari
// 1/16/06
#include "mesh.h"
#include "csg.h"
#include "lightmap.h"
#include "gl_ext_arb.h"
#include "shaders.h"
#include "binary_file_io.h"
#include "profiler.h"
#include <functional>

using std::cerr;

float const DZ_VAL_SCALE     = 2.0;
float const DARKNESS_THRESH  = 0.1;
float const DEF_SKY_GLOBAL_LT= 0.25; // when ray tracing is not used
float const FLASHLIGHT_RAD   = 4.0;

colorRGBA const flashlight_colors[2] = {colorRGBA(1.0, 0.8, 0.5, 1.0), colorRGBA(0.8, 0.8, 1.0, 1.0)}; // incandescent, LED


bool using_lightmap(0), lm_alloc(0), has_dl_sources(0), has_spotlights(0), has_line_lights(0), use_dense_voxels(0), has_indir_lighting(0);
bool dl_smap_enabled(0), flashlight_on(0), enable_dlight_bcubes(0);
unsigned dl_tid(0), elem_tid(0), gb_tid(0), dl_bc_tid(0), DL_GRID_BS(0), flashlight_color_id(0);
float DZ_VAL2(0.0), DZ_VAL_INV2(0.0);
float czmin0(0.0), lm_dz_adj(0.0);
cube_t dlight_bcube(all_zeros_cube);
vector<dls_cell> ldynamic;
vector<unsigned char> ldynamic_enabled;
vector<light_source> light_sources_a, dl_sources, dl_sources2; // static ambient, static diffuse, dynamic {cur frame, next frame}
vector<light_source_trig> light_sources_d;
lmap_manager_t lmap_manager;
llv_vect local_light_volumes;
indir_dlight_group_manager_t indir_dlight_group_manager;


extern int animate2, display_mode, camera_coll_id, scrolling, read_light_files[], write_light_files[];
extern unsigned create_voxel_landscape;
extern bool disable_dlights;
extern float czmin, czmax, fticks, zbottom, ztop, XY_SCENE_SIZE, FAR_CLIP, CAMERA_RADIUS, indir_light_exp, light_int_scale[], force_czmin, force_czmax;
extern colorRGB cur_ambient, cur_diffuse;
extern coll_obj_group coll_objects;
extern vector<light_source> enabled_lights;


inline bool add_cobj_ok(coll_obj const &cobj) { // skip small things like tree leaves and such
	return (cobj.fixed && !cobj.disabled() && cobj.volume > 0.0001); // cobj.type == COLL_CUBE
}
colorRGBA const &get_flashlight_color() {return flashlight_colors[flashlight_color_id];}


// *** R_PROFILE IMPLEMENTATION ***


void r_profile::reset_bbox(float const bb_[2][2]) {
	
	clear();
	bb       = rect(bb_);
	tot_area = bb.area();
	assert(tot_area > 0.0);
}


void r_profile::clear() {
	
	rects.resize(0);
	filled    = 0;
	avg_alpha = 1.0;
}


void r_profile::add_rect_int(rect const &r) {

	for (unsigned i = 0; i < rects.size(); ++i) { // try rect merge
		if (rects[i].merge_with(r)) return;
	}
	rects.push_back(r);
}


bool r_profile::add_rect(float const d[3][2], unsigned d0, unsigned d1, float alpha=1.0) {
	
	if (filled || alpha == 0.0) return 0;
	rect r(d, d0, d1);
	if (!r.nonzero() || !r.overlaps(bb.d)) return 0;
	r.clip_to(bb.d);
	//if (r.is_near_zero_area())  return 0;
	
	if (r.equal(bb.d)) { // full containment
		if (alpha < 1.0) {
			// *** WRITE ***
		}
		else {avg_alpha = 1.0;}
		rects.resize(0);
		add_rect_int(r);
		filled = 1;
		return 1;
	}
	if (rects.empty()) { // single rect performance optimization
		add_rect_int(r);
		avg_alpha = alpha;
		return 1;
	}
	unsigned const nrects((unsigned)rects.size());

	for (unsigned i = 0; i < nrects; ++i) { // check if contained in any rect
		if (rects[i].contains(r.d)) return 1; // minor performance improvement
	}
	pend.push_back(r);

	while (!pend.empty()) { // merge new rect into working set while removing overlaps
		bool bad_rect(0);
		rect rr(pend.front());
		pend.pop_front();

		for (unsigned i = 0; i < nrects; ++i) { // could start i at the value of i where rr was inserted into pend
			if (rects[i].overlaps(rr.d)) { // split rr
				rects[i].subtract_from(rr, pend);
				bad_rect = 1;
				break;
			}
		}
		if (!bad_rect) add_rect_int(rr);
	}
	if (rects.size() > nrects) avg_alpha = 1.0; // at least one rect was added, *** FIX ***
	return 1;
}


float r_profile::clipped_den_inv(float const c[2]) const { // clip by first dimension
	
	if (filled)        return (1.0 - avg_alpha);
	if (rects.empty()) return 1.0;
	bool const no_clip(c[0] == bb.d[0][0] && c[1] == bb.d[0][1]);
	unsigned const nrects((unsigned)rects.size());
	float a(0.0);

	if (no_clip) {
		for (unsigned i = 0; i < nrects; ++i) {a += rects[i].area();}
	}
	else {
		for (unsigned i = 0; i < nrects; ++i) {a += rects[i].clipped_area(c);}
	}
	if (a == 0.0) return 1.0;
	a *= avg_alpha;
	float const area(no_clip ? tot_area : (c[1] - c[0])*(bb.d[1][1] - bb.d[1][0]));
	if (a > area + TOLER) cout << "a = " << a << ", area = " << area << ", size = " << rects.size() << endl;
	assert(a <= area + TOLER);
	return (area - a)/area;
}


void r_profile::clear_within(float const c[2]) {

	float const rd[2][2] = {{c[0], c[1]}, {bb.d[1][0], bb.d[1][1]}};
	rect const r(rd);
	bool removed(0);

	for (unsigned i = 0; i < rects.size(); ++i) {
		if (!r.overlaps(rects[i].d)) continue;
		r.subtract_from(rects[i], pend);
		swap(rects[i], rects.back());
		rects.pop_back();
		removed = 1;
		--i; // wraparound OK
	}
	copy(pend.begin(), pend.end(), back_inserter(rects));
	pend.resize(0);
	if (removed) filled = 0;
	//avg_alpha = 1.0; // recalculate?
}


// *** MAIN LIGHTING CODE ***


void reset_cobj_counters() {
	for (unsigned i = 0; i < (unsigned)coll_objects.size(); ++i) {coll_objects[i].counter = -1;}
}


void lmcell::get_final_color(colorRGB &color, float max_indir, float indir_scale, float extra_ambient) const {

	float const max_s(max(sc[0], max(sc[1], sc[2])));
	float const max_g(max(gc[0], max(gc[1], gc[2])));
	float const sv_scaled((max_s > 0.0 && sv > 0.0) ? min(1.0f, sv*light_int_scale[LIGHTING_SKY   ])/max_s : 0.0);
	float const gv_scaled((max_g > 0.0 && gv > 0.0) ? min(1.0f, gv*light_int_scale[LIGHTING_GLOBAL])/max_g : 0.0);
	bool const apply_sqrt(indir_light_exp > 0.49 && indir_light_exp < 0.51), apply_exp(!apply_sqrt && indir_light_exp != 1.0);

	UNROLL_3X(float indir_term((sv_scaled*sc[i_] + extra_ambient)*cur_ambient[i_] + gv_scaled*gc[i_]*cur_diffuse[i_]); \
			  if (indir_term > 0.0 && apply_sqrt) {indir_term = sqrt(indir_term);} \
			  else if (indir_term > 0.0 && apply_exp) {indir_term = pow(indir_term, indir_light_exp);} \
			  color[i_] = min(max_indir, indir_scale*indir_term) + min(1.0f, lc[i_]*light_int_scale[LIGHTING_LOCAL]);)
}

void lmcell::get_final_color_local(colorRGB &color) const {
	UNROLL_3X(color[i_] = min(1.0f, lc[i_]*light_int_scale[LIGHTING_LOCAL]);)
}

void lmcell::set_outside_colors() {
	sv = 1.0;
	gv = 0.0;
	UNROLL_3X(sc[i_] = gc[i_] = 1.0; lc[i_] = 0.0;)
}


inline bool is_inside_lmap(int x, int y, int z) {return (z >= 0 && z < MESH_SIZE[2] && !point_outside_mesh(x, y));}
bool lmap_manager_t::is_valid_cell(int x, int y, int z) const {return (is_inside_lmap(x, y, z) && vlmap[y][x] != NULL);}

// Note: only intended to work in ground mode where sizes are MESH_X_SIZE and MESH_Y_SIZE
lmcell *lmap_manager_t::get_lmcell_round_down(point const &p) { // round down
	int const x(get_xpos_round_down(p.x)), y(get_ypos_round_down(p.y)), z(get_zpos(p.z));
	return (is_valid_cell(x, y, z) ? &vlmap[y][x][z] : NULL);
}
lmcell *lmap_manager_t::get_lmcell(point const &p) { // round to center
	int const x(get_xpos(p.x)), y(get_ypos(p.y)), z(get_zpos(p.z));
	return (is_valid_cell(x, y, z) ? &vlmap[y][x][z] : NULL);
}

void lmap_manager_t::reset_all(lmcell const &init_lmcell) {
	for (auto i = vldata_alloc.begin(); i != vldata_alloc.end(); ++i) {*i = init_lmcell;}
}

template<typename T> void lmap_manager_t::alloc(unsigned nbins, unsigned xsize, unsigned ysize, unsigned zsize, T **nonempty_bins, lmcell const &init_lmcell) {

	lm_xsize = xsize; lm_ysize = ysize; lm_zsize = zsize;
	if (vlmap == NULL) {matrix_gen_2d(vlmap, lm_xsize, lm_ysize);} // create column headers once
	vldata_alloc.resize(max(nbins, 1U), init_lmcell); // make size at least 1, even if there are no bins, so we can test on emptiness
	unsigned cur_v(0);

	// initialize light volume
	for (unsigned i = 0; i < lm_ysize; ++i) {
		for (unsigned j = 0; j < lm_xsize; ++j) {
			if (nonempty_bins != nullptr && !nonempty_bins[i][j]) { // nonempty_bins is used for sparse mode
				vlmap[i][j] = NULL;
				continue;
			}
			assert(cur_v + lm_zsize <= vldata_alloc.size());
			vlmap[i][j] = &vldata_alloc[cur_v];
			cur_v      += lm_zsize;
		}
	}
	assert(cur_v == nbins);
}

template void lmap_manager_t::alloc(unsigned nbins, unsigned xsize, unsigned ysize, unsigned zsize, unsigned char **nonempty_bins, lmcell const &init_lmcell); // explicit instantiation


void lmap_manager_t::init_from(lmap_manager_t const &src) {

	//assert(!is_allocated());
	//clear_cells(); // probably unnecessary
	alloc(src.vldata_alloc.size(), src.lm_xsize, src.lm_ysize, src.lm_zsize, src.vlmap, lmcell());
	copy_data(src);
}


// *this = blend_weight*dest + (1.0 - blend_weight)*(*this)
void lmap_manager_t::copy_data(lmap_manager_t const &src, float blend_weight) {

	assert(vlmap && src.vlmap);
	assert(src.lm_xsize == lm_xsize && src.lm_ysize == lm_ysize && src.lm_zsize == lm_zsize);
	assert(src.vldata_alloc.size() == vldata_alloc.size());
	assert(blend_weight >= 0.0);
	if (blend_weight == 0.0) return; // keep existing dest

	if (blend_weight == 1.0) {
		vldata_alloc = src.vldata_alloc; // deep copy all lmcell data
		return;
	}
	for (unsigned i = 0; i < lm_ysize; ++i) { // openmp?
		for (unsigned j = 0; j < lm_xsize; ++j) {
			if (!vlmap[i][j]) {assert(!src.vlmap[i][j]); continue;}
			assert(src.vlmap[i][j]);
			
			for (unsigned z = 0; z < lm_zsize; ++z) {
				vlmap[i][j][z].mix_lighting_with(src.vlmap[i][j][z], blend_weight);
			}
		}
	}
}


// *this = val*lmc + (1.0 - val)*(*this)
void lmcell::mix_lighting_with(lmcell const &lmc, float val) {

	float const omv(1.0 - val); // Note: we ignore the flow values and smoke for now
	sv = val*lmc.sv + omv*sv;
	gv = val*lmc.gv + omv*gv;
	UNROLL_3X(sc[i_] = val*lmc.sc[i_] + omv*sc[i_];)
	UNROLL_3X(gc[i_] = val*lmc.gc[i_] + omv*gc[i_];)
	UNROLL_3X(lc[i_] = val*lmc.lc[i_] + omv*lc[i_];)
}


int light_grid_base::check_lmap_get_grid_index(point const &p) const {
	int const x(get_xpos_round_down(p.x)), y(get_ypos_round_down(p.y)), z(get_zpos(p.z));
	if (!lmap_manager.is_valid_cell(x, y, z)) return -1; // the global lightmap doesn't have this cell
	return get_ix(x, y, z);
}


void light_volume_local::allocate() {
	compressed = 0;
	set_bounds(0, MESH_X_SIZE, 0, MESH_Y_SIZE, 0, MESH_SIZE[2]);
	data.clear();
	data.resize(get_num_data()); // init to all zeros
}

void light_volume_local::add_color(point const &p, colorRGBA const &color) { // inlined in the header?

	assert(!compressed); // compressed is read only
	int const ix(check_lmap_get_grid_index(p));
	if (ix < 0) return; // if the global lightmap doesn't have this cell, the local lmap shouldn't need it
	assert((unsigned)ix < data.size());
	UNROLL_3X(data[ix].lc[i_] += color[i_]*color.alpha;)
	changed = 1;
}

void light_volume_local::add_lighting(colorRGB &color, int x, int y, int z) const {

	//if (!is_active()) return; // not yet allocated - caller should check this
	if (!check_xy_bounds(x, y) || z < bounds[2][0] || z >= bounds[2][1]) return;
	unsigned const ix(((y - bounds[1][0])*(bounds[0][1] - bounds[0][0]) + (x - bounds[0][0]))*(bounds[2][1] - bounds[2][0]) + (z - bounds[2][0]));
	assert(ix < data.size());
	UNROLL_3X(color[i_] = min(1.0f, color[i_]+data[ix].lc[i_]*scale);)
}

bool light_volume_local::read(string const &filename) {

	assert(!is_allocated());
	binary_file_reader reader;
	if (!reader.open(filename)) return 0;

	if (!reader.read(bounds, sizeof(int), 6)) {
		cerr << "Error: Failed to read header from light volume file '" << filename << "'." << endl;
		return 0;
	}
	data.resize(get_num_data());
	assert(is_allocated());

	if (!reader.read(&data.front(), sizeof(lmcell_local), data.size())) {
		cerr << "Error: Failed to read data from light volume file '" << filename << "'." << endl;
		return 0;
	}
	compressed = 1; // llvols are always written compressed
	changed    = 1;
	cout << "Read light volume file '" << filename << "'." << endl;
	return 1;
}

bool light_volume_local::write(string const &filename) const {

	assert(is_allocated());
	assert(compressed); // llvols are always written compressed
	binary_file_writer writer;
	if (!writer.open(filename)) return 0;

	if (!writer.write(bounds, sizeof(int), 6)) {
		cerr << "Error: Failed to write header to light volume file '" << filename << "'." << endl;
		return 0;
	}
	if (!writer.write(&data.front(), sizeof(lmcell_local), data.size())) {
		cerr << "Error: Failed to write data to light volume file '" << filename << "'." << endl;
		return 0;
	}
	cout << "Wrote light volume file '" << filename << "'." << endl;
	return 1;
}

void light_volume_local::set_bounds(int x1, int x2, int y1, int y2, int z1, int z2) {
	bounds[0][0] = x1; bounds[0][1] = x2; bounds[1][0] = y1; bounds[1][1] = y2; bounds[2][0] = z1; bounds[2][1] = z2;
}

void update_range(int bnds[2], int v) {bnds[0] = min(bnds[0], v); bnds[1] = max(bnds[1], v+1);} // max is one past the end

void light_volume_local::compress(bool verbose) {

	if (compressed) return; // already compressed
	float const toler(1.0/(256.0 * max(0.001f, scale)));
	assert(is_allocated());
	set_bounds(MESH_X_SIZE, 0, MESH_Y_SIZE, 0, MESH_SIZE[2], 0);
	bool nonempty(0);

	for (int y = 0; y < MESH_Y_SIZE; ++y) {
		for (int x = 0; x < MESH_X_SIZE; ++x) {
			for (int z = 0; z < MESH_SIZE[2]; ++z) {
				if (data[get_ix(x, y, z)].is_near_zero(toler)) continue;
				update_range(bounds[0], x);
				update_range(bounds[1], y);
				update_range(bounds[2], z);
				nonempty = 1;
			}
		}
	}
	if (!nonempty) { // empty case, generally shouldn't happen
		set_bounds(0, 0, 0, 0, 0, 0);
		data.clear();
		return;
	}
	vector<lmcell_local> comp_data(get_num_data());
	unsigned data_pos(0);
	
	for (int y = bounds[1][0]; y < bounds[1][1]; ++y) {
		for (int x = bounds[0][0]; x < bounds[0][1]; ++x) {
			for (int z = bounds[2][0]; z < bounds[2][1]; ++z) {
				comp_data[data_pos++] = data[get_ix(x, y, z)];
			}
		}
	}
	assert(data_pos == comp_data.size());

	if (verbose) {
		cout << "uncomp size: " << data.size() << ", bounds: {" << bounds[0][0] << "," << bounds[0][1] << "},{" << bounds[1][0] << ","
			 << bounds[1][1] << "},{" << bounds[2][0] << "," << bounds[2][1] << "}" << " comp size: " << comp_data.size() << endl;
	}
	data.swap(comp_data);
	compressed = 1;
}

void light_volume_local::init(unsigned lvol_ix, float scale_, string const &filename) {

	RESET_TIME;
	set_scale(scale_);
	if (!filename.empty() && read(filename)) return; // see if there is an existing file to read
	gen_data(lvol_ix, 1);
	if (!filename.empty()) {write(filename);} // write the output file
	PRINT_TIME("Local Dlight Volume Creation");
}

void light_volume_local::gen_data(unsigned lvol_ix, bool verbose) {
	
	//RESET_TIME;
	allocate();
	//PRINT_TIME("Alloc");
	compute_ray_trace_lighting((LIGHTING_DYNAMIC + lvol_ix), verbose);
	//PRINT_TIME("Alloc + Trace");
	compress(verbose);
	//PRINT_TIME("Alloc + Trace + Compress");
}


unsigned tag_ix_map::get_ix_for_name(string const &name) {

	if (name == "none" || name == "null" || name.empty()) return 0;
	auto ret(name_to_ix.insert(make_pair(name, next_ix)));
	if (ret.second) {++next_ix;} // increment next_ix if a new value was inserted
	return ret.first->second;
}

unsigned indir_dlight_group_manager_t::get_ix_for_name(std::string const &name, float scale) {

	unsigned const tag_ix(tag_ix_map::get_ix_for_name(name));
	if (tag_ix >= groups.size()) {groups.resize(tag_ix+1);}
	else if (groups[tag_ix].scale != scale) {cout << "Warning: dlight name '" << name << "' was set to two different scales of " << groups[tag_ix].scale << " and " << scale << endl;}
	groups[tag_ix].scale = scale;
	if (name.find_last_of('.') != string::npos) {groups[tag_ix].filename = name;} // if it has a file extension (.), assume it's a filename
	return tag_ix;
}

void indir_dlight_group_manager_t::add_dlight_ix_for_tag_ix(unsigned tag_ix, unsigned dlight_ix) {

	if (tag_ix == 0) return; // first group is empty
	assert(tag_ix < groups.size());
	groups[tag_ix].dlight_ixs.push_back(dlight_ix); // check valid dlight_ix?
}

void indir_dlight_group_manager_t::create_needed_llvols() {

	for (unsigned i = 0; i < groups.size(); ++i) {
		group_t &g(groups[i]);
		if (g.dlight_ixs.empty()) continue; // no lights for this group (including empty group 0)
		unsigned num_enabled(0);
		bool is_dynamic(0);

		for (auto l = g.dlight_ixs.begin(); l != g.dlight_ixs.end(); ++l) {
			assert(*l < light_sources_d.size());
			light_source_trig &ls(light_sources_d[*l]);
			assert(ls.get_indir_dlight_ix() == i);
			num_enabled += ls.is_enabled();
			is_dynamic  |= ls.need_update_indir();
		}
		// scale by the ratio of enabled to disabled lights, which is approximate but as close as we can get with a single volume for this group of lights;
		// if more precision/control is required, the group can be split into multiple lighting volumes at the cost of increased runtime/memory/storage
		float const scale(g.scale*light_int_scale[LIGHTING_DYNAMIC]*(float(num_enabled)/g.dlight_ixs.size()));

		if (g.llvol_ix >= 0) { // already valid - check enabled state
			assert((unsigned)g.llvol_ix < local_light_volumes.size());
			local_light_volumes[g.llvol_ix]->set_scale(scale);
			if (is_dynamic && num_enabled > 0) {local_light_volumes[g.llvol_ix]->gen_data(g.llvol_ix, 0);} // dynamic update
		}
		else if (num_enabled > 0) { // not valid but needed - create
			g.llvol_ix = local_light_volumes.size();
			local_light_volumes.push_back(std::unique_ptr<light_volume_local>(new light_volume_local(i)));
			local_light_volumes[g.llvol_ix]->init(g.llvol_ix, scale, g.filename);
		}
	}
}


void create_dlight_volumes() {indir_dlight_group_manager.create_needed_llvols();}


bool has_fixed_cobjs(int x, int y) {

	assert(!point_outside_mesh(x, y));
	vector<int> const &cvals(v_collision_matrix[y][x].cvals);

	for (vector<int>::const_iterator i = cvals.begin(); i != cvals.end(); ++i) {
		if (coll_objects[*i].fixed && coll_objects[*i].status == COLL_STATIC) {return 1;}
	}
	return 0;
}

void regen_lightmap() {

	if (MESH_Z_SIZE == 0) return; // not using lmap
	assert(lmap_manager.is_allocated());
	clear_lightmap();
	assert(!lmap_manager.is_allocated());
	build_lightmap(0);
	assert(lmap_manager.is_allocated());
}


void clear_lightmap() {

	if (!lmap_manager.is_allocated()) return;
	kill_current_raytrace_threads(); // kill raytrace threads and wait for them to finish since they are using the current lightmap
	lmap_manager.clear_cells();
	using_lightmap = 0;
	lm_alloc       = 0;
	czmin0         = czmin;
}


void calc_flow_profile(r_profile flow_prof[3], int i, int j, bool proc_cobjs, float zstep) {

	assert(zstep > 0.0);
	lmcell *vldata(lmap_manager.get_column(j, i));
	if (vldata == NULL) return;
	float const bbz[2][2] = {{get_xval(j), get_xval(j+1)}, {get_yval(i), get_yval(i+1)}}; // X x Y
	vector<pair<float, unsigned> > cobj_z;

	if (proc_cobjs) {
		coll_cell const &cell(v_collision_matrix[i][j]);
		unsigned const ncv((unsigned)cell.cvals.size());

		for (unsigned q = 0; q < ncv; ++q) {
			unsigned const cid(cell.cvals[q]);
			coll_obj const &cobj(coll_objects.get_cobj(cid));
			if (cobj.status != COLL_STATIC) continue;
			if (cobj.d[2][1] < zbottom)     continue; // below the mesh
			if ((cobj.type == COLL_CYLINDER_ROT || cobj.type == COLL_CAPSULE) && !line_is_axis_aligned(cobj.points[0], cobj.points[1])) continue; // bounding cube is too conservative, skip
			if (cobj.type == COLL_TORUS && !line_is_axis_aligned(cobj.points[0], cobj.points[0]+cobj.norm)) continue; // bounding cube is too conservative, skip
			rect const r_cobj(cobj.d, 0, 1);
			if (!r_cobj.nonzero())          continue; // zero Z cross section (vertical polygon)
			float cztop;
					
			if (r_cobj.overlaps(bbz) && add_cobj_ok(cobj) && cobj.clip_in_2d(bbz, cztop, 0, 1, 1)) {
				cobj_z.push_back(make_pair(cztop, cid)); // still incorrect for coll polygon since x and y aren't clipped
			}
		}
		sort(cobj_z.begin(), cobj_z.end(), std::greater<pair<float, unsigned> >()); // max to min z
	}
	unsigned const ncv2((unsigned)cobj_z.size());

	for (int v = MESH_SIZE[2]-1; v >= 0; --v) { // top to bottom
		float zb(czmin0 + v*zstep), zt(zb + zstep); // cell Z bounds
		
		if (zt < mesh_height[i][j]) { // under mesh
			UNROLL_3X(vldata[v].pflow[i_] = 0;) // all zeros
		}
		else if (!proc_cobjs /*|| ncv2 == 0*/) { // ignore cobjs or no cobjs
			UNROLL_3X(vldata[v].pflow[i_] = 255;) // all ones
		}
		else { // above mesh case
			float const bb[3][2]  = {{bbz[0][0], bbz[0][1]}, {bbz[1][0], bbz[1][1]}, {zb, zt}};
			float const bbx[2][2] = {{bb[1][0], bb[1][1]}, {zb, zt}}; // YxZ
			float const bby[2][2] = {{zb, zt}, {bb[0][0], bb[0][1]}}; // ZxX
			flow_prof[0].reset_bbox(bbx);
			flow_prof[1].reset_bbox(bby);
			flow_prof[2].reset_bbox(bbz);
			
			for (unsigned c2 = 0; c2 < ncv2; ++c2) { // could make this more efficient
				coll_obj &cobj(coll_objects[cobj_z[c2].second]);
				if (cobj.d[0][0] >= bb[0][1] || cobj.d[0][1]     <= bb[0][0]) continue; // no intersection
				if (cobj.d[1][0] >= bb[1][1] || cobj.d[1][1]     <= bb[1][0]) continue;
				if (cobj.d[2][0] >= bb[2][1] || cobj_z[c2].first <= bb[2][0]) continue;
				float const cztop(cobj.d[2][1]);
				cobj.d[2][1] = cobj_z[c2].first;
						
				for (unsigned d = 0; d < 3; ++d) { // critical path
					flow_prof[d].add_rect(cobj.d, (d+1)%3, (d+2)%3, 1.0);
				}
				cobj.d[2][1] = cztop; // restore original value
			} // for c2
			for (unsigned e = 0; e < 3; ++e) {
				float const fv(flow_prof[e].den_inv());
				assert(fv > -TOLER);
				vldata[v].pflow[e] = (unsigned char)(255.5*CLIP_TO_01(fv));
			}
		} // if above mesh
	} // for v
}

cube_t get_scene_bounds_bcube() { // for use with indir lighting
	return cube_t(-X_SCENE_SIZE, X_SCENE_SIZE, -Y_SCENE_SIZE, Y_SCENE_SIZE, get_zval_min(), get_zval_max());
}
float calc_czspan() {return max(0.0f, ((czmax + lm_dz_adj) - czmin0 + TOLER));}

unsigned get_grid_xsize() {return max((MESH_X_SIZE >> DL_GRID_BS), 1);}
unsigned get_grid_ysize() {return max((MESH_Y_SIZE >> DL_GRID_BS), 1);}
unsigned get_ldynamic_ix(unsigned x, unsigned y) {return (y >> DL_GRID_BS)*get_grid_xsize() + (x >> DL_GRID_BS);}


void build_lightmap(bool verbose) {

	if (lm_alloc) return; // what about recreating the lightmap if the scene has changed?
	if (force_czmin != 0.0) {czmin = force_czmin;}
	if (force_czmax != 0.0) {czmax = force_czmax;}

	// prevent the z range from being empty/denormalized when there are no cobjs
	if (use_dense_voxels) {
		czmin = min(czmin, zbottom);
		czmax = max(czmax, (czmin + Z_SCENE_SIZE - 0.5f*DZ_VAL));
	}
	else if (czmin >= czmax) {
		czmin = min(czmin, zbottom);
		czmax = max(czmax, ztop);
	}

	// calculate and allocate some data we need even if the lmap is not used
	assert(DZ_VAL > 0.0);
	DZ_VAL2     = DZ_VAL/DZ_VAL_SCALE;
	DZ_VAL_INV2 = 1.0/DZ_VAL2;
	czmin0      = czmin;//max(czmin, zbottom);
	assert(lm_dz_adj >= 0.0);

	if (!disable_dlights) {
		ldynamic.resize(get_grid_xsize()*get_grid_ysize());
		ldynamic_enabled.resize(ldynamic.size(), 0);
	}
	if (MESH_Z_SIZE == 0) return;

	RESET_TIME;
	unsigned nonempty(0);
	unsigned char **need_lmcell = NULL;
	matrix_gen_2d(need_lmcell);
	bool has_fixed(0);
	
	// determine where we will need lmcells
	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			bool const fixed(!coll_objects.empty() && has_fixed_cobjs(j, i));
			need_lmcell[i][j] = (use_dense_voxels || fixed);
			has_fixed        |= fixed; // only used in an assertion below
			if (need_lmcell[i][j]) ++nonempty;
		}
	}

	// add cells surrounding static scene lights
	// Note: this isn't really necessary when using ray casting for lighting,
	//       but it helps ensure there are lmap cells around light sources to light the dynamic objects
	for (unsigned i = 0; i < light_sources_a.size(); ++i) {
		cube_t bcube; // unused
		int bnds[3][2];
		light_sources_a[i].get_bounds(bcube, bnds, SQRT_CTHRESH);

		for (int y = bnds[1][0]; y <= bnds[1][1]; ++y) {
			for (int x = bnds[0][0]; x <= bnds[0][1]; ++x) {
				if (!need_lmcell[y][x]) {++nonempty;}
				need_lmcell[y][x] |= 2;
			}
		}
	}

	// determine allocation and voxel grid sizes
	reset_cobj_counters();
	float const czspan(calc_czspan()), dz(DZ_VAL_INV2*czspan);
	assert(dz >= 0.0);
	assert(coll_objects.empty() || !has_fixed || dz > 0.0); // too strict (all cobjs can be shifted off the mesh)
	unsigned zsize(unsigned(dz + 1));
	
	if ((int)zsize > MESH_Z_SIZE) {
		cout << "* Warning: Scene height extends beyond the specified z range. Clamping zsize of " << zsize << " to " << MESH_Z_SIZE << "." << endl;
		zsize = MESH_Z_SIZE;
	}
	unsigned const nbins(nonempty*zsize);
	MESH_SIZE[2] = zsize; // override MESH_SIZE[2]
	float const zstep(czspan/zsize);
	if (verbose) {cout << "Lightmap zsize= " << zsize << ", nonempty= " << nonempty << ", bins= " << nbins << ", czmin= " << czmin0 << ", czmax= " << czmax << endl;}
	assert(zstep > 0.0);
	bool raytrace_lights[NUM_LIGHTING_TYPES] = {0};
	for (unsigned i = 0; i < NUM_LIGHTING_TYPES; ++i) {raytrace_lights[i] = (read_light_files[i] || write_light_files[i]);}
	has_indir_lighting = (raytrace_lights[LIGHTING_SKY] || raytrace_lights[LIGHTING_GLOBAL] || create_voxel_landscape);
	lmcell init_lmcell;

	if (!has_indir_lighting) { // set a default value that isn't all black
		init_lmcell.sv = init_lmcell.gv = DEF_SKY_GLOBAL_LT;
		UNROLL_3X(init_lmcell.sc[i_] = init_lmcell.gc[i_] = 1.0;)
	}
	lmap_manager.alloc(nbins, MESH_X_SIZE, MESH_Y_SIZE, zsize, need_lmcell, init_lmcell);
	assert(lmap_manager.is_allocated());
	using_lightmap = (nonempty > 0);
	lm_alloc       = 1;

	// calculate particle flow values
	r_profile flow_prof[3]; // particle {x, y, z}

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			bool const proc_cobjs(need_lmcell[i][j] & 1);
			calc_flow_profile(flow_prof, i, j, proc_cobjs, zstep);
		}
	}

	// add in static light sources
	if (!raytrace_lights[LIGHTING_LOCAL]) {
		for (unsigned i = 0; i < light_sources_a.size(); ++i) {
			light_source &ls(light_sources_a[i]);
			point const lpos1(ls.get_pos()), lpos2(ls.get_pos()), lposc(0.5*(lpos1 + lpos2)); // start, end, center
			if (!is_over_mesh(lposc)) continue;
			colorRGBA const &lcolor(ls.get_color());
			cube_t bcube; // unused
			int bnds[3][2] = {}, cent[3] = {}, cobj(-1), last_cobj(-1);
			
			for (unsigned j = 0; j < 3; ++j) {
				cent[j] = max(0, min(MESH_SIZE[j]-1, get_dim_pos(lposc[j], j))); // clamp to mesh bounds
			}
			ls.get_bounds(bcube, bnds, SQRT_CTHRESH);
			check_coll_line(lpos1, lpos2, cobj, -1, 1, 2, 1); // check cobj containment and ignore that shape (ignore voxels)

			for (int y = bnds[1][0]; y <= bnds[1][1]; ++y) {
				for (int x = bnds[0][0]; x <= bnds[0][1]; ++x) {
					assert(lmap_manager.get_column(x, y));
					float const xv(get_xval(x)), yv(get_yval(y));

					for (int z = bnds[2][0]; z <= bnds[2][1]; ++z) {
						assert(unsigned(z) < zsize);
						point const p(xv, yv, get_zval(z));
						point lpos(lposc); // will be updated for line lights
						float cscale(ls.get_intensity_at(p, lpos));
						if (cscale < CTHRESH) {if (z > cent[2]) break; else continue;}
				
						if (ls.is_directional()) {
							cscale *= ls.get_dir_intensity(lpos - p);
							if (cscale < CTHRESH) continue;
						}
						point const lpos_ext(lpos + HALF_DXY*(p - lpos).get_norm()); // extend away from light to account for light fixtures
						if ((last_cobj >= 0 && coll_objects[last_cobj].line_intersect(lpos_ext, p)) ||
							check_coll_line(p, lpos_ext, last_cobj, cobj, 1, 3)) {continue;}
						lmcell &lmc(lmap_manager.get_lmcell(x, y, z));
						UNROLL_3X(lmc.lc[i_] = min(1.0f, (lmc.lc[i_] + cscale*lcolor[i_]));) // what about diffuse/normals?
					} // for z
				} // for x
			} // for y
		} // for i
	}
	if (nbins > 0) {
		if (verbose) PRINT_TIME(" Lighting Setup + XYZ Passes");
		// Note: sky and global lighting use the same data structure for reading/writing, so they should have the same filename if used together
		string const type_names[NUM_LIGHTING_TYPES] = {" Sky", " Global", " Local", " Cobj Accum", " Dynamic"};

		for (unsigned ltype = 0; ltype < NUM_LIGHTING_TYPES; ++ltype) {
			if (raytrace_lights[ltype]) {
				compute_ray_trace_lighting(ltype, 1); // verbose=1
				if (verbose) {PRINT_TIME((type_names[ltype] + " Lighting Load/Ray Trace").c_str());}
			}
		}
	}
	reset_cobj_counters();
	matrix_delete_2d(need_lmcell);
	if (!scrolling) {PRINT_TIME(" Lighting Total");}
}


int get_clamped_xpos(float xval) {return max(0, min(MESH_X_SIZE-1, get_xpos(xval)));}
int get_clamped_ypos(float yval) {return max(0, min(MESH_Y_SIZE-1, get_ypos(yval)));}


void update_flow_for_voxels(vector<cube_t> const &cubes) {

	//RESET_TIME;
	if (!lm_alloc || !lmap_manager.is_allocated() || cubes.empty()) return;
	cube_t bcube(cubes.front());
	for (auto i = cubes.begin()+1; i != cubes.end(); ++i) {bcube.union_with_cube(*i);}
	int const bcx1(get_clamped_xpos(bcube.d[0][0])), bcx2(get_clamped_xpos(bcube.d[0][1]));
	int const bcy1(get_clamped_xpos(bcube.d[1][0])), bcy2(get_clamped_xpos(bcube.d[1][1])); // what if entirely off the mesh?
	int const dx(bcx2 - bcx1 + 1), dy(bcy2 - bcy1 + 1);
	vector<unsigned char> updated;
	if (cubes.size() > 1) {updated.resize(dx*dy, 0);}

	for (auto i = cubes.begin(); i != cubes.end(); ++i) {
		int const cx1(get_clamped_xpos(i->d[0][0])), cx2(get_clamped_xpos(i->d[0][1]));
		int const cy1(get_clamped_xpos(i->d[1][0])), cy2(get_clamped_xpos(i->d[1][1]));
		float const zstep(calc_czspan()/MESH_SIZE[2]);
		r_profile flow_prof[3];

		for (int y = cy1; y <= cy2; ++y) {
			for (int x = cx1; x <= cx2; ++x) {
				if (cubes.size() > 1) {
					unsigned const ix((y - bcy1)*dx + (x - bcx1));
					if (updated[ix]) continue;
					updated[ix] = 1;
				}
				assert(!point_outside_mesh(x, y));
				bool const fixed(!coll_objects.empty() && has_fixed_cobjs(x, y));
				calc_flow_profile(flow_prof, y, x, (use_dense_voxels || fixed), zstep);
			} // for x
		} //for y
	}
	//PRINT_TIME("Update Flow");
}

void update_indir_light_tex_range(lmap_manager_t const &lmap, vector<unsigned char> &tex_data,
	unsigned xsize, unsigned y1, unsigned y2, unsigned zsize, float lighting_exponent, bool local_only, bool mt)
{
	bool const apply_sqrt(lighting_exponent > 0.49 && lighting_exponent < 0.51), apply_exp(!apply_sqrt && lighting_exponent != 1.0);
	assert(lmap.is_allocated());

#pragma omp parallel for schedule(static) if (mt)
	for (int y = y1; y < (int)y2; ++y) {
		for (unsigned x = 0; x < xsize; ++x) {
			unsigned const off(zsize*(y*xsize + x));
			lmcell const *const vlm(lmap.get_column(x, y));
			assert(vlm != nullptr); // not supported in this flow
			colorRGB color;

			for (unsigned z = 0; z < zsize; ++z) {
				unsigned const off2(4*(off + z));
				lmcell const &lmc(vlm[z]);
				
				if (local_only) { // optimization
					if (lmc.lc[0] == 0.0 && lmc.lc[1] == 0.0 && lmc.lc[2] == 0.0) { // special case for all zeros
						tex_data[off2+0] = tex_data[off2+1] = tex_data[off2+2] = 0;
						continue;
					}
					lmc.get_final_color_local(color);
				}
				else {
					lmc.get_final_color(color, 1.0, 1.0);
				}
				UNROLL_3X(color[i_] = CLIP_TO_01(color[i_]);) // map to [0,1] range before calling pow()/sqrt()
				if      (apply_sqrt) {UNROLL_3X(color[i_] = sqrt(color[i_]););}
				else if (apply_exp)  {UNROLL_3X(color[i_] = pow(color[i_], lighting_exponent););}
				UNROLL_3X(tex_data[off2+i_] = (unsigned char)(255*color[i_]);)
			} // for z
		} // for x
	} // for y
}
void indir_light_tex_from_lmap(unsigned &tid, lmap_manager_t const &lmap, vector<unsigned char> &tex_data,
	unsigned xsize, unsigned ysize, unsigned zsize, float lighting_exponent, bool local_only)
{
	tex_data.resize(4*xsize*ysize*zsize, 0);
	assert(!tex_data.empty()); // size must be nonzero
	update_indir_light_tex_range(lmap, tex_data, xsize, 0, ysize, zsize, lighting_exponent, local_only, 1); // mt=1
	if (tid == 0) {tid = create_3d_texture(zsize, xsize, ysize, 4, tex_data, GL_LINEAR, GL_CLAMP_TO_EDGE);} // see update_smoke_indir_tex_range
	else {update_3d_texture(tid, 0, 0, 0, zsize, xsize, ysize, 4, tex_data.data());} // stored {Z,X,Y}
}


// *** Dynamic Lights Code ***


void setup_2d_texture(unsigned &tid) {
	setup_texture(tid, 0, 0, 0, 0, 0, 1);
}


// Note: This technique is commonly referred to as Clustered Shading
// texture units used:
// 0: reserved for object textures
// 1: reserved for indirect sky lighting and smoke (if enabled)
// 2: dynamic light data
// 3: dynamic light element array
// 4: dynamic light grid bag
// 5: voxel flow (not yet enabled) / reserved for bump maps
// 6: reserved for shadow map sun
// 7: reserved for shadow map moon
// 8: reserved for specular maps
// 11: reserved for detail normal map
// 15: dlight bounding cubes (optionally enabled)
void upload_dlights_textures(cube_t const &bounds, float &dlight_add_thresh) { // 0.21ms => 0.05ms with dlights_enabled

	if (disable_dlights) return;
	static bool last_dlights_empty(0);
	bool const cur_dlights_empty(dl_sources.empty());
	if (cur_dlights_empty && last_dlights_empty && dl_tid != 0 && elem_tid != 0 && gb_tid != 0) return; // no updates
	last_dlights_empty = cur_dlights_empty;
	//highres_timer_t timer("Dlight Texture Upload"); // 0.083ms

	// step 1: the light sources themselves
	unsigned const max_dlights           = 1024;
	unsigned const base_floats_per_light = 12; // XYZ pos, radius, RGBA color, XYZ dir/pos2, beamwidth
	unsigned const max_floats_per_light  = base_floats_per_light + 1; // add one for shadow map index
	//unsigned const max_floats_per_light      = base_floats_per_light + dl_smap_enabled;
	unsigned const ysz((max_floats_per_light+3)/4), stride(4*ysz); // round up to nearest multiple of 4
	static vector<float> dl_data;
	dl_data.resize(max_dlights*stride, 0.0); // 16k floats / 64KB data
	float *dl_data_ptr(dl_data.data());
	if (dl_sources.size() > max_dlights) {cerr << "Warning: Exceeded max lights of " << max_dlights << endl;}
	unsigned const ndl(min(max_dlights, (unsigned)dl_sources.size()));
	float const radius_scale(1.0/(0.5*bounds.dx())); // bounds x radius inverted
	vector3d const poff(bounds.get_llc()), psize(bounds.get_urc() - poff);
	vector3d const pscale(1.0/psize.x, 1.0/psize.y, 1.0/psize.z);
	has_spotlights = has_line_lights = 0;

	for (unsigned i = 0; i < ndl; ++i) {
		bool const line_light(dl_sources[i].is_line_light());
		float *data(dl_data_ptr + i*stride); // stride is texel RGBA
		dl_sources[i].pack_to_floatv(data); // {center,radius, color, dir,beamwidth, [smap_index]}
		UNROLL_3X(data[i_] = (data[i_] - poff[i_])*pscale[i_];) // scale pos to [0,1] range
		UNROLL_3X(data[i_+4] *= 0.1;) // scale color down
		if (line_light) {UNROLL_3X(data[i_+8] = (data[i_+8] - poff[i_])*pscale[i_];)} // scale to [0,1] range
		data[3] *= radius_scale; // radius field
		has_spotlights  |= dl_sources[i].is_directional();
		has_line_lights |= line_light;
	} // for i
	if (dl_tid == 0) {
		setup_2d_texture(dl_tid);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, ysz, max_dlights, 0, GL_RGBA, GL_FLOAT, dl_data_ptr);
	}
	else {
		bind_2d_texture(dl_tid);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ysz, ndl, GL_RGBA, GL_FLOAT, dl_data_ptr);
	}
	std::fill(dl_data.begin(), dl_data.begin()+ndl*stride, 0.0); // zero fill the data

	// step 1b: optionally setup dlights bcubes texture
	if (enable_dlight_bcubes) {
		static vector<float> dl_bc_data;
		dl_bc_data.resize(6*max_dlights, 0.0); // we need 2 RGB values to store 6 bcube floats; 6k floats / 24KB data
		float *bc_data_ptr(dl_bc_data.data());

		for (unsigned i = 0; i < ndl; ++i) {
			cube_t const bcube(dl_sources[i].calc_bcube(1));
			float *data(bc_data_ptr + 6*i); // stride is texel RGB, encoded as {x1, y1, z1, x2, y2, z2}

			for (unsigned dir = 0; dir < 2; ++dir) {
				for (unsigned dim = 0; dim < 3; ++dim) {*(data++) = (bcube.d[dim][dir] - poff[dim])*pscale[dim];} // scale to [0,1] range
			}
		}
		if (dl_bc_tid == 0) {
			setup_2d_texture(dl_bc_tid);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, 2, max_dlights, 0, GL_RGB, GL_FLOAT, bc_data_ptr);
		}
		else {
			bind_2d_texture(dl_bc_tid);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, ndl, GL_RGB, GL_FLOAT, bc_data_ptr);
		}
		std::fill(dl_bc_data.begin(), dl_bc_data.begin()+ndl*6, 0.0); // zero fill the data
	}

	// step 2: grid bag entries
	static unsigned num_warnings(0);
	static vector<unsigned> gb_data;
	static vector<unsigned short> elem_data;
	unsigned const elem_tex_x = (1<<8); // must agree with value in shader
	unsigned const elem_tex_y = (1<<10); // larger = slower, but more lights/higher quality
	unsigned const max_gb_entries(elem_tex_x*elem_tex_y), gbx(get_grid_xsize()), gby(get_grid_ysize());
	assert(max_gb_entries <= (1<<24)); // gb_data low bits allocation
	elem_data.clear();
	gb_data.resize(gbx*gby, 0);

	for (unsigned y = 0; y < gby; ++y) {
		for (unsigned x = 0; x < gbx; ++x) {
			unsigned const gb_ix(x + y*gbx); // {start, end, unused}
			gb_data[gb_ix] = elem_data.size(); // 24 low bits = start_ix
			if (!ldynamic_enabled[gb_ix]) continue; // no lights for this grid
			dls_cell const &dlsc(ldynamic[gb_ix]);
			unsigned num_ixs(dlsc.size());
			if (num_ixs == 0) continue; // no lights for this grid
			unsigned short const *const ixs(dlsc.get_src_ixs());
			assert(num_ixs < 256);
			num_ixs = min(num_ixs, unsigned(max_gb_entries - elem_data.size())); // enforce max_gb_entries limit
			
			for (unsigned i = 0; i < num_ixs; ++i) {
				if (ixs[i] < ndl) {elem_data.push_back((unsigned short)ixs[i]);} // if dlight index is too high, skip
			}
			unsigned const num_ix(elem_data.size() - gb_data[gb_ix]);
			assert(num_ix < (1<<8));
			gb_data[gb_ix] += (num_ix << 24); // 8 high bits = num_ix
			if (elem_data.size() >= max_gb_entries) break;
		} // for x
		if (elem_data.size() >= max_gb_entries) break;
	} // for y
	if (elem_data.size() > 0.9*max_gb_entries) {
		if (elem_data.size() >= max_gb_entries && num_warnings < 100) {
			std::cerr << "Warning: Exceeded max # indexes (" << max_gb_entries << ") in dynamic light texture upload" << endl;
			++num_warnings;
		}
		dlight_add_thresh = min(0.25f, (dlight_add_thresh + 0.005f)); // increase thresh to clip the dynamic lights to a smaller radius
	}
	if (elem_tid == 0) {
		setup_2d_texture(elem_tid);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, elem_tex_x, elem_tex_y, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
	}
	bind_2d_texture(elem_tid);
	unsigned const height(min(elem_tex_y, unsigned(elem_data.size()/elem_tex_x+1U))); // approximate ceiling
	elem_data.reserve(elem_tex_x*height); // ensure it's large enough for the padded upload
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, elem_tex_x, height, GL_RED_INTEGER, GL_UNSIGNED_SHORT, &elem_data.front());

	// step 3: grid bag(s)
	if (gb_tid == 0) {
		setup_2d_texture(gb_tid);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, gbx, gby, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &gb_data.front()); // Nx x Ny
	}
	else {
		bind_2d_texture(gb_tid);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gbx, gby, GL_RED_INTEGER, GL_UNSIGNED_INT, &gb_data.front());
	}
	check_gl_error(440);
	//cout << "ndl: " << ndl << ", elix: " << elem_data.size() << ", gb_sz: " << gb_data.size() << endl;
}


void setup_and_bind_shadow_matrix_ubo();

void setup_dlight_shadow_maps(shader_t &s) {
	bool arr_tex_set(0); // required to only bind texture arrays once
	setup_and_bind_shadow_matrix_ubo();
	for (auto i = dl_sources.begin(); i != dl_sources.end(); ++i) {i->setup_and_bind_smap_texture(s, arr_tex_set);}
	ubo_wrap_t::post_render(); // unbind the UBO
}


void setup_dlight_textures(shader_t &s, bool enable_dlights_smap) {

	if (disable_dlights) return;
	assert(dl_tid > 0 && elem_tid > 0 && gb_tid > 0 );
	set_one_texture(s, dl_tid,   2, "dlight_tex");
	set_one_texture(s, elem_tid, 3, "dlelm_tex");
	set_one_texture(s, gb_tid,   4, "dlgb_tex");
	if (enable_dlight_bcubes) {set_one_texture(s, dl_bc_tid, 15, "dlbcube_tex");} // TU_ID 15 is shared with ripples texture, hopefully we won't have a situation where we need both
	if (enable_dlights_smap && shadow_map_enabled()) {setup_dlight_shadow_maps(s);}
	s.add_uniform_float("LT_DIR_FALLOFF", LT_DIR_FALLOFF);
}


colorRGBA gen_fire_color(float &cval, float &inten, float rate) {
	inten = max(0.6f, min(1.0f, (inten + 0.04f*rate*fticks*signed_rand_float())));
	cval  = max(0.0f, min(1.0f, (cval  + 0.02f*rate*fticks*signed_rand_float())));
	colorRGBA color(1.0, 0.9, 0.7);
	blend_color(color, color, colorRGBA(1.0, 0.6, 0.2), cval, 0);
	return color;
}

point get_camera_light_pos() {
	return (get_camera_pos() + 0.1*CAMERA_RADIUS*cview_dir); // slightly in front of the camera to avoid zero length light_dir vector in dynamic lighting
}

void add_camera_candlelight() {
	static float cval(0.5), inten(0.75);
	add_dynamic_light(1.5*inten, get_camera_light_pos(), gen_fire_color(cval, inten));
}

void add_camera_flashlight() {

	//add_dynamic_light(FLASHLIGHT_RAD, get_camera_light_pos(), get_flashlight_color(), cview_dir, FLASHLIGHT_BW);
	flashlight_on = 1;

	if (world_mode == WMODE_GROUND && (display_mode & 0x0100)) { // add one bounce of indirect lighting
		unsigned const NUM_VPLS = 32;
		float const theta(acosf(1.0f - FLASHLIGHT_BW /*- 0.5*LT_DIR_FALLOFF*/)); // flashlight beam angle
		float const rad_per_len(0.95*tan(theta));
		vector3d vab[2];
		get_ortho_vectors(cview_dir, vab);
		point const lpos(get_camera_light_pos());

		for (unsigned i = 0; i < NUM_VPLS; ++i) {
			float const a(TWO_PI*i/NUM_VPLS);
			vector3d const delta((sin(a)*vab[0] + cos(a)*vab[1]).get_norm()); // already normalized?
			vector3d const dir(cview_dir + rad_per_len*delta);
			int cindex;
			point cpos;
			vector3d cnorm;
			
			if (check_coll_line_exact(lpos, (lpos + 0.5*FLASHLIGHT_RAD*dir), cpos, cnorm, cindex, 0.0, camera_coll_id, 1, 0, 0)) {
				cpos -= 0.0001*FLASHLIGHT_RAD*cnorm; // move behind the collision plane so as not to multiply light
				assert(cindex >= 0);
				colorRGBA const color(get_flashlight_color().modulate_with(coll_objects[cindex].get_avg_color()));
				add_dynamic_light(0.12*FLASHLIGHT_RAD, cpos, color*0.15, cnorm, 0.4); // wide angle (almost hemisphere)
			}
		} // for i
	}
}

light_source get_player_flashlight_light_source(float radius_scale) {
	point camera(get_camera_pos());
	if (world_mode == WMODE_INF_TERRAIN) {camera -= get_tiled_terrain_model_xlate();}
	float const bw((world_mode == WMODE_INF_TERRAIN) ? 0.001 : FLASHLIGHT_BW); // hack to adjust flashlight for city/building larger falloff values
	return light_source(FLASHLIGHT_RAD*radius_scale, camera, camera, get_flashlight_color(), 1, cview_dir, bw);
}

void add_player_flashlight_light_source(float radius_scale) { // for buildings
	dl_sources.push_back(get_player_flashlight_light_source(radius_scale));
	dl_sources.back().disable_shadows(); // shadows not needed
}

void init_lights() {
	assert(light_sources_d.size() == FLASHLIGHT_LIGHT_ID); // must be empty at this point (first light is added here)
	bool const use_smap = 0; // not yet enabled
	light_sources_d.push_back(light_source_trig(get_player_flashlight_light_source(1.0), use_smap));
}

void sync_flashlight() {
	assert(FLASHLIGHT_LIGHT_ID < light_sources_d.size());
	light_sources_d[FLASHLIGHT_LIGHT_ID].set_dynamic_state(get_camera_light_pos(), cview_dir, get_flashlight_color(), flashlight_on);
}


void add_dynamic_light(float sz, point const &p, colorRGBA const &c, vector3d const &d, float bw, point *line_end_pos, bool is_static_pos) {

	if (!animate2 || c == BLACK) return;
	if (XY_MULT_SIZE >= 512*512) return; // mesh is too large for dynamic lighting
	float const sz_scale((world_mode == WMODE_UNIVERSE) ? 1.0 : sqrt(0.1*XY_SCENE_SIZE));
	dl_sources2.push_back(light_source(sz_scale*sz, p, (line_end_pos ? *line_end_pos : p), c, !is_static_pos, d, bw));
}


void add_line_light(point const &p1, point const &p2, colorRGBA const &color, float size, float intensity) {

	if (!animate2) return;
	point p[2] = {p1, p2};
	if (!do_line_clip_scene(p[0], p[1], zbottom, max(ztop, czmax))) return;
	float const radius(size*intensity), pt_offset((1.0 - SQRTOFTWOINV)*radius);

	if (dist_less_than(p1, p2, radius)) { // short segment, use a single point light
		add_dynamic_light(radius, p[0], color);
	}
	else { // add a real line light
		vector3d const dir((p[1] - p[0]).get_norm());
		p[0] += dir*pt_offset; // shrink line slightly for a better effect
		p[1] -= dir*pt_offset;
		add_dynamic_light(radius, p[0], color, dir, 1.0, &p[1]);
	}
}


bool dls_cell::check_add_light(unsigned ix) const {

	if (empty()) return 1;
	assert(ix < dl_sources.size());
	light_source const &ls(dl_sources[ix]);

	for (unsigned i = 0; i < sz; ++i) {
		unsigned const ix2(lsrc[i]);
		assert(ix2 < dl_sources.size());
		assert(ix2 != ix);
		if (ls.try_merge_into(dl_sources[ix2])) return 0;
	}
	return 1;
}

void dls_cell::add_light_range(unsigned six, unsigned eix, unsigned char &enabled_flag) {
	if (!enabled_flag) {sz = 0; enabled_flag = 1;} // clear if marked as disabled, then enable
	min_eq(eix, MAX_LSRC-sz+six);
	for (unsigned ix = six; ix < eix; ++ix) {lsrc[sz++] = ix;}
}


void clear_dynamic_lights() {

	//if (!animate2) return;
	if (dl_sources.empty()) return; // only clear if light pos/size has changed?
	for (auto i = ldynamic_enabled.begin(); i != ldynamic_enabled.end(); ++i) {*i = 0;} // 0.015ms
	dl_sources.clear();
}


void calc_spotlight_pdu(light_source const &ls, pos_dir_up &pdu) {

	if (ls.is_line_light() || !ls.is_very_directional()) return; // not a spotlight
	cylinder_3dw const cylin(ls.calc_bounding_cylin(0.0, 1)); // clip_to_scene_bcube=1
	vector3d const dir(cylin.p2 - cylin.p1);
	if (dir.x == 0.0 && dir.y == 0.0) return; // vertical
	float const len(dir.mag());
	pdu = pos_dir_up(cylin.p1, dir/len, plus_z, tan(cylin.r2/len), 0.0, ls.get_radius(), 1.0, 1);
}


void add_dynamic_lights_ground(float &dlight_add_thresh) {

	//highres_timer_t timer("Dynamic Light Add");
	sync_flashlight();
	if (!animate2) return;
	if (disable_dlights) {dl_sources.clear(); return;}
	assert(!ldynamic.empty());
	assert(ldynamic_enabled.size() == ldynamic.size());
	clear_dynamic_lights();
	dl_sources.swap(dl_sources2);
	dl_smap_enabled = 0;

	for (auto i = light_sources_d.begin(); i != light_sources_d.end(); ++i) {
		// Note: more efficient to do VFC here, but won't apply to get_indir_light() (or is_in_darkness())
		if (!i->is_enabled() || !i->is_visible()) continue;
		i->check_shadow_map();
		dl_sources.push_back(*i);
		dl_smap_enabled |= i->smap_enabled();
	}
#if 0
	for (unsigned i = 0; i < 10; ++i) { // add some random lights (omnidirectional)
		point const pos(gen_rand_scene_pos());
		dl_sources.push_back(light_source(0.94, pos, pos, BLUE, 1));
	}
#endif
	// Note: do we want to sort by y/x position to minimize cache misses?
	stable_sort(dl_sources.begin(), dl_sources.end(), std::greater<light_source>()); // sort by largest to smallest radius
	unsigned const ndl((unsigned)dl_sources.size()), gbx(get_grid_xsize()), gby(get_grid_ysize());
	has_dl_sources     = (ndl > 0);
	dlight_add_thresh *= 0.99f;
	bool first(1);
	float const sqrt_dlight_add_thresh(sqrt(dlight_add_thresh));
	point const dlight_shift(-0.5*DX_VAL, -0.5*DY_VAL, 0.0);
	float const grid_dx(DX_VAL*(1 << DL_GRID_BS)), grid_dy(DY_VAL*(1 << DL_GRID_BS));
	float const z1(min(czmin, zbottom)), z2(max(czmax, ztop));

	for (unsigned ix = 0; ix < ndl; ++ix) {
		light_source const &ls(dl_sources[ix]);
		if (!ls.is_user_placed() && !ls.is_visible()) continue; // view culling (user placed lights are culled above as light_sources_d)
		float const ls_radius(ls.get_radius());
		if ((min(ls.get_pos().z, ls.get_pos2().z) - ls_radius) > max(ztop, czmax)) continue; // above everything, rarely occurs
		point const &lpos(ls.get_pos()), &lpos2(ls.get_pos2());
		bool const line_light(ls.is_line_light());
		int const xcent(get_xpos(lpos.x) >> DL_GRID_BS), ycent(get_ypos(lpos.y) >> DL_GRID_BS);
		
		if (!line_light && xcent >= 0 && ycent >= 0 && xcent < (int)gbx && ycent < (int)gby) {
			unsigned const gb_ix(ycent*gbx + xcent);
			if (ldynamic_enabled[gb_ix] && !ldynamic[gb_ix].check_add_light(ix)) continue; // merged into existing light, skip
		}
		cube_t bcube;
		int bnds[3][2];
		ls.get_bounds(bcube, bnds, sqrt_dlight_add_thresh, 1, dlight_shift); // clip_to_scene_bcube=1
		if (first) {dlight_bcube = bcube;} else {dlight_bcube.union_with_cube(bcube);}
		first = 0;
		int const radius(((int(ls_radius*max(DX_VAL_INV, DY_VAL_INV)) + 1) >> DL_GRID_BS) + 1), rsq(radius*radius);
		float const line_rsq((ls_radius + HALF_DXY)*(ls_radius + HALF_DXY));
		if (DL_GRID_BS > 0) {for (unsigned d = 0; d < 4; ++d) {bnds[d>>1][d&1] >>= DL_GRID_BS;}}
		pos_dir_up pdu;
		calc_spotlight_pdu(ls, pdu);

		for (int y = bnds[1][0]; y <= bnds[1][1]; ++y) { // add lights to ldynamic
			int const cmp_val(rsq - (y-ycent)*(y-ycent)), offset(y*gbx);

			for (int x = bnds[0][0]; x <= bnds[0][1]; ++x) {
				if (line_light) {
					float const px(get_xval(x << DL_GRID_BS)), py(get_yval(y << DL_GRID_BS)), lx(lpos2.x - lpos.x), ly(lpos2.y - lpos.y);
					float const cp_mag(lx*(lpos.y - py) - ly*(lpos.x - px));
					if (cp_mag*cp_mag > line_rsq*(lx*lx + ly*ly)) continue;
				} else if ((x-xcent)*(x-xcent) > cmp_val) continue; // skip

				if (pdu.valid) {
					float const px(get_xval(x << DL_GRID_BS)), py(get_yval(y << DL_GRID_BS));
					if (!pdu.cube_visible_for_light_cone(cube_t(px-grid_dx, px+grid_dx, py-grid_dy, py+grid_dy, z1, z2))) continue; // tile not in spotlight cylinder
				}
				//if (DL_GRID_BS == 0 && bcube.z1() > v_collision_matrix[y << DL_GRID_BS][x << DL_GRID_BS].zmax) continue; // should be legal, but doesn't seem to help
				ldynamic[offset + x].add_light(ix, ldynamic_enabled[offset + x]); // could do flow clipping here?
			} // for x
		} // for y
	} // for ix (light index)
}

void add_dynamic_lights_city(cube_t const &scene_bcube, float &dlight_add_thresh, float falloff) {

	if (disable_dlights) {dl_sources.clear(); return;}
	assert(DL_GRID_BS == 0); // not supported
	unsigned const ndl((unsigned)dl_sources.size()), gbx(MESH_X_SIZE), gby(MESH_Y_SIZE);
	has_dl_sources     = (ndl > 0);
	if (!has_dl_sources) return; // nothing else to do
	dlight_add_thresh *= 0.99;
	if (!scene_bcube.is_strictly_normalized()) {cerr << "Invalid scene_bcube: " << scene_bcube.str() << endl;}
	//highres_timer_t timer("Dynamic Light Add"); // 0.18ms start, 0.37ms in office building
	assert(scene_bcube.dx() > 0.0 && scene_bcube.dy() > 0.0);
	point const scene_llc(scene_bcube.get_llc()); // Note: zval ignored
	vector3d const scene_sz(scene_bcube.get_size()); // Note: zval ignored
	float const sqrt_dlight_add_thresh(sqrt(dlight_add_thresh));
	float const grid_dx(scene_sz.x/gbx), grid_dy(scene_sz.y/gby), grid_dx_inv(1.0/grid_dx), grid_dy_inv(1.0/grid_dy);

	for (unsigned ix = 0; ix < ndl;) { // Note: no increment
		light_source const &ls(dl_sources[ix]); // Note: should always be visible
		point const &lpos(ls.get_pos());
		unsigned const start_ix(ix);

		for (++ix; ix < ndl; ++ix) { // determine range of stacked lights with the same X/Y value
			light_source const &ls2(dl_sources[ix]);
			if (ls2.get_pos().x != lpos.x || ls2.get_pos().y != lpos.y || ls2.get_radius() != ls.get_radius() || ls2.get_dir() != ls.get_dir()) break;
		}
		int const xcent((lpos.x - scene_llc.x)*grid_dx_inv + 0.5f), ycent((lpos.y - scene_llc.y)*grid_dy_inv + 0.5f);
		cube_t bcube(ls.calc_bcube(0, sqrt_dlight_add_thresh, 0, falloff)); // padded below

		if (ls.is_very_directional() && (ls.get_dir().x != 0.0 || ls.get_dir().y != 0.0)) {
			bcube.expand_by(vector3d(grid_dx, grid_dy, 0.0)); // add one grid unit for spotlights not pointed up/down
		}
		int bnds[2][2] = {};

		for (unsigned e = 0; e < 2; ++e) {
			bnds[0][e] = max(0, min((int)gbx-1, int((bcube.d[0][e] - scene_llc.x)*grid_dx_inv)));
			bnds[1][e] = max(0, min((int)gby-1, int((bcube.d[1][e] - scene_llc.y)*grid_dy_inv)));
		}
		int const radius(max(1, round_fp(ls.get_radius()*max(grid_dx_inv, grid_dy_inv))) + 2), rsq(radius*radius);

		if (ix - start_ix == 1) { // single light case
			for (int y = bnds[1][0]; y <= bnds[1][1]; ++y) { // add lights to ldynamic
				int const cmp_val(rsq - (y-ycent)*(y-ycent)), offset(y*gbx);

				for (int x = bnds[0][0]; x <= bnds[0][1]; ++x) {
					if ((x-xcent)*(x-xcent) <= cmp_val) {ldynamic[offset + x].add_light(start_ix, ldynamic_enabled[offset + x]);}
				}
			} // for y
		}
		else { // stacked lights case (buildings)
			for (int y = bnds[1][0]; y <= bnds[1][1]; ++y) { // add lights to ldynamic
				int const cmp_val(rsq - (y-ycent)*(y-ycent)), offset(y*gbx);

				for (int x = bnds[0][0]; x <= bnds[0][1]; ++x) {
					if ((x-xcent)*(x-xcent) <= cmp_val) {ldynamic[offset + x].add_light_range(start_ix, ix, ldynamic_enabled[offset + x]);}
				}
			} // for y
		}
	} // for ix (light index)
}


bool is_visible_to_any_dir_light(point const &pos, float radius, int cobj, int skip_dynamic) {
	for (unsigned l = 0; l < NUM_LIGHT_SRC; ++l) {
		if (is_visible_to_light_cobj(pos, l, radius, cobj, skip_dynamic)) return 1;
	}
	return 0;
}

bool is_in_darkness(point const &pos, float radius, int cobj) { // used for AI

	colorRGBA c(WHITE);
	get_indir_light(c, pos); // this is faster so do it first
	if ((c.R + c.G + c.B) > DARKNESS_THRESH) return 0;
	return !is_visible_to_any_dir_light(pos, radius, cobj, 1); // skip_dynamic=1
}

void get_indir_light(colorRGBA &a, point const &p) { // used for particle clouds and is_in_darkness() test

	if (!lm_alloc) return;
	assert(lmap_manager.is_allocated());
	colorRGB cscale(cur_ambient);
	int const x(get_xpos_round_down(p.x)), y(get_ypos_round_down(p.y)), z(get_zpos(p.z));
	
	if (!point_outside_mesh(x, y) && p.z > czmin0) { // inside the mesh range and above the lowest cobj
		float val(get_voxel_terrain_ao_lighting_val(p));
		
		if (using_lightmap && p.z < czmax && lmap_manager.get_column(x, y) != NULL) { // not above all collision objects and not empty cell
			lmap_manager.get_lmcell(x, y, z).get_final_color(cscale, 0.5, val);
		}
		else if (val < 1.0) {
			cscale *= val;
		}
		if (!dl_sources.empty() && dlight_bcube.contains_pt(p)) {
			unsigned const gb_ix(get_ldynamic_ix(x, y));

			if (ldynamic_enabled[gb_ix]) {
				dls_cell const &ldv(ldynamic[gb_ix]);

				for (unsigned l = 0; l < (unsigned)ldv.size(); ++l) {
					unsigned const ls_ix(ldv.get(l));
					assert(ls_ix < dl_sources.size());
					light_source const &lsrc(dl_sources[ls_ix]);
					point lpos;
					float color_scale(lsrc.get_intensity_at(p, lpos));
					if (color_scale < CTHRESH) continue;
					if (lsrc.is_directional()) {color_scale *= lsrc.get_dir_intensity(lpos - p);}
					cscale += lsrc.get_color()*color_scale;
				} // for l
			}
		}
	}
	UNROLL_3X(a[i_] *= min(1.0f, cscale[i_]);)
}

bool is_any_dlight_visible(point const &p) {
	
	int const x(get_xpos_round_down(p.x)), y(get_ypos_round_down(p.y));
	if (point_outside_mesh(x, y)) return 0; // outside the mesh range
	if (dl_sources.empty() || !dlight_bcube.contains_pt(p)) return 0;
	unsigned const gb_ix(get_ldynamic_ix(x, y));
	if (!ldynamic_enabled[gb_ix]) return 0;
	dls_cell const &ldv(ldynamic[gb_ix]);

	for (unsigned l = 0; l < (unsigned)ldv.size(); ++l) {
		unsigned const ls_ix(ldv.get(l));
		assert(ls_ix < dl_sources.size());
		light_source const &lsrc(dl_sources[ls_ix]);
		point lpos;
		float const color_scale(lsrc.get_intensity_at(p, lpos));
		if (color_scale < CTHRESH) continue;
		if (lsrc.is_directional() && color_scale*lsrc.get_dir_intensity(lpos - p) < CTHRESH) continue;
		int index(-1); // unused
		
		if (lsrc.smap_enabled()) {
			lpos += (p - lpos).get_norm()*(1.01*lsrc.get_near_clip());
			if (!coll_pt_vis_test(p, lpos, 0.0, index, -1, 0, 3)) continue; // no cobj, skip_dynamic=0, use shadow alpha
		}
		return 1; // found
	} // for l
	return 0;
}

