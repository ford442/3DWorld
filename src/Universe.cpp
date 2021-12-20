// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 9/4/02
#include "function_registry.h"
#include "universe.h"
#include "explosion.h"
#include "textures.h"
#include "shaders.h"
#include "gl_ext_arb.h"
#include "asteroid.h"


// temperatures
float const CGAS_TEMP        = 5.00;
float const MIN_LAND_TEMP    = 5.50;
float const MIN_COLONY_TEMP  = 6.00;
float const MIN_PLANT_TEMP   = 7.00;
float const MIN_LIVE_TEMP    = 9.00;
float const FREEZE_TEMP      = 12.0;
float const MAX_LIVE_TEMP    = 20.0;
float const MAX_PLANT_TEMP   = 25.0;
float const MAX_COLONY_TEMP  = 28.0;
float const MAX_LAND_TEMP    = 29.0;
float const BOIL_TEMP        = 30.0;
float const NO_AIR_TEMP      = 32.0;
float const NDIV_SIZE_SCALE  = 15.0;
float const NEBULA_PROB      = 0.7;

bool const SHOW_SPHERE_TIME  = 0; // debugging

unsigned const MAX_TRIES     = 100;
unsigned const SPHERE_MAX_ND = 256;
unsigned const RING_TEX_SZ   = 256;
unsigned const MIN_GALAXIES_PER_CELL   = 1;
unsigned const MAX_GALAXIES_PER_CELL   = 4;
unsigned const MIN_AST_FIELD_PER_GALAXY= 0;
unsigned const MAX_AST_FIELD_PER_GALAXY= 8;
unsigned const MAX_SYSTEMS_PER_GALAXY  = 500;
unsigned const MAX_PLANETS_PER_SYSTEM  = 16;
unsigned const MAX_MOONS_PER_PLANET    = 8;
unsigned const GAS_GIANT_TSIZE         = 1024;
unsigned const GAS_GIANT_BANDS         = 63;

int   const RAND_CONST       = 1;
float const ROTREV_TIMESCALE = 1.0;
float const ROT_RATE_CONST   = 0.5*ROTREV_TIMESCALE;
float const REV_RATE_CONST   = 1.0*ROTREV_TIMESCALE;
float const STAR_BRIGHTNESS  = 1.4;
float const MIN_TEX_OBJ_SZ   = 4.0;
float const MAX_WATER        = 0.75;
float const GLOBAL_AMBIENT   = 0.25;
float const GAS_GIANT_MIN_REL_SZ = 0.34;

unsigned const noise_tu_id = 11; // so as not to conflict with other ground mode textures when drawing plasma


bool have_sun(1);
unsigned star_cache_ix(0);
int uxyz[3] = {0, 0, 0};
unsigned char water_c[3] = {0}, ice_c[3] = {0};
unsigned char const *const wic[2] = {water_c, ice_c};
float univ_sun_rad(AVG_STAR_SIZE), univ_temp(0.0), cloud_time(0.0), universe_ambient_scale(1.0), planet_update_rate(1.0);
point univ_sun_pos(all_zeros);
colorRGBA sun_color(SUN_LT_C);
s_object current;
universe_t universe; // the top level universe
vector<uobject const *> show_info_uobjs;


extern bool enable_multisample, using_tess_shader, no_shift_universe;
extern int window_width, window_height, animate2, display_mode, onscreen_display, show_scores, iticks, frame_counter;
extern unsigned enabled_lights;
extern float fticks, system_max_orbit;
extern double tfticks;
extern point universe_origin;
extern colorRGBA bkg_color, sunlight_color;
extern exp_type_params et_params[];

int gen_rand_seed1(point const &center);
int gen_rand_seed2(point const &center);
bool is_shadowed(point const &pos, float radius, bool expand, ussystem const &sol, uobject const *&sobj);
unsigned get_texture_size(float psize);
bool get_gravity(s_object &result, point pos, vector3d &gravity, int offset);
void set_sun_loc_color(point const &pos, colorRGBA const &color, float radius, bool shadowed, bool no_ambient, float a_scale, float d_scale, shader_t *shader=NULL);
void set_light_galaxy_ambient_only(shader_t *shader=NULL);
void set_universe_ambient_color(colorRGBA const &color, shader_t *shader=NULL);
void set_universe_lighting_params(bool for_universe_draw);
bool universe_intersection_test(line_query_state &lqs, point const &pos, vector3d const &dir, float range, bool include_asteroids);



point get_scaled_upt() {return point(CELL_SIZE*uxyz[0], CELL_SIZE*uxyz[1], CELL_SIZE*uxyz[2]);}
void offset_pos    (point &pos) {pos += get_scaled_upt();}
void offset_pos_inv(point &pos) {pos -= get_scaled_upt();}

float temp_to_deg_c(float temp) {return (100.0f*(temp - FREEZE_TEMP)/(BOIL_TEMP - FREEZE_TEMP));} // 10 => 0, 20 => 100
float temp_to_deg_f(float temp) {return (1.8*temp_to_deg_c(temp) + 32.0);}

//float get_screen_width() {return (float)window_width;}
float get_screen_width() {return 1920.0;} // normalize to window_width=1920

float calc_sphere_size(point const &pos, point const &camera, float radius, float d_adj) {
	return get_screen_width()*radius/max(TOLERANCE, (p2p_dist(pos, camera) + d_adj)); // approx. in pixels
}

bool sphere_size_less_than(point const &pos, point const &camera, float radius, float num_pixels) {
	return (get_screen_width()*radius < num_pixels*p2p_dist(pos, camera));
}

float get_pixel_size(float radius, float dist) {
	return min(10000.0f, get_screen_width()*radius/max(dist, TOLERANCE)); // approx. in pixels
}

bool get_draw_as_line(float dist, vector3d const &vcp, float vcp_mag) {
	return (get_pixel_size(1.0, dist)*cross_product(get_player_velocity(), vcp).mag() > 1.0f*vcp_mag);
}

void set_star_light_atten(int light, float atten, shader_t *shader=NULL) {
	setup_gl_light_atten(light, STAR_CONST, atten*STAR_LINEAR_SCALED, atten*STAR_QUAD_SCALED, shader);
}


s_object get_shifted_sobj(s_object const &sobj) {

	s_object sobj2(sobj);
	UNROLL_3X(sobj2.cellxyz[i_] += uxyz[i_];)
	return sobj2;
}


// not very useful, but could be after planets and ships are scaled to do better/closer planet approaches
void setup_universe_fog(s_object const &closest) {

	if (closest.type != UTYPE_PLANET) return;
	assert(closest.object != NULL);
	float const atmos(closest.get_planet().atmos);
	if (atmos == 0.0) return;
	float const rel_dist((p2p_dist(get_player_pos(), closest.object->pos) - get_player_radius())/closest.object->radius);

	if (rel_dist < PLANET_ATM_RSCALE) {
		float const ascale(CLIP_TO_01((PLANET_ATM_RSCALE - rel_dist)/(PLANET_ATM_RSCALE - 1.0f)));
		colorRGBA color(0.8, 0.8, 0.8, 0.33*atmos*ascale);
		blend_color(color, color, closest.get_star().color, 0.7, 0);
		add_camera_filter(color, 4, -1, CAM_FILT_FOG);
	}
}


// Note: not threadsafe
uobject *line_intersect_universe(point const &start, vector3d const &dir, float length, float line_radius, float &dist) {

	point coll;
	s_object target;
	static line_query_state lqs;

	if (universe.get_trajectory_collisions(lqs, target, coll, dir, start, length, line_radius)) { // destroy, query, beams
		if (target.is_solid()) {
			dist = p2p_dist(start, coll);
			if (target.type == UTYPE_ASTEROID) {return &target.get_asteroid();}
			if (target.object != NULL) {return static_cast<uobject *>(target.object);}
		}
	}
	dist = 0.0;
	return NULL;
}


void destroy_sobj(s_object const &target) {

	//point coll_pos; get_point_of_collision(target, ship_pos, coll_pos);
	int const etype(ETYPE_ANIM_FIRE);

	switch (target.type) {
	case UTYPE_STAR:
		target.get_star().def_explode(u_exp_size[target.type], etype, signed_rand_vector());
		target.get_galaxy().calc_color(); // recompute color (what about when galaxies overlap?)
		break;
	case UTYPE_PLANET:
		target.get_planet().def_explode(u_exp_size[target.type], etype, signed_rand_vector());
		break;
	case UTYPE_MOON:
		target.get_moon().def_explode(u_exp_size[target.type], etype, signed_rand_vector());
		break;
	case UTYPE_ASTEROID:
		target.destroy_asteroid();
		break;
	}
	target.register_destroyed_sobj();
}


// *** DRAW CODE ***


struct planet_draw_data_t {
	unsigned ix;
	float size;
	bool selected;
	upos_point_type pos; // pos cached before planet update
	shadow_vars_t svars;
	planet_draw_data_t() : ix(0), size(0.0), selected(0) {}
	planet_draw_data_t(unsigned ix_, float size_, upos_point_type const &pos_, shadow_vars_t const &svars_, bool sel) :
		ix(ix_), size(size_), selected(sel), pos(pos_), svars(svars_) {}
};


float get_light_scale(unsigned light) {return (is_light_enabled(light) ? 1.0 : 0.0);}

void set_light_scale(shader_t &s, bool use_light2) {

	vector3d const light_scale(get_light_scale(0), get_light_scale(1), (use_light2 ? 1.0 : 0.0));
	s.add_uniform_vector3d("light_scale", light_scale);
}


class universe_shader_t : public shader_t {

	typedef map<const char *, int> name_loc_map; // key is a pointer? use a string?
	name_loc_map name_to_loc;
	shadowed_uobject shadowed_state;

	int get_loc(char const *const name) {
#if 1
		return get_uniform_loc(name);
#else
		name_loc_map::const_iterator it(name_to_loc.find(name));
		if (it != name_to_loc.end()) {return it->second;}
		int const loc(get_uniform_loc(name));
		name_to_loc[name] = loc;
		return loc;
#endif
	}

	void shared_setup(const char *texture_name="tex0") {
		begin_shader();
		add_uniform_int(texture_name, 0);
	}
	void setup_planet_star_noise_tex() {
		set_3d_texture_as_current(get_noise_tex_3d(64, 1), noise_tu_id); // grayscale noise
	}
	void set_planet_uniforms(float atmosphere, shadow_vars_t const &svars, bool use_light2) {
		set_light_scale(*this, use_light2);
		set_uniform_float(get_loc("atmosphere"), atmosphere);
		set_uniform_vector3d(get_loc("sun_pos"), svars.sun_pos);
		set_uniform_float(get_loc("sun_radius"), svars.sun_radius);
		set_uniform_vector3d(get_loc("ss_pos"),  svars.ss_pos);
		set_uniform_float(get_loc("ss_radius"),  svars.ss_radius);
		upload_mvm_to_shader(*this, "fg_ViewMatrix");
	}

public:
	void enable_planet(urev_body const &body, shadow_vars_t const &svars, bool use_light2) {
		if (!is_setup()) {
			bool proc_detail_vs(0);

			if (body.gas_giant) {
				set_prefix("#define GAS_GIANT",    1); // FS
				set_prefix("#define RIDGED_NOISE", 1); // FS
			}
			else if (body.use_procedural_shader()) {
				set_prefix("#define PROCEDURAL_DETAIL", 1); // FS
				set_prefix("uniform sampler3D cloud_noise_tex;", 0); // VS
				if (body.water >= 1.0) {set_prefix("#define ALL_WATER_ICE", 1);} // FS
				else {set_prefix("#define DOMAIN_WARP", 1);} // FS - add domain warping for procedural planets with land (slower but looks better)

				if (body.use_vert_shader_offset()) {
					proc_detail_vs = 1;
#if 1 // tessellation shaders
					set_prefix("uniform sampler3D cloud_noise_tex;", 4); // TES
					set_prefix("#define NO_GL_COLOR", 1); // FS - needed on some drivers because TC/TE don't have fg_Color_vf
					set_tess_control_shader("planet_draw");
					set_tess_eval_shader("procedural_planet.part*+planet_draw"); // draw calls need to use GL_PATCHES instead of GL_TRIANGLES
					glPatchParameteri(GL_PATCH_VERTICES, 3); // triangles; max is 32
					// GL_PATCH_DEFAULT_OUTER_LEVEL GL_PATCH_DEFAULT_INNER_LEVEL
#endif
				}
			}
			bool const has_craters(body.has_craters());
			set_prefix("#define NUM_OCTAVES 8", 1); // FS
			if (has_craters) {set_prefix("#define HAS_CRATERS", 1);} // FS
			set_prefix(make_shader_bool_prefix("has_rings", (svars.ring_ro > 0.0)), 1); // FS
			string frag_shader_str("ads_lighting.part*+perlin_clouds_3d.part*+sphere_shadow.part*+rand_gen.part*");
			if (has_craters) {frag_shader_str += "+craters.part";}
			string vert_shader_str(proc_detail_vs ? "procedural_planet.part*+planet_draw_procedural" : "planet_draw");
			if (has_tess_shader()) {vert_shader_str += "_tess";}
			set_vert_shader(vert_shader_str);
			set_frag_shader(frag_shader_str+"+procedural_planet.part*+planet_draw");
			shared_setup();
			add_uniform_int("cloud_noise_tex", noise_tu_id);
			add_uniform_int("ring_tex",        2);
			add_uniform_float("time", 5.0E-6*cloud_time);
		}
		using_tess_shader = has_tess_shader();
		enable();
		set_specular(1.0, 80.0);
		set_uniform_vector3d(get_loc("rscale"),   svars.rscale);
		set_uniform_float(get_loc("obj_radius"),  body.radius);
		set_uniform_float(get_loc("ring_ri"),     svars.ring_ri);
		set_uniform_float(get_loc("ring_ro"),     svars.ring_ro);
		set_uniform_float(get_loc("noise_scale"), 4.0*body.cloud_scale); // clouds / gas giant noise
		set_uniform_float(get_loc("population"),  ((body.population >= 10.0) ? 1.0 : 0.0));
		set_uniform_color(get_loc("rim_color"),   (colorRGB)body.get_rim_color());
		if (using_tess_shader) {set_uniform_vector3d(get_loc("camera_pos"), make_pt_global(get_player_pos()));}
		
		if (!body.gas_giant) { // else rseed_val=body.colorA.R?
			set_uniform_float(get_loc("water_val"), body.water);
			set_uniform_float(get_loc("lava_val"),  body.lava);
			body.upload_colors_to_shader(*this);
		}
		float const cf_scale((world_mode == WMODE_UNIVERSE) ? 1.0 : UNIV_NCLIP_SCALE);
		// increase cloud frequency in z to add a coriolis effect
		set_uniform_vector3d(get_loc("cloud_freq"), cf_scale*(body.gas_giant ? vector3d(1.2, 1.2, 6.0) : vector3d(1.0, 1.0, 2.0)));
		set_planet_uniforms(body.cloud_density*body.atmos, svars, use_light2);
		setup_planet_star_noise_tex();
	}
	void disable_planet() {
		using_tess_shader = 0; // may or may not have been set to 1
		if (is_setup()) {clear_specular(); disable();}
	}

	void enable_star(colorRGBA const &colorA, colorRGBA const &colorB, float radius) { // no lighting
		if (!is_setup()) {
			set_prefix("#define NUM_OCTAVES 8", 1); // FS
			set_vert_shader("star_draw");
			set_frag_shader("perlin_clouds_3d.part*+star_draw");
			//set_frag_shader("star_draw_iq");
			begin_shader();
			add_uniform_int("tex0", 0);
			add_uniform_int("cloud_noise_tex", noise_tu_id);
			add_uniform_float("time", 1.2E-4*cloud_time);
			add_uniform_float("noise_scale", 3.5);
		}
		enable();
		set_uniform_color(get_loc("colorA"), colorA);
		set_uniform_color(get_loc("colorB"), colorB);
		set_uniform_float(get_loc("radius"), radius);
		setup_planet_star_noise_tex();
	}
	void disable_star() {
		if (is_setup()) {disable();}
	}

	void enable_ring(uplanet const &planet, point const &planet_pos, float back_face_mult, bool a2c) {	
		if (!is_setup()) {
			set_vert_shader("planet_draw");
			set_frag_shader("ads_lighting.part*+sphere_shadow.part*+sphere_shadow_casters.part+planet_rings");
			set_prefix("#define ENABLE_SHADOWS", 1); // FS
			shared_setup("ring_tex");
			add_uniform_int("noise_tex",     1);
			add_uniform_int("particles_tex", 2);
			add_uniform_float("obj_radius", 1.0); // so that vertex scaling within the planet shader does nothing
		}
		select_multitex(NOISE_GEN_MIPMAP_TEX, 1, 0);
		select_multitex(SPARSE_NOISE_TEX,     2, 1);
		enable();
		set_specular(0.5, 50.0);
		shadowed_state.calc_shadowers_for_planet(planet);
		shadowed_state.upload_shadow_casters(*this);
		set_uniform_vector3d(get_loc("planet_pos"), planet_pos);
		set_uniform_float(get_loc("planet_radius"), planet.radius);
		set_uniform_float(get_loc("ring_ri"),       planet.ring_ri);
		set_uniform_float(get_loc("ring_ro"),       planet.ring_ro);
		set_uniform_float(get_loc("bf_draw_sign"),  back_face_mult);
		set_uniform_float(get_loc("alpha_scale"),   (a2c ?  1.0 : 0.6));
		set_uniform_vector3d(get_loc("camera_pos"), make_pt_global(get_player_pos()));
	}
	void disable_ring() {
		if (is_setup()) {clear_specular(); disable();}
	}

	void enable_atmospheric(uplanet const &planet, point const &planet_pos, shadow_vars_t const &svars) {	
		if (!is_setup()) {
			set_vert_shader("atmosphere");
			set_frag_shader("ads_lighting.part*+sphere_shadow.part*+atmosphere");
			begin_shader();
		}
		enable();
		bool const uses_hmap(!planet.gas_giant && !planet.use_procedural_shader());
		set_uniform_vector3d(get_loc("camera_pos"), make_pt_global(get_player_pos()));
		set_uniform_vector3d(get_loc("planet_pos"), planet_pos);
		set_uniform_vector3d(get_loc("atmos_density"), planet.atmos*vector3d(0.0, (uses_hmap ? 1.8 : 3.2), 0.0)); // linear atmospheric density
		set_uniform_float(get_loc("planet_radius"), planet.radius*(uses_hmap ? (1.0 - 0.25*planet.get_hmap_scale()) : 1.0)); // average between ravg and rmin
		set_uniform_float(get_loc("atmos_radius" ), planet.radius*PLANET_ATM_RSCALE);
		set_uniform_color(get_loc("inner_color"  ), (colorRGB)planet.ai_color);
		set_uniform_color(get_loc("outer_color"  ), (colorRGB)planet.ao_color);
		set_planet_uniforms(planet.atmos, svars, 0); // atmosphere has no planet reflection light (light2)
	}
	void disable_atmospheric() {
		if (is_setup()) {disable();}
	}
};


class star_shader_t {
	universe_shader_t star_shader;
public:
	void enable_star_shader(colorRGBA const &colorA, colorRGBA const &colorB, float radius, float intensity=1.0) {
		star_shader.enable_star(colorA, colorB, radius);
		set_star_intensity(intensity);
	}
	void set_star_intensity(float intensity) {star_shader.add_uniform_float("intensity", intensity);}
	void disable_star_shader() {star_shader.disable_star();}
};

class ushader_group : public star_shader_t {

	universe_shader_t planet_shader[2][5][2]; // {without/with rings}x{rocky, procedural, all water, gas giant, proc + vert hmap}x{without/with craters} - not all variations used
	universe_shader_t ring_shader, cloud_shader, atmospheric_shader;
	shader_t color_only_shader, planet_colored_shader, point_sprite_shader;

