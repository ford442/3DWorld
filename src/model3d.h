// 3D World - 3D Model Rendering Classes
// by Frank Gennari
// 8/17/11
#pragma once

#include "3DWorld.h"
#include "collision_detect.h" // for polygon_t
#include "cobj_bsp_tree.h" // for cobj_tree_tquads_t
#include "shadow_map.h" // for smap_data_t and rotation_t
#include "gl_ext_arb.h"
//#include <unordered_map>

using namespace std;

typedef map<string, unsigned> string_map_t;

unsigned const MAX_VMAP_SIZE     = (1 << 18); // 256K
unsigned const BUILTIN_TID_START = (1 << 16); // 65K
float const POLY_COPLANAR_THRESH = 0.98;


struct geom_xform_t { // should be packed, can read/write as POD

	vector3d tv;
	float scale;
	bool mirror[3], swap_dim[3][3];

	geom_xform_t(vector3d const &tv_=zero_vector, float scale_=1.0) : tv(tv_), scale(scale_) {
		restore_mirror_and_swap();
	}
	void restore_mirror_and_swap() {
		for (unsigned i = 0; i < 3; ++i) {
			UNROLL_3X(swap_dim[i][i_] = 0;)
			mirror[i] = 0;
		}
	}
	void xform_pos_rm(point &pos) const {
		UNROLL_3X(if (mirror[i_]) {pos[i_] = -pos[i_];})
		
		for (unsigned i = 0; i < 3; ++i) {
			UNROLL_3X(if (swap_dim[i][i_]) {swap(pos[i], pos[i_]);})
		}
	}
	void inv_xform_pos_rm(point &pos) const { // Note: unused/untested
		for (unsigned i = 0; i < 3; ++i) {
			UNROLL_3X(if (swap_dim[2-i][2-i_]) {swap(pos[2-i], pos[2-i_]);})
		}
		UNROLL_3X(if (mirror[i_]) {pos[i_] = -pos[i_];}) // order-independent, inverse has no effect
	}
	void xform_pos_rms(point &pos) const {
		xform_pos_rm(pos);
		pos *= scale;
	}
	void inv_xform_pos_rms(point &pos) const {
		assert(scale != 0.0);
		pos /= scale;
		inv_xform_pos_rm(pos);
	}
	void xform_pos(point &pos) const { // rotate, mirror, scale, translate
		xform_pos_rms(pos);
		pos += tv;
	}
	void inv_xform_pos(point &pos) const {
		pos -= tv;
		inv_xform_pos_rms(pos);
	}
	cube_t get_xformed_cube_ts(cube_t const &cube) const {return cube*scale + tv;} // Note: RM ignored
	void xform_vect(vector<point> &v) const {
		for (vector<point>::iterator i = v.begin(); i != v.end(); ++i) {xform_pos(*i);}
	}
	bool operator==(geom_xform_t const &x) const;
	bool operator!=(geom_xform_t const &x) const {return !operator==(x);}
};


struct model3d_xform_t : public geom_xform_t, public rotation_t { // should be packed, can read/write as POD

	base_mat_t material;
	int group_cobjs_level;
	float voxel_spacing;
	cube_t bcube_xf;

	model3d_xform_t(vector3d const &tv_=zero_vector, float scale_=1.0) : geom_xform_t(tv_, scale_), group_cobjs_level(0.0), voxel_spacing(0.0), bcube_xf(all_zeros) {}
	model3d_xform_t(geom_xform_t const &xf) : geom_xform_t(xf), group_cobjs_level(0.0), voxel_spacing(0.0), bcube_xf(all_zeros) {}
	cube_t get_xformed_cube(cube_t const &cube) const;
	cube_t const &get_xformed_bcube(cube_t const &bcube);
	void clear_bcube() {bcube_xf.set_to_zeros();}
	void apply_inv_xform_to_pdu(pos_dir_up &pdu) const;
	void apply_to_tquad(coll_tquad &tquad) const;
	void apply_gl() const;

	bool eq_xforms(model3d_xform_t const &x) const {return (rotation_t::operator==(x) && geom_xform_t::operator==(x));}
	bool operator==(model3d_xform_t const &x) const {return (eq_xforms(x) && material == x.material);}
	bool operator!=(model3d_xform_t const &x) const {return !operator==(x);}
	bool is_identity() const {return eq_xforms(model3d_xform_t());}

