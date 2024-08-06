// 3D World - Building Gameplay Logic
// by Frank Gennari 12/29/21

#include "3DWorld.h"
#include "function_registry.h"
#include "buildings.h"
#include "openal_wrap.h"

using std::string;

float const THROW_VELOCITY = 0.0050;
float const ALERT_THRESH   = 0.08; // min sound alert level for AIs
float const PLAYER_RESPAWN = 5.0; // in seconds

bool do_room_obj_pickup(0), use_last_pickup_object(0), show_bldg_pickup_crosshair(0), player_near_toilet(0), player_attracts_flies(0), player_wait_respawn(0);
bool city_action_key(0), can_do_building_action(0);
int can_pickup_bldg_obj(0), player_in_elevator(0); // player_in_elevator: 0=no, 1=in, 2=in + doors closed, 3=moving
float office_chair_rot_rate(0.0), cur_building_sound_level(0.0);
carried_item_t player_held_object;
bldg_obj_type_t bldg_obj_types[NUM_ROBJ_TYPES];
vector<sphere_t> cur_sounds; // radius = sound volume

extern bool camera_in_building, player_is_hiding, player_in_unlit_room, disable_blood;
extern int window_width, window_height, display_framerate, display_mode, game_mode, building_action_key, frame_counter, player_in_basement, player_in_water, animate2;
extern float fticks, CAMERA_RADIUS;
extern double tfticks, camera_zh;
extern colorRGBA vignette_color;
extern building_params_t global_building_params;
extern building_t const *player_building;


void place_player_at_xy(float xval, float yval);
void show_key_icon(vector<colorRGBA> const &key_colors);
void show_flashlight_icon();
void show_pool_cue_icon();
bool is_shirt_model(room_object_t const &obj);
bool is_pants_model(room_object_t const &obj);
bool player_at_full_health();
bool player_is_thirsty();
void register_fly_attract(bool no_msg);
room_obj_or_custom_item_t steal_from_car(room_object_t const &car, float floor_spacing, bool do_pickup);
float get_filing_cabinet_drawers(room_object_t const &c, vect_cube_t &drawers);
void reset_creepy_sounds();
void clear_building_water_splashes();

bool in_building_gameplay_mode() {return (game_mode == GAME_MODE_BUILDINGS);} // replaces dodgeball mode

// object types/pickup

void setup_bldg_obj_types() {
	static bool was_setup(0);
	if (was_setup) return; // nothing to do
	was_setup = 1;
	// player_coll, ai_coll, rat_coll, pickup, attached, is_model, lg_sm, value, weight, name [capacity]
	//                                                pc ac rc pu at im ls value  weight  name
	bldg_obj_types[TYPE_TABLE     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 1, 70.0,  40.0,  "table");
	bldg_obj_types[TYPE_CHAIR     ] = bldg_obj_type_t(0, 1, 1, 1, 0, 0, 1, 50.0,  25.0,  "chair"); // skip player collisions because they can be in the way and block the path in some rooms
	bldg_obj_types[TYPE_STAIR     ] = bldg_obj_type_t(1, 0, 1, 0, 1, 0, 1, 0.0,   0.0,   "stair");
	bldg_obj_types[TYPE_STAIR_WALL] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 1, 0.0,   0.0,   "stairs wall");
	bldg_obj_types[TYPE_PG_WALL   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "parking garage wall"); // detail object
	bldg_obj_types[TYPE_PG_PILLAR ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "support pillar"); // detail object
	bldg_obj_types[TYPE_PG_BEAM   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "ceiling beam"); // detail object
	bldg_obj_types[TYPE_ELEVATOR  ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "elevator");
	bldg_obj_types[TYPE_PARK_SPACE] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 0, 0.0,   0.0,   "parking space"); // detail object
	bldg_obj_types[TYPE_RAMP      ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "ramp"); // detail object
	bldg_obj_types[TYPE_LIGHT     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 0, 40.0,  5.0,   "light");
	bldg_obj_types[TYPE_RUG       ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 50.0,  20.0,  "rug");
	bldg_obj_types[TYPE_PICTURE   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 100.0, 1.0,   "picture"); // should be random value
	bldg_obj_types[TYPE_WBOARD    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 50.0,  25.0,  "whiteboard");
	bldg_obj_types[TYPE_BOOK      ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 3, 10.0,  1.0,   "book");
	bldg_obj_types[TYPE_BCASE     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 3, 150.0, 100.0, "bookcase"); // Note: can't pick up until bookcase can be expanded and books taken off
	bldg_obj_types[TYPE_TCAN      ] = bldg_obj_type_t(0, 1, 1, 1, 0, 0, 2, 12.0,  2.0,   "trashcan"); // skip player collisions because they can be in the way and block the path in some rooms
	bldg_obj_types[TYPE_DESK      ] = bldg_obj_type_t(1, 1, 1, 0, 0, 0, 3, 100.0, 80.0,  "desk"); // drawers are small items
	bldg_obj_types[TYPE_BED       ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 3, 300.0, 200.0, "bed"); // pillows are small, and the rest is large
	bldg_obj_types[TYPE_WINDOW    ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1, 0.0,   0.0,   "window");
	bldg_obj_types[TYPE_BLOCKER   ] = bldg_obj_type_t(0, 0, 0, 0, 0, 0, 0, 0.0,   0.0,   "<blocker>");  // not a drawn object; block other objects, but not the player or AI
	bldg_obj_types[TYPE_COLLIDER  ] = bldg_obj_type_t(1, 1, 1, 0, 0, 0, 0, 0.0,   0.0,   "<collider>"); // not a drawn object; block the player and AI
	bldg_obj_types[TYPE_CUBICLE   ] = bldg_obj_type_t(0, 0, 1, 0, 1, 0, 1, 500.0, 250.0, "cubicle"); // skip collisions because they have their own colliders, but include rat coll
	bldg_obj_types[TYPE_STALL     ] = bldg_obj_type_t(1, 1, 1, 1, 1, 0, 1, 40.0,  20.0,  "bathroom divider"); // can pick up short sections of bathroom stalls (urinal dividers)
	bldg_obj_types[TYPE_SIGN      ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 3, 10.0,  1.0,   "sign");
	bldg_obj_types[TYPE_COUNTER   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 3, 0.0,   0.0,   "kitchen counter");
	bldg_obj_types[TYPE_CABINET   ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 3, 0.0,   0.0,   "kitchen cabinet");
	bldg_obj_types[TYPE_KSINK     ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 3, 0.0,   0.0,   "kitchen sink");
	bldg_obj_types[TYPE_BRSINK    ] = bldg_obj_type_t(1, 1, 0, 0, 1, 0, 1, 0.0,   0.0,   "bathroom sink"); // for office building bathrooms
	bldg_obj_types[TYPE_PLANT     ] = bldg_obj_type_t(0, 1, 1, 1, 0, 0, 3, 18.0,  8.0,   "potted plant"); // AI collides with plants on the floor
	bldg_obj_types[TYPE_DRESSER   ] = bldg_obj_type_t(1, 1, 1, 0, 0, 0, 3, 120.0, 110.0, "dresser"); // Note: can't pick up until drawers can be opened and items removed from them
	bldg_obj_types[TYPE_NIGHTSTAND] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 3, 60.0,  45.0,  "nightstand");
	bldg_obj_types[TYPE_FLOORING  ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1, 0.0,   0.0,   "flooring");
	// closets can't be picked up, but they can block a pickup; marked as large because small objects are not modified; marked as is_model because closets can contain lamps
	bldg_obj_types[TYPE_CLOSET    ] = bldg_obj_type_t(1, 1, 1, 1, 1, 1, 1, 0.0,   0.0,   "closet");
	bldg_obj_types[TYPE_WALL_TRIM ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 0, 0.0,   0.0,   "wall trim"); // detail object
	bldg_obj_types[TYPE_RAILING   ] = bldg_obj_type_t(1, 0, 0, 0, 1, 0, 2, 0.0,   0.0,   "railing");
	bldg_obj_types[TYPE_CRATE     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2, 10.0,  12.0,  "crate"); // should be random value
	bldg_obj_types[TYPE_BOX       ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2, 5.0,   8.0,   "box");   // should be random value
	bldg_obj_types[TYPE_MIRROR    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 40.0,  15.0,  "mirror");
	bldg_obj_types[TYPE_SHELVES   ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2, 0.0,   0.0,   "shelves");
	bldg_obj_types[TYPE_KEYBOARD  ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 15.0,  2.0,   "keyboard");
	bldg_obj_types[TYPE_SHOWER    ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 1, 0.0,   0.0,   "shower"); // technically large + small, but only large objects are dynamically updated
	bldg_obj_types[TYPE_RDESK     ] = bldg_obj_type_t(1, 1, 1, 0, 0, 0, 1, 800.0, 300.0, "reception desk");
	bldg_obj_types[TYPE_BOTTLE    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 1.0,   1.0,   "bottle", 1); // single use
	bldg_obj_types[TYPE_WINE_RACK ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 3, 75.0,  40.0,  "wine rack");
	bldg_obj_types[TYPE_COMPUTER  ] = bldg_obj_type_t(0, 1, 1, 1, 0, 0, 2, 500.0, 20.0,  "computer"); // rats can collide with computers
	bldg_obj_types[TYPE_MWAVE     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 100.0, 50.0,  "microwave oven");
	bldg_obj_types[TYPE_PAPER     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.0,   0.0,   "sheet of paper"); // will have a random value that's often 0
	bldg_obj_types[TYPE_BLINDS    ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1, 50.0,  7.0,   "window blinds");
	bldg_obj_types[TYPE_PEN       ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.10,  0.02,  "pen");
	bldg_obj_types[TYPE_PENCIL    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.10,  0.02,  "pencil");
	bldg_obj_types[TYPE_PAINTCAN  ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 12.0,  8.0,   "paint can");
	bldg_obj_types[TYPE_LG_BALL   ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 15.0,  1.2,   "ball");
	bldg_obj_types[TYPE_HANGER_ROD] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 10.0,  5.0,   "hanger rod");
	bldg_obj_types[TYPE_DRAIN     ] = bldg_obj_type_t(0, 0, 1, 0, 1, 0, 2, 0.0,   0.0,   "drain pipe");
	bldg_obj_types[TYPE_MONEY     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 20.0,  0.0,   "pile of money"); // $20 bills
	bldg_obj_types[TYPE_PHONE     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 200.0, 0.1,   "cell phone");
	bldg_obj_types[TYPE_TPROLL    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.25,  0.1,   "TP roll", 200);
	bldg_obj_types[TYPE_SPRAYCAN  ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 2.0,   1.0,   "spray paint", 5000);
	bldg_obj_types[TYPE_MARKER    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.20,  0.05,  "marker",      10000);
	bldg_obj_types[TYPE_BUTTON    ] = bldg_obj_type_t(0, 0, 0, 1, 1, 0, 2, 1.0,   0.05,  "button");
	bldg_obj_types[TYPE_CRACK     ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 2, 0.0,   0.0,   "crack");
	bldg_obj_types[TYPE_SWITCH    ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 2, 10.0,  0.1,   "switch");
	bldg_obj_types[TYPE_BREAKER   ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 2, 20.0,  0.1,   "breaker");
	bldg_obj_types[TYPE_PLATE     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 6.0,   0.25,  "plate");
	bldg_obj_types[TYPE_LAPTOP    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 600.0, 8.0,   "laptop");
	bldg_obj_types[TYPE_FPLACE    ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 1, 0.0,   2000.0,"fireplace");
	bldg_obj_types[TYPE_LBASKET   ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2, 12.0,  2.0,   "laundry basket");
	bldg_obj_types[TYPE_WHEATER   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 300.0, 500.0, "water heater"); // detail object
	bldg_obj_types[TYPE_FURNACE   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 500.0, 200.0, "furnace"); // detail object
	bldg_obj_types[TYPE_TAPE      ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 2.0,   0.4,   "duct tape", 1000);
	bldg_obj_types[TYPE_OUTLET    ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 0, 7.0,   0.1,   "outlet"); // detail object
	bldg_obj_types[TYPE_PIPE      ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "pipe"); // detail object; only vertical pipes are collidable
	bldg_obj_types[TYPE_CURB      ] = bldg_obj_type_t(0, 0, 1, 0, 1, 0, 0, 0.0,   100.0, "curb"); // for parking garages; only rats collide
	bldg_obj_types[TYPE_BRK_PANEL ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 2, 1000.0,100.0, "breaker panel");
	bldg_obj_types[TYPE_VENT      ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 0, 20.0,  2.0,   "vent"); // detail object
	bldg_obj_types[TYPE_ATTIC_DOOR] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 2, 100.0, 50.0,  "attic door"); // door/ladder
	bldg_obj_types[TYPE_CHIMNEY   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 2, 1000.0,1000.0,"chimney"); // interior chimney in attic
	bldg_obj_types[TYPE_DUCT      ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0, 0.0,   0.0,   "duct"); // detail object
	bldg_obj_types[TYPE_TOY       ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 2.0,   0.1,   "toy"); // plastic ring stack
	bldg_obj_types[TYPE_DRESS_MIR ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 1, 100.0, 30.0,  "mirror");
	bldg_obj_types[TYPE_PAN       ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 15.0,  4.0,   "frying pan");
	bldg_obj_types[TYPE_VASE      ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 20.0,  1.0,   "vase");
	bldg_obj_types[TYPE_URN       ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 40.0,  2.0,   "urn");
	bldg_obj_types[TYPE_FCABINET  ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 3, 100.0, 220.0, "filing cabinet"); // body is large, drawers and their contents are small
	bldg_obj_types[TYPE_STAPLER   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 10.0,  0.6,   "stapler");
	bldg_obj_types[TYPE_WIND_SILL ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1,  0.0,  0.0,   "window sill");
	bldg_obj_types[TYPE_EXT_STEP  ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1,  0.0,  0.0,   "exterior step");
	bldg_obj_types[TYPE_BALCONY   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 0,  0.0,  0.0,   "balcony"); // exterior object
	bldg_obj_types[TYPE_SPRINKLER ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 0,  0.0,  0.0,   "fire sprinkler"); // detail object
	bldg_obj_types[TYPE_FEXT_MOUNT] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 2,  0.0,  0.0,   "fire extinguisher mount");
	bldg_obj_types[TYPE_FEXT_SIGN ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,  5.0,  0.2,   "fire extinguisher sign");
	bldg_obj_types[TYPE_PIZZA_BOX ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 10.0,  1.0,   "box of pizza");
	bldg_obj_types[TYPE_PIZZA_TOP ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.05,  0.01,  "pizza topping"); // pepperoni, tomato, pepper, olive, etc.; may need sub-types with diff names
	bldg_obj_types[TYPE_TEESHIRT  ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 10.0,  0.25,  "tee shirt");
	bldg_obj_types[TYPE_PANTS     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 16.0,  0.50,  "jeans");
	bldg_obj_types[TYPE_BLANKET   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 20.0,  2.0,   "blanket");
	bldg_obj_types[TYPE_SERVER    ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2, 10000, 400.00,"server"); // small because it's in a windowless room; too heavy for inventory
	bldg_obj_types[TYPE_POOL_BALL ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,  2.0,  0.37,  "pool ball");
	bldg_obj_types[TYPE_POOL_CUE  ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 20.0,  1.2,   "pool cue");
	bldg_obj_types[TYPE_WALL_MOUNT] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 2,  0.0,  0.0,   "wall mounting bracket");
	bldg_obj_types[TYPE_POOL_TILE ] = bldg_obj_type_t(1, 0, 0, 0, 1, 0, 2,  0.0,  0.0,   "pool tile");
	bldg_obj_types[TYPE_POOL_FLOAT] = bldg_obj_type_t(1, 0, 0, 1, 0, 0, 2, 10.0,  1.0,   "pool float");
	bldg_obj_types[TYPE_BENCH     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2, 40.0,  30.0,  "bench");
	bldg_obj_types[TYPE_DIV_BOARD ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 2,  0.0,  100.0, "diving board");
	bldg_obj_types[TYPE_FALSE_DOOR] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1,  0.0,  0.0,   "door");
	bldg_obj_types[TYPE_FLASHLIGHT] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 15.0,  1.0,   "flashlight");
	bldg_obj_types[TYPE_CANDLE    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,  1.0,  0.4,   "candle", 10000);
	bldg_obj_types[TYPE_CAMERA    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 150.0, 1.5,   "security camera");
	bldg_obj_types[TYPE_CLOCK     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,  20.0, 1.0,   "clock");
	bldg_obj_types[TYPE_DOWNSPOUT ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1,  0.0,  0.0,   "downspout");
	bldg_obj_types[TYPE_SHELFRACK ] = bldg_obj_type_t(1, 1, 1, 1, 1, 0, 1,  0.0,  0.0,   "shelf rack");
	bldg_obj_types[TYPE_CHIM_CAP  ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 1,  0.0,  0.0,   "exterior step");
	bldg_obj_types[TYPE_FOOD_BOX  ] = bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2,  8.0,  1.0,   "box of food");
	bldg_obj_types[TYPE_SAFE      ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 2, 250.0, 300.0, "safe");
	bldg_obj_types[TYPE_LADDER    ] = bldg_obj_type_t(1, 0, 0, 0, 1, 0, 1,  0.0,  0.0,   "ladder");
	bldg_obj_types[TYPE_CHECKOUT  ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 1,  0.0,  300.0, "checkout counter");
	bldg_obj_types[TYPE_FISHTANK  ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 100.0, 160.0, "fish tank");
	bldg_obj_types[TYPE_LAVALAMP  ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 30.0,  3.0,   "lava lamp");
	bldg_obj_types[TYPE_SHOWERTUB ] = bldg_obj_type_t(1, 1, 1, 0, 1, 0, 1,  0.0,  0.0,   "shower"); // this is the shower part of a shower+tub combo; technically large and small
	bldg_obj_types[TYPE_TRASH     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,  0.0,  0.1,   "trash");
	bldg_obj_types[TYPE_VALVE     ] = bldg_obj_type_t(0, 0, 0, 0, 1, 0, 0,  0.0,  0.0,   "valve"); // detail object
	bldg_obj_types[TYPE_DBG_SHAPE ] = bldg_obj_type_t(0, 0, 0, 0, 0, 0, 1,  0.0,  0.0,   "debug shape"); // small (optimization)
	// player_coll, ai_coll, rat_coll, pickup, attached, is_model, lg_sm, value, weight, name [capacity]
	// 3D models
	bldg_obj_types[TYPE_TOILET    ] = bldg_obj_type_t(1, 1, 1, 1, 1, 1, 0, 120.0, 88.0,  "toilet");
	bldg_obj_types[TYPE_SINK      ] = bldg_obj_type_t(1, 1, 1, 1, 1, 1, 0, 80.0,  55.0,  "sink");
	bldg_obj_types[TYPE_TUB       ] = bldg_obj_type_t(1, 1, 1, 0, 1, 1, 1, 250.0, 200.0, "bathtub"); // large object for sides
	bldg_obj_types[TYPE_FRIDGE    ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 700.0, 300.0, "refrigerator"); // no pickup, too large and may want to keep it for future hunger bar
	bldg_obj_types[TYPE_STOVE     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 400.0, 150.0, "stove");
	bldg_obj_types[TYPE_TV        ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 1, 400.0, 70.0,  "TV");
	bldg_obj_types[TYPE_MONITOR   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 1, 250.0, 15.0,  "computer monitor");
	bldg_obj_types[TYPE_COUCH     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 600.0, 300.0, "couch");
	bldg_obj_types[TYPE_OFF_CHAIR ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 150.0, 60.0,  "office chair");
	bldg_obj_types[TYPE_URINAL    ] = bldg_obj_type_t(1, 1, 1, 1, 1, 1, 0, 100.0, 80.0,  "urinal");
	bldg_obj_types[TYPE_LAMP      ] = bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0, 25.0,  12.0,  "lamp");
	bldg_obj_types[TYPE_WASHER    ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 300.0, 150.0, "washer");
	bldg_obj_types[TYPE_DRYER     ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 300.0, 160.0, "dryer");
	// keys are special because they're potentially either a small object or an object model (in a drawer)
	bldg_obj_types[TYPE_KEY       ] = bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.0,   0.05,  "room key"); // drawn as an object, not a model
	bldg_obj_types[TYPE_HANGER    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 0, 0.25,  0.05,  "clothes hanger");
	bldg_obj_types[TYPE_CLOTHES   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 0, 10.0,  0.25,  "clothes"); // teeshirt, shirt, pants, etc.
	bldg_obj_types[TYPE_FESCAPE   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 1, 0, 10000, 4000,  "fire escape"); // technically exterior, not interior
	bldg_obj_types[TYPE_CUP       ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 0, 5.0,   0.2,   "cup");
	bldg_obj_types[TYPE_TOASTER   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 2, 20.0,  2.5,   "toaster"); // mixed model and object geom
	bldg_obj_types[TYPE_HOOD      ] = bldg_obj_type_t(0, 0, 1, 0, 1, 1, 0, 200.0, 40.0,  "ventilation hood");
	bldg_obj_types[TYPE_RCHAIR    ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 120.0, 45.0,  "rocking chair");
	bldg_obj_types[TYPE_SILVER    ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 2, 10.0,  0.2,   "silverware"); // drawn as a small static object when expanded (in a drawer)
	bldg_obj_types[TYPE_TOY_MODEL ] = bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0, 4.0,   0.2,   "toy"); // plastic ring stack
	bldg_obj_types[TYPE_CEIL_FAN  ] = bldg_obj_type_t(0, 0, 0, 0, 1, 1, 0, 200.0, 25.0,  "ceiling fan");
	bldg_obj_types[TYPE_FIRE_EXT  ] = bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0, 25.0,  10.0,  "fire extinguisher", 250);
	bldg_obj_types[TYPE_FOLD_SHIRT] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 2, 10.0,  0.25,  "folded shirt"); // drawn as a small static object when expanded (in a drawer)
	bldg_obj_types[TYPE_PLANT_MODEL]= bldg_obj_type_t(0, 1, 1, 1, 0, 1, 0, 15.0,  5.0,   "potted plant"); // AI collides with plants on the floor
	bldg_obj_types[TYPE_POOL_TABLE] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 400.0, 250.0, "pool table");
	bldg_obj_types[TYPE_POOL_LAD  ] = bldg_obj_type_t(0, 0, 1, 0, 1, 1, 0, 200.0, 35.0,  "pool ladder");
	bldg_obj_types[TYPE_BAR_STOOL ] = bldg_obj_type_t(1, 1, 1, 1, 0, 1, 0, 100.0, 40.0,  "bar stool");
	bldg_obj_types[TYPE_PADLOCK   ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 0, 10.0,  0.2,   "padlock");
	bldg_obj_types[TYPE_CASHREG   ] = bldg_obj_type_t(1, 1, 1, 0, 1, 1, 0, 1000,  200,   "cash register");
	bldg_obj_types[TYPE_WFOUNTAIN ] = bldg_obj_type_t(1, 1, 1, 0, 1, 1, 0, 200,   80,    "water fountain");
	bldg_obj_types[TYPE_BANANA    ] = bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0, 0.25,  0.3,   "banana");
	bldg_obj_types[TYPE_BAN_PEEL  ] = bldg_obj_type_t(1, 0, 1, 1, 0, 1, 0, 0.0,   0.05,  "banana peel");
	// animals; not room objects
	bldg_obj_types[TYPE_RAT       ] = bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0, 8.99,  1.0,   "rat"); // can be picked up
	bldg_obj_types[TYPE_ROACH     ] = bldg_obj_type_t(0, 0, 0, 1, 0, 1, 0, 0.0,   0.01,  "cockroach");
	bldg_obj_types[TYPE_SPIDER    ] = bldg_obj_type_t(0, 0, 1, 0, 0, 0, 0, 0.0,   0.1,   "spider");
	bldg_obj_types[TYPE_SNAKE     ] = bldg_obj_type_t(0, 0, 1, 0, 0, 0, 0, 50.00, 4.0,   "snake");
	bldg_obj_types[TYPE_INSECT    ] = bldg_obj_type_t(0, 0, 0, 0, 0, 1, 0, 0.0,   0.01,  "insect");
	//                                                pc ac rc pu at im ls value  weight  name [capacity]
}