	universe_shader_t &get_planet_shader(urev_body const &body, shadow_vars_t const &svars) {
		unsigned const type(body.gas_giant ? 3 : (body.use_procedural_shader() ? ((body.water >= 1.0) ? 2 : (body.use_vert_shader_offset() ? 4 : 1)) : 0));
		return planet_shader[svars.ring_ro > 0.0][type][body.has_craters()];
	}
public:
	vpc_shader_t nebula_shader;
	shader_t asteroid_shader;
	vector<planet_draw_data_t> atmos_to_draw, rings_to_draw;
	icosphere_manager_t *planet_manager;

	ushader_group(icosphere_manager_t *pm=nullptr) : planet_manager(pm) {}
	void enable_planet_shader(urev_body const &body, shadow_vars_t const &svars, bool use_light2) {
		get_planet_shader(body, svars).enable_planet(body, svars, use_light2);
	}
	void disable_planet_shader(urev_body const &body, shadow_vars_t const &svars) {get_planet_shader(body, svars).disable_planet();}
	
	void enable_ring_shader(uplanet const &planet, point const &planet_pos, float back_face_mult, bool a2c) {
		ring_shader.enable_ring(planet, planet_pos, back_face_mult, a2c);
	}
	void disable_ring_shader() {ring_shader.disable_ring();}
	void enable_atmospheric_shader(uplanet const &planet, point const &planet_pos, shadow_vars_t const &svars) {
		atmospheric_shader.enable_atmospheric(planet, planet_pos, svars);
	}
	void disable_atmospheric_shader() {atmospheric_shader.disable_atmospheric();}
	
	void enable_color_only_shader(colorRGBA const *const color=nullptr) {
		if (!color_only_shader.is_setup()) {color_only_shader.begin_color_only_shader();}
		else {color_only_shader.enable();}
		if (color) {color_only_shader.set_cur_color(*color);}
	}
	void disable_color_only_shader() {color_only_shader.disable();}

	void planet_colored_shader_setup(shader_t &s, bool use_light2) { // Note: use_light2 is ignored in the shader
		s.set_vert_shader("per_pixel_lighting");
		s.set_frag_shader("ads_lighting.part*+planet_draw_distant");
		s.begin_shader();
		set_light_scale(s, use_light2);
	}
	void enable_planet_colored_shader(bool use_light2, colorRGBA const &color) { // Note: use_light2 is ignored in the shader
		if (!planet_colored_shader.is_setup()) {planet_colored_shader_setup(planet_colored_shader, use_light2);}
		else {planet_colored_shader.enable();}
		planet_colored_shader.set_cur_color(color);
	}
	void disable_planet_colored_shader() {
		planet_colored_shader.disable();
	}

	void enable_point_sprite_shader(float point_size) { // for planets
		if (!point_sprite_shader.is_setup()) {
			point_sprite_shader.set_prefix("#define POINT_SPRITE_MODE", 0); // VS
			planet_colored_shader_setup(point_sprite_shader, 0);
		}
		else {point_sprite_shader.enable();}
		assert(point_size > 0.0);
		point_sprite_shader.add_uniform_float("point_size_pixels", point_size);
		set_point_sprite_mode(1); // enable
	}
	void disable_point_sprite_shader() {
		point_sprite_shader.disable();
		set_point_sprite_mode(0); // disable
	}
};


// not meant to be used in universe mode; expected to be called while in blend mode
void draw_one_star(colorRGBA const &colorA, colorRGBA const &colorB, point const &pos, float radius, int ndiv, bool add_halo) {

	static int lfc(0);
	if (animate2 && frame_counter > lfc) {cloud_time += 8.0f*fticks; lfc = frame_counter;} // at most once per frame
	star_shader_t ss;
	ss.enable_star_shader(colorA, colorB, radius);
	set_additive_blend_mode();
	fgPushMatrix();
	translate_to(pos);
	uniform_scale(radius);
	draw_sphere_vbo_raw(ndiv, 0);

	if (add_halo) {
		static quad_batch_draw qbd;
		qbd.add_xlated_billboard(pos, all_zeros, get_camera_pos(), up_vector, WHITE, 4.0, 4.0); // halo
		qbd.draw_as_flares_and_clear(BLUR_TEX);
	}
	fgPopMatrix();
	set_std_blend_mode();
	ss.disable_star_shader();
}


void invalidate_cached_stars() {++star_cache_ix;}


// no_distant: 0: draw everything, 1: draw current cell only, 2: draw current system only
void universe_t::draw_all_cells(s_object const &clobj, bool skip_closest, bool no_move, int no_distant, bool gen_only, bool no_asteroid_dust) {

	//RESET_TIME;
	if (animate2) {cloud_time += fticks;}
	unpack_color(water_c, P_WATER_C); // recalculate every time
	unpack_color(ice_c,   P_ICE_C  );
	std::unique_ptr<ushader_group> usg_ptr(new ushader_group(&planet_manager)); // allocate on the heap since this object is large
	ushader_group &usg(*usg_ptr);

	if (no_distant < 2 || clobj.type < UTYPE_SYSTEM) { // drawing pass 0
		// gather together the set of cells that are visible and need to be drawn
		vector<cell_ixs_t> to_draw;
		cell_ixs_t cix;
		int cur_cix(-1);

		for (cix.ix[2] = 0; cix.ix[2] < int(U_BLOCKS); ++cix.ix[2]) { // z
			for (cix.ix[1] = 0; cix.ix[1] < int(U_BLOCKS); ++cix.ix[1]) { // y
				for (cix.ix[0] = 0; cix.ix[0] < int(U_BLOCKS); ++cix.ix[0]) { // x
					bool const sel_cell(clobj.cellxyz[0] == cix.ix[0] && clobj.cellxyz[1] == cix.ix[1] && clobj.cellxyz[2] == cix.ix[2]);
					if (no_distant > 0 && !sel_cell) continue;
					if (!get_cell(cix.ix).is_visible()) continue;
					if (sel_cell) {cur_cix = to_draw.size();}
					to_draw.push_back(cix);
				}
			}
		}

		// first pass - create systems and draw distant stars
		for (vector<cell_ixs_t>::const_iterator i = to_draw.begin(); i != to_draw.end(); ++i) {
			UNROLL_3X(current.cellxyz[i_] = i->ix[i_] + uxyz[i_];)
			bool const sel_cell(cur_cix == (i - to_draw.begin()));
			get_cell(i->ix).draw_systems(usg, clobj, 0, no_move, skip_closest, sel_cell, gen_only, no_asteroid_dust); // and asteroids
		}
		if (!gen_only) {
			for (vector<cell_ixs_t>::const_iterator i = to_draw.begin(); i != to_draw.end(); ++i) {
				get_cell(i->ix).draw_nebulas(usg); // draw nebulas
			}
		}
	}
	if (clobj.has_valid_system()) { // in a system
		for (unsigned pass = 1; pass < 3; ++pass) { // drawing passes 1-2
			UNROLL_3X(current.cellxyz[i_] = clobj.cellxyz[i_] + uxyz[i_];)
			ucell &cell(get_cell(clobj.cellxyz));
			if (cell.is_visible()) {cell.draw_systems(usg, clobj, pass, no_move, skip_closest, 1, gen_only, no_asteroid_dust);} // and asteroids
		}
	}
	//PRINT_TIME("Draw Cells");
	//exit(0);
}


void set_current_system_light(s_object const &clobj, point const &pspos, float a_scale, float d_scale) { // called in ground mode

	if (clobj.has_valid_system() && clobj.get_star().is_ok()) { // in a system
		ustar const &sun(clobj.get_star());
		point const pos(clobj.get_ucell().rel_center + sun.pos);
		sunlight_color = sun.get_light_color();
		set_sun_loc_color(pos, sunlight_color, sun.radius, 0, 0, a_scale, d_scale); // ending light from current system
		univ_sun_pos = pos;
		univ_sun_rad = sun.radius;
		univ_temp    = sun.get_temperature_at_pt(pspos);
		have_sun     = 1;

		if (clobj.type == UTYPE_PLANET) { // planet (what about moon?)
			// calculate temperature based on distance of pspos to the poles
			uplanet const &planet(clobj.get_planet());
			vector3d const dir(pspos - planet.pos);
			float const pole_align(fabs(dot_product(planet.rot_axis, dir)/dir.mag()));
			// +/- 2 depending on distance to pole (should this be a percentage instead?)
			univ_temp += 2.0 - 4.0*pole_align;
			univ_temp  = max(0.0f, univ_temp);
		}
	}
	else {
		//set_light_galaxy_ambient_only();
		set_universe_lighting_params(0); // ending light default (for for universe draw)
		univ_sun_pos = point(0.0, 0.0, 1.0);
		univ_sun_rad = AVG_STAR_SIZE;
		univ_temp    = 0.0;
		sun_color    = sunlight_color;
		have_sun     = 0;
	}
	univ_temp = temp_to_deg_c(univ_temp);
}


// similar to draw_universe_sun_flare(), maybe we can use that for the player?
bool is_shadowed(point const &pos, float radius, bool expand, ussystem const &sol, uobject const *&sobj) {

	float const exp(expand ? radius : 0.0);
	vector3d const dir((sol.sun.pos - pos).get_norm());
	point const pos2(pos + dir*(1.1f*(radius + exp))); // extend beyond the object radius so that we don't get a collision with it
	point const spos(sol.sun.pos - dir*(1.1*sol.sun.radius));

	for (unsigned k = 0; k < sol.planets.size(); ++k) {
		uplanet const &planet(sol.planets[k]);
		
		if (line_sphere_int_cont(pos2, spos, planet.pos, (planet.radius + exp))) {
			sobj = &planet;
			return 1; // shadowed by planet
		}
		if (planet.moons.empty()) continue;
		if (!line_sphere_int_cont(pos2, spos, planet.pos, (planet.mosize + exp))) continue; // not shadowed by planet or its moons

		for (unsigned l = 0; l < planet.moons.size(); ++l) {
			if (line_sphere_int_cont(pos2, spos, planet.moons[l].pos, (planet.moons[l].radius + exp))) {
				sobj = &planet.moons[l];
				return 1; // shadowed by moon
			}
		}
	}
	return 0;
}


void atten_color(colorRGBA &color, point const &p1, point const &p2, float radius, float expand) {

	assert(radius > 0.0 && expand > 0.0 && expand != 1.0);
	float const dist(p2p_dist(p1, p2));
	assert(isfinite(dist) && dist > 0.0);
	//assert(dist >= radius && dist <= expand*radius); // might be too strong (failed after running for 10 hours)
	float const atten(CLIP_TO_01((expand - dist/radius)/(expand - 1.0f)));
	color *= atten;
}


bool get_universe_sun_pos(point const &pos, point &spos) {

	s_object result;

	if (universe.get_close_system(pos, result, 1.0)) {
		ustar const &star(result.get_star());
		
		if (star.is_ok()) {
			spos = star.pos;
			return 1;
		}
	}
	return 0;
}


bool has_sun_lighting(point const &pos) {
	s_object result;
	return (universe.get_close_system(pos, result, 2.0) != 0);
}

float get_univ_ambient_scale() {return universe_ambient_scale*GLOBAL_AMBIENT*ATTEN_AMB_VAL*OM_WCA;}


// return values: -1: no sun, 0: not shadowed, 1: < half shadowed, 2: > half shadowed, 3: fully shadowed
// caches sobj and only determines if there is a shadow if NULL
int set_uobj_color(point const &pos, float radius, bool known_shadowed, int shadow_thresh, point *sun_pos, colorRGBA *sun_color,
				   uobject const *&sobj, float ambient_scale_s, float ambient_scale_no_s, shader_t *shader, bool no_shadow_check) // based on current star and current galaxy
{
	assert(!is_nan(pos));
	assert(radius < CELL_SIZE);
	float const expand(2.0);
	s_object result;
	bool const found(universe.get_close_system(pos, result, 1.0) != 0);
	bool const blend(!found && universe.get_close_system(pos, result, expand));
	ussystem *sol(NULL);
	point pos2(pos);
	offset_pos(pos2);
	float ambient_scale(ambient_scale_no_s);
	bool ambient_color_set(0);

	if (result.galaxy >= 0) { // close to a galaxy
		pos2 -= result.get_ucell().pos;
		assert(!is_nan(pos));
		colorRGBA color;

		if (result.system >= 0) { // close to a solar system
			sol   = &result.get_system();
			color = sol->get_galaxy_color(); // non-const
			ambient_scale = ambient_scale_s;
		}
		else {
			ugalaxy const &galaxy(result.get_galaxy()); // galaxies shouldn't overlap
			color = galaxy.color;
			if (result.cluster >= 0) {color = (color + result.get_cluster().color)*0.5;} // average galaxy and cluster colors
			//atten_color(color, pos2, galaxy.pos, (galaxy.radius + MAX_SYSTEM_EXTENT), expand);
		}
		if (ambient_scale > 0.0) {
			set_universe_ambient_color(color*ambient_scale, shader);
			ambient_color_set = 1;
		}
	}
	if (!ambient_color_set) {set_light_a_color(get_universe_ambient_light(1), BLACK, shader);} // light is not enabled or disabled
	
	if (!found && !blend) { // no sun
		set_light_galaxy_ambient_only(shader);
		return -1;
	}
	assert(sol);
	int shadow_val(0);
	ustar const &sun(sol->sun);

	if (!sun.is_ok()) { // no sun
		set_light_galaxy_ambient_only(shader);
		return -1;
	}
	point const spos(result.get_ucell().rel_center + sun.pos);
	if (sun_pos) {*sun_pos = sun.pos;} // spos?
	colorRGBA color(sun.get_light_color());
	if (blend) {atten_color(color, pos2, sun.pos, (sol->radius + MAX_PLANET_EXTENT), expand);}
	if (sun_color) {*sun_color = blend_color(WHITE, color, (WHITE_COMP_D + get_univ_ambient_scale()), 0);}
	
	if (!no_shadow_check && sun.is_ok() && (sobj != NULL || is_shadowed(pos2, radius, 1, *sol, sobj))) { // check for planet/moon shadowing
		++shadow_val;
		assert(sobj != NULL);
		float const sr(sobj->get_radius());

		if (line_sphere_int_cont(pos2, sun.pos, sobj->get_pos(), sr)) {
			++shadow_val;
			if (sr > radius && line_sphere_int_cont(pos2, sun.pos, sobj->get_pos(), (sr - radius))) ++shadow_val;
		}
	}
	set_sun_loc_color(spos, color, sun.radius, (known_shadowed || shadow_val > shadow_thresh), (ambient_scale == 0.0), BASE_AMBIENT, BASE_DIFFUSE, shader); // check for sun
	return shadow_val;
}


void ucell::draw_nebulas(ushader_group &usg) const {

	point const &camera(get_player_pos());

	for (unsigned i = 0; i < galaxies->size(); ++i) { // back-to-front sort not needed?
		if ((*galaxies)[i].nebula.is_valid()) {(*galaxies)[i].nebula.draw(rel_center, camera, U_VIEW_DIST, usg.nebula_shader);}
	}
}


bool ucell::is_visible() const {

	if (galaxies == nullptr) return 0; // galaxies not yet allocated
	if (!univ_sphere_vis(rel_center, CELL_SPHERE_RAD)) return 0;
	cube_t bcube(rel_center, rel_center);
	bcube.expand_by(CELL_SPHERE_RAD);
	if (!player_pdu.cube_visible(bcube)) return 0;

	for (unsigned i = 0; i < galaxies->size(); ++i) { // remember, galaxies can overlap
		if (univ_sphere_vis((rel_center + (*galaxies)[i].pos), (*galaxies)[i].radius)) return 1; // conservative, since galaxies are not spherical
	}
	return 0;
}


void ucell::draw_all_stars(ushader_group &usg, bool clear_star_pld) {

	enable_blend();
	set_additive_blend_mode();
	set_multisample(0);

	if (!star_pld.empty()) {
		usg.enable_color_only_shader();
		if (clear_star_pld) {star_pld.draw_and_clear();} else {star_pld.draw_vbo();}
		usg.disable_color_only_shader();
	}
	star_psd.draw_and_clear(WHITE_TEX, 1.0); // unblended
	set_multisample(1);
	set_std_blend_mode();
	disable_blend();
}


void find_best_moon_shadower(uplanet const &planet, point_d const &targ_pos, float targ_radius, point_d const &pos_offset, point_d const &sun_pos, point &ss_pos, float &ss_radius) {

	float max_overlap(0.0);
	point const tpos(targ_pos + pos_offset);

	// Note: if more than one moon shadows the planet/moon, it will be incorrect, but that case is very rare
	for (unsigned l = 0; l < planet.moons.size(); ++l) {
		umoon const &moon(planet.moons[l]);
		if (moon.pos == targ_pos) continue; // skip self
		point_d const mpos(moon.pos + pos_offset);

		if (p2p_dist_sq(mpos, sun_pos) < p2p_dist_sq(targ_pos, sun_pos)) { // moon closer to sun than planet
			float const overlap((targ_radius + moon.radius) - pt_line_dist(mpos, sun_pos, targ_pos));

			if (overlap > max_overlap) {
				max_overlap = overlap;
				ss_pos      = make_pt_global(mpos);
				ss_radius   = moon.radius;
			}
		}
	}
}


