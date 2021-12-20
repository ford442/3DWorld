// 3D World - Building Generation
// by Frank Gennari
// 5/22/17

#include "3DWorld.h"
#include "function_registry.h"
#include "shaders.h"
#include "buildings.h"
#include "mesh.h"
#include "draw_utils.h" // for point_sprite_drawer_sized
#include "subdiv.h" // for sd_sphere_d
#include "tree_3dw.h" // for tree_placer_t
#include "profiler.h"
#include "shadow_map.h" // for get_empty_smap_tid

using std::string;

bool const DRAW_WINDOWS_AS_HOLES = 1;
bool const ADD_ROOM_SHADOWS      = 1;
bool const ADD_ROOM_LIGHTS       = 1;
bool const DRAW_EXT_REFLECTIONS  = 1;
float const WIND_LIGHT_ON_RAND      = 0.08;
float const BASEMENT_ENTRANCE_SCALE = 0.33;

bool camera_in_building(0), player_in_basement(0), interior_shadow_maps(0), player_is_hiding(0);
int player_in_closet(0); // uses flags RO_FLAG_IN_CLOSET (player in closet), RO_FLAG_LIT (closet light is on), RO_FLAG_OPEN (closet door is open)
building_params_t global_building_params;
building_t const *player_building(nullptr);

extern bool start_in_inf_terrain, draw_building_interiors, flashlight_on, enable_use_temp_vbo, toggle_room_light;
extern bool teleport_to_screenshot, enable_dlight_bcubes, player_in_elevator, can_do_building_action;
extern unsigned room_mirror_ref_tid;
extern int rand_gen_index, display_mode, window_width, window_height, camera_surf_collide, animate2, building_action_key;
extern float CAMERA_RADIUS, city_dlight_pcf_offset_scale, fticks, FAR_CLIP;
extern point sun_pos, pre_smap_player_pos;
extern vector<light_source> dl_sources;
extern tree_placer_t tree_placer;
extern shader_t reflection_shader;


void get_all_model_bcubes(vector<cube_t> &bcubes); // from model3d.h

float get_door_open_dist() {return 3.5*CAMERA_RADIUS;}

void tid_nm_pair_dstate_t::set_for_shader(float new_bump_map_mag) {
	if (new_bump_map_mag == bump_map_mag) return; // no change
	bump_map_mag = new_bump_map_mag;
	if (bmm_loc == -1) {bmm_loc = s.get_uniform_loc("bump_map_mag");} // set on the first call
	s.set_uniform_float(bmm_loc, bump_map_mag);
}
tid_nm_pair_dstate_t::~tid_nm_pair_dstate_t() { // restore to default if needed
	if (bmm_loc && bump_map_mag != 1.0) {s.set_uniform_float(bmm_loc, 1.0);} // bmm_loc should have been set
}

tid_nm_pair_t tid_nm_pair_t::get_scaled_version(float scale) const {
	tid_nm_pair_t tex(*this);
	tex.tscale_x *= scale;
	tex.tscale_y *= scale;
	return tex;
}
void tid_nm_pair_t::set_gl(tid_nm_pair_dstate_t &state) const {
	if (tid == FONT_TEXTURE_ID) {text_drawer::bind_font_texture();}
	else if (tid == REFLECTION_TEXTURE_ID) {
		if (bind_reflection_shader()) return;
	} else {select_texture(tid);}
	bool const has_normal_map(get_nm_tid() != FLAT_NMAP_TEX);
	if (has_normal_map) {select_multitex(get_nm_tid(), 5);} // else we set bump_map_mag=0.0
	state.set_for_shader(has_normal_map ? 1.0 : 0.0); // enable or disable normal map (only ~25% of calls have a normal map)
	if (emissive > 0.0) {state.s.add_uniform_float("emissive_scale", emissive);} // enable emissive
	if (spec_mag > 0  ) {state.s.set_specular(spec_mag/255.0, shininess);}
}
void tid_nm_pair_t::unset_gl(tid_nm_pair_dstate_t &state) const {
	if (tid == REFLECTION_TEXTURE_ID && room_mirror_ref_tid != 0) {state.s.make_current(); return;}
	if (emissive > 0.0) {state.s.add_uniform_float("emissive_scale", 0.0);} // disable emissive
	if (spec_mag > 0  ) {state.s.clear_specular();}
}
void tid_nm_pair_t::toggle_transparent_windows_mode() { // hack
	if      (tid == BLDG_WINDOW_TEX    ) {tid = BLDG_WIND_TRANS_TEX;}
	else if (tid == BLDG_WIND_TRANS_TEX) {tid = BLDG_WINDOW_TEX;}
}

void room_object_t::check_normalized() const {
	if (!is_strictly_normalized()) {std::cerr << "denormalized object of type " << unsigned(type) << " at " << str() << endl; assert(0);}
}

bool room_object_t::enable_rugs    () {return !global_building_params.rug_tids    .empty();}
bool room_object_t::enable_pictures() {return !global_building_params.picture_tids.empty();}

int select_tid_from_list(vector<unsigned> const &tids, unsigned ix) {return (tids.empty() ? -1 : tids[ix % tids.size()]);}
int room_object_t::get_rug_tid         () const {return select_tid_from_list(global_building_params.rug_tids,     obj_id);}
int room_object_t::get_picture_tid     () const {return select_tid_from_list(global_building_params.picture_tids, obj_id);}
int room_object_t::get_tv_tid          () const {return select_tid_from_list(global_building_params.picture_tids, obj_id/2);} // divide by 2 because even obj_id is turned off
int room_object_t::get_comp_monitor_tid() const {return select_tid_from_list(global_building_params.desktop_tids, obj_id/2);} // divide by 2 because even obj_id is turned off
int room_object_t::get_sheet_tid       () const {return select_tid_from_list(global_building_params.sheet_tids,   obj_id);}
int room_object_t::get_paper_tid       () const {return select_tid_from_list(global_building_params.paper_tids,   obj_id);}

void do_xy_rotate(float rot_sin, float rot_cos, point const &center, point &pos) {
	float const x(pos.x - center.x), y(pos.y - center.y); // translate to center
	pos.x = x*rot_cos - y*rot_sin + center.x;
	pos.y = y*rot_cos + x*rot_sin + center.y;
}
void do_xy_rotate_normal(float rot_sin, float rot_cos, point &pos) { // point rotate without the translate
	float const x(pos.x), y(pos.y);
	pos.x = x*rot_cos - y*rot_sin;
	pos.y = y*rot_cos + x*rot_sin;
}
void building_geom_t::do_xy_rotate    (point const &center, point &pos) const {::do_xy_rotate( rot_sin, rot_cos, center, pos);}
void building_geom_t::do_xy_rotate_inv(point const &center, point &pos) const {::do_xy_rotate(-rot_sin, rot_cos, center, pos);}
void building_geom_t::do_xy_rotate_normal    (point &n) const {::do_xy_rotate_normal( rot_sin, rot_cos, n);}
void building_geom_t::do_xy_rotate_normal_inv(point &n) const {::do_xy_rotate_normal(-rot_sin, rot_cos, n);}


class building_texture_mgr_t {
	int window_tid, hdoor_tid, odoor_tid, bdoor_tid, bdoor2_tid, gdoor_tid, ac_unit_tid1, ac_unit_tid2, bath_wind_tid, helipad_tex, solarp_tex;

	int ensure_tid(int &tid, const char *name) {
		if (tid < 0) {tid = get_texture_by_name(name);}
		if (tid < 0) {tid = WHITE_TEX;} // failed to load texture - use a simple white texture
		return tid;
	}
public:
	building_texture_mgr_t() : window_tid(-1), hdoor_tid(-1), odoor_tid(-1), bdoor_tid(-1), bdoor2_tid(-1), gdoor_tid(-1),
		ac_unit_tid1(-1), ac_unit_tid2(-1), bath_wind_tid(-1), helipad_tex(-1), solarp_tex(-1) {}
	int get_window_tid   () const {return window_tid;}
	int get_hdoor_tid    () {return ensure_tid(hdoor_tid,     "white_door.jpg");} // house door
	int get_odoor_tid    () {return ensure_tid(odoor_tid,     "buildings/office_door.jpg");} // office door (low resolution); unused
	int get_bdoor_tid    () {return ensure_tid(bdoor_tid,     "buildings/building_door.jpg");} // metal + glass building door
	int get_bdoor2_tid   () {return ensure_tid(bdoor2_tid,    "buildings/metal_door.jpg");} // metal building door; unused
	int get_gdoor_tid    () {return ensure_tid(gdoor_tid,     "buildings/garage_door.jpg");} // garage door
	int get_ac_unit_tid1 () {return ensure_tid(ac_unit_tid1,  "buildings/AC_unit1.jpg");} // AC unit (should this be a <d> loop?)
	int get_ac_unit_tid2 () {return ensure_tid(ac_unit_tid2,  "buildings/AC_unit2.jpg");} // AC unit
	int get_bath_wind_tid() {return ensure_tid(bath_wind_tid, "buildings/window_blocks.jpg");} // bathroom window
	int get_helipad_tid  () {return ensure_tid(helipad_tex,   "buildings/helipad.jpg");}
	int get_solarp_tid   () {return ensure_tid(solarp_tex,    "buildings/solar_panel.jpg");}

	bool check_windows_texture() {
		if (!global_building_params.windows_enabled()) return 0;
		if (window_tid >= 0) return 1; // already generated
		gen_building_window_texture(global_building_params.get_window_width_fract(), global_building_params.get_window_height_fract());
		window_tid = BLDG_WINDOW_TEX;
		return 1;
	}
	bool is_door_tid(int tid) const {return (tid >= 0 && (tid == hdoor_tid || tid == odoor_tid || tid == bdoor_tid || tid == bdoor2_tid || tid == gdoor_tid));}
};
building_texture_mgr_t building_texture_mgr;

int get_rect_panel_tid() {return building_texture_mgr.get_gdoor_tid();} // use garage doors
int get_bath_wind_tid () {return building_texture_mgr.get_bath_wind_tid();}
int get_int_door_tid  () {return building_texture_mgr.get_hdoor_tid();}


class texture_id_mapper_t {
	vector<unsigned> tid_to_slot_ix;
	vector<int> tid_to_nm_tid;
	set<unsigned> ext_wall_tids;
	unsigned next_slot_ix;

	void register_tid(int tid) {
		if (tid < 0) return; // not allocated
		if (tid >= (int)tid_to_slot_ix.size()) {tid_to_slot_ix.resize(tid+1, 0);}
		if (tid_to_slot_ix[tid] == 0) {tid_to_slot_ix[tid] = next_slot_ix++;}
		//cout << "register " << tid << " slot " << tid_to_slot_ix[tid] << endl;
	}
	void register_tex(tid_nm_pair_t const &tex) {
		register_tid(tex.tid);

		if (tex.tid > 0 && tex.nm_tid > 0 && tex.nm_tid != FLAT_NMAP_TEX) {
			if (tex.tid >= (int)tid_to_nm_tid.size()) {tid_to_nm_tid.resize(tex.tid+1, -1);}
			tid_to_nm_tid[tex.tid] = tex.nm_tid;
		}
	}
public:
	texture_id_mapper_t() : next_slot_ix(1) {} // slots start at 1; slot 0 is for untextured

	void init() {
		if (!tid_to_slot_ix.empty()) return; // already inited
		unsigned const num_special_tids = 5;
		int const special_tids[num_special_tids] = {WHITE_TEX, FENCE_TEX, PANELING_TEX, TILE_TEX, WOOD_TEX}; // for elevators, etc.
		tid_to_slot_ix.push_back(0); // untextured case
		register_tid(building_texture_mgr.get_window_tid());
		register_tid(building_texture_mgr.get_hdoor_tid());
		//register_tid(building_texture_mgr.get_odoor_tid()); // enable when this door type is used
		register_tid(building_texture_mgr.get_bdoor_tid());
		register_tid(building_texture_mgr.get_bdoor2_tid());
		register_tid(building_texture_mgr.get_gdoor_tid());
		register_tid(building_texture_mgr.get_ac_unit_tid1());
		register_tid(building_texture_mgr.get_ac_unit_tid2());
		register_tid(building_texture_mgr.get_helipad_tid());
		register_tid(building_texture_mgr.get_solarp_tid());
		for (unsigned i = 0; i < num_special_tids; ++i) {register_tid(special_tids[i]);}

		for (auto i = global_building_params.materials.begin(); i != global_building_params.materials.end(); ++i) {
			register_tex(i->side_tex);
			register_tex(i->roof_tex);
			register_tex(i->wall_tex);
			register_tex(i->ceil_tex);
			register_tex(i->floor_tex);
			register_tex(i->house_ceil_tex);
			register_tex(i->house_floor_tex);
			ext_wall_tids.insert(i->side_tex.tid);
		} // for i
		cout << "Used " << (next_slot_ix-1) << " slots for texture IDs up to " << (tid_to_slot_ix.size()-1) << endl;
	}
	unsigned get_slot_ix(int tid) const {
		if (tid < 0) return 0; // untextured - slot 0
		assert(tid < (int)get_num_slots());
		assert(tid_to_slot_ix[tid] > 0);
		return tid_to_slot_ix[tid];
	}
	int get_normal_map_for_tid(int tid) const {
		if (tid < 0 || (unsigned)tid >= tid_to_nm_tid.size()) return -1; // no normal map
		return tid_to_nm_tid[tid];
	}
	unsigned get_num_slots() const {return tid_to_slot_ix.size();}
	bool is_ext_wall_tid(unsigned tid) const {return (ext_wall_tids.find(tid) != ext_wall_tids.end());}
};
texture_id_mapper_t tid_mapper;

int get_normal_map_for_bldg_tid(int tid) {return tid_mapper.get_normal_map_for_tid(tid);}

class tid_vert_counter_t {
	vector<unsigned> counts;
public:
	tid_vert_counter_t() {counts.resize(tid_mapper.get_num_slots(), 0);} // resized to max tid
	void update_count(int tid, unsigned num) {
		if (tid < 0) return;
		assert((unsigned)tid < counts.size());
		counts[tid] += num;
	}
	unsigned get_count(int tid) const {
		if (tid < 0) return 0;
		assert((unsigned)tid < counts.size());
		return counts[tid];
	}
};


class indir_tex_mgr_t {
	unsigned tid; // Note: owned by building_indir_light_mgr, not us
	cube_t lighting_bcube;
public:
	indir_tex_mgr_t() : tid(0) {}
	bool enabled() const {return (tid > 0);}

	bool create_for_building(building_t const &b, unsigned bix, point const &target) {
		b.create_building_volume_light_texture(bix, target, tid);
		lighting_bcube = b.bcube;
		return 1;
	}
	bool setup_for_building(shader_t &s) const {
		if (!enabled()) return 0; // no texture set
		float const dx(lighting_bcube.dx()/MESH_X_SIZE), dy(lighting_bcube.dy()/MESH_Y_SIZE), dxy_offset(0.5f*(dx + dy));
		set_3d_texture_as_current(tid, 1); // indir texture uses TU_ID=1
		s.add_uniform_vector3d("alt_scene_llc",   lighting_bcube.get_llc());
		s.add_uniform_vector3d("alt_scene_scale", lighting_bcube.get_size());
		s.add_uniform_float("half_dxy", dxy_offset);
		return 1;
	}
};
indir_tex_mgr_t indir_tex_mgr;

bool player_in_dark_closet() {return (player_in_closet && !(player_in_closet & (RO_FLAG_OPEN | RO_FLAG_LIT)));}

struct building_lights_manager_t : public city_lights_manager_t {
	void setup_building_lights(vector3d const &xlate) {
		//highres_timer_t timer("Building Dlights Setup"); // 1.9/1.9
		float const light_radius(0.1*light_radius_scale*get_tile_smap_dist()); // distance from the camera where lights are drawn
		if (!begin_lights_setup(xlate, light_radius, dl_sources)) return;
		if (!player_in_dark_closet()) {add_building_interior_lights(xlate, lights_bcube);} // no room lights if player is hiding in a closed closet with light off (prevents light leakage)
		if (flashlight_on) {add_player_flashlight(0.12);} // add player flashlight, even when outside of building so that flashlight can shine through windows
		clamp_to_max_lights(xlate, dl_sources);
		tighten_light_bcube_bounds(dl_sources); // clip bcube to tight bounds around lights for better dlights texture utilization (possible optimization)
		if (ADD_ROOM_SHADOWS) {setup_shadow_maps(dl_sources, (camera_pdu.pos - xlate), global_building_params.max_shadow_maps);}
		finalize_lights(dl_sources);
	}
	virtual bool enable_lights() const {return ((draw_building_interiors && ADD_ROOM_LIGHTS) || flashlight_on);}
};

building_lights_manager_t building_lights_manager;


void set_interior_lighting(shader_t &s, bool have_indir) {
	if (have_indir || player_in_dark_closet()) { // using indir lighting, or player in a closed closet with the light off
		s.add_uniform_float("diffuse_scale",       0.0); // no diffuse from sun/moon
		s.add_uniform_float("ambient_scale",       (player_in_dark_closet() ? 0.1 : 0.0)); // no ambient for indir; slight ambient for closet closet with light off
		s.add_uniform_float("hemi_lighting_scale", 0.0); // disable hemispherical lighting (should we set hemi_lighting=0 in the shader?)
	}
	else {
		float const light_scale(ADD_ROOM_LIGHTS ? 0.5 : 1.0); // lower for basement, lower when using room lights
		float const light_change_amt(fticks/(2.0f*TICKS_PER_SECOND));
		static float blscale(1.0); // indir/ambient lighting slowly transitions when entering or leaving the basement
		if (player_in_basement) {blscale = max(0.0f, (blscale - light_change_amt));} // decrease
		else                    {blscale = min(1.0f, (blscale + light_change_amt));} // increase
		s.add_uniform_float("diffuse_scale",       0.2f*blscale*light_scale); // reduce diffuse and specular lighting for sun/moon
		s.add_uniform_float("ambient_scale",       0.5f*(1.0f + blscale)*light_scale); // brighter ambient
		s.add_uniform_float("hemi_lighting_scale", 0.2f*blscale*light_scale); // reduced hemispherical lighting
	}
}
void reset_interior_lighting(shader_t &s) {
	s.add_uniform_float("diffuse_scale", 1.0); // re-enable diffuse and specular lighting for sun/moon
	s.add_uniform_float("ambient_scale", 1.0); // reset to default
	s.add_uniform_float("hemi_lighting_scale", 0.5); // reset to default
}
void reset_interior_lighting_and_end_shader(shader_t &s) {
	reset_interior_lighting(s);
	s.end_shader();
}
void setup_building_draw_shader(shader_t &s, float min_alpha, bool enable_indir, bool force_tsl, bool use_texgen) { // for building interiors
	float const pcf_scale = 0.2;
	bool const have_indir(enable_indir && indir_tex_mgr.enabled() && !(player_in_closet && !(player_in_closet & RO_FLAG_OPEN))); // disable indir if the player is in a closed closet
	int const use_bmap(global_building_params.has_normal_map), interior_use_smaps((ADD_ROOM_SHADOWS && ADD_ROOM_LIGHTS) ? 2 : 1); // dynamic light smaps only
	cube_t const lights_bcube(building_lights_manager.get_lights_bcube());
	s.set_prefix("#define LINEAR_DLIGHT_ATTEN", 1); // FS; improves room lighting (better light distribution vs. framerate trade-off)
	city_shader_setup(s, lights_bcube, ADD_ROOM_LIGHTS, interior_use_smaps, use_bmap, min_alpha, force_tsl, pcf_scale, use_texgen, have_indir, 0); // is_outside=0
	set_interior_lighting(s, have_indir);
	if (have_indir) {indir_tex_mgr.setup_for_building(s);}
}


/*static*/ void building_draw_utils::calc_normals(building_geom_t const &bg, vector<vector3d> &nv, unsigned ndiv) {

	assert(bg.flat_side_amt >= 0.0 && bg.flat_side_amt < 0.5); // generates a flat side
	assert(bg.alt_step_factor >= 0.0 && bg.alt_step_factor < 1.0);
	if (bg.flat_side_amt > 0.0) {assert(ndiv > 4);} // should be at least 5 sides, 6-8 is better
	float const ndiv_inv(1.0/ndiv), css(TWO_PI*ndiv_inv*(1.0f - bg.flat_side_amt));
	float sin_ds[2] = {}, cos_ds[2] = {};

	if (bg.alt_step_factor > 0.0) { // alternate between large and small steps (cube with angled corners, etc.)
		assert(!(ndiv&1));
		float const css_v[2] = {css*(1.0f + bg.alt_step_factor), css*(1.0f - bg.alt_step_factor)};
		UNROLL_2X(sin_ds[i_] = sin(css_v[i_]); cos_ds[i_] = cos(css_v[i_]);)
	}
	else { // uniform side length
		sin_ds[0] = sin_ds[1] = sin(css);
		cos_ds[0] = cos_ds[1] = cos(css);
	}
	float sin_s(0.0), cos_s(1.0), angle0(bg.start_angle); // start at 0.0
	if (bg.half_offset) {angle0 = 0.5*css;} // for cube
	if (angle0 != 0.0) {sin_s = sin(angle0); cos_s = cos(angle0);} // uncommon case
	nv.resize(ndiv);

	for (unsigned S = 0; S < ndiv; ++S) { // build normals table
		bool const d(S&1);
		float const s(sin_s), c(cos_s);
		nv[S].assign(s, c, 0.0);
		sin_s = s*cos_ds[d] + c*sin_ds[d];
		cos_s = c*cos_ds[d] - s*sin_ds[d];
	}
}

/*static*/ void building_draw_utils::calc_poly_pts(building_geom_t const &bg, cube_t const &bcube, vector<point> &pts, float expand) {

	calc_normals(bg, pts, bg.num_sides);
	vector3d const sz(bcube.get_size());
	point const cc(bcube.get_cube_center());
	float const rx(0.5*sz.x + expand), ry(0.5*sz.y + expand); // expand polygon by sphere radius
	for (unsigned i = 0; i < bg.num_sides; ++i) {pts[i].assign((cc.x + rx*pts[i].x), (cc.y + ry*pts[i].y), 0.0);} // convert normals to points
}

// Note: invert_tc only applies to doors
void add_tquad_to_verts(building_geom_t const &bg, tquad_with_ix_t const &tquad, cube_t const &bcube, tid_nm_pair_t const &tex,
	colorRGBA const &color, vect_vnctcc_t &verts, bool invert_tc_x, bool exclude_frame, bool no_tc, bool no_rotate)
{
	assert(tquad.npts == 3 || tquad.npts == 4); // triangles or quads
	bool const do_rotate(bg.is_rotated() && !no_rotate);
	point const center(do_rotate ? bcube.get_cube_center() : all_zeros); // rotate about bounding cube / building center
	vert_norm_comp_tc_color vert;
	float tsx(0.0), tsy(0.0), tex_off(0.0);
	bool dim(0);

	if (tquad.type == tquad_with_ix_t::TYPE_WALL) { // side/wall
		tsx = 2.0f*tex.tscale_x; tsy = 2.0f*tex.tscale_y; // adjust for local vs. global space change
		dim = (tquad.pts[0].x == tquad.pts[1].x);
		if (world_mode != WMODE_INF_TERRAIN) {tex_off = (dim ? yoff2*DY_VAL : xoff2*DX_VAL);}
	}
	else if (tquad.type == tquad_with_ix_t::TYPE_ROOF || tquad.type == tquad_with_ix_t::TYPE_ROOF_ACC) { // roof cap
		float const denom(0.5f*(bcube.dx() + bcube.dy()));
		tsx = tex.tscale_x/denom; tsy = tex.tscale_y/denom;
	}
	vert.set_c4(color);
	vector3d normal(tquad.get_norm());
	if (do_rotate) {bg.do_xy_rotate_normal(normal);}
	vert.set_norm(normal);
	invert_tc_x ^= (tquad.type == tquad_with_ix_t::TYPE_IDOOR2 || tquad.type == tquad_with_ix_t::TYPE_ODOOR2); // interior/office door, back face

	for (unsigned i = 0; i < tquad.npts; ++i) {
		vert.v = tquad.pts[i];

		if (no_tc) { // untextured, for door edges
			vert.t[0] = vert.t[1] = 0.0;
		}
		else if (tquad.type == tquad_with_ix_t::TYPE_WALL) { // side/wall
			vert.t[0] = (vert.v[dim] + tex_off)*tsx; // use nonzero width dim
			vert.t[1] = (vert.v.z - bcube.z1())*tsy;
		}
		else if (tquad.type == tquad_with_ix_t::TYPE_ROOF) { // roof cap
			vert.t[0] = (vert.v.x - bcube.x1())*tsx; // varies from 0.0 and bcube x1 to 1.0 and bcube x2
			vert.t[1] = (vert.v.y - bcube.y1())*tsy; // varies from 0.0 and bcube y1 to 1.0 and bcube y2
		}
		else if (tquad.type == tquad_with_ix_t::TYPE_ROOF_ACC) { // roof access cover
			if (fabs(normal.z) > 0.5) {vert.t[0] = vert.v.x*tsx; vert.t[1] = vert.v.y*tsy;} // facing up, use XY plane
			else {vert.t[0] = (vert.v.x + vert.v.y)*tsx; vert.t[1] = vert.v.z*tsy;} // facing to the side, use XZ or YZ plane
		}
		else if (tquad.is_exterior_door() || tquad.type == tquad_with_ix_t::TYPE_HELIPAD || tquad.type == tquad_with_ix_t::TYPE_SOLAR) { // textured from (0,0) to (1,1)
			vert.t[0] = float((i == 1 || i == 2) ^ invert_tc_x);
			vert.t[1] = float((i == 2 || i == 3));
			if (tquad.type == tquad_with_ix_t::TYPE_SOLAR) {vert.t[0] *= 4.0; vert.t[1] *= 4.0;} // 4 reptitions in each dimension
		}
		else if (tquad.is_interior_door()) { // interior door textured/stretched in Y unless SPLIT_DOOR_PER_FLOOR=1
			vert.t[0] = tex.tscale_x*((i == 1 || i == 2) ^ invert_tc_x);
			vert.t[1] = tex.tscale_y*((i == 2 || i == 3));
			if (exclude_frame) {vert.t[0] = 0.07 + 0.86*vert.t[0];}
			if (SPLIT_DOOR_PER_FLOOR) {vert.t[1] *= 0.97;} // trim off the top door frame
		}
		else if (tquad.type == tquad_with_ix_t::TYPE_TRIM) {} // untextured - no tex coords
		else {assert(0);}
		if (do_rotate) {bg.do_xy_rotate(center, vert.v);}
		verts.push_back(vert);
	} // for i
}


#define EMIT_VERTEX() \
	vert.v = pt*sz + llc; \
	vert.t[ st] = tscale[ st]*(vert.v[d] + tex_vert_off[d]); \
	vert.t[!st] = tscale[!st]*(vert.v[i] + tex_vert_off[i]); \
	vert.t[0] += tex.txoff; \
	vert.t[1] += tex.tyoff; \
	if (apply_ao) {vert.copy_color(cw[pt.z > 0.5]);} \
	if (bg.is_rotated()) {bg.do_xy_rotate(center, vert.v);} \
	verts.push_back(vert);

#define EMIT_VERTEX_SIMPLE() \
	vert.v = pt*sz + llc; \
	vert.t[ st] = tscale[ st]*(ws_texture ? vert.v[d] : pt[d]); \
	vert.t[!st] = tscale[!st]*(ws_texture ? vert.v[i] : pt[i]); \
	verts.push_back(vert);

