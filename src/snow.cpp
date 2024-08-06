// 3D World - Snow Accumulation and Renderign code
// by Frank Gennari
// 5/5/10
#include "3DWorld.h"
#include "mesh.h"
#include "gl_ext_arb.h"
#include "shaders.h"
#include "model3d.h"


unsigned const VOXELS_PER_DIV = 8; // 1024 for 128 vertex mesh
unsigned const MAX_STRIP_LEN  = 200; // larger = faster, less overhead; smaller = smaller edge strips, better culling
int      const Z_CHECK_RANGE  = 1; // larger = smoother and fewer strips, but longer preprocessing
bool const ENABLE_SNOW_DLIGHTS= 1; // looks nice, but slow


bool has_snow(0);
point vox_delta;
map<int, unsigned> x_strip_map;

extern int display_mode, camera_mode, read_snow_file, write_snow_file;
extern unsigned num_snowflakes;
extern float ztop, zbottom, temperature, snow_depth, snow_random;
extern vector3d wind;
extern char *snow_file;
extern model3ds all_models;


typedef short coord_type;
typedef unsigned short count_type;
unsigned const MAX_COUNT((1 << (sizeof(count_type) << 3)) - 1);

void increment_printed_number(unsigned num);

void setup_detail_normal_map(shader_t &s, float tscale);
void setup_detail_normal_map_prefix(shader_t &s, bool enable);


// **************** LOW-LEVEL CONTAINER CLASSES ****************

struct zval_avg {
	count_type c;
	float z;
	zval_avg(count_type c_=0, float z_=0.0) : c(c_), z(z_) {}
	// Note: With a 1024x1024 voxel grid we should get an average of 1 count per 1M snow
	//       so we can have at max 64M snowflakes.
	//       However, we can get snow to stack up at a vertical edge so we need to clamp the count
	void update(float zval) {if (c < MAX_COUNT) {++c; z += zval;}}
	bool valid() const {return (c > 0);}
	float getz() const {return z/c;}
};


struct voxel_t {
	coord_type p[3] = {0};

	voxel_t(void) {}
	voxel_t(coord_type x, coord_type y, coord_type z) {p[0] = x; p[1] = y; p[2] = z;}
	
	voxel_t(point const &pt) {
		p[0] = int(vox_delta.x*(pt.x + X_SCENE_SIZE) + 0.5);
		p[1] = int(vox_delta.y*(pt.y + Y_SCENE_SIZE) + 0.5);
		p[2] = int(vox_delta.z*(pt.z - czmin));
	}
	point get_pt() const {
		return point((-X_SCENE_SIZE + p[0]/vox_delta.x), (-Y_SCENE_SIZE + p[1]/vox_delta.y), (czmin + p[2]/vox_delta.z));
	}
	bool operator<(voxel_t const &v) const { // sorted {x, y, z}
		if (p[0] < v.p[0]) return 1;
		if (p[0] > v.p[0]) return 0;
		if (p[1] < v.p[1]) return 1;
		if (p[1] > v.p[1]) return 0;
		return (p[2] < v.p[2]);
	}
	bool operator==(voxel_t const &v) const {return (p[0] == v.p[0] && p[1] == v.p[1] && p[2] == v.p[2]);}
};


struct voxel_z_pair {
	voxel_t v;
	zval_avg z;
	voxel_z_pair() {}
	voxel_z_pair(voxel_t const &v_, zval_avg const &z_=zval_avg()) : v(v_), z(z_) {}
};


struct strip_entry {
	point p;
	vector3d n;
	strip_entry() {}
	strip_entry(point const &p_) : p(p_) {}
};

class strip_vect_t : public vector<strip_entry> {
	float zval;
	bool z_valid;
public:
	strip_vect_t() : zval(0.0), z_valid(0) {}

