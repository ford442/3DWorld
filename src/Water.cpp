// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari and Hiral Patel
// 3/15/02

#include "mesh.h"
#include "spillover.h"
#include "physics_objects.h"
#include "openal_wrap.h"
#include "shaders.h"
#include "gl_ext_arb.h"


float    const RIPPLE_DAMP1        = 0.95;
float    const RIPPLE_DAMP2        = 0.02;
float    const RIPPLE_MAT_ATTEN    = 0.965;
float    const MAX_RIPPLE_HEIGHT   = 1.0;
float    const MAX_SPLASH_SIZE     = 80.0;
float    const SMALL_DZ            = 0.001;
float    const WATER_WIND_EFF2     = 0.0005;
float    const SNOW_THRESHOLD      = 8.0;
float    const MELT_RATE           = 10.0;
float    const SHADE_MELT          = 0.5;
float    const NIGHT_MELT          = 0.3;
float    const RAIN_VOLUME         = 0.05;
float    const BLOOD_VOLUME        = 0.01;
float    const FLOW_HEIGHT0        = 0.001;
float    const G_W_STOP_DEPTH      = 0.0;
float    const G_W_START_DEPTH     = 0.1;
float    const SQUARE_S_AREA       = 1.0;
float    const INIT_AREA           = 10.0;
float    const EVAPORATION         = 1.0E-5; // small for now
float    const EVAP_TEMP_POINT     = 0.0;
float    const W_TEX_STRETCH       = 2.0;
float    const MAX_WATER_ACC       = 25.0;
float    const DARK_WATER_ATTEN    = 1.0; // higher is lighter, lower is darker
float    const INT_WATER_ATTEN     = 0.5; // for interior water
unsigned const NUM_WATER_SPRINGS   = 2;
unsigned const MAX_RIPPLE_STEPS    = 2;
unsigned const UPDATE_STEP         = 8; // update water only every nth ripple computation
int      const EROSION_DIST        = 4;
float    const EROSION_RATE        = 0.0; //0.01
bool     const DEBUG_WATER_TIME    = 0; // DEBUGGING
bool     const DEBUG_RIPPLE_TIME   = 0;
bool     const NO_ICE_RIPPLES      = 0;
bool     const USE_SEA_FOAM        = 1;
int      const UPDATE_UW_LANDSCAPE = 2;

float const w_spec[2][2] = {{0.9, 80.0}, {0.5, 60.0}};

enum {SPILL_NONE, SPILL_OUTSIDE, SPILL_INSIDE};


struct water_spring { // size = 40;

	int enabled;
	float dpf, diff, acc;
	point pos;
	vector3d vel;

	water_spring() : enabled(0), dpf(0.0f), diff(0.0f), acc(0.0f) {}

	water_spring(int en, float dpf_, float diff_, float acc_, point const &pos_, vector3d const &vel_)
		: enabled(en), dpf(dpf_), diff(diff_), acc(acc_), pos(pos_), vel(vel_) {}
};


struct water_section {

	float zval, wvol;
	int x1, y1, x2, y2;

	water_section() : zval(0.0f), wvol(0.0f), x1(0), y1(0), x2(0), y2(0) {}

	water_section(float x1_, float y1_, float x2_, float y2_, float zval_, float wvol_) : zval(zval_), wvol(wvol_),
		x1(get_xpos_clamp(x1_)), y1(get_ypos_clamp(y1_)), x2(get_xpos_clamp(x2_)), y2(get_ypos_clamp(y2_))
	{
		if (x1 > x2) swap(x1, x2);
		if (y1 > y2) swap(y1, y2);
	}
	bool is_nonzero() const {return (x1 < x2 && y1 < y2);}
};



// Global Variables
bool water_is_lava(0);
int total_watershed(0), has_accumulation(0), start_ripple(0), DISABLE_WATER(0), first_water_run(0), has_snow_accum(0), added_wsprings(0);
float max_water_height, min_water_height, def_water_level;
vector<valley> valleys;
vector<water_spring> water_springs;
vector<water_section> wsections;
spillover spill;

extern bool using_lightmap, has_snow, fast_water_reflect, enable_clip_plane_z, begin_motion;
extern int display_mode, frame_counter, TIMESCALE2, I_TIMESCALE2, world_mode, rand_gen_index, animate, animate2, blood_spilled;
extern int landscape_changed, xoff2, yoff2, scrolling, dx_scroll, dy_scroll, INIT_DISABLE_WATER;
extern float temperature, zmax, zmin, zbottom, ztop, light_factor, water_plane_z, fticks, mesh_scale, water_h_off_rel, clip_plane_z;
extern float TIMESTEP, TWO_XSS, TWO_YSS, XY_SCENE_SIZE;
extern vector3d up_norm, wind, total_wind;
extern int coll_id[];
extern obj_group obj_groups[];
extern coll_obj_group coll_objects;
extern water_params_t water_params;


void calc_water_normals();
void compute_ripples();
void update_valleys_and_draw_spillover();
void update_water_volumes();
void draw_spillover(vector<vert_norm_color> &verts, int i, int j, int si, int sj, int index, int vol_over, float blood_mix, float mud_mix);
int  calc_rest_pos(vector<int> &path_x, vector<int> &path_y, vector<char> &rp_set, int &x, int &y);
void calc_water_flow();
void init_water_springs(int nws);
void process_water_springs();
void add_waves();
void update_accumulation(int xpos, int ypos);
bool update_depth_if_underwater(point const &pos, float &depth);

void add_hole_in_landscape_texture(int xpos, int ypos, float blend);
void setup_mesh_and_water_shader(shader_t &s, bool use_detail_normal_map, bool is_water);



inline bool cont_surf(int wsi, int wsi2, float zval) {
	assert(size_t(wsi) < valleys.size() && size_t(wsi2) < valleys.size());
	return (wsi == wsi2 || fabs(zval - valleys[wsi2].zval) < SMALL_DZ || spill.member2way(wsi, wsi2));
}

bool get_water_enabled(int x, int y) {

	if (water_enabled == NULL) return 1;
	int const xx(x + xoff2), yy(y + yoff2);
	return (point_outside_mesh(xx, yy) || water_enabled[yy][xx]);
}

bool has_water(int x, int y) {
	return (world_mode == WMODE_GROUND && !point_outside_mesh(x, y) && wminside[y][x] && get_water_enabled(x, y));
}

bool mesh_is_underwater(int x, int y) {
	return (has_water(x, y) && mesh_height[y][x] < water_matrix[y][x]);
}


void water_color_atten_at_pos(colorRGBA &c, point const &pos) {

	if (DISABLE_WATER || world_mode == WMODE_UNIVERSE || pos.z > ((world_mode == WMODE_GROUND) ? max_water_height : water_plane_z)) return;
	int const x(get_xpos(pos.x)), y(get_ypos(pos.y));
		
	if (world_mode != WMODE_GROUND || (has_water(x, y) && pos.z < water_matrix[y][x])) {
		if (water_is_lava) {c = LAVA_COLOR; return;} // placeholder
		water_color_atten((float *)&(c.R), x, y, pos);
	}
}


void select_water_ice_texture(shader_t &shader, colorRGBA &color) {

	if (water_is_lava) {
		select_texture(LAVA_TEX);
		shader.set_specular(0.1, 10.0); // low specular
		color = LAVA_COLOR;
		return;
	}
	bool const is_ice(temperature <= W_FREEZE_POINT);
	select_texture(is_ice ? (int)ICE_TEX : (int)WATER_TEX);
	shader.set_specular(w_spec[is_ice][0], w_spec[is_ice][1]);
	color  = (is_ice ? ICE_C : WATER_C);
	color *= DARK_WATER_ATTEN;
}


void set_tt_water_specular(shader_t &shader) {

	if (water_is_lava) {shader.set_specular(0.2, 20.0); return;} // 2x low specular
	bool const is_ice(get_cur_temperature() <= W_FREEZE_POINT);
	shader.set_specular(2.0*w_spec[is_ice][0], 2.0*w_spec[is_ice][1]); // 2x specular
}


colorRGBA get_tt_water_color() {

	if (water_is_lava) {return LAVA_COLOR;}
	bool const is_ice(get_cur_temperature() <= W_FREEZE_POINT);
	colorRGBA color((is_ice ? ICE_C : WATER_C).modulate_with(texture_color(is_ice ? (int)ICE_TEX : (int)WATER_TEX)));
	color *= DARK_WATER_ATTEN;
	blend_color(color, colorRGBA(0.15, 0.1, 0.05, 1.0), color, water_params.mud, 0); // blend in mud color
	color.alpha *= water_params.alpha;
	color       *= water_params.bright;
	return color;
}


colorRGBA get_landscape_color(int xpos, int ypos) {

	int cindex; // unused
	assert(!point_outside_mesh(xpos, ypos));
	point const pos(get_xval(xpos), get_yval(ypos), mesh_height[ypos][xpos]);
	float const diffuse(coll_pt_vis_test(pos, get_light_pos(), 0.0, cindex, -1, 0, 3) ? 1.0 : 0.0); // Note: ignoring cloud shadows on mesh
	return get_landscape_texture_color(xpos, ypos)*(0.5*(1.0 + diffuse)); // half ambient and half diffuse
}


void get_object_color(int cindex, colorRGBA &color) {

	coll_obj const &cobj(coll_objects.get_cobj(cindex));
	if (cobj.cp.coll_func == smiley_collision && has_invisibility(cobj.cp.cf_index)) return; // invisible simleys don't have a reflection
	color = cobj.get_avg_color();
}


struct water_vertex_calc_t {

	bool draw_fast;
	float zval1, zval2;

	water_vertex_calc_t() : draw_fast(0), zval1(def_water_level), zval2(def_water_level) {}

	void calc_zvals(int i, int j, int wsi, int dx, int dy, bool outside_water) {
		float const water_zmin(zbottom - MIN_WATER_DZ);
		zval1 = max(water_matrix[i][j], water_zmin) - SMALL_NUMBER; // never go below water_zmin, what if wminside[i][j]==0 ?

		if (i+dy >= 0 && i+dy < MESH_Y_SIZE) {
			if (outside_water || watershed_matrix[i+dy][j].wsi == wsi ||
				(wminside[i+dy][j] == 1 && cont_surf(wsi, watershed_matrix[i+dy][j].wsi, valleys[wsi].zval)))
			{
				zval2 = max(water_matrix[i+dy][j], water_zmin) - SMALL_NUMBER; // never go below water_zmin
			}
		}
	}

