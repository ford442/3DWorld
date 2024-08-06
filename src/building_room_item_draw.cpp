// 3D World - Building Interior Room Item Drawing
// by Frank Gennari 4/17/21

#include "3DWorld.h"
#include "function_registry.h"
#include "buildings.h"
#include "city.h" // for object_model_loader_t
#include "subdiv.h" // for sd_sphere_d
#include "profiler.h"
#include "openal_wrap.h"


unsigned room_geom_mem(0);
quad_batch_draw candle_qbd;
vect_room_object_t pending_objs;
object_model_loader_t building_obj_model_loader;

extern bool camera_in_building;
extern int display_mode, frame_counter, animate2, player_in_basement;
extern unsigned room_mirror_ref_tid;
extern float fticks, office_chair_rot_rate, building_ambient_scale;
extern point actual_player_pos, player_candle_pos;
extern cube_t smap_light_clip_cube;
extern pos_dir_up camera_pdu;
extern building_t const *player_building;
extern carried_item_t player_held_object;
extern building_params_t global_building_params;

unsigned get_num_screenshot_tids();
tid_nm_pair_t get_phone_tex(room_object_t const &c);
template< typename T > void gen_quad_ixs(vector<T> &ixs, unsigned size, unsigned ix_offset);
void draw_emissive_billboards(quad_batch_draw &qbd, int tid);
void draw_car_in_pspace(car_t &car, shader_t &s, vector3d const &xlate, bool shadow_only);
void set_car_model_color(car_t &car);
bldg_obj_type_t get_taken_obj_type(room_object_t const &obj);
int get_toilet_paper_nm_id();
void setup_monitor_screen_draw(room_object_t const &monitor, rgeom_mat_t &mat, std::string &onscreen_text);
void add_tv_or_monitor_screen(room_object_t const &c, rgeom_mat_t &mat, std::string const &onscreen_text, rgeom_mat_t *text_mat);
bool check_clock_time();
bool have_fish_model();
void register_fishtank(room_object_t const &obj, bool is_visible);
void end_fish_draw(shader_t &s, bool inc_pools_and_fb);

bool has_key_3d_model() {return building_obj_model_loader.is_model_valid(OBJ_MODEL_KEY);}

colorRGBA room_object_t::get_model_color() const {return building_obj_model_loader.get_avg_color(get_model_id());}

// skip_faces: 1=Z1, 2=Z2, 4=Y1, 8=Y2, 16=X1, 32=X2 to match CSG cube flags
void rgeom_mat_t::add_cube_to_verts(cube_t const &c, colorRGBA const &color, point const &tex_origin,
	unsigned skip_faces, bool swap_tex_st, bool mirror_x, bool mirror_y, bool inverted, bool z_dim_uses_ty)
{
	//assert(c.is_normalized()); // no, bathroom window is denormalized
	vertex_t v;
	v.set_c4(color);

	// Note: stolen from draw_cube() with tex coord logic, back face culling, etc. removed
	for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions, drawn as {Z, X, Y}
		unsigned const d[2] = {i, ((i+1)%3)}, n((i+2)%3);
		bool const tex_st(swap_tex_st ^ (z_dim_uses_ty && (d[1] == 2)));

		for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
			if (skip_faces & (1 << (2*(2-n) + j))) continue; // skip this face
			v.set_ortho_norm(n, (bool(j) ^ inverted));
			v.v[n] = c.d[n][j];

			for (unsigned s1 = 0; s1 < 2; ++s1) {
				v.v[d[1]] = c.d[d[1]][s1];
				v.t[tex_st] = ((tex.tscale_x == 0.0) ? float(s1) : (tex.tscale_x*(v.v[d[1]] - tex_origin[d[1]]) + tex.txoff)); // tscale==0.0 => fit texture to cube

				for (unsigned k = 0; k < 2; ++k) { // iterate over vertices
					bool const s2(bool(k^j^s1)^inverted^1); // need to orient the vertices differently for each side
					v.v[d[0]] = c.d[d[0]][s2];
					v.t[!tex_st] = ((tex.tscale_y == 0.0) ? float(s2) : (tex.tscale_y*(v.v[d[0]] - tex_origin[d[0]]) + tex.tyoff));
					quad_verts.push_back(v);
					if (mirror_x) {quad_verts.back().t[0] = 1.0 - v.t[0];} // use for pictures and books
					if (mirror_y) {quad_verts.back().t[1] = 1.0 - v.t[1];} // used for books
				} // for k
			} // for s1
		} // for j
	} // for i
}
// untextured version of the above function
void rgeom_mat_t::add_cube_to_verts_untextured(cube_t const &c, colorRGBA const &color, unsigned skip_faces) { // add an inverted flag?
	vertex_t v;
	v.set_c4(color);

	for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
		unsigned const d[2] = {i, ((i+1)%3)}, n((i+2)%3);

		for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
			if (skip_faces & (1 << (2*(2-n) + j))) continue; // skip this face
			v.set_ortho_norm(n, j);
			v.v[n] = c.d[n][j];

			for (unsigned s1 = 0; s1 < 2; ++s1) {
				v.v[d[1]] = c.d[d[1]][s1];
				v.v[d[0]] = c.d[d[0]][!(j^s1)]; quad_verts.push_back(v);
				v.v[d[0]] = c.d[d[0]][ (j^s1)]; quad_verts.push_back(v);
			} // for s1
		} // for j
	} // for i
}

template<typename T> void add_inverted_triangles(T &verts, vector<unsigned> &indices, unsigned verts_start, unsigned ixs_start) {
	unsigned const verts_end(verts.size()), numv(verts_end - verts_start);
	verts.resize(verts_end + numv);

	for (unsigned i = verts_start; i < verts_end; ++i) {
		verts[i+numv] = verts[i];
		verts[i+numv].invert_normal();
	}
	unsigned const ixs_end(indices.size()), numi(ixs_end - ixs_start);
	indices.resize(ixs_end + numi);
	for (unsigned i = 0; i < numi; ++i) {indices[ixs_end + i] = (indices[ixs_end - i - 1] + numv);} // copy in reverse order
}

void swap_cube_z_xy(cube_t &c, bool dim) {
	swap(c.z1(), c.d[dim][0]);
	swap(c.z2(), c.d[dim][1]);
}

void rgeom_mat_t::add_ortho_cylin_to_verts(cube_t const &c, colorRGBA const &color, int dim, bool draw_bot, bool draw_top, bool two_sided, bool inv_tb,
	float rs_bot, float rs_top, float side_tscale, float end_tscale, bool skip_sides, unsigned ndiv, float side_tscale_add, bool swap_txy, float len_tc2, float len_tc1)
{
	if (dim == 2) { // Z: this is our standard v_cylinder
		add_vcylin_to_verts(c, color, draw_bot, draw_top, two_sided, inv_tb, rs_bot, rs_top, side_tscale, end_tscale, skip_sides, ndiv, side_tscale_add, swap_txy, len_tc2, len_tc1);
		return;
	}
	cube_t c_rot(c);
	swap_cube_z_xy(c_rot, dim);
	unsigned const itri_verts_start_ix(itri_verts.size()), ixs_start_ix(indices.size());
	add_vcylin_to_verts(c_rot, color, draw_bot, draw_top, two_sided, inv_tb, rs_bot, rs_top, side_tscale, end_tscale, skip_sides, ndiv, side_tscale_add, swap_txy, len_tc2, len_tc1);
	
	for (auto v = itri_verts.begin()+itri_verts_start_ix; v != itri_verts.end(); ++v) { // swap triangle vertices and normals
		std::swap(v->v[2], v->v[dim]);
		std::swap(v->n[2], v->n[dim]);
	}
	std::reverse(indices.begin()+ixs_start_ix, indices.end()); // fix winding order
}
void rgeom_mat_t::add_vcylin_to_verts(cube_t const &c, colorRGBA const &color, bool draw_bot, bool draw_top, bool two_sided, bool inv_tb, float rs_bot,
	float rs_top, float side_tscale, float end_tscale, bool skip_sides, unsigned ndiv, float side_tscale_add, bool swap_txy, float len_tc2, float len_tc1)
{
	point const center(c.get_cube_center());
	float const radius(0.5*min(c.dx(), c.dy())); // cube X/Y size should be equal/square
	add_cylin_to_verts(point(center.x, center.y, c.z1()), point(center.x, center.y, c.z2()), radius*rs_bot, radius*rs_top,
		color, draw_bot, draw_top, two_sided, inv_tb, side_tscale, end_tscale, skip_sides, ndiv, side_tscale_add, swap_txy, len_tc2, len_tc1);
}
void rgeom_mat_t::add_cylin_to_verts(point const &bot, point const &top, float bot_radius, float top_radius, colorRGBA const &color, bool draw_bot, bool draw_top,
	bool two_sided, bool inv_tb, float side_tscale, float end_tscale, bool skip_sides, unsigned ndiv, float side_tscale_add, bool swap_txy, float len_tc2, float len_tc1)
{
	assert((!skip_sides) || draw_bot || draw_top); // must draw something
	point const ce[2] = {bot, top};
	float const ndiv_inv(1.0/ndiv), half_end_tscale(0.5*end_tscale);
	vector3d v12;
	vector_point_norm const &vpn(gen_cylinder_data(ce, bot_radius, top_radius, ndiv, v12));
	color_wrapper const cw(color);
	unsigned itris_start(itri_verts.size()), ixs_start(indices.size()), itix(itris_start), iix(ixs_start);

	if (!skip_sides) {
		unsigned const ixs_off[6] = {1,2,0, 3,2,1}; // 1 quad = 2 triangles
		bool const flat_sides(ndiv <= 6 && side_tscale == 0.0); // hack to draw bolts untextured with flat sides, since no other cylinders have only 6 sides
		unsigned const num_side_verts(flat_sides ? 4*ndiv : 2*(ndiv+1)), unique_verts_per_side(flat_sides ? 4 : 2);
		itri_verts.resize(itris_start + num_side_verts);
		indices.resize(ixs_start + 6*ndiv);

		if (flat_sides) {
			for (unsigned i = 0; i < ndiv; ++i) { // vertex data
				unsigned const in((i+1)%ndiv);
				point const pts[4] = {vpn.p[(i<<1)+0], vpn.p[(i<<1)+1], vpn.p[(in<<1)+0], vpn.p[(in<<1)+1]};
				norm_comp const normal(get_poly_norm(pts));
				for (unsigned n = 0; n < 4; ++n) {itri_verts[itix++].assign(pts[n], normal, 0.0, 0.0, cw);} // all tcs=0
			}
		}
		else {
			for (unsigned i = 0; i <= ndiv; ++i) { // vertex data
				unsigned const s(i%ndiv);
				float const ts(side_tscale*(1.0f - i*ndiv_inv) + side_tscale_add);
				norm_comp const normal(0.5*(vpn.n[s] + vpn.n[(i+ndiv-1)%ndiv])); // normalize?
				itri_verts[itix++].assign(vpn.p[(s<<1)+0], normal, (swap_txy ? len_tc1 : ts), (swap_txy ? ts : len_tc1), cw);
				itri_verts[itix++].assign(vpn.p[(s<<1)+1], normal, (swap_txy ? len_tc2 : ts), (swap_txy ? ts : len_tc2), cw);
			}
		}
		for (unsigned i = 0; i < ndiv; ++i) { // index data
			unsigned const ix0(itris_start + unique_verts_per_side*i);
			for (unsigned j = 0; j < 6; ++j) {indices[iix++] = ix0 + ixs_off[j];}
		}
		// room object drawing uses back face culling and single sided lighting; to make lighting two sided, need to add verts with inverted normals/winding dirs
		if (two_sided) {add_inverted_triangles(itri_verts, indices, itris_start, ixs_start);}
	}
	// maybe add top and bottom end cap using triangles, currently using all TCs=0.0
	unsigned const num_ends((unsigned)draw_top + (unsigned)draw_bot);
	itris_start = itix = itri_verts.size();
	ixs_start   = iix  = indices.size();
	itri_verts.resize(itris_start + (ndiv + 1)*num_ends);
	indices.resize(ixs_start + 3*ndiv*num_ends);

	for (unsigned bt = 0; bt < 2; ++bt) {
		if (!(bt ? draw_top : draw_bot)) continue; // this disk not drawn
		norm_comp const normal((bool(bt) ^ inv_tb) ? v12 : -v12);
		unsigned const center_ix(itix);
		itri_verts[itix++].assign(ce[bt], normal, half_end_tscale, half_end_tscale, cw); // center

		for (unsigned I = 0; I < ndiv; ++I) {
			unsigned const i(bt ? ndiv-I-1 : I); // invert winding order for top face
			vector3d const &side_normal(vpn.n[i]);
			itri_verts[itix++].assign(vpn.p[(i<<1) + bt], normal, half_end_tscale*(side_normal.x + 1.0), half_end_tscale*(side_normal.y + 1.0), cw); // assign tcs from side normal
			indices[iix++] = center_ix; // center
			indices[iix++] = center_ix + i + 1;
			indices[iix++] = center_ix + ((i+1)%ndiv) + 1;
		}
	} // for bt
	if (inv_tb) {std::reverse(indices.begin()+ixs_start, indices.end());} // reverse the order to swap triangle winding order
	if (two_sided) {add_inverted_triangles(itri_verts, indices, itris_start, ixs_start);}
}

void rgeom_mat_t::add_disk_to_verts(point const &pos, float radius, vector3d const &dir, colorRGBA const &color, bool swap_txy, bool inv_ts, bool inv_tt) {
	assert(radius > 0.0);
	color_wrapper const cw(color);
	norm_comp const nc(dir);
	unsigned const ndiv(N_CYL_SIDES), itris_start(itri_verts.size());
	float const css(-1.0*TWO_PI/(float)ndiv), sin_ds(sin(css)), cos_ds(cos(css));
	float sin_s(0.0), cos_s(1.0);
	vector3d const v1(cross_product(dir, (fabs(dir.x) > fabs(dir.y) ? plus_y : plus_x)).get_norm()), v2(cross_product(dir, v1).get_norm());
	itri_verts.emplace_back(pos, nc, 0.5, 0.5, cw);

	for (unsigned i = 0; i < ndiv; ++i) {
		float const s(sin_s), c(cos_s), ts(0.5*(1.0 + (swap_txy ? c : s))), tt(0.5*(1.0 + (swap_txy ? s : c)));
		itri_verts.emplace_back((pos + (radius*s)*v1 + (radius*c)*v2), nc, (inv_ts ? 1.0-ts : ts), (inv_tt ? 1.0-tt : tt), cw);
		indices.push_back(itris_start); // center
		indices.push_back(itris_start + i + 1);
		indices.push_back(itris_start + ((i+1)%ndiv) + 1);
		sin_s = s*cos_ds + c*sin_ds;
		cos_s = c*cos_ds - s*sin_ds;
	}
}

// Note: size can be nonuniform in X/Y/Z
void rgeom_mat_t::add_sphere_to_verts(point const &center, vector3d const &size, colorRGBA const &color, bool low_detail,
	vector3d const &skip_hemi_dir, tex_range_t const &tr, xform_matrix const *const matrix, float ts_add, float tt_add)
{
	static vector<vert_norm_tc>      cached_verts[2]; // high/low detail, reused across all calls
	static vector<vert_norm_comp_tc> cached_vncs [2];
	static vector<unsigned>          cached_ixs  [2];
	vector<vert_norm_tc>      &verts(cached_verts[low_detail]);
	vector<vert_norm_comp_tc> &vncs (cached_vncs [low_detail]);
	vector<unsigned>          &ixs  (cached_ixs  [low_detail]);
	unsigned const ndiv(get_rgeom_sphere_ndiv(low_detail));

	if (verts.empty()) { // not yet created, create and cache verts
		sd_sphere_d sd(all_zeros, 1.0, ndiv);
		sphere_point_norm spn;
		sd.gen_points_norms(spn);
		sd.get_quad_points(verts, &ixs);
		assert((ixs.size()&3) == 0); // must be a multiple of 4
		vncs.resize(verts.size());
		for (unsigned i = 0; i < verts.size(); ++i) {vncs[i] = vert_norm_comp_tc(verts[i].v, verts[i].n, verts[i].t[0], verts[i].t[1]);} // vntc => vnctc
	}
	color_wrapper const cw(color);
	unsigned const ioff(itri_verts.size());
	float const tscale[2] = {(tr.x2 - tr.x1), (tr.y2 - tr.y1)}; // scale existing [0.0, 1.0] texture coords into the specified range

	if (matrix) { // must apply matrix transform to verts and normals and reconstruct norm_comps
		for (auto i = verts.begin(); i != verts.end(); ++i) {
			point pt(i->v*size);
			vector3d normal(i->n);
			matrix->apply_to_vector3d(pt); matrix->apply_to_vector3d(normal);
			itri_verts.emplace_back((pt + center), normal, (tr.x1 + i->t[0]*tscale[0] + ts_add), (tr.y1 + i->t[1]*tscale[1] + tt_add), cw);
		}
	}
	else if (skip_hemi_dir != zero_vector) { // only draw one hemisphere; assumes skip_hemi_dir is along a primary {x, y, z} axis
		// spheres are generated in a circle in the XY plane in the outer loop, and from top to bottom in Z in the inner loop;
		// to draw the top half we need to draw/include the "equator" plus the first/top half of each circular band
		unsigned const stride(ndiv+1), t_end(ndiv/2), out_stride(t_end + 1), indices_start(indices.size());
		bool const inv_hemi(skip_hemi_dir == plus_x || skip_hemi_dir == plus_y || skip_hemi_dir == plus_z);
		bool const swap_x(skip_hemi_dir == plus_x || skip_hemi_dir == -plus_x), swap_y(skip_hemi_dir == plus_y || skip_hemi_dir == -plus_y);
		assert(vncs.size() == stride*stride);

		for (unsigned s = 0; s <= ndiv; ++s) { // XY circular band
			for (unsigned t = 0; t <= t_end; ++t) { // vertical slices in Z; start in the middle; assumes ndiv is an even number
				auto v(vncs[s*stride + t]); // deep copy
				unsigned const ix(itri_verts.size());
				// rotate into correct orientation by swapping coordinates and mirroring; must apply to both the vertex and the normal
				if (inv_hemi) {v.v.z = -v.v.z; v.n[2] = 255 - v.n[2];}
				if (swap_x  ) {std::swap(v.v.x, v.v.z); std::swap(v.n[0], v.n[2]);}
				if (swap_y  ) {std::swap(v.v.y, v.v.z); std::swap(v.n[1], v.n[2]);}
				if (swap_x || swap_y) {v.v.z = -v.v.z; v.n[2] = 255 - v.n[2];}
				itri_verts.emplace_back((v.v*size + center), v, (tr.x1 + v.t[0]*tscale[0] + ts_add), (tr.y1 + v.t[1]*tscale[1] + tt_add), cw);
				if (t == t_end || s == ndiv) continue; // no indices added for last s or t values
				unsigned const qixs[4] = {ix, ix+out_stride, ix+out_stride+1, ix+1};
				for (unsigned i = 0; i < 6; ++i) {indices.push_back(qixs[quad_to_tris_ixs[i]]);} // quads (2 triangles)
			} // for t
		} // for s
		assert(indices.back() < itri_verts.size());
		if (inv_hemi) {std::reverse(indices.begin()+indices_start, indices.end());} // set correct winding order
		return;
	}
	else { // can use vncs (norm_comps)
		for (auto i = vncs.begin(); i != vncs.end(); ++i) {
			itri_verts.emplace_back((i->v*size + center), *i, (tr.x1 + i->t[0]*tscale[0] + ts_add), (tr.y1 + i->t[1]*tscale[1] + tt_add), cw);
		}
	}
	for (auto i = ixs.begin(); i != ixs.end(); i += 4) { // indices are for quads, but we want triangles, so expand them
		indices.push_back(*(i+0) + ioff); indices.push_back(*(i+1) + ioff); indices.push_back(*(i+2) + ioff);
		indices.push_back(*(i+3) + ioff); indices.push_back(*(i+0) + ioff); indices.push_back(*(i+2) + ioff);
	}
	assert(indices.back() < itri_verts.size());
}

