// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 5/19/02

#include "gameplay.h"
#include "explosion.h"
#include "transform_obj.h"
#include "player_state.h"
#include "physics_objects.h"
#include "shape_line3d.h"
#include "openal_wrap.h"


bool const SELF_LASER_DAMAGE = 1;
bool const SMILEY_GAS        = 1;
bool const FADE_MESSAGE_ALPHA= 1;
bool const FREEZE_MODE       = 1; // for raptor
float const UWATER_FERR_ADD  = 0.05;
float const UWATER_FERR_MUL  = 2.0; // lower accuracy
float const ZOOM_FERR_MULT   = 0.5; // higher accuracy
float const LASER_REFL_ATTEN = 0.95;

// should put these in a file
string const all_smiley_names[] =
{"0-ZERO-0", "Kodiak", "Grim Reaper", "Cannon fodder", "Psycho", "Yomamma", "Mr. Awesome", "IownU", "Fragmaster", "Archangel", "Smiley:)", "Mr Shootey"};


struct text_message_params {

	bool fade;
	int itime, mtime, priority;
	float size, yval;
	colorRGBA color;
	text_message_params() : fade(0), itime(0), mtime(0), priority(0), size(0.0f), yval(0.0f), color(WHITE) {}
	text_message_params(int t, float s, colorRGBA const &c, int p, bool fade_=0, float y=0.0)
		: fade(fade_), itime(fade ? 3*t/2 : t), mtime(itime), priority(p), size(s), yval(y), color(c) {}
};


int following(0), camera_flight(0), blood_spilled(0), camera_invincible(0), br_source(0), UNLIMITED_WEAPONS(0), last_inventory_frame(0);
float camera_health(100.0), team_damage(1.0), self_damage(1.0), player_damage(1.0), smiley_damage(1.0);
point orig_camera(all_zeros), orig_cdir(plus_z);
vector<spark_t> sparks;
vector<beam3d> beams;
text_message_params msg_params;
string message;
blood_spot blood_spots[NUM_BS];
player_state *sstates = NULL;
vector<team_info> teaminfo;
vector<bbox> team_starts;


extern bool vsync_enabled, spraypaint_mode, smoke_visible, begin_motion, flashlight_on, disable_fire_delay, disable_recoil, enable_translocator, disable_blood, play_gameplay_alert;
extern int game_mode, window_width, window_height, world_mode, fire_key, spectate, animate2;
extern int camera_reset, frame_counter, camera_mode, camera_coll_id, camera_surf_collide, b2down;
extern int num_groups, num_smileys, left_handed, iticks, DISABLE_WATER, voxel_editing, player_dodgeball_id;
extern int free_for_all, teams, show_scores, camera_view, xoff, yoff, display_mode, destroy_thresh;
extern unsigned create_voxel_landscape, spheres_mode, flashlight_color_id;
extern float temperature, ball_velocity, water_plane_z, zmin, zmax, ztop, zbottom, czmax, fticks, crater_depth, crater_radius;
extern float max_water_height, XY_SCENE_SIZE, TIMESTEP, FAR_CLIP, atmosphere, camera_shake, base_gravity, dist_to_fire_sq;
extern double c_phi, tfticks;
extern point surface_pos, camera_last_pos;
extern string coll_damage_name;
extern int coll_id[];
extern obj_type object_types[];
extern obj_group obj_groups[];
extern char player_name[];
extern coll_obj_group coll_objects;

bool push_movable_cobj(unsigned index, vector3d &delta, point const &pushed_from);
void enter_building_gameplay_mode();
bool flashlight_enabled();


point &get_sstate_pos(int id) {
	return ((id == CAMERA_ID) ? camera_pos : obj_groups[coll_id[SMILEY]].get_obj(id).pos);
}
vector3d &get_sstate_dir(int id) {
	return ((id == CAMERA_ID) ? cview_dir : obj_groups[coll_id[SMILEY]].get_obj(id).orientation);
}
float get_sstate_radius(int id) {
	return ((id == CAMERA_ID) ? CAMERA_RADIUS : object_types[SMILEY].radius);
}


int gen_game_obj(int type) {

	switch (type) {
		case POWERUP:
			//return PU_SPEED;
			return rand()%NUM_POWERUPS;
		case WEAPON:
			while (1) {
				//return W_LASER;
				int const id(rand()%NUM_WEAPONS);
				if (weapons[id].need_weapon) return id;
			}
		case AMMO:
			while (1) {
				//return W_LASER;
				int const id(rand()%NUM_WEAPONS);
				if (id == W_XLOCATOR) continue; // translocator can't be picked up as ammo
				if (weapons[id].need_ammo) return id;
			}
		default: assert(0);
	}
	return -1;
}


int get_ammo_or_obj(int wid) {

	switch (wid) {
	case W_M16:     return SHELLC;
	case W_SHOTGUN: return PROJECTILE;
	case W_PLASMA:  return PLASMA;
	case W_LASER:   return BEAM;
	}
	return weapons[wid].obj_id; // could be -1 for invalid
}


int wid_need_weapon(int wid) { // used in draw_world.cpp
	return weapons[wid].need_weapon;
}

bool is_area_damage(int type) {
	return (type == DROWNED || type == FROZEN || type == SUFFOCATED || type == GASSED);
}


int same_team(int source, int target) {

	if (teams <= 1) return (!((source == CAMERA_ID) ^ (target == CAMERA_ID)));
	int source_team((source + teams)%teams);
	int target_team((target + teams)%teams);
	return (source_team == target_team);
}


void blood_on_camera(unsigned num_spots) {

	num_spots = min(num_spots, NUM_BS/4);
	float const xrand(0.012*((float)window_width/(float)window_height));

	for (unsigned i = 0, n = 0; i < NUM_BS && n < num_spots; ++i) {
		if (blood_spots[i].time <= 0 || ((NUM_BS - i) <= (num_spots - n))) {
			blood_spots[i].pos.assign(rand_uniform(-xrand, xrand), rand_uniform(-0.012, 0.012), -0.02);
			blood_spots[i].size = rand_uniform(3.0, 50.0);
			blood_spots[i].time = int(10 + 3*blood_spots[i].size);
			++n;
		}
	}
}


int compute_damage(float &energy, int type, int obj_index, int source, int target) {

	if (type == COLLISION && energy < 0.0) return 1; // negative damage/heal
	energy = max(energy, 0.0f);

	if (type >= 0 && type < NUM_TOT_OBJS && (type != BALL || obj_groups[coll_id[type]].get_obj(obj_index).status == 1 || obj_groups[coll_id[type]].get_obj(obj_index).status == 2))	{
		energy += object_types[type].damage;
	}
	if (type == SHIELD) {sstates[target].shields = min(MAX_SHIELDS, (100.0f + sstates[target].shields));}
	if (type == HEALTH || type == SHIELD || type == POWERUP || type == COLLISION) return 1;

	if (source == target && energy > 0.0) { // hit yourself
		if (self_damage == 0.0) return 0;
		if (type == BALL) {energy = 0.0;} else {energy *= self_damage;}
	}
	if (energy == 0.0) return 1;
	blood_spilled = 1;

	if (type == STAR5 || type == SHRAPNEL) {
		energy *= 0.8*fticks;
	}
	else if (type == FRAGMENT) {
		dwobject &obj(obj_groups[coll_id[type]].get_obj(obj_index));
		if (obj.vdeform.x < 0.5) return 0; // too small to cause damage
		energy *= obj.vdeform.x;
		if (obj.flags & TYPE_FLAG) {obj.init_dir = 0.8*obj.init_dir + 0.2*vector3d(1.0, 0.0, 0.0);} // triangle fragment - make red/bloody
	}
	if (sstates[target].powerup == PU_SHIELD) {
		if (source == target && (type != LANDMINE && type != FELL && type != DROWNED && type != CRUSHED && type != COLLISION)) return 0;
		energy *= sstates[target].get_shield_scale();
	}
	if (type != FELL && type != DROWNED) energy *= sstates[source].get_damage_scale();
	energy *= ((target == CAMERA_ID) ? player_damage : smiley_damage);

	if (source != target && same_team(source, target)) {
		if (team_damage == 0.0) return 0;
		energy *= team_damage; // hit by a teammate
	}
	float const shield_damage(min(0.75f*HEALTH_PER_DAMAGE*energy, sstates[target].shields));
	sstates[target].shields -= shield_damage;
	energy -= shield_damage/HEALTH_PER_DAMAGE;
	return 1;
}


bool self_coll_invalid(int type, int obj_index) {

	if (is_rocket_type(type) || type == PROJECTILE || type == LASER || type == STAR5 || type == GASSED || type == TELEFRAG || type == XLOCATOR || type == JUMP_PAD) { // || type == SAWBLADE
		return 1;
	}
	if ((type == GRENADE || type == CGRENADE || type == S_BALL || type == BALL || type == PLASMA || type == SHRAPNEL || type == SAWBLADE) &&
		obj_groups[coll_id[type]].get_obj(obj_index).time < 10)
	{
		return 1;
	}
	if ((type == STAR5 || (type == SHRAPNEL && obj_groups[coll_id[type]].get_obj(obj_index).angle == 0.0)) &&
		obj_groups[coll_id[type]].get_obj(obj_index).velocity.mag_sq() > 1.0)
	{
		return 1;
	}
	if (type == LANDMINE && obj_groups[coll_id[type]].get_obj(obj_index).time < (int)SMILEY_LM_ACT_TIME) return 1;
	return 0;
}


// smileys and camera
void gen_dead_smiley(int source, int target, float energy, point const &pos, vector3d const &velocity, vector3d const &coll_dir,
					 int damage_type, float health, float radius, float blood_v, bool burned, int type)
{
	// eyes, nose, and tongue
	int const pcid(coll_id[SFPART]), offset(4*(target+1));
	int end_p_loop(4);
	float part_v(5.0 + 0.5*sqrt(energy)), chunk_v(7.5 + 0.2*sqrt(energy));
	vector3d orient(0.0, 0.0, 0.0);
	player_state &sstate(sstates[target]);
	bool const frozen(!burned && sstate.freeze_time > 0);

	if (target == CAMERA_ID) {
		orient = cview_dir;
	}
	else {
		assert(target < num_smileys);
		orient = obj_groups[coll_id[SMILEY]].get_obj(target).orientation; // should be normalized
	}
	if (sstate.kill_time < int(2*TICKS_PER_SECOND) || sstate.powerup == PU_DAMAGE) {
		++end_p_loop; // tongue out
	}
	assert(unsigned(offset + end_p_loop) <= obj_groups[pcid].max_objs);
	unsigned const part_ids[5] = {SF_EYE, SF_EYE, SF_NOSE, SF_HEADBAND, SF_TONGUE};

	for (int i = 0; i < end_p_loop; ++i) { // {eye, eye, nose, headband, [tongue]}
		obj_groups[pcid].create_object_at((i+offset), pos);
		dwobject &obj(obj_groups[pcid].get_obj(i+offset));
		obj.pos += gen_rand_vector(radius*rand_uniform(0.7, 1.3), 1.0, PI);
		obj.direction = part_ids[i];
		obj.source = target;
		gen_blood_velocity(obj.velocity, velocity, coll_dir, part_v, 0.3, 0.2, damage_type, health);
		if (part_ids[i] == SF_TONGUE) {obj.orientation = orient;} // tongue
	}

	if (!disable_blood) { // chunks
		int const chunk_start(NUM_CHUNK_BLOCKS*(SMILEY_NCHUNKS*(target+1) + sstate.chunk_index));
		int const ccid(coll_id[CHUNK]), end_c_loop(chunk_start + SMILEY_NCHUNKS);
		assert(unsigned(end_c_loop) <= obj_groups[ccid].max_objs);
		if (burned) chunk_v *= 0.4;

		for (int i = chunk_start; i < end_c_loop; ++i) { // chunks
			obj_groups[ccid].create_object_at(i, pos);
			dwobject &obj(obj_groups[ccid].get_obj(i));
			obj.pos       += gen_rand_vector(radius*rand_uniform(0.7, 1.3), 1.0, PI);
			obj.init_dir   = signed_rand_vector_norm();
			obj.init_dir.z = fabs(obj.init_dir.z);
			gen_blood_velocity(obj.velocity, velocity, coll_dir, chunk_v, 0.25, 0.22, damage_type, health);
			float const vmag(obj.velocity.mag());
			if (vmag > TOLERANCE) obj.pos += obj.velocity*(radius/vmag);
			if (burned) {obj.flags |= TYPE_FLAG;} // use TYPE_FLAG to encode burned state
			else if (frozen) {obj.flags |= FROZEN_FLAG;}
		}
	}
	// skull
	if (burned || energy < 250.0) { // frozen?
		obj_groups[coll_id[SKULL]].create_object_at(target+1, pos);
		dwobject &obj(obj_groups[coll_id[SKULL]].get_obj(target+1));
		obj.orientation = orient;
		obj.direction   = (burned ? 1 : 0); // encode burned state in direction
		gen_blood_velocity(obj.velocity, velocity, coll_dir, part_v, 0.15, 0.1, damage_type, health);
	}
	if (burned) {
		gen_fire(pos, 1.0, source);
		gen_smoke(pos);
	}
	else if (frozen) {} // nothing?
	else if (!disable_blood) { // add blood
		add_color_to_landscape_texture(BLOOD_C, pos.x, pos.y, min(1.5, 0.4*double(sqrt(blood_v)))*radius);
		modify_grass_at((pos - vector3d(0.0, 0.0, radius)), 1.8*radius, 0, 0, 0, 1, 1, 0, BLOOD_C); // check_uw?
	}
	sstate.chunk_index = (sstate.chunk_index + 1) % NUM_CHUNK_BLOCKS;
	
	if (type == DROWNED) {
		gen_sound(SOUND_DROWN, pos);
	}
	else if (type == CRUSHED || type == FELL) {
		gen_sound(SOUND_SQUISH, pos);
	}
	else if (type == GASSED) {
		gen_sound(SOUND_SCREAM3, pos, 1.0, rand_uniform(0.8, 1.2));
	}
	else if (type == FROZEN || type == SUFFOCATED || type == PLASMA_LT_D) { // these are rare ones
		gen_sound(SOUND_SQUEAL, pos);
	}
	else if (burned) {
		gen_sound(SOUND_SCREAM2, pos, 1.0, rand_uniform(0.7, 1.3));
	}
	else if (type == ROCKET) {
		gen_sound(SOUND_DEATH,   pos, 1.0, rand_uniform(0.7, 1.3));
	}
	else {
		gen_sound(SOUND_SCREAM1, pos, 1.0, rand_uniform(0.7, 1.3));
	}
}


unsigned create_blood(int index, int amt_denom, point const &pos, float obj_radius, vector3d const &velocity,
					  vector3d const &coll_dir, float blood_v, int damage_type, float health, bool burned, bool frozen)
{
	assert(amt_denom > 0);
	int const cid(coll_id[burned ? (unsigned)CHARRED : /*(frozen ? (unsigned)WDROPLET : (unsigned)BLOOD)*/(unsigned)BLOOD]);
	obj_group &objg(obj_groups[cid]);
	unsigned const blood_amt(objg.max_objs/(obj_groups[coll_id[SMILEY]].max_objs+1));
	unsigned const start_ix(blood_amt*index);
	unsigned const end_ix(min(objg.max_objs, unsigned(blood_amt*(index+1))));

	for (unsigned i = (start_ix + rand()%amt_denom); i < end_ix; i += amt_denom) {
		objg.create_object_at(i, pos);
		objg.get_obj(i).pos += gen_rand_vector(obj_radius*rand_uniform(0.7, 1.3), 1.0, PI);
		gen_blood_velocity(objg.get_obj(i).velocity, velocity, coll_dir, blood_v, 0.3, 0.3, damage_type, health);
	}
	return blood_amt;
}


inline float get_shrapnel_damage(float energy, int index) {

	return 0.5*energy + object_types[SHRAPNEL].damage*(1.0 -
		((float)obj_groups[coll_id[SHRAPNEL]].get_obj(index).time)/((float)object_types[SHRAPNEL].lifetime));
}


bool player_state::pickup_ball(int index) {

	if (game_mode != GAME_MODE_DODGEBALL) return 0;
	dwobject &obj(obj_groups[coll_id[BALL]].get_obj(index));
	if (obj.disabled())    return 0; // already picked up this frame?
	if (UNLIMITED_WEAPONS) return 0; //obj.disable()?
	assert(p_ammo[W_BALL] == (int)balls.size());
	weapon            = W_BALL;
	p_weapons[W_BALL] = 1;
	++p_ammo[W_BALL];
	balls.push_back(index);
	obj.status = OBJ_STAT_RES; // reserved status
	return 1;
}


bool is_burned(int type, int br_source) {

	return (type == PLASMA || type == FIRE || type == BURNED || type == LASER || type == BEAM ||
		(type == BLAST_RADIUS && br_source == PLASMA));
}


void player_coll(int type, int obj_index) {

	if (type == STAR5) {
		obj_groups[coll_id[type]].get_obj(obj_index).time += int(200.0*fticks);
	}
	else if (type == SHRAPNEL) {
		obj_groups[coll_id[type]].get_obj(obj_index).time += int(10.0*fticks);
	}
}


void update_kill_health(float &health) {
	health = max(health, min(100.0f, (health + KILL_HEALTH)));
}


// ***********************************
// COLLISION DAMAGE CODE
// ***********************************

bool is_pickup_item(int type) {return (type == POWERUP || type == WEAPON || type == AMMO || type == WA_PACK || type == HEALTH || type == SHIELD || type == KEYCARD);}

int proc_coll_types(int type, int obj_index, float &energy) {

	int const ocid(coll_id[type]);
	bool const is_typed_pickup(type == POWERUP || type == WEAPON || type == AMMO || type == WA_PACK || type == KEYCARD);

	if (is_pickup_item(type)) {
		obj_groups[ocid].get_obj(obj_index).disable();
		energy = 0.0; // no damage done
	}
	else if ((type == GRENADE || type == CGRENADE) && (rand()&3) == 0) {
		obj_groups[ocid].get_obj(obj_index).time = object_types[type].lifetime; // maybe explode on collision
	}
	return (is_typed_pickup ? (int)obj_groups[ocid].get_obj(obj_index).direction : 0);
}