	void blend_reflection_color(point const &v, colorRGBA &color, vector3d const &n, point const &camera) {
		vector3d const view_dir((v - camera).get_norm());
		float const view_dp(dot_product(view_dir, n));
		//if (view_dp >= 0.0) {color = ALPHA0; return;} // back facing water - not valid (need to check all connected vertices/faces?)

		// add some green at shallow view angles
		blend_color(color, colorRGBA(0.0, 1.0, 0.5, 1.0), color, 0.25*(1.0 - fabs(view_dp)), 0);
		vector3d dir;
		calc_reflection_angle(view_dir, dir, n);
		point vs(v), ve(v + dir*(4.0*XY_SCENE_SIZE/dir.mag()));
		colorRGBA rcolor(ALPHA0);
		bool mesh_int(0), cobj_int(0);

		if (!fast_water_reflect) {
			bool skip_mesh(!do_line_clip_scene(vs, ve, zbottom, max(ztop, czmax)));
			int xpos, ypos;
			float mesh_zval;
			int const fast(2); // less accurate but faster

			if (!skip_mesh && line_intersect_mesh(vs, ve, xpos, ypos, mesh_zval, fast, 1) /*&& mesh_zval > vs.z*/) { // not sure about this last test
				float const t((fabs(ve.z - vs.z) < TOLERANCE) ? 0.0 : (mesh_zval - vs.z)/(ve.z - vs.z));
				ve       = (vs + (ve - vs)*t);
				rcolor   = get_landscape_color(xpos, ypos);
				mesh_int = 1;
			
				if (dir.z < 0.0 && is_underwater(ve)) {
					// if mesh int point is underwater, attenuate along light path and blend like we do when drawing the mesh
					// could calculate approx water intersection and call recursively, but there are too many problems with that approach
					water_color_atten_pt(&rcolor.R, xpos, ypos, ve, v, get_light_pos());
					float const t2((fabs(v.z - ve.z) < TOLERANCE) ? 0.0 : (water_matrix[ypos][xpos] - ve.z)/(v.z - ve.z));
					ve = (ve + (v - ve)*t2); // updated approx water collision point
					blend_color(rcolor, color, rcolor, 0.5, 1); // add in a watery color
				}
			}
			int cindex;
			point cpos; // unused
			vector3d cnorm; // unused
			assert(!is_nan(vs) && !is_nan(ve));

			if (check_coll_line_exact(vs, ve, cpos, cnorm, cindex, 0.0, -1, 1, 0, 0)) {
				get_object_color(cindex, rcolor);
				cobj_int = 1;
			}
		}
		if (!mesh_int && !cobj_int) { // no mesh intersect and no cobj intersect
			vector3d const vdir((ve - vs).get_norm());
			float const cloud_density(get_cloud_density(vs, vdir)); // cloud_color vs. bkg_color
			blend_color(rcolor, get_cloud_color(), get_bkg_color(vs, vdir), CLIP_TO_01(2.0f*cloud_density), 1);
		}
		if (rcolor.alpha > 0.0) {
			float const refract_ix((temperature <= W_FREEZE_POINT) ? ICE_INDEX_REFRACT : WATER_INDEX_REFRACT);
			float r(CLIP_TO_01(get_fresnel_reflection(-view_dir, n, 1.0, refract_ix))); // maybe incorrect if out of the [0.0, 1.0] range?
			rcolor.alpha = 1.0; // transparent objects
			blend_color(color, rcolor, color, r*rcolor.alpha, 1);
		}
	}

	void calc_vertex_cn(vert_norm_color &vnc, int i, int j, colorRGBA const &color_in) {
		assert(!point_outside_mesh(j, i));
		vnc.n = wat_vert_normals[i][j];
		colorRGBA color(color_in); // note that the texture is blue, so that's why the color is more whiteish

		if (!(display_mode & 0x20) && !draw_fast && !has_snow && vnc.v.z > mesh_height[i][j]) { // calculate water reflection and blend into color
			point const camera(get_camera_pos());
			if (camera.z > vnc.v.z) {blend_reflection_color(vnc.v, color, vnc.n, camera);} // below the camera
		}
		vnc.set_c4(color);
	}
};


unsigned const RESERVED_IX = 0xFFFFFFFF;

class mesh_strip_drawer : public indexed_vao_manager_t {
 
protected:
	vector<vert_norm_color> verts; // vertex data
private:
	vector<unsigned> pt_to_ix; // maps mesh {x,y} values to vertex index
	vector<unsigned> indices; // indexes into verts
public:
	mesh_strip_drawer() {pt_to_ix.resize(XY_MULT_SIZE, RESERVED_IX);}
 
	bool add_vertex(int x, int y, float zval=0.0) {
		unsigned const pos(y*MESH_X_SIZE + x);
		assert(pos < pt_to_ix.size());
		unsigned &ix(pt_to_ix[pos]);
		bool const is_new_vertex(ix == RESERVED_IX);
		
		if (is_new_vertex) {
			ix = verts.size();
			vert_norm_color vnc; // zval/color/normal not filled in here - up to the caller to do this
			vnc.v.assign(get_xval(x), get_yval(y), zval);
			verts.push_back(vnc);
		}
		indices.push_back(ix);
		return is_new_vertex;
	}
	void end_strip() {indices.push_back(RESERVED_IX);}
	
	void draw() {
		if (verts.empty()) {assert(indices.empty()); return;} // nothing to do
		create_and_upload(verts, indices, 2);
		vert_norm_color::set_vbo_arrays();
		glEnable(GL_PRIMITIVE_RESTART);
		glPrimitiveRestartIndex(RESERVED_IX);
		glDrawRangeElements(GL_TRIANGLE_STRIP, 0, verts.size(), indices.size(), GL_UNSIGNED_INT, NULL);
		++num_frame_draw_calls;
		glDisable(GL_PRIMITIVE_RESTART);
		post_render();
	}
	void clear() {
		clear_vbos();
		verts.clear();
		indices.clear();
		for (auto i = pt_to_ix.begin(); i != pt_to_ix.end(); ++i) {*i = RESERVED_IX;}
	}
};


class water_strip_drawer : public mesh_strip_drawer, public water_vertex_calc_t {

public:
	void add_segment(int i, int j, int wsi, int dx, int dy) {
		calc_zvals(i, j, wsi, dx, dy, 1); // outside_water=1

		for (int d = 0; d < 2; ++d) {
			bool const dd((d != 0) ^ (dx < 0) ^ (dy < 0));
			add_vertex(j, i+dd*dy, (dd ? zval2 : zval1));
		}
	}
	void calc_vertex_colors_normals(colorRGBA const &color_in) {
		if (verts.empty()) return;
		// Note: frame_counter check is a hack to avoid Nvidia driver perf problem where we get only a few FPS for the first 8s
		bool const use_threads(!fast_water_reflect && !draw_fast && !(display_mode & 0x20) /*&& frame_counter > 100*/);

		// run on multiple threads when we have the slow ray-traced per-vertex water reflections enabled (assumes a quad core machine)
#pragma omp parallel for num_threads(4) schedule(dynamic,8) if (use_threads)
		for (int i = 0; i < (int)verts.size(); ++i) {
			vert_norm_color &vnc(verts[i]);
			calc_vertex_cn(vnc, get_ypos(vnc.v.y), get_xpos(vnc.v.x), color_in);
		}
	}
	void draw_outside_water_range(int x1, int y1, int x2, int y2, int dx, int dy) {
		if (x1 == x2 || y1 == y2) return; // empty range
		int last_draw(0);

		for (int i = y1; i != y2; i += dy) {
			for (int j = x1; j != x2+dx; j += dx) {
				if (j != x2 && (
					(wminside[i][j]       == 2 && water_matrix[i][j]       > mesh_height[i][j])    ||
					(wminside[i+dy][j]    == 2 && water_matrix[i+dy][j]    > mesh_height[i+dy][j]) ||
					(wminside[i][j+dx]    == 2 && water_matrix[i][j+dx]    > mesh_height[i][j+dx]) ||
					(wminside[i+dy][j+dx] == 2 && water_matrix[i+dy][j+dx] > mesh_height[i+dy][j+dx])))
				{
					add_segment(i, j, 0, dx, dy);
					last_draw = 1;
				}
				else if (last_draw) {
					add_segment(i, j, 0, dx, dy);
					end_strip();
					last_draw = 0;
				}
			} // for j
		} // for i
		assert(!last_draw);
	}
};


class water_surface_draw : public water_vertex_calc_t {

	struct color_scale_ix {
		vector3d n;
		color_wrapper cw;
		int ix;
		color_scale_ix() : n(zero_vector), ix(-1) {}
		color_scale_ix(color_wrapper const &cw_, vector3d const &n_, int ix_) : n(n_), cw(cw_), ix(ix_) {}
	};

	vector<color_scale_ix> last_row_colors;
	vector<vert_norm_color> verts;

public:
	water_surface_draw() {last_row_colors.resize(MESH_X_SIZE);}
	vector<vert_norm_color> &get_verts() {return verts;}

	void draw_water_surface(int i, int j, colorRGBA const &color_in, int wsi) {
		assert(unsigned(j) < last_row_colors.size());
		calc_zvals(i, j, wsi, 1, 1, 0); // outside_water=0
		float const x(get_xval(j)), y(get_yval(i));

		for (int d = 0; d < 2; ++d) {
			point const v(x, y+d*DY_VAL, (d ? zval2 : zval1));
			vert_norm_color vnc(v, last_row_colors[j].n, last_row_colors[j].cw.c); // note that the texture is blue, so that's why the color is more whiteish
			
			if (last_row_colors[j].ix != (i+d)) { // gets here about half the time
				calc_vertex_cn(vnc, (i+d), j, color_in);
				if (d) {last_row_colors[j] = color_scale_ix(vnc, vnc.n, (i+d));}
			}
			verts.push_back(vnc);
		}
	}
	void end_strip() {draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);}
};