	void xform_pos(point &pos) const { // rotate, mirror, scale, arb_rotate, translate
		xform_pos_rms(pos);
		rotate_point(pos, -1.0); // negative rotate?
		pos += tv;
	}
	void inv_xform_pos(point &pos) const {
		pos -= tv;
		rotate_point(pos, 1.0); // negative rotate?
		inv_xform_pos_rms(pos);
	}
	void apply_material_override(base_mat_t &mat) const {
		if (material.tid >= 0) {mat.tid = material.tid;}
		if (material.shine > 0.0) {mat.shine = material.shine;}
		if (material.color != ALPHA0) {mat.color = material.color;}
		if (material.spec_color != BLACK) {mat.spec_color = material.spec_color;}
	}
};


struct vntc_ix_t {
	unsigned vix, nix, tix;
	vntc_ix_t(unsigned vix_=0, unsigned nix_=0, unsigned tix_=0) : vix(vix_), nix(nix_), tix(tix_) {}
};


struct poly_header_t {
	unsigned npts, obj_id;
	int mat_id;
	vector3d n;

	poly_header_t(int mat_id_=-1, unsigned obj_id_=0) : npts(0), obj_id(obj_id_), mat_id(mat_id_), n(zero_vector) {}
};


struct poly_data_block {
	vector<poly_header_t> polys;
	vector<vntc_ix_t> pts;
};


struct model3d_stats_t {
	unsigned verts, quads, tris, blocks, mats, transforms;
	model3d_stats_t() : verts(0), quads(0), tris(0), blocks(0), mats(0), transforms(0) {}
	void print() const;
};


// for computing vertex normals from face normals
struct counted_normal : public vector3d { // size = 16

	unsigned count;

	counted_normal() : vector3d(zero_vector), count(0) {}
	counted_normal(vector3d const &n) : vector3d(n), count(1) {}
	void add_normal(vector3d const &n) {*this += n; ++count;}
	bool is_valid() const {return (count > 0);}
};

// unused
struct weighted_normal : public vector3d { // size = 16

	float weight;

	weighted_normal() : vector3d(zero_vector), weight(0.0) {}
	weighted_normal(vector3d const &n, float w=1.0) : vector3d(w*n), weight(w) {}
	void add_normal(vector3d const &n, float w=1.0) {*this += w*n; weight += w;}
	void add_normal(vector3d const &n, point const *const poly_pts, unsigned npts) {add_normal(n, polygon_area(poly_pts, npts));}
	bool is_valid() const {return (weight > 0.0);}
};

//template<typename T> class vertex_map_t : public unordered_map<T, unsigned, hash_by_bytes<T>> {
template<typename T> class vertex_map_t : public map<T, unsigned> {

	int last_mat_id;
	unsigned last_obj_id;
	bool average_normals;

public:
	vertex_map_t(bool average_normals_=0) : last_mat_id(-1), last_obj_id(0), average_normals(average_normals_) {}
	bool get_average_normals() const {return average_normals;}
	
	void check_for_clear(int mat_id) {
		if (mat_id != last_mat_id || this->size() >= MAX_VMAP_SIZE) {
			last_mat_id = mat_id;
			this->clear();
		}
	}
};

typedef vertex_map_t<vert_norm_tc> vntc_map_t;
typedef vertex_map_t<vert_norm_tc_tan> vntct_map_t;


struct get_polygon_args_t {
	vector<coll_tquad> &polygons;
	colorRGBA color;
	bool quads_only;
	unsigned lod_level;

	get_polygon_args_t(vector<coll_tquad> &polygons_, colorRGBA const &color_, bool quads_only_=0, unsigned lod_level_=0)
		: polygons(polygons_), color(color_), quads_only(quads_only_), lod_level(lod_level_) {}
};


template<typename T> cube_t get_polygon_bbox(vector<T> const &p) {
	if (p.empty()) return all_zeros_cube;
	cube_t bbox(p.front().v, p.front().v);
	for (unsigned i = 1; i < p.size(); ++i) {bbox.union_with_pt(p[i].v);}
	return bbox;
}