void freeze_player(player_state &sstate, float energy) {
	float const freeze_secs(0.4*sqrt(energy)), freeze_ticks(freeze_secs*TICKS_PER_SECOND);
	sstate.freeze_time = int(sqrt(freeze_ticks*freeze_ticks + sstate.freeze_time*sstate.freeze_time)); // stacks as sqrt()
}

string get_kill_str(int type) {
	if (type == FELL) {return "Teleporter";} // can only kill with teleporter, not fell
	if (type == COLLISION && !coll_damage_name.empty()) return coll_damage_name;
	return obj_type_names[type];
}

void camera_weapon_ammo_pickup(point const &position, colorRGBA &cam_filter_color) {
	cam_filter_color = BLUE;
	last_inventory_frame = frame_counter;
	gen_sound(SOUND_ITEM, position, 0.25);
}


bool camera_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (type == DROPLET && get_blood_mix(position) > 0.5) {blood_on_camera(1);}
	if (type == CAMERA || type == SMILEY || !camera_mode || spectate) return 1; // no collisions in these cases
	if (!game_mode && type != KEYCARD) return 1; // only keycards can be interacted with in non-gameplay mode
	if (!damage_done(type, obj_index)) return 1;
	if (camera_health < 0.0) return 1; // already dead
	int const source(get_damage_source(type, obj_index, CAMERA_ID));
	assert(source >= CAMERA_ID && source < num_smileys);
	if (source == CAMERA_ID && self_coll_invalid(type, obj_index)) return 0; // hit yourself
	int damage_type(0);
	player_state &sstate(sstates[CAMERA_ID]);
	colorRGBA cam_filter_color(RED);
	float const last_h(camera_health);
	int const wa_id(proc_coll_types(type, obj_index, energy));

	switch (type) {
	case POWERUP:
		sstate.powerup      = wa_id;
		sstate.powerup_time = POWERUP_TIME;
		cam_filter_color    = WHITE;
		print_text_onscreen(powerup_names[wa_id], GREEN, 1.0, 2*MESSAGE_TIME/3, 1);
		gen_sound(SOUND_POWERUP, position);
		break;

	case HEALTH:
		cam_filter_color = GREEN;
		print_text_onscreen("+50 Health", GREEN, 1.0, 2*MESSAGE_TIME/3, 1);
		gen_sound(SOUND_ITEM, position, 0.5);
		break;

	case SHIELD:
		cam_filter_color = YELLOW;
		print_text_onscreen("+100 Shields", GREEN, 1.0, 2*MESSAGE_TIME/3, 1);
		gen_sound(SOUND_ITEM, position, 0.5);
		break;

	case WEAPON:
		sstate.p_weapons[wa_id] = 1;
		sstate.p_ammo[wa_id]    = min(weapons[wa_id].max_ammo, sstate.p_ammo[wa_id]+weapons[wa_id].def_ammo);
		print_text_onscreen(weapons[wa_id].name, GREEN, 0.8, 2*MESSAGE_TIME/3, 1);
		camera_weapon_ammo_pickup(position, cam_filter_color);
		break;

	case AMMO:
		sstate.p_ammo[wa_id] = min(weapons[wa_id].max_ammo, sstate.p_ammo[wa_id]+weapons[wa_id].def_ammo);
		print_text_onscreen((std::to_string(weapons[wa_id].def_ammo) + " " + weapons[wa_id].name + " ammo"), GREEN, 0.8, 2*MESSAGE_TIME/3, 1);
		camera_weapon_ammo_pickup(position, cam_filter_color);
		break;

	case WA_PACK:
		{
			int const pickup_ammo((int)obj_groups[coll_id[type]].get_obj(obj_index).angle);
			sstate.p_weapons[wa_id] = 1;
			sstate.p_ammo[wa_id]    = min((int)weapons[wa_id].max_ammo, (sstate.p_ammo[wa_id] + pickup_ammo));
			print_text_onscreen((weapons[wa_id].name + " pack with ammo " + std::to_string(pickup_ammo)), GREEN, 0.8, 2*MESSAGE_TIME/3, 1);
			camera_weapon_ammo_pickup(position, cam_filter_color);
		}
		break;

	case KEYCARD:
		sstate.keycards.insert(wa_id);
		print_text_onscreen("You picked up a keycard", GREEN, 1.2, MESSAGE_TIME, 1);
		cam_filter_color = GREEN;
		gen_sound(SOUND_ITEM, position, 0.5);
		break;

	case BALL:
		if (energy < 10.0 && sstate.pickup_ball(obj_index)) {
			print_text_onscreen("You have the ball", GREEN, 1.2, 2*MESSAGE_TIME/3, 1);
			gen_sound(SOUND_POWERUP, position, 0.5);
			//last_inventory_frame = frame_counter; // acts more like a powerup than an inventory item
		}
		else {cam_filter_color = RED;}
		break;

	case LANDMINE:
		damage_type      = 1;
		cam_filter_color = RED;
		break;
	case SHRAPNEL:
		energy = get_shrapnel_damage(energy, obj_index);
		cam_filter_color = RED;
		break;
	case BLAST_RADIUS:
		if (br_source == LANDMINE) damage_type = 1;
		cam_filter_color = RED;
		break;
	case GASSED:
		print_text_onscreen("Poison Gas Detected", OLIVE, 1.2, MESSAGE_TIME, 1);
		cam_filter_color = ((frame_counter & 15) ? DK_RED : OLIVE);
		break;
	case IMPACT:
		if (sstate.freeze_time > 0) {energy *= 2.0;} // 2x impact damage when frozen
		break;
	default:
		cam_filter_color = RED;
	}
	if (!compute_damage(energy, type, obj_index, source, CAMERA_ID)) return 1;
	if (energy > 0.0 && camera_invincible) return 1;
	float const last_health(camera_health);
	camera_health -= HEALTH_PER_DAMAGE*energy;
	camera_health = min(camera_health, ((sstate.powerup == PU_REGEN) ? MAX_REGEN_HEALTH : max(last_h, MAX_HEALTH)));
	bool const is_blood(energy > 0.0 && !is_area_damage(type) && type != FREEZE_BOMB && (type != COLLISION || (rand()&31) == 0));
	player_coll(type, obj_index);
	point const camera(get_camera_pos());
	vector3d const coll_dir(get_norm_rand(vector3d(position, camera)));
	bool const burned(is_burned(type, br_source)), alive(camera_health >= 0.0);
	float const blood_v((energy > 0.0) ? (6.0 + 0.6*sqrt(energy)) : 0.0);
	
	if (is_blood && !disable_blood) {
		create_blood(0, (alive ? 30 : 1), camera, CAMERA_RADIUS, velocity, coll_dir, blood_v, damage_type, camera_health, burned, (sstate.freeze_time > 0));
	}
	if (burned) {sstate.freeze_time = 0;} // thaw
	else if (type == FREEZE_BOMB) {
		freeze_player(sstate, energy);
		print_text_onscreen("Frozen", RED, 1.2, MESSAGE_TIME, 3);
	}
	if (alive) {
		if (is_blood && cam_filter_color == RED) {
			if (sstate.drop_weapon(coll_dir, cview_dir, camera, CAMERA_ID, energy, type)) {
				print_text_onscreen("Oops, you dropped your weapon!", RED, 1.2, 2*MESSAGE_TIME/3, 3);
			}
			if (camera_shake == 0.0) camera_shake = 1.0;
		}
		if (energy > 1.0E-4 || cam_filter_color != RED) {
			cam_filter_color.alpha = 0.001*fabs(energy) + 0.25;
		}
		else {
			cam_filter_color.alpha = 0.0;
		}
		if (type == FELL) {gen_sound(SOUND_SQUISH, camera, 0.3);}
		
		if (type != CRUSHED && type != DROWNED && camera_health <= 20.0 && last_health > 20.0) {
			gen_sound(SOUND_SCARED, camera); // almost dead
		}
	}
	else { // dead
		camera_mode = !camera_mode;
		reset_camera_pos();
		sstate.powerup         = PU_NONE;
		sstate.powerup_time    = 0;
		camera_health          = 100.0;
		cam_filter_color.alpha = 1.0;
		remove_reset_coll_obj(camera_coll_id);
		gen_dead_smiley(source, CAMERA_ID, energy, camera, velocity, coll_dir, damage_type, camera_health, CAMERA_RADIUS, blood_v, burned, type);
		assert(obj_groups[coll_id[SFPART]].max_objects() > 0);
		obj_groups[coll_id[SFPART]].get_obj(0).flags |= CAMERA_VIEW; // camera follows the eye
		orig_cdir = cview_dir;
		
		if (source == CAMERA_ID) { // camera/player suicide
			cam_filter_color = BLACK;
			string str;

			switch (type) {
				case FELL:       str = "FELL";             break;
				case DROWNED:    str = "DROWNED";          break;
				case FIRE:       str = "SUICIDE by FIRE";  break;
				case BURNED:     str = "BURNED to DEATH";  break;
				case FROZEN:     str = "FROZE to DEATH";   break;
				case SUFFOCATED: str = "SUFFOCATED";       break;
				case CRUSHED:    str = "CRUSHED to DEATH"; break;
				case GASSED:     str = "Gassed to Death";  break;
				default:         str = string("SUICIDE with ") + get_kill_str(type);
			}
			print_text_onscreen(str, RED, 1.0, MESSAGE_TIME, 3);
			sstate.register_suicide();
		}
		else {
			string str;
			if (type == SMILEY) {str = string("Fragged by ") + sstates[source].name;}
			else if (type == FIRE || type == BURNED) {str = string("BURNED to DEATH by ") + sstates[source].name;}
			else {str = string("Fragged by ") + sstates[source].name + "'s " + get_weapon_qualifier(type, index, source) + get_kill_str(type);}

			if (same_team(source, CAMERA_ID)) {
				sstates[source].register_team_kill();
				print_text_onscreen(str, RED, 1.0, MESSAGE_TIME, 3); // killed by your teammate
				gen_delayed_sound(1.0, SOUND_DOH, get_sstate_pos(source));
			}
			else {
				sstates[source].register_kill();
				print_text_onscreen(str, ORANGE, 1.0, MESSAGE_TIME, 3); // killed by an enemy
			}
		}
		if (!same_team(CAMERA_ID, source) && obj_groups[coll_id[SMILEY]].is_enabled()) {
			update_kill_health(obj_groups[coll_id[SMILEY]].get_obj(source).health);
		}
		if (is_blood && !burned && !disable_blood) {blood_on_camera(rand()%10);}
		sstate.drop_pack(camera);
		remove_reset_coll_obj(camera_coll_id);
		init_sstate(CAMERA_ID, 0);
		sstate.register_death(source);
		camera_reset  = 0;
		b2down        = 0;
		following     = 0; // ???
		orig_camera   = camera_origin;
	}
	if (cam_filter_color.alpha > 0.0) {add_camera_filter(cam_filter_color, CAMERA_SPHERE_TIME, -1, CAM_FILT_DAMAGE);}
	return 1;
}


bool smiley_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (index == CAMERA_ID) {return camera_collision(type, obj_index, velocity, position, energy, type);}
	if (type  == CAMERA   ) {return camera_collision(type, index, velocity, position, energy, SMILEY);}
	if (type == KEYCARD) return 0; // smileys don't pick up keycards yet
	int damage_type(0), cid(coll_id[SMILEY]);
	assert(cid >= 0);
	assert(obj_groups[cid].enabled);
	assert(index >= 0 && index < num_smileys);
	if (!game_mode || type == SMILEY)  return 1;
	if (obj_groups[cid].get_obj(index).disabled() || obj_groups[cid].get_obj(index).health < 0.0) return 1;
	if (!damage_done(type, obj_index)) return 1;
	int const source(get_damage_source(type, obj_index, index));
	assert(source >= CAMERA_ID && source < num_smileys);
	if (source == index && self_coll_invalid(type, obj_index)) return 0; // hit itself
	player_coll(type, obj_index);
	int const wa_id(proc_coll_types(type, obj_index, energy));
	player_state &sstate(sstates[index]);

	switch (type) {
	case POWERUP:
		sstate.powerup      = wa_id;
		sstate.powerup_time = POWERUP_TIME;
		print_text_onscreen((sstates[index].name + " has " + powerup_names[wa_id]), get_smiley_team_color(index), 0.8, 2*MESSAGE_TIME, 0);
	case HEALTH:
		obj_groups[cid].get_td()->get_mesh(index).mult_by(0.4);
	case SHIELD:
		energy = 0.0;
		break;

	case WEAPON:
		sstate.p_weapons[wa_id] = 1;
		sstate.p_ammo[wa_id]    = min(weapons[wa_id].max_ammo, sstate.p_ammo[wa_id]+weapons[wa_id].def_ammo);
		if (sstate.weapon == W_BBBAT || sstate.weapon == W_SBALL || rand()%10 > 4) {
			sstate.weapon = wa_id; // switch weapons
		}
		break;

	case AMMO:
		sstate.p_ammo[wa_id] = min(weapons[wa_id].max_ammo, sstate.p_ammo[wa_id]+weapons[wa_id].def_ammo);
		if ((!weapons[wa_id].need_weapon || (sstate.p_weapons[wa_id] != 0 && sstate.p_ammo[wa_id] == 0)) &&
			(sstate.weapon == W_BBBAT || wa_id != W_SBALL) &&
		    (sstate.weapon == W_BBBAT || sstate.weapon == W_SBALL || rand()%10 > 5)) {
			sstate.weapon = wa_id; // switch weapons
		}
		break;

	case WA_PACK:
		sstate.p_weapons[wa_id] = 1;
		sstate.p_ammo[wa_id]    = min((int)weapons[wa_id].max_ammo,
			sstate.p_ammo[wa_id]+(int)obj_groups[coll_id[type]].get_obj(obj_index).angle);
		if ((sstate.weapon == W_BBBAT || wa_id != W_SBALL) &&
			(sstate.weapon == W_BBBAT || sstate.weapon == W_SBALL || rand()%10 > 6)) {
			sstate.weapon = wa_id; // switch weapons
		}
		break;

	case BALL:
		if (energy < 10.0) {sstate.pickup_ball(obj_index);}
		break;
	case LANDMINE:
		damage_type = 1;
		break;
	case SHRAPNEL:
		energy = get_shrapnel_damage(energy, obj_index);
		break;
	case BLAST_RADIUS:
		if (br_source == LANDMINE) {damage_type = 1;}
		break;
	case IMPACT:
		if (sstate.freeze_time > 0) {energy *= 2.0;} // 2x impact damage when frozen
		break;
	}
	if (!compute_damage(energy, type, obj_index, source, index)) return 1;
	dwobject &obji(obj_groups[cid].get_obj(index));
	obji.health = min((obji.health - HEALTH_PER_DAMAGE*energy), (sstate.powerup == PU_REGEN) ? MAX_REGEN_HEALTH : MAX_HEALTH);
	int const alive(obji.health >= 0.0);
	if (energy <= 0.0 && alive) return 1;
	bool const burned(is_burned(type, br_source));
	float const radius(object_types[SMILEY].radius);
	point const obj_pos(obji.pos);
	float blood_v(6.0 + 0.6*sqrt(energy));
	vector3d coll_dir(get_norm_rand(vector3d(position, obj_pos)));
	if (burned) {sstate.freeze_time = 0;} // thaw
	else if (type == FREEZE_BOMB) {freeze_player(sstate, energy);}

	if (alive) {
		if (type != FELL && type != CRUSHED && !is_area_damage(type)) {
			if (sstate.shields < 0.01) {
				float const damage_amt(max(2.0, min(8.0, 0.8*sqrt(energy))));
				add_damage_to_smiley(coll_dir, damage_amt, index, type);
			}
			if (energy > 0.1 && source != index && coll_dir != zero_vector && type != FIRE && type != BURNED &&
				type != ROCK && (type != SHRAPNEL || energy > 10.0*object_types[SHRAPNEL].damage))
			{
				sstate.was_hit = HIT_TIME;
				sstate.hitter  = source;
				sstate.hit_dir = coll_dir;
			}
		}
		sstate.drop_weapon(coll_dir, vector3d(sstate.target_pos, obj_pos), obj_pos, index, energy, type);
		blood_v  *= 0.5;
		coll_dir *= -4.0;
		if (type == FELL) {gen_sound(SOUND_SQUISH, obj_pos, 0.2);}
	}
	if (!burned && type != FREEZE_BOMB && !is_area_damage(type) && !disable_blood) {
		unsigned const blood_amt(create_blood(index+1, (alive ? 30 : 1), obj_pos, radius,
			velocity, coll_dir, blood_v, damage_type, obji.health, burned, (sstate.freeze_time > 0)));
		float const cdist(distance_to_camera(obj_pos));

		if (cdist < 4.0*radius && sphere_in_camera_view(obj_pos, radius, 2)) {
			int const nblood(max(1, int(((cdist + 0.1)/radius)*blood_amt*max(0.5f, min(1.0f, (-obji.health/50.0f + 0.2f)))/2.0f)));
			blood_on_camera(rand()%(nblood+1));
			gen_sound(SOUND_SPLAT1, get_camera_pos());
		}
	}
	if (alive) return 1;

	// dead
	sstate.powerup      = PU_NONE;
	sstate.powerup_time = 0;
	gen_dead_smiley(source, index, energy, obj_pos, velocity, coll_dir, damage_type, obji.health, radius, blood_v, burned, type);
	player_state &ssource(sstates[source]);

	if (source == CAMERA_ID) { // camera/player
		if (camera_mode == 1 && camera_surf_collide) { // playing
			int type2(type);
			int const weapon((type == BLAST_RADIUS ? br_source : ssource.weapon));
			bool const head_shot(type != BLAST_RADIUS && type != FIRE && type != LANDMINE && type != TELEPORTER && type != TELEFRAG &&
				type != FELL && type != CRUSHED && type != XLOCATOR_DEATH && !is_area_damage(type));
			if (game_mode == GAME_MODE_DODGEBALL && type == CAMERA) {type2 = BALL;} // dodgeball
			string const str(string("You fragged ") + sstates[index].name + " with " + get_weapon_qualifier(type, weapon, source) +
				get_kill_str(type2) + (head_shot ? "\nHead Shot" : ""));

			if (same_team(source, index)) { // player killed a teammate
				print_text_onscreen(str, RED, 1.0, MESSAGE_TIME, 2);
				sstates[source].register_team_kill();
				gen_delayed_sound(1.0, SOUND_DOH, get_camera_pos());
			}
			else { // player killed an enemy
				print_text_onscreen(str, MAGENTA, 1.0, MESSAGE_TIME, 2);
				sstates[source].register_kill();
				update_kill_health(camera_health);
			}
		}
	}
	else if (source == index) { // suicide
		assert(source < num_smileys);
		string str(sstates[source].name);

		switch (type) {
		case DROWNED:    str += " Drowned";              break;
		case BURNED:     str += " Burned to Death";      break;
		case FROZEN:     str += " Froze to Death";       break;
		case FELL:       str += " Fell to his Death";    break;
		case SUFFOCATED: str += " Suffocated";           break;
		case CRUSHED:    str += " was Crushed to Death"; break;
		case GASSED:     str += " was Gassed to Death";  break;
		default:         str += " suicided with " + get_weapon_qualifier(type, (type == BLAST_RADIUS ? br_source : ssource.weapon), source) + get_kill_str(type);
		}
		print_text_onscreen(str, CYAN, 1.0, MESSAGE_TIME/2, 0);
		ssource.register_suicide();
	}
	else {
		assert(source < num_smileys && index < num_smileys);
		string const str(sstates[index].name + " was fragged by " + sstates[source].name + "'s " +
			get_weapon_qualifier(type, (type == BLAST_RADIUS ? br_source : ssource.weapon), source) + get_kill_str(type));
		
		if (same_team(source, index)) { // killed a teammate
			print_text_onscreen(str, PINK, 1.0, MESSAGE_TIME/2, 0);
			ssource.register_team_kill();
			gen_delayed_sound(1.0, SOUND_DOH, get_sstate_pos(source), 0.7);
		}
		else { // killed an enemy
			print_text_onscreen(str, YELLOW, 1.0, MESSAGE_TIME/2, 0);
			//if (free_for_all)
			ssource.register_kill();
		}
		if (!same_team(index, source)) {update_kill_health(obj_groups[coll_id[SMILEY]].get_obj(source).health);}
	}
	sstate.drop_pack(obj_pos);
	remove_reset_coll_obj(obji.coll_id);
	sstate.register_death(source);
	obji.status = 0;
	if (game_mode != GAME_MODE_DODGEBALL) {gen_smoke(position);}
	return 1;
}