// pass:
//  0. Draw all except for the player's system
//  If player's system is none (update: galaxy is none) then stop
//  1. Draw the player's system except for the player's sobj
//  2. Draw the player's sobj
void ucell::draw_systems(ushader_group &usg, s_object const &clobj, unsigned pass, bool no_move, bool skip_closest, bool sel_cell, bool gen_only, bool no_asteroid_dust) {

	point const &camera(get_player_pos());
	vector3d const vcp(camera, rel_center);
	float const vcp_mag(vcp.mag()), dist(vcp_mag - CELL_SPHERE_RAD);
	bool const cache_stars(pass == 0 && !sel_cell && !gen_only && !get_draw_as_line(dist, vcp, vcp_mag) && dist_less_than(camera, last_player_pos, STAR_MIN_SIZE) &&
		bkg_color == last_bkg_color && star_cache_ix == last_star_cache_ix);

	if (cache_stars && cached_stars_valid) { // should be true in combined_gu mode
		draw_all_stars(usg, 0); // star_psd should be empty
		return; // that's it
	}
	last_bkg_color     = bkg_color;
	last_player_pos    = camera;
	last_star_cache_ix = star_cache_ix;
	cached_stars_valid = 0;
	star_pld.clear();
	point_d const pos(rel_center);

	if (cache_stars) {
		for (unsigned i = 0; i < galaxies->size(); ++i) {
			ugalaxy &galaxy((*galaxies)[i]);
			if (calc_sphere_size((pos + galaxy.pos), camera, STAR_MAX_SIZE, -galaxy.radius) < 0.18) continue; // too far away
			current.galaxy = i;
			galaxy.process(*this); // is this necessary?

			for (unsigned s = 0; s < galaxy.sols.size(); ++s) {
				ussystem &sol(galaxy.sols[s]);
				point_d const spos(pos + sol.pos);

				if (calc_sphere_size(spos, camera, sol.sun.radius) > 0.1 && sol.sun.is_ok()) {
					sol.sun.draw(spos, usg, star_pld, star_psd, 1, 0);
				}
			}
		} // galaxy i
		draw_all_stars(usg, 0);
		cached_stars_valid = 1;
		return;
	}
	bool const p_system(clobj.has_valid_system());
	// use lower detail when the player is moving quickly in hyperspeed since objects zoom by so quickly
	float const velocity_mag(get_player_velocity().mag()), sscale_val(1.0/max(1.0f, 2.0f*velocity_mag)); // up to 5x lower

	// draw galaxies
	for (unsigned i = 0; i < galaxies->size(); ++i) { // remember, galaxies can overlap
		bool const sel_g(sel_cell && (int)i == clobj.galaxy), sclip(!sel_g || (display_mode & 0x01) != 0);
		if (p_system && pass > 0 && !sel_g) continue; // drawn in previous pass
		ugalaxy &galaxy((*galaxies)[i]);
		point_d const gpos(pos + galaxy.pos);
		if (calc_sphere_size(gpos, camera, STAR_MAX_SIZE, -galaxy.radius) < 0.18) continue; // too far away
		if (!univ_sphere_vis(gpos, galaxy.radius)) continue; // conservative, since galaxies are not spherical
		current.galaxy = i;
		galaxy.process(*this);
		// force planets and moons to be created for ship colonization; test for starting galaxy by looking at proximity to starting point (conservative)
		bool const gen_all_bodies(no_shift_universe && dist_less_than(gpos, universe_origin, GALAXY_MIN_SIZE));

		if (!gen_only && pass == 0 && sel_g && !galaxy.asteroid_fields.empty()) { // draw asteroid fields (sel_g?)
			set_universe_ambient_color(galaxy.color);
			uasteroid_field::begin_render(usg.asteroid_shader, 0, 1);

			for (vector<uasteroid_field>::iterator f = galaxy.asteroid_fields.begin(); f != galaxy.asteroid_fields.end(); ++f) {
				if (!no_move) {f->apply_physics(pos, camera);}
				f->draw(pos, camera, usg.asteroid_shader, 0);
			}
			uasteroid_field::end_render(usg.asteroid_shader);
		}
		for (unsigned c = 0; c < galaxy.clusters.size(); ++c) {
			point const cpos(pos + galaxy.clusters[c].center);
			if (!gen_all_bodies && !univ_sphere_vis(cpos, galaxy.clusters[c].bounds)) continue;
			float const max_size(calc_sphere_size(cpos, camera, STAR_MAX_SIZE));
			ugalaxy::system_cluster const &cl(galaxy.clusters[c]);
			//set_universe_ambient_color((galaxy.color + cl.color)*0.5); // average the galaxy and cluster colors (but probably always reset below)

			for (unsigned j = cl.s1; j < cl.s2; ++j) {
				bool const sel_s(sel_g && (int)j == clobj.system);
				if (p_system && ((pass == 0 && sel_s) || (pass != 0 && !sel_s))) continue; // draw in another pass
				ussystem &sol(galaxy.sols[j]);
				float const sradius(sol.sun.radius);

				if (max_size*sradius < 0.1f*STAR_MAX_SIZE) {
					if (!sel_g) sol.free_uobj();
					continue;
				}
				point_d const spos(pos + sol.pos);
				float const sizes(calc_sphere_size(spos, camera, sradius));

				if (sizes < 0.1) {
					if (!sel_g) sol.free_uobj();
					continue;
				}
				bool const update_pass(sel_g && !no_move && ((int(tfticks)+j)&31) == 0);
				if (!update_pass && sol.gen != 0 && !univ_sphere_vis(spos, sol.radius)) continue;
				bool const sel_sun(sel_s && (clobj.type == UTYPE_STAR || clobj.type == UTYPE_SYSTEM));
				bool const skip_s(p_system && ((pass == 1 && sel_sun) || (pass == 2 && !sel_sun)));
				bool const has_sun(sol.sun.is_ok()), sun_visible(has_sun && !skip_s && univ_sphere_vis(spos, 2.0*sradius));
				bool const planets_visible(PLANET_MAX_SIZE*sscale_val*sizes >= 0.3f*sradius || !sclip);
				bool draw_asteroid_belt(0);
				current.system  = j;
				current.cluster = sol.cluster_id;

				for (unsigned sol_draw_pass = 0; sol_draw_pass < unsigned(1+sel_s); ++sol_draw_pass) { // behind sun, in front of sun
					if (!gen_only && sun_visible && sol_draw_pass == unsigned(sel_s)) {
						bool const calc_flare_intensity(sel_g && no_asteroid_dust); // skip in reflection mode when no_asteroid_dust==1
						if (!sol.sun.draw(spos, usg, star_pld, star_psd, 0, calc_flare_intensity)) continue;
					}
					if (sol_draw_pass == 0 && (planets_visible || gen_all_bodies)) {sol.process();}
					if (sol.planets.empty()) continue;

					if (planets_visible) { // asteroid fields may also be visible
						// Note: asteroid belts aren't drawn in the first frame because sel_s won't be set, since clobj isn't valid before the first frame is drawn (universe not yet created)
						if (!gen_only && sol.asteroid_belt != nullptr && sel_s && sol_draw_pass == 0 && pass == 1) {
							if (!no_move) {sol.asteroid_belt->apply_physics(pos, camera);}
							shader_t asteroid_belt_shader;
							sol.asteroid_belt->begin_render(asteroid_belt_shader, 0);
							sol.asteroid_belt->draw(pos, camera, asteroid_belt_shader, 0);
							uasteroid_field::end_render(asteroid_belt_shader);
							draw_asteroid_belt = 1;
						}
						if (!has_sun) { // sun is gone
							set_light_galaxy_ambient_only();
						}
						else {
							set_sun_loc_color(spos, sol.sun.get_light_color(), sradius, 0, 0, BASE_AMBIENT, BASE_DIFFUSE); // slow - can this be made faster?
						}
						set_universe_ambient_color(sol.get_galaxy_color());
					}
					else { // we know all planets are too far away
						if (!sel_g) sol.free_planets(); // optional
						if (!update_pass) continue;
					}
					usg.atmos_to_draw.resize(0);
					usg.rings_to_draw.resize(0);
					vector<std::shared_ptr<uasteroid_belt_planet>> planet_asteroid_belts;

					for (unsigned k = 0; k < sol.planets.size(); ++k) {
						bool const sel_p(sel_s && (clobj.type == UTYPE_PLANET || clobj.type == UTYPE_MOON) && (int)k == clobj.planet);
						if (p_system && (pass == 2 && !sel_p)) continue; // draw in another pass
						uplanet &planet(sol.planets[k]);
						point_d const ppos(pos + planet.pos);
						if (sel_s && (p2p_dist_sq(camera, ppos) < p2p_dist_sq(camera, spos)) != (sol_draw_pass != 0)) continue; // don't draw planet in this pass
						float const pradius(PLANET_ATM_RSCALE*planet.radius), sizep(sscale_val*calc_sphere_size(ppos, camera, pradius));
						bool skip_draw(!planets_visible);

						if (sclip && sizep < (planet.ring_data.empty() ? 0.6 : 0.3)) {
							if (gen_all_bodies) {planet.process();} // process anyway to ensure moons are generated for ship colonization
							else if (!sel_g && sizep < 0.3) planet.free_uobj();
							if (update_pass) {skip_draw = 1;} else {continue;}
						}
						current.planet = k;
						bool const sel_planet(sel_p && clobj.type == UTYPE_PLANET), skip_planet_draw(skip_closest && sel_planet);
						bool const skip_p(p_system && ((pass == 1 && sel_planet) || (pass == 2 && !sel_planet)));
						if (!skip_p && !no_move) {planet.do_update(sol.pos, has_sun, has_sun);} // Note: update even when not visible so that ship colonization and population are correct
						if (skip_draw || (planet.gen != 0 && !univ_sphere_vis(ppos, planet.mosize))) continue;
						bool const planet_visible((sizep >= 0.6 || !sclip) && univ_sphere_vis(ppos, pradius));
						shadow_vars_t svars(make_pt_global(spos), (has_sun ? sradius : 0.0), all_zeros, 0.0, planet.rscale, planet.ring_ri, planet.ring_ro);

						if (planet_visible && planet.is_ok()) {
							if (has_sun) { // determine if any moons shadow the planet
								find_best_moon_shadower(planet, planet.pos, planet.radius, pos, spos, svars.ss_pos, svars.ss_radius);
							}
							if (!gen_only && !skip_p && !skip_planet_draw) {
								planet.bind_rings_texture(2);
								planet.check_gen_texture(int(sizep));
								planet.draw(ppos, usg, planet_plds, svars, 0, sel_s); // ignore return value?
							}
						} // planet visible
						planet.process();
						bool const skip_moons(p_system && sel_planet && !skip_p), sel_moon(sel_p && clobj.type == UTYPE_MOON);

						if (!gen_only && sizep >= 1.0 && !skip_moons && !planet.moons.empty()) {
							int const p_light(2);

							if (has_sun && planet.is_ok()) { // setup planet as an additional light source for all moons
								colorRGBA const pcolor(sol.sun.get_light_color().modulate_with(planet.color)); // very inexact, but maybe close enough
								set_colors_and_enable_light(p_light, BLACK, pcolor); // use planet diffuse
								set_gl_light_pos(p_light, make_pt_global(ppos), 1.0); // point light at planet center
								set_star_light_atten(p_light, 5.0*PLANET_MAX_SIZE/planet.radius);
							}
							for (unsigned l = 0; l < planet.moons.size(); ++l) { // draw moons
								bool const sel_m(sel_moon && (int)l == clobj.moon);
								if (p_system && ((pass == 1 && sel_m) || (pass == 2 && !sel_m))) continue; // draw in another pass
								if (skip_closest && sel_m) continue; // skip (closest)
								umoon &moon(planet.moons[l]);
								if (!moon.is_ok()) continue;
								point_d const mpos(pos + moon.pos);
								float const sizem(sscale_val*calc_sphere_size(mpos, camera, moon.radius));
								if ((sizem < 0.2 && sclip) || !univ_sphere_vis(mpos, moon.radius)) continue;
								current.moon = l;
								//planet.bind_rings_texture(2); // use the planet's rings texture for shadows
								moon.check_gen_texture(int(sizem));
								shadow_vars_t svars2(svars.sun_pos, svars.sun_radius, make_pt_global(ppos), planet.radius, all_ones, 0.0, 0.0);

								if (p2p_dist_sq(mpos, spos) < p2p_dist_sq(ppos, spos) + 0.5*p2p_dist(ppos, mpos)) { // moon closer to sun than planet + half orbit dist
									find_best_moon_shadower(planet, moon.pos, moon.radius, pos, spos, svars2.ss_pos, svars2.ss_radius); // planet doesn't shadow moon, maybe moon does
								}
								moon.draw(mpos, usg, planet_plds, svars2, planet.is_ok(), sel_p);
							}
							if (has_sun && planet.is_ok()) {clear_colors_and_disable_light(p_light);}
						}
						if (!gen_only && planet.is_ok() && ((!skip_p && !sel_moon) || (skip_p && sel_moon))) {
							float const ring_scale(sizep*planet.get_ring_rscale());

							if (planet.asteroid_belt != nullptr && ring_scale > 10.0) { // can get here for multiple planets
								if (ring_scale > 20.0 && !no_move) {planet.asteroid_belt->apply_physics(pos, camera);}
								planet_asteroid_belts.push_back(planet.asteroid_belt);
							}
							if (!planet.ring_data.empty() && ring_scale > 2.5) {
								usg.rings_to_draw.push_back(planet_draw_data_t(k, sizep, ppos, svars, sel_p));
							}
							if (planet_visible && !skip_planet_draw && !planet.gas_giant && planet.atmos > 0.05 && sizep > 5.0) {
								usg.atmos_to_draw.push_back(planet_draw_data_t(k, sizep, ppos, svars, sel_p));
							}
						}
					} // planet k
					if (!planet_plds[0].empty() || !planet_plds[1].empty()) {
						if (!planet_plds[0].empty()) {usg.enable_point_sprite_shader(1.0);}
						planet_plds[0].draw_and_clear();
						if (!planet_plds[1].empty()) {usg.enable_point_sprite_shader(2.0);}
						planet_plds[1].draw_and_clear();
						usg.disable_point_sprite_shader();
					}
					for (auto pab = planet_asteroid_belts.begin(); pab != planet_asteroid_belts.end(); ++pab) {
						shader_t asteroid_belt_shader;
						(*pab)->begin_render(asteroid_belt_shader, 0); // we normally only get here once per frame, so the overhead is acceptable
						(*pab)->draw(pos, camera, asteroid_belt_shader, 1);
						uasteroid_field::end_render(asteroid_belt_shader);
					}
					enable_blend();

					for (unsigned pass = 0; pass < 2; ++pass) { // draw rings behind planets, then atmosphere, then rings in front of planet
						for (auto k = usg.rings_to_draw.begin(); k != usg.rings_to_draw.end(); ++k) {
							uplanet &planet(sol.planets[k->ix]);
							bool const use_a2c(!k->selected); // only for distant rings (looks bad when up close)
							bool const use_two_passes(!use_a2c && !usg.atmos_to_draw.empty() && fabs(dot_product((camera - k->pos).get_norm(), planet.rot_axis.get_norm())) < 0.5);
							
							// distant planets: draw planet+atmos then rings; nearby planets: draw rings, then planet+atmos, then maybe draw near part of rings in a second pass
							if ((pass != 0) == use_a2c || use_two_passes) {
								if (use_a2c) {glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);}
								planet.ensure_rings_texture();
								planet.draw_prings(usg, k->pos, k->size, (use_two_passes ? ((pass == 1) ? -1.0 : 1.0) : 0), use_a2c);
								if (use_a2c) {glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);}
							}
						}
						if (pass == 0 && !usg.atmos_to_draw.empty()) {
							glEnable(GL_CULL_FACE);

							for (vector<planet_draw_data_t>::const_iterator k = usg.atmos_to_draw.begin(); k != usg.atmos_to_draw.end(); ++k) {
								uplanet const &planet(sol.planets[k->ix]);
								planet.draw_atmosphere(usg, k->pos, k->size, k->svars, p2p_dist(camera, (pos + planet.pos)));
							}
							glDisable(GL_CULL_FACE);
						}
					} // pass
					disable_blend();

					for (auto pab = planet_asteroid_belts.begin(); pab != planet_asteroid_belts.end(); ++pab) { // changes lighting, draw last
						(*pab)->draw_detail(pos, camera, no_asteroid_dust, 0, 0.1); // low density, no dust
					}
				} // sol_draw_pass
				if (draw_asteroid_belt) { // changes lighting, draw last
					sol.asteroid_belt->draw_detail(pos, camera, no_asteroid_dust, 1, 1.0);
				}
			} // system j
		} // cluster cs
	} // galaxy i
	if (!gen_only) {draw_all_stars(usg, 1);}
}


void maybe_show_uobj_info(uobject const *const uobj) {
	if (show_scores) {show_info_uobjs.push_back(uobj);}
}

void add_nearby_uobj_text(text_drawer_t &text_drawer) {

	for (auto i = show_info_uobjs.begin(); i != show_info_uobjs.end(); ++i) {
		assert(*i != nullptr);
		string const str((*i)->get_name() + ": " + (*i)->get_info());
		float const size(0.2 + 12.0*p2p_dist((*i)->pos, get_camera_pos()));
		text_drawer.strs.push_back(text_string_t(str, make_pt_global((*i)->pos), size, CYAN));
	}
	show_info_uobjs.clear(); // reset for next frame
}


// *** UPDATE CODE ***


void universe_t::init() {

	assert(U_BLOCKS & 1); // U_BLOCKS is odd

	for (unsigned i = 0; i < U_BLOCKS; ++i) { // z
		for (unsigned j = 0; j < U_BLOCKS; ++j) { // y
			for (unsigned k = 0; k < U_BLOCKS; ++k) { // x
				int const ii[3] = {(int)k, (int)j, (int)i};
				cells[i][j][k].gen_cell(ii);
			}
		}
	}
}


void universe_t::shift_cells(int dx, int dy, int dz) {

	assert((abs(dx) + abs(dy) + abs(dz)) == 1);
	vector3d const vxyz((float)dx, (float)dy, (float)dz);

	for (unsigned i = 0; i < U_BLOCKS; ++i) { // z
		for (unsigned j = 0; j < U_BLOCKS; ++j) { // y
			for (unsigned k = 0; k < U_BLOCKS; ++k) { // x
				int const i2(i + dz), j2(j + dy), k2(k + dx);
				bool const zout(i2 < 0 || i2 >= int(U_BLOCKS));
				bool const yout(j2 < 0 || j2 >= int(U_BLOCKS));
				bool const xout(k2 < 0 || k2 >= int(U_BLOCKS));

				if (xout || yout || zout) { // allocate new cell
					int const ii[3]     = {(int)k, (int)j, (int)i};
					temp.cells[i][j][k].gen = 0;
					temp.cells[i][j][k].gen_cell(ii);
				}
				else {
					cells[i2][j2][k2].gen           = 1;
					temp.cells[i][j][k]             = cells[i2][j2][k2];
					temp.cells[i][j][k].rel_center -= vxyz*CELL_SIZE;
				}
			}
		}
	}
	for (unsigned i = 0; i < U_BLOCKS; ++i) { // z
		for (unsigned j = 0; j < U_BLOCKS; ++j) { // y
			for (unsigned k = 0; k < U_BLOCKS; ++k) { // x
				if (cells[i][j][k].gen == 0) cells[i][j][k].free_uobj();
				cells[i][j][k]     = temp.cells[i][j][k];
				cells[i][j][k].gen = 0;
			}
		}
	}
}


// *** GENERATION CODE ***
inline int gen_rand_seed1(point const &center) {

	return 196613*(int(RS_SCALE*center.x+0.5)) +
		   393241*(int(RS_SCALE*center.y+0.5)) +
		   786433*(int(RS_SCALE*center.z+0.5)) + RAND_CONST*123;
}


inline int gen_rand_seed2(point const &center) {

	return 6291469*(int(RS_SCALE*center.x+0.5)) +
		   3145739*(int(RS_SCALE*center.y+0.5)) +
		   1572869*(int(RS_SCALE*center.z+0.5)) + RAND_CONST*456;
}


void ucell::gen_cell(int const ii[3]) {

	if (gen) return; // already generated
	UNROLL_3X(rel_center[i_] = CELL_SIZE*(float(ii[i_] - (int)U_BLOCKSo2));)
	pos    = rel_center + get_scaled_upt();
	radius = 0.5*CELL_SIZE;
	set_rand2_state(gen_rand_seed1(pos), gen_rand_seed2(pos));
	get_rseeds();
	galaxies.reset(new vector<ugalaxy>);
	galaxies->resize(rand_uniform_uint2(MIN_GALAXIES_PER_CELL, MAX_GALAXIES_PER_CELL));

	for (unsigned l = 0; l < galaxies->size(); ++l) { // gen galaxies
		if (!(*galaxies)[l].create(*this, l)) { // can't place the galaxy
			galaxies->resize(l); // so remove it
			break;
		}
	}
	gen = 1;
}


ugalaxy::ugalaxy() : lrq_rad(0.0), lrq_pos(all_zeros), color(BLACK) {}
ugalaxy::~ugalaxy() {}


bool ugalaxy::create(ucell const &cell, int index) {

	current.type = UTYPE_GALAXY;
	gen_rseeds();
	clear_systems();
	gen      = 0;
	radius   = rand_uniform2(GALAXY_MIN_SIZE, GALAXY_MAX_SIZE);
	xy_angle = rand_uniform2(0.0, TWO_PI);
	axis     = signed_rand_vector2_norm();
	scale    = vector3d(1.0, rand_uniform2(0.6, 1.0), rand_uniform2(0.07, 0.2));
	lrq_rad  = 0.0;
	lrq_pos  = all_zeros;
	gen_name(current);
	cube_t const cube(-radius*scale, radius*scale);
	point galaxy_ext(all_zeros), pts[8];
	cube.get_points(pts);
	rotate_vector3d_multi(axis, -xy_angle, pts, 8);

	for (unsigned p = 0; p < 8; ++p) {
		for (unsigned j = 0; j < 3; ++j) {galaxy_ext[j] = max(galaxy_ext[j], fabs(pts[p][j]));}
	}
	for (unsigned j = 0; j < 3; ++j) {
		galaxy_ext[j] = (CELL_SIZEo2 - MAX_SYSTEM_EXTENT - min(GALAXY_OVERLAP*radius, galaxy_ext[j]));
		assert(galaxy_ext[j] >= 0.0);
	}
	for (unsigned i = 0; i < MAX_TRIES; ++i) {
		for (unsigned j = 0; j < 3; ++j) {pos[j] = double(galaxy_ext[j])*signed_rand_float2();}
		bool too_close(0);

		for (int j = 0; j < index && !too_close; ++j) {
			too_close = is_close_to((*cell.galaxies)[j], GALAXY_OVERLAP);
		}
		if (!too_close) return 1;
	}
	return 0;
}


void ugalaxy::apply_scale_transform(point &pos_) const {

	UNROLL_3X(pos_[i_] *= scale[i_];)
	rotate_vector3d(axis, xy_angle, pos_);
}