bldg_obj_type_t const &get_room_obj_type(room_object_t const &obj) {
	assert(obj.type < NUM_ROBJ_TYPES);
	return bldg_obj_types[obj.type];
}
float carried_item_t::get_remaining_capacity_ratio() const {
	unsigned const capacity(get_room_obj_type(*this).capacity);
	return ((capacity == 0) ? 1.0 : (1.0 - float(min(use_count, capacity))/float(capacity))); // Note: zero capacity is unlimited and ratio returned is always 1.0
}

float const mattress_weight(80.0), sheets_weight(4.0), pillow_weight(1.0);

rand_gen_t rgen_from_obj(room_object_t const &obj) {
	rand_gen_t rgen;
	rgen.set_state((12345*abs(obj.x1()) + obj.obj_id), 67890*abs(obj.y1()));
	return rgen;
}
float get_paper_value(room_object_t const &obj) {
	rand_gen_t rgen(rgen_from_obj(obj));
	if (rgen.rand_float() >= 0.25) return 0.0; // only 25% of papers have some value
	float const val_mult((rgen.rand_float() < 0.25) ? 10.0 : 1.0); // 25% of papers have higher value
	return val_mult*(2 + (rgen.rand()%10))*(1 + (rgen.rand()%10));
}
bldg_obj_type_t get_taken_obj_type(room_object_t const &obj) {
	if (obj.type == TYPE_PICTURE && obj.taken_level > 0) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 20.0, 6.0, "picture frame");} // second item to take from picture
	if (obj.type == TYPE_TPROLL  && obj.taken_level > 0) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 6.0,  0.5, "toilet paper holder");} // second item to take from tproll

	if (obj.type == TYPE_BED) { // player_coll, ai_coll, rat_coll, pickup, attached, is_model, lg_sm, value, weight, name
		if (obj.taken_level > 1) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 250.0, mattress_weight, "mattress"  );} // third item to take from bed
		if (obj.taken_level > 0) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 1, 80.0,  sheets_weight,   "bed sheets");} // second item to take from bed
		return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 20.0, pillow_weight, "pillow"); // first item to take from bed
	}
	if (obj.type == TYPE_PLANT && !(obj.flags & RO_FLAG_ADJ_BOT)) { // plant not on a table/desk
		if (obj.taken_level > 1) {return bldg_obj_type_t(0, 0, 1, 1, 0, 0, 1, 10.0, 10.0, "plant pot");} // third item to take
		if (obj.taken_level > 0) {return bldg_obj_type_t(0, 0, 1, 1, 0, 0, 1, 1.0,  10.0, "dirt"     );} // second item to take
		return bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 25.0, 5.0, "plant"); // first item to take
	}
	if (obj.type == TYPE_TOY) { // take one ring at a time then the base (5 parts)
		if (obj.taken_level < 4) {return bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 0.5, 0.025, "toy ring");}
		// else take the toy base
	}
	if (obj.type == TYPE_TABLE    && obj.is_broken ()) {return bldg_obj_type_t(1, 1, 1, 1, 0, 0, 1,  25.0, 40.0, "broken table");}
	if (obj.type == TYPE_COMPUTER && obj.is_broken ()) {return bldg_obj_type_t(0, 0, 1, 1, 0, 0, 2, 100.0, 20.0, "old computer");}
	if (obj.type == TYPE_BOX      && obj.is_open   ()) {return bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2,   0.0, 0.05, "opened box"  );}
	if (obj.type == TYPE_CRATE    && obj.is_open   ()) {return bldg_obj_type_t(1, 1, 1, 1, 0, 0, 2,   2.0, 0.5,  "opened crate");}
	if (obj.type == TYPE_TV       && obj.is_broken ()) {return bldg_obj_type_t(1, 1, 1, 1, 0, 1, 1,  20.0, 70.0, "broken TV"   );}
	if (obj.type == TYPE_MONITOR  && obj.is_broken ()) {return bldg_obj_type_t(1, 1, 1, 1, 0, 1, 1,  10.0, 15.0, "broken computer monitor");}
	if (obj.type == TYPE_LIGHT    && obj.is_broken ()) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 0,  20.0,  5.0, "flickering light");}
	if (obj.type == TYPE_LIGHT    && obj.is_broken2()) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 0,  10.0,  5.0, "broken light");}
	if (obj.type == TYPE_RAT      && obj.is_broken ()) {return bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0,   0.0,  1.0, "cooked/dead rat");}
	if (obj.type == TYPE_ROACH    && obj.is_broken ()) {return bldg_obj_type_t(0, 0, 0, 1, 0, 1, 0,   0.0, 0.01, "dead cockroach");} // same stats as live cockroach
	if (obj.type == TYPE_FIRE_EXT && obj.is_broken ()) {return bldg_obj_type_t(0, 0, 1, 1, 0, 1, 0,  20.0, 10.0, "empty fire extinguisher");}
	if (obj.type == TYPE_CANDLE   && obj.is_used   ()) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,   0.5,  0.4, "used candle");}
	if (obj.type == TYPE_POOL_FLOAT&&obj.is_broken ()) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2,   5.0,  1.0, "deflated pool float");} // half value, no player coll

	if (obj.type == TYPE_INSECT) { // unused
		bool const is_fly(obj.is_hanging());
		string const name(string(obj.is_broken() ? "dead " : "") + (is_fly ? "fly" : "cockroach"));
		return bldg_obj_type_t(0, 0, 0, 1, 0, !is_fly, (is_fly ? 2 : 0), 0.0, 0.01, name);
	}
	if (obj.type == TYPE_BOTTLE) {
		bottle_params_t const &bparams(bottle_params[obj.get_bottle_type()]);
		bldg_obj_type_t type(0, 0, 0, 1, 0, 0, 2,  bparams.value, 1.0, bparams.name);

		if (obj.is_bottle_empty()) {
			type.name    = "empty " + type.name;
			type.weight *= 0.25;
			type.value   = 0.0;
		}
		else if (!(obj.flags & RO_FLAG_NO_CONS)) {
			type.name = "consumable " + type.name;
		}
		return type;
	}
	if (obj.type == TYPE_PIZZA_BOX) {
		if (obj.taken_level == 1) {return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 0.0,  0.25, "empty pizza box");} // whether or not the box is open
		if (obj.is_open()) { // take the pizza
			// pizza slice? what about using TYPE_PIZZA_TOP?
			return bldg_obj_type_t(0, 0, 0, 1, 0, 0, 2, 10.0, 0.75, "pizza");
		}
	} // else take the entire box with the pizza
	// default value, name may be modified below
	bldg_obj_type_t type(get_room_obj_type(obj));

	if (obj.type == TYPE_LG_BALL) {
		ball_type_t const &bt(obj.get_ball_type());
		type.name   = bt.name; // use a more specific type name
		type.value  = bt.value;
		type.weight = bt.weight;
	}
	else if (obj.type == TYPE_CLOTHES) {
		if      (is_shirt_model(obj)) {type.name = "shirt";}
		else if (is_pants_model(obj)) {type.name = "pants";}
	}
	else if (obj.type == TYPE_MIRROR && (obj.flags & RO_FLAG_IS_HOUSE)) {
		type.name = "medicine cabinet";
	}
	else if (obj.type == TYPE_PAPER) {
		float const value(get_paper_value(obj));
		if      (value >= 500.0) {type.name = "top secret document";}
		else if (value >= 100.0) {type.name = "confidential document";}
		else if (value >    0.0) {type.name = "valuable document";}
	}
	else if (obj.type == TYPE_POOL_BALL) {
		unsigned const number(obj.item_flags); // starts from 0; cue ball is 15
		assert(number < 16);
		std::ostringstream oss;
		if (number == 15) {oss << "cue ball";} else {oss << (number+1) << " ball";}
		type.name = oss.str();
	}
	else if (obj.type == TYPE_FOOD_BOX) {
		string const &food_name(obj.get_food_box_name());
		if (!food_name.empty()) {type.name = food_name;}
	}
	else if (obj.type == TYPE_KEY) {
		assert(obj.obj_id < NUM_LOCK_COLORS);
		type.name = lock_color_names[obj.obj_id] + " " + type.name;
	}
	return type;
}
bool is_refillable(room_object_t const &obj) {return (obj.type == TYPE_FIRE_EXT);}

float get_obj_value(room_object_t const &obj) {
	float value(get_taken_obj_type(obj).value);
	if (obj.type == TYPE_CRATE || obj.type == TYPE_BOX) {value *= (1 + (rgen_from_obj(obj).rand() % 20));}
	else if (obj.type == TYPE_PAPER) {value = get_paper_value(obj);}
	else if (obj.type == TYPE_MONEY) {
		unsigned const num_bills(round_fp(obj.dz()/(0.01*obj.get_length())));
		value *= num_bills;
	}
	if (obj.is_used() && !is_refillable(obj)) {value = 0.01*floor(50.0*value);} // used objects have half value, rounded down to the nearest cent
	return value;
}
float get_obj_weight(room_object_t const &obj) {
	return get_taken_obj_type(obj).weight; // constant per object type, for now, but really should depend on object size/volume
}
bool is_consumable(room_object_t const &obj) {
	if (!in_building_gameplay_mode() || obj.type != TYPE_BOTTLE || obj.is_bottle_empty() || (obj.flags & RO_FLAG_NO_CONS)) return 0; // not consumable
	unsigned const bottle_type(obj.get_bottle_type());
	bool const is_drink(bottle_type == BOTTLE_TYPE_WATER || bottle_type == BOTTLE_TYPE_COKE);

	if (is_drink || bottle_type == BOTTLE_TYPE_MEDS) { // healing items
		if (obj.is_all_zeros()) return 1; // unsized item taken from a car, always consume
		
		if (player_at_full_health()) { // if player is at full health, heal is not needed, so add this item to inventory rather than comsume it
			if (is_drink && player_is_thirsty()) return 1; // player needs to drink
			return 0;
		}
	}
	return 1;
}
bool is_healing_food(room_object_t const &obj) {
	return (obj.type == TYPE_PIZZA_BOX && obj.is_open() && obj.taken_level == 0 && in_building_gameplay_mode() && !player_at_full_health());
}

void show_weight_limit_message() {
	std::ostringstream oss;
	oss << "Over weight limit of " << global_building_params.player_weight_limit << " lbs";
	print_text_onscreen(oss.str(), RED, 1.0, 1.5*TICKS_PER_SECOND, 0);
}

class phone_manager_t {
	bool is_enabled=0, is_ringing = 0, is_on=0;
	double stop_ring_time=0.0, next_ring_time=0.0, next_cycle_time=0.0, auto_off_time=0.0, next_button_time=0.0;
	rand_gen_t rgen;

	void schedule_next_ring() {next_ring_time = tfticks + (double)rgen.rand_uniform(30.0, 120.0)*TICKS_PER_SECOND;} // 30s to 2min
public:
	bool is_phone_ringing() const {return is_ringing;}
	bool is_phone_on     () const {return is_on     ;}

	void next_frame() {
		if (!is_enabled || !camera_in_building) {} // do nothing
		else if (is_ringing) {
			if (tfticks > stop_ring_time) {is_ringing = 0; schedule_next_ring();} // stop automatically
			else if (tfticks > next_cycle_time) { // start a new ring cycle
				gen_sound_thread_safe_at_player(get_sound_id_for_file("phone_ring.wav"), 1.0);
				register_building_sound_at_player(1.0);
				next_cycle_time += 4.2*TICKS_PER_SECOND; // 4.2s between rings
			}
		}
		else {
			if (tfticks > next_ring_time) { // start a new ring cycle
				is_ringing      = 1;
				stop_ring_time  = tfticks + (double)rgen.rand_uniform(12.0, 24.0)*TICKS_PER_SECOND; // 10-20s into the future
				next_cycle_time = tfticks; // cycle begins now
			}
			if (is_on && tfticks > auto_off_time) {is_on = 0;} // auto off
		}
	}
	void enable() {
		if (is_enabled) return; // already enabled
		is_enabled = 1;
		is_ringing = is_on = 0;
		schedule_next_ring();
	}
	void disable() {
		is_enabled = is_ringing = is_on = 0;
	}
	void player_action() {
		if (tfticks < next_button_time) return; // skip if pressed immediately after the last press (switch debouncer)
		next_button_time = tfticks + 0.25*TICKS_PER_SECOND;

		if (is_ringing) { // switch off
			is_ringing     = is_on = 0;
			stop_ring_time = tfticks; // now
			schedule_next_ring();
			register_achievement("Spam Risk");
		}
		else {
			is_on ^= 1;
			if (is_on) {auto_off_time = tfticks + 4.0*TICKS_PER_SECOND;} // 4s auto off delay
		}
		gen_sound_thread_safe_at_player(SOUND_CLICK, 0.5);
	}
};

phone_manager_t phone_manager;

struct tape_manager_t {
	bool in_use;
	float last_toggle_time;
	vector<point> points;
	point last_pos;
	room_object_t tape;
	building_t *cur_building;

	tape_manager_t() : in_use(0), last_toggle_time(0.0), cur_building(nullptr) {}

	void toggle_use(room_object_t const &tape_, building_t *building) {
		if ((tfticks - last_toggle_time) < 0.5*TICKS_PER_SECOND) return; // don't toggle too many times per frame
		last_toggle_time = tfticks;
		if (in_use) {clear();} // end use
		else {tape = tape_;	cur_building = building; in_use = 1;} // begin use
	}
	void clear() {
		if (cur_building != nullptr) {cur_building->maybe_update_tape(last_pos, 1);} // end_of_tape=1
		cur_building = nullptr;
		points.clear();
		tape = room_object_t();
		in_use = 0;
	}
};

bool phone_is_ringing() {return phone_manager.is_phone_ringing();}

tape_manager_t tape_manager;

unsigned const NUM_ACHIEVEMENTS = 17;

class achievement_tracker_t {
	// Rat Food, Top Secret Document, Mr. Yuck, Zombie Hunter, Royal Flush, Zombie Bashing, One More Drink, Bathroom Reader, TP Artist,
	// Master Lockpick, Squeaky Clean, Sleep with the Fishes, Splat the Spider, 7 years of bad luck, Tastes Like Chicken, Spam Risk, Ball in Pocket
	set<string> achievements;
	// some way to make this persistent, print these out somewhere, or add small screen icons?
public:
	bool register_achievement(string const &achievement) {
		if (!achievements.insert(achievement).second) return 0; // we already have this one
		std::ostringstream msg;
		msg << "You have unlocked a new achievement:\n" << achievement << " (" << achievements.size() << "/" << NUM_ACHIEVEMENTS << ")";
		// Note: can set a yval of -0.005 to not block other text, but there can only be one active onscreen message at once anyway
		print_text_onscreen(msg.str(), WHITE, 1.25, 3*TICKS_PER_SECOND, 20);
		return 1;
	}
};
achievement_tracker_t achievement_tracker;

bool register_achievement(string const &str) {return achievement_tracker.register_achievement(str);}

class player_inventory_t { // manages player inventory, health, and other stats
	vector<carried_item_t> carried; // interactive items the player is currently carrying
	vector<dead_person_t > dead_players;
	float cur_value, cur_weight, tot_value, tot_weight, damage_done, best_value, player_health, drunkenness, bladder, bladder_time, oxygen, thirst;
	float prev_player_zval, respawn_time=0.0;
	unsigned num_doors_unlocked, has_key; // has_key is a bit mask for key colors
	bool prev_in_building, has_flashlight, is_poisoned, poison_from_spider, has_pool_cue;

	void register_player_death(unsigned sound_id, string const &why) {
		player_wait_respawn = 1;
		if (PLAYER_RESPAWN == 0.0) {player_respawn();} // respawn now
		else {respawn_time = tfticks + PLAYER_RESPAWN*TICKS_PER_SECOND;} // respawn later
		gen_sound_thread_safe_at_player(sound_id);
		print_text_onscreen(("You Have Died" + why), RED, 2.0, 2*TICKS_PER_SECOND, 10);
		clear();
	}
	static void print_value_and_weight(std::ostringstream &oss, float value, float weight) {
		oss << ": value $";
		if (value < 1.0 && value > 0.0) {oss << ((value < 0.1) ? "0.0" : "0.") << round_fp(100.0*value);} // make sure to print the leading/trailing zero for cents
		else {oss << value;}
		oss << " weight " << weight << " lbs";
	}
public:
	player_inventory_t() {clear_all();}

	void clear() { // called on player death
		max_eq(best_value, tot_value);
		cur_value     = cur_weight = tot_value = tot_weight = damage_done = 0.0;
		drunkenness   = bladder = bladder_time = prev_player_zval = 0.0;
		player_health = oxygen = thirst = 1.0; // full health, oxygen, and (anti-)thirst
		num_doors_unlocked = has_key = 0; // num_doors_unlocked not saved on death, but maybe should be?
		prev_in_building = has_flashlight = is_poisoned = poison_from_spider = has_pool_cue = 0;
		player_attracts_flies = 0;
		phone_manager.disable();
		carried.clear();
		tape_manager.clear();
	}
	void clear_all() { // called on game mode init
		tot_value = best_value = 0.0;
		clear();
	}
	void drop_all_inventory_items(point const &camera_bs, building_t &building) { // on player death
		if (!building.has_room_geom()) return;

		while (!carried.empty()) {
			room_object_t const &obj(carried.back());

			if (obj.has_dstate() || obj.type == TYPE_BOOK || obj.type == TYPE_RAT || (obj.type == TYPE_FIRE_EXT && obj.is_broken())) {
				if (building.maybe_use_last_pickup_room_object(camera_bs, 1, 1)) continue; // no_time_check=1, random_dir=1
			}
			carried.pop_back();
		} // end while
	}
	void player_respawn() {
		point const xlate(get_camera_coord_space_xlate());
		place_player_at_xy(xlate.x, xlate.y); // move back to the origin/spawn location
		if (PLAYER_RESPAWN > 0.0) {print_text_onscreen("You Have Been Reborn", RED, 2.0, 2*TICKS_PER_SECOND, 10);}
		player_wait_respawn = 0;
	}
	void take_damage(float amt, int poison_type=0) { // poison_type: 0=none, 1=spider, 2=snake
		player_health -= amt*(1.0f - 0.75f*min(drunkenness, 1.0f)); // up to 75% damage reduction when drunk

		if (poison_type > 0) {
			if (!is_poisoned) { // first poisoning (by spider)
				print_text_onscreen("You have been poisoned", DK_RED, 1.0, 2.5*TICKS_PER_SECOND, 0);
				is_poisoned = 1;
			}
			poison_from_spider = (poison_type == 1);
		}
	}
	void record_damage_done(float amt) {damage_done += amt;}
	void return_object_to_building(room_object_t const &obj) {damage_done -= get_obj_value(obj);}
	bool check_weight_limit(float weight) const {return ((cur_weight + weight) <= global_building_params.player_weight_limit);}
	bool can_pick_up_item(room_object_t const &obj) const {return check_weight_limit(get_obj_weight(obj));}
	float get_carry_weight_ratio() const {return min(1.0f, cur_weight/global_building_params.player_weight_limit);}
	float get_speed_mult () const {return (1.0f - 0.4f*get_carry_weight_ratio())*((bladder > 0.9) ? 0.6 : 1.0);} // 40% reduction for heavy load, 40% reduction for full bladder
	float get_drunkenness() const {return drunkenness;}
	float get_oxygen     () const {return oxygen;}
	bool  player_is_dead () const {return (player_health <= 0.0);}
	unsigned player_has_key    () const {return has_key;}
	bool  player_has_flashlight() const {return has_flashlight;}
	bool  player_has_pool_cue  () const {return has_pool_cue;}
	bool  player_at_full_health() const {return (player_health == 1.0 && !is_poisoned);}
	bool  player_is_thirsty    () const {return (thirst < 0.5);}
	bool  player_holding_lit_candle() const {return (!carried.empty() && carried.back().type == TYPE_CANDLE && carried.back().is_lit());}
	void  refill_thirst() {thirst = 1.0;}