void rgeom_mat_t::add_vert_torus_to_verts(point const &center, float r_inner, float r_outer, colorRGBA const &color, float tscale, bool low_detail) {
	unsigned const ndiv(get_rgeom_sphere_ndiv(low_detail));
	float const ts_tt(tscale/ndiv), ds(TWO_PI/ndiv), cds(cos(ds)), sds(sin(ds));
	vector<float> const &sin_cos(gen_torus_sin_cos_vals(ndiv));
	color_wrapper const cw(color);

	for (unsigned s = 0; s < ndiv; ++s) { // outer
		float const theta(s*ds), ct(cos(theta)), st(sin(theta)), ct2(ct*cds - st*sds), st2(st*cds + ct*sds);
		point const pos [2] = {point(ct, st, 0.0), point(ct2, st2, 0.0)};
		point const vpos[2] = {(center + pos[0]*r_outer), (center + pos[1]*r_outer)};
		unsigned const tri_ix_start(itri_verts.size()), ixs_start(indices.size());

		// Note: drawn as one triangle strip
		for (unsigned t = 0; t <= ndiv; ++t) { // inner
			unsigned const t_((t == ndiv) ? 0 : t);
			float const cp(sin_cos[(t_<<1)+0]), sp(sin_cos[(t_<<1)+1]);

			for (unsigned i = 0; i < 2; ++i) {
				vector3d const delta(pos[1-i]*sp + vector3d(0.0, 0.0, cp));
				itri_verts.emplace_back((vpos[1-i] + delta*r_inner), delta, ts_tt*(s+1-i), ts_tt*t, cw);
			}
		} // for t
		for (unsigned n = 0; n < 3; ++n) {indices.push_back(tri_ix_start + n);} // first triangle

		for (unsigned n = tri_ix_start+3; n < itri_verts.size(); ++n) { // each vertex after this creates a new triangle
			unsigned const ix1(indices[indices.size()-2]), ix2(indices.back()); // two previous indices
			indices.push_back(ix1);
			indices.push_back(ix2);
			indices.push_back(n); // new triangle index
		}
		// swap the winding order of every other triangle, stepping in triangle pairs
		for (unsigned i = ixs_start; i < indices.size(); i += 6) {std::swap(indices[i+4], indices[i+5]);}
	} // for s
}
void rgeom_mat_t::add_contained_vert_torus_to_verts(cube_t const &c, colorRGBA const &color, float tscale, bool low_detail) {
	float const r_inner(0.5*c.dz()), r_outer(0.25*(c.dx() + c.dy()) - r_inner);
	assert(r_inner > 0.0 && r_outer > 0.0); // cube must be wider than it is tall
	add_vert_torus_to_verts(c.get_cube_center(), r_inner, r_outer, color, tscale, low_detail);
}
void rgeom_mat_t::add_ortho_torus_to_verts(point const &center, float r_inner, float r_outer, unsigned dim, colorRGBA const &color, float tscale, bool low_detail) {
	assert(dim < 3);
	unsigned const verts_start(itri_verts.size()), ixs_start(indices.size());
	add_vert_torus_to_verts(all_zeros, r_inner, r_outer, color, tscale, low_detail);
	
	if (dim < 2) { // swap X or Y with Z
		for (auto i = itri_verts.begin()+verts_start; i != itri_verts.end(); ++i) {
			std::swap(i->v[dim], i->v[2]);
			std::swap(i->n[dim], i->n[2]);
		}
		reverse(indices.begin()+ixs_start, indices.end()); // reverse winding order
	}
	for (auto i = itri_verts.begin()+verts_start; i != itri_verts.end(); ++i) {i->v += center;}
}

void rgeom_mat_t::add_triangle_to_verts(point const v[3], colorRGBA const &color, bool two_sided, float tscale) {
	color_wrapper cw(color);
	norm_comp normal(get_poly_norm(v));
	float const ts[3] = {0.0, 0.0, tscale}, tt[3] = {0.0, tscale, 0.0}; // hard-coded for now, maybe pass in?

	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned n = 0; n < 3; ++n) {
			indices.push_back(itri_verts.size()); // since we only support indexed triangles, we have to assign each vertex its own index
			unsigned const ix(side ? 2-n : n); // reverse order for side=1
			itri_verts.emplace_back(v[ix], normal, ts[ix], tt[ix], cw);
		}
		if (side == 0) {
			if (!two_sided) break; // single sided
			normal.invert_normal();
		}
	} // for side
}

class rgeom_alloc_t {
	deque<rgeom_storage_t> free_list; // one per unique texture ID/material
public:
	void alloc_safe(rgeom_storage_t &s) {
#pragma omp critical(rgeom_alloc)
		alloc(s);
	}
	void alloc(rgeom_storage_t &s) { // attempt to use free_list entry to reuse existing capacity
		if (free_list.empty()) return; // no pre-alloc
		//cout << TXT(free_list.size()) << TXT(free_list.back().get_tot_vert_capacity()) << endl; // total mem usage is 913/1045

		// try to find a free list element with the same tex so that we balance out material memory usage/capacity better
		for (unsigned i = 0; i < free_list.size(); ++i) {
			if (!free_list[i].tex.is_compatible(s.tex)) continue;
			s.swap_vectors(free_list[i]); // transfer existing capacity from free list
			free_list[i].swap(free_list.back());
			free_list.pop_back();
			return; // done
		}
	}
	void free(rgeom_storage_t &s) {
		s.clear(); // in case the caller didn't clear it
		if (s.get_mem_usage() == 0) return; // no memory allocated, no point in adding to the free list
		free_list.push_back(rgeom_storage_t(s.tex)); // record tex of incoming element
		s.swap_vectors(free_list.back()); // transfer existing capacity to free list; clear capacity from s
	}
	unsigned get_mem_usage() const {
		unsigned mem(free_list.size()*sizeof(rgeom_storage_t));
		for (auto i = free_list.begin(); i != free_list.end(); ++i) {
			//cout << i->tex.tid << "\t" << i->tex.shadowed << "\t" << (i->quad_verts.capacity() + i->itri_verts.capacity()) << "\t" << i->get_mem_usage() << endl; // TESTING
			mem += i->get_mem_usage();
		}
		return mem;
	}
	unsigned size() const {return free_list.size();}
};
rgeom_alloc_t rgeom_alloc; // static allocator with free list, shared across all buildings; not thread safe


vbo_cache_t::vbo_cache_entry_t vbo_cache_t::alloc(unsigned size, bool is_index) {
	assert(size > 0); // not required, but a good sanity check
	auto &e(entries[is_index]);
	//if ((v_used % 1000) == 0) {print_stats();} // TESTING
	unsigned const max_size(size + size/5); // no more than 20% wasted cap
	auto best_fit(e.end());
	unsigned target_sz(size);

	for (auto i = e.begin(); i != e.end(); ++i) {
		target_sz += 8; // increase with every iteration
		if (i->size < size || i->size > max_size) continue; // too small or too large
		if (best_fit != e.end() && i->size >= best_fit->size) continue; // not the best fit
		best_fit = i;
		if (i->size < target_sz) break; // close fit, done
	}
	if (best_fit != e.end()) {
		vbo_cache_entry_t const ret(*best_fit); // deep copy
		swap(*best_fit, e.back());
		e.pop_back();
		++v_reuse; s_reuse += ret.size; ++v_used; s_used += ret.size; // Note: s_reuse can overflow
		assert(v_free > 0); assert(s_free >= size);
		--v_free; s_free -= size;
		return ret; // done
	} // for i
	++v_alloc; s_alloc += size; ++v_used; s_used += size;
	return vbo_cache_entry_t(create_vbo(), 0); // create a new VBO
}
void vbo_cache_t::free(unsigned &vbo, unsigned size, bool is_index) {
	if (!vbo) return; // nothing allocated
	if (size == 0) {delete_and_zero_vbo(vbo); return;} // shouldn't get here?
	assert(v_used > 0); assert(s_used >= size);
	--v_used; s_used -= size; ++v_free; s_free += size;
	entries[is_index].emplace_back(vbo, size);
	vbo = 0;
}
void vbo_cache_t::clear() { // unused
	for (unsigned d = 0; d < 2; ++d) {
		for (vbo_cache_entry_t &entry : entries[d]) {
			delete_vbo(entry.vbo);
			room_geom_mem -= entry.size;
		}
		entries[d].clear();
	}
}
void vbo_cache_t::print_stats() const {
	cout << "VBOs: A " << v_alloc << " U " << v_used << " R " << v_reuse << " F " << v_free
		 << "  SZ: A " << (s_alloc>>20) << " U " << (s_used>>20) << " R " << (s_reuse>>20) << " F " << (s_free>>20) << endl; // in MB
}

/*static*/ vbo_cache_t rgeom_mat_t::vbo_cache;

void rgeom_storage_t::clear(bool free_memory) {
	if (free_memory) {clear_container(quad_verts);} else {quad_verts.clear();}
	if (free_memory) {clear_container(itri_verts);} else {itri_verts.clear();}
	if (free_memory) {clear_container(indices   );} else {indices   .clear();}
}
void rgeom_storage_t::swap_vectors(rgeom_storage_t &s) { // Note: doesn't swap tex
	quad_verts.swap(s.quad_verts);
	itri_verts.swap(s.itri_verts);
	indices.swap(s.indices);
}
void rgeom_storage_t::swap(rgeom_storage_t &s) {
	swap_vectors(s);
	std::swap(tex, s.tex);
}

void rgeom_mat_t::clear() {
	clear_vbos();
	clear_vectors();
	num_verts = num_ixs = 0;
}
void rgeom_mat_t::clear_vbos() {
	vbo_cache.free(vao_mgr.vbo,  vert_vbo_sz, 0);
	vbo_cache.free(vao_mgr.ivbo, ixs_vbo_sz,  1);
	vao_mgr.clear_vaos(); // Note: VAOs not reused because they generally won't be used with the same {vbo, ivbo} pair
	vert_vbo_sz = ixs_vbo_sz = 0;
}

void rotate_verts(vector<rgeom_mat_t::vertex_t> &verts, building_t const &building) {
	point const center(building.bcube.get_cube_center());

	for (auto i = verts.begin(); i != verts.end(); ++i) {
		building.do_xy_rotate(center, i->v);
		vector3d n(i->get_norm());
		building.do_xy_rotate_normal(n);
		i->set_norm(n);
	}
}
void rgeom_mat_t::create_vbo(building_t const &building) {
	if (building.is_rotated()) { // rotate all vertices to match the building rotation
		rotate_verts(quad_verts, building);
		rotate_verts(itri_verts, building);
	}
	create_vbo_inner();
	rgeom_alloc.free(*this); // vertex and index data is no longer needed and can be cleared
}
void rgeom_mat_t::create_vbo_inner() {
	assert(itri_verts.empty() == indices.empty());
	unsigned const qsz(quad_verts.size()*sizeof(vertex_t)), itsz(itri_verts.size()*sizeof(vertex_t)), tot_verts_sz(qsz + itsz);
	// in most cases when num_verts starts out nonzero there is no actual update for this material, but accurately skipping the VBO update is difficult;
	// hashing the vertex data is too slow, and simply summing the verts is inaccurate for things like light switch rotations and buildings far from the origin
	num_verts = quad_verts.size() + itri_verts.size();
	if (num_verts == 0) return; // nothing to do
	gen_quad_ixs(indices, 6*(quad_verts.size()/4), itri_verts.size()); // append indices for quad_verts
	num_ixs = indices.size();
	unsigned const ix_data_sz(num_ixs*sizeof(unsigned));

	if (vao_mgr.vbo && tot_verts_sz <= vert_vbo_sz && vao_mgr.ivbo && ix_data_sz <= ixs_vbo_sz) { // reuse previous VBOs
		update_indices(vao_mgr.ivbo, indices);
		check_bind_vbo(vao_mgr.vbo);
	}
	else { // create a new VBO
		clear_vbos(); // free any existing VBO memory
		auto vret(vbo_cache.alloc(tot_verts_sz, 0)); // verts
		auto iret(vbo_cache.alloc(ix_data_sz,   1)); // indices
		vao_mgr.vbo  = vret.vbo;
		vao_mgr.ivbo = iret.vbo;
		check_bind_vbo(vao_mgr.vbo);

		if (vret.size == 0) { // newly created
			vert_vbo_sz = tot_verts_sz;
			upload_vbo_data(nullptr, tot_verts_sz);
			room_geom_mem += tot_verts_sz;
		}
		else { // existing
			vert_vbo_sz = vret.size;
			assert(tot_verts_sz <= vert_vbo_sz);
		}
		if (iret.size == 0) { // newly created
			ixs_vbo_sz = ix_data_sz;
			upload_to_vbo(vao_mgr.ivbo, indices, 1, 1);
			room_geom_mem += indices.size()*sizeof(unsigned);
		}
		else { // existing
			ixs_vbo_sz = iret.size;
			assert(ix_data_sz <= ixs_vbo_sz );
			update_indices(vao_mgr.ivbo, indices, ix_data_sz);
		}
	}
	if (itsz > 0) {upload_vbo_sub_data(itri_verts.data(), 0,    itsz);}
	if (qsz  > 0) {upload_vbo_sub_data(quad_verts.data(), itsz, qsz );}
	bind_vbo(0);
	check_gl_error(475);

	if (num_verts >= 32) {dir_mask = 63;} // too many verts, assume all orients
	else {
		dir_mask = 0;
		for (unsigned n = 0; n < 2; ++n) {
			for (auto const &v : (n ? itri_verts : quad_verts)) {
				for (unsigned d = 0; d < 3; ++d) {
					if (v.n[d] < 0) {dir_mask |= 1<<(2*d);} else if (v.n[d] > 0) {dir_mask |= 1<<(2*d+1);}
				}
			}
		} // for n
	}
	// calculate bcube, for use in VFC for the shadow pass;
	// only enable for small blocks to reduce runtime overhead, plus they're more likely to be occluded
	if (num_verts >= 256) {bcube.set_to_zeros();}
	else {
		bcube.set_from_point(itri_verts.empty() ? quad_verts.front().v : itri_verts.front().v);
		for (auto const &v : itri_verts) {bcube.union_with_pt(v.v);}
		for (auto const &v : quad_verts) {bcube.union_with_pt(v.v);}
	}
}

bool brg_batch_draw_t::has_ext_geom() const {
	for (tile_block_t const &tb : ext_by_tile) {
		for (mat_entry_t const &e : tb.to_draw) {
			if (!e.mats.empty()) return 1;
		}
	}
	return 0;
}
void brg_batch_draw_t::clear() {
	to_draw.clear();
	ext_by_tile.clear();
	tid_to_first_mat_map.clear();
	cur_tile_slot = 0;
}
void brg_batch_draw_t::set_camera_dir_mask(point const &camera_bs, cube_t const &bcube) {
	camera_dir_mask = 0;
	for (unsigned d = 0; d < 3; ++d) {
		if (camera_bs[d] < bcube.d[d][1]) {camera_dir_mask |= 1<<(2*d  );}
		if (camera_bs[d] > bcube.d[d][0]) {camera_dir_mask |= 1<<(2*d+1);}
	}
}
void brg_batch_draw_t::next_tile(cube_t const &bcube) {
	for (unsigned i = 0; i < ext_by_tile.size(); ++i) { // try to find an unused slot
		if (!ext_by_tile[i].bcube.is_all_zeros()) continue; // already used
		ext_by_tile[i].bcube = bcube;
		cur_tile_slot = i;
		return;
	}
	cur_tile_slot = ext_by_tile.size();
	ext_by_tile.emplace_back(bcube); // create a new slot
}
void brg_batch_draw_t::add_material(rgeom_mat_t const &m, bool is_ext_tile) {
	if (is_ext_tile) {assert(cur_tile_slot < ext_by_tile.size());}
	vector<mat_entry_t>& dest(is_ext_tile ? ext_by_tile[cur_tile_slot].to_draw : to_draw);

	for (auto &i : dest) { // check all existing materials for a matching texture, etc.
		if (i.tex.is_compat_ignore_shadowed(m.tex)) {i.mats.push_back(&m); return;} // found existing material
	}
	dest.emplace_back(m); // add a new material entry
}
void brg_batch_draw_t::draw_and_clear_batch(vector<mat_entry_t> &batch, tid_nm_pair_dstate_t &state) {
	for (auto &i : batch) {
		if (i.mats.empty()) continue; // empty slot
		i.tex.set_gl(state);
		for (auto const &m : i.mats) {m->draw_inner(0);} // shadow_only=0
		i.tex.unset_gl(state);
		i.mats.clear(); // clear mats but not batch
	}
}
void brg_batch_draw_t::draw_and_clear(shader_t &s) {
	if (to_draw.empty()) return;
	tid_nm_pair_dstate_t state(s);
	enable_blend(); // needed for rugs, book, and sign text
	draw_and_clear_batch(to_draw, state);
	disable_blend();
	indexed_vao_manager_with_shadow_t::post_render();
}
void brg_batch_draw_t::draw_and_clear_ext_tiles(shader_t &s, vector3d const &xlate) {
	if (ext_by_tile.empty()) return;
	tid_nm_pair_dstate_t state(s);
	enable_blend(); // needed for sign text

	for (tile_block_t &tb : ext_by_tile) {
		if (tb.to_draw.empty()) continue; // skip empty batches
		try_bind_tile_smap_at_point((tb.bcube.get_cube_center() + xlate), s);
		draw_and_clear_batch(tb.to_draw, state);
	}
	disable_blend();
	indexed_vao_manager_with_shadow_t::post_render();
}
void brg_batch_draw_t::clear_ext_tiles() {
	for (tile_block_t &tb : ext_by_tile) {tb.bcube = cube_t();} // reset for next frame
}

