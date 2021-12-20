// 3D World - Cars for Procedural Cities
// by Frank Gennari
// 11/19/18
#include "city.h"
#include "file_utils.h"
#include "openal_wrap.h"
#include "explosion.h" // for add_blastr()
#include "lightmap.h" // for light_source
#include "profiler.h"
#include <cfloat> // for FLT_MAX

bool const DYNAMIC_HELICOPTERS = 1;
float const MIN_CAR_STOP_SEP   = 0.25; // in units of car lengths

extern bool tt_fire_button_down, enable_hcopter_shadows, city_action_key;
extern int display_mode, game_mode, map_mode, animate2;
extern float FAR_CLIP;
extern point pre_smap_player_pos;
extern vector<light_source> dl_sources;
extern city_params_t city_params;


float get_clamped_fticks() {return min(fticks, 4.0f);} // clamp to 100ms

float car_t::get_max_lookahead_dist() const {return (get_length() + city_params.road_width);} // extend one car length + one road width in front
float car_t::get_turn_rot_z(float dist_to_turn) const {return (1.0 - CLIP_TO_01(4.0f*fabs(dist_to_turn)/city_params.road_width));}

void car_t::set_bcube(point const &center, vector3d const &sz) {
	height = sz.z;
	bcube.set_from_point(center);
	bcube.expand_by_xy(0.5*(dim ? vector3d(sz.y, sz.x, sz.z) : sz)); // swap size X and Y when dim==1
	bcube.z2() += height; // set height
}
bool car_t::headlights_on() const { // no headlights when parked
	return (engine_running && (in_tunnel || ((light_factor < (0.5 + HEADLIGHT_ON_RAND)) && is_night(HEADLIGHT_ON_RAND*signed_rand_hash(height + max_speed)))));
}
bool car_t::is_close_to_player() const { // for debugging
	return dist_xy_less_than(get_camera_building_space(), get_center(), city_params.road_width);
}

void car_t::apply_scale(float scale) {
	if (scale == 1.0) return; // no scale
	height *= scale;
	point const pos(get_center());
	vector2d sz((bcube.x2() - pos.x), (bcube.y2() - pos.y));
	is_truck = (scale > 1.2); // trucks are larger in size

	if (is_truck) { // truck bcube is taller and less wide
		height   *= 1.55;
		sz[ dim] *= 0.85; // length
		sz[!dim] *= 0.75; // width
	}
	bcube.z2() = bcube.z1() + height; // z1 is unchanged
	bcube.x1() = pos.x - scale*sz.x; bcube.x2() = pos.x + scale*sz.x;
	bcube.y1() = pos.y - scale*sz.y; bcube.y2() = pos.y + scale*sz.y;
}

void car_t::destroy() { // Note: not calling create_explosion(), so no chain reactions
	point const pos(get_center() + get_tiled_terrain_model_xlate());
	float const length(get_length());
	static rand_gen_t rgen;

	for (int n = 0; n < rgen.rand_int(3, 5); ++n) {
		vector3d off(rgen.signed_rand_vector_spherical()*(0.5*length));
		off.z = abs(off.z); // not into the ground
		point const exp_pos(pos + off);
		float const radius(rgen.rand_uniform(1.0, 1.5)*length), time(rgen.rand_uniform(0.3, 0.8));
		add_blastr(exp_pos, (exp_pos - get_camera_pos()), radius, 0.0, time*TICKS_PER_SECOND, CAMERA_ID, YELLOW, RED, ETYPE_ANIM_FIRE, nullptr, 1);
		gen_smoke(exp_pos, 1.0, rgen.rand_uniform(0.4, 0.6));
	} // for n
	gen_delayed_from_player_sound(SOUND_EXPLODE, pos, 1.0);
	destroyed = 1;
	engine_running = 0;
	park();
}

float car_t::get_min_sep_dist_to_car(car_t const &c, bool add_one_car_len) const { // should this depend on in_reverse?
	float const avg_len(0.5f*(get_length() + c.get_length())); // average length of the two cars
	float const min_speed(max(0.0f, (min(cur_speed, c.cur_speed) - 0.1f*max_speed))); // relative to max speed of 1.0, clamped to 10% at bottom end for stability
	return avg_len*(MIN_CAR_STOP_SEP + 1.11*min_speed + (add_one_car_len ? 1.0 : 0.0)); // 25% to 125% car length, depending on speed (2x on connector roads)
}

string car_t::str() const {
	std::ostringstream oss;
	oss << "Car " << TXT(dim) << TXT(dir) << TXT(cur_city) << TXT(cur_road) << TXT(cur_seg) << TXTi(turn_dir) << TXT(dz)
		<< TXT(max_speed) << TXT(cur_speed) << TXTi(cur_road_type) << TXTi(color_id) << " bcube=" << bcube.str();
	return oss.str();
}

string car_t::label_str() const {
	std::ostringstream oss;
	oss << TXT(dim) << TXTn(dir) << TXT(cur_city) << TXT(cur_road) << TXTn(cur_seg) << TXT(dz) << TXTn(turn_val) << TXT(max_speed) << TXTn(cur_speed)
		<< "sleep=" << is_sleeping() << " wait_time=" << get_wait_time_secs() << "\n" << TXTin(cur_road_type)
		<< TXTn(stopped_at_light) << TXTn(in_isect()) << "cars_in_front=" << count_cars_in_front() << "\n" << TXT(dest_city) << TXTn(dest_isec);
	oss << "car=" << this << " car_in_front=" << car_in_front << endl; // debugging
	return oss.str();
}

void car_t::choose_max_speed(rand_gen_t &rgen) { // add some speed variation
	max_speed = rgen.rand_uniform(0.66, 1.0);
	engine_running = 1; // car starts up
}
void car_t::move(float speed_mult) {
	prev_bcube = bcube;
	if (destroyed || stopped_at_light || is_stopped()) return;
	assert(speed_mult >= 0.0 && cur_speed > 0.0 && cur_speed <= CONN_ROAD_SPEED_MULT*max_speed); // Note: must be valid for connector road => city transitions
	float dist(cur_speed*speed_mult);
	if (dz != 0.0) {dist *= min(1.25, max(0.75, (1.0 - 0.5*dz/get_length())));} // slightly faster down hills, slightly slower up hills
	min_eq(dist, 0.25f*city_params.road_width); // limit to half a car length to prevent cars from crossing an intersection in a single frame
	move_by((dir ^ in_reverse) ? dist : -dist);
	// update waiting state
	float const cur_pos(bcube.d[dim][dir]);
	if (fabs(cur_pos - waiting_pos) > get_length()) {waiting_pos = cur_pos; reset_waiting();} // update when we move at least a car length
}

void car_t::set_target_speed(float speed_factor) {
	float const target_speed(speed_factor*max_speed);
	if      (cur_speed < 0.9*target_speed) {maybe_accelerate();}
	else if (cur_speed > 1.1*target_speed) {decelerate();}
}

void car_t::maybe_accelerate(float mult) {
	if (car_in_front) {
		float const dist_sq(p2p_dist_xy_sq(get_center(), car_in_front->get_center())), length(get_length());

		if (dist_sq > length*length) { // if cars are colliding, let the collision detection system handle it
			float const dmin(get_min_sep_dist_to_car(*car_in_front, 1)); // add_one_car_len=1; space between the two car centers
			if (dist_sq < dmin*dmin) {decelerate(mult); return;} // too close to the car in front - decelerate instead
		}
	}
	accelerate(mult);
}

void car_t::sleep(rand_gen_t &rgen, float min_time_secs) {
	park();
	if (destroyed || is_sleeping()) return; // don't reset wake_time if already sleeping
	wake_time = (float)tfticks + rgen.rand_uniform(1.0, 2.0)*min_time_secs*TICKS_PER_SECOND; // randomly wait 1-2x min_time_secs
}
bool car_t::maybe_wake(rand_gen_t &rgen) {
	if (destroyed || !is_sleeping() || tfticks < wake_time) return 0; // continue to sleep
	wake_time = 0.0;
	choose_max_speed(rgen);
	return 1;
}

bool car_t::maybe_apply_turn(float centerline, bool for_driveway) {
	bool const right_turn(turn_dir == TURN_RIGHT); // 0=left, 1=right
	bool const tdir(dir ^ in_reverse); // direction of movement
	point const car_center(get_center()), prev_center(prev_bcube.get_cube_center());
	float const prev_val(prev_center[dim]), cur_val(car_center[dim]);
	float const turn_radius_mult(for_driveway ? 1.0 : ((cur_road_type == TYPE_ISEC2) ? 2.0 : 1.0)); // larger turn radius for 2-way intersections (bends)
	float const turn_radius((for_driveway ? 0.75 : 1.0)*(right_turn ? 0.15 : 0.25)*turn_radius_mult*city_params.road_width); // right turn has smaller radius; driveway has smaller radius
	float const dist_to_turn(max(0.0f, (cur_val - centerline)*(tdir ? -1.0f : 1.0f))); // Note: can be negative if we overshot the turn, so clamp to 0
	if (dist_to_turn > turn_radius) return 0; // not yet time to turn
	// Note: cars turn around their center points, not their front wheels, which looks odd
	float const dist_from_turn_start(turn_radius - dist_to_turn);
	float const dev(turn_radius - sqrt(max((turn_radius*turn_radius - dist_from_turn_start*dist_from_turn_start), 0.0f))); // clamp to 0 to avoid NAN due to FP error
	float const new_center(turn_val + dev*((right_turn ^ tdir ^ dim) ? 1.0 : -1.0));
	rot_z = (right_turn ? -1.0 : 1.0)*(1.0 - CLIP_TO_01(dist_to_turn/turn_radius));
	bcube.translate_dim(!dim, (new_center - car_center[!dim])); // translate to new center point
	vector3d const move_dir(get_center() - prev_center); // total movement from car + turn
	float const move_dist(move_dir.mag());

	if (move_dist > TOLERANCE) { // avoid division by zero
		float const frame_dist(p2p_dist_xy(car_center, prev_center)); // total XY distance the car is allowed to move
		vector3d const delta(move_dir*(frame_dist/move_dist - 1.0)); // overshoot value due to turn
		bcube += delta;
	}
	if (min(prev_val, cur_val) <= centerline && max(prev_val, cur_val) > centerline) { // crossed the lane centerline boundary
		move_by(centerline - bcube.get_center_dim(dim)); // align to lane centerline, using the current position (post translate above)
		complete_turn_and_swap_dim();
	}
	return 1;
}
void car_t::complete_turn_and_swap_dim() {
	vector3d const car_sz(bcube.get_size());
	float const size_adj(0.5f*(car_sz[dim] - car_sz[!dim]));
	vector3d expand(zero_vector);
	expand[dim] -= size_adj; expand[!dim] += size_adj;
	bcube.expand_by(expand); // fix aspect ratio
	if ((dim == 0) ^ (turn_dir == TURN_LEFT)) {dir ^= 1;}
	dim     ^= 1;
	rot_z    = 0.0;
	turn_val = 0.0; // reset
	turn_dir = TURN_NONE; // turn completed
	entering_city = 0; // clear flag in case we turned into the city
}

