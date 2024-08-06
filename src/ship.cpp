// 3D World - Ship models for universe mode
// by Frank Gennari
// 8/25/05

#include "ship.h"
#include "ship_util.h"
#include "explosion.h"
#include "obj_sort.h"
#include "timetest.h"
#include "shaders.h"
#include "draw_utils.h"
#include "gl_ext_arb.h"


bool const TIMETEST          = (GLOBAL_TIMETEST || 0);
unsigned const NUM_TIMESTEPS = 4;
unsigned const NUM_EXTRA_DAM = 4;


bool player_autopilot(0), player_auto_stop(0), hold_fighters(0), dock_fighters(0), ship_cube_map_reflection(0);
int onscreen_display(0);
unsigned univ_reflection_tid(0);
unsigned alloced_fobjs[3] = {0}; // testing
float uobj_rmax(0.0), urm_ship(0.0), urm_static(0.0), urm_proj(0.0);
point player_death_pos(all_zeros), universe_origin(all_zeros);
vector<free_obj *> uobjs; // ships, projectiles, etc.
vector<cached_obj> coll_objs; // only collision objects
vector<cached_obj> ships[NUM_ALIGNMENT], all_ships; // ships only - do we want vectors of u_ship*?
vector<cached_obj> stat_objs; // static objects, for intersection tests
vector<cached_obj> coll_proj; // collision enabled projectiles, for point defense code
vector<cached_obj> decoys;    // decoy projectiles, for projectile seeking
vector<cached_obj> c_uobjs_lit; // lit objects, used for lighting
vector<ship_explosion> exploding;
vector<free_obj const *> a_targets(NUM_ALIGNMENT, NULL), attackers(NUM_ALIGNMENT, NULL);
vector<cached_obj> c_uobjs;
usw_ray_group trail_rays, beam_rays; // engine trails, beams
vector<temp_source> temp_sources;
vector<hyper_inhibit_t> hyper_inhibits;
pt_line_drawer particle_pld;
point_sprite_drawer_sized glow_psd, smoke_psd;
shader_t emissive_shader;

float weap_damage[NUM_UWEAP+NUM_EXTRA_DAM] = {0};
float ship_damage_done[NUM_US_CLASS]  = {0};
float ship_damage_taken[NUM_US_CLASS] = {0};
float align_s_damage[NUM_ALIGNMENT]   = {0};
float align_t_damage[NUM_ALIGNMENT]   = {0};
float friendly_fire[NUM_ALIGNMENT]    = {0};
unsigned weap_kills[NUM_UWEAP+NUM_EXTRA_DAM] = {0};
unsigned ship_kills[NUM_US_CLASS]     = {0};
unsigned ship_deaths[NUM_US_CLASS]    = {0};
unsigned team_credits[NUM_ALIGNMENT]  = {0};
unsigned init_credits[NUM_ALIGNMENT]  = {0};
unsigned ind_ships_used[NUM_ALIGNMENT]= {0};
unsigned align_s_kills[NUM_ALIGNMENT] = {0};
unsigned align_t_kills[NUM_ALIGNMENT] = {0};
unsigned friendly_kills[NUM_ALIGNMENT]= {0};


extern bool allow_shader_invariants;
extern int show_framerate, frame_counter, display_mode, animate2, do_run, show_scores;
extern float fticks, player_sensor_dist_mult;
extern double tfticks;
extern unsigned owner_counts[];
extern float resource_counts[];
extern exp_type_params et_params[];
extern vector<us_class> sclasses;
extern vector<us_weapon> us_weapons;


void collision_detect_objects(vector<cached_obj> &objs0, unsigned t);
void draw_and_update_engine_trails(line_tquad_draw_t &drawer);
void add_nearby_uobj_text(text_drawer_t &text_drawer);
void print_univ_owner_stats();


// ************ STATISTICS GATHERING ************


void print_n_spaces(int n) { // there must be a better way to do this

	for (int j = 0; j < n; ++j) cout << " ";
}


void register_attack_from(free_obj const *attacker, unsigned target_align) {

	assert(attacker != NULL && target_align < a_targets.size());
	if (a_targets[target_align] == NULL) a_targets[target_align] = attacker; // team takes offense
}


void register_damage(int s_sclass, int t_sclass, int wclass, float damage, unsigned s_align, unsigned t_align, bool is_kill, bool is_self) {

	if (s_sclass != SWCLASS_UNDEF && !is_self) { // register damage done/kills (no suicides)
		assert(s_sclass != SCLASS_NONSHIP);
		assert(s_sclass >= 0 && (unsigned)s_sclass < NUM_US_CLASS);
		ship_damage_done[(unsigned)s_sclass] += damage;
		if (is_kill) ++ship_kills[(unsigned)s_sclass];
	}
	if (t_sclass != SWCLASS_UNDEF) { // register damage taken/deaths
		assert(t_sclass != SCLASS_NONSHIP);
		assert(t_sclass >= 0 && (unsigned)t_sclass < NUM_US_CLASS);
		ship_damage_taken[(unsigned)t_sclass] += damage;
		if (is_kill) ++ship_deaths[(unsigned)t_sclass];
	}
	if (wclass != SWCLASS_UNDEF) { // register weapon stats
		unsigned ix;
		switch (wclass) {
		case WCLASS_COLLISION: ix = NUM_UWEAP+0; break;
		case WCLASS_HEAT:      ix = NUM_UWEAP+1; break;
		case WCLASS_EXPLODE:   ix = NUM_UWEAP+2; break;
		case WCLASS_CAPTURE:   ix = NUM_UWEAP+3; break;
		default:
			assert(wclass >= 0 && (unsigned)wclass < NUM_UWEAP);
			ix = (unsigned)wclass;
		}
		weap_damage[ix] += damage;
		if (is_kill) ++weap_kills[ix];
	}
	assert(s_align <= NUM_ALIGNMENT && t_align <= NUM_ALIGNMENT);
	bool const is_friendly_fire(s_align != NUM_ALIGNMENT && s_align == t_align && !is_self);
	if (s_align != NUM_ALIGNMENT && !is_self) {align_s_damage[s_align] += damage;}
	if (t_align != NUM_ALIGNMENT) {align_t_damage[t_align] += damage;}
	if (is_friendly_fire) {friendly_fire[s_align] += damage;}

	if (is_kill) {
		if (s_align != NUM_ALIGNMENT && !is_self) {++align_s_kills[s_align];}
		if (t_align != NUM_ALIGNMENT) {++align_t_kills[t_align];}
		if (is_friendly_fire) {++friendly_kills[s_align];}
	}
}


unsigned get_num_chars(unsigned v) {
	unsigned n(1);
	while (v >= 10) {++n; v /= 10;}
	return n;
}
void write_uint_pad(unsigned v, unsigned min_chars, string const &sep) {
	assert(min_chars <= 12);
	cout << v;
	for (unsigned n = get_num_chars(v); n < min_chars; ++n) {cout.put(' ');} // pad with spaces
	cout << sep;
}
void update_maxvals(unsigned *v, unsigned n) {
	for (unsigned i = 0; i < n; ++i) {v[i] = get_num_chars(v[i]);}
}