void draw_water(bool no_update, bool draw_fast) {

	RESET_TIME;
	int wsi(0), last_water(2), last_draw(0), lc0(landscape_changed);
	colorRGBA color(WHITE);
	static float tdx(0.0), tdy(0.0);
	float const tx_scale(W_TEX_STRETCH/TWO_XSS), ty_scale(W_TEX_STRETCH/TWO_YSS);
	
	if (!no_update) {
		process_water_springs();
		add_waves();
		if (DEBUG_WATER_TIME) {PRINT_TIME("0 Add Waves");}
	}
	if (DISABLE_WATER) return;
	bool const use_foam(USE_SEA_FOAM && !water_is_lava);
	shader_t s;
	if (use_foam) {s.set_prefix("#define ADD_DETAIL_TEXTURE", 1);} // FS
	setup_mesh_and_water_shader(s, 0, 1);
	if (use_foam) {s.add_uniform_float("detail_tex_scale", 0.0);}
	point const camera(get_camera_pos());
	bool const is_ice(temperature <= W_FREEZE_POINT);

	if (!is_ice) {
		tdx -= WATER_WIND_EFF2*wind.x*fticks;
		tdy -= WATER_WIND_EFF2*wind.y*fticks;
	}
	float const tx_val(tx_scale*xoff2*DX_VAL + tdx), ty_val(ty_scale*yoff2*DX_VAL + tdy);
	if (DEBUG_WATER_TIME) {PRINT_TIME("1 Init");}

	// draw exterior water (oceans)
	if (!enable_clip_plane_z || water_plane_z > clip_plane_z) { // outside water
		if (camera.z >= water_plane_z) {draw_water_sides(s, 1);}
		if (DEBUG_WATER_TIME) {PRINT_TIME("2.1 Draw Water Sides");}
		enable_blend();
		select_water_ice_texture(s, color);
		setup_texgen(tx_scale, ty_scale, tx_val, ty_val, 0.0, s, 0);
		if (!water_is_lava) {color.alpha *= 0.5;} // increase alpha for water
		int const xend(MESH_X_SIZE-1), yend(MESH_Y_SIZE-1);

		if (use_foam) { // use sea foam texture
			select_texture(FOAM_TEX, 1);
			setup_texgen(20.0*tx_scale, 20.0*ty_scale, 0.0, 0.0, 0.0, s, 0);
			s.add_uniform_float("detail_tex_scale", 1.0);
		}
		static water_strip_drawer wsdraw;
		wsdraw.draw_fast = draw_fast;
		// draw back-to-front away from the player in 4 quadrants to make the alpha blending work correctly
		point const camera_adj(camera - point(0.5*DX_VAL, 0.5*DY_VAL, 0.0)); // hack to fix incorrect offset
		int const cxpos(max(0, min(xend, get_xpos(camera_adj.x)))), cypos(max(0, min(yend, get_ypos(camera_adj.y))));
		wsdraw.draw_outside_water_range(cxpos, cypos, xend, yend,  1,  1);
		wsdraw.draw_outside_water_range(cxpos, cypos, xend, 0,     1, -1);
		wsdraw.draw_outside_water_range(cxpos, cypos, 0,    yend, -1,  1);
		wsdraw.draw_outside_water_range(cxpos, cypos, 0,    0,    -1, -1);
		wsdraw.calc_vertex_colors_normals(color);
		wsdraw.draw();
		wsdraw.clear();
		if (use_foam) {s.add_uniform_float("detail_tex_scale", 0.0);}
		if (DEBUG_WATER_TIME) {PRINT_TIME("2.2 Water Draw Fixed");}
		if (camera.z < water_plane_z) {draw_water_sides(s, 1);}
	}
	static rand_gen_t rgen;
	
	if (!no_update && !no_grass()) {
		unsigned const num_updates(max(1, XY_MULT_SIZE/256));

		for (unsigned n = 0; n < num_updates; ++n) {
			int const i(rgen.rand()%MESH_Y_SIZE), j(rgen.rand()%MESH_X_SIZE); // select a random mesh element

			if (wminside[i][j] == 1 && get_water_enabled(j, i) && !is_mesh_disabled(j, i) && mesh_height[i][j] < water_matrix[i][j]) {
				modify_grass_at(get_mesh_xyz_pos(j, i), HALF_DXY, 0, 0, 0, 1); // check underwater
			}
		}
		if (DEBUG_WATER_TIME) {PRINT_TIME("3 Grass Update");}
	}
	select_water_ice_texture(s, color);
	setup_texgen(tx_scale, ty_scale, tx_val, ty_val, 0.0, s, 0);
	enable_blend();
	
	if (!no_update) {
		update_valleys_and_draw_spillover(); // draws spillover sections using the same shader
		if (DEBUG_WATER_TIME) {PRINT_TIME("4 Water Valleys Update");}
		assert(fticks != 0.0);

		if (animate2) { // call the function that computes the ripple effect
			unsigned num_steps(1);

			if (!(start_ripple || first_water_run) || is_ice || (!start_ripple && first_water_run)) {}
			else {
				assert(I_TIMESCALE2 > 0);
				num_steps = max(1U, min(MAX_RIPPLE_STEPS, unsigned(min(3.0f, fticks)*min(MAX_I_TIMESCALE, I_TIMESCALE2))));
			}
			for (unsigned i = 0; i < num_steps; ++i) {compute_ripples();}
			calc_water_normals();
		}
		if (DEBUG_WATER_TIME) {PRINT_TIME("5 Water Ripple Update");}
	}
	unsigned nin(0);
	int xin[4] = {}, yin[4] = {}, last_wsi(-1);
	bool const disp_snow((display_mode & 0x40) && temperature <= SNOW_MAX_TEMP);
	if (!water_is_lava) {color *= INT_WATER_ATTEN;} // attenuate for interior water
	colorRGBA wcolor(color);
	water_surface_draw wsd;
	
	// draw interior water (ponds)
	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (!is_ice && has_snow_accum && !no_update) {update_accumulation(j, i);}
			nin = 0;

			if (wminside[i][j] == 1) {
				wsi = watershed_matrix[i][j].wsi;
				assert(size_t(wsi) < valleys.size());
				float const z_min(z_min_matrix[i][j]), zval(valleys[wsi].zval), wzval(water_matrix[i][j]), wzmax(max(zval, wzval));

				if (wzmax >= z_min - G_W_STOP_DEPTH) {
					if (!is_ice && UPDATE_UW_LANDSCAPE && !no_update && wzval > zval && (rgen.rand()&63) == 0) {
						add_hole_in_landscape_texture(j, i, 1.2*fticks*(0.02 + wzmax - z_min));
					}
					float delta_area(1.0);

					if (point_interior_to_mesh(j, i)) { // more accurate but less efficient
						delta_area = 0.0;

						for (int y = -1; y <= 1; ++y) {
							for (int x = -1; x <= 1; ++x) {
								if (zval >= mesh_height[i+y][j+x]) {delta_area += 1.0;}
							}
						}
						delta_area /= 9.0;
					}
					valleys[wsi].area += SQUARE_S_AREA*delta_area; // if !no_update?
					
					if (wzmax >= z_min && i < MESH_Y_SIZE-1 && j < MESH_X_SIZE-1) {
						xin[nin] = j; yin[nin] = i; ++nin;

						for (int ii = i; ii < i+2; ++ii) {
							for (int jj = j + (ii == i); jj < j+2; ++jj) {
								if (wminside[ii][jj] == 1 && cont_surf(wsi, watershed_matrix[ii][jj].wsi, zval)) {
									xin[nin] = jj; yin[nin] = ii; ++nin;
								}
							}
						}
						if (nin == 4) {
							if (disp_snow && accumulation_matrix[i][j] >= SNOW_THRESHOLD && (mesh_height[i][j] <= zval || (i > 0 && j > 0 && mesh_height[i-1][j-1] <= zval))) {
								if (last_water != 0) { // snow
									wsd.end_strip();
									select_texture(SNOW_TEX);
									s.set_specular(0.6, 20.0);
									last_water = 0;
									color      = WHITE;
								}
							}
							else if (last_water != 1) {
								wsd.end_strip();
								select_water_ice_texture(s, color);
								color     *= INT_WATER_ATTEN; // attenuate for interior water
								last_water = 1;
							}
							if (wsi != last_wsi) {
								float const blood_mix(valleys[wsi].blood_mix), mud_mix(valleys[wsi].mud_mix);
								wcolor = color;

								if (blood_mix > 0.99) {wcolor = RED;} // all blood
								else {
									wcolor.alpha *= 0.5;
									if (mud_mix   > 0.0) {blend_color(wcolor, (!is_ice ? MUD_S_C : LT_BROWN), wcolor, mud_mix, 1);}
									if (blood_mix > 0.0) {blend_color(wcolor, RED, wcolor, blood_mix, 1);}
								}
								last_wsi = wsi;
							}
							wsd.draw_water_surface(i, j, wcolor, wsi);
							last_draw = 1;
						} // nin == 4
					}
				}
			}
			if (nin < 4) { // not a full quad of water
				if (last_draw) {wsd.draw_water_surface(i, j, wcolor, wsi);}

				if (nin == 3) { // single triangle, use color from previous vertex
					vector<vert_norm_color> &verts(wsd.get_verts());
					colorRGBA prev_color(verts.empty() ? wcolor : verts.back().get_c4()); // verts should be nonempty, so should get reset

					for (unsigned p = 0; p < 3; ++p) { // Note: if last_draw==1, degenerate triangles are generated
						point const pt(get_xval(xin[p]), get_yval(yin[p]), (water_matrix[yin[p]][xin[p]] - SMALL_NUMBER));
						verts.emplace_back(pt, wat_vert_normals[yin[p]][xin[p]], prev_color);
					}
				}
				if (last_draw || nin == 3) {wsd.end_strip();}
				last_draw = 0;
			}
		} // for j
	} // for i
	wsd.end_strip();
	disable_blend();
	s.clear_specular();
	s.end_shader();

	if (!no_update) {
		update_water_volumes();
		if (!lc0 && rgen.rand()%5 != 0) {landscape_changed = 0;} // reset, only update landscape 20% of the time
		first_water_run = 0;
	}
	if (DEBUG_WATER_TIME) {PRINT_TIME("6 Water Draw");}
}