class building_draw_t {

	class draw_block_t {
		struct vert_ix_pair {
			unsigned qix, tix; // {quads, tris}
			vert_ix_pair(unsigned qix_, unsigned tix_) : qix(qix_), tix(tix_) {}
			bool operator==(vert_ix_pair const &v) const {return (qix == v.qix && tix == v.tix);}
		};
		// Note: not easy to use vao_manager_t due to upload done before active shader + shadow vs. geometry pass, but we can use vao_wrap_t's directly
		vbo_wrap_t vbo;
		vao_wrap_t vao, svao; // regular + shadow; do we need a vao_manager_with_shadow_t?
		vector<vert_ix_pair> pos_by_tile; // {quads, tris}
		unsigned tri_vbo_off;
		unsigned start_num_verts[2] = {0}; // for quads and triangles
	public:
		bool no_shadows;
		tid_nm_pair_t tex;
		vect_vnctcc_t quad_verts, tri_verts;

		draw_block_t() : tri_vbo_off(0), no_shadows(0) {}
		void record_num_verts() {start_num_verts[0] = num_quad_verts(); start_num_verts[1] = num_tri_verts();}

		void draw_geom_range(tid_nm_pair_dstate_t &state, bool shadow_only, vert_ix_pair const &vstart, vert_ix_pair const &vend) { // use VBO rendering
			if (vstart == vend) return; // empty range - no verts for this tile
			if (shadow_only && no_shadows) return; // no shadows on this material
			if (!shadow_only) {tex.set_gl(state);}
			assert(vbo.vbo_valid());
			(shadow_only ? svao : vao).create_from_vbo<vert_norm_comp_tc_color>(vbo, 1, 1); // setup_pointers=1, always_bind=1

			if (vstart.qix != vend.qix) {
				assert(vstart.qix < vend.qix);
				draw_quads_as_tris((vend.qix - vstart.qix), vstart.qix);
			}
			if (vstart.tix != vend.tix) {
				assert(vstart.tix < vend.tix);
				glDrawArrays(GL_TRIANGLES, (vstart.tix + tri_vbo_off), (vend.tix - vstart.tix));
			}
			if (!shadow_only) {tex.unset_gl(state);}
			vao_manager_t::post_render();
		}
		void draw_all_geom(tid_nm_pair_dstate_t &state, bool shadow_only, bool direct_draw_no_vbo, vertex_range_t const *const exclude=nullptr) {
			if (shadow_only && no_shadows) return; // no shadows on this material

			if (direct_draw_no_vbo) {
				enable_use_temp_vbo = 1; // hack to fix missing wall artifacts when not using a core context
				assert(!exclude); // not supported in this mode
				bool const use_texture(!shadow_only && (!quad_verts.empty() || !tri_verts.empty()));
				if (use_texture) {tex.set_gl(state);} // Note: colors are not disabled here
				if (!quad_verts.empty()) {draw_quad_verts_as_tris(quad_verts, 0, 1, 1);}
				if (!tri_verts .empty()) {draw_verts(tri_verts, GL_TRIANGLES, 0, 1);}
				if (use_texture) {tex.unset_gl(state);}
				enable_use_temp_vbo = 0;
			}
			else {
				if (pos_by_tile.empty()) return; // nothing to draw for this block/texture
				vert_ix_pair const &start(pos_by_tile.front()), end(pos_by_tile.back());
				if (!exclude) {draw_geom_range(state, shadow_only, start, end); return;} // non-exclude case
				assert(exclude->start >= start.qix && exclude->start < exclude->end && exclude->end <= end.qix); // exclude (start, end) must be a subset of (start.qix, end.qix)
				draw_geom_range(state, shadow_only, start, vert_ix_pair(exclude->start, end.tix)); // first block of quads and all tris
				draw_geom_range(state, shadow_only, vert_ix_pair(exclude->end, end.tix), end); // second block of quads and no tris
			}
		}
		void draw_quad_geom_range(tid_nm_pair_dstate_t &state, vertex_range_t const &range, bool shadow_only=0) { // no tris; empty range is legal
			draw_geom_range(state, shadow_only, vert_ix_pair(range.start, 0), vert_ix_pair(range.end, 0));
		}
		void draw_geom_tile(tid_nm_pair_dstate_t &state, unsigned tile_id, bool shadow_only) {
			if (pos_by_tile.empty()) return; // nothing to draw for this block/texture
			assert(tile_id+1 < pos_by_tile.size()); // tile and next tile must be valid indices
			draw_geom_range(state, shadow_only, pos_by_tile[tile_id], pos_by_tile[tile_id+1]); // shadow_only=0
		}
		void upload_to_vbos() {
			assert((quad_verts.size()%4) == 0);
			assert((tri_verts.size()%3) == 0);
			tri_vbo_off = quad_verts.size(); // triangles start after quads
			quad_verts.insert(quad_verts.end(), tri_verts.begin(), tri_verts.end()); // add tri_verts to quad_verts
			clear_cont(tri_verts); // no longer needed
			if (!quad_verts.empty()) {vbo.create_and_upload(quad_verts, 0, 1);}
			clear_cont(quad_verts); // no longer needed
		}
		void register_tile_id(unsigned tid) {
			if (tid+1 == pos_by_tile.size()) return; // already saw this tile
			assert(tid >= pos_by_tile.size()); // tid must be strictly increasing
			pos_by_tile.resize(tid+1, vert_ix_pair(quad_verts.size(), tri_verts.size())); // push start of new range back onto all previous tile slots
		}
		void finalize(unsigned num_tiles) {
			if (pos_by_tile.empty()) return; // nothing to do
			register_tile_id(num_tiles); // add terminator
			remove_excess_cap(pos_by_tile);
		}
		void clear_verts() {quad_verts.clear(); tri_verts.clear(); pos_by_tile.clear();}
		void clear_vbos () {vbo.clear(); vao.clear(); svao.clear();}
		void clear() {clear_vbos(); clear_verts();}
		bool empty() const {return (quad_verts.empty() && tri_verts.empty());}
		bool has_drawn() const {return !pos_by_tile.empty();}
		unsigned num_quad_verts() const {return quad_verts.size();}
		unsigned num_tri_verts () const {return tri_verts .size();}
		unsigned num_verts() const {return (quad_verts.size() + tri_verts.size());}
		unsigned num_tris () const {return (quad_verts.size()/2 + tri_verts.size()/3);} // Note: 1 quad = 4 verts = 2 triangles
		unsigned start_quad_vert() const {return start_num_verts[0];}
		unsigned start_tri_vert () const {return start_num_verts[0];}
	}; // end draw_block_t
	vector<draw_block_t> to_draw; // one per texture, assumes tids are dense

	vect_vnctcc_t &get_verts(tid_nm_pair_t const &tex, bool quads_or_tris=0) { // default is quads
		unsigned const ix(get_to_draw_ix(tex));
		if (ix >= to_draw.size()) {to_draw.resize(ix+1);}
		draw_block_t &block(to_draw[ix]);
		block.register_tile_id(cur_tile_id);
		if (block.empty()) {block.tex = tex;} // copy material first time
		else {
			assert(block.tex.tid == tex.tid);
			int const bnm(block.tex.get_nm_tid()), tnm(tex.get_nm_tid());

			if (bnm != tnm) { // else normal maps must agree
				if (bnm == FLAT_NMAP_TEX) {block.tex.nm_tid = tnm;} // assume this normal map is correct and assign it to the block
				else if (tnm != FLAT_NMAP_TEX) { // allow if if block has normal map but tex does not - block will override the texture
					std::cerr << "mismatched normal map for texture ID " << block.tex.tid << " in slot " << ix << ": " << bnm << " vs. " << tnm << endl;
					assert(0);
				}
			}
			// if new texture has specular and block does not, copy specular parameters from new texture; this is needed for house wood floors
			if (tex.spec_mag && !block.tex.spec_mag) {block.tex.spec_mag = tex.spec_mag; block.tex.shininess = tex.shininess;}
		}
		return (quads_or_tris ? block.tri_verts : block.quad_verts);
	}
	static void setup_ao_color(colorRGBA const &color, float bcz1, float ao_bcz2, float z1, float z2, color_wrapper cw[2], vert_norm_comp_tc_color &vert, bool no_ao) {
		if (!no_ao && global_building_params.ao_factor > 0.0) {
			min_eq(z1, ao_bcz2); min_eq(z2, ao_bcz2); // clamp zvals to AO zmax
			float const dz_mult(global_building_params.ao_factor/(ao_bcz2 - bcz1));
			UNROLL_2X(cw[i_].set_c4(color*((1.0f - global_building_params.ao_factor) + dz_mult*((i_ ? z2 : z1) - bcz1)));)
		} else {vert.set_c4(color);} // color is shared across all verts
	}
	vector<vector3d> normals; // reused across add_cylinder() calls
	vector<vert_norm_tc> sphere_verts; // reused
	point cur_camera_pos;
	bool is_city;

	struct wall_seg_t {
		float dlo, dhi, ilo, ihi;
		wall_seg_t() : dlo(0.0), dhi(1.0), ilo(0.0), ihi(1.0) {}

		wall_seg_t(float dlo_, float dhi_, float ilo_, float ihi_) : dlo(dlo_), dhi(dhi_), ilo(ilo_), ihi(ihi_) {
			assert(dlo <= dhi && ilo <= ihi && dlo >= 0.0f && dhi <= 1.0f && ilo >= 0.0f && ihi <= 1.0f); // should be (dlo < dhi && ilo < ihi), but can fail due to FP error
		}
		bool is_normalized() const {return (dlo < dhi && ilo < ihi);}
	};
	vector<wall_seg_t> segs;
	vect_cube_t faces;

public:
	unsigned cur_tile_id;
	vect_cube_t temp_cubes, temp_cubes2;
	vector<float> temp_wall_edges;

	building_draw_t(bool is_city_=0) : cur_camera_pos(zero_vector), is_city(is_city_), cur_tile_id(0) {}
	void init_draw_frame() {cur_camera_pos = get_camera_pos();} // capture camera pos during non-shadow pass to use for shadow pass
	bool empty() const {return to_draw.empty();}
	void reserve_verts(tid_nm_pair_t const &tex, size_t num, bool quads_or_tris=0) {get_verts(tex, quads_or_tris).reserve(num);}
	unsigned get_to_draw_ix(tid_nm_pair_t const &tex) const {return tid_mapper.get_slot_ix(tex.tid);}
	unsigned get_num_verts (tid_nm_pair_t const &tex, bool quads_or_tris=0) {return get_verts(tex, quads_or_tris).size();}

	void get_all_mat_verts(vect_vnctcc_t &verts, bool triangles) const {
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {vector_add_to((triangles ? i->tri_verts : i->quad_verts), verts);}
	}
	void begin_draw_range_capture() {
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->record_num_verts();}
	}
	void end_draw_range_capture(draw_range_t &r) const { // capture quads added since begin_draw_range_capture() call across to_draw
		for (unsigned i = 0, rix = 0; i < to_draw.size(); ++i) {
			unsigned const start(to_draw[i].start_quad_vert()), end(to_draw[i].num_quad_verts());
			if (start == end) continue; // empty, skip
			assert(start < end);
			assert(rix < MAX_DRAW_BLOCKS); // make sure we have enough slots for this entry
			r.vr[rix++] = vertex_range_t(start, end, i);
		}
	}
	void toggle_transparent_windows_mode() {
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->tex.toggle_transparent_windows_mode();}
	}
	void set_no_shadows_for_tex(tid_nm_pair_t const &tex) {
		unsigned const ix(get_to_draw_ix(tex));
		assert(ix < to_draw.size()); // must call get_verts() on this tex first
		to_draw[ix].no_shadows = 1;
	}
	void add_cylinder(building_t const &bg, point const &pos, point const &rot_center, float height, float rx, float ry,
		tid_nm_pair_t const &tex, colorRGBA const &color, unsigned dim_mask, bool no_ao, bool clip_windows)
	{
		unsigned const ndiv(bg.num_sides); // Note: no LOD
		assert(ndiv >= 3);
		bool const smooth_normals(ndiv >= 16); // cylinder vs. N-gon
		float const bcz1(bg.bcube.z1()), z_top(pos.z + height), tscale_x(2.0*tex.tscale_x), tscale_y(2.0*tex.tscale_y); // adjust for local vs. global space change
		bool const apply_ao(!no_ao && global_building_params.ao_factor > 0.0);
		vert_norm_comp_tc_color vert;
		color_wrapper cw[2];
		setup_ao_color(color, bcz1, bg.ao_bcz2, pos.z, z_top, cw, vert, no_ao);
		float tex_pos[2] = {0.0, 1.0};
		building_draw_utils::calc_normals(bg, normals, ndiv);
		UNROLL_2X(tex_pos[i_] = ((i_ ? z_top : pos.z) - bcz1);)

		if (dim_mask & 3) { // draw sides
			auto &verts(get_verts(tex)); // Note: cubes are drawn with quads, so we want to emit quads here
			float tot_perim(0.0), cur_perim[2] = {0.0, 0.0};
			for (unsigned S = 0; S < ndiv; ++S) {tot_perim += p2p_dist(normals[S], normals[(S+1)%ndiv]);}
			float const tscale_mult(TWO_PI*sqrt((rx*rx + ry*ry)/2.0f)/tot_perim);
				
			for (unsigned S = 0; S < ndiv; ++S) { // generate vertex data quads
				vector3d const &n1(normals[S]), &n2(normals[(S+1)%ndiv]);
				cur_perim[0]  = cur_perim[1];
				cur_perim[1] += p2p_dist(n1, n2);
				vector3d normal(n1 + n2); normal.x *= ry; normal.y *= rx; // average the two vertex normals for the flat face normal
				if (bg.is_rotated()) {bg.do_xy_rotate_normal(normal);}
				bool const cur_smooth_normals(smooth_normals && (bg.flat_side_amt == 0.0 || S+1 != ndiv)); // flat side of cylindrical building is not smooth
				if (!cur_smooth_normals) {vert.set_norm(normal.get_norm());}

				for (unsigned d = 0; d < 2; ++d) {
					vector3d const &n(d ? n2 : n1);
					vert.t[0] = tscale_x*cur_perim[d]*tscale_mult + tex.txoff; // Note: could try harder to ensure an integer multiple to fix seams, but not a problem in practice
					
					if (cur_smooth_normals) {
						vector3d normal(n); normal.x *= ry; normal.y *= rx; // scale normal by radius (swapped)
						if (bg.is_rotated()) {bg.do_xy_rotate_normal(normal);}
						vert.set_norm(normal.get_norm());
					}
					vert.v.assign((pos.x + rx*n.x), (pos.y + ry*n.y), 0.0);
					if (bg.is_rotated()) {bg.do_xy_rotate(rot_center, vert.v);}

					for (unsigned e = 0; e < 2; ++e) {
						vert.v.z = ((d^e) ? z_top : pos.z);
						vert.t[1] = tscale_y*tex_pos[d^e] + tex.tyoff;
						if (apply_ao) {vert.copy_color(cw[d^e]);}
						verts.push_back(vert);
					}
					if (clip_windows) {clip_low_high_tc(verts[verts.size()-1].t[1], verts[verts.size()-2].t[1]);} // is this necessary?
				} // for d
			} // for S
		} // end draw sides
		if (dim_mask & 4) { // draw end(s) / roof
			auto &tri_verts(get_verts(tex, 1));
			
			for (unsigned d = 0; d < 2; ++d) { // bottom, top
				if (is_city && pos.z == bcz1 && d == 0) continue; // skip bottom
				vert.set_ortho_norm(2, d); // +/- z
				if (apply_ao) {vert.copy_color(cw[d]);}
				vert_norm_comp_tc_color center(vert);
				center.t[0] = center.t[1] = 0.0; // center of texture space for this disk
				center.v = pos;
				if (d) {center.v.z += height;}
				if (bg.is_rotated()) {bg.do_xy_rotate(rot_center, center.v);}
				unsigned const start(tri_verts.size());

				for (unsigned S = 0; S < ndiv; ++S) { // generate vertex data triangles
					tri_verts.push_back(center);

					for (unsigned e = 0; e < 2; ++e) {
						if (S > 0 && e == 0) {tri_verts.push_back(tri_verts[tri_verts.size()-2]); continue;} // reuse prev vertex
						vector3d const &n(normals[(S+e)%ndiv]);
						vert.v.assign((pos.x + rx*n.x), (pos.y + ry*n.y), center.v.z);
						vert.t[0] = tscale_x*n[0]; vert.t[1] = tscale_y*n[1];
						if (bg.is_rotated()) {bg.do_xy_rotate(rot_center, vert.v);}
						tri_verts.push_back(vert);
					}
				} // for S
				if (d == 1) {std::reverse(tri_verts.begin()+start, tri_verts.end());} // winding order is wrong, but it's easier to reverse it than change all of the indexing logic
			} // for d
		} // end draw end(s)
	}

	void add_tquad(building_geom_t const &bg, tquad_with_ix_t const &tquad, cube_t const &bcube, tid_nm_pair_t const &tex, colorRGBA const &color,
		bool invert_tc_x=0, bool exclude_frame=0, bool no_tc=0)
	{
		add_tquad_to_verts(bg, tquad, bcube, tex, color, get_verts(tex, (tquad.npts == 3)), invert_tc_x, exclude_frame, no_tc); // 0=quads, 1=tris
	}

	static void set_rotated_normal(vert_norm_comp_tc_color &vert, building_t const &b, vector3d &norm, unsigned n, bool dir) {
		norm.z = 0.0; // likely doesn't need to be set, but okay to set to 0
		if (n == 0) {norm.x =  b.rot_cos; norm.y = b.rot_sin;} // X
		else        {norm.x = -b.rot_sin; norm.y = b.rot_cos;} // Y
		vert.set_norm(dir ? norm : -norm);
	}

	// clip_windows: 0=no clip, 1=clip for building, 2=clip for house
	// dim_mask bits: enable dims: 1=x, 2=y, 4=z | disable cube faces: 8=x1, 16=x2, 32=y1, 64=y2, 128=z1, 256=z2
	void add_section(building_t const &bg, bool clip_to_other_parts, cube_t const &cube, tid_nm_pair_t const &tex,
		colorRGBA const &color, unsigned dim_mask, bool skip_bottom, bool skip_top, bool no_ao, int clip_windows,
		float door_ztop=0.0, unsigned door_sides=0, float offset_scale=1.0, bool invert_normals=0, cube_t const *const clamp_cube=nullptr)
	{
		assert(bg.num_sides >= 3); // must be nonzero volume
		bool const is_rotated(bg.is_rotated());
		point const center(!is_rotated ? all_zeros : bg.bcube.get_cube_center()); // rotate about bounding cube / building center
		vector3d const sz(cube.get_size()), llc(cube.get_llc()); // move origin from center to min corner

		if (bg.num_sides != 4) { // not a cube, use cylinder
			assert(door_ztop == 0.0); // not supported
			point const ccenter(cube.get_cube_center()), pos(ccenter.x, ccenter.y, cube.z1());
			//float const rscale(0.5*((num_sides <= 8) ? SQRT2 : 1.0)); // larger for triangles/cubes/hexagons/octagons (to ensure overlap/connectivity), smaller for cylinders
			float const rscale(0.5); // use shape contained in bcube so that bcube tests are correct, since we're not creating L/T/U shapes for this case
			add_cylinder(bg, pos, center, sz.z, rscale*sz.x, rscale*sz.y, tex, color, dim_mask, no_ao, clip_windows);
			return;
		}
		// else draw as a cube (optimized flow)
		auto &verts(get_verts(tex, bg.is_pointed)); // bg.is_pointed ? tris : quads
		vert_norm_comp_tc_color vert;
		if (bg.is_pointed) {dim_mask &= 3;} // mask off z-dim since pointed objects (antenna) have no horizontal surfaces
		float const tscale[2] = {2.0f*tex.tscale_x, 2.0f*tex.tscale_y}; // adjust for local vs. global space change
		bool const apply_ao(!no_ao && global_building_params.ao_factor > 0.0);
		color_wrapper cw[2];
		setup_ao_color(color, bg.bcube.z1(), bg.ao_bcz2, cube.z1(), cube.z2(), cw, vert, no_ao);
		vector3d tex_vert_off(((world_mode == WMODE_INF_TERRAIN) ? zero_vector : vector3d(xoff2*DX_VAL, yoff2*DY_VAL, 0.0)));
		if (clip_windows) {tex_vert_off.z -= bg.bcube.get_llc().z;} // don't adjust X/Y pos for windows, because other code needs to know where windows are placed
		else {tex_vert_off -= bg.bcube.get_llc();} // normalize to building LLC to keep tex coords small
		if (is_city && cube.z1() == bg.bcube.z1()) {skip_bottom = 1;} // skip bottoms of first floor parts drawn in cities
		float const window_vspacing(bg.get_window_vspace()), window_h_border(0.75*bg.get_window_h_border()), offset_val(0.01*offset_scale*window_vspacing);
		vector3d norm; // used for rotated buildings

		for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
			unsigned const n((i+2)%3), d((i+1)%3), st(i&1); // n = dim of normal, i/d = other dims
			if (!(dim_mask & (1<<n))) continue; // check for enabled dims
			bool const do_xy_clip(clip_windows && n < 2 && !is_rotated), clip_d(d != 2); // clip the non-z dim;

			for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
				if (skip_bottom && n == 2 && j == 0) continue; // skip bottom side
				if (skip_top    && n == 2 && j == 1) continue; // skip top    side
				unsigned const dir(bool(j) ^ invert_normals);
				if (dim_mask & (1<<(2*n+dir+3)))     continue; // check for disabled faces
				if (clamp_cube != nullptr && (cube.d[n][dir] < clamp_cube->d[n][0] || cube.d[n][dir] > clamp_cube->d[n][1])) continue; // outside clamp cube, drop this face
				if (n < 2 && is_rotated) {set_rotated_normal(vert, bg, norm, n, j);} // XY only
				else {vert.n[i] = 0; vert.n[d] = 0; vert.n[n] = (j ? 127 : -128);} // -1.0 or 1.0
				point pt; // parameteric position within cube in [vec3(0), vec3(1)]
				pt[n] = dir; // our cube face, in direction of normal

				if (bg.is_pointed) { // antenna triangle; parts clipping doesn't apply to this case since there are no opposing cube faces
					unsigned const ix(verts.size()); // first vertex of this triangle
					assert(door_ztop == 0.0); // not supported
					pt[!n] = j^n^1; pt.z = 0;
					EMIT_VERTEX(); // bottom low
					pt[!n] = j^n;
					EMIT_VERTEX(); // bottom high
					pt[!n] = 0.5; pt[n] = 0.5; pt.z = 1;
					EMIT_VERTEX(); // top
					vector3d normal;
					get_normal(verts[ix+0].v, verts[ix+1].v, verts[ix+2].v, normal, 1); // update with correct normal
					vert.set_norm(normal);
					UNROLL_3X(verts[ix+i_].set_norm(vert);)
					continue; // no windows/clipping
				}
				segs.clear();

				if (bg.has_interior() && clip_to_other_parts && bg.real_num_parts > 1 && n != 2) {
					// clip walls XY to remove intersections; this applies to both walls and windows
					cube_t face(cube);
					face.d[n][!j] = face.d[n][j]; // shrink to zero thickness face
					faces.clear();
					faces.push_back(face);
					float const sz_d_inv(1.0/sz[d]), sz_i_inv(1.0/sz[i]);
					float num_windows(0.0);

					if (do_xy_clip) { // side of non-rotated building
						unsigned const cdim(clip_d ? d : i); // clip dim, horizontal (X or Y)
						float tlo(tscale[0]*(face.d[cdim][0] + tex_vert_off[cdim]) + tex.txoff), thi(tscale[0]*(face.d[cdim][1] + tex_vert_off[cdim]) + tex.txoff); // TCx
						clip_low_high_tc(tlo, thi); // TCx of unclipped wall
						num_windows = (thi - tlo); // for unclipped wall
						assert(num_windows >= 0.0);
						if (num_windows < 0.5) continue; // no space for a window before clip
					}
					for (auto p = bg.parts.begin(); p != bg.get_real_parts_end(); ++p) {
						if (p->contains_cube(cube)) continue; // skip ourself (including door part)
						if (cube.d[d][1] <= p->d[d][0] || cube.d[d][0] >= p->d[d][1] || cube.d[i][1] <= p->d[i][0] || cube.d[i][0] >= p->d[i][1]) continue; // check for overlap in the two quad dims
						subtract_cube_from_cubes(*p, faces, nullptr, 1, 1); // no holes, clip_in_z=1, include_adj=1
					}
					for (unsigned f = 0; f < faces.size(); ++f) { // convert from cubes to parametric coordinates in [0.0, 1.0] range
						cube_t const &F(faces[f]);
						if (F.get_sz_dim(d) == 0.0 || F.get_sz_dim(i) == 0.0) continue; // adjacent part zero area strip, skip
						// don't copy/enable the top segment for house windows because houses always have a sloped roof section on top that will block the windows
						if (clip_windows == 2 && F.z1() > cube.z1()) continue;
						wall_seg_t seg((F.d[d][0] - llc[d])*sz_d_inv, (F.d[d][1] - llc[d])*sz_d_inv, (F.d[i][0] - llc[i])*sz_i_inv, (F.d[i][1] - llc[i])*sz_i_inv);

						if (do_xy_clip) { // clip the horizontal vertices of the quad that was just added to make the windows line up with the other side
							float &lo(clip_d ? seg.dlo : seg.ilo), &hi(clip_d ? seg.dhi : seg.ihi);
							if (lo > 0.01) {lo = ceil (lo*num_windows - window_h_border)/num_windows;} // if clipped on lo edge, round up   to nearest whole window
							if (hi < 0.99) {hi = floor(hi*num_windows + window_h_border)/num_windows;} // if clipped on hi edge, round down to nearest whole window
							if (hi - lo < 0.01f) continue; // no space for a window after clip (optimization)
						}
						segs.emplace_back(seg);
					} // for f
				}
				else {
					segs.push_back(wall_seg_t()); // single seg
				}
				if (clip_windows && bg.has_chimney == 2) { // remove windows blocked by the chimney; there can be at most one segment
					cube_t block(bg.get_chimney());
					block.union_with_cube(bg.get_fireplace()); // merge with fireplace

					if (fabs(cube.d[n][j] - block.d[n][!j]) < 0.01*window_vspacing) { // chimney is adjacent to this side, or close enough
						// remove vertical columns of whole windows that overlap the chimney or fireplace
						unsigned const cdim(clip_d ? d : i); // clip dimension
						float const ts(tscale[bool(st) ^ clip_d ^ 1]), toff(cdim ? tex.tyoff : tex.txoff); // texture scale and offset for clip dim
						float t_lo(ts*(cube.d[cdim][0] + tex_vert_off[cdim]) + toff), t_hi(ts*(cube.d[cdim][1] + tex_vert_off[cdim]) + toff);
						clip_low_high_tc(t_lo, t_hi);
						float const num_windows(round_fp(t_hi - t_lo)); // window count for this wall; should be an exact integer

						if (num_windows > 0) { // we have at least one window
							float const edge_buffer(0.9*bg.get_window_h_border()/num_windows); // slightly smaller than the space to the sides of a window, in parametric space
							float c_lo((block.d[cdim][0] - llc[cdim])/sz[cdim]), c_hi((block.d[cdim][1] - llc[cdim])/sz[cdim]); // parametric bounds of chimney
							c_lo += edge_buffer; c_hi -= edge_buffer; // space to the left and right of the window is allowed to overlap the chimney/fireplace
							c_lo  = floor(num_windows*c_lo)/num_windows; // round down to an exact window boundary
							c_hi  = ceil (num_windows*c_hi)/num_windows; // round up   to an exact window boundary

							for (auto s = segs.begin(); s != segs.end(); ++s) {
								float &lo(clip_d ? s->dlo : s->ilo), &hi(clip_d ? s->dhi : s->ihi);
								if (lo > c_hi || hi < c_lo) continue; // doesn't overlap the chimney (side clipped to door may partially overlap the chimney)
								wall_seg_t s2(*s);
								hi = c_lo; // *s becomes lo seg
								(clip_d ? s2.dlo : s2.ilo) = c_hi; // s2 becomes hi seg
								if (s2.is_normalized()) {segs.push_back(s2);} // Note: s->is_normalized() check is done in the segs iteration below
								break; // s is invalidated, have to break; there shouldn't be any other segs spanning chimney anyway
							} // for s
						}
					}
				}
				for (auto s = segs.begin(); s != segs.end(); ++s) {
					wall_seg_t const &seg(*s);
					if (!seg.is_normalized()) continue; // zero area or denormalized - skip (can get here due to chimney clipping)
					unsigned const ix(verts.size()); // first vertex of this quad
					pt[d] = seg.dlo;
					pt[i] = (j ? seg.ilo : seg.ihi); // need to orient the vertices differently for each side
					//if (bg.roof_recess > 0.0 && n == 2 && j == 1) {pt.z -= bg.roof_recess*cube.dz();}
					EMIT_VERTEX(); // 0 !j
					pt[i] = (j ? seg.ihi : seg.ilo);
					EMIT_VERTEX(); // 0 j
					pt[d] = seg.dhi;
					EMIT_VERTEX(); // 1 j
					pt[i] = (j ? seg.ilo : seg.ihi);
					EMIT_VERTEX(); // 1 !j
					float const offset((j ? 1.0 : -1.0)*offset_val);

					if (((i == 2) ? seg.ilo : seg.dlo) == 0.0 && (door_sides & (1 << (2*n + j)))) { // clip zval to exclude door z-range (except for top segment)
						for (unsigned k = ix; k < ix+4; ++k) {
							auto &v(verts[k]);
							float const delta(door_ztop - v.v.z);
							if (v.v.z < door_ztop) {v.v.z = door_ztop;} // make all windows start above the door
							// move slightly away from the building wall to avoid z-fighting (vertex is different from building and won't have same depth)
							if (is_rotated) {v.v += offset*norm;} else {v.v[n] += offset;}
							if (delta > 0.0) {v.t[1] += tscale[1]*delta;} // recalculate tex coord
						}
					}
					else if (clip_windows && DRAW_WINDOWS_AS_HOLES) { // move slightly away from the building wall to avoid z-fighting
						for (unsigned k = ix; k < ix+4; ++k) {
							if (is_rotated) {verts[k].v += offset*norm;} else {verts[k].v[n] += offset;}
						}
					}
					if (clip_windows && n < 2) { // clip the texture coordinates of the quad that was just added (side of building)
						clip_low_high_tc(verts[ix+0].t[!st], verts[ix+1].t[!st]);
						clip_low_high_tc(verts[ix+2].t[!st], verts[ix+3].t[!st]);
						clip_low_high_tc(verts[ix+0].t[ st], verts[ix+3].t[ st]);
						clip_low_high_tc(verts[ix+1].t[ st], verts[ix+2].t[ st]);
						// Note: if we're drawing windows, and either of the texture coords have zero ranges, we can drop this quad; but this is uncommon and maybe not worth the trouble
					}
					if (clamp_cube != nullptr && *clamp_cube != cube && n < 2 && !is_rotated) { // x/y dims only; can't apply to rotated building
						unsigned const dim((i == 2) ? d : i); // x/y
						unsigned const sec_vix(ix + (st ? 1 : 3)); // opposite vertex in this dim
						float const delta_tc(verts[sec_vix].t[0]   - verts[ix].t[0]  );
						float const delta_v (verts[sec_vix].v[dim] - verts[ix].v[dim]);
						assert(delta_v != 0.0);
						float const fin_tscale(delta_tc/delta_v); // tscale[0] post-clip

						for (unsigned k = ix; k < ix+4; ++k) {
							auto &v(verts[k]);
							float &val(v.v[dim]);
							float const dlo(clamp_cube->d[dim][0] - val), dhi(val - clamp_cube->d[dim][1]); // dists outside clamp_cube
							if (dlo > 0.0) {val += dlo; v.t[0] += fin_tscale*dlo;} // shift pos
							if (dhi > 0.0) {val -= dhi; v.t[0] -= fin_tscale*dhi;} // shift neg
						} // for k
					}
				} // for seg s
			} // for j
		} // for i
	}

	void add_cube(building_t const &bg, cube_t const &cube, tid_nm_pair_t const &tex, colorRGBA const &color,
		bool swap_txy, unsigned dim_mask, bool skip_bottom, bool skip_top, bool ws_texture)
	{
		assert(dim_mask != 0); // must draw at least some face
		auto &verts(get_verts(tex));
		vector3d const sz(cube.get_size()), llc(cube.get_llc()); // move origin from center to min corner
		vert_norm_comp_tc_color vert;
		vert.set_c4(color);
		float const tscale[2] = {tex.tscale_x, tex.tscale_y};
		unsigned const verts_start(verts.size());
		bool const is_rotated(bg.is_rotated());
		vector3d norm; // used for rotated buildings

		for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
			unsigned const n((i+2)%3), d((i+1)%3), st(bool(i&1) ^ swap_txy); // n = dim of normal, i/d = other dims
			if (!(dim_mask & (1<<n))) continue; // check for enabled dims

			for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
				if (skip_bottom && n == 2 && j == 0) continue; // skip bottom side
				if (skip_top    && n == 2 && j == 1) continue; // skip top    side
				if (n < 2 && is_rotated) {set_rotated_normal(vert, bg, norm, n, j);} // XY only
				else {vert.n[i] = 0; vert.n[d] = 0; vert.n[n] = (j ? 127 : -128);} // -1.0 or 1.0
				point pt; // parameteric position within cube in [vec3(0), vec3(1)]
				pt[n] = j; // our cube face, in direction of normal
				pt[d] = 0.0;
				pt[i] = !j; // need to orient the vertices differently for each side
				EMIT_VERTEX_SIMPLE(); // 0 !j
				pt[i] = j;
				EMIT_VERTEX_SIMPLE(); // 0 j
				pt[d] = 1.0;
				EMIT_VERTEX_SIMPLE(); // 1 j
				pt[i] = !j;
				EMIT_VERTEX_SIMPLE(); // 1 !j
			} // for j
		} // for i
		if (is_rotated) {
			point const center(bg.bcube.get_cube_center());
			for (auto i = (verts.begin() + verts_start); i != verts.end(); ++i) {bg.do_xy_rotate(center, i->v);}
		}
	}

	void add_fence(building_t const &bg, cube_t const &fence, tid_nm_pair_t const &tex, colorRGBA const &color, bool mult_sections) {
		bool const dim(fence.dy() < fence.dx()); // smaller/separating dim
		float const length(fence.get_sz_dim(!dim)), height(fence.dz());
		float const post_width(fence.get_sz_dim(dim)), post_hwidth(0.5*post_width), beam_hwidth(0.5*post_hwidth), beam_hheight(1.0*post_hwidth);
		unsigned const num_sections(ceil(0.3*length/height)), num_posts(num_sections + 1), num_beams(2); // could also use 3 beams
		float const post_spacing((length - post_width)/num_sections), beam_spacing(height/(num_beams+0.5f));
		cube_t post(fence); // copy dim and Z values
		cube_t beam(fence); // copy dim and !dim values
		beam.expand_in_dim(!dim, -post_width); // remove overlap with end posts at both ends
		beam.expand_in_dim( dim, (beam_hwidth - post_hwidth));
		unsigned skip_ix(num_posts); // start at an invalid value

		if (mult_sections && dim == 0) { // skip end post on the corner in dim=0 because it's duplicated with the corner post in the other dim
			float const dmin(post_width + ((bg.has_chimney == 2) ? bg.get_chimney().get_sz_dim(!dim) : 0.0)); // include chimney width, can increase the house bcube beyond the fence
			if      (fabs(bg.bcube.d[!dim][1] - fence.d[!dim][1]) < dmin) {beam.d[!dim][1] += post_hwidth; skip_ix = num_posts-1;} // skip last post
			else if (fabs(bg.bcube.d[!dim][0] - fence.d[!dim][0]) < dmin) {beam.d[!dim][0] -= post_hwidth; skip_ix = 0;} // skip first post
		}
		for (unsigned i = 0; i < num_posts; ++i) { // add posts
			set_wall_width(post, (fence.d[!dim][0] + post_hwidth + i*post_spacing), post_hwidth, !dim);
			if (i != skip_ix) {add_cube(bg, post, tex, color, 0, 7, 1, 0, 1);} // skip bottom, ws_texture=1
		}
		for (unsigned i = 0; i < num_beams; ++i) { // add beams
			set_wall_width(beam, (fence.z1() + (i+1)*beam_spacing), beam_hheight, 2); // set beam zvals
			add_cube(bg, beam, tex, color, 0, (4U + (1U<<unsigned(dim))), 0, 0, 1); // skip !dim sides, ws_texture=1
		}
	}

	void add_roof_dome(point const &pos, float rx, float ry, tid_nm_pair_t const &tex, colorRGBA const &color, bool onion) {
		auto &verts(get_verts(tex));
		color_wrapper cw(color);
		unsigned const ndiv(N_SPHERE_DIV);
		float const ravg(0.5f*(rx + ry)), t_end(onion ? 1.0 : 0.5);
		point center(pos);
		if (onion) {center.z += 0.5*ravg; rx *= 1.2; ry *= 1.2;} // move up slightly and increase radius
		float const arx(rx/ravg), ary(ry/ravg);
		sphere_verts.clear();
		sd_sphere_d sd(all_zeros, ravg, ndiv);
		sd.gen_points_norms_static(0.0, 1.0, 0.0, t_end); // top half hemisphere dome
		sd.get_quad_points(sphere_verts, nullptr, 0, 0.0, 1.0, 0.0, t_end); // quads
			
		for (auto i = sphere_verts.begin(); i != sphere_verts.end(); ++i) {
			i->v.y *= ary;
			i->v.x *= arx;
			if (onion && i->v.z > 0.0) {i->v.z += 0.05f*ravg*(1.0f/(1.01f - i->v.z/ravg) - 1.0f);} // form a point at the top
			verts.emplace_back(vert_norm_comp_tc((i->v + center), i->n, i->t[0]*tex.tscale_x, i->t[1]*tex.tscale_y), cw);
		}
	}

	unsigned num_verts() const {
		unsigned num(0);
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {num += i->num_verts();}
		return num;
	}
	unsigned num_tris() const {
		unsigned num(0);
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {num += i->num_tris();}
		return num;
	}
	void upload_to_vbos() {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->upload_to_vbos();}}
	void clear_vbos    () {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->clear_vbos();}}
	void clear         () {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->clear();}}
	unsigned get_num_draw_blocks() const {return to_draw.size();}
	void finalize(unsigned num_tiles) {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->finalize(num_tiles);}}
	
	// tex_filt_mode: 0=draw everything, 1=draw exterior walls only, 2=draw everything but exterior walls, 3=draw everything but exterior walls and doors
	void draw(shader_t &s, bool shadow_only, bool direct_draw_no_vbo=0, int tex_filt_mode=0, vertex_range_t const *const exclude=nullptr) {
		tid_nm_pair_dstate_t state(s);

		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {
			if (tex_filt_mode && (tid_mapper.is_ext_wall_tid(i->tex.tid) != (tex_filt_mode == 1))) continue; // not an ext wall texture, skip
			if (tex_filt_mode == 3 && building_texture_mgr.is_door_tid(i->tex.tid)) continue; // door texture, skip
			bool const use_exclude(exclude && exclude->draw_ix == int(i - to_draw.begin()));
			i->draw_all_geom(state, shadow_only, direct_draw_no_vbo, (use_exclude ? exclude : nullptr));
		}
	}
	void draw_tile(shader_t &s, unsigned tile_id, bool shadow_only=0) {
		tid_nm_pair_dstate_t state(s);
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->draw_geom_tile(state, tile_id, shadow_only);}
	}
	void draw_block(shader_t &s, unsigned ix, bool shadow_only, vertex_range_t const *const exclude=nullptr) {
		if (ix >= to_draw.size()) return;
		tid_nm_pair_dstate_t state(s);
		to_draw[ix].draw_all_geom(state, shadow_only, 0, exclude);
	}
	void draw_quad_geom_range(tid_nm_pair_dstate_t &state, vertex_range_t const &range, bool shadow_only=0) {
		if (range.draw_ix < 0 || (unsigned)range.draw_ix >= to_draw.size()) return; // invalid range, skip
		to_draw[range.draw_ix].draw_quad_geom_range(state, range, shadow_only);
	}
	void draw_quads_for_draw_range(shader_t &s, draw_range_t const &draw_range, bool shadow_only=0) {
		tid_nm_pair_dstate_t state(s);
		for (unsigned i = 0; i < MAX_DRAW_BLOCKS; ++i) {draw_quad_geom_range(state, draw_range.vr[i], shadow_only);}
	}
}; // end building_draw_t