	bool can_open_door(door_t const &door) { // non-const because num_doors_unlocked is modified
		if (!door.check_key_mask_unlocks(has_key)) {
			string str("Door is locked");
			if      (door.is_locked_unlockable()) {str += " and unlockable";}
			else if (door.is_padlocked        ()) {str += " with " + lock_color_names[door.get_padlock_color_ix()] + " padlock";}
			print_text_onscreen(str, RED, 1.0, 2.0*TICKS_PER_SECOND, 0);
			gen_sound_thread_safe_at_player(SOUND_CLICK, 1.0, 0.6);
			return 0;
		}
		if (door.blocked) {
			print_text_onscreen("Door is blocked", RED, 1.0, 2.0*TICKS_PER_SECOND, 0);
			gen_sound_thread_safe_at_player(SOUND_DOOR_CLOSE, 1.0, 0.6);
			return 0;
		}
		if (door.is_closed_and_locked()) {
			print_text_onscreen("Door unlocked", BLUE, 1.0, 1.8*TICKS_PER_SECOND, 0);
			++num_doors_unlocked;
			if (num_doors_unlocked == 5) {register_achievement("Master Lockpick");} // unlock 5 doors
		}
		return 1;
	}
	void register_in_closed_bathroom_stall() const {
		if (!carried.empty() && carried.back().type == TYPE_BOOK) {register_achievement("Bathroom Reader");}
	}
	void get_dead_players_in_building(vector<dead_person_t> &dps, building_t const &building) const {
		for (dead_person_t const &p : dead_players) {
			if (p.building == &building) {dps.push_back(p);}
		}
	}
	void use_medicine() {
		player_health = 1.0;
		is_poisoned   = 0;
		print_text_onscreen("Used Medicine", CYAN, 0.8, 1.5*TICKS_PER_SECOND, 0);
		gen_sound_thread_safe_at_player(SOUND_GULP, 1.0);
	}
	void register_reward(float value) {
		assert(value > 0.0);
		std::ostringstream oss;
		oss << "Reward of $" << value;
		print_text_onscreen(oss.str(), YELLOW, 1.0, 1.5*TICKS_PER_SECOND, 0);
		cur_value += value; // doesn't count as damage
	}
	void switch_item(bool dir) { // Note: current item is always carried.back()
		if (carried.size() <= 1) return; // no other item to switch to
		if (dir) {std::rotate(carried.begin(), carried.begin()+1, carried.end());}
		else     {std::rotate(carried.begin(), carried.end  ()-1, carried.end());}
		gen_sound_thread_safe_at_player(SOUND_CLICK, 0.5);
		tape_manager.clear();
	}
	void add_item(room_object_t const &obj) {
		float health(0.0), drunk(0.0), liquid(0.0); // add these fields to bldg_obj_type_t?
		bool const bladder_was_full(bladder >= 0.9);
		float const value(get_obj_value(obj));
		if (obj.type == TYPE_PAPER && value >= 500.0) {register_achievement("Top Secret Document");}
		
		if ((obj.type == TYPE_TCAN && !obj.was_expanded()) || obj.type == TYPE_TOILET || obj.type == TYPE_URINAL || (obj.type == TYPE_RAT && obj.is_broken())) {
			register_fly_attract(0); // trahscans not on a shelf, toilets, urinals, and dead rats attract flies
		}
		damage_done += value;
		colorRGBA text_color(GREEN);
		std::ostringstream oss;
		oss << get_taken_obj_type(obj).name;

		if (is_consumable(obj)) { // nonempty bottle, consumable
			// should alcohol, poison, and medicine help with thirst? I guess alcohol helps somewhat
			switch (obj.get_bottle_type()) {
			case BOTTLE_TYPE_WATER : health =  0.25; liquid = 1.0; break; // water
			case BOTTLE_TYPE_COKE  : health =  0.50; liquid = 1.0; break; // Coke
			case BOTTLE_TYPE_BEER  : drunk  =  0.25; liquid = 0.5; break; // beer
			case BOTTLE_TYPE_WINE  : drunk  =  0.50; liquid = 0.5; break; // wine (entire bottle)
			case BOTTLE_TYPE_POISON: health = -0.50; break; // poison - take damage
			case BOTTLE_TYPE_MEDS  : health =  1.00; is_poisoned = 0; break; // medicine, restore full health and cure poisoning
			default: assert(0);
			}
		}
		else if (is_healing_food(obj)) {
			health = 0.50; // healing pizza
		}
		if (health > 0.0) { // heal
			player_health = min(1.0f, (player_health + health));
			oss << ": +" << round_fp(100.0*health) << "% Health";
		}
		if (health < 0.0) { // take damage
			player_health += health;
			oss << ": " << round_fp(100.0*health) << "% Health";
			text_color = RED;
			add_camera_filter(colorRGBA(RED, 0.25), 4, -1, CAM_FILT_DAMAGE); // 4 ticks of red damage
			gen_sound_thread_safe_at_player(SOUND_DOH, 0.5);
			if (player_is_dead()) {register_achievement("Mr. Yuck");}
		}
		if (liquid > 0.0) {
			oss << ": -" << round_fp(100.0*liquid) << "% Thirst";
			thirst = min(1.0f, (thirst + liquid));
		}
		if (obj.type == TYPE_FLASHLIGHT) {has_flashlight = 1;} // also goes in inventory
		if (obj.type == TYPE_POOL_CUE  ) {has_pool_cue   = 1;} // also goes in inventory

		if (obj.type == TYPE_KEY) {
			has_key |= (1 << obj.obj_id); // mark as having the key and record the color, but it doesn't go into the inventory or contribute to weight or value
		}
		else if (health == 0.0 && drunk == 0.0) { // print value and weight if item is not consumed
			float const weight(get_obj_weight(obj));
			cur_value  += value;
			cur_weight += weight;
			
			if (obj.is_interactive()) {
				carried.push_back(obj);
				room_object_t &co(carried.back());

				if (obj.type == TYPE_BOOK) { // clear dim and dir for books
					float const dx(co.dx()), dy(co.dy()), dz(co.dz());

					if (dz > min(dx, dy)) { // upright book from a bookcase, put it on its side facing the player
						co.y2() = co.y1() + dz;
						co.x2() = co.x1() + max(dx, dy);
						co.z2() = co.z1() + min(dx, dy);
					}
					else if (co.dim) { // swap aspect ratio to make dim=0
						co.x2() = co.x1() + dy;
						co.y2() = co.y1() + dx;
					}
					co.dim       = co.dir = 0;
					co.flags    &= ~(RO_FLAG_RAND_ROT | RO_FLAG_OPEN); // remove the rotate and open bits
					co.light_amt = 1.0; // max light for books when carrying
				}
				else if (obj.type == TYPE_PHONE) {
					if (co.dim) { // swap aspect ratio to make dim=0
						float const dx(co.dx()), dy(co.dy());
						co.x2() = co.x1() + dy;
						co.y2() = co.y1() + dx;
					}
					co.dim = co.dir = 0; // clear dim and dir
					phone_manager.enable();
				}
				else if (obj.type == TYPE_BOTTLE) { // medicine bottle
					float const dx(co.dx()), dy(co.dy()), dz(co.dz());

					if (max(dx, dy) > dz) { // sideways bottle, make upright
						co.y2() = co.y1() + dz;
						co.x2() = co.x1() + dz;
						co.z2() = co.z1() + max(dx, dy);
					}
					co.dim = co.dir = 0; // vertical
				}
				tape_manager.clear();
			}
			print_value_and_weight(oss, value, get_obj_weight(obj));
		}
		else { // add one drink to the bladder, 25% of capacity
			bladder = min(1.0f, (bladder + 0.25f));
		}
		if (drunk > 0.0) {
			drunkenness += drunk;
			oss << ": +" << round_fp(100.0*drunk) << "% Drunkenness";
			if (drunkenness > 0.99f && (drunkenness - drunk) <= 0.99f) {oss << "\nYou are drunk"; text_color = DK_GREEN;}
		}
		if (!bladder_was_full && bladder >= 0.9f) {oss << "\nYou need to use the bathroom"; text_color = YELLOW;}
		print_text_onscreen(oss.str(), text_color, 1.0, 3*TICKS_PER_SECOND, 0);
	}
	void add_custom_item(custom_item_t const &item) { // Note: no support for consumables, health, drunk, or interactive items
		cur_value   += item.value;
		cur_weight  += item.weight;
		damage_done += item.value;
		std::ostringstream oss;
		oss << item.name;
		print_value_and_weight(oss, item.value, item.weight);
		print_text_onscreen(oss.str(), GREEN, 1.0, 3*TICKS_PER_SECOND, 0);
	}
	bool take_person(uint8_t &person_has_key, float person_height) {
		if (drunkenness < 1.5) { // not drunk enough
			print_text_onscreen("Not drunk enough", RED, 1.0, 2.0*TICKS_PER_SECOND, 0);
			return 0;
		}
		float const value(1000), weight((person_height > 0.025) ? 180.0 : 80.0); // always worth $1000; use height to select man vs. girl
		if (!check_weight_limit(weight)) {show_weight_limit_message(); return 0;}
		has_key |= person_has_key; // steal their key(s)
		person_has_key = 0;
		cur_value  += value;
		cur_weight += weight;
		std::ostringstream oss;
		oss << "zombie: value $" << value << " weight " << weight << " lbs";
		print_text_onscreen(oss.str(), GREEN, 1.0, 4*TICKS_PER_SECOND, 0);
		register_achievement("Zombie Hunter");
		return 1; // success
	}
	bool try_use_last_item(room_object_t &obj) {
		if (carried.empty()) return 0; // no interactive carried item
		obj = carried.back(); // deep copy
		
		if (!obj.has_dstate()) { // not a droppable/throwable item(ball)
			if (!obj.can_use()) return 0; // should never get here?
			if (obj.type == TYPE_CANDLE && player_in_water != 2 && carried.back().get_remaining_capacity_ratio() > 0.0) {carried.back().flags ^= RO_FLAG_LIT;} // toggle candle light
			return 1;
		}
		remove_last_item(); // drop the item - remove it from our inventory
		return 1;
	}
	bool update_last_item_use_count(int val) { // val can be positive or negative
		if (val == 0) return 1; // no change
		assert(!carried.empty());
		carried_item_t &obj(carried.back());
		unsigned const capacity(get_room_obj_type(obj).capacity);

		if (capacity > 0) {
			max_eq(val, -int(obj.use_count)); // can't go negative
			obj.use_count += val;

			if (obj.use_count >= capacity) { // remove after too many uses
				if (obj.type == TYPE_FIRE_EXT) {
					float const old_value(get_obj_value(obj));
					obj.flags |= RO_FLAG_BROKEN; // mark as empty
					float const new_value(get_obj_value(obj));
					assert(new_value <= old_value); // value can't increase
					cur_value -= min(cur_value, (old_value - new_value));
					return 1;
				}
				if (obj.type == TYPE_TPROLL) {register_achievement("TP Artist");}
				remove_last_item();
				return 0;
			}
		}
		// mark used last so that medicine doesn't leave us with half it's value because the amount subtracted if half if it's used;
		// do other consumables with multiple uses still have this problem?
		obj.flags |= RO_FLAG_USED;
		return 1;
	}
	bool mark_last_item_used() {
		return update_last_item_use_count(1);
	}
	void mark_last_item_broken() {
		assert(!carried.empty());
		carried.back().flags |= RO_FLAG_BROKEN;
	}
	void remove_last_item() {
		assert(!carried.empty());
		room_object_t const &obj(carried.back());
		cur_value  -= get_obj_value (obj);
		cur_weight -= get_obj_weight(obj);
		cur_value   = 0.01*round_fp(100.0*cur_value ); // round to nearest cent
		cur_weight  = 0.01*round_fp(100.0*cur_weight); // round to nearest 0.01 lb
		assert(cur_value > -0.01 && cur_weight > -0.01); // sanity check for math
		max_eq(cur_value,  0.0f); // handle FP rounding error
		max_eq(cur_weight, 0.0f);
		carried.pop_back(); // Note: invalidates obj
		tape_manager.clear();
	}
	void collect_items(bool keep_interactive) { // called when player exits a building
		if (!keep_interactive) {has_key = 0;} // key only good for current building; flashlight and pool cue can be used in all buildings
		phone_manager.disable(); // phones won't ring when taken out of their building, since the player can't switch to them anyway
		tape_manager.clear();
		if (carried.empty() && cur_weight == 0.0 && cur_value == 0.0) return; // nothing to add
		float keep_value(0.0), keep_weight(0.0);

		if (keep_interactive) { // carried items don't contribute to collected value and weight; their value and weight remain in the inventory after collection
			for (auto i = carried.begin(); i != carried.end(); ++i) {
				keep_value  += get_obj_value (*i);
				keep_weight += get_obj_weight(*i);
			}
			cur_value  -= keep_value;
			cur_weight -= keep_weight;
			cur_value   = 0.01*round_fp(100.0*cur_value); // round to nearest cent
		}
		else {
			carried.clear();
		}
		if (cur_weight > 0.0 || cur_value > 0.0) { // we have some change in value or weight to print
			std::ostringstream oss;
			oss << "Added value $" << cur_value << " Added weight " << cur_weight << " lbs\n";
			tot_value  += cur_value;
			tot_weight += cur_weight;
			tot_value   = 0.01*round_fp(100.0*tot_value); // round to nearest cent
			oss << "Total value $" << tot_value << " Total weight " << tot_weight << " lbs";
			print_text_onscreen(oss.str(), GREEN, 1.0, 4*TICKS_PER_SECOND, 0);
		}
		cur_value  = keep_value;
		cur_weight = keep_weight;
		player_attracts_flies = 0; // even if items remain in the player's inventory
	}
	void show_stats() const {
		if (!carried.empty()) {
			player_held_object = carried.back(); // deep copy last pickup object if usable
			
			if (player_held_object.type == TYPE_PHONE) {
				if (phone_manager.is_phone_ringing()) {player_held_object.flags |= RO_FLAG_EMISSIVE;} // show ring screen
				else if (phone_manager.is_phone_on()) {player_held_object.flags |= RO_FLAG_OPEN    ;} // show lock screen
			}
		}
		if (display_framerate) { // controlled by framerate toggle
			float const aspect_ratio((float)window_width/(float)window_height);

			if (cur_weight > 0.0 || tot_weight > 0.0 || best_value > 0.0) { // don't show stats until the player has picked something up
				std::ostringstream oss;
				oss << "Cur $" << cur_value << " / " << cur_weight << " lbs  Total $" << tot_value << " / " << tot_weight
					<< " lbs  Best $" << best_value << "  Damage $" << round_fp(damage_done); // print damage to nearest dollar
				
				if (!carried.empty()) {
					unsigned const capacity(get_room_obj_type(player_held_object).capacity);
					oss << "  [" << get_taken_obj_type(player_held_object).name; // print the name of the throwable object
					if (capacity > 0 && !player_held_object.is_broken()) {oss << " " << (capacity - carried.back().use_count) << "/" << capacity;} // print use/capacity if nonempty
					oss << "]";
				}
				draw_text(GREEN, -0.010*aspect_ratio, -0.011, -0.02, oss.str(), 0.8); // size=0.8
			}
			if (phone_manager.is_phone_ringing() && player_held_object.type == TYPE_PHONE) { // player is holding a ringing phone, give them a hint
				draw_text(LT_BLUE, -0.001*aspect_ratio, -0.009, -0.02, "[Press space to silence phone]");
			}
			if (in_building_gameplay_mode()) {
				float const lvl(min(cur_building_sound_level, 1.0f));
				unsigned const num_bars(round_fp(20.0*lvl));

				if (num_bars > 0) { // display sound meter
					colorRGBA const color(lvl, (1.0 - lvl), 0.0, 1.0); // green => yellow => orange => red
					draw_text(color, -0.005*aspect_ratio, -0.010, -0.02, string(num_bars, '#'));
				}
				// technically, the player is still hiding, even if the phone is ringing; zombies may be attracted to the sound but won't actually see the player
				if (player_is_hiding && camera_in_building && !phone_manager.is_phone_ringing()) {draw_text(LT_BLUE, -0.001*aspect_ratio, -0.009, -0.02, "[Hiding]");}
			}
		}
		if (in_building_gameplay_mode()) {
			// Note: shields is used for drunkenness; values are scaled from 0-1 to 0-100; powerup values are for bladder fullness
			vector<status_bar_t> extra_bars;
			colorRGBA const thirst_color((thirst < 0.2 && ((int(tfticks)/8)&1)) ? MAGENTA : BLUE); // flash magenta when low
			extra_bars.emplace_back(WHITE, get_carry_weight_ratio(), ICON_CARRY); // carry weight
			extra_bars.emplace_back(thirst_color, thirst, ICON_WATER); // thirst
			if (oxygen < 1.0) {extra_bars.emplace_back(CYAN, oxygen, ICON_OXYGEN);} // oxygen bar is only shown when oxygen is less than full
			draw_health_bar(100.0*player_health, 100.0*drunkenness, bladder, YELLOW, is_poisoned, extra_bars);
		}
		if (has_key) {
			vector<colorRGBA> key_colors(NUM_LOCK_COLORS);
			for (unsigned n = 0; n < NUM_LOCK_COLORS; ++n) {key_colors[n] = ((has_key & (1 << n)) ? lock_colors[n] : ALPHA0);}
			show_key_icon(key_colors);
		}
		if (has_flashlight) {show_flashlight_icon();}
		if (has_pool_cue  ) {show_pool_cue_icon  ();}
	}
	bool apply_fall_damage(float delta_z, float dscale=1.0) {
		if (!in_building_gameplay_mode()) return 0;
		if (!camera_in_building)          return 0; // only take fall damage when inside the building (no falling off the roof for now)
		float const fall_damage_start(3.0*CAMERA_RADIUS); // should be a function of building floor spacing?
		if (delta_z < fall_damage_start)  return 0; // no damage
		player_health -= dscale*(delta_z - fall_damage_start)/fall_damage_start;
		if (player_is_dead()) {register_player_death(SOUND_SQUISH, " from a fall"); return 1;} // dead
		gen_sound_thread_safe_at_player(SOUND_SQUISH, 0.5);
		add_camera_filter(colorRGBA(RED, 0.25), 4, -1, CAM_FILT_DAMAGE); // 4 ticks of red damage
		register_building_sound_at_player(1.0);
		return 0; // hurt but alive
	}
	void next_frame() {
		if (player_wait_respawn) {
			if (tfticks > respawn_time) {player_respawn();}
			return;
		}
		show_stats();
		phone_manager.next_frame(); // even if not in gameplay mode?
		float const fticks_clamped(min(fticks, 0.25f*TICKS_PER_SECOND)); // limit to 250ms so that the player doesn't die when un-paused
		float const elapsed_time(animate2 ? fticks_clamped : 0.0); // no time elapsed when time is paused
		// update candle, even when not in gameplay mode
		if (!carried.empty()) {
			carried_item_t &obj(carried.back());

			if (obj.type == TYPE_CANDLE && obj.is_lit()) {
				if ((frame_counter % 10) == 0) {obj.use_count += 10.0*elapsed_time;} // special logic for integer incrementing
				min_eq(obj.use_count, get_room_obj_type(obj).capacity); // use_count can't be > capacity
				if (obj.get_remaining_capacity_ratio() <= 0.0) {obj.flags &= ~RO_FLAG_LIT;} // goes out when used up
				if (player_in_water == 2) {obj.flags &= ~RO_FLAG_LIT;} // goes out under water
			}
		}
		if (!in_building_gameplay_mode()) return;
		// handle oxygen
		float oxygen_use_rate((elapsed_time/TICKS_PER_SECOND)/30.0); // used up in 30s

		if (player_in_water == 2) { // head underwater, lose oxygen slowly
			oxygen = max(0.0f, (oxygen - oxygen_use_rate));
			
			if (oxygen == 0.0) {
				register_player_death(SOUND_DROWN, " by drowning");
				
				if (player_building != nullptr) { // leave a dead player body floating on the water
					cube_t water_cube(player_building->get_water_cube());
					water_cube.expand_by_xy(-(CAMERA_RADIUS + 0.5*camera_zh)); // shrink by player half height so as not to clip outside the water
					point player_pos(get_camera_building_space());
					water_cube.clamp_pt_xy(player_pos);
					player_pos.z = water_cube.z2() - 0.1*CAMERA_RADIUS; // slightly below the water surface
					dead_players.emplace_back(player_pos, cview_dir, player_building);
				}
				return;
			}
		}
		else {oxygen = min(1.0f, (oxygen + 10.0f*oxygen_use_rate));} // head above water, gain oxygen quickly
		// handle player fall damage logic
		point const camera_pos(get_camera_pos());
		float const player_zval(camera_pos.z), delta_z(prev_player_zval - player_zval);
		if (camera_in_building != prev_in_building) {prev_in_building = camera_in_building;}
		else if (prev_player_zval != 0.0 && delta_z > 0.0 && apply_fall_damage(delta_z)) return; // Note: fall damage may no longer trigger with slow player fall logic
		prev_player_zval = player_zval;
		// handle death events
		if (player_is_dead()) {register_player_death(SOUND_SCREAM3, ""); return;} // dead
		
		if (drunkenness > 2.0) {
			register_player_death(SOUND_DROWN, " of alcohol poisoning");
			register_achievement("One More Drink");
			return;
		}
		if (is_poisoned) {
			player_health -= 0.0002*elapsed_time;

			if (player_is_dead()) {
				string const poison_type(poison_from_spider ? " of spider venom" : " of snake venom");
				register_player_death(SOUND_DEATH, poison_type);
				return;
			}
		}
		if (thirst <= 0.0) {
			register_player_death(SOUND_GULP, " of thirst"); // not sure what the sound should be
			return;
		}
		// update state for next frame
		drunkenness = max(0.0f, (drunkenness - 0.0001f*elapsed_time)); // slowly decrease over time
		// should the player drink when underwater? maybe depends on how clean the water is? how about only if thirst < 0.5
		if (player_in_water == 2 && thirst < 0.5) {thirst = min(1.0f, (thirst + 0.01f *elapsed_time));} // underwater
		else {thirst = max(0.0f, (thirst - 0.0001f*elapsed_time));} // slowly decrease over time (250s)
		
		if (player_near_toilet) { // empty bladder
			if (bladder > 0.9) {gen_sound_thread_safe_at_player(SOUND_GASP);} // urinate
			if (bladder > 0.0) { // toilet flush
#pragma omp critical(gen_sound)
				gen_delayed_sound(1.0, SOUND_FLUSH, camera_pos); // delay by 1s
				register_building_sound_at_player(0.5);
				register_achievement("Royal Flush");
			}
			bladder = 0.0;
		}
		else if (bladder > 0.9) {
			bladder_time += elapsed_time;

			if (bladder_time > 5.0*TICKS_PER_SECOND) { // play the "I have to go" sound
				gen_sound_thread_safe_at_player(SOUND_HURT);
				bladder_time = 0.0;
			}
		}
		player_near_toilet = 0;
	}
	colorRGBA get_vignette_color() const {
		if (thirst < 0.1 && ((int(tfticks)/8)&1)) return colorRGBA(0.0, 0.0, 0.0, 10.0*(0.10 - thirst)); // flash black when about to die of thirst
		if (oxygen < 0.1)         return colorRGBA(0.0, 0.0, 0.0, 10.0*(0.10 - oxygen)); // black; doesn't really work well
		if (player_health < 0.25) return colorRGBA(1.0, 0.0, 0.0, 4.0 *(0.25 - player_health)); // red
		if (is_poisoned)          return colorRGBA(0.0, 1.0, 0.0, 0.5); // green
		if (bladder > 0.75)       return colorRGBA(1.0, 1.0, 0.0, 2.0*(bladder - 0.75)); // yellow
		return ALPHA0;
	}
}; // end player_inventory_t