void calc_water_normals() {

	if (DISABLE_WATER) return;
	vector3d *wsn0(wat_surf_normals[0]), *wsn1(wat_surf_normals[1]);

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		// if we have ice, the normals can't change and don't need to be updated;
		// however, if they were never calculated, they do need to be update on the first frame;
		// since we can't tell if the normals are valid, it's safest to always just update them
		// *** but it's certainly not right to always set them to plus_z ***
#if 0
		if (temperature <= W_FREEZE_POINT) { // ice
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				if (!point_interior_to_mesh(j, i) || wminside[i][j]) {wsn1[j] = wat_vert_normals[i][j] = plus_z;}
			}
		} else
#endif
		{ // water
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				if (point_interior_to_mesh(j, i) && wminside[i][j] && water_matrix[i][j] >= z_min_matrix[i][j]) { // inside
					wsn1[j] = get_matrix_surf_norm(water_matrix, NULL, MESH_X_SIZE, MESH_Y_SIZE, j, i);
					vector3d nv(wsn1[j]);
					if (i > 0)          {nv += wsn0[j];}
					if (i > 0 && j > 0) {nv += wsn0[j-1];}
					if (j > 0)          {nv += wsn1[j-1];}
					wat_vert_normals[i][j] = nv.get_norm();
				}
				else {
					wsn1[j] = wat_vert_normals[i][j] = plus_z;
				}
			} // for j
		}
		swap(wsn0, wsn1);
	} // for i
}


inline void update_water_edges(int i, int j) {

	if (i > 0 && wminside[i-1][j] == 1) {
		water_matrix[i][j] = valleys[watershed_matrix[i-1][j].wsi].zval;
	}
	else if (j > 0 && wminside[i][j-1] == 1) {
		water_matrix[i][j] = valleys[watershed_matrix[i][j-1].wsi].zval;
	}
	else {
		water_matrix[i][j] = def_water_level;
	}
}


void compute_ripples() {

	if (DISABLE_WATER) return;
	static unsigned dtime1(0), dtime2(0), counter(0);
	bool const update_iter((counter%UPDATE_STEP) == 0);
	RESET_TIME;

	if (temperature > W_FREEZE_POINT && (start_ripple || first_water_run)) {
		float const tstep(max(fticks, 0.25f)); // ensure some min amount of damping to prevent unstable ripples when the framerate is very high
		float const rm_atten(pow(RIPPLE_MAT_ATTEN, tstep)), rdamp1(pow(RIPPLE_DAMP1, tstep)), rdamp2(RIPPLE_DAMP2*tstep);
		start_ripple = 0;

		for (int i = 0; i < MESH_Y_SIZE; ++i) {
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				if (!wminside[i][j] || water_matrix[i][j] < z_min_matrix[i][j] /*|| !get_water_enabled(j, i)*/) continue;
				short const i8(watershed_matrix[i][j].inside8);
				fix_fp_mag(ripples[i][j].rval);
				float const rmij(ripples[i][j].rval);
				float &acc(ripples[i][j].acc);
				fix_fp_mag(acc);
				acc *= rm_atten;
				if (!start_ripple && fabs(acc) > 1.0E-6) start_ripple = 1;

				// 00 0- -0 0+ +0 -- +- ++ -+  22  11
				// 01 02 04 08 10 20 40 80 100 200 400
				if (point_interior_to_mesh(j, i)) { // fast mode
					float const d0( rmij - ripples[i  ][j-1].rval);
					if (i8 & 0x02)  ripples[i  ][j-1].acc += d0;
					float const d2( rmij - ripples[i  ][j+1].rval);
					if (i8 & 0x08)  ripples[i  ][j+1].acc += d2;
					float const d4((rmij - ripples[i-1][j-1].rval)*SQRTOFTWOINV);
					if (i8 & 0x20)  ripples[i-1][j-1].acc += d4;
					float const d1( rmij - ripples[i-1][j  ].rval);
					if (i8 & 0x04)  ripples[i-1][j  ].acc += d1;
					float const d7((rmij - ripples[i-1][j+1].rval)*SQRTOFTWOINV);
					if (i8 & 0x100) ripples[i-1][j+1].acc += d7;
					float const d5((rmij - ripples[i+1][j-1].rval)*SQRTOFTWOINV);
					if (i8 & 0x40)  ripples[i+1][j-1].acc += d5;
					float const d3( rmij - ripples[i+1][j  ].rval);
					if (i8 & 0x10)  ripples[i+1][j  ].acc += d3;
					float const d6((rmij - ripples[i+1][j+1].rval)*SQRTOFTWOINV);
					if (i8 & 0x80)  ripples[i+1][j+1].acc += d6;
					acc -= d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7;
					fix_fp_mag(acc);
					continue;
				}
				if (j > 0) {
					float const dz(rmij - ripples[i][j-1].rval);
					if (i8 & 0x02) ripples[i][j-1].acc += dz;
					acc -= dz;

					if (i > 0) {
						float const dz((rmij - ripples[i-1][j-1].rval)*SQRTOFTWOINV);
						if (i8 & 0x20) ripples[i-1][j-1].acc += dz;
						acc -= dz;
					}
					if (i < MESH_Y_SIZE-1) {
						float const dz((rmij - ripples[i+1][j-1].rval)*SQRTOFTWOINV);
						if (i8 & 0x40) ripples[i+1][j-1].acc += dz;
						acc -= dz;
					}
				}
				if (i > 0) {
					float const dz(rmij - ripples[i-1][j].rval);
					if (i8 & 0x04) ripples[i-1][j].acc += dz;
					acc -= dz;
				}
				if (j < MESH_X_SIZE-1) {
					float const dz(rmij - ripples[i][j+1].rval);
					if (i8 & 0x08) ripples[i][j+1].acc += dz;
					acc -= dz;

					if (i < MESH_Y_SIZE-1) {
						float const dz((rmij - ripples[i+1][j+1].rval)*SQRTOFTWOINV);
						if (i8 & 0x80) ripples[i+1][j+1].acc += dz;
						acc -= dz;
					}
					if (i > 0) {
						float const dz((rmij - ripples[i-1][j+1].rval)*SQRTOFTWOINV);
						if (i8 & 0x100) ripples[i-1][j+1].acc += dz;
						acc -= dz;
					}
				}
				if (i < MESH_Y_SIZE-1) {
					float const dz(rmij - ripples[i+1][j].rval);
					if (i8 & 0x10) ripples[i+1][j].acc += dz;
					acc -= dz;
				}
				fix_fp_mag(acc);
			} // for j
		} // for i
		if (DEBUG_RIPPLE_TIME) dtime1 += GET_DELTA_TIME;
		
		for (int i = 0; i < MESH_Y_SIZE; ++i) {
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				float ripple_zval(0.0);

				if (wminside[i][j]) {
					float const zval(rdamp1*(ripples[i][j].rval + rdamp2*ripples[i][j].acc)); // ripple wave height
					ripple_zval = ((fabs(zval) < TOLERANCE) ? 0.0 : zval); // prevent small floating point numbers
				}
				if (wminside[i][j] == 1) { // dynamic water
					int const wsi(watershed_matrix[i][j].wsi);
					assert(size_t(wsi) < valleys.size());

					if (water_matrix[i][j] < z_min_matrix[i][j] && fabs(ripples[i][j].rval) < 1.0E-4 && fabs(ripples[i][j].acc) < 1.0E-4) { // under ground - no ripple
						if (update_iter) water_matrix[i][j] = valleys[wsi].zval;
						continue;
					}
					float const depth(valleys[wsi].depth);

					if (depth < 0) {
						ripples[i][j].rval *= rm_atten;
						if (update_iter) water_matrix[i][j] = valleys[wsi].zval;
						continue;
					}
					float const zval(max(min(ripple_zval, depth), -depth)); // max ripple height equals water depth
					ripples[i][j].rval = rm_atten*zval;
					water_matrix[i][j] = valleys[wsi].zval + zval;
				}
				else if (wminside[i][j] == 2) { // fixed water
					ripples[i][j].rval = rm_atten*ripple_zval;
					water_matrix[i][j] = water_plane_z + min(MAX_RIPPLE_HEIGHT, ripple_zval);
					water_matrix[i][j] = max(water_matrix[i][j], zbottom);
				}
				else if (update_iter) {
					if (get_water_enabled(j, i)) {
						update_water_edges(i, j);
					}
					else {
						ripples[i][j].rval = 0.0; // not sure if this is correct, or if there is something else that should be done here
					}
				}
			} // for j
		} // for i
		if (DEBUG_RIPPLE_TIME) dtime2 += GET_DELTA_TIME;
	}
	else { // no ripple
		matrix_clear_2d(ripples);

		// must clear ripples at least once at the beginning
		if (NO_ICE_RIPPLES || counter == 0 || temperature > W_FREEZE_POINT) {
			for (int i = 0; i < MESH_Y_SIZE; ++i) {
				for (int j = 0; j < MESH_X_SIZE; ++j) {
					if (wminside[i][j] == 1) {
						water_matrix[i][j] = valleys[watershed_matrix[i][j].wsi].zval;
					}
					else {
						update_water_edges(i, j);
					}
				} // for j
			} // for i
		}
	} // ripple
	++counter;

	if (DEBUG_RIPPLE_TIME && (counter%20) == 0) {
		cout << "times = " << dtime1 << ", " << dtime2 << endl; // cumulative
		dtime1 = dtime2 = 0;
	}
}