	void add(voxel_z_pair const &vz, float d) {
		point p(vz.v.get_pt());
		
		if (vz.z.valid()) {
			zval  = vz.z.getz();
			p.z   = zval + vz.z.c*d; // zval is average zval + snow depth
			zval -= 0.1/vox_delta.z; // shift to move sligltly under the object (optional)

			if (!z_valid) {
				for (unsigned i = 0; i < size(); ++i) {
					operator[](i).p.z = zval;
				}
				z_valid = 1;
			}
		}
		else if (z_valid) {
			p.z = zval;
		}
		push_back(strip_entry(p));
	}
	void reset() {
		resize(0);
		z_valid = 0;
	}
};


// **************** BLOCK ALLOCATION ****************

unsigned const BLOCK_SZ = 65536; // 1.5MB blocks

class strip_block_alloc {

	vector<strip_entry *> blocks;
	unsigned pos;

public:
	strip_block_alloc() : pos(0) {}
	unsigned get_pos() const {return pos;}

	~strip_block_alloc() {
		for (vector<strip_entry *>::iterator i = blocks.begin(); i != blocks.end(); ++i) {
			delete [] *i;
		}
	}

	strip_entry *alloc(unsigned sz) {
		assert(sz <= 2*BLOCK_SZ); // could use larger block size or singles vector

		if (blocks.empty() || (pos + sz) > BLOCK_SZ) {
			blocks.push_back(new strip_entry[BLOCK_SZ]);
			pos = 0;
		}
		strip_entry *const cur(blocks.back() + pos);
		pos += sz;
		return cur;
	}

	void status() const {
		cout << "blocks: " << blocks.size() << ", pos: " << pos
			 << ", mem: "  << blocks.size()*BLOCK_SZ*sizeof(strip_entry) << endl;
	}

	unsigned get_cur_block_id() const {
		assert(!blocks.empty());
		return ((unsigned)blocks.size() - 1);
	}
};

strip_block_alloc sb_alloc;


// **************** CORE CLASSES ****************

class snow_renderer; // forward declaration

class strip_t {

	strip_entry *strips; // block allocated
	unsigned size, block_id, block_pos;

public:
	bool is_edge;
	int xval, y_start;

	strip_t(bool edge=0) : strips(NULL), size(0), block_id(0), block_pos(0), is_edge(edge), xval(0), y_start(0) {}
	unsigned get_size() const {return size;}
	strip_entry const *get_strips() const {return strips;}

	point get_pos(unsigned pos) const {
		assert(strips && pos < size);
		return strips[pos].p;
	}
	point get_norm(unsigned pos) const {
		assert(strips && pos < size);
		return strips[pos].n;
	}

	void finalize(strip_vect_t const &strip_vect, int xval_, int y_start_) { // can only call this once
		xval    = xval_;
		y_start = y_start_;

		// allocate data
		assert(size == 0);
		size      = (unsigned)strip_vect.size();
		strips    = sb_alloc.alloc(size);
		block_id  = sb_alloc.get_cur_block_id();
		block_pos = sb_alloc.get_pos();
		assert(strips);
		assert(size <= block_pos);
		block_pos -= size; // get starting position
		for (unsigned i = 0; i < size; ++i) {strips[i] = strip_vect[i];}

		// calculate normals
		assert(size >= 4);
		vector<vector3d> nt(size-2); // triangle face normals

		for (unsigned i = 0; i < nt.size(); ++i) { // calculate triangle normals
			nt[i] = cross_product((strips[i].p - strips[i+1].p), (strips[i+2].p - strips[i+1].p)).get_norm();
			if (nt[i].z < 0.0) {nt[i].negate();} // normal is always +z
		}
		for (unsigned i = 0; i < size; ++i) { // average triangle normals to get vertex normals
			unsigned const k1((i > 2) ? i-2 : 0), k2(min((unsigned)nt.size()-1, i));
			assert(k2 >= k1);
			vector3d &n(strips[i].n);
			n = zero_vector;
			for (unsigned k = k1; k <= k2; ++k) {n += nt[k];}
			n.normalize(); // average triangle normals
		}
	}
};


class voxel_map : public map<voxel_t, zval_avg> { // must be a sorted map
public:
	zval_avg find_adj_z(voxel_t &v, zval_avg const &zv_old, float depth, voxel_map *cur_x_map=NULL);
	bool read(char const *const fn);
	bool write(char const *const fn) const;
};