void show_stats() {

	int const cwidth(18);
	cout << endl << "Weapons:       <Damage>     <Kills>" << endl;
	
	for (unsigned i = 0; i < NUM_UWEAP; ++i) {
		if (us_weapons[i].damage == 0.0 || weap_damage[i] == 0.0) continue;
		cout << us_weapons[i].name;
		print_n_spaces(cwidth - (int)us_weapons[i].name.size());
		cout << ": " << (int)weap_damage[i] << "\t" << weap_kills[i] << endl;
	}
	string const damage_names[NUM_EXTRA_DAM] = {"Collision", "Heat", "Explosion", "Capture"};

	for (unsigned i = 0; i < NUM_EXTRA_DAM; ++i) {
		if (weap_damage[NUM_UWEAP+i] == 0.0) continue;
		cout << damage_names[i];
		print_n_spaces(cwidth - (int)damage_names[i].size());
		cout << ": " << (int)weap_damage[NUM_UWEAP+i] << "\t" << weap_kills[NUM_UWEAP+i] << endl;
	}
	unsigned num_ships[NUM_US_CLASS] = {0}, num_ships_align[NUM_ALIGNMENT][NUM_US_CLASS] = {};
	float of[NUM_ALIGNMENT] = {0}, de[NUM_ALIGNMENT] = {0}, val[NUM_ALIGNMENT] = {0};
	unsigned cost[NUM_ALIGNMENT] = {0};

	for (unsigned i = 0; i < all_ships.size(); ++i) {
		if (all_ships[i].flags & OBJ_FLAGS_DECY) continue; // decoy flare?
		free_obj const *const obj(all_ships[i].obj);
		assert(obj != nullptr);
		int const sclass(obj->get_src_sclass());
		assert(sclass != SWCLASS_UNDEF);
		unsigned const sc((unsigned)sclass), align(obj->get_align());
		assert(align < NUM_ALIGNMENT);
		assert(sc < NUM_US_CLASS);
		++num_ships[sc];

		if (!obj->is_fighter()) {
			/*if (TEAM_ALIGNED(align))*/ ++num_ships_align[align][sc];
		}
		float const off(TICKS_PER_SECOND*obj->offense()), def(obj->defense());
		of[align]   += off;
		de[align]   += def;
		val[align]  += 0.001*off*def;
		cost[align] += obj->get_cost();
	}
	unsigned maxvals[4] = {1000000, 1000000, 1000000, 1000000}; // starting values set the min column width

	for (unsigned i = 0; i < NUM_US_CLASS; ++i) {
		max_eq(maxvals[0], (unsigned)ship_damage_done [i]);
		max_eq(maxvals[1], (unsigned)ship_damage_taken[i]);
		max_eq(maxvals[2], ship_kills [i]);
		max_eq(maxvals[3], ship_deaths[i]);
	}
	update_maxvals(maxvals, 4);
	cout << endl << "Ships:             <DDone> <DTake> <Kills> <Death> <R> <B> <O> <P> <Num>" << endl;
	
	for (unsigned i = 0; i < NUM_US_CLASS; ++i) {
		if (ship_damage_done[i] == 0.0 && ship_damage_taken[i] == 0.0 && num_ships[i] == 0) continue;
		cout << sclasses[i].name;
		print_n_spaces(cwidth - (int)sclasses[i].name.size());
		cout << ": ";
		write_uint_pad(ship_damage_done [i], maxvals[0]);
		write_uint_pad(ship_damage_taken[i], maxvals[1]);
		write_uint_pad(ship_kills       [i], maxvals[2]);
		write_uint_pad(ship_deaths      [i], maxvals[3]);

		for (unsigned j = 0; j < NUM_ALIGNMENT; ++j) {
			if (TEAM_ALIGNED(j) && j != ALIGN_PIRATE && j != ALIGN_PLAYER) {write_uint_pad(num_ships_align[j][i], 3);}
		}
		cout << num_ships[i] << endl;
	}
	unsigned maxvals2[13] = {1000000, 1000000, 10000000, 1000000, 1000000, 1000000, 1000000,  100000000, 100000000, 10000000, 1000000, 100000000, 1000};

	for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {
		max_eq(maxvals2[0 ], (unsigned)align_s_damage[i]);
		max_eq(maxvals2[1 ], (unsigned)align_t_damage[i]);
		max_eq(maxvals2[2 ], (unsigned)friendly_fire [i]);
		max_eq(maxvals2[3 ], align_s_kills [i]);
		max_eq(maxvals2[4 ], align_t_kills [i]);
		max_eq(maxvals2[5 ], friendly_kills[i]);
		max_eq(maxvals2[6 ], owner_counts  [i]);
		max_eq(maxvals2[7 ], (unsigned)of[i]);
		max_eq(maxvals2[8 ], (unsigned)de[i]);
		max_eq(maxvals2[9 ], (unsigned)val[i]);
		max_eq(maxvals2[10], cost[i]);
		max_eq(maxvals2[11], team_credits[i]);
		max_eq(maxvals2[12], (unsigned)resource_counts[i]);
	}
	update_maxvals(maxvals2, 13);
	cout << endl << "Ship aligns:       <DDone> <DTake> <Friend> <Kills> <Death> <TKill> <Owned>" << endl;

	// owned (planets and moons) does not include those read from a modmap file that already have owners
	for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {
		cout << align_names[i];
		print_n_spaces(cwidth - (int)align_names[i].size());
		cout << ": ";
		write_uint_pad(align_s_damage[i], maxvals2[0]);
		write_uint_pad(align_t_damage[i], maxvals2[1]);
		write_uint_pad(friendly_fire [i], maxvals2[2]);
		write_uint_pad(align_s_kills [i], maxvals2[3]);
		write_uint_pad(align_t_kills [i], maxvals2[4]);
		write_uint_pad(friendly_kills[i], maxvals2[5]);
		write_uint_pad(owner_counts  [i], maxvals2[6]);
		cout << endl;
	}
	cout << endl << "Total Ship Ratings: <Offense> <Defense> <Value>  <Cost>  <Credits> <Resources>" << endl;

	for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {
		cout << align_names[i];
		print_n_spaces(cwidth - (int)align_names[i].size());
		cout << ":  ";
		write_uint_pad(of  [i], maxvals2[7 ]);
		write_uint_pad(de  [i], maxvals2[8 ]);
		write_uint_pad(val [i], maxvals2[9 ]);
		write_uint_pad(cost[i], maxvals2[10]);
		write_uint_pad(team_credits   [i], maxvals2[11]);
		write_uint_pad(resource_counts[i], maxvals2[12]);
		cout << endl;
	}
	print_univ_owner_stats();
	cout << "Alloced: Ships: " << alloced_fobjs[0] << " - " << alloced_fobjs[1] << " = " <<
		(alloced_fobjs[0] - alloced_fobjs[1]) << ", Proj+Part: " << alloced_fobjs[2] << " (x100)" << endl;
}