int get_smiley_hit(vector3d &hdir, int index) {

	assert(index < num_smileys);
	if (sstates[index].was_hit == 0) return 0;
	hdir = sstates[index].hit_dir;
	return sstates[index].was_hit;
}


string get_weapon_qualifier(int type, int index, int source) {

	bool const qd(sstates[source].powerup == PU_DAMAGE); // quad damage
	if (index == CAMERA_ID) {index = sstates[CAMERA_ID].weapon;} // camera weapon
	string qstr;
	
	if ((type == IMPACT && index != IMPACT) || (type == PROJECTILE && index != PROJECTILE)) {
		assert(index >= 0 && index < NUM_WEAPONS);
		qstr = weapons[index].name + " ";
	}
	else if (type == BLAST_RADIUS && index != BLAST_RADIUS) {
		assert(index >= 0 && index < NUM_TOT_OBJS);
		qstr = obj_type_names[index] + " ";
	}
	if (qd) return string("quad damage ") + qstr;
	return qstr;
}


bool dwobject::lm_coll_invalid() const {return (time < (int)LM_ACT_TIME);}

int damage_done(int type, int index) {

	int const cid(coll_id[type]);

	if (cid >= 0 && index >= 0 && !is_pickup_item(type) && obj_groups[cid].is_enabled()) { // skip pickup items because they're not object-dependent
		dwobject &obj(obj_groups[cid].get_obj(index));
		if ((obj.flags & (WAS_PUSHED | FLOATING)) && (type != BALL || game_mode != GAME_MODE_DODGEBALL)) return 0; // floating on water or pushed after stopping, can no longer damage

		if (type == LANDMINE) {
			if (obj.status == 1 || obj.lm_coll_invalid()) return 0; // not activated
			obj.status = 0;
			return 1;
		}
	}
	return damage_done_obj[type];
}


void gen_blood_velocity(vector3d &vout, vector3d const &velocity, vector3d const &coll_dir, float blood_v, float md, float mv, int type, float health) {

	assert(!is_nan(coll_dir));
	float const hv(max(0.7, min(1.1, (-health/40.0 + 0.25)))), mag(rand_uniform(0.5*blood_v, blood_v));
	vout   = gen_rand_vector(mag, 2.0, 0.52*PI);
	vout.z = fabs(vout.z); // always up

	for (unsigned i = 0; i < 3; ++i) {
		vout[i] = hv*(-md*blood_v*coll_dir[i] + mv*velocity[i] + vout[i]);
		if (type == 1 && i < 2) vout[i] *= 0.2; // x and y
		assert(isfinite(vout[i]));
	}
}


void gen_rocket_smoke(point const &pos, vector3d const &orient, float radius, bool freeze) { // rocket, seekd, and raptor

	if (!animate2) return;
	point const dpos(pos + (3.0*radius)*orient.get_norm());
	
	if (distance_to_camera_sq(pos) > 0.04 && iticks > rand()%3) {
		if (freeze) {gen_arb_smoke(pos, FREEZE_COLOR, vector3d(0,0,0.05), rand_uniform(0.01, 0.025), 0.5, 0.0, 0.0, NO_SOURCE, SMOKE, 0, 0.5, 1);} // blue smoke, no lighting
		else {
			gen_smoke(dpos, 0.2, 1.0);
			gen_arb_smoke(dpos, WHITE, vector3d(0.0, 0.0, 0.02), rand_uniform(0.01, 0.025), rand_uniform(0.7, 0.9),
				rand_uniform(0.0, 0.2), 0.0, NO_SOURCE, SMOKE, 0, 1.0, 0, 0.1); // contrail, tsfact=0.1
		}
	}
	//if (freeze) {add_blastr(dpos, orient, 4.0*radius, 0.0, 4, NO_SOURCE, FREEZE_COLOR, colorRGBA(0,0,0.5,0), ETYPE_FUSION);}
	if (freeze) {add_blastr(dpos, orient, 3.5*radius, 0.0, 4, NO_SOURCE, WHITE, colorRGBA(0,0,0.2,0), ETYPE_PC_ICE);}
	else        {add_blastr(dpos, orient, 3.0*radius, 0.0, 4, NO_SOURCE, WHITE, colorRGBA(0.2,0,0,0), ETYPE_PART_CLOUD);}
	//else {add_blastr(pos, orient, 2.0*radius, 0.0, 4, NO_SOURCE, YELLOW, RED, ETYPE_ANIM_FIRE);}
}


void gen_landmine_scorch(point const &pos) {

	float const o_radius(object_types[LANDMINE].radius);
	point coll_pos;
	vector3d coll_norm;
	int cindex(-1);
	
	if (check_coll_line_exact(pos, (pos - vector3d(0.0, 0.0, 1.2*o_radius)), coll_pos, coll_norm, cindex, 0.0, -1, 0, 0, 1, 0) && coll_norm == plus_z) { // no voxels
		gen_explosion_decal(point(pos.x, pos.y, coll_objects.get_cobj(cindex).d[2][1]), o_radius, coll_norm, coll_objects[cindex], 2); // top of cube
	}
}


bool default_obj_coll(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type, int cobj_type) {

	dwobject &obj(obj_groups[coll_id[cobj_type]].get_obj(index));
	bool valid_coll(1);

	if (type == CAMERA || type == SMILEY) {
		if (cobj_type == S_BALL) return 1;
		energy = get_coll_energy(zero_vector, obj.velocity, object_types[obj.type].mass);
		valid_coll = smiley_collision(((type == CAMERA) ? CAMERA_ID : obj_index), index, velocity, position, energy, cobj_type);
		if (valid_coll && cobj_type != MAT_SPHERE) {obj.disable();} // return?
	}
	if (type != LASER && type != BEAM && type != PARTICLE) {obj.elastic_collision(position, energy, type);} // partially elastic collision
	return valid_coll;
}


bool landmine_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (type != CAMERA && type != SMILEY) return 1;
	if (type == CAMERA) obj_index = CAMERA_ID; else assert(obj_index < num_smileys);
	dwobject &obj(obj_groups[coll_id[LANDMINE]].get_obj(index));
	point const pos(get_sstate_pos(obj_index));

	if (sstates[obj_index].powerup == PU_FLIGHT && pos.z > object_types[SMILEY].radius + int_mesh_zval_pt_off(pos, 1, 0)) {
		return 0; // don't run into landmine when flying above it
	}
	if (obj_index == get_damage_source(LANDMINE, index, obj_index) && obj.time < (int)SMILEY_LM_ACT_TIME) {
		return 0; // camera/smiley ran into his own landmine
	}
	//if (!smiley_collision(obj_index, index, velocity, obj.pos, energy, LANDMINE)) return 0;
	blast_radius(obj.pos, LANDMINE, index, obj.source, 0);
	gen_smoke(obj.pos);
	gen_landmine_scorch(obj.pos);
	obj.status = 0;
	return 1;
}


bool pushable_collision(int index, point const &position, float force, int type, int obj_type) { // Note: return value is *not* valid_coll

	if (!animate2) return 1; // pushed, but no effect

	if (type == CAMERA || type == SMILEY) {
		dwobject &obj(obj_groups[coll_id[obj_type]].get_obj(index));

		if (obj.status != 1 && obj.status != 2) { // only if on ground or stopped
			// Note: an object resting on a destroyable static object will still have status 1 and will not be pushable
			if (obj.status == 4) {obj.flags |= WAS_PUSHED;}
			point pos(position);

			if (obj.flags & IS_CUBE_FLAG) {
				vector3d const dir(pos - obj.pos);
				if (dir == zero_vector) return 0; // no movement
				int const dim(get_max_dim(dir));
				if (dim == 2) return 0; // don't push in z
				pos = obj.pos;
				pos[dim] += dir.get_norm()[dim]*dir.mag(); // make push dir on cube side using ortho vector
				force *= PI/6.0; // less force for larger object volume (cube vs. sphere)
			}
			obj.elastic_collision(pos, force, type); // add some extra energy so that we can push the object
			return 1;
		}
	}
	return 0;
}

// something runs into a dodgeball
bool dodgeball_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (type == CAMERA || type == SMILEY) {
		if (pushable_collision(index, position, 100.0, type, BALL)) {
			if (game_mode != GAME_MODE_DODGEBALL) return 1; // doesn't seem to help
			energy = 0.0;
		}
		return smiley_collision(((type == CAMERA) ? CAMERA_ID : obj_index), index, velocity, position, energy, BALL);
	}
	return default_obj_coll(index, obj_index, velocity, position, energy, type, BALL);
}

bool mat_sphere_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	if (pushable_collision(index, position, 30000.0, type, MAT_SPHERE)) return 1;
	return default_obj_coll(index, obj_index, velocity, position, energy, type, MAT_SPHERE);
}

bool skull_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	pushable_collision(index, position, 2000.0, type, SKULL);
	return 1;
}

bool translocator_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (type == CAMERA || type == SMILEY) {
		int const player_id((type == CAMERA) ? CAMERA_ID : obj_index);
		dwobject const &obj(obj_groups[coll_id[XLOCATOR]].get_obj(obj_index));
		
		if (obj.enabled() && obj.source == player_id) {
			bool const picked_up(pickup_player_translator(player_id));
			assert(picked_up);
			return 1;
		}
	}
	pushable_collision(index, position, 20000.0, type, XLOCATOR);
	return 1;
}

bool keycard_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	if (type == CAMERA) {return camera_collision(KEYCARD, index, velocity, position, energy, KEYCARD);} // only the player can pick up a keycard
	return default_obj_coll(index, obj_index, velocity, position, energy, type, KEYCARD);
}

bool health_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	return default_obj_coll(index, obj_index, velocity, position, energy, type, HEALTH);
}

bool shield_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	return default_obj_coll(index, obj_index, velocity, position, energy, type, SHIELD);
}

bool powerup_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	return default_obj_coll(index, obj_index, velocity, position, energy, type, POWERUP);
}

bool weapon_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	return default_obj_coll(index, obj_index, velocity, position, energy, type, WEAPON);
}

bool ammo_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	return default_obj_coll(index, obj_index, velocity, position, energy, type, AMMO);
}

bool pack_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	return default_obj_coll(index, obj_index, velocity, position, energy, type, WA_PACK);
}

bool sball_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {
	if (!default_obj_coll(index, obj_index, velocity, position, energy, type, S_BALL)) return 0;
	pushable_collision(index, position, 20.0, type, S_BALL);
	return 1;
}

bool rock_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (type != SEEK_D && type != ROCKET && type != IMPACT) return 1;
	float num(rand_uniform(0.0, 6.0));
	if (index == 0)          num *= 2.0; // large rock
	if      (type == SEEK_D) num *= 2.0;
	else if (type == ROCKET) num *= 1.5;
	int const shooter(get_damage_source(type, obj_index, index));
	float const p[7] = {2.5, 5.0, 4.0, 0.2, 1.0, 0.5, 0.5};
	gen_rubble(ROCK, int(num), position, shooter, p);
	return 1;
}

bool sawblade_collision(int index, int obj_index, vector3d const &velocity, point const &position, float energy, int type) {

	if (type == CAMERA || type == SMILEY) {
		if (obj_groups[coll_id[SAWBLADE]].get_obj(index).source == ((type == CAMERA) ? CAMERA_ID : obj_index)) { // self collision
			// pick up ammo, no damage?
		}
	}
	if (!default_obj_coll(index, obj_index, velocity, position, energy, type, SAWBLADE)) return 0;
	if (type == CAMERA || type == SMILEY) {obj_groups[coll_id[SAWBLADE]].get_obj(index).direction = 1;} // flag as bloody
	return 1;
}


// ***********************************
// WEAPON/EFFECTS CODE
// ***********************************


void gen_rubble(int type, int num, point const &pos, int shooter, float const p[7]) {

	obj_group &objg(obj_groups[coll_id[type]]);

	for (int o = 0; o < num; ++o) {
		int const i(objg.choose_object());
		objg.create_object_at(i, pos);
		dwobject &obj(objg.get_obj(i));
		obj.init_dir      = signed_rand_vector_norm();
		obj.velocity      = gen_rand_vector(rand_uniform(p[0], p[1]), p[2], PI_TWO);
		obj.orientation.x = p[3] + p[4]*rand_float(); // size
		obj.orientation.y = p[5] + p[6]*rand_float(); // color
		obj.time          = int(0.5*rand_float()*(float)object_types[type].lifetime);
		obj.source        = shooter;
	}
}


void create_ground_rubble(point pos, int shooter, float hv, float close, int calc_hv) {

	int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));
	bool const outside(point_outside_mesh(xpos, ypos));
	if ((is_in_ice(xpos, ypos) && is_underwater(pos)) || is_mesh_disabled(xpos, ypos)) return;
	if (!outside && wminside[ypos][xpos] && (water_matrix[ypos][xpos] - mesh_height[ypos][xpos]) > 0.25f*MAX_SPLASH_DEPTH) return;
	
	if (calc_hv) {
		hv = (outside ? 0.5 : get_rel_height(mesh_height[ypos][xpos], zmin, zmax));
	}
	if (hv < 0.57) { // dirt
		int const num(int(close*(1.0f - 3.0f*fabs(hv - 0.3f))*(75 + rand()%150)));
		float const params[7] = {3.5, 5.5, 5.0, 0.3, 0.7, 0.6, 0.4};
		gen_rubble(DIRT, num, pos, shooter, params); // or SAND
	}
	if (hv > 0.5) { // rocks
		int const num(int(close*(3.0f*min(0.3f, (hv - 0.5f)) + 0.1f)*(30 + rand()%60)));
		float const params[7] = {3.0, 6.0, 4.5, 0.2, 1.0, 0.5, 0.5};
		gen_rubble(ROCK, num, pos, shooter, params);
	}
}


void dwobject::update_vel_from_damage(vector3d const &dv) {

	velocity += dv*min(1.0, 2.0*object_types[type].terminal_vel/dv.mag()); // 2x terminal velocity
}


void dwobject::damage_object(float damage, point const &dpos, point const &shoot_pos, int weapon) {

	point const pos0((weapon == W_BLADE) ? shoot_pos : dpos);
	float const damage2(0.1f*damage/max(1.0E-6f, (p2p_dist(pos, pos0)*sqrt(object_types[type].mass)))); // careful of divide by zero
	health -= HEALTH_PER_DAMAGE*damage;
	flags  &= ~STATIC_COBJ_COLL;

	if (health >= 0 && weapon != W_LASER) { // still alive, send it flying
		status = 1;
		if (type != LANDMINE) flags &= ~ALL_COLL_STOPPED;
		update_vel_from_damage((pos - pos0)*((weapon == W_BLADE) ? -1.6*damage2 : damage2)); // W_BLADE : pull object towards you
	}
}