struct data_block { // packed voxel_z_pair for read/write
	coord_type p[3] = {};
	count_type c = 0;
	float z = 0.0;

	void add_to_map(voxel_map &vmap) const {
		voxel_t v;
		for (unsigned i = 0; i < 3; ++i) v.p[i] = p[i];
		vmap[v] = zval_avg(c, z);
	}
	void set_from_map_iter(voxel_map::const_iterator it) {
		for (unsigned i = 0; i < 3; ++i) p[i] = it->first.p[i];
		c = it->second.c;
		z = it->second.z;
	}
};


// this tends to take a large fraction of the preprocessing time
zval_avg voxel_map::find_adj_z(voxel_t &v, zval_avg const &zv_old, float depth, voxel_map *cur_x_map) {

	coord_type best_dz(0);
	zval_avg res;
	voxel_t v2_s(v), v2_e(v);
	v2_s.p[2] -= min(Z_CHECK_RANGE, (int)v2_s.p[2]);
	v2_e.p[2] += Z_CHECK_RANGE+1; // one past the end

	for (iterator it = lower_bound(v2_s); it != end() && it->first < v2_e;) { // Note: no increment of it
		zval_avg const z2(it->second);
		assert(z2.valid());
		if (zv_old.valid() && fabs(z2.getz() - zv_old.getz()) > depth) {++it; continue;} // delta z too large
		voxel_t const v2(it->first);

		if (cur_x_map) {
			erase(it++);
			(*cur_x_map)[v2] = z2;
		} else {++it;}
		coord_type const dz(v2.p[2] - v.p[2]);
		if (!res.valid() || abs(dz) < abs(best_dz)) {best_dz = dz;}
		res.c += z2.c;
		res.z += z2.z;
	}
	if (res.valid()) {v.p[2] += best_dz;}
	return res;
}


bool voxel_map::read(char const *const fn) {

	FILE *fp;
	assert(fn != NULL);
	if (!open_file(fp, fn, "snow map", "rb")) return 0;
	cout << "Reading snow file from " << fn << endl;
	size_t const n(fread(&vox_delta, sizeof(float), 3, fp));
	assert(n == 3);
	unsigned map_size(0);
	size_t const sz_read(fread(&map_size, sizeof(unsigned), 1, fp));
	assert(sz_read == 1);
	data_block data;
	
	for (unsigned i = 0; i < map_size; ++i) {
		size_t const nr(fread(&data, sizeof(data_block), 1, fp));
		assert(nr == 1);
		data.add_to_map(*this);
	}
	checked_fclose(fp);
	return 1;
}


bool voxel_map::write(char const *const fn) const {

	FILE *fp;
	assert(fn != NULL);
	if (!open_file(fp, fn, "snow map", "wb")) return 0;
	cout << "Writing snow file to " << fn << endl;
	size_t const n(fwrite(&vox_delta, sizeof(float), 3, fp));
	assert(n == 3);
	unsigned const map_size((unsigned)size()); // should be size_t?
	size_t const sz_write(fwrite(&map_size, sizeof(map_size), 1, fp));
	assert(sz_write == 1);
	data_block data;

	for (const_iterator i = begin(); i != end(); ++i) {
		data.set_from_map_iter(i);
		size_t const nw(fwrite(&data, sizeof(data_block), 1, fp));
		assert(nw == 1);
	}
	checked_fclose(fp);
	return 1;
}


// **************** VBO/ELEMENT INDEX CODE ****************


class snow_renderer {

	indexed_vbo_manager_t vbo_mgr;
	float last_x;
	unsigned nquads;
	vector<vert_norm> data;
	vector<unsigned> indices, strip_offsets;
	map<point, unsigned> vmap[2]; // {prev, next} rows

public:
	snow_renderer() : last_x(0.0), nquads(0) {}
	// can't free in the destructor because the gl context may be destroyed before this point
	//~snow_renderer() {free_vbos();}
	bool empty() const {return data.empty();}