point ugalaxy::gen_valid_system_pos() const {

	float const rsize(radius*(1.0 - sqrt(rand2d())));
	point pos2(gen_rand_vector2(rsize));
	apply_scale_transform(pos2);
	return pos2 + pos;
}


float ugalaxy::get_radius_at(point const &pos_, bool exact) const {

	if (!exact && lrq_rad > 0.0 && p2p_dist_sq(pos_, lrq_pos) < 0.000001*min(radius*radius, p2p_dist_sq(pos_, pos))) {
		return 1.001*lrq_rad; // point is so close to last point that we can used the cached value (be conservative)
	}
	vector3d dir(pos_);
	rotate_vector3d(axis, -xy_angle, dir);
	dir[0] *= scale[0];
	dir[1] *= scale[1];
	dir[2] *= scale[2];
	float const rval(radius*dir.mag());
	lrq_rad = rval;
	lrq_pos = pos_;
	return rval;
}


bool ugalaxy::is_close_to(ugalaxy const &g, float overlap_amount) const {

	vector3d const delta(pos - g.pos);
	float const dist(delta.mag());
	return (dist < TOLERANCE || dist < ((overlap_amount/dist)*(get_radius_at(-delta) + g.get_radius_at(delta)) + SYSTEM_MIN_SPACING));
}


void ugalaxy::calc_color() {

	color = BLACK;

	for (unsigned j = 0; j < sols.size(); ++j) {
		color += sols[j].sun.get_ambient_color_val();
		//sols[j].calc_color(); // delay until later
	}
	color *= 1.5/MAX_SYSTEMS_PER_GALAXY;
	color.set_valid_color();
}


void ugalaxy::calc_bounding_sphere() {

	float dist_sq_max(0.0);

	for (unsigned i = 0; i < sols.size(); ++i) {
		dist_sq_max = max(p2p_dist_sq(pos, sols[i].pos), dist_sq_max);
	}
	radius = sqrt(dist_sq_max);
}


void ugalaxy::process(ucell const &cell) {

	if (gen) return;
	//RESET_TIME;
	current.type = UTYPE_GALAXY;
	set_rseeds();

	// gen systems
	unsigned num_systems(max(MAX_SYSTEMS_PER_GALAXY/10, rand2()%(MAX_SYSTEMS_PER_GALAXY+1)));
	vector<point> placed;

	for (unsigned i = 0; i < cell.galaxies->size(); ++i) { // find galaxies that overlap this one
		ugalaxy const &g((*cell.galaxies)[i]);
		if (&g == this || !is_close_to(g, 1.0)) continue;

		for (unsigned j = 0; j < g.sols.size(); ++j) { // find systems in other galaxies that overlap this one
			point const spos(g.pos + g.sols[j].pos);
			vector3d const sdelta(spos - pos);
			float const sdist(sdelta.mag());
			
			if (sdist < TOLERANCE || (sdist < (radius/sdist + MAX_SYSTEM_EXTENT) &&
				sdist < (get_radius_at(sdelta)/sdist + MAX_SYSTEM_EXTENT)))
			{
				placed.push_back(spos);
			}
		}
	}
	for (unsigned i = 0; i < num_systems; ++i) {
		if (!gen_system_loc(placed)) num_systems = i; // can't place it, give up
	}
	sols.resize(num_systems);
	unsigned tot_systems(0);

	for (unsigned c = 0, cur = 0; c < clusters.size(); ++c) { // calculate actual center and radius values for each cluster
		system_cluster &cl(clusters[c]);
		unsigned const nsystems((unsigned)cl.systems.size());
		assert(nsystems > 0);
		cl.radius = 0.0;
		cl.bounds = 0.0;
		cl.center = all_zeros;
		cl.s1     = cur;
		cl.color  = BLACK;
		current.cluster = c;
		for (unsigned i = 0; i < nsystems; ++i) {cl.center += cl.systems[i];}
		cl.center /= nsystems;

		for (unsigned i = 0; i < nsystems; ++i, ++cur) {
			cl.radius        = max(cl.radius, p2p_dist_sq(cl.center, cl.systems[i]));
			current.system   = cur;
			sols[cur].galaxy = this;
			sols[cur].cluster_id = c;
			sols[cur].create(cl.systems[i]);
			cl.color += sols[cur].sun.get_ambient_color_val();
		}
		clear_container(cl.systems);
		cl.color    *= 1.0/nsystems;
		cl.color.set_valid_color();
		cl.radius    = sqrt(cl.radius);
		cl.bounds    = cl.radius + MAX_SYSTEM_EXTENT;
		tot_systems += nsystems;
		cl.s2        = cur;
	}
	assert(tot_systems == num_systems);
	calc_bounding_sphere();
	calc_color();
	lrq_rad = 0.0;
	//PRINT_TIME("Gen Galaxy");

	if (num_systems > MAX_SYSTEMS_PER_GALAXY/4 && rand_float2() < NEBULA_PROB) { // gen nebula
		nebula.pos = gen_valid_system_pos();
		nebula.gen(radius, *this);
	}
	//PRINT_TIME("Gen Nebula");

	// gen asteroid fields
	unsigned const num_af(rand_uniform_uint2(MIN_AST_FIELD_PER_GALAXY, MAX_AST_FIELD_PER_GALAXY));
	asteroid_fields.resize(num_af);

	for (vector<uasteroid_field>::iterator i = asteroid_fields.begin(); i != asteroid_fields.end(); ++i) {
		i->init(gen_valid_system_pos(), radius*rand_uniform2(0.005, 0.01));
	}
	//PRINT_TIME("Gen Asteroid Fields");
	gen = 1;
}


bool ugalaxy::gen_system_loc(vector<point> const &placed) {

	for (unsigned i = 0; i < MAX_TRIES; ++i) {
		point const pos2(gen_valid_system_pos());
		bool bad_pos(0);
		
		for (unsigned j = 0; j < 3 && !bad_pos; ++j) {
			if (fabs(pos2[j]) > (CELL_SIZEo2 - MAX_SYSTEM_EXTENT)) bad_pos = 1; // shouldn't really get here
		}
		for (unsigned j = 0; j < placed.size() && !bad_pos; ++j) {
			bad_pos = dist_less_than(pos2, placed[j], SYSTEM_MIN_SPACING);
		}
		for (unsigned c = 0; c < clusters.size() && !bad_pos; ++c) {
			if (dist_less_than(pos2, clusters[c].center, clusters[c].bounds)) { // close to a cluster
				vector<point> const &cs(clusters[c].systems);

				for (unsigned s = 0; s < cs.size() && !bad_pos; ++s) {
					bad_pos = dist_less_than(pos2, cs[s], SYSTEM_MIN_SPACING);
				}
			}
		}
		if (bad_pos) continue;
		unsigned in_cluster((unsigned)clusters.size());
		float dmin(0.0);

		for (unsigned c = 0; c < clusters.size(); ++c) {
			float const test_dist((dmin == 0.0) ? clusters[c].radius : min(clusters[c].radius, dmin));

			if (dist_less_than(pos2, clusters[c].center, test_dist)) {
				in_cluster = c;
				dmin       = p2p_dist(pos2, clusters[c].center);
			}
		}
		if (in_cluster == clusters.size()) { // create initial clusters with fixed radius around a starting point
			float const cluster_size(0.1*radius + 0.3*p2p_dist(pos2, pos));
			clusters.push_back(system_cluster(cluster_size, pos2));
			clusters.back().systems.reserve(MAX_SYSTEMS_PER_GALAXY/10);
		}
		assert(in_cluster < clusters.size());
		system_cluster &cl(clusters[in_cluster]);
		cl.systems.push_back(pos2);

		if (cl.systems.size() == 2) { // use the center of the two points
			cl.center = (cl.systems[0] + cl.systems[1])*0.5;
			cl.bounds = 0.0;
		}
		cl.bounds = max(cl.bounds, (p2p_dist(pos2, cl.center) + SYSTEM_MIN_SPACING));
		return 1;
	}
	return 0;
}


void ussystem::create(point const &pos_) {

	current.type = UTYPE_SYSTEM;
	gen_rseeds();
	planets.clear();
	gen    = 0;
	radius = 0.0;
	pos    = pos_;
	galaxy_color.alpha = 0.0; // set to an invalid state
	sun.create(pos);
}


void ustar::create(point const &pos_) {

	current.type = UTYPE_STAR;
	set_defaults();
	gen_rseeds();
	pos      = pos_;
	// temperature/radius/color aren't statistically accurate, see:
	// http://en.wikipedia.org/wiki/Stellar_classification
	temp     = rand_gaussian2(55.0, 10.0);
	radius   = 0.25*rand_uniform2(STAR_MIN_SIZE, STAR_MAX_SIZE) + (37.5*STAR_MAX_SIZE/temp)*rand_gaussian2(0.3, 0.1);
	radius   = max(radius, STAR_MIN_SIZE); // a lot of stars are of size exactly STAR_MIN_SIZE
	gen_color();
	density  = rand_uniform2(3.0, 5.0);
	set_grav_mass();
	rot_axis = signed_rand_vector2_norm(); // for orbital plane
	gen      = 1; // get_name() is called later
	current.type = UTYPE_STAR;
	if (current.is_destroyed()) status = 1;
}


colorRGBA const &ussystem::get_galaxy_color() {

	if (galaxy_color.alpha == 0.0) {calc_color();} // lazy update
	assert(galaxy_color.alpha > 0.0);
	return galaxy_color;
}


void ussystem::calc_color() { // delayed until the color is actually needed

	assert(galaxy);
	vector<ussystem> const &sols(galaxy->sols);
	assert(!sols.empty());
	galaxy_color = BLACK;
	float const max_dist(5.0*SYSTEM_MIN_SPACING), max_dist_sq(max_dist*max_dist);
	float sum(0.0);

	for (unsigned j = 0; j < galaxy->clusters.size(); ++j) {
		ugalaxy::system_cluster const &cl(galaxy->clusters[j]);
		if (p2p_dist_sq(pos, cl.center) > (max_dist + cl.radius)*(max_dist + cl.radius)) continue; // too far away

		for (unsigned s = cl.s1; s < cl.s2; ++s) {
			if (&sols[s] == this)   continue; // skip ourselves
			float const dist(p2p_dist_sq(pos, sols[s].sun.pos));
			if (dist > max_dist_sq) continue; // too far away
			float const weight(1.0/max(dist, TOLERANCE)); // 1/r^2 falloff
			sum          += weight;
			galaxy_color += sols[s].sun.get_ambient_color_val()*weight;
		}
	}
	if (sum == 0.0) return; // no other systems
	galaxy_color *= 1.5f/((sum/sols.size())*MAX_SYSTEMS_PER_GALAXY); // remember to normalize
	galaxy_color.set_valid_color();
}


uplanet *ussystem::get_planet_by_name(string const &name) {

	for (unsigned i = 0; i < planets.size(); ++i) {
		if (planets[i].getname() == name) return &planets[i];
	}
	return nullptr;
}

umoon *ussystem::get_moon_by_name(string const &name) { // slow

	for (unsigned i = 0; i < planets.size(); ++i) {
		umoon *moon(planets[i].get_moon_by_name(name));
		if (moon != nullptr) return moon;
	}
	return nullptr;
}


void ussystem::process() {

	if (gen) return;
	current.type = UTYPE_STAR;
	sun.set_rseeds();
	sun.gen_name(current);
	current.type = UTYPE_SYSTEM;
	set_rseeds();
	planets.resize((unsigned)sqrt(float((rand2()%(MAX_PLANETS_PER_SYSTEM+1))*(rand2()%(MAX_PLANETS_PER_SYSTEM+1)))));
	float const sradius(sun.radius);
	radius = sradius;

	if (system_max_orbit != 1.0 && system_max_orbit > 0.0) { // ignore zero and negative values
		if (system_max_orbit < 1.0) {system_max_orbit = 1.0/system_max_orbit;}
		// Note: This code will produce elliptical system (planet and asteroid belt) orbits, which have some issues/limitations:
		// * Temperature changes with distance to the sun, which may cause problems with AI ships entering or leaving planets
		// * Collisions between planets, moons, and asteroids may be possible because initial distances may be larger than min distances
		// * Planet and asteroid updates will be slower and may be less stable over long periods of time
		float const min_orbit(1.0/system_max_orbit); // lower end
		orbit_scale.assign(rand_uniform2(min_orbit, system_max_orbit), rand_uniform2(min_orbit, system_max_orbit), 1.0); // z is always 1.0
	}
	for (unsigned i = 0; i < planets.size(); ++i) {
		current.planet    = i;
		planets[i].system = this;

		if (!planets[i].create_orbit(planets, i, pos, sun.rot_axis, sradius, PLANET_MAX_SIZE, PLANET_MIN_SIZE,
			INTER_PLANET_MIN_SPACING, PLANET_TO_SUN_MAX_SPACING, PLANET_TO_SUN_MIN_SPACING, 0.0, orbit_scale))
		{ // failed to place planet
			planets.resize(i);
			remove_excess_cap(planets);
			break;
		}
		float const dmax(planets[i].orbit + planets[i].radius + MOON_TO_PLANET_MAX_SPACING + MOON_MAX_SIZE);
		radius = max(radius, dmax); // too bad we can't use p.mosize
	}
	sun.num_satellites = (unsigned short)planets.size();
	assert(asteroid_belt == nullptr);

	if (planets.size() > 1 && !(rand2() & 1)) {
		vector<float> orbits(planets.size());
		for (unsigned i = 0; i < planets.size(); ++i) {orbits[i] = planets[i].orbit;}
		sort(orbits.begin(), orbits.end()); // smallest to largest
		unsigned const inner_planet(rand2() % (orbits.size()-1)); // between two planet orbits, so won't increase system radius
		float const ab_radius(0.5f*(orbits[inner_planet] + orbits[inner_planet+1])); // halfway between two planet orbits
		asteroid_belt.reset(new uasteroid_belt_system(sun.rot_axis, this));
		asteroid_belt->init(pos, ab_radius); // gen_asteroids() will be called when drawing
	}
	radius = max(radius, 0.5f*(PLANET_TO_SUN_MIN_SPACING + PLANET_TO_SUN_MAX_SPACING)); // set min radius so that hyperspeed coll works
	gen    = 1;
}


bool urev_body::can_land() const {
	return (!gas_giant && temp >= MIN_LAND_TEMP && temp <= MAX_LAND_TEMP);
}

bool urev_body::colonizable() const {
	return (is_ok() && !gas_giant && temp >= MIN_COLONY_TEMP && temp <= MAX_COLONY_TEMP && colonizable_int());
}

bool urev_body::liveable() const { // only planets are liveable
	return (is_ok() && !gas_giant && water > 0.15 && atmos > 0.25 && temp >= MIN_LIVE_TEMP && temp <= MAX_LIVE_TEMP);
}

void uplanet::calc_temperature() {
	assert(orbit > TOLERANCE);
	temp = system->sun.get_temperature_at_dist(orbit); // ~2-50
}


void uplanet::create(bool phase) {

	if (phase == 1) return; // no phase 1, only phase 0
	current.type = UTYPE_PLANET;
	gen_rotrev();
	mosize = radius;
	moons.clear();
	ring_data.clear();
	float const rel_radius((radius - PLANET_MIN_SIZE)/(PLANET_MAX_SIZE - PLANET_MIN_SIZE));

	// atmosphere, water, temperature, gravity
	calc_temperature();
	density = rand_uniform2(0.8, 1.2);
	if (temp < CGAS_TEMP) {density *= 0.5 + 0.5*(temp/CGAS_TEMP);} // cold gas
	set_grav_mass();
	
	if (temp < FREEZE_TEMP) { // cold
		gas_giant = (rel_radius > GAS_GIANT_MIN_REL_SZ);
		atmos     = (gas_giant ? 1.0 : rand_uniform2(-0.2, 1.0)); // less atmosphere for ice planets?
		water     = (gas_giant ? 0.2 : 1.0)*min(1.0f, rand_uniform2(0.0, 1.2)); // ice // rand_uniform2(0.0, MAX_WATER)
		comment   = " (Cold)";
		if      (gas_giant)    {comment += " Gas Giant";}
		else if (atmos > 0.5 && water > 0.25 && temp > MIN_PLANT_TEMP) {comment += ((water > 0.99) ? " Ocean Planet" : " Terran Planet");}
		else if (water > 0.75) {comment += " Ice Planet";}
		else                   {comment += " Rocky Planet";}
	}
	else if (temp > NO_AIR_TEMP) { // very hot
		gas_giant = (rel_radius > GAS_GIANT_MIN_REL_SZ);
		atmos     = (gas_giant ? 1.0 : rand_uniform2(-1.0, 1.0));
		water     = 0.0;
		lava      = (gas_giant ? 0.0 : max(0.0f, rand_uniform2(-0.4, 0.4)));
		comment   = " (Very Hot)";
		if      (gas_giant)   {comment += " Gas Giant";}
		else if (lava > 0.05) {comment += " Volcanic Planet";}
		else                  {comment += " Rocky Planet";}
	}
	else if (temp > BOIL_TEMP) { // hot (rare)
		atmos   = rand_uniform2(-0.9, 0.5);
		water   = 0.0;
		comment = " (Hot) Rocky Planet";
	}
	else { // average temp
		atmos   = rand_uniform2(-0.3, 1.5);
		water   = max(0.0f, min(MAX_WATER, 0.5f*(atmos + rand_uniform2(-MAX_WATER, 0.9*MAX_WATER))));
		comment = " (Temperate)";
		if (water > 0.99)                     {comment += " Ocean Planet";} // Note: currently doesn't exist
		else if (atmos > 0.5 && water > 0.25) {comment += " Terran Planet";}
		else                                  {comment += " Rocky Planet";}
	}
	atmos     = CLIP_TO_01(atmos);
	float const rsc_scale(liveable() ? 2.0 : (colonizable() ? 1.0 : 0.5));
	resources = 750.0*radius*rsc_scale*(1.0 + 0.25*atmos - 0.25*fabs(0.5 - water))*(1.0 - fabs(1.0 - density));
	check_owner(current); // must be after setting of resources
	gen_color();
	gen_name(current);
	calc_snow_thresh();
	cloud_scale  = rand_uniform2(1.0, 2.0);
	current.type = UTYPE_PLANET;
	if (current.is_destroyed()) {status = 1;}
}


float uplanet::get_vegetation() const {
	return ((has_vegetation() && temp > MIN_PLANT_TEMP && temp < MAX_PLANT_TEMP) ? sqrt(atmos*water) : 0.0);
}

bool uplanet::has_ice_debris() const {
	return (temp < 0.75*FREEZE_TEMP && water > 0.3); // only for ice planets
}