// *** Drawing ***

int get_building_door_tid(unsigned type) { // exterior doors, and interior doors of limited types
	switch(type) {
	case tquad_with_ix_t::TYPE_HDOOR : return building_texture_mgr.get_hdoor_tid ();
	//case tquad_with_ix_t::TYPE_ODOOR : return building_texture_mgr.get_odoor_tid (); // enable when this texture is ready
	case tquad_with_ix_t::TYPE_ODOOR : return building_texture_mgr.get_hdoor_tid ();
	case tquad_with_ix_t::TYPE_RDOOR : return building_texture_mgr.get_hdoor_tid ();
	case tquad_with_ix_t::TYPE_BDOOR : return building_texture_mgr.get_bdoor_tid ();
	case tquad_with_ix_t::TYPE_BDOOR2: return building_texture_mgr.get_bdoor2_tid();
	case tquad_with_ix_t::TYPE_GDOOR : return building_texture_mgr.get_gdoor_tid ();
	default: assert(0);
	}
	return -1; // never gets here
}

void add_driveway_or_porch(building_draw_t &bdraw, building_t const &building, cube_t const &c, colorRGBA const &color) {
	if (c.is_all_zeros()) return;
	tid_nm_pair_t const tex(get_texture_by_name("roads/asphalt.jpg"), 16.0);
	bdraw.add_section(building, 0, c, tex, color, 7, 1, 0, 1, 0); // all dims, skip bottom, no AO
}

void building_t::get_all_drawn_verts(building_draw_t &bdraw, bool get_exterior, bool get_interior, bool get_int_ext_walls) {

	assert(get_exterior || get_interior || get_int_ext_walls); // must be at least one of these
	if (!is_valid()) return; // invalid building
	building_mat_t const &mat(get_material());
	if (detail_color == BLACK) {detail_color = roof_color;} // use roof color if not set

	if (get_exterior) { // exterior building parts
		bool const need_top_roof(roof_type == ROOF_TYPE_FLAT || roof_type == ROOF_TYPE_DOME || roof_type == ROOF_TYPE_ONION);
		
		for (auto i = parts.begin(); i != parts.end(); ++i) { // multiple cubes/parts/levels - no AO for houses
			if (is_basement(i)) continue; // don't need to draw the basement exterior walls since they should be underground
			
			if (has_chimney == 2 && (i+2 == parts.end())) { // fireplace; skip inside face (optimization); needs to be draw as part of the interior instead
				unsigned dim_mask(3); // disable faces: 8=x1, 16=x2, 32=y1, 64=y2
				if      (i->x1() <= bcube.x1()) {dim_mask |= 16;} // Note: may not work on rotated buildings
				else if (i->x2() >= bcube.x2()) {dim_mask |=  8;}
				else if (i->y1() <= bcube.y1()) {dim_mask |= 64;}
				else if (i->y2() >= bcube.y2()) {dim_mask |= 32;}
				tid_nm_pair_t const tp(mat.side_tex.get_scaled_version(2.0)); // smaller bricks
				bdraw.add_section(*this, 0, *i, tp, side_color, dim_mask, 0, 0, is_house, 0); // XY exterior walls
				bdraw.add_section(*this, 0, *i, tp, side_color, 4, 1, 0, 1, 0); // draw top of fireplace exterior, even if not wider than the chimney - should it be sloped?
				continue;
			}
			else if (has_chimney && (i+1 == parts.end())) { // chimney
				tid_nm_pair_t const tp(mat.side_tex.get_scaled_version(2.0)); // smaller bricks
				bdraw.add_section(*this, 0, *i, tp, side_color, 3, 0, 0, is_house, 0); // XY exterior walls
				float const wall_width(0.25*min(i->dx(), i->dy()));
				cube_t hole(*i);
				hole.expand_by_xy(-wall_width);
				cube_t sides[4]; // {-y, +y, -x, +x}
				subtract_cube_xy(*i, hole, sides);
				unsigned const face_masks[4] = {127-64, 127-32, 127-16, 127-8}; // enable XYZ but skip all but {+y, -y, +x, -x} in XY
				for (unsigned n = 0; n < 4; ++n) {bdraw.add_section(*this, 0, sides[n], tp, side_color, face_masks[n], 1, 0, 1, 0);} // skip bottom
				continue;
			}
			bdraw.add_section(*this, 1, *i, mat.side_tex, side_color, 3, 0, 0, is_house, 0); // XY exterior walls
			bool skip_top((!need_top_roof && (is_house || i+1 == parts.end())) || is_basement(i)); // don't add the flat roof for the top part in this case
			// skip the bottom of stacked cubes (not using ground_floor_z1); need to draw the porch roof, so test i->dz()
			bool const is_stacked(num_sides == 4 && i->z1() > bcube.z1() && i->dz() > 0.5f*get_window_vspace());
			if (is_stacked && skip_top) continue; // no top/bottom to draw

			if (!is_house && !skip_top && interior) {
				if (clip_part_ceiling_for_stairs(*i, bdraw.temp_cubes, bdraw.temp_cubes2)) {
					for (auto c = bdraw.temp_cubes.begin(); c != bdraw.temp_cubes.end(); ++c) { // add floors after removing stairwells
						bdraw.add_section(*this, 1, *c, mat.roof_tex, roof_color, 4, 1, 0, is_house, 0); // only Z dim
					}
					skip_top = 1;
					if (is_stacked) continue; // no top/bottom to draw
				}
			}
			bdraw.add_section(*this, 1, *i, mat.roof_tex, roof_color, 4, is_stacked, skip_top, is_house, 0); // only Z dim
		} // for i
		for (auto i = roof_tquads.begin(); i != roof_tquads.end(); ++i) {
			if (i->type == tquad_with_ix_t::TYPE_HELIPAD) {
				bdraw.add_tquad(*this, *i, bcube, building_texture_mgr.get_helipad_tid(), WHITE);
			}
			else if (i->type == tquad_with_ix_t::TYPE_SOLAR) {
				bdraw.add_tquad(*this, *i, bcube, building_texture_mgr.get_solarp_tid(), colorRGBA(0.6, 0.6, 0.6)); // panel is too bright compared to the roof, use a darker color
			}
			else if (i->type == tquad_with_ix_t::TYPE_TRIM) {
				bdraw.add_tquad(*this, *i, bcube, tid_nm_pair_t(), LT_GRAY); // untextured
			}
			else if (i->type == tquad_with_ix_t::TYPE_ROOF || i->type == tquad_with_ix_t::TYPE_ROOF_ACC) { // use roof texture
				bdraw.add_tquad(*this, *i, bcube, mat.roof_tex.get_scaled_version(2.0), roof_color);
			}
			else { // use wall texture
				bdraw.add_tquad(*this, *i, bcube, mat.side_tex, side_color);
			}
		}
		for (auto i = details.begin(); i != details.end(); ++i) { // draw roof details
			if (i->type == ROOF_OBJ_AC) {
				bool const swap_st(i->dx() > i->dy());
				bool const tex_id((details.size() + parts.size() + mat_ix) & 1); // somewhat of a hash of various things; deterministic
				int const ac_tid(tex_id ? building_texture_mgr.get_ac_unit_tid1() : building_texture_mgr.get_ac_unit_tid2());
				bdraw.add_cube(*this, *i, tid_nm_pair_t(ac_tid, -1, (swap_st ? 1.0 : -1.0), 1.0), WHITE, swap_st, 4, 1, 0, 0); // Z, skip bottom, ws_texture=0
				bdraw.add_cube(*this, *i, tid_nm_pair_t(ac_tid, -1, 0.3, 1.0), WHITE, 0, 3, 1, 0, 0); // XY with stretched texture, ws_texture=0
				continue;
			}
			bool const skip_bot(i->type != ROOF_OBJ_SCAP), pointed(i->type == ROOF_OBJ_ANT); // draw antenna as a point
			building_t b(building_geom_t(4, rot_sin, rot_cos, pointed)); // cube
			b.bcube = bcube;
			tid_nm_pair_t tex;
			colorRGBA color;

			if (i->type == ROOF_OBJ_WALL && mat.add_windows) { // wall of brick/block building, use side color
				tex   = mat.side_tex;
				color = side_color;
			}
			else { // otherwise use roof color
				tex   = mat.roof_tex.get_scaled_version(1.5);
				color = detail_color*(pointed ? 0.5 : 1.0);
			}
			bdraw.add_section(b, 0, *i, tex, color, 7, skip_bot, 0, 1, 0); // all dims, no AO
		}
		for (auto i = doors.begin(); i != doors.end(); ++i) { // these are the exterior doors
			colorRGBA const &dcolor((i->type == tquad_with_ix_t::TYPE_GDOOR) ? WHITE : door_color); // garage doors are always white
			bdraw.add_tquad(*this, *i, bcube, tid_nm_pair_t(get_building_door_tid(i->type), -1, 1.0, 1.0), dcolor);
		}
		for (auto i = fences.begin(); i != fences.end(); ++i) {
			bdraw.add_fence(*this, *i, tid_nm_pair_t(WOOD_TEX, 0.4f/min(i->dx(), i->dy())), WHITE, (fences.size() > 1));
		}
		add_driveway_or_porch(bdraw, *this, driveway, LT_GRAY);
		add_driveway_or_porch(bdraw, *this, porch,    LT_GRAY);

		if (roof_type == ROOF_TYPE_DOME || roof_type == ROOF_TYPE_ONION) {
			cube_t const &top(parts.back()); // top/last part
			point const center(top.get_cube_center());
			float const dx(top.dx()), dy(top.dy()), tscale(4.0f/(dx + dy));
			tid_nm_pair_t tex(mat.roof_tex); // use a different dome texture?
			tex.tscale_x *= tscale; tex.tscale_y *= tscale;
			bdraw.add_roof_dome(point(center.x, center.y, top.z2()), 0.5*dx, 0.5*dy, tex, roof_color*1.5, (roof_type == ROOF_TYPE_ONION));
		}
	}
	if (get_int_ext_walls /*&& interior*/) { // only needed if building has an interior?
		ext_side_qv_range.draw_ix = bdraw.get_to_draw_ix(mat.wall_tex);
		ext_side_qv_range.start   = bdraw.get_num_verts (mat.wall_tex);

		for (auto i = parts.begin(); i != get_real_parts_end_inc_sec(); ++i) { // multiple cubes/parts/levels, room parts only, no AO
			if (!is_basement(i)) {bdraw.add_section(*this, 1, *i, mat.wall_tex, wall_color, 3, 0, 0, 1, 0);} // XY
		}
		ext_side_qv_range.end = bdraw.get_num_verts(mat.wall_tex);

		if (has_basement()) {
			tid_nm_pair_t tp;

			if ((mat_ix + parts.size()) & 1) { // randomly select one of two textures
				tp = tid_nm_pair_t(CBLOCK_TEX, get_texture_by_name("normal_maps/cblock_NRM.jpg", 1), 1.0, 1.0);
			}
			else {
				tp = tid_nm_pair_t(get_texture_by_name("cblock2.jpg"), get_texture_by_name("normal_maps/cblock2_NRM.jpg", 1), 1.0, 1.0);
			}
			bdraw.add_section(*this, 1, parts[basement_part_ix], tp, WHITE, 3, 0, 0, 1, 0); // XY, always white
		}
	}
	if (get_interior && interior != nullptr) { // interior building parts
		bdraw.begin_draw_range_capture();
		tid_nm_pair_t const &ceil_tex(is_house ? mat.house_ceil_tex   : mat.ceil_tex   );
		colorRGBA const &ceil_color  (is_house ? mat.house_ceil_color : mat.ceil_color );

		for (auto i = interior->floors.begin(); i != interior->floors.end(); ++i) { // 600K T
			tid_nm_pair_t floor_tex;
			colorRGBA floor_color;

			if (is_house) {
				if (has_sec_bldg() && get_sec_bldg().contains_cube(*i)) {floor_tex = tid_nm_pair_t(get_texture_by_name("roads/asphalt.jpg"), 16.0);} // garage or shed
				else if (i->z2() < ground_floor_z1) {floor_tex = mat.basement_floor_tex;} // basement
				else {floor_tex = mat.house_floor_tex;}
				floor_color = mat.house_floor_color;
			}
			else {floor_tex = mat.floor_tex; floor_color = mat.floor_color;} // office
			bdraw.add_section(*this, 0, *i, floor_tex, floor_color, 4, 1, 0, 1, 0); // no AO; skip_bottom; Z dim only
		}
		for (auto i = interior->ceilings.begin(); i != interior->ceilings.end(); ++i) { // 600K T
			// skip top surface of all but top floor ceilings if the roof is sloped;
			// if this is an office building, the ceiling could be at a lower floor with a flat roof even if the highest floor has a sloped roof, so we must skip it
			bool const skip_top(roof_type == ROOF_TYPE_FLAT || !is_house || !(interior->top_ceilings_mask & (uint64_t(1) << ((i - interior->ceilings.begin()) & 63))));
			bool const is_basement(is_house && i->z1() < ground_floor_z1); // use wall texture for basement ceilings, not fancy ceiling texture
			bdraw.add_section(*this, 0, *i, (is_basement ? mat.wall_tex : ceil_tex), ceil_color, 4, 0, skip_top, 1, 0); // no AO; Z dim only
		}
		// minor optimization: don't need shadows for ceilings because lights only point down; assumes ceil_tex is only used for ceilings; not true for all houses
		if (!is_house) {bdraw.set_no_shadows_for_tex(mat.ceil_tex);}

		for (unsigned dim = 0; dim < 2; ++dim) { // Note: can almost pass in (1U << dim) as dim_filt, if it wasn't for door cutouts (2.2M T)
			for (auto i = interior->walls[dim].begin(); i != interior->walls[dim].end(); ++i) {
				colorRGBA const &color((i->z1() < ground_floor_z1) ? WHITE : wall_color); // basement walls are always white
				bdraw.add_section(*this, 0, *i, mat.wall_tex, color, 3, 0, 0, 1, 0); // no AO; X/Y dims only
			}
		}
		// Note: stair/elevator landings can probably be drawn in room_geom along with stairs, though I don't think there would be much benefit in doing so
		for (auto i = interior->landings.begin(); i != interior->landings.end(); ++i) { // added per-floor (530K T)
			unsigned dim_mask(3); // disable faces: 8=x1, 16=x2, 32=y1, 64=y2
			if (i->for_elevator) {dim_mask |= (120 - (1 << (i->get_face_id() + 3)));} // disable all but the open side of the elevator
			bdraw.add_section(*this, 0, *i, mat.wall_tex, mat.wall_color, dim_mask, 0, 0, 1, 0, 0.0, 0, 1.0, 1); // no AO; X/Y dims only, inverted normals
		}
		for (auto i = interior->elevators.begin(); i != interior->elevators.end(); ++i) {
			bool const dim(i->dim), dir(i->dir);
			float const spacing(i->get_wall_thickness()), frame_width(i->get_frame_width()); // space between inner/outer walls + frame around door
			unsigned dim_mask(3); // x and y dims enabled
			dim_mask |= (1 << (i->get_door_face_id() + 3)); // disable the face for the door opening
			bdraw.add_section(*this, 0, *i, mat.wall_tex, mat.wall_color, dim_mask, 0, 0, 1, 0); // outer elevator is textured like the walls
			cube_t entrance(*i);
			entrance.d[dim][!dir] = entrance.d[dim][dir] + (dir ? -1.0f : 1.0f)*spacing; // set correct thickness

			for (unsigned d = 0; d < 2; ++d) { // add frame on both sides of the door opening
				cube_t frame(entrance); // one side
				frame.d[!dim][d] = frame.d[!dim][!d] + (d ? 1.0f : -1.0f)*frame_width; // set position
				unsigned dim_mask2(3); // x and y dims enabled
				dim_mask2 |= (1 << (2*(!dim) + (!d) + 3)); // 3 faces drawn
				bdraw.add_section(*this, 0, frame, mat.wall_tex, mat.wall_color, dim_mask2, 0, 0, 1, 0);
			}
			cube_t inner_cube(*i);
			inner_cube.expand_by_xy(-spacing);
			// add interior of elevator by drawing the inside of the cube with a slightly smaller size, with invert_normals=1; normal mapped?
			tid_nm_pair_t wall_tex(FENCE_TEX, -1, 16.0, 16.0);
			wall_tex.set_specular(0.5, 20.0);
			bdraw.add_section(*this, 0, inner_cube, wall_tex, WHITE, dim_mask, 0, 0, 1, 0, 0.0, 0, 1.0, 1);
			// Note elevator doors are dynamic and are drawn as part of room_geom
		} // for i
		// Note: interior doors are drawn as part of room_geom
		bdraw.end_draw_range_capture(interior->draw_range); // 80MB, 394MB, 836ms
	} // end interior case
}