class team_status_tracker_t {
	struct team_entry_t {
		bool enabled, started, end_game, lost;
		team_entry_t() : enabled(0), started(0), end_game(0), lost(0) {}
		void init(bool enabled_) {enabled = enabled_; started = end_game = lost = 0;}
	};
	team_entry_t team_entries[NUM_ALIGNMENT];
	bool inited, game_over;

public:
	team_status_tracker_t() : inited(0), game_over(0) {}
	void init() { // auto inited on first status check, but can be manually inited for multiple games
		bool const teams_valid[NUM_ALIGNMENT] = {0, 1, 0, 0, 1, 1, 1, 1};
		for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {team_entries[i].init(teams_valid[i] && team_credits[i] > 0);}
		inited    = 1;
		game_over = 0;
	}
	void check_team_status() {
		if (game_over) return;
		//timer_t timer("Team Stats");
		if (!inited) {init();}
		// Note: we can determine ownership status either by looking at planets+moons or orbiting ships; orbiting ships should be simpler and easier
		unsigned ntships[NUM_ALIGNMENT] = {0}; // total ships
		unsigned noships[NUM_ALIGNMENT] = {0}; // orbiting ships
		unsigned num_left(0), team_left(0), team_with_most_owned(0);

		for (unsigned i = 0; i < all_ships.size(); ++i) {
			if (all_ships[i].flags & OBJ_FLAGS_DECY) continue; // decoy flare?
			assert(all_ships[i].obj != nullptr);
			unsigned const align(all_ships[i].obj->get_align());
			assert(align < NUM_ALIGNMENT);
			++ntships[align];
			if (all_ships[i].flags & OBJ_FLAGS_ORBT) {++noships[align];}
		}
		for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {
			if (noships[i] > noships[team_with_most_owned]) {team_with_most_owned = i;}
		}
		for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {
			team_entry_t &e(team_entries[i]);
			//cout << "Team " << align_names[i] << " enabled " << e.enabled << " started " << e.started << " lost " << e.lost << " ts " << ntships[i] << " os " << noships[i] << endl; // TESTING
			if (!e.enabled || e.lost) continue; // disabled or already lost
			
			if (i == team_with_most_owned && have_excess_credits(i) && !e.end_game) {
				e.end_game = 1;
				cout << "*** " << align_names[i] << " End Game Strategy" << endl;
			}
			if (ntships[i] == 0 || (e.started && noships[i] == 0)) { // so ships, or started and no orbiting ships (planets/moons claimed)
				cout << "*** " << align_names[i] << " Team Lost (" << (ntships[i] ? "No Colonies" : "No Ships") << ")" << endl;
				e.lost = 1;
				continue;
			}
			if (!e.started && noships[i] > 0) { // first orbiting ship (planet/moon claimed)
				cout << "*** " << align_names[i] << " Team Started" << endl;
				e.started = 1;
			}
			++num_left;
			team_left = i;
		} // for i
		if (num_left == 1) {
			if (frame_counter > 0) {cout << "*** " << align_names[team_left] << " Team Won" << endl;} // don't print anything on the first frame for trivial win conditions
			game_over = 1;
		}
		else if (num_left == 0) {
			if (frame_counter > 0) {cout << "*** " << "No Team Won" << endl;}
			game_over = 1;
		}
	}
};

team_status_tracker_t team_status_tracker;


// ************ PLAYER INTERFACE, ETC. ************


upos_point_type const &get_player_pos()   {return player_ship().get_pos();}
vector3d const &get_player_dir()          {return player_ship().get_dir();}
vector3d const &get_player_up()           {return player_ship().get_up();}
vector3d const &get_player_velocity()     {return player_ship().get_velocity();}
free_obj const *get_player_target()       {return player_ship().get_target();}
float get_player_radius()                 {return player_ship().get_radius();}
void set_player_pos(point const &pos_)    {player_ship().set_pos(pos_);}
void set_player_dir(vector3d const &dir_) {player_ship().set_dir(dir_);}
void set_player_up(vector3d const &upv_)  {player_ship().set_upv(upv_);}
void stop_player_ship()                   {player_ship().set_vel(zero_vector);}
void set_player_target(free_obj const *target) {player_ship().set_target(target);}


void change_speed_mode(int val) {

	if (world_mode != WMODE_UNIVERSE) return;
	if (!player_ship().specs().has_fast_speed)        val = 0;
	if (!player_ship().specs().has_hyper && val == 2) val = 3;
	player_ship().set_max_sf((val == 0) ? SLOW_SPEED_FACTOR : FAST_SPEED_FACTOR);
	assert(val >= 0 && val <= 3);
	char const *const strs[4] = {"Low Speed", "High Speed", "Hyperspeed", "High Speed"};
	print_text_onscreen(strs[val], GREEN, 1.0, ((3*TICKS_PER_SECOND)/2));
}

void toggle_player_ship_stop() {
	player_auto_stop = !player_auto_stop;
}

void change_fire_primary() {
	static bool fire_primary(0);
	fire_primary = !fire_primary;
	string const msg(string("Fire All Primary Weapons: ") + (fire_primary ? "ON" : "OFF"));
	print_text_onscreen(msg, GREEN, 1.0, int(1.2*TICKS_PER_SECOND));
	player_ship().set_fire_primary(fire_primary);
}

void reset_player_target() {
	player_ship().reset_target();
	print_text_onscreen("Target Reset", PURPLE, 0.8, TICKS_PER_SECOND);
}

void toggle_dock_fighters() {
	dock_fighters = !dock_fighters;
	hold_fighters = 0;
	print_text_onscreen((dock_fighters ? "Dock Fighters: On" : "Dock Fighters: Off"), PURPLE, 0.8, TICKS_PER_SECOND);
}

void toggle_hold_fighters() {
	hold_fighters = !hold_fighters;
	dock_fighters = 0;
	print_text_onscreen((hold_fighters ? "Hold Fighters: On" : "Hold Fighters: Off"), PURPLE, 0.8, TICKS_PER_SECOND);
}

void toggle_autopilot() {
	player_autopilot = !player_autopilot;
}

void auto_target_player_closest_enemy() {

	free_obj const *target(nullptr);
	u_ship const *const player(&player_ship());
	point const player_pos(player->get_pos());
	float dmin_sq(0.0);
	
	for (unsigned i = 0; i < all_ships.size(); ++i) {
		free_obj const *const obj(all_ships[i].obj);
		assert(obj != nullptr);
		if (!obj->is_hostile_to(player)) continue;
		float const dist_sq(p2p_dist_sq(obj->get_pos(), player_pos));
		if (dmin_sq == 0.0 || dist_sq < dmin_sq) {dmin_sq = dist_sq; target = obj;}
	}
	if (target == nullptr) return; // no targets found
	print_text_onscreen((string("Target Acquired: ") + target->get_name()), PURPLE, 0.5, TICKS_PER_SECOND, 1);
	set_player_target(target);
}


// ************ OBJECT PROCESSING ************


