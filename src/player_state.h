// 3D World - Player game state classes
// by Frank Gennari
// 7/3/06
#pragma once

#include "3DWorld.h"
using std::string;

bool const PLAYER_CAN_ENTER_BUILDINGS = 1;

float const JUMP_COOL  = 0.6; // cooloff time between jumps
float const JUMP_TIME  = 0.2; // time of jump acceleration
float const JUMP_ACCEL = 1.0; // jump acceleration

unsigned const HIT_TIME     = 6;
int const NUM_WEAPONS       = 17;
unsigned const POWERUP_TIME = unsigned(40*TICKS_PER_SECOND);
int const dodgeball_tids[]  = {SKULL_TEX, RADIATION_TEX, YUCK_TEX};
unsigned const NUM_DB_TIDS(sizeof(dodgeball_tids)/sizeof(int));

// weapons
enum {W_UNARMED=0, W_BBBAT, W_BALL, W_SBALL, W_ROCKET, W_LANDMINE, W_SEEK_D, W_STAR5, W_M16, W_SHOTGUN, W_GRENADE,
	  W_LASER, W_PLASMA, W_BLADE, W_GASSER, W_RAPTOR, W_XLOCATOR, /* non-selectable*/ W_CGRENADE, W_SAWBLADE, W_TELEPORTER, NUM_TOT_WEAPONS};

enum {SF_EYE=0, SF_NOSE, SF_TONGUE, SF_HEADBAND, NUM_SMILEY_PARTS};


struct bbox { // size = 20
	float x1, y1, x2, y2;
	int index;
	bbox() : x1(0), y1(0), x2(0), y2(0), index(0) {}
};


struct team_info { // size = 20
	bbox bb; // add others?
};


struct od_data { // size = 12

	int id, type, val;
	float dist;

	od_data() : id(0), type(0), val(0), dist(0.0f) {};
	od_data(int type0, int id0, float dist0, int val0=0) : id(id0), type(type0), val(val0), dist(dist0) {};
	bool operator<(od_data const &o) const {return (dist < o.dist);}
};


typedef unsigned short wpt_ix_t;
typedef vector<wpt_ix_t> waypt_adj_vect;


struct waypoint_t {

	bool user_placed, placed_item, goal, temp, visited, disabled, next_valid;
	int came_from, item_group, item_ix, coll_id, connected_to;
	float g_score, f_score;
	point pos;
	double last_smiley_time;
	waypt_adj_vect next_wpts, prev_wpts;

	waypoint_t(point const &p=all_zeros, int cid=-1, bool up=0, bool i=0, bool g=0, bool t=0);
	void mark_visited_by_smiley(unsigned const smiley_id);
	float get_time_since_last_visited(unsigned const smiley_id) const;
	void clear();
	bool unreachable() const {return prev_wpts.empty();}
};


class waypoint_vector : public vector<waypoint_t> {

	vector<wpt_ix_t> free_list;

public:
	wpt_ix_t add(waypoint_t const &w);
	void remove(wpt_ix_t ix);
	void clear() {vector<waypoint_t>::clear(); free_list.clear();}
};


struct wpt_goal {

	int mode; // 0: none, 1: user wpt, 2: placed item wpt, 3: goal wpt, 4: wpt index, 5: closest wpt, 6: closest visible wpt, 7: goal pos (new wpt)
	unsigned wpt;
	point pos;

	wpt_goal(int m=0, unsigned w=0, point const &p=all_zeros);
	bool is_reachable() const;
};


class waypt_used_set {

	unsigned last_wp;
	int last_frame;
	map<unsigned, int> used;

public:
	waypt_used_set() : last_wp(0), last_frame(0) {}	
	void clear();
	void insert(unsigned wp);
	bool is_valid(unsigned wp);
};


class unreachable_pts {

	int try_counts;
	float try_dist_sq;
	vector<point> cant_get;

public:
	unreachable_pts() : try_counts(0), try_dist_sq(0.0) {}
	
	void clear() {
		reset_try();
		cant_get.clear();
	}
	void reset_try(float tdist_sq=0.0) {
		try_counts  = 0;
		try_dist_sq = tdist_sq;
	}
	bool cant_reach(point const &pos) const;
	bool proc_target(point const &pos, point const &target, point const &last_target, bool can_reach);
	void add(point const &pos) {cant_get.push_back(pos);}
	void shift_by(vector3d const &vd);
};