// shadow_only: 0=non-shadow pass, 1=shadow pass, 2=shadow pass with alpha mask texture
void rgeom_mat_t::draw(tid_nm_pair_dstate_t &state, brg_batch_draw_t *bbd, int shadow_only, bool reflection_pass, bool exterior_geom) {
	assert(!(exterior_geom && shadow_only)); // exterior geom shadows are not yet supported
	if (shadow_only && !en_shadows)         return; // shadows not enabled for this material (picture, whiteboard, rug, etc.)
	if (shadow_only && tex.emissive == 1.0) return; // assume this is a light source and shouldn't produce shadows
	if (reflection_pass && tex.tid == REFLECTION_TEXTURE_ID) return; // don't draw reflections of mirrors as this doesn't work correctly
	if (num_verts == 0) return; // Note: should only happen when reusing materials and all objects using this material were removed
	// VFC test for shadow pass on sparse materials that have their bcubes calculated; only really helps with backrooms;
	// here we don't add xlate to bcube because it's the location of a light source that's already in building space, not camera space
	if (shadow_only && !bcube.is_all_zeros() && !camera_pdu.cube_visible(bcube)) return;
	vao_setup(shadow_only);

	// Note: the shadow pass doesn't normally bind textures and set uniforms, so we don't need to combine those calls into batches
	if (bbd != nullptr && !shadow_only) { // add to batch draw (optimization)
		if (dir_mask > 0 && bbd->camera_dir_mask > 0 && (dir_mask & bbd->camera_dir_mask) == 0) return; // check for visible surfaces
		bbd->add_material(*this, exterior_geom);
	}
	else { // draw this material now
		if (shadow_only != 1) {tex.set_gl  (state);} // ignores texture scale for now; enable alpha texture for shadow pass
		draw_inner(shadow_only);
		if (shadow_only != 1) {tex.unset_gl(state);}
	}
}
void rgeom_mat_t::pre_draw(int shadow_only) const {
	vao_mgr.pre_render(shadow_only != 0);
}
void rgeom_mat_t::draw_geom() const {
	glDrawRangeElements(GL_TRIANGLES, 0, num_verts, num_ixs, GL_UNSIGNED_INT, nullptr);
	++num_frame_draw_calls;
}
void rgeom_mat_t::draw_inner(int shadow_only) const {
	pre_draw(shadow_only);
	draw_geom();
}
void rgeom_mat_t::vao_setup(bool shadow_only) {
	vao_mgr.create_and_upload(vector<vertex_t>(), vector<unsigned>(), shadow_only, 0, 1); // pass empty vectors because data is already uploaded; dynamic_level=0, setup_pointers=1
}
void rgeom_mat_t::upload_draw_and_clear(tid_nm_pair_dstate_t &state) { // Note: called by draw_interactive_player_obj() and water_draw_t
	if (empty()) return; // nothing to do; can this happen?
	create_vbo_inner();
	draw(state, nullptr, 0, 0, 0); // no brg_batch_draw_t, shadow=reflection=exterior=0
	clear();
	indexed_vao_manager_with_shadow_t::post_render();
}

void building_materials_t::clear() {
	invalidate();
	for (iterator m = begin(); m != end(); ++m) {m->clear();}
	vector<rgeom_mat_t>::clear();
}
unsigned building_materials_t::count_all_verts() const {
	unsigned num_verts(0);
	for (const_iterator m = begin(); m != end(); ++m) {num_verts += m->num_verts;}
	return num_verts;
}
rgeom_mat_t &building_materials_t::get_material(tid_nm_pair_t const &tex, bool inc_shadows) {
	// for now we do a simple linear search because there shouldn't be too many unique materials
	for (iterator m = begin(); m != end(); ++m) {
		if (!m->tex.is_compatible(tex)) continue;
		if (inc_shadows) {m->enable_shadows();}
		// tscale diffs don't make new materials; copy tscales from incoming tex; this field may be used locally by the caller, but isn't used for drawing
		m->tex.tscale_x = tex.tscale_x; m->tex.tscale_y = tex.tscale_y;
		if (m->get_tot_vert_capacity() == 0) {rgeom_alloc.alloc_safe(*m);} // existing but empty entry, allocate capacity from the allocator free list
		return *m;
	}
	emplace_back(tex); // not found, add a new material
	if (inc_shadows) {back().enable_shadows();}
	rgeom_alloc.alloc_safe(back());
	return back();
}
void building_materials_t::create_vbos(building_t const &building) {
	for (iterator m = begin(); m != end(); ++m) {m->create_vbo(building);}
	valid = 1;
}
void building_materials_t::draw(brg_batch_draw_t *bbd, shader_t &s, int shadow_only, bool reflection_pass, bool exterior_geom) {
	if (!valid) return; // pending generation of data, don't draw yet
	//highres_timer_t timer("Draw Materials"); // 0.0168
	tid_nm_pair_dstate_t state(s);
	for (iterator m = begin(); m != end(); ++m) {m->draw(state, bbd, shadow_only, reflection_pass, exterior_geom);}
}
void building_materials_t::upload_draw_and_clear(shader_t &s) {
	tid_nm_pair_dstate_t state(s);
	for (iterator m = begin(); m != end(); ++m) {m->upload_draw_and_clear(state);}
}

void building_room_geom_t::add_tquad(building_geom_t const &bg, tquad_with_ix_t const &tquad, cube_t const &bcube, tid_nm_pair_t const &tex,
	colorRGBA const &color, bool invert_tc_x, bool exclude_frame, bool no_tc)
{
	assert(tquad.npts == 4); // quads only, for doors
	add_tquad_to_verts(bg, tquad, bcube, tex, color, mats_doors.get_material(tex, 1).quad_verts, invert_tc_x, exclude_frame, no_tc, 1); // inc_shadows=1, no_rotate=1
}

void building_room_geom_t::clear() {
	clear_materials();
	objs.clear();
	light_bcubes.clear();
	has_elevators = 0;
}
void building_room_geom_t::clear_materials() { // clears all materials
	mats_static .clear();
	mats_alpha  .clear();
	mats_small  .clear();
	mats_text   .clear();
	mats_amask  .clear();
	mats_dynamic.clear();
	mats_doors  .clear();
	mats_lights .clear();
	mats_detail .clear();
	mats_exterior.clear();
	mats_ext_detail.clear();
	obj_model_insts.clear(); // these are associated with static VBOs
}
// Note: used for room lighting changes; detail object changes are not supported
void building_room_geom_t::check_invalid_draw_data() {
	if (invalidate_mats_mask & (1 << MAT_TYPE_SMALL  )) { // small objects
		mats_small.invalidate();
		mats_amask.invalidate();
		mats_text .invalidate(); // Note: for now text is assigned to type MAT_TYPE_SMALL since it's always drawn with small objects
	}
	if (invalidate_mats_mask & (1 << MAT_TYPE_STATIC )) { // large objects and 3D models
		mats_static  .invalidate();
		mats_alpha   .invalidate();
		mats_exterior.invalidate(); // not needed since this is immutable?
		obj_model_insts.clear();
	}
	//if (invalidate_mats_mask & (1 << MAT_TYPE_TEXT  )) {mats_text    .invalidate();} // text objects
	if (invalidate_mats_mask & (1 << MAT_TYPE_DYNAMIC )) {mats_dynamic .invalidate();} // dynamic objects
	if (invalidate_mats_mask & (1 << MAT_TYPE_DOORS   )) {mats_doors   .invalidate();}
	if (invalidate_mats_mask & (1 << MAT_TYPE_LIGHTS  )) {mats_lights  .invalidate();}
	if (invalidate_mats_mask & (1 << MAT_TYPE_DETAIL  )) {mats_detail  .invalidate();}
	invalidate_mats_mask = 0; // reset for next frame
}
void building_room_geom_t::invalidate_draw_data_for_obj(room_object_t const &obj, bool was_taken) {
	if (obj.is_dynamic() || (obj.type == TYPE_BUTTON && obj.in_elevator())) { // elevator buttons are drawn as dynamic objects
		update_dynamic_draw_data();
		return;
	}
	bldg_obj_type_t const type(was_taken ? get_taken_obj_type(obj) : get_room_obj_type(obj));
	if (type.lg_sm & 2)            {invalidate_small_geom ();} // small objects
	if (type.lg_sm & 1)            {invalidate_static_geom();} // large objects and 3D models
	if (type.is_model )            {invalidate_model_geom ();} // model
	else if (type.lg_sm == 0)      {invalidate_detail_geom();} // detail object
	if (obj.type == TYPE_CEIL_FAN) {invalidate_lights_geom();} // invalidate the light on the fan as well
}
// Note: called when adding, removing, or moving objects
void building_room_geom_t::update_draw_state_for_room_object(room_object_t const &obj, building_t &building, bool was_taken) {
	invalidate_draw_data_for_obj(obj, was_taken);
	//if (type.ai_coll) {building.invalidate_nav_graph();} // removing this object should not affect the AI navigation graph
	modified_by_player = 1; // flag so that we avoid re-generating room geom if the player leaves and comes back
}
unsigned building_room_geom_t::get_num_verts() const {
	return (mats_static.count_all_verts() +
		mats_small.count_all_verts() +
		mats_text.count_all_verts() +
		mats_detail.count_all_verts() +
		mats_dynamic.count_all_verts() +
		mats_lights.count_all_verts() +
		mats_amask.count_all_verts() +
		mats_alpha.count_all_verts() +
		mats_doors.count_all_verts() +
		mats_exterior.count_all_verts()) +
		mats_ext_detail.count_all_verts();
}

building_materials_t &building_room_geom_t::get_building_mat(tid_nm_pair_t const &tex, bool dynamic, unsigned small, bool transparent, bool exterior) {
	assert(!(dynamic && exterior));
	assert(small <= 2); // 0=mats_static, 1=mats_small, 2=mats_detail
	if (transparent) {assert(!small && !dynamic && !exterior);} // transparent objects must be static and can't be small
	if (exterior)   return (small ? mats_ext_detail : mats_exterior);
	if (dynamic)    return mats_dynamic;
	if (small == 2) return mats_detail;
	if (small)      return ((tex.tid == FONT_TEXTURE_ID) ? mats_text : mats_small);
	return (transparent ? mats_alpha : mats_static);
}
rgeom_mat_t &building_room_geom_t::get_metal_material(bool inc_shadows, bool dynamic, unsigned small, bool exterior, colorRGBA const &spec_color) {
	tid_nm_pair_t tex(-1, 1.0, inc_shadows);
	tex.set_specular_color(spec_color, 0.8, 60.0);
	return get_material(tex, inc_shadows, dynamic, small, 0, exterior);
}

void room_object_t::set_as_bottle(unsigned rand_id, unsigned max_type, bool no_empty, unsigned exclude_mask) {
	assert(max_type > 0 && max_type < NUM_BOTTLE_TYPES);
	obj_id = (uint16_t)rand_id;
	// cycle with a prime number until a valid type is selected; it's up to the caller to not exclude everything and make this infinite loop
	while (get_bottle_type() > max_type || ((1 << get_bottle_type()) & exclude_mask)) {obj_id += 13;}
	if (no_empty) {obj_id &= 127;} // strip off second empty bit
	color  = bottle_params[get_bottle_type()].color;
}

void building_room_geom_t::create_static_vbos(building_t const &building) {
	//highres_timer_t timer("Gen Room Geom"); // 2.35ms
	float const tscale(2.0/obj_scale);
	tid_nm_pair_t const &wall_tex(building.get_material().wall_tex);
	static vect_room_object_t rugs;
	rugs.clear();

	for (auto i = objs.begin(); i != objs.end(); ++i) {
		if (!i->is_visible() || i->is_dynamic()) continue; // skip invisible and dynamic objects
		if (!i->is_strictly_normalized()) {cout << "Denormalized object of type " << unsigned(i->type) << ": " << i->str() << endl; assert(0);}
		assert(i->type < NUM_ROBJ_TYPES);

		switch (i->type) {
		case TYPE_TABLE:   add_table   (*i, tscale, 0.12, 0.08); break; // top_dz=12% of height, leg_width=8% of height
		case TYPE_CHAIR:   add_chair   (*i, tscale); break;
		case TYPE_STAIR:   add_stair   (*i, tscale, tex_origin); break;
		case TYPE_STAIR_WALL: add_stairs_wall(*i, tex_origin, wall_tex); break;
		case TYPE_RUG:     rugs.push_back(*i); break; // must be drawn last - save until later
		case TYPE_PICTURE: add_picture (*i); break;
		case TYPE_WBOARD:  add_picture (*i); break;
		case TYPE_BOOK:    add_book    (*i, 1, 0, 0); break; // lg
		case TYPE_BCASE:   add_bookcase(*i, 1, 0, 0, tscale, 0); break; // lg
		case TYPE_WINE_RACK: add_wine_rack(*i, 1, 0, tscale); break;
		case TYPE_DESK:    add_desk    (*i, tscale, 1, 0); break;
		case TYPE_RDESK:   add_reception_desk(*i, tscale); break;
		case TYPE_BED:     add_bed     (*i, 1, 0, tscale); break;
		case TYPE_WINDOW:  add_window  (*i, tscale); break;
		case TYPE_TUB:     add_tub_outer (*i); break;
		case TYPE_SINK:    add_sink_water(*i); break;
		case TYPE_TV: case TYPE_MONITOR: add_tv_picture(*i); break;
		case TYPE_CUBICLE: add_cubicle (*i, tscale); break;
		case TYPE_STALL:   add_br_stall(*i); break;
		case TYPE_COUNTER: add_counter (*i, tscale, 1, 0); break; // lg
		case TYPE_CABINET: add_cabinet (*i, tscale, 1, 0); break; // lg
		case TYPE_KSINK:   add_counter (*i, tscale, 1, 0); break; // counter with kitchen  sink; lg
		case TYPE_BRSINK:  add_counter (*i, tscale, 1, 0); break; // counter with bathroom sink; lg
		case TYPE_PLANT:   add_potted_plant(*i, 1, 0); break; // pot only
		case TYPE_DRESSER: case TYPE_NIGHTSTAND: add_dresser(*i, tscale, 1, 0); break;
		case TYPE_DRESS_MIR: add_dresser_mirror(*i, tscale); break;
		case TYPE_FLOORING:add_flooring(*i, tscale); break;
		case TYPE_CLOSET:  add_closet  (*i, wall_tex, building.get_trim_color(), 1, 0); break; // inc_lg=1, inc_sm=0
		case TYPE_MIRROR:  add_mirror  (*i); break;
		case TYPE_SHOWER:  add_shower  (*i, tscale, 1, 0); break; // inc_lg=1, inc_sm=0
		case TYPE_SHOWERTUB: add_shower_tub(*i, wall_tex, building.get_trim_color(), tscale, 1, 0); break; // inc_lg=1, inc_sm=0
		case TYPE_BLINDS:  add_blinds  (*i); break;
		case TYPE_FPLACE:  add_fireplace(*i, tscale); break;
		case TYPE_FCABINET: add_filing_cabinet(*i, 1, 0); break; // lg
		case TYPE_SIGN:    add_sign    (*i, 1, 1, 1); break; // lg, exterior_only=1
		case TYPE_PIPE:    add_pipe(*i, 1); break; // add_exterior=1
		case TYPE_WIND_SILL: add_window_sill  (*i); break;
		case TYPE_EXT_STEP:  add_exterior_step(*i); break;
		case TYPE_BALCONY:   add_balcony(*i, building.ground_floor_z1, building.is_in_city); break;
		case TYPE_FALSE_DOOR: add_false_door(*i); break;
		case TYPE_RAILING:   if (i->is_exterior()) {add_railing(*i);}  break; // exterior only
		case TYPE_DOWNSPOUT: add_downspout(*i); break;
		case TYPE_SHELFRACK: add_rack(*i, 1, 0); break; // add_rack=1, add_objs=0
		case TYPE_CHIM_CAP:  add_chimney_cap(*i); break;
		case TYPE_LADDER:    add_ext_ladder(*i); break;
		case TYPE_CHECKOUT:  add_checkout(*i, tscale); break;
		case TYPE_FISHTANK:  add_fishtank(*i); break;
		//case TYPE_FRIDGE: if (i->is_open()) {} break; // draw open fridge?
		case TYPE_ELEVATOR: break; // not handled here
		case TYPE_BLOCKER:  break; // not drawn
		case TYPE_COLLIDER: break; // not drawn
		default: break;
		} // end switch
	} // for i
	add_skylights_details(building);
	for (room_object_t &rug : rugs) {add_rug(rug);} // rugs are added last so that alpha blending of their edges works
	// Note: verts are temporary, but cubes are needed for things such as collision detection with the player and ray queries for indir lighting
	//highres_timer_t timer2("Gen Room Geom VBOs"); // < 2ms
	mats_static  .create_vbos(building);
	mats_alpha   .create_vbos(building);
	mats_exterior.create_vbos(building); // Note: ideally we want to include window dividers from trim_objs, but that may not have been created yet
	//cout << "static: size: " << rgeom_alloc.size() << " mem: " << rgeom_alloc.get_mem_usage() << endl; // start=47MB, peak=132MB
}

void building_room_geom_t::create_small_static_vbos(building_t const &building) {
	//highres_timer_t timer("Gen Room Geom Small"); // 7.8ms, slow building at 26,16; up to 36ms on new computer for buildings with large retail areas
	model_objs.clear(); // currently model_objs are only created for small objects in drawers, so we clear this here
	add_small_static_objs_to_verts(expanded_objs, building.get_trim_color());
	add_small_static_objs_to_verts(objs,          building.get_trim_color());
	add_attic_interior_and_rafters(building, 2.0/obj_scale, 0); // only if there's an attic; detail_pass=0
}