template<typename T> class vntc_vect_t : public vector<T>, public indexed_vao_manager_with_shadow_t {

protected:
	bool has_tangents, finalized;
	sphere_t bsphere;
	cube_t bcube;

public:
	using vector<T>::empty;
	using vector<T>::size;
	unsigned obj_id;

	vntc_vect_t(unsigned obj_id_=0) : has_tangents(0), finalized(0), obj_id(obj_id_) {bcube.set_to_zeros();}
	void clear();
	void make_private_copy() {vbo = ivbo = 0;} // Note: to be called *only* after a deep copy
	void calc_bounding_volumes();
	void ensure_bounding_volumes() {if (bsphere.radius == 0.0) {calc_bounding_volumes();}}
	cube_t get_bcube () const {return get_polygon_bbox(*this);}
	point get_center () const {return bsphere.pos;}
	float get_bradius() const {return bsphere.radius;}
	unsigned get_gpu_mem() const {return (vbo_valid() ? size()*sizeof(T) : 0);}
	void optimize(unsigned npts) {remove_excess_cap();}
	void remove_excess_cap() {if (20*vector<T>::size() < 19*vector<T>::capacity()) {vector<T>::shrink_to_fit();}}
	void write(ostream &out) const;
	void read(istream &in);
};


template<typename T> class indexed_vntc_vect_t : public vntc_vect_t<T> {

public:
	vector<unsigned> indices; // needs to be public for merging operation
private:
	bool need_normalize, optimized, prev_ucc;
	float avg_area_per_tri, amin, amax;

	struct geom_block_t {
		unsigned start_ix, num;
		cube_t bcube;
		geom_block_t() : start_ix(0), num(0) {}
		geom_block_t(unsigned s, unsigned n, cube_t const &bc) : start_ix(s), num(n), bcube(bc) {}
	};
	vector<geom_block_t> blocks;

	struct lod_block_t {
		unsigned start_ix, num;
		float tri_area;
		lod_block_t() : start_ix(0), num(0), tri_area(0.0) {}
		lod_block_t(unsigned s, unsigned n, float a) : start_ix(s), num(n), tri_area(a) {}
		unsigned get_end_ix() const {return (start_ix + num);}
	};
	vector<lod_block_t> lod_blocks;
	unsigned get_block_ix(float area) const;

public:
	using vntc_vect_t<T>::size;
	using vntc_vect_t<T>::empty;
	using vntc_vect_t<T>::begin;
	using vntc_vect_t<T>::end;
	using vntc_vect_t<T>::at;
	using vntc_vect_t<T>::operator[];
	using vntc_vect_t<T>::finalized;
	using vntc_vect_t<T>::bcube;
	using vntc_vect_t<T>::bsphere;
	
	indexed_vntc_vect_t(unsigned obj_id_=0) : vntc_vect_t<T>(obj_id_), need_normalize(0), optimized(0), prev_ucc(0), avg_area_per_tri(0.0), amin(0.0), amax(0.0) {}
	void calc_tangents(unsigned npts) {assert(0);}
	void render(shader_t &shader, bool is_shadow_pass, point const *const xlate, unsigned npts, bool no_vfc=0);
	void reserve_for_num_verts(unsigned num_verts);
	void add_poly(polygon_t const &poly, vertex_map_t<T> &vmap);
	void add_triangle(triangle const &t, vertex_map_t<T> &vmap);
	unsigned add_vertex(T const &v, vertex_map_t<T> &vmap);
	void add_index(unsigned ix) {assert(ix < size()); indices.push_back(ix);}
	void subdiv_recur(vector<unsigned> const &ixs, unsigned npts, unsigned skip_dims, cube_t *bcube_in=nullptr);
	void optimize(unsigned npts);
	void gen_lod_blocks(unsigned npts);
	void finalize(unsigned npts);
	void finalize_lod_blocks(unsigned npts);
	void simplify(vector<unsigned> &out, float target) const;
	void simplify_meshoptimizer(vector<unsigned> &out, float target) const;
	void simplify_indices(float reduce_target);
	void clear();
	void clear_blocks() {blocks.clear(); lod_blocks.clear();}
	unsigned num_verts() const {return unsigned(indices.empty() ? size() : indices.size());}
	T       &get_vert(unsigned i)       {return (*this)[indices.empty() ? i : indices[i]];}
	T const &get_vert(unsigned i) const {return (*this)[indices.empty() ? i : indices[i]];}
	unsigned get_ix  (unsigned i) const {assert(i < indices.size()); return indices[i];}
	float get_prim_area(unsigned i, unsigned npts) const;
	float calc_area(unsigned npts);
	void get_polygons(get_polygon_args_t &args, unsigned npts) const;
	unsigned get_gpu_mem() const {return (vntc_vect_t<T>::get_gpu_mem() + (this->ivbo_valid() ? indices.size()*sizeof(unsigned) : 0));}
	void invert_tcy();
	void write(ostream &out) const;
	void read(istream &in, unsigned npts);
	bool indexing_enabled() const {return !indices.empty();}
	void mark_need_normalize() {need_normalize = 1;}
};


