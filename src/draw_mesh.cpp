// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 9/27/02

#include "3DWorld.h"
#include "mesh.h"
#include "textures.h"
#include "gl_ext_arb.h"
#include "shaders.h"
#include "draw_utils.h"


float const W_TEX_SCALE0     = 1.0;
float const WATER_WIND_EFF   = 0.0005;
float const SURF_HEAL_RATE   = 0.005;
float const MAX_SURFD        = 20.0;
int   const SHOW_MESH_TIME   = 0;
int   const SHOW_NORMALS     = 0;
int   const DEBUG_COLLS      = 0; // 0 = disabled, 1 = lines, 2 = cubes
int   const DISABLE_TEXTURES = 0;


struct fp_ratio {
	float n, d;
	inline float get_val() const {return ((d == 0.0) ? 1.0 : n/d);}
};


// Global Variables
bool clear_landscape_vbo(0), clear_mvd_vbo(0);
float lt_green_int(1.0), water_xoff(0.0), water_yoff(0.0), wave_time(0.0);
vector<fp_ratio> uw_mesh_lighting; // for water caustics

extern bool using_lightmap, combined_gu, has_snow, detail_normal_map, use_core_context, underwater, water_is_lava, have_indir_smoke_tex, water_is_lava, fog_enabled;
extern bool enable_ground_csm;
extern int num_local_minima, world_mode, xoff, yoff, xoff2, yoff2, ground_effects_level, animate2;
extern int display_mode, frame_counter, verbose_mode, DISABLE_WATER, read_landscape, disable_inf_terrain, mesh_detail_tex;
extern float zmax, zmin, ztop, zbottom, light_factor, max_water_height, init_temperature, univ_temp, atmosphere, mesh_scale_z, snow_cov_amt, CAMERA_RADIUS;
extern float water_plane_z, temperature, fticks, mesh_scale, mesh_z_cutoff, TWO_XSS, TWO_YSS, XY_SCENE_SIZE, FAR_CLIP, sun_radius, ocean_depth_opacity_mult;
extern point litning_pos, sun_pos, moon_pos;
extern vector3d up_norm, wind;
extern colorRGB mesh_color_scale;
extern colorRGBA bkg_color, cur_fog_color;
extern float h_dirt[];
extern water_params_t water_params;


void draw_sides_and_bottom(bool shadow_pass);
void set_cloud_intersection_shader(shader_t &s);
void set_indir_lighting_block(shader_t &s, bool use_smoke, bool use_indir);
bool no_sparse_smap_update();
bool draw_distant_water();
bool use_water_plane_tess();
bool enable_ocean_waves();
void set_smap_enable_for_shader(shader_t &s, bool enable_smap, int shader_type);
void setup_mesh_and_water_shader(shader_t &s, bool use_detail_normal_map, bool is_water);
colorRGB get_underwater_atten_color(float mud_amt);


float camera_min_dist_to_surface() { // min dist of four corners and center

	point pos;
	get_matrix_point(0, 0, pos);
	float dist(distance_to_camera(pos));
	get_matrix_point(MESH_X_SIZE-1, 0, pos);
	dist = min(dist, distance_to_camera(pos));
	get_matrix_point(0, MESH_Y_SIZE-1, pos);
	dist = min(dist, distance_to_camera(pos));
	get_matrix_point(MESH_X_SIZE-1, MESH_Y_SIZE-1, pos);
	dist = min(dist, distance_to_camera(pos));
	get_matrix_point(MESH_X_SIZE/2, MESH_Y_SIZE/2, pos);
	dist = min(dist, distance_to_camera(pos));
	return dist;
}


float integrate_water_dist(point const &targ_pos, point const &src_pos, float const water_z) {

	if (src_pos.z == targ_pos.z) return 0.0;
	float const t(min(1.0f, (water_z - targ_pos.z)/fabs(src_pos.z - targ_pos.z))); // min(1.0,...) for underwater case
	point p_int(targ_pos + (src_pos - targ_pos)*t);
	
	if (world_mode == WMODE_GROUND) {
		int const xp(get_xpos(targ_pos.x)), yp(get_ypos(targ_pos.y));
		if (!point_outside_mesh(xp, yp)) {p_int.z = min(src_pos.z, water_matrix[yp][xp]);} // account for ripples
	}
	return p2p_dist(p_int, targ_pos)*mesh_scale;
}


void water_color_atten_pt(float *c, int x, int y, point const &pos, point const &p1, point const &p2) {

	float const scale(WATER_COL_ATTEN*((world_mode != WMODE_GROUND || wminside[y][x] == 2) ? 1.0 : 4.0));
	float const wh((world_mode == WMODE_GROUND) ? water_matrix[y][x] : water_plane_z); // higher for interior water
	float const dist(scale*(integrate_water_dist(pos, p1, wh) + integrate_water_dist(pos, p2, wh)));
	atten_by_water_depth(c, dist);
}