bool car_t::must_wait_entering_or_crossing_road(vector<car_t> const &cars, driveway_t const &driveway, unsigned road_ix, float lookahead_time) const {
	bool const in_driveway(cur_road_type == TYPE_DRIVEWAY);
	bool const rdim(!driveway.dim), rdir(driveway.dir ^ rdim); // dim/dir of cars on the road we're on or entering
	float far_side (bcube.d[rdim][ rdir]); // side of car not in danger of being hit
	float near_side(bcube.d[rdim][!rdir]); // side of car in danger of being hit
	if (in_driveway) {far_side += (rdir ? 1.0 : -1.0)*get_length();} // if exiting a driveway, must leave space for the car to fit in the road
	if (in_driveway && in_reverse) {near_side -= (rdir ? 1.0 : -1.0)*get_length();} // if backing out of driveway, we must have space to fit when fully backed out
	// the following logic is similar to ped_manager_t::has_nearby_car_on_road(), except it only considers one lane of the road
	// since we don't have cars split out per-city here like we do in ped_mgr, we have to sort by city and then road
	car_base_t ref_car; ref_car.cur_city = cur_city; ref_car.cur_road = road_ix; ref_car.max_speed = 1.0; // so that it isn't treated as parked
	auto range_start(std::lower_bound(cars.begin(), cars.end(), ref_car, comp_car_city_then_road())); // binary search acceleration to find the first car on the same city and road
	auto closest_car(cars.end());
	// Note: checking for people on the sidewalk by the driveway is too difficult, so instead the people should check for cars before crossing driveways

	for (auto it = range_start; it != cars.end(); ++it) {
		car_t const &c(*it);
		if (c.cur_road != road_ix || c.cur_city != cur_city) break; // different road or city, done
		if (c.dir != rdir)    continue; // car is traveling on the side of the road that we're not entering, ignore it
		if (c.bcube == bcube) continue; // skip ourself (can occasionally get here, I think when the car is waiting for another car to pass)
		float const val(c.bcube.d[rdim][!rdir]); // back end of the car
		if (rdir) {if (val > far_side) break;   } // car already passed us, not a threat - done (cars are sorted in this dim)
		else      {if (val < far_side) continue;} // car already passed us, not a threat - skip to next car
		float const front_pos(c.bcube.d[rdim][rdir]); // front end of the car
		if ((front_pos > near_side) == rdir) return 1; // already intersects in dimension dim, must wait
		//if (!city_single_cube_visible_check(get_center(), c.bcube)) continue; // we could check this, but it's generally always true
		if (closest_car == cars.end()) {closest_car = it;} // first threatening car
		else {
			float const val2(closest_car->bcube.d[rdim][!rdir]); // back end of the other car
			if (rdir ? (val > val2) : (val < val2)) {closest_car = it;} // this car is closer
		}
		if (!rdir) break; // no cars can be closer than this (cars are sorted in this dim)
	} // for it
	if (closest_car == cars.end()) return 0; // no car found, safe
	car_t const &c(*closest_car);
	if (c.turn_dir != TURN_NONE)              return 0; // car is turning; since it's already on this road, it must be turning off this road and can be ignored
	if (c.stopped_at_light || c.is_stopped()) return 0; // stopped, maybe at a light; we could calculate the light change time like with peds, but maybe it's okay to not wait
	float const front_pos(c.bcube.d[rdim][rdir]); // front of the closest car
	// moving and not turning; assume it may be accelerating, and could reach max_speed by the time it passes near_side
	float const travel_dist(lookahead_time*CAR_SPEED_SCALE*city_params.car_speed*c.max_speed); // conservative travel dist
	return (fabs(front_pos - near_side) < travel_dist);
}
bool car_t::check_for_road_clear_and_wait(vector<car_t> const &cars, driveway_t const &driveway, unsigned road_ix) {
	if (!must_wait_entering_or_crossing_road(cars, driveway, road_ix, 2.0*TICKS_PER_SECOND)) return 0; // lookahead_time=2.0s
	decelerate_fast(); // is this needed?
	return 1; // wait for the path to become clear
}

bool car_t::run_enter_driveway_logic(vector<car_t> const &cars, driveway_t const &driveway) {
	if (dim == driveway.dim) return 0; // car must be on a road perpendicular to the driveway, which may be the road connected to it
	cube_t const turn_area(driveway.extend_across_road()); // includes driveway and the road adjacent to it
	if (!bcube.intersects_xy(turn_area)) return 0; // not yet crossed into turn area

	if (turn_dir == TURN_NONE) {
		if (prev_bcube.intersects_xy(turn_area)) return 0; // not yet turning, and in turn area last frame - too late to turn (likely car was spawned here)
		if (driveway.ped_count > 0) return 1; // pedestrian(s) in driveway, wait
		bool const dw_turn_dir(dir ^ driveway.dir ^ driveway.dim); // turn into driveway: 0=left, 1=right
		// if turning left: check for oncoming cars, wait until clear; only done at start of turn - if a car comes along mid-turn then we can't stop
		if (!dw_turn_dir && check_for_road_clear_and_wait(cars, driveway, cur_road)) return 1;
		turn_dir = (dw_turn_dir ? (uint8_t)TURN_RIGHT : (uint8_t)TURN_LEFT);
		begin_turn(); // capture car centerline before the turn
	}
	set_target_speed(0.4); // 40% of max speed
	float const centerline(driveway.get_center_dim(!driveway.dim));
	maybe_apply_turn(centerline, 1); // for_driveway=1

	if (turn_dir == TURN_NONE) { // turn has been completed, change to being in driveway even though we may not be onto the driveway yet
		cur_road_type = TYPE_DRIVEWAY;
		cur_road = (unsigned short)driveway.plot_ix; // store plot_ix in road field
		cur_seg  = dest_driveway; // store driveway index in cur_seg
	}
	return 1;
}
void car_t::pull_into_driveway(driveway_t const &driveway, rand_gen_t &rgen) {
	assert(dim == driveway.dim);
	assert(dir != driveway.dir);
	float const stop_pos(driveway.get_center_dim(driveway.dim)), car_center(bcube.get_center_dim(driveway.dim));

	if ((car_center < stop_pos) == driveway.dir) { // reached the driveway center, stop
		dest_valid     = 0;
		dest_driveway  = -1;
		engine_running = 0;
		sleep(rgen, 60.0); // sleep for 60-120s rather than permanently parking
	}
}
void car_t::back_or_pull_out_of_driveway(driveway_t const &driveway) {
	assert(dim == driveway.dim);
	// |---> driveway dir=0, car dir=1
	in_reverse = (dir != driveway.dir); // back up if pointing away from the road
	turn_dir   = (in_reverse ? (int)TURN_LEFT : (int)TURN_RIGHT); // always turn right when exiting the driveway/entering the road (left when backing out)
	set_target_speed(in_reverse ? 0.2 : 0.3); // 20-30% of max speed
	begin_turn(); // capture car centerline before the turn
}
// returns 1 when the exit + turn are complete
bool car_t::exit_driveway_to_road(vector<car_t> const &cars, driveway_t const &driveway, float centerline, unsigned road_ix, rand_gen_t &rgen) {
	if (driveway.intersects_xy(bcube)) { // partially out of the driveway/into the road
		if (driveway.contains_cube_xy(prev_bcube)) { // was contained in driveway last frame - just entered the road
			if (check_for_road_clear_and_wait(cars, driveway, road_ix)) {
				bcube = prev_bcube; // hack: move back to previous position so that we trigger this check again the next time we get here
				sleep(rgen, 1.0); // wait 1-2s and try again
				return 0;
			}
		}
	}
	bool const is_turning(maybe_apply_turn(centerline, 1)); // for_driveway=1

	if (is_turning && turn_dir == TURN_NONE) { // turn has been completed
		if (in_reverse) {decelerate_fast();} // pause before going forward
		driveway.in_use = 0; // Note: in_use flag is mutable
		in_reverse      = 0;
		return 1; // driveway exit complete, continue forward
	}
	set_target_speed(in_reverse ? 0.25 : 0.35); // 25-35% of max speed
	return 0;
}

point car_base_t::get_front(float dval) const {
	point car_front(get_center());
	car_front[dim] += (dir ? dval : -dval)*get_length(); // half length
	return car_front;
}

bool car_t::front_intersects_car(car_t const &c) const {
	return (c.bcube.contains_pt(get_front(0.25)) || c.bcube.contains_pt(get_front(0.5))); // check front-middle and very front
}

void car_t::honk_horn_if_close() const {
	point const pos(get_center());
	if (dist_less_than((pos + get_tiled_terrain_model_xlate()), get_camera_pos(), 1.0)) {gen_sound(SOUND_HORN, pos);}
}
void car_t::honk_horn_if_close_and_fast() const {
	if (cur_speed > 0.25*max_speed) {honk_horn_if_close();}
}

void car_t::on_alternate_turn_dir(rand_gen_t &rgen) {
	honk_horn_if_close();
	// 25% chance of choosing a new destination rather than driving in circles; will be in current city
	if (dest_driveway < 0 && (rgen.rand()&3) == 0) {dest_valid = 0;}
}

void car_t::register_adj_car(car_t &c) {
	if (car_in_front != nullptr) {
		point const center(get_center());
		if (p2p_dist_xy_sq(center, c.get_center()) > p2p_dist_xy_sq(center, car_in_front->get_center())) return; // already found a closer car
	}
	cube_t cube(bcube);
	cube.d[dim][!dir] = cube.d[dim][dir];
	cube.d[dim][dir] += (dir ? 1.0 : -1.0)*get_max_lookahead_dist();
	if (cube.intersects_xy_no_adj(c.bcube)) {car_in_front = &c;} // projected cube intersects other car
}