template<typename T> struct vntc_vect_block_t : public deque<indexed_vntc_vect_t<T> > {

	using deque<indexed_vntc_vect_t<T> >::begin;
	using deque<indexed_vntc_vect_t<T> >::end;
	
	void finalize(unsigned npts);
	void clear() {free_vbos(); deque<indexed_vntc_vect_t<T> >::clear();}
	void free_vbos();
	cube_t get_bcube() const;
	unsigned get_gpu_mem() const;
	float calc_draw_order_score() const;
	unsigned num_verts() const;
	unsigned num_unique_verts() const;
	void get_stats(model3d_stats_t &stats) const {stats.blocks += (unsigned)this->size(); stats.verts += num_unique_verts();}
	float calc_area(unsigned npts);
	void get_polygons(get_polygon_args_t &args, unsigned npts) const;
	void invert_tcy();
	void simplify_indices(float reduce_target);
	void merge_into_single_vector();
	bool write(ostream &out) const;
	bool read(istream &in, unsigned npts);
};


template<typename T> struct geometry_t {

	vntc_vect_block_t<T> triangles, quads;

	void calc_tangents_blocks(vntc_vect_block_t<T> &blocks, unsigned npts) {assert(0);}
	void calc_tangents();
	void render_blocks(shader_t &shader, bool is_shadow_pass, point const *const xlate, vntc_vect_block_t<T> &blocks, unsigned npts);
	void render(shader_t &shader, bool is_shadow_pass, point const *const xlate);
	bool empty() const {return (triangles.empty() && quads.empty());}
	unsigned get_gpu_mem() const {return (triangles.get_gpu_mem() + quads.get_gpu_mem());}
	void add_poly_to_polys(polygon_t const &poly, vntc_vect_block_t<T> &v, vertex_map_t<T> &vmap, unsigned obj_id=0) const;
	void add_poly(polygon_t const &poly, vertex_map_t<T> vmap[2], unsigned obj_id=0);
	void get_polygons(get_polygon_args_t &args) const;
	cube_t get_bcube() const;
	void invert_tcy() {triangles.invert_tcy(); quads.invert_tcy();}
	void finalize  () {triangles.finalize(3); quads.finalize(4);}
	void free_vbos () {triangles.free_vbos(); quads.free_vbos();}
	void clear();
	void get_stats(model3d_stats_t &stats) const;
	void calc_area(float &area, unsigned &ntris);
	void simplify_indices(float reduce_target);
	bool write(ostream &out) const {return (triangles.write(out)  && quads.write(out)) ;}
	bool read(istream &in)         {return (triangles.read(in, 3) && quads.read(in, 4));}
};