void gen_uw_lighting() {

	uw_mesh_lighting.resize(MESH_X_SIZE*MESH_Y_SIZE);
	point const lpos(get_light_pos());
	float const ssize(X_SCENE_SIZE + Y_SCENE_SIZE + Z_SCENE_SIZE);
	vector<point> rows[2]; // {last, current} y rows
	for (unsigned i = 0; i < 2; ++i) rows[i].resize(MESH_X_SIZE, all_zeros);
	float const dxy_val_inv[2] = {DX_VAL_INV, DY_VAL_INV};
	float const refract_ix((temperature <= W_FREEZE_POINT) ? ICE_INDEX_REFRACT : WATER_INDEX_REFRACT);

	for (vector<fp_ratio>::iterator i = uw_mesh_lighting.begin(); i != uw_mesh_lighting.end(); ++i) {
		i->n = i->d = 0.0; // initialize
	}
	for (int y = 0; y < MESH_Y_SIZE; ++y) {
		for (int x = 0; x < MESH_X_SIZE; ++x) {
			if (!mesh_is_underwater(x, y)) continue;
			point const p1(get_xval(x), get_yval(y), water_matrix[y][x]); // point on water surface
			vector3d const dir(p1 - lpos);
			vector3d v_refract(dir);
			bool const refracted(calc_refraction_angle(dir, v_refract, wat_vert_normals[y][x], 1.0, refract_ix));
			assert(refracted); // can't have total internal reflection going into the water if the physics are sane
			point const p2(p1 + v_refract.get_norm()*ssize); // distant point along refraction vector
			point cpos;
			if (!line_intersect_mesh(p1, p2, cpos, 1, 1)) continue; // no intersection
			rows[1][x] = cpos;
			if (x == 0 || y == 0) continue; // not an interior point
			if (rows[0][x].z == 0.0 || rows[0][x-1].z == 0.0 || rows[1][x-1].z == 0.0) continue; // incomplete block
			float rng[2][2] = {{cpos.x, cpos.y}, {cpos.x, cpos.y}}; // {min,max} x {x,y} - bounds of mesh surface light patch through this water patch
			int bnds[2][2]; // {min,max} x {x,y} - integer mesh index bounds

			for (unsigned d = 0; d < 2; ++d) { // x,y
				for (unsigned e = 0; e < 2; ++e) { // last,cur y (row)
					for (unsigned f = 0; f < 2; ++f) { // last,cur x
						rng[0][d] = min(rng[0][d], rows[e][x-f][d]);
						rng[1][d] = max(rng[1][d], rows[e][x-f][d]);
					}
				}
				assert(rng[0][d] < rng[1][d]);
				bnds[0][d] = max(0,              int(floor((rng[0][d] + SCENE_SIZE[d])*dxy_val_inv[d])));
				bnds[1][d] = min(MESH_SIZE[d]-1, int(ceil ((rng[1][d] + SCENE_SIZE[d])*dxy_val_inv[d])));
				assert(bnds[0][d] <= bnds[1][d]); // can this fail?
			}
			float const weight_n(1.0f/((rng[1][0] - rng[0][0])*(rng[1][1] - rng[0][1]))); // weight of this patch of light
			float const weight_d(1.0f/(DX_VAL*DY_VAL));
			float const init_cr[2] = {(-X_SCENE_SIZE + DX_VAL*bnds[0][0]), (-Y_SCENE_SIZE + DY_VAL*bnds[0][1])};
			float crng[2][2]; // {min,max} x {x,y} - range of this mesh quad
			crng[0][1] = init_cr[1];
			crng[1][1] = init_cr[1] + DY_VAL;

			for (int yy = bnds[0][1]; yy < bnds[1][1]; ++yy) {
				assert(yy >= 0 && yy < MESH_Y_SIZE);
				float const ysz(min(crng[1][1], rng[1][1]) - max(crng[0][1], rng[0][1])); // intersection: min(UB) - max(LB)
				assert(ysz >= 0.0);
				if (ysz <= 0.0) continue;
				crng[0][0] = init_cr[0];
				crng[1][0] = init_cr[0] + DX_VAL;

				for (int xx = bnds[0][0]; xx < bnds[1][0]; ++xx) {
					assert(xx >= 0 && xx < MESH_X_SIZE);
					float const xsz(min(crng[1][0], rng[1][0]) - max(crng[0][0], rng[0][0])); // intersection: min(UB) - max(LB)
					assert(xsz >= 0.0);
					if (xsz <= 0.0) continue;
					unsigned const ix(yy*MESH_X_SIZE + xx);
					uw_mesh_lighting[ix].n += weight_n*xsz*ysz; // amount of light through patch of water hitting this mesh quad
					uw_mesh_lighting[ix].d += weight_d*xsz*ysz;
					crng[0][0] += DX_VAL;
					crng[1][0] += DX_VAL;
				}
				crng[0][1] += DY_VAL;
				crng[1][1] += DY_VAL;
			}
		} // for x
		rows[0].swap(rows[1]);
		for (int x = 0; x < MESH_X_SIZE; ++x) rows[1][x] = all_zeros; // reset last row
	} // for y
}


// texture units used: 0: terrain texture, 1/10: detail texture
void set_landscape_texgen(float tex_scale, int xoffset, int yoffset, int xsize, int ysize, shader_t &shader, unsigned detail_tu_id) {

	float const tx(tex_scale*(((float)xoffset)/((float)xsize) + 0.5));
	float const ty(tex_scale*(((float)yoffset)/((float)ysize) + 0.5));
	setup_texgen(tex_scale/TWO_XSS, tex_scale/TWO_YSS, tx, ty, 0.0, shader, 0);
	select_texture(mesh_detail_tex, detail_tu_id); // detail texture
}

void set_landscape_texture_texgen(shader_t &shader) {

	if (!DISABLE_TEXTURES) {
		select_texture(LANDSCAPE_TEX);
		set_landscape_texgen(1.0, xoff, yoff, MESH_X_SIZE, MESH_Y_SIZE, shader, 10);
	}
}


vao_manager_t mesh_data_vao_mgr;

void draw_mesh_vbo(bool shadow_pass) {

	if (clear_landscape_vbo) {
		mesh_data_vao_mgr.clear();
		clear_landscape_vbo = 0;
	}
	//if (shadow_pass && mesh_data_vao_mgr.vbo == 0 && no_sparse_smap_update()) return;
	// Note: using 4-byte indexed quads takes about the same amount of GPU memory
	// Note: ignores detail texture
	colorRGBA const color(mesh_color_scale*DEF_DIFFUSE);
	shader_t s;

	if (!shadow_pass && shadow_map_enabled()) { // enable shadows and some other effects
		setup_mesh_and_water_shader(s, detail_normal_map, 0);
		s.set_cur_color(color);
	}
	else {
		s.begin_simple_textured_shader(0.0, !shadow_pass, 1, &color); // lighting + texgen
	}
	set_landscape_texture_texgen(s);
	
	if (mesh_data_vao_mgr.vbo == 0) {
		vector<vert_norm_comp> data; // vertex and normals
		data.reserve(2*MESH_X_SIZE*(MESH_Y_SIZE-1));

		for (int i = 0; i < MESH_Y_SIZE-1; ++i) {
			for (int j = 0; j < MESH_X_SIZE; ++j) {
				for (unsigned k = 0; k < 2; ++k) {data.emplace_back(get_mesh_xyz_pos(j, i+k), vertex_normals[i+k][j]);}
			}
		}
		mesh_data_vao_mgr.create_and_upload(data, 0, 1); // and setup pointers
		bind_vbo(0); // unbind mesh vbo
	}
	mesh_data_vao_mgr.enable_vao();

	for (int i = 0; i < MESH_Y_SIZE-1; ++i) { // use glMultiDrawArrays()?
		glDrawArrays(GL_TRIANGLE_STRIP, 2*i*MESH_X_SIZE, 2*MESH_X_SIZE);
		++num_frame_draw_calls;
	}
	mesh_data_vao_mgr.disable_vao();
	s.end_shader();
}