unsigned car_t::count_cars_in_front(cube_t const &range) const { // Note: currently only used for debug printouts, so the iteration limit is acceptable
	unsigned num(0);
	car_t const *cur_car(this);

	for (unsigned i = 0; i < 50; ++i) { // limit iterations
		cur_car = cur_car->car_in_front;
		if (!cur_car || (!range.is_all_zeros() && !range.contains_pt_xy(cur_car->get_center()))) break;
		if (cur_car->dim != dim || cur_car->dir == dir) {++num;} // include if not going in opposite direction
	}
	return num;
}

float car_t::get_sum_len_space_for_cars_in_front(cube_t const &range) const {
	float len(0.0);
	car_t const *cur_car(this);

	// Note: should exit once we reach the end of the line of cars, or once we go off the current road segment;
	// this iteration may be very long for cars stopped on long, congested connector roads;
	// however, it should only be queried by one other car per frame, which means this is overall constant time per frame;
	// limit to 1000 iterations in case something goes wrong and we get an circular chain of cars (all stopped at the same spot?)
	for (unsigned n = 0; n < 1000; ++n) { // avg len = city_params.get_nom_car_size().x
		if (cur_car->dim != dim || cur_car->dir == dir) {len += cur_car->get_length();} // include if not going in opposite direction
		cur_car = cur_car->car_in_front;
		if (!cur_car || !range.contains_pt_xy(cur_car->get_center())) break;
	}
	return len * (1.0 + MIN_CAR_STOP_SEP); // car length + stopped space (including one extra space for the car behind us)
}

bool car_t::proc_sphere_coll(point &pos, point const &p_last, float radius, vector3d const &xlate, vector3d *cnorm) const {
	return sphere_cube_int_update_pos(pos, radius, (bcube + xlate), p_last, 1, 0, cnorm); // Note: approximate when car is tilted or turning
	//return sphere_sphere_int((bcube.get_cube_center() + xlate), pos, bcube.get_bsphere_radius(), radius, cnorm, pos); // Note: handle cnorm in if using this
}

bool car_t::check_collision(car_t &c, road_gen_base_t const &road_gen) {

	if (c.dim != dim) { // turning in an intersection, etc. (Note: may not be needed, but at least need to return here)
		car_t *to_stop(nullptr);
		if (c.front_intersects_car(*this)) {to_stop = &c;}
		else if (front_intersects_car(c))  {to_stop = this;}
		if (!to_stop) return 0;
		to_stop->decelerate_fast(); // attempt to prevent one car from T-boning the other
		to_stop->bcube = to_stop->prev_bcube;
		to_stop->honk_horn_if_close_and_fast();
		return 1;
	}
	if (dir != c.dir) return 0; // traveling on opposite sides of the road
	float const sep_dist(get_min_sep_dist_to_car(c));
	float const test_dist(0.999*sep_dist); // slightly smaller than separation distance
	cube_t bcube_ext(bcube);
	bcube_ext.d[dim][0] -= test_dist; bcube_ext.d[dim][1] += test_dist; // expand by test_dist distance
	if (!bcube_ext.intersects_xy(c.bcube)) return 0;
	float const front(bcube.d[dim][dir]), c_front(c.bcube.d[dim][dir]);
	bool const move_c((front < c_front) ^ dir); // move the car that's behind
	// Note: we could slow the car in behind, but that won't work for initial placement collisions when speed == 0
	car_t &cmove(move_c ? c : *this); // the car that will be moved
	car_t const &cstay(move_c ? *this : c); // the car that won't be moved
	//cout << "Collision between " << cmove.str() << " and " << cstay.str() << endl;
	if (cstay.is_stopped()) {cmove.decelerate_fast();} else {cmove.decelerate();}
	float const dist(cstay.bcube.d[dim][!dir] - cmove.bcube.d[dim][dir]); // signed distance between the back of the car in front, and the front of the car in back
	point delta(all_zeros);
	delta[dim] += dist + (cmove.dir ? -sep_dist : sep_dist); // force separation between cars
	cube_t const &bcube(road_gen.get_bcube_for_car(cmove));
	if (cstay.max_speed < cmove.max_speed) {cmove.front_car_turn_dir = cstay.turn_dir;} // record the turn dir of this slow car in front of us so we can turn a different way

	if (!bcube.contains_cube_xy(cmove.bcube + delta)) { // moved outside its current road segment bcube
		//if (cmove.bcube == cmove.prev_bcube) {return 1;} // collided, but not safe to move the car (init pos or second collision)
		if (cmove.bcube != cmove.prev_bcube) { // try resetting to last frame's position
			cmove.bcube  = cmove.prev_bcube; // restore prev frame's pos
			//cmove.honk_horn_if_close_and_fast();
			return 1; // done
		}
		else { // keep the car from moving outside its current segment (init collision case)
			if (cmove.dir) {max_eq(delta[dim], min(0.0f, 0.999f*(bcube.d[cmove.dim][0] - cmove.bcube.d[cmove.dim][0])));}
			else           {min_eq(delta[dim], max(0.0f, 0.999f*(bcube.d[cmove.dim][1] - cmove.bcube.d[cmove.dim][1])));}
		}
	}
	cmove.bcube += delta;
	return 1;
}


bool comp_car_road_then_pos::operator()(car_t const &c1, car_t const &c2) const { // sort spatially for collision detection and drawing
	if (c1.cur_city != c2.cur_city) return (c1.cur_city < c2.cur_city);
	if (c1.is_parked() != c2.is_parked()) {return c2.is_parked();} // parked cars last
	if (c1.cur_road != c2.cur_road) return (c1.cur_road < c2.cur_road);

	if (c1.is_parked()) { // sort parked cars back to front relative to camera so that alpha blending works
		return (p2p_dist_xy_sq(c1.bcube.get_cube_center(), camera_pos) > p2p_dist_xy_sq(c2.bcube.get_cube_center(), camera_pos));
	}
	return (c1.bcube.d[c1.dim][c1.dir] < c2.bcube.d[c2.dim][c2.dir]); // compare front end of car (used for collisions)
}


void ao_draw_state_t::draw_ao_qbd() {
	if (ao_qbd.empty()) return;
	enable_blend();
	select_texture(BLUR_CENT_TEX);
	ao_qbd.draw_and_clear();
	select_texture(WHITE_TEX); // reset back to default/untextured
	disable_blend();
}

void occlusion_checker_t::set_camera(pos_dir_up const &pdu) {
	if ((display_mode & 0x08) == 0) {state.building_ids.clear(); return;} // testing
	pos_dir_up near_pdu(pdu);
	near_pdu.far_ = 2.0*city_params.road_spacing; // set far clipping plane to one city block (currently 3.0)
	get_city_building_occluders(near_pdu, state);
	//cout << "occluders: " << state.building_ids.size() << endl;
}
bool occlusion_checker_t::is_occluded(cube_t const &c) {
	if (state.building_ids.empty() && occluders.empty()) return 0;
	float const z(c.z2()); // top edge
	point const corners[4] = {point(c.x1(), c.y1(), z), point(c.x2(), c.y1(), z), point(c.x2(), c.y2(), z), point(c.x1(), c.y2(), z)};
	if (check_city_pts_occluded(corners, 4, state)) return 1;

	for (auto c = occluders.begin(); c != occluders.end(); ++c) {
		bool occluded(1);

		for (unsigned i = 0; i < 4; ++i) {
			if (!check_line_clip(state.pos, (corners[i] + state.xlate), c->d)) {occluded = 0; break;}
		}
		if (occluded) return 1;
	} // for i
	return 0;
}

void ao_draw_state_t::pre_draw(vector3d const &xlate_, bool use_dlights_, bool shadow_only_) {
	draw_state_t::pre_draw(xlate_, use_dlights_, shadow_only_, 1); // always_setup_shader=1 (required for model drawing)
	
	if (!shadow_only) {
		occlusion_checker.set_exclude_camera_building(); // if the player is inside a building, skip occlusion culling
		occlusion_checker.set_camera(camera_pdu);
	}
}

/*static*/ float car_draw_state_t::get_headlight_dist() {return 3.5*city_params.road_width;} // distance headlights will shine

colorRGBA car_draw_state_t::get_headlight_color(car_t const &car) const {
	return colorRGBA(1.0, 1.0, (1.0 + 0.8*(fract(1000.0*car.max_speed) - 0.5)), 1.0); // slight yellow-blue tinting using max_speed as a hash
}

void car_draw_state_t::pre_draw(vector3d const &xlate_, bool use_dlights_, bool shadow_only_) {
	//set_enable_normal_map(use_model3d_bump_maps()); // used only for some car models, and currently doesn't work
	ao_draw_state_t::pre_draw(xlate_, use_dlights_, shadow_only_);
	select_texture(WHITE_TEX);
}

void car_draw_state_t::draw_unshadowed() {
	qbds[0].draw_and_clear();
	draw_ao_qbd();
}

void car_draw_state_t::add_car_headlights(vector<car_t> const &cars, vector3d const &xlate_, cube_t &lights_bcube) {
	xlate = xlate_; // needed earlier in the flow
	for (auto i = cars.begin(); i != cars.end(); ++i) {add_car_headlights(*i, lights_bcube);}
}

/*static*/ void car_draw_state_t::gen_car_pts(car_t const &car, bool include_top, point pb[8], point pt[8]) {
	point const center(car.get_center());
	cube_t const &c(car.bcube);
	float const z1(center.z - 0.5*car.height), z2(center.z + 0.5*car.height), zmid(center.z + (include_top ? 0.1 : 0.5)*car.height), length(car.get_length());
	bool const dim(car.dim), dir(car.dir);
	set_cube_pts(c, z1, zmid, dim, dir, pb); // bottom

	if (include_top) {
		cube_t top_part(c);
		top_part.d[dim][0] += (dir ? 0.25 : 0.30)*length; // back
		top_part.d[dim][1] -= (dir ? 0.30 : 0.25)*length; // front
		set_cube_pts(top_part, zmid, z2, dim, dir, pt); // top
	}
	if (car.dz != 0.0) { // rotate all points about dim !d
		float const sine_val((dir ? 1.0f : -1.0f)*car.dz/length), cos_val(sqrt(1.0f - sine_val*sine_val));
		rotate_pts(center, sine_val, cos_val, dim, 2, pb);
		if (include_top) {rotate_pts(center, sine_val, cos_val, dim, 2, pt);}
	}
	if (car.rot_z != 0.0) { // turning about the z-axis: rot_z of [0.0, 1.0] maps to angles of [0.0, PI/2=90 degrees]
		float const sine_val(sinf(0.5f*PI*car.rot_z)), cos_val(sqrt(1.0f - sine_val*sine_val));
		rotate_pts(center, sine_val, cos_val, 0, 1, pb);
		if (include_top) {rotate_pts(center, sine_val, cos_val, 0, 1, pt);}
	}
}