player_inventory_t player_inventory;

float get_player_drunkenness() {return player_inventory.get_drunkenness();}
float get_player_oxygen     () {return player_inventory.get_oxygen     ();}
float get_player_building_speed_mult() {return player_inventory.get_speed_mult();}
bool player_can_open_door(door_t const &door) {return player_inventory.can_open_door(door);}
void register_in_closed_bathroom_stall() {player_inventory.register_in_closed_bathroom_stall();}
bool player_at_full_health() {return player_inventory.player_at_full_health();}
bool player_is_thirsty    () {return player_inventory.player_is_thirsty    ();}
bool player_holding_lit_candle() {return player_inventory.player_holding_lit_candle();}
void refill_thirst() {player_inventory.refill_thirst();}
void apply_building_fall_damage(float delta_z) {player_inventory.apply_fall_damage(delta_z, 0.5);} // dscale=0.5
void get_dead_players_in_building(vector<dead_person_t> &dead_players, building_t const &building) {player_inventory.get_dead_players_in_building(dead_players, building);}

void pool_ball_in_pocket() {
	player_inventory.register_reward(100.0);
	register_achievement("Ball in Pocket");
}

void register_building_sound_for_obj(room_object_t const &obj, point const &pos) {
	float const weight(get_obj_weight(obj)), volume((weight <= 1.0) ? 0.0 : min(1.0f, 0.01f*weight)); // heavier objects make more sound
	register_building_sound(pos, volume);
}

void record_building_damage(float damage) {
	player_inventory.record_damage_done(damage);
}
void register_broken_object(room_object_t const &obj) {
	float const damage(obj.is_parked_car() ? 250.0 : get_obj_value(obj)); // broken car window is $250
	player_inventory.record_damage_done(damage);
}

bool register_player_object_pickup(room_object_t const &obj, point const &at_pos) {
	bool const can_pick_up(player_inventory.can_pick_up_item(obj));

	if (!do_room_obj_pickup) { // player has not used the pickup key, but we can still use this to notify the player that an object can be picked up
		can_pickup_bldg_obj = (can_pick_up ? 1 : 2);
		return 0;
	}
	if (!can_pick_up) {show_weight_limit_message(); return 0;}
	if      (is_consumable  (obj)) {gen_sound_thread_safe_at_player(SOUND_GULP,   1.00);}
	else if (is_healing_food(obj)) {gen_sound_thread_safe_at_player(SOUND_EATING, 1.00);}
	else                           {gen_sound_thread_safe_at_player(SOUND_ITEM,   0.25);}
	register_building_sound_for_obj(obj, at_pos);
	do_room_obj_pickup = 0; // no more object pickups
	return 1;
}

bool building_t::player_pickup_object(point const &at_pos, vector3d const &in_dir) {
	if (!has_room_geom()) return 0;
	return interior->room_geom->player_pickup_object(*this, at_pos, in_dir);
}
bool building_room_geom_t::player_pickup_object(building_t &building, point const &at_pos, vector3d const &in_dir) {
	point at_pos_rot(at_pos);
	vector3d in_dir_rot(in_dir);
	building.maybe_inv_rotate_pos_dir(at_pos_rot, in_dir_rot);
	float const range_max(3.0*CAMERA_RADIUS), drawer_range_max(2.5*CAMERA_RADIUS);
	float range(range_max), obj_dist(0.0);
	int rat_ix(-1);

	if (bldg_obj_types[TYPE_RAT].pickup) { // check rats
		for (auto r = rats.begin(); r != rats.end(); ++r) {
			if (r->is_hiding) continue; // can't pick up when hiding
			point p1c(at_pos), p2c(at_pos + in_dir*range);
			if (!do_line_clip(p1c, p2c, r->get_bcube().d)) continue; // test ray intersection vs. bcube
			float const dist(p2p_dist(at_pos, p1c));
			if (dist >= range) continue; // too far
			if (building.check_for_wall_ceil_floor_int(at_pos, p1c)) continue; // check for occlusion
			range  = dist;
			rat_ix = (r - rats.begin());
		} // for r
	}
	//if (bldg_obj_types[TYPE_SPIDER].pickup) {} // check spiders (future work)
	//if (bldg_obj_types[TYPE_SNAKE ].pickup) {} // check snakes  (future work)
	int const obj_id(find_nearest_pickup_object(building, at_pos_rot, in_dir_rot, range, obj_dist));
	float drawer_range(min(range, drawer_range_max));
	if (obj_id >= 0) {min_eq(drawer_range, obj_dist);} // only include drawers that are closer than the pickup object
	if (open_nearest_drawer(building, at_pos_rot, in_dir_rot, drawer_range_max, 1, 0)) return 1; // try objects in drawers; pickup_item=1
	
	if (obj_id < 0) { // no room object to pick up
		if (rat_ix >= 0) { // can pick up a rat
			rat_t const &r(rats[rat_ix]);
			unsigned *mwave_flag(nullptr);

			if (r.dead) { // dead - check if in a microwave
				auto objs_end(get_placed_objs_end()); // skip buttons/stairs/elevators

				for (auto i = objs.begin(); i != objs_end; ++i) {
					if (i->type != TYPE_MWAVE || !i->contains_pt(r.pos)) continue; // wrong object
					if (!i->is_open()) return 0; // inside a closed microwave, can't pick up
					mwave_flag = &i->flags;
					break;
				}
			}
			room_object_t rat(r.get_bcube_with_dir(), TYPE_RAT, 0, 0, 0, (r.dead ? RO_FLAG_BROKEN : 0)); // no room, flag as broken if dead
			if (!register_player_object_pickup(rat, at_pos)) return 0;
			player_inventory.add_item(rat);
			if (mwave_flag) {*mwave_flag &= ~RO_FLAG_NONEMPTY;} // microwave is now empty

			if (!r.dead) { // squeak if alive
				gen_sound_thread_safe(SOUND_RAT_SQUEAK, building.local_to_camera_space(rats[rat_ix].pos));
				register_building_sound(rats[rat_ix].pos, 0.8);
			}
			rats.erase(rats.begin() + rat_ix); // remove the rat from the building
			modified_by_player = 1;
			return 1;
		}
		return 0;
	}
	room_object_t &obj(get_room_object_by_index(obj_id));
	bool const is_shelfrack(obj.type == TYPE_SHELFRACK);
	if (is_shelfrack && obj.obj_expanded()) return 0; // line hits back of shelf rack, not another object; no pickup

	if (obj.type == TYPE_SHELVES || is_shelfrack || (obj.type == TYPE_WINE_RACK && !obj.obj_expanded())) { // shelves/racks or unexpanded wine rack
		assert(!obj.obj_expanded()); // should not have been expanded

		if (is_shelfrack && !do_room_obj_pickup) {
			// player has not used the pickup key; since expanding a shelf rack is expensive, just get the object list for selection;
			// but this also means we can't open boxes and microwaves until we take an object from this shelfrack and expand it for real
			static vect_room_object_t temp_objs;
			temp_objs.clear();
			get_shelfrack_objects(obj, temp_objs);
			temp_objs.swap(expanded_objs); // use our temp objects
			obj.flags |=  RO_FLAG_EXPANDED; // temporarily mark expanded to avoid infinite recursion and enable the blocked-by-back-of-shelf logic above
			player_pickup_object(building, at_pos, in_dir);
			obj.flags &= ~RO_FLAG_EXPANDED;
			temp_objs.swap(expanded_objs); // back to regular expanded objects
			return 0; // object not actually picked up; no small geom invalidation (which is the point of this optimization)
		}
		expand_object(obj, building);
		bool const picked_up(player_pickup_object(building, at_pos, in_dir)); // call recursively on contents
		// if we picked up an object, assume the VBOs have already been updated; otherwise we need to update them to expand this object
		if (!picked_up) {invalidate_small_geom();} // assumes expanded objects are all "small"
		return picked_up;
	}
	if (obj.type == TYPE_BCASE) {
		static vect_room_object_t books;
		books.clear();
		get_bookcase_books(obj, books);
		int closest_obj_id(-1);
		float dmin_sq(0.0);
		point const p2(at_pos + in_dir*range);

		for (auto i = books.begin(); i != books.end(); ++i) {
			point p1c(at_pos), p2c(p2);
			if (!do_line_clip(p1c, p2c, i->d))  continue; // test ray intersection vs. bcube
			float const dsq(p2p_dist(at_pos, p1c)); // use closest intersection point
			if (dmin_sq > 0.0 && dsq > dmin_sq) continue; // not the closest
			closest_obj_id = (i - books.begin()); // valid pickup object
			dmin_sq = dsq; // this object is the closest
		} // for i
		if (dmin_sq == 0.0) return 0; // no book to pick up
		room_object_t &book(books[closest_obj_id]);
		if (!register_player_object_pickup(book, at_pos)) return 0;
		// set flag bit to remove this book from the bookcase; supports up to 48 books
		obj.set_combined_flags(obj.get_combined_flags() | obj.get_book_ix_mask(book.item_flags));
		player_inventory.add_item(book);
		update_draw_state_for_room_object(book, building, 1);
		return 1;
	}
	if (obj.is_parked_car()) {
		// returns either a standard room object or a custom item; in general, we won't return both, but in any case we're successful if either type of item is taken
		room_obj_or_custom_item_t const loot(steal_from_car(obj, building.get_window_vspace(), do_room_obj_pickup));
		bool ret(0);
		
		if (loot.obj.type != TYPE_NONE && register_player_object_pickup(loot.obj, at_pos)) {
			player_inventory.add_item(loot.obj);
			ret = 1;
		}
		if (loot.item.valid()) {
			bool const can_pick_up(player_inventory.check_weight_limit(loot.item.weight));
			if (!do_room_obj_pickup) {can_pickup_bldg_obj = (can_pick_up ? 1 : 2);} // notify the player of an object to pick up
			else if (!can_pick_up) {show_weight_limit_message();}
			else {
				do_room_obj_pickup = 0; // no more object pickups
				gen_sound_thread_safe_at_player(SOUND_ITEM, 0.25);
				player_inventory.add_custom_item(loot.item);
				ret = 1;
			}
		}
		if (ret) {++obj.taken_level;} // onto next item
		return ret;
	}
	if (!register_player_object_pickup(obj, at_pos)) return 0;
	remove_object(obj_id, building);
	return 1;
}

void print_entering_building(string const &str) {
	print_text_onscreen(str, WHITE, 1.0, 2.0*TICKS_PER_SECOND, 0);
}
void building_t::register_player_change_floor(unsigned old_floor, unsigned new_floor) const {
	if (!multi_family) return; // single residence/name or office building
	if (camera_pos.z < ground_floor_z1) return; // skip if basement, since no one resident owns it
	print_entering_building("Entering " + get_name_for_floor(new_floor) + " Residence");
}
void building_t::register_player_enter_building() const {
	//print_building_manifest(); // for debugging
	//print_building_stats   (); // for debugging

	if (!name.empty()) {
		string str("Entering " + name); // Re-Entering if player_visited?
		if (is_house) {str += " Residence";}
		str += "\nType: " + btype_names[btype];
		if (!address.empty()) {str += "\n" + address;} // add address on a second line if known
		if (interior && !interior->people.empty()) {str += "\nPopulation " + std::to_string(interior->people.size());}
		print_entering_building(str);
	}
	reset_creepy_sounds();
	player_visited = 1;
}
void building_t::register_player_exit_building(bool entered_another_building) const {
	// only collect items in gameplay mode where there's a risk the player can lose them; otherwise, let the player carry items between buildings
	if (!entered_another_building) {player_inventory.collect_items(!in_building_gameplay_mode());} // only when not in a building
	clear_building_water_splashes(); // what if player exits through a connected extended basement room and the splashes are still visible?
}

bool is_obj_in_or_on_obj(room_object_t const &parent, room_object_t const &child) {
	if (parent.type == TYPE_WINE_RACK && parent.contains_pt(child.get_cube_center()))     return 1; // check for wine bottles left in wine rack
	if (fabs(child.z1() - parent.z2()) < 0.05*parent.dz() && child.intersects_xy(parent)) return 1; // zval test
	if (parent.type == TYPE_BOX   && parent.is_open() && parent.contains_cube(child))     return 1; // open box with an object inside
	if (parent.type == TYPE_BED   && child.z1() <= parent.z2() && child.z1() > parent.zc() && child.intersects_xy(parent)) return 1; // object on the mattress of a bed
	if (parent.type == TYPE_STOVE && parent.contains_cube(child))                         return 1; // pan, etc. on a stove
	
	if (parent.type == TYPE_POOL_TABLE && child.type == TYPE_POOL_CUE) { // handle pool cue leaning against pool table
		cube_t table_exp(parent);
		table_exp.expand_by_xy(min(child.dx(), child.dy()));
		if (table_exp.intersects(child)) return 1;
	}
	return 0;
}
bool object_can_have_something_on_it(room_object_t const &obj) {
	auto const type(obj.type);
	// only these types can have objects placed on them (what about TYPE_SHELF? what about TYPE_BED with a ball, book, or blanket placed on it?)
	return (type == TYPE_TABLE || type == TYPE_DESK || type == TYPE_COUNTER || type == TYPE_DRESSER || type == TYPE_NIGHTSTAND ||
		type == TYPE_BOX || type == TYPE_CRATE || type == TYPE_WINE_RACK || type == TYPE_BOOK || type == TYPE_STOVE || type == TYPE_MWAVE ||
		type == TYPE_BED || type == TYPE_SERVER || type == TYPE_PIZZA_BOX || type == TYPE_LAPTOP || type == TYPE_FOLD_SHIRT
		/*|| type == TYPE_FCABINET*/ /*|| type == TYPE_SHELF*/);
}
bool object_has_something_on_it(room_object_t const &obj, vect_room_object_t const &objs, vect_room_object_t::const_iterator objs_end) {
	if (!object_can_have_something_on_it(obj)) return 0;

	for (auto i = objs.begin(); i != objs_end; ++i) {
		if (i->type == TYPE_BLOCKER)      continue; // ignore blockers (from removed objects)
		if (*i == obj)                    continue; // skip self (bcube check)
		if (is_obj_in_or_on_obj(obj, *i)) return 1;
	}
	return 0;
}
float get_combined_stacked_obj_weights(room_object_t const &obj, vect_room_object_t const &objs, vect_room_object_t::const_iterator objs_end) {
	float weight(0.0);

	for (auto i = objs.begin(); i != objs_end; ++i) {
		if (i->type == TYPE_BLOCKER) continue; // ignore blockers (from removed objects)
		if (*i == obj)               continue; // skip self (bcube check)
		if (is_obj_in_or_on_obj(obj, *i)) {weight += get_room_obj_type(*i).weight;}
	}
	return weight;
}

cube_t get_true_obj_bcube(room_object_t const &obj) { // for player object pickup and move
	if (obj.type == TYPE_PEN || obj.type == TYPE_PENCIL || obj.type == TYPE_POOL_CUE) {
		cube_t obj_bcube(obj);
		obj_bcube.expand_in_dim(!obj.dim, obj.get_width());
		return obj_bcube;
	}
	if (obj.type == TYPE_BOTTLE && obj.rotates()) { // rotated bottle on floor; drawing doesn't perfectly match the bcube, so increase the size a bit
		cube_t obj_bcube(obj);
		obj_bcube.expand_by_xy(0.5*obj.min_len()); // expand by half radius
		return obj_bcube;
	}
	if (is_ball_type(obj.type)) {
		cube_t obj_bcube(obj);
		obj_bcube.expand_by(0.25*obj.get_radius()); // make it 25% larger
		return obj_bcube;
	}
	if (obj.type == TYPE_BED) { // do more accurate check with various parts of the bed
		cube_t cubes[6]; // frame, head, foot, mattress, pillow, legs_bcube
		get_bed_cubes(obj, cubes);
		return cubes[3]; // check mattress only, since we can only take the mattress, sheets, and pillows
	}
	if (obj.type == TYPE_POOL_TABLE) {
		cube_t obj_bcube(obj);
		obj_bcube.z1() += 0.50*obj.dz(); // only include the top half, so that we can pick up objects such as balls under the pool table
		obj_bcube.z2() -= 0.05*obj.dz(); // make it slightly shorter so that it's easier to pick up pool balls
		return obj_bcube;
	}
	if (obj.is_obj_model_type()) {
		cube_t obj_bcube(obj);
		obj_bcube.expand_by(-0.1*obj.get_size()); // since models don't fill their bcubes, shrink them a bit when doing a ray query
		return obj_bcube;
	}
	return obj; // unmodified
}