void setup_detail_normal_map_prefix(shader_t &s, bool enable) {

	if (enable) {
		s.set_prefix("#define USE_BUMP_MAP",    1); // FS
		s.set_prefix("#define USE_BUMP_MAP_DL", 1); // FS
		s.set_prefix("#define BUMP_MAP_CUSTOM", 1); // FS
		s.set_prefix(make_shader_bool_prefix("use_fg_ViewMatrix", 0), 1); // FS - disabled
	}
}

void setup_detail_normal_map(shader_t &s, float tscale) { // also used for tiled terrain mesh
	select_texture(ROCK_NORMAL_TEX, 11);
	s.add_uniform_int("detail_normal_tex", 11);
	s.add_uniform_vector2d("detail_normal_tex_scale", vector2d(tscale*X_SCENE_SIZE, tscale*Y_SCENE_SIZE));
}

void setup_shader_underwater_atten(shader_t &s, float atten_scale, float mud_amt) {
	s.add_uniform_float("water_atten",    atten_scale);
	s.add_uniform_color("uw_atten_max",   uw_atten_max);
	s.add_uniform_color("uw_atten_scale", ((mud_amt > 0.0) ? get_underwater_atten_color(mud_amt) : uw_atten_scale));
}


// tu_ids used: 0: diffuse map, 1: indir lighting, 2-4 dynamic lighting, 5: bump map, 6-7 shadow map, 8: cloud shadow texture, 10: detail map, 11: detail normal map
void setup_mesh_and_water_shader(shader_t &s, bool use_detail_normal_map, bool is_water) {

	bool const cloud_shadows(!has_snow && atmosphere > 0.0 && ground_effects_level >= 2);
	bool const indir_lighting(using_lightmap && have_indir_smoke_tex), use_smap(shadow_map_enabled());
	s.setup_enabled_lights(2, 2); // FS
	set_dlights_booleans(s, 1, 1); // FS
	s.check_for_fog_disabled();
	if (cloud_shadows) {s.set_prefix("#define ENABLE_CLOUD_SHADOWS", 1);} // FS
	if (use_smap && enable_ground_csm) {s.set_prefix("#define ENABLE_CASCADED_SHADOW_MAPS", 1);} // FS
	s.set_prefix("in vec3 eye_norm;", 1); // FS
	setup_detail_normal_map_prefix(s, use_detail_normal_map);
	s.set_prefix(make_shader_bool_prefix("indir_lighting", indir_lighting), 1); // FS
	s.set_prefix(make_shader_bool_prefix("hemi_lighting",  0), 1); // FS (disabled)
	s.set_prefix(make_shader_bool_prefix("use_shadow_map", use_smap), 1); // FS
	s.set_vert_shader("texture_gen.part+draw_mesh");
	s.set_frag_shader("ads_lighting.part*+shadow_map.part*+dynamic_lighting.part*+indir_lighting.part+linear_fog.part+detail_normal_map.part+cloud_sphere_shadow.part+draw_mesh");
	s.begin_shader();
	if (use_smap) {set_smap_shader_for_all_lights(s);}
	set_indir_lighting_block(s, 0, indir_lighting); // calls setup_scene_bounds()
	s.setup_fog_scale();
	setup_dlight_textures(s);
	s.add_uniform_int("tex0", 0);
	s.add_uniform_int("tex1", 10);
	s.add_uniform_float("snow_cov_amt", ((is_water && temperature >= W_FREEZE_POINT) ? 0.0 : snow_cov_amt)); // 0 for water; should this be set to 0 for ice?
	if (use_detail_normal_map) {setup_detail_normal_map(s, 2.0);}
	if (cloud_shadows        ) {set_cloud_intersection_shader(s);}
}


class mesh_data_store {
protected:
	struct norm_color_ix {
		vector3d n;
		color_wrapper c;
		int ix;
		norm_color_ix() : ix(-1) {}
		void assign(vert_norm_color const &vnc, int ix_) {n = vnc.n; c = (color_wrapper)vnc; ix = ix_;}
	};

	vector<vert_norm_color> data;
	vector<norm_color_ix> last_rows;

	void update_vertex(int i, int j) {
		float color_scale(DEF_DIFFUSE);
		float &sd(surface_damage[i][j]);

		if (sd > 0.0) {
			if (!reflection_pass) {sd = min(MAX_SURFD, max(0.0f, (sd - fticks*SURF_HEAL_RATE)));}
			color_scale *= max(0.0f, (1.0f - sd));
		}
		colorRGB color(mesh_color_scale*color_scale);
		data[c].n = vertex_normals[i][j];

		// water light attenuation: total distance from sun/moon, reflected off bottom, to viewer
		if (!DISABLE_WATER && data[c].v.z < max_water_height && data[c].v.z < water_matrix[i][j]) {
			water_color_atten(&color.R, j, i, data[c].v);

			if (wminside[i][j] == 1) {
				colorRGBA wc(WHITE);
				select_liquid_color(wc, j, i);
				UNROLL_3X(color[i_] *= wc[i_];)
			}
			if (!uw_mesh_lighting.empty()) { // water caustics: slow and low resolution, but conceptually interesting
				// Note: normal is never set to zero because we need it for dynamic light sources
				data[c].n *= max((float)pow(uw_mesh_lighting[i*MESH_X_SIZE + j].get_val(), 8), 0.01f); // enhance the contrast (can be > 1.0)
			}
		}
		data[c].set_c3(color);
	}

public:
	unsigned c;
	bool reflection_pass;

	mesh_data_store() : c(0), reflection_pass(0) {last_rows.resize(MESH_X_SIZE+1);}

	bool add_mesh_vertex_pair(int i, int j, float x, float y) {
		if (is_mesh_disabled(j, i) || is_mesh_disabled(j, i+1)) return 0;
		if (mesh_z_cutoff > -FAR_DISTANCE && mesh_z_cutoff > max(mesh_height[i][j], mesh_height[i+1][j])) return 0;
		
		for (unsigned p = 0; p < 2; ++p, ++c, y += DY_VAL) {
			int const iinc(min((MESH_Y_SIZE-1), int(i+p)));
			//assert(c < data.size());
			data[c].v.assign(x, y, mesh_height[iinc][j]);
			//assert(unsigned(j) < last_rows.size());
		
			if (last_rows[j].ix == iinc) { // gets here nearly half the time
				data[c].n = last_rows[j].n;
				UNROLL_4X(data[c].c[i_] = last_rows[j].c.c[i_];)
			}
			else {
				update_vertex(iinc, j);
				last_rows[j].assign(data[c], iinc);
			}
		}
		return 1;
	}
};