bool sphere_in_light_cone_approx(pos_dir_up const &pdu, point const &center, float radius) {
	float const dist(p2p_dist(pdu.pos, center)), radius_at_dist(dist*pdu.sterm), rmod(radius_at_dist + radius);
	return pt_line_dist_less_than(center, pdu.pos, (pdu.pos + pdu.dir), rmod);
}

void car_draw_state_t::draw_car(car_t const &car, bool is_dlight_shadows, bool in_garage) { // Note: all quads
	if (car.destroyed) return;
	point const center(car.get_center());

	if (is_dlight_shadows) { // dynamic spotlight shadow; xlate should be all zeros in this case
		if (!dist_less_than(camera_pdu.pos, center, 0.6*camera_pdu.far_)) return; // optimization
		// since we know the dlight is a spotlight with a cone shape rather than a frustum, we can do a tighter visibility test
		if (!sphere_in_light_cone_approx(camera_pdu, center, car.bcube.get_xy_bsphere_radius())) return;
		if (car.bcube.contains_pt_exp(camera_pdu.pos, 0.1*car.height)) return; // don't self-shadow
	}
	point const center_xlated(center + xlate);
	if (!shadow_only && !dist_less_than(camera_pdu.pos, center_xlated, 0.5*draw_tile_dist)) return; // check draw distance, dist_scale=0.5
	if (!camera_pdu.sphere_visible_test(center_xlated, 0.5f*car.height*CAR_RADIUS_SCALE) || !camera_pdu.cube_visible(car.bcube + xlate)) return;
	begin_tile(center); // enable shadows
	colorRGBA const &color(car.get_color());
	float const dist_val(p2p_dist(camera_pdu.pos, center_xlated)/draw_tile_dist);
	bool const draw_top(dist_val < 0.25 && !car.is_truck), dim(car.dim), dir(car.dir);
	bool const draw_model(car_model_loader.num_models() > 0 &&
		(is_dlight_shadows ? dist_less_than(pre_smap_player_pos, center, 0.05*draw_tile_dist) : (shadow_only || dist_val < 0.05)));
	float const sign((dim^dir) ? -1.0 : 1.0);
	point pb[8], pt[8]; // bottom and top sections
	gen_car_pts(car, draw_top, pb, pt);

	if (draw_model && car_model_loader.is_model_valid(car.model_id)) {
		if (!in_garage && is_occluded(car.bcube)) return; // only check occlusion for expensive car models; skip cars in garages (maybe exclude the containing building?)
		vector3d const front_n(cross_product((pb[5] - pb[1]), (pb[0] - pb[1])).get_norm()*sign);
		car_model_loader.draw_model(s, center, car.bcube, front_n, color, xlate, car.model_id, shadow_only, (dist_val > 0.035));
	}
	else { // draw simple 1-2 cube model
		quad_batch_draw &qbd(qbds[emit_now]);
		color_wrapper cw(color);
		draw_cube(qbd, cw, center, pb, 1, (dim^dir)); // bottom (skip_bottom=1)
		if (draw_top) {draw_cube(qbd, cw, center, pt, 1, (dim^dir));} // top (skip_bottom=1)
		if (emit_now) {qbds[1].draw_and_clear();} // shadowed (only emit when tile changes?)
	}
	if (shadow_only) return; // shadow pass - done
	if (car.cur_road_type == TYPE_BUILDING) return; // in a garage/building, nothing else to draw

	if (dist_val < 0.04 && fabs(car.dz) < 0.01) { // add AO planes when close to the camera and on a level road
		float const length(car.get_length());
		point pao[4];

		for (unsigned i = 0; i < 4; ++i) {
			point &v(pao[i]);
			v = pb[i] - center;
			v[ dim] += 0.1*length*SIGN(v[ dim]); // increase length slightly
			v[!dim] += 0.1*length*SIGN(v[!dim]); // increase width  slightly
			v   += center;
			v.z += 0.02*car.height; // shift up slightly to avoid z-fighting
		}
		/*if (!car.headlights_on()) { // daytime, adjust shadow to match sun pos
			vector3d const sun_dir(0.5*length*(center - get_sun_pos()).get_norm());
			vector3d const offset(sun_dir.x, sun_dir.y, 0.0);
			for (unsigned i = 0; i < 4; ++i) {pao[i] += offset;} // problems: double shadows, non-flat surfaces, buildings, texture coords/back in center, non-rectangular
		}*/
		ao_qbd.add_quad_pts(pao, colorRGBA(0, 0, 0, 0.9), plus_z);
	}
	if (dist_val > 0.3)  return; // to far - no lights to draw
	if (car.is_parked()) return; // no lights when parked
	vector3d const front_n(cross_product((pb[5] - pb[1]), (pb[0] - pb[1])).get_norm()*sign);
	unsigned const lr_xor(((camera_pdu.pos[!dim] - xlate[!dim]) - center[!dim]) < 0.0f);
	bool const brake_lights_on(car.is_almost_stopped() || car.stopped_at_light), headlights_on(car.headlights_on());
	float const hv1(car.is_truck ? 0.8 : 0.2), hv2(1.0 - hv1); // headlights and tail lights; blend from bottom to top
	float const sv1(car.is_truck ? 0.8 : 0.3), sv2(1.0 - hv1); // turn signals; blend from bottom to top

	if (headlights_on && dist_val < 0.3) { // night time headlights
		colorRGBA const hl_color(get_headlight_color(car));

		for (unsigned d = 0; d < 2; ++d) { // L, R
			unsigned const lr(d ^ lr_xor ^ 1);
			point const pos((lr ? 0.2 : 0.8)*(hv1*pb[0] + hv2*pb[4]) + (lr ? 0.8 : 0.2)*(hv1*pb[1] + hv2*pb[5]));
			add_light_flare(pos, front_n, hl_color, 2.0, 0.65*car.height); // pb 0,1,4,5
		}
	}
	if ((brake_lights_on || headlights_on || car.in_reverse) && dist_val < 0.2) { // brake/tail/backup lights
		for (unsigned d = 0; d < 2; ++d) { // L, R
			unsigned const lr(d ^ lr_xor);
			point const pos((lr ? 0.2 : 0.8)*(hv1*pb[2] + hv2*pb[6]) + (lr ? 0.8 : 0.2)*(hv1*pb[3] + hv2*pb[7]));
			colorRGBA const bl_color(car.in_reverse ? colorRGBA(1.0, 0.9, 0.7, 1.0) : colorRGBA(1.0, 0.1, 0.05, 1.0)); // yellow-white/near red; pb 2,3,6,7
			add_light_flare(pos, -front_n, bl_color, (brake_lights_on ? 1.0 : 0.5), 0.5*car.height);
		}
	}
	if (car.turn_dir != TURN_NONE && car.cur_city != CONN_CITY_IX && dist_val < 0.1) { // turn signals (not on connector road bends)
		float const ts_period = 1.5; // in seconds
		double const time(fract((tfticks + 1000.0*car.max_speed)/(double(ts_period)*TICKS_PER_SECOND))); // use car max_speed as seed to offset time base

		if (time > 0.5) { // flash on and off
			bool const tdir((car.turn_dir == TURN_LEFT) ^ dim ^ dir); // R=1,2,5,6 or L=0,3,4,7
			vector3d const side_n(cross_product((pb[6] - pb[2]), (pb[1] - pb[2])).get_norm()*sign*(tdir ? 1.0 : -1.0));

			for (unsigned d = 0; d < 2; ++d) { // B, F
				point const pos(sv1*pb[tdir ? (d ? 1 : 2) : (d ? 0 : 3)] + sv2*pb[tdir ? (d ? 5 : 6) : (d ? 4 : 7)]);
				add_light_flare(pos, (side_n + (d ? 1.0 : -1.0)*front_n).get_norm(), colorRGBA(1.0, 0.75, 0.0, 1.0), 1.5, 0.3*car.height); // normal points out 45 degrees
			}
		}
	}
}

void car_draw_state_t::draw_helicopter(helicopter_t const &h, bool shadow_only) {
	if (shadow_only && !h.dynamic_shadow && h.state != helicopter_t::STATE_WAIT) return; // don't draw moving helicopters in the shadow pass; wait until they land
	if (!check_cube_visible(h.bcube, (shadow_only ? 0.0 : 0.75))) return; // dist_scale=0.75
	if (is_occluded(h.bcube)) return; // yes, this seems to work
	assert(helicopter_model_loader.is_model_valid(h.model_id));
	point const center(h.bcube.get_cube_center());
	begin_tile(center); // enable shadows
	city_model_t const &model(helicopter_model_loader.get_model(h.model_id));
	unsigned blade_mat_mask(0);

	if (h.blade_rot != 0.0 && model.blade_mat_id >= 0) { // separate blades from the rest of the model for custom rotation
		blade_mat_mask = ~(1 << model.blade_mat_id); // skip prop blades material
		vector3d dir(h.dir);
		rotate_vector3d(plus_z, h.blade_rot, dir);
		helicopter_model_loader.draw_model(s, center, h.bcube, dir, WHITE, xlate, h.model_id, shadow_only, 0, 0, blade_mat_mask); // draw prop blades only
		blade_mat_mask = ~blade_mat_mask;
	}
	helicopter_model_loader.draw_model(s, center, h.bcube, h.dir, WHITE, xlate, h.model_id, shadow_only, 0, 0, blade_mat_mask); // low_detail=0, enable_animations=0
}