template<typename T> void building_t::add_door_verts(cube_t const &D, T &drawer, uint8_t door_type,
	bool dim, bool dir, bool opened, bool opens_out, bool exterior, bool on_stairs, bool hinge_side) const
{
	float const ty((exterior || SPLIT_DOOR_PER_FLOOR) ? 1.0 : D.dz()/get_window_vspace()); // tile door texture across floors for unsplit interior doors
	int const type(tquad_with_ix_t::TYPE_IDOOR); // always use interior door type, even for exterior door, because we're drawing it in 3D inside the building
	bool const opens_up(door_type == tquad_with_ix_t::TYPE_GDOOR);
	// exclude the frame on open interior doors
	bool const exclude_frame((door_type == tquad_with_ix_t::TYPE_HDOOR || door_type == tquad_with_ix_t::TYPE_ODOOR) && !exterior && opened);
	unsigned const num_edges(opens_up ? 4 : 2);
	int const tid(get_building_door_tid(door_type));
	float const half_thickness(opens_up ? 0.01*D.dz() : 0.5*DOOR_THICK_TO_WIDTH*D.get_sz_dim(!dim));
	unsigned const num_sides((door_type == tquad_with_ix_t::TYPE_BDOOR || door_type == tquad_with_ix_t::TYPE_BDOOR2) ? 2 : 1); // double doors for office building exterior
	tid_nm_pair_t const tp(tid, -1, 1.0f/num_sides, ty);
	colorRGBA const &color((exterior && !opens_up) ? door_color : WHITE); // garage doors are always white

	for (unsigned side = 0; side < num_sides; ++side) { // {right, left}
		cube_t dc(D);
		if (num_sides == 2) {dc.d[!dim][bool(side) ^ dim ^ dir ^ 1] = 0.5f*(D.d[!dim][0] + D.d[!dim][1]);} // split door in half
		// we don't want to draw the open stairs door because it may get in the way, but we need to overwrite the previous verts, so make it zero area
		if (opened && on_stairs) {dc.z2() = dc.z1();}
		bool const int_other_side(exterior ? 0 : hinge_side);
		bool const swap_sides(exterior ? (side == 0) : int_other_side); // swap sides for right half of exterior door
		tquad_with_ix_t const door(set_door_from_cube(dc, dim, dir, type, 0.0, exterior, opened, opens_out, opens_up, swap_sides));
		vector3d const normal(door.get_norm());
		tquad_with_ix_t door_edges[4] = {door, door, door, door}; // most doors will only use 2 of these

		for (unsigned d = 0; d < 2; ++d) {
			tquad_with_ix_t door_side(door);
			vector3d const offset((d ? -1.0 : 1.0)*half_thickness*normal);
			for (unsigned n = 0; n < 4; ++n) {door_side.pts[n] += offset;}

			for (unsigned e = 0; e < num_edges; ++e) {
				unsigned const ixs[4][2] = {{1, 2}, {3, 0}, {0, 1}, {2, 3}};
				door_edges[e].pts[2*d+1] = door_side.pts[ixs[e][ d]];
				door_edges[e].pts[2*d+0] = door_side.pts[ixs[e][!d]];
			}
			if (d == 1) { // back face
				swap(door_side.pts[0], door_side.pts[1]);
				swap(door_side.pts[2], door_side.pts[3]);
				door_side.type = (is_house ? (unsigned)tquad_with_ix_t::TYPE_IDOOR2 : (unsigned)tquad_with_ix_t::TYPE_ODOOR2); // back side of house/office door
			}
			drawer.add_tquad(*this, door_side, bcube, tp, color, (int_other_side && !opened), exclude_frame, 0); // invert_tc_x=xxx, no_tc=0
		} // for d
		if (opened || on_stairs) { // add untextured door edges; only needed for open doors or doors at the bottom of basement stairs
			for (unsigned e = 0; e < num_edges; ++e) {
				drawer.add_tquad(*this, door_edges[e], bcube, tp, color, 0, 0, 1); // invert_tc_x=0, exclude_frame=0, no_tc=1, will use a single texel from the corner of the door texture
			}
		}
	} // for side
}

// explicit template specialization
template void building_t::add_door_verts(cube_t const &D, building_room_geom_t &drawer, uint8_t door_type,
	bool dim, bool dir, bool opened, bool opens_out, bool exterior, bool on_stairs, bool hinge_side) const;

void building_t::get_all_drawn_window_verts(building_draw_t &bdraw, bool lights_pass, float offset_scale, point const *const only_cont_pt_in) const {

	if (!is_valid()) return; // invalid building
	point only_cont_pt;

	if (only_cont_pt_in) {
		only_cont_pt = *only_cont_pt_in;
		maybe_inv_rotate_point(only_cont_pt);
	}
	building_mat_t const &mat(get_material());

	if (!global_building_params.windows_enabled() || (lights_pass ? !mat.add_wind_lights : !mat.add_windows)) { // no windows for this material
		if (only_cont_pt_in) {cut_holes_for_ext_doors(bdraw, only_cont_pt, 0xFFFF);} // still need to draw holes for doors
		return;
	}
	int const window_tid(building_texture_mgr.get_window_tid());
	if (window_tid < 0) return; // not allocated - error?
	if (mat.wind_xscale == 0.0 || mat.wind_yscale == 0.0) return; // no windows for this material?
	tid_nm_pair_t tex(window_tid, -1, mat.get_window_tx(), mat.get_window_ty(), mat.wind_xoff, mat.wind_yoff);
	colorRGBA color;

	if (lights_pass) { // slight yellow-blue tinting using bcube x1/y1 as a hash
		float const tint(0.2*fract(100.0f*(bcube.x1() + bcube.y1())));
		color = colorRGBA((1.0 - tint), (1.0 - tint), (0.8 + tint), 1.0);
	} else {color = mat.window_color;}
	// only clip non-city windows; city building windows tend to be aligned with the building textures (maybe should be a material option?)
	int const clip_windows(mat.no_city ? (is_house ? 2 : 1) : 0);
	float const floor_spacing(get_window_vspace()), door_ztop(doors.empty() ? 0.0f : (EXACT_MULT_FLOOR_HEIGHT ? (ground_floor_z1 + floor_spacing) : doors.front().pts[2].z));
	unsigned draw_parts_mask(0);
	bool room_with_stairs(0);
	cube_t cont_part; // part containing the point

	if (only_cont_pt_in) {
		cont_part = get_part_containing_pt(only_cont_pt);
		room_with_stairs = room_containing_pt_has_stairs(only_cont_pt);
	}
	for (auto i = parts.begin(); i != get_real_parts_end_inc_sec(); ++i) { // multiple cubes/parts/levels, excluding chimney/porch/etc.
		if (is_basement(i)) continue; // skip the basement
		cube_t draw_part;
		cube_t const *clamp_cube(nullptr);

		if (only_cont_pt_in && !i->contains_pt(only_cont_pt)) { // not the part containing the point
			if (room_with_stairs && are_parts_stacked(*i, cont_part)) { // windows may be visible through stairs in rooms with stacked parts
				draw_part = cont_part;
				draw_part.intersect_with_cube_xy(*i);
				clamp_cube = &draw_part;
			}
			else {
				if (i->z2() < only_cont_pt.z || i->z1() > only_cont_pt.z) continue; // z-range not contained, skip
				bool skip(0);

				for (unsigned d = 0; d < 2; ++d) {
					if (i->d[ d][0] != cont_part.d[ d][1] && i->d[ d][1] != cont_part.d[ d][0]) continue; // not adj in dim d
					if (i->d[!d][0] >= cont_part.d[!d][1] || i->d[!d][1] <= cont_part.d[!d][0]) continue; // no overlap in dim !d
					if (i->d[!d][1] < only_cont_pt[!d] || i->d[!d][0] > only_cont_pt[!d]) {skip = 1; break;} // other dim range not contained, skip
					draw_part = *i; // deep copy
					max_eq(draw_part.d[!d][0], cont_part.d[!d][0]); // clamp to contained part in dim !d
					min_eq(draw_part.d[!d][1], cont_part.d[!d][1]);
					clamp_cube = &draw_part;
					break;
				} // for d
				if (skip || clamp_cube == nullptr) continue; // skip if adj in neither dim, always skip (but could check chained adj case)
			}
		}
		unsigned const part_ix(i - parts.begin());
		unsigned const dsides((part_ix < 4 && mat.add_windows) ? door_sides[part_ix] : 0); // skip windows on sides with doors, but only for buildings with windows
		bdraw.add_section(*this, 1, *i, tex, color, 3, 0, 0, 1, clip_windows, door_ztop, dsides, offset_scale, 0, clamp_cube); // XY, no_ao=1
		draw_parts_mask |= (1 << part_ix);

		// add ground floor windows next to doors
		if (dsides == 0) continue; // no doors
		float const space(0.25*floor_spacing), toler(0.1*floor_spacing);

		for (unsigned dim = 0; dim < 2; ++dim) {
			unsigned const num_windows(get_num_windows_on_side(i->d[!dim][0], i->d[!dim][1]));
			if (num_windows <= 1) continue; // no space to split the windows on this wall
			float const window_spacing(i->get_sz_dim(!dim)/num_windows), side_lo(i->d[!dim][0]), side_hi(i->d[!dim][1]);

			for (unsigned dir = 0; dir < 2; ++dir) {
				if (!(dsides & (1 << (2*dim + dir)))) continue; // no door on this side
				unsigned const dim_mask((1 << dim) + (1 << (3 + 2*dim + (1-dir)))); // enable only this dim but disable the other dir
				float const wall_pos(i->d[dim][dir]);
				vector<float> &wall_edges(bdraw.temp_wall_edges);
				wall_edges.clear();

				for (auto d = doors.begin(); d != doors.end(); ++d) {
					cube_t const c(d->get_bcube());
					if ((c.dy() < c.dx()) != dim) continue; // wrong dim
					if (c.d[dim][0]-toler > wall_pos || c.d[dim][1]+toler < wall_pos) continue; // door not on this wall
					float const door_lo(c.d[!dim][0]), door_hi(c.d[!dim][1]);
					if (door_lo > side_hi || door_hi < side_lo) continue; // door not on this part
					// align to an exact multiple of window period so that bottom floor windows line up with windows on the floors above and no walls are clipped
					if (wall_edges.empty()) {wall_edges.push_back(side_lo); wall_edges.push_back(side_hi);} // first wall, add end points
					wall_edges.push_back(door_lo - space); // low
					wall_edges.push_back(door_hi + space); // high
				} // for d
				if (wall_edges.empty()) continue; // no door, could be a non-main door (roof access, garage, shed) - does the mean there are no windows on this exterior wall?
				assert(!(wall_edges.size() & 1)); // must be an even number
				sort(wall_edges.begin(), wall_edges.end());

				for (unsigned e = 0; e < wall_edges.size(); e += 2) { // each pair of points should be the {left, right} edge of a wall section
					cube_t c(*i);
					c.d[!dim][0] = window_spacing*ceil ((wall_edges[e  ] - side_lo)/window_spacing) + side_lo; // lo, clamped to whole windows
					c.d[!dim][1] = window_spacing*floor((wall_edges[e+1] - side_lo)/window_spacing) + side_lo; // hi, clamped to whole windows
					float const wall_len(c.get_sz_dim(!dim));
					if (wall_len < 0.5*window_spacing) continue; // wall too small to add here
					c.z2() = door_ztop;
					tid_nm_pair_t tex2(tex);
					tex2.tscale_x = 0.5f*round_fp(wall_len/window_spacing)/wall_len;
					tex2.txoff    = -2.0*tex2.tscale_x*c.d[!dim][0];
					bdraw.add_section(*this, 1, c, tex2, color, dim_mask, 0, 0, 1, clip_windows, door_ztop, 0, offset_scale, 0, clamp_cube); // no_ao=1
				} // for e
			} // for dir
		} // for dim
	} // for i
	if (only_cont_pt_in) { // camera inside this building, cut out holes so that the exterior doors show through
		cut_holes_for_ext_doors(bdraw, only_cont_pt, draw_parts_mask);
	}
}

void building_t::get_all_drawn_window_verts_as_quads(vect_vnctcc_t &verts) const {
	building_draw_t bdraw; // should this be a static variable?
	get_all_drawn_window_verts(bdraw);
	bdraw.get_all_mat_verts(verts, 0); // combine quad verts across materials (should only be one)
}

void building_t::cut_holes_for_ext_doors(building_draw_t &bdraw, point const &contain_pt, unsigned draw_parts_mask) const {
	float const floor_spacing(get_window_vspace());

	for (auto d = doors.begin(); d != doors.end(); ++d) { // cut a hole for each door
		tquad_with_ix_t door(*d);
		move_door_to_other_side_of_wall(door, 0.3, 0); // move a bit in front of the normal interior door (0.3 vs. 0.2)
		cube_t const door_bcube(door.get_bcube());
		bool contained(0);

		for (auto i = parts.begin(); i != get_real_parts_end_inc_sec(); ++i) {
			if (!i->intersects(door_bcube)) continue;
			contained = ((draw_parts_mask & (1<<(i-parts.begin()))) != 0);
			if (contain_pt.z > (door_bcube.z1() + floor_spacing) && !i->contains_pt(contain_pt)) {contained = 0;} // camera in a different part on a floor above the door
			break;
		}
		if (!contained) continue; // part skipped, skip door as well
		clip_door_to_interior(door, 0);
		bdraw.add_tquad(*this, door, bcube, tid_nm_pair_t(WHITE_TEX), WHITE);
	} // for d
}

bool building_t::get_nearby_ext_door_verts(building_draw_t &bdraw, shader_t &s, point const &pos, float dist) { // for exterior doors
	tquad_with_ix_t door;
	int const door_ix(find_ext_door_close_to_point(door, pos, dist));
	register_open_ext_door_state(door_ix);
	if (door_ix < 0) return 0; // no nearby door
	move_door_to_other_side_of_wall(door, -1.01, 0); // move a bit further away from the outside of the building to make it in front of the orig door
	clip_door_to_interior(door, 1); // clip to floor
	bdraw.add_tquad(*this, door, bcube, tid_nm_pair_t(WHITE_TEX), WHITE);
	// draw the opened door
	building_draw_t open_door_draw;
	vector3d const normal(door.get_norm());
	bool const opens_outward(!is_house), dim(fabs(normal.x) < fabs(normal.y)), dir(normal[dim] < 0.0);
	add_door_verts(door.get_bcube(), open_door_draw, door.type, dim, dir, 1, opens_outward, 1, 0); // opened=1, exterior=1, on_stairs=0

	// draw other exterior doors as closed in case they're visible through the open door
	for (auto d = doors.begin(); d != doors.end(); ++d) {
		if (int(d - doors.begin()) == door_ix) continue; // skip the open door
		vector3d const normal2(d->get_norm());
		if (dot_product_ptv(normal2, pos, d->pts[0]) > 0.0) continue; // facing exterior of door rather than interior, skip
		bool const dim2(fabs(normal2.x) < fabs(normal2.y)), dir2(normal2[dim] > 0.0); // dir2 is reversed
		add_door_verts(d->get_bcube(), open_door_draw, d->type, dim2, dir2, 0, opens_outward, 1, 0); // opened=0, exterior=1, on_stairs=0
	}
	open_door_draw.draw(s, 0, 1); // direct_draw_no_vbo=1
	return 1;
}
void building_t::get_all_nearby_ext_door_verts(building_draw_t &bdraw, shader_t &s, vector<point> const &pts, float dist) {
	for (auto const &p : pts) {
		// we currently only support drawing one open door, so stop when we find one; future work is to use a bit mask to keep track of which doors are open
		if (get_nearby_ext_door_verts(bdraw, s, p, dist)) return;
	}
}

void building_t::get_split_int_window_wall_verts(building_draw_t &bdraw_front, building_draw_t &bdraw_back, point const &only_cont_pt_in, bool make_all_front) const {
	if (!is_valid()) return; // invalid building
	point only_cont_pt(only_cont_pt_in);
	maybe_inv_rotate_point(only_cont_pt);
	building_mat_t const &mat(get_material());
	cube_t const cont_part(get_part_containing_pt(only_cont_pt)); // part containing the point
	
	for (auto i = parts.begin(); i != get_real_parts_end_inc_sec(); ++i) { // multiple cubes/parts/levels; include house garage/shed
		if (is_basement(i)) continue; // skip basement walls because they have no windows

		if (make_all_front || i->contains_pt(only_cont_pt) || // part containing the point
			are_parts_stacked(*i, cont_part)) // stacked building parts, contained, draw as front in case player can see through stairs
		{
			bdraw_front.add_section(*this, 1, *i, mat.wall_tex, wall_color, 3, 0, 0, 1, 0); // XY
			continue;
		}
		unsigned back_dim_mask(3), front_dim_mask(0); // enable dims: 1=x, 2=y, 4=z | disable cube faces: 8=x1, 16=x2, 32=y1, 64=y2, 128=z1, 256=z2
		cube_t front_clip_cube(*i);

		for (unsigned d = 0; d < 2; ++d) {
			if (i->d[ d][0] != cont_part.d[ d][1] && i->d[ d][1] != cont_part.d[ d][0]) continue; // not adj in dim d
			if (i->d[!d][0] >= cont_part.d[!d][1] || i->d[!d][1] <= cont_part.d[!d][0]) continue; // no overlap in dim !d
			// if we get here, *i and cont_part are adjacent in dim d
			if (i->d[!d][1] < only_cont_pt[!d] || i->d[!d][0] > only_cont_pt[!d]) break; // point not contained in other dim range, draw part as back
			
			for (unsigned e = 0; e < 2; ++e) { // check for coplanar sides (wall extensions)
				unsigned const disable_bit(1 << (3 + 2*(1-d) + e));
				if (i->d[!d][e] != cont_part.d[!d][e] && ((i->d[!d][e] < cont_part.d[!d][e]) ^ e)) {front_dim_mask |= disable_bit; continue;} // not coplanar, disable edge from front
				front_dim_mask |= (1<<(1-d)); // coplanar, make other edge dim a front dim
				back_dim_mask  |= disable_bit; // disable this edge from back
			}
			for (unsigned e = 0; e < 2; ++e) { // check for extensions outside cont_part where back walls could be viewed through a window and split them out
				if (i->d[!d][e] != cont_part.d[!d][e] && ((i->d[!d][e] < cont_part.d[!d][e]) ^ e)) {
					cube_t back_clip_cube(*i);
					front_clip_cube.d[!d][e] = back_clip_cube.d[!d][!e] = cont_part.d[!d][e]; // split point
					bdraw_back.add_section(*this, 1, *i, mat.wall_tex, wall_color, back_dim_mask, 0, 0, 1, 0, 0.0, 0, 1.0, 0, &back_clip_cube);
				}
			}
			back_dim_mask &= ~(1<<d); front_dim_mask |= (1<<d); // draw only the other dim as back and this dim as front
			break;
		} // for d
		if (back_dim_mask  > 0) {bdraw_back .add_section(*this, 1, *i, mat.wall_tex, wall_color, back_dim_mask,  0, 0, 1, 0);}
		if (front_dim_mask > 0) {bdraw_front.add_section(*this, 1, *i, mat.wall_tex, wall_color, front_dim_mask, 0, 0, 1, 0, 0.0, 0, 1.0, 0, &front_clip_cube);}
	} // for i
}

void building_t::get_ext_wall_verts_no_sec(building_draw_t &bdraw) const { // used for blocking room shadows between parts
	if (real_num_parts == 1) return; // one part, light can't leak
	building_mat_t const &mat(get_material());

	for (auto p = parts.begin(); p != get_real_parts_end(); ++p) {
		unsigned const part_ix(p - parts.begin());
		unsigned dim_mask(3); // start with XY only

		for (unsigned d = 0; d < 4; ++d) { // 4 sides of this part
			bool skip_this_side(p->d[d>>1][d&1] == bcube.d[d>>1][d&1]); // exterior wall is on the edge of the bcube and can't shadow anything
			// houses and building courtyards can have exterior doors not along the bcube that aren't handled by the above case;
			// drawing the wall containing this door in the shadow map will cause lighting artifacts, so skip this wall;
			// it should be okay because we only need one of two walls intersecting an interior->exterior->interior light ray to suppress it,
			// and buildings generally won't have two doors on adjacent interior sides
			if (part_ix < 4) {skip_this_side |= bool(door_sides[part_ix] & (1<<d));} // only check base parts
			if (skip_this_side) {dim_mask |= (1<<(d+3));} // disable cube faces: 8=x1, 16=x2, 32=y1, 64=y2
		} // for d
		bdraw.add_section(*this, 1, *p, mat.side_tex, side_color, dim_mask, 0, 0, 1, 0);
	} // for p
}

void building_t::add_split_roof_shadow_quads(building_draw_t &bdraw) const {
	if (!interior || is_house || real_num_parts == 1) return; // no a stacked case

	for (auto i = parts.begin(); i != get_real_parts_end(); ++i) {
		if (is_basement(i)) continue; // skip the basement
		if (i->z2() == bcube.z2()) continue; // skip top roof

		if (clip_part_ceiling_for_stairs(*i, bdraw.temp_cubes, bdraw.temp_cubes2)) {
			for (auto c = bdraw.temp_cubes.begin(); c != bdraw.temp_cubes.end(); ++c) { // add floors after removing stairwells
				bdraw.add_section(*this, 1, *c, tid_nm_pair_t(), BLACK, 4, 1, 0, 1, 0); // only Z dim
			}
		}
	} // for i
}

// writes to the depth buffer only to prevent the terrain from being drawn in the basement
// to be called when the player is inside this building; when outside the building, the exterior walls/windows will write to the depth buffer instead
void building_t::write_basement_entrance_depth_pass(shader_t &s) const {
	if (!interior || !has_basement()) return;
	float const zval(parts[basement_part_ix].z2()), z(zval + BASEMENT_ENTRANCE_SCALE*get_floor_thickness());
	s.set_cur_color(ALPHA0); // fully transparent
	select_texture(WHITE_TEX);
	enable_blend();
	glEnable(GL_CULL_FACE);

	for (auto i = interior->stairwells.begin(); i != interior->stairwells.end(); ++i) {
		if (i->z1() >= zval) continue; // not basement stairwell
		// only draw top surface - bottom surface of terrain is not drawn when player is in the basement
		vert_wrap_t const verts[4] = {point(i->x1(), i->y1(), z), point(i->x2(), i->y1(), z), point(i->x2(), i->y2(), z), point(i->x1(), i->y2(), z)};
		draw_verts(verts, 4, GL_TRIANGLE_FAN); // single quad
	}
	glDisable(GL_CULL_FACE);
	disable_blend();
}


class building_creator_t {

	unsigned grid_sz, gpu_mem_usage, person_start, person_end;
	vector3d range_sz, range_sz_inv, max_extent;
	cube_t range, buildings_bcube;
	rand_gen_t rgen, ai_rgen;
	vect_building_t buildings;
	vector<vector<unsigned>> bix_by_plot; // cached for use with pedestrian collisions
	vector<int> peds_by_bix; // index of first person in each building; -1 is empty
	vector<building_ai_state_t> ai_state;
	// dynamic verts, static exterior verts, windows, window lights, interior walls/ceilings/floors, interior exterior walls
	building_draw_t building_draw, building_draw_vbo, building_draw_windows, building_draw_wind_lights, building_draw_interior, building_draw_int_ext_walls;
	point_sprite_drawer_sized building_lights;
	vector<point> points; // reused temporary
	bool use_smap_this_frame, has_interior_geom;

	struct grid_elem_t {
		vector<cube_with_ix_t> bc_ixs;
		vect_cube_t road_segs; // or driveways
		cube_t bcube;
		bool has_room_geom;
		grid_elem_t() : has_room_geom(0) {}
		bool empty() const {return (bc_ixs.empty() && road_segs.empty());}

		void add(cube_t const &c, unsigned ix, bool is_road_seg) {
			bcube.assign_or_union_with_cube(c);
			if (is_road_seg) {road_segs.push_back(c);} else {bc_ixs.emplace_back(c, ix);}
		}
	};
	vector<grid_elem_t> grid, grid_by_tile;