// coll_test: 0 = none, 1 = test only but still add, 2 = test and add only if no coll
bool add_uobj(free_obj *obj, int coll_test) {

	assert(obj);
	bool coll(0);

	if (coll_test) {
		unsigned const coll_ix(check_for_obj_coll(obj->get_pos(), obj->get_c_radius()));
		coll = (coll_ix > 0);
	}
	if (!coll || coll_test != 2) uobjs.push_back(obj);
	return coll;
}


void add_uobj_ship(u_ship *ship) {

	assert(ship);
	++alloced_fobjs[0];
	bool const coll(add_uobj(ship, 0));
	assert(!coll);
}


// spawn_mode: 0 = normal, 1 = randomly spawned once, 2 = randomly spawned and will respawn
u_ship *create_ship(unsigned sclass, point const &pos0, unsigned align, unsigned ai_type, unsigned target_mode, bool rand_orient, int spawn_mode) {

	u_ship *ship(NULL);

	if (spawn_mode > 0) {
		assert(!sclasses[sclass].dynamic_cobjs); // currently abomination and reaper
		ship = new rand_spawn_ship(sclass, pos0, align, ai_type, target_mode, rand_orient, (spawn_mode > 1));
	}
	else if (sclasses[sclass].dynamic_cobjs) {
		ship = new multipart_ship(sclass, pos0, align, ai_type, target_mode, rand_orient);
	}
	else {
		ship = new u_ship(sclass, pos0, align, ai_type, target_mode, rand_orient);
	}
	add_uobj_ship(ship);
	return ship;
}


void get_cached_objs(vector<free_obj *> const &objs, vector<cached_obj> &cobjs) {

	size_t const size(objs.size());
	cobjs.resize(size);
	for (size_t i = 0; i < size; ++i) {cobjs[i].set_obj(objs[i]);}
}

void remove_bad_cobjs_and_particles(vector<cached_obj> &objs) {

	auto i(objs.begin()), o(i);
	bool had_removed(0);

	for (; i != objs.end(); ++i) { // rebuild the object set without the particles
		if (i->flags & (OBJ_FLAGS_BAD_ | OBJ_FLAGS_PART)) {had_removed = 1; continue;}
		if (had_removed) {*o = *i;}
		++o;
	}
	objs.erase(o, objs.end());
}


void apply_univ_physics() {

	if (show_framerate) show_stats();
	unsigned nsh(0), npr(0), npa(0); // testing
	RESET_TIME;
	if (animate2) {trail_rays.clear(); beam_rays.clear();}
	player_ship().fix_upv();
	purge_old_objs();
	if (TIMETEST) PRINT_TIME("  Purge");
	get_cached_objs(uobjs, c_uobjs);
	if (TIMETEST) PRINT_TIME("  Get Cached");
	unsigned const nobjs((unsigned)c_uobjs.size());
	assert(uobjs.size() == nobjs);
	all_ships.resize(0);
	stat_objs.resize(0);
	coll_proj.resize(0);
	coll_objs.resize(0);
	decoys.resize(0);
	exploding.resize(0);
	temp_sources.resize(0);
	hyper_inhibits.resize(0);
	uobj_rmax = urm_ship = urm_static = urm_proj = 0.0;
	
	for (unsigned i = 0; i < NUM_ALIGNMENT; ++i) {
		ships[i].resize(0);
		ind_ships_used[i] = 0;
		if ((unsigned(tfticks)&7) == 0) {team_credits[i] += unsigned(fticks*resource_counts[i]);} // every 8 frames
	}
	for (unsigned i = 0; i < nobjs; ++i) { // must be after purge_old_objs() while pointers are all valid
		unsigned const flags(c_uobjs[i].flags);
		if (flags & OBJ_FLAGS_BAD_) continue;
		float const radius(c_uobjs[i].radius);

		if (flags & OBJ_FLAGS_SHIP) { // ship
			free_obj const *const obj(c_uobjs[i].obj);
			unsigned const alignment(obj->get_align());
			urm_ship = max(urm_ship, radius);
			ships[alignment].push_back(c_uobjs[i]);
			all_ships.push_back(c_uobjs[i]);
			if (obj->is_exploding()) {exploding.push_back(obj->get_explosion());}
			if (obj->is_ship() && !obj->is_fighter() && !obj->is_orbiting()) {++ind_ships_used[alignment];}
		}
		if (flags & OBJ_FLAGS_DECY) { // decoy
			decoys.push_back(c_uobjs[i]);
		}
		if (flags & OBJ_FLAGS_STAT) { // static object
			urm_static = max(urm_static, radius);
			stat_objs.push_back(c_uobjs[i]);
		}
		if (!(flags & OBJ_FLAGS_NCOL)) { // has collisions
			coll_objs.push_back(c_uobjs[i]);

			if (flags & OBJ_FLAGS_PROJ) { // projectile
				urm_proj = max(urm_proj, radius);
				coll_proj.push_back(c_uobjs[i]);
			}
		}
		if (TIMETEST) {
			if (flags & OBJ_FLAGS_PROJ) ++npr;
			if (flags & OBJ_FLAGS_SHIP) ++nsh;
			if (flags & OBJ_FLAGS_PART) ++npa;
		}
		if (!(flags & OBJ_FLAGS_PARC)) {uobj_rmax = max(uobj_rmax, radius);}
	}
	//if (TIMETEST) cout << "  nobj: " << nobjs << " ship: " << nsh << " proj: " << npr << " part: " << npa << endl;
	if (TIMETEST) PRINT_TIME("  Rmax + Ship Vector Creation");

	if (animate2) {
		// before or after advance time and collision detection?
		for (unsigned i = 0; i < nobjs; ++i) { // can create new objects here
			if (c_uobjs[i].flags & (OBJ_FLAGS_SHIP | OBJ_FLAGS_PROJ)) {c_uobjs[i].obj->ai_action();}
		}
		if (player_autopilot) {update_cpos();}
		if (TIMETEST) PRINT_TIME("  AI Action");

		// c_uobjs is invalid at this point
		// don't update nobjs - delay first physics event for new objects until next frame
		for (unsigned i = 0; i < nobjs; ++i) {uobjs[i]->apply_physics();}
		if (TIMETEST) PRINT_TIME("  Apply Physics");
		float const timestep(fticks/NUM_TIMESTEPS);

		for (unsigned t = 0; t < NUM_TIMESTEPS; ++t) { // here is where the objects move
			collision_detect_objects(coll_objs, t);
			if (t == 0) {remove_bad_cobjs_and_particles(coll_objs);}

			for (unsigned i = 0; i < nobjs; ++i) {
				if (!uobjs[i]->is_ok()) {
					// nothing
				}
				else if (uobjs[i]->get_flags() & (OBJ_FLAGS_DIST | OBJ_FLAGS_ORBT)) {
					if (t == 0) {uobjs[i]->advance_time(fticks);}
				}
				else {uobjs[i]->advance_time(timestep);}
			}
		}
		if (TIMETEST) PRINT_TIME("  Advance + Collision");
	}
	else {
		player_ship().apply_physics();
		player_ship().advance_time(fticks);
		player_ship().ai_action();
	}
	team_status_tracker.check_team_status();
}