class texture_manager {
	struct tex_work_item_t {
		unsigned tid;
		bool is_nm;
		tex_work_item_t(unsigned tid_, bool is_nm_) : tid(tid_), is_nm(is_nm_) {}
		// we really shouldn't have the same tid with different is_nm; should we just ingore is_nm when comparing
		bool operator< (tex_work_item_t const &w) const {return ((tid == w.tid) ? (is_nm < w.is_nm) : (tid < w.tid));}
		bool operator==(tex_work_item_t const &w) const {return (tid == w.tid && is_nm == w.is_nm);}
	};
protected:
	deque<texture_t> textures;
	string_map_t tex_map; // maps texture filenames to texture indexes
	vector<tex_work_item_t> to_load;
public:
	unsigned create_texture(string const &fn, bool is_alpha_mask, bool verbose, bool invert_alpha=0, bool wrap=1, bool mirror=0, bool force_grayscale=0);
	bool empty() const {return textures.empty();}
	void clear();
	void free_tids();
	void free_textures();
	void free_client_mem();
	bool ensure_texture_loaded(int tid, bool is_bump);
	void bind_alpha_channel_to_texture(int tid, int alpha_tid);
	bool ensure_tid_loaded(int tid, bool is_bump) {return ((tid >= 0) ? ensure_texture_loaded(tid, is_bump) : 0);}
	void ensure_tid_bound(int tid) {if (tid >= 0) {get_texture(tid).check_init();}} // if allocated
	void add_work_item(int tid, bool is_nm);
	void load_work_items_mt();
	void bind_texture(int tid) const {get_texture(tid).bind_gl();}
	colorRGBA get_tex_avg_color(int tid) const {return get_texture(tid).get_avg_color();}
	bool has_binary_alpha(int tid) const {return get_texture(tid).has_binary_alpha;}
	bool might_have_alpha_comp(int tid) const {return (tid >= 0 && get_texture(tid).ncolors == 4);}
	texture_t const &get_texture(int tid) const;
	texture_t &get_texture(int tid);
	unsigned get_cpu_mem() const;
	unsigned get_gpu_mem() const;
};


struct material_params_t { // Warning: changing this struct will invalidate the model3d file format

	colorRGB ka, kd, ks, ke, tf;
	float ns, ni, alpha, tr;
	unsigned illum;
	bool skip, is_used, unused1, unused2; // unused bools to pad the struct

	material_params_t() : ka(WHITE), kd(WHITE), ks(BLACK), ke(BLACK), tf(WHITE), ns(1.0), ni(1.0),
		alpha(1.0), tr(0.0), illum(2), skip(0), is_used(0), unused1(0), unused2(0) {}
}; // must be padded


struct material_t : public material_params_t {

	bool might_have_alpha_comp, tcs_checked;
	int a_tid, d_tid, s_tid, ns_tid, alpha_tid, bump_tid, refl_tid;
	float draw_order_score, avg_area_per_tri;
	float metalness; // < 0 disables; should go into material_params_t, but that would invalidate the model3d file format
	string name, filename;

	geometry_t<vert_norm_tc> geom;
	geometry_t<vert_norm_tc_tan> geom_tan;

	material_t(string const &name_=string(), string const &fn=string())
		: might_have_alpha_comp(0), tcs_checked(0), a_tid(-1), d_tid(-1), s_tid(-1), ns_tid(-1), alpha_tid(-1), bump_tid(-1), refl_tid(-1),
		draw_order_score(0.0), avg_area_per_tri(0.0), metalness(-1.0), name(name_), filename(fn) {}
	bool add_poly(polygon_t const &poly, vntc_map_t vmap[2], vntct_map_t vmap_tan[2], unsigned obj_id=0);
	void mark_as_used() {is_used = 1;}
	bool mat_is_used () const {return is_used;}
	bool use_bump_map() const;
	bool use_spec_map() const;
	unsigned get_gpu_mem() const {return (geom.get_gpu_mem() + geom_tan.get_gpu_mem());}
	void finalize() {geom.finalize(); geom_tan.finalize();}
	int get_render_texture() const {return ((d_tid >= 0 || a_tid < 0) ? d_tid : a_tid);} // return diffuse texture unless ambient texture is specified but diffuse texture is not
	bool get_needs_alpha_test() const {return (alpha_tid >= 0 || might_have_alpha_comp);}
	bool is_partial_transparent() const {return (alpha < 1.0 || get_needs_alpha_test());}
	void compute_area_per_tri();
	void simplify_indices(float reduce_target);
	void ensure_textures_loaded(texture_manager &tmgr);
	void init_textures(texture_manager &tmgr);
	void queue_textures_to_load(texture_manager &tmgr);
	void check_for_tc_invert_y(texture_manager &tmgr);
	void render(shader_t &shader, texture_manager const &tmgr, int default_tid, bool is_shadow_pass, bool is_z_prepass,
		int enable_alpha_mask, bool is_bmap_pass, point const *const xlate);
	colorRGBA get_ad_color() const;
	colorRGBA get_avg_color(texture_manager const &tmgr, int default_tid=-1) const;
	bool write(ostream &out) const;
	bool read(istream &in);
};


struct voxel_params_t; // forward declaration
class voxel_manager; // forward declaration

class model3d {