bool obj_has_open_drawers(room_object_t const &obj) {
	return ((obj.type == TYPE_NIGHTSTAND || obj.type == TYPE_DRESSER || obj.type == TYPE_DESK || obj.type == TYPE_FCABINET) && obj.drawer_flags);
}
int building_room_geom_t::find_nearest_pickup_object(building_t const &building, point const &at_pos, vector3d const &in_dir, float range, float &obj_dist) const {
	int closest_obj_id(-1);
	float dmin_sq(0.0), t(0.0); // t is for intersection checks but its value is unused
	point const p2(at_pos + in_dir*range);

	for (unsigned vect_id = 0; vect_id < 2; ++vect_id) {
		auto const &obj_vect((vect_id == 1) ? expanded_objs : objs), &other_obj_vect((vect_id == 1) ? objs : expanded_objs);
		unsigned const obj_id_offset((vect_id == 1) ? objs.size() : 0);
		auto objs_end((vect_id == 1) ? expanded_objs.end() : get_stairs_start()); // skip stairs and elevators
		auto other_objs_end((vect_id == 1) ? get_stairs_start() : expanded_objs.end());

		for (auto i = obj_vect.begin(); i != objs_end; ++i) {
			if (!get_room_obj_type(*i).pickup && !i->is_parked_car()) continue; // this object type can't be picked up
			cube_t const obj_bcube(get_true_obj_bcube(*i));
			point p1c(at_pos), p2c(p2);
			if (!do_line_clip(p1c, p2c, obj_bcube.d)) continue; // test ray intersection vs. bcube
			float dsq(p2p_dist(at_pos, p1c)); // use closest intersection point

			// check trash in trashcan; if found, set dist closer than the trashcan
			if (i->type == TYPE_TRASH && in_dir.z < -0.75 && vect_id == 0 && closest_obj_id >= 0) {
				room_object_t const &cur_closest(objs[closest_obj_id]);
				if (cur_closest.type == TYPE_TCAN && cur_closest.contains_pt(i->get_cube_center())) {dsq = 0.9*dmin_sq;}
			}
			if (dmin_sq > 0.0 && dsq > dmin_sq) continue; // not the closest
			if (obj_bcube.contains_pt(at_pos))  continue; // skip when the player is standing inside a plant, etc.
			if (player_in_elevator && !i->in_elevator() && !i->is_dynamic()) continue; // can't take an elevator call button from inside the elevator
		
			if (obj_bcube == *i) { // check non-cube shapes, but only when get_true_obj_bcube() didn't return a custom cube
				if (i->shape == SHAPE_SPHERE) {
					float const radius(i->get_radius());
					if (!sphere_test_comp(p1c, i->get_cube_center(), (p1c - p2c), radius*radius, t)) continue;
				}
				else if (i->shape == SHAPE_CYLIN) {
					cylinder_3dw const cylin(i->get_cylinder());
					if (!line_int_cylinder(p1c, p2c, cylin.p1, cylin.p2, cylin.r1, cylin.r2, 1, t)) continue; // check_ends=1
				}
				else if (i->shape == SHAPE_VERT_TORUS) { // without this check we can't take an object out of its hole
					float const ri(0.5*i->dz()), ro(i->get_radius() - ri);
					if (!line_torus_intersect_rescale(p1c, p2c, i->get_cube_center(), plus_z, ri, ro, t)) continue;
				}
			}
			if (i->type == TYPE_CLOSET || (i->type == TYPE_STALL && i->shape != SHAPE_SHORT)) { // can only take short stalls (separating urinals)
				if (!i->is_open() && !i->contains_pt(at_pos)) { // stalls/closets block the player from taking toilets/boxes unless open, or the player is inside
					closest_obj_id = -1;
					dmin_sq = dsq;
				}
				continue;
			}
			if (i->type == TYPE_CHAIR) { // separate back vs. seat vs. legs check for improved accuracy
				cube_t cubes[3]; // seat, back, legs_bcube
				get_chair_cubes(*i, cubes);
				bool intersects(0);
				for (unsigned n = 0; n < 3 && !intersects; ++n) {intersects |= cubes[n].line_intersects(p1c, p2c);}
				if (!intersects) continue;
			}
			if (i->type == TYPE_HANGER_ROD && i->item_flags > 0) { // nonempty hanger rod
				// search for hangers and don't allow hanger rod to be taken until the hangers are all taken
				bool has_hanger(0);

				for (auto j = i+1; j != (i + i->item_flags); ++j) { // iterate over all objects hanging on the hanger rod and look for untaken hangers
					if (j->type == TYPE_HANGER) {has_hanger = 1; break;}
				}
				if (has_hanger) continue;
			}
			if (i->type == TYPE_HANGER  && i->is_hanging() && (i+1) != objs_end && (i+1)->type == TYPE_CLOTHES) continue; // hanger with clothes - must take clothes first
			if (i->type == TYPE_MIRROR  && !i->is_house())                continue; // can only pick up mirrors from houses, not office buildings
			if (i->type == TYPE_TABLE   && i->shape == SHAPE_CUBE)        continue; // can only pick up short (TV) tables and cylindrical tables
			if (i->type == TYPE_BED     && i->taken_level > 2)            continue; // can only take pillow, sheets, and mattress - not the frame
			if (i->type == TYPE_SHELVES   && i->obj_expanded())           continue; // shelves are   already expanded, can no longer select this object
			if (i->type == TYPE_MIRROR  && i->is_open())                  continue; // can't take mirror/medicine cabinet until it's closed
			if (i->type == TYPE_LIGHT   && !i->is_visible())              continue; // can't take light attached to a ceiling fan as a separate object
			if (i->type == TYPE_MWAVE   && (i->flags & RO_FLAG_NONEMPTY)) continue; // can't take a microwave with something inside it
			if (i->type == TYPE_PADLOCK && i->is_active())                continue; // padlock in locked onto a door, can't take

			if (i->type == TYPE_SHELFRACK && i->obj_expanded()) { // shelf rack is already expanded, can no longer select this object
				// check the back of the shelf rack to make sure the player can't take an object through it
				cube_t back, top, sides[2], shelves[5];
				get_shelf_rack_cubes(*i, back, top, sides, shelves);
				point p1c2(p1c), p2c2(p2c);
				if (!do_line_clip(p1c2, p2c2, back.d)) continue; // line not hitting back
				dsq = p2p_dist(at_pos, p1c2); // use closest intersection point
				if (dmin_sq > 0.0 && dsq > dmin_sq)    continue; // not the closest
			}
			if (obj_has_open_drawers(*i))                                 continue; // can't take if any drawers are open
			if (object_has_something_on_it(*i,       obj_vect, objs_end)) continue; // can't remove a table, etc. that has something on it
			if (object_has_something_on_it(*i, other_obj_vect, other_objs_end)) continue; // check the other one as well
			if (building.check_for_wall_ceil_floor_int(at_pos, p1c))      continue; // skip if it's on the other side of a wall, ceiling, or floor
			closest_obj_id = (i - obj_vect.begin()) + obj_id_offset; // valid pickup object
			dmin_sq = dsq; // this object is the closest, even if it can't be picked up
		} // for i
	} // for vect_id
	obj_dist = sqrt(dmin_sq);
	return closest_obj_id;
}

bool is_counter   (room_object_t const &obj) {return (obj.type == TYPE_COUNTER || obj.type == TYPE_KSINK);}
bool obj_has_doors(room_object_t const &obj) {return (is_counter(obj) || obj.type == TYPE_CABINET);}

void get_obj_drawers_or_doors(room_object_t const &obj, vect_cube_t &drawers, room_object_t &drawers_part, float &drawer_extend) {
	// Note: this is a messy solution and must match the drawing code, but it's unclear how else we can get the location of the drawers
	if (obj_has_doors(obj)) {
		get_cabinet_or_counter_doors(obj, drawers, drawers); // combine doors and drawers together; will sort them out later
		drawers_part  = obj; // need to at least copy the IDs and flags
		drawer_extend = (obj.dir ? 1.0 : -1.0)*0.8*obj.get_depth(); // used for cabinet drawers
	}
	else if (obj.type == TYPE_FCABINET) {
		drawers_part  = obj;
		drawer_extend = get_filing_cabinet_drawers(obj, drawers);
	}
	else {
		if (obj.type == TYPE_DESK) {
			drawers_part = get_desk_drawers_part(obj);
			bool const side(obj.obj_id & 1);
			drawers_part.d[!obj.dim][side] -= (side ? 1.0 : -1.0)*0.85*get_tc_leg_width(obj, 0.06);
		}
		else {
			assert(obj.type == TYPE_DRESSER || obj.type == TYPE_NIGHTSTAND);
			drawers_part = get_dresser_middle(obj);
			drawers_part.expand_in_dim(!obj.dim, -0.5*get_tc_leg_width(obj, 0.10));
		}
		drawer_extend = get_drawer_cubes(drawers_part, drawers, 0, 1); // front_only=0, inside_only=1
	}
}
bool building_room_geom_t::open_nearest_drawer(building_t &building, point const &at_pos, vector3d const &in_dir, float range, bool pickup_item, bool check_only) {
	int closest_obj_id(-1);
	float dmin_sq(0.0);
	point const p2(at_pos + in_dir*range);
	auto objs_end(get_placed_objs_end()); // skip buttons/stairs/elevators

	for (auto i = objs.begin(); i != objs_end; ++i) {
		bool const is_counter_type(is_counter(*i) || i->type == TYPE_CABINET);
		// drawers that can be opened or picked from
		bool const has_drawers(i->type == TYPE_DRESSER || i->type == TYPE_NIGHTSTAND || i->type == TYPE_COUNTER ||
			i->type == TYPE_FCABINET || (i->type == TYPE_DESK && i->desk_has_drawers()));
		if (!(has_drawers || (!pickup_item && is_counter_type))) continue; // || doors that can be opened (no item pickup)
		cube_t bcube(*i);
		float &front_face(bcube.d[i->dim][i->dir]);
		if ((front_face < at_pos[i->dim]) ^ i->dir) continue; // can't open from behind
		// expand outward to include open drawers; not that this can make it difficult to select a drawer at the inside corner of the kitchen counter
		if (has_drawers && i->drawer_flags > 0) {front_face += 0.75*(i->dir ? 1.0 : -1.0)*i->get_length();}
		point p1c(at_pos), p2c(p2);
		if (!do_line_clip(p1c, p2c, bcube.d)) continue; // test ray intersection vs. bcube
		float const dsq(p2p_dist_sq(at_pos, p1c)); // use closest intersection point
		if (dmin_sq > 0.0 && dsq > dmin_sq) continue; // not the closest
		if (building.check_for_wall_ceil_floor_int(at_pos, p1c)) continue; // skip if it's on the other side of a wall, ceiling, or floor
		closest_obj_id = (i - objs.begin());
		dmin_sq = dsq; // this object is the closest, even if it can't be picked up
	} // for i
	if (closest_obj_id < 0) return 0; // no object
	room_object_t &obj(objs[closest_obj_id]);
	room_object_t drawers_part;
	vect_cube_t drawers; // or doors
	bool const has_doors(obj_has_doors(obj));
	float drawer_extend(0.0); // signed, for drawers only
	get_obj_drawers_or_doors(obj, drawers, drawers_part, drawer_extend);
	dmin_sq = 0.0;
	int closest_drawer_id(-1);

	for (auto i = drawers.begin(); i != drawers.end(); ++i) {
		point p1c(at_pos), p2c(p2);
		if (!do_line_clip(p1c, p2c, i->d)) continue; // test ray intersection vs. drawer
		float const dsq(p2p_dist_sq(at_pos, p1c)); // use closest intersection point
		if (dmin_sq == 0.0 || dsq < dmin_sq) {closest_drawer_id = (i - drawers.begin()); dmin_sq = dsq;} // update if closest
	}
	if (closest_drawer_id < 0) return 0; // no drawer
	cube_t const &drawer(drawers[closest_drawer_id]); // Note: drawer cube is the interior part
	// since we're mixing doors and drawers for kitchen cabinets, check the height to width ration to determine if it's a drawer or a door
	bool const is_door(has_doors && drawer.dz() > 0.5*drawer.get_sz_dim(!obj.dim));
	if (is_door && pickup_item) return 0; // nothing to do for door when picking up items
	
	if (pickup_item && !is_door) { // pick up item in drawer rather than opening drawer; doesn't apply to doors because items aren't in the doors themselves
		if (!(obj.drawer_flags & (1U << closest_drawer_id))) return 0; // drawer is not open
		unsigned sel_item_ix(0);
		float stack_z1(0.0);
		room_object_t item;

		for (unsigned item_ix = 0; item_ix < 16; ++item_ix) { // take the *last* item in the drawer first, which will be the top item if stacked
			room_object_t const cand_item(get_item_in_drawer(drawers_part, drawer, closest_drawer_id, item_ix, stack_z1));
			if (cand_item.type == TYPE_NONE) break; // no more items
			item        = cand_item;
			sel_item_ix = item_ix;
		}
		if (item.type == TYPE_NONE) return 0; // no items in this drawer
		if (check_only)             return 1;
		if (!register_player_object_pickup(item, at_pos)) return 0;
		obj.item_flags |= (1U << (closest_drawer_id + 4*sel_item_ix)); // flag item as taken
		player_inventory.add_item(item);
		update_draw_state_for_room_object(item, building, 1);
	}
	else { // open or close the drawer/door
		cube_t c_test(drawer);
		if (is_door) {c_test.d[obj.dim][obj.dir] += (obj.dir ? 1.0 : -1.0)*drawer.get_sz_dim(!obj.dim);} // expand outward by the width of the door
		else         {c_test.d[obj.dim][obj.dir] += drawer_extend;} // drawer
		unsigned const flag_bit(1U << (unsigned)closest_drawer_id);

		if (!(obj.drawer_flags & flag_bit)) { // closed - check if door/drawer can open
			if (cube_intersects_moved_obj(c_test, closest_obj_id)) return 0; // blocked, can't open; ignore this object

			if (obj.was_moved()) { // object was moved, check if the door/drawer is now blocked
				for (auto i = objs.begin(); i != objs_end; ++i) {
					if (i->no_coll() || i->type == TYPE_BLOCKER) continue; // skip non-collidable or our own drawer blocker that was added
					if ((i - objs.begin()) == closest_obj_id)    continue; // skip self
					if (i->intersects(c_test)) {cout << int(i->type) << endl; return 0;} // blocked; should we call get_true_room_obj_bcube()?
				}
			}
		}
		if (check_only) return 1;
		obj.drawer_flags ^= flag_bit; // toggle flag bit for selected drawer

		if ((obj.drawer_flags & flag_bit) && !(obj.state_flags & flag_bit)) { // first opening of this drawer
			//if (!has_doors || is_door) {} // no spiders in kitchen counter drawers, only in doors?
			maybe_spawn_spider_in_drawer(obj, c_test, closest_drawer_id, building.get_window_vspace(), is_door);
			obj.state_flags |= flag_bit; // mark as having been opened so that we don't try to spawn another spider next time
		}
		point const drawer_center(drawer.get_cube_center());
		if (is_door) {building.play_door_open_close_sound(drawer_center, obj.is_open(), 0.5, 1.5);}
		else if (obj.type == TYPE_FCABINET) {gen_sound_thread_safe(SOUND_METAL_DOOR, building.local_to_camera_space(drawer_center), 0.5, 1.25);}
		else                                {gen_sound_thread_safe(SOUND_SLIDING,    building.local_to_camera_space(drawer_center), 0.5);}
		register_building_sound(drawer_center, 0.4);

		if (is_door) { // expand any items in the cabinet so that the player can pick them up
			// Note: expanding cabinets by opening a single door will allow the player to take items from anywhere in the cabinet, even if behind a closed door
			expand_object(obj, building);
		}
		if (has_doors) { // applies to both doors and cabinet drawers
			// find any cabinets adjacent to this one in the other dim (inside corner) and ensure the opposing door is closed so that they don't intersect
			for (auto i = objs.begin(); i != objs_end; ++i) {
				if ((is_counter(obj) && !is_counter(*i)) || (obj.type == TYPE_CABINET && i->type != TYPE_CABINET)) continue; // wrong object type
				if (i->dim == obj.dim) continue; // not opposing dim (also skips obj itself)
				float const dir_sign(i->dir ? 1.0 : -1.0);
				cube_t i_exp(*i);
				i_exp.d[i->dim][i->dir] += dir_sign*i->get_depth(); // expand other counter/cabinet to account for open doors
				if (!i_exp.intersects(c_test)) continue;
				get_cabinet_or_counter_doors(*i, drawers, drawers); // combine doors and drawers together again; invalidates <drawer>

				for (auto j = drawers.begin(); j != drawers.end(); ++j) {
					cube_t drawer_exp(*j);
					drawer_exp.d[i->dim][i->dir] += dir_sign*j->get_sz_dim(!i->dim); // expand outward by the width of the door
					if (drawer_exp.intersects(c_test)) {i->drawer_flags &= ~(1U << (j - drawers.begin()));} // make sure any intersecting doors are closed
				}
			} // for i
			update_draw_state_for_room_object(obj, building, 0); // need to update both static (for door openings) and small objects
		}
		else { // drawer
			invalidate_small_geom(); // only need to update small objects for drawers
		}
	}
	return 1;
}

room_object_t &building_room_geom_t::get_room_object_by_index(unsigned obj_id) {
	if (obj_id < objs.size()) {return objs[obj_id];}
	unsigned const exp_obj_id(obj_id - objs.size());
	assert(exp_obj_id < expanded_objs.size());
	return expanded_objs[exp_obj_id];
}

// Note: obj_vect is either objs or expanded_objs
void building_room_geom_t::remove_objs_contained_in(cube_t const &c, vect_room_object_t &obj_vect, building_t &building) {
	for (room_object_t &obj : obj_vect) {
		if (obj.type == TYPE_BLOCKER || !c.contains_pt(obj.get_cube_center())) continue;
		room_object_t const old_obj(obj); // deep copy
		obj.remove();
		update_draw_state_for_room_object(old_obj, building, 1);
	}
}
void building_room_geom_t::remove_object(unsigned obj_id, building_t &building) {
	room_object_t &obj(get_room_object_by_index(obj_id));
	room_object_t const old_obj(obj); // deep copy
	assert(obj.type != TYPE_ELEVATOR); // elevators require special updates for drawing logic and cannot be removed at this time
	player_inventory.add_item(obj);
	bldg_obj_type_t const type(get_taken_obj_type(obj)); // capture type before updating obj
	bool const is_light(obj.type == TYPE_LIGHT);

	if      (obj.type == TYPE_PICTURE   && obj.taken_level == 0) {++obj.taken_level;} // take picture, leave frame
	else if (obj.type == TYPE_PIZZA_BOX && obj.taken_level == 0 && obj.is_open()) {++obj.taken_level;} // take pizza, leave box
	else if (obj.type == TYPE_TPROLL && !(obj.taken_level > 0 || (obj.flags & RO_FLAG_WAS_EXP))) {++obj.taken_level;} // take TP roll, leave holder; not for expanded TP rolls
	else if (obj.type == TYPE_BED) {++obj.taken_level;} // take pillow(s), then sheets, then mattress
	else if (obj.type == TYPE_PLANT && !(obj.flags & RO_FLAG_ADJ_BOT)) { // plant not on a table/desk
		if (obj.taken_level > 1) {obj.remove();} // take pot - gone
		else {++obj.taken_level;} // take plant then dirt
	}
	else if (obj.type == TYPE_TOILET || obj.type == TYPE_SINK || obj.type == TYPE_TUB || obj.type == TYPE_URINAL) {
		// tubs currently can't be removed, but if they could, they would leave drains
		// leave a drain in the floor and water pipe(s); should this be on the wall instead for sinks?
		unsigned flags(RO_FLAG_NOCOLL);
		bool ddim(0), ddir(0); // for drain
		cube_t drain;

		if (obj.type == TYPE_URINAL) { // drain is on the wall
			float const radius(0.065*obj.dz());
			point pipe_p1;
			pipe_p1.z = obj.z1() + 0.25*obj.dz(); // near the bottom
			pipe_p1[!obj.dim] = obj.get_center_dim(!obj.dim);
			pipe_p1[ obj.dim] = obj.d[obj.dim][!obj.dir];
			drain.set_from_point(pipe_p1);
			drain.expand_in_dim(obj.dim, 0.05*obj.get_depth()); // set length
			drain.expand_in_dim(!obj.dim, radius);
			drain.expand_in_dim(2,        radius);
			ddim = obj.dim; ddir = 1; // orient similar to pipes
			flags |= (obj.dir ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO); // draw exposed end
		}
		else { // drain is on the floor
			float const radius(((obj.type == TYPE_SINK) ? 0.045 : 0.08)*obj.dz());
			drain.set_from_point(cube_bot_center(obj));
			drain.expand_by_xy(radius); // expand by radius
			drain.z2() += 0.02*obj.dz();
		}
		obj = room_object_t(drain, TYPE_DRAIN, obj.room_id, ddim, ddir, flags, obj.light_amt, SHAPE_CYLIN, DK_GRAY); // replace with drain
		invalidate_draw_data_for_obj(obj);
	}
	else if (obj.type == TYPE_TOY) { // take one ring at a time then the base (5 parts)
		if (obj.taken_level >= 4) {obj.remove();} // take the toy base
		else {++obj.taken_level;} // take a ring
	}
	else if (obj.type == TYPE_CEIL_FAN && obj_id > 0) { // Note: currently can't be picked up
		// find and remove the light assigned to this ceiling fan; should be a few objects before this one
		room_object_t &light_obj(get_room_object_by_index(obj.obj_id));
		if (light_obj.type == TYPE_LIGHT) {light_obj.remove();}
		obj.remove();
	}
	else { // replace it with an invisible blocker that won't collide with anything
		obj.remove();
	}
	if (old_obj.type == TYPE_MIRROR && old_obj.obj_expanded()) {
		remove_objs_contained_in(old_obj, expanded_objs, building); // search for and remove any contained medicine or other objects
	}
	if (old_obj.type == TYPE_WBOARD || old_obj.type == TYPE_PICTURE || old_obj.type == TYPE_MIRROR) {
		cube_t bc(old_obj);
		bc.d[old_obj.dim][old_obj.dir] += (old_obj.dir ? 1.0 : -1.0)*building.get_wall_thickness();
		building.remove_paint_in_cube(bc);
	}
	if (old_obj.type == TYPE_RUG || old_obj.type == TYPE_FLOORING) {
		cube_t bc(old_obj);
		bc.z2() += building.get_wall_thickness();
		building.remove_paint_in_cube(bc);
	}
	if (old_obj.type == TYPE_TCAN) {
		unsigned const ix_end(min(size_t(obj_id+11U), objs.size())); // support for up to 10 trash items

		for (unsigned i = obj_id+1; i < ix_end; ++i) {
			room_object_t &c(objs[i]);
			if (c.type == TYPE_TRASH && old_obj.contains_pt(c.get_cube_center())) {c.remove();} // remove trash as well
		}
	}
	if (is_light) {invalidate_lights_geom();}
	update_draw_state_for_room_object(old_obj, building, 1);
	building.check_for_water_splash(cube_bot_center(old_obj), 0.8);
}