class mesh_vertex_draw : public mesh_data_store {
public:
	mesh_vertex_draw() {
		data.resize(2*(MESH_X_SIZE+1));
		vert_norm_color::set_vbo_arrays(1, &data.front());
	}
	void emit_strip() {
		if (c >= 3) {glDrawArrays(GL_TRIANGLE_STRIP, 0, c); ++num_frame_draw_calls;} // at least one triangle
		c = 0;
	}
};


class mesh_vertex_draw_vbo : public vao_manager_t, public mesh_data_store {

	vector<unsigned> strip_ixs;

public:
	mesh_vertex_draw_vbo() {
		data.resize(2*(MESH_X_SIZE+1)*MESH_Y_SIZE);
		strip_ixs.reserve(MESH_Y_SIZE+1);
	}
	void begin_draw() {strip_ixs.resize(1, 0); c = 0;}
	void emit_strip() {strip_ixs.push_back(c);}

	void final_draw() {
		if (!vbo) { // create new vbo
			data.resize(c); // resize smaller if possible (in cases where mesh_enable reduces the vertex count)
			create_and_upload(data, 2, 1); // streaming
		}
		else {
			pre_render(1);
			upload_vector_to_vbo(data);
		}
		for (vector<unsigned>::const_iterator i = strip_ixs.begin(); i+1 != strip_ixs.end(); ++i) { // skip last element
			glDrawArrays(GL_TRIANGLE_STRIP, *i, (*(i+1) - *i));
			++num_frame_draw_calls;
		}
		post_render();
	}
};


template<typename T> void draw_mesh_mvd_core(T &mvd) {

	// clamp mesh draw range to the camera x/y value for x/y cube map faces
	int x_start(0), x_end(MESH_X_SIZE-1), y_start(0), y_end(MESH_Y_SIZE-1);
	if      (cview_dir ==  plus_x) {x_start = max(x_start, get_xpos(get_camera_pos().x)-1);}
	else if (cview_dir == -plus_x) {x_end   = min(x_end,   get_xpos(get_camera_pos().x)+1);}
	else if (cview_dir ==  plus_y) {y_start = max(y_start, get_ypos(get_camera_pos().y)-1);}
	else if (cview_dir == -plus_y) {y_end   = min(y_end,   get_ypos(get_camera_pos().y)+1);}

	for (int i = y_start; i < y_end; ++i) {
		float const y(get_yval(i));

		for (int j = x_start; j < x_end; ++j) {
			if (!mvd.add_mesh_vertex_pair(i, j, get_xval(j), y)) {mvd.emit_strip();}
		}
		mvd.add_mesh_vertex_pair(i, x_end, get_xval(x_end), y);
		mvd.emit_strip();
	} // for i
}

mesh_vertex_draw_vbo mvd_vbo;
tile_blend_tex_data_t mesh_tbt_data;

void clear_landscape_vbo_now() { // called during context switch or shutdown
	mesh_data_vao_mgr.clear();
	mvd_vbo.clear();
	mesh_tbt_data.clear_context();
}

void draw_mesh_mvd(bool reflection_pass) {

	shader_t s;
	s.set_prefix("#define MULT_DETAIL_TEXTURE", 1); // FS
	setup_mesh_and_water_shader(s, detail_normal_map, 0);
	set_landscape_texture_texgen(s);

	if (use_core_context) {
		mvd_vbo.reflection_pass = reflection_pass;
		if (clear_mvd_vbo) {mvd_vbo.clear(); clear_mvd_vbo = 0;}
		mvd_vbo.begin_draw();
		draw_mesh_mvd_core(mvd_vbo);
		mvd_vbo.final_draw();
	}
	else {
		mesh_vertex_draw mvd;
		mvd.reflection_pass = reflection_pass;
		draw_mesh_mvd_core(mvd);
	}
	s.end_shader();
}


void display_mesh(bool shadow_pass, bool reflection_pass) { // fast array version

	if (mesh_height == NULL) return; // no mesh to display
	if (clear_landscape_vbo) {clear_mvd_vbo = 1;}

	if (shadow_pass) {
		draw_mesh_vbo(1);
		float lzmin(0.0);
		if (light_factor <= 0.4)      {lzmin = moon_pos.z;}
		else if (light_factor >= 0.6) {lzmin = sun_pos.z;}
		else                          {lzmin = min(sun_pos.z, moon_pos.z);}
		if (lzmin < zmax) {draw_sides_and_bottom(1);} // sun/moon is low on the horizon, so include the mesh sides
		return;
	}
	RESET_TIME;

	if ((display_mode & 0x80) && !water_is_lava && !DISABLE_WATER && zmin < max_water_height && ground_effects_level != 0) {
		gen_uw_lighting();
		if (SHOW_MESH_TIME) {PRINT_TIME("Underwater Lighting");}
	}
	else {
		uw_mesh_lighting.clear();
	}
	if (DEBUG_COLLS) {
		shader_t s;

		if (DEBUG_COLLS == 2) {
			enable_blend();
			s.begin_color_only_shader(colorRGBA(1.0, 0.0, 0.0, 0.1));

			for (int i = 0; i < MESH_Y_SIZE-1; ++i) {
				for (int j = 0; j < MESH_X_SIZE; ++j) {
					if (v_collision_matrix[i][j].zmin < v_collision_matrix[i][j].zmax) {
						point const p1(get_xval(j+0), get_yval(i+0),v_collision_matrix[i][j].zmin);
						point const p2(get_xval(j+1), get_yval(i+1),v_collision_matrix[i][j].zmax);
						draw_cube((p1 + p2)*0.5, (p2.x - p1.x), (p2.y - p1.y), (p2.z - p1.z), 0);
					}
				}
			}
			disable_blend();
		}
		else {
			ensure_outlined_polygons();
			s.begin_color_only_shader(BLUE);
			vector<vert_wrap_t> verts;

			for (int i = 0; i < MESH_Y_SIZE-1; ++i) {			
				for (int j = 0; j < MESH_X_SIZE; ++j) {
					for (unsigned d = 0; d < 2; ++d) {
						verts.push_back(point(get_xval(j), get_yval(i+d), max(czmin, v_collision_matrix[i+d][j].zmax)));
					}
				}
				draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
			}
			set_fill_mode();
		}
		s.end_shader();
	}
	if (!reflection_pass) {update_landscape_texture();}
	if (SHOW_MESH_TIME) {PRINT_TIME("Landscape Texture");}

	if (ground_effects_level == 0 || reflection_pass) { // simpler, more efficient mesh draw
		draw_mesh_vbo(0);
	}
	else { // slower mesh draw with more features (surface damage, underwater lighting and effects)
		draw_mesh_mvd(reflection_pass);
	}
	if (SHOW_MESH_TIME) {PRINT_TIME("Draw");}
	if (!reflection_pass) {draw_sides_and_bottom(0);} // not generally needed in the reflection pass, since reflective objects should be over the mesh

	if (SHOW_NORMALS) {
		vector<vert_wrap_t> verts;
		verts.reserve(2*XY_MULT_SIZE);
		shader_t s;
		s.begin_color_only_shader(RED);

		for (int i = 1; i < MESH_Y_SIZE-2; ++i) {
			for (int j = 1; j < MESH_X_SIZE-1; ++j) {
				point const pos(get_xval(j), get_yval(i), mesh_height[i][j]);
				verts.push_back(pos);
				verts.push_back(pos + 0.1*vertex_normals[i][j]);
			}
		}
		draw_verts(verts, GL_LINES);
		s.end_shader();
	}
	if (SHOW_MESH_TIME) {PRINT_TIME("Final");}
}