void uplanet::process() {

	if (gen) return;
	current.type = UTYPE_PLANET;
	set_rseeds();
	if ((gas_giant || temp < CGAS_TEMP) && (rand2()&1)) {gen_prings();} // rings
	unsigned num_moons(0);

	if (rand2()&1) { // has moons
		num_moons = (unsigned)sqrt(float((rand2()%(MAX_MOONS_PER_PLANET+1))*(rand2()%(MAX_MOONS_PER_PLANET+1))));
	}
	moons.resize(num_moons);

	for (unsigned i = 0; i < moons.size(); ++i) {
		current.moon    = i;
		moons[i].planet = this;

		if (!moons[i].create_orbit(moons, i, pos, rot_axis, radius, MOON_MAX_SIZE, MOON_MIN_SIZE,
			INTER_MOON_MIN_SPACING, MOON_TO_PLANET_MAX_SPACING, MOON_TO_PLANET_MIN_SPACING, MOON_TO_PLANET_MIN_GAP, rscale))
		{ // failed to place moon
			moons.resize(i);
			remove_excess_cap(moons);
			break;
		}
		float const mo(moons[i].orbit), xy_scale(rscale.xy_mag()), mo_scaled(mo/xy_scale);
		if (mo_scaled < ring_ro) {moons[i].radius *= 0.5*(1.0 + max(0.0f, (mo_scaled - ring_ri)/(ring_ro - ring_ri)));} // smaller radius for moons within the planet's rings
		mosize = max(mosize, (radius + mo + moons[i].radius)); // multiply orbit by xy_scale?
	}
	if (!moons.empty()) { // calculate rotation rate about rotation axis due to moons (Note: Stars can rotate as well.)
		// rk_term = r/(2*PI*a*k);
		// T^2 = k*(4*PI*PI*a*a*a/(G*(M + m)*cosf(i)*cosf(i)))*((m/M)*(r/R) + (M/m)*(D/d)*rk_term*rk_term);
		// a = average moon orbit, M = mass, m = sum of moon mass, R = radius, r = average moon radius
		// rot_rate = 360.0/t = 360.0/(3600*TICKS_PER_SECOND*T) = 1.0/(10.0*TICKS_PER_SECOND*sqrt(...))
		float rav(0.0), aav(0.0), dav(0.0), cav(0.0), mtot(0.0);

		for (unsigned i = 0; i < moons.size(); ++i) { // technically, this should be reclaculated if a moon is destroyed
			mtot += moons[i].mass;
			rav  += moons[i].radius*moons[i].mass;
			aav  += moons[i].orbit*moons[i].mass;
			dav  += moons[i].density*moons[i].mass;
			cav  += (1.0 - fabs(dot_product(rot_axis, moons[i].rev_axis)))*moons[i].mass; // probably not quite right
		}
		rav /= mtot;
		aav /= mtot;
		dav /= mtot;
		cav /= mtot;
		float const k(rand_uniform2(0.05, 0.5)), ci(cosf(cav)), rk_term(rav/(2*PI*aav*k));
		float const T_sq(k*(4*PI*PI*aav*aav*aav/(mass + mtot)*ci*ci)*((mtot/mass)*(rav/radius) + (mass/mtot)*(density/dav)*rk_term*rk_term));
		assert(T_sq > 0.0);
		rot_rate = ROT_RATE_CONST/(10.0*TICKS_PER_SECOND*sqrt(T_sq));
	}
	num_satellites = (unsigned short)moons.size();
	// gas giants have atmosphere=1.0, but can have variable cloud density
	if (gas_giant) {cloud_density = max(0.0f, rand_uniform2(-0.25, 0.75));} // Note: computed here to avoid altering the random number generator in create()
	gen = 1;
}


point_d uplanet::do_update(point_d const &p0, bool update_rev, bool update_rot) {

	bool const has_sun(system->sun.is_ok());
	if (!has_sun) {temp = 0.0;}
	point_d const planet_pos(urev_body::do_update(p0, (has_sun && update_rev), (has_sun && update_rot)));
	bool const ok(is_ok());
	
	for (unsigned i = 0; i < moons.size(); ++i) {
		moons[i].do_update(planet_pos, ok, ok);
		if (!has_sun) {moons[i].temp = 0.0;}
	}
	if (ok && has_sun && is_owned() && colonizable()) {
		float const pop_rate((population == 0) ? 1.0 : 0.0001); // init + growth
		float const pop_scale(2.0E6*(liveable() ? 1.0 : 0.25)*radius*radius*(1.1 - water)*((water > 0.05) ? 1.0 : 0.1)*(atmos + 0.1)); // based on land area
		population += pop_scale*pop_rate;
		population  = max(population, 0.5f*prev_pop); // keep at least half of the population from when previously owned
		population  = min(population, 5.0f*pop_scale);
		prev_pop    = 0.0;
	}
	else if (population > 0.0) {
		prev_pop   = population;
		population = 0.0;
	}
	return planet_pos;
}


struct upring {
	float radius1, radius2;
};


void uplanet::gen_prings() {

	unsigned const nr((rand2()%10)+1);
	float const sr(4.0/nr);
	float lastr(rand_uniform2(1.1*radius, 1.2*radius));
	vector<upring> rings(nr);

	for (unsigned i = 0; i < nr; ++i) {
		upring &ring(rings[i]);
		ring.radius1 = lastr        + sr*radius*rand_uniform2(-0.05, 0.05);
		ring.radius2 = ring.radius1 + sr*radius*rand_uniform2(0.05,  0.3 );
		lastr = ring.radius2;
	}
	ring_data.resize(RING_TEX_SZ);
	for (unsigned i = 0; i < RING_TEX_SZ; ++i) {ring_data[i].set_c4(ALPHA0);}
	ring_ri = rings.front().radius1;
	ring_ro = rings.back().radius2;
	float const rdiv((RING_TEX_SZ-3)/(ring_ro - ring_ri));
	colorRGBA rcolor(color);
	UNROLL_3X(rcolor[i_] += rand_uniform2(0.1, 0.6);)
	float alpha(rand_uniform2(0.75, 1.0));

	for (vector<upring>::const_iterator i = rings.begin(); i != rings.end(); ++i) {
		unsigned const tri(1+(i->radius1 - ring_ri)*rdiv), tro(1+(i->radius2 - ring_ri)*rdiv);
		assert(tri > 0 && tro+1 < RING_TEX_SZ && tri < tro);
		UNROLL_3X(rcolor[i_] = CLIP_TO_01(rcolor[i_]*(1.0f + rand_uniform2(-0.15, 0.15)));)
		alpha = CLIP_TO_01(alpha*(1.0f + rand_uniform2(-0.1, 0.1)));

		for (unsigned j = tri; j < tro; ++j) {
			float const v(fabs(j - 0.5f*(tri + tro))/(0.5f*(tro - tri)));
			rcolor.A = alpha*(1.0f - v*v);
			ring_data[j].add_c4(rcolor);
		}
	}
	for (unsigned i = 0; i < 2; ++i) {rscale[i] = rand_uniform2(1.0, 2.2);} // x/y
	rscale.z = 1.0; // makes no difference
	float max_rs(0.0);
	UNROLL_3X(max_rs = max(max_rs, rscale[i_]);)
	mosize = max(mosize, max_rs*lastr); // extend planet effective size
	
	assert(asteroid_belt == nullptr);
	asteroid_belt.reset(new uasteroid_belt_planet(rot_axis, this));
	asteroid_belt->init_rings(pos); // gen_asteroids() will be called when drawing
}


umoon *uplanet::get_moon_by_name(string const &name) {

	for (unsigned i = 0; i < moons.size(); ++i) {
		if (moons[i].getname() == name) return &moons[i];
	}
	return nullptr;
}


void uplanet::get_valid_orbit_r(float &orbit_r, float obj_r) const { // for satellites

	assert(is_ok());

	for (unsigned i = 0; i < moons.size(); ++i) {
		if (!moons[i].is_ok()) continue;
		float const orad(ORBIT_SPACE_MARGIN*(obj_r + moons[i].radius));

		if ((moons[i].orbit - orad) < orbit_r && (moons[i].orbit + orad) > orbit_r) { // orbits overlap (rarely happens)
			orbit_r = (moons[i].orbit - orad);
		}
	}
	orbit_r = max(orbit_r, (radius + ORBIT_SPACE_MARGIN*obj_r)); // apply min_orbit
}


void umoon::get_valid_orbit_r(float &orbit_r, float obj_r) const { // for satellites

	assert(is_ok());
	if (!planet || !planet->is_ok()) return;
	float const orad(ORBIT_SPACE_MARGIN*(obj_r + planet->radius));
	orbit_r = min(orbit_r, (orbit - orad));
	orbit_r = max(orbit_r, (radius + ORBIT_SPACE_MARGIN*obj_r)); // apply min_orbit
}


void umoon::calc_temperature() {

	temp = planet->system->sun.get_temperature_at_pt(pos);
	if (shadowed_by_planet()) {temp *= 0.75;} // cooler in shadow
}


void umoon::create(bool phase) { // no rotation due to satellites

	current.type = UTYPE_MOON;
	
	if (phase == 0) {
		gen_rotrev();
		gen = 2;
	}
	else {
		assert(gen == 2);
		density = rand_uniform2(0.8, 1.2);
		set_grav_mass();
		temp = planet->temp;
		gen_color();
		gen_name(current);
		resources = 750.0*radius*(colonizable() ? 2.0 : 1.0)*(1.0 - fabs(1.0 - density));
		if ((rand2()&3) == 0) {water = rand_uniform2(0.0, 0.2);} // some moons have a small amount of water
		check_owner(current); // must be after setting of resources
		calc_temperature(); // has to be after setting of resources - resources must be independent of moon position/temperature
		calc_snow_thresh();
		gen = 1;
	}
	current.type = UTYPE_MOON;
	if (current.is_destroyed()) status = 1;
}


void rotated_obj::rgen_values() {

	rot_ang  = rot_ang0 = 360.0*rand2d(); // degrees in OpenGL
	rev_ang  = rev_ang0 = 360.0*rand2d(); // degrees in OpenGL
	rot_axis = signed_rand_vector2_norm();
}


void urev_body::gen_rotrev() {

	set_defaults();
	gen_rseeds();
	tid = tsize = 0;
	rot_rate = rev_rate = 0.0;
	rotated_obj::rgen_values();
	// inclination angle = angle between rot_axis and rev_axis

	// calculate revolution rate around parent
	// mu = G*M, a = orbit, R = radius, T = 2*PI*sqrt(a*a*a/mu)
	// T = 1.4*sqrt(a*a*a/R*R*R) hours (Standard planet), 1 hour = 3600*TICKS_PER_SECOND ticks
	// rev_rate = 360.0/t = 360.0/(3600*TICKS_PER_SECOND*T) = 1.0/(0.14*TICKS_PER_SECOND*sqrt(a*a*a/R*R*R))
	float const aoR(orbit/radius);
	rev_rate = REV_RATE_CONST/(0.14*TICKS_PER_SECOND*aoR*sqrt(aoR));
}


float get_elliptical_orbit_radius(vector3d const &axis, vector3d const &orbit_scale, vector3d vref) {
	rotate_norm_vector3d_into_plus_z(axis, vref); // Note: rev_axis is parent's rot_axis
	float const s(orbit_scale.x*vref.y), c(orbit_scale.y*vref.x);
	return orbit_scale.x*orbit_scale.y/sqrt(s*s + c*c); // https://www.quora.com/How-do-I-find-the-radius-of-an-ellipse-at-a-given-angle-to-its-axis
}

// Note: Update is only done when the objects solar system is visible to the player
point_d urev_body::do_update(point_d const &p0, bool update_rev, bool update_rot) { // orbit is around p0

	float const ra1(rev_ang);
	double const update_time(planet_update_rate*tfticks); // Note: use a double to minimize FP errors
	if ((animate2 || rot_ang == 0.0) && update_rot) {rot_ang = rot_ang0 + update_time*double(rot_rate);}
	if ((animate2 || rev_ang == 0.0) && update_rev) {rev_ang = rev_ang0 + update_time*double(rev_rate);}
	// compute absolute pos every update: stable over long time periods, but jittery due to fp error between frames
	// compute relative pos every update: unstable over time, but very smooth movement between frames
	point_d new_pos(v_orbit);
	rotate_vector3d(vector3d_d(rev_axis), rev_ang/TO_DEG, new_pos); // more accurate (is this necessary?)
	double orbit_radius(orbit);

	if (orbit_scale != all_ones) { // nonuniform/elliptical orbit scale (planet with rings)
		orbit_radius *= get_elliptical_orbit_radius(rev_axis, orbit_scale, new_pos); // Note: rev_axis is parent's rot_axis
		orbit_radius /= orbit_scale.xy_mag(); // normalize ellipse area
	}
	new_pos *= orbit_radius;
	new_pos += p0;
	pos      = new_pos;
	if (update_rev && int(10*ra1) != int(10*rev_ang)) {calc_temperature();} // update every 0.1 degree
	return new_pos;
}


template<typename T>
bool urev_body::create_orbit(vector<T> const &objs, int i, point const &pos0, vector3d const &raxis, float radius0,
							 float max_size, float min_size, float rspacing, float ispacing, float minspacing, float min_gap, vector3d const &oscale)
{
	radius = (min(0.4f*radius0, max_size) - min_size)*((float)rand2d()) + min_size;
	float const rad2(radius + rspacing), min_orbit(max((MIN_RAD_SPACE_FACTOR*(radius + radius0) + min_gap), minspacing));
	orbit_scale = oscale;
	rev_axis    = raxis + signed_rand_vector2_norm()*ORBIT_PLANE_DELTA;
	rev_axis.normalize();
	vector3d const start_vector(signed_rand_vector2_norm()); // doesn't matter, any will do
	cross_product(rev_axis, start_vector, v_orbit);
	v_orbit.normalize();
	bool too_close(1);
	unsigned counter;

	for (counter = 0; counter < MAX_TRIES && too_close; ++counter) {
		orbit     = rand_uniform2(min_orbit, ispacing);
		too_close = 0;

		for (int j = 0; j < i; ++j) { // slightly inefficient
			if (fabs(objs[j].orbit - orbit) < ORBIT_SPACE_MARGIN*(rad2 + objs[j].radius)) { // what about checking against objs[j].rad_spacing?
				too_close = 1; break;
			}
		}
	}
	if (too_close) return 0;
	create(0);
	do_update(pos0);
	create(1);
	return 1;
}


void urev_body::dec_orbiting_refs(s_object const &sobj) {

	assert(sobj.object == this);
	//assert(orbiting_refs > 0); // too strong - can fail if player leaves the galaxy and planets/moons are deleted (refs are reset)
	if (orbiting_refs >  0) {--orbiting_refs;}
	if (orbiting_refs == 0) {set_owner(sobj, NO_OWNER);}
}


// *** COLORS ***

void ustar::gen_color() {

	if (temp < 25.0) { // black: 0-25 (black hole)
		color = BLACK;
	}
	else if (temp < 30.0) { // deep red: 25-30
		color.assign(0.2*(temp - 25.0), 0.0, 0.0, 1.0);
	}
	else if (temp < 40.0) { // red-orange-yellow: 30-40
		color.assign(1.0, 0.1*(temp - 30.0), 0.0, 1.0);
	}
	else if (temp < 65.0) { // yellow-white: 40-65
		color.assign(1.0, 1.0, 0.04*(temp - 40.0), 1.0);
	}
	else if (temp < 75.0) { // white-blue: 65-75
		color.assign((0.6 + 0.05*(75.0  - temp)), (0.8 + 0.025*(75.0 - temp)), 1.0, 1.0);
	}
	else { // blueish: > 75
		color.assign(0.6, 0.8, 1.0);
	}
	color.set_valid_color();
	gen_colorAB(0.8*MP_COLOR_VAR);
	if (temp < 30.0) colorA.G = colorA.B = colorB.G = colorB.B = 0.0; // make sure it's just red
}


colorRGBA ustar::get_ambient_color_val() const {

	return (is_ok() ? colorRGBA(color.R, color.G, color.B, color.A)*sqrt(radius/STAR_MAX_SIZE) : BLACK);
}


colorRGBA ustar::get_light_color() const {

	//return color;
	// max the RGB for the two colors in each color channel to get a saturation effect
	return colorRGBA(max(colorA.R, colorB.R), max(colorA.G, colorB.G), max(colorA.B, colorB.B), color.A);
}


void uplanet::gen_color() {

	float const bright(rand_uniform2(0.5, 0.75));
	color.assign((0.75*bright + 0.40*rand2d()), (0.50*bright + 0.30*rand2d()), (0.25*bright + 0.15*rand2d()), 1.0);
	color.set_valid_color();
	
	if (has_vegetation()) { // override with Earth/terran colors (replace the above code?)
		colorA = colorRGBA(0.05, 0.35, 0.05, 1.0);
		colorB = colorRGBA(0.60, 0.45, 0.25, 1.0);
		adjust_colorAB(0.25*MP_COLOR_VAR);
		blend_color(color, colorA, colorB, 0.5, 0); // average the two colors
		ai_color = WHITE; // earth-like atmosphere colors
		ao_color = BLUE;
	}
	else {
		gen_colorAB(MP_COLOR_VAR);
		ai_color = colorA; // alien/toxic atmosphere colors
		ao_color = colorB;
	}
	if (!gas_giant) {
		if (water > 0.0) { // blend water with land for distant views?
			blend_color(color, ((temp < FREEZE_TEMP) ? P_ICE_C : P_WATER_C), color, water, 0);
		}
		if (atmos > 0.0) { // blend atmosphere with planet color for distant views
			blend_color(color, CLOUD_C, color, 0.25*atmos, 0);
		}
	}
	color.set_valid_color();
}


void umoon::gen_color() {
	
	float const brightness(rand_uniform2(0.5, 0.75));
	for (unsigned i = 0; i < 3; ++i) {color[i] = 0.75*brightness + 0.25*rand2d();}
	color.alpha = 1.0;
	color.set_valid_color();
	gen_colorAB(1.4*MP_COLOR_VAR);
}


void uobj_solid::adjust_colorAB(float delta) {

	for (unsigned i = 0; i < 3; ++i) {
		float const d(delta*rand2d());
		colorA[i] += d;
		colorB[i] -= d;
	}
	colorA.set_valid_color();
	colorB.set_valid_color();
}

void uobj_solid::gen_colorAB(float delta) {

	colorA = colorB = color;
	adjust_colorAB(delta);
}


// *** TEXTURES ***


void urev_body::check_gen_texture(unsigned size) {

	if (use_procedural_shader()) return; // no texture used
	if (size <= MIN_TEX_OBJ_SZ)  return; // too small to be textured

	if (gas_giant) { // perfectly spherical, no surface used
		if (!glIsTexture(tid)) {create_gas_giant_texture();} // texture has not been generated
		return;
	}
	unsigned const tsize0(get_texture_size(size));

	if (!glIsTexture(tid)) { // texture has not been generated
		gen_surface();
	}
	else if (tsize0 != tsize) { // new texture size
		::free_texture(tid); // delete old texture
	}
	else {
		return; // nothing to do
	}
	create_rocky_texture(tsize0); // new texture
}


void urev_body::create_rocky_texture(unsigned size) {

	tsize = size;
	assert(tsize <= MAX_TEXTURE_SIZE);
	vector<unsigned char> data(3*tsize*tsize);
	gen_texture_data_and_heightmap(&data.front(), tsize);
	setup_texture(tid, 0, 1, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, tsize, tsize, 0, GL_RGB, GL_UNSIGNED_BYTE, &data.front());
}


void urev_body::create_gas_giant_texture() {

	tsize = GAS_GIANT_TSIZE;
	vector<unsigned char> data(3*tsize);
	set_rseeds();
	colorRGBA color(colorA);
	
	for (unsigned i = 0; i < tsize; ++i) {
		if ((rand2() & GAS_GIANT_BANDS) == 0) {
			blend_color(color, colorA, colorB, rand_float2(), 0);
		}
		else {
			UNROLL_3X(color[i_] = CLIP_TO_01(color[i_] + 0.02f*signed_rand_float2());)
		}
		UNROLL_3X(data[3*i+i_] = (unsigned char)(255.0*color[i_]);)
	}
	bool const mipmap = 1; // Note: somewhat slow when the player is flying by quickly
	setup_1d_texture(tid, mipmap, 0, 0, 0);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB8, tsize, 0, GL_RGB, GL_UNSIGNED_BYTE, &data.front());
	if (mipmap) {gen_mipmaps(1);}
}


bool urev_body::use_procedural_shader() const {
	return (!gas_giant && water < 1.0 && (display_mode & 0x20) == 0); // for planets and moons
}


void urev_body::upload_colors_to_shader(shader_t &s) const {

	if (!use_procedural_shader()) return;
	bool const frozen(temp < FREEZE_TEMP);
	s.add_uniform_color("water_color",  (frozen ? P_ICE_C : P_WATER_C));
	s.add_uniform_color("color_a",      colorA);
	s.add_uniform_color("color_b",      colorB);
	s.add_uniform_float("temperature",  temp);
	s.add_uniform_float("snow_thresh",  snow_thresh);
	s.add_uniform_float("cold_scale",   (frozen ? 0.0 : 1.0)); // frozen ice planets don't use coldness (they're always cold)
	s.add_uniform_float("noise_offset", orbit); // use as a random seed
	s.add_uniform_float("terrain_scale",1.25*get_hmap_scale()); // divide terrain_scale by radius instead?
	s.add_uniform_float("nmap_mag",     CLIP_TO_01(2.0f - 0.2f*p2p_dist(pos, get_player_pos())/radius)); // 1.0 at dist < 5R, 0.0 at dist > 10R
}