	void add_all_strips(vector<strip_t> const &strips) {
		nquads = 0;

		for (vector<strip_t>::const_iterator i = strips.begin(); i != strips.end(); ++i) {
			nquads += (i->get_size() - 2)/2;
		}
		unsigned const num_ixs(2*nquads + 3*strips.size()); // 2 per quad, 2 per strip end, 1 per strip restart
		indices.reserve(num_ixs);
		data.reserve(5*nquads/4); // 20% extra
		strip_offsets.reserve(strips.size()+1);

		for (vector<strip_t>::const_iterator i = strips.begin(); i != strips.end(); ++i) {
			strip_offsets.push_back((unsigned)indices.size());
			add_strip(*i);
		}
		strip_offsets.push_back((unsigned)indices.size());
		assert(indices.size() == num_ixs);
	}

	void add_strip(strip_t const &s) {
		unsigned const size(s.get_size());
		assert(size >= 4 && !(size & 1)); // must be even
		strip_entry const *strips(s.get_strips());
		float const xval(strips[0].p.x);

		if (xval != last_x) {
			vmap[0].clear();
			vmap[0].swap(vmap[1]);
			last_x = xval;
		}
		for (unsigned i = 0; i < size; ++i) {add(strips[i+0].p, strips[i+0].n, (i&1));}
		indices.push_back(PRIMITIVE_RESTART_IX); // restart the strip
	}

private:
	unsigned add(point const &v, vector3d const &n, unsigned map_ix) { // can't be called after finalize()
		assert(vbo_mgr.vbo == 0 && vbo_mgr.ivbo == 0);
		auto it(vmap[map_ix].find(v));
		unsigned ix(0);

		if (it != vmap[map_ix].end()) { // existing vertex
			ix = it->second;
			assert(ix < data.size());
			data[ix].n = (data[ix].n + n)*0.5; // average the normals???
		}
		else {
			ix = (unsigned)data.size();
			vmap[map_ix][v] = ix;
			data.emplace_back(v, n);
		}
		indices.push_back(ix);
		return ix;
	}

public:
	void free_vbos() {vbo_mgr.clear_vbos();}

	void update_region(unsigned strip_ix, unsigned strip_pos, unsigned strip_len, float new_z) { // Note: could use ranges/blocks optimization
		
		if (!vbo_mgr.vbo) return; // vbo not allocated, so all will be updated when it gets allocated during drawing
		bind_vbo(vbo_mgr.vbo, 0);
		assert(strip_ix+1 < strip_offsets.size());
		assert(strip_len >= 4); // at least one quad
		unsigned const cur_six(strip_offsets[strip_ix]), next_six(strip_offsets[strip_ix+1]);
		unsigned const num_quads((strip_len-2)/2), quad_ix(min(strip_pos/2, num_quads-1));
		assert((next_six - cur_six) == (2*num_quads + 3)); // error check: 2 per quad, 2 at end, 1 for restart
		unsigned const start_index_ix(cur_six + 2*quad_ix); // quad vertex index

		for (unsigned i = 0; i < 4; ++i) { // 4 points on the quad
			unsigned index_ix(start_index_ix + i);
			assert(index_ix < indices.size() && index_ix < next_six);
			unsigned const data_ix(indices[index_ix]);
			assert(data_ix != PRIMITIVE_RESTART_IX);
			assert(data_ix < data.size());
			vector3d const norm(((i & 1) ? -1.0 : 1.0), ((i < 2) ? 1.0 : -1.0), 0.0);
			data[data_ix].n   = (data[data_ix].n + norm.get_norm())*0.5; // very approximate, but seems to be okay
			data[data_ix].v.z = new_z;
			upload_vbo_sub_data(&data[data_ix], data_ix*sizeof(vert_norm), sizeof(vert_norm), 0);
		}
		bind_vbo(0, 0);
	}

	void finalize() {
		assert(vbo_mgr.vbo == 0 && vbo_mgr.ivbo == 0);
		assert(!indices.empty());
		for (unsigned d = 0; d < 2; ++d) {vmap[d].clear();}
	}