	// read/write options
	string filename;
	int recalc_normals, group_cobjs_level;

	// geometry
	geometry_t<vert_norm_tc> unbound_geom;
	base_mat_t unbound_mat;
	vector<polygon_t> split_polygons_buffer;
	cube_t bcube, bcube_all_xf, occlusion_cube;
	unsigned model_refl_tid, model_refl_tsize, model_refl_last_tsize, model_indir_tid;
	int reflective; // reflective: 0=none, 1=planar, 2=cube map
	int indoors; // 0=no/outdoors, 1=yes/indoors, 2=unknown
	bool from_model3d_file, has_cobjs, needs_alpha_test, needs_bump_maps, has_spec_maps, has_gloss_maps, xform_zvals_set, needs_trans_pass;
	float metalness; // should be per-material, but not part of the material file and specified per-object instead

	// materials
	deque<material_t> materials;
	string_map_t mat_map; // maps material names to materials indexes
	set<string> undef_materials; // to reduce warning messages
	cobj_tree_tquads_t coll_tree;
	bool textures_loaded;

	// transforms
	vector<model3d_xform_t> transforms;

	// shadows
	struct model_smap_data_t : public smap_data_t {
		model3d *model;

		model_smap_data_t(unsigned tu_id_, unsigned smap_sz_, model3d *model_) : smap_data_t(tu_id_, smap_sz_), model(model_) {assert(model);}
		virtual void render_scene_shadow_pass(point const &lpos);
		//virtual bool needs_update(point const &lpos);
	};
	typedef vect_smap_t<model_smap_data_t> per_model_smap_data;
	map<rotation_t, per_model_smap_data> smap_data;

	// lighting
	string sky_lighting_fn;
	unsigned sky_lighting_sz[3];
	float sky_lighting_weight;
	//lmap_manager_t local_lmap_manager;

	// temporaries to be reused
	vector<pair<float, unsigned> > to_draw, to_draw_xf;

	void update_bbox(polygon_t const &poly);
	void create_indir_texture();

public:
	texture_manager &tmgr; // stores all textures

	model3d(string const &filename_, texture_manager &tmgr_, int def_tid=-1, colorRGBA const &def_c=WHITE, int reflective_=0, float metalness_=0.0, int recalc_normals_=0, int group_cobjs_level_=0)
		: filename(filename_), recalc_normals(recalc_normals_), group_cobjs_level(group_cobjs_level_), unbound_mat(((def_tid >= 0) ? def_tid : WHITE_TEX), def_c),
		bcube(all_zeros_cube), bcube_all_xf(all_zeros), occlusion_cube(all_zeros), model_refl_tid(0), model_refl_tsize(0), model_refl_last_tsize(0), model_indir_tid(0),
		reflective(reflective_), indoors(2), from_model3d_file(0), has_cobjs(0), needs_alpha_test(0), needs_bump_maps(0), has_spec_maps(0), has_gloss_maps(0),
		xform_zvals_set(0), needs_trans_pass(0), metalness(metalness_), textures_loaded(0), sky_lighting_weight(0.0), tmgr(tmgr_)
	{UNROLL_3X(sky_lighting_sz[i_] = 0;)}
	~model3d() {clear();}
	size_t num_materials() const {return materials.size();}

	material_t &get_material(int mat_id) {
		assert(mat_id >= 0 && (unsigned)mat_id < materials.size());
		return materials[mat_id];
	}
	base_mat_t const &get_unbound_material() const {return unbound_mat;}