void urev_body::get_surface_color(unsigned char *data, float val, float phi) const { // val in [0,1]

	bool const frozen(temp < FREEZE_TEMP);
	unsigned char const white[3] = {255, 255, 255};
	unsigned char const gray[3]  = {100, 100, 100};
	float const coldness(frozen ? 0.0 : fabs(phi - PI_TWO)*2.0*PI_INV); // phi=PI/2 => equator, phi=0.0 => north pole, phi=PI => south pole

	if (water >= 1.0 || val < water) { // underwater
		RGB_BLOCK_COPY(data, wic[frozen]);

		if (coldness > 0.8) { // ice
			float const blend_val(CLIP_TO_01(5.0f*(coldness - 0.8f) + 1.0f*(val - water)));
			BLEND_COLOR(data, white, data, blend_val);
		}
		return;
	}
	float const water_adj(0.07), val_ws((water > 0.0) ? wr_scale*(val - water) : val); // rescale for water

	if (water > 0.2 && atmos > 0.1) { // Earthlike planet
		if      (val_ws < 0.1)  {RGB_BLOCK_COPY(data, b);} // low ground
		else if (val_ws < 0.4)  {BLEND_COLOR(data, a, b, 3.3333*(val_ws - 0.1));}
		else if (val_ws < 0.45) {RGB_BLOCK_COPY(data, a);} // medium ground
		else if (val_ws < 0.6)  {BLEND_COLOR(data, gray, a, 6.6667*(val_ws - 0.45));}
		else                    {RGB_BLOCK_COPY(data, gray);} // high ground
	}
	else { // alien-like planet
		BLEND_COLOR(data, a, b, val_ws);
	}
	if (lava > 0.0) { // hot lava planet
		float const lava_adj(0.07);
		unsigned char const lavac[3] = {255, 0, 0}; // red
		if      (val < lava)            {RGB_BLOCK_COPY(data, lavac);} // move up?
		else if (val < lava + lava_adj) {BLEND_COLOR(data, data, lavac, (val - lava)/lava_adj);} // close to lava line
	}
	else if (temp < BOIL_TEMP) { // handle water/ice/snow
		if (val < water + water_adj) { // close to water line (can have a little water even if water == 0)
			BLEND_COLOR(data, data, wic[frozen], (val - water)/water_adj);
			
			if (coldness > 0.8) { // ice
				float const blend_val(CLIP_TO_01(5.0f*(coldness - 0.8f) + 1.0f*(val - water)));
				BLEND_COLOR(data, white, data, blend_val);
			}
		}
		else {
			float const st((1.0f - coldness*coldness)*snow_thresh);
			if (val > (st + 1.0E-6f)) {BLEND_COLOR(data, white, data, (val - st)/(1.0f - st));} // blend in some snow
		}
	}
}


void urev_body::calc_snow_thresh() {

	float const snow_temp(CLIP_TO_01(2.0f*((0.5f*FREEZE_TEMP + 0.5f*BOIL_TEMP) - temp))/(BOIL_TEMP - FREEZE_TEMP));
	float const snow_val(CLIP_TO_01(2.0f*(water - 0.05f))*snow_temp);
	snow_thresh = max(water, (1.0f - snow_val)); // ~0.9 to 1.0, where lower is more snow
}


void uobj_solid::get_colors(unsigned char ca[3], unsigned char cb[3]) const {

	for (unsigned d = 0; d < 3; ++d) {
		assert(colorA[d] >= 0.0 && colorA[d] <= 1.0);
		assert(colorB[d] >= 0.0 && colorB[d] <= 1.0);
	}
	unpack_color(ca, colorA);
	unpack_color(cb, colorB);
}


unsigned get_texture_size(float psize) {

	unsigned i;
	unsigned const ps2(unsigned(2.0*psize));

	for (i = 8; i < MAX_TEXTURE_SIZE; i <<= 1) {
		if (ps2 < i) return i;
	}
	return i;
}


void free_universe_context() {universe.free_context();}

void universe_t::free_context() { // should be OK even if universe isn't setup

	for (unsigned z = 0; z < U_BLOCKS; ++z) { // z
		for (unsigned y = 0; y < U_BLOCKS; ++y) { // y
			for (unsigned x = 0; x < U_BLOCKS; ++x) { // x
				ucell &cell(cells[z][y][x]);
				cell.free_context();
				if (cell.galaxies == NULL) continue;
				
				for (unsigned i = 0; i < cell.galaxies->size(); ++i) {
					ugalaxy &galaxy((*cell.galaxies)[i]);
					
					for (unsigned j = 0; j < galaxy.sols.size(); ++j) {
						ussystem &sol(galaxy.sols[j]);
						
						for (unsigned k = 0; k < sol.planets.size(); ++k) {
							uplanet &planet(sol.planets[k]);
							planet.free_texture();
							for (unsigned l = 0; l < planet.moons.size(); ++l) {planet.moons[l].free_texture();}
						}
					}
				}
			}
		}
	}
	planet_manager.clear();
}


// *** MEMORY - FREE CODE ***


void ucell::free_uobj() {

	gen = 0;

	if (galaxies != nullptr) {
		for (vector<ugalaxy>::iterator i = galaxies->begin(); i != galaxies->end(); ++i) {i->free_uobj();}
		galaxies.reset();
	}
}


void ugalaxy::clear_systems() {

	sols.clear();
	clusters.clear();
	asteroid_fields.clear();
}


void ugalaxy::free_uobj() {

	gen = 0;
	for (unsigned i = 0; i < sols.size(); ++i) {sols[i].free_uobj();}
	clear_systems();
}


void ussystem::free_planets() {

	for (unsigned i = 0; i < planets.size(); ++i) {planets[i].free_uobj();}
}


void ussystem::free_uobj() {

	gen = 0;
	if (!planets.empty()) {free_planets();}
	
	if (asteroid_belt) {
		asteroid_belt->free_uobj();
		asteroid_belt.reset();
	}
	planets.clear();
	sun.free_uobj();
	galaxy_color.alpha = 0.0; // set to an invalid state
}


void uplanet::free_uobj() {

	for (unsigned i = 0; i < moons.size(); ++i) {moons[i].free_uobj();}

	if (asteroid_belt) {
		asteroid_belt->free_uobj();
		asteroid_belt.reset();
	}
	moons.clear();
	ring_data.clear();
	urev_body::free_uobj();
}


void urev_body::free_uobj() {

	if (gen) free_texture();
	surface.reset(); // OK if already NULL
	//unset_owner();
	uobj_solid::free_uobj();
}


// *** DRAW CODE ***


void rotated_obj::apply_gl_rotate() const {
	rotate_from_v2v(rot_axis, plus_z);
	rotate_about(rot_ang, plus_z); // in degrees
}

void rotated_obj::rotate_vector(vector3d &v) const {
	rotate_norm_vector3d_into_plus_z(rot_axis, v);
	rotate_vector3d(plus_z, rot_ang/TO_DEG, v); // in radians
}

void rotated_obj::rotate_vector_inv(vector3d &v) const {
	rotate_vector3d(plus_z, -rot_ang/TO_DEG, v); // in radians
	rotate_norm_vector3d_into_plus_z(rot_axis, v, -1.0); // inverse rotate
}


void move_in_front_of_far_clip(point_d &pos, point const &camera, float &size, float dist, float dscale) {

	if (dist > 0.75*UNIV_FAR_CLIP) { // behind far clipping plane - move closer and scale
		float const pscale(UNIV_FAR_CLIP/(dscale*dist));
		size *= pscale;
		pos   = camera - (camera - pos)*pscale;
	}
}


bool ustar::draw(point_d pos_, ushader_group &usg, pt_line_drawer_no_lighting_t &star_pld, point_sprite_drawer &star_psd, bool distant, bool calc_flare_intensity) {

	point const &camera(get_player_pos()); // view frustum has already been checked
	vector3d const vcp(camera, pos_);
	float const vcp_mag(vcp.mag()), dist(vcp_mag - radius);
	if (dist > U_VIEW_DIST) return 0; // too far away
	float size(get_pixel_size(radius, dist)); // approx. in pixels
	if (size < 0.02) return 0; // too small
	float const st_prod(STAR_BRIGHTNESS*size*temp);
	if (st_prod < 2.0f || st_prod*temp < 4.0f) return 0; // too dim
	move_in_front_of_far_clip(pos_, camera, size, vcp_mag, 1.35);
	colorRGBA ocolor(color);

	if (st_prod < 30.0) {
		float const red_shift(0.03*(30.0 - st_prod));
		ocolor.R = min(1.0, ocolor.R*(1.0 + 1.0*red_shift));
		ocolor.G = max(0.0, ocolor.G*(1.0 - 0.3*red_shift));
		ocolor.B = max(0.0, ocolor.B*(1.0 - 0.7*red_shift));
		blend_color(ocolor, ocolor, bkg_color, 0.00111*st_prod*st_prod, 1); // small, attenuate (divide by 900)
	}
	if (size < 2.7) { // both point cases below, normal is camera->object vector
		pos_ = make_pt_global(pos_);

		if (size > 1.5) {
			star_psd.add_pt(vert_color(pos_, color)); // add size as well?
		}
		else if (!distant && get_draw_as_line(dist, vcp, vcp_mag)) { // lines of light - "warp speed"
			blend_color(ocolor, ocolor, bkg_color, 0.5, 1); // half color to make it less bright
			star_pld.add_line(pos_, ocolor, (pos_ - get_player_velocity()), ocolor); // lines are always one pixel wide
		}
		else {
			star_pld.add_pt(pos_, ocolor);
		}
	}
	else { // sphere
		int ndiv(max(4, min(56, int(0.5*NDIV_SIZE_SCALE*sqrt(size)))));
		if (ndiv > 16) {ndiv = ndiv & 0xFFFC;}
		if (world_mode != WMODE_UNIVERSE) {ndiv = max(4, ndiv/2);} // lower res when in background
		assert(ndiv > 0);
		fgPushMatrix();
		global_translate(pos_);
		uniform_scale(radius);
		float flare_intensity((size > 4.0) ? 1.0 : 0.0);

		if (flare_intensity > 0.0 && calc_flare_intensity) { // determine occlusion from planets
			unsigned const npts = 20;
			static point pts[npts];
			static bool pts_valid(0);
			unsigned nvis(0);
			line_query_state lqs;
		
			for (unsigned i = 0; i < npts; ++i) {
				if (!pts_valid) {pts[i] = signed_rand_vector_norm();}
				vector3d const tdir((pos_ + pts[i]*radius) - camera);
				float const tdist(tdir.mag());
				if (!universe_intersection_test(lqs, camera, tdir, 0.95*tdist, 0)) {++nvis;} // excludes asteroids
			}
			pts_valid = 1;
			flare_intensity = ((nvis == 0) ? 0 : (0.2 + 0.8*float(nvis)/float(npts)));
		}
		if (flare_intensity > 0.0) { // draw star's flares
			float const cfr(max(1.0, 60.0/size)), alpha(flare_intensity*CLIP_TO_01(0.5f*(size - 4.0f)));
			colorRGBA ca(colorA), cb(colorB);
			ca.A *= alpha; cb.A *= alpha;
			usg.enable_star_shader(ca, cb, radius, 1.0);
			enable_blend();
			set_additive_blend_mode();
			static quad_batch_draw qbd; // probably doesn't need to be static

			if (cfr > 1.5) {
				qbd.add_xlated_billboard(pos_, all_zeros, camera, up_vector, WHITE, 0.4*cfr, 0.4*cfr);
				qbd.draw_as_flares_and_clear(FLARE1_TEX);
				qbd.add_xlated_billboard(pos_, all_zeros, camera, up_vector, WHITE, 0.5, cfr);
				qbd.add_xlated_billboard(pos_, all_zeros, camera, up_vector, WHITE, cfr, 0.5);
				qbd.draw_as_flares_and_clear(BLUR_TEX);
			}
			if (size > 6.0) {
				usg.set_star_intensity(min(1.8f, 0.1f*size)); // variable intensity from 0.6 to 1.8
				qbd.add_xlated_billboard(pos_, all_zeros, camera, up_vector, WHITE, 4.0, 4.0);
				qbd.draw_as_flares_and_clear(BLUR_TEX);
			}
			set_std_blend_mode();
			disable_blend();
		}
		usg.enable_star_shader(colorA, colorB, radius, 1.0);
		select_texture(WHITE_TEX);
		draw_sphere_vbo(all_zeros, 1.0, ndiv, 0); // use vbo if ndiv is small enough
		if (world_mode == WMODE_UNIVERSE && size >= 64) {select_texture(BLUR_TEX); draw_flares(ndiv, 1);}
		usg.disable_star_shader();
		fgPopMatrix();
		if (size > 4.0) {maybe_show_uobj_info(this);}
	} // end sphere draw
	return 1;
}


bool urev_body::use_vert_shader_offset() const {
	if (atmos > 0.15) return 0; // doesn't work well for atmosphere
	static bool tess_enabled(1);
	if (tess_enabled && !check_for_tess_shader()) {tess_enabled = 0;} // disable tess - not supported
	return tess_enabled;
}

bool urev_body::draw(point_d pos_, ushader_group &usg, pt_line_drawer planet_plds[2], shadow_vars_t const &svars, bool use_light2, bool enable_text_tag) {

	point const &camera(get_player_pos());
	vector3d const vcp(camera, pos_);
	float const vcp_mag(vcp.mag()), dist(max(TOLERANCE, (vcp_mag - radius)));
	if (vcp_mag < TOLERANCE || dist > U_VIEW_DIST) return 0; // too close or too far away
	bool const universe_mode(world_mode == WMODE_UNIVERSE);
	float size(get_pixel_size(radius, dist)); // approx. in pixels
	if (size < 0.5 && !(display_mode & 0x01)) return 0; // too small
	if (size < 2.5 && !universe_mode)         return 0; // don't draw distant planets in combined_gu mode
	if (!univ_sphere_vis(pos_, radius))       return 1; // check if in the view volume
		
	if (universe_mode && !(display_mode & 0x01) && dist < UNIV_FAR_CLIP && is_owned()) { // owner color
		colorRGBA const owner_color(get_owner_color());
		usg.enable_color_only_shader(&owner_color);
		draw_sphere_vbo(make_pt_global(pos_), radius*max(1.2, 3.0/size), 8, 0); // at least 3 pixels
		usg.disable_color_only_shader();
		return 1;
	}
	move_in_front_of_far_clip(pos_, camera, size, vcp_mag, 1.35);
	colorRGBA ocolor(color);

	if (universe_mode && !(display_mode & 0x02)) {
		show_colonizable_liveable(pos_, radius, usg); // show liveable/colonizable planets/moons
	}
	if (size < 2.5) { // both point cases below, normal is camera->object vector
		if (size < 1.0) {blend_color(ocolor, ocolor, bkg_color, size*size, 1);} // small, attenuate
		planet_plds[size > 1.5].add_pt(make_pt_global(pos_), vcp/vcp_mag, ocolor); // orient towards camera
		return 1;
	}

	// calculate ndiv
	bool const texture(size > MIN_TEX_OBJ_SZ && tid > 0), procedural(use_procedural_shader()), heightmap(has_heightmap());
	float const ndiv_factor(heightmap ? (universe_mode ? 1.0 : 0.5) : 0.25); // lower res when in background
	int ndiv(ndiv_factor*NDIV_SIZE_SCALE*sqrt(size));
		
	if (size < 64.0) {
		ndiv = max(4, min(48, ndiv));
		if (ndiv > 16) {ndiv = ndiv & 0xFFFC;}
	}
	else {
		int const ndiv_max(SPHERE_MAX_ND/(heightmap ? 1 : 2));
		int const pref_ndiv(min(ndiv_max, ndiv));
		for (ndiv = 1; ndiv < pref_ndiv; ndiv <<= 1) {} // make a power of 2
		ndiv = min(ndiv, ndiv_max); // final clamp
	}
	assert(ndiv > 0);

	// draw as sphere
	// Note: the following line *must* be before the local transforms are applied as it captures the current MVM in the call
	if (texture || procedural) {usg.enable_planet_shader(*this, svars, use_light2);} // always WHITE
	fgPushMatrix();
	global_translate(pos_);
	apply_gl_rotate();
	
	if (procedural) {
		// nothing to do here
	}
	else if (texture) { // texture map
		assert(tid > 0);
		if (gas_giant) {bind_1d_texture(tid);} else {bind_2d_texture(tid);}
	}
	else {
		usg.enable_planet_colored_shader(use_light2, ocolor);
	}
	// Note: when the player approaches the planet very quickly, we can have ndiv > N_SPHERE_DIV, but the texture hasn't been generated yet,
	// so we must guard the ndiv check to avoid asserting on the next line (very rare)
	if (using_tess_shader || ((texture || procedural) && ndiv > N_SPHERE_DIV)) {
		assert(texture || procedural);
		glEnable(GL_CULL_FACE);

		if (heightmap) {
			assert(!using_tess_shader);
			draw_surface(pos_, size, ndiv);
		}
		else if (use_vert_shader_offset()) { // minor issue of face alignment cracks, but better triangle distribution
			assert(!gas_giant);
			set_multisample(0); // prevent artifacts at face boundaries
			assert(usg.planet_manager);
			usg.planet_manager->draw_sphere(using_tess_shader ? max(4, ndiv/16) : ndiv); // denser/more uniform vertex distribution for vertex/tess shader height mapping
			set_multisample(1);
		}
		else { // gas giant or atmosphere: don't need high resolution since they have no heightmaps
			assert(!using_tess_shader);
			draw_subdiv_sphere(all_zeros, 1.0, (use_vert_shader_offset() ? 2 : 1)*ndiv, zero_vector, NULL, int(gas_giant), 1); // no back-face culling
		}
		glDisable(GL_CULL_FACE);
	}
	else {
		if (surface != nullptr) {surface->clear_cache();} // only gets here when the object is visible
		if (!(texture || procedural)) {uniform_scale(radius);} // no push/pop required
		draw_sphere_vbo_raw(min(ndiv, 3*N_SPHERE_DIV/2), texture); // small sphere - use vbo - clamp to MAX_SPHERE_VBO_NDIV
	}
	if (texture || procedural) {usg.disable_planet_shader(*this, svars);} else {usg.disable_planet_colored_shader();}
	fgPopMatrix();
	if (enable_text_tag && size > 4.0) {maybe_show_uobj_info(this);}
	return 1;
}


vector<float> &get_empty_perturb_map(int ndiv) {

	static vector<float> perturb_map;
	perturb_map.resize(ndiv*(ndiv+1));
	return perturb_map;
}


void urev_body::draw_surface(point_d const &pos_, float size, int ndiv) {

	RESET_TIME;
	assert(ndiv > 0);
	assert(surface != nullptr);
	bool const SD_TIMETEST(SHOW_SPHERE_TIME && size >= 256.0);
	float const hmap_scale(get_hmap_scale());
	vector<float> &perturb_map(get_empty_perturb_map(ndiv));

	if (!surface->sd.equal(all_zeros, 1.0, ndiv)) {
		surface->free_context();
		float const cutoff(surface->min_cutoff), omcinv(surface->get_one_minus_cutoff());
		unsigned const ssize(surface->ssize);
		assert(ssize > 0 && tsize > 0);

		for (int i = 0; i < ndiv; ++i) { // maybe could skip this if perturb_map is the same?
			unsigned const iy((i*ssize)/ndiv), offset((ndiv+1)*i);

			for (int j = 0; j <= ndiv; ++j) {
				//unsigned const ix((j*ssize)/(ndiv+1));
				unsigned const ix((j == ndiv) ? (ssize-1) : (j*ssize)/ndiv);
				float const val(surface->heightmap[iy + ssize*ix]); // x and y are swapped
				perturb_map[j + offset] = hmap_scale*(omcinv*(max(cutoff, val) - cutoff) - 0.5f); // duplicated with urev_body::surface_test()
			}
		}
		surface->setup_draw_sphere(all_zeros, 1.0, -0.5*hmap_scale, ndiv, &perturb_map.front());
	}
	surface->sd.draw_ndiv_pow2_vbo(ndiv); // sphere heightmap for rocky planet or moon
	if (SD_TIMETEST) PRINT_TIME("Sphere Draw Fast");
}