	void draw() {
		assert(!indices.empty());
		vbo_mgr.create_and_upload(data, indices, 0, 1); // set_vbo_arrays() is called internally
		vbo_mgr.pre_render();
		vert_norm::set_vbo_arrays();
		glDrawRangeElements(GL_TRIANGLE_STRIP, 0, (unsigned)data.size(), (unsigned)indices.size(), GL_UNSIGNED_INT, 0);
		++num_frame_draw_calls;
		vbo_mgr.post_render();
	}

	void show_stats() const {
		cout << "snow verts: " << data.size() << ", indices: " << indices.size() << ", quads: " << nquads << endl;
		cout << "snow mem: " << (data.size()*sizeof(vert_norm) + indices.size()*sizeof(unsigned)) << endl;
	}
};

snow_renderer snow_draw;


// **************** TOP LEVEL FUNCTIONS ****************

vector<strip_t> snow_strips;


bool get_mesh_ice_pt(point const &p1, point &p2) {

	int const xpos(get_xpos(p1.x)), ypos(get_ypos(p1.y));
	if (point_outside_mesh(xpos, ypos)) return 0; // shouldn't get here (much)
	float const mh(interpolate_mesh_zval(p1.x, p1.y, 0.0, 0, 1));
	float const h(max(mh, water_matrix[ypos][xpos])); // mesh or ice (water)
	p2.assign(p1.x, p1.y, h);
	return 1;
}


vector3d get_rand_snow_vect(rand_gen_t &rgen, float amount=1.0) {
	return (snow_random == 0.0 ? zero_vector : vector3d(amount*snow_random*rgen.rgauss(), amount*snow_random*rgen.rgauss(), 0.0));
}


bool check_snow_line_coll(point const &pos1, point const &pos2, point &cpos, vector3d &cnorm) {

	int cindex(-1);
	colorRGBA model_color; // unused
	bool const cobj_coll(check_coll_line_exact(pos1, pos2, cpos, cnorm, cindex, 0.0, -1, 0, 0, 1));
	bool const model_coll(all_models.check_coll_line(pos1, (cobj_coll ? cpos : pos2), cpos, cnorm, model_color, 1));
	return (cobj_coll || model_coll);
}


void create_snow_map(voxel_map &vmap) {

	// distribute snowflakes over the scene and build the voxel map of hits
	int const num_per_dim(1024*(unsigned)sqrt((float)num_snowflakes)); // in M, so sqrt div by 1024
	float const zval(max(ztop, czmax)), zv_scale(1.0f/(zval - zbottom));
	float const xscale(2.0*X_SCENE_SIZE/num_per_dim), yscale(2.0*Y_SCENE_SIZE/num_per_dim);
	vector3d wind_vector(0.25*(zval - zbottom)*wind);
	wind_vector.z = 0.0; // zval is unused/ignored
	all_models.build_cobj_trees(1);
	cout << "Snow accumulation progress (out of " << num_per_dim << "):     0";

#pragma omp parallel for schedule(dynamic,1)
	for (int y = 0; y < num_per_dim; ++y) {
		if (omp_get_thread_num_3dw() == 0) {increment_printed_number(y);} // progress for thread 0
		rand_gen_t rgen;
		rgen.set_state(123, y);

		for (int x = 0; x < num_per_dim; ++x) {
			point pos1(-X_SCENE_SIZE + x*xscale, -Y_SCENE_SIZE + y*yscale, zval), pos2;
			// add slightly more randomness for numerical precision reasons
			for (unsigned d = 0; d < 2; ++d) {pos1[d] += SMALL_NUMBER*rgen.signed_rand_float();}
			if (!get_mesh_ice_pt(pos1, pos2)) continue; // only pos1/pos2 zvals differ; skip if invalid point
			assert(pos2.z < pos1.z);
			pos1 += get_rand_snow_vect(rgen, 1.0); // add some gaussian randomness for better distribution
			pos1 -= wind_vector; // offset starting point by wind vector (upwind)
			point cpos;
			vector3d cnorm;
			bool invalid(0);
			unsigned iter(0);
			
			while (check_snow_line_coll(pos1, pos2, cpos, cnorm)) {
				if (cnorm.z > 0.0) { // collision with a surface that points up - we're done
					pos2 = cpos;
					break;
				}
				if (snow_random == 0.0 || iter > 100) { // something odd happened
					invalid = 1;
					break;
				}
				// collision with vertical or bottom surface
				float const val(CLIP_TO_01((pos1.z - zbottom)*zv_scale));
				vector3d const delta(get_rand_snow_vect(rgen, 0.1*val));
				pos1 = cpos - (pos2 - pos1).get_norm()*SMALL_NUMBER; // push a small amount back from the object
				pos2 = pos1 + ((dot_product(delta, cnorm) < 0.0) ? -delta : delta);
				
				if (!get_mesh_ice_pt(pos2, pos2)) { // invalid point
					invalid = 1;
					break;
				}
				++iter;
			} // end while
			if (!invalid) {
				voxel_t const voxel(pos2);
#pragma omp critical(snow_map_update)
				vmap[voxel].update(pos2.z);
			}
		} // for x
	} // for y
	cout << endl;
}