void car_draw_state_t::add_car_headlights(car_t const &car, cube_t &lights_bcube) {
	if (!car.headlights_on()) return;
	float const headlight_dist(get_headlight_dist());
	cube_t bcube(car.bcube);
	bcube.expand_by(headlight_dist);
	if (!lights_bcube.contains_cube_xy(bcube))   return; // not contained within the light volume
	if (!camera_pdu.cube_visible(bcube + xlate)) return; // VFC
	float const sign((car.dim^car.dir) ? -1.0 : 1.0);
	point pb[8], pt[8]; // bottom and top sections
	gen_car_pts(car, 0, pb, pt); // draw_top=0
	vector3d const front_n(cross_product((pb[5] - pb[1]), (pb[0] - pb[1])).get_norm()*sign);
	vector3d const dir((0.5*front_n - 0.5*plus_z).get_norm()); // point slightly down
	colorRGBA const color(get_headlight_color(car));
	float const beamwidth = 0.08;
	min_eq(lights_bcube.z1(), bcube.z1());
	max_eq(lights_bcube.z2(), bcube.z2());

	if (!dist_less_than((car.get_center() + xlate), camera_pdu.pos, 2.0*headlight_dist)) { // single merged headlight when far away
		point const pos(0.5*(0.2*pb[0] + 0.8*pb[4] + 0.2*pb[1] + 0.8*pb[5]));
		dl_sources.push_back(light_source(headlight_dist, pos, pos, color*1.333, 1, dir, 1.2*beamwidth));
	}
	else { // two separate left/right headlights
		for (unsigned d = 0; d < 2; ++d) { // L, R
			point const pos((d ? 0.2 : 0.8)*(0.2*pb[0] + 0.8*pb[4]) + (d ? 0.8 : 0.2)*(0.2*pb[1] + 0.8*pb[5]));
			dl_sources.push_back(light_source(headlight_dist, pos, pos, color, 1, dir, beamwidth)); // share shadow maps between headlights?
		}
	}
}


void car_manager_t::remove_destroyed_cars() {
	remove_destroyed(cars);
	car_destroyed = 0;
}

void car_manager_t::init_cars(unsigned num) {
	if (num == 0) return;
	timer_t timer("Init Cars");
	cars.reserve(num);
	for (unsigned n = 0; n < num; ++n) {add_car();}
	cout << "Dynamic Cars: " << cars.size() << endl;
}

void car_manager_t::add_parked_cars(vector<car_t> const &new_cars, vect_cube_t const &garages) {
	first_parked_car = cars.size(); // Note: sort may invalidate this, but okay for use in finalize_cars()
	cars.reserve(cars.size() + new_cars.size() + garages.size());
	vector_add_to(new_cars, cars);
	first_garage_car = cars.size(); // Note: sort may invalidate this, but okay for use in finalize_cars()
	if (garages.empty()) return; // done
	vector3d const nom_car_size(city_params.get_nom_car_size());
	car_t car; // no cur_city/cur_road/cur_segq
	car.park();
	car.cur_city      = NO_CITY_IX; // special value
	car.cur_road_type = TYPE_BUILDING; // garage
	rand_gen_t rgen;
	
	for (auto i = garages.begin(); i != garages.end(); ++i) {
		if ((rgen.rand()&3) == 0) continue; // 25% of garages have no car
		car.dim = (i->dx() < i->dy()); // long dim
		car.dir = rgen.rand_bool(); // Note: ignores garage dir because some cars and backed in and some are pulled in
		point const center(i->get_cube_center());
		car.set_bcube(point(center.x, center.y, i->z1()), nom_car_size);
		assert(i->contains_cube_xy(car.bcube));
		cars.push_back(car);
		garages_bcube.assign_or_union_with_cube(car.bcube);
	} // for i
}

void car_manager_t::finalize_cars() {
	if (empty()) return;
	unsigned const num_models(car_model_loader.num_models());

	for (auto i = cars.begin(); i != cars.end(); ++i) {
		int fixed_color(-1);

		if (num_models > 0) {
			for (unsigned n = 0; n < 20; ++n) {
				if (FORCE_MODEL_ID >= 0) {i->model_id = (unsigned char)FORCE_MODEL_ID;}
				else {i->model_id = ((num_models > 1) ? (rgen.rand() % num_models) : 0);}
				city_model_t const &model(car_model_loader.get_model(i->model_id));
				// if there are multiple models to choose from, and this car is in a garage, try for a model that's not scaled up (the truck) (what about driveways?)
				if (FORCE_MODEL_ID < 0 && num_models > 1 && unsigned(i-cars.begin()) >= first_garage_car && n+1 < 20 && model.scale > 1.0) continue;
				fixed_color = model.fixed_color_id;
				i->apply_scale(model.scale);
				break;
			} // for n
		}
		i->color_id = ((fixed_color >= 0) ? fixed_color : (rgen.rand() % NUM_CAR_COLORS));
		assert(i->is_valid());
	} // for i
	cout << "Total Cars: " << cars.size() << endl; // 4000 on the road + 4372 parked + 433 garage (out of 594) = 8805
}

vector3d car_manager_t::get_helicopter_size(unsigned model_id) { // Note: non-const because this call may load the model
	return city_params.get_nom_car_size()*helicopter_model_loader.get_model(model_id).scale;
}

void car_manager_t::add_helicopters(vect_cube_t const &hp_locs) {
	unsigned const num_models(helicopter_model_loader.num_models());
	if (num_models == 0) return;
	helipads.resize(hp_locs.size());

	for (auto i = hp_locs.begin(); i != hp_locs.end(); ++i) {
		unsigned const hp_ix(i - hp_locs.begin());
		helipad_t &helipad(helipads[hp_ix]);
		helipad.bcube = *i;
		if (rgen.rand_bool()) continue; // add 50% of the time
		unsigned const model_id((num_models == 0) ? 0 : (rgen.rand()%num_models));
		if (!helicopter_model_loader.is_model_valid(model_id)) continue; // no model to draw, skip this helicopter
		vector3d const helicopter_sz(get_helicopter_size(model_id));
		vector3d const dir(rgen.signed_rand_vector_xy().get_norm()); // random direction
		point const center(i->get_cube_center()); // Note: delta_z should be 0
		cube_t bcube;
		bcube.z2() = helicopter_sz.z; // z1 at helipad surface, z2 at helicopter height (after adding center)
		// Note: since we're going to be rotating the helicopter, and we can't get the correct AA bcube when it's rotated at an off-axis angle, take the max of the length and width;
		//       this will be somewhere between the proper length/width and the AA bcube of the model, which is at most sqrt(2) larger at 45 degrees rotated;
		//       it doesn't have to be perfect because we're not doing collision checks
		bcube.expand_by_xy(0.5*max(helicopter_sz.x, helicopter_sz.y));
		helicopter_t helicopter((bcube + center), dir, model_id, hp_ix, DYNAMIC_HELICOPTERS);
		if (helicopter.dynamic) {helicopter.wait_time = rgen.rand_uniform(5.0, 30.0);} // delay 5-30s to prevent all helicopters from lifting off at the same time
		helicopters.push_back(helicopter);
		helipad.in_use = 1;
	} // for i
	cout << TXT(helipads.size()) << TXT(helicopters.size()) << endl; // 55/30
}

void car_city_vect_t::clear_cars() {
	for (unsigned d = 0; d < 2; ++d) {cars[d][0].clear(); cars[d][1].clear();}
}

void car_manager_t::extract_car_data(vector<car_city_vect_t> &cars_by_city) const {
	if (cars.empty()) return;
	//timer_t timer("Extract Car Data");
	// create parked cars vectors on first call; this is used for pedestrian navigation within parking lots;
	// it won't be rebuilt on car destruction, but that should be okay
	bool const add_parked_cars(cars_by_city.empty());
	for (auto i = cars_by_city.begin(); i != cars_by_city.end(); ++i) {i->clear_cars();} // clear prev frame's state

	for (auto i = cars.begin(); i != cars.end(); ++i) {
		if (i->cur_city >= cars_by_city.size()) {cars_by_city.resize(i->cur_city+1);}
		auto &dest(cars_by_city[i->cur_city]);
		if (!i->is_parked()) {dest.cars[i->dim][i->dir].push_back(*i);} // moving on road
		else if (add_parked_cars) {dest.parked_car_bcubes.emplace_back(i->bcube, i->cur_road);} // parked, not yet updated
	}
}

bool car_manager_t::proc_sphere_coll(point &pos, point const &p_last, float radius, vector3d *cnorm) const { // pos is in camera space
	vector3d const xlate(get_camera_coord_space_xlate());
	float const dist(p2p_dist(pos, p_last));

	for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
		cube_t const city_bcube(get_cb_bcube(*cb) + xlate);
		if (pos.z - radius > city_bcube.z2() + city_params.get_max_car_size().z) continue; // above the cars
		if (!sphere_cube_intersect_xy(pos, (radius + dist), city_bcube)) continue;
		cube_t sphere_bc; sphere_bc.set_from_sphere((pos - xlate), radius);
		unsigned start(0), end(0);
		get_car_ix_range_for_cube(cb, sphere_bc, start, end);

		for (unsigned c = start; c != end; ++c) {
			if (cars[c].proc_sphere_coll(pos, p_last, radius, xlate, cnorm)) return 1;
		}
	} // for cb
	return 0;
}

void car_manager_t::destroy_cars_in_radius(point const &pos_in, float radius) {
	vector3d const xlate(get_camera_coord_space_xlate());
	point const pos(pos_in - xlate);
	bool const is_pt(radius == 0.0);

	for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
		cube_t const city_bcube(get_cb_bcube(*cb));
		if (pos.z - radius > city_bcube.z2() + city_params.get_max_car_size().z) continue; // above the cars
		if (is_pt ? !city_bcube.contains_pt_xy(pos) : !sphere_cube_intersect_xy(pos, radius, city_bcube)) continue;
		unsigned const start(cb->start), end((cb+1)->start); // Note: shouldnt be called frequently enough to need road/parking lot acceleration
		assert(end <= cars.size() && start <= end);

		for (unsigned c = start; c != end; ++c) {
			car_t &car(cars[c]);

			if (is_pt ? car.bcube.contains_pt(pos) : dist_less_than(car.get_center(), pos, radius)) { // destroy if within the sphere
				car.destroy();
				car_destroyed = 1;
				// invalidate tile shadow map for destroyed parked cars
				if (city_params.car_shadows && car.is_parked()) {invalidate_tile_smap_at_pt((car.get_center() + xlate), 0.5*car.get_length());} // radius = length/2
			}
		} // for c
	} // for cb
}