void blast_radius(point const &pos, int type, int obj_index, int shooter, int chain_level) {

	point const temp1(camera_origin);
	vector3d const temp2(cview_dir);
	if (!game_mode) return;
	int const wtype(obj_weapons[type]);
	if (wtype < 0)  return;
	assert(type >= 0);
	if (BLAST_CHAIN_DELAY > 0 && type != FREEZE_BOMB) {gen_smoke(pos);}
	assert(wtype <= NUM_WEAPONS);
	float damage(weapons[wtype].blast_damage), size(weapons[wtype].blast_radius);
	dwobject const &obj(obj_groups[coll_id[type]].get_obj(obj_index));

	if (type == PLASMA) {
		float const psize(obj.init_dir.x);
		assert(psize >= 0.0);
		damage *= psize*psize;
		size   *= psize*psize;
	}
	if (following) {
		camera_origin = orig_camera;
		cview_dir     = orig_cdir;
	}
	if (type != IMPACT && type < NUM_TOT_OBJS && coll_id[type] > 0) shooter = obj.source;
	damage *= sstates[shooter].get_damage_scale();
	bool const cview((obj.flags & CAMERA_VIEW) != 0);
	create_explosion(pos, shooter, chain_level, damage, size, type, cview);

	if (following) {
		camera_origin = temp1;
		cview_dir     = temp2;
	}
}


// returns true if there are no objects blocking the explosion
bool check_explosion_damage(point const &p1, point const &p2, int cobj) {

	int cindex;
	if (line_intersect_mesh(p1, p2)) return 0;
	if (!check_coll_line(p1, p2, cindex, cobj, 1, 0)) return 1;
	return (coll_objects.get_cobj(cindex).destroy >= SHATTERABLE); // blocked by a non destroyable static object
}


void exp_damage_groups(point const &pos, int shooter, int chain_level, float damage, float size, int type, bool cview) { // slow

	float dist(distance_to_camera(pos));

	if (!spectate && dist <= size && (type != IMPACT || shooter != CAMERA_ID) && (type != SEEK_D || !cview)) {
		if (check_explosion_damage(pos, get_camera_pos(), camera_coll_id)) {
			br_source = type;
			camera_collision(type, shooter, zero_vector, pos, damage*(1.02 - dist/size), BLAST_RADIUS);
		}
	}
	for (int g = 0; g < num_groups; ++g) { // apply blast radius damage to objects
		obj_group &objg(obj_groups[g]);
		if (!objg.enabled) continue;
		int type2(objg.type);
		bool const large_obj(objg.large_radius());

		if (objg.flags & PRECIPITATION) { // rain isn't affected
			if (temperature <= RAIN_MIN_TEMP) type2 = PRECIP; else continue;
		}
		if (type2 == CAMERA || type2 == PLASMA || type2 == BLOOD || type2 == CHARRED) continue;
		assert(object_types[type2].mass > 0.0);
		bool const can_move(object_types[type2].friction_factor < 3.0*STICK_THRESHOLD);
		float const dscale(0.1/sqrt(object_types[type2].mass));
		unsigned const nobj(objg.end_id);
		assert(nobj <= objg.max_objects());

		for (unsigned i = 0; i < nobj; ++i) {
			if (!objg.obj_within_dist(i, pos, size)) continue; // size+radius?
			dwobject &obj(objg.get_obj(i));
			if (large_obj && !check_explosion_damage(pos, obj.pos, obj.coll_id)) continue; // blocked by an object
			float const damage2(damage*(1.02 - p2p_dist(obj.pos, pos)/size));
			
			if (type2 == SMILEY && (type != IMPACT || shooter != (int)i)) {
				br_source = type;
				smiley_collision(i, shooter, zero_vector, pos, damage2, BLAST_RADIUS);
			}
			else {
				obj.health -= HEALTH_PER_DAMAGE*damage2;
				
				if (can_move) {
					obj.flags &= ~STATIC_COBJ_COLL;

					if (obj.health >= 0.0 || (BLAST_CHAIN_DELAY > 0 && chain_level > 0)) {
						if (temperature > W_FREEZE_POINT || !(obj.flags & IN_WATER)) { // not stuck in ice
							obj.status = 1;
							obj.flags &= ~ALL_COLL_STOPPED;
							obj.update_vel_from_damage((obj.pos - pos)*(damage2*dscale)); // similar to damage_object()
						}
					}
				}
				if (obj.health < 0.0) {
					if ((object_types[type2].flags & OBJ_EXPLODES) && (type2 != LANDMINE || !obj.lm_coll_invalid())) {
						if (BLAST_CHAIN_DELAY == 0) {
							obj.status = 0;
							blast_radius(obj.pos, type2, i, obj.source, chain_level+1);
							if (type2 != FREEZE_BOMB) {gen_smoke(obj.pos);}
						}
						else {
							obj.health = object_types[type2].health; // ???
							obj.time   = max(obj.time, object_types[type2].lifetime-((int)BLAST_CHAIN_DELAY)*(chain_level+1));
						}
					}
					else {
						if (type2 == PLASMA) gen_fire(obj.pos, obj.init_dir.x, obj.source);
						obj.status = 0;
					}
				} // health test
				else if (type2 == LEAF) {
					obj.vdeform  *= max(0.2, (1.0 - damage2/1000.0));
				}
				else if (type2 == FRAGMENT) {
					obj.init_dir *= max(0.2, (1.0 - damage2/2000.0));
				}
			} // SMILEY test
		} // for i
	} // for g
}


void create_explosion(point const &pos, int shooter, int chain_level, float damage, float size, int type, bool cview) {

	assert(damage >= 0.0 && size >= 0.0);
	assert(type != SMILEY);
	if (!game_mode || damage < TOLERANCE || size < TOLERANCE) return;
	//RESET_TIME;
	int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));
	float bradius(0.0), depth(0.0);
	bool const underwater(is_underwater((pos + vector3d(0.0, 0.0, -0.5*size)), 0, &depth));

	if (underwater) {
		depth = min(0.25f, max(depth, 0.01f));
		assert(damage >= 0.0);
		add_splash(pos, xpos, ypos, 0.002*damage/depth, (0.4 + 2.0*depth)*size, 1);
	}
	if (type == FREEZE_BOMB) {
		bradius = 1.2*size;
		add_blastr(pos, (pos - get_camera_pos()), bradius, 0.0, int(2.0*BLAST_TIME), shooter, LT_BLUE, DK_BLUE, ETYPE_STARB, nullptr, 1, 0.5); // no damage, half size sphere
		gen_delayed_from_player_sound(SOUND_ICE_CRACK, pos, 1.0);
		//add_water_particles(pos, vector3d(0.0, 0.0, 10.0), 1.0, 0.5*bradius, 0.0, 0.0, rand_uniform(50, 100)); // doesn't alpha blend properly with explosion
		colorRGBA const color(0.75, 0.75, 1.0, 1.0); // white-ice
		//spraypaint_tree_leaves(pos, 1.0*bradius, color); // ineffective, since leaves tend to be green and don't tint to blue

		if (pos.z < interpolate_mesh_zval(pos.x, pos.y, 0.0, 1, 1) + 0.25*bradius) { // near mesh surface
			if (display_mode & 0x01) {add_color_to_landscape_texture(color, pos.x, pos.y, 0.3*bradius);}
			if (display_mode & 0x02) {modify_grass_at(pos, 1.0*bradius, 0, 0, 0, 1, 1, 0, color);}
		}
		return; // no other effects
	}
	else if (type == GRENADE || type == CGRENADE) {
		bradius = 0.9*size;
		add_blastr(pos, (pos - get_camera_pos()), bradius, damage, int(1.5*BLAST_TIME), shooter, LT_YELLOW, RED, ETYPE_STARB, nullptr, 1, ((type == CGRENADE) ? 1.0 : 0.0));
	}
	else {
		bradius = 0.7*size;
		int const time(((type == BLAST_RADIUS) ? 2 : 1)*BLAST_TIME);
		bool const create_exp_sphere(is_rocket_type(type) || type == LANDMINE);
		add_blastr(pos, signed_rand_vector_norm(), bradius, damage, int(2.3*time), shooter, LT_YELLOW, RED, ETYPE_ANIM_FIRE, nullptr, 1, (create_exp_sphere ? 1.0 : 0.0));
		//add_blastr(pos, signed_rand_vector_norm(), bradius, damage, time, shooter, LT_YELLOW, RED, ETYPE_FIRE, nullptr, 1, (create_exp_sphere ? 1.0 : 0.0));
	}
	//exp_cobjs.push_back(add_coll_sphere(pos, size, cobj_params(0.0, WHITE, 0, 1, explosion_coll, exp_cobjs.size()))); // cobj for next frame
	exp_damage_groups(pos, shooter, chain_level, damage, size, type, cview);
	exp_damage_trees(pos, damage, bradius, type);
	if (world_mode == WMODE_INF_TERRAIN) {destroy_city_in_radius(pos, 0.5*bradius);}

	if (damage > 500.0 || is_rocket_type(type)) { // everything except for plasma
		gen_delayed_from_player_sound((underwater? (unsigned)SOUND_SPLASH2 : (unsigned)SOUND_EXPLODE), pos, min(1.5, max(0.5, damage/1000.0)));
		float const blast_force(size/distance_to_camera(pos));
		if (!underwater && blast_force > 0.5) {camera_shake = min(1.0, 2.0*blast_force);}
	}
	if (type == GRENADE) { // shrapnel fragments
		unsigned const num(weapons[W_GRENADE].nfragments + rand()%(weapons[W_GRENADE].nfragments/4));
		obj_group &objg(obj_groups[coll_id[SHRAPNEL]]);

		for (unsigned o = 0; o < num; ++o) {
			int const i(objg.choose_object());
			objg.create_object_at(i, pos);
			dwobject &obj(objg.get_obj(i));
			obj.velocity    = gen_rand_vector(rand_uniform(6.0, 12.0), 2.5, 0.75*PI);
			obj.orientation = signed_rand_vector_norm();
			obj.angle       = 360.0*rand_float();
			obj.source      = shooter;
			obj.direction   = W_GRENADE;
		}
	}
	else if (type == CGRENADE) { // grenades
		unsigned const num(weapons[W_CGRENADE].nfragments);
		obj_group &objg(obj_groups[coll_id[GRENADE]]);

		for (unsigned o = 0; o < num; ++o) { // less efficient
			int const i(objg.choose_object());
			objg.create_object_at(i, pos);
			dwobject &obj(objg.get_obj(i));
			// we stagger the grenade lifetimes so they explode on consecutive frames, except when using the voxel landscape where that causes too many updates
			obj.time     = (create_voxel_landscape ? 0 : o);
			obj.velocity = gen_rand_vector(rand_uniform(8.0, 10.0), 2.0, PI_TWO);
			obj.init_dir = signed_rand_vector_norm();
			obj.source   = shooter;
		}
	}
	if (size > 0.3) {
		float const search_radius(0.25*bradius);
		cube_t bcube(pos, pos);
		bcube.expand_by(search_radius);
		vector<unsigned> &cobjs(coll_objects.get_temp_cobjs());
		get_intersecting_cobjs_tree(bcube, cobjs, -1, 0.0, 0, 0, -1); // get candidates
		// not entirely correct, since this ignores non-intersecting cobjs (bcube intersects but sphere does not)
		unsigned const max_parts(cobjs.empty() ? 0 : unsigned(10000*size/cobjs.size()));
		bool const emissive(type == PLASMA);

		for (auto i = cobjs.begin(); i != cobjs.end(); ++i) { // find closest cobj(s), use normal and color
			coll_obj const &cobj(coll_objects.get_cobj(*i));
			vector3d normal(plus_z);
			point cpos; // unused
			if (!cobj.sphere_intersects_exact(pos, search_radius, normal, cpos)) continue;
			//if (!cobj.sphere_intersects(pos, search_radius)) continue;
			//vector3d normal((plus_z + (pos - cobj.get_center_pt()).get_norm()).get_norm()); // too inexact
			colorRGBA color((emissive ? YELLOW : cobj.get_avg_color()), 1.0); // ignore texture since this is an area effect; alpha is always 1.0
			add_explosion_particles(pos, 10.0*normal, 5.0, 0.25*bradius, color, (rand() % max_parts), emissive);

			if (cobj.is_movable() && cobj.destroy <= destroy_thresh) {
				float const dist(p2p_dist(cobj.get_cube_center(), pos)), move_radius(0.5*bradius);

				if (dist > 1.0E-6 && dist < move_radius) {
					vector3d delta(-2.0E-4*size*normal*((move_radius - dist)/move_radius)/cobj.get_group_mass());
					push_movable_cobj(*i, delta, pos);
				}
			}
		}
	}
	if (size > 0.2) {
		unsigned const max_parts((type == PLASMA) ? 250 : 50), num_parts(rand() % int(max_parts*size));
		gen_particles(pos, num_parts);
	}
	// large damage - throws up dirt and makes craters (later destroys trees)
	if ((type == IMPACT || damage > 1000.0) && is_over_mesh(pos) && !point_outside_mesh(xpos, ypos)) {
		float const zval(interpolate_mesh_zval(pos.x, pos.y, 0.0, 0, 1));
		float const damage2(5.0E-6*Z_SCENE_SIZE*crater_depth*damage*(256.0/(float)XY_SUM_SIZE)), crater_dist(0.36*crater_radius*size);

		if (fabs(zval - pos.z) < crater_dist) { // on/close to ground
			int const crater(damage >= 1000.0 && point_interior_to_mesh(xpos, ypos) && type != PLASMA);
			float hv(0.5), close(1.0);

			if (crater) {
				close = 1.1 - fabs(zval - pos.z)/crater_dist;
				size *= close*sqrt(XY_SCENE_SIZE);
				hv    = add_crater_to_landscape_texture(pos.x, pos.y, size);
				update_mesh_height(xpos, ypos, int(crater_dist/HALF_DXY), damage2, 0.0, 0, (crater_radius*crater_depth > 0.25f));
			}
			if ((h_collision_matrix[ypos][xpos] - mesh_height[ypos][xpos]) < SMALL_NUMBER) {
				create_ground_rubble(pos, shooter, hv, close, !crater);
			}
		}
	}
	if (damage > 100.0 && destroy_thresh <= 1) {
		bool const big(type == BLAST_RADIUS);
		destroy_coll_objs(pos, damage, shooter, type);
		float const radius((big ? 4.0 : 1.0)*sqrt(damage)/650.0); // same as in destroy_coll_objs()
		unsigned const num_fragments((big ? 2 : 1)*(10 + rand()%10)); // 20-40
		update_voxel_sphere_region(pos, radius, -0.5, shooter, num_fragments);
	}
	//PRINT_TIME("Blast Radius");
}


void do_impact_damage(point const &fpos, vector3d const &dir, vector3d const &velocity, point const &shoot_pos, float radius, int shooter, int weapon, float mag) {

	float damage(mag*weapons[weapon].blast_damage), sound_gain(0.0);
	bool create_sound(1);

	if (weapon == W_BLADE) {
		int const ammo(UNLIMITED_WEAPONS ? 1 : sstates[shooter].p_ammo[weapon]);
		damage *= sqrt((double)ammo + 1.0)/SQRT2;
		create_sound = ((rand()&7) == 0);
	}
	point pos(fpos + dir*(1.25*radius));
	float const coll_radius(0.75*radius);

	for (int g = 0; g < num_groups; ++g) {
		obj_group &objg(obj_groups[g]);
		if (!objg.enabled || (!objg.large_radius() && (objg.type != FRAGMENT || weapon != W_BLADE))) continue;
		int const type(objg.type);
		float const robj(object_types[type].radius), rad(coll_radius + robj);
		
		for (unsigned i = 0; i < objg.end_id; ++i) {
			if (type == SMILEY && (int)i == shooter) continue; // this is the shooter
			if (!objg.obj_within_dist(i, pos, rad))  continue;

			if (type == SMILEY) {
				if (weapon == W_BLADE) {++sstates[shooter].cb_hurt;}
				if (weapon == W_BBBAT && shooter == CAMERA_ID) {update_player_bbb_texture(0.05, 1);}
				smiley_collision(i, shooter, velocity, pos, damage, IMPACT);
				sound_gain = 0.7;
			}
			else if (type == FRAGMENT) {
				objg.get_obj(i).status = 0; // just destroy the fragment
			}
			else if (type != CAMERA) {
				objg.get_obj(i).damage_object(damage, pos, shoot_pos, weapon);
			}
		}
	}
	if (shooter >= 0 && !spectate) {
		point const camera(get_camera_pos());

		if (dist_less_than(pos, camera, (coll_radius + CAMERA_RADIUS))) {
			camera_collision(weapon, shooter, velocity, pos, damage, IMPACT);
			sound_gain = 1.0;
		}
	}
	if (create_sound && sound_gain > 0.0) {
		gen_sound(((weapon == W_BBBAT) ? (int)SOUND_HURT : (int)SOUND_SQUISH2), pos, sound_gain);
	}
	damage *= sstates[shooter].get_damage_scale();
	if (weapon != W_BLADE || rand()%20 == 0) {do_rock_damage(pos, radius, damage);}

	if (weapon == W_BLADE && destroy_thresh <= 1 && sstates[shooter].fire_frame > 0 && (rand()&7) == 0) {
		destroy_coll_objs(pos, 18.0*damage, shooter, IMPACT);
		update_voxel_sphere_region(pos, radius, -0.05, shooter, (rand()%3));
	}
	if (weapon == W_BBBAT && sstates[shooter].fire_frame > 0) {
		destroy_coll_objs(pos, 0.5*damage, shooter, IMPACT);
	}
	destroy_city_in_radius(pos, radius);
	int const xpos(get_xpos(fpos.x)), ypos(get_ypos(fpos.y));

	if (has_water(xpos, ypos) && water_matrix[ypos][xpos] > (fpos.z - radius) &&
		water_matrix[ypos][xpos] < (fpos.z + radius + MAX_SPLASH_DEPTH))
	{
		add_splash(fpos, xpos, ypos, damage, 0.6*radius, (weapon != W_BLADE || ((rand()&31) == 0)));
	}
}