int building_room_geom_t::find_avail_obj_slot() const {
	auto objs_end(get_stairs_start()); // skip stairs and elevators

	for (auto i = objs.begin(); i != objs_end; ++i) {
		if (i->type == TYPE_BLOCKER) {return int(i - objs.begin());} // blockers are used as temporaries for room object placement and to replace removed objects
	}
	return -1; // no slot found
}
void building_room_geom_t::add_expanded_object(room_object_t const &obj) {
	for (auto i = expanded_objs.begin(); i != expanded_objs.end(); ++i) {
		if (i->type == TYPE_BLOCKER) {*i = obj; return;} // found a slot - done
	}
	expanded_objs.push_back(obj); // not found - in this case we can add a new object
}
bool building_room_geom_t::add_room_object(room_object_t const &obj, building_t &building, bool set_obj_id, vector3d const &velocity) {
	assert(obj.type != TYPE_LIGHT); // lights can't be added this way
	assert(get_room_obj_type(obj).pickup); // currently must be a pickup object

	if (!set_obj_id && obj.was_expanded()) { // if object was expanded, and it's not a dynamic object, use an expanded slot (books, etc.)
		assert(velocity == zero_vector);
		add_expanded_object(obj);
	}
	else {
		int obj_id(-1);

		if (obj.type == TYPE_POOL_BALL) {
			unsigned const cand_obj_id(obj.state_flags + obj.item_flags + 1); // find it's place in the pool table
			if (cand_obj_id < buttons_start && objs[cand_obj_id].type == TYPE_BLOCKER) {obj_id = cand_obj_id;} // slot is available
		}
		if (obj_id < 0) {obj_id = find_avail_obj_slot();} // select an ID if not yet assigned
		if (obj_id < 0) return 0; // no slot found
		room_object_t &added_obj(get_room_object_by_index(obj_id));
		added_obj = obj; // overwrite with new object
		if (set_obj_id) {added_obj.obj_id = (uint16_t)(obj.has_dstate() ? allocate_dynamic_state() : obj_id);}
		if (velocity != zero_vector) {get_dstate(added_obj).velocity = velocity;}
	}
	update_draw_state_for_room_object(obj, building, 0);
	return 1;
}

float building_room_geom_t::get_combined_obj_weight(room_object_t const &obj) const {
	float weight(get_room_obj_type(obj).weight);

	if (obj.type == TYPE_BED) { // beds are special because they're composed of heavy items that can be removed
		if (obj.taken_level < 3) {weight += mattress_weight;}
		if (obj.taken_level < 2) {weight += sheets_weight  ;}
		if (obj.taken_level < 1) {weight += pillow_weight  ;}
	}
	if (!object_can_have_something_on_it(obj)) return weight; // not stackable
	weight += get_combined_stacked_obj_weights(obj, objs, get_stairs_start());
	weight += get_combined_stacked_obj_weights(obj, expanded_objs, expanded_objs.end());
	return weight;
}

bool is_movable(room_object_t const &obj) {
	if (obj.no_coll() || obj.type == TYPE_BLOCKER) return 0; // no blockers
	bldg_obj_type_t const &bot(get_room_obj_type(obj));
	return (bot.weight >= 40.0 && !bot.attached); // heavy non-attached objects, including tables
}
bool building_t::move_nearest_object(point const &at_pos, vector3d const &in_dir, float range, int mode) { // mode: 0=normal, 1=pull
	assert(has_room_geom());
	int closest_obj_id(-1);
	float dmin_sq(0.0);
	point const p2(at_pos + in_dir*range);
	vect_room_object_t &objs(interior->room_geom->objs), &expanded_objs(interior->room_geom->expanded_objs);
	auto objs_end(interior->room_geom->get_stairs_start()); // skip stairs and elevators

	// determine which object the player may be choosing to move
	for (auto i = objs.begin(); i != objs_end; ++i) {
		if (i->no_coll() || i->type == TYPE_BLOCKER) continue; // not interactive
		
		if (i->type == TYPE_POOL_TABLE) {
			// don't push pool table if there are balls on it, since it's too easily to accidentally do this when trying to hit a pool ball
			bool has_ball(0);
			
			for (unsigned n = 1; n <= 16; ++n) {
				auto const bi(i + n);
				if (bi == objs_end) break;
				if (bi->type == TYPE_POOL_BALL) {has_ball = 1; break;}
			}
			if (has_ball) continue;
		}
		cube_t const obj_bcube(get_true_obj_bcube(*i));
		point p1c(at_pos), p2c(p2);
		if (!do_line_clip(p1c, p2c, obj_bcube.d)) continue; // test ray intersection vs. bcube
		float const dsq(p2p_dist(at_pos, p1c)); // use closest intersection point
		if (dmin_sq > 0.0 && dsq > dmin_sq)       continue; // not the closest
		if (obj_has_open_drawers(*i))             continue; // can't move if any drawers are open
		if (check_for_wall_ceil_floor_int(at_pos, p1c)) continue; // skip if it's on the other side of a wall, ceiling, or floor
		closest_obj_id = (i - objs.begin()); // valid pickup object
		dmin_sq = dsq; // this object is the closest
	} // for i
	if (closest_obj_id < 0) return 0;

	// determine move direction and distance
	room_object_t &obj(objs[closest_obj_id]);
	if (!is_movable(obj))        return 0; // closest object isn't movable
	if (obj.contains_pt(at_pos)) return 0; // player is inside this object?
	float const obj_weight(interior->room_geom->get_combined_obj_weight(obj));
	float const move_dist(rand_uniform(0.5, 1.0)*CAMERA_RADIUS*(100.0f/max(75.0f, obj_weight))); // heavier objects move less; add some global randomness
	vector3d delta(obj.closest_pt(at_pos) - at_pos);
	delta.z = 0.0; // XY only
	delta.normalize();
	if (mode == 1) {delta.negate();} // changes push to pull ('r' key vs 'e' key)
	cube_t player_bcube;
	player_bcube.set_from_sphere(at_pos, get_scaled_player_radius());
	player_bcube.z1() -= get_player_height();
	// Note: setting this to 0 will allow objects to be pushed/pulled out of the room, but it may also allow doors to be closed on objects
	bool const keep_in_room = 1;

	// attempt to move the object
	for (unsigned mdir = 0; mdir < 3; ++mdir) { // X+Y, closer dim, further dim
		vector3d move_vector(zero_vector);
		if (mdir == 0) {move_vector = delta*move_dist;} // move diag in XY
		else { // move in one dim
			if (delta.x == 0.0 || delta.y == 0.0) break; // no more dims to try (only one mdir iteration)
			bool const dim(fabs(delta.x) < fabs(delta.y));
			move_vector[dim] = delta[dim]*move_dist;
		}
		for (unsigned n = 0; n < 5; ++n, move_vector *= 0.5) { // move in several incrementally smaller steps
			room_object_t moved_obj(obj);
			moved_obj += move_vector; // only the position changes
			if (player_bcube.intersects(moved_obj)) continue; // don't intersect the player - applies to pull mode
			if (!is_obj_pos_valid(moved_obj, keep_in_room, !keep_in_room, 1)) continue; // allow_block_door=!keep_in_room, check_stairs=1
			bool bad_placement(0);

			for (auto i = objs.begin(); i != objs_end && !bad_placement; ++i) {
				if (i == objs.begin() + closest_obj_id)      continue; // skip self
				if (i->no_coll() || i->type == TYPE_BLOCKER) continue; // skip non-coll objects and blockers that add clearance between objects as these won't block this obj
				if (obj.type == TYPE_BED && is_ball_type(i->type)) continue; // ignore ball on/under bed
				
				if (i->is_open() && i->is_small_closet()) { // check open closet door collision
					cube_t cubes[5]; // front left, left side, front right, right side, door
					get_closet_cubes(*i, cubes, 1); // get cubes for walls and door; for_collision=1
					for (unsigned n = 0; n < 5; ++n) {bad_placement |= (!cubes[n].is_all_zeros() && cubes[n].intersects(moved_obj));}
				}
				else {bad_placement = i->intersects(moved_obj);}
			} // for i
			// Note: okay to skip expanded_objs because these should already be on/inside some other object; this allows us to move wine racks containing wine
			if (bad_placement) continue; // intersects another object, try a smaller movement
			interior->room_geom->invalidate_draw_data_for_obj(obj);

			// move objects inside or on top of this one
			for (unsigned vect_id = 0; vect_id < 2; ++vect_id) {
				auto &obj_vect((vect_id == 1) ? expanded_objs : objs);
				auto obj_vect_end((vect_id == 1) ? expanded_objs.end() : objs_end); // skip stairs and elevators

				for (auto i = obj_vect.begin(); i != obj_vect_end; ++i) {
					if (i->type == TYPE_BLOCKER || *i == obj) continue; // ignore blockers and self
					if (!is_obj_in_or_on_obj(obj, *i))        continue;
					*i += move_vector; // move this object as well
					i->flags |= RO_FLAG_MOVED;
					if (!keep_in_room) {assign_correct_room_to_object(*i);}
					interior->room_geom->invalidate_draw_data_for_obj(*i);
				} // for i
			} // for vect_id
			// mark doors as blocked
			room_t const &room(get_room(obj.room_id));

			for (auto i = interior->doors.begin(); i != interior->doors.end(); ++i) { // check for door intersection
				if (keep_in_room) { // in this case the object can't be pushed into the path of the door
					if (i->open || !door_opens_inward(*i, room)) continue; // if the door is already open, or opens in the other direction, it can't be blocked
				}
				bool const inc_open(0), check_dirs(i->get_check_dirs());
				if (is_cube_close_to_door(moved_obj, 0.0, inc_open, *i, check_dirs))              {i->blocked = 1;} // newly blocked  , either dir
				else if (i->blocked && is_cube_close_to_door(obj, 0.0, inc_open, *i, check_dirs)) {i->blocked = 0;} // newly unblocked, either dir
				else {continue;}
				if (!i->for_closet) {interior->door_state_updated = 1;} // trigger AI update if this is a door between rooms
			} // for i
			// update this object
			obj = moved_obj; // keep this placement
			if (!keep_in_room) {assign_correct_room_to_object(obj);}
			if (!obj.was_moved()) {interior->room_geom->moved_obj_ids.push_back(closest_obj_id);} // add to moved_obj_ids on first movement
			obj.flags |= RO_FLAG_MOVED;
			interior->room_geom->modified_by_player = 1; // flag so that we avoid re-generating room geom if the player leaves and comes back
			gen_sound_thread_safe_at_player(SOUND_SLIDING, 1.0, 1.0, 1); // skip_if_already_playing=1
			register_building_sound_at_player(0.7);
			return 1; // success
		} // for n
	} // for mdir
	return 0; // failed
}

void play_obj_fall_sound(room_object_t const &obj, point const &player_pos) {
	gen_sound_thread_safe(SOUND_OBJ_FALL, (get_camera_pos() + (obj.get_cube_center() - player_pos)));
	register_building_sound_for_obj(obj, player_pos);
}

void building_t::assign_correct_room_to_object(room_object_t &obj) const {
	int const room_id(get_room_containing_pt(obj.get_cube_center()));

	if (room_id >= 0 && room_id != obj.room_id) { // room should be valid, but okay if not; don't update lighting if it's the same room
		obj.room_id   = room_id;
		obj.light_amt = 0.5f + 0.5f*get_window_vspace()*interior->rooms[room_id].get_light_amt(); // blend 50% max light to avoid harsh changes when moving between rooms
	}
}

void drop_inventory_item(building_t const &b, room_object_t const &obj, point const &player_pos) {
	player_inventory.return_object_to_building(obj); // re-add this object's value
	player_inventory.remove_last_item(); // used
	if (!b.check_for_water_splash(obj.get_cube_center(), 0.5)) {play_obj_fall_sound(obj, player_pos);} // splash or drop sound; should it be based on weight?
}
bool building_t::drop_room_object(room_object_t &obj, point const &dest, point const &player_pos, bool dim, bool dir) {
	obj.dim    = dim;
	obj.dir    = dir;
	obj.flags |= RO_FLAG_WAS_EXP;
	obj.taken_level = 1;
	obj.translate(dest - cube_bot_center(obj));
	assign_correct_room_to_object(obj); // set new room; required for opening books; room should be valid, but okay if not
	if (point_in_attic(obj.get_cube_center())) {obj.flags |= RO_FLAG_IN_ATTIC;} else {obj.flags &= ~RO_FLAG_IN_ATTIC;} // set attic flag
	if (!interior->room_geom->add_room_object(obj, *this)) return 0;
	maybe_squish_animals(obj, player_pos);
	return 1;
}

bool building_t::maybe_use_last_pickup_room_object(point const &player_pos, bool no_time_check, bool random_dir) { // Note: player_pos is in building space
	if (player_in_elevator) return 0; // can't use items in elevators
	assert(has_room_geom());
	static bool delay_use(0);
	static double last_use_time(0.0);
	if (!no_time_check && delay_use && (tfticks - last_use_time) < 0.5*TICKS_PER_SECOND) return 0; // half second delay on prev item use or switch
	delay_use = 0;
	room_object_t obj;
	if (!player_inventory.try_use_last_item(obj)) return 0;
	float const player_radius(get_scaled_player_radius());
	vector3d dir(cview_dir); // camera view direction

	if (random_dir) { // used for player inventory drop items
		static rand_gen_t rgen;
		dir = rgen.signed_rand_vector_spherical_xy();
	}
	if (obj.has_dstate()) { // it's a dynamic object (ball), throw it; only activated with use_object/'E' key
		point dest(player_pos + (1.2f*(player_radius + obj.get_radius()))*dir);
		dest.z -= 0.5*player_radius; // slightly below the player's face
		obj.translate(dest - cube_bot_center(obj));
		obj.flags |= RO_FLAG_DYNAMIC; // make it dynamic, assuming it will be dropped/thrown
		if (!interior->room_geom->add_room_object(obj, *this, 1, THROW_VELOCITY*dir)) return 0;
		player_inventory.return_object_to_building(obj); // re-add this object's value
		if (!check_for_water_splash(obj.get_cube_center(), 0.7)) {play_obj_fall_sound(obj, player_pos);} // splash or drop
		delay_use = 1;
	}
	else if (obj.can_use()) { // active with either use_object or fire key
		if (obj.type == TYPE_TPROLL) {
			if (player_in_water) return 0; // can't place TP in water
			point const dest(player_pos + (1.5f*player_radius)*dir);
			if (!apply_toilet_paper(dest, dir, 0.5*obj.dz())) return 0;
			player_inventory.mark_last_item_used();
		}
		else if (obj.type == TYPE_SPRAYCAN || obj.type == TYPE_MARKER) { // spraypaint or marker
			if (player_in_water == 2) return 0; // can't use when fully underwater
			unsigned emissive_color_id(0);
			if (obj.type == TYPE_SPRAYCAN && obj.color == GD_SP_COLOR) {emissive_color_id = 1 + (obj.obj_id % NUM_SP_EMISSIVE_COLORS);} // spraypaint glows in the dark
			if (!apply_paint(player_pos, dir, obj.color, emissive_color_id, obj.type)) return 0;
			player_inventory.mark_last_item_used();
		}
		else if (obj.type == TYPE_TAPE) {
			if (player_in_water == 2) return 0; // can't use when fully underwater
			tape_manager.toggle_use(obj, this);
		}
		else if (obj.type == TYPE_BOOK || obj.type == TYPE_RAT) { // items that can be dropped
			bool const is_rat(obj.type == TYPE_RAT);
			float const half_width(0.5*max(max(obj.dx(), obj.dy()), obj.dz())); // use conservative max dim
			point dest(player_pos + (1.2f*(player_radius + half_width))*dir);

			if (is_rat) {
				bool const was_dead(obj.is_broken());
				bool is_dead(was_dead);
				bool const dropped(add_rat(dest, half_width, dir, player_pos, is_dead)); // facing away from the player
				if (is_dead && !was_dead) {player_inventory.mark_last_item_broken();}
				
				if (!was_dead) { // squeak if alive
					gen_sound_thread_safe_at_player(SOUND_RAT_SQUEAK); // play the sound whether or not we can drop the rat
					register_building_sound(player_pos, 0.8);
				}
				if (!dropped) return 0;
			}
			else { // book; orient based on the player's primary direction
				if (!get_zval_for_obj_placement(dest, half_width, dest.z, 0)) return 0; // no suitable placement found; add_z_bias=0
				//obj.flags |= RO_FLAG_RAND_ROT; // maybe set this for some random books to have them misaligned? or does that cause problems with clipping through objects?
				bool const place_dim(fabs(dir.y) < fabs(dir.x)), place_dir((dir[!place_dim] > 0) ^ place_dim);

				if (obj.dim != place_dim) {
					float const dx(obj.dx()), dy(obj.dy());
					obj.x2() = obj.x1() + dy;
					obj.y2() = obj.y1() + dx;
				}
				if (!drop_room_object(obj, dest, player_pos, place_dim, place_dir)) return 0;
			}
			drop_inventory_item(*this, obj, player_pos);
			delay_use = 1;
		}
		else if (obj.type == TYPE_PHONE) {
			phone_manager.player_action();
		}
		else if (obj.is_medicine()) {
			if (!player_at_full_health()) { // don't use if not needed
				player_inventory.use_medicine();
				player_inventory.mark_last_item_used(); // will remove it
			}
		}
		else if (obj.type == TYPE_FIRE_EXT) {
			if (obj.is_broken()) { // empty, drop it
				float const radius(obj.get_radius());
				point dest(player_pos + (1.2f*(player_radius + radius))*dir);
				if (!get_zval_for_obj_placement(dest, radius, dest.z, 0)) return 0; // can't drop, so keep it in the inventory
				bool const place_dim(fabs(dir.y) < fabs(dir.x)), place_dir(dir[!place_dim] > 0);
				if (!drop_room_object(obj, dest, player_pos, !place_dim, place_dir)) return 0;
				drop_inventory_item(*this, obj, player_pos);
				return 1;
			}
			if (player_in_water == 2) return 0; // can't spray when fully underwater
			static double next_sound_time(0.0);

			if (tfticks > next_sound_time) { // play sound if sprayed/marked, but not too frequently; marker has no sound
				gen_sound_thread_safe_at_player(SOUND_SPRAY, 0.5);
				register_building_sound(player_pos, 0.1);
				next_sound_time = tfticks + 0.25*TICKS_PER_SECOND;
			}
			static rand_gen_t rgen;
			float const obj_radius(obj.get_radius()), r_sum(player_radius + obj_radius);
			point const part_pos(player_pos + (1.1f*r_sum)*dir);
			vector3d const velocity(0.0015*(dir + 0.12*rgen.signed_rand_vector())); // add a bit of random variation
			point const ray_start(player_pos + player_radius*dir);
			interior->room_geom->particle_manager.add_particle(part_pos, velocity, WHITE, 1.0*obj_radius, PART_EFFECT_CLOUD);
			player_inventory.mark_last_item_used();
			player_inventory.record_damage_done(0.05); // very small amount of damage

			if (!interior->people.empty()) { // check for people in range
				point const zombie_ray_end(player_pos + (2.0f*r_sum)*dir);

				for (unsigned i = 0; i < interior->people.size(); ++i) {
					cube_t const person_bcube(interior->people[i].get_bcube());
					if (person_bcube.line_intersects(ray_start, zombie_ray_end)) {maybe_zombie_retreat(i, part_pos);}
				}
			}
			point const fire_ray_end(player_pos + (4.0f*r_sum)*dir); // longer range
			interior->room_geom->fire_manager.put_out_fires(ray_start, fire_ray_end, 0.5*r_sum); // check for fires in range and put them out
		}
		else if (obj.type == TYPE_CANDLE) { // nothing else to do at the moment
			delay_use = 1;
		}
		//else if (obj.type == TYPE_FLASHLIGHT) {} // only use flashlight when selected in inventory?
		else {assert(0);}
	}
	else {assert(0);}
	last_use_time = tfticks;
	return 1;
}