struct destination_marker {

	int xpos, ypos, dmin_sq;
	float min_depth;
	bool valid;

	destination_marker() : xpos(0), ypos(0), dmin_sq(0), min_depth(0.0), valid(0) {}
	bool add_candidate(int x1, int y1, int x2, int y2, float depth, float radius);
	void update_dmin(int x, int y) {if (valid) dmin_sq = (xpos - x)*(xpos - x) + (ypos - y)*(ypos - y);}
	point get_pos() const;
	void clear() {valid = 0; dmin_sq = 0; min_depth = 0.0;}
};


struct type_wt_t {
	unsigned type;
	float weight;
	type_wt_t(unsigned t=0, float w=1.0) : type(t), weight(w) {}
};


struct user_waypt_t {
	int type;
	point pos;
	user_waypt_t(int type_=0, point const &pos_=all_zeros) : type(type_), pos(pos_) {}
};


struct player_state { // size = big

	struct count_t {
		unsigned c;
		count_t(unsigned c_=0) : c(c_) {}
	};

	bool plasma_loaded, on_waypt_path, is_jumping;
	int target, objective, weapon, wmode, powerup, powerup_time, cb_hurt;
	int kills, deaths, suicides, team_kills, max_kills, tot_kills, killer;
	int init_frame, fire_frame, was_hit, hitter, last_teleporter, target_visible, kill_time, rot_counter, uw_time, jump_time, freeze_time;
	int target_type, stopped_time, last_waypoint;
	unsigned tid, fall_counter, chunk_index;
	float shields, plasma_size, zvel, dpos, last_dz, last_zvel, last_wpt_dist;
	double ticks_since_fired;
	point target_pos, objective_pos, cb_pos;
	vector3d hit_dir, velocity;
	string name;
	int p_weapons[NUM_WEAPONS], p_ammo[NUM_WEAPONS];
	vector<unsigned char> tdata;
	vector<int> balls;
	set<unsigned> keycards;
	vector<type_wt_t> target_types; // reused across frames in smiley_select_target()
	map<unsigned, count_t> blocked_waypts;
	waypt_used_set waypts_used;
	unreachable_pts unreachable[2]; // {objects, waypoints}
	destination_marker dest_mark;
	rand_gen_t player_rgen;

	// footstep/snow footprint state
	point prev_foot_pos;
	unsigned step_num;
	bool foot_down;

	player_state() : plasma_loaded(0), on_waypt_path(0), is_jumping(0), target(-1), objective(-1), weapon(0), wmode(0), powerup(PU_NONE), powerup_time(0),
		 cb_hurt(0), kills(0), deaths(0), suicides(0), team_kills(0), max_kills(0), tot_kills(0), killer(NO_SOURCE), init_frame(0), fire_frame(0), was_hit(0),
		hitter(NO_SOURCE), last_teleporter(NO_SOURCE), target_visible(0), kill_time(0), rot_counter(0), uw_time(0), jump_time(0), freeze_time(0), target_type(0),
		stopped_time(0), last_waypoint(-1), tid(0), fall_counter(0), chunk_index(0), shields(0.0), plasma_size(0.0), zvel(0.0), dpos(0.0), last_dz(0.0), last_zvel(0.0),
		last_wpt_dist(0.0), ticks_since_fired(0.0), target_pos(all_zeros), objective_pos(all_zeros), cb_pos(all_zeros), hit_dir(all_zeros), velocity(zero_vector),
		prev_foot_pos(all_zeros), step_num(0), foot_down(0)
	{init_wa();}

	void init_wa();
	void init(bool w_start);
	void reset_wpt_state();
	bool no_weap_id(int cur_weapon) const;
	bool no_ammo_id(int cur_weapon) const;
	bool no_weap() const {return no_weap_id(weapon);}
	bool no_ammo() const {return no_ammo_id(weapon);}
	float weapon_range(bool use_far_clip) const;
	void jump(point const &pos);
	void verify_wmode();
	bool can_fire_weapon()   const;
	bool no_weap_or_ammo()   const {return (no_weap() || no_ammo());}
	float get_damage_scale() const {return ((powerup == PU_DAMAGE) ? 4.0 : 1.0);}
	float get_rspeed_scale() const {return ((powerup == PU_SPEED)  ? 1.5 : 1.0);}
	float get_fspeed_scale() const {return ((powerup == PU_SPEED)  ? 2.0 : 1.0);}
	float get_shield_scale() const {return ((powerup == PU_SHIELD) ? 0.5 : 1.0);}
	int get_prev_fire_time_in_ticks() const;
	int get_drown_time() const;