// fire and gas damage and telefrags (Note: index is unused)
void do_area_effect_damage(point const &pos, float effect_radius, float damage, int index, int source, int type) {

	float const radius(object_types[SMILEY].radius + effect_radius);
	point camera_pos(get_camera_pos());
	camera_pos.z -= 0.5*get_player_height(); // average/center of camera

	if (type == FIRE) {
		float const dist_sq(p2p_dist_sq(camera_pos, pos));
		dist_to_fire_sq = ((dist_to_fire_sq == 0.0) ? dist_sq : min(dist_to_fire_sq, dist_sq));
		if (((frame_counter+index)&31) == 0) {destroy_coll_objs(pos, 32.0*damage, source, FIRE);} // check for exploding objects every 32 frames
	}
	if (!spectate && camera_mode == 1 && game_mode && dist_less_than(pos, camera_pos, radius)) { // test the player
		if (camera_collision(CAMERA_ID, ((source == NO_SOURCE) ? CAMERA_ID : source), zero_vector, pos, damage, type)) {
			if (type == FIRE && camera_health > 0.0 && ((rand()&63) == 0)) {gen_sound(SOUND_AGONY, pos);} // skip if player has shielding and self damage?
		}
	}
	obj_group const &objg(obj_groups[coll_id[SMILEY]]);
	
	if (objg.enabled) { // test the smileys
		for (unsigned i = 0; i < objg.end_id; ++i) {
			if (!objg.get_obj(i).disabled() && dist_less_than(pos, objg.get_obj(i).pos, radius)) {
				// test for objects blocking the damage effects?
				smiley_collision(i, ((source == NO_SOURCE) ? i : source), zero_vector, pos, damage, type);
			}
		}
	}
}


void player_teleported(point const &pos, int player_id) { // check for telefrags
	do_area_effect_damage(pos, 2.0*object_types[SMILEY].radius, 10000, -1, player_id, TELEFRAG); // telefrag
}

bool remove_player_translocator(int player_id) {

	obj_group &objg(obj_groups[coll_id[XLOCATOR]]);
	if (!objg.enabled) return 0; // disabled, do nothing

	for (unsigned i = 0; i < objg.end_id; ++i) {
		dwobject &obj(objg.get_obj(i));
		if (!obj.disabled() && obj.source == player_id) {obj.status = OBJ_STAT_BAD; return 1;}
	}
	return 0;
}

bool pickup_player_translator(int player_id) {

	assert(sstates != NULL); // shouldn't get here in this case
	assert(player_id >= CAMERA_ID && player_id < num_smileys);
	if (!remove_player_translocator(player_id)) return 0;
	player_state &sstate(sstates[player_id]);
	sstate.ticks_since_fired = tfticks; // update fire time to avoid spurious refire
	assert(sstate.p_ammo[W_XLOCATOR] == 0);
	sstate.p_ammo[W_XLOCATOR] = 1; // pickup
	gen_sound(SOUND_ITEM, get_sstate_pos(player_id), 1.0);
	return 1;
}

void translocator_death(int player_id) {
	smiley_collision(player_id, player_id, zero_vector, get_sstate_pos(player_id), 10000, XLOCATOR_DEATH);
}

bool try_use_translocator(int player_id) {

	// Should smileys shoot enemy translocators?
	assert(sstates != NULL); // shouldn't get here in this case
	assert(player_id >= CAMERA_ID && player_id < num_smileys);
	obj_group &objg(obj_groups[coll_id[XLOCATOR]]);
	if (!objg.enabled) return 0; // disabled, do nothing
	bool const is_camera(player_id == CAMERA_ID);
	float const delta_h(CAMERA_RADIUS - object_types[XLOCATOR].radius + (is_camera ? get_player_height() : 0.0));
	point &player_pos(get_sstate_pos(player_id)); // by reference - can modify
	player_state &sstate(sstates[player_id]);
	sstate.ticks_since_fired = tfticks; // update fire time to avoid spurious refire, whether or not this fails

	for (unsigned i = 0; i < objg.end_id; ++i) {
		dwobject &obj(objg.get_obj(i));
		if (obj.disabled() || obj.source != player_id) continue; // disabled, or wrong player's translocator
		obj.status = OBJ_STAT_BAD; // remove translocator
		remove_coll_object(obj.coll_id);
		assert(sstate.p_ammo[W_XLOCATOR] == 0);
		sstate.p_ammo[W_XLOCATOR] = 1; // add translocator back into player's inventory for reuse
		teleport_object(player_pos, player_pos, (obj.pos + vector3d(0, 0, delta_h)), CAMERA_RADIUS, player_id); // teleport the player
		if (is_camera) {camera_last_pos = surface_pos = (player_pos - vector3d(0, 0, get_player_height()));} // update surface_pos and last_pos as well (actually moves the player/camera)
		return 1; // success
	} // for i
	translocator_death(player_id); // no translocator = death
	return 0; // not found
}


void switch_player_weapon(int val, bool mouse_wheel) {

	if (game_mode && (game_mode == GAME_MODE_FPS || world_mode == WMODE_GROUND)) {
		if (sstates != NULL) {
			sstates[CAMERA_ID].switch_weapon(val, 1);
			last_inventory_frame = frame_counter;
		}
	}
	else if (world_mode == WMODE_INF_TERRAIN) {change_inf_terrain_fire_mode(val, mouse_wheel);}
	else if (world_mode == WMODE_GROUND && create_voxel_landscape) {change_voxel_editing_mode(val);}
	else if (spraypaint_mode) {change_spraypaint_color(val);}
	else if (spheres_mode   ) {change_sphere_material (val, 0);}
	else if (flashlight_on  ) {flashlight_color_id ^= 1; play_switch_weapon_sound();}
}


void player_state::switch_weapon(int val, int verbose) {

	if (game_mode == GAME_MODE_DODGEBALL) {
		weapon = ((UNLIMITED_WEAPONS || p_ammo[W_BALL] > 0) ? (int)W_BALL : (int)W_UNARMED);
		return;
	}
	do {
		weapon = (weapon+NUM_WEAPONS+val)%NUM_WEAPONS;
		if (weapon == W_XLOCATOR) {
			if (enable_translocator) break; // always selectable, even if no ammo
			continue; // translocator disabled (Note: check is probably unnecessary because ammo will be 0)
		}
	} while (!can_fire_weapon());

	if (verbose) {print_weapon(weapon);}
	play_switch_weapon_sound();
	wmode      = 0; // maybe don't reset this?
	fire_frame = 0;
	cb_hurt    = 0;
}


int player_state::get_prev_fire_time_in_ticks() const {return int(get_fspeed_scale()*(tfticks - ticks_since_fired));}

bool player_state::can_fire_weapon() const {return ((UNLIMITED_WEAPONS && weapon != W_XLOCATOR) || !no_weap_or_ammo());}

void player_state::use_translocator(int player_id) {
	if (get_prev_fire_time_in_ticks() > (int)weapons[weapon].fire_delay) {
		if (wmode&1) {if (!pickup_player_translator(player_id)) {translocator_death(player_id);}} // recall
		else {try_use_translocator(player_id);} // use
	}
}

void player_state::gamemode_fire_weapon() { // camera/player fire

	static int fire_frame(0);
	if (frame_counter == fire_frame) return; // to prevent two fires in the same frame
	fire_frame = frame_counter;

	if (game_mode == GAME_MODE_NONE || (game_mode == GAME_MODE_BUILDINGS && world_mode == WMODE_INF_TERRAIN)) { // flashlight/candlelight/spraypaint mode only
		if (voxel_editing) {modify_voxels();}
		else if (spraypaint_mode) {spray_paint ((wmode & 1) != 0);}
		else if (spheres_mode   ) {throw_sphere((wmode & 1) != 0);}
		// flashlight can be disabled in building gameplay mode; the mouse button will use the current item instead of the flashlight
		else if (!flashlight_enabled()) {/*print_text_onscreen("No Flashlight", RED, 1.0, 1.0*TICKS_PER_SECOND, 0);*/}
		else if (world_mode == WMODE_GROUND && (wmode & 1)) {add_camera_candlelight();}
		else {add_camera_flashlight();}
		return;
	}
	if (!camera_reset) return;
	point const camera(get_camera_pos());
	if (temperature <= W_FREEZE_POINT && is_underwater(camera))   return; // under ice
	if (sstates != nullptr && sstates[CAMERA_ID].freeze_time > 0) return; // frozen, can't fire
	
	if (following) {
		following    = 0;
		camera_reset = 1;
		return;
	}
	if (weapon != W_UNARMED && !can_fire_weapon()) { // can't fire
		if (weapon == W_XLOCATOR) {use_translocator(CAMERA_ID);}
		else if (weapon != W_ROCKET && weapon != W_SEEK_D && weapon != W_PLASMA && weapon != W_GRENADE && weapon != W_RAPTOR) { // this test is questionable
			switch_weapon(1, 1); // auto-switch to a weapon that can be fired
			if (weapon == W_BBBAT)   {switch_weapon( 1, 1);}
			if (weapon == W_UNARMED) {switch_weapon(-1, 1);}
			if (game_mode == GAME_MODE_DODGEBALL && weapon == W_BBBAT) {switch_weapon(1, 1);}
			return; // no weapon/out of ammo
		}
	}
	else { // can fire
		verify_wmode();
		bool const fmode2(wmode & 1);
		int const psize((int)ceil(plasma_size));
		int chosen;
		int const status(fire_projectile(camera, cview_dir, CAMERA_ID, chosen));

		if (status == 2 && weapon == W_SEEK_D && fmode2) { // follow the seek and destroy
			if (chosen >= 0) {obj_groups[coll_id[SEEK_D]].get_obj(chosen).flags |= CAMERA_VIEW;}
			orig_camera  = camera_origin; // camera is actually at the SEEK_D location, not quite right, but interesting
			orig_cdir    = cview_dir;
			camera_reset = 0;
			following    = 1;
		}
		int &pammo(p_ammo[weapon]);

		if (status != 0 && (!UNLIMITED_WEAPONS || weapon == W_XLOCATOR) && !no_weap() && pammo > 0) {
			if (weapon == W_PLASMA && psize > 1) {
				pammo = max(0, pammo-psize); // large plasma burst
			}
			else if (weapon == W_GRENADE && fmode2 && (pammo >= int(weapons[W_CGRENADE].def_ammo) || UNLIMITED_WEAPONS)) {
				pammo -= weapons[W_CGRENADE].def_ammo; // cluster grenade
			}
			else if (weapon == W_BLADE) {
				if (wmode&1) { // SAWBLADE
					--pammo;
					if (pammo == 0) {p_weapons[weapon] = 0;}
				}
			}
			else {--pammo;}
		}
	}
	//if (frame_counter == fire_frame) return;
	int const dtime(get_prev_fire_time_in_ticks());

	if ((game_mode == GAME_MODE_DODGEBALL && weapon == W_BBBAT) || (!UNLIMITED_WEAPONS && no_ammo() && (weapon == W_LASER || dtime >= int(weapons[weapon].fire_delay)))) {
		switch_weapon(1, 1);
		if (weapon == W_UNARMED) {switch_weapon(1, 1);}
		if (weapon == W_BBBAT)   {switch_weapon(1, 1);}
		if (weapon == W_UNARMED) {switch_weapon(1, 1);}
	}
	player_dodgeball_id = -1; // reset
}


void add_laser_beam(beam3d const &beam) {

	beams.push_back(beam);
	add_line_light(beam.pts[0], beam.pts[1], beam.color, 0.35, min(1.0f, sqrt(beam.intensity)));
	//if (smoke_visible) {} // check for smoke along laser beam path and add glow halo? or just enable smoke dynamic lighting?
}


void create_shell_casing(point const &fpos, vector3d const &dir, int shooter, float radius, unsigned char type) {

	obj_group &objg(obj_groups[coll_id[SHELLC]]);
	int const i(objg.choose_object());
	objg.create_object_at(i, fpos);
	dwobject &obj(objg.get_obj(i));
	obj.pos.z      -= 0.8*radius;
	obj.pos        += dir*(1.2*radius);
	obj.velocity    = gen_rand_vector(rand_uniform(0.0, 0.25f*(type + 1)), 3.0, PI) + vector3d(dir.y*0.7f, -dir.x*0.7f, 5.0);
	obj.orientation = signed_rand_vector_norm();
	obj.angle       = rand_uniform(0.0, TWO_PI);
	//obj.init_dir    = dir;
	obj.init_dir    = vector3d(PI*signed_rand_float(), 0.0, 0.0); // angle
	obj.time        = -1;
	obj.source      = shooter;
	obj.direction   = type; // 0 = M16, 1 = shotgun
}


void create_shrapnel(point const &pos, vector3d const &dir, float firing_error, unsigned nshots, int shooter, int weapon) {

	unsigned const num(2*nshots + 2);
	obj_group &objg(obj_groups[coll_id[SHRAPNEL]]);

	for (unsigned o = 0; o < num; ++o) {
		int const i(objg.choose_object());
		objg.create_object_at(i, pos);
		dwobject &obj(objg.get_obj(i));
		obj.angle       = 1.0;
		obj.velocity    = dir;
		vadd_rand(obj.velocity, firing_error);
		obj.velocity   *= rand_uniform(65.0, 80.0);
		obj.orientation = signed_rand_vector_norm();
		obj.angle       = 360.0*rand_float();
		obj.source      = shooter;
		obj.direction   = weapon;
	}
}

// delayed projectiles are intended for use with fast projectiles (M16 and shotgun) that are not infinite speed, none of which are currently enabled
struct delayed_proj_t {
	point pos;
	vector3d dir;
	int shooter;
	float damage, velocity;

	delayed_proj_t(point const &p, vector3d const &d, float dam, int s, float v) : pos(p), dir(d), shooter(s), damage(dam), velocity(v) {}
};

vector<delayed_proj_t> delayed_projs;

void projectile_test_delayed(point const &pos, vector3d const &dir, float firing_error, float damage,
	int shooter, float &range, float intensity, int ignore_cobj, float velocity, vector3d *dir_used_ptr=nullptr)
{
	float const max_range(velocity*fticks); // Note: velocity=0.0 => infinite speed/instant hit (use tstep instead of fticks here?)
	vector3d dir_used(dir);
	projectile_test(pos, dir, firing_error, damage, shooter, range, intensity, ignore_cobj, max_range, &dir_used);
	if (dir_used_ptr) {*dir_used_ptr = dir_used;}
	if (max_range == 0.0 || range < max_range) return; // inf speed, or hit something within range, done
	point const new_pos(pos + dir_used.get_norm()*max_range); // the location of this projectile after this frame's timestep
	if (!get_scene_bounds().contains_pt(new_pos)) return; // outside the scene, done
	delayed_projs.push_back(delayed_proj_t(new_pos, dir_used, damage, shooter, velocity));
}

void proc_delayed_projs() {

	if (!animate2) return;
	vector<delayed_proj_t> cur_delayed_projs;
	cur_delayed_projs.swap(delayed_projs); // swap for next frame, calls below may add to delayed_projs

	for (auto i = cur_delayed_projs.begin(); i != cur_delayed_projs.end(); ++i) { // Note: firing error has already been applied
		float range(0.0); // unused
		projectile_test_delayed(i->pos, i->dir, 0.0, i->damage, i->shooter, range, 1.0, get_shooter_coll_id(i->shooter), i->velocity);
	}
}


float weapon_t::get_fire_vel() const {return (v_add + ball_velocity*v_mult);}