void ustar::draw_flares(int ndiv, bool texture) {

	if (solar_flares.empty()) solar_flares.resize(4 + (rand()&3));
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	enable_blend();
	
	for (unsigned i = 0; i < solar_flares.size(); ++i) {
		if (animate2) {solar_flares[i].update(color);}
		solar_flares[i].draw(1.0, max(3, ndiv/4), texture); // Note: already translated by pos and scaled by radius
	}
	disable_blend();
	glDisable(GL_CULL_FACE);
}


void ustar::solar_flare::gen(colorRGBA const &color) {

	length   = rand_uniform(0.08, 0.16);
	radius   = rand_uniform(0.01, 0.04);
	angle    = rand_uniform(0.00, 360.0);
	time     = 0;
	lifetime = unsigned(rand_uniform(0.5, 2.0)*TICKS_PER_SECOND);
	dir      = signed_rand_vector().get_norm();
	color1   = colorRGBA((color.R + 0.25), color.G, (color.B - 0.1), 0.75*color.A);
	color1.set_valid_color();
	color2   = ALPHA0;
}


void ustar::solar_flare::update(colorRGBA const &color) {

	if (time >= lifetime) time = lifetime = 0;
	if (lifetime == 0) gen(color);
	if (lifetime == 0) return; // gen failed
	float const fticks_capped(min(fticks, 4.0f));
	time    = min(lifetime, (time + iticks));
	length += 0.02*length*fticks_capped;
	radius += 0.02*radius*fticks_capped;
	angle  += 0.2*fticks_capped;
	length  = min(length, 2.0f);
	radius  = min(radius, 0.5f);
}


void ustar::solar_flare::draw(float size, int ndiv, bool texture) const {

	if (lifetime == 0) return;
	assert(length > 0.0 && radius > 0.0 && size > 0.0);
	fgPushMatrix();
	rotate_about(angle, dir);
	draw_fast_cylinder(point(0.0, 0.0, 0.9*size), point(0.0, 0.0, (length + 0.9)*size), 0.1*size, 0.0, ndiv, texture);
	fgPopMatrix();
}


void urev_body::show_colonizable_liveable(point const &pos_, float radius0, ushader_group &usg) const {

	colorRGBA color;
	if      (liveable())    {color = GREEN;}
	else if (colonizable()) {color = RED;}
	else {return;}
	usg.enable_color_only_shader(&color);
	draw_sphere_vbo(make_pt_global(pos_), 1.2*radius0, 12, 0);
	usg.disable_color_only_shader();
}


void uplanet::ensure_rings_texture() {

	if (ring_data.empty() || ring_tid > 0) return; // no rings, or texture already created
	bool const mipmap = 1; // Note: somewhat slow when the player is flying by quickly
	setup_1d_texture(ring_tid, mipmap, 0, 0, 0);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, RING_TEX_SZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, &ring_data.front());
	if (mipmap) {gen_mipmaps(1);}
}

void uplanet::bind_rings_texture(unsigned tu_id) const { // setup ring texture so we can create ring shadows

	if (ring_data.empty() || ring_tid == 0) return;
	set_active_texture(2);
	bind_1d_texture(ring_tid);
	set_active_texture(0);
}

void uplanet::draw_prings(ushader_group &usg, upos_point_type const &pos_, float size_, float back_face_mult, bool a2c) const {

	if (ring_data.empty()) return;
	assert(ring_ri > 0.0 && ring_ri < ring_ro);
	assert(ring_tid > 0);
	bind_1d_texture(ring_tid);
	usg.enable_ring_shader(*this, make_pt_global(pos_), back_face_mult, a2c); // always WHITE
	//glDepthMask(GL_FALSE); // no depth writing - causes bright spots (rather than dark spots)
	fgPushMatrix();
	global_translate(pos_);
	rotate_into_plus_z(rot_axis); // rotate so that rot_axis is in +z
	scale_by(rscale);
	draw_tquad(ring_ro, ring_ro, 0.0);
	usg.disable_ring_shader();
	//glDepthMask(GL_TRUE);
	fgPopMatrix();
}


void uplanet::draw_atmosphere(ushader_group &usg, upos_point_type const &pos_, float size_, shadow_vars_t const &svars, float camera_dist_from_center) const {

	float const cloud_radius(PLANET_ATM_RSCALE*radius);
	usg.enable_atmospheric_shader(*this, make_pt_global(pos_), svars); // always WHITE
	fgPushMatrix();
	global_translate(pos_);
	apply_gl_rotate();
	uniform_scale(min(1.01f*cloud_radius, camera_dist_from_center));
	draw_sphere_vbo_raw(max(4, min(N_SPHERE_DIV, int(6.0*size_))), 1);
	fgPopMatrix();
	usg.disable_atmospheric_shader();
}


// *** PROCESSING/QUERY CODE ***


bool umoon::shadowed_by_planet() {

	assert(planet != NULL && planet->system != NULL);
	vector3d const v1(pos, planet->pos), v2(planet->pos, planet->system->sun.pos);
	float const dotp(dot_product(v1, v2));
	if (dotp < 0) return 0;
	float const dps(planet->orbit), rp(planet->radius), rs(planet->system->sun.radius);
	assert(orbit > TOLERANCE && dps > TOLERANCE);
	float const dx(orbit*sin(safe_acosf(dotp/(orbit*dps)))), rx(rp - (orbit/dps)*(rs - rp));
	return (dx < rx);
}


string uplanet::get_atmos_string() const {

	if (atmos == 0.0    ) {return "None";}
	if (gas_giant       ) {return "Dense Toxic";}
	if (atmos < 0.25    ) {return "Trace";}
	if (has_vegetation()) {return "Breathable";}
	return "Toxic";
}


string urev_body::get_info() const {

	ostringstream oss;
	oss << "Radius: " << radius << ", Temp: " << temp << ", Resources: " << resources << ", Water: " << water
		<< ", Atmos: " << atmos << " (" << get_atmos_string() << "), Vegetation: " << get_vegetation() << endl
		<< "Can Land: " << can_land() << ", Colonizable: " << colonizable()
		<< ", Liveable: " << liveable() << ", Satellites: " << num_satellites << comment;
	get_owner_info(oss, 0); // show_uninhabited=0 to reduce clutter
	if (population > 0) {oss << ", Population: " << unsigned(population) << "M";}
	return oss.str();
}


string ustar::get_info() const {

	ostringstream oss;
	oss << "Radius: " << radius << ", Temp: " << 100.0*temp << ", Planets: " << num_satellites;
	return oss.str();
}


// if not find_largest then find closest
int universe_t::get_closest_object(s_object &result, point pos, int max_level, bool include_asteroids,
	bool offset, float expand, bool get_destroyed, float g_expand, float r_add, int galaxy_hint) const
{
	float min_gdist(CELL_SIZE);
	if (offset) offset_pos(pos);
	point posc(pos);
	UNROLL_3X(posc[i_] += CELL_SIZEo2;)
	result.init();

	// find the correct cell
	point const cell_origin(cells[0][0][0].pos);
	UNROLL_3X(result.cellxyz[i_] = int((posc[i_] - cell_origin[i_])/CELL_SIZE);)
	
	if (bad_cell_xyz(result.cellxyz)) {
		UNROLL_3X(result.cellxyz[i_] = -1;)
		return 0;
	}
	ucell const &cell(get_cell(result.cellxyz));
	if (max_level == UTYPE_CELL ) {result.type = UTYPE_CELL; result.val =  1; return 1;} // cell
	if (cell.galaxies == nullptr) {result.type = UTYPE_CELL; result.val = -1; return 0;} // not yet generated
	pos -= cell.pos;
	float const planet_thresh(expand*4.0*MAX_PLANET_EXTENT + r_add), moon_thresh(expand*2.0*MAX_PLANET_EXTENT + r_add);
	float const pt_sq(planet_thresh*planet_thresh), mt_sq(moon_thresh*moon_thresh);
	static int last_galaxy(-1), last_cluster(-1), last_system(-1);
	int const first_galaxy_to_try((galaxy_hint >= 0) ? galaxy_hint : last_galaxy);
	unsigned const ng((unsigned)cell.galaxies->size());
	unsigned const go((first_galaxy_to_try >= 0 && first_galaxy_to_try < int(ng)) ? last_galaxy : 0);
	bool found_system(0);

	for (unsigned gc_ = 0; gc_ < ng && !found_system; ++gc_) { // find galaxy
		unsigned gc(gc_);
		if (gc == 0) {gc = go;} else if (gc == go) {gc = 0;}
		ugalaxy &galaxy((*cell.galaxies)[gc]);
		if (!galaxy.gen) continue; // not yet generated
		float const distg(p2p_dist(pos, galaxy.pos));
		if (distg > g_expand*(galaxy.radius + MAX_SYSTEM_EXTENT) + r_add) continue;
		float const galaxy_radius(galaxy.get_radius_at((pos - galaxy.pos)/max(distg, TOLERANCE)));
		if (distg > g_expand*(galaxy_radius + MAX_SYSTEM_EXTENT) + r_add) continue;

		if (max_level == UTYPE_GALAXY) { // galaxy
			if (distg < result.dist) {result.assign(gc, -1, -1, distg, UTYPE_GALAXY, NULL);}
			continue;
		}
		else if (result.object == NULL && distg < min_gdist) {
			result.galaxy = gc;
			result.type   = UTYPE_GALAXY;
			min_gdist     = distg;
		}
		if (include_asteroids) { // check for asteroid field collisions
			for (vector<uasteroid_field>::const_iterator i = galaxy.asteroid_fields.begin(); i != galaxy.asteroid_fields.end(); ++i) {
				if (!dist_less_than(pos, i->pos, expand*i->radius+r_add)) continue;

				// asteroid positions are dynamic, so spatial subdivision is difficult - we just do a slow linear iteration here
				for (uasteroid_field::const_iterator j = i->begin(); j != i->end(); ++j) {
					if (!dist_less_than(pos, j->pos, expand*j->radius+r_add)) continue;
					result.assign(gc, -1, -1, p2p_dist(pos, j->pos), UTYPE_ASTEROID, NULL);
					result.asteroid_field = (i - galaxy.asteroid_fields.begin());
					result.asteroid       = (j - i->begin());
				}
			}
		}
		unsigned const num_clusters((unsigned)galaxy.clusters.size());
		unsigned const co((last_cluster >= 0 && last_cluster < int(num_clusters) && gc == go) ? last_cluster : 0);

		for (unsigned cl_ = 0; cl_ < num_clusters && !found_system; ++cl_) { // find cluster
			unsigned cl(cl_);
			if (cl == 0) cl = co; else if (cl == co) cl = 0;
			ugalaxy::system_cluster const &cluster(galaxy.clusters[cl]);
			float const testval(expand*cluster.bounds + r_add);
			if (p2p_dist_sq(pos, cluster.center) > testval*testval) continue;
			unsigned const cs1(cluster.s1), cs2(cluster.s2);
			unsigned const so((last_system >= int(cs1) && last_system < int(cs2) && cl == co) ? last_system : cs1);

			for (unsigned s_ = cs1; s_ < cs2 && !found_system; ++s_) {
				unsigned s(s_);
				if (s == cs1) s = so; else if (s == so) s = cs1;
				ussystem &system(galaxy.sols[s]);
				assert(system.cluster_id == cl); // testing
				float const dists_sq(p2p_dist_sq(pos, system.pos)), testval2(expand*(system.radius + MAX_PLANET_EXTENT) + r_add);
				if (dists_sq > testval2*testval2) continue;
				float dists(sqrt(dists_sq));
				found_system = (expand <= 1.0 && dists < system.radius);
				
				if (system.sun.is_ok() || get_destroyed) {
					dists -= system.sun.radius;

					if (dists < result.dist) {
						result.assign(gc, cl, s, dists, UTYPE_SYSTEM, &system.sun);

						if (dists <= 0.0) { // sun collision
							result.val = 2; return 2; // system
						}
					}
				}
				if (max_level == UTYPE_SYSTEM || max_level == UTYPE_STAR) continue; // system/star

				if (include_asteroids && system.asteroid_belt != nullptr) { // check for asteroid belt collisions
					if (system.asteroid_belt->sphere_might_intersect(pos, expand*system.asteroid_belt->get_max_asteroid_radius()+r_add)) {
						// asteroid positions are dynamic, so spatial subdivision is difficult - we just do a slow linear iteration here
						for (uasteroid_field::const_iterator j = system.asteroid_belt->begin(); j != system.asteroid_belt->end(); ++j) {
							if (!dist_less_than(pos, j->pos, expand*j->radius+r_add)) continue;
							result.assign(gc, cl, s, p2p_dist(pos, j->pos), UTYPE_ASTEROID, NULL);
							result.asteroid_field = AST_BELT_ID; // special asteroid belt identifier
							result.asteroid       = (j - system.asteroid_belt->begin());
						}
					}
				}
				unsigned const np((unsigned)system.planets.size());
				
				for (unsigned pc = 0; pc < np; ++pc) { // find planet
					uplanet &planet(system.planets[pc]);
					float distp_sq(p2p_dist_sq(pos, planet.pos));
					if (distp_sq > pt_sq) continue;
					float const distp(sqrt(distp_sq) - planet.radius);
					//if (include_asteroids && planet.asteroid_belt != nullptr) {}
					
					if (planet.is_ok() || get_destroyed) {
						if (distp < result.dist) {
							result.assign(gc, cl, s, distp, UTYPE_PLANET, &planet);
							result.planet = pc;

							if (distp <= 0.0) { // planet collision
								result.val = 2; return 2;
							}
						}
					}
					if (max_level == UTYPE_PLANET) continue; // planet
					unsigned const nm((unsigned)planet.moons.size());
					
					for (unsigned mc = 0; mc < nm; ++mc) { // find moon
						umoon &moon(planet.moons[mc]);
						if (!moon.is_ok() && !get_destroyed) continue;
						float const distm_sq(p2p_dist_sq(pos, moon.pos));
						if (distm_sq > mt_sq)                continue;
						float const distm(sqrt(distm_sq) - moon.radius);

						if (distm < result.dist) {
							result.assign(gc, cl, s, distm, UTYPE_MOON, &moon);
							result.planet = pc;
							result.moon   = mc;

							if (distm <= 0.0) { // moon collision
								result.val = 1; return 2;
							}
						}
					} // moon
				} // planet
			} // system
		} // cluster
	} // galaxy
	result.val = ((result.dist < CELL_SIZE) ? 1 : -1);
	if (result.galaxy  >= 0) {last_galaxy  = result.galaxy; }
	if (result.cluster >= 0) {last_cluster = result.cluster;}
	if (result.system  >= 0) {last_system  = result.system; }
	return (result.val == 1);
}


void check_asteroid_belt_coll(std::shared_ptr<uasteroid_belt> asteroid_belt, point const &curr, vector3d const &dir, float dist, float line_radius,
	int cix, int six, int pix, s_object &result, point &coll, float &ctest_dist, float &asteroid_dist, float &ldist)
{
	if (!asteroid_belt) return;
	if (!asteroid_belt->line_might_intersect(curr, (curr + dist*dir), line_radius)) return;
		
	for (vector<uasteroid>::const_iterator a = asteroid_belt->begin(); a != asteroid_belt->end(); ++a) {
		if (!pt_line_dir_dist_less_than(a->pos, curr, dir, (a->radius + line_radius))) continue;

		if (a->line_intersection(curr, dir, ((ctest_dist == 0.0) ? dist : ctest_dist), line_radius, ldist)) {
			result.assign_asteroid(ldist, (a - asteroid_belt->begin()), AST_BELT_ID);
			if (pix >= 0) {result.planet = pix;}
			result.system  = six;
			result.cluster = cix;
			asteroid_dist  = ldist;
			ctest_dist     = ldist;
			coll           = a->pos;
		}
	} // for a
}