void building_room_geom_t::add_nested_objs_to_verts(vect_room_object_t const &objs_to_add) {
	vector_add_to(objs_to_add, pending_objs); // nested objects are added at the end so that small and text materials are thread safe
}
void building_room_geom_t::add_small_static_objs_to_verts(vect_room_object_t const &objs_to_add, colorRGBA const &trim_color, bool inc_text) {
	if (objs_to_add.empty()) return; // don't add untextured material, otherwise we may fail the (num_verts > 0) assert
	float const tscale(2.0/obj_scale);

	for (unsigned i = 0; i < objs_to_add.size(); ++i) { // Note: iterating with indices to avoid invalid ref when add_nested_objs_to_verts() is called
		room_object_t const &c(objs_to_add[i]);
		if (!c.is_visible() || c.is_dynamic()) continue; // skip invisible and dynamic objects
		assert(c.is_strictly_normalized());
		assert(c.type < NUM_ROBJ_TYPES);

		switch (c.type) {
		case TYPE_BOOK:      add_book     (c, 0, 1, inc_text); break; // sm, maybe text
		case TYPE_BCASE:     add_bookcase (c, 0, 1, inc_text, tscale, 0); break; // sm, maybe text
		case TYPE_BED:       add_bed      (c, 0, 1, tscale); break;
		case TYPE_DESK:      add_desk     (c, tscale, 0, 1); break;
		case TYPE_DRESSER: case TYPE_NIGHTSTAND: add_dresser(c, tscale, 0, 1); break;
		case TYPE_TCAN:      add_trashcan (c); break;
		case TYPE_SIGN:      add_sign     (c, 1, inc_text); break; // sm, maybe text
		case TYPE_CLOSET:    add_closet   (c, tid_nm_pair_t(), trim_color, 0, 1); break; // add closet wall trim and interior objects, don't need wall_tex; inc_lg=0, inc_sm=1
		case TYPE_SHOWER:    add_shower   (c, tscale, 0, 1); break; // inc_lg=0, inc_sm=1
		case TYPE_SHOWERTUB: add_shower_tub(c, tid_nm_pair_t(), trim_color, tscale, 0, 1); break; // don't need wall_tex; inc_lg=0, inc_sm=1
		case TYPE_RAILING:   if (!c.is_exterior()) {add_railing(c);}  break; // interior only
		case TYPE_PLANT:     add_potted_plant(c, 0, 1); break; // plant only
		case TYPE_CRATE:     add_crate    (c); break; // not small but only added to windowless rooms
		case TYPE_BOX:       add_box      (c); break; // not small but only added to windowless rooms
		case TYPE_SHELVES:   add_shelves  (c, tscale); break; // not small but only added to windowless rooms
		case TYPE_SHELFRACK: add_rack(c, 0, 1); break; // add_rack=0, add_objs=1
		case TYPE_MWAVE:     add_mwave    (c); break;
		case TYPE_COMPUTER:  add_computer (c); break;
		case TYPE_KEYBOARD:  add_keyboard (c); break;
		case TYPE_WINE_RACK: add_wine_rack(c, 0, 1, tscale); break;
		case TYPE_BOTTLE:    add_bottle   (c); break;
		case TYPE_VASE:      add_vase     (c); break;
		case TYPE_URN:       add_vase     (c); break;
		case TYPE_PAPER:     add_paper    (c); break;
		case TYPE_PAINTCAN:  add_paint_can(c); break;
		case TYPE_PEN: case TYPE_PENCIL: case TYPE_MARKER: add_pen_pencil_marker(c); break;
		case TYPE_LG_BALL:   add_lg_ball   (c); break;
		case TYPE_HANGER_ROD:add_hanger_rod(c); break;
		case TYPE_DRAIN:     add_drain_pipe(c); break;
		case TYPE_KEY:       if (has_key_3d_model()) {model_objs.push_back(c);} else {add_key(c);} break; // draw or add as 3D model
		case TYPE_SILVER:    if (c.was_expanded()  ) {model_objs.push_back(c);} break; // only draw here if expanded
		case TYPE_FOLD_SHIRT:if (c.was_expanded()  ) {model_objs.push_back(c);} break; // only draw here if expanded
		case TYPE_MONEY:     add_money   (c); break;
		case TYPE_PHONE:     add_phone   (c); break;
		case TYPE_TPROLL:    add_tproll  (c); break;
		case TYPE_TAPE:      add_tape    (c); break;
		case TYPE_SPRAYCAN:  add_spraycan(c); break;
		case TYPE_CRACK:     add_crack   (c); break;
		case TYPE_SWITCH:    add_switch  (c, 0); break; // draw_detail_pass=0
		case TYPE_BREAKER:   add_breaker (c); break;
		case TYPE_PLATE:     add_plate   (c); break;
		case TYPE_LAPTOP:    add_laptop  (c); break;
		case TYPE_PIZZA_BOX: add_pizza_box(c); break;
		case TYPE_PIZZA_TOP: add_pizza_top(c); break;
		case TYPE_BUTTON:    if (!c.in_elevator()) {add_button(c);} break; // skip buttons inside elevators, which are drawn as dynamic objects
		case TYPE_LBASKET:   add_laundry_basket(c); break;
		case TYPE_TOASTER:   add_toaster_proxy (c); break;
		case TYPE_WHEATER:   add_water_heater  (c); break;
		case TYPE_FURNACE:   add_furnace       (c); break;
		case TYPE_BRK_PANEL: add_breaker_panel (c); break; // only added to basements
		case TYPE_ATTIC_DOOR:add_attic_door(c, tscale); break;
		case TYPE_TOY:       add_toy(c); break;
		case TYPE_PAN:       add_pan(c); break;
		case TYPE_COUNTER: add_counter (c, tscale, 0, 1); break; // sm
		case TYPE_KSINK:   add_counter (c, tscale, 0, 1); break; // sm
		case TYPE_CABINET: add_cabinet (c, tscale, 0, 1); break; // sm
		case TYPE_FCABINET: add_filing_cabinet(c,  0, 1); break; // sm
		case TYPE_STAPLER: add_stapler(c); break;
		case TYPE_FEXT_MOUNT: add_fire_ext_mount(c); break;
		case TYPE_FEXT_SIGN:  add_fire_ext_sign (c); break;
		case TYPE_TEESHIRT:   add_teeshirt(c); break;
		case TYPE_PANTS:      add_pants   (c); break;
		case TYPE_BLANKET:    add_blanket (c); break;
		case TYPE_SERVER:     add_server  (c); break;
		case TYPE_POOL_BALL:  add_pool_ball(c); break;
		case TYPE_POOL_CUE:   add_pool_cue (c); break;
		case TYPE_WALL_MOUNT: add_wall_mount (c); break;
		case TYPE_POOL_TILE:  add_pool_tile  (c, tscale); break;
		case TYPE_POOL_FLOAT: add_pool_float(c); break;
		case TYPE_BENCH:      add_bench(c); break;
		case TYPE_DIV_BOARD:  add_diving_board(c); break;
		case TYPE_FLASHLIGHT: add_flashlight(c); break;
		case TYPE_CANDLE:     add_candle(c); break;
		case TYPE_CAMERA:     add_camera(c); break;
		case TYPE_CLOCK:      add_clock (c, 0); break; // add_dynamic=0
		case TYPE_FOOD_BOX:   add_food_box(c); break;
		case TYPE_SAFE:       add_safe(c); break;
		case TYPE_LAVALAMP:   add_lava_lamp(c); break;
		case TYPE_TRASH:      add_trash(c); break;
		case TYPE_DBG_SHAPE:  add_debug_shape(c); break;
		default: break;
		} // end switch
	} // for i
}

void building_room_geom_t::create_text_vbos() {
	//highres_timer_t timer("Gen Room Geom Text");

	for (unsigned d = 0; d < 2; ++d) { // {objs, expanded_objs}
		vect_room_object_t const &v(d ? expanded_objs : objs);

		for (auto i = v.begin(); i != v.end(); ++i) {
			if (!i->is_visible()) continue; // skip invisible objects
			switch (i->type) {
			case TYPE_BOOK:  add_book    (*i, 0, 0, 1); break; // text only
			case TYPE_BCASE: add_bookcase(*i, 0, 0, 1, 1.0, 0); break; // text only
			case TYPE_SIGN:  add_sign    (*i, 0, 1); break; // text only
			default: break;
			} // end switch
		} // for i
	} // for d
}

void building_room_geom_t::create_detail_vbos(building_t const &building) {
	// currently only small objects that are non-interactive and can't be taken; TYPE_SWITCH almost counts; also, anything in the basement not seen from outside the building
	auto objs_end(get_placed_objs_end()); // skip buttons/stairs/elevators
	tid_nm_pair_t const &wall_tex(building.get_material().wall_tex);

	for (auto i = objs.begin(); i != objs_end; ++i) {
		if (!i->is_visible()) continue;

		switch (i->type) {
		case TYPE_OUTLET:     add_outlet(*i); break;
		case TYPE_VENT:       add_vent  (*i); break;
		case TYPE_SWITCH:     add_switch(*i, 1); break; // draw_detail_pass=0
		case TYPE_PG_WALL:    add_basement_wall  (*i, tex_origin, wall_tex); break;
		case TYPE_PG_PILLAR:  add_basement_pillar(*i, wall_tex); break;
		case TYPE_PG_BEAM:    add_basement_beam  (*i, wall_tex); break;
		case TYPE_RAMP:       add_pg_ramp        (*i, wall_tex.tscale_x); break;
		case TYPE_PARK_SPACE: add_parking_space  (*i, wall_tex.tscale_x); break;
		case TYPE_PIPE:       add_pipe(*i, 0); break; // add_exterior=0
		case TYPE_SPRINKLER:  add_sprinkler(*i); break;
		case TYPE_VALVE:      add_valve(*i); break;
		case TYPE_CURB:       add_curb(*i); break;
		case TYPE_CHIMNEY:    add_chimney(*i, building.get_material().side_tex); break; // uses exterior wall texture
		case TYPE_DUCT:       add_duct(*i); break;
		default: break;
		} // end switch
	} // for i
	for (auto const &i : trim_objs) {
		assert(i.type == TYPE_WALL_TRIM);
		add_wall_trim(i);
	}
	add_attic_interior_and_rafters(building, 2.0/obj_scale, 1); // only if there's an attic; detail_pass=1
	mats_detail    .create_vbos(building);
	mats_ext_detail.create_vbos(building);
}

void building_room_geom_t::create_obj_model_insts(building_t const &building) { // handle drawing of 3D models
	//highres_timer_t timer("Gen Room Model Insts");
	obj_model_insts.clear();

	for (unsigned vect_id = 0; vect_id < 2; ++vect_id) {
		auto const &obj_vect((vect_id == 1) ? expanded_objs : objs);
		unsigned const obj_id_offset((vect_id == 1) ? objs.size() : 0);
		auto objs_end((vect_id == 1) ? expanded_objs.end() : get_placed_objs_end()); // skip buttons/stairs/elevators

		for (auto i = obj_vect.begin(); i != objs_end; ++i) {
			if (!i->is_visible() || !i->is_obj_model_type()) continue;
			if (i->type == TYPE_KEY || (i->type == TYPE_SILVER && i->was_expanded())) continue; // drawn as small object model
			vector3d dir(i->get_dir());

			if (i->rotates()) {
				float const angle(123.4*i->x1() + 456.7*i->y1() + 567.8*i->z1()); // random rotation angle based on position
				vector3d const rand_dir(vector3d(sin(angle), cos(angle), 0.0).get_norm());
				dir = ((dot_product(rand_dir, dir) < 0.0) ? -rand_dir : rand_dir); // random, but facing in the correct general direction

				if (i->type == TYPE_RCHAIR) { // rotate to face the center of the room
					vector3d const center_dir(building.get_room(i->room_id).get_cube_center() - i->get_cube_center());
					if (SIGN(dir.x) != SIGN(center_dir.x)) {dir.x *= -1.0;}
					if (SIGN(dir.y) != SIGN(center_dir.y)) {dir.y *= -1.0;}
				}
			}
			if (building.is_rotated()) {building.do_xy_rotate_normal(dir);}
			obj_model_insts.emplace_back((i - obj_vect.begin() + obj_id_offset), dir);
			//get_untextured_material().add_cube_to_verts_untextured(*i, WHITE); // for debugging of model bcubes
		} // for i
	} // for vect_id
}

void building_room_geom_t::create_lights_vbos(building_t const &building) {
	//highres_timer_t timer("Gen Room Geom Light"); // 0.75ms
	float const tscale(2.0/obj_scale);
	auto objs_end(get_placed_objs_end()); // skip buttons/stairs/elevators

	for (auto i = objs.begin(); i != objs_end; ++i) {
		if (i->is_visible() && i->type == TYPE_LIGHT) {add_light(*i, tscale);}
		else if (i->type == TYPE_CEIL_FAN) {add_ceiling_fan_light(*i, get_room_object_by_index(i->obj_id));} // pass in both the fan and the light
	}
	mats_lights.create_vbos(building);
}

void building_room_geom_t::create_dynamic_vbos(building_t const &building, point const &camera_bs, vector3d const &xlate, bool play_clock_tick) {
	//highres_timer_t timer(string("Gen Room Geom Dynamic ") + (building.is_house ? "house" : "office"));
	float const clock_sound_dist(10.0*CAMERA_RADIUS);
	
	// is it better to have a rgeom type just for clocks that gets updated when the second/minute changes?
	// unclear if this would help, since we need to iterate over objs in either case, and that may be more expensive than drawing (and would be shared the current way);
	// plus when we get here we often want to update both dynamic objects and clocks anyway
	if (!obj_dstate.empty() || have_clock) { // we have an object with dynamic state or a dynamic clock
		auto objs_end(get_placed_objs_end()); // skip buttons/stairs/elevators

		for (auto i = objs.begin(); i != objs_end; ++i) {
			if (!i->is_visible()) continue; // only visible objects; can't do VFC because this is not updated every frame
			
			if (i->type == TYPE_CLOCK) {
				add_clock(*i, 1); // add_dynamic=1
				if (!play_clock_tick || (i->item_flags & 1)) continue; // skip for digital clocks
				point const pos(i->get_cube_center());
				if (fabs(pos.z - camera_bs.z) > 0.75*building.get_window_vspace()) continue; // different floors
				float const dist(p2p_dist(camera_bs, pos));
				if (dist > clock_sound_dist) continue;
				if (!building.get_room(i->room_id).contains_pt(camera_bs)) continue; // only play ticking when the player and clock are in the same room (ignoring rotation)
				gen_sound_thread_safe(SOUND_CLICK, (pos + xlate), 0.5*(1.0 - dist/clock_sound_dist));
				continue;
			}
			if (!i->is_dynamic()) continue; // only dynamic objects
			switch (i->type) {
			case TYPE_LG_BALL  : add_lg_ball  (*i); break;
			case TYPE_POOL_BALL: add_pool_ball(*i); break;
			default: assert(0); // not a supported dynamic object type
			}
		} // for i
	}
	for (auto e = building.interior->elevators.begin(); e != building.interior->elevators.end(); ++e) {
		assert(e->car_obj_id < objs.size());
		assert(e->button_id_start < e->button_id_end && e->button_id_end <= objs.size());
		float const fc_thick_scale(building.get_elevator_fc_thick_scale());
		add_elevator_doors(*e, fc_thick_scale); // add dynamic elevator doors
		// draw elevator car for this elevator
		add_elevator(objs[e->car_obj_id], *e, 2.0/obj_scale, fc_thick_scale, building.calc_floor_offset(e->z1()),
			building.get_window_vspace(), building.has_parking_garage, !building.interior->elevators_disabled);

		for (auto j = objs.begin() + e->button_id_start; j != objs.begin() + e->button_id_end; ++j) {
			if (j->type == TYPE_BLOCKER) continue; // button was removed?
			assert(j->type == TYPE_BUTTON);
			if (j->in_elevator()) {add_button(*j);} // add button as a dynamic object if it's inside the elevator
		}
	} // for e
	mats_dynamic.create_vbos(building);
}

void building_room_geom_t::create_door_vbos(building_t const &building) {
	//highres_timer_t timer("Gen Room Geom Doors"); // 0.1ms
	vector<door_t> const &doors(building.interior->doors);
	uint8_t const door_type(building.is_residential() ? (uint8_t)tquad_with_ix_t::TYPE_HDOOR : (uint8_t)tquad_with_ix_t::TYPE_ODOOR);
	bool const have_door_handle_model(building_obj_model_loader.is_model_valid(OBJ_MODEL_DOOR_HANDLE));

	for (door_t const &d : doors) { // interior doors; opens_out=0, exterior=0
		building.add_door_verts(d, *this, door_type, d.dim, d.open_dir, d.open_amt, 0, 0, d.on_stairs, d.hinge_side, d.is_bldg_conn, d.mult_floor_room); // opens_out=0, exterior=0
		if (have_door_handle_model) {}// TODO: add model, maybe to obj_model_insts
		//else {add_door_handle(d);}
	}
	mats_doors.create_vbos(building);
}

bool ceiling_fan_is_on(room_object_t &obj, vect_room_object_t const &objs) {
	if (!obj.is_powered()) return 0; // only enabled for some fans
	assert(obj.obj_id < objs.size());
	return (objs[obj.obj_id].type == TYPE_LIGHT && objs[obj.obj_id].is_light_on()); // fan is on if light is on
}
void rotate_dir_about_z(vector3d &dir, float angle) { // Note: assumes dir is normalized
	if (angle == 0.0) return;
	assert(dir.z == 0.0); // dir must be in XY plane
	float const new_angle(atan2(dir.y, dir.x) + angle);
	dir.assign(cosf(new_angle), sinf(new_angle), 0.0);
}
void apply_room_obj_rotate(room_object_t &obj, obj_model_inst_t &inst, vect_room_object_t const &objs) {
	if (!animate2 || !(obj.flags & RO_FLAG_ROTATING)) return;

	if (obj.type == TYPE_OFF_CHAIR) {
		if (office_chair_rot_rate == 0.0) {obj.flags &= ~RO_FLAG_ROTATING; return;} // if no longer rotating, clear rotation bit
		rotate_dir_about_z(inst.dir, office_chair_rot_rate*fticks);
	}
	else if (obj.type == TYPE_HANGER || obj.type == TYPE_CLOTHES) {
		inst.dir = obj.get_dir(); // reset before applying rotate
		float const angle(((obj.flags & RO_FLAG_ADJ_LO) ? -1.0 : 1.0)*0.08*TWO_PI);
		rotate_dir_about_z(inst.dir, -angle); // limited rotation angle
	}
	else if (obj.type == TYPE_CEIL_FAN) { // rotate the entire model rather than just the fan blades
		if (ceiling_fan_is_on(obj, objs)) {rotate_dir_about_z(inst.dir, 0.2*fticks);} // fan is on if light is on
	}
	else {
		cerr << "Error: apply_room_obj_rotate() on unsupported object type " << unsigned(obj.type) << endl;
		assert(0); // unsupported object type
	}
}