void sort_uobjects() { // originally part of apply_univ_physics()

	get_cached_objs(uobjs, c_uobjs); // re-validate since new objects may have been added and old ones may have moved
	sort(c_uobjs.begin(), c_uobjs.end(), comp_co_fast_x()); // re-sort
	unsigned const ncuo((unsigned)c_uobjs.size());

	// update uobjs to have the same sort order
	for (unsigned i = 0; i < ncuo; ++i) {uobjs[i] = c_uobjs[i].obj;} // what about objects with time == 0? exclude them?
}


bool proc_coll(free_obj *o1, free_obj *o2) {

	assert(o1 != NULL && o2 != NULL);

	if (o1->is_proj() && o2->is_proj()) { // projectile-projectile collision
		if (o1->no_proj_coll() || o2->no_proj_coll())                return 0; // no p-p coll for this type
		if (o1->get_src() != NULL && o1->get_src() == o2->get_src()) return 0; // ship's projectiles don't collide with each other
	}
	if (o1->is_stationary() && o2->is_stationary()) return 0; // two stationary objects - if they collide we can't do anything
	if (!o1->obj_int_obj(o2)) return 0;
	point const p1(o1->get_pos()), p2(o2->get_pos()); // cache these in case they change
	vector3d const v1(o1->get_tot_vel_at(p2)), v2(o2->get_tot_vel_at(p1)); // cache these in case they change
	float const elasticity(o1->get_elasticity()*o2->get_elasticity());
	o1->collision(p2, v2, o2->get_mass(), o2->get_c_radius(), o2, elasticity);
	point const new_p1(o1->get_pos());
	o1->set_pos(p1); // reset to orig pos so that o2 collision uses the correct pos
	o2->collision(p1, v1, o1->get_mass(), o1->get_c_radius(), o1, elasticity);
	o1->set_pos(new_p1);
	return 1;
}


void collision_detect_objects(vector<cached_obj> &objs, unsigned t) {

	//RESET_TIME;
	unsigned const size((unsigned)objs.size());
	static vector<interval> intervals;
	intervals.clear();
	intervals.reserve(2*size);

	for (unsigned i = 0; i < size; ++i) {
		if (objs[i].flags & OBJ_FLAGS_BAD_) continue;

		if (t > 0 && (objs[i].flags & (OBJ_FLAGS_DIST | OBJ_FLAGS_ORBT))) {
			if (t == 1) {objs[i].refresh();}
			continue;
		}
		if (t > 0) {objs[i].refresh();} // physics advance was run since last refresh
		double const radius(objs[i].radius), val(objs[i].pos.x);
		float const left(float(val - radius)), right(float(val + radius));
		assert(radius > 0.0);
		if (left == right) continue; // floating point precision limitation or bug?
		assert(left < right);
		intervals.push_back(interval(left,  i, 1));
		intervals.push_back(interval(right, i, 0));
	}
	unsigned const size2((unsigned)intervals.size());
	static vector<unsigned> locs, work;
	locs.resize(size);
	work.clear();
	sort(intervals.begin(), intervals.end());

	for (unsigned i = 0; i < size2; ++i) {
		unsigned const ix(intervals[i].ix & ~LEFT_EDGE_BIT), ix_flags(objs[ix].flags);
		unsigned bad_flags(OBJ_FLAGS_BAD_);
		if ( ix_flags & OBJ_FLAGS_PART) {bad_flags |= OBJ_FLAGS_PART;} // skip particle-particle collisions
		if ( ix_flags & OBJ_FLAGS_NOC2) {bad_flags |= OBJ_FLAGS_NOC2;} // both objects have their C2 flags set, skip the collision
		if ((ix_flags & OBJ_FLAGS_PROJ) && (ix_flags & OBJ_FLAGS_NOPC)) {bad_flags |= OBJ_FLAGS_PROJ;} // no projectile-projectile collision
		
		if (intervals[i].ix & LEFT_EDGE_BIT) { // start a new sphere
			unsigned const wsize((unsigned)work.size());

			if (wsize > 0) {
				point const pos_i(objs[ix].pos);
				float const c_radius_i(objs[ix].radius), pisd(pos_i.y);

				for (unsigned k = 0; k < wsize; ++k) {
					cached_obj &obj(objs[work[k]]);
					if (obj.flags & bad_flags) continue;
					float const radius(c_radius_i + obj.radius);
					if (fabs(pisd - obj.pos.y) > radius || !dist_less_than(pos_i, obj.pos, radius)) continue; // no intersection

					if (proc_coll(objs[ix].obj, obj.obj)) {
						objs[ix].refresh(); // ???
						obj.refresh(); // ???
					}
				}
			}
			locs[ix] = wsize;
			work.push_back(ix);
		}
		else { // end a current sphere
			assert(!work.empty());
			unsigned const lix(locs[ix]);
			//assert(ix < size && lix < work.size() && work[lix] == ix && locs[work.back()] == work.size()-1);
			swap(work[lix], work.back());
			locs[work[lix]] = lix;
			work.pop_back();
		}
	}
	assert(work.empty());
	//PRINT_TIME("Collision");
}


uparticle *gen_particle(unsigned type, colorRGBA const &c1, colorRGBA const &c2, unsigned lt, point const &pos,
						vector3d const &vel, float size, float damage, unsigned align, bool coll, int texture_id)
{
	static free_obj_allocator<uparticle> allocator;
	uparticle *part = allocator.alloc(type);
	part->set_params(type, pos, vel, signed_rand_vector_norm(), size, c1, c2, lt, damage, align, coll, texture_id);
	if (type == PTYPE_GLOW) {part->add_flag(OBJ_FLAGS_NOLT);} // no lights on a glow particle
	uobjs.push_back(part);
	return part;
}


void add_parts_projs(point const &pos, float radius, vector3d const &dir, colorRGBA const &color, int type, int align,free_obj const *const parent) {

	assert(type < NUM_ETYPES);

	switch (type) {
		case ETYPE_FUSION:
		case ETYPE_FUSION_ROT:
		case ETYPE_ESTEAL:
		case ETYPE_SIEGE:
			{
				if (rand()&1) break;
				bool const es(type == ETYPE_ESTEAL), seige(type == ETYPE_SIEGE);
				int const num(int(rand_uniform(0.0, (seige ? 8.0 : 2.0))));
				exp_type_params const &expt(et_params[type]);

				for (int i = 0; i < num; ++i) {
					vector3d const dpos(signed_rand_vector(1.0*radius));
					vector3d const vel(dpos.get_norm()*(0.0001*(seige ? 2.4 : 1.0)*rand_uniform(0.4, 1.0)));
					gen_particle(PTYPE_GLOW, (es ? expt.c1 : color), (es ? expt.c2 : color), (es ? 6.0 : 12.0)*expt.duration,
						(pos + dpos), vel, (seige ? 0.3 : 1.0)*radius*rand_uniform(0.1, 0.5), 0.0, align, 0);
				}
			}
			break;

		case ETYPE_EBURST:
			{
				int const num(min(40, int(fticks*((rand()&7) + 12))));

				for (int i = 0; i < num; ++i) {
					unsigned const type((rand() & 1) ? (unsigned)UWEAP_ATOMIC : (unsigned)UWEAP_ENERGY);
					vector3d const vel((signed_rand_vector(rand_uniform(0.5, 1.0)) + dir)*0.0006);
					vector3d const v(vel.get_norm());
					vector3d const dir(v), upv(signed_rand_vector_norm());
					us_projectile *proj(create_projectile(type, parent, align, (pos + v*radius), vel, dir, upv));
					proj->set_time(unsigned(0.5*proj->specs().lifetime)); // half the lifetime
				}
			}
			break;
	}
}