// Note: we could calculate xpos and ypox from pos, but in all callers xpos and ypos are already available
void add_splash(point const &pos, int xpos, int ypos, float energy, float radius, bool add_sound, vector3d const &vadd, bool add_droplets) {

	//energy *= 10.0; // debugging
	if (DISABLE_WATER || !(display_mode & 0x04) || temperature <= W_FREEZE_POINT) return;
	if (point_outside_mesh(xpos, ypos) || is_mesh_disabled(xpos, ypos))           return; // no mesh, no splash
	if (water_matrix[ypos][xpos] < (mesh_height[ypos][xpos] - 0.5*radius))        return; // water is too low
	int in_wmatrix(0), wsi(0);
	float water_mix(1.0);

	if (wminside[ypos][xpos] == 1) {
		float const mh_r(mesh_height[ypos][xpos] + radius);

		if (h_collision_matrix[ypos][xpos] < mh_r && water_matrix[ypos][xpos] < (mh_r + MAX_SPLASH_DEPTH)) {
			wsi        = watershed_matrix[ypos][xpos].wsi;
			water_mix  = (1.0 - valleys[wsi].blood_mix);
			in_wmatrix = 1;
		}
	}
	assert(energy >= 0.0);
	energy = min(energy, 10000.0f);
	float const splash_size(min(0.006f*(0.5f + water_mix)*sqrt((4.0f/XY_SCENE_SIZE)*energy), MAX_SPLASH_SIZE));
	if (splash_size < TOLERANCE) return;
	float const rad(min(4, (int)ceil(0.5f*radius/(DX_VAL + DY_VAL)))), radsq(rad*rad);
	int const droplet_id(coll_id[DROPLET]);

	if (add_droplets && energy > 5.0 && droplet_id >= 0 && temperature > W_FREEZE_POINT && point_interior_to_mesh(xpos, ypos)) {
		// splash size = 0.3 - rain, 2.0 - ball, 60.0 - rocket
		float const sqrt_energy(sqrt(energy));
		int ndrops(min(MAX_SPLASH_DROP, int((0.5 + 0.2*water_mix)*sqrt_energy)));

		if (ndrops > 0 && water_matrix[ypos][xpos] >= z_min_matrix[ypos][xpos] && wminside[ypos][xpos]) {
			if (in_wmatrix) {
				valleys[wsi].mud_mix += 0.12*sqrt_energy/(valleys[wsi].w_volume + 1.0);
				valleys[wsi].mud_mix  = min(1.0f, valleys[wsi].mud_mix);
			}
			if (wminside[ypos][xpos] == 1) { // dynamic water
				ndrops = min(ndrops, int(0.25*valleys[watershed_matrix[ypos][xpos].wsi].w_volume));
			}
			if (ndrops > 0) {
				//valleys[watershed_matrix[ypos][xpos].wsi].w_volume -= ndrops;
				float const vz(5.0 + (0.2 + 0.1*water_mix)*sqrt_energy);
				point const ppos(pos + vector3d(0.0, 0.0, 1.0*radius));
				float const mud_mix(in_wmatrix ? valleys[wsi].mud_mix : 0.0), blood_mix(in_wmatrix ? valleys[wsi].blood_mix : 0.0);
				add_water_particles(ppos, vadd+vector3d(0.0, 0.0, 1.5*vz), 0.25*vz, 1.0*radius, mud_mix, blood_mix, (20 + (rand()&7))*ndrops);
				obj_group &objg(obj_groups[droplet_id]);
					
				for (int o = 0; o < ndrops; ++o) {
					int const i(objg.choose_object());
					objg.create_object_at(i, ppos);
					objg.get_obj(i).velocity = vadd + gen_rand_vector(vz*rand_uniform(0.05, 0.1), 20.0, PI_TWO);
					vadd_rand(objg.get_obj(i).pos, 1.0*radius, 1);
				}
				if (add_sound && energy > 10.0) {gen_sound(SOUND_SPLASH1, pos, min(1.0, 0.05*sqrt_energy));}
			}
		}
	}
	int const irad(ceil(rad)), x1(max((xpos - irad), 0)), y1(max((ypos - irad), 0));
	int const x2(min((xpos + irad), MESH_X_SIZE-1)), y2(min((ypos + irad), MESH_Y_SIZE-1));

	for (int i = y1; i <= y2; i++) {
		for (int j = x1; j <= x2; j++) {
			if (((i - ypos)*(i - ypos) + (j - xpos)*(j - ypos)) <= radsq && wminside[i][j]) {ripples[i][j].rval += splash_size;}
		}
	}
	start_ripple = 1;
}


void add_waves() { // add waves due to wind

	//RESET_TIME;
	if (DISABLE_WATER || !(display_mode & 0x04) || !(display_mode & 0x0100) || water_is_lava) return;
	if (temperature <= W_FREEZE_POINT || !animate2) return;
	float const wave_amplitude(0.006/mesh_scale), wave_freq(0.06), depth_scale(20.0);
	float const wind_freq(0.02), wind_amplitude(0.15/mesh_scale), wind_resist(0.75);
	float const wxoff(wind_resist*total_wind.x/TWO_XSS/MESH_X_SIZE);
	float const wyoff(wind_resist*total_wind.y/TWO_YSS/MESH_Y_SIZE);
	float const fticks_clamped(min(fticks, 10.0f));
	static float wave_time(0.0);
	wave_time += fticks_clamped;
	if (wave_time > 4000.0) {wave_time = 0.0;} // reset at 4000 ticks (2 min. or so) to avoid FP error
	
#pragma omp parallel for schedule(static,8) num_threads(2)
	for (int y = 0; y < MESH_Y_SIZE; ++y) {
		for (int x = 0; x < MESH_X_SIZE; ++x) {
			if (!wminside[y][x] || !get_water_enabled(x, y)) continue; // only in water
			float const wh(water_matrix[y][x]), depth(wh - mesh_height[y][x]);
			if (depth < SMALL_NUMBER) continue; // not deep enough for waves
			vector3d const local_wind(get_local_wind(x, y, wh));
			float const lwmag(local_wind.mag());
			float const tx(min(0.2f, fabs(local_wind.y))*wind_freq*(x + xoff2)/lwmag - wxoff);
			float const ty(min(0.2f, fabs(local_wind.x))*wind_freq*(y + yoff2)/lwmag - wyoff);
			float const val(get_texture_component(WIND_TEX, tx, ty, 0));
			float const wval(wind_amplitude*min(2.5f, sqrt(lwmag))*val*min(depth, 0.1f));
			
			if (wminside[y][x] == 2) { // outside water (oceans)
				ripples[y][x].rval += wval + wave_amplitude*fticks_clamped*sin(wave_freq*wave_time + depth_scale*depth);
			}
			else if (fabs(ripples[y][x].rval) < 0.1*wval) { // don't add wind if already rippling to prevent instability
				ripples[y][x].rval += wval;
			}
			start_ripple = 1;
		}
	}
	//PRINT_TIME("Add Waves");
}


// *** BEGIN VALLEYS/SPILLOVER ***


void check_spillover(int i, int j, int ii, int jj, int si, int sj, float zval, int wsi) { // does wsi overflow?

	float const z_over(zval - mesh_height[ii][jj]);
	if (z_over <= max(0.0f, valleys[wsi].sf.z_over)) return;

	if (wminside[i][j] != 1) { // spillover outside
		valleys[wsi].sf = valley::spill_func(-1, i, j, si, sj, SPILL_OUTSIDE, z_over);
		return;
	}
	int const index(watershed_matrix[i][j].wsi); // spills into this pool
	assert(size_t(index) < valleys.size());

	if (index != wsi && zval > valleys[index].zval) { // spillover inside
		if (!spill.member(index, wsi)) valleys[wsi].sf = valley::spill_func(index, i, j, si, sj, SPILL_INSIDE, z_over); // pools not combined
		valleys[wsi].spill_index = index;
	}
}


void sync_water_height(int wsi, int skip_ix, float zval, float z_over, vector<unsigned> &cc) {

	spill.get_connected_components(wsi, cc);

	for (unsigned k = 0; k < cc.size(); ++k) {
		if ((int)cc[k] == wsi || (int)cc[k] == skip_ix) continue;
		valley &v(valleys[cc[k]]);
		v.zval      = zval;//max(zval, v.zval);
		v.dz       -= z_over;
		v.w_volume  = v.lwv; // ???
		v.mud_mix   = valleys[wsi].mud_mix; // is this necessary?
		v.blood_mix = valleys[wsi].blood_mix;
	}
}