// returns: 0=not fired, 1=fired (projectile or impact), 2=fired object, 3=fired (no object)
int player_state::fire_projectile(point fpos, vector3d dir, int shooter, int &chosen_obj) {

	chosen_obj = -1;
	float damage_scale(1.0), range(0.0);
	assert(can_fire_weapon());
	int weapon_id(weapon);

	if (wmode & 1) { // secondary fire projectiles
		if (weapon == W_GRENADE) {weapon_id = W_CGRENADE;}
		if (weapon == W_BLADE  ) {weapon_id = W_SAWBLADE;}
		if (weapon == W_BALL && game_mode == GAME_MODE_FPS) {weapon_id = W_TELEPORTER;} // not in dodgeball mode
	}
	int const dtime(get_prev_fire_time_in_ticks());
	bool const rapid_fire(weapon_id == W_ROCKET && (wmode&1)), is_player(shooter == CAMERA_ID);
	weapon_t const &w(weapons[weapon_id]);
	int fire_delay((int)w.fire_delay);
	if (disable_fire_delay) {fire_delay = 0;}
	if (UNLIMITED_WEAPONS && !is_player && weapon_id == W_LANDMINE) {fire_delay *= 2;} // avoid too many landmines
	else if (!FREEZE_MODE && weapon == W_RAPTOR && (wmode&1)) {fire_delay *= 2;} // 2x fire delay for multi-shot mode (but regular fire for freeze mode)
	if (rapid_fire) {fire_delay /= 3;}
	float const radius(get_sstate_radius(shooter));
	unsigned nshots(w.nshots);

	if (weapon_id == W_LASER) { // always fires
		damage_scale = fticks/max(fire_delay, 1);
	}
	else if (dtime < fire_delay) { // add light between firing frames to avoid tearing
		if (!vsync_enabled && is_player && weapon_id == W_M16) {add_dynamic_light(1.0, fpos, YELLOW);}
		return 0;
	}
	if (weapon_id == W_M16  && (wmode&1) == 1) {++rot_counter;}
	if (weapon_id == W_RAPTOR) {++rot_counter;}
	ticks_since_fired = tfticks;
	bool const underwater(is_underwater(fpos));
	float firing_error(w.firing_error), shot_delta(0.0);
	if (underwater) {firing_error *= UWATER_FERR_MUL;}
	if (is_player && do_zoom) {firing_error *= ZOOM_FERR_MULT;} // higher accuracy when zooming in
	if (rapid_fire) {firing_error *= 20.0;} // rockets
	dir.normalize();
	fire_frame = max(1, fire_delay);
	float const damage(damage_scale*w.blast_damage), vel(w.get_fire_vel());
	int const ignore_cobj(get_shooter_coll_id(shooter));
	int type(w.obj_id);
	
	if (is_player && camera_surf_collide && !disable_recoil && powerup != PU_FLIGHT) { // recoil (only for player)
		float recoil(w.recoil);
		if (is_jumping || fall_counter > 0 ) {recoil *= 2.5;} // low friction when in the air
		else if (velocity.mag() < TOLERANCE) {recoil *= 0.4;} // only static (not kinetic) friction (Note: only player velocity is correct)
		move_camera_pos(dir, -recoil); // backwards recoil
		if (weapon_id == W_M16 || weapon_id == W_SHOTGUN) {c_phi += 0.25*w.recoil;} // upward recoil (tilt backwards)
		update_cpos();
	}
	switch (weapon_id) {
		case W_M16:     add_dynamic_light(1.0, fpos, YELLOW); break;
		case W_SHOTGUN: add_dynamic_light(1.3, fpos, YELLOW); break;
		case W_LASER:   add_dynamic_light(0.6, fpos, get_laser_beam_color(shooter)); break;
		case W_PLASMA:  add_dynamic_light(0.9, fpos, ORANGE); break;
	}
	switch (weapon_id) {
	case W_M16: // line of sight damage
		gen_sound(SOUND_GUNSHOT, fpos, 0.5);
		gen_arb_smoke((fpos + ((wmode&1) ? 1.5 : 1.8)*radius*dir + vector3d(0.0, 0.0, -0.4*radius)), WHITE,
			vector3d(0.0, 0.0, 0.15), ((wmode&1) ? 0.0025 : 0.0015), 0.25, 0.2, 0.0, shooter, SMOKE, 0, 0.01);

		if ((wmode&1) != 1) { // not firing shrapnel
			if (dtime > 10) {firing_error *= 0.1;}
			if (underwater) {firing_error += UWATER_FERR_ADD;}
			vector3d dir_used(dir);
			projectile_test_delayed(fpos, dir, firing_error, damage, shooter, range, 1.0, ignore_cobj, vel, &dir_used);
			create_shell_casing(fpos, dir_used, shooter, radius, 0);
			if (!is_player && range > 0.1*radius) {beams.push_back(beam3d(1, shooter, fpos, (fpos + range*dir_used), ORANGE, 1.0));} // generate bullet light trail
			return 1;
		} // fallthrough to shotgun case
	case W_SHOTGUN:
		if ((wmode&1) == 1) { // shrapnel cannon/chaingun
			create_shrapnel((fpos + dir*(0.1*radius)), dir, firing_error, nshots, shooter, weapon_id);
		}
		else { // normal 12-gauge/M16
			if (underwater) {firing_error += UWATER_FERR_ADD;}

			for (int i = 0; i < int(nshots); ++i) { // can be slow if trees are involved
				projectile_test_delayed(fpos, dir, firing_error, damage, shooter, range, 1.0, ignore_cobj, vel);
			}
		}
		if (weapon_id == W_SHOTGUN) {
			for (unsigned i = 0; i < 2; ++i) {create_shell_casing(fpos, dir, shooter, radius, 1);}
			gen_sound(SOUND_SHOTGUN, fpos);
			gen_arb_smoke((fpos + 1.4*radius*dir + vector3d(0.0, 0.0, -0.4*radius)), WHITE, vector3d(0.0, 0.0, 0.3), 0.003, 0.8, 0.7, 0.0, shooter, SMOKE, 0, 0.01);
		}
		return 1;

	case W_BBBAT: // baseball bat
		do_impact_damage(fpos, dir, velocity, fpos, radius, shooter, weapon_id, 1.0);
		gen_sound(SOUND_SWING, fpos);
		//cobj.register_coll() ???
		return 1;

	case W_PLASMA:
		if ((wmode&1) == 1) {
			plasma_loaded = !plasma_loaded;

			if (plasma_loaded) {
				plasma_size = 1.0;
				return 0; // loaded but not fired
			}
		}
		else plasma_loaded = 0;
		gen_sound(SOUND_FIREBALL, fpos);
		break;

	case W_LASER: { // line of sight damage
			vector3d final_dir(dir);
			projectile_test(fpos, dir, firing_error, damage, shooter, range, 1.0, ignore_cobj, 0.0, &final_dir);

			if (range > 1.1*radius) {
				beam3d const beam((range >= 0.9*FAR_CLIP), shooter, (fpos + final_dir*radius), (fpos + final_dir*range), get_laser_beam_color(shooter));
				add_laser_beam(beam); // might not need to actually add laser itself for camera/player (only need line light?)
			}
		}
		break;

	case W_SEEK_D:
		fpos += dir*radius; // fire from in front of shooter, but then there is no recoil
		gen_sound(SOUND_ROCKET, fpos, 1.5, 1.0);
		break;

	case W_GASSER: {
			float const r(radius + w.blast_radius);
			point start_pos(fpos + dir*(0.5*r));
			
			if (is_underwater(start_pos)) {
				colorRGBA color(DK_GREEN, 0.75);

				for (unsigned n = 0; n < 4; ++n) {
					start_pos += signed_rand_vector(0.05*radius);
					gen_bubble(start_pos, 0.0, color);
				}
			}
			else {
				start_pos   += dir*(0.5*r);
				start_pos.z -= 0.25*r;
				vector3d dir2(dir);
				if (firing_error != 0.0) {vadd_rand(dir2, firing_error);}
				bool const is_fire(wmode & 1);
				vector3d const gas_vel(dir2*vel + vector3d(0.0, 0.0, 0.2));
				colorRGBA const color(is_fire ? colorRGBA(1.0, 0.75, 0.0) : GREEN);
				int const smoke_type (is_fire ? (int)FIRE : (int)GASSED);
				float const density(0.5*rand_uniform(0.5, 1.0));
				float const darkness(0.6*rand_uniform(0.7, 1.0));
				float const radius(w.blast_radius*rand_uniform(0.8, 1.2));
				gen_arb_smoke(start_pos, color, gas_vel, radius, density, darkness, w.blast_damage, shooter, smoke_type, 0);
				gen_sound((is_fire ? (unsigned)SOUND_FIREBALL : (unsigned)SOUND_HISS), start_pos, 0.7, 1.2);
			}
		}
		return 1;

	case W_RAPTOR:
		if ((wmode&1) == 1) { // secondary fire mode, double the reload time/half the fire rate
			if (FREEZE_MODE) {type = FREEZE_BOMB;} // freeze mode
			else { // multi-shot mode
				unsigned const shot_count = 4;
				int &pammo(sstates[shooter].p_ammo[weapon_id]);
			
				if (pammo >= (int)shot_count || UNLIMITED_WEAPONS) {
					firing_error *= 2.0;
					shot_delta    = 7.0*object_types[RAPT_PROJ].radius; // required to prevent shots from colliding with each other
					nshots = shot_count;
					if (!UNLIMITED_WEAPONS) {pammo -= (shot_count-1);} // requires more ammo
				}
			}
		}
		gen_sound(SOUND_ROCKET, fpos, 0.5, 1.8);
		break;

	case W_XLOCATOR: gen_sound(SOUND_BOING,  fpos, 1.0, 1.5);
		fpos += dir*(0.25*radius); // fire from in front of shooter
		break;

	case W_BLADE:    gen_sound(SOUND_DRILL,  fpos, 0.5, 0.8); break;
	case W_SAWBLADE: gen_sound(SOUND_DRILL,  fpos, 0.5, 0.8); break; // bounce?
	case W_ROCKET:   gen_sound(SOUND_ROCKET, fpos, 1.0, 1.2); break;
	case W_BALL:     gen_sound(SOUND_SWING,  fpos, 0.7, 1.4); break;
	case W_TELEPORTER:gen_sound(SOUND_SWING, fpos, 1.2, 2.0); break;
	case W_SBALL:    gen_sound(SOUND_SWING,  fpos, 0.4, 1.5); break;
	case W_GRENADE:  gen_sound(SOUND_SWING,  fpos, 0.5, 1.3); break;
	case W_CGRENADE: gen_sound(SOUND_SWING,  fpos, 0.6, 1.2); break;
	case W_STAR5:    gen_sound(SOUND_SWING,  fpos, 0.3, 2.0); break;
	case W_LANDMINE: gen_sound(SOUND_ALERT,  fpos, 0.3, 2.5); break;
	}
	if (type < 0) return 3;
	int const cid(coll_id[type]);
	assert(cid >= 0 && cid < NUM_TOT_OBJS);
	obj_group &objg(obj_groups[cid]);
	assert(objg.max_objs > 0);
	float const rdist(0.75 + ((weapon_id == W_PLASMA) ? 0.5*(plasma_size - 1.0) : 0.0)); // change?
	float const radius_sum(radius + object_types[type].radius);
	assert(nshots <= objg.max_objs);
	bool const dodgeball(game_mode == GAME_MODE_DODGEBALL && weapon_id == W_BALL && !UNLIMITED_WEAPONS);
	if (dodgeball) {assert(nshots <= balls.size());}
	float shot_offset(0.0);

	for (unsigned shot = 0; shot < nshots; ++shot) {
		int const chosen(dodgeball ? balls.back() : objg.choose_object());
		if (dodgeball) {balls.pop_back();}
		chosen_obj = chosen;
		assert(chosen >= 0); // make sure there is an object available
		vector3d dir2(dir);

		if (firing_error != 0.0) {
			vadd_rand(dir2, firing_error);
			dir2.normalize();
		}
		objg.create_object_at(chosen, fpos);
		dwobject &obj(objg.get_obj(chosen));
		obj.pos      += dir2*(rdist*radius_sum + shot_offset);
		obj.velocity  = dir2*vel;
		obj.init_dir  = -dir2;
		obj.time      = -1;
		obj.source    = shooter;
		obj.flags    |= WAS_FIRED;
		if (rapid_fire) {obj.time = int((1.0f - rand_uniform(0.1, 0.9)*rand_uniform(0.1, 0.9))*object_types[type].lifetime);}
		shot_offset += shot_delta; // move to next shot pos

		switch (weapon_id) {
		case W_PLASMA:
			obj.init_dir.x = float(pow(double(plasma_size), 0.75)); // psize
			obj.pos.z     += 0.2*radius_sum;
			plasma_size    = 1.0;
			break;
		case W_BALL:
			obj.pos.z += 0.2*radius_sum;
			break;
		case W_STAR5:
			obj.init_dir += gen_rand_vector(0.1, 1.0, PI);
			obj.init_dir.normalize();
			break;
		}
	}
	if ((wmode&1) == 0 && weapon_id == W_PLASMA) {plasma_size = 1.0;}
	return 2;
}


int get_range_to_mesh(point const &pos, vector3d const &vcf, point &coll_pos) { // returns: 0=no coll, 1=mesh coll, 2=ice coll

	vector3d const vca(pos + vcf*FAR_CLIP);
	if (world_mode == WMODE_INF_TERRAIN) {return (line_intersect_tiled_mesh(pos, vca, coll_pos) ? 1 : 0);}
	point ice_coll_pos(vca);
	bool ice_int(0);

	// compute range to ice surface (currently only at the default water level)
	if (temperature <= W_FREEZE_POINT) { // firing into ice
		int ixpos(get_xpos(pos.x)), iypos(get_ypos(pos.y));

		if (has_water(ixpos, iypos) && pos.z < water_matrix[iypos][ixpos]) {
			coll_pos = pos;
			return 2; // can't get any range if under ice
		}
		if (vcf.z < -1.0E-6) { // firing down
			float const t(-(pos.z - water_plane_z)/vcf.z), xval(pos.x + t*vcf.x), yval(pos.y + t*vcf.y);
			ixpos = get_xpos(xval);
			iypos = get_ypos(yval);

			if (mesh_is_underwater(ixpos, iypos)) {
				ice_coll_pos.assign(xval, yval, water_matrix[iypos][ixpos]);
				ice_int = 1;
			}
		}
	}
	if (fabs(vcf.z) > TOLERANCE && line_intersect_mesh(pos, vca, coll_pos)) { // skip if dir has no z component
		if (!ice_int || p2p_dist_sq(pos, ice_coll_pos) > p2p_dist_sq(pos, coll_pos)) return 1; // collides with mesh
	}
	if (ice_int) { // collides with ice before mesh
		coll_pos = ice_coll_pos;
		return 2;
	}
	return 0;
}


colorRGBA get_laser_beam_color(int shooter) {return get_smiley_team_color(shooter);}

void add_laser_beam_segment(point const &start_pos, point coll_pos, vector3d const &vref, colorRGBA const &color, int coll, bool distant, float intensity) {

	if (!coll || distant) {coll_pos = start_pos + vref*FAR_CLIP;}
	else if (start_pos == coll_pos) return;
	add_laser_beam(beam3d(distant, NO_SOURCE, start_pos, coll_pos, color, intensity));
}


void modify_alpha_for_cube_light_atten(float &alpha, float light_atten, float thickness);

void gen_glass_shard_from_cube_window(cube_t const &cube, cobj_params const &cp, point const &pos) {

	int const dmin(get_min_dim(cube)), dim1((dmin+1)%3), dim2((dmin+2)%3); // min cube dim
	point const center(cube.get_cube_center());
	float const val(center[dmin]), thickness(cube.min_len()), thresh(0.001*thickness);
	point points[3]; // triangle
	points[0][dim1] = pos[dim1]; points[0][dim2] = pos[dim2];

	while (1) {
		bool const dir(rand()&1);
		int const edim(dir ? dim2 : dim1), sdim(dir ? dim1 : dim2);
		bool const side((pos[edim] - cube.d[edim][0]) < (cube.d[edim][1] - pos[edim]));
		points[1][edim] = points[2][edim] = (side ? cube.d[edim][0]-thresh : cube.d[edim][1]+thresh); // add thresh to avoid z-fighting by burying edge in cobj

		for (unsigned d = 0; d < 2; ++d) {
			points[1+d][sdim] = rand_uniform(cube.d[sdim][d], pos[sdim]); // random pos between cube corner and perpendicular to center pos
		}
		float const base(fabs(points[2][sdim] - points[1][sdim])), height(fabs(points[1][edim] - pos[edim]));
		if (base > 0.75*height && height > 0.5*base) break; // keep if not a high aspect ratio triangle
	}
	UNROLL_3X(points[i_][dmin] = val;)
	int const cindex(add_coll_polygon(points, 3, cp, thickness)); // should reuse index slot and not invalidate cobj
	coll_obj &cobj(coll_objects.get_cobj(cindex));
	cobj.destroy = SHATTERABLE;
	cobj.set_reflective_flag(0); // not supported yet
	cobj.cp.light_atten = 0.0; // optional - unused for polygons
	modify_alpha_for_cube_light_atten(cobj.cp.color.alpha, cp.light_atten, thickness);
}