bool car_manager_t::get_color_at_xy(point const &pos, colorRGBA &color, int int_ret) const { // Note: pos in local TT space
	if (cars.empty()) return 0;
	if (int_ret != INT_ROAD && int_ret != INT_PARKING) return 0; // not a road or a parking lot - no car intersections

	for (auto cb = car_blocks_by_road.begin(); cb+1 < car_blocks_by_road.end(); ++cb) { // use cars_by_road to accelerate query
		if (!get_cb_bcube(*cb).contains_pt_xy(pos)) continue; // skip
		unsigned start(cb->start), end((cb+1)->start);
		if      (int_ret == INT_ROAD)    {end   = cb->first_parked;} // moving cars only (beginning of range)
		else if (int_ret == INT_PARKING) {start = cb->first_parked;} // parked cars only (end of range)
		assert(start <= end);
		assert(end < cars_by_road.size()); // strictly less

		for (unsigned i = start; i != end; ++i) {
			cube_with_ix_t const &v(cars_by_road[i]);
			if (!v.contains_pt_xy(pos)) continue; // skip
			unsigned const ix_end(cars_by_road[i+1].ix);
			assert(ix_end <= cars.size());

			for (unsigned c = v.ix; c != ix_end; ++c) {
				if (cars[c].bcube.contains_pt_xy(pos)) {color = cars[c].get_color(); return 1;}
			}
		}
	} // for cb
	return 0;
}

car_t const *car_manager_t::get_car_at_pt(point const &pos, bool is_parked) const {
	for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
		if (!get_cb_bcube(*cb).contains_pt_xy(pos)) continue; // skip
		unsigned start(cb->start), end((cb+1)->start);
		if (!is_parked) {end   = cb->first_parked;} // moving cars only (beginning of range)
		else            {start = cb->first_parked;} // parked cars only (end of range)
		if (start > end || end > cars.size()) {cout << TXT(start) << TXT(end) << TXT(cars.size()) << TXT(is_parked) << endl;}
		assert(start <= end && end <= cars.size());

		for (unsigned c = start; c != end; ++c) {
			if (cars[c].bcube.contains_pt_xy(pos)) {return &cars[c];}
		}
	} // for cb
	return nullptr; // no car found
}

car_t const *car_manager_t::get_car_at(point const &p1, point const &p2) const { // Note: p1/p2 in local TT space
	for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
		if (!get_cb_bcube(*cb).line_intersects(p1, p2)) continue; // skip
		unsigned start(cb->start), end((cb+1)->start);
		assert(start <= end && end <= cars.size());

		for (unsigned c = start; c != end; ++c) { // Note: includes parked cars
			if (cars[c].bcube.line_intersects(p1, p2)) {return &cars[c];}
		}
	} // for cb
	return nullptr; // no car found
}
car_t const *car_manager_t::get_car_at_player(float max_dist) const {
	point const p1(get_camera_building_space()), p2(p1 + cview_dir*max_dist);
	return get_car_at(p1, p2);
}

bool car_manager_t::line_intersect_cars(point const &p1, point const &p2, float &t) const { // Note: p1/p2 in local TT space
	bool ret(0);

	for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
		if (!get_cb_bcube(*cb).line_intersects(p1, p2)) continue; // skip
		unsigned start(cb->start), end((cb+1)->start);
		assert(start <= end && end <= cars.size());

		for (unsigned c = start; c != end; ++c) { // Note: includes parked cars
			ret |= check_line_clip_update_t(p1, p2, t, cars[c].bcube);
		}
	} // for cb
	return ret;
}

int car_manager_t::find_next_car_after_turn(car_t &car) {
	road_isec_t const &isec(get_car_isec(car));
	if (car.turn_dir == TURN_NONE && !isec.is_global_conn_int()) return -1; // car not turning, and not on connector road isec: should be handled by sorted car_in_front logic
	unsigned const dest_orient(isec.get_dest_orient_for_car_in_isec(car, 0)); // Note: may be before, during, or after turning
	int road_ix(isec.rix_xy[dest_orient]), seg_ix(isec.conn_ix[dest_orient]);
	unsigned city_ix(car.cur_city);
	//cout << TXT(car.get_orient()) << TXT(dest_orient) << TXT(city_ix) << TXT(road_ix) << TXT(seg_ix) << endl;
	assert((road_ix < 0) == (seg_ix < 0));

	if (road_ix < 0) { // goes to connector road
		city_ix = CONN_CITY_IX;
		road_ix = decode_neg_ix(road_ix);
		seg_ix  = decode_neg_ix(seg_ix );
	}
	point const car_center(car.get_center());
	float dmin(car.get_max_lookahead_dist()), dmin_sq(dmin*dmin);
	// include normal sorted order car; this is needed when going straight through connector road 4-way intersections where cur_road changes within the intersection
	if (car.car_in_front && car.car_in_front->get_orient() != dest_orient) {car.car_in_front = 0;} // not the correct car (turning a different way)
	if (car.turn_dir == TURN_NONE && car.car_in_front) {min_eq(dmin_sq, p2p_dist_sq(car_center, car.car_in_front->get_center()));}
	int ret_car_ix(-1);

	for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
		if (cb->cur_city != city_ix) continue; // incorrect city - skip
		unsigned const start(cb->start), end(cb->first_parked);
		assert(end <= cars.size() && start <= end);
		auto range_end(cars.begin()+end);
		car_t ref_car; ref_car.cur_road = road_ix;
		auto it(std::lower_bound(cars.begin()+start, range_end, ref_car, comp_car_road())); // binary search acceleration
		float prev_dist_sq(FLT_MAX);

		for (; it != range_end; ++it) {
			if (&(*it) == &car) continue; // skip self
			assert(it->cur_city == city_ix); // must be same city
			if (it->cur_road != road_ix) break; // different road, done

			if (it->cur_road_type == TYPE_RSEG) { // road segment
				if (it->cur_seg != seg_ix) continue; // on a different segment, skip
			}
			else if (it->cur_road_type != car.cur_road_type || it->cur_seg != car.cur_seg) continue; // in a different intersection
			if (it->get_orient() != dest_orient) continue; // wrong orient
			float const dist_sq(p2p_dist_sq(car_center, it->get_center()));
			if (p2p_dist_sq(car_center, it->get_front()) < dist_sq) continue; // front is closer than back - this car is not in front of us (waiting on other side of isect?)
			//cout << TXT(dmin_sq) << TXT(dist_sq) << (dist_sq < dmin_sq) << endl;

			if (dist_sq < dmin_sq) { // new closest car
				if (&(*it) != car.car_in_front) {ret_car_ix = (it - cars.begin());} // record index if set to a new value
				car.car_in_front = &(*it);
				dmin_sq = dist_sq;
			}
			else if (dist_sq > prev_dist_sq) break; // we're moving too far away from the car
			prev_dist_sq = dist_sq;
		} // for it
	} // for cb
	return ret_car_ix;
}

bool car_manager_t::check_car_for_ped_colls(car_t &car) const {
	if (car.cur_city >= peds_crossing_roads.peds.size())  return 0; // no peds in this city (includes connector road network)
	if (car.turn_val != 0.0 || car.turn_dir != TURN_NONE) return 0; // for now, don't check for cars when turning as this causes problems with blocked intersections
	auto const &peds_by_road(peds_crossing_roads.peds[car.cur_city]);
	if (car.cur_road >= peds_by_road.size()) return 0; // no peds in this road
	auto const &peds(peds_by_road[car.cur_road]);
	if (peds.empty()) return 0;
	cube_t coll_area(car.bcube);
	coll_area.d[ car.dim][!car.dir] = coll_area.d[car.dim][car.dir]; // exclude the car itself
	coll_area.d[ car.dim][car.dir] += (car.dir ? 1.25 : -1.25)*car.get_length(); // extend the front
	coll_area.d[!car.dim][0] -= 0.5*car.get_width();
	coll_area.d[!car.dim][1] += 0.5*car.get_width();
	static rand_gen_t rgen;

	for (auto i = peds.begin(); i != peds.end(); ++i) {
		if (coll_area.contains_pt_xy_exp(i->pos, i->radius)) {
			car.decelerate_fast();
			if ((rgen.rand()&3) == 0) {car.honk_horn_if_close_and_fast();}
			return 1;
		}
	} // for i
	return 0;
}