bool universe_t::get_trajectory_collisions(line_query_state &lqs, s_object &result, point &coll, vector3d dir, point start,
	float dist, float line_radius, bool include_asteroids) const
{
	int cell_status(1), cs[3] = {};
	float rdist(0.0), ldist(0.0), t(0.0);
	vector3d c1, c2, val, tv, step;

	// check for simple point
	if (dist <= 0.0) {
		int const ival(get_closest_object(result, start, UTYPE_MOON, 1, 1, 1.0));

		if (ival == 2 || (ival == 1 && result.dist <= line_radius)) { // collision at start
			coll = start; return 1;
		}
		coll.assign(0.0, 0.0, 0.0);
		return 0;
	}

	// offset start point
	offset_pos(start);

	// get end point
	dir.normalize();
	point end(start + dir*dist);

	// calculate cell block boundaries
	point const p_low(cells[0][0][0].pos), p_hi(cells[U_BLOCKS-1][U_BLOCKS-1][U_BLOCKS-1].pos);

	for (unsigned d = 0; d < 3; ++d) {
		c1[d] = p_low[d] - CELL_SIZEo2;
		c2[d] = p_hi[d]  + CELL_SIZEo2;
		if (start[d] <= c1[d] || start[d] >= c2[d]) return 0; // start is out of the cell block
	}

	// clip line by cell block boundaries (might be unneccessary due to the above check)
	float const cd[3][2] = {{c1.x, c2.x}, {c1.y, c2.y}, {c1.z, c2.z}};
	if (!do_line_clip(start, end, cd)) return 0; // out of cell block

	// make sure it's inside the cell block
	start += dir*RANGE_OFFSET;
	end   -= dir*RANGE_OFFSET;

	{ // check for start point collision (slow but important)
		bool const fast_test(1);
		int const ival(get_closest_object(result, start, (fast_test ? (int)UTYPE_CELL : (int)UTYPE_MOON), !fast_test, 0, 1.0));
		if (ival == 0 && result.val == 0) {coll.assign(0.0, 0.0, 0.0); return 0;} // not even inside a valid cell (is this possible? error?)

		if (!fast_test && (ival == 2 || (ival == 1 && result.dist <= line_radius))) { // collision at start
			coll = start;
			return 1;
		}
	}
	coll.assign(0.0, 0.0, 0.0);
	float T(0.0);

	// get points referenced to current cell
	ucell const *cell(&get_cell(result));
	point p0(start);
	start -= cell->pos;
	end   -= cell->pos;
	point curr(start);
	vector3d const dv(end, start);

	for (unsigned d = 0; d < 3; ++d) {
		if (dir[d] > 0.0) {cs[d] = 1; val[d] = c2[d];} else {cs[d] = -1; val[d] = c1[d];} // determine cell step direction
		step[d] = CELL_SIZE*((float)cs[d]); // determine line distance parameters
	}
	coll_test ctest;
	vector<coll_test> &gv(lqs.gv), &sv(lqs.sv), &pv(lqs.pv), &av(lqs.av);
	gv.resize(0);
	sv.resize(0);
	pv.resize(0);

	// main loop
	while (T <= 1.0) {
		if (cell_status == 0) { // search for next cell
			// find t values
			for (unsigned d = 0; d < 3; ++d) {
				if (dv[d] == 0) {tv[d] = 2.0;} else {tv[d] = (val[d] - start[d])/dv[d];}
			}
			// determine closest edge and update t and cell
			unsigned const dim((tv.x < tv.y) ? ((tv.x < tv.z) ? 0 : 2) : ((tv.y < tv.z) ? 1 : 2)); // smallest dim
			T = tv[dim];
			result.cellxyz[dim] += cs[dim];
			val[dim] += step[dim];
			if (T > 1.0)           return 0; // out of cells
			if (result.bad_cell()) return 0; // off the array
			cell = &get_cell(result);
			curr = p0 - cell->pos;
		}
		if (cell->galaxies == nullptr) return 0; // galaxies not yet allocated
		cell_status   = 0;
		result.galaxy = result.cluster = result.system = result.planet = result.moon = -1;
		vector<ugalaxy> &galaxies(*cell->galaxies);
		assert(galaxies.size() <= MAX_GALAXIES_PER_CELL); // just a consistency check

		for (unsigned i = 0; i < galaxies.size(); ++i) { // search for galaxies
			float const g_radius(galaxies[i].radius + MAX_SYSTEM_EXTENT);
			if (!dist_less_than(curr, galaxies[i].pos, (g_radius + dist))) continue;

			if (line_intersect_sphere(curr, dir, galaxies[i].pos, (g_radius+line_radius), rdist, ldist, t)) {
				ctest.index = i; // line passes through galaxy
				ctest.dist  = ldist;
				gv.push_back(ctest);
			}
		}
		std::sort(gv.begin(), gv.end());

		for (unsigned gc = 0; gc < gv.size(); ++gc) {
			ugalaxy &galaxy(galaxies[gv[gc].index]);
			if (!galaxy.gen) continue; // not yet generated

			if (include_asteroids) { // asteroid fields
				for (vector<uasteroid_field>::const_iterator i = galaxy.asteroid_fields.begin(); i != galaxy.asteroid_fields.end(); ++i) {
					if (!dist_less_than(curr, i->pos, (i->radius + dist))) continue;

					if (line_intersect_sphere(curr, dir, i->pos, (i->radius+line_radius), rdist, ldist, t)) {
						ctest.index = i - galaxy.asteroid_fields.begin(); // line passes through asteroid field
						ctest.dist  = ldist;
						av.push_back(ctest); // line passes through asteroid field
					}
				}
				std::sort(av.begin(), av.end());
				ctest.dist = 0.0;

				for (unsigned ac = 0; ac < av.size(); ++ac) {
					uasteroid_field const &af(galaxy.asteroid_fields[av[ac].index]);

					for (vector<uasteroid>::const_iterator i = af.begin(); i != af.end(); ++i) {
						if (!pt_line_dir_dist_less_than(i->pos, curr, dir, (i->radius + line_radius))) continue;

						if (i->line_intersection(curr, dir, ((ctest.dist == 0.0) ? dist : ctest.dist), line_radius, ldist)) {
							result.assign_asteroid(ldist, (i - af.begin()), av[ac].index);
							ctest.dist = ldist;
							coll       = i->pos;
						}
					} // for i
				} // for ac
				av.resize(0);
			}
			float asteroid_dist(ctest.dist);

			// clusters
			for (unsigned c = 0; c < galaxy.clusters.size(); ++c) {
				ugalaxy::system_cluster const &cl(galaxy.clusters[c]);
				if (!dist_less_than(curr, cl.center, (cl.bounds + dist))) continue;
				if (!line_intersect_sphere(curr, dir, cl.center, (cl.bounds+line_radius), rdist, ldist, t)) continue;
				
				for (unsigned i = cl.s1; i < cl.s2; ++i) { // search for systems
					float const s_radius(galaxy.sols[i].radius + MAX_PLANET_EXTENT);
					if (!dist_less_than(curr, galaxy.sols[i].pos, (s_radius + dist))) continue;

					if (line_intersect_sphere(curr, dir, galaxy.sols[i].pos, (s_radius+line_radius), rdist, ldist, t)) {
						ctest.index = i; // line passes through system
						ctest.dist  = ldist;
						ctest.rad   = rdist;
						ctest.t     = t;
						sv.push_back(ctest);
					}
				}
			}
			std::sort(sv.begin(), sv.end());
			result.galaxy = gv[gc].index;

			// systems
			for (unsigned sc = 0; sc < sv.size(); ++sc) {
				ussystem &system(galaxy.sols[sv[sc].index]);
				
				if (include_asteroids && system.asteroid_belt != nullptr) {
					check_asteroid_belt_coll(system.asteroid_belt, curr, dir, dist, line_radius, system.cluster_id, sv[sc].index,
						-1, result, coll, ctest.dist, asteroid_dist, ldist);
				}
				if (system.sun.is_ok() && sv[sc].rad <= system.sun.radius && sv[sc].t > 0.0 && sv[sc].dist <= dist) {
					if (asteroid_dist == 0.0 || sv[sc].dist < asteroid_dist) { // asteroid is not closer
						ctest.index = -1;
						ctest.dist  = sv[sc].dist;
						ctest.rad   = sv[sc].rad;
						ctest.t     = t;
						pv.push_back(ctest); // line intersects sun
					}
				}
				for (unsigned i = 0; i < system.planets.size(); ++i) { // search for planets
					uplanet &planet(system.planets[i]);
					float const p_radius(planet.mosize);
					if (!dist_less_than(curr, planet.pos, (p_radius + dist))) continue;
					
					if (include_asteroids && planet.asteroid_belt != nullptr) {
						check_asteroid_belt_coll(planet.asteroid_belt, curr, dir, dist, line_radius, system.cluster_id, sv[sc].index,
							i, result, coll, ctest.dist, asteroid_dist, ldist);
					}
					// FIXME: test against exact planet contour?
					if (line_intersect_sphere(curr, dir, planet.pos, (p_radius+line_radius), rdist, ldist, t)) {
						if (asteroid_dist > 0.0 && ldist > asteroid_dist) continue; // asteroid is closer
						ctest.index = i;
						ctest.dist  = ldist;
						ctest.rad   = rdist;
						ctest.t     = t;
						pv.push_back(ctest); // line passes through planet orbit
					}
				}
				if (pv.empty()) continue; // no intersecting planets
				std::sort(pv.begin(), pv.end());
				result.system  = sv[sc].index;
				result.cluster = system.cluster_id;

				for (unsigned pc = 0; pc < pv.size(); ++pc) {
					if (pv[pc].index < 0) { // sun is closer
						result.dist   = pv[pc].dist;
						result.object = &(system.sun);
						result.type   = UTYPE_STAR;
						coll          = system.sun.pos; // center, not collision point, fix?
						return 1;
					}
					uplanet &planet(system.planets[pv[pc].index]);

					if (planet.is_ok() && pv[pc].rad <= planet.radius && pv[pc].t > 0.0 && pv[pc].dist <= dist) {
						ctest.index = -1; // line intersects planet
						ctest.dist  = pv[pc].dist;
					}
					else {
						ctest.dist = 2.0*dist;
					}
					for (unsigned i = 0; i < planet.moons.size(); ++i) { // search for moons
						umoon const &moon(planet.moons[i]);

						if (moon.is_ok()) {
							float const m_radius(moon.radius);
							if (!dist_less_than(curr, moon.pos, (m_radius + dist))) continue;

							// FIXME: test against exact moon contour?
							if (line_intersect_sphere(curr, dir, moon.pos, (m_radius+line_radius), rdist, ldist, t)) {
								if (t > 0.0 && ldist <= dist && ldist < ctest.dist) {
									ctest.index = i; // line intersects moon
									ctest.dist  = ldist;
								}
							}
						}
					}
					if (ctest.dist > dist) continue;
					result.planet = pv[pc].index;
					result.dist   = ctest.dist;

					if (ctest.index < 0) { // planet is closer
						result.object = &planet;
						result.type   = UTYPE_PLANET;
					}
					else { // moon is closer
						result.moon   = ctest.index;
						result.object = &(planet.moons[ctest.index]);
						result.type   = UTYPE_MOON;
					}
					coll = result.object->pos; // center, not collision point, fix?
					return 1;
				} // pc
				pv.resize(0);
			} // sc
			sv.resize(0);
			if (result.type == UTYPE_ASTEROID) {return 1;} // asteroid intersection
		} // gc
		gv.resize(0);
	} // while t
	return 0;
}


float get_temp_in_system(s_object const &clobj, point const &pos, point &sun_pos) {

	assert(clobj.system >= 0);
	ussystem const &system(clobj.get_system());
	ustar const &sun(system.sun);
	if (!sun.is_ok()) return 0.0; // sun is dead
	sun_pos = sun.pos;
	point pos2(pos);
	offset_pos(pos2);
	float const sr(sun.radius), rdist_sq(p2p_dist_sq((pos2 - clobj.get_ucell().pos), system.pos));
	return ((rdist_sq < sr*sr) ? 4.0f : 1.0f)*sun.get_energy()*min(1.0f/rdist_sq, 10.0f/(sr*sr)); // Note: same as sun.get_temperature_at_pt(pos), unless inside the sun
}


float universe_t::get_point_temperature(s_object const &clobj, point const &pos, point &sun_pos) const {

	if (clobj.system >= 0) {return get_temp_in_system(clobj, pos, sun_pos);} // existing system is valid
	s_object result; // invalid system - expand the search radius and try again
	if (!get_closest_object(result, pos, UTYPE_SYSTEM, 0, 1, 4.0, 0, 1.0, 0.0, clobj.galaxy) || result.system < 0) return 0.0;
	return get_temp_in_system(result, pos, sun_pos);
}


bool get_gravity(s_object &result, point pos, vector3d &gravity, int offset) {

	gravity.assign(0.0, 0.0, 0.0);
	if (result.val != 1) return 0; // only if in the cell block but not collided
	if (result.bad_cell() || result.galaxy < 0 || result.system < 0) return 0;
	if (offset) offset_pos(pos);
	ussystem const &system(result.get_system());
	pos -= result.get_ucell().pos;
	system.sun.add_gravity_vector(gravity, pos); // add sun's gravity
	
	for (unsigned i = 0; i < system.planets.size(); ++i) { // add planets' gravity
		system.planets[i].add_gravity_vector(gravity, pos);
	}
	if (result.planet >= 0) {
		uplanet const &planet(result.get_planet());

		for (unsigned i = 0; i < planet.moons.size(); ++i) { // add moons' gravity
			planet.moons[i].add_gravity_vector(gravity, pos);
		}
	}
	return 1;
}


inline void uobj_solid::set_grav_mass() {

	gravity = radius*density;
	mass    = MASS_SCALE*gravity*radius*radius; // used to be f(r^2) not f(r^3)
}


// *** PARAMS CODE ***


void set_sun_loc_color(point const &pos, colorRGBA const &color, float radius, bool shadowed, bool no_ambient, float a_scale, float d_scale, shader_t *shader) {

	colorRGBA uambient, udiffuse;
	int const light(0);
	point const lpos(make_pt_global(pos));
	float const ambient_scale(a_scale*get_univ_ambient_scale());

	// set color
	for (unsigned i = 0; i < 3; ++i) {
		float const ci(WHITE_COMP_D + OM_WCD*color[i]);
		uambient[i]  = (no_ambient ? 0.0f : ambient_scale*color[i]);
		udiffuse[i]  = (shadowed   ? 0.0f : d_scale*ci);
		sun_color[i] = ci;
	}
	uambient[3] = (no_ambient ? 0.0f : 1.0f);
	udiffuse[3] = (shadowed   ? 0.0f : 1.0f);
	set_colors_and_enable_light(light, uambient, udiffuse, shader);
	// set position
	set_gl_light_pos(light, lpos, 1.0, shader); // point light source
	// set light attenuation - cache this?
	set_star_light_atten(light, max(0.25f, min(1.0f, STAR_MAX_SIZE/radius)), shader);
}


void set_light_galaxy_ambient_only(shader_t *shader) {
	clear_colors_and_disable_light(0, shader); // need to zero it out as well, since shaders ignore light enable state
}


void set_universe_ambient_color(colorRGBA const &color, shader_t *shader) {

	int const light(get_universe_ambient_light(1)); // always for universe mode
	enable_light(light);
	colorRGBA ambient(BLACK);
	UNROLL_3X(ambient[i_] = universe_ambient_scale*GLOBAL_AMBIENT*BASE_AMBIENT*(WHITE_COMP_A + OM_AAV*OM_WCA*color[i_]);)
	set_light_a_color(light, ambient, shader);
	setup_gl_light_atten(light, 1.0, 0.0, 0.0, shader);
}


void set_universe_lighting_params(bool for_universe_draw) { // called for both universe and ground mode

	int const a_light(get_universe_ambient_light(for_universe_draw)), s_light(0);
	set_colors_and_enable_light(s_light, GRAY, WHITE); // single star diffuse + ambient
	set_gl_light_pos(s_light, all_zeros, 0.0); // directional light
	set_colors_and_enable_light(a_light, GRAY, BLACK); // universe + galaxy ambient
	set_gl_light_pos(a_light, all_zeros, 0.0); // directional light
}


// ****************** CLASS HIERARCHY CODE **********************


void uobject::explode(float damage, float bradius, int etype, vector3d const &edir, int exp_time, int wclass, int align, unsigned eflags, free_obj const *parent_) {

	if (!is_ok()) return;
	status = 1;
	assert(etype < NUM_ETYPES);
	if (etype == ETYPE_NONE) return;
	assert(bradius > 0.0);
	exp_type_params const &ep(et_params[etype]);
	int const etime((exp_time == 0) ? int(ep.duration*max(6, min(20, int(3.0*bradius/radius)))) : exp_time);
	add_blastr(pos, edir, bradius, damage, etime, align, ep.c1, ep.c2, etype, parent_);
	if (damage > 0.0) register_explosion(pos, bradius, damage, eflags, wclass, this, parent_); // delay one frame
}

void uobject::def_explode(float size, int etype, vector3d const &edir, int wclass, int align, unsigned eflags, free_obj const *parent_, float dscale) {
	
	explode(EXPLOSION_DAMAGE*dscale*size*radius, size*radius, etype, edir, 0, wclass, align, eflags, parent_);
	//assert(!is_ok()); // not true for comets, or anything else that respawns
}


void uobject::add_gravity_vector_base(vector3d &vgravity, point const &mpos, float gfactor, float gmax) const {

	if (!is_ok()) return;
	vector3d const dir(pos - mpos);
	float const dmag(dir.mag()), dist(max(dmag, radius));
	// this is the gravity acceleration - multiply by object mass to get force
	vgravity += dir*(min(gfactor/(dist*dist), gmax)/dist);
}


float ustar::get_temperature_at_pt(point const &pt) const {return get_temperature_at_dist_sq(p2p_dist_sq(pos, pt));}

vector3d ustar::get_solar_wind_accel(point const &obj_pos, float obj_mass, float obj_surf_area) const {

	assert(obj_mass > 0.0 && obj_surf_area > 0.0);
	if (!is_ok()) {return zero_vector;}
	vector3d const dir_from_sun(obj_pos - pos);
	float const mag_sq(dir_from_sun.mag_sq());
	if (mag_sq < TOLERANCE) {return zero_vector;}
	return dir_from_sun.get_norm()*(get_energy()*obj_surf_area/(obj_mass*mag_sq));
}


void urev_body::free_texture() { // and also free vbos

	if (surface != nullptr) {surface->free_context();}
	::free_texture(tid);
	tsize = 0;
}


void uplanet::free_texture() {

	urev_body::free_texture();
	::free_texture(ring_tid);
}


void urev_body::explode(float damage, float bradius, int etype, vector3d const &edir, int exp_time, int wclass,
						int align, unsigned eflags, free_obj const *parent_)
{
	gen_fragments();
	uobject::explode(damage, bradius, etype, edir, exp_time, wclass, align, eflags, parent_);
}


// is this really OS/machine independent (even 32-bit vs. 64-bit)?
void uobj_rgen::gen_rseeds() {
	rgen.rseed1 = rand2();
	rgen.rseed2 = rand2();
}

void uobj_rgen::get_rseeds() {rgen = global_rand_gen;}
void uobj_rgen::set_rseeds() const {global_rand_gen = rgen;}


// s_object


bool s_object::write(ostream &out) const {

	if (!out.good()) return 0;
	out << type << " " << cellxyz[0] << " " << cellxyz[1] << " " << cellxyz[2] << " "
		<< galaxy << " " << cluster << " " << system << " " << planet << " " << moon << " " << id;
	return out.good();
}


bool s_object::read(istream &in) {

	if (!in.good()) return 0;
	return ((in >> type >> cellxyz[0] >> cellxyz[1] >> cellxyz[2] >> galaxy >> cluster >> system >> planet >> moon >> id) && in.good());
}


void s_object::init() {

	dist   = CELL_SIZE;
	galaxy = cluster = system = planet = moon = asteroid_field = asteroid = -1;
	type   = UTYPE_NONE;
	val    = id = 0;
	object = NULL;
	cellxyz[0] = cellxyz[1] = cellxyz[2] = 0;
}


void s_object::assign(int gc, int cl, int sy, float di, int ty, uobj_solid *obj) {

	galaxy  = gc;
	cluster = cl;
	system  = sy;
	dist    = di;
	type    = ty;
	object  = obj;
}


bool s_object::operator<(const s_object &I) const {

	if (type < I.type) return 1;
	if (type > I.type) return 0;
	if (type == UTYPE_NONE) return 0;
	if (cellxyz[0] < I.cellxyz[0]) return 1;
	if (cellxyz[0] > I.cellxyz[0]) return 0;
	if (cellxyz[1] < I.cellxyz[1]) return 1;
	if (cellxyz[1] > I.cellxyz[1]) return 0;
	if (cellxyz[2] < I.cellxyz[2]) return 1;
	if (cellxyz[2] > I.cellxyz[2]) return 0;
	if (type == UTYPE_CELL) return 0;
	if (galaxy < I.galaxy)  return 1;
	if (galaxy > I.galaxy)  return 0;
	if (type == UTYPE_GALAXY) return 0;
	if (cluster < I.cluster)  return 1;
	if (cluster > I.cluster)  return 0; // maybe unnecessary
	if (system < I.system)    return 1;
	if (system > I.system)    return 0;
	if (type == UTYPE_SYSTEM || type == UTYPE_STAR) return 0;
	if (planet < I.planet)    return 1;
	if (planet > I.planet)    return 0;
	if (type == UTYPE_PLANET) return 0;
	if (moon   < I.moon)      return 1;
	if (moon   > I.moon)      return 0;
	if (type == UTYPE_MOON)   return 0;
	return (id < I.id);
}


void s_object::print() const {

	cout << "type: " << type << ", cell: " << cellxyz[0] << ", " << cellxyz[1] << ", " << cellxyz[2] << ", galaxy: "
		 << galaxy << ", cluster: " << cluster << ", system: " << system << ", planet = " << planet << ", moon = "
		 << moon << ", id = " << id << endl;
}

bool s_object::bad_cell() const {
	return universe.bad_cell_xyz(cellxyz);
}

ucell &s_object::get_ucell() const {
	assert(type >= UTYPE_CELL);
	return universe.get_cell(*this);
}

uasteroid_field &s_object::get_asteroid_field() const {
	assert(type == UTYPE_ASTEROID);
	ugalaxy &g(get_galaxy());
	assert(asteroid_field >= 0);
	assert((unsigned)asteroid_field < g.asteroid_fields.size());
	return g.asteroid_fields[asteroid_field];
}

uasteroid_belt &s_object::get_asteroid_belt() const {
	assert(asteroid_field == AST_BELT_ID);
	assert(asteroid >= 0);
	
	if (planet >= 0) { // planet asteroid belt
		uplanet &planet(get_planet());
		assert(planet.asteroid_belt != nullptr);
		assert((unsigned)asteroid < planet.asteroid_belt->size());
		return *planet.asteroid_belt;
	}
	else { // system asteroid belt
		ussystem &system(get_system());
		assert(system.asteroid_belt != nullptr);
		assert((unsigned)asteroid < system.asteroid_belt->size());
		return *system.asteroid_belt;
	}
}

uasteroid &s_object::get_asteroid() const {
	if (asteroid_field == AST_BELT_ID) {return get_asteroid_belt()[asteroid];} // special identifier for system asteroid belt
	uasteroid_field &af(get_asteroid_field());
	assert(asteroid >= 0);
	assert((unsigned)asteroid < af.size());
	return af[asteroid];
}

void s_object::destroy_asteroid() const {
	assert(type == UTYPE_ASTEROID);

	if (asteroid_field == AST_BELT_ID) { // special identifier for system asteroid belt
		get_asteroid_belt().destroy_asteroid(asteroid);
	}
	else {
		get_asteroid_field().destroy_asteroid(asteroid);
	}
}