us_projectile *create_projectile(unsigned type, free_obj const *const parent, unsigned align, point const &pos,
								 vector3d const &vel, vector3d const &dir, vector3d const &upv)
{
	static free_obj_allocator<us_projectile> allocator;
	us_projectile *proj(allocator.alloc(type));
	proj->set_parent(parent);
	proj->set_align(align);
	proj->set_pos(pos);
	proj->set_vel(vel);
	proj->set_dir(dir);
	proj->set_upv(upv);
	add_uobj(proj);
	return proj;
}


// ************************** SHIFT AND DRAW ****************************


void add_colored_lights(point const &pos, float radius, colorRGBA const &color, float time, unsigned num, free_obj const *const obj) {

	for (unsigned i = 0; i < num; ++i) {
		vector3d const dir(signed_rand_vector());
		point const pos2(pos + dir*(2.0*radius));
		add_blastr(pos2, dir, 0.25*radius, 0.0, unsigned(time*TICKS_PER_SECOND), ALIGN_NEUTRAL, color, color, ETYPE_NONE, obj);
	}
}


void shift_univ_objs(point const &pos, bool shift_player_ship) {

	for (unsigned i = 0; i < uobjs.size(); ++i) { // update c_uobjs?
		assert(uobjs[i] != NULL);
		bool const player(uobjs[i]->is_player_ship());

		if (shift_player_ship || !player) { // shift even if !is_ok()
			uobjs[i]->move_by(pos);
			if (!player || MOVE_PLAYER_RPOS) {uobjs[i]->move_reset_by(pos);}
		}
	}
	player_death_pos += pos;
	universe_origin  += pos;
}


void clear_univ_obj_contexts() {

	for (unsigned i = 0; i < uobjs.size(); ++i) {
		if (uobjs[i]) {uobjs[i]->clear_context();}
	}
}


class motion_particles_t {

	rand_gen_t rgen;
	vector<point> pts;

	bool is_valid_pos(point const &pos, point const &camera) const {
		return (dist_less_than(pos, camera, 0.04) && player_pdu.point_visible_test(pos));
	}
	void gen_pos(point &pos, point const &camera, vector3d const &vnorm) {
		float const vdist(max(0.0, 0.02*dot_product(vnorm, player_pdu.dir)));

		for (unsigned n = 0; n < 20; ++n) { // make up to 20 attemps to generate an onscreen particle
			pos = camera + vdist*vnorm + rgen.signed_rand_vector_spherical(0.02);
			if (is_valid_pos(pos, camera)) break;
		}
	}
public:
	void update_and_draw(point const &camera, vector3d const &vnorm, float length, float width) {
		assert(length > 0.0 && width > 0.0);
		if (pts.empty()) {
			pts.resize(50); // npts
			for (unsigned i = 0; i < pts.size(); ++i) {gen_pos(pts[i], camera, vnorm);}
		}
		for (unsigned i = 0; i < pts.size(); ++i) {
			if (!is_valid_pos(pts[i], camera)) {gen_pos(pts[i], camera, vnorm);}
			trail_rays.push_back(usw_ray(width, width, pts[i], (pts[i] - length*vnorm), GRAY, GRAY));
		}
	}
};

motion_particles_t motion_particles;


void maybe_draw_motion_dust() { // if moving, even if unpowered

	// draw particles around ship showing motion
	if (!animate2 || !player_near_system()) return;
	u_ship const &pship(player_ship());
	if (do_run == 2 && !player_inside_system()) return; // hyperspeed: only draw dust when inside the system
	vector3d const &pvel(pship.get_velocity());
	float const pvmag(pvel.mag()), vmax(SLOW_SPEED_FACTOR*pship.specs().max_speed);
	if (pvmag < 0.8*vmax) return; // too slow
	float const length(1.0*min(4.0f*vmax, pvmag));
	motion_particles.update_and_draw(pship.get_pos(), pvel/pvmag, length, 0.00002);
}


void draw_ship_crosshair(pt_line_drawer &pld, point const &pos, colorRGBA const &color, float radius=0.0) { // for onscreen targeting

	double const dist_extend(0.1);
	upos_point_type const &camera(get_player_pos2());
	vector3d_d dir(camera, pos);
	double const dist(dir.mag());
	if (dist < TOLERANCE) return;
	dir /= dist; // normalize
	vector3d_d const delta(dir*dist_extend);
	point_d const chpos(make_pt_global(camera - delta));
	vector3d const vortho(cross_product(up_vector, vector3d(dir)));
	double sz(0.004*dist_extend), slen(1.0);
	point verts[4];
	
	if (radius > 0.0) { // sized object
		float const min_sz(radius*dist_extend/dist);
		
		if (sz < min_sz) { // object size larger than min targeting size
			slen = max(0.1, 0.5*sz/min_sz);
			sz   = min_sz;
		}
	}
	for (unsigned i = 0; i < 4; ++i) { // transform 4 quad points
		verts[i] = chpos + up_vector*((i>>1) ? sz : -sz) + vortho*(((i&1)^(i>>1)) ? sz : -sz);
	}
	for (unsigned i = 0; i < 4; ++i) { // 4 line segments
		point const &p1(verts[i]), &p2(verts[(i+1)&3]);

		if (slen < 0.5) { // split up into 2 sections like [-  -]
			pld.add_line(p1, dir, color, (p1 + slen*(p2 - p1)), dir, color);
			pld.add_line(p2, dir, color, (p2 - slen*(p2 - p1)), dir, color);
		}
		else { // leave as a single full segment
			pld.add_line(p1, dir, color, p2, dir, color);
		}
	}
}


void draw_all_crosshairs(pt_line_drawer &pld) {

	if (pld.empty()) return;
	glDisable(GL_DEPTH_TEST); // always draw on top
	glEnable(GL_LINE_SMOOTH);
	enable_blend(); // is this needed?
	pld.draw();
	disable_blend();
	glDisable(GL_LINE_SMOOTH);
	glEnable(GL_DEPTH_TEST);
}


void set_sane_light_atten(shader_t *shader=nullptr) {

	// force all lights to be enabled temporarily so that we can set them to constant atten to avoid div-by-zero if we don't set them later
	for (unsigned i = 0; i < MAX_SHADER_LIGHTS; ++i) {
		bool const enabled(is_light_enabled(i));
		if (!enabled) {enable_light(i);}
		setup_gl_light_atten(i, 1.0, 0.0, 0.0, shader);
		if (!enabled) {disable_light(i);}
	}
}