void car_manager_t::next_frame(ped_manager_t const &ped_manager, float car_speed) {
	if (!animate2) return;
	helicopters_next_frame(car_speed);
	if (cars.empty()) return;
	// Warning: not really thread safe, but should be okay; the ped state should valid at all points (thought maybe inconsistent) and we don't need it to be exact every frame
	ped_manager.get_peds_crossing_roads(peds_crossing_roads);
	//timer_t timer("Update Cars"); // 4K cars = 0.7ms / 2.1ms with destinations + navigation
#pragma omp critical(modify_car_data)
	{
		if (car_destroyed) {remove_destroyed_cars();} // at least one car was destroyed in the previous frame - remove it/them
		sort(cars.begin(), cars.end(), comp_car_road_then_pos(camera_pdu.pos - dstate.xlate)); // sort by city/road/position for intersection tests and tile shadow map binds
	}
	entering_city.clear();
	car_blocks.clear();
	float const speed(CAR_SPEED_SCALE*car_speed*get_clamped_fticks());
	bool saw_parked(0);

	for (auto i = cars.begin(); i != cars.end(); ++i) { // move cars
		unsigned const cix(i - cars.begin());
		i->car_in_front = nullptr; // reset for this frame

		if (car_blocks.empty() || i->cur_city != car_blocks.back().cur_city) {
			if (!saw_parked && !car_blocks.empty()) {car_blocks.back().first_parked = cix;} // no parked cars in prev city
			saw_parked = 0; // reset for next city
			car_blocks.emplace_back(cix, i->cur_city);
		}
		if (i->is_parked()) {
			if (!saw_parked) {car_blocks.back().first_parked = cix; saw_parked = 1;}
			i->maybe_wake(rgen);
			continue; // no update for parked cars
		}
		i->move(speed);
		if (i->entering_city) {entering_city.push_back(cix);} // record for use in collision detection
		if (!i->stopped_at_light && i->is_almost_stopped() && i->in_isect()) {get_car_isec(*i).stoplight.mark_blocked(i->dim, i->dir);} // blocking intersection
		register_car_at_city(*i);
	} // for i
	if (!saw_parked && !car_blocks.empty()) {car_blocks.back().first_parked = cars.size();} // no parked cars in final city
	car_blocks.emplace_back(cars.size(), 0); // add terminator

	for (auto i = cars.begin(); i != cars.end(); ++i) { // collision detection
		if (i->is_parked()) continue; // no collisions for parked cars
		bool const on_conn_road(i->cur_city == CONN_CITY_IX);
		float const length(i->get_length()), max_check_dist(max(3.0f*length, (length + i->get_max_lookahead_dist()))); // max of collision dist and car-in-front dist

		for (auto j = i+1; j != cars.end(); ++j) { // check for collisions with cars on the same road (can't test seg because they can be on diff segs but still collide)
			if (i->cur_city != j->cur_city || i->cur_road != j->cur_road) break; // different cities or roads
			if (!on_conn_road && i->cur_road_type == j->cur_road_type && abs((int)i->cur_seg - (int)j->cur_seg) > (on_conn_road ? 1 : 0)) break; // diff road segs or diff isects
			check_collision(*i, *j);
			i->register_adj_car(*j);
			j->register_adj_car(*i);
			if (!dist_xy_less_than(i->get_center(), j->get_center(), max_check_dist)) break;
		}
		if (on_conn_road) { // on connector road, check before entering intersection to a city
			for (auto ix = entering_city.begin(); ix != entering_city.end(); ++ix) {
				if (*ix != unsigned(i - cars.begin())) {check_collision(*i, cars[*ix]);}
			}
		}
		if (i->in_isect()) {
			int const next_car(find_next_car_after_turn(*i)); // Note: calculates in i->car_in_front
			if (next_car >= 0) {check_collision(*i, cars[next_car]);} // make sure we collide with the correct car
		}
		if (!peds_crossing_roads.peds.empty()) {check_car_for_ped_colls(*i);}
	} // for i
	update_cars(); // run update logic

	if (map_mode) { // create cars_by_road
		// cars have moved since the last sort and may no longer be in city/road order, but this algorithm doesn't require that;
		// out-of-order cars will end up in their own blocks, which is less efficient but still correct
		car_blocks_by_road.clear();
		cars_by_road.clear();
		unsigned cur_city(1<<31), cur_road(1<<31); // start at invalid values
		bool saw_parked(0);

		for (auto i = cars.begin(); i != cars.end(); ++i) {
			if (i->cur_road_type == TYPE_BUILDING) continue; // ignore cars in buildings
			bool const new_city(i->cur_city != cur_city), new_parked(!saw_parked && i->is_parked());
			unsigned const cbr_ix(cars_by_road.size());
			if (new_parked && !car_blocks_by_road.empty()) {car_blocks_by_road.back().first_parked = cbr_ix; saw_parked = 1;}

			if (new_city || new_parked || i->cur_road != cur_road) { // new city/road
				if (new_city) { // new city
					if (!saw_parked && !car_blocks_by_road.empty()) {car_blocks_by_road.back().first_parked = cbr_ix;} // no parked cars in prev city
					saw_parked = 0; // reset for next city
					car_blocks_by_road.emplace_back(cbr_ix, i->cur_city);
				}
				cars_by_road.emplace_back(i->bcube, (i - cars.begin())); // start a new block
				cur_city = i->cur_city;
				cur_road = i->cur_road;
			}
			else {cars_by_road.back().union_with_cube(i->bcube);}
		} // for i
		if (!saw_parked && !car_blocks_by_road.empty()) {car_blocks_by_road.back().first_parked = cars_by_road.size();} // no parked cars in final city
		car_blocks_by_road.emplace_back(cars_by_road.size(), 0); // add terminator
		cars_by_road.emplace_back(cube_t(), cars.size()); // add terminator
	}
	//cout << TXT(cars.size()) << TXT(entering_city.size()) << TXT(in_isects.size()) << endl; // TESTING
}

// calculate max zval along line for buildings and terrain; this is not intended to be fast;
// there are at least three possible approaches:
// 1. Step in small increments along the path and test terrain and building heights at each point, similar to player collision detection, and record the max zval
// 2. Similar to 1, but step through each tile and test collision for everything in that tile; probably faster, but requires custom line intersection code
// 3. Cast a ray through the buildings and terrain and incrementally increase the ray's zval until there are no hits; possibly faster, but less accurate
// Note: another limitation is that this is a line query, not a cylinder query, so the helicopter may still clip a building
float get_flight_path_zmax(point const &p1, point const &p2, float radius) {
	//highres_timer_t timer("Get Line Zmax"); // ~0.1ms
	assert(p1.z == p2.z); // for now, only horizontal lines are supported
	float cur_zmax(p1.z);
	// test terrain using approach #1
	float const dist(p2p_dist(p1, p2)), step_sz(min(DX_VAL, DY_VAL)); // step_sz is somewhat arbitrary; smaller is more accurate but slower
	unsigned const num_steps(dist/step_sz + 1);
	vector3d const step((p2 - p1)/float(num_steps));
	point pos(p1 + get_camera_coord_space_xlate()); // convert from building to camera space
	assert(num_steps < 10000); // let's be reasonable

	for (unsigned n = 0; n < num_steps; ++n) {
		max_eq(cur_zmax, get_exact_zval(pos.x, pos.y)); // not using radius here (assumes it's small compared to terrain elevation changes)
		pos += step;
	}
	// test buildings using approach #2
	float const house_extra_zval(20.0*city_params.road_width); // doesn't seem to make a difference, since helicopters don't normally fly over residential areas anyway
	update_buildings_zmax_for_line(p1, p2, radius, house_extra_zval, cur_zmax);
	return cur_zmax;
}


void helicopter_t::invalidate_tile_shadow_map(vector3d const &shadow_offset, bool repeat_next_frame) const {
	invalidate_tile_smap_at_pt((bcube.get_cube_center() + shadow_offset), 0.5*max(bcube.dx(), bcube.dy()), repeat_next_frame);
}