void add_vertex(vector<vert_norm_tc> &verts, vector3d const &n, float x, float y, float z, bool in_y, float tscale=1.0) { // xz or zy
	verts.emplace_back(point(x, y, z), n, tscale*(in_y ? z : x), tscale*(in_y ? y : z));
}

void draw_sides_and_bottom(bool shadow_pass) {

	int const lx(MESH_X_SIZE-1), ly(MESH_Y_SIZE-1);
	float const botz(zbottom - MESH_BOT_QUAD_DZ), z_avg(0.5f*(zbottom + ztop)), ts(4.0f/(X_SCENE_SIZE + Y_SCENE_SIZE));
	float const x1(-X_SCENE_SIZE), y1(-Y_SCENE_SIZE), x2(X_SCENE_SIZE-DX_VAL), y2(Y_SCENE_SIZE-DY_VAL);
	int const texture((!read_landscape && get_rel_height(z_avg, zmin, zmax) > lttex_dirt[2].zval) ? (int)ROCK_TEX : (int)DIRT_TEX);
	shader_t s;

	if (shadow_pass) {
		s.begin_shadow_map_shader();
		vert_wrap_t const bverts[4] = {point(x1, y1, botz), point(x1, y2, botz), point(x2, y2, botz), point(x2, y1, botz)};
		draw_verts(bverts, 4, GL_TRIANGLE_FAN); // bottom
		vector<vert_wrap_t> verts;
		verts.reserve(2*max(MESH_X_SIZE, MESH_Y_SIZE));

		for (unsigned d = 0; d < 2; ++d) {
			float const xlimit(d ? x2 : x1), ylimit(d ? y2 : y1);

			for (int i = 0; i < MESH_X_SIZE; ++i) { // y sides
				float const xv(get_xval(i));
				verts.push_back(point(xv, ylimit, botz));
				verts.push_back(point(xv, ylimit, mesh_height[d?ly:0][i]));
			}
			draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);

			for (int i = 0; i < MESH_Y_SIZE; ++i) { // x sides
				float const yv(get_yval(i));
				verts.push_back(point(xlimit, yv, botz));
				verts.push_back(point(xlimit, yv, mesh_height[i][d?lx:0]));
			}
			draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
		} // for d
	}
	else {
		if (DISABLE_TEXTURES) {
			s.begin_simple_textured_shader(0.0, 1, 0, &WHITE); // with lighting
			select_texture(DISABLE_TEXTURES ? WHITE_TEX : texture);
		}
		else {
			mesh_tbt_data.ensure_textures(texture);
			s.setup_enabled_lights(2, 1); // sun and moon VS lighting
			s.set_vert_shader("ads_lighting.part*+two_lights_texture");
			s.set_frag_shader("linear_fog.part+tiling_and_blending.part+textured_with_tb");
			s.check_for_fog_disabled();
			s.begin_shader();
			s.set_cur_color(WHITE);
			if (fog_enabled) {s.setup_fog_scale();}
			mesh_tbt_data.bind_shader(s);
		}
		bool const back_face_cull = 1;
		point const camera(get_camera_pos());
		
		if (!back_face_cull || camera.z < botz) {
			vert_norm_tc const bverts[4] = {
				vert_norm_tc(point(x1, y1, botz), -plus_z, ts*x1, ts*y1),
				vert_norm_tc(point(x1, y2, botz), -plus_z, ts*x1, ts*y2),
				vert_norm_tc(point(x2, y2, botz), -plus_z, ts*x2, ts*y2),
				vert_norm_tc(point(x2, y1, botz), -plus_z, ts*x2, ts*y1)
			};
			draw_verts(bverts, 4, GL_TRIANGLE_FAN); // bottom
		}
		vector<vert_norm_tc> verts;

		for (unsigned d = 0; d < 2; ++d) {
			float const xlimit(d ? x2 : x1), ylimit(d ? y2 : y1);
			vector3d const &n1(d ? plus_y : -plus_y), &n2(d ? plus_x : -plus_x);

			if (!back_face_cull || (camera.y - ylimit)*n1.y > 0.0f) { // camera facing this side
				for (int i = 0; i < MESH_X_SIZE; ++i) { // y sides
					float const xv(get_xval(i));
					add_vertex(verts, n1, xv, ylimit, botz, 0, ts);
					add_vertex(verts, n1, xv, ylimit, mesh_height[d?ly:0][i], 0, ts);
				}
				draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
			}
			if (!back_face_cull || (camera.x - xlimit)*n2.x > 0.0f) { // camera facing this side
				for (int i = 0; i < MESH_Y_SIZE; ++i) { // x sides
					float const yv(get_yval(i));
					add_vertex(verts, n2, xlimit, yv, botz, 1, ts);
					add_vertex(verts, n2, xlimit, yv, mesh_height[i][d?lx:0], 1, ts);
				}
				draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
			}
		} // for d
	}
	s.end_shader();
}


class water_renderer {

	int check_zvals;
	float tex_scale;
	colorRGBA color;
	quad_batch_draw qbd;
	shader_t &shader;

	void draw_vert(float x, float y, float z, bool in_y, bool neg_edge);
	void draw_x_sides(bool neg_edge);
	void draw_y_sides(bool neg_edge);
	void draw_sides(unsigned ix);

public:
	water_renderer(int cz, shader_t &shader_) : check_zvals(cz), tex_scale(W_TEX_SCALE0/Z_SCENE_SIZE), shader(shader_) {}
	void draw();
};