void building_room_geom_t::draw_interactive_player_obj(carried_item_t const &c, shader_t &s, vector3d const &xlate) { // held by the player
	static rgeom_mat_t mat; // allocated memory is reused across frames; VBO is recreated every time; untextured
	bool needs_blend(0);

	if (c.type == TYPE_SPRAYCAN || c.type == TYPE_MARKER) {
		room_object_t c_rot(c);
		c_rot.dir = 0; // facing up
		unsigned const dim(get_max_dim(c.get_size()));

		if (dim != 2) { // if not oriented in Z
			UNROLL_2X(swap(c_rot.d[dim][i_], c_rot.d[2][i_]);); // rotate into Z dir
			c_rot.translate(c.get_cube_center() - c_rot.get_cube_center()); // translate it back to the correct location
		}
		if (c.type == TYPE_SPRAYCAN) {add_spraycan_to_material(c_rot, mat, 1);} // draw_bottom=1
		else {add_pen_pencil_marker_to_material(c_rot, mat);}
	}
	else if (c.type == TYPE_TPROLL || c.type == TYPE_TAPE) { // apply get_player_cview_rot_matrix()?
		add_vert_roll_to_material(c, mat, c.get_remaining_capacity_ratio(), 1); // player_held=1; unfortunately, we don't support texturing the TP roll here
		needs_blend = 1;
	}
	else if (c.type == TYPE_BOOK) {
		static building_room_geom_t tmp_rgeom;
		float const z_rot_angle(-(atan2(cview_dir.y, cview_dir.x) + PI_TWO));
		tmp_rgeom.add_book(c, 1, 1, 1, 0.0, 0, 0, z_rot_angle); // draw lg/sm/text
		enable_blend(); // needed for book text
		tmp_rgeom.mats_small.upload_draw_and_clear(s);
		tmp_rgeom.mats_text .upload_draw_and_clear(s);
		disable_blend();
		return;
	}
	else if (c.type == TYPE_PHONE) {
		float const z_rot_angle(-atan2(cview_dir.y, cview_dir.x));

		if (c.flags & (RO_FLAG_EMISSIVE | RO_FLAG_OPEN)) { // phone is ringing or locked screen
			static rgeom_mat_t screen_mat;
			screen_mat.tex = get_phone_tex(c);
			screen_mat.add_cube_to_verts(c, WHITE, all_zeros, ~EF_Z2, 0, 1); // mirror_x=1
			rotate_verts(screen_mat.quad_verts, plus_z, z_rot_angle, c.get_cube_center(), 0); // rotate all quad verts about Z axis
			tid_nm_pair_dstate_t state(s);
			screen_mat.upload_draw_and_clear(state);
		}
		else {mat.add_cube_to_verts(c, BLACK, all_zeros, ~EF_Z2);} // screen drawn as black
		mat.add_cube_to_verts(c, c.color, all_zeros, EF_Z2);
		rotate_verts(mat.quad_verts, plus_z, z_rot_angle, c.get_cube_center(), 0); // rotate all quad verts about Z axis
	}
	else if (c.type == TYPE_RAT) { // draw the rat facing away from the player
		bool const is_dead(c.is_broken()); // upside down if dead; shadow_pass=0; not animated
		building_obj_model_loader.draw_model(s, c.get_cube_center(), c, cview_dir, rat_color, xlate, OBJ_MODEL_RAT, 0, 0, nullptr, 0, 0, 0, is_dead);
		check_mvm_update();
		return; // don't need to run the code below
	}
	else if (c.is_medicine()) {
		static building_room_geom_t tmp_rgeom;
		room_object_t bottle(c);
		vector3d const sz(bottle.get_size());

		for (unsigned d = 0; d < 2; ++d) { // if bottle was on its side, draw it as upright
			if (sz[d] < sz.z) continue;
			float const val(0.5*(sz[d] - sz.z));
			bottle.expand_in_dim(2,  val);
			bottle.expand_in_dim(d, -val);
			break;
		}
		tmp_rgeom.add_bottle(bottle, 1); // add_bottom=1
		tmp_rgeom.mats_small.upload_draw_and_clear(s);
	}
	else if (c.type == TYPE_FIRE_EXT) {
		building_obj_model_loader.draw_model(s, c.get_cube_center(), c, cview_dir, WHITE, xlate, OBJ_MODEL_FIRE_EXT);
		check_mvm_update();
		return; // don't need to run the code below
	}
	else if (c.type == TYPE_CANDLE) {
		static building_room_geom_t tmp_rgeom;
		room_object_t obj(c);
		obj.z2() -= (1.0 - c.get_remaining_capacity_ratio())*0.9*c.dz(); // slowly burn down shorter over time
		tmp_rgeom.add_candle(obj);
		tmp_rgeom.mats_small.upload_draw_and_clear(s);
		
		if (c.is_lit()) { // add flame; will be drawn after building interior geom
			player_candle_pos.assign(obj.xc(), obj.yc(), obj.z2());
			float const radius(0.8*c.get_radius()), height(4.0*radius);
			point const camera_bs(get_camera_pos() - xlate);
			point center(player_candle_pos + 0.2*height*plus_z);
			center += 0.1*radius*(camera_bs - center).get_norm(); // move slightly in front of the wick
			candle_qbd.add_animated_billboard(center, camera_bs, up_vector, WHITE, radius, 0.5*height, fract(2.0f*tfticks/TICKS_PER_SECOND));
			
			if (animate2) { // add smoke particles about once every 20 frames
				static rand_gen_t smoke_rgen;

				if ((smoke_rgen.rand() % 20) == 0) {
					particle_manager.add_particle((center + 0.5*height*plus_z), 0.0001*plus_z, colorRGBA(GRAY, 0.25), 1.2*radius, PART_EFFECT_SMOKE);
				}
			}
		}
	}
	else if (c.type == TYPE_FLASHLIGHT) {
		assert(0); // not yet implemented because the flashlight is triggered by the mouse but not actually drawn
	}
	else {assert(0);}
	if (needs_blend) {enable_blend();}
	tid_nm_pair_dstate_t state(s);
	mat.upload_draw_and_clear(state);
	if (needs_blend) {disable_blend();}
}

void draw_candle_flames() {
	draw_emissive_billboards(candle_qbd, FIRE_TEX);
}

class water_draw_t {
	rgeom_mat_t mat;
	float tex_off;
public:
	water_draw_t() : mat(rgeom_mat_t(tid_nm_pair_t(FOAM_TEX))), tex_off(0.0) {}

	void add_water_for_sink(room_object_t const &obj) {
		if (!obj.is_active()) return;
		bool const is_cube(obj.type == TYPE_KSINK || obj.type == TYPE_BRSINK);
		float const dz(obj.dz());
		cube_t c;
		point pos(obj.get_cube_center());
		pos[obj.dim] += (obj.dir ? -1.0 : 1.0)*(is_cube ? 0.25 : 0.095)*obj.get_length(); // move toward the back of the sink
		c.set_from_sphere(pos, (is_cube ? 0.02 : 0.0055)*dz);
		set_cube_zvals(c, (obj.z1() + (is_cube ? 0.7 : 0.6)*dz), (obj.z1() + (is_cube ? 1.3 : 0.925)*dz));
		unsigned const verts_start(mat.itri_verts.size());
		mat.add_vcylin_to_verts(c, colorRGBA(WHITE, 0.5), 0, 0, 0, 0, 1.0, 1.0, 0.2);
		for (auto i = mat.itri_verts.begin() + verts_start; i != mat.itri_verts.end(); ++i) {i->t[1] *= 1.2; i->t[1] += tex_off;}
	}
	void draw_and_clear(shader_t &s) {
		if (mat.empty()) return;
		glDepthMask(GL_FALSE); // disable depth writing - fixes sky visible through exterior wall, but then not drawn in front of exterior wall
		enable_blend();
		tid_nm_pair_dstate_t state(s);
		mat.upload_draw_and_clear(state);
		disable_blend();
		glDepthMask(GL_TRUE); // re-enable depth writing
		if (animate2) {tex_off += 0.02*fticks;} // animate the texture
		if (tex_off > 1.0) {tex_off -= 1.0;}
	}
};

water_draw_t water_draw;

class lava_lamp_draw_t {
	bool closest_is_on=0;
	float dmin_sq=0.0, cur_time=0.0;
	quad_batch_draw qbd;
public:
	void add_lava_lamp(room_object_t const &obj, point const &camera_bs, building_t const &building) {
		// Note: must match code in building_room_geom_t::add_lava_lamp()
		float const radius(obj.get_radius()), r_top(0.5*radius), height(obj.dz());
		point center(obj.get_cube_center());
		if (building.is_rotated()) {building.do_xy_rotate(building.bcube.get_cube_center(), center);}
		vector3d const vdir((camera_bs - center).get_norm()), vside(cross_product(vdir, plus_z).get_norm());
		point pts[4] = {(center - radius*vside), (center + radius*vside), (center + r_top*vside), (center - r_top*vside)};
		pts[0].z = pts[1].z = obj.z1() + 0.42 *height; // z1
		pts[2].z = pts[3].z = obj.z2() - 0.167*height; // z2
		bool const is_on(obj.is_light_on());
		color_wrapper const cw(is_on ? WHITE : GRAY); // we can't do proper lighting here, so make the color gray if it's off
		qbd.add_quad_pts(pts, cw, vdir, tex_range_t());
		float const dsq(p2p_dist_sq(camera_bs, center));
		
		if (dmin_sq == 0.0 || dsq < dmin_sq) {
			dmin_sq = dsq;
			closest_is_on = is_on;
		}
	}
	void next_frame() {
		if (closest_is_on) {cur_time += fticks;} // update even if empty/not visible
		dmin_sq = 0.0; // reset for next frame
	}
	void draw_and_clear(shader_t &s) { // Note: not drawn in the shadow pass, so the interior part doesn't cast a shadow
		if (qbd.empty()) {
			if (cur_time > 10000) {cur_time = 0.0;} // reset time when not visible to avoid FP accuracy issues
			return;
		}
		shader_t lls;
		lls.set_vert_shader("no_lighting_tex_coord");
		lls.set_frag_shader("lava_lamp");
		lls.begin_shader();
		lls.add_uniform_float("time", cur_time/TICKS_PER_SECOND);
		qbd.draw_and_clear();
		s.make_current(); // switch back to the normal shader
	}
}; // lava_lamp_draw_t

lava_lamp_draw_t lava_lamp_draw;

int room_object_t::get_model_id() const { // Note: first 8 bits is model ID, last 8 bits is sub-model ID
	assert(type >= TYPE_TOILET);
	if (type == TYPE_MONITOR) return OBJ_MODEL_TV; // monitor has same model as TV
	int id((int)type + OBJ_MODEL_TOILET - TYPE_TOILET);
	if (type == TYPE_HANGER || type == TYPE_CLOTHES || type == TYPE_PLANT_MODEL) {id += ((int)item_flags << 8);} // choose a sub_model_id for these types using bits 8-15
	return id;
}

void building_t::draw_room_geom(brg_batch_draw_t *bbd, shader_t &s, shader_t &amask_shader, occlusion_checker_noncity_t &oc, vector3d const &xlate,
	unsigned building_ix, bool shadow_only, bool reflection_pass, unsigned inc_small, bool player_in_building)
{
	if (!has_room_geom()) return;

	if (0 && (display_mode & 0x20) && !shadow_only && player_in_building && bcube.contains_pt(camera_pdu.pos - xlate)) { // debug visualization of light occluders
		vect_colored_cube_t cc;
		gather_interior_cubes(cc, get_bcube_inc_extensions());
		select_texture(WHITE_TEX);
		for (colored_cube_t const &c : cc) {s.set_cur_color(c.color); draw_simple_cube(c);}
		return;
	}
	if (ENABLE_MIRROR_REFLECTIONS && !shadow_only && !reflection_pass && player_in_building) {find_mirror_needing_reflection(xlate);}
	interior->room_geom->draw(bbd, s, amask_shader, *this, oc, xlate, building_ix, shadow_only, reflection_pass, inc_small, player_in_building);
	//enable_blend();
	//glDisable(GL_CULL_FACE);
	//s.set_cur_color(colorRGBA(1.0, 0.0, 0.0, 0.5)); // for use with debug visualization
}
void building_t::gen_and_draw_room_geom(brg_batch_draw_t *bbd, shader_t &s, shader_t &amask_shader, occlusion_checker_noncity_t &oc, vector3d const &xlate,
	unsigned building_ix, bool shadow_only, bool reflection_pass, unsigned inc_small, bool player_in_building, bool ext_basement_conn_visible)
{
	if (!interior) return;
	if (!global_building_params.enable_rotated_room_geom && is_rotated()) return; // rotated buildings: need to fix texture coords, room object collisions, mirrors, etc.

	if (!shadow_only && !player_in_building && !camera_pdu.point_visible_test(bcube.get_cube_center() + xlate)) {
		// skip if none of the building parts are visible to the camera; this is rare, so it may not help
		bool any_part_visible(0);

		for (auto p = parts.begin(); p != get_real_parts_end_inc_sec(); ++p) {
			if (camera_pdu.cube_visible(*p + xlate)) {any_part_visible = 1; break;}
		}
		// check the extended basement bcube if visible through a connecting room
		any_part_visible |= (ext_basement_conn_visible && has_ext_basement() && camera_pdu.cube_visible(interior->basement_ext_bcube + xlate));
		any_part_visible |= point_in_attic(camera_pdu.pos - xlate); // check the attic

		if (!any_part_visible) {
			// exterior geometry such as roof vents and chimney caps may still be visible even if no parts are visible, so draw them
			if (is_house && !reflection_pass && has_room_geom()) {interior->room_geom->mats_exterior.draw(bbd, s, 0, 0, 1);}
			return;
		}
	}
	if (!has_room_geom()) {
		interior->room_geom.reset(new building_room_geom_t(bcube.get_llc()));
		// capture state before generating backrooms, which may add more doors
		interior->room_geom->init_num_doors   = interior->doors      .size();
		interior->room_geom->init_num_dstacks = interior->door_stacks.size();
		rand_gen_t rgen;
		rgen.set_state(building_ix, parts.size()); // set to something canonical per building
		interior->room_geom->decal_manager.rgen = rgen; // copy rgen for use with decals
		gen_room_details(rgen, building_ix); // generate so that we can draw it
		assert(has_room_geom());
	}
	if (has_room_geom() && (inc_small == 2 || inc_small == 3)) {add_wall_and_door_trim_if_needed();} // gen trim (exterior and interior) when close to the player
	draw_room_geom(bbd, s, amask_shader, oc, xlate, building_ix, shadow_only, reflection_pass, inc_small, player_in_building);
}
void building_t::clear_room_geom() {
	if (!has_room_geom()) return;
	if (interior->room_geom->modified_by_player) return; // keep the player's modifications and don't delete the room geom
	// restore pre-room_geom door state by removing any doors added to backrooms
	assert(interior->room_geom->init_num_doors   <= interior->doors      .size());
	assert(interior->room_geom->init_num_dstacks <= interior->door_stacks.size());
	interior->doors      .resize(interior->room_geom->init_num_doors  );
	interior->door_stacks.resize(interior->room_geom->init_num_dstacks);
	interior->room_geom->clear(); // free VBO data before deleting the room_geom object
	interior->room_geom.reset();
}

void get_stove_burner_locs(room_object_t const &stove, point locs[4]) {
	vector3d const sz(stove.get_size());
	bool const dim(stove.dim), dir(stove.dir);
	float const zval(stove.z2() - 0.23*sz.z), dsign(dir ? -1.0 : 1.0);

	for (unsigned w = 0; w < 2; ++w) { // width dim
		float const wval(stove.d[!dim][0] + ((bool(w) ^ dim ^ dir ^ 1) ? 0.72 : 0.28)*sz[!dim]); // left/right burners

		for (unsigned d = 0; d < 2; ++d) { // depth dim
			float const dval(stove.d[dim][dir] + dsign*(d ? 0.66 : 0.375)*sz[dim]); // front/back burners
			point pos(dval, wval, zval);
			if (dim) {swap(pos.x, pos.y);}
			locs[2*w + d] = pos;
		} // for d
	} // for w
}
void draw_stove_flames(room_object_t const &stove, point const &camera_bs, shader_t &s) {
	if (stove.item_flags == 0) return; // no burners on
	static quad_batch_draw flame_qbd; // reused across frames
	vector3d const sz(stove.get_size());
	point locs[4];
	get_stove_burner_locs(stove, locs);
	float const dsign(stove.dir ? -1.0 : 1.0);

	for (unsigned w = 0; w < 2; ++w) { // width dim
		float const radius((w ? 0.09 : 0.07)*sz.z);

		for (unsigned d = 0; d < 2; ++d) { // depth dim
			if (!(stove.item_flags & (1U<<(2U*w + d)))) continue; // burner not on
			flame_qbd.add_quad_dirs(locs[2*w + d], dsign*radius*plus_x, -dsign*radius*plus_y, WHITE); // use a negative Y to get the proper CW order; flip with dsign for symmetry
		}
	} // for w
	if (flame_qbd.empty()) return;
	select_texture(get_texture_by_name("interiors/gas_burner.png"));
	s.set_color_e(WHITE); // emissive
	enable_blend();
	set_additive_blend_mode();
	glDepthMask(GL_FALSE);
	flame_qbd.draw_and_clear();
	glDepthMask(GL_TRUE);
	set_std_blend_mode();
	disable_blend();
	s.set_color_e(BLACK);
}