	grid_elem_t &get_grid_elem(unsigned gx, unsigned gy) {
		assert(gx < grid_sz && gy < grid_sz && !grid.empty());
		return grid[gy*grid_sz + gx];
	}
	grid_elem_t const &get_grid_elem(unsigned gx, unsigned gy) const {
		assert(gx < grid_sz && gy < grid_sz && !grid.empty());
		return grid[gy*grid_sz + gx];
	}
	struct bix_by_x1 {
		vector<building_t> const &buildings;
		bix_by_x1(vector<building_t> const &buildings_) : buildings(buildings_) {}
		bool operator()(unsigned const a, unsigned const b) const {return (buildings[a].bcube.x1() < buildings[b].bcube.x1());}
	};
	unsigned get_grid_ix(point pos) const {
		range.clamp_pt_xy(pos);
		unsigned gxy[2] = {};
		for (unsigned d = 0; d < 2; ++d) {
			float const v((pos[d] - range.d[d][0])*range_sz_inv[d]);
			gxy[d] = unsigned(v*(grid_sz-1));
			assert(gxy[d] < grid_sz);
		}
		return (gxy[1]*grid_sz + gxy[0]);
	}
	void get_grid_range(cube_t const &bcube, unsigned ixr[2][2]) const { // {lo,hi}x{x,y}
		point llc(bcube.get_llc()), urc(bcube.get_urc());
		range.clamp_pt_xy(llc);
		range.clamp_pt_xy(urc);
		for (unsigned d = 0; d < 2; ++d) {
			float const v1((llc[d] - range.d[d][0])*range_sz_inv[d]), v2((urc[d] - range.d[d][0])*range_sz_inv[d]);
			ixr[0][d] = unsigned(v1*(grid_sz-1));
			ixr[1][d] = unsigned(v2*(grid_sz-1));
			assert(ixr[0][d] < grid_sz && ixr[1][d] < grid_sz);
		}
	}
	void add_to_grid(cube_t const &bcube, unsigned bix, bool is_road_seg) {
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {get_grid_elem(x, y).add(bcube, bix, is_road_seg);}
		}
	}
	bool check_for_overlaps(vector<unsigned> const &ixs, cube_t const &test_bc, building_t const &b, float expand_rel, float expand_abs, vector<point> &points) const {
		for (auto i = ixs.begin(); i != ixs.end(); ++i) {
			building_t const &ob(get_building(*i));
			if (test_bc.intersects_xy(ob.bcube) && ob.check_bcube_overlap_xy(b, expand_rel, expand_abs, points)) return 1;
		}
		return 0;
	}
	bool check_for_overlaps(vector<cube_with_ix_t> const &bc_ixs, cube_t const &test_bc, building_t const &b, float expand_rel, float expand_abs, vector<point> &points) const {
		for (auto i = bc_ixs.begin(); i != bc_ixs.end(); ++i) {
			if (test_bc.intersects_xy(*i) && get_building(i->ix).check_bcube_overlap_xy(b, expand_rel, expand_abs, points)) return 1;
		}
		return 0;
	}

	void add_building_to_grid(building_t const &b, unsigned gix, unsigned bix) {
		grid_by_tile[gix].add(b.bcube, bix, 0);
		if (b.enable_driveway_coll()) {grid_by_tile[gix].add(b.driveway, bix, 1);} // add driveway as well
	}
	void build_grid_by_tile(bool single_tile) {
		grid_by_tile.clear();

		if (single_tile || world_mode != WMODE_INF_TERRAIN) { // not used in this mode - add all buildings to the first tile
			grid_by_tile.resize(1);
			grid_by_tile[0].bc_ixs.reserve(buildings.size());

			for(unsigned bix = 0; bix < buildings.size(); ++bix) {
				building_t const &b(buildings[bix]);
				if (!b.bcube.is_all_zeros()) {add_building_to_grid(b, 0, bix);} // skip invalid buildings
			}
			return;
		}
		//timer_t timer("build_grid_by_tile");
		map<uint64_t, unsigned> tile_to_gbt;

		for(unsigned bix = 0; bix < buildings.size(); ++bix) {
			unsigned gix;
			building_t const &b(buildings[bix]);
			if (b.bcube.is_all_zeros()) continue; // skip invalid buildings
			uint64_t const tile_id(get_tile_id_containing_point_no_xyoff(b.bcube.get_cube_center()));
			auto it(tile_to_gbt.find(tile_id));

			if (it == tile_to_gbt.end()) { // new element
				gix = grid_by_tile.size();
				grid_by_tile.push_back(grid_elem_t());
				tile_to_gbt[tile_id] = gix;
			}
			else { // existing element
				gix = it->second;
				assert(gix < grid_by_tile.size());
			}
			add_building_to_grid(b, gix, bix);
		} // for bix
	}

	bool check_valid_building_placement(building_params_t const &params, building_t const &b, vect_cube_t const &avoid_bcubes, cube_t const &avoid_bcubes_bcube,
		float min_building_spacing, unsigned plot_ix, bool non_city_only, bool use_city_plots, bool check_plot_coll)
	{
		float const expand_val(b.is_rotated() ? 0.05 : 0.1); // expand by 5-10% (relative - multiplied by building size)
		vector3d const b_sz(b.bcube.get_size());
		vector3d expand(expand_val*b_sz);
		for (unsigned d = 0; d < 2; ++d) {max_eq(expand[d], min_building_spacing);} // ensure the min building spacing (only applies to the current building)
		cube_t test_bc(b.bcube);
		test_bc.expand_by_xy(expand);

		if (use_city_plots) {
			assert(plot_ix < bix_by_plot.size());
			if (check_for_overlaps(bix_by_plot[plot_ix], test_bc, b, expand_val, min_building_spacing, points)) return 0;
			bix_by_plot[plot_ix].push_back(buildings.size());
		}
		else if (check_plot_coll && !avoid_bcubes.empty() && avoid_bcubes_bcube.intersects_xy(test_bc) &&
			has_bcube_int_xy(test_bc, avoid_bcubes, params.sec_extra_spacing)) // extra expand val
		{
			return 0;
		}
		else if (non_city_only && check_city_tline_cube_intersect_xy(test_bc)) {return 0;} // check transmission lines
		else {
			float const extra_spacing(non_city_only ? params.sec_extra_spacing : 0.0); // absolute value of expand
			test_bc.expand_by_xy(extra_spacing);
			unsigned ixr[2][2];
			get_grid_range(test_bc, ixr);

			for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
				for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
					grid_elem_t const &ge(get_grid_elem(x, y));
					if (!test_bc.intersects_xy(ge.bcube)) continue;
					if (check_for_overlaps(ge.bc_ixs, test_bc, b, expand_val, max(min_building_spacing, extra_spacing), points)) {return 0;}
				} // for x
			} // for y
		}
		return 1;
	}

	struct building_cand_t : public building_t {
		vect_cube_t &temp_parts;
		building_cand_t(vect_cube_t &temp_parts_) : temp_parts(temp_parts_) {temp_parts.clear(); parts.swap(temp_parts);} // parts takes temp_parts memory
		~building_cand_t() {parts.swap(temp_parts);} // memory returned from parts to temp_parts
	};