void update_valleys_and_draw_spillover() {

	for (unsigned i = 0; i < valleys.size(); ++i) {
		valley &v(valleys[i]);
		assert(v.area >= 0.0);
		assert(v.fvol >= 0.0);
		assert(v.w_volume >= 0.0);
		v.area     += INIT_AREA*SQUARE_S_AREA;
		v.w_volume += v.fvol;
		v.fvol      = 0.0;
		v.blood_mix = CLIP_TO_01(v.blood_mix);
		v.mud_mix   = ((v.mud_mix < 0.0001f) ? 0.0f : CLIP_TO_01(v.mud_mix)*pow(0.998f, fticks)); // slowly settles
		float const dv(min((v.w_volume - v.lwv), MAX_WATER_ACC));
		float delta_z(v.get_volume()*dv/v.area); // this is inaccurate because v.area is not constant
		//if (v.old_area > 0.0 && v.area > 0.0 && v.area != v.old_area) delta_z -= (1.0 - v.old_area/(0.5*(v.area + v.old_area)))*v.dz;
		v.dz    = delta_z;
		v.zval += v.dz;
		v.spill_integral = 0.9*v.spill_integral + 0.1*v.spill_vol;
		if (v.w_volume == 0.0) {spill.remove_connected(i);}
	} // for i
	vector<unsigned> cc;
	vector<unsigned char> used(valleys.size(), 0);

	for (unsigned i = 0; i < valleys.size(); ++i) {
		if (used[i]) continue; // already processed
		spill.get_connected_components(i, cc, &used);
		
		if (cc.empty()) { // unconnected (optimization)
			used[i] = 1;
			continue;
		}
		cc.push_back(i); // add current v to the combined set
		valley combined;

		for (auto j = cc.begin(); j != cc.end(); ++j) { // calculate sums
			assert(*j < valleys.size());
			assert(!used[*j]);
			used[*j] = 1;
			valley &v(valleys[*j]);
			combined.area      += v.area;
			combined.w_volume  += v.w_volume;
			combined.zval      += v.zval*v.area; // zval determined by surface area (weighed average)
			combined.blood_mix += v.w_volume*v.blood_mix; // composition determined by volume
			combined.mud_mix   += v.w_volume*v.mud_mix;
		}
		assert(combined.area     > 0.0);
		assert(combined.w_volume > 0.0);
		combined.zval     /= combined.area;
		combined.blood_mix = CLIP_TO_01(combined.blood_mix/combined.w_volume); // do we have to clip these?
		combined.mud_mix   = CLIP_TO_01(combined.mud_mix  /combined.w_volume);

		for (auto j = cc.begin(); j != cc.end(); ++j) { // update all connected valleys
			valley &v(valleys[*j]);
			float const delta_z(combined.zval - v.zval); // new - old
			v.w_volume  = max(TOLERANCE, (v.w_volume + delta_z*v.area/v.get_volume())); // not sure about this???
			v.dz       += delta_z;
			v.zval      = combined.zval; // do we want to update v.w_volume based on v.dz?
			v.blood_mix = combined.blood_mix;
			v.mud_mix   = combined.mud_mix;
		}
	} // for i
	for (unsigned i = 0; i < valleys.size(); ++i) { // reset for next iteration
		valley &v(valleys[i]);
		v.area        = 0.0;
		v.spill_vol   = 0.0;
		v.has_spilled = 0;
		v.sf.spill    = SPILL_NONE;
		v.sf.z_over   = 0.0;
		v.spill_index = -1;
		v.depth       = v.zval - mesh_height[v.y][v.x];
	} // for i

	// check for spillover offscreen or into another pool
	int const ijd[4][4] = {{0,1,0,1}, {0,-1,0,0}, {1,0,1,0}, {-1,0,0,0}};

	for (int i = 1; i < MESH_Y_SIZE-1; ++i) {
		for (int j = 1; j < MESH_X_SIZE-1; ++j) {
			if (wminside[i][j] != 1) continue;
			int const wsi(watershed_matrix[i][j].wsi);
			float const zval(valleys[wsi].zval);
			if (zval < z_min_matrix[i][j]) continue;

			for (unsigned k = 0; k < 4; ++k) {
				check_spillover(i+ijd[k][0], j+ijd[k][1], i+ijd[k][2], j+ijd[k][3], i, j, zval, wsi);
			}
		}
	}
	vector<vert_norm_color> verts;

	for (unsigned i = 0; i < valleys.size(); ++i) { // update spill graph and other data
		valley &v(valleys[i]); // pool that may be spilling (source)
		valley::spill_func const &sf(v.sf);

		if (sf.spill != SPILL_NONE) { // this pool has spilled over its border
			if (sf.z_over <= 0.0) continue;
			// what happens when w_volume < lwv? what about dz < 0?
			float vol_over(max(0.0f, (v.w_volume - v.lwv)));
			if (v.dz > TOLERANCE) vol_over *= min(1.0f, (sf.z_over/v.dz)); // fraction of last amount that spilled
			
			if (sf.spill == SPILL_INSIDE && vol_over > 0.0) { // spilled into another pool (1 direction), send water to the target
				assert(watershed_matrix[sf.si][sf.sj].wsi == (int)i);
				assert(sf.index != (int)i);
				valley &vs(valleys[sf.index]); // pool that is spilled into (dest)
				float const nv_ratio(CLIP_TO_01(vol_over/max(TOLERANCE, vs.w_volume)));
				v.dz         -= sf.z_over;
				vs.spill_vol += vol_over;
				vs.blood_mix  = CLIP_TO_01(nv_ratio*v.blood_mix + (1.0f - nv_ratio)*vs.blood_mix);
				vs.mud_mix    = CLIP_TO_01(nv_ratio*v.mud_mix   + (1.0f - nv_ratio)*vs.mud_mix);
				spill.insert(i, sf.index); // source, dest
			}
			v.w_volume    = max(0.0f, (v.w_volume - vol_over)); // ???
			v.has_spilled = 1;
			float const zval(max((v.zval - sf.z_over), v.min_zval)); // zval at spill point (local minima)
			sync_water_height(i, sf.index, zval, sf.z_over, cc);
			draw_spillover(verts, sf.i, sf.j, sf.si, sf.sj, sf.index, int(vol_over), v.blood_mix, v.mud_mix);
			v.zval = zval;
		}
		else if (v.spill_index >= 0) {
			spill.insert(i, v.spill_index); // source, dest
		}
		/*else if (wminside[sf.si][sf.sj] == 1 && watershed_matrix[sf.si][sf.sj].wsi == i) { // current sf is valid
			if (v.zval + TOLERANCE < mesh_height[sf.si][sf.sj]) spill.remove_all_i(i); // water is below min point => no spill
		}*/
		max_water_height = max(max_water_height, valleys[i].zval);
	} // for i
}


void update_water_volumes() {

	for (unsigned i = 0; i < valleys.size(); ++i) {
		valley &v(valleys[i]);
		bool const has_lwv(v.lwv > 0.0);
		if (v.has_spilled) {v.w_volume = v.lwv;} // spilled into another pool, reset to last value
		else {v.lwv += min((v.w_volume - v.lwv), MAX_WATER_ACC);} // update (with thresh)
		v.w_volume += v.spill_vol; // new water added from other pools this frame

		if (temperature > EVAP_TEMP_POINT && v.w_volume > 0.0) { // apply evaporation
			v.w_volume = max(0.0f, (v.w_volume - EVAPORATION*(temperature - EVAP_TEMP_POINT)*v.area));
		}
		if (v.w_volume < 0.0) cout << "wv: " << v.w_volume << ", sv: " << v.spill_vol << ", lwv: " << v.lwv << endl; // testing
		assert(v.w_volume >= 0.0);
		
		// don't let the graph get unlinked until the depth is 0 (should use z_spill or something?)
		//if (v.w_volume == 0.0 && v.depth > 0.0 && v.area > 0.0) v.w_volume = v.lwv = TOLERANCE;
		
		if (has_lwv && v.w_volume == 0.0) { // no more water (all evaporated)
			if (v.zval < v.min_zval) {v.zval = v.min_zval;} else {v.lwv = 1.0;}
			v.has_spilled = 0;
			spill.remove_all_i(i); // reset connectivity graph entry
		}
	}
}


// *** END VALLEYS/SPILLOVER ***


int draw_spill_section(vector<vert_norm_color> &verts, int x1, int y1, int x2, int y2, float z1, float z2, float width, int volume, int index, float blood_mix, float mud_mix) {

	assert(abs(x2 - x1) <= 1 && abs(y2 - y1) <= 1);
	float const flow_height(FLOW_HEIGHT0*Z_SCENE_SIZE);
	assert(!point_outside_mesh(x1, y1));
	if (!get_water_enabled(x1, y1) && !get_water_enabled(x2, y2)) return 2; // water disabled
	spillway_matrix[y1][x1] = (short)frame_counter;
	float xa(get_xval(x1)), ya(get_yval(y1));
	if ((z1+flow_height) < water_plane_z && (z2+flow_height) < water_plane_z)               return 0; // both ends under water plane
	if ((z1+flow_height) < water_matrix[y1][x1] && (z2+flow_height) < water_matrix[y2][x2]) return 0; // both ends under local water
	vector3d const &norm(vertex_normals[y1][x1]);

	if (x1 == x2 && y1 == y2) { // end at a point
		colorRGBA const color(verts.empty() ? (water_is_lava ? LAVA_COLOR : WATER_C) : verts.back().get_c4()); // assert that verts is nonempty?
		for (unsigned i = 0; i < 2; ++i) {verts.emplace_back(point(xa, ya, z1), norm, color);}
		return 0;
	}
	float const xb(get_xval(x2)), yb(get_yval(y2));
	float const slope(fabs((z2 - z1)/sqrt((xb - xa)*(xb - xa) + (yb - ya)*(yb - ya))));

	if (UPDATE_UW_LANDSCAPE > 1 && (rand()%5) == 0) {
		float const rate(0.01*fticks*slope*sqrt((float)volume));

		if (rate > 0) {
			int xv[4] = {x1, x2, x2, x1}, yv[4] = {y1, y1, y2, y2};

			for (unsigned i = 0; i < 4; ++i) {
				add_hole_in_landscape_texture(xv[i], yv[i], rate);

				if (EROSION_RATE > 0.0) { // erode terrain
					update_mesh_height(xv[i], yv[i], EROSION_DIST, EROSION_RATE*rate, 4.0, 1, 0);
					// update grass?
				}
			}
		}
	}
	colorRGBA color(BLOOD_C);

	if (blood_mix < 0.99) {
		if (index < 0 && (min(x1, x2) <= 1 || min(y1, y2) <= 1 || max(x1, x2) >= MESH_X_SIZE-2 || max(y1, y2) >= MESH_Y_SIZE-2)) {
			color.assign(0.3, 0.3, 1.0, 0.22);
		}
		else if (z1 == z2) {
			color.assign(0.3, 0.3, 1.0, 0.2);
		}
		else {
			assert(slope != 0.0);
			color.B = min(1.0f, max(0.6f, (0.3f + 0.6f/slope)));
			color.A = min(0.6f, max(0.2f, (0.2f + 0.2f*slope)));
			color.R = color.G = min(0.7*color.B, max(0.3, (0.2 + 0.4*slope)));
		}
		if (mud_mix   > 0.01) {blend_color(color, MUD_C,   color, mud_mix,   1);}
		if (blood_mix > 0.01) {blend_color(color, BLOOD_C, color, blood_mix, 1);}
	}
	xa += flow_height*norm.x;
	ya += flow_height*norm.y;
	z1 += flow_height*norm.z;

	if (x1 != x2 && y1 != y2) { // diagonal
		assert(abs(x2 - x1) <= 1 && abs(y2 - y1) <= 1);
		width /= SQRT2;
	}
	for (unsigned i = 0; i < 2; ++i) {
		float const xv(xa + ((y1 == y2) ? 0.0 : ((i ^ (y1 > y2)) ?  width : -width)));
		float const yv(ya + ((x1 == x2) ? 0.0 : ((i ^ (x1 > x2)) ? -width :  width)));
		verts.emplace_back(point(xv, yv, z1), norm, color);
	}
	return 1;
}