void draw_obj_model(obj_model_inst_t const &i, room_object_t const &obj, shader_t &s, vector3d const &xlate, point const &obj_center, bool shadow_only) {
	bool const emissive_first_mat(!shadow_only && obj.type == TYPE_LAMP      && obj.is_light_on());
	bool const emissive_body_mat (!shadow_only && obj.type == TYPE_WALL_LAMP && obj.is_light_on());
	bool const use_low_z_bias(obj.type == TYPE_CUP && !shadow_only);
	bool const untextured(obj.flags & RO_FLAG_UNTEXTURED);
	bool const upside_down((obj.type == TYPE_RAT || obj.type == TYPE_ROACH || obj.type == TYPE_INSECT) && obj.is_broken());
	if (emissive_first_mat) {s.set_color_e(LAMP_COLOR*0.4);}
	if (use_low_z_bias    ) {s.add_uniform_float("norm_bias_scale", 0.5*DEF_NORM_BIAS_SCALE);} // half the default value
	// Note: lamps are the most common and therefore most expensive models to draw
	int const model_id(obj.get_model_id()); // first 8 bits is model ID, last 8 bits is sub-model ID
	unsigned rot_only_mat_mask(0);
	vector3d dir(i.dir);

	if (dir != obj.get_dir()) { // handle models that have rotating parts; similar to car_draw_state_t::draw_helicopter()
		if      (obj.type == TYPE_CEIL_FAN ) {rot_only_mat_mask =  1;} // only the first material (fan blades) rotate
		else if (obj.type == TYPE_OFF_CHAIR) {rot_only_mat_mask = ~1;} // all but the first material (base) rotates

		if (rot_only_mat_mask > 0) { // draw the rotated part
			building_obj_model_loader.draw_model(s, obj_center, obj, dir, obj.color, xlate, model_id, shadow_only,
				0, nullptr, ~rot_only_mat_mask, untextured, 0, upside_down, emissive_body_mat);
			dir = obj.get_dir(); // base model rotation based on object dim/dir orient and not instance rotation vector
		}
	}
	// disable the leg and rubber feet for hanging monitors; materials are {glass screen, plastic body, logo + metal + leg, object, rubber feet}
	if ((obj.type == TYPE_MONITOR || obj.type == TYPE_TV) && obj.is_hanging()) {rot_only_mat_mask |= 20;}

	building_obj_model_loader.draw_model(s, obj_center, obj, dir, obj.color, xlate, model_id, shadow_only,
		0, nullptr, rot_only_mat_mask, untextured, 0, upside_down, emissive_body_mat);
	if (!shadow_only && obj.type == TYPE_STOVE) {draw_stove_flames(obj, (camera_pdu.pos - xlate), s);} // draw blue burner flame
	if (use_low_z_bias    ) {s.add_uniform_float("norm_bias_scale", DEF_NORM_BIAS_SCALE);} // restore to the defaults
	if (emissive_first_mat) {s.set_color_e(BLACK);}
}

void brg_batch_draw_t::draw_obj_models(shader_t &s, vector3d const &xlate, bool shadow_only) const {
	for (obj_model_inst_with_obj_t const &i : models_to_draw) {
		point const obj_center(i.obj.get_cube_center());
		try_bind_tile_smap_at_point(obj_center, s);
		draw_obj_model(i, i.obj, s, xlate, obj_center, shadow_only);
	}
	if (!models_to_draw.empty()) {check_mvm_update();}
}

float get_ao_shadow(room_object_t const &c, bool enable_indir) {
	// include types that don't contribute to indir lighting; these always contribute AO shadows
	if (c.type == TYPE_BAR_STOOL || c.type == TYPE_SHELVES) return 0.25; // light shadow
	if (c.type == TYPE_OFF_CHAIR || c.type == TYPE_BENCH || c.type == TYPE_RCHAIR || c.type == TYPE_CASHREG) return 0.5; // medium shadow
	if (c.type == TYPE_PARK_SPACE && c.is_used()) return 0.75; // parked car; dense shadow
	if (enable_indir) return 0.0; // skip objects below because they're already handled by indir lighting

	if (c.type == TYPE_TABLE) {
		if (c.is_glass_table()) return 0.0; // no shadow
		return ((c.shape == SHAPE_TALL) ? 0.25 : 0.5); // small/medium shadow
	}
	if (c.type == TYPE_BED) {return ((c.taken_level > 2) ? 0.35 : 0.5);} // reduced AO when the mattress has been taken and light gets through the slats
	if (c.type == TYPE_BCASE || c.type == TYPE_DRESSER || c.type == TYPE_NIGHTSTAND || c.type == TYPE_COUCH) return 0.75; // dense shadow
	if (c.type == TYPE_SINK || c.type == TYPE_TOILET || c.type == TYPE_STALL) return 0.25; // light shadow
	if (c.type == TYPE_CHAIR || c.type == TYPE_DESK || c.type == TYPE_RDESK || c.type == TYPE_POOL_TABLE) return 0.5; // medium shadow
	return 0.0; // no shadow
}

// Note: non-const because it creates the VBO; inc_small: 0=large only, 1=large+small, 2=large+small+ext detail, 3=large+small+ext detail+int detail, 4=ext only
void building_room_geom_t::draw(brg_batch_draw_t *bbd, shader_t &s, shader_t &amask_shader, building_t const &building, occlusion_checker_noncity_t &oc,
	vector3d const &xlate, unsigned building_ix, bool shadow_only, bool reflection_pass, unsigned inc_small, bool player_in_building)
{
	if (empty()) return; // no geom
	unsigned const num_screenshot_tids(get_num_screenshot_tids());
	static int last_frame(0);
	static unsigned num_geom_this_frame(0); // used to limit per-frame geom gen time; doesn't apply to shadow pass, in case shadows are cached
	if (frame_counter < 100 || frame_counter > last_frame) {num_geom_this_frame = 0; last_frame = frame_counter;} // unlimited for the first 100 frames
	point const camera_bs(camera_pdu.pos - xlate);
	float const floor_spacing(building.get_window_vspace());
	bool const draw_ext_only(inc_small == 4);
	if (draw_ext_only) {inc_small = 0;}
	// don't draw ceiling lights when player is above the building unless there's a light placed on a skylight
	bool const draw_lights(!draw_ext_only && (camera_bs.z < building.bcube.z2() + (building.has_skylight_light ? 20.0*floor_spacing : 0.0)));
	// only parking garages and attics have detail objects that cast shadows
	bool const draw_detail_objs(inc_small >= 2 && (!shadow_only || building.has_parking_garage || building.has_attic()));
	bool const draw_int_detail_objs(inc_small >= 3 && !shadow_only);
	// update clocks if moved to next second; only applies to the player's building
	bool const update_clocks(player_in_building && inc_small >= 2 && !shadow_only && !reflection_pass && have_clock && check_clock_time());
	bool const player_in_doorway(building.point_near_ext_door(camera_bs, get_door_open_dist()));
	bool const player_in_building_or_doorway(player_in_building || player_in_doorway);
	if (bbd != nullptr) {bbd->set_camera_dir_mask(camera_bs, ((camera_bs.z < building.ground_floor_z1) ? building.get_bcube_inc_extensions() : building.bcube));}
	brg_batch_draw_t *const bbd_in(bbd); // capture bbd for instance drawing before setting to null if player_in_building
	if (player_in_building_or_doorway) {bbd = nullptr;} // use immediate drawing when player is in the building because draw order matters for alpha blending
	bool enable_indir(0);

	if (player_in_building && !shadow_only && !reflection_pass) { // indir lighting auto update logic
		static bool last_enable_indir(0);
		enable_indir = enable_building_indir_lighting_no_cib();
		if (enable_indir != last_enable_indir) {invalidate_mats_mask |= 0xFF;} // update all geom when material lighting changes due to indir
		last_enable_indir = enable_indir;
	}
	if (has_pictures && num_pic_tids != num_screenshot_tids) {
		invalidate_static_geom(); // user created a new screenshot texture, and this building has pictures - recreate room geom
		num_pic_tids = num_screenshot_tids;
	}
	if (update_clocks) {update_dynamic_draw_data();}
	check_invalid_draw_data();

	// generate vertex data in the shadow pass or if we haven't hit our generation limit; must be consistent for static and small geom
	// Note that the distance cutoff for mats_static and mats_small is different, so we generally won't be creating them both
	// unless the player just appeared by this building, or we need to update the geometry; in either case this is higher priority and we want to update both
	if (shadow_only || num_geom_this_frame < max(global_building_params.max_room_geom_gen_per_frame, 1U)) {
		if (!mats_static.valid) { // create static materials if needed
			create_obj_model_insts(building);
			create_static_vbos(building);
			if (!shadow_only) {++num_geom_this_frame;}
		}
		bool const create_small(inc_small && !mats_small.valid), create_text(draw_int_detail_objs && !mats_text.valid);
		//highres_timer_t timer("Create Small + Text VBOs", (create_small || create_text));

		// Note: it's not legal to use multiple threads for retail buildings because the shelf racks contain books, which draw text in the non-text pass;
		// this isn't thread safe due to text material and static text verts; text time should be small in this case, so threads are unlikely to help anyway
		if (create_small && create_text && !building.has_retail()) { // MT case
#pragma omp parallel num_threads(2)
			if (omp_get_thread_num_3dw() == 0) {create_small_static_vbos(building);} else {create_text_vbos();}
		}
		else { // serial case
			if (create_small) {create_small_static_vbos(building);}
			if (create_text ) {create_text_vbos        ();}
		}
		add_small_static_objs_to_verts(pending_objs, building.get_trim_color(), create_text);
		pending_objs.clear();

		// upload VBO data serially
		if (create_small) {
			mats_small.create_vbos(building);
			mats_amask.create_vbos(building);
		}
		if (create_text) {mats_text.create_vbos(building);}
		if (!shadow_only) {num_geom_this_frame += (unsigned(create_small) + unsigned(create_text));}

		// Note: not created on the shadow pass unless trim_objs has been created so that we don't miss including it;
		// the trim_objs test is needed to handle parking garage and attic objects, which are also drawn as details
		if (draw_detail_objs && (!shadow_only || !trim_objs.empty()) && !mats_detail.valid) { // create detail materials if needed (mats_detail and mats_ext_detail)
			create_detail_vbos(building);
			if (!shadow_only) {++num_geom_this_frame;}
		}
	}
	if (draw_lights && !mats_lights .valid) {create_lights_vbos (building);} // create lights  materials if needed (no limit)
	if (inc_small   && !mats_dynamic.valid) {create_dynamic_vbos(building, camera_bs, xlate, update_clocks);} // create dynamic materials if needed (no limit)
	if (!mats_doors.valid) {create_door_vbos(building);} // create door materials if needed (no limit)
	enable_blend(); // needed for rugs and book text
	assert(s.is_setup());
	if (!draw_ext_only) {mats_static .draw(bbd, s, shadow_only, reflection_pass);} // this is the slowest call
	if (draw_lights)    {mats_lights .draw(bbd, s, shadow_only, reflection_pass);}
	if (inc_small  )    {mats_dynamic.draw(bbd, s, shadow_only, reflection_pass);}
	if (inc_small >= 3) {mats_detail .draw(bbd, s, shadow_only, reflection_pass);} // now included in the shadow pass
	
	// draw exterior geom; shadows not supported; always use bbd;
	// skip in reflection pass because that control flow doesn't work and is probably not needed (except for L-shaped house?)
	if (!shadow_only && !reflection_pass && player_in_basement < 2) { // skip for player fully in the basement
		// is there a way to incrementally blend/dither this geometry in? it's not drawn in the correct order for alpha blending to work
		mats_exterior.draw(bbd_in, s, shadow_only, reflection_pass, 1); // exterior_geom=1
		if (draw_detail_objs) {mats_ext_detail.draw(bbd_in, s, shadow_only, reflection_pass, 1);} // exterior_geom=1
	}
	if (!draw_ext_only) {mats_doors.draw(bbd, s, shadow_only, reflection_pass);}
	if (inc_small     ) {mats_small.draw(bbd, s, shadow_only, reflection_pass);}

	if (!mats_amask.empty() && !draw_ext_only) { // draw plants, etc. using alpha masks in the detail pass
		if (shadow_only) {
			if (!amask_shader.is_setup()) {amask_shader.begin_simple_textured_shader(0.9);} // need to use texture with alpha test
			else {amask_shader.make_current();}
			mats_amask.draw(nullptr, amask_shader, 2, 0); // shadow pass with alpha mask; no brg_batch_draw
			s.make_current(); // switch back to the normal shader
		}
		else if (reflection_pass) {
			mats_amask.draw(nullptr, s, 0, 1); // no brg_batch_draw
		}
		// this is expensive: only enable for the main draw pass and skip for buildings the player isn't in
		else if (inc_small >= 2 && (player_in_building || !camera_in_building)) {
			// without the special shader these won't look correct when drawn through windows
			if (!amask_shader.is_setup()) {setup_building_draw_shader(amask_shader, 0.9, 1, 1, 0);} // min_alpha=0.9, enable_indir=1, force_tsl=1, use_texgen=0, water_damage=0.0
			else {amask_shader.make_current();}
			mats_amask.draw(nullptr, amask_shader, 0, 0); // no brg_batch_draw
			s.make_current(); // switch back to the normal shader
		}
	}
	if (draw_int_detail_objs && !shadow_only) {mats_text.draw(bbd, s, shadow_only, reflection_pass);} // text must be drawn last; drawn as interior detail objects
	disable_blend();
	indexed_vao_manager_with_shadow_t::post_render();
	if (draw_ext_only) return; // done
	bool const disable_cull_face(0); // better but slower?
	if (disable_cull_face) {glDisable(GL_CULL_FACE);}
	point const building_center(building.bcube.get_cube_center());
	oc.set_exclude_bix(building_ix);
	water_sound_manager_t water_sound_manager(camera_bs);
	rgeom_mat_t monitor_screens_mat, onscreen_text_mat=rgeom_mat_t(tid_nm_pair_t(FONT_TEXTURE_ID));
	string onscreen_text;
	bool const is_rotated(building.is_rotated()), is_residential(building.is_residential()), check_occlusion(display_mode & 0x08);
	bool const check_clip_cube(shadow_only && !is_rotated && !smap_light_clip_cube.is_all_zeros()); // check clip cube for shadow pass; not implemented for rotated buildings
	bool const skip_interior_objs(!player_in_building_or_doorway && !shadow_only), has_windows(building.has_windows());
	cube_t const clip_cube_bs(smap_light_clip_cube - xlate);
	// skip for rotated buildings and reflection pass, since reflected pos may be in a different room; should we use actual_player_pos for shadow_only mode?
	int const camera_room((is_rotated || reflection_pass) ? -1 : building.get_room_containing_pt(camera_bs));
	int camera_part(-1);
	unsigned last_room_ix(building.interior->rooms.size()), last_floor_ix(0); // start at an invalid value
	bool camera_in_closed_room(0), last_room_closed(0), obj_drawn(0);

	if (camera_room >= 0) { // check for stairs in case an object is visible through an open door on a floor above or below
		room_t const &room(building.get_room(camera_room));
		unsigned const camera_floor(room.get_floor_containing_zval(max(camera_bs.z, room.z1()), floor_spacing)); // clamp zval to room range
		camera_in_closed_room = (!room.has_stairs_on_floor(camera_floor) && building.all_room_int_doors_closed(camera_room, camera_bs.z));
		camera_part           = room.part_id;
	}
	// draw object models
	for (auto i = obj_model_insts.begin(); i != obj_model_insts.end(); ++i) {
		room_object_t &obj(get_room_object_by_index(i->obj_id));
		
		if (skip_interior_objs && obj.is_interior()) { // don't draw objects in interior rooms if the player is outside the building (useful for office bathrooms)
			// draw interior objects in residential buildings, such as plumbing fixtures visible through open interior bathroom doors, but not if in basement
			if (!is_residential || obj.z1() < building.ground_floor_z1) continue;
		}
		if (check_clip_cube && !clip_cube_bs.intersects(obj)) continue; // shadow map clip cube test: fast and high rejection ratio, do this first

		if (shadow_only) {
			if (obj.type == TYPE_CEIL_FAN) continue; // not shadow casting; would shadow its own light
			if (obj.is_exterior())         continue; // outdoors; no indoor shadow
			if (obj.type == TYPE_KEY || obj.type == TYPE_SILVER || obj.type == TYPE_FOLD_SHIRT) continue; // small
			if (obj.z1() > camera_bs.z)    continue; // above the light
			if (obj.z2() < camera_bs.z - 2.0*floor_spacing) continue; // more than two floors below the light
		}
		else if (!building.is_house && !has_windows) { // windowless building
			if (obj.z1() > camera_bs.z + floor_spacing || obj.z2() < camera_bs.z - 2.0*floor_spacing) continue; // more than one floor of difference
		}
		point obj_center(obj.get_cube_center());
		if (is_rotated) {building.do_xy_rotate(building_center, obj_center);}
		
		if (!shadow_only && obj.type != TYPE_FIRE_EXT) { // distance culling; allow fire extinguishers to be visible all the way down a long hallway
			float cull_dist(32.0*(obj.dx() + obj.dy() + obj.dz()));
			if (building.check_pt_in_retail_room(obj_center)) {cull_dist *= 2.5;} // increased culling distance for retail areas
			if (!dist_less_than(camera_bs, obj_center, cull_dist)) continue; // too far
		}
		if (!(is_rotated ? building.is_rot_cube_visible(obj, xlate) : camera_pdu.cube_visible(obj + xlate))) continue; // VFC
		if (check_occlusion && building.check_obj_occluded(obj, camera_bs, oc, reflection_pass)) continue;

		if (camera_room >= 0) {
			room_t const &room(building.get_room(obj.room_id));
			unsigned const floor_ix(room.get_floor_containing_zval(obj.zc(), floor_spacing));

			if (obj.room_id != last_room_ix || floor_ix != last_floor_ix) { // new room or new floor
				last_room_closed = building.all_room_int_doors_closed(obj.room_id, obj.zc());
				last_room_ix     = obj.room_id;
				last_floor_ix    = floor_ix;
			}
			// if either the camera or the object are in different rooms with closed doors,
			// on the same floor (not separated by stairs) of the same part (not visible across windows), then the object isn't visible
			if ((last_room_closed || camera_in_closed_room) && obj.room_id != camera_room && (room.part_id == camera_part || !has_windows)) continue;
		}
		apply_room_obj_rotate(obj, *i, objs); // Note: may modify obj by clearing flags and inst by updating dir
		
		if (bbd_in && !shadow_only && !is_rotated && (obj.type == TYPE_WALL_LAMP || obj.is_exterior())) { // not for rotated buildings
			// wall lamp has transparent glass and must be drawn last; fire escape and wall lamp use outdoor lighting
			bbd_in->models_to_draw.emplace_back(*i, obj);
		}
		else {
			//draw_simple_cube(obj); // TESTING
			draw_obj_model(*i, obj, s, xlate, obj_center, shadow_only); // draw now
			obj_drawn = 1;
		}
		// check for security camera monitor if player is in this building; must be on on, powered, and active
		if (player_in_building && obj.type == TYPE_MONITOR && !(obj.obj_id & 1) && obj.is_powered() && obj.is_active()) {
			onscreen_text.clear();
			setup_monitor_screen_draw(obj, monitor_screens_mat, onscreen_text);
			add_tv_or_monitor_screen (obj, monitor_screens_mat, onscreen_text, &onscreen_text_mat);
			s.set_color_e(WHITE); // emissive
			tid_nm_pair_dstate_t screen_state(s, 1), text_state(s, 0); // no_set_texture=1/0
			monitor_screens_mat.upload_draw_and_clear(screen_state);

			if (!onscreen_text_mat.empty()) {
				enable_blend();
				onscreen_text_mat.upload_draw_and_clear(text_state);
				disable_blend();
			}
			s.set_color_e(BLACK);
		}
		if (player_in_building && !shadow_only && obj.type == TYPE_SINK) { // sink
			water_sound_manager.register_running_water(obj, building);
			water_draw.add_water_for_sink(obj);
		}
	} // for i
	if (player_in_building) { // only drawn for the player building
		if (!shadow_only && !reflection_pass) { // these models aren't drawn in the shadow or reflection passes; no emissive or rotated objects
			for (auto i = model_objs.begin(); i != model_objs.end(); ++i) {
				point obj_center(i->get_cube_center());
				if (is_rotated) {building.do_xy_rotate(building_center, obj_center);}
				if (!shadow_only && !dist_less_than(camera_bs, obj_center, 100.0*i->max_len())) continue; // too far away
				if (!(is_rotated ? building.is_rot_cube_visible(*i, xlate) : camera_pdu.cube_visible(*i + xlate))) continue; // VFC
				if (check_occlusion && building.check_obj_occluded(*i, camera_bs, oc, reflection_pass)) continue;
				vector3d dir(i->get_dir());
				if (is_rotated) {building.do_xy_rotate_normal(dir);}
				building_obj_model_loader.draw_model(s, obj_center, *i, dir, i->color, xlate, i->get_model_id(), shadow_only, 0); // animations disabled
				obj_drawn = 1;
			} // for model_objs
		}
	}
	if (player_in_building_or_doorway) {
		bool const skip_animals(shadow_only && building.has_retail() && building.get_retail_part().contains_pt(camera_bs));
		if (!skip_animals) {draw_animals(s, building, oc, xlate, camera_bs, shadow_only, reflection_pass, check_clip_cube);}
	}
	if (disable_cull_face) {glEnable(GL_CULL_FACE);} // re-enable face culling
	if (obj_drawn) {check_mvm_update();} // needed after popping model transform matrix

	// draw water for sinks that are turned on, lava lamps, fish in fishtanks, and AO shadows; these aren't visible when the player is outside looking in through a window
	if (player_in_building_or_doorway && !shadow_only) {
		bool const draw_fish(!reflection_pass && have_fish_model() && (player_in_doorway || building.point_in_building_or_basement_bcube(camera_bs)));
		float const ao_z_off(1.1*building.get_flooring_thick()); // slightly above rugs and flooring
		float const ao_zmin(camera_bs.z - 2.0*floor_spacing);
		static quad_batch_draw ao_qbd;
		bool const inc_pools_and_fb(draw_fish && building.begin_fish_draw());
		auto objs_end(get_placed_objs_end()); // skip buttons/stairs/elevators
		point pts[4];

		for (auto i = objs.begin(); i != objs_end; ++i) {
			if (i->type == TYPE_KSINK || i->type == TYPE_BRSINK) { // TYPE_SINK is handled above
				water_sound_manager.register_running_water(*i, building);
				if (!i->is_active()) continue; // not turned on
				if (!(is_rotated ? building.is_rot_cube_visible(*i, xlate) : camera_pdu.cube_visible(*i + xlate))) continue; // VFC
				water_draw.add_water_for_sink(*i);
			}
			else if (i->type == TYPE_LAVALAMP) {
				if (!(is_rotated ? building.is_rot_cube_visible(*i, xlate) : camera_pdu.cube_visible(*i + xlate))) continue; // VFC
				lava_lamp_draw.add_lava_lamp(*i, camera_bs, building);
			}
			else if (i->type == TYPE_FISHTANK && draw_fish) {
				bool const visible(is_rotated ? building.is_rot_cube_visible(*i, xlate) : camera_pdu.cube_visible(*i + xlate));
				register_fishtank(*i, visible);
			}
			if (i->z1() < camera_bs.z && i->z1() > ao_zmin) { // camera not below or too far above this object
				float const ao_shadow(get_ao_shadow(*i, enable_indir));

				if (ao_shadow > 0.0) { // add AO shadow quad on the floor below the object
					if (!is_rotated && !camera_pdu.cube_visible(*i + xlate)) continue; // VFC - may not help much
					float rscale(0.5 + 0.5*(1.0 - ao_shadow)); // 0.5 will be the size of the object; dense shadow is sharper/smaller radius
					if (i->type == TYPE_CASHREG || i->type == TYPE_PARK_SPACE) {rscale *= 0.75;} // bcube is larger than it should be for cash registers and parked cars
					set_z_plane_rect_pts(point(i->xc(), i->yc(), (i->z1() + ao_z_off)), rscale*i->dx(), rscale*i->dy(), pts);

					if (is_rotated) {
						for (unsigned n = 0; n < 4; ++n) {building.do_xy_rotate(building_center, pts[n]);}
					}
					ao_qbd.add_quad_pts(pts, colorRGBA(0, 0, 0, ao_shadow), plus_z);
				}
			}
		} // for i
		for (person_t const &p : building.interior->people) {
			if (p.is_on_stairs) continue; // AO shadows may look wrong on stairs
			cube_t const pbc(p.get_bcube());
			if (pbc.z1() > camera_bs.z || pbc.z1() < ao_zmin) continue; // camera below or too far above, skip
			if (!is_rotated && !camera_pdu.cube_visible(pbc + xlate)) continue; // VFC - may not help much
			set_z_plane_rect_pts(point(p.pos.x, p.pos.y, (pbc.z1() + ao_z_off)), 0.4*p.radius, 0.4*p.radius, pts);
			ao_qbd.add_quad_pts(pts, colorRGBA(0, 0, 0, 0.4), plus_z);
		}
		// Note: animals are generally too small to have AO shadows
		lava_lamp_draw.draw_and_clear(s);
		if (!reflection_pass) {lava_lamp_draw.next_frame();}
		end_fish_draw(s, inc_pools_and_fb);
		draw_and_clear_blur_qbd(ao_qbd);
	}
	water_sound_manager.finalize();
	water_draw.draw_and_clear(s);

	if (player_in_building && !shadow_only && player_held_object.is_valid() && &building == player_building) {
		// draw the item the player is holding; actual_player_pos should be the correct position for reflections
		point const obj_pos((reflection_pass ? actual_player_pos : camera_bs) + CAMERA_RADIUS*cview_dir - vector3d(0.0, 0.0, 0.5*CAMERA_RADIUS));
		player_held_object.translate(obj_pos - player_held_object.get_cube_center());
		//unsigned room_id(building.get_room_containing_pt(obj_pos));
		//if (room_id >= 0) {player_held_object.room_id = room_id;} // is this necessary?
		if (is_ball_type(player_held_object.type)) {draw_ball_in_building(player_held_object, s);} // the only supported dynamic object type
		else if (player_held_object.can_use()) {draw_interactive_player_obj(player_held_object, s, xlate);}
		else {assert(0);}
	}
	// alpha blended, should be drawn near last
	decal_manager.draw_building_interior_decals(s, player_in_building_or_doorway, shadow_only); // draw decals in this building
	
	if (player_in_building && !shadow_only) {
		particle_manager.draw(s, xlate);
		fire_manager    .draw(s, xlate);
	}
	if (!shadow_only && !mats_alpha.empty()) { // draw last; not shadow casters; for shower glass, etc.
		enable_blend();
		glDepthMask(GL_FALSE); // disable depth writing
		mats_alpha.draw(bbd, s, shadow_only, reflection_pass);
		glDepthMask(GL_TRUE);
		disable_blend();
		indexed_vao_manager_with_shadow_t::post_render();
	}
}