	// creation and query
	bool are_textures_loaded() const {return textures_loaded;}
	void set_has_cobjs() {has_cobjs = 1;}
	void add_transform(model3d_xform_t const &xf) {transforms.push_back(xf);}
	unsigned add_triangles(vector<triangle> const &triangles, colorRGBA const &color, int mat_id=-1, unsigned obj_id=0);
	unsigned add_polygon(polygon_t const &poly, vntc_map_t vmap[2], vntct_map_t vmap_tan[2], int mat_id=-1, unsigned obj_id=0);
	void add_triangle(polygon_t const &tri, vntc_map_t &vmap, int mat_id=-1, unsigned obj_id=0);
	void get_polygons(vector<coll_tquad> &polygons, bool quads_only=0, bool apply_transforms=0, unsigned lod_level=0) const;
	void get_transformed_bcubes(vector<cube_t> &bcubes) const;
	void get_cubes(vector<cube_t> &cubes, model3d_xform_t const &xf) const;
	colorRGBA get_avg_color() const;
	unsigned get_gpu_mem() const;
	int get_material_ix(string const &material_name, string const &fn, bool okay_if_exists=0);
	int find_material(string const &material_name);
	void mark_mat_as_used(int mat_id);
	void set_xform_zval_from_tt_height(bool flatten_mesh);
	void finalize();
	void clear();
	void free_context();
	void clear_smaps(); // frees GL state
	void load_all_used_tids();
	void bind_all_used_tids();
	void calc_tangent_vectors();
	void simplify_indices(float reduce_target);
	static void bind_default_flat_normal_map() {select_multitex(FLAT_NMAP_TEX, 5);}
	void set_sky_lighting_file(string const &fn, float weight, unsigned sz[3]);
	void set_occlusion_cube(cube_t const &cube) {occlusion_cube = cube;}
	void set_target_translate_scale(point const &target_pos, float target_radius, geom_xform_t &xf) const;
	void render_materials_def(shader_t &shader, bool is_shadow_pass, int reflection_pass, bool is_z_prepass, int enable_alpha_mask,
		unsigned bmap_pass_mask, int trans_op_mask, point const *const xlate, xform_matrix const *const mvm=nullptr)
	{
		render_materials(shader, is_shadow_pass, reflection_pass, is_z_prepass, enable_alpha_mask, bmap_pass_mask, trans_op_mask, unbound_mat, rotation_t(), xlate, mvm);
	}
	void render_materials(shader_t &shader, bool is_shadow_pass, int reflection_pass, bool is_z_prepass, int enable_alpha_mask, unsigned bmap_pass_mask,
		int trans_op_mask, base_mat_t const &unbound_mat, rotation_t const &rot, point const *const xlate=nullptr, xform_matrix const *const mvm=nullptr,
		bool force_lod=0, float model_lod_mult=1.0, float fixed_lod_dist=0.0, bool skip_cull_face=0, bool is_scaled=0);
	void render_material(shader_t &shader, unsigned mat_id, bool is_shadow_pass, bool is_z_prepass=0, int enable_alpha_mask=0, bool is_bmap_pass=0, point const *const xlate=nullptr);
	void render_with_xform(shader_t &shader, model3d_xform_t &xf, xform_matrix const &mvm, bool is_shadow_pass,
		int reflection_pass, bool is_z_prepass, int enable_alpha_mask, unsigned bmap_pass_mask, int reflect_mode, int trans_op_mask);
	void render(shader_t &shader, bool is_shadow_pass, int reflection_pass, bool is_z_prepass, int enable_alpha_mask,
		unsigned bmap_pass_mask, int reflect_mode, int trans_op_mask, vector3d const &xlate);
	material_t *get_material_by_name(string const &name);
	colorRGBA set_color_for_material(unsigned mat_id, colorRGBA const &color);
	int set_texture_for_material(unsigned mat_id, int tid);
	void ensure_reflection_cube_map();
	cube_t get_single_transformed_bcube(vector3d const &xlate=zero_vector) const;
	void setup_shadow_maps();
	void set_local_model_scene_bounds(shader_t &s);
	bool has_any_transforms() const {return !transforms.empty();}
	cube_t const &get_bcube() const {return bcube;}
	cube_t calc_bcube_including_transforms();
	void union_bcube_with(cube_t const &c) {bcube.union_with_cube(c);}
	void build_cobj_tree(bool verbose);
	bool check_coll_line_cur_xf(point const &p1, point const &p2, point &cpos, vector3d &cnorm, colorRGBA &color, bool exact);
	bool check_coll_line(point const &p1, point const &p2, point &cpos, vector3d &cnorm, colorRGBA &color, bool exact, bool build_bvh_if_needed=0);
	bool get_needs_alpha_test() const {return needs_alpha_test;}
	bool get_needs_trans_pass() const {return needs_trans_pass;}
	bool get_needs_bump_maps () const {return needs_bump_maps;}
	bool uses_spec_map()        const {return has_spec_maps;}
	bool uses_gloss_map()       const {return has_gloss_maps;}
	bool is_planar_reflective() const {return (reflective == 1);}
	bool is_cube_map_reflective() const {return (reflective == 2);}
	bool is_reflective()        const {return (reflective != 0);}
	void compute_area_per_tri();
	void get_stats(model3d_stats_t &stats) const;
	void show_stats() const;
	void get_all_mat_lib_fns(set<std::string> &mat_lib_fns) const;
	bool write_to_disk (string const &fn) const;
	bool read_from_disk(string const &fn);
	static void proc_model_normals(vector<counted_normal> &cn, int recalc_normals, float nmag_thresh=0.7);
	static void proc_model_normals(vector<weighted_normal> &wn, int recalc_normals, float nmag_thresh=0.7);
	void write_to_cobj_file(std::ostream &out) const;
};