void water_renderer::draw_vert(float x, float y, float z, bool in_y, bool neg_edge) { // in_y is slice orient

	colorRGBA c(color);
	point p(x, y, z), v(get_camera_pos());

	if (bool((v[!in_y] - p[!in_y]) < 0.0f) ^ neg_edge) { // camera viewing the inside face of the water side
		do_line_clip_scene(p, v, zbottom, z);
		float const atten(WATER_COL_ATTEN*p2p_dist(p, v));
		atten_by_water_depth(&c.R, atten);
		c.A = CLIP_TO_01(atten);
	}
	vector3d normal(zero_vector);
	normal[in_y] = (neg_edge ? -1.0 : 1.0);
	qbd.verts.emplace_back(point(x, y, z), normal, tex_scale*(in_y ? z : x), tex_scale*(in_y ? y : z), c);
}


void water_renderer::draw_x_sides(bool neg_edge) {

	int const end_val(neg_edge ? 0 : MESH_X_SIZE-1);
	float const limit(neg_edge ? -X_SCENE_SIZE : X_SCENE_SIZE-DX_VAL);
	float yv(-Y_SCENE_SIZE);
	setup_texgen_full(0.0, tex_scale, 0.0, 0.0, 0.0, 0.0, tex_scale, 0.0, shader, 0);

	for (int i = 1; i < MESH_Y_SIZE; ++i) { // x sides
		float const mh1(mesh_height[i][end_val]), mh2(mesh_height[i-1][end_val]);
		float const wm1(water_matrix[i][end_val] - SMALL_NUMBER), wm2(water_matrix[i-1][end_val] - SMALL_NUMBER);

		if (!check_zvals || mh1 < wm1 || mh2 < wm2) {
			draw_vert(limit, yv,        wm2,           1, neg_edge);
			draw_vert(limit, yv+DY_VAL, wm1,           1, neg_edge);
			draw_vert(limit, yv+DY_VAL, min(wm1, mh1), 1, neg_edge);
			draw_vert(limit, yv,        min(wm2, mh2), 1, neg_edge);
		}
		yv += DY_VAL;
	}
	qbd.draw_and_clear_quads();
}


void water_renderer::draw_y_sides(bool neg_edge) {

	int const end_val(neg_edge ? 0 : MESH_Y_SIZE-1);
	float const limit(neg_edge ? -Y_SCENE_SIZE : Y_SCENE_SIZE-DY_VAL);
	float xv(-X_SCENE_SIZE);
	setup_texgen_full(tex_scale, 0.0, 0.0, 0.0, 0.0, 0.0, tex_scale, 0.0, shader, 0);
	
	for (int i = 1; i < MESH_X_SIZE; ++i) { // y sides
		float const mh1(mesh_height[end_val][i]), mh2(mesh_height[end_val][i-1]);
		float const wm1(water_matrix[end_val][i] - SMALL_NUMBER), wm2(water_matrix[end_val][i-1] - SMALL_NUMBER);

		if (!check_zvals || mh1 < wm1 || mh2 < wm2) {
			draw_vert(xv,        limit, wm2,           0, neg_edge);
			draw_vert(xv+DX_VAL, limit, wm1,           0, neg_edge);
			draw_vert(xv+DX_VAL, limit, min(wm1, mh1), 0, neg_edge);
			draw_vert(xv,        limit, min(wm2, mh2), 0, neg_edge);
		}
		xv += DX_VAL;
	}
	qbd.draw_and_clear_quads();
}


void water_renderer::draw_sides(unsigned ix) {

	switch (ix) { // xn xp yn yp
		case 0: draw_x_sides(1); break;
		case 1: draw_x_sides(0); break;
		case 2: draw_y_sides(1); break;
		case 3: draw_y_sides(0); break;
		default: assert(0);
	}
}


void water_renderer::draw() { // modifies color

	select_water_ice_texture(shader, color);
	enable_blend();
	float const pts[4][2] = {{-X_SCENE_SIZE, 0.0}, {X_SCENE_SIZE, 0.0}, {0.0, -Y_SCENE_SIZE}, {0.0, Y_SCENE_SIZE}};
	vector<pair<float, unsigned> > sides(4);

	for (unsigned i = 0; i < 4; ++i) {
		sides[i] = make_pair(-distance_to_camera_sq(point(pts[i][0], pts[i][1], water_plane_z)), i);
	}
	sort(sides.begin(), sides.end()); // largest to smallest distance
	for (unsigned i = 0; i < 4; ++i) {draw_sides(sides[i].second);} // draw back to front
	disable_blend();
	shader.clear_specular();
}


void draw_water_sides(shader_t &shader, int check_zvals) {

	water_renderer wr(check_zvals, shader);
	wr.draw();
}


void setup_water_plane_texgen(float s_scale, float t_scale, shader_t &shader, int mode) {

	vector3d const wdir(vector3d(wind.x, wind.y, 0.0).get_norm());// wind.z is probably 0.0 anyway (nominal 1,0,0)
	float const tscale(W_TEX_SCALE0/Z_SCENE_SIZE), xscale(tscale*wdir.x), yscale(tscale*wdir.y);
	float const tdx(tscale*(xoff2 - xoff)*DX_VAL + water_xoff), tdy(tscale*(yoff2 - yoff)*DY_VAL + water_yoff);
	setup_texgen_full(s_scale*xscale, s_scale*yscale, 0.0, s_scale*(tdx*wdir.x + tdy*wdir.y), -t_scale*yscale, t_scale*xscale, 0.0, t_scale*(-tdx*wdir.y + tdy*wdir.x), shader, mode);
}

void set_water_plane_uniforms(shader_t &s) {

	s.add_uniform_float("wave_time",      wave_time);
	s.add_uniform_float("wave_amplitude", water_params.wave_amp*min(1.0, 1.5*wind.mag())); // No waves if (temperature < W_FREEZE_POINT)?
	s.add_uniform_float("water_plane_z",  water_plane_z);
}

float get_tess_wave_height() {return 0.02/mesh_scale_z;}