public:
	building_creator_t(bool is_city=0) : grid_sz(1), gpu_mem_usage(0), person_start(0), person_end(0), max_extent(zero_vector),
		building_draw(is_city), building_draw_vbo(is_city), use_smap_this_frame(0), has_interior_geom(0) {}
	bool empty() const {return buildings.empty();}

	void clear() {
		buildings.clear();
		grid.clear();
		grid_by_tile.clear();
		bix_by_plot.clear();
		peds_by_bix.clear();
		clear_vbos();
		buildings_bcube = cube_t();
		gpu_mem_usage = 0;
	}
	unsigned get_num_buildings() const {return buildings.size();}
	unsigned get_gpu_mem_usage() const {return gpu_mem_usage;}
	vector3d const &get_max_extent() const {return max_extent;}
	building_t const &get_building(unsigned ix) const {assert(ix < buildings.size()); return buildings[ix];}
	building_t       &get_building(unsigned ix)       {assert(ix < buildings.size()); return buildings[ix];} // non-const version; not intended to be used to change geometry
	cube_t const &get_building_bcube(unsigned ix) const {return get_building(ix).bcube;}
	
	bool get_building_door_pos_closest_to(unsigned ix, point const &target_pos, point &door_pos) const {
		return get_building(ix).get_building_door_pos_closest_to(target_pos, door_pos);
	}
	cube_t const &get_bcube() const {return buildings_bcube;}
	bool is_visible(vector3d const &xlate) const {return (!empty() && camera_pdu.cube_visible(buildings_bcube + xlate));}
	bool is_single_tile() const {return (grid_by_tile.size() == 1);}
	
	bool get_building_hit_color(point const &p1, point const &p2, colorRGBA &color) const { // exterior only; p1 and p2 are in building space
		float t(1.0); // unused
		unsigned hit_bix(0);
		unsigned const ret(check_line_coll(p1, p2, t, hit_bix, 0, 1)); // no_coll_pt=1; returns: 0=no hit, 1=hit side, 2=hit roof
		if (ret == 0) return 0;
		building_t const &b(get_building(hit_bix));
		switch (ret) {
		case 1: color = b.get_avg_side_color  (); break;
		case 2: color = b.get_avg_roof_color  (); break;
		case 3: color = b.get_avg_detail_color(); break;
		default: assert(0);
		}
		return 1;
	}
	void gen(building_params_t const &params, bool city_only, bool non_city_only, bool is_tile, bool allow_flatten, int rseed=123) {
		assert(!(city_only && non_city_only));
		clear();
		if (params.tt_only && world_mode != WMODE_INF_TERRAIN) return;
		if (params.gen_inf_buildings() && !is_tile && !city_only) return; // secondary buildings - not added here
		vect_city_zone_t city_plot_bcubes;
		vector<unsigned> valid_city_plot_ixs;
		bool residential(0); // Note: may not be correct for a mix of residential and non-residential, but this only matters if the material nonemptiness or ranges differ

		if (city_only) {
			unsigned num_residential(0), num_non_residential(0);
			get_city_plot_zones(city_plot_bcubes); // Note: assumes approx equal area for placement distribution

			for (auto i = city_plot_bcubes.begin(); i != city_plot_bcubes.end(); ++i) {
				if (i->is_park) continue; // skip parks
				valid_city_plot_ixs.push_back(i - city_plot_bcubes.begin()); // record non-park plots
				if (i->is_residential) {++num_residential;} else {++num_non_residential;}
			}
			//assert(!valid_city_plot_ixs.empty()); // too strong? what happens if this is true?
			residential = (num_residential > num_non_residential); // consider this city residential if the majority of non-park plots are residential
		}
		vector<unsigned> const &mat_ix_list(params.get_mat_list(city_only, non_city_only, residential));
		if (params.materials.empty() || mat_ix_list.empty()) return; // no materials
		timer_t timer("Gen Buildings", !is_tile);
		float const def_water_level(get_water_z_height()), min_building_spacing(get_min_obj_spacing());
		vector3d const offset(-xoff2*DX_VAL, -yoff2*DY_VAL, 0.0);
		vector3d const xlate((world_mode == WMODE_INF_TERRAIN) ? offset : zero_vector); // cancel out xoff2/yoff2 translate
		vector3d const delta_range((world_mode == WMODE_INF_TERRAIN) ? zero_vector : offset);
		range = params.materials[mat_ix_list.front()].pos_range; // range is union over all material ranges
		for (auto i = mat_ix_list.begin()+1; i != mat_ix_list.end(); ++i) {range.union_with_cube(params.materials[*i].pos_range);}
		range     += delta_range;
		range_sz   = range.get_size(); // Note: place_radius is relative to range cube center
		max_extent = zero_vector;
		assert(range_sz.x > 0.0 && range_sz.y > 0.0);
		UNROLL_2X(range_sz_inv[i_] = 1.0/range_sz[i_];) // xy only
		if (!is_tile) {buildings.reserve(params.num_place);}
		grid_sz = (is_tile ? 4 : 32); // tiles are small enough that they don't need grids
		grid.resize(grid_sz*grid_sz); // square
		unsigned num_tries(0), num_gen(0), num_skip(0);
		if (rseed == 0) {rseed = 123;} // 0 is a bad value
		rseed += params.buildings_rand_seed; // add in rand_seed from the config file
		rgen.set_state(rand_gen_index, rseed); // update when mesh changes, otherwise determinstic
		vect_cube_t avoid_bcubes;
		cube_t avoid_bcubes_bcube;

		if (non_city_only) {
			get_city_bcubes(avoid_bcubes);
			get_city_road_bcubes(avoid_bcubes, 1); // connector roads only
			get_all_model_bcubes(avoid_bcubes);
			expand_cubes_by_xy(avoid_bcubes, get_road_max_width());
			for (auto i = avoid_bcubes.begin(); i != avoid_bcubes.end(); ++i) {avoid_bcubes_bcube.assign_or_union_with_cube(*i);}
		}
		bool const use_city_plots(!valid_city_plot_ixs.empty()), check_plot_coll(!avoid_bcubes.empty());
		bix_by_plot.resize(city_plot_bcubes.size());
		point center(all_zeros);
		unsigned num_consec_fail(0), max_consec_fail(0);
		vect_cube_t temp_parts;

		for (unsigned i = 0; i < params.num_place; ++i) {
			bool success(0);

			for (unsigned n = 0; n < params.num_tries; ++n) { // 10 tries to find a non-overlapping building placement
				unsigned plot_ix(0), city_plot_ix(0), pref_dir(0);
				bool residential(0);

				if (use_city_plots) { // select a random plot, if available
					bool success(0);

					for (unsigned N = 0; N < 10; ++N) { // 10 tries to choose a plot that has the capacity for a new building
						plot_ix = valid_city_plot_ixs[rgen.rand() % valid_city_plot_ixs.size()];
						assert(plot_ix < city_plot_bcubes.size());
						if (!city_plot_bcubes[plot_ix].is_full()) {success = 1; break;}
					}
					if (!success) break; // all candidate plots were full
					residential = city_plot_bcubes[plot_ix].is_residential;
					if (residential && params.mat_gen_ix_res.empty()) break; // no residential buildings available, break from n loop (but retry i loop with new plot)
				}
				building_cand_t b(temp_parts);
				b.mat_ix = params.choose_rand_mat(rgen, city_only, non_city_only, residential); // set material
				building_mat_t const &mat(b.get_material());
				cube_t pos_range;
				float border_scale(1.0);
				unsigned max_floors(0); // starts at unlimited
				
				if (use_city_plots) { // select a random plot, if available
					city_zone_t const &plot(city_plot_bcubes[plot_ix]);
					pos_range = plot;
					center.z  = plot.zval; // optimization: take zval from plot rather than calling get_exact_zval()
					b.assigned_plot = plot; // only really needed for residential sub-plots
					city_plot_ix    = ((plot.parent_plot_ix >= 0) ? plot.parent_plot_ix : plot_ix);
					max_floors      = plot.max_floors;
					if (residential) {pref_dir = plot.street_dir;}
					pos_range.expand_by_xy(-min_building_spacing); // force min spacing between building and edge of plot
					if (plot.capacity == 1) {border_scale *= 2.0;} // use smaller border scale since the individual building plots should handle borders
				}
				else {
					pos_range = mat.pos_range + delta_range;
				}
				vector3d const pos_range_sz(pos_range.get_size());
				assert(pos_range_sz.x > 0.0 && pos_range_sz.y > 0.0);
				point const place_center(pos_range.get_cube_center());
				bool const is_residential_block(residential && !pref_dir);
				float const min_center_dist(is_residential_block ? 0.3*min(pos_range_sz.x, pos_range_sz.y) : 0.0);
				bool keep(0);
				++num_tries;

				for (unsigned m = 0; m < params.num_tries; ++m) {
					for (unsigned d = 0; d < 2; ++d) {center[d] = rgen.rand_uniform(pos_range.d[d][0], pos_range.d[d][1]);} // x,y
					// place residential buildings around the edges of the plot (pos_range) unless the street dir has already been assigned (individual house plot) / keep out of the center
					if (min_center_dist > 0.0 && dist_xy_less_than(center, place_center, min_center_dist)) continue;
					if (is_tile || mat.place_radius == 0.0 || dist_xy_less_than(center, place_center, mat.place_radius)) {keep = 1; break;} // place_radius ignored for tiles
				}
				if (!keep) continue; // placement failed, skip
				b.is_house = (mat.house_prob > 0.0 && (residential || rgen.rand_float() < mat.house_prob)); // force a house if residential and houses are enabled
				float const size_scale(b.is_house ? mat.gen_house_size_scale(rgen) : 1.0);
				
				for (unsigned d = 0; d < 2; ++d) { // x,y
					float const size_cap(border_scale*pos_range_sz[d]*(b.is_house ? 0.8 : 1.0)); // size cap relative to plot size
					float const sz(0.5*rgen.rand_uniform(min(size_scale*mat.sz_range.d[d][0], 0.3f*size_cap),
						                                 min(size_scale*mat.sz_range.d[d][1], 0.5f*size_cap))); // use pos range size for max
					b.bcube.d[d][0] = center[d] - sz;
					b.bcube.d[d][1] = center[d] + sz;
				}
				if (is_residential_block && b.bcube.contains_pt_xy(place_center)) continue; // house should not contain the center point of the plot
				if ((use_city_plots || is_tile) && !pos_range.contains_cube_xy(b.bcube)) continue; // not completely contained in plot/tile (pre-rot)
				if (!use_city_plots) {b.gen_rotation(rgen);} // city plots are Manhattan (non-rotated) - must rotate before bcube checks below
				if (is_tile && !pos_range.contains_cube_xy(b.bcube)) continue; // not completely contained in tile
				if (start_in_inf_terrain && b.bcube.contains_pt_xy(get_camera_pos())) continue; // don't place a building over the player appearance spot
				if (!check_valid_building_placement(params, b, avoid_bcubes, avoid_bcubes_bcube, min_building_spacing,
					city_plot_ix, non_city_only, use_city_plots, check_plot_coll)) continue; // check overlap (use city plot_ix rather than sub-plot ix)
				++num_gen;
				if (!use_city_plots) {center.z = get_exact_zval(center.x+xlate.x, center.y+xlate.y);} // only calculate when needed
				float const z_sea_level(center.z - def_water_level);
				if (z_sea_level < 0.0) break; // skip underwater buildings, failed placement
				if (z_sea_level < mat.min_alt || z_sea_level > mat.max_alt) break; // skip bad altitude buildings, failed placement
				float const hmin(use_city_plots ? pos_range.z1() : 0.0), hmax(use_city_plots ? pos_range.z2() : 1.0);
				assert(hmin <= hmax);
				float const height_range(mat.sz_range.dz());
				assert(height_range >= 0.0);
				float const z_size_scale(size_scale*(b.is_house ? rgen.rand_uniform(0.6, 0.8) : 1.0)); // make houses slightly shorter on average to offset extra height added by roof
				float height_val(0.5f*z_size_scale*(mat.sz_range.z1() + height_range*rgen.rand_uniform(hmin, hmax)));
				if (max_floors > 0) {min_eq(height_val, max_floors*b.get_window_vspace());} // limit height based on max floors
				assert(height_val > 0.0);
				b.set_z_range(center.z, (center.z + height_val));
				assert(b.bcube.is_strictly_normalized());
				mat.side_color.gen_color(b.side_color, rgen);
				mat.roof_color.gen_color(b.roof_color, rgen);
				if (use_city_plots) {b.street_dir = (pref_dir ? pref_dir : get_street_dir(b.bcube, pos_range));}
				add_to_grid(b.bcube, buildings.size(), 0);
				vector3d const sz(b.bcube.get_size());
				float const mult[3] = {0.5, 0.5, 1.0}; // half in X,Y and full in Z
				UNROLL_3X(max_extent[i_] = max(max_extent[i_], mult[i_]*sz[i_]);)
				buildings.push_back(b);
				if (use_city_plots) {++city_plot_bcubes[plot_ix].nbuildings;}
				success = 1;
				break; // done
			} // for n
			if (success) {num_consec_fail = 0;}
			else {
				++num_consec_fail;
				max_eq(max_consec_fail, num_consec_fail);

				if (num_consec_fail >= (is_tile ? 50U : 5000U)) { // too many failures - give up
					if (!is_tile) {cout << "Failed to place a building after " << num_consec_fail << " tries, giving up after " << i << " iterations" << endl;}
					break;
				}
			}
		} // for i
		if (buildings.capacity() > 2*buildings.size()) {buildings.shrink_to_fit();}
		bix_by_x1 cmp_x1(buildings);
		for (auto i = bix_by_plot.begin(); i != bix_by_plot.end(); ++i) {sort(i->begin(), i->end(), cmp_x1);}
		if (!is_tile) {timer.end();} // use a single timer for tile mode

		if (params.flatten_mesh && !use_city_plots) { // not needed for city plots, which are already flat
			timer_t timer("Gen Building Zvals", !is_tile);
			bool const do_flatten(allow_flatten && using_tiled_terrain_hmap_tex()); // can't always flatten terrain when using tiles

#pragma omp parallel for schedule(static,1) if (!is_tile)
			for (int i = 0; i < (int)buildings.size(); ++i) {
				building_t &b(buildings[i]);

				if (do_flatten) { // flatten the mesh under the bcube to a height of mesh_zval
					//assert(!b.is_rotated()); // too strong?
					flatten_hmap_region(b.bcube);
				}
				else { // extend building bottom downward to min mesh height
					bool const shift_top(DRAW_WINDOWS_AS_HOLES); // shift is required to preserve height for floor alignment of building interiors
					float &zmin(b.bcube.z1()); // Note: grid bcube z0 value won't be correct, but will be fixed conservatively below
					float const orig_zmin(zmin);
					unsigned num_below(0);
					
					for (int d = 0; d < 4; ++d) {
						float const zval(get_exact_zval(b.bcube.d[0][d&1]+xlate.x, b.bcube.d[1][d>>1]+xlate.y)); // approximate for rotated buildings
						min_eq(zmin, zval);
						num_below += (zval < def_water_level);
					}
					max_eq(zmin, def_water_level); // don't go below the water
					float const dz(orig_zmin - zmin), max_dz(b.get_material().max_delta_z);
					if (shift_top) {b.bcube.z2() -= dz;} // shift top down as well to keep the height constant
					if (num_below > 2 || // more than 2 corners underwater
						(max_dz > 0.0 && dz > max_dz)) // too steep of a slope
					{
						b.bcube.set_to_zeros();
						++num_skip;
					}
					else if (!b.parts.empty()) {
						b.parts.back().z1() = b.bcube.z1(); // update base z1
						if (shift_top) {b.parts.back().z2() -= dz;} // shift top down as well
						assert(b.parts.back().dz() > 0.0);
					}
				}
			} // for i
			if (do_flatten) { // use conservative zmin for grid
				for (auto i = grid.begin(); i != grid.end(); ++i) {i->bcube.z1() = def_water_level;}
			}
		} // if flatten_mesh
		{ // open a scope
			timer_t timer2("Gen Building Geometry", !is_tile);
			bool const use_mt(!is_tile || global_building_params.gen_building_interiors); // only single threaded for tiles with no interiors, which is a fast case anyway
#pragma omp parallel for schedule(static,1) num_threads(2) if (use_mt)
			for (int i = 0; i < (int)buildings.size(); ++i) {buildings[i].gen_geometry(i, 1337*i+rseed);}
		} // close the scope
		if (0 && non_city_only) { // perform room graph analysis
			timer_t timer3("Building Room Graph Analysis");
			for (auto b = buildings.begin(); b != buildings.end(); ++b) {
				if (b->has_complex_floorplan) continue; // room graph isn't really valid for this building type
				//if (b->is_house) continue;
				unsigned num_comp(b->count_connected_room_components());
				if (b->has_sec_bldg()) {--num_comp;} // exclude garage/shed
				//cout << num_comp;
				if (num_comp > 1) {cout << num_comp << ": " << b->bcube.get_cube_center().str() << endl;}
			}
			cout << endl;
		}
		// re-generate grid based on new building bcubes that include things like roofs and chimneys;
		// since bcubes should only increase in size, we don't need to reset grid bcubes
		for (auto g = grid.begin(); g != grid.end(); ++g) {g->bc_ixs.clear();}

		for (auto b = buildings.begin(); b != buildings.end(); ++b) { // add driveways and calculate has_interior_geom
			if (b->enable_driveway_coll()) {
				if (!b->driveway.is_all_zeros()) {add_to_grid(b->driveway, (b - buildings.begin()), 1);}
				if (!b->porch   .is_all_zeros()) {add_to_grid(b->porch,    (b - buildings.begin()), 1);}
			}
			add_to_grid(b->bcube, (b - buildings.begin()), 0);
			buildings_bcube.assign_or_union_with_cube(b->bcube);
			has_interior_geom |= b->has_interior();
		} // for b
		if (!is_tile && (!city_only || residential)) {place_building_trees(rgen);}

		if (!is_tile) {
			cout << "WM: " << world_mode << " MCF: " << max_consec_fail << " Buildings: " << params.num_place << " / " << num_tries << " / " << num_gen
				 << " / " << buildings.size() << " / " << (buildings.size() - num_skip) << endl;
			building_stats_t s;
			for (auto b = buildings.begin(); b != buildings.end(); ++b) {b->update_stats(s);}
			cout << TXT(s.nbuildings) << TXT(s.nparts) << TXT(s.ndetails) << TXT(s.ntquads) << TXT(s.ndoors) << TXT(s.ninterior)
				 << TXT(s.nrooms) << TXT(s.nceils) << TXT(s.nfloors) << TXT(s.nwalls) << TXT(s.nrgeom) << TXT(s.nobjs) << TXT(s.nverts) << endl;
		}
		build_grid_by_tile(is_tile);
		create_vbos(is_tile);
	} // end gen()

	struct pt_by_xval {
		bool operator()(point const &a, point const &b) const {return (a.x < b.x);}
	};

	void place_building_trees(rand_gen_t &rgen) {
		if (!has_city_trees()) return;
		vector<point> placements;

		for (auto b = buildings.begin(); b != buildings.end(); ++b) {
			if (b->tree_pos != all_zeros) {placements.push_back(b->tree_pos);}
		}
		if (placements.empty()) return;
		sort(placements.begin(), placements.end(), pt_by_xval()); // sort by xval
		float const block_xsize(X_SCENE_SIZE);
		float cur_xval(0.0);
		unsigned num_blocks(0);
		bool const allow_bush = 0; // no bushes for now
		bool const is_sm_tree = 0; // deciduous trees only

		for (auto p = placements.begin(); p != placements.end(); ++p) {
			if (p == placements.begin() || p->x > (cur_xval + block_xsize)) {
				tree_placer.begin_block(is_sm_tree);
				cur_xval = p->x;
				++num_blocks;
			}
			int const ttype(rgen.rand()%100); // Note: okay to leave at -1; also, don't have to set to a valid tree type
			tree_placer.add(*p, 0, ttype, allow_bush, is_sm_tree);
		} // for p
		cout << "Num Placed Trees: " << placements.size() << ", Blocks: " << num_blocks << endl;
	}

	void get_all_garages(vect_cube_t &garages) const {
		for (auto b = buildings.begin(); b != buildings.end(); ++b) {
			if (!b->interior) continue; // no interior/car not visible

			if (b->has_garage) { // exterior/detatched garage
				assert(b->parts.size() >= 3); // must be at least two parts + garage
				garages.push_back(b->parts[2]); // place a car here - up to the car manager to decide on the details
				garages.back().z1() += 0.5*b->get_floor_thickness(); // place car above the floor
			}
			else if (b->has_int_garage) { // interior garage
				for (auto r = b->interior->rooms.begin(); r != b->interior->rooms.end(); ++r) {
					if (r->get_room_type(0) != RTYPE_GARAGE) continue;
					garages.push_back(*r); // place a car here - up to the car manager to decide on the details
					garages.back().z1() += 0.5*b->get_floor_thickness(); // place car above the floor
					break;
				}
			}
		} // for b
	}
	void get_all_helipads(vect_cube_t &helipads) const {
		for (auto b = buildings.begin(); b != buildings.end(); ++b) {
			if (b->has_helipad) {helipads.push_back(b->get_helipad_bcube());}
		}
	}

	struct building_ai_cand_t {
		unsigned bix, num;
		bool is_house;
		building_ai_cand_t(unsigned b, bool h) : bix(b), num(0), is_house(h) {}
	};

	unsigned get_person_capacity() const {
		unsigned cap(0);
		for (auto b = buildings.begin(); b != buildings.end(); ++b) {cap += b->get_person_capacity_mult();}
		return cap;
	}
	bool place_people(vect_building_place_t &locs, float radius, float speed_mult, unsigned num, unsigned start) {
		assert(locs.empty());
		if (num == 0 || empty() || !global_building_params.gen_building_interiors) return 0; // no people, buildings, or interiors
		vector<building_ai_cand_t> cand_buildings; // {building_ix, is_house}

		for (auto b = buildings.begin(); b != buildings.end(); ++b) {
			unsigned const cap_mult(b->get_person_capacity_mult()); // 0-2
			for (unsigned n = 0; n < cap_mult; ++n) {cand_buildings.emplace_back((b - buildings.begin()), b->is_house);}
		}
		if (cand_buildings.empty()) return 0; // no interiors, or all buildings rotated
		locs.reserve(num);
		rand_gen_t rgen2; // Note: we could also use our rgen member variable
		point ppos;

		if (num >= cand_buildings.size()) {
			// assign one person per building slot ahead of time, then randomly distribute the remaining people; should ensure each building has at least one person
			for (auto i = cand_buildings.begin(); i != cand_buildings.end(); ++i) {
				if (get_building(i->bix).place_person(ppos, radius, rgen2)) {locs.emplace_back(ppos, i->bix); ++i->num; --num;}
			}
		}
		for (unsigned n = 0; n < num; ++n) {
			for (unsigned attempts = 0; attempts < 5; ++attempts) { // make up to 5 attempts to choose a building
				building_ai_cand_t &chosen(cand_buildings[rgen2.rand() % cand_buildings.size()]);
				if (attempts < 3 && chosen.is_house && chosen.num >= 4) continue; // try another building if this is a house and already has > 4 people in it
				if (get_building(chosen.bix).place_person(ppos, radius, rgen2)) {locs.emplace_back(ppos, chosen.bix); ++chosen.num; break;}
			}
		} // for n
		if (locs.empty()) return 0;
		sort(locs.begin(), locs.end());
		peds_by_bix.resize(buildings.size(), -1);

		for (unsigned i = 0; i < locs.size(); ++i) {
			unsigned const bix(locs[i].bix);
			assert(bix < peds_by_bix.size());
			if (peds_by_bix[bix] < 0) {peds_by_bix[bix] = start + i;} // record first ped index for each building
		}
		person_start = start; person_end = start + locs.size();
		return 1;
	}
	int get_ped_ix_for_bix(unsigned bix) const {return ((bix < peds_by_bix.size()) ? peds_by_bix[bix] : -1);}

	// called once per frame
	void update_ai_state(vector<pedestrian_t> &people, float delta_dir) { // returns the new pos of each person; dir/orient can be determined from the delta
		if (person_start == person_end) return; // no people to update
		buildings.ai_room_update(ai_state, people, person_start, person_end, delta_dir, ai_rgen);
	}

	static void select_person_shadow_shader(shader_t &person_shader) {
		if (!person_shader.is_setup()) {
			enable_animations_for_shader(person_shader);
			setup_smoke_shaders(person_shader, 0.0, 0, 0, 0, 0, 0, 0);
		} else {person_shader.make_current();}
	}

	static void multi_draw_shadow(vector3d const &xlate, vector<building_creator_t *> const &bcs) {
		//timer_t timer("Draw Buildings Shadow");
		fgPushMatrix();
		translate_to(xlate);
		shader_t s, person_shader;
		s.begin_color_only_shader(); // really don't even need colors
		glEnable(GL_CULL_FACE); // slightly faster for interior shadow maps
		vector<point> points; // reused temporary
		building_draw_t ext_parts_draw; // roof and exterior walls
		bool has_garage(0);

		for (auto i = bcs.begin(); i != bcs.end(); ++i) {
			if (interior_shadow_maps) { // draw interior shadow maps
				occlusion_checker_noncity_t oc(**i);
				point const lpos(get_camera_pos() - xlate); // Note: camera_pos is actually the light pos

				// draw interior for the building containing the light
				for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) {
					if (!g->bcube.contains_pt(lpos)) continue; // wrong tile
					
					for (auto bi = g->bc_ixs.begin(); bi != g->bc_ixs.end(); ++bi) {
						building_t &b((*i)->get_building(bi->ix));
						if (!b.interior || !b.bcube.contains_pt(lpos)) continue; // no interior or wrong building
						has_garage |= b.has_a_garage();
						(*i)->building_draw_interior.draw_quads_for_draw_range(s, b.interior->draw_range, 1); // shadow_only=1
						b.add_split_roof_shadow_quads(ext_parts_draw);
						// no batch draw for shadow pass since textures aren't used; draw everything, since shadow may be cached
						b.draw_room_geom(nullptr, s, oc, xlate, bi->ix, 1, 0, 1, 1); // shadow_only=1, inc_small=1, player_in_building=1
						bool const player_close(dist_less_than(lpos, pre_smap_player_pos, camera_pdu.far_)); // Note: pre_smap_player_pos already in building space
						b.get_ext_wall_verts_no_sec(ext_parts_draw); // add exterior walls to prevent light leaking between adjacent parts
						bool const add_player_shadow(camera_surf_collide ? player_close : 0);
						int const ped_ix((*i)->get_ped_ix_for_bix(bi->ix)); // Note: assumes only one building_draw has people
						if (ped_ix < 0 && !add_player_shadow) continue; // nothing else to draw
						bool const camera_in_this_building(b.check_point_or_cylin_contained(pre_smap_player_pos, 0.0, points));

						if (ped_ix >= 0 && (camera_in_this_building || player_close)) { // draw people in this building
							if (global_building_params.enable_people_ai) { // handle animations
								select_person_shadow_shader(person_shader);
								draw_peds_in_building(ped_ix, ped_draw_vars_t(b, oc, person_shader, xlate, bi->ix, 1, 0)); // draw people in this building
								s.make_current(); // switch back to normal building shader
							}
							else {draw_peds_in_building(ped_ix, ped_draw_vars_t(b, oc, s, xlate, bi->ix, 1, 0));} // no animations
						}
						if (add_player_shadow && camera_in_this_building) {
							if (global_building_params.enable_people_ai) { // handle animations
								select_person_shadow_shader(person_shader);
								draw_player_model(person_shader, xlate, 1); // shadow_only=1
								s.make_current(); // switch back to normal building shader
							}
							else {draw_player_model(s, xlate, 1);} // shadow_only=1
						}
					} // for bi
				} // for g
			}
			else { // draw exterior shadow maps
				for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) { // draw only visible tiles
					point const pos(g->bcube.get_cube_center() + xlate);
					if (!camera_pdu.sphere_and_cube_visible_test(pos, g->bcube.get_bsphere_radius(), (g->bcube + xlate))) continue; // VFC
					(*i)->building_draw_vbo.draw_tile(s, (g - (*i)->grid_by_tile.begin()), 1);
				}
				//(*i)->building_draw_vbo.draw(s, 1); // less CPU time but more GPU work, in general seems to be slower
			}
		} // for i
		if ( interior_shadow_maps) {glDisable(GL_CULL_FACE);} // need to draw back faces of exterior parts to handle shadows on blinds
		ext_parts_draw.draw(s, 1, 1);
		if (!interior_shadow_maps) {glDisable(GL_CULL_FACE);}
		s.end_shader();
		fgPopMatrix();
		if (has_garage) {draw_cars_in_garages(xlate, 1);} // only for building interior shadows, not city shadows
	}
	static bool check_tile_smap(bool shadow_only) {
		return (!shadow_only && world_mode == WMODE_INF_TERRAIN && shadow_map_enabled());
	}

	void add_interior_lights(vector3d const &xlate, cube_t &lights_bcube) { // Note: non const because this caches light bcubes
		if (!ADD_ROOM_LIGHTS) return;
		if (!DRAW_WINDOWS_AS_HOLES || !draw_building_interiors || !has_interior_geom) return; // no interior
		point const camera(get_camera_pos()), camera_xlated(camera - xlate);
		vector<point> points; // reused temporary
		vect_cube_t ped_bcubes; // reused temporary
		occlusion_checker_noncity_t oc(*this);
		bool is_first_building(1);
		//highres_timer_t timer("Add Interior Lights");

		for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
			if (!lights_bcube.intersects_xy(g->bcube)) continue; // not within light volume (too far from camera)
			if (!camera_pdu.sphere_and_cube_visible_test((g->bcube.get_cube_center() + xlate), g->bcube.get_bsphere_radius(), (g->bcube + xlate))) continue; // VFC

			for (auto bi = g->bc_ixs.begin(); bi != g->bc_ixs.end(); ++bi) {
				building_t &b(get_building(bi->ix));
				if (!b.has_room_geom()) continue; // no interior room geom, skip
				if (!lights_bcube.intersects_xy(b.bcube)) continue; // not within light volume (too far from camera)
				bool const camera_in_this_building(b.check_point_or_cylin_contained(camera_xlated, 0.0, points));
				// limit room lights to when the player is in a building because we can restrict them to a single floor, otherwise it's too slow
				if (!camera_pdu.cube_visible(b.bcube + xlate)) continue; // VFC
				if (is_first_building) {oc.set_camera(camera_pdu);} // setup occlusion culling on the first visible building
				is_first_building = 0;
				oc.set_exclude_bix(bi->ix);
				b.add_room_lights(xlate, bi->ix, camera_in_this_building, get_ped_ix_for_bix(bi->ix), oc, ped_bcubes, lights_bcube);
			} // for bi
		} // for g
	}

	// reflection_pass: 0 = not reflection pass, 1 = reflection for room with exterior wall, 2 = reflection for room with interior wall, 3 = reflection from mirror in a house
	static void multi_draw(int shadow_only, int reflection_pass, vector3d const &xlate, vector<building_creator_t *> const &bcs) {
		if (bcs.empty()) return;

		if (shadow_only) {
			assert(!reflection_pass);
			multi_draw_shadow(xlate, bcs);
			return;
		}
		// ensure shadow map TU_IDs are valid in case we try to use them without binding to a tile;
		// maybe this fixes the occasional shader undefined behavior warning we get with the debug callback?
		for (unsigned l = 0; l < NUM_LIGHT_SRC; ++l) {
			if (!light_valid_and_enabled(l)) continue;
			bind_texture_tu(get_empty_smap_tid(), GLOBAL_SMAP_START_TU_ID+l); // bind empty shadow map
		}
		building_t const *const prev_player_building(player_building);

		if (!reflection_pass) {
			interior_shadow_maps = 1; // set state so that above call will know that it was called recursively from here and should draw interior shadow maps
			enable_dlight_bcubes = 1; // needed around this call so that light bcubes are sent to the GPU
			building_lights_manager.setup_building_lights(xlate); // setup lights on first (opaque) non-shadow pass
			enable_dlight_bcubes = 0; // disable when creating the reflection image (will be set when we re-enter multi_draw())
			interior_shadow_maps = 0;
			create_mirror_reflection_if_needed();
			draw_cars_in_garages(xlate, 0); // must be done before drawing buildings because windows write to the depth buffer
			player_building = nullptr; // reset, may be set below
		}
		//timer_t timer("Draw Buildings"); // 0.57ms (2.6ms with glFinish(), 6.3ms with building interiors)
		point const camera(get_camera_pos()), camera_xlated(camera - xlate);
		int const use_bmap(global_building_params.has_normal_map);
		bool const night(is_night(WIND_LIGHT_ON_RAND));
		// check for sun or moon; also need the smap pass for drawing with dynamic lights at night, so basically it's always enabled
		bool const use_tt_smap(check_tile_smap(0)); // && (night || light_valid_and_enabled(0) || light_valid_and_enabled(1)));
		bool have_windows(0), have_wind_lights(0), have_interior(0), this_frame_camera_in_building(0), this_frame_player_in_basement(0);
		unsigned max_draw_ix(0);
		shader_t s;

		for (auto i = bcs.begin(); i != bcs.end(); ++i) {
			assert(*i);
			have_windows     |= !(*i)->building_draw_windows.empty();
			have_wind_lights |= !(*i)->building_draw_wind_lights.empty();
			have_interior    |= (draw_building_interiors && (*i)->has_interior_geom);
			max_eq(max_draw_ix, (*i)->building_draw_vbo.get_num_draw_blocks());
			if (night) {(*i)->ensure_window_lights_vbos();}
			
			if ((*i)->is_single_tile()) { // only for tiled buildings
				(*i)->use_smap_this_frame = (use_tt_smap && try_bind_tile_smap_at_point(((*i)->grid_by_tile[0].bcube.get_cube_center() + xlate), s, 1)); // check_only=1
			}
		}
		bool const draw_interior(DRAW_WINDOWS_AS_HOLES && (have_windows || global_building_params.add_city_interiors) && draw_building_interiors);
		bool const v(world_mode == WMODE_GROUND), indir(v), dlights(v), use_smap(v);
		float const min_alpha        = 0.0; // 0.0 to avoid alpha test
		city_dlight_pcf_offset_scale = 0.6; // reduced for building interiors; below 0.6 and shadow edges are blocky, but above 0.6 clothes hangers have double shadows
		enable_dlight_bcubes         = 1; // using light bcubes is both faster and more correct when shadow maps are not enabled
		fgPushMatrix();
		translate_to(xlate);
		building_draw_t interior_wind_draw, ext_door_draw;
		vector<building_draw_t> int_wall_draw_front, int_wall_draw_back;
		vector<vertex_range_t> per_bcs_exclude;
		building_t const *building_cont_player(nullptr);

		// draw building interiors with standard shader and no shadow maps; must be drawn first before windows depth pass
		if (have_interior) {
			//timer_t timer2("Draw Building Interiors");
			float const interior_draw_dist(global_building_params.interior_view_dist_scale*2.0f*(X_SCENE_SIZE + Y_SCENE_SIZE));
			float const room_geom_draw_dist(0.4*interior_draw_dist), room_geom_sm_draw_dist(0.05*interior_draw_dist), z_prepass_dist(0.25*interior_draw_dist);
			glEnable(GL_CULL_FACE); // back face culling optimization, helps with expensive lighting shaders
			glCullFace(reflection_pass ? GL_FRONT : GL_BACK);

			if (ADD_ROOM_LIGHTS) { // use z-prepass to reduce time taken for shading
				setup_smoke_shaders(s, 0.0, 0, 0, 0, 0, 0, 0); // everything disabled, but same shader so that vertex transforms are identical
				glPolygonOffset(1.0, 1.0);
				if (reflection_pass) {glEnable(GL_POLYGON_OFFSET_FILL);} // not sure why, but a polygon offset is required for the reflection pass
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // Disable color rendering, we only want to write to the Z-Buffer
				
				for (auto i = bcs.begin(); i != bcs.end(); ++i) { // draw interior for the tile containing the camera
					float const ddist_scale((*i)->building_draw_windows.empty() ? 0.1 : 1.0);

					for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) {
						if (reflection_pass ? g->bcube.contains_pt_xy(camera_xlated) : g->bcube.closest_dist_xy_less_than(camera_xlated, ddist_scale*z_prepass_dist)) {
							(*i)->building_draw_interior.draw_tile(s, (g - (*i)->grid_by_tile.begin()));
						}
					}
				}
				if (reflection_pass) {glDisable(GL_POLYGON_OFFSET_FILL);}
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				s.end_shader();
				set_std_depth_func_with_eq();
			}
			// Note: the best I can come up with is applying animations to both buildings and people, making sure to set animation_time to 0.0 for buildings;
			// otherwise, we would need to switch between two different shaders every time we come across a building with people in it; not very clean, but seems to work
			bool const enable_animations(global_building_params.enable_people_ai && draw_interior);
			if (enable_animations) {enable_animations_for_shader(s);}
			setup_building_draw_shader(s, min_alpha, 1, 0, 0); // enable_indir=1, force_tsl=0, use_texgen=0
			if (enable_animations) {s.add_uniform_int("animation_id", 0);}
			if (reflection_pass) {draw_player_model(s, xlate, 0);} // shadow_only=0
			vector<point> points; // reused temporary
			vect_cube_t ped_bcubes; // reused temporary
			static brg_batch_draw_t bbd; // allocated memory is reused across calls
			int indir_bcs_ix(-1), indir_bix(-1);

			if (draw_interior) {
				per_bcs_exclude.resize(bcs.size());
				int_wall_draw_front.resize(bcs.size());
				int_wall_draw_back.resize(bcs.size());
			}
			for (auto i = bcs.begin(); i != bcs.end(); ++i) { // draw only nearby interiors
				unsigned const bcs_ix(i - bcs.begin());
				float const door_open_dist(get_door_open_dist());
				float const ddist_scale((*i)->building_draw_windows.empty() ? 0.05 : 1.0); // if there are no windows, we can wait until the player is very close to draw the interior
				occlusion_checker_noncity_t oc(**i);
				bool is_first_tile(1);

				for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
					if (reflection_pass && !g->bcube.contains_pt_xy(camera_xlated)) continue; // not the correct tile

					if (!g->bcube.closest_dist_less_than(camera_xlated, ddist_scale*interior_draw_dist)) { // too far
						if (g->has_room_geom) { // need to clear room geom
							for (auto bi = g->bc_ixs.begin(); bi != g->bc_ixs.end(); ++bi) {(*i)->get_building(bi->ix).clear_room_geom(0);} // force=0
							g->has_room_geom = 0;
						}
						continue;
					}
					if (!camera_pdu.sphere_and_cube_visible_test((g->bcube.get_cube_center() + xlate), g->bcube.get_bsphere_radius(), (g->bcube + xlate))) continue; // VFC
					if (is_first_tile) {(*i)->ensure_interior_geom_vbos();} // we need the interior geom at this point
					(*i)->building_draw_interior.draw_tile(s, (g - (*i)->grid_by_tile.begin()));
					// iterate over nearby buildings in this tile and draw interior room geom, generating it if needed
					if (!g->bcube.closest_dist_less_than(camera_xlated, ddist_scale*room_geom_draw_dist)) continue; // too far
					if (is_first_tile && !reflection_pass) {oc.set_camera(camera_pdu);} // setup occlusion culling on the first visible tile
					is_first_tile = 0;
					
					for (auto bi = g->bc_ixs.begin(); bi != g->bc_ixs.end(); ++bi) {
						building_t &b((*i)->get_building(bi->ix));
						if (!b.interior) continue; // no interior, skip
						if (reflection_pass && !b.bcube.contains_pt_xy(camera_xlated)) continue; // not the correct building
						if (!b.bcube.closest_dist_less_than(camera_xlated, ddist_scale*room_geom_draw_dist)) continue; // too far away
						if (!camera_pdu.cube_visible(b.bcube + xlate)) continue; // VFC
						b.maybe_gen_chimney_smoke();
						bool const camera_near_building(b.bcube.contains_pt_xy_exp(camera_xlated, door_open_dist));
						if (!camera_near_building && !b.has_windows()) continue; // player is outside a windowless building (city office building)
						bool const player_in_building_bcube(b.bcube.contains_pt_xy(camera_xlated)); // player is within the building's bcube
						if ((display_mode & 0x08) && !player_in_building_bcube && b.is_entire_building_occluded(camera_xlated, oc)) continue; // check occlusion
						int const ped_ix((*i)->get_ped_ix_for_bix(bi->ix)); // Note: assumes only one building_draw has people
						bool const inc_small(b.bcube.closest_dist_less_than(camera_xlated, ddist_scale*room_geom_sm_draw_dist));
						b.gen_and_draw_room_geom(&bbd, s, oc, xlate, ped_bcubes, bi->ix, ped_ix, 0, reflection_pass, inc_small, player_in_building_bcube); // shadow_only=0
						g->has_room_geom = 1;
						if (!draw_interior) continue;
						if (ped_ix >= 0) {draw_peds_in_building(ped_ix, ped_draw_vars_t(b, oc, s, xlate, bi->ix, 0, reflection_pass));} // draw people in this building
						// check the bcube rather than check_point_or_cylin_contained() so that it works with roof doors that are outside any part?
						if (!camera_near_building) {b.player_not_near_building(); continue;} // camera not near building
						if (reflection_pass == 2) continue; // interior room, don't need to draw windows and exterior doors
						b.get_nearby_ext_door_verts(ext_door_draw, s, camera_xlated, door_open_dist); // and draw opened door
						bool const camera_in_building(b.check_point_or_cylin_contained(camera_xlated, 0.0, points));
						if (!reflection_pass) {b.update_grass_exclude_at_pos(camera_xlated, xlate, camera_in_building);} // disable any grass inside the building part(s) containing the player
						// Note: if we skip this check and treat all walls/windows as front/containing part, this almost works, but will skip front faces of other buildings
						if (!camera_in_building) continue; // camera not in building
						// pass in camera pos to only include the part that contains the camera to avoid drawing artifacts when looking into another part of the building
						// neg offset to move windows on the inside of the building's exterior wall;
						// since there are no basement windows, we should treat the player as being in the part above so that windows are drawn correctly through the basement stairs
						point pt_ag(camera_xlated);
						max_eq(pt_ag.z, (b.ground_floor_z1 + b.get_floor_thickness()));
						b.get_all_drawn_window_verts(interior_wind_draw, 0, -0.1, &pt_ag);
						assert(bcs_ix < int_wall_draw_front.size() && bcs_ix < int_wall_draw_back.size());
						b.get_split_int_window_wall_verts(int_wall_draw_front[bcs_ix], int_wall_draw_back[bcs_ix], pt_ag, 0);
						building_cont_player = &b; // there can be only one
						per_bcs_exclude[bcs_ix] = b.ext_side_qv_range;
						if (reflection_pass) continue; // don't execute the code below
						this_frame_camera_in_building  = 1;
						this_frame_player_in_basement |= b.is_pos_in_basement(camera_xlated - vector3d(0.0, 0.0, BASEMENT_ENTRANCE_SCALE*b.get_floor_thickness()));
						player_building = &b;
						if (display_mode & 0x10) {indir_bcs_ix = bcs_ix; indir_bix = bi->ix;} // compute indirect lighting for this building
						// run any player interaction logic here
						if (toggle_room_light  ) {b.toggle_room_light(camera_xlated);}
						if (building_action_key) {b.apply_player_action_key(camera_xlated, cview_dir, (building_action_key-1), 0);} // check_only=0
						else {can_do_building_action = b.apply_player_action_key(camera_xlated, cview_dir, 0, 1);} // mode=0, check_only=1
						b.player_pickup_object(camera_xlated, cview_dir);
						if (teleport_to_screenshot) {b.maybe_teleport_to_screenshot();}
						if (animate2) {b.update_player_interact_objects(camera_xlated, bi->ix, ped_ix);} // update dynamic objects if the player is in the building
					} // for bi
				} // for g
			} // for i
			bbd.draw_and_clear(s);
			if (ADD_ROOM_LIGHTS) {set_std_depth_func();} // restore
			glDisable(GL_CULL_FACE);

			if (!reflection_pass) { // update once; non-interior buildings (such as city buildings) won't update this
				camera_in_building = this_frame_camera_in_building;
				player_in_basement = this_frame_player_in_basement;
			}
			reset_interior_lighting_and_end_shader(s);
			reflection_shader.clear();

			// update indir lighting using ray casting
			if (indir_bcs_ix >= 0 && indir_bix >= 0) {indir_tex_mgr.create_for_building(bcs[indir_bcs_ix]->get_building(indir_bix), indir_bix, camera_xlated);}
			else if (!reflection_pass) {end_building_rt_job();}
			
			if (draw_interior && have_windows && reflection_pass != 2) { // write to stencil buffer, use stencil test for back facing building walls
				shader_t holes_shader;
				setup_smoke_shaders(holes_shader, 0.9, 0, 0, 0, 0, 0, 0); // min_alpha=0.9 for depth test
				setup_stencil_buffer_write();
				glStencilOpSeparate((reflection_pass ? GL_BACK : GL_FRONT), GL_KEEP, GL_KEEP, GL_KEEP); // ignore front faces
				glStencilOpSeparate((reflection_pass ? GL_FRONT : GL_BACK), GL_KEEP, GL_KEEP, GL_INCR); // mark stencil on back faces
				interior_wind_draw.draw(holes_shader, 0, 1); // draw back facing windows; direct_draw_no_vbo=1
				end_stencil_write();
			}
		} // end have_interior
		if (!reflection_pass) {toggle_room_light = teleport_to_screenshot = 0; building_action_key = 0;} // reset these even if the player wasn't in a building

		if (draw_interior && reflection_pass != 2) { // skip for interior room reflections (but what about looking out through the bathroom door?)
			// draw back faces of buildings, which will be interior walls
			setup_building_draw_shader(s, min_alpha, 1, 1, 1); // enable_indir=1, force_tsl=1, use_texgen=1
			glEnable(GL_CULL_FACE);
			glCullFace(reflection_pass ? GL_BACK : GL_FRONT); // draw back faces

			for (auto i = bcs.begin(); i != bcs.end(); ++i) {
				if ((*i)->empty()) continue; // no buildings
				unsigned const bcs_ix(i - bcs.begin());
				vertex_range_t const *exclude(nullptr);
				building_mat_t const &mat((*i)->buildings.front().get_material()); // Note: assumes all wall textures have a consistent tscale
				// translate texture near the camera to get better tex coord resolution; make a multiple of tscale to avoid visible shift
				vector3d texgen_origin(xoff2*DX_VAL, yoff2*DY_VAL, 0.0);
				for (unsigned d = 0; d < 2; ++d) {texgen_origin[d] = mat.wall_tex.tscale_x*int(texgen_origin[d]/mat.wall_tex.tscale_x);}
				s.add_uniform_vector3d("texgen_origin", texgen_origin);
				setup_texgen_full(2.0f*mat.wall_tex.tscale_x, 2.0f*mat.wall_tex.tscale_x, 0.0, 0.0, 0.0, 0.0, 2.0f*mat.wall_tex.tscale_y, 0.0, s, 0);
				
				if (!per_bcs_exclude.empty()) { // draw this range using stencil test but the rest of the buildings without stencil test
					vertex_range_t const &vr(per_bcs_exclude[bcs_ix]);

					if (vr.draw_ix >= 0) { // nonempty
						exclude = &vr; // use this exclude
						glEnable(GL_STENCIL_TEST);
						glStencilFunc(GL_EQUAL, 0, ~0U); // keep if stencil bit has not been set by above pass
						glStencilOpSeparate(GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
						int_wall_draw_front[bcs_ix].draw(s, 0, 1); // draw back facing walls for front part of building with    stencil test
						glDisable(GL_STENCIL_TEST);
						int_wall_draw_back [bcs_ix].draw(s, 0, 1); // draw back facing walls for back  part of building without stencil test
					}
				}
				(*i)->building_draw_int_ext_walls.draw(s, 0, 0, 0, exclude); // exterior walls only, no stencil test
				s.add_uniform_vector3d("texgen_origin", zero_vector);
			} // for i
			reset_interior_lighting_and_end_shader(s);

			if (DRAW_EXT_REFLECTIONS || !reflection_pass) {
				// if we're not by an exterior door, draw the back sides of exterior doors as closed; always draw non-ext walls/non doors (roof geom)
				int const tex_filt_mode(ext_door_draw.empty() ? 2 : 3);
				setup_building_draw_shader(s, min_alpha, 0, 1, 0); // enable_indir=0, force_tsl=1, use_texgen=0
				for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_vbo.draw(s, 0, 0, tex_filt_mode);}
				reset_interior_lighting_and_end_shader(s);
			}
			glCullFace(reflection_pass ? GL_FRONT : GL_BACK); // draw front faces

			if (!reflection_pass) { // draw windows and doors in depth pass to create holes
				shader_t holes_shader;
				setup_smoke_shaders(holes_shader, 0.9, 0, 0, 0, 0, 0, 0); // min_alpha=0.9 for depth test - need same shader to avoid z-fighting
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // Disable color writing, we only want to write to the Z-Buffer
				if (!ext_door_draw.empty()) {glDisable(GL_DEPTH_CLAMP);}
				for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_windows.draw(holes_shader, 0);} // draw windows on top of other buildings
				glEnable(GL_DEPTH_CLAMP); // make sure holes are not clipped by the near plane
				ext_door_draw.draw(holes_shader, 0, 1); // direct_draw_no_vbo=1
				setup_depth_clamp(); // restore
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				holes_shader.end_shader();
			}
			glDisable(GL_CULL_FACE);

			if (reflection_pass != 2 && have_buildings_ext_paint()) { // draw spraypaint/markers on building exterior, if needed
				setup_building_draw_shader(s, DEF_CITY_MIN_ALPHA, 1, 1, 0); // alpha test, enable_indir=1, force_tsl=1, use_texgen=0
				draw_buildings_ext_paint();
				reset_interior_lighting_and_end_shader(s);
			}
		} // end draw_interior

		// everything after this point is part of the building exteriors and uses city lights rather than building room lights
		if (reflection_pass && (!DRAW_EXT_REFLECTIONS || reflection_pass != 3)) { // skip this early exit for house reflections if enabled
			fgPopMatrix();
			enable_dlight_bcubes = 0;
			return;
		}
		city_dlight_pcf_offset_scale = 1.0; // restore city value
		if (!reflection_pass) {setup_city_lights(xlate);}

		// main/batched draw pass
		setup_smoke_shaders(s, min_alpha, 0, 0, indir, 1, dlights, 0, 0, (use_smap ? 2 : 1), use_bmap, 0, 0, 0, 0.0, 0.0, 0, 0, 1); // is_outside=1

		if (!reflection_pass) { // don't want to do this in the reflection pass
			for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw.init_draw_frame();}
		}
		glEnable(GL_CULL_FACE);
		glCullFace(reflection_pass ? GL_FRONT : GL_BACK);
		if (!ext_door_draw.empty()) {glDisable(GL_DEPTH_CLAMP);} // if an exterior door was drawn, make sure we don't clamp the walls over the holes

		if (1/*!reflection_pass*/) { // draw front faces of buildings; nearby shadowed buildings will be drawn later
			for (unsigned ix = 0; ix < max_draw_ix; ++ix) {
				for (auto i = bcs.begin(); i != bcs.end(); ++i) {
					if (!(*i)->use_smap_this_frame) {(*i)->building_draw_vbo.draw_block(s, ix, 0);}
				}
			}
		}
		set_std_depth_func_with_eq();
		glPolygonOffset(-1.0, -1.0); // useful for avoiding z-fighting on building windows

		if (have_windows) { // draw windows, front facing only (not viewed from interior)
			bool const transparent_windows(draw_interior && !reflection_pass);
			enable_blend();
			glDepthMask(GL_FALSE); // disable depth writing
			glEnable(GL_POLYGON_OFFSET_FILL);

			for (auto i = bcs.begin(); i != bcs.end(); ++i) { // draw windows on top of other buildings
				// need to swap opaque window texture with transparent texture for this draw pass
				if (transparent_windows) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
				(*i)->building_draw_windows.draw(s, 0);
				if (transparent_windows) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
			}
			//interior_wind_draw.draw(s, 0, 1); // draw opaque front facing windows of building the player is in; direct_draw_no_vbo=1
			glDisable(GL_POLYGON_OFFSET_FILL);
			glDepthMask(GL_TRUE); // re-enable depth writing
			disable_blend();
		}
		glDisable(GL_CULL_FACE);
		if (building_cont_player) {building_cont_player->write_basement_entrance_depth_pass(s);} // drawn last
		s.end_shader();

		// post-pass to render building exteriors in nearby tiles that have shadow maps; shadow maps don't work right when using reflections
		if (use_tt_smap && !reflection_pass) {
			//timer_t timer2("Draw Buildings Smap"); // 0.3
			bool const use_city_dlights(!reflection_pass);
			city_shader_setup(s, get_city_lights_bcube(), use_city_dlights, 1, use_bmap, min_alpha); // use_smap=1
			float const draw_dist(get_tile_smap_dist() + 0.5f*(X_SCENE_SIZE + Y_SCENE_SIZE));
			glEnable(GL_CULL_FACE); // cull back faces to avoid lighting/shadows on inside walls of building interiors

			for (auto i = bcs.begin(); i != bcs.end(); ++i) {
				bool const single_tile((*i)->is_single_tile()), no_depth_write(!single_tile);
				if (single_tile && !(*i)->use_smap_this_frame) continue; // optimization
				if (no_depth_write) {glDepthMask(GL_FALSE);} // disable depth writing

				for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
					if (!g->bcube.closest_dist_less_than(camera_xlated, draw_dist)) continue; // too far
					point const pos(g->bcube.get_cube_center() + xlate);
					if (!camera_pdu.sphere_and_cube_visible_test(pos, g->bcube.get_bsphere_radius(), (g->bcube + xlate))) continue; // VFC
					if (!try_bind_tile_smap_at_point(pos, s)) continue; // no shadow maps - not drawn in this pass
					unsigned const tile_id(g - (*i)->grid_by_tile.begin());
					(*i)->building_draw_vbo.draw_tile(s, tile_id);

					if (!(*i)->building_draw_windows.empty()) {
						enable_blend();
						glEnable(GL_POLYGON_OFFSET_FILL);
						if (!no_depth_write) {glDepthMask(GL_FALSE);} // always disable depth writing
						if (draw_interior) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
						(*i)->building_draw_windows.draw_tile(s, tile_id); // draw windows on top of other buildings
						if (draw_interior) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
						if (!no_depth_write) {glDepthMask(GL_TRUE);} // always re-enable depth writing
						glDisable(GL_POLYGON_OFFSET_FILL);
						disable_blend();
					}
				} // for g
				if (no_depth_write) {glDepthMask(GL_TRUE);} // re-enable depth writing
			} // for i
			glDisable(GL_CULL_FACE);
			s.end_shader();
		}
		if (night && have_wind_lights) { // add night time random lights in windows
			enable_blend();
			glDepthMask(GL_FALSE); // disable depth writing
			float const low_v(0.5 - WIND_LIGHT_ON_RAND), high_v(0.5 + WIND_LIGHT_ON_RAND), lit_thresh_mult(1.0 + 2.0*CLIP_TO_01((light_factor - low_v)/(high_v - low_v)));
			s.end_shader();
			s.set_vert_shader("window_lights");
			s.set_frag_shader("linear_fog.part+window_lights");
			s.set_prefix("#define FOG_FADE_TO_TRANSPARENT", 1);
			setup_tt_fog_pre(s);
			s.begin_shader();
			s.add_uniform_float("lit_thresh_mult", lit_thresh_mult); // gradual transition of lit window probability around sunset
			setup_tt_fog_post(s);
			for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_wind_lights.draw(s, 0);} // add bloom?
			glDepthMask(GL_TRUE); // re-enable depth writing
			disable_blend();
		}
		if (!ext_door_draw.empty()) {setup_depth_clamp();} // restore
		glCullFace(GL_BACK);
		set_std_depth_func();
		fgPopMatrix();
		enable_dlight_bcubes = 0;

		if (player_building != prev_player_building) { // building transition
			if (player_building     ) {player_building     ->register_player_enter_building();}
			if (prev_player_building) {prev_player_building->register_player_exit_building ();}
		}
	}

	void draw_building_lights(vector3d const &xlate) { // add night time lights to buildings; non-const because it modifies building_lights
		if (empty() || !is_night(WIND_LIGHT_ON_RAND)) return;
		//timer_t timer("Building Lights"); // 0.06ms
		set_additive_blend_mode();
		enable_blend();
		glDepthMask(GL_FALSE); // disable depth writing
		vector3d const max_extent(get_buildings_max_extent());
		float const draw_dist(20.0*max_extent.mag());
		point const camera(get_camera_pos() - xlate); // in building space
		colorRGBA const light_colors[16] = {RED,RED,RED,RED,RED,RED,RED,RED, BLUE,BLUE,BLUE,BLUE, WHITE,WHITE, YELLOW, GREEN};

		for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) {
			if (!g->bcube.closest_dist_less_than(camera, draw_dist)) continue; // too far away
			if (!camera_pdu.cube_visible(g->bcube + xlate)) continue;

			for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {
				building_t const &b(get_building(i->ix));
				if (b.details.empty()) continue;
				if (!is_night((((321*i->ix) & 7)/7.0)*WIND_LIGHT_ON_RAND)) continue; // gradually turn on
				if (!b.bcube.closest_dist_less_than(camera, draw_dist)) continue; // too far away
				if (!camera_pdu.cube_visible(b.bcube + xlate)) continue;
				
				for (auto j = b.details.begin(); j != b.details.end(); ++j) {
					if (j->type != ROOF_OBJ_ANT) continue; // not an antenna
					unsigned const num_segs(max(1U, (((123*i->ix) & 3) + unsigned(6.0*j->dz()/max_extent.z)))); // some mix of height and randomness
					point const center(j->get_cube_center());
					point pos(point(center.x, center.y, j->z2()) + xlate);
					float const radius(1.2f*(j->dx() + j->dy())), z_step(0.6*j->dz()/num_segs);
					float const alpha(min(1.0f, 1.5f*(1.0f - p2p_dist(camera, center)/draw_dist))); // fade with distance
					colorRGBA const color(light_colors[i->ix & 15], alpha);

					for (unsigned n = 0; n < num_segs; ++n) { // distribute lights along top half of antenna
						building_lights.add_pt(sized_vert_t<vert_color>(vert_color(pos, color), radius));
						pos.z -= z_step;
					}
				} // for j
			} // for i
		} // for g
		building_lights.draw_and_clear(BLUR_TEX, 0.0, 0, 1, 0.005); // use geometry shader for unlimited point size
		glDepthMask(GL_TRUE); // re-enable depth writing
		disable_blend();
		set_std_blend_mode();
	}

	void get_all_window_verts(building_draw_t &bdraw, bool light_pass) {
		bdraw.clear();

		for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
			bdraw.cur_tile_id = (g - grid_by_tile.begin());
			for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {get_building(i->ix).get_all_drawn_window_verts(bdraw, light_pass);}
		}
		bdraw.finalize(grid_by_tile.size());
	}
	void get_interior_drawn_verts() {
		// pre-allocate interior wall, celing, and floor verts, assuming all buildings have the same materials
		tid_vert_counter_t vert_counter;

		for (auto b = buildings.begin(); b != buildings.end(); ++b) {
			if (!b->interior) continue; // no interior
			building_mat_t const &mat(b->get_material());
			unsigned const nv_wall(16*(b->interior->walls[0].size() + b->interior->walls[1].size() + b->interior->landings.size()) + 36*b->interior->elevators.size());
			unsigned const nfloors(b->interior->floors.size());
			vert_counter.update_count(mat.house_floor_tex.tid, 4*((b->is_house && nfloors > 0) ? (nfloors - b->has_sec_bldg()) : 0));
			vert_counter.update_count(mat.floor_tex.tid, 4*(b->is_house ? b->has_sec_bldg() : nfloors));
			vert_counter.update_count((b->is_house ? mat.house_ceil_tex.tid : mat.ceil_tex.tid ), 4*b->interior->ceilings.size());
			vert_counter.update_count(mat.wall_tex.tid, nv_wall);
			vert_counter.update_count(FENCE_TEX, 12*b->interior->elevators.size());
			vert_counter.update_count(building_texture_mgr.get_hdoor_tid(), 8*b->interior->doors.size());
			vert_counter.update_count(WHITE_TEX, 8*b->interior->doors.size());
		}
		for (unsigned i = 0; i < tid_mapper.get_num_slots(); ++i) {
			unsigned const count(vert_counter.get_count(i));
			if (count > 0) {building_draw_interior.reserve_verts(tid_nm_pair_t(i), count);}
		}
		// generate vertex data
		building_draw_interior.clear();
		building_draw_int_ext_walls.clear();

		for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
			building_draw_interior.cur_tile_id = (g - grid_by_tile.begin());

			for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {
				get_building(i->ix).get_all_drawn_verts(building_draw_interior,      0, 1, 0); // interior
				get_building(i->ix).get_all_drawn_verts(building_draw_int_ext_walls, 0, 0, 1); // interior of exterior walls
			}
		}
		building_draw_interior.finalize(grid_by_tile.size());
		building_draw_int_ext_walls.finalize(grid_by_tile.size());
	}
	void get_all_drawn_verts(bool is_tile) { // Note: non-const; building_draw is modified
		if (buildings.empty()) return;
		//timer_t timer("Get Building Verts"); // 140/670
		int const num_passes(is_tile ? 2 : 3); // skip interior pass for tiles; these verts will be generated later when they're needed for drawing

#pragma omp parallel for schedule(static) num_threads(num_passes)
		for (int pass = 0; pass < num_passes; ++pass) { // parallel loop doesn't help much because pass 0 takes most of the time
			if (pass == 0) { // exterior pass
				building_draw_vbo.clear();

				for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
					building_draw_vbo.cur_tile_id = (g - grid_by_tile.begin());
					for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {get_building(i->ix).get_all_drawn_verts(building_draw_vbo, 1, 0, 0);} // exterior
				}
				building_draw_vbo.finalize(grid_by_tile.size());
			}
			else if (pass == 1) { // windows pass
				get_all_window_verts(building_draw_windows, 0);
				if (is_night(WIND_LIGHT_ON_RAND)) {get_all_window_verts(building_draw_wind_lights, 1);} // only generate window verts at night
			}
			else if (pass == 2) { // interior pass; skip for is_tile case
				get_interior_drawn_verts();
			}
		} // for pass
	}
	void update_mem_usage(bool is_tile) {
		unsigned const num_everts(building_draw_vbo.num_verts() + building_draw_windows.num_verts() + building_draw_wind_lights.num_verts());
		unsigned const num_etris( building_draw_vbo.num_tris () + building_draw_windows.num_tris () + building_draw_wind_lights.num_tris ());
		unsigned const num_iverts(building_draw_interior.num_verts() + building_draw_int_ext_walls.num_verts());
		unsigned const num_itris( building_draw_interior.num_tris () + building_draw_int_ext_walls.num_tris ());
		gpu_mem_usage += (num_everts + num_iverts)*sizeof(vert_norm_comp_tc_color);
		if (!is_tile) {cout << "Building V: " << num_everts << ", T: " << num_etris << ", interior V: " << num_iverts << ", T: " << num_itris << ", mem: " << gpu_mem_usage << endl;}
	}
	void create_vbos(bool is_tile) {
		building_texture_mgr.check_windows_texture();
		tid_mapper.init();
		timer_t timer("Create Building VBOs", !is_tile);
		get_all_drawn_verts(is_tile);
		update_mem_usage(is_tile);
		building_draw_vbo.upload_to_vbos();
		building_draw_windows.upload_to_vbos();
		building_draw_wind_lights.upload_to_vbos(); // Note: may be empty if not night time
		building_draw_interior.upload_to_vbos();
		building_draw_int_ext_walls.upload_to_vbos();
	}
	void ensure_interior_geom_vbos() { // only for is_tile case
		if (!has_interior_geom) return; // no interior geom, nothing to do
		if (!building_draw_interior.empty()) return; // already created
		//timer_t timer("Create Building Interiors VBOs");
		get_interior_drawn_verts();
		update_mem_usage(1); // is_tile=1
		building_draw_interior.upload_to_vbos();
		building_draw_int_ext_walls.upload_to_vbos();
	}
	void ensure_window_lights_vbos() {
		if (!building_draw_wind_lights.empty()) return; // already calculated
		building_texture_mgr.check_windows_texture();
		get_all_window_verts(building_draw_wind_lights, 1);
		building_draw_wind_lights.upload_to_vbos();
	}
	void clear_vbos() {
		building_draw.clear_vbos();
		building_draw_vbo.clear_vbos();
		building_draw_windows.clear_vbos();
		building_draw_wind_lights.clear_vbos();
		building_draw_interior.clear_vbos();
		building_draw_int_ext_walls.clear_vbos();
		for (auto i = buildings.begin(); i != buildings.end(); ++i) {i->clear_room_geom(1);} // force=1; likely required for tiled buildings
		gpu_mem_usage = 0;
	}
	bool check_sphere_coll(point &pos, point const &p_last, float radius, bool xy_only=0, vector3d *cnorm=nullptr, bool check_interior=0) const { // Note: pos is in camera space
		if (empty()) return 0;
		vector3d const xlate(get_camera_coord_space_xlate());
		vect_cube_t ped_bcubes; // reused across calls
		vector<point> points; // reused across calls

		if (radius == 0.0) { // point coll - ignore p_last as well
			point const p1x(pos - xlate);
			if (!range.contains_pt_xy(p1x)) return 0; // outside buildings bcube
			unsigned const gix(get_grid_ix(p1x));
			grid_elem_t const &ge(grid[gix]);
			if (ge.empty()) return 0; // skip empty grid
			if (!(xy_only ? ge.bcube.contains_pt_xy(p1x) : ge.bcube.contains_pt(p1x))) return 0; // no intersection - skip this grid

			for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
				if (!(xy_only ? b->contains_pt_xy(p1x) : b->contains_pt(p1x))) continue;
				if (get_building(b->ix).check_sphere_coll(pos, p_last, ped_bcubes, xlate, 0.0, xy_only, points, cnorm, check_interior)) return 1;
			}
			return check_road_seg_sphere_coll(ge, pos, p_last, xlate, radius, xy_only, cnorm);
		}
		float const expand_val(3.0*radius); // use a larger value to handle things outside the building bcube such as AC units
		cube_t bcube; bcube.set_from_sphere((pos - xlate), expand_val);
		if (!range.intersects_xy(bcube)) return 0; // outside buildings bcube
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		float const dist(p2p_dist(pos, p_last));

		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.empty()) continue; // skip empty grid
				if (!(xy_only ? sphere_cube_intersect_xy(pos, (radius + dist), (ge.bcube + xlate)) :
					sphere_cube_intersect(pos, (radius + dist), (ge.bcube + xlate)))) continue; // Note: makes little difference

				// Note: assumes buildings are separated so that only one sphere collision can occur
				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
					if (!b->intersects_xy(bcube)) continue;

					if (check_interior) {
						ped_bcubes.clear();
						int const ped_ix(get_ped_ix_for_bix(b->ix));
						if (ped_ix >= 0) {get_ped_bcubes_for_building(ped_ix, b->ix, ped_bcubes);}
					}
					if (get_building(b->ix).check_sphere_coll(pos, p_last, ped_bcubes, xlate, radius, xy_only, points, cnorm, check_interior)) return 1;
				} // for b
				if (check_road_seg_sphere_coll(ge, pos, p_last, xlate, radius, xy_only, cnorm)) return 1;
			} // for x
		} // for y
		return 0;
	}
	bool check_road_seg_sphere_coll(grid_elem_t const &ge, point &pos, point const &p_last, vector3d const &xlate, float radius, bool xy_only, vector3d *cnorm) const {
		for (auto r = ge.road_segs.begin(); r != ge.road_segs.end(); ++r) {
			cube_t const cube(*r + xlate);
			if (!cube.contains_pt_xy(pos) || (pos.z - radius) > cube.z2()) continue; // no collision - test top surface only
			pos.z = cube.z2() + radius;
			if (cnorm) {*cnorm = plus_z;}
			return 1;
		}
		return 0;
	}
	// Note: pos is in building space, out is in camera space; no building rotation applied
	void get_driveway_sphere_coll_cubes(point const &pos, float radius, bool xy_only, vect_cube_t &out) const {
		if (empty()) return;
		cube_t bcube; bcube.set_from_sphere(pos, radius);
		if (!range.intersects_xy(bcube)) return; // outside buildings bcube
		vector3d const xlate(get_camera_coord_space_xlate());
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);

		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.road_segs.empty()) continue; // skip empty grid

				// Note: no grid bcube test for now; driveways are sparse so it's probably not needed
				for (auto r = ge.road_segs.begin(); r != ge.road_segs.end(); ++r) {
					if (check_bcube_sphere_coll(*r, pos, radius, xy_only)) {out.push_back(*r + xlate);}
				}
			} // for x
		} // for y
	}

	// Note: p1 and p2 are in building space
	unsigned check_line_coll(point const &p1, point const &p2, float &t, unsigned &hit_bix, bool ret_any_pt, bool no_coll_pt) const {
		if (empty()) return 0;
		vector<point> points; // reused across calls

		if (p1.x == p2.x && p1.y == p2.y) { // vertical line special case optimization (for example map mode)
			if (!get_bcube().contains_pt_xy(p1)) return 0;
			unsigned const gix(get_grid_ix(p1));
			grid_elem_t const &ge(grid[gix]);
			if (ge.bc_ixs.empty()) return 0; // skip empty grid
			if (!ge.bcube.contains_pt_xy(p1)) return 0; // no intersection - skip this grid

			for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
				if (!b->contains_pt_xy(p1)) continue;
				unsigned const ret(get_building(b->ix).check_line_coll(p1, p2, t, points, 0, ret_any_pt, no_coll_pt));
				if (ret) {hit_bix = b->ix; return ret;} // can only intersect one building
			} // for b
			return 0; // no coll
		}
		cube_t bcube(p1, p2);
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		unsigned coll(0); // 0=none, 1=side, 2=roof
		point end_pos(p2);

		// for now, just do a slow iteration over every grid element within the line's bbox in XY
		// Note: should probably iterate over the grid in XY order from the start to the end of the line, or better yet use a line drawing algorithm
		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty()) continue; // skip empty grid
				if (!check_line_clip(p1, end_pos, ge.bcube.d)) continue; // no intersection - skip this grid

				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) { // Note: okay to check the same building more than once
					if (!b->intersects(bcube)) continue;
					float t_new(t);
					unsigned const ret(get_building(b->ix).check_line_coll(p1, p2, t_new, points, 0, ret_any_pt, no_coll_pt));

					if (ret && t_new <= t) { // closer hit pos, update state
						t = t_new; hit_bix = b->ix; coll = ret;
						end_pos = p1 + t*(p2 - p1);
						if (ret_any_pt) return coll;
					}
				} // for b
			} // for x
		} // for y
		return coll; // 0=none, 1=side, 2=roof, 3=details
	}

	// Note: p1 and p2 are in building space
	void update_zmax_for_line(point const &p1, point const &p2, float radius, float house_extra_zval, float &cur_zmax) const {
		if (empty()) return;
		cube_t bcube(p1, p2);
		bcube.expand_by_xy(radius);
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		vector<point> points; // reused across calls
		
		// for now, just do a slow iteration over every grid element within the line's bbox in XY
		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty() || ge.bcube.z2() < cur_zmax) continue; // skip empty grid or grids below the line
				cube_t grid_bc(ge.bcube);
				grid_bc.expand_by_xy(radius);
				if (!check_line_clip(p1, p2, grid_bc.d)) continue; // no intersection - skip this grid

				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) { // Note: okay to check the same building more than once
					if (b->z2() < cur_zmax) continue;
					cube_t bbc(*b);
					bbc.expand_by_xy(radius);
					if (!b->intersects(bbc) || !check_line_clip(p1, p2, bbc.d)) continue;
					building_t const &building(get_building(b->ix));
					float t(1.0); // result is unused
					// Note: unclear if we need to do a detailed line collision check, maybe testing the building bbox is good enough?
					// if radius is passed in as nonzero, then simply assume it intersects because check_line_coll() can't easily take a radius value
					if (radius > 0.0 || building.check_line_coll(p1, p2, t, points, 0, 1, 1)) {
						max_eq(cur_zmax, (b->z2() + (building.is_house ? house_extra_zval : 0.0f)));
					}
				} // for b
			} // for x
		} // for y
	}

	// Note: we can get building_id by calling check_ped_coll() or get_building_bcube_at_pos(); p1 and p2 are in building space
	bool check_line_coll_building(point const &p1, point const &p2, unsigned building_id) const { // Note: not thread safe due to static points
		assert(building_id < buildings.size());
		static vector<point> points; // reused across calls
		float t_new(1.0);
		return buildings[building_id].check_line_coll(p1, p2, t_new, points, 0, 1);
	}

	int get_building_bcube_contains_pos(point const &pos) { // Note: not thread safe due to static points
		if (empty()) return -1;
		unsigned const gix(get_grid_ix(pos));
		grid_elem_t const &ge(grid[gix]);
		if (ge.bc_ixs.empty() || !ge.bcube.contains_pt(pos)) return -1; // skip empty or non-containing grid
		static vector<point> points; // reused across calls

		for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
			if (b->contains_pt(pos)) {return b->ix;} // found
		}
		return -1;
	}

	bool check_ped_coll(point const &pos, float radius, unsigned plot_id, unsigned &building_id) const { // Note: not thread safe due to static points
		if (empty()) return 0;
		assert(plot_id < bix_by_plot.size());
		vector<unsigned> const &bixes(bix_by_plot[plot_id]); // should be populated in gen()
		if (bixes.empty()) return 0;
		cube_t bcube; bcube.set_from_sphere(pos, radius);
		static vector<point> points; // reused across calls

		// Note: assumes buildings are separated so that only one ped collision can occur
		for (auto b = bixes.begin(); b != bixes.end(); ++b) {
			building_t const &building(get_building(*b));
			if (building.bcube.x1() > bcube.x2()) break; // no further buildings can intersect (sorted by x1)
			if (!building.bcube.intersects_xy(bcube)) continue;
			if (building.check_point_or_cylin_contained(pos, 2.0*radius, points)) {building_id = *b; return 1;} // double the radius value to add padding to account for inaccuracy
		}
		return 0;
	}
	bool select_building_in_plot(unsigned plot_id, unsigned rand_val, unsigned &building_id) const {
		if (bix_by_plot.empty()) return 0; // not setup / no buildings
		assert(plot_id < bix_by_plot.size());
		vector<unsigned> const &bixes(bix_by_plot[plot_id]);
		if (bixes.empty()) return 0;
		building_id = bixes[rand_val % bixes.size()];
		return 1;
	}

	void query_for_cube(cube_t const &query_cube, vect_cube_t &cubes, int query_mode) const { // Note: called on init, don't need to use get_camera_coord_space_xlate()
		if (empty()) return; // nothing to do
		unsigned ixr[2][2];
		get_grid_range(query_cube, ixr);

		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty() || !query_cube.intersects_xy(ge.bcube)) continue;

				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
					if (!query_cube.intersects_xy(*b)) continue;
					if (get_grid_ix(query_cube.get_llc().max(b->get_llc())) != (y*grid_sz + x)) continue; // add only if in home grid (to avoid duplicates)
					if      (query_mode == 0) {cubes.push_back(*b);} // return building bcube
					else if (query_mode == 1) {get_building(b->ix).maybe_add_house_driveway(query_cube, cubes, b->ix);} // return house driveways
					else {assert(0);} // invalid mode/not implemented
				}
			} // for x
		} // for y
	}
	void get_overlapping_bcubes      (cube_t const &xy_range, vect_cube_t &bcubes   ) const {return query_for_cube(xy_range, bcubes,    0);}
	void add_house_driveways_for_plot(cube_t const &plot,     vect_cube_t &driveways) const {return query_for_cube(plot,     driveways, 1);}

	void get_power_points(cube_t const &xy_range, vector<point> &ppts) const { // similar to above function, but returns points rather than cubes
		if (empty()) return; // nothing to do
		unsigned ixr[2][2];
		get_grid_range(xy_range, ixr);

		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty() || !xy_range.intersects_xy(ge.bcube)) continue;

				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
					if (!xy_range.intersects_xy(*b)) continue;
					if (get_grid_ix(xy_range.get_llc().max(b->get_llc())) != (y*grid_sz + x)) continue; // add only if in home grid (to avoid duplicates)
					get_building(b->ix).get_power_point(ppts);
				}
			} // for x
		} // for y
	}

	void get_occluders(pos_dir_up const &pdu, building_occlusion_state_t &state) const {
		state.init(pdu.pos, get_camera_coord_space_xlate());
		
		for (auto g = grid.begin(); g != grid.end(); ++g) {
			if (g->bc_ixs.empty()) continue;
			point const pos(g->bcube.get_cube_center() + state.xlate);
			if (!pdu.sphere_and_cube_visible_test(pos, g->bcube.get_bsphere_radius(), (g->bcube + state.xlate))) continue; // VFC
			
			for (auto b = g->bc_ixs.begin(); b != g->bc_ixs.end(); ++b) {
				if ((int)b->ix == state.exclude_bix) continue; // excluded
				cube_t const c(*b + state.xlate); // check far clipping plane first because that's more likely to reject buildings
				// if player's inside this building, skip occlusion so that objects are visible through windows
				if (state.skip_cont_camera && !player_in_basement && c.contains_pt(pdu.pos) && get_building(b->ix).has_windows()) continue;
				if (dist_less_than(pdu.pos, c.closest_pt(pdu.pos), pdu.far_) && pdu.cube_visible(c)) {state.building_ids.push_back(*b);}
			}
		}
	}
	bool check_pts_occluded(point const *const pts, unsigned npts, building_occlusion_state_t &state) const { // pts are in building space
		point const pos_bs(state.pos - state.xlate);

		for (auto b = state.building_ids.begin(); b != state.building_ids.end(); ++b) {
			if ((int)b->ix == state.exclude_bix) continue;
			if (get_region(pos_bs, b->d) & get_region(pts[0], b->d)) continue; // line outside - early reject optimization
			building_t const &building(get_building(b->ix));
			bool occluded(1);

			for (unsigned i = 0; i < npts; ++i) {
				float t(1.0); // start at end of line
				if (!building.check_line_coll(pos_bs, pts[i], t, state.temp_points, 1)) {occluded = 0; break;}
			}
			if (occluded) return 1;
		} // for b
		return 0;
	}
	bool single_cube_visible_check(point const &pos, cube_t const &c) const {
		if (empty()) return 1;
		float const z(c.z2()); // top edge
		point const pts[4] = {point(c.x1(), c.y1(), z), point(c.x2(), c.y1(), z), point(c.x2(), c.y2(), z), point(c.x1(), c.y2(), z)};
		cube_t query_region(c);
		query_region.union_with_pt(pos);
		vector<point> temp_points; // could maybe reuse across calls if thread safe?

		for (auto g = grid.begin(); g != grid.end(); ++g) {
			if (g->bc_ixs.empty() || !g->bcube.intersects(query_region)) continue;

			for (auto b = g->bc_ixs.begin(); b != g->bc_ixs.end(); ++b) {
				if (!b->intersects(query_region)) continue;
				building_t const &building(get_building(b->ix));
				bool occluded(1);

				for (unsigned i = 0; i < 4; ++i) {
					float t(1.0); // start at end of line
					if (!building.check_line_coll(pos, pts[i], t, temp_points, 1)) {occluded = 0; break;}
				}
				if (occluded) return 0;
			} // for b
		} // for g
		return 1;
	}
}; // building_creator_t