void create_univ_cube_map() {
	
	if (!ship_cube_map_reflection) return;
	static unsigned last_size(0);
	unsigned const tex_size = 512; // make dynamic?
	u_ship const &player(player_ship()); // Note: we shouldn't have to actually modify player dir and upv, as long as callers all use player_pdu
	point const camera_pos(player.get_pos());
	pos_dir_up const prev_player_pdu(player_pdu);
	setup_cube_map_reflection_texture(univ_reflection_tid, tex_size, last_size);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	set_custom_viewport(tex_size, 90.0, UNIV_NEAR_CLIP, UNIV_FAR_CLIP); // 90 degree FOV

	for (unsigned dim = 0; dim < 3; ++dim) {
		for (unsigned dir = 0; dir < 2; ++dir) {
			unsigned const face_id(2*dim + !dir);
			vector3d view_dir(zero_vector), up(-plus_y);
			view_dir[dim] = (dir ? 1.0 : -1.0);
			if (dim == 1) {up = (dir ? plus_z : -plus_z);} // Note: in OpenGL, the cube map top/bottom is in Y, and up dir is special in this dim
			float const angle(0.5*90.0*TO_RADIANS); // 90 degree FOV
			player_pdu = pos_dir_up(camera_pos, view_dir, up, angle, UNIV_NEAR_CLIP, UNIV_FAR_CLIP, 1.0, 1);
			//player_pdu.valid = 0; // for debugging - very slow
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			vector3d const eye(camera_pos - 0.001*view_dir);
			fgLookAt(eye.x, eye.y, eye.z, camera_pos.x, camera_pos.y, camera_pos.z, up.x, up.y, up.z);
			draw_universe_all(1, 0, 1, 0, 0, 1);
			render_to_texture_cube_map(univ_reflection_tid, tex_size, face_id); // render reflection to texture
		} // for dir
	} // for dim
	gen_mipmaps(6); // optional?
	player_pdu = prev_player_pdu; // restore prev value
	restore_scene_state();
	check_gl_error(541);
}

void setup_ship_draw_shader(shader_t &s) {

	//s.set_prefix("#define ALPHA_MASK_TEX", 1); // FS
	s.set_vert_shader("ship_draw");
	s.set_frag_shader("bump_map.part+ads_lighting.part*+black_body_burn.part+ship_draw");
	s.set_prefix("#define USE_BUMP_MAP", 1); // FS
	if (allow_shader_invariants) {s.set_prefix("invariant gl_Position;", 0);} // VS
	if (ship_cube_map_reflection) {s.set_prefixes_str("#define ENABLE_CUBE_MAP_REFLECT", 3);} // VS/FS
	s.begin_shader();
	s.add_uniform_int("tex0", 0);
	s.add_uniform_int("bump_map", 2);
	s.add_uniform_float("lum_scale",  0.0);
	s.add_uniform_float("lum_offset", 0.0);
	set_sane_light_atten(&s);
	// burn mask stuff
	select_texture(DISINT_TEX, 1);
	s.add_uniform_int ("burn_mask", 1); // used instead of alpha_mask_tex
	s.add_uniform_float("burn_offset",   -1.0);
	s.add_uniform_float("burn_tex_scale", 1.0);

	if (ship_cube_map_reflection) {
		bind_texture_tu(univ_reflection_tid, 14);
		s.add_uniform_int("reflection_tex", 14);
		upload_mvm_to_shader(s, "fg_ViewMatrix");
		s.add_uniform_vector3d("camera_pos", get_camera_pos());
	}
	char const *uniforms[] = {"postproc_color_op", "do_lighting_op", "maybe_bump_map_op"};
	char const *bindings[] = {"no_op", "normal_lighting", "no_bump_map"}; // defaults
	s.set_all_subroutines(1, 3, uniforms, bindings);
}


void disable_exp_lights(shader_t *s) {

	for (unsigned i = 0; i < NUM_EXP_LIGHTS; ++i) {
		enable_light(EXPLOSION_LIGHT + i); // enable it first to force updating of shader uniforms if it was left disabled but not black
		clear_colors_and_disable_light((EXPLOSION_LIGHT + i), s);
	}
}


void add_player_ship_engine_light() {

	if (!animate2) return;
	u_ship const &ps(player_ship());
	if (!ps.powered()) return; // check if player is providing thrust?
	colorRGBA const &color(ps.specs().engine_color);
	add_blastr(ps.pos, ps.get_dir(), 2.0*ps.get_radius(), 0.0, 0, -1, color, color, ETYPE_NONE, &ps);
}


void setup_shield_shader(shader_t &shader, int noise_tu_id) {
	shader.set_vert_shader("no_lighting_pos_tc");
	shader.set_frag_shader("ship_shields");
	shader.begin_shader();
	shader.add_uniform_float("min_alpha", 0.001);
	shader.add_uniform_int("tex0", 0);
	shader.add_uniform_int("noise_tex", noise_tu_id);
	bind_texture_tu(get_noise_tex_3d(64, 1), noise_tu_id); // grayscale noise
	static float shields_time(0.0);
	if (animate2) {shields_time += fticks; if (shields_time > 1.0E4) {shields_time = 0.0;}} // reset when large to avoid FP error
	shader.add_uniform_float("time", shields_time);
}