// adds two back-to-back quads for two sided lighting
void add_tape_quad(point const &p1, point const &p2, float width, color_wrapper const &color, quad_batch_draw &qbd, vector3d const &wdir=plus_z) {
	vector3d const dir(p2 - p1), wvect(0.5*width*wdir);
	vector3d normal(cross_product(dir, wdir).get_norm());
	point pts[4] = {(p1 - wvect), (p1 + wvect), (p2 + wvect), (p2 - wvect)};
	qbd.add_quad_pts(pts, color,  normal);
	swap(pts[1], pts[3]); // swap winding order and draw with reversed normal for two sided lighting
	qbd.add_quad_pts(pts, color, -normal);
}

void building_t::play_tape_sound(point const &sound_pos, float sound_gain, bool tape_break) const {
	int const sound_id(tape_break ? get_sound_id_for_file("tape_pull.wav") : get_sound_id_for_file("tape.wav"));
	gen_sound_thread_safe(sound_id, local_to_camera_space(sound_pos), sound_gain);
	register_building_sound(sound_pos, 0.35*sound_gain);
}

bool building_t::maybe_update_tape(point const &player_pos, bool end_of_tape) {
	if (!tape_manager.in_use) return 0;
	assert(has_room_geom());
	auto &decal_mgr(interior->room_geom->decal_manager);
	room_object_t const &obj(tape_manager.tape);
	float const thickness(obj.dz()), pad_dist(0.1*thickness);
	point pos(player_pos + (1.5f*get_scaled_player_radius())*cview_dir);
	interior->line_coll(*this, player_pos, pos, pos); // clip the camera ray to any geometry to avoid putting the point on the wrong side of a wall
	point sound_pos;
	float sound_gain(0.0);

	if (end_of_tape) { // add final tape
		if (tape_manager.points.empty()) return 0; // no tape
		decal_mgr.pend_tape_qbd.clear();
		point const end_pos(interior->find_closest_pt_on_obj_to_pos(*this, pos, pad_dist, 1)); // no_ceil_floor=1
		add_tape_quad(tape_manager.points.back(), end_pos, thickness, obj.color, decal_mgr.tape_qbd); // add final segment
		sound_pos = end_pos; sound_gain = 1.0;
	}
	else if (tape_manager.points.empty()) { // first point
		point const start_pos(interior->find_closest_pt_on_obj_to_pos(*this, pos, pad_dist, 1)); // starting position for tape; no_ceil_floor=1
		tape_manager.points.push_back(start_pos);
		decal_mgr.commit_pend_tape_qbd(); // commit any previous tape
		interior->room_geom->modified_by_player = 1; // make sure tape stays in this building
		sound_pos = start_pos; sound_gain = 1.0;
	}
	else {
		point last_pt(tape_manager.points.back()), p_int;

		if (!dist_less_than(last_pt, pos, thickness) && interior->line_coll(*this, last_pt, pos, p_int)) { // no short segments
			p_int += 0.5*thickness*(pos - last_pt).get_norm(); // move past the object to avoid an intersection at the starting point on the next call
			tape_manager.points.push_back(p_int);
			add_tape_quad(last_pt, p_int, thickness, obj.color, decal_mgr.tape_qbd);
			last_pt = p_int;
			//sound_pos = p_int; sound_gain = 0.1; // too noisy?
		}
		decal_mgr.pend_tape_qbd.clear();
		add_tape_quad(last_pt, pos, thickness, obj.color, decal_mgr.pend_tape_qbd);
		// update use count based on length change
		float const prev_dist(p2p_dist(last_pt, tape_manager.last_pos)), cur_dist(p2p_dist(last_pt, pos)), delta(cur_dist - prev_dist);
		int const delta_use_count(round_fp(0.5f*delta/thickness));
		if (!player_inventory.update_last_item_use_count(delta_use_count)) {tape_manager.clear();} // check if we ran out of tape
	}
	if (sound_gain > 0.0) {play_tape_sound(sound_pos, sound_gain, 0);} // tape_break=0
	tape_manager.last_pos = pos;
	return 1;
}

// returns the index of the first quad vertex, or -1 if no intersection found
int tape_quad_batch_draw::moving_vert_cyilin_int_tape(point &cur_pos, point const &prev_pos, float z1, float z2, float radius, float slow_amt, bool is_player) const {
	if (verts.empty()) return -1;
	if (cur_pos == prev_pos) return -1; // stopped, no effect
	assert(!(verts.size() % 12)); // must be a multiple of 12 (pairs of quads formed from two triangles)
	assert(slow_amt >= 0.0 && slow_amt <= 1.0); // 0.0 => no change to cur_pos, 1.0 => move back to prev_pos
	cylinder_3dw const cylin(point(cur_pos.x, cur_pos.y, z1), point(cur_pos.x, cur_pos.y, z2), radius, radius);

	for (unsigned i = 0; i < verts.size(); i += 12) { // iterate over pairs of back-to-back quads
		point const &p1(verts[i].v), &p2(verts[i+1].v); // get two opposite corners of the first quad, which approximates the line of the tape
		if (dist_xy_less_than(p1, p2, 1.5*radius)) continue; // skip short lines; ignore zval to skip already split vertical tape segments
		if (!line_intersect_cylinder(p1, p2, cylin, 1)) continue; // check_ends=1
		if (pt_line_dist(prev_pos, p1, p2) < pt_line_dist(cur_pos, p1, p2)) continue; // new point is further from the line - moving away, skip
		if (slow_amt > 0.0) {cur_pos = slow_amt*prev_pos + (1.0 - slow_amt)*cur_pos;}
		// hack to avoid player intersection with just-placed tape: skip if prev pos also intersects when not slowing
		else if (is_player && line_intersect_cylinder(p1, p2, cylinder_3dw(point(prev_pos.x, prev_pos.y, z1), point(prev_pos.x, prev_pos.y, z2), radius, radius), 1)) continue;
		return i;
	}
	return -1; // not found
}
void tape_quad_batch_draw::split_tape_at(unsigned first_vert, point const &pos, float min_zval) {
	assert(first_vert+12 <= verts.size());
	assert(!(first_vert % 12)); // must be the start of a quad pair
	point p1(verts[first_vert].v), p2(verts[first_vert+1].v); // get two opposite corners of the first quad; copy to avoid invalid reference
	// find the lengths of both hanging segments, clipped to min_zval (the floor)
	float const d1(p2p_dist(p1, pos)), d2(p2p_dist(p2, pos)), t(d1/(d1 + d2)); // find split point
	float const width(p2p_dist(p1, verts[first_vert+2].v));
	float const len(p2p_dist(p1, p2)), len1(min(t*len, (p1.z - min_zval))), len2(min((1.0f - t)*len, (p2.z - min_zval))); // should be positive?
	// find the bottom points of the two hanging segments
	vector3d const dir((p2 - p1).get_norm());
	//p1 += 0.1*width*dir; p2 -= 0.1*width*dir; // shift all points slightly inward to avoid z-fighting
	point const p1b(p1.x, p1.y, p1.z-len1), p2b(p2.x, p2.y, p2.z-len2);
	vector3d wdir(cross_product(dir, plus_z).get_norm()); // segments hang with normal oriented toward the split point/along the original dir
	color_wrapper const cw(verts[first_vert]);

	if (len1 > 0.0) {
		unsigned const verts_start(verts.size());
		add_tape_quad(p1, p1b, width, cw, *this, wdir);
		assert(verts.size() == verts_start+12);
		for (unsigned i = 0; i < 12; ++i) {verts[first_vert+i] = verts[verts_start+i];} // overwrite old verts with new verts
		verts.resize(verts_start); // remove new verts
	}
	else { // zero length segment, overwrite with zeros to remove this quad
		for (unsigned i = 0; i < 12; ++i) {verts[first_vert+i].v = zero_vector;}
	}
	if (len2 > 0.0) {add_tape_quad(p2, p2b, width, cw, *this, -wdir);} // add the other segment
}

// Note: cur_pos.z should be between z1 and z2
void building_t::handle_vert_cylin_tape_collision(point &cur_pos, point const &prev_pos, float z1, float z2, float radius, bool is_player) const {
	if (!has_room_geom()) return;
	tape_quad_batch_draw &tape_qbd(interior->room_geom->decal_manager.tape_qbd); // Note: technically, this violates const-ness of this function
	// first, test if tape is very close, and if so, break it; otherwise, slow down the player/AI when colliding with tape
	int const vert_ix(tape_qbd.moving_vert_cyilin_int_tape(cur_pos, prev_pos, z1, z2, 0.2*radius, 0.0, is_player)); // 20% radius, slow_amt=0.0
	
	if (vert_ix >= 0) {
		tape_qbd.split_tape_at(vert_ix, cur_pos, z1); // min_zval=z1
		play_tape_sound(cur_pos, 1.0, 1); // tape_break=1
	}
	else {tape_qbd.moving_vert_cyilin_int_tape(cur_pos, prev_pos, z1, z2, radius, 0.85, is_player);} // slow_amt=0.85
}

// spraypaint, markers, and decals

bool line_int_cube_get_t(point const &p1, point const &p2, cube_t const &cube, float &tmin) {
	float tmin0(0.0), tmax0(1.0);
	if (get_line_clip(p1, p2, cube.d, tmin0, tmax0) && tmin0 < tmin) {tmin = tmin0; return 1;}
	return 0;
}
template<typename T> bool line_int_cubes_get_t(point const &p1, point const &p2, vector<T> const &cubes, float &tmin, cube_t &target) { // cube_t, cube_with_ix_t, etc.
	bool had_int(0);

	for (auto c = cubes.begin(); c != cubes.end(); ++c) {
		if (line_int_cube_get_t(p1, p2, *c, tmin)) {target = *c; had_int = 1;}
	}
	return had_int;
}
vector3d get_normal_for_ray_cube_int_xy(point const &p, cube_t const &c, float tolerance) {
	vector3d n(zero_vector);

	for (unsigned d = 0; d < 2; ++d) { // find the closest intersecting cube XY edge, which will determine the normal vector
		if (fabs(p[d] - c.d[d][0]) < tolerance) {n[d] = -1.0; break;} // test low  edge
		if (fabs(p[d] - c.d[d][1]) < tolerance) {n[d] =  1.0; break;} // test high edge
	}
	return n;
}

class paint_manager_t : public paint_draw_t { // for paint on exterior walls/windows, viewed from inside the building
	building_t const *paint_bldg = nullptr;
public:
	bool have_paint_for_building() const { // only true if the building contains the player
		return (paint_bldg && (have_any_sp() || !m_qbd.empty()) && paint_bldg->bcube.contains_pt(get_camera_building_space()));
	}
	quad_batch_draw &get_paint_qbd_for_bldg(building_t const *const building, bool is_marker, unsigned emissive_color_id) {
		if (building != paint_bldg) { // paint switches to this building
			clear();
			paint_bldg = building;
		}
		return get_paint_qbd(is_marker, emissive_color_id);
	}
};

paint_manager_t ext_paint_manager;
bool have_buildings_ext_paint() {return ext_paint_manager.have_paint_for_building();}
void draw_buildings_ext_paint(shader_t &s) {ext_paint_manager.draw_paint(s);}
float get_paint_max_radius(bool is_spraypaint) {return (is_spraypaint ? 2.0 : 0.035)*CAMERA_RADIUS;}

float get_paint_radius(point const &source, point const &hit_pos, bool is_spraypaint) { // for spray paint and markers
	float const max_radius(get_paint_max_radius(is_spraypaint));
	return (is_spraypaint ? min(max_radius, max(0.05f*max_radius, 0.1f*p2p_dist(source, hit_pos))) : max_radius); // modified version of get_spray_radius()
}

bool building_t::apply_paint(point const &pos, vector3d const &dir, colorRGBA const &color, unsigned emissive_color_id, room_object const obj_type) const { // spraypaint/marker
	bool const is_spraypaint(obj_type == TYPE_SPRAYCAN), is_marker(obj_type == TYPE_MARKER);
	assert(is_spraypaint || is_marker); // only these two are supported
	// find intersection point and normal; assumes pos is inside the building
	assert(has_room_geom());
	float const max_dist((is_spraypaint ? 16.0 : 3.0)*CAMERA_RADIUS), tolerance(0.01*get_wall_thickness());
	vector3d const delta(max_dist*dir);
	point const pos2(pos + delta);
	float tmin(1.0);
	vector3d normal;
	cube_t target;
	
	for (unsigned d = 0; d < 2; ++d) {
		if (line_int_cubes_get_t(pos, pos2, interior->walls[d], tmin, target)) {
			normal    = zero_vector;
			normal[d] = -SIGN(dir[d]); // normal is opposite of ray dir in this dim
		}
		if (is_pos_in_pg_or_backrooms(pos) && line_int_cubes_get_t(pos, pos2, interior->room_geom->pgbr_walls[d], tmin, target)) {
			normal    = zero_vector;
			normal[d] = -SIGN(dir[d]); // normal is opposite of ray dir in this dim
		}
	} // for d
	if (line_int_cubes_get_t(pos, pos2, interior->floors  , tmin, target)) {normal =  plus_z;}
	if (line_int_cubes_get_t(pos, pos2, interior->ceilings, tmin, target)) {normal = -plus_z;}
	
	// check closed interior doors
	for (auto i = interior->doors.begin(); i != interior->doors.end(); ++i) {
		if (i->open_amt > 0.0) continue;
		cube_t door(i->get_true_bcube());
		if (!line_int_cube_get_t(pos, pos2, door, tmin)) continue;
		target = door;
		normal = zero_vector;
		normal[i->dim] = -SIGN(dir[i->dim]);
	} // for i
	// check for rugs, pictures, and whiteboards, which can all be painted over; also check for walls from closets
	auto objs_end(interior->room_geom->get_placed_objs_end()); // skip buttons/stairs/elevators
	bool const is_wall(normal.x != 0.0 || normal.y != 0.0), is_floor(normal == plus_z);
	bool walls_blocked(0);

	for (auto i = interior->room_geom->objs.begin(); i != objs_end; ++i) {
		float const pre_tmin(tmin);

 		if ((is_wall && (i->type == TYPE_PICTURE || i->type == TYPE_WBOARD || i->type == TYPE_MIRROR)) ||
			(is_floor && (i->type == TYPE_RUG || i->type == TYPE_FLOORING)) ||
			(i->type == TYPE_POOL_TILE && i->shape == SHAPE_CUBE))
		{
			if (line_int_cube_get_t(pos, pos2, *i, tmin)) {target = *i;} // Note: return value is ignored, we only need to update tmin and target; normal should be unchanged
		}
		else if (i->type == TYPE_CLOSET && line_int_cube_get_t(pos, pos2, *i, tmin)) {
			point const cand_p_int(pos + tmin*delta);

			if (i->is_open()) { // exclude open doors
				cube_t door(get_open_closet_door(*i));
				door.expand_in_dim(i->dim, 0.5*get_wall_thickness());
				if (line_int_cube_get_t(pos, pos2, door, tmin)) {tmin = pre_tmin; continue;} // restore tmin and skip
			}
			normal = get_normal_for_ray_cube_int_xy(cand_p_int, *i, tolerance); // should always return a valid normal
			target = *i;
		}
		else if (i->type == TYPE_STALL || i->type == TYPE_CUBICLE) {
			cube_t c(*i);

			if (i->type == TYPE_STALL && i->shape != SHAPE_SHORT) { // toilet stall, clip cube to wall height
				float const dz(c.dz());
				c.z2() -= 0.35*dz; c.z1() += 0.15*dz;
			}
			float tmin0(tmin);
			if (!line_int_cube_get_t(pos, pos2, c, tmin0)) continue;
			if (i->contains_pt(pos)) continue; // inside stall/cubicle, can't paint the exterior
			vector3d const n(get_normal_for_ray_cube_int_xy((pos + tmin0*delta), c, tolerance)); // should always return a valid normal
			if (n[i->dim] != 0) {walls_blocked = 1; continue;} // only the side walls count; avoids dealing with open doors
			tmin = tmin0; normal = n; target = c;
		}
	} // for i
	for (auto i = interior->elevators.begin(); i != interior->elevators.end(); ++i) { // check elevators
		float tmin0(tmin);
		if (!line_int_cube_get_t(pos, pos2, *i, tmin0)) continue;
		if (i->contains_pt(pos)) {walls_blocked = 1; continue;} // can't spraypaint the outside of the elevator when standing inside it
		vector3d const n(get_normal_for_ray_cube_int_xy((pos + tmin0*delta), *i, tolerance)); // should always return a valid normal
		if (n[i->dim] == (i->dir ? 1.0 : -1.0)) {walls_blocked = 1; continue;} // skip elevator opening, even if not currently open
		tmin = tmin0; normal = n; target = *i;
	} // for i
	for (auto i = interior->stairwells.begin(); i != interior->stairwells.end(); ++i) { // check stairs
		if (!i->is_u_shape() && !i->has_walled_sides()) continue; // no walls, skip
		// expand by wall half-width; see building_t::add_stairs_and_elevators()
		cube_t c(*i);
		c.expand_by_xy(STAIRS_WALL_WIDTH_MULT*i->get_step_length()); // wall half width
		float tmin0(tmin);
		if (!line_int_cube_get_t(pos, pos2, c, tmin0)) continue;
		if (c.contains_pt(pos)) {walls_blocked = 1; continue;} // can't spraypaint the outside of the stairs when standing inside them
		vector3d const n(get_normal_for_ray_cube_int_xy((pos + tmin0*delta), c, tolerance)); // should always return a valid normal

		if (i->is_u_shape()) {
			if (n[i->dim] == (i->dir ? -1.0 : 1.0)) {walls_blocked = 1; continue;} // skip stairs opening
		}
		else if (i->has_walled_sides()) {
			// Note: we skip the end for SHAPE_WALLED and only check the sides because it depends on the floor we're on
			if (n[i->dim] != 0) {walls_blocked = 1; continue;} // skip stairs opening, either side
		}
		else {assert(0);} // unsupported stairs type
		tmin = tmin0; normal = n; target = c;
	} // for i
	// check exterior walls; must be done last; okay to add spraypaint and markers over windows but not over doors since they can be opened
	cube_t const part(get_part_containing_pt(pos));
	float tmin0(0.0), tmax0(1.0);
	bool exterior_wall(0);

	if (get_line_clip(pos, pos2, part.d, tmin0, tmax0) && tmax0 < tmin) { // part edge is the closest intersection point
		// check other parts to see if ray continues into them; if not, it exited the building; this implementation isn't perfect but should be close enough
		point const cand_p_int(pos + tmax0*delta);
		bool found(0);

		for (auto p = parts.begin(); p != get_real_parts_end(); ++p) {
			if (*p == part || !p->contains_pt_exp(cand_p_int, tolerance)) continue; // ray does not continue into this new part
			if (check_line_clip(cand_p_int, pos2, p->d)) {found = 1; break;} // ray continues into this part
		}
		if (!found) { // ray has exited the building
			vector3d const n(-get_normal_for_ray_cube_int_xy(cand_p_int, part, tolerance)); // negate the normal because we're looking for the exit point from the cube
			
			if (n != zero_vector) {
				float const radius(get_paint_radius(pos, cand_p_int, is_spraypaint));
				bool hit_ext_door(0);

				for (auto const &d : doors) {
					if (d.get_bcube().contains_pt_exp(cand_p_int, radius)) {hit_ext_door = 1; break;}
				}
				if (!hit_ext_door) {tmin = tmax0; normal = n; target = part; exterior_wall = 1;}
			}
		}
	}
	if (normal == zero_vector)            return 0; // no walls, ceilings, floors, etc. hit
	if (walls_blocked && normal.z == 0.0) return 0; // can't spraypaint walls through elevator, stairs, etc.
	point p_int(pos + tmin*delta);
	if (check_line_intersect_doors(pos, p_int, 1))       return 0; // blocked by door, no spraypaint; can't add spraypaint over door in case door is opened; inc_open=1
	if (has_pool() && interior->pool.contains_pt(p_int)) return 0; // can't use in the pool
	float const max_radius(get_paint_max_radius(is_spraypaint)), radius(get_paint_radius(pos, p_int, is_spraypaint));
	float const alpha((is_spraypaint && radius > 0.5*max_radius) ? (1.0 - (radius - 0.5*max_radius)/max_radius) : 1.0); // 0.5 - 1.0
	p_int += 0.01*radius*normal; // move slightly away from the surface
	assert(get_bcube_inc_extensions().contains_pt(p_int));
	unsigned const dim(get_max_dim(normal)), d1((dim+1)%3), d2((dim+2)%3);

	// check that entire circle is contained in the target
	for (unsigned e = 0; e < 2; ++e) {
		unsigned const d(e ? d2 : d1);
		if (p_int[d] - 0.9*radius < target.d[d][0] || p_int[d] + 0.9*radius > target.d[d][1]) return 0; // extends outside the target surface in this dim
	}
	static point last_p_int(all_zeros);
	if (dist_less_than(p_int, last_p_int, 0.25*radius)) return 1; // too close to previous point, skip (to avoid overlapping sprays at the same location); still return 1
	last_p_int = p_int;
	vector3d dir1, dir2; // unit vectors
	dir1[d1] = 1.0; dir2[d2] = 1.0;
	float const winding_order_sign(-SIGN(normal[dim])); // make sure to invert the winding order to match the normal sign
	// Note: interior spraypaint draw uses back face culling while exterior draw does not; invert the winding order for exterior quads so that they show through windows correctly
	vector3d const dx(radius*dir1*winding_order_sign*(exterior_wall ? -1.0 : 1.0));
	colorRGBA const paint_color(((emissive_color_id > 0) ? WHITE : color), alpha); // color is always white if emissive
	quad_batch_draw &qbd(interior->room_geom->decal_manager.paint_draw[exterior_wall].get_paint_qbd(is_marker, emissive_color_id));
	qbd.add_quad_dirs(p_int, dx, radius*dir2, paint_color, normal); // add interior/exterior paint
	
	if (exterior_wall) { // add exterior paint only; will be drawn after building interior, but without iterior lighting, so it will be darker
		ext_paint_manager.get_paint_qbd_for_bldg(this, is_marker, emissive_color_id).add_quad_dirs(p_int, dx, radius*dir2, paint_color, normal);
	}
	static double next_sound_time(0.0);

	if (tfticks > next_sound_time) { // play sound if sprayed/marked, but not too frequently; marker has no sound
		gen_sound_thread_safe_at_player((is_spraypaint ? (int)SOUND_SPRAY : (int)SOUND_SQUEAK), 0.25);
		if (is_spraypaint) {register_building_sound(pos, 0.1);}
		next_sound_time = tfticks + double(is_spraypaint ? 0.5 : 0.25)*TICKS_PER_SECOND;
	}
	player_inventory.record_damage_done(is_spraypaint ? 1.0 : 0.1); // spraypaint does more damage than markers
	return 1;
}