point projectile_test(point const &pos, vector3d const &vcf_, float firing_error, float damage, int shooter,
	float &range, float intensity, int ignore_cobj, float max_range, vector3d *vcf_used)
{
	assert(isfinite(damage));
	assert(intensity <= 1.0);
	assert(LASER_REFL_ATTEN < 1.0);
	int closest(-1), closest_t(0), coll(0), cindex(-1);
	point coll_pos(pos);
	vector3d vcf(vcf_), coll_norm(plus_z);
	float specular(0.0), luminance(0.0), alpha(1.0), refract_ix(1.0), hardness(1.0);
	player_state const &sstate(sstates[shooter]);
	int const wtype(sstate.weapon), wmode(sstate.wmode);
	float const vcf_mag(vcf.mag()), radius(get_sstate_radius(shooter));
	bool const is_laser(wtype == W_LASER);
	colorRGBA const laser_color(is_laser ? get_laser_beam_color(shooter) : BLACK);
	float MAX_RANGE(min((float)FAR_CLIP, 2.0f*(X_SCENE_SIZE + Y_SCENE_SIZE + Z_SCENE_SIZE)));
	if (max_range > 0.0) {MAX_RANGE = min(MAX_RANGE, max_range);}
	range = MAX_RANGE;
	vcf  /= vcf_mag;

	if (firing_error != 0.0) {
		vcf += signed_rand_vector_spherical(firing_error);
		vcf.normalize();
	}
	vector3d const vcf0(vcf*vcf_mag);
	if (vcf_used) {*vcf_used = vcf0;}
	int intersect(0);

	if (world_mode == WMODE_INF_TERRAIN) {
		point const pos2(pos + vcf*range);
		intersect = line_intersect_tiled_mesh(pos, pos2, coll_pos, 1); // check terrain; inc_trees=1
		point p_int;
		
		if (line_intersect_city(pos, pos2, p_int)) { // check city (buildings, cars, bridges, tunnels, city objects, etc.)
			if (!intersect || p2p_dist_sq(pos, p_int) < p2p_dist_sq(pos, coll_pos)) { // keep closest intersection point
				if (damage > 0.0) {destroy_city_in_radius((p_int + vcf*object_types[PROJC].radius), 0.0);} // destroy whatever is at this location
				coll_pos = p_int;
				intersect = 1;
			}
		}
	}
	else {
		intersect = get_range_to_mesh(pos, vcf, coll_pos);
	}
	if (intersect) {
		range = p2p_dist(pos, coll_pos);
		if (intersect == 1) {coll_pos = pos + vcf*(range - 0.01);} // not an ice intersection - simple and inexact, but seems OK
		coll_pos.z += SMALL_NUMBER;
	}

	// search for collisions with static objects (like trees)
	int const laser_m2(SELF_LASER_DAMAGE && is_laser && (wmode&1) && intensity < 1.0);
	int const proj_type(is_laser ? (int)BEAM : (int)PROJECTILE);
	range = get_projectile_range(pos, vcf, 0.01*radius, range, coll_pos, coll_norm, coll, cindex, shooter, !is_laser, ignore_cobj);
	point p_int(pos), lsip(pos);
	bool is_metal(0);

	if (cindex >= 0) {
		cobj_params const &cp(coll_objects.get_cobj(cindex).cp);
		hardness = min(cp.elastic, 1.0f); // prevent problems with bouncy objects (sawblade and teleporter)

		if (is_laser) {
			get_lum_alpha(cp.color, cp.tid, luminance, alpha);
			specular   = cp.spec_color.get_luminance();
			refract_ix = cp.refract_ix;
			is_metal   = (cp.metalness > 0.0);
		}
	}
	for (int g = 0; g < num_groups; ++g) { // collisions with dynamic group objects - this can be slow (Note that some of these are already in cobjs test)
		obj_group const &objg(obj_groups[g]);
		int const type(objg.type);
		obj_type const &otype(object_types[type]);
		if (!objg.enabled || !objg.large_radius() || type == PLASMA || type == TELEPORTER) continue;
		
		for (unsigned i = 0; i < objg.end_id; ++i) {
			if (type == SMILEY && (int)i == shooter && !laser_m2) continue; // this is the shooter
			if (!objg.obj_within_dist(i, pos, (range + otype.radius + SMALL_NUMBER))) continue;
			point const &apos(objg.get_obj(i).pos);

			if (line_sphere_int(vcf0, lsip, apos, otype.radius, p_int, 1)) {
				closest   = i;
				closest_t = type;
				coll_pos  = p_int;
				range     = p2p_dist(pos, coll_pos);
				hardness  = 1.0; // 'hard'
				
				if (is_laser && type != SMILEY && type != CAMERA) {
					get_lum_alpha(otype.color, otype.tid, luminance, alpha);
					specular = ((otype.flags & SPECULAR) ? 1.0 : ((otype.flags & LOW_SPECULAR) ? 0.5 : 0.0));
				}
			}
		} // for i
	}
	if ((shooter >= 0 || laser_m2) && !spectate) { // check camera/player
		point const camera(get_camera_pos());
		float const dist(distance_to_camera(pos));

		if (dist < (range + CAMERA_RADIUS + SMALL_NUMBER) && line_sphere_int(vcf0, lsip, camera, CAMERA_RADIUS, p_int, 1)) {
			closest   = 0;
			closest_t = CAMERA;
			coll_pos  = p_int;
			range     = p2p_dist(pos, coll_pos);
			hardness  = 1.0;
		}
	}

	// check for smoke occlusion of laser beam
	if (is_laser && smoke_visible) {
		vector3d const delta(coll_pos - pos);
		unsigned const nsteps(delta.mag()/HALF_DXY); // okay if zero

		if (nsteps > 1) {
			float const SMOKE_SCALE = 0.25;
			vector3d const step(delta/nsteps);
			point cur_pos(pos);
			float visibility(1.0);

			for (unsigned i = 0; i < nsteps; ++i) {
				visibility *= (1.0f - SMOKE_SCALE*get_smoke_at_pos(cur_pos));

				if (visibility < 0.25) { // mostly smoke, laser beam ends here (dissipates in the smoke)
					range = p2p_dist(pos, cur_pos);
					return cur_pos;
				}
				cur_pos += step;
			}
			damage    *= visibility;
			intensity *= visibility;
		}
	}
	bool no_spark(0);

	// hit cobjs (like tree leaves)
	if (coll && cindex >= 0 && closest < 0) {
		coll_obj &cobj(coll_objects.get_cobj(cindex));
		cobj.register_coll(TICKS_PER_SECOND/(is_laser ? 4 : 2), proj_type);
		bool const is_glass(cobj.cp.is_glass(cobj.destroy == SHATTERABLE));

		if ((!is_laser || (cobj.cp.color.alpha == 1.0 && intensity >= 0.5)) && cobj.can_be_scorched()) { // lasers only scorch opaque surfaces
			float const decal_radius(rand_uniform(0.004, 0.006));

			if (decal_contained_in_cobj(cobj, coll_pos, coll_norm, decal_radius, get_max_dim(coll_norm))) {
				colorRGBA dcolor;
				int decal_tid(-1);
				point decal_pos(coll_pos);

				if (is_laser) {
					decal_tid = FLARE3_TEX; dcolor = BLACK;
					decal_pos += cross_product(coll_norm, signed_rand_vector(0.75*decal_radius*range)); // break up the regularity of the laser beam, which has no firing error
				}
				else if (is_glass) {
					decal_tid = FLARE3_TEX; dcolor = (WHITE*0.5 + cobj.cp.color*0.5);
				}
				else {
					decal_tid = BULLET_D_TEX;
					colorRGBA const tcolor(texture_color(BULLET_D_TEX)), ocolor(cobj.get_avg_color());
					UNROLL_3X(dcolor[i_] = ocolor[i_]/max(0.01f, tcolor[i_]);) // fudge the color so that dcolor * tcolor = ocolor
					dcolor.alpha = 1.0;
					//dcolor.set_valid_color(); // more consisten across lighting conditions, but less aligned to the object color
				}
				if (decal_tid >= 0) {gen_decal(decal_pos, decal_radius, coll_norm, decal_tid, cindex, dcolor, is_glass, 1);} // inherit partial glass color

				if (coll_norm.z > -0.5 && !is_glass && ((is_laser && (rand()&1)) || wtype == W_M16)) { // create small dust clouds/smoke at hit locations
					point const smoke_pos(coll_pos + decal_radius*coll_norm);
					vector3d const smoke_vel(vector3d(0.1*coll_norm.x, 0.1*coll_norm.y, 0.1));
					gen_arb_smoke(smoke_pos, WHITE, smoke_vel, 0.2*decal_radius, 1.0, (is_laser ? 1.0 : 0.5), 0.0, shooter, SMOKE, 0, 0.01);
				}
			}
		}
		if (wtype == W_M16 && !cobj.is_tree_leaf() && shooter != CAMERA_ID && cindex != camera_coll_id && distance_to_camera(coll_pos) < 2.5*CAMERA_RADIUS) {
			gen_sound(SOUND_RICOCHET, coll_pos); // ricochet near player
		}
		if (!is_laser) { // projectile case
			float range_unused(0.0);

			if (cobj.is_tree_leaf()) { // leaf hit, projectile continues
				projectile_test(coll_pos, vcf, firing_error, damage, shooter, range_unused, 1.0, cindex, max_range); // return value is unused
				no_spark = 1;
			}
			else if (is_glass && (rand()&1) == 0) { // projectile, 50% chance of continuing through glass with twice the firing error and 75% the damage
				projectile_test(coll_pos, vcf, 2.0*firing_error, 0.75*damage, shooter, range_unused, 1.0, cindex, max_range); // return value is unused
				no_spark = 1;
			}
		}
		unsigned dest_prob(cobj.cp.destroy_prob);
		if (dest_prob == 0) { // unspecified, use default values
			if (cobj.destroy == SHATTERABLE) {
				if (is_glass && cobj.type != COLL_CUBE) {dest_prob = 1;} else {dest_prob = 50;} // shattering is less likely than exploding, except for glass spheres/cylinders
			}
			else {dest_prob = 10;} // base value for exploding
		}
		if (sstate.powerup == PU_DAMAGE) {dest_prob = max(1U, dest_prob/4);} // more likely with quad damage

		if ((!is_laser && cobj.destroy >= SHATTERABLE && (rand()%dest_prob) == 0) || // shattered
			(cobj.destroy >= EXPLODEABLE && (rand()%dest_prob) == 0)) // exploded
		{
			if (is_glass && cobj.cp.color.alpha < 0.5 && cobj.type == COLL_CUBE && !cobj.maybe_is_moving() && (rand()&15) != 0) {gen_glass_shard_from_cube_window(cobj, cobj.cp, coll_pos);}
			destroy_coll_objs(coll_pos, 500.0, shooter, PROJECTILE, SMALL_NUMBER); // shatter or explode the object on occasion (critical hit)
		}
		else if (!is_laser && cobj.cp.cobj_type == COBJ_TYPE_VOX_TERRAIN && destroy_thresh == 0) {
			update_voxel_sphere_region(coll_pos, object_types[PROJC].radius, -0.04, shooter, 0);
		}
		else if (!is_laser && cobj.is_movable()) { // projectile, movable, and not destroyed
			vector3d delta(4.0E-8*damage*coll_norm*dot_product(vcf, coll_norm)/cobj.get_group_mass());
			push_movable_cobj(cindex, delta, coll_pos);
		}
	}

	// use collision point/object for damage, sparks, etc.
	if (closest_t == CAMERA) {
		camera_collision(wtype, shooter, zero_vector, coll_pos, damage, proj_type);
	}
	else if (closest_t == SMILEY) {
		smiley_collision(closest, shooter, zero_vector, coll_pos, damage, proj_type);
		if (is_laser && (rand()%6) == 0) gen_smoke(coll_pos);
	}
	else if ((intersect || coll) && dist_less_than(coll_pos, pos, (X_SCENE_SIZE + Y_SCENE_SIZE))) { // spark
		if (!no_spark && !is_underwater(coll_pos)) {
			colorRGBA scolor(is_laser ? laser_color : colorRGBA(1.0, 0.7, 0.0, 1.0));
			//if (is_laser && coll && cindex >= 0 && closest < 0) {scolor = get_cobj_color_at_point(cindex, coll_pos, coll_norm, 0);} // TESTING
			float const ssize((is_laser ? ((wmode&1) ? 0.015 : 0.020)*intensity : 0.025)*((closest_t == CAMERA) ? 0.5 : 1.0));
			sparks.push_back(spark_t(coll_pos, scolor, ssize));
			point const light_pos(coll_pos - vcf*(0.1*ssize));
			add_dynamic_light(0.6*CLIP_TO_01(sqrt(intensity)), light_pos, scolor);
			bool gen_part(0);
			if (is_laser) {gen_part = (intensity >= 0.5 && (alpha > 0.75 && ((rand()&1) == 0)));}
			else          {gen_part = (hardness >= 0.5);} // including ricochets
			if (coll && gen_part) {gen_particles(light_pos, 1, 0.5, 1);} // particle
		}
	}
	
	// process laser reflections
	bool const reflects((intersect == 2 && !coll && closest == -1) || ((wmode&1) && coll) || (is_metal && coll && closest == -1));
	
	if (is_laser && ((coll && alpha < 1.0) || reflects) &&
		closest_t != CAMERA && closest_t != SMILEY && intensity > 0.01)
	{
		float range0(MAX_RANGE), range1(range0), atten(LASER_REFL_ATTEN);
		point end_pos(coll_pos);
		vector3d vref(vcf);
		int coll2(coll);

		if (coll) { // hit coll obj
			float reflect(alpha);

			if (reflects) {
				if (alpha < 1.0 && refract_ix != 1.0) { // semi-transparent - fresnel reflection
					reflect = get_reflected_weight(get_fresnel_reflection(-vcf, coll_norm, 1.0, refract_ix), alpha);
				}
				else { // specular + diffuse reflections
					reflect = CLIP_TO_01(alpha*(specular + (1.0f - specular)*luminance)); // could use red component for laser
				}
				if (reflect > 0.01) { // reflected light
					reflect = min(reflect, LASER_REFL_ATTEN); // prevent stack overflow
					calc_reflection_angle(vcf, vref, coll_norm);
					end_pos = projectile_test(coll_pos, vref, 0.0, reflect*damage, shooter, range0, reflect*intensity, -1, max_range);
				}
			}
			if (alpha < 1.0) {
				float refract(1.0 - reflect);

				if (refract > 0.01) { // refracted light (index of refraction changes angle?)
					if (cindex >= 0) refract *= coll_objects[cindex].get_light_transmit(coll_pos, (coll_pos + vcf*FAR_CLIP));
					refract = min(LASER_REFL_ATTEN, refract);

					if (refract > 0.01) {
						point const cp(projectile_test(coll_pos, vcf, 0.0, refract*damage, shooter, range1, refract*intensity, cindex, max_range));
						add_laser_beam_segment(coll_pos, cp, vcf, laser_color, coll, (range1 > 0.9*MAX_RANGE), refract*intensity);
					}
				}
			}
		}
		else { // hit ice - ice may not actually be in +z (if it has frozen ripples)
			int const xpos(get_xpos(coll_pos.x)), ypos(get_ypos(coll_pos.y));

			if (!point_outside_mesh(xpos, ypos)) { // can this ever fail?
				calc_reflection_angle(vcf, vref, wat_vert_normals[ypos][xpos]); // don't have water surface normals
				end_pos = projectile_test(coll_pos, vref, 0.0, atten*damage, shooter, range0, atten*intensity, -1, max_range);
				coll2   = (end_pos != coll_pos);
			}
		}
		if (reflects && atten > 0.0) {add_laser_beam_segment(coll_pos, end_pos, vref, laser_color, coll2, (range0 > 0.9*MAX_RANGE), atten*intensity);}
	}

	// process bullet ricochets for M16
	if (coll && wtype == W_M16 && hardness > 0.5 && damage > 5.0) { // hit coll obj
		float const dp(-dot_product(vcf, coll_norm));

		if (dp > 0.0) { // collision on correct side (always gets here?)
			float const dvel(hardness*(1.0 - dp)), rdscale(0.9*dvel*dvel);

			if (rdscale > 0.4) {
				float range0(MAX_RANGE);
				vector3d vref(vcf);
				calc_reflection_angle(vcf, vref, coll_norm);
				projectile_test(coll_pos, vref, 0.0, rdscale*damage, shooter, range0, rdscale*intensity, -1, max_range);
			}
		}
	}
	float const dscale((shooter >= CAMERA_ID) ? sstate.get_damage_scale() : 1.0);

	if (closest < 0) { // not a dynamic object (static object)
		if (intersect == 1 && !coll) { // mesh intersection
			//surface_damage[get_ypos(coll_pos.y)][get_xpos(coll_pos.x)] += dscale;
			if (is_laser) {modify_grass_at(coll_pos, 0.25*HALF_DXY, 0, 1);} // burn
		}
	}
	else if (closest_t != SMILEY && closest_t != CAMERA) {
		obj_groups[coll_id[closest_t]].get_obj(closest).damage_object(dscale*damage, coll_pos, pos, wtype);
	}
	return coll_pos;
}


float get_projectile_range(point const &pos, vector3d vcf, float dist, float range, point &coll_pos, vector3d &coll_norm,
						   int &coll, int &cindex, int source, int check_splash, int ignore_cobj)
{
	if (world_mode == WMODE_INF_TERRAIN) {coll = 0; cindex = -1; return range;} // not yet implemented
	vcf.normalize();
	float const splash_val((!DISABLE_WATER && check_splash && (temperature > W_FREEZE_POINT)) ? SPLASH_BASE_SZ*100.0 : 0.0);
	point const pos1(pos + vcf*dist), pos2(pos + vcf*range);
	coll = check_coll_line_exact(pos1, pos2, coll_pos, coll_norm, cindex, splash_val, ignore_cobj);

	if (coll) {
		coll_obj &cobj(coll_objects.get_cobj(cindex));
		if (cobj.cp.coll_func) {cobj.cp.coll_func(cobj.cp.cf_index, 0, zero_vector, pos, 0.0, (check_splash ? (int)PROJC : (int)LASER));} // apply collision function
		range = p2p_dist(coll_pos, pos);
	}
	if (splash_val > 0.0 && is_underwater(pos1)) {gen_line_of_bubbles(pos1, (pos + vcf*range));}
	return range;
}


void do_cblade_damage_and_update_pos(point &pos, int shooter) {

	player_state &sstate(sstates[shooter]);
	int fframe(sstate.fire_frame);
	int const delay(max(1u, weapons[sstate.weapon].fire_delay));
	float const cradius(object_types[SMILEY].radius);
	vector3d const dir(get_sstate_dir(shooter));
	point const shoot_pos(pos);
	pos.z -= 0.05;

	if (fframe > 0 && !(sstate.wmode&1)) { // carnage blade extension
		point coll_pos;
		vector3d coll_norm; // unused
		int coll(0), cindex(-1);
		int const fdir(fframe > (delay/2)), ff(fdir ? (delay - fframe) : fframe); // fdir = forward
		float const ext_scale(cradius/0.06), max_ext(ext_scale*CBLADE_EXT_PT*ff); // ext_scale = CAMERA_RADIUS/DEF_CAMERA_RADIUS
		float range(get_projectile_range(pos, dir, 1.1*cradius, (1.5f*cradius + ext_scale*CBLADE_EXT), coll_pos, coll_norm, coll, cindex, shooter, 0));
		bool cobj_coll(coll && cindex >= 0);

		if (get_range_to_mesh(pos, dir, coll_pos)) {
			float const mesh_range(p2p_dist(pos, coll_pos)-0.1f);

			if (mesh_range < range) {
				range     = mesh_range;
				cobj_coll = 0;
			}
			if (max_ext > (range - 0.8f*cradius)) {modify_grass_at(coll_pos, 0.75*cradius, 0, 0, 1);} // cut grass
		}
		if (cobj_coll) {coll_objects.get_cobj(cindex).register_coll(TICKS_PER_SECOND/2, IMPACT);}
		sstate.dpos = max(0.0f, min(max_ext, (range - 0.8f*cradius)));
		pos += dir*sstate.dpos;
	}
	// always doing damage
	do_impact_damage(pos, dir, zero_vector, shoot_pos, cradius, shooter, W_BLADE, (1.0 + 0.25*(sstate.dpos > 0)));
	pos.z += 0.05;

	// throw up debris
	if ((rand()%6) == 0) {
		point pos2(pos + dir*(1.25*cradius));
		int const xpos(get_xpos(pos2.x)), ypos(get_ypos(pos2.y));
		
		if (!point_outside_mesh(xpos, ypos) && (pos.z - mesh_height[ypos][xpos]) < 0.5f*cradius) {
			surface_damage[ypos][xpos] += 1.0;

			if (rand()%5 == 0) {
				pos2.z = mesh_height[ypos][xpos] + 0.25*cradius;
				create_ground_rubble(pos2, shooter, 0.0, 0.01, 1);
			}
		}
	}
	if (fframe > 0) {pos -= dir*(1.0*cradius);}
}


// ***********************************
// DRAWING CODE
// ***********************************


struct team_stats_t {
	int kills, deaths, score;
	team_stats_t() : kills(0), deaths(0), score(0) {}
	void add(player_state const &s) {kills += s.tot_kills; deaths += s.deaths; score += s.get_score();}
};