void draw_univ_objects() {

	//RESET_TIME;
	unsigned const nobjs((unsigned)c_uobjs.size());
	static vector<pair<float, free_obj *> > sorted;
	static vector<free_obj *> unsorted;
	sorted.resize(0);
	unsorted.resize(0);
	point const &camera(get_player_pos2());
	float const ch_dist(player_ship().specs().sensor_dist*player_sensor_dist_mult);
	free_obj const *player_target(get_player_target());
	pt_line_drawer ch_pld;
	text_drawer_t text_drawer;

	for (unsigned i = 0; i < nobjs; ++i) { // make negative so it's sorted largest to smallest
		cached_obj const &co(c_uobjs[i]);
		assert(co.obj != nullptr);
		bool const is_bad((co.flags & OBJ_FLAGS_BAD_) != 0);
		float const radius_scaled(co.obj->get_draw_radius()), max_radius(max(radius_scaled, co.radius));
		bool draw_obj(0);
		if (is_bad) {} // not drawn
		else if (is_distant(co.pos, radius_scaled)) { // distant
			if ((co.flags & OBJ_FLAGS_PROJ) && !is_distant(co.pos, 10.0*radius_scaled) && univ_sphere_vis(co.pos, max_radius)) {
				co.obj->draw_flares_only(); // visible projectile, distant but not too distant - draw flares only
			}
		}
		else if (univ_sphere_vis(co.pos, max_radius)) { // Note: VFC must be conservative, so use larger radius
			// make static objects (such as asteroids) have a large distance so that they're drawn first;
			// this is okay as they should be opaque, and will make good occluders
			if (co.flags & OBJ_FLAGS_STAT) {unsorted.push_back(co.obj);} // add other cases (sphere/tri particles, etc.)?
			else {sorted.push_back(make_pair(-(p2p_dist(co.pos, camera) - radius_scaled), co.obj));}
			draw_obj = 1;
		}
		if (!draw_obj) {co.obj->reset_lights();} // reset for next frame
		bool const show_crosshair(onscreen_display || co.obj == player_target);
		
		if ((show_crosshair || show_scores) && !is_bad && (co.flags & OBJ_FLAGS_SHIP) && univ_sphere_vis(co.pos, max_radius) && !co.obj->is_player_ship()) {		
			if ((!(co.flags & OBJ_FLAGS_DIST) || dist_less_than(co.pos, camera, ch_dist)) && co.obj->visibility() > 0.1 && co.obj->get_time() > 0) {
				colorRGBA const &color(alignment_colors[co.obj->get_align()]);
				if (show_crosshair) {draw_ship_crosshair(ch_pld, co.pos, color, max_radius);}

				if (show_scores && dist_less_than(co.pos, camera, 0.4)) { // tab key
					string const str(co.obj->get_name() + "\n" + co.obj->get_info());
					text_drawer.strs.push_back(text_string_t(str, make_pt_global(co.pos), 2.0, color));
				}
			}
		}
	} // for i
	sort(sorted.begin(), sorted.end()); // sort uobjs by distance to camera
	//PRINT_TIME("Sort");
	select_texture(WHITE_TEX); // always textured (see end_texture())
	enable_blend(); // doesn't hurt

	// setup emissive shader, which is used for ship shields and EMP (will be disabled by ship draw shader)
	setup_shield_shader(emissive_shader, 1);

	// draw ubojs
	shader_t s;
	vpc_shader_t upc_shader;
	setup_ship_draw_shader(s);
	disable_exp_lights(&s); // make sure the explosion lights start out cleared
	for (auto i = unsorted.begin(); i != unsorted.end(); ++i) {(*i)->draw_and_reset_lights(s, upc_shader);}
	for (auto i = sorted.begin();   i != sorted.end();   ++i) {i->second->draw_and_reset_lights(s, upc_shader);}
	emissive_shader.end_shader();
	upc_shader.end_shader(); // leaves program==0
	disable_exp_lights(nullptr); // make sure the explosion lights end cleared
	enable_blend(); // redundant?

	if (!particle_pld.empty()) {
		uobject const *sobj(NULL); // unused
		set_uobj_color(camera, 0.0, 0, 1, nullptr, nullptr, sobj, 2.0, 2.0, NULL); // increased ambient scale
		s.enable();
		particle_pld.draw_and_clear();
	}
	s.end_shader();
	glDepthMask(GL_FALSE);
	smoke_psd.draw_and_clear(SMOKE_PUFF_TEX); // uses a point sprite shader internally
	glow_psd .draw_and_clear(BLUR_TEX      ); // uses a point sprite shader internally
	glDepthMask(GL_TRUE);

	disable_blend();
	set_additive_blend_mode();
	maybe_draw_motion_dust();
	draw_and_update_engine_trails(trail_rays.drawer);
	trail_rays.draw(0.0); // draw engine trails
	beam_rays.draw(50.0); // draw beam weapons and lightning
	set_std_blend_mode();
	add_nearby_uobj_text(text_drawer); // only if show_scores?
	text_drawer.draw();

	if (onscreen_display) { // for crosshairs
		draw_ship_crosshair(ch_pld, universe_origin, MAGENTA); // starts off as player start marker - world origin
		if (player_death_pos != universe_origin) {draw_ship_crosshair(ch_pld, player_death_pos, ORANGE);} // player death marker
	}
	if (!ch_pld.empty()) {
		shader_t chs;
		chs.begin_color_only_shader();
		draw_all_crosshairs(ch_pld);
		chs.end_shader();
	}
	//PRINT_TIME("Draw");
}


void purge_old_objs() {

	unsigned nbad(0);
	unsigned const nobjs((unsigned)uobjs.size());
	check_explosion_refs();

	for (unsigned i = 0; i < a_targets.size(); ++i) {
		if (a_targets[i] != NULL && a_targets[i]->not_a_target()) a_targets[i] = NULL;
	}
	for (unsigned i = 0; i < attackers.size(); ++i) {
		if (attackers[i] != NULL && attackers[i]->not_a_target()) attackers[i] = NULL;
	}
	for (unsigned i = 0; i < nobjs; ++i) {
		if (VERIFY_REFS) {uobjs[i]->verify_status();}
		if (uobjs[i]->to_be_removed()) {++nbad;}
		else {uobjs[i]->check_ref_objs();} // allow everyone to remove their references to objects that are about to be removed
	}
	if (nbad == 0) return; // no bad objects
	static vector<free_obj *> uobjs2;
	uobjs2.resize(0);
	uobjs2.reserve(nobjs - nbad);

	for (unsigned i = 0; i < nobjs; ++i) { // rebuild uobjs vector
		if (uobjs[i]->to_be_removed()) {
			if (!uobjs[i]->dec_ref()) { // all ships and stationary objects should be deleted through this point and nowhere else, can't allocate on the stack
				assert(!uobjs[i]->is_player_ship());
				assert(uobjs[i]->is_ship() || uobjs[i]->is_stationary() || uobjs[i]->is_part_cloud()); // sanity check
				if (uobjs[i]->is_ship()) {++alloced_fobjs[1];}
				delete uobjs[i]; // Note: this is the *only* place where ships, stationary objects, and particle clouds are deleted
			}
		}
		else {
			uobjs[i]->next_frame();
			uobjs2.push_back(uobjs[i]);
			if (VERIFY_REFS) uobjs[i]->verify_status();
		}
	}
	assert(uobjs2.size() == (nobjs - nbad));
	uobjs.swap(uobjs2);
}


us_class const &get_sclass_obj(unsigned sclass) {

	assert(sclass < NUM_US_CLASS);
	return sclasses[sclass];
}

struct null_deleter {
    template<typename T> void operator()(T *) {}
};

// Note: must be here where all ship_coll_obj derived classes have complete types
void cobj_vector_t::resize(size_t sz) {vector<p_const_ship_coll_obj>::resize(sz);}
void cobj_vector_t::clear() {vector<p_const_ship_coll_obj>::clear();}
void cobj_vector_t::add(ship_coll_obj const *const o) {push_back(p_const_ship_coll_obj(o));}
void cobj_vector_t::add_no_delete(ship_coll_obj const *const o) {push_back(p_const_ship_coll_obj(o, null_deleter()));}


float get_ship_cost(unsigned sclass, unsigned align, unsigned reserve_credits, float discount) {

	assert(discount <= 1.0);
	if (init_credits[align] == 0) return 0.0; // no credits allocated, none used
	float const cost(unsigned(sclasses[sclass].cost*(1.0 - discount)));
	//cout << cost << " req for " << get_name() << ", have " << team_credits[align] << endl;
	if (team_credits[align] < (cost + reserve_credits)) return -1.0; // can't afford
	return cost;
}


bool alloc_resources_for(unsigned sclass, unsigned align, unsigned reserve_credits, float discount) {

	float const cost(get_ship_cost(sclass, align, reserve_credits, discount));
	if (cost < 0.0) return 0;
	team_credits[align] -= cost;
	return 1;
}