class building_tiles_t {
	typedef pair<int, int> xy_pair;
	typedef map<xy_pair, building_creator_t> tile_map_t;
	tile_map_t tiles; // key is {x, y} pair
	//set<xy_pair> generated; // only used in heightmap terrain mode, and generally limited to the size of the heightmap in tiles
	vector3d max_extent;

	tile_map_t::const_iterator get_tile_by_pos_cs(point const &pos) const { // Note: pos is in camera space
		vector3d const xlate(get_camera_coord_space_xlate());
		int const x(round_fp(0.5f*(pos.x - xlate.x)/X_SCENE_SIZE)), y(round_fp(0.5f*(pos.y - xlate.y)/Y_SCENE_SIZE));
		return tiles.find(make_pair(x, y));
	}
	tile_map_t::const_iterator get_tile_by_pos_bs(point const &pos) const { // Note: pos is in building space
		int const x(round_fp(0.5f*pos.x/X_SCENE_SIZE)), y(round_fp(0.5f*pos.y/Y_SCENE_SIZE));
		return tiles.find(make_pair(x, y));
	}
public:
	building_tiles_t() : max_extent(zero_vector) {}
	bool     empty() const {return tiles.empty();}
	unsigned size()  const {return tiles.size();}
	vector3d get_max_extent() const {return max_extent;}