void draw_spillover(vector<vert_norm_color> &verts, int i, int j, int si, int sj, int index, int vol_over, float blood_mix, float mud_mix) {

	if (vol_over <= 0) return;
	assert(!point_outside_mesh(j, i));
	bool const nov(index == -1);
	assert(nov || size_t(index) < valleys.size());
	int x1(j), x2(j), y1(i), y2(i);
	bool last_iteration(0);
	float z1(mesh_height[i][j]);
	float const zval(nov ? zmin : valleys[index].zval);
	float const width(min(0.012f, 0.1f*(DX_VAL + DY_VAL)*(sqrt((float)vol_over) + 1.0f)));
	float const v_splash(min(10.0f, (float)vol_over));
	int const xs(nov ? -1 : valleys[index].x), ys(nov ? -1 : valleys[index].y);
	int count(0);

	if (!point_outside_mesh(sj, si)) {
		draw_spill_section(verts, sj, si, x1, y1, mesh_height[si][sj], z1, width, vol_over, index, blood_mix, mud_mix);
	}
	for (; (count < XY_SUM_SIZE) && (x1 != xs || y1 != ys) && point_interior_to_mesh(x1, y1); ++count) {
		x2 = w_motion_matrix[y1][x1].x;
		y2 = w_motion_matrix[y1][x1].y;
		assert(!point_outside_mesh(x2, y2));
		float const z2(mesh_height[y2][x2]);
		int const draw_res(draw_spill_section(verts, x1, y1, x2, y2, z1, z2, width, vol_over, index, blood_mix, mud_mix));
		if (last_iteration || draw_res == 0) {add_splash(point(get_xval(x1), get_yval(y1), water_matrix[y1][x1]), x1, y1, 1.5*v_splash, 0.002*v_splash, 0);} // hit fixed ocean/lake
		if (last_iteration || draw_res != 1 || (x2 == x1 && y2 == y1)) break; // edge, disabled, or valley
		last_iteration = (zval >= z2);
		z1 = (last_iteration ? zval : z2);
		x1 = x2;
		y1 = y2;
	}
	if (count == XY_SUM_SIZE) cout << "Error: Exceeded iteration limit in draw_spillover()." << endl;
	draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
}


void float_downstream(point &pos, float radius) {

	int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));
	if (point_outside_mesh(xpos, ypos) || wminside[ypos][xpos] != 1) return; // not in an inside pool
	if (!mesh_is_underwater(xpos, ypos)) return; // no longer floating?
	if ((pos.z - radius) < (mesh_height[ypos][xpos] + 0.5f*radius))   return; // too close to the bottom of the pool
	int const wsi(watershed_matrix[ypos][xpos].wsi);
	assert(size_t(wsi) < valleys.size());
	valley::spill_func const &sf(valleys[wsi].sf);
	if (sf.spill == SPILL_NONE || sf.z_over == 0.0 || valleys[wsi].spill_integral == 0.0) return; // not spilled
	point const spill_pt(get_xval(sf.sj), get_yval(sf.si), pos.z);
	float const dist(p2p_dist(pos, spill_pt));
	if (dist < SMALL_NUMBER) return; // avoid divide-by-zeros
	float const vel(5.0E-5f*valleys[wsi].spill_integral/(DX_VAL + DY_VAL + dist));
	pos += (spill_pt - pos)*(min(0.005f, vel)/dist);
}


void change_water_level(float water_level) {

	water_h_off_rel = 0.0; // so that get_rel_wpz() will return the base water level
	water_h_off_rel = water_level - get_rel_wpz();
	def_water_level = water_plane_z = get_water_z_height(); // ???
	calc_watershed();
}


void calc_watershed() {

	int mode(0);

	if (DISABLE_WATER == 1) {
		for (int i = 0; i < MESH_Y_SIZE; ++i) {
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				water_matrix[i][j] = zmin - 1.0;
			}
		}
		if (world_mode == WMODE_GROUND) {def_water_level = water_plane_z = zmin;} 
		init_water_springs(NUM_WATER_SPRINGS);
		return;
	}
	if (ztop < water_plane_z) { // all water
		def_water_level = water_plane_z;

		for (int i = 0; i < MESH_Y_SIZE; ++i) {
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				water_matrix[i][j]     = water_plane_z;
				wminside[i][j]         = 2;
				wat_vert_normals[i][j] = plus_z;
				watershed_matrix[i][j].inside8 = 0x1FF; // all outside water
			}
		}
		max_water_height = def_water_level;
		min_water_height = def_water_level;
		return;
	}
	else if (zbottom < water_plane_z) { // some water
		def_water_level = water_plane_z;
		mode            = 1;
	}
	else { // no water
		def_water_level = zmin;
	}
	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			watershed_matrix[i][j].x = watershed_matrix[i][j].y = 0;
		}
	}
	max_water_height = def_water_level;
	min_water_height = def_water_level;
	vector<char> rp_set(XY_MULT_SIZE, 0);
	vector<int> path_x(XY_SUM_SIZE), path_y(XY_SUM_SIZE);

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (!get_water_enabled(j, i)) { // disabled
				wminside[i][j] = 0;
				continue;
			}
			int x(j), y(i);
			int const crp(point_interior_to_mesh(j, i) ? calc_rest_pos(path_x, path_y, rp_set, x, y) : 0);
			wminside[i][j] = ((mode == 1 && mesh_height[y][x] < water_plane_z) ? 2 : crp);
		}
	}
	calc_water_flow();
	init_water_springs(NUM_WATER_SPRINGS);
	matrix_clear_2d(ripples);
	first_water_run = 1;

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (wminside[i][j] == 1) { // dynamic water
				int const wsi(watershed_matrix[i][j].wsi);
				assert(wsi >= 0 && wsi < (int)valleys.size());
				water_matrix[i][j] = valleys[wsi].zval;
			}
			else if (wminside[i][j] == 2) { // fixed water
				water_matrix[i][j] = water_plane_z;
			}
			else { // no water
				water_matrix[i][j] = def_water_level; // this seems safe
			}
			short &i8(watershed_matrix[i][j].inside8);
			i8 = 0;
			// 00 0- -0 0+ +0 -- +- ++ -+  22  11
			// 01 02 04 08 10 20 40 80 100 200 400
			if (wminside[i][j])      i8 |= 0x01;
			if (wminside[i][j] == 2) i8 |= 0x200;
			if (wminside[i][j] == 1) i8 |= 0x400;
			
			if (j > 0 && wminside[i][j-1]) {
				i8 |= 0x02;
				if (i > 0 && wminside[i-1][j-1]) i8 |= 0x20;
			}
			if (i > 0 && wminside[i-1][j]) {
				i8 |= 0x04;
				if (j < MESH_X_SIZE-1 && wminside[i-1][j+1]) i8 |= 0x100;
			}
			if (j < MESH_X_SIZE-1 && wminside[i][j+1]) {
				i8 |= 0x08;
				if (i < MESH_Y_SIZE-1 && wminside[i+1][j+1]) i8 |= 0x80;
			}
			if (i < MESH_Y_SIZE-1 && wminside[i+1][j]) {
				i8 |= 0x10;
				if (j > 0 && wminside[i+1][j-1]) i8 |= 0x40;
			}
		} // for j
	} // for i
}


int calc_rest_pos(vector<int> &path_x, vector<int> &path_y, vector<char> &rp_set, int &x, int &y) { // return 0 if off the map

	int path_counter(0), x2(0), y2(0);
	bool found(0);

	do {
		if (path_counter < XY_SUM_SIZE) {
			path_x[path_counter] = x;
			path_y[path_counter] = y;
			++path_counter;
		}
		char const rps(rp_set[y*MESH_X_SIZE+x]);

		if (rps) {
			x2    = watershed_matrix[y][x].x;
			y2    = watershed_matrix[y][x].y;
			x     = x2;
			y     = y2;
			found = (rps == 1);
			break;
		}
		x2 = w_motion_matrix[y][x].x;
		y2 = w_motion_matrix[y][x].y;
		if (x2 == x && y2 == y) {found = 1; break;}
		x = x2;
		y = y2;
	} while (point_interior_to_mesh(x, y));

	for (--path_counter; path_counter >= 0; --path_counter) {
		int const x2(path_x[path_counter]), y2(path_y[path_counter]);
		watershed_matrix[y2][x2].x = x;
		watershed_matrix[y2][x2].y = y;
		rp_set[y2*MESH_X_SIZE+x2]  = (1 + (found == 0));
	}
	return found;
}


bool add_water_section(float x1, float y1, float x2, float y2, float zval, float wvol) {

	water_section ws(x1, y1, x2, y2, zval, wvol);
	if (!ws.is_nonzero()) return 0;
	wsections.push_back(ws);
	return 1;
}


void calc_water_flow() {

	if (DISABLE_WATER == 1) return;
	vector<int> minima;
	map<pair<int, int>, valley> pool_zvals;

	if (scrolling) { // save old pool zval state
		for (unsigned i = 0; i < valleys.size(); ++i) {
			pool_zvals.insert(make_pair(make_pair((valleys[i].x - dx_scroll), (valleys[i].y - dy_scroll)), valleys[i]));
		}
	}
	valleys.clear();
	spill.clear();

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (wminside[i][j] != 1) continue;
			int const wmij(watershed_matrix[i][j].x + MESH_X_SIZE*watershed_matrix[i][j].y);
			if (minima.empty() || wmij != minima.back()) minima.push_back(wmij);
		}
	}
	unsigned const msize((unsigned)minima.size());
	if (msize == 0) return;
	std::sort(minima.begin(), minima.end());
	total_watershed = 0;

	for (unsigned i = 0; i < wsections.size(); ++i) {
		valleys.push_back(valley(((wsections[i].x1 + wsections[i].x2) >> 1), ((wsections[i].y1 + wsections[i].y2) >> 1)));
	}
	for (unsigned i = 0; i < msize; ++i) {
		if (i == msize-1 || minima[i] != minima[i+1]) {
			valley const v((minima[i]%MESH_X_SIZE), (minima[i]/MESH_X_SIZE));
			if (!(mesh_height[v.y][v.x] <= water_plane_z || !get_water_enabled(v.x, v.y))) valleys.push_back(v);
		}
	}
	if (valleys.size() > 1000) cout << "Warning: This landscape contains " << valleys.size() << " water pooling locations." << endl;
	
	if (valleys.size() > 32767) {
		std::cerr << "Error: Too many water pools. Max is 32767." << endl;
		exit(1);
	}
	spill.init((unsigned)valleys.size());
	vector<int> vmap(XY_MULT_SIZE, 0);

	for (unsigned k = 0; k < valleys.size(); ++k) {
		int const index(valleys[k].x + MESH_X_SIZE*valleys[k].y);
		assert(index < XY_MULT_SIZE);
		vmap[index] = k+1;
	}
	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (wminside[i][j] == 1) {
				int const index(watershed_matrix[i][j].x + MESH_X_SIZE*watershed_matrix[i][j].y);
				assert(index < XY_MULT_SIZE);

				if (vmap[index] == 0) {
					wminside[i][j] = 0;
				}
				else {
					watershed_matrix[i][j].wsi = vmap[index]-1;
				}
				++total_watershed;
			}
			else {
				watershed_matrix[i][j].wsi = -1; // invalid
			}
		}
	}
	for (unsigned i = 0; i < wsections.size(); ++i) {
		int const x1(max(0, wsections[i].x1)), y1(max(0, wsections[i].y1));
		int const x2(min(MESH_X_SIZE-1, wsections[i].x2)), y2(min(MESH_Y_SIZE-1, wsections[i].y2));
		assert(x1 <= x2 && y1 <= y2);

		for (int y = y1; y < y2; ++y) {
			for (int x = x1; x < x2; ++x) {
				wminside[y][x]             = 1;
				watershed_matrix[y][x].wsi = i;

				if (water_enabled != NULL) { // not sure about this
					if (y > y1 && y < y2-1 && x > x1 && x < x2-1) water_enabled[y][x] = 1;
				}
			}
		}
	}
	for (unsigned i = 0; i < valleys.size(); ++i) {
		valleys[i].create(i);
			
		if (scrolling) { // attempt to restore zval state
			auto it(pool_zvals.find(make_pair(valleys[i].x, valleys[i].y)));
			
			if (it != pool_zvals.end()) { // not quite right - lacks spill/merge dependencies and other per-frame state, pool might be cut off by the mesh
				valleys[i].copy_state_from(it->second);
			}
		}
	}
	assert(valleys.size() >= wsections.size());

	for (unsigned i = 0; i < wsections.size(); ++i) {
		valleys[i].zval = valleys[i].min_zval = wsections[i].zval;
		valleys[i].fvol = wsections[i].wvol;
		valleys[i].lwv  = valleys[i].fvol;
	}
}