struct model3ds : public deque<model3d> {

	texture_manager tmgr;

	void clear();
	void free_context();
	void render(bool is_shadow_pass, int reflection_pass, int trans_op_mask, vector3d const &xlate); // non-const
	void ensure_reflection_cube_maps();
	void set_xform_zval_from_tt_height(bool flatten_mesh);
	bool has_any_transforms() const;
	cube_t calc_and_return_bcube(bool only_reflective);
	void get_all_model_bcubes(vector<cube_t> &bcubes) const;
	unsigned get_gpu_mem() const;
	void build_cobj_trees(bool verbose);
	bool check_coll_line(point const &p1, point const &p2, point &cpos, vector3d &cnorm, colorRGBA &color, bool exact, bool build_bvh_if_needed=0);
	void write_to_cobj_file(std::ostream &out) const;
};


class model_from_file_t {

	string rel_path;
protected:
	model3d &model;

public:
	model_from_file_t(string const &fn, model3d &model_) : model(model_) {rel_path = get_path(fn);}
	string open_include_file(string const &fn, string const &type, ifstream &in_inc) const;
	static string get_path(string const &fn);
	int get_texture(string const &fn, bool is_alpha_mask, bool verbose, bool invert_alpha=0, bool wrap=1, bool mirror=0, bool force_grayscale=0);
	void check_and_bind(int &tid, string const &tfn, bool is_alpha_mask, bool verbose, bool invert_alpha=0, bool wrap=1, bool mirror=0);
};


template<typename T> bool split_polygon(polygon_t const &poly, vector<T> &ppts, float coplanar_thresh, bool allow_quads=1);

bool use_model3d_bump_maps();
void coll_tquads_from_triangles(vector<triangle> const &triangles, vector<coll_tquad> &ppts, colorRGBA const &color);
void free_model_context();
void render_models(int shadow_pass, int reflection_pass, int trans_op_mask=3, vector3d const &xlate=zero_vector);
void ensure_model_reflection_cube_maps();
void auto_calc_model_zvals();
void get_cur_model_polygons(vector<coll_tquad> &ppts, model3d_xform_t const &xf=model3d_xform_t(), unsigned lod_level=0);
unsigned get_loaded_models_gpu_mem();
void get_cur_model_edges_as_cubes(vector<cube_t> &cubes, model3d_xform_t const &xf);
void get_cur_model_as_cubes(vector<cube_t> &cubes, model3d_xform_t const &xf);
bool add_transform_for_cur_model(model3d_xform_t const &xf);
void set_sky_lighting_file_for_cur_model(string const &fn, float weight, unsigned sz[3]);
void set_occlusion_cube_for_cur_model(cube_t const &cube);
bool have_cur_model();
cube_t calc_and_return_all_models_bcube(bool only_reflective=0);
void get_all_model_bcubes(vector<cube_t> &bcubes);
void write_models_to_cobj_file(std::ostream &out);
void adjust_zval_for_model_coll(point &pos, float radius, float mesh_zval, float step_height=0.0);
void check_legal_movement_using_model_coll(point const &prev, point &cur, float radius=0.0);

bool load_model_file(string const &filename, model3ds &models, geom_xform_t const &xf, int def_tid, colorRGBA const &def_c,
	int reflective, float metalness, int recalc_normals, int group_cobjs_level, bool write_file, bool verbose);
bool read_model_file(string const &filename, vector<coll_tquad> *ppts, geom_xform_t const &xf, int def_tid, colorRGBA const &def_c,
	int reflective, float metalness, bool load_model_file, int recalc_normals, int group_cobjs_level, bool write_file, bool verbose);