void car_manager_t::helicopters_next_frame(float car_speed) {
	if (helicopters.empty()) return;
	//highres_timer_t timer("Helicopters Update");
	float const clamp_fticks(get_clamped_fticks()), elapsed_secs(clamp_fticks/TICKS_PER_SECOND);
	float const speed(2.0*CAR_SPEED_SCALE*car_speed); // helicopters are 2x faster than cars
	float const takeoff_speed(0.2*speed), land_speed(0.2*speed), rotate_rate(0.02*clamp_fticks);
	float const shadow_thresh(1.0f*(X_SCENE_SIZE + Y_SCENE_SIZE)); // ~1 tile
	point const xlate(get_camera_coord_space_xlate()), camera_bs(camera_pdu.pos - xlate);
	vector3d const shadow_dir(-get_light_pos().get_norm()); // primary light direction (sun/moon)

	for (auto i = helicopters.begin(); i != helicopters.end(); ++i) {
		if (i->state == helicopter_t::STATE_WAIT) { // stopped, assumed on a helipad
			assert(i->velocity == zero_vector);
			if (i->wait_time == 0.0) continue; // idle, don't update
			i->wait_time -= elapsed_secs;
			if (i->wait_time > 0.0)  continue; // still waiting
			// choose a new destination
			int new_dest_hp(-1);

			for (unsigned n = 0; n < 20; ++n) { // make 100 attempts to choose a new dest helipad
				unsigned hp_ix(rgen.rand() % helipads.size());
				if (hp_ix != i->dest_hp && helipads[hp_ix].is_avail()) {new_dest_hp = hp_ix; break;}
			}
			if (new_dest_hp < 0) {i->wait_time = 1.0; continue;} // wait 1s and try again later
			vector3d const model_sz(get_helicopter_size(i->model_id));
			float const hc_height(model_sz.z), hc_width(model_sz.y), hc_length(model_sz.x);
			float const min_vert_clearance(2.0f*hc_height), min_climb_height(max(min_vert_clearance, 5.0f*hc_height));
			float const min_hc_spacing(2.0f*sqrt(hc_height*hc_height + hc_width*hc_width));
			float const avoid_dist(2.0*SQRT2*max(hc_length, hc_width)); // increase radius factor for added clearance
			assert(i->dest_hp < helipads.size());
			helipad_t &helipad(helipads[new_dest_hp]);
			point p1(i->bcube.get_cube_center()), p2(helipad.bcube.get_cube_center());
			helipads[i->dest_hp].in_use = 0; // old dest
			helipad.reserved = 1;
			i->wait_time = 0.0; // no longer waiting
			i->dest_hp   = new_dest_hp;
			i->velocity  = vector3d(0.0, 0.0, takeoff_speed);
			p1.z = p2.z  = max(p1.z, p2.z) + min_climb_height;
			i->fly_zval  = max(p1.z, (get_flight_path_zmax(p1, p2, avoid_dist) + min_vert_clearance));
			i->state     = helicopter_t::STATE_TAKEOFF;
			i->invalidate_tile_shadow_map(xlate, 0); // update static shadows for this tile to remove the helicopter shadow; resting on roof, no need to compute shadow_offset

			// check if the flight path intersects another helicopter and increase fly_zval to avoid it
			for (auto j = helicopters.begin(); j != helicopters.end(); ++j) {
				if (i == j) continue; // skip self
				if (j->state == helicopter_t::STATE_WAIT) continue; // not flying
				if (fabs(i->fly_zval - j->fly_zval) > min_vert_clearance) continue; // zvals far apart
				assert(j->dest_hp < helipads.size());
				point const p1b(j->bcube.get_cube_center()), p2b(helipads[j->dest_hp].bcube.get_cube_center());
				if (line_line_dist(p1, p2, p1b, p2b) > min_hc_spacing) continue;
				cube_t bounds1(p1, p2), bounds2(p1b, p2b);
				bounds1.expand_by(min_hc_spacing);
				if (!bounds1.intersects(bounds2)) continue;
				i->fly_zval = j->fly_zval + min_vert_clearance; // increase fly zval
			} // for j
		} // end stopped case
		else { // moving
			assert(i->wait_time == 0.0); // must not be waiting
			assert(i->dest_hp < helipads.size()); // must have a valid dest helipad
			helipad_t &helipad(helipads[i->dest_hp]);
			assert(helipad.reserved); // sanity check

			if (i->state == helicopter_t::STATE_TAKEOFF) {
				vector3d dir((helipad.bcube.get_cube_center() - i->get_landing_pt()).get_norm()); // direction to new dest helipad
				dir.z = 0.0; // no tilt for now
				// vertical takeoff
				float const takeoff_dz(i->fly_zval - i->bcube.z1()), max_rise_dist(takeoff_speed*clamp_fticks), rise_dist(min(takeoff_dz, max_rise_dist));
				assert(takeoff_dz >= 0.0);
				i->bcube += vector3d(0.0, 0.0, rise_dist);

				if (rise_dist == takeoff_dz) { // reached the target height and can now fly horizontally
					i->dir      = dir; // set final dir
					i->velocity = speed * rgen.rand_uniform(0.9, 1.1) * i->dir; // move in dir with minor speed variation
					i->state    = helicopter_t::STATE_FLY;
				}
				else {
					i->dir = (rotate_rate*dir + (1.0 - rotate_rate)*i->dir).get_norm(); // gradually rotate to the correct direction
				}
			}
			else if (i->state == helicopter_t::STATE_LAND) {
				float const land_dz(i->bcube.z1() - helipad.bcube.z2()), max_fall_dist(land_speed*clamp_fticks), fall_dist(min(land_dz, max_fall_dist));
				assert(land_dz >= 0.0);
				// vertical landing, no need to re-orient dir
				i->bcube -= vector3d(0.0, 0.0, fall_dist);

				if (fall_dist == land_dz) { // landed
					i->velocity  = zero_vector; // full stop
					i->wait_time = rgen.rand_uniform(30, 60); // wait 30-60s to take off again
					i->state = helicopter_t::STATE_WAIT; // transition back to the waiting state
					helipad.in_use   = 1;
					helipad.reserved = 0;
					i->invalidate_tile_shadow_map(xlate, 0); // update static shadows for this tile to add the helicopter shadow; resting on roof, no need to compute shadow_offset
				}
			}
			else {
				assert(i->state == helicopter_t::STATE_FLY);
				point const cur_pos(i->get_landing_pt()), dest_pos(helipad.bcube.get_cube_center());
				cube_t dest(dest_pos);
				vector3d const delta_pos(clamp_fticks*i->velocity); // distance of travel this frame
				dest.expand_by_xy(delta_pos.mag());
			
				if (dest.contains_pt_xy(cur_pos)) { // reached destination
					vector3d xy_move((dest_pos.x - cur_pos.x), (dest_pos.y - cur_pos.y), 0.0);
					i->bcube   += xy_move; // move to destination XY (center of dest helipad)
					i->velocity = vector3d(0.0, 0.0, -land_speed);
					i->state    = helicopter_t::STATE_LAND;
				}
				else { // moving to destination
					i->bcube += delta_pos; // move by one timestep
				}
			}
			if (i->velocity != zero_vector) {
				i->blade_rot += 0.75*clamp_fticks; // rotate the blade; should this scale with velocity?
				if (i->blade_rot > TWO_PI) {i->blade_rot -= TWO_PI;} // keep rotation value small
			}
			// helicopter dynamic shadows look really neat, but significantly reduce framerate; enable with backslash key
			i->dynamic_shadow = 0;

			if (enable_hcopter_shadows) {
				point const center(i->bcube.get_cube_center());

				if (p2p_dist(center, camera_bs) < shadow_thresh) { // the player is nearby (optimization)
					// since the helicopter can be flying quite far above the terrain, the shadows can be cast far away;
					// we need to find the correct tile that the shadow lands on so that we can clear and update it;
					// also, the shadow should be drawn if the location it falls on is visible to the player;
					// here we check both the terrain and buildings for the shadow location using a ray cast, which is approximate;
					// this may not work if the shadow falls across multiple objects such as a tall building and the terrain below it
					float const dmax(4.0*shadow_thresh); // ~4 tile widths
					point start_pt(center + xlate), end_pt(start_pt + shadow_dir*dmax), p_int; // in camera space
					float dmin(dmax);
					if (line_intersect_tiled_mesh(start_pt, end_pt, p_int)) {min_eq(dmin, p2p_dist(start_pt, p_int)); end_pt = p_int;}
					if (line_intersect_city      (start_pt, end_pt, p_int)) {min_eq(dmin, p2p_dist(start_pt, p_int));}

					if (dmin < dmax) { // enable shadows if the line intersects either the terrain or buildings within dmax; otherwise, the shadow falls too far away
						vector3d const shadow_offset(shadow_dir*dmin + xlate);
						i->dynamic_shadow = camera_pdu.cube_visible(i->bcube + shadow_offset);
						if (i->dynamic_shadow) {i->invalidate_tile_shadow_map(shadow_offset, 1);} // invalidate shadow maps for this frame and the next one
					}
				}
			}
		} // end moving case
	} // for i
	// show flight path debug lines?
}

// Note: not yet used, but may be useful in checking for helicopter mid-air collisions in the future
bool car_manager_t::check_helicopter_coll(cube_t const &bc) const {
	for (auto i = helicopters.begin(); i != helicopters.end(); ++i) {
		if (i->bcube.intersects(bc)) return 1;
	}
	return 0;
}

void car_manager_t::draw(int trans_op_mask, vector3d const &xlate, bool use_dlights, bool shadow_only, bool is_dlight_shadows, bool garages_pass) {
	if (cars.empty()  && helicopters.empty()) return; // nothing to draw
	if ( garages_pass && first_garage_car == cars.size()) return; // no cars in garages
	if (!garages_pass && first_garage_car == 0 && helicopters.empty()) return; // only cars in garages

	if (trans_op_mask & 1) { // opaque pass, should be first
		if (is_dlight_shadows && !city_params.car_shadows) return;
		//timer_t timer(string("Draw Cars") + (garages_pass ? " Garages" : " City") + (shadow_only ? " Shadow" : "")); // 10K cars = 1.5ms / 2K cars = 0.33ms
		bool const only_parked(shadow_only && !is_dlight_shadows); // sun/moon shadows are precomputed and cached, so only include static objects such as parked cars
		setup_occluders();
		dstate.xlate = xlate;
		dstate.use_building_lights = garages_pass;
		fgPushMatrix();
		translate_to(xlate);
		dstate.pre_draw(xlate, use_dlights, shadow_only);
		if (!shadow_only) {dstate.s.add_uniform_float("hemi_lighting_normal_scale", 0.0);} // disable hemispherical lighting normal because the transforms make it incorrect
		float const draw_tile_dist(dstate.draw_tile_dist);

		for (auto cb = car_blocks.begin(); cb+1 < car_blocks.end(); ++cb) {
			if (cb->is_in_building() != garages_pass) continue; // wrong pass
			cube_t const block_bcube(get_cb_bcube(*cb) + xlate);
			if (!shadow_only && !block_bcube.closest_dist_less_than(camera_pdu.pos, 0.5*draw_tile_dist)) continue; // check draw distance, dist_scale=0.5
			if (!camera_pdu.cube_visible(block_bcube)) continue; // city not visible - skip
			unsigned const end((cb+1)->start);
			assert(end <= cars.size());

			for (unsigned c = cb->start; c != end; ++c) {
				car_t const &car(cars[c]);
				if (only_parked && !(car.is_parked() && !car.is_sleeping())) continue; // skip non-parked cars
				if (skip_car_draw(car)) continue;
				dstate.draw_car(car, is_dlight_shadows, garages_pass);
			}
		} // for cb
		if (!garages_pass && !is_dlight_shadows) {draw_helicopters(shadow_only);} // draw helicopters in the normal draw pass
		if (!shadow_only) {dstate.s.add_uniform_float("hemi_lighting_normal_scale", 1.0);} // restore
		dstate.post_draw();
		fgPopMatrix();
		static car_t const *sel_car(nullptr);

		if (!game_mode && !garages_pass && !shadow_only) {
			if (tt_fire_button_down) {sel_car = get_car_at_player(FAR_CLIP);} // no distance limit
			
			if (sel_car != nullptr && !sel_car->in_garage()) { // car found
				dstate.set_label_text(sel_car->label_str(), (sel_car->get_center() + xlate));
				shader_t s;
				s.begin_color_only_shader(YELLOW);
				ensure_outlined_polygons();
				draw_simple_cube(sel_car->bcube + xlate);

				if (!sel_car->is_parked() && sel_car->dest_valid) { // draw destination of moving car
					if (sel_car->cur_city != sel_car->dest_city) { // dest is in a different city
						s.set_cur_color(RED);
						cube_t city_bcube(get_city_bcube(sel_car->dest_city));
						city_bcube.z2() += 10.0*city_params.road_width; // increase height to make it more easily visible
						draw_simple_cube(city_bcube + xlate);
					}
					if (sel_car->dest_driveway >= 0) {
						s.set_cur_color(CYAN);
						cube_t dw_bcube(get_car_dest_bcube(*sel_car, 0)); // driveway
						dw_bcube.z2() += 4.0*city_params.road_width; // increase height to make it more easily visible
						draw_simple_cube(dw_bcube + xlate);
					}
					s.set_cur_color(PURPLE);
					cube_t isec_bcube(get_car_dest_bcube(*sel_car, 1)); // isec
					isec_bcube.z2() += 4.0*city_params.road_width; // increase height to make it more easily visible
					draw_simple_cube(isec_bcube + xlate);
				}
				set_fill_mode(); // reset
				s.end_shader();
			}
		}
		if (animate2) {sel_car = nullptr;} // only reset sel_car when physics is enabled so that debug display will stay enabled when stopping time
		dstate.show_label_text();

		if (city_action_key && !garages_pass && !shadow_only) {
			car_t const *const car(get_car_at_player(8.0*CAMERA_RADIUS));
			if (car != nullptr) {print_text_onscreen(car->label_str(), YELLOW, 1.0, 1.5*TICKS_PER_SECOND, 0);}
		}
	}
	if ((trans_op_mask & 2) && !shadow_only) {dstate.draw_and_clear_light_flares();} // transparent pass; must be done last for alpha blending, and no translate
}

void car_manager_t::draw_helicopters(bool shadow_only) {
	for (auto i = helicopters.begin(); i != helicopters.end(); ++i) {dstate.draw_helicopter(*i, shadow_only);}
}

bool car_can_fit(cube_t const &c) {
	vector3d const car_sz(get_nom_car_size());
	return (max(c.dx(), c.dy()) > 1.2f*car_sz.x && min(c.dx(), c.dy()) > 1.2f*car_sz.y && c.dz() > 1.2f*car_sz.z);
}