// textures used: 1=normal map, 2=mesh height map, 3=raindrops, 4=ocean normal map, 5=foam, 6=shadow map, 7=raindrops, 8=reflection, 13-14=tile shadow maps
void setup_water_plane_shader(shader_t &s, bool no_specular, bool reflections, bool add_waves, bool rain_mode, bool use_depth,
	bool depth_only, colorRGBA const &color, colorRGBA const &rcolor, bool use_tess)
{
	if (water_is_lava) {use_depth = 0;} // lava has no depth-dependent effects
	if (no_specular) {s.set_prefix("#define NO_SPECULAR",      1);} // FS
	if (depth_only)  {s.set_prefix("#define WRITE_DEPTH_ONLY", 1);} // FS
	if (use_depth)   {s.set_prefix("#define USE_WATER_DEPTH",  1);} // FS
	if (use_depth)   {s.set_prefix("#define USE_WATER_DEPTH",  4);} // tess eval
	if (use_tess)    {s.set_prefix("#define NO_FOG_FRAG_COORD",1);} // FS - needed on some drivers because TC/TE don't have fg_FogFragCoord
	s.setup_enabled_lights(3, 2); // FS
	setup_tt_fog_pre(s);
	bool const use_foam(!water_is_lava), enable_shadow_maps(use_depth && shadow_map_enabled()); // shadow maps are enabled during the normal pass that uses depth
	s.set_prefix(make_shader_bool_prefix("use_foam",    use_foam),         1); // FS
	s.set_prefix(make_shader_bool_prefix("reflections", reflections),      1); // FS
	s.set_prefix(make_shader_bool_prefix("add_waves",   add_waves),        1); // FS
	s.set_prefix(make_shader_bool_prefix("add_rain",    (rain_mode && 1)), 1); // FS
	s.set_prefix(make_shader_bool_prefix("add_noise",   (rain_mode && 0)), 1); // FS
	s.set_prefix(make_shader_bool_prefix("is_lava",     water_is_lava),    1); // FS
	set_smap_enable_for_shader(s, enable_shadow_maps, 1); // FS
	
	if (use_tess) { // tessellation shaders
		s.set_prefix("#define TESS_MODE", 1); // FS
		s.set_vert_shader("texture_gen.part+water_plane_tess");
		s.set_tess_control_shader("water_plane");
		s.set_tess_eval_shader("water_plane"); // draw calls need to use GL_PATCHES instead of GL_TRIANGLES
		glPatchParameteri(GL_PATCH_VERTICES, 4); // quads; max is 32
	}
	else {
		s.set_vert_shader("texture_gen.part+water_plane");
	}
	s.set_frag_shader("linear_fog.part+ads_lighting.part*+shadow_map.part*+tiled_shadow_map.part*+water_ripples.part+water_plane");
	s.begin_shader();
	setup_tt_fog_post(s);
	s.add_uniform_int  ("reflection_tex",   8);
	s.add_uniform_color("water_color",      color);
	s.add_uniform_color("reflect_color",    rcolor);
	s.add_uniform_int  ("height_tex",       2);
	s.add_uniform_int  ("shadow_tex",       6);
	s.add_uniform_float("water_green_comp", water_params.green);
	s.add_uniform_float("reflect_scale",    water_params.reflect);
	s.add_uniform_float("mesh_z_scale",     mesh_scale);
	
	if (use_tess) {
		s.add_uniform_vector3d("camera_pos", get_camera_pos());
		s.add_uniform_float("wave_height",   get_tess_wave_height());
		s.add_uniform_float("wave_width",    max(1.0, 1.0/mesh_scale_z));
	}
	if (enable_shadow_maps) {setup_tile_shader_shadow_map(s);}
	set_water_plane_uniforms(s);
	setup_water_plane_texgen(1.0, 1.0, s, 0);
	if (no_specular) {s.clear_specular();} else {set_tt_water_specular(s);}
	// Note: we could add procedural cloud soft shadows like in tiled terrain mesh and grass, but it's probably not worth the added complexity and runtime

	// waves (as normal maps)
	if (add_waves) {
		select_texture(WATER_NORMAL_TEX,       1);
		select_texture(OCEAN_WATER_NORMAL_TEX, 4);
		s.add_uniform_int("water_normal_tex",      1);
		s.add_uniform_int("deep_water_normal_tex", 4);
	}
	if (rain_mode) {
		select_texture(RAINDROP_TEX, 3);
		s.add_uniform_int  ("noise_tex", 3);
		s.add_uniform_float("noise_time", frame_counter); // rain ripples
		select_texture(RIPPLE_MAP_TEX, 7);
		s.add_uniform_int("ripple_tex", 7);
		s.add_uniform_float("rain_intensity", get_rain_intensity());
	}
	select_texture(FOAM_TEX, 5);
	s.add_uniform_int("foam_tex", 5);
}


void draw_plane_to_far_clip(float zval) {

	indexed_mesh_draw<vert_wrap_t> imd;
	float const size(camera_pdu.far_*SQRT2);
	imd.render_z_plane(-size, -size, size, size, zval, 8, 8); // 8x8 grid
	imd.free_context();
}

void draw_distant_mesh_bottom(float terrain_zmin) {
	colorRGBA const uw_color(1.0-uw_atten_max.R, 1.0-uw_atten_max.G, 1.0-uw_atten_max.B);
	draw_plane_to_far_clip(terrain_zmin); // hopefully below tile water level
}


class lava_bubble_manager_t {
	struct bubble : public sphere_t {
		float time; // 0.0=start, 1.0=end
		bool valid;

		bubble() : time(0.0), valid(0) {}
		void age(float dtime) {time += dtime;}
		float     get_radius() const {return radius*(1.0 + time);}
		colorRGBA get_color () const {return colorRGBA(1.0, 0.4*(1.0 - time), 0.0, min(1.0, 5.0*(1.0 - time)));} // orange => transparent red
	};
	vector<bubble> bubbles;
	vector<sphere_t> splashes;
	quad_batch_draw splash_qbd;
	rand_gen_t rgen;

	void gen_bubble(bubble &b, float lava_zval) {
		point const center(get_camera_pos());
		float const range(1.5f*(X_SCENE_SIZE + Y_SCENE_SIZE));
		b.pos.assign((center.x + range*rgen.rand_uniform(-1.0, 1.0)), (center.y + range*rgen.rand_uniform(-1.0, 1.0)), lava_zval);
		b.radius = rgen.rand_uniform(0.3, 0.6)*CAMERA_RADIUS;
		b.time   = 0.0;
		b.valid  = (int_mesh_zval_pt_off(b.pos, 1, 1) < lava_zval); // check if over lava
	}
public:
	void clear() {bubbles.clear(); splashes.clear();}