void remove_quads_in_bcube_from_qbd(cube_t const &c, quad_batch_draw &qbd) {
	unsigned const num_verts(qbd.verts.size());
	assert((num_verts % 6) == 0); // must be quads formed from pairs of triangles

	for (unsigned n = 0; n < num_verts; n += 6) { // iterate over quads
		point center;
		for (unsigned m = 0; m < 6; ++m) {center += qbd.verts[n+m].v;}
		if (!c.contains_pt(center/6.0)) continue;
		for (unsigned m = 0; m < 6; ++m) {qbd.verts[n+m].v = zero_vector;} // move all points to the origin to remove this quad
	} // for n
}
void building_t::remove_paint_in_cube(cube_t const &c) const { // for whiteboards, pictures, etc.
	if (!has_room_geom()) return;

	for (unsigned exterior_wall = 0; exterior_wall < 2; ++exterior_wall) {
		paint_draw_t &pd(interior->room_geom->decal_manager.paint_draw[exterior_wall]);
		remove_quads_in_bcube_from_qbd(c, pd.m_qbd);
		for (unsigned n = 0; n <= NUM_SP_EMISSIVE_COLORS; ++n) {remove_quads_in_bcube_from_qbd(c, pd.sp_qbd[n]);}
	}
}

bool room_object_t::can_use() const { // excludes dynamic objects
	if (is_medicine()) return 1; // medicine can be carried in the inventory and used later
	if (type == TYPE_TPROLL) {return (taken_level == 0);} // can only use the TP roll, not the holder
	return (type == TYPE_SPRAYCAN || type == TYPE_MARKER || type == TYPE_BOOK || type == TYPE_PHONE || type == TYPE_TAPE || type == TYPE_RAT ||
		type == TYPE_FIRE_EXT || type == TYPE_CANDLE /*|| type == TYPE_FLASHLIGHT*/);
}
bool room_object_t::can_place_onto() const { // Note: excludes flat objects such as TYPE_RUG and TYPE_BLANKET
	return (type == TYPE_TABLE || type == TYPE_DESK || type == TYPE_DRESSER || type == TYPE_NIGHTSTAND || type == TYPE_COUNTER || type == TYPE_KSINK ||
		type == TYPE_BRSINK || type == TYPE_BED || type == TYPE_BOX || type == TYPE_CRATE || type == TYPE_KEYBOARD || type == TYPE_BOOK ||
		type == TYPE_FCABINET || type == TYPE_MWAVE || type == TYPE_POOL_TABLE); // TYPE_STAIR?
}

bool building_t::apply_toilet_paper(point const &pos, vector3d const &dir, float half_width) {
	// for now, just drop a square of TP on the floor; could do better; should the TP roll shrink in size as this is done?
	assert(has_room_geom());
	static point last_tp_pos;
	if (dist_xy_less_than(pos, last_tp_pos, 1.5*half_width)) return 0; // too close to prev pos
	last_tp_pos = pos;
	float zval(pos.z);
	if (!get_zval_for_obj_placement(pos, half_width, zval, 1)) return 0; // no suitable placement found; add_z_bias=1
	vector3d d1(dir.x, dir.y, 0.0);
	if (d1 == zero_vector) {d1 = plus_x;} else {d1.normalize();}
	vector3d d2(cross_product(d1, plus_z));
	if (d2 == zero_vector) {d2 = plus_y;} else {d2.normalize();}
	interior->room_geom->decal_manager.tp_qbd.add_quad_dirs(point(pos.x, pos.y, zval), d1*half_width, d2*half_width, WHITE, plus_z);
	interior->room_geom->modified_by_player = 1; // make sure TP stays in this building
	// Note: no damage done for TP
	return 1;
}

void building_t::register_player_death(point const &camera_bs) { // due to zombie kill
	add_blood_decal(camera_bs, get_scaled_player_radius());
	player_inventory.drop_all_inventory_items(camera_bs, *this);
}
void building_t::add_blood_decal(point const &pos, float radius, colorRGBA const &color) {
	bool const is_blood(color == WHITE); // else insect guts; here WHITE represents real red blood, while any other color is something custom
	if (disable_blood && is_blood) return; // disable_blood only applies to red blood, not bug guts
	assert(has_room_geom());
	float zval(pos.z);
	if (!get_zval_of_floor(pos, radius, zval)) return; // no suitable floor found
	// Note: blood is never cleared and will continue to accumulate in the current building
	interior->room_geom->decal_manager.add_blood_or_stain(point(pos.x, pos.y, zval), radius, color, is_blood);
	interior->room_geom->modified_by_player = 1; // make sure blood stays in this building
	player_inventory.record_damage_done(is_blood ? 100.0 : 1.0); // blood is a mess to clean up; bug guts less so (though damage will be reset on player death anyway)
}

void building_t::add_broken_glass_to_floor(point const &pos, float radius) {
	assert(has_room_geom());
	float zval(pos.z);
	if (!get_zval_of_floor(pos, radius, zval)) return; // no suitable floor found
	static rand_gen_t rgen;
	add_broken_glass_decal(point(pos.x, pos.y, zval), radius, rgen);
}
void building_t::add_broken_glass_decal(point const &pos, float radius, rand_gen_t &rgen) {
	assert(has_room_geom());
	float const angle(TWO_PI*rgen.rand_float()); // use a random rotation
	vector3d const v1(sin(angle), cos(angle), 0.0), v2(cross_product(v1, plus_z));
	interior->room_geom->decal_manager.glass_qbd.add_quad_dirs(pos, v1*radius, v2*radius, WHITE, plus_z); // Note: never cleared
}

// sound/audio tracking

class sound_tracker_t {
	point pos;
	float volume;
	int cur_frame;
public:
	sound_tracker_t() : volume(0.0), cur_frame(0) {}

	void register_sound(point const &pos_, float volume_) {
		if (cur_frame == frame_counter && volume_ < volume) return; // not the loudest sound this frame
		pos = pos_; volume = volume_; cur_frame = frame_counter;
	}
	sphere_t get_for_cur_frame() const {
		if (volume == 0.0 || cur_frame+1 < frame_counter) return sphere_t(); // no sound, or sound is more than one frame old
		return sphere_t(pos, volume); // encode pos and volume in a sphere
	}
};
sound_tracker_t sound_tracker; // used for rats
sphere_t get_cur_frame_loudest_sound() {return sound_tracker.get_for_cur_frame();}

void register_building_sound(point const &pos, float volume) { // Note: pos is in building space
	if (volume == 0.0) return;
	assert(volume > 0.0); // can't be negative
	bool const in_gameplay(in_building_gameplay_mode());

#pragma omp critical(building_sounds_update)
	{ // since this can be called by both the draw thread and the AI update thread, it should be in a critical section
	  // only used by building AI, so only needed in gameplay mode; cap at 100 sounds in case they're not being cleared
		if (in_gameplay && volume > ALERT_THRESH && cur_sounds.size() < 100) {
			float const max_merge_dist(0.5*CAMERA_RADIUS);
			bool merged(0);

			for (auto i = cur_sounds.begin(); i != cur_sounds.end(); ++i) { // attempt to merge with an existing nearby sound
				if (dist_less_than(pos, i->pos, max_merge_dist)) {i->radius += volume; merged = 1;}
			}
			if (!merged) {cur_sounds.emplace_back(pos, volume);} // Note: volume is stored in radius field of sphere_t
		}
		if (in_gameplay) {cur_building_sound_level += volume;} // only needed in gameplay and pickup modes
		sound_tracker.register_sound(pos, volume); // always done; assumes player is in/near a building; needed for rats
	}
}
void register_building_sound_at_player(float volume) {
	register_building_sound(get_camera_building_space(), 1.0);
}

float get_closest_building_sound(point const &at_pos, point &sound_pos, float floor_spacing) {
	if (cur_sounds.empty()) return 0;
	float max_vol(0.0); // 1.0 at a sound=1.0 volume at a distance of floor_spacing

	for (auto i = cur_sounds.begin(); i != cur_sounds.end(); ++i) {
		float vol(i->radius/max(0.01f*floor_spacing, p2p_dist(i->pos, at_pos)));
		if (fabs(i->pos.z - at_pos.z) > 0.75f*floor_spacing) {vol *= 0.5;} // half the volume when the sound comes from another floor
		if (vol > max_vol) {max_vol = vol; sound_pos = i->pos;}
	} // for i
	//cout << TXT(cur_sounds.size()) << TXT(max_vol) << endl;
	float const rel_vol(max_vol*floor_spacing);
	return ((rel_vol > 0.06f) ? rel_vol : 0.0);
}

void maybe_play_zombie_sound(point const &sound_pos_bs, unsigned zombie_ix, bool alert_other_zombies, bool high_priority, float gain, float pitch) {
	unsigned const NUM_ZSOUNDS = 5;
	static rand_gen_t rgen;
	static double next_time_all(0.0), next_times[NUM_ZSOUNDS] = {};
	if (!high_priority && tfticks < next_time_all) return; // don't play any sound too frequently
	if (!high_priority && (rgen.rand()&3) != 0)    return; // only generate a sound 25% of the time (each frame), to allow more than one zombie to get a chance
	unsigned const sound_id(zombie_ix%NUM_ZSOUNDS); // choose one of the zombie sounds, determined by the current zombie
	double &next_time(next_times[sound_id]);
	if (!high_priority && tfticks < next_time) return; // don't play this particular sound too frequently
	next_time_all = tfticks + double(rgen.rand_uniform(1.0, 2.0))*TICKS_PER_SECOND; // next sound of any  type can play between 0.8 and 2.0s in the future
	next_time     = tfticks + double(rgen.rand_uniform(2.5, 5.0))*TICKS_PER_SECOND; // next sound of this type can play between 2.5 and 5.0s in the future
	gen_sound_thread_safe((SOUND_ZOMBIE1 + sound_id), (sound_pos_bs + get_camera_coord_space_xlate()), gain, pitch);
	if (alert_other_zombies) {register_building_sound(sound_pos_bs, 0.4);}
}

void water_sound_manager_t::register_running_water(room_object_t const &obj, building_t const &building) {
	if (!obj.is_active()) return; // not turned on
	if (fabs(obj.z2() - camera_bs.z) > building.get_window_vspace()) return; // on the wrong floor
	point const pos(obj.get_cube_center());
	float const dsq(p2p_dist_sq(pos, camera_bs));
	if (dmin_sq == 0.0 || dsq < dmin_sq) {closest = building.local_to_camera_space(pos); dmin_sq = dsq;}
}
void water_sound_manager_t::finalize() {
	if (dmin_sq == 0.0) return; // no water found
	static point prev_closest;
	bool const skip_if_already_playing(closest == prev_closest); // don't reset sound loop unless it moves to a different sink
	prev_closest = closest;
	gen_sound_thread_safe(SOUND_SINK, closest, 1.0, 1.0, 0.06, skip_if_already_playing); // fast distance falloff; will loop at the end if needed
}

class creepy_sound_manager_t {
	int sound_ix=-1;
	float time_to_next_sound=0.0; // in ticks
	point sound_pos;
	rand_gen_t rgen;
	static unsigned const NUM_SOUNDS = 8;
	int sounds[NUM_SOUNDS] = {SOUND_OBJ_FALL, SOUND_WOOD_CRACK, SOUND_SNOW_STEP, -1, -1, -1, -1, -1};
	string const sound_filenames[NUM_SOUNDS] = {"", "", "", "knock_rattle.wav", "knock_wood.wav", "knock_door.wav", "creak1.wav", "creak2.wav"};

	void schedule_next_sound() {time_to_next_sound = rgen.rand_uniform(15.0, 60.0)*TICKS_PER_SECOND;} // every 15-60s
public:
	void reset() {
		sound_ix  = -1;
		sound_pos = zero_vector; // in local building space
		schedule_next_sound();
	}
	void next_frame(building_t const &building, point const &player_pos) { // player_pos is in building space
		if (NUM_SOUNDS == 0)          return; // no sounds enabled
		if (player_in_water >= 2)     return; // skip if player underwater
		time_to_next_sound -= fticks;
		if (time_to_next_sound > 0.0) return; // not yet
		bool const gen_new_pos(sound_pos == all_zeros || rgen.rand_bool());
		bool const gen_new_sound(sound_ix < 0 || (gen_new_pos && NUM_SOUNDS > 1));
		if (gen_new_pos  ) {sound_pos = building.choose_creepy_sound_pos(player_pos, rgen);}
		if (gen_new_sound) {sound_ix  = rgen.rand()%NUM_SOUNDS;} // select a random new sound
		assert(sound_ix >= 0 && (unsigned)sound_ix < NUM_SOUNDS);
		int &sid(sounds[sound_ix]);
		
		if (sid < 0) { // sound not yet loaded
			assert(!sound_filenames[sound_ix].empty());
			sid = get_sound_id_for_file(sound_filenames[sound_ix]);
			if (sid < 0) {sid = SOUND_OBJ_FALL;} // if load failed, use a default sound
		}
		float const gain(rgen.rand_uniform(0.1, 1.0)), pitch(rgen.rand_uniform(0.9, 1.1));
		gen_sound_thread_safe(sid, building.local_to_camera_space(sound_pos), gain, pitch); // doesn't alert zombies
		schedule_next_sound();
	}
};

creepy_sound_manager_t creepy_sound_manager;

void reset_creepy_sounds() {creepy_sound_manager.reset();}

point building_t::choose_creepy_sound_pos(point const &player_pos, rand_gen_t &rgen) const {
	float const max_dist(4.0*get_window_vspace());
	int const room_ix(get_room_containing_pt(player_pos));
	if (room_ix < 0) {return (player_pos + rgen.signed_rand_vector_spherical_xy(max_dist));} // player room invalid, use a random sound pos
	// player room is valid; choose location in a nearby building basement room
	point sound_pos;
	cube_t room_exp(get_room(room_ix));
	room_exp.expand_by(2.0*get_wall_thickness()); // expand to include walls and overlap adj rooms

	for (unsigned n = 0; n < 100; ++n) { // 100 attempts to find a valid pos
		sound_pos = player_pos + rgen.signed_rand_vector_spherical_xy(max_dist);
		if (room_exp.contains_pt_xy(sound_pos))  continue; // same room as player
		int const room_ix2(get_room_containing_pt(sound_pos));
		if (room_ix2 < 0 || room_ix2 == room_ix) continue; // invalid room
		cube_t const &room2(get_room(room_ix2)); // okay if regular basement room
		if (!room2.intersects(room_exp)) break; // non-adjacent room, not visible/connected, pos is valid
		bool is_visible(0);
			
		// adjacent room, check line of sight vs. doors
		for (door_t const &door : interior->doors) {
			if (door.open_amt == 0.0)        continue; // fully closed, skip
			if (door.z2() > ground_floor_z1) continue; // not a basement door
			if (check_line_clip(player_pos, sound_pos, door.get_true_bcube().d)) {is_visible = 1; break;}
		}
		if (!is_visible) break; // valid pos
	} // for n
	return sound_pos;
}
void building_t::update_creepy_sounds(point const &player_pos) const {
	if (player_in_basement == 3) {creepy_sound_manager.next_frame(*this, player_pos);} // update if player in extended basement
}

void play_hum_sound(point const &pos, float gain, float pitch) { // pos is in building space; nominal hum is 100Hz
	gen_sound_thread_safe(SOUND_NEON_SIGN, (pos + get_camera_coord_space_xlate()), gain, pitch, 1.0, 1); // skip_if_already_playing=1
}

// gameplay logic

unsigned player_has_room_key() {return player_inventory.player_has_key     ();}
bool     player_has_pool_cue() {return player_inventory.player_has_pool_cue();}

bool flashlight_enabled() { // flashlight can't be used in tiled terrain building gameplay mode if the player didn't pick up a flashlight
	return (world_mode != WMODE_INF_TERRAIN || !in_building_gameplay_mode() || player_inventory.player_has_flashlight());
}

// returns player_dead
// should we include falling damage? currently the player can't fall down elevator shafts or stairwells,
// and falling off building roofs doesn't count because gameplay isn't enabled because the player isn't in the building
bool player_take_damage(float damage_scale, int poison_type, uint8_t *has_key) {
	if (player_wait_respawn) return 0;
	static double last_scream_time(0.0), last_hurt_time(0.0);

	if (damage_scale < 0.01) { // hurt for rats, scream for zombies and spiders
		if (tfticks - last_hurt_time > 0.5*TICKS_PER_SECOND) {
			gen_sound_thread_safe_at_player(SOUND_HURT2);
			last_hurt_time = tfticks;
		}
	}
	else {
		if (tfticks - last_scream_time > 2.0*TICKS_PER_SECOND) {
			gen_sound_thread_safe_at_player(SOUND_SCREAM1);
			last_scream_time = tfticks;
		}
	}
	add_camera_filter(colorRGBA(RED, (0.13 + 3.0*damage_scale)), 1, -1, CAM_FILT_DAMAGE); // 1 tick of red damage
	player_inventory.take_damage(damage_scale*fticks, poison_type); // take damage over time

	if (player_inventory.player_is_dead()) {
		if (has_key) {*has_key |= player_has_room_key();}
		return 1;
	}
	return 0;
}
// return value: 0=no effect, 1=player is killed, 2=this person is killed
int register_ai_player_coll(uint8_t &has_key, float height) {
	if (camera_in_building && do_room_obj_pickup && player_inventory.take_person(has_key, height)) {
		gen_sound_thread_safe_at_player(SOUND_ITEM, 0.5);
		do_room_obj_pickup = 0; // no more object pickups
		return 2;
	}
	return player_take_damage(0.04, 0, &has_key);
}

void building_gameplay_action_key(int mode, bool mouse_wheel) {
	if (camera_in_building) { // building interior action
		if (player_wait_respawn) {player_inventory.player_respawn();} // forced respawn
		else if (mouse_wheel) {player_inventory.switch_item(mode != 0);}
		else if (mode == 0) {building_action_key    = 1;} // 'q'
		else if (mode == 1) {do_room_obj_pickup     = 1;} // 'e'
		else if (mode == 2) {use_last_pickup_object = 1;} // 'E'
		else if (mode == 3) {building_action_key    = 2;} // 'r'
		else {assert(0);} // unsupported key/mode
		show_bldg_pickup_crosshair = 1; // show crosshair on first pickup, too difficult to pick up objects without it
	}
	else { // building exterior/city/road/car action
		if (mode == 1 || mode == 2) {city_action_key = 1;} // 'e'/'E'
		else {} // 'q'/'r'
	}
}

void attenuate_rate(float &v, float rate) {
	if (v != 0.0) {
		v *= exp(-rate*fticks); // exponential slowdown
		if (fabs(v) < 0.001) {v = 0.0;} // stop moving
	}
}

void building_gameplay_next_frame() {
	attenuate_rate(office_chair_rot_rate, 0.05); // update office chair rotation
	vignette_color = ALPHA0; // reset, may be set below

	if (player_wait_respawn) {
		vignette_color = RED;
	}
	else if (in_building_gameplay_mode()) { // run gameplay update logic
		show_bldg_pickup_crosshair = 1;
		// update sounds used by AI
		auto i(cur_sounds.begin()), o(i);

		for (; i != cur_sounds.end(); ++i) {
			i->radius *= exp(-0.04*fticks);
			if (i->radius > ALERT_THRESH) {*(o++) = *i;} // keep if above thresh
		}
		cur_sounds.erase(o, cur_sounds.end());
		cur_building_sound_level = min(1.2f, max(0.0f, (cur_building_sound_level - 0.01f*fticks))); // gradual decrease
		vignette_color = player_inventory.get_vignette_color();
	}
	player_held_object = carried_item_t();
	player_inventory.next_frame();
	// reset state for next frame
	can_pickup_bldg_obj = 0;
	do_room_obj_pickup  = city_action_key = can_do_building_action = player_in_unlit_room = 0;
}

void enter_building_gameplay_mode() {player_inventory.clear_all();}