void draw_billboards(quad_batch_draw &qbd, int tid, bool no_depth_write=1, bool do_blend=1) {
	if (qbd.empty()) return;
	if (no_depth_write) {glDepthMask(GL_FALSE);} // disable depth write
	if (do_blend) {enable_blend();}
	select_texture(tid);
	bind_default_flat_normal_map(); // no normal map
	qbd.draw_and_clear();
	if (do_blend) {disable_blend();}
	if (no_depth_write) {glDepthMask(GL_TRUE);}
}
void draw_emissive_billboards(quad_batch_draw &qbd, int tid) {
	if (qbd.empty()) return;
	shader_t s;
	s.begin_simple_textured_shader(); // unlit/emissive and textured
	set_additive_blend_mode();
	draw_billboards(qbd, tid);
	set_std_blend_mode();
}

class particle_texture_manager_t {
	string const fns[NUM_PART_EFFECTS] = {"", "", "", "", "water_splash.png", "white_circle.png"}; // none, sparks, clouds, smoke, splash, bubble
	int tids[NUM_PART_EFFECTS] = {-1, BLUR_CENT_TEX, BLUR_TEX, BLUR_TEX, -1, -1}; // none, sparks, clouds, smoke, splash, bubble
public:
	int get_tid(unsigned effect) {
		assert(effect < NUM_PART_EFFECTS);
		int &tid(tids[effect]);
		if (tid < 0 && !fns[effect].empty()) {tid = get_texture_by_name(fns[effect]);} // load if needed
		return tid;
	}
};
particle_texture_manager_t particle_texture_manager;

void particle_manager_t::draw(shader_t &s, vector3d const &xlate) { // non-const because qbd is modified
	if (particles.empty() || !camera_pdu.cube_visible(get_bcube() + xlate)) return; // no particles are visible
	point const viewer_bs(camera_pdu.pos - xlate);
	vector<sphere_t> bubbles;

	for (particle_t const &p : particles) {
		if (!camera_pdu.sphere_visible_test((p.pos + xlate), p.radius)) continue; // VFC
		vector3d const vdir(viewer_bs - p.pos), up_dir((p.vel == zero_vector) ? plus_z : p.vel);
		vector3d v1(cross_product(vdir, up_dir).get_norm()*p.radius);
		vector3d v2(cross_product(v1,   vdir  ).get_norm()*p.radius);
		assert(p.effect < NUM_PART_EFFECTS);
		if (p.effect == PART_EFFECT_SPARK) {v2 *= (1.0 + 1500.0*p.vel.mag());} // stretch in velocity dir
		if (p.effect == PART_EFFECT_BUBBLE) {bubbles.emplace_back(p.pos, p.radius);}
		else {qbds[p.effect].add_quad_dirs(p.pos, v1, v2, p.color, plus_z);} // use +z form the normal
	} // for p
	for (unsigned i = 0; i < NUM_PART_EFFECTS; ++i) {
		quad_batch_draw &qbd(qbds[i]);
		if (qbd.empty()) continue;
		int const tid(particle_texture_manager.get_tid(i));

		if (i == PART_EFFECT_SPARK) { // draw emissive particles with a custom shader
			draw_emissive_billboards(qbd, tid); // smooth alpha blended edges
			s.make_current();
		}
		else {draw_billboards(qbd, tid);} // no depth write, blend
	} // for i
	if (!bubbles.empty()) {
		// draw bubbles as spheres since billboards don't work well with the underwater postprocessing shader (due to depth and blend issues);
		// we can't make them transparent though because the underwater effect is run later and won't alpha blend properly
		// Note: can probably use instanced drawing here
		s.add_uniform_float("ambient_scale", 0.5);
		select_texture(WHITE_TEX);
		bind_default_flat_normal_map(); // no normal map
		s.set_cur_color(colorRGBA(0.6, 0.8, 1.0)); // blue-green tinted
		begin_sphere_draw(0); // untextured
		for (sphere_t const &b : bubbles) {draw_sphere_vbo(b.pos, b.radius, N_SPHERE_DIV, 0);} // textured=0
		end_sphere_draw();
		s.add_uniform_float("ambient_scale", building_ambient_scale); // reset
		check_mvm_update(); // needed after sphere drawing applies transforms
	}
}

void fire_manager_t::draw(shader_t &s, vector3d const &xlate) {
	if (fires.empty() || !camera_pdu.cube_visible(get_bcube() + xlate)) return; // no particles are visible
	point const viewer_bs(camera_pdu.pos - xlate);

	for (fire_t const &f : fires) {
		float const height(f.get_height());
		point const center(f.get_center());
		if (!camera_pdu.sphere_visible_test((center + xlate), max(f.radius, 0.5f*height))) continue; // VFC
		qbd.add_animated_billboard(center, viewer_bs, up_vector, WHITE, 1.5*f.radius, 0.5*height, fract(2.0f*f.time/TICKS_PER_SECOND));
	}
	draw_emissive_billboards(qbd, FIRE_TEX);
	s.make_current();
}

template<bool check_sz, typename T> bool are_pts_occluded_by_any_cubes(point const &pt, point const *const pts, unsigned npts,
	cube_t const &occ_area, T begin, T end, unsigned dim, float min_sz=0.0, float max_sep_dist=0.0)
{
	assert(npts > 0);

	for (auto c = begin; c != end; ++c) {
		if (check_sz && c->get_sz_dim(!dim) < min_sz) break; // too small an occluder; since cubes are sorted by size in this dim, we can exit the loop here
		if (dim <= 2 && (pt[dim] < c->d[dim][0]) == (pts[0][dim] < c->d[dim][0])) continue; // skip if cube face does not separate pt from the first point (dim > 2 disables)
		if (max_sep_dist > 0.0 && fabs(pt[dim] - c->get_center_dim(dim)) > max_sep_dist) continue; // check only one floor below/ceiling above
		if (!c->intersects(occ_area)) continue; // not between the object and viewer
		if (!check_line_clip(pt, pts[0], c->d)) continue; // first point does not intersect
		bool not_occluded(0);

		for (unsigned p = 1; p < npts; ++p) { // skip first point
			if (!check_line_clip(pt, pts[p], c->d)) {not_occluded = 1; break;}
		}
		if (!not_occluded) return 1;
	} // for c
	return 0;
}
template<bool check_sz, typename T> bool are_pts_occluded_by_any_cubes(point const &pt, point const *const pts, unsigned npts,
	cube_t const &occ_area, vector<T> const &cubes, unsigned dim, float min_sz=0.0, float max_sep_dist=0.0)
{
	return are_pts_occluded_by_any_cubes<check_sz>(pt, pts, npts, occ_area, cubes.begin(), cubes.end(), dim, min_sz, max_sep_dist);
}

car_t car_from_parking_space(room_object_t const &o) {
	rand_gen_t rgen;
	rgen.set_state(333*o.obj_id, o.obj_id+3);
	rgen.rand_mix();
	car_t car;
	car.dim     = o.dim;
	car.dir     = o.dir; // or random?
	car.cur_seg = o.obj_id; // store the random seed in car.cur_seg
	point center(o.get_cube_center());
	center[ o.dim] += 0.03*o.get_length()*rgen.signed_rand_float(); // small random misalign front/back
	center[!o.dim] += 0.05*o.get_width ()*rgen.signed_rand_float(); // small random misalign side
	car.set_bcube(point(center.x, center.y, o.z1()), get_nom_car_size());
	return car;
}
pair<cube_t, colorRGBA> car_bcube_color_from_parking_space(room_object_t const &o) {
	car_t car(car_from_parking_space(o));
	set_car_model_color(car);
	return make_pair(car.bcube, car.get_color());
}
bool check_cube_occluded(cube_t const &cube, vect_cube_t const &occluders, point const &viewer) {
	if (occluders.empty()) return 0;
	point pts[8];
	unsigned const npts(get_cube_corners(cube.d, pts, viewer, 0)); // should return only the 6 visible corners
	cube_t occ_area(cube);
	occ_area.union_with_pt(viewer); // any occluder must intersect this cube
	return are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, occluders, 3); // set invalid dim of 3 because cubes are of mixed dim and we can't use that optimization
}
struct comp_car_by_dist {
	vector3d const &viewer;
	comp_car_by_dist(vector3d const &viewer_) : viewer(viewer_) {}
	bool operator()(car_t const &c1, car_t const &c2) const {
		return (p2p_dist_xy_sq(c1.bcube.get_cube_center(), viewer) > p2p_dist_xy_sq(c2.bcube.get_cube_center(), viewer));
	}
};

bool building_t::has_cars_to_draw(bool player_in_building) const {
	if (!has_room_geom()) return 0;
	if (player_in_building && has_parking_garage) return 1; // parking garage cars are drawn if the player is in the building
	if (interior->room_geom->has_garage_car)      return 1; // have car in a garage
	return 0;
}
void building_t::draw_cars_in_building(shader_t &s, vector3d const &xlate, bool player_in_building, bool shadow_only) const {
	if (!has_room_geom()) return; // can get here in rare cases, maybe only shadow_only pass
	point viewer(camera_pdu.pos - xlate);
	bool const player_in_this_building(player_in_building && bcube.contains_pt(viewer)); // check before rotating
	bool const check_occlusion(display_mode & 0x08);
	float const floor_spacing(get_window_vspace());
	vect_room_object_t const &objs(interior->room_geom->objs);
	auto objs_end(interior->room_geom->get_placed_objs_end()); // skip buttons/stairs/elevators
	unsigned const pg_wall_start(interior->room_geom->wall_ps_start);
	assert(pg_wall_start < objs.size());
	static vector<car_t> cars_to_draw; // reused across frames
	cars_to_draw.clear();

	if (interior->room_geom->has_garage_car && player_in_basement < 2) { // car in a house garage
		room_object_t const &obj(objs[pg_wall_start]);
		assert(obj.type == TYPE_PARK_SPACE); // must be a parking space

		// skip if viewer is in this building and on a different floor
		if (obj.is_used() && (!player_in_this_building || !check_occlusion || !has_int_garage ||
			int((viewer.z - bcube.z1())/floor_spacing) == int((obj.z2() - bcube.z1())/floor_spacing)))
		{
			car_t car(car_from_parking_space(obj));
			if (camera_pdu.cube_visible(car.bcube + xlate)) {cars_to_draw.push_back(car);}
		}
	}
	else if (player_in_this_building && has_parking_garage) { // cars in parking garages
		// only draw if the player or light is in the basement, or the player is on the first floor where a car may be visible through the stairs
		float max_vis_zval(ground_floor_z1);
		if (!shadow_only) {max_vis_zval += floor_spacing;} // player on first floor?
		if (viewer.z > max_vis_zval) return;
		viewer = get_inv_rot_pos(viewer); // not needed because there are no cars in rotated buildings?
		vect_cube_t occluders; // should this be split out per PG level?

		// start at walls, since parking spaces are added after those; breaking is incorrect for multiple PG levels
		for (auto i = (objs.begin() + pg_wall_start); i != objs_end; ++i) {
			if (check_occlusion && i->type == TYPE_PG_WALL) {occluders.push_back(*i);}
			if (i->type != TYPE_PARK_SPACE || !i->is_used()) continue; // not a space, or no car in this space
			if (i->z2() < viewer.z - 2.0*floor_spacing)      continue; // move than a floor below - skip
			car_t car(car_from_parking_space(*i));
			if (!shadow_only && check_occlusion && viewer.z > ground_floor_z1 && !line_intersect_stairs_or_ramp(viewer, car.get_center())) continue;
			if (camera_pdu.cube_visible(car.bcube + xlate)) {cars_to_draw.push_back(car);}
		}
		if (cars_to_draw.empty()) return;

		if (check_occlusion) {
			// gather occluders from parking garage ceilings and floors (below ground floor)
			for (auto const &ceiling : interior->fc_occluders) {
				if (ceiling.z1() <= max_vis_zval) {occluders.push_back(ceiling);}
			}
			auto in(cars_to_draw.begin()), out(in);

			for (; in != cars_to_draw.end(); ++in) { // filter out occluded cars
				if (!check_cube_occluded(in->bcube, occluders, viewer)) {*(out++) = *in;}
			}
			cars_to_draw.erase(out, cars_to_draw.end());
		} // end check_occlusion
		std::sort(cars_to_draw.begin(), cars_to_draw.end(), comp_car_by_dist(viewer)); // required for correct window alpha blending
	}
	if (cars_to_draw.empty()) return;
	
	if (!s.is_setup()) { // caller didn't set up the shader
		if (shadow_only) {s.begin_shadow_map_shader();} // this path should be unused
		else {setup_building_draw_shader(s, 0.0, 1, 0, 0);} // min_alpha=0.0, enable_indir=1, force_tsl=0, use_texgen=0, water_damage=0.0
	}
	for (auto &car : cars_to_draw) {draw_car_in_pspace(car, s, xlate, shadow_only);}
	check_mvm_update(); // needed after popping model transform matrix
}