	void next_frame(float lava_zval) {
		if (bubbles.empty()) {
			unsigned const num = 250;
			bubbles.resize(num);

			for (auto i = bubbles.begin(); i != bubbles.end(); ++i) { // init bubbles
				gen_bubble(*i, lava_zval);
				i->age(rgen.rand_float()); // age a random amount so that all bubbles don't start at the same size
			}
			return;
		}
		float const lifetime = 4.0; // seconds
		float const elapsed(fticks/TICKS_PER_SECOND), dtime(elapsed/lifetime);
		splashes.clear();

		for (auto i = bubbles.begin(); i != bubbles.end(); ++i) {
			i->age(dtime);

			if (i->time > 1.0) {
				gen_smoke((i->pos + vector3d(0.0, 0.0, i->get_radius())), 1.5, 1.0, colorRGBA(4.0, 4.0, 4.0, 1.0)); // brighter color to represent steam rather than smoke
				gen_bubble(*i, lava_zval); // spawn a new bubble
			}
			else if (i->time > 0.8) {
				float const radius(i->get_radius());
				splashes.emplace_back((i->pos + vector3d(0.0, 0.0, 0.7*radius)), 10.0*(i->time - 0.7)*radius);
			}
		}
	}
	void draw(shader_t &s) {
		if (bubbles.empty()) return;

		for (auto i = bubbles.begin(); i != bubbles.end(); ++i) {
			if (!i->valid) continue;
			float const radius(i->get_radius());
			if (!camera_pdu.sphere_visible_test(i->pos, radius)) continue;
			s.add_uniform_color("water_color", i->get_color());
			draw_subdiv_sphere(i->pos, radius, 16, 0, 0); // untextured
			//draw_sphere_vbo(i->pos, radius, 16, 0); // no, we don't want the translate in here
		}
		if (splashes.empty()) return;
		point const camera(get_camera_pos());
		s.end_shader();
		setup_smoke_shaders(s, 0.01, 0, 1, 0, 1, 0, 0);
		select_texture(FLARE2_TEX);

		for (auto i = splashes.begin(); i != splashes.end(); ++i) {
			splash_qbd.add_billboard(i->pos, camera, up_vector, colorRGBA(1.0, 0.1, 0.0, 1.0), i->radius, i->radius, tex_range_t(), 0, &plus_z);
		}
		splash_qbd.draw_and_clear();
	}
};


// texture units used: 8: reflection texture, 1: water normal map, 2: mesh height texture, 3: rain noise, 4: deep water normal map
void draw_water_plane(float zval, float terrain_zmin, unsigned reflection_tid) {

	if (DISABLE_WATER) return;
	bool const reflections(!(display_mode & 0x20));
	bool const no_specular(light_factor <= 0.4 || (get_sun_pos().z - sun_radius) < water_plane_z); // no sun or it's below the water level
	colorRGBA const color(get_tt_water_color());

	if (animate2 && get_cur_temperature() > W_FREEZE_POINT) {
		float const wwspeed(WATER_WIND_EFF*fticks*(water_is_lava ? 0.25 : 1.0));
		water_xoff -= wind.x*wwspeed;
		water_yoff -= wind.y*wwspeed;
		wave_time  += fticks*(water_is_lava ? 0.5 : 1.0);
		// reset at 3600 ticks (90s) to avoid FP error - this number is a multiple of shallow and deep water wave periods, and of the tess eval period
		if (wave_time > 3600.0) {wave_time = 0.0;}
	}
	enable_blend();
	bind_texture_tu_def_white_tex(reflection_tid, 8); // reflection texture tu_id=8
	point const camera(get_camera_pos());
	bool const add_waves(enable_ocean_waves());
	bool const camera_underwater(camera.z < zval);
	bool const rain_mode(add_waves && !water_is_lava && is_rain_enabled() /*&& !camera_underwater*/);
	bool const use_tess(use_water_plane_tess());
	colorRGBA rcolor(reflection_tid ? WHITE : cur_fog_color); // or blend_color(rcolor, bkg_color, get_cloud_color(), 0.75, 1)?
	rcolor.alpha = 0.5*(0.5 + color.alpha);
	shader_t s;
	set_std_depth_func_with_eq(); // helps prevent Z-fighting

	if (use_tess) {
		glCullFace(camera_underwater ? GL_FRONT : GL_BACK);
		glEnable(GL_CULL_FACE);

		// first path with depth only, so that we can remove water behind other water
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // Disable color rendering, we only want to write to the Z-Buffer
		setup_water_plane_shader(s, 0, 0, 0, 0, 0, 1, WHITE, WHITE, 1); // depth_only=1, use_tess=1
		draw_tiled_terrain_water(s, zval);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// second pass with colors only
		glDepthMask(GL_FALSE); // no depth writing
		s.end_shader();
	}
	setup_water_plane_shader(s, no_specular, reflections, add_waves, rain_mode, 1, 0, color, rcolor, use_tess); // depth_only=0, use_depth=1
	s.add_uniform_float("normal_z", (camera_underwater ? -1.0 : 1.0));
	s.add_uniform_float("depth_opacity_mult", ocean_depth_opacity_mult);
	s.set_cur_color(WHITE);
	draw_tiled_terrain_water(s, zval);
	set_std_depth_func();
	s.end_shader();

	if (use_tess) {
		glDepthMask(GL_TRUE);
		glDisable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
	if (draw_distant_water()) { // camera is high above the mesh (Enable when extended mesh is drawn)
		setup_water_plane_shader(s, no_specular, reflections, add_waves, 0, 0, 0, color, rcolor, 0); // rain_mode=0, use_depth=0, use_tess=0
		s.set_cur_color(WHITE);
		draw_plane_to_far_clip(zval - 0.01); // slightly below water level
		s.end_shader();
	}
	static lava_bubble_manager_t lava_bubble_manager;

	if (water_is_lava && !camera_underwater && (camera.z - zval) < 50.0f*CAMERA_RADIUS) { // camera just above lava surface
		select_texture(WHITE_TEX); // no reflections
		setup_water_plane_shader(s, no_specular, 0, 0, 0, 0, 0, color, BLACK, 0); // reflections=0, add_waves=0, rain_mode=0, use_depth=0, use_tess=0
		// Note: bound uniforms and textures should be set to valid values from above
		s.set_cur_color(WHITE);
		if (animate2) {lava_bubble_manager.next_frame(zval);}
		lava_bubble_manager.draw(s);
		s.end_shader();
	}
	else {lava_bubble_manager.clear();}

	disable_blend();
}