void add_strip(strip_vect_t const &strip, bool is_edge, int xval, int y_start) {

	assert(!strip.empty());
	snow_strips.push_back(strip_t(is_edge));
	snow_strips.back().finalize(strip, xval, y_start);
}


void create_snow_strips(voxel_map &vmap) {

	// create strips of snow for rendering
	voxel_map cur_x_map, last_x_map;
	unsigned const num_xy_voxels(VOXELS_PER_DIV*VOXELS_PER_DIV*XY_MULT_SIZE);
	float const delta_depth(snow_depth*num_xy_voxels/(1024.0f*1024.0f*num_snowflakes));
	unsigned n_strips(0), n_edge_strips(0), strip_len(0), edge_strip_len(0);
	int last_x(0);
	strip_vect_t strip, edge_strip;
	vector<voxel_z_pair> vs;
	snow_strips.clear();
	snow_strips.reserve(8*num_xy_voxels/MAX_STRIP_LEN); // should be more than enough

	while (!vmap.empty()) {
		pair<voxel_t, zval_avg> start(*vmap.begin());
		voxel_t v1(start.first);
		zval_avg zv(start.second);
		assert(zv.valid());

		if (v1.p[0] != last_x) { // we moved on to the next x-value, so update the x maps
			last_x = v1.p[0];
			last_x_map.clear();
			cur_x_map.swap(last_x_map);
			bool const did_ins(x_strip_map.insert(make_pair(last_x, (unsigned)snow_strips.size())).second);
			assert(did_ins); // map should guarantee strictly increasing x
		}
		vmap.erase(vmap.begin());
		cur_x_map[v1] = zv;
		vs.resize(0);
		--v1.p[1];
		vs.push_back(voxel_z_pair(v1)); // zero start
		++v1.p[1];
		vs.push_back(voxel_z_pair(v1, zv));
		
		while (1) { // generate a strip in y with constant x
			++v1.p[1];
			zv = vmap.find_adj_z(v1, zv, snow_depth, &cur_x_map);
			//if (!zv.valid()) --v1.p[1]; // move back one step
			vs.push_back(voxel_z_pair(v1, zv));
			if (!zv.valid()) break; // end of strip
		}
		unsigned const sz((unsigned)vs.size()), num_parts((sz - 1)/MAX_STRIP_LEN + 1); // ceiling

		for (unsigned n = 0; n < num_parts; ++n) {
			unsigned const start_pos(n*MAX_STRIP_LEN);
			unsigned end_pos(min(sz, start_pos+MAX_STRIP_LEN+1));
			unsigned num_ends(0), last_edge(0);
			int const y_start(vs[start_pos].v.p[1]);
			int edge_y_start(y_start);
			strip.reset();
			edge_strip.reset();

			if ((sz - end_pos) < MAX_STRIP_LEN/10) { // don't create tiny strips
				end_pos = sz;
				++n;
			}
			for (unsigned i = start_pos; i < end_pos; ++i) {
				bool const end_element(i == 0 || i+1 == sz);
				voxel_t v2(vs[i].v);
				++v2.p[0]; // move to next x row
				zval_avg z2(vmap.find_adj_z(v2, vs[i].z, snow_depth));
				if (end_element) z2.c = 0; // zero terminate start/end points
				strip.add(vs[i], delta_depth); // first edge
				strip.add(voxel_z_pair(v2, z2), delta_depth); // second edge

				// generate edge strips
				if ((end_pos - start_pos) <= 3) continue; // too small for edge srtips
				voxel_t v3(vs[i].v);
				--v3.p[0]; // move to prev x row
				zval_avg z3(last_x_map.find_adj_z(v3, vs[i].z, snow_depth));
				
				if (!end_element && !z3.valid()) {
					last_edge = (unsigned)edge_strip.size() + 2;
					++num_ends;
				}
				if (num_ends == 0) {
					edge_strip.reset();
					edge_y_start = v3.p[1];
				}
				if (end_element) z3.c = 0; // zero terminate start/end points
				edge_strip.add(vs[i], delta_depth); // first edge
				edge_strip.add(voxel_z_pair(v3, z3), delta_depth); // second edge
			} // for i
			if (num_ends > 0) {
				if (last_edge+2 < edge_strip.size()) edge_strip.resize(last_edge+2); // clip off extra points
				add_strip(edge_strip, 1, last_x, edge_y_start); // add if some edge elements
				edge_strip_len += (unsigned)edge_strip.size();
				++n_edge_strips;
			}
			add_strip(strip, 0, last_x, y_start);
			strip_len += (unsigned)strip.size();
			++n_strips;
		} // for n
	}
	cout << "strips: " << n_strips << " base, " << n_edge_strips << " edge" << ", total vector " << snow_strips.size() << endl;
	cout << "strip lengths: " << strip_len << " base, " << edge_strip_len << " edge" << endl;
	cout << "x_strip_map: " << x_strip_map.size() << endl;
	snow_draw.add_all_strips(snow_strips);
	snow_draw.finalize();
	snow_draw.show_stats();
}