void append_line_pt(vector<vert_wrap_t> &line_pts, point const &pos) {
	if (line_pts.size() > 1) {line_pts.emplace_back(line_pts.back());} // duplicate point to create a line segment
	line_pts.emplace_back(pos);
}
void building_t::debug_people_in_building(shader_t &s) const {
	if (!has_people()) return;
	shader_t color_shader;
	color_shader.begin_color_only_shader(YELLOW);
	vector<vert_wrap_t> line_pts;

	for (person_t const &p : interior->people) {
		// use different colors if the path uses the nav grid or was shortened
		color_shader.set_cur_color(p.path.uses_nav_grid ? (p.path.is_shortened ? LT_BLUE : GREEN) : (p.path.is_shortened ? ORANGE : YELLOW));
		for (point const &v : p.path) {append_line_pt(line_pts, v);}
		if (p.target_valid ())        {append_line_pt(line_pts, p.target_pos);}
		if (!line_pts.empty())        {append_line_pt(line_pts, p.pos);} // add starting point if there's a valid path
		if (line_pts.size() > 1) {draw_verts(line_pts, GL_LINES);}
		line_pts.clear();
		for (point const &v : p.path) {draw_sphere_vbo(v, 0.25*p.radius, 16, 0);}

		if (p.target_valid()) {
			if      (p.goal_type == GOAL_TYPE_PLAYER) {color_shader.set_cur_color(RED   );}
			else if (p.goal_type == GOAL_TYPE_SOUND ) {color_shader.set_cur_color(PURPLE);}
			else if (p.goal_type == GOAL_TYPE_ROOM  ) {color_shader.set_cur_color(BLUE  );}
			draw_sphere_vbo(p.target_pos, 0.25*p.radius, 16, 0);
		}
	} // for p
	color_shader.end_shader();
	s.make_current();
}

// Note: c is in local building space and viewer_in is in non-rotated building space
bool building_t::check_obj_occluded(cube_t const &c, point const &viewer_in, occlusion_checker_noncity_t &oc, bool reflection_pass, bool c_is_building_part) const {
	if (!interior) return 0; // could probably make this an assert
	//highres_timer_t timer("Check Object Occlusion"); // 0.001ms
	point const viewer(get_inv_rot_pos(viewer_in)); // rotate viewer pos into building space
	bool const player_in_building(reflection_pass || point_in_building_or_basement_bcube(viewer)); // if reflection pass, assume the player is in this building
	bool const targ_in_basement(c.z2() <= ground_floor_z1);
	float const floor_spacing(get_window_vspace()), ground_floor_ceiling(ground_floor_z1 + floor_spacing);
	bool checked_conn_ret(0);
	
	if (targ_in_basement) { // fully inside basement
		if (viewer.z > ground_floor_ceiling) return 1; // viewer not on first floor
		
		if (!player_in_building) { // player not in this building
			checked_conn_ret = (camera_in_building && player_in_basement && interior_visible_from_other_building_ext_basement(oc.get_xlate(), oc.query_is_for_light));
			if (!checked_conn_ret) return 1;
		}
	}
	point pts[8];
	unsigned const npts(get_cube_corners(c.d, pts, viewer, 0)); // should return only the 6 visible corners
	cube_t occ_area(c);
	occ_area.union_with_pt(viewer); // any occluder must intersect this cube
	point const center(c.get_cube_center());
	vector3d const dir(viewer - center);
	bool const pri_dim(fabs(dir.x) < fabs(dir.y));
	
	if (!c_is_building_part && !reflection_pass) {
		// check walls of this building; not valid for reflections because the reflected camera may be on the other side of a wall/mirror
		if (targ_in_basement && is_pos_in_pg_or_backrooms(viewer)) { // object and pos are both in the parking garage or backrooms
			if (interior->has_backrooms) {
				// check for occlusion from the wall segments on either side of the extended basement door that separates it from the basement
				bool const dim(interior->extb_wall_dim), dir(interior->extb_wall_dir);
				door_t const &door(interior->get_ext_basement_door());
				cube_t wall(get_basement());
				min_eq(wall.z1(), interior->basement_ext_bcube.z1()); // cover both basement and ext basement in Z
				float const wall_pos(wall.d[dim][dir]);
				wall.d[dim][!dir] = wall_pos;
				wall.d[dim][ dir] = wall_pos + (dir ? 1.0 : -1.0)*get_wall_thickness(); // extend slightly into ext basement
				assert(wall.is_strictly_normalized());
				cube_t walls[2] = {wall, wall}; // lo, hi
				for (unsigned d = 0; d < 2; ++d) {walls[d].d[!dim][!d] = door.d[!dim][d];} // exclude the door
				if (are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, walls, walls+2, dim, 0.0)) return 1; // no size check
			}
			if (has_room_geom() && check_pg_br_wall_occlusion(viewer, pts, npts, occ_area, dir)) return 1;
		}
		else { // regular walls case
			for (unsigned D = 0; D < 2; ++D) {
				bool const d(bool(D) ^ pri_dim); // try primary dim first
				float const min_sz(0.5*c.get_sz_dim(!d)); // account for perspective
				if (are_pts_occluded_by_any_cubes<1>(viewer, pts, npts, occ_area, interior->walls[d], d, min_sz)) return 1; // with size check (helps with light bcubes)
			}
		}
	}
	if (!c_is_building_part && player_in_building) {
		// viewer inside this building; includes shadow_only case and reflection_pass (even if reflected camera is outside the building);
		// check floors/ceilings of this building
		if (fabs(viewer.z - c.zc()) > (reflection_pass ? 1.0 : 0.5)*floor_spacing) { // on different floors
			float max_sep_dist(floor_spacing);
			
			if (has_tall_retail()) { // handle ceilings more than one part tall
				cube_t const retail_part(get_retail_part());
				if (retail_part.contains_pt(viewer) || retail_part.contains_pt(center)) {max_sep_dist *= retail_floor_levels;}
			}
			if (are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, interior->fc_occluders, 2, 0.0, max_sep_dist)) return 1;
		}
	}
	else if (camera_in_building) { // player in some other building
		if (checked_conn_ret  ) return 0; // player in ext basement connected to this building; skip below checks in this case
		if (player_in_basement) return 1; // if player is in the basement of a different building, they probably can't see an object in this building
		if (player_in_windowless_building()) return 1; // player inside another windowless office building, objects in this building not visible
		if (is_rotated()) return 0; // not implemented yet - need to rotate viewer and pts into coordinate space of player_building

		if (player_building != nullptr && player_building->interior) { // check walls of the building the player is in
			if (player_building != this) { // otherwise player_in_this_building should be true; note that we can get here from building_t::add_room_lights()
				for (unsigned D = 0; D < 2; ++D) { // check walls of the building the player is in; can't use min_sz due to perspective effect of walls near the camera
					bool const d(bool(D) ^ pri_dim); // try primary dim first
					if (are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, player_building->interior->walls[d], d)) return 1;
				}
				if (fabs(viewer.z - c.zc()) > 0.5*floor_spacing) { // check floors and ceilings of the building the player is in
					if (are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, player_building->interior->fc_occluders, 2, 0.0, floor_spacing)) return 1;
				}
			}
		}
	}
	else if (viewer.z < bcube.z2()) { // player not in a building and not above this building
		if (is_rotated())                       return 0; // not implemented yet - c is not an axis aligned cube in global coordinate space
		if (has_windows() && oc.is_occluded(c)) return 1; // check other buildings; not needed for windowless buildings since they shouldn't be drawn (and wrong for walkways)
	}
	if (!c_is_building_part && viewer.z > ground_floor_ceiling && is_cube()) {
		// player above first floor of this building; check if object is occluded by a roof; we don't check bcube.z2() becase a lower part roof may be an occluder
		for (auto p = parts.begin(); p != get_real_parts_end(); ++p) {
			float const roof_z(p->z2());
			if (viewer.z < roof_z) continue; // viewer below the roof
			if (c.z2()   > roof_z) continue; // object above the roof
			if (p->contains_pt_xy(viewer) && p->contains_pt_xy(c.get_cube_center())) continue; // skip stacked parts case; should be handled by floor occluders
			cube_t roof(*p);
			roof.z1() = roof_z - get_fc_thickness();
			
			// check if on top floor of roof with a skylight, or on the stairs of the floor below; could split the roof in this case, but that may not make much of a difference
			if ((c.z2() > (roof_z - floor_spacing) || (c.z2() > (roof_z - 2.0f*floor_spacing) && check_cube_on_or_near_stairs(c))) && check_skylight_intersection(roof)) {
				continue;
			}
			bool not_occluded(0);

			for (unsigned p = 0; p < npts; ++p) {
				if (!check_line_clip(viewer, pts[p], roof.d)) {not_occluded = 1; break;}
			}
			if (!not_occluded) return 1;
		} // for p
	}
	if (check_pt_in_retail_room(center) && viewer.z > ground_floor_z1 && viewer.z < ground_floor_ceiling && has_room_geom()) {
		// both the object and the viewer are in the ground floor of a retail building - check shelf rack backs as occluders
		if (check_shelfrack_occlusion(viewer, pts, npts, occ_area)) return 1;
	}
	return 0;
}
bool building_t::check_pg_br_wall_occlusion(point const &viewer, point const *const pts, unsigned npts, cube_t const &occ_area, vector3d const &view_dir) const {
	bool const pri_dim(fabs(view_dir.x) < fabs(view_dir.y));
	index_pair_t start, end;
	get_pgbr_wall_ix_for_pos(viewer, start, end);
	// in cases where we clipped under the building the range will be empty and there will be no occlusion

	for (unsigned D = 0; D < 2; ++D) {
		bool const d(bool(D) ^ pri_dim); // try primary dim first
		vect_cube_t const &walls(interior->room_geom->pgbr_walls[d]);
		if (are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, walls.begin()+start.ix[d], walls.begin()+end.ix[d], d, 0.0)) return 1; // no size check
	}
	return 0;
}
bool building_t::check_shelfrack_occlusion(point const &viewer, point const *const pts, unsigned npts, cube_t const &occ_area) const {
	bool const long_dim(get_retail_long_dim());
	return are_pts_occluded_by_any_cubes<0>(viewer, pts, npts, occ_area, interior->room_geom->shelf_rack_occluders, !long_dim);
}

bool building_t::is_entire_building_occluded(point const &viewer, occlusion_checker_noncity_t &oc) const {
	if (is_rotated()) return 0; // not handled (optimization)

	for (auto i = parts.begin(); i != get_real_parts_end_inc_sec(); ++i) {
		if (has_basement() && (i - parts.begin()) == (int)basement_part_ix) continue; // skip the basement, which isn't visible from outside the building
		if (!check_obj_occluded(*i, viewer, oc, 0, 1)) return 0; // c_is_building_part=1
	}
	if (interior_visible_from_other_building_ext_basement(oc.get_xlate(), oc.query_is_for_light)) return 0;
	return 1; // all parts occluded
}

bool paint_draw_t::have_any_sp() const {
	for (unsigned i = 0; i <= NUM_SP_EMISSIVE_COLORS; ++i) {
		if (!sp_qbd[i].empty()) return 1;
	}
	return 0;
}
quad_batch_draw &paint_draw_t::get_paint_qbd(bool is_marker, unsigned emissive_color_id) {
	if (is_marker) return m_qbd;
	return sp_qbd[(emissive_color_id == 0) ? 0 : (1 + ((emissive_color_id-1)%NUM_SP_EMISSIVE_COLORS))]; // choose spraypaint emissive color
}
void paint_draw_t::draw_paint(shader_t &s) const {
	bool const have_sp(have_any_sp());
	if (!have_sp && m_qbd.empty()) return; // nothing to do
	glDepthMask(GL_FALSE); // disable depth write
	enable_blend();
	bind_default_flat_normal_map(); // no normal map

	if (have_sp) {
		select_texture(BLUR_CENT_TEX); // spraypaint - smooth alpha blended edges

		for (unsigned i = 0; i <= NUM_SP_EMISSIVE_COLORS; ++i) {
			quad_batch_draw const &qbd(sp_qbd[i]);
			if (qbd.empty()) continue;
			if (i > 0) {s.set_color_e(sp_emissive_colors[i-1]);}
			qbd.draw();
			if (i > 0) {s.clear_color_e();}
		} // for i
	}
	if (!m_qbd.empty()) {
		select_texture(get_texture_by_name("circle.png", 0, 0, 1, 0.0, 1, 1, 1)); // markers - sharp edges, used as alpha mask with white background color
		m_qbd.draw();
	}
	disable_blend();
	glDepthMask(GL_TRUE);
}
void paint_draw_t::clear() {
	for (unsigned i = 0; i <= NUM_SP_EMISSIVE_COLORS; ++i) {sp_qbd[i].clear();}
	m_qbd.clear();
}

void building_decal_manager_t::commit_pend_tape_qbd() {
	pend_tape_qbd.add_quads(tape_qbd);
	pend_tape_qbd.clear();
}
void building_decal_manager_t::add_burn_spot(point const &pos, float radius) {
	// if there are too many existing spots, remove the first (oldest) one; this can be slow, but shouldn't happen very often
	unsigned const max_spots = 100;
	unsigned const num_spots(burn_qbd.verts.size()/6); // 6 verts/2 triangles per quad
	if (num_spots >= max_spots) {burn_qbd.verts.erase(burn_qbd.verts.begin(), (burn_qbd.verts.begin() + 6*(num_spots - max_spots + 1)));}
	burn_qbd.add_quad_dirs(pos, -plus_x*radius, plus_y*radius, BLACK); // -x!
}
void building_decal_manager_t::add_blood_or_stain(point const &pos, float radius, colorRGBA const &color, bool is_blood, unsigned dim, bool dir) {
	assert(dim <= 2);
	tex_range_t tex_range(tex_range_t::from_atlas((rgen.rand()&1), (rgen.rand()&1), 2, 2)); // 2x2 texture atlas
	tex_range.swap_xy = rgen.rand_bool();
	vector3d ds, dt, dn;
	dn[dim] = (dir ? 1.0 : -1.0);
	ds[(dim+1)%3] = -1.0;
	dt[(dim+2)%3] =  1.0;
	if (!dir) {swap(ds, dt);} // use correct winding order
	blood_qbd[!is_blood].add_quad_dirs(pos, ds*radius, dt*radius, color, dn, tex_range); // -x!
}
void building_decal_manager_t::draw_building_interior_decals(shader_t &s, bool player_in_building, bool shadow_only) const {
	if (shadow_only) { // shadow pass, draw tape only
		if (player_in_building) {
			tape_qbd.draw(); // somewhat inefficient, since we have to send all the data for every light source
			pend_tape_qbd.draw();
		}
		return;
	}
	paint_draw[1].draw_paint(s); // draw exterior paint always - this will show up on windows (even when looking outside into another part of the same building)
	if (!player_in_building) return;
	paint_draw[0].draw_paint(s); // draw interior paint

	if (!tp_qbd.empty()) { // toilet paper squares: double sided, lit from top
		glDisable(GL_CULL_FACE); // draw both sides
		select_texture(WHITE_TEX);
		select_texture(get_toilet_paper_nm_id(), 5); // apply normal map
		tp_qbd.draw(); // use a VBO for this if the player leaves the building and then comes back?
		bind_default_flat_normal_map(); // no normal map
		glEnable(GL_CULL_FACE);
	}
	if (!tape_qbd.empty() || !pend_tape_qbd.empty()) { // tape lines: single sided so that lighting works, both sides drawn independently
		select_texture(WHITE_TEX);
		tape_qbd.draw();
		pend_tape_qbd.draw();
	}
	if (!blood_qbd[0].empty() || !blood_qbd[1].empty() || !glass_qbd.empty() || !burn_qbd.empty()) { // draw alpha blended decals
		glDepthMask(GL_FALSE); // disable depth write
		enable_blend();
		int const blood_tids[2] = {BLOOD_SPLAT_TEX, get_texture_by_name("atlas/blood_white.png")};

		for (unsigned i = 0; i < 2; ++i) {
			if (blood_qbd[i].empty()) continue;
			select_texture(blood_tids[i]);
			blood_qbd[i].draw();
		}
		if (!glass_qbd.empty()) {
			select_texture(get_texture_by_name("interiors/broken_glass.png"));
			glass_qbd.draw();
		}
		if (!burn_qbd.empty()) {
			select_texture(BLUR_CENT_TEX);
			burn_qbd.draw();
		}
		disable_blend();
		glDepthMask(GL_TRUE);
	}
}