	int create_tile(int x, int y, bool allow_flatten) { // return value: 0=already exists, 1=newly generaged, 2=re-generated
		xy_pair const loc(x, y);
		auto it(tiles.find(loc));
		if (it != tiles.end()) return 0; // already exists
		//cout << "Create building tile " << x << "," << y << ", tiles: " << tiles.size() << endl; // 299 tiles
		building_creator_t &bc(tiles[make_pair(x, y)]); // insert it
		assert(bc.empty());
		int const border(allow_flatten ? 1 : 0); // add a 1 pixel border around the tile to avoid creating a seam when an adjacent tile's edge height is modified
		cube_t bcube(all_zeros);
		bcube.x1() = get_xval(x*MESH_X_SIZE + border);
		bcube.y1() = get_yval(y*MESH_Y_SIZE + border);
		bcube.x2() = get_xval((x+1)*MESH_X_SIZE - border);
		bcube.y2() = get_yval((y+1)*MESH_Y_SIZE - border);
		global_building_params.set_pos_range(bcube);
		int const rseed(x + (y << 16) + 12345); // should not be zero
		bc.gen(global_building_params, 0, have_cities(), 1, allow_flatten, rseed); // if there are cities, then tiles are non-city/secondary buildings
		global_building_params.restore_prev_pos_range();
		max_extent = max_extent.max(bc.get_max_extent());
		//if (allow_flatten) {return (generated.insert(loc).second ? 1 : 2);} // Note: caller no longer uses this value, so don't need to maintain generated
		return 1;
	}
	bool remove_tile(int x, int y) {
		auto it(tiles.find(make_pair(x, y)));
		if (it == tiles.end()) return 0; // not found
		//cout << "Remove building tile " << x << "," << y << ", tiles: " << tiles.size() << endl;
		it->second.clear_vbos(); // free VBOs/VAOs
		tiles.erase(it);
		return 1;
	}
	void clear_vbos() {
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {i->second.clear_vbos();}
	}
	void clear() {
		clear_vbos();
		tiles.clear();
	}
	bool check_sphere_coll(point &pos, point const &p_last, float radius, bool xy_only=0, vector3d *cnorm=nullptr, bool check_interior=0) const { // Note: pos is in camera space
		if (empty()) return 0;

		if (radius == 0.0) { // single point, use map lookup optimization (for example for grass)
			auto it(get_tile_by_pos_cs(pos));
			if (it == tiles.end()) return 0;
			return it->second.check_sphere_coll(pos, p_last, radius, xy_only, cnorm, check_interior);
		}
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			if (i->second.check_sphere_coll(pos, p_last, radius, xy_only, cnorm, check_interior)) return 1;
		}
		return 0;
	}
	void get_driveway_sphere_coll_cubes(point const &pos, float radius, bool xy_only, vect_cube_t &out) const {
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {i->second.get_driveway_sphere_coll_cubes(pos, radius, xy_only, out);}
	}
	bool get_building_hit_color(point const &p1, point const &p2, colorRGBA &color) const { // Note: p1/p2 are in building space
		if (empty()) return 0;

		if (p1.x == p2.x && p1.y == p2.y) { // vertical line, use map lookup optimization (for overhead map mode)
			auto it(get_tile_by_pos_bs(p1));
			if (it == tiles.end()) return 0;
			return it->second.get_building_hit_color(p1, p2, color);
		}
		cube_t const line_bcube(p1, p2);

		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			if (!i->second.get_bcube().intersects(line_bcube)) continue; // optimization
			if (i->second.get_building_hit_color(p1, p2, color)) return 1; // line is generally pointed down and can only intersect one building; return the first hit
		}
		return 0;
	}
	unsigned check_line_coll(point const &p1, point const &p2, float &t, bool ret_any_pt, bool no_coll_pt) const {
		if (empty()) return 0;
		cube_t const line_bcube(p1, p2);
		unsigned ret(0), hit_bix(0); // internal tile index, can't return

		for (auto i = tiles.begin(); i != tiles.end(); ++i) { // no vertical line test
			if (!i->second.get_bcube().intersects(line_bcube)) continue; // optimization
			unsigned const tile_ret(i->second.check_line_coll(p1, p2, t, hit_bix, ret_any_pt, no_coll_pt));
			if (tile_ret) {ret = tile_ret;}
		}
		return ret;
	}
	void update_zmax_for_line(point const &p1, point const &p2, float radius, float house_extra_zval, float &cur_zmax) const { // Note p1/p2 are in building space
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			if (!check_line_clip(p1, p2, i->second.get_bcube().d)) continue; // optimization
			i->second.update_zmax_for_line(p1, p2, radius, house_extra_zval, cur_zmax);
		}
	}
	void add_drawn(vector3d const &xlate, vector<building_creator_t *> &bcs) {
		float const draw_dist(get_draw_tile_dist());
		point const camera(get_camera_pos() - xlate);

		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			//if (!i->second.get_bcube().closest_dist_xy_less_than(camera, draw_dist)) continue; // distance test (conservative)
			if (!dist_xy_less_than(camera, i->second.get_bcube().get_cube_center(), draw_dist)) continue; // distance test (aggressive)
			if (i->second.is_visible(xlate)) {bcs.push_back(&i->second);}
		}
	}
	void add_interior_lights(vector3d const &xlate, cube_t &lights_bcube) {
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			cube_t const &bcube(i->second.get_bcube());
			if (!lights_bcube.intersects_xy(bcube)) continue; // not within light volume (too far from camera)
			if (!camera_pdu.cube_visible(bcube + xlate)) continue; // VFC
			i->second.add_interior_lights(xlate, lights_bcube);
		}
	}
	unsigned get_person_capacity() const {
		return 0; // Note: incomplete, and won't place any people anyway because there are no tiles created when ped_manager_t::init() is called
		unsigned cap(0);
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {cap += i->second.get_person_capacity();}
		return cap;
	}
	bool place_people(vect_building_place_t &locs, float radius, float speed_mult, unsigned num, unsigned start) {
		assert(locs.empty());
		unsigned tot_buildings(0);
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {tot_buildings += i->second.get_num_buildings();}
		if (tot_buildings == 0) return 0; // no tiles with buildings
		float const people_per_building(float(num)/float(tot_buildings)); // on average
		vect_building_place_t cur_locs;
		
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			unsigned const num_this_tile(round_fp(people_per_building*i->second.get_num_buildings()));
			if (num_this_tile == 0) continue; // no buildings, no people
			cur_locs.clear();
			i->second.place_people(cur_locs, radius, speed_mult, num_this_tile, (start + locs.size()));
			vector_add_to(cur_locs, locs);
		}
		cout << TXT(num) << TXT(locs.size()) << TXT(tiles.size()) << TXT(tot_buildings) << endl; // TESTING
		return 1;
	}
	void update_ai_state(vector<pedestrian_t> &people, float delta_dir) {
		// not yet implemented; see above code
		//for (auto i = tiles.begin(); i != tiles.end(); ++i) {i->second.update_ai_state(people, delta_dir);}
	}
	void get_occluders(pos_dir_up const &pdu, building_occlusion_state_t &state) const {
		auto it(get_tile_by_pos_cs(pdu.pos));
		if (it != tiles.end()) {it->second.get_occluders(pdu, state);}
	}
	bool check_pts_occluded(point const *const pts, unsigned npts, building_occlusion_state_t &state) const {
		auto it(get_tile_by_pos_cs(state.pos));
		return ((it == tiles.end()) ? 0 : it->second.check_pts_occluded(pts, npts, state));
	}
	void get_all_garages(vect_cube_t &garages) const {
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {i->second.get_all_garages(garages);}
	}
	unsigned get_tot_num_buildings() const {
		unsigned num(0);
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {num += i->second.get_num_buildings();}
		return num;
	}
	unsigned get_gpu_mem_usage() const {
		unsigned mem(0);
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {mem += i->second.get_gpu_mem_usage();}
		return mem;
	}
}; // end building_tiles_t


void occlusion_checker_noncity_t::set_camera(pos_dir_up const &pdu) {
	if ((display_mode & 0x08) == 0) {state.building_ids.clear(); return;} // occlusion culling disabled
	pos_dir_up near_pdu(pdu);
	near_pdu.far_ = 0.5f*(X_SCENE_SIZE + Y_SCENE_SIZE); // set far clipping plane to half a tile (currently 4.0)
	bc.get_occluders(near_pdu, state);
	//cout << "buildings: " << bc.get_num_buildings() << ", occluders: " << state.building_ids.size() << endl;
}
bool occlusion_checker_noncity_t::is_occluded(cube_t const &c) {
	if (state.building_ids.empty()) return 0;
	float const z(c.z2()); // top edge
	point const corners[4] = {point(c.x1(), c.y1(), z), point(c.x2(), c.y1(), z), point(c.x2(), c.y2(), z), point(c.x1(), c.y2(), z)};
	return bc.check_pts_occluded(corners, 4, state);
}


building_creator_t building_creator(0), building_creator_city(1);
building_tiles_t building_tiles;

int create_buildings_tile(int x, int y, bool allow_flatten) { // return value: 0=already exists, 1=newly generaged, 2=re-generated
	if (!global_building_params.gen_inf_buildings()) return 0;
	return building_tiles.create_tile(x, y, allow_flatten);
}
bool remove_buildings_tile(int x, int y) {
	if (!global_building_params.gen_inf_buildings()) return 0;
	return building_tiles.remove_tile(x, y);
}

vector3d get_tt_xlate_val() {return ((world_mode == WMODE_INF_TERRAIN) ? vector3d(xoff*DX_VAL, yoff*DY_VAL, 0.0) : zero_vector);}

void gen_buildings() {
	global_building_params.finalize();
	update_sun_and_moon(); // need to update light_factor from sun to know if we need to generate window light geometry

	if (world_mode == WMODE_INF_TERRAIN && have_cities()) {
		building_creator_city.gen(global_building_params, 1, 0, 0, 1); // city buildings
		global_building_params.restore_prev_pos_range(); // hack to undo clip to city bounds to allow buildings to extend further out
		if (global_building_params.add_secondary_buildings) {building_creator.gen(global_building_params, 0, 1, 0, 1);} // non-city secondary buildings
	} else {building_creator.gen (global_building_params, 0, 0, 0, 1);} // mixed buildings
}
void draw_buildings(int shadow_only, int reflection_pass, vector3d const &xlate) {
	//if (!building_tiles.empty()) {cout << "Building Tiles: " << building_tiles.size() << " Tiled Buildings: " << building_tiles.get_tot_num_buildings() << endl;} // debugging
	if (world_mode != WMODE_INF_TERRAIN) {building_tiles.clear();}
	vector<building_creator_t *> bcs;
	// don't draw city buildings for interior shadows
	bool const draw_city(world_mode == WMODE_INF_TERRAIN && (shadow_only != 2 || !interior_shadow_maps || global_building_params.add_city_interiors));
	bool const draw_sec ((shadow_only != 2 || interior_shadow_maps)); // don't draw secondary buildings for exterior dynamic shadows
	if (draw_city && building_creator_city.is_visible(xlate)) {bcs.push_back(&building_creator_city);}
	if (draw_sec  && building_creator     .is_visible(xlate)) {bcs.push_back(&building_creator     );}
	building_tiles.add_drawn(xlate, bcs);
	building_creator_t::multi_draw(shadow_only, reflection_pass, xlate, bcs);
}
void draw_building_lights(vector3d const &xlate) {
	building_creator_city.draw_building_lights(xlate);
	//building_creator.draw_building_lights(xlate); // only city buildings for now
}
bool proc_buildings_sphere_coll(point &pos, point const &p_int, float radius, bool xy_only, vector3d *cnorm, bool check_interior, bool exclude_city) { // pos is in camera space
	player_in_closet   = 0; // reset for this call
	player_is_hiding   = 0;
	player_in_elevator = 0;
	// we generally won't intersect more than one of these categories, so we can return true without checking all cases
	return ((!exclude_city && building_creator_city.check_sphere_coll(pos, p_int, radius, xy_only, cnorm, check_interior)) ||
		                           building_creator.check_sphere_coll(pos, p_int, radius, xy_only, cnorm, check_interior) ||
		                             building_tiles.check_sphere_coll(pos, p_int, radius, xy_only, cnorm, check_interior));
}
bool check_buildings_sphere_coll(point const &pos, float radius, bool apply_tt_xlate, bool xy_only, bool check_interior, bool exclude_city) {
	point center(pos);
	if (apply_tt_xlate) {center += get_tt_xlate_val();} // apply xlate for all static objects - not the camera
	return proc_buildings_sphere_coll(center, pos, radius, xy_only, nullptr, check_interior, exclude_city);
}
bool check_buildings_point_coll(point const &pos, bool apply_tt_xlate, bool xy_only, bool check_interior) {
	return check_buildings_sphere_coll(pos, 0.0, apply_tt_xlate, xy_only, check_interior);
}
bool check_buildings_no_grass(point const &pos) { // for tiled terrain mode; pos is in local space
	point center(pos + get_tt_xlate_val()); // convert from building space to camera space
	if (building_creator.check_sphere_coll(center, pos, 0.0, 1, nullptr)) return 1; // secondary buildings only
	if (building_tiles  .check_sphere_coll(center, pos, 0.0, 1, nullptr)) return 1;
	return 0;
}
void get_driveway_sphere_coll_cubes(point const &pos, float radius, bool xy_only, vect_cube_t &out) { // for tiled terrain mode; pos is in local space
	building_creator.get_driveway_sphere_coll_cubes(pos, radius, xy_only, out);
	building_tiles  .get_driveway_sphere_coll_cubes(pos, radius, xy_only, out);
}
unsigned check_buildings_line_coll(point const &p1, point const &p2, float &t, unsigned &hit_bix, bool ret_any_pt) { // for line_intersect_city(); p1/p2 are in camera space
	vector3d const xlate(get_camera_coord_space_xlate());
	point const p1x(p1 - xlate), p2x(p2 - xlate); // convert from camera to building space
	unsigned const coll1(building_creator_city.check_line_coll(p1x, p2x, t, hit_bix, ret_any_pt, 0));
	if (coll1 && ret_any_pt) return coll1;
	unsigned const coll2(building_creator.check_line_coll(p1x, p2x, t, hit_bix, ret_any_pt, 1));
	if (coll2 && ret_any_pt) return coll2;
	unsigned const coll3(building_tiles.check_line_coll(p1x, p2x, t, ret_any_pt, 1)); // Note: does't take/set hit_bix
	return (coll3 ? coll3 : (coll2 ? coll2 : coll1));
}
bool check_city_building_line_coll_bs(point const &p1, point const &p2, point &p_int) { // Note: p1/p2 are in building space
	float t(1.0);
	unsigned hit_bix(0); // unused
	if (!building_creator_city.check_line_coll(p1, p2, t, hit_bix, 0, 0)) return 0; // ret_any_pt=0
	p_int = p1 + t*(p2 - p1);
	return 1;
}
bool check_city_building_line_coll_bs_any(point const &p1, point const &p2) {
	float t(1.0); // unused
	unsigned hit_bix(0); // unused
	return building_creator_city.check_line_coll(p1, p2, t, hit_bix, 1, 1); // ret_any_pt=1, no_coll_pt=1
}
void update_buildings_zmax_for_line(point const &p1, point const &p2, float radius, float house_extra_zval, float &cur_zmax) {
	building_creator_city.update_zmax_for_line(p1, p2, radius, house_extra_zval, cur_zmax);
	building_creator     .update_zmax_for_line(p1, p2, radius, house_extra_zval, cur_zmax);
	building_tiles       .update_zmax_for_line(p1, p2, radius, house_extra_zval, cur_zmax);
}
bool get_buildings_line_hit_color(point const &p1, point const &p2, colorRGBA &color) { // Note: p1 and p2 are in camera space
	vector3d const xlate(get_camera_coord_space_xlate());
	point const p1x(p1 - xlate), p2x(p2 - xlate); // convert from camera to building space
	if (world_mode == WMODE_INF_TERRAIN && building_creator_city.get_building_hit_color(p1x, p2x, color)) return 1;
	if (building_tiles.get_building_hit_color(p1x, p2x, color)) return 1;
	return building_creator.get_building_hit_color(p1x, p2x, color);
}
bool have_city_buildings() {return !building_creator_city.empty();}
bool have_secondary_buildings() {return (global_building_params.add_secondary_buildings && global_building_params.num_place > 0);}
bool have_buildings() {return (!building_creator.empty() || !building_creator_city.empty() || !building_tiles.empty());} // for postproc effects
bool no_grass_under_buildings() {return (world_mode == WMODE_INF_TERRAIN && !(building_creator.empty() && building_tiles.empty()) && global_building_params.flatten_mesh);}
unsigned get_buildings_gpu_mem_usage() {return (building_creator.get_gpu_mem_usage() + building_creator_city.get_gpu_mem_usage() + building_tiles.get_gpu_mem_usage());}

vector3d get_buildings_max_extent() { // used for TT shadow bounds + map mode
	return building_creator.get_max_extent().max(building_creator_city.get_max_extent()).max(building_tiles.get_max_extent());
}
void clear_building_vbos() {
	building_creator.clear_vbos();
	building_creator_city.clear_vbos();
	building_tiles.clear_vbos();
}

// city interface
void set_buildings_pos_range(cube_t const &pos_range) {global_building_params.set_pos_range(pos_range);}
void get_building_bcubes(cube_t const &xy_range, vect_cube_t &bcubes) {building_creator_city.get_overlapping_bcubes(xy_range, bcubes);} // Note: no xlate applied
void get_building_power_points(cube_t const &xy_range, vector<point> &ppts) {building_creator_city.get_power_points(xy_range, ppts  );} // Note: no xlate applied
void add_house_driveways_for_plot(cube_t const &plot, vect_cube_t &driveways) {building_creator_city.add_house_driveways_for_plot(plot, driveways);} // Note: no xlate applied
void end_register_player_in_building();
float get_max_house_size() {return global_building_params.get_max_house_size();}

void add_building_interior_lights(point const &xlate, cube_t &lights_bcube) {
	//highres_timer_t timer("Add building interior lights"); // 0.97/0.37
	building_creator.add_interior_lights(xlate, lights_bcube);
	building_creator_city.add_interior_lights(xlate, lights_bcube);
	building_tiles.add_interior_lights(xlate, lights_bcube);
	end_register_player_in_building(); // required for AI player following
}
// cars + peds
void get_city_building_occluders(pos_dir_up const &pdu, building_occlusion_state_t &state) {building_creator_city.get_occluders(pdu, state);}
bool check_city_pts_occluded(point const *const pts, unsigned npts, building_occlusion_state_t &state) {return building_creator_city.check_pts_occluded(pts, npts, state);}
bool city_single_cube_visible_check(point const &pos, cube_t const &c) {return building_creator_city.single_cube_visible_check(pos, c);}
cube_t get_building_lights_bcube() {return building_lights_manager.get_lights_bcube();}
// used for pedestrians in cities
cube_t get_building_bcube(unsigned building_id) {return building_creator_city.get_building_bcube(building_id);}

bool get_building_door_pos_closest_to(unsigned building_id, point const &target_pos, point &door_pos) {
	return building_creator_city.get_building_door_pos_closest_to(building_id, target_pos, door_pos);
}
bool check_line_coll_building(point const &p1, point const &p2, unsigned building_id) {return building_creator_city.check_line_coll_building(p1, p2, building_id);}
int get_building_bcube_contains_pos(point const &pos) {return building_creator_city.get_building_bcube_contains_pos(pos);}
bool check_buildings_ped_coll(point const &pos, float radius, unsigned plot_id, unsigned &building_id) {return building_creator_city.check_ped_coll(pos, radius, plot_id, building_id);}
bool select_building_in_plot(unsigned plot_id, unsigned rand_val, unsigned &building_id) {return building_creator_city.select_building_in_plot(plot_id, rand_val, building_id);}

// used for people in buildings
cube_t get_sec_building_bcube(unsigned building_id) {return building_creator.get_building_bcube(building_id);} // unused
bool enable_building_people_ai() {return global_building_params.enable_people_ai;}

template<typename T> void place_people_in_buildings(T &bc, vect_building_place_t &locs, float radius, float speed_mult, unsigned tot_num, unsigned cap, unsigned tot_cap) {
	unsigned const num(uint64_t(tot_num)*cap/tot_cap);
	if (num == 0) return;
	vect_building_place_t cur_locs;
	bc.place_people(cur_locs, radius, speed_mult, num, locs.size());
	if (locs.empty()) {locs.swap(cur_locs);} else {vector_add_to(cur_locs, locs);}
}
bool place_building_people(vect_building_place_t &locs, float radius, float speed_mult, unsigned num) {
	unsigned const bt_cap(building_tiles.get_person_capacity()), bc_cap(building_creator.get_person_capacity()), bcc_cap(building_creator_city.get_person_capacity());
	unsigned const tot_cap(bt_cap + bc_cap + bcc_cap);
	if (tot_cap == 0) return 0; // can't place any people
	place_people_in_buildings(building_tiles,        locs, radius, speed_mult, num, bt_cap,  tot_cap);
	place_people_in_buildings(building_creator,      locs, radius, speed_mult, num, bc_cap,  tot_cap); // regular/secondary buildings
	place_people_in_buildings(building_creator_city, locs, radius, speed_mult, num, bcc_cap, tot_cap); // city buildings
	return 1;
}
void update_building_ai_state(vector<pedestrian_t> &people, float delta_dir) { // Note: each creator will manage its own range of people
	if (!global_building_params.enable_people_ai || !draw_building_interiors || !animate2) return;
	building_creator     .update_ai_state(people, delta_dir);
	building_creator_city.update_ai_state(people, delta_dir);
	building_tiles       .update_ai_state(people, delta_dir);
}

void get_all_garages(vect_cube_t &garages) {
	building_creator.get_all_garages(garages);
	building_creator_city.get_all_garages(garages); // doesn't have houses/garages yet, but leave it in in case they're added in the future
	building_tiles.get_all_garages(garages); // not sure if this should be included
}
void get_all_city_helipads(vect_cube_t &helipads) {building_creator_city.get_all_helipads(helipads);} // city only for now

bool is_pos_in_player_building(point const &pos) { // pos is in global space
	if (!camera_in_building || player_building == nullptr) return 0;
	//static vector<point> points; // reused across calls
	//return player_building->check_point_or_cylin_contained(pos, 0.0, points);
	return player_building->check_point_xy_in_part(pos); // don't need to draw if above the building either, since there are no skylights
}