bool snow_enabled() {
	return (temperature < W_FREEZE_POINT && !snow_draw.empty() && (display_mode & 0x02));
}


void gen_snow_coverage() {

	if (snow_depth <= 0.0 || num_snowflakes == 0) return; // disabled
	cout << "Determining Snow Coverage" << endl;
	voxel_map vmap;

	// setup voxel scales
	vox_delta.assign(VOXELS_PER_DIV/DX_VAL, VOXELS_PER_DIV/DY_VAL, 1.0/(max(DZ_VAL/VOXELS_PER_DIV, snow_depth)));
	
	if (read_snow_file) {
		RESET_TIME;
		if (!vmap.read(snow_file)) {has_snow = 0; return;}
		PRINT_TIME("Read Snow Voxel Map");
	}
	else {
		RESET_TIME;
		create_snow_map(vmap);
		PRINT_TIME("Build Snow Voxel Map");
	}
	if (write_snow_file) {
		RESET_TIME;
		vmap.write(snow_file);
		PRINT_TIME("Write Snow Voxel Map");
	}
	cout << "voxels: " << vmap.size() << endl;
	RESET_TIME;
	create_snow_strips(vmap);
	has_snow = snow_enabled();
	PRINT_TIME("Snow Strip Creation");
}


tile_blend_tex_data_t snow_tbt_data;

void reset_snow_vbos() {
	snow_draw.free_vbos();
	snow_tbt_data.clear_context();
}


