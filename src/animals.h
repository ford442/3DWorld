// 3D World - Classes for procedural animals
// by Frank Gennari
// 3-17-16
#pragma once

#include "3DWorld.h"
#include "function_registry.h"
#include "inlines.h"

class tile_t;

struct adj_tiles_t {
	tile_t *adj[9] = {0};
	bool valid = 0;
	void ensure_valid(tile_t const *const tile);
};

struct tile_offset_t {
	int dxoff, dyoff;

	tile_offset_t(int dxoff_=0, int dyoff_=0) : dxoff(dxoff_), dyoff(dyoff_) {}
	void set_from_xyoff2() {dxoff = -xoff2; dyoff = -yoff2;}
	int get_delta_xoff() const {return ((xoff - xoff2) - dxoff);}
	int get_delta_yoff() const {return ((yoff - yoff2) - dyoff);}
	vector3d get_xlate() const {return vector3d(get_delta_xoff()*DX_VAL, get_delta_yoff()*DY_VAL, 0.0);}
	vector3d subtract_from(tile_offset_t const &o) const {return vector3d((o.dxoff - dxoff)*DX_VAL, (o.dyoff - dyoff)*DY_VAL, 0.0);}
};


class animal_t : public sphere_t {

public:
	vector3d velocity;
protected:
	bool enabled;
	vector3d dir;
	colorRGBA color;

	int get_ndiv(point const &pos_) const;
	void gen_dir_vel(rand_gen_t &rgen, float speed);
	float get_mesh_zval_at_pos(tile_t const *const tile) const;
public:
	animal_t() : enabled(0), color(BLACK) {}
	void apply_force(vector3d const &force) {velocity += force;}
	void apply_force_xy(vector3d const &force) {velocity.x += force.x; velocity.y += force.y;}
	bool is_enabled() const {return enabled;}
	bool distance_check(point const &pos_, float vis_dist_scale) const;
	bool is_visible(point const &pos_, float vis_dist_scale=1.0) const;
	point get_camera_space_pos() const {return (pos + get_camera_coord_space_xlate());}
};

class fish_t : public animal_t {

	float get_half_height() const {return 0.4*radius;} // approximate
public:
	static bool type_enabled();
	static bool can_place_in_tile(tile_t const *const tile);
	bool gen(rand_gen_t &rgen, cube_t const &range, tile_t const *const tile);
	bool update(rand_gen_t &rgen, tile_t const *const tile);
	void draw(shader_t &s, tile_t const *const tile, bool &first_draw) const;
};

class bird_t : public animal_t {

	bool flocking;
	float time;
public:
	bird_t() : flocking(0), time(0.0) {}
	static bool type_enabled() {return 1;} // no model, always enabled
	static bool can_place_in_tile(tile_t const *const tile) {return 1;} // always allowed
	bool gen(rand_gen_t &rgen, cube_t const &range, tile_t const *const tile);
	bool update(rand_gen_t &rgen, tile_t const *const tile);
	void apply_force_xy_const_vel(vector3d const &force);
	void draw(shader_t &s, tile_t const *const tile, bool &first_draw) const;
};

struct vect_butterfly_t;

class butterfly_t : public animal_t {

	bool dest_valid, is_mating, gender; // 0=male, 1=female
	float time, rest_time, mate_time, explore_time, speed_factor, rot_rate, alt_change, fwd_accel, rot_accel, alt_accel, dest_alignment;
	point cur_dest, prev_dest;
	sphere_t dest_bsphere;
	mutable vector<point> path;

	void update_dest(rand_gen_t &rgen, tile_t const *const tile);
public:
	friend struct vect_butterfly_t;
	butterfly_t() : dest_valid(0), is_mating(0), gender(0), time(0.0), rest_time(0.0), mate_time(0.0), explore_time(0.0), speed_factor(1.0),
		rot_rate(0.0), alt_change(0.0), fwd_accel(0.0), rot_accel(0.0), alt_accel(0.0), dest_alignment(0.0) {}
	static bool type_enabled();
	static bool can_place_in_tile(tile_t const *const tile);
	bool can_mate_with(butterfly_t const &b) const;
	point get_camera_space_dest() const {return (cur_dest + get_camera_coord_space_xlate());}
	bool gen(rand_gen_t &rgen, cube_t const &range, tile_t const *const tile);
	bool update(rand_gen_t &rgen, tile_t const *const tile);
	void draw(shader_t &s, tile_t const *const tile, bool &first_draw) const;
};


class animal_group_base_t {
protected:
	rand_gen_t rgen;
	bool generated;
public:
	animal_group_base_t() : generated(0) {}
	bool was_generated() const {return generated;}
};

template<typename A> class animal_group_t : public vector<A>, public animal_group_base_t {

	cube_t bcube;
public:
	void gen(unsigned num, cube_t const &range, tile_t const *const tile);
	void update(tile_t const *const tile);
	void remove(unsigned ix);
	void draw_animals(shader_t &s, tile_t const *const tile) const;
	void clear() {vector<A>::clear(); generated = 0;}
};

struct vect_fish_t : public animal_group_t<fish_t> {};

struct vect_bird_t : public animal_group_t<bird_t> {
	void flock(tile_t const *const tile, adj_tiles_t &adj_tiles);
};

struct vect_butterfly_t : public animal_group_t<butterfly_t> {
	void run_mating(tile_t const *const tile, adj_tiles_t &adj_tiles);
};

bool birds_active();