void init_water_springs(int nws) {

	if (added_wsprings || nws == 0) return;
	water_springs.clear();
	unsigned const smod(max(1, (XY_MULT_SIZE/nws)));

	for (int i = 1; i < MESH_Y_SIZE-1; ++i) {
		for (int j = 1; j < MESH_X_SIZE-1; ++j) {
			set_rand2_state((54563  *(i + yoff2) + 23423  *rand_gen_index), (4365435*(j + xoff2) + 6456541*rand_gen_index)); // not prime
			if ((rand2()%smod) != 0)   continue;
			point const pos(get_xval(j), get_yval(i), (interpolate_mesh_zval(get_xval(j), get_yval(i), 0.0, 0, 1) + 0.02));
			if (pos.z < water_plane_z) continue;
			water_springs.push_back(water_spring(1, rand_uniform2(1.5, 3.0), 0.1, 0.0, pos, gen_rand_vector2(5.0, 3.0, PI_TWO)));
		}
	}
}


void process_water_springs() {

	if (DISABLE_WATER && INIT_DISABLE_WATER) return;
	int const cid(coll_id[WDROPLET]);
	if (!begin_motion || !animate || !animate2 || temperature <= W_FREEZE_POINT) return;
	if (ztop < water_plane_z)  return; // all water
	if (water_springs.empty()) return;

	for (unsigned i = 0; i < water_springs.size(); ++i) {
		water_spring &ws(water_springs[i]);
		if (!ws.enabled) continue;
		
		if (temperature > get_max_t(WDROPLET)) { // water boils
			if (rand()&1) gen_smoke(ws.pos);
			continue;
		}
		ws.acc += fticks*(TIMESTEP/DEF_TIMESTEP)*ws.dpf;

		for (unsigned j = 0; j < ws.acc; ++j) {
			int const k(obj_groups[cid].choose_object());
			obj_groups[cid].create_object_at(k, ws.pos);
			vadd_rand(obj_groups[cid].get_obj(k).pos, 0.02*ws.diff);
			obj_groups[cid].get_obj(k).velocity = ws.vel + gen_rand_vector(ws.vel.mag()*ws.diff, 1.0, PI);
		}
		ws.acc -= (int)ws.acc;
	}
}


void add_water_spring(point const &pos, vector3d const &vel, float rate, float diff, int calc_z, int gen_vel) {

	vector3d const vel0(gen_vel ? gen_rand_vector(5.0, 3.0, PI_TWO) : vel);
	water_spring ws(1, rate, diff, 0.0, pos, vel0);
	if (calc_z) ws.pos.z = interpolate_mesh_zval(pos.x, pos.y, 0.0, 0, 1) + 0.02;
	water_springs.push_back(ws);
	++added_wsprings;
}


void shift_water_springs(vector3d const &vd) {

	if (added_wsprings == 0) return; // dynamically created, not placed

	for (unsigned i = 0; i < water_springs.size(); ++i) {
		water_springs[i].pos += vd;
	}
}


float get_water_zmin(float mheight) {
	return max(def_water_level, (mheight - (float)G_W_START_DEPTH));
}


void make_outside_water(int x, int y) {

	if (wminside[y][x] == 2) return; // already outside water
	wminside[y][x] = 2; // make outside water (anything else we need to update? what if all of a valley disappears?)
	watershed_matrix[y][x].wsi = -1; // invalid
	water_matrix[y][x] = water_plane_z; // may be unnecessary
}


void update_water_zval(int x, int y, float old_mh) {

	assert(!point_outside_mesh(x, y));
	if (!get_water_enabled(x, y)) return; // ???

	if (mesh_height[y][x] < water_plane_z) { // check if this pos is under the mesh
		make_outside_water(x, y); // previously above the mesh
	}
	if (wminside[y][x] != 1) return; // not inside water
	int const wsi(watershed_matrix[y][x].wsi);
	assert(wsi >= 0);
	if (wsi < (int)wsections.size()) return; // don't update water sections
	valley &v(valleys[wsi]);
	float const mh(mesh_height[y][x]), new_zmin(get_water_zmin(mh));
	// could use delta_h = (mh - old_mh) for updating volume
	// FIXME: add new valley if there is a new local mimima?
	if (new_zmin >= v.min_zval) return; // no change
	//v.zval += (v.min_zval - new_zmin);
	if (v.zval == v.min_zval) {v.zval = new_zmin;} // no water yet, so move zval with zmin
	v.min_zval = new_zmin;
	v.x        = x;
	v.y        = y;
}


bool is_underwater(point const &pos, int check_bottom, float *depth) { // or under ice
	
	if (depth) {*depth = 0.0;}
	if (DISABLE_WATER || !(display_mode & 0x04)) return 0; // water disabled

	if (world_mode == WMODE_INF_TERRAIN) {
		if (pos.z < water_plane_z) {
			if (depth) {*depth = water_plane_z - pos.z;}
			return 1;
		}
		if (check_bottom && depth && update_depth_if_underwater(pos, *depth)) return 1; // check for player in city swimming pools and ponds
		return 0;
	}
	assert(water_matrix && mesh_height);
	if (!is_over_mesh(pos) || pos.z < zmin || pos.z > max_water_height) return 0;
	//if (pos.z < min_water_height) return 1;
	int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));
	if (!has_water(xpos, ypos)) return 0; // off the mesh or disabled

	if (pos.z <= water_matrix[ypos][xpos] && (!check_bottom || pos.z >= mesh_height[ypos][xpos])) {
		if (depth) {*depth = water_matrix[ypos][xpos] - pos.z;}
		return 1;
	}
	return 0;
}


void update_accumulation(int xpos, int ypos) {

	float acc(accumulation_matrix[ypos][xpos]);
	if (acc <= 0.0) return;
	has_accumulation = 1; // melt snow
	float melted(((temperature - W_FREEZE_POINT)/MELT_RATE)*(NIGHT_MELT + (1.0 - NIGHT_MELT)*light_factor));
	//if (light_factor >= 0.5 && shadow_mask != NULL && (shadow_mask[LIGHT_SUN][ypos][xpos] & SHADOWED_ALL)) {melted *= SHADE_MELT;}
	accumulation_matrix[ypos][xpos] = max(0.0f, (acc - melted));

	if (!DISABLE_WATER && wminside[ypos][xpos] == 1) {
		int const wsi(watershed_matrix[ypos][xpos].wsi);
		valleys[wsi].fvol = min(4.0f*MAX_WATER_ACC, valleys[wsi].fvol + SNOW_ACC*melted);
	}
}


int get_water_wsi(int xpos, int ypos) {

	if (point_outside_mesh(xpos, ypos) || wminside[ypos][xpos] != 1) return -1;
	int const wsi(watershed_matrix[ypos][xpos].wsi);
	assert(size_t(wsi) < valleys.size());
	return wsi;
}

void select_liquid_color(colorRGBA &color, int xpos, int ypos) {

	int const wsi(get_water_wsi(xpos, ypos));
	if (wsi < 0) return;
	if (water_is_lava) {color = LAVA_COLOR; return;}
	float const blood_mix(valleys[wsi].blood_mix), mud_mix(valleys[wsi].mud_mix);
	if (mud_mix   > 0.0) {blend_color(color, MUD_C,   color, mud_mix,   1);}
	if (blood_mix > 0.0) {blend_color(color, BLOOD_C, color, blood_mix, 1);}
}

void select_liquid_color(colorRGBA &color, point const &pos) {
	select_liquid_color(color, get_xpos(pos.x), get_ypos(pos.y));
}


float get_blood_mix(point const &pos) {

	int const wsi(get_water_wsi(get_xpos(pos.x), get_ypos(pos.y)));
	if (wsi < 0) return 0.0;
	return valleys[wsi].blood_mix;
}


void valley::copy_state_from(valley const &v) {

	zval      = v.zval;
	w_volume  = v.w_volume;
	blood_mix = v.blood_mix;
	mud_mix   = v.mud_mix;
	fvol      = v.fvol;
	dz        = v.dz;
	lwv       = v.lwv;
}


void valley::create(int wsi) {

	assert(!point_outside_mesh(x, y));
	zval = min_zval = get_water_zmin(mesh_height[y][x]);

	if (size_t(watershed_matrix[y][x].wsi) >= wsections.size() && watershed_matrix[y][x].wsi != wsi) {
		cout << "Error in valley::create(): Invalid pool index: " << wsi << " vs. " << watershed_matrix[y][x].wsi << "." << endl;
		assert(0);
	}
	if (!wminside[y][x]) {
		cout << "Error in valley::create(): Zero flag in watershed matrix." << endl;
		assert(0);
	}
}


float valley::get_volume() const {

	return (blood_mix*BLOOD_VOLUME + (1.0 - blood_mix)*RAIN_VOLUME);
}