	void register_kill() {++kills; ++tot_kills; max_kills = max(max_kills, kills); kill_time = 0;}
	void register_team_kill() {++team_kills;}
	void register_suicide() {++suicides;}
	void register_death(int killer_) {++deaths; kills = 0; killer = killer_;}
	int get_score() const {return (tot_kills - deaths - team_kills);}
	
	void smiley_fire_weapon(int smiley_id);
	int find_nearest_enemy(point const &pos, pos_dir_up const &pdu, point const &avoid_dir, int smiley_id,
		point &target, int &target_visible, float &min_dist) const;
	void check_cand_waypoint(point const &pos, point const &avoid_dir, int smiley_id,
		unsigned i, int curw, float dmult, pos_dir_up const &pdu, bool next, float max_dist_sq);
	void mark_waypoint_reached(int curw, int smiley_id);
	int find_nearest_obj(point const &pos, pos_dir_up const &pdu, point const &avoid_dir, int smiley_id, point &target_pt,
		float &min_dist, vector<type_wt_t> types, int last_target_visible, int last_target_type);
	int check_smiley_status(dwobject &obj, int smiley_id);
	void drop_pack(point const &pos);
	int drop_weapon(vector3d const &coll_dir, vector3d const &nfront, point const &pos, int index, float energy, int type);
	void smiley_select_target(dwobject &obj, int smiley_id);
	float get_pos_cost(int smiley_id, point pos, point const &opos, pos_dir_up const &pdu, float radius, float step_height, bool check_dists);
	int smiley_motion(dwobject &obj, int smiley_id);
	void advance(dwobject &obj, int smiley_id);
	void shift(vector3d const &vd);
	void check_switch_weapon(int smiley_id);
	float get_rel_enemy_vel(point const &pos) const;
	int target_in_range(point const &pos) const;
	void smiley_action(int smiley_id);
	void next_frame();
	void use_translocator(int player_id);

	// camera members
	void gamemode_fire_weapon();
	void switch_weapon(int val, int verbose);
	bool pickup_ball(int index);
	int fire_projectile(point fpos, vector3d dir, int shooter, int &chosen_obj);
	void update_camera_frame();
	void update_sstate_game_frame(int i);
	void free_balls();
	void update_weapon_cobjs(int i);
};


struct teleporter : public sphere_t, public volume_part_cloud {

	point dest;
	double last_used_tfticks;
	float draw_radius_scale;
	unsigned tid;
	int source;
	bool is_portal, is_indoors, enabled;

	teleporter() : dest(all_zeros), last_used_tfticks(0.0), draw_radius_scale(1.5), tid(0), source(NO_SOURCE), is_portal(0), is_indoors(0), enabled(1) {}
	void from_obj(dwobject const &obj);
	float get_draw_radius  () const {return draw_radius_scale*radius;}
	float get_teleport_dist() const {return p2p_dist(pos, dest);}
	bool do_portal_draw() const;
	void setup() {gen_pts(get_draw_radius());}
	void write_to_cobj_file(std::ostream &out) const;
	void create_portal_texture();
	void draw(vpc_shader_t &s, bool is_dynamic);
	bool maybe_teleport_object(point &opos, float oradius, int player_id, bool small_object);
	void free_context() {free_texture(tid);}
};


struct jump_pad : public sphere_t {
	
	vector3d velocity; // should be up
	double last_used_tfticks;

	jump_pad() : velocity(zero_vector), last_used_tfticks(0.0) {}
	void draw(shader_t &s) const;
	bool maybe_jump(point &opos, vector3d &obj_velocity, float oradius, int player_id);
};


// function prototypes
bool check_step_dz(point &cur, point const &lpos, float radius);
int find_optimal_next_waypoint(unsigned cur, wpt_goal const &goal, set<unsigned> const &wps_penalty);
void find_optimal_waypoint(point const &pos, vector<od_data> &oddatav, wpt_goal const &goal);
bool can_make_progress(point const &pos, point const &opos, bool check_uw);
bool is_valid_path(point const &start, point const &end, bool check_uw);
colorRGBA get_keycard_color(unsigned color_id);