void show_user_stats() {

	bool const is_smiley0(spectate && num_smileys > 0 && obj_groups[coll_id[SMILEY]].enabled);
	player_state &sstate(sstates[is_smiley0 ? 0 : CAMERA_ID]);
	static char text[MAX_CHARS];
	
	if (camera_mode == 1 && camera_surf_collide) {
		float chealth(is_smiley0 ? obj_groups[coll_id[SMILEY]].get_obj(0).health : camera_health);
		int const ammo((UNLIMITED_WEAPONS && weapons[sstate.weapon].need_ammo) ? -666 : sstate.p_ammo[sstate.weapon]);
		sprintf(text, "%s %d  %s %d  %s %d  Frags %d  Best %d  Total %d  Deaths %d",
			((chealth < 25.0) ? "HEALTH" : "Health"), int(chealth + 0.5),
			((sstate.shields < 25.0) ? "SHIELDS" : "Shields"), int(sstate.shields + 0.5),
			((sstate.no_ammo()) ? "AMMO" : "Ammo"), ammo,
			sstate.kills, max(sstate.max_kills, -sstate.deaths), sstate.tot_kills, sstate.deaths);
		draw_text(RED, -0.014, -0.012, -0.022, text);

		if (sstate.powerup_time > 0 && sstate.powerup != PU_NONE) {
			sprintf(text, "%is %s", int(float(sstate.powerup_time)/TICKS_PER_SECOND + 0.5f), powerup_names[sstate.powerup].c_str());
			draw_text(get_powerup_color(sstate.powerup), -0.015, -0.012, -0.025, text);
		}
		draw_health_bar(chealth, sstate.shields, float(sstate.powerup_time)/POWERUP_TIME, get_powerup_color(sstate.powerup));
	}
	if (show_scores) {
		team_stats_t tot_stats;

		for (int i = CAMERA_ID; i < num_smileys; ++i) {
			float const yval(0.01f - 0.0014f*(i+1));
			sprintf(text, "%s: K: %i D: %i S: %i TK: %i Score: %i\n",
				sstates[i].name.c_str(), sstates[i].tot_kills, sstates[i].deaths, sstates[i].suicides, sstates[i].team_kills, sstates[i].get_score());
			draw_text(get_smiley_team_color(i), -0.008, yval, -0.02, text);
			tot_stats.add(sstates[i]);
			if (sstates[i].powerup >= 0) {draw_text(get_powerup_color(sstates[i].powerup), -0.009, yval, -0.02, "O");}
		}
		if (teams > 1) {
			vector<team_stats_t> team_stats(teams);
		
			for (int i = CAMERA_ID; i < num_smileys; ++i) {
				team_stats[(i+teams)%teams].add(sstates[i]);
			}
			for (unsigned i = 0; i < (unsigned)teams; ++i) {
				sprintf(text, "Team %u: Kills: %i Deaths: %i Score: %i\n",
					i, team_stats[i].kills, team_stats[i].deaths, team_stats[i].score);
				draw_text(get_smiley_team_color(i), -0.008, 0.01f-0.0014f*(i+num_smileys+1)-0.0008f, -0.02, text);
			}
		} // teams > 1
		sprintf(text, "Total: Kills: %i Deaths: %i Score: %i\n", tot_stats.kills, tot_stats.deaths, tot_stats.score);
		draw_text(WHITE, -0.008, 0.01f-0.0014f*(num_smileys+teams+1)-0.0016f, -0.02, text);
	} // show_scores
}


void show_other_messages() {

	if (msg_params.mtime <= 0) return;
	colorRGBA color(msg_params.color);
	if (msg_params.fade) {color.A *= min(1.0, msg_params.mtime/(0.4*msg_params.itime));}
	point const p(point(-0.008, msg_params.yval, -0.02)/msg_params.size);
	draw_text(color, p.x, 0.005+p.y, p.z, message.c_str(), 1.0);
	msg_params.mtime -= iticks;
}

void print_text_onscreen(string const &text, colorRGBA const &color, float size, int time, int priority, float yval) {
	if (msg_params.mtime > 0 && msg_params.priority > priority) return; // do this before the strcpy
	message    = text;
	msg_params = text_message_params(time, size, color, priority, FADE_MESSAGE_ALPHA, yval);
}
void print_text_onscreen_default(string const &text) {
	print_text_onscreen(text, WHITE, 1.0, MESSAGE_TIME);
}

void print_weapon(int weapon_id) {
	print_text_onscreen(weapons[weapon_id].name, WHITE, 1.0, MESSAGE_TIME/4, 1);
}

void print_debug_text(string const &text, int priority) {
	print_text_onscreen(text, YELLOW, 1.0, MESSAGE_TIME, priority);
}


// ***********************************
// GAME CONTROL/QUERY CODE
// ***********************************


void init_game_state() {

	if (sstates != NULL) return; // make sure this isn't called more than once
	sstates  = new player_state[num_smileys+1]; // most of the parameters are initialized in the constructor
	teaminfo.resize(teams);
	++sstates; // advance pointer so that camera/player can be sstates[-1]
	sstates[CAMERA_ID].name = player_name;
	vector<string> avail_smiley_names;

	for (unsigned i = 0; i < sizeof(all_smiley_names)/sizeof(string); ++i) {
		avail_smiley_names.push_back(all_smiley_names[i]);
	}
	for (int i = 0; i < num_smileys; ++i) {
		init_smiley(i);

		if (!avail_smiley_names.empty()) {
			unsigned const nid(rand()%avail_smiley_names.size());
			sstates[i].name = avail_smiley_names[nid];
			avail_smiley_names.erase(avail_smiley_names.begin() + nid);
		}
		else {
			sstates[i].name = "Smiley " + std::to_string(i);
		}
	}
	for (int i = 0; i < teams; ++i) {
		teaminfo[i].bb.x1 = -X_SCENE_SIZE + DX_VAL;
		teaminfo[i].bb.y1 = -Y_SCENE_SIZE + DY_VAL;
		teaminfo[i].bb.x2 =  X_SCENE_SIZE - DX_VAL;
		teaminfo[i].bb.y2 =  Y_SCENE_SIZE - DY_VAL;
	}
	for (unsigned i = 0; i < team_starts.size(); ++i) {
		assert(team_starts[i].index >= 0 && team_starts[i].index < teams);
		if (team_starts[i].x1 > team_starts[i].x2) swap(team_starts[i].x1, team_starts[i].x2);
		if (team_starts[i].y1 > team_starts[i].y2) swap(team_starts[i].y1, team_starts[i].y2);
		teaminfo[team_starts[i].index].bb = team_starts[i];
	}
	if (game_mode != GAME_MODE_NONE) {change_game_mode();} // handle init_game_mode
}


int get_damage_source(int type, int index, int questioner) {

	assert(questioner >= CAMERA_ID);
	if (index == NO_SOURCE) return questioner; // hurt/killed by nature, call it a suicide
	if (type == DROWNED || type == CRUSHED || type == COLLISION || type == MAT_SPHERE) return questioner; // self damage
	assert(index >= CAMERA_ID);
	if (type == SMILEY || type == BURNED || type == FIRE || type == FELL) return index;
	if (type == CAMERA) return -1;
	assert(type >= 0);
	
	if (type < CAMERA || type == SAWBLADE || type == RAPT_PROJ || type == FREEZE_BOMB) {
		int cid(coll_id[type]);

		if (cid < 0 || cid >= num_groups) {
			for (int i = 0; i < num_groups; ++i) {
				if (obj_groups[i].type == type) {cid = i; break;}
			}
		}
		if (cid >= 0 && cid < num_groups && (unsigned)index < obj_groups[cid].max_objects()) {
			assert(obj_groups[cid].is_enabled());
			int const source(obj_groups[cid].get_obj(index).source);
			if (source == NO_SOURCE) return questioner;
			assert(source >= CAMERA_ID);
			return source;
		}
		cout << "cid = " << cid << ", index = " << index << ", max = " << obj_groups[cid].max_objects() << endl;
		assert(0);
	}
	return index;
}


int player_state::get_drown_time() const {return (game_mode ? (uw_time - DROWN_TIME) : 0);}

bool player_is_drowning() {return (sstates != nullptr && sstates[CAMERA_ID].get_drown_time() > 0);}

bool check_underwater(int who, float &depth) { // check if player is drowning

	assert(who >= CAMERA_ID && who < num_smileys);
	point const pos(get_sstate_pos(who));
	bool const underwater(is_underwater(pos, 1, &depth));
	if (sstates == nullptr) return underwater; // assert(0)?
	player_state &state(sstates[who]);
	int const dtime(state.get_drown_time());

	if (underwater) {
		int const prev_uw_time(state.uw_time); // in ticks
		state.uw_time += iticks;

		if (dtime > 0 && (state.uw_time/TICKS_PER_SECOND > prev_uw_time/TICKS_PER_SECOND)) { // once per second
			float const damage(2.0*fticks*dtime);
			smiley_collision(who, who, zero_vector, pos, damage, DROWNED);
		}
	}
	else {
		if (dtime > 0) {gen_sound(SOUND_GASP, pos);}
		state.uw_time = 0;
	}
	return underwater;
}


void player_fall(int id) { // smileys and the player (camera)

	if (!game_mode) return;
	assert(id >= CAMERA_ID && id < num_smileys);
	float const zvel(sstates[id].last_zvel), dz(sstates[id].last_dz);
	float const vel(-zvel - FALL_HURT_VEL), fall_hurt_dist(FALL_HURT_HEIGHT*CAMERA_RADIUS), dz2(-dz - fall_hurt_dist);
	if (id == CAMERA_ID && -dz > CAMERA_RADIUS) {camera_shake = min(1.0f, -dz/fall_hurt_dist);}
	if (-dz > 0.75*CAMERA_RADIUS) {gen_sound(SOUND_OBJ_FALL, get_sstate_pos(id), min(1.0, -0.1*dz/CAMERA_RADIUS), 0.7);}
	if (dz2 < 0.0) return;
	int source(id);
	if (sstates[id].last_teleporter != NO_SOURCE) {source = sstates[id].last_teleporter;} // give credit for the kill
	smiley_collision(id, source, vector3d(0.0, 0.0, -zvel), get_sstate_pos(id), 5.0*vel*vel, FELL);
}


void update_camera_velocity(vector3d const &v) {
	if (sstates == NULL) return; // assert(0)?
	sstates[CAMERA_ID].velocity = v/(TIMESTEP*fticks);
}


void init_game_mode() {

	if (world_mode == WMODE_INF_TERRAIN && game_mode == GAME_MODE_BUILDINGS) {
		print_text_onscreen("Building Gameplay Mode", WHITE, 2.0, MESSAGE_TIME, 4);
		enter_building_gameplay_mode();
	}
	else {
		string const str(string("Playing ") + ((game_mode == GAME_MODE_FPS) ? "Deathmatch" : "Dodgeball") + " as " + player_name);
		print_text_onscreen(str, WHITE, 2.0, MESSAGE_TIME, 4);
		if (!free_for_all) {teams = 0;}
		free_dodgeballs(1, 1);
		init_sstate(CAMERA_ID, (game_mode == GAME_MODE_FPS));
		if (game_mode == GAME_MODE_DODGEBALL) {init_smileys();}
	
		for (int i = CAMERA_ID; i < num_smileys; ++i) {
			sstates[i].killer = NO_SOURCE; // no one
			if (game_mode == GAME_MODE_FPS) {init_sstate(i, 1);} // ???
		}
	}
	if (play_gameplay_alert && frame_counter > 0) {gen_sound(SOUND_ALERT, get_camera_pos(), 0.5);} // not on first frame
}


void update_game_frame() {

	assert(sstates != NULL);
	sstates[CAMERA_ID].update_camera_frame();
	for (int i = CAMERA_ID; i < num_smileys; ++i) {sstates[i].update_sstate_game_frame(i);}
	proc_delayed_projs(); // after game state update but before processing of fire key

	if (game_mode && sstates != NULL && sstates[CAMERA_ID].freeze_time > 0) {
		add_camera_filter(colorRGBA(FREEZE_COLOR, min(0.5, 0.5*sstates[CAMERA_ID].freeze_time/TICKS_PER_SECOND)), 1, -1, CAM_FILT_FROZEN);
	}
}


void player_state::update_camera_frame() {

	if (powerup_time < 0.0)   {print_text_onscreen("Powerup Expired", WHITE, 1.0, MESSAGE_TIME/2, 1);}
	if (powerup == PU_REGEN ) {camera_health = min(MAX_REGEN_HEALTH, camera_health + 0.1f*fticks);}
	if (powerup == PU_FLIGHT) {camera_flight = 1;}
	kill_time += max(1, iticks);
	next_frame();
}


void player_state::update_sstate_game_frame(int i) {

	if (!animate2) return;
	if (powerup_time == 0) {powerup = PU_NONE;}
	else if (animate2) {
		powerup_time -= iticks;
		if (powerup_time < 0) {powerup_time = 0;}
	}
	if (powerup == PU_REGEN && shields > 1.0) {shields = min(MAX_SHIELDS, shields + 0.075f*fticks);}
		
	if (plasma_loaded && weapon == W_PLASMA) {
		plasma_size += get_fspeed_scale()*fticks*PLASMA_SIZE_INCREASE;
		plasma_size  = min(plasma_size, MAX_PLASMA_SIZE);
	}
	fire_frame = max(0,    (fire_frame - iticks));
	shields    = max(0.0f, (shields    - 0.01f*fticks));
	if (world_mode != WMODE_GROUND) return;

	// check temperature for too hot/too cold
	obj_group const &objg(obj_groups[coll_id[SMILEY]]);
	if (i != CAMERA_ID && (!begin_motion || !objg.enabled)) return;
	obj_type const &objt(object_types[SMILEY]);
	point const pos(get_sstate_pos(i));
	bool const obj_enabled((i == CAMERA_ID && camera_mode == 1) || (i != CAMERA_ID && !objg.get_obj(i).disabled()));
	float const fire_intensity(get_ground_fire_intensity(pos, objt.radius));
	if (fire_intensity > 0.0) {smiley_collision(i, NO_SOURCE, zero_vector, pos, 20.0*fticks*fire_intensity, BURNED);}

	if (temperature < 0.75*objt.min_t) {
		float const damage(1.0*fticks/max(0.001f, (objt.min_t - temperature)/objt.min_t));
		smiley_collision(i, NO_SOURCE, zero_vector, pos, damage, FROZEN);
	}
	if (temperature > 0.75*objt.max_t) {
		float const damage(2.0*fticks/max(0.001f, (objt.max_t - temperature)/objt.max_t));
		smiley_collision(i, NO_SOURCE, zero_vector, pos, damage, BURNED);
		if (obj_enabled && (rand()&3) == 0) {gen_smoke(pos);}
	}
	if (atmosphere < 0.2) {
		float const damage(1.0*fticks/max(atmosphere, 0.01f));
		smiley_collision(i, NO_SOURCE, zero_vector, pos, damage, SUFFOCATED);
	}
	if (powerup != PU_NONE && powerup_time > 0 && obj_enabled) {add_dynamic_light(1.3, pos, get_powerup_color(powerup));}

	if (SMILEY_GAS && game_mode == GAME_MODE_FPS && obj_enabled && powerup == PU_SHIELD && powerup_time > (int)INIT_PU_SH_TIME && !(rand()&31)) {
		vector3d const dir(get_sstate_dir(i)), vel(velocity*0.5 - dir*1.2);
		point const spos(pos - dir*get_sstate_radius(i)); // generate gas
		gen_arb_smoke(spos, GREEN, vel, rand_uniform(0.01, 0.05), rand_uniform(0.3, 0.7), rand_uniform(0.2, 0.6), 10.0, i, GASSED, 0);
	}
}


void player_state::free_balls() {

	if (balls.empty()) return;
	obj_group &objg(obj_groups[coll_id[BALL]]);

	if (objg.enabled) {
		for (unsigned j = 0; j < balls.size(); ++j) {
			unsigned const index(balls[j]);
			assert(objg.get_obj(index).disabled());
			objg.get_obj(index).status = 0;
		}
	}
	balls.clear();
	p_weapons[W_BALL] = 0;
	p_ammo[W_BALL]    = 0;
	if (weapon == W_BALL) {weapon = W_UNARMED;}
}


void free_dodgeballs(bool camera, bool smileys) {

	if (sstates == NULL) return;
	for (int i = (camera ? CAMERA_ID : 0); i < (smileys ? num_smileys : 0); ++i) {sstates[i].free_balls();}
}


void gamemode_rand_appear() {

	if (!game_mode) return;
	gen_smiley_or_player_pos(surface_pos, CAMERA_ID);
	camera_last_pos = surface_pos;
	free_dodgeballs(1, 0);
	init_sstate(CAMERA_ID, (game_mode == GAME_MODE_FPS), 1); // show_appear_effect=1
}


void change_game_mode() {

	int types[] = {HEALTH, SHIELD, POWERUP, WEAPON, AMMO};
	unsigned const ntypes(UNLIMITED_WEAPONS ? 3 : 5);
	game_mode = game_mode % 3; // 0/1/2
	for (unsigned i = 0; i < ntypes; ++i) {obj_groups[coll_id[types[i]]].set_enable(game_mode == GAME_MODE_FPS);}
	obj_groups[coll_id[BALL]].set_enable(game_mode == GAME_MODE_DODGEBALL);

	if (game_mode == GAME_MODE_DODGEBALL) { // dodgeball mode
		assert(sstates != NULL);
		sstates[CAMERA_ID].switch_weapon(1, 1); // player switch to dodgeball

		for (unsigned i = 0; i < ntypes; ++i) {
			int const group(coll_id[types[i]]);
			assert(group >= 0 && group < NUM_TOT_OBJS);
			obj_groups[group].free_objects();
		}
	}
	else if (game_mode == GAME_MODE_NONE) { // non gameplay mode
		assert(coll_id[BALL] >= 0 && coll_id[BALL] < NUM_TOT_OBJS);
		obj_groups[coll_id[BALL]].free_objects();
	}
	if (game_mode) {init_game_mode();}
	else {free_dodgeballs(1, 1);}
}


bool has_keycard_id(int source, unsigned keycard_id) {
	if (source < CAMERA_ID || source >= num_smileys) return 0; // can be NO_SOURCE, etc.
	set<unsigned> const &keycards(sstates[source].keycards);
	return (keycards.find(keycard_id) != keycards.end());
}