void draw_snow(bool shadow_only) {

	has_snow = snow_enabled();
	if (!has_snow) return;
	//RESET_TIME;
	shader_t s;
	bool const use_smap(!shadow_only && shadow_map_enabled());
	bool const detail_normal_map(!shadow_only); //  && camera_mode == 1?
	bool const enable_dlights(!shadow_only && ENABLE_SNOW_DLIGHTS);
	s.setup_enabled_lights(2, 2); // FS
	//s.set_prefix("#define BLEND_DIST_DETAIL_NMAP", 1); // FS - this is optional when using tiling_and_blending
	if (detail_normal_map) {s.set_prefix("#define USE_TILE_BLEND_NMAP", 1);} // FS
	s.set_prefix("in vec3 eye_norm;", 1); // FS
	set_dlights_booleans(s, enable_dlights, 1); // FS
	s.check_for_fog_disabled();
	setup_detail_normal_map_prefix(s, detail_normal_map);
	s.set_prefixes(make_shader_bool_prefix("no_normalize", !use_smap), 3); // VS/FS
	s.set_prefix(make_shader_bool_prefix("use_shadow_map",  use_smap), 1); // FS
	s.set_vert_shader("texture_gen.part+snow");
	s.set_frag_shader("linear_fog.part+tiling_and_blending.part+ads_lighting.part*+shadow_map.part*+dynamic_lighting.part*+detail_normal_map.part+per_pixel_lighting_textured");
	s.begin_shader();
	s.setup_scene_bounds();
	s.setup_fog_scale();
	s.add_uniform_int("tex0", 0);
	if (enable_dlights) {setup_dlight_textures(s);}
	if (use_smap) {set_smap_shader_for_all_lights(s);}

	if (detail_normal_map) {
		s.add_uniform_vector2d("detail_normal_tex_scale", vector2d(0.047*X_SCENE_SIZE, 0.047*Y_SCENE_SIZE));
		snow_tbt_data.ensure_textures(ROCK_NORMAL_TEX);
		snow_tbt_data.bind_shader(s);
	}
	s.set_specular(0.5, 50.0);
	s.set_cur_color(SNOW_COLOR);
	select_texture(SNOW_TEX); // detail texture (or could use NOISE_TEX)
	setup_texgen(50.0, 50.0, 0.0, 0.0, 0.0, s, 0);
	glEnable(GL_PRIMITIVE_RESTART);
	glPrimitiveRestartIndex(PRIMITIVE_RESTART_IX);
	snow_draw.draw();
	glDisable(GL_PRIMITIVE_RESTART);
	s.clear_specular();
	s.end_shader();
	//PRINT_TIME("Snow Draw");
}


bool get_snow_height(point const &p, float radius, float &zval, vector3d &norm, bool crush_snow) {

	if (!has_snow || snow_strips.empty()) return 0;
	voxel_t const v(p);
	int const xval(v.p[0]), yval(v.p[1]);
	auto it(x_strip_map.find(xval));
	if (it == x_strip_map.end()) return 0; // x-value not found
	assert(it->second < snow_strips.size());
	assert(snow_strips[it->second].xval == xval);
	
	for (unsigned i = it->second; i < snow_strips.size(); ++i) {
		strip_t const &s(snow_strips[i]);
		if (s.xval > xval)    break; // went too far in x
		assert(s.xval == xval);
		if (s.is_edge)        continue; // ignore edge strips for now
		if (s.y_start > yval) break; // went too far in y
		unsigned const len(s.get_size() >> 1); // divide by 2 because we want length in y
		assert(len >= 2);
		int const y_end(s.y_start + len - 1);
		if (yval > y_end)     continue; // not at y value yet
		unsigned const pos((yval - s.y_start) << 1);
		float const z(s.get_pos(pos).z);
		
		// Note: can get here for more than one strip if there is strip overlap in {y,z}
		if ((p.z - radius) < z && (p.z + radius) > z) {
			zval = z;
			norm = s.get_norm(pos);
			if (crush_snow) {snow_draw.update_region(i, pos, s.get_size(), min(z, max((z - 0.25f*radius), (p.z - radius))));}
			return 1;
		}
	}
	return 0;
}

bool crush_snow_at_pt(point const &p, float radius) {

	float zval; // unused
	vector3d norm; // unused
	return get_snow_height(p, radius, zval, norm, 1);
}



