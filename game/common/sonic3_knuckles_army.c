/* Knuckles & Knuckles: additive native Knuckles actors for S&K code
 * (standalone S&K and the S&K half of Sonic 3 & Knuckles share every address
 * below; see sonic3k.lst). Each extra is a real $4A-byte object in the top
 * of Dynamic_object_RAM whose code pointer is a plain rts (locret_13FC0), so
 * Process_Sprites leaves it alone. The host steps every extra through the
 * stock Obj_Knuckles ($16444) at Obj_ResetCollisionResponseList ($6C2C),
 * i.e. after Player_1/Player_2 and before the collision list is cleared, the
 * slot Player_2 would occupy. Player-1-only globals are saved around each
 * extra; physics has per-extra copies. Control comes from the extra's seat
 * (a connected controller) or from a port of Tails_CPU_Control's follow,
 * catch-up and spindash states ($139CC) with per-extra history delays. */
#include "sonic3_knuckles_army.h"
#include "sonic3_knuckles_art.h"
#include "sonic3_video.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include "sim_step.h"
#include "video/genesis_machine.h"
#include "sonic3_knuckles_title.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cmd_send_response(const char *json);

const unsigned s3_army_sizes[4] = { 8, 16, 24, 34 };
S3ArmyConfig s3_army;

void s3_army_defaults(void) { s3_army.enabled = 0; s3_army.size = 16; }
int s3_army_valid_size(unsigned size)
{
    for (unsigned i = 0; i < 4; ++i) if (s3_army_sizes[i] == size) return 1;
    return 0;
}

/* ---- Stock S&K RAM ($FFxxxx) -------------------------------------------- */
enum {
    PLAYER_1 = 0xB000, OBJ = 0x4A, OBJ_FIRST = 0xB000, OBJ_SLOTS = 110,
    DYN_FIRST = 0xB0DE, DYN_END = 0xCAE2,
    BUBBLES = 0xCB2C, DUST = 0xCC54,
    POS_TABLE = 0xE500, STAT_TABLE = 0xE400, POS_INDEX = 0xEE26,
    CAMERA_X = 0xEE78, CAMERA_Y = 0xEE7C, CAMERA_X_COPY = 0xEE80, CAMERA_Y_COPY = 0xEE84,
    SCREEN_Y_WRAP = 0xEEAA, SCROLL_LOCK_P2 = 0xEE0B, HSCROLL_P2 = 0xEE28,
    CAMERA_MIN_X = 0xEE14, CAMERA_MAX_X = 0xEE16,
    DISTANCE_TOP = 0xEE2C, GLIDE_SHAKE = 0xEF66,
    GAME_MODE = 0xF600, CTRL_1_LOGICAL = 0xF602, CTRL_1 = 0xF604, PAUSED = 0xF63A,
    WATER_ENTERED = 0xF64D, LEVEL_STARTED = 0xF711, GLIDE_FLAGS = 0xF74E,
    PHYSICS = 0xF760, PREV_FRAME = 0xF766, ANGLES = 0xF768, COLLISION_ADDR = 0xF796,
    REVERSE_GRAVITY = 0xF7C6, CTRL_1_LOCKED = 0xF7CA, CHAIN = 0xF7D0,
    LEVEL_FRAME = 0xFE04, DEBUG_PLACEMENT = 0xFE08, ZONE = 0xFE10,
    SUPER_FLAG = 0xFE19, HUD_TIMER = 0xFE1E, PLAYER_MODE = 0xFF08,
    DEMO = 0xFFD0, DEBUG_ART = 0xFFCE, COMPETITION = 0xFFE8,
};
/* SST fields (sonic3k.constants.asm). */
enum {
    F_RENDER = 4, F_ROUTINE = 5, F_WIDTH = 7, F_PRIORITY = 8, F_ART = 0xA, F_X = 0x10, F_Y = 0x14,
    F_XVEL = 0x18, F_YVEL = 0x1A, F_GVEL = 0x1C, F_ANIM = 0x20, F_FRAME = 0x22,
    F_DJ_PROP = 0x25, F_STATUS = 0x2A, F_STATUS2 = 0x2B, F_AIR = 0x2C, F_FLIP_TYPE = 0x2D,
    F_CONTROL = 0x2E, F_DJ_FLAG = 0x2F, F_FLIPS = 0x30, F_FLIP_SPEED = 0x31,
    F_MOVE_LOCK = 0x32, F_INVULN = 0x34, F_INVINC = 0x35, F_SHOES = 0x36, F_STATUS3 = 0x37,
    F_SCROLL_DELAY = 0x39, F_NEXT_TILT = 0x3A, F_CONVEX = 0x3C, F_SPINDASH = 0x3D,
    F_SPINDASH_COUNT = 0x3E, F_JUMPING = 0x40, F_41 = 0x41, F_INTERACT = 0x42,
    F_TOP_SOLID = 0x46, F_LRB_SOLID = 0x47,
};
enum { ST_FACING = 1, ST_AIR = 2, ST_ONOBJ = 8, ST_PUSH = 0x20, ST_UNDERWATER = 0x40 };
enum { BTN_UP = 1, BTN_DOWN = 2, BTN_LEFT = 4, BTN_RIGHT = 8, BTN_B = 0x10, BTN_C = 0x20,
       BTN_A = 0x40, BTN_START = 0x80, BTN_ABC = 0x70 };

typedef struct {
    uint32_t callee, ret, reaction;
    uint8_t kind;
} ArmySite;
enum { REACT_NONE, REACT_STANDING, REACT_BELOW, REACT_SIDE };
/* Code addresses, solid routines and spring reactions, all derived from the
 * pinned listing by tools/gen_knuckles_army_sites.py. */
#include "sonic3_knuckles_army_sites.inc"
enum {
    PC_STEP = ARMY_PC_STEP, PC_KNUCKLES = ARMY_PC_KNUCKLES, PC_DPLC = ARMY_PC_DPLC,
    PC_DEATH = ARMY_PC_DEATH, PC_DRAW = ARMY_PC_DRAW, PC_RENDER = ARMY_PC_RENDER,
    PC_ALLOC = ARMY_PC_ALLOC, PC_ALLOC_AFTER = ARMY_PC_ALLOC_AFTER,
    PC_TRANSITION = ARMY_PC_TRANSITION, CODE_IDLE = ARMY_CODE_IDLE,
};

enum { MODE_BENCHED, MODE_FOLLOW, MODE_SPINDASH, MODE_PARKED, MODE_CATCHUP };
typedef struct Actor {
    uint16_t slot, flight, interact_code, input, settle, inputs_seen; /* seen: debug, reset per query */
    uint8_t mode, delay, phase, auto_jump, held, displayed, visible, driven;
    int8_t gap;
    uint8_t physics[6];
    uint8_t solid[OBJ_SLOTS];  /* this extra's p2 standing/pushing bits ($50) */
} Actor;
static Actor actors[S3_ARMY_MAX];
static unsigned count;         /* extras configured for the running level */
static uint32_t slot_codes[OBJ_SLOTS];
static int inside_actor = -1, inside_solid;
/* Seamless act transitions shift objects but not Pos_table: stale history
 * entries are corrected by the recorded offset until they age out. */
static int16_t shift_x, shift_y;
static unsigned shift_age = 64;

/* World positions; the VDP pass and the custom-width renderer each project
 * them through the camera that produced the frame they draw. */
typedef struct { int16_t x, y; uint16_t art; uint8_t frame, flip; } Shown;
typedef struct { Shown actor[S3_ARMY_MAX]; unsigned count; int16_t camera_x, camera_y; uint16_t wrap; } Crowd;
static Crowd published, shown;
static KnuxArt art;
static int art_ready;

static uint16_t rd16(unsigned a) { a &= 0xFFFF; return (uint16_t)(g_ram[a] << 8 | g_ram[(a + 1) & 0xFFFF]); }
static uint32_t rd32(unsigned a) { return (uint32_t)rd16(a) << 16 | rd16(a + 2); }
static void wr16(unsigned a, unsigned v) { a &= 0xFFFF; g_ram[a] = (uint8_t)(v >> 8); g_ram[(a + 1) & 0xFFFF] = (uint8_t)v; }
static void wr32(unsigned a, uint32_t v) { wr16(a, v >> 16); wr16(a + 2, v); }
static int16_t s16(unsigned a) { return (int16_t)rd16(a); }

unsigned s3_army_seat(unsigned index) { return index + 1; }
static int human(unsigned k, uint8_t *pad)
{
    unsigned seat = s3_army_seat(k);
    if (seat >= GENESIS_SIM_MAX_PLAYERS || !(genesis_sim_human_mask() & (1u << seat))) return 0;
    *pad = (uint8_t)genesis_sim_pad((int)seat);
    return 1;
}

static int zone_supported(void)
{
    unsigned zone = g_ram[ZONE];
    /* Story zones only. DDZ ($0C) is a free-flight Super fight; bonus and
     * competition stages ($0E-$15) have their own player logic. */
    return zone < 0x0C || zone == 0x0D || zone == 0x16 || zone == 0x17;
}
static int armed(void)
{
    return s3_army.enabled && art_ready && (g_ram[GAME_MODE] & 0x7F) == 0x0C &&
        rd16(PLAYER_MODE) == 3 && !rd16(COMPETITION) && !rd16(DEMO) && zone_supported();
}
int s3_army_active(void) { return count && armed(); }

static int actor_of(unsigned address)
{
    address &= 0xFFFF;
    if (address < DYN_FIRST || address >= DYN_END) return -1;
    for (unsigned k = 0; k < count; ++k) if (actors[k].slot == address) return (int)k;
    return -1;
}

/* A balanced synthetic return slot: nested generated calls keep their strict
 * JSR-stack checks and no generated C is edited. */
static M68KState native(uint32_t pc)
{
    M68KState before = g_cpu;
    recomp_push_return(CODE_IDLE);
    recomp_call_addr(pc);
    M68KState after = g_cpu;
    g_cpu = before;
    return after;
}

/* ---- Pos_table history (native 16-frame delay is per-extra here) -------- */
static unsigned history_index(unsigned delay)
{
    return (uint8_t)(rd16(POS_INDEX) - (delay * 4 + 4));
}
static void history_at(unsigned delay, int *x, int *y, unsigned *input, unsigned *status)
{
    unsigned at = history_index(delay);
    *x = s16(POS_TABLE + at); *y = s16(POS_TABLE + at + 2);
    if (delay + 1 > shift_age) { *x -= shift_x; *y -= shift_y; }
    if (input) *input = rd16(STAT_TABLE + at);
    if (status) *status = g_ram[STAT_TABLE + at + 2];
}

/* ---- Lifecycle ------------------------------------------------------------ */
static void configure_actor(unsigned k)
{
    Actor *a = &actors[k];
    /* Distinct history delays (8..60 frames), jump-retry phases and trailing
     * offsets keep the crowd from stacking. Deterministic: no game RNG. */
    a->delay = (uint8_t)(count > 1 ? 8 + k * 52 / (count - 1) : 16);
    a->phase = (uint8_t)((k * 23 + 5) & 63);
    int spread = 10 + (int)(k / 2) * 5;
    if (spread > 92) spread = 92 - (int)(k % 9) * 7;
    a->gap = (int8_t)((k & 1) ? spread : -spread);
}
static void park(Actor *a)
{
    unsigned o = a->slot;
    a->mode = MODE_PARKED; a->flight = 0; a->displayed = 0;
    g_ram[o + F_CONTROL] = 0x81; g_ram[o + F_STATUS] = ST_AIR;
    wr16(o + F_X, 0x7F00); wr16(o + F_Y, 0);
    g_ram[o + F_DJ_FLAG] = 0; g_ram[o + F_ROUTINE] = 2;
}
static int free_slot(void)
{
    for (unsigned o = DYN_END - OBJ; o > DYN_FIRST + 2 * OBJ; o -= OBJ)
        if (!rd32(o)) return (int)o;
    return 0;
}
static unsigned free_slots(void)
{
    unsigned n = 0;
    for (unsigned o = DYN_FIRST; o < DYN_END; o += OBJ) n += !rd32(o);
    return n;
}
static void default_physics(uint8_t p[6])
{
    /* Knuckles_Init: Max_speed $600, Acceleration $C, Deceleration $80. */
    const uint8_t d[6] = { 6, 0, 0, 0x0C, 0, 0x80 };
    memcpy(p, d, 6);
}
/* Level starts and intros can move Player 1 after his first frame (MHZ's
 * Knuckles intro starts him elsewhere). Fresh extras stay beside him until he
 * has control; afterwards only the Tails-style catch-up brings them back. */
enum { SETTLE_FRAMES = 120 };
static void place_beside_leader(Actor *a)
{
    unsigned o = a->slot;
    wr16(o + F_X, (unsigned)(s16(PLAYER_1 + F_X) + a->gap));
    wr16(o + F_Y, rd16(PLAYER_1 + F_Y));
    wr16(o + F_XVEL, 0); wr16(o + F_YVEL, 0); wr16(o + F_GVEL, 0);
    g_ram[o + F_STATUS] = (uint8_t)((g_ram[PLAYER_1 + F_STATUS] & ~(ST_ONOBJ | ST_PUSH)) | ST_AIR);
    wr16(o + F_INTERACT, 0);
}
static void settle(Actor *a)
{
    if (!a->settle) return;
    unsigned o = a->slot, p = PLAYER_1;
    /* A scripted leader can stand outside the level's bounds (MHZ's nap at
     * x=128). Player_LevelBound would clamp every re-placed extra back onto
     * one spot, so wait off-screen and glide in once he is playable. */
    int px = s16(p + F_X);
    if (px < s16(CAMERA_MIN_X) + 16 || px > s16(CAMERA_MAX_X) + 320 - 24) {
        a->settle = 0;
        park(a);
        return;
    }
    if (g_ram[p + F_CONTROL] & 0x80) a->settle = SETTLE_FRAMES; else --a->settle;
    int dx = s16(o + F_X) - s16(p + F_X), dy = s16(o + F_Y) - s16(p + F_Y);
    if (abs(dx) > 96 || abs(dy) > 96) place_beside_leader(a);
}
static void spawn(unsigned k, unsigned o, int fresh)
{
    Actor *a = &actors[k];
    a->slot = (uint16_t)o;
    memcpy(g_ram + o, g_ram + PLAYER_1, OBJ);
    wr32(o, CODE_IDLE);
    memset(a->solid, 0, sizeof a->solid);
    g_ram[o + F_ROUTINE] = 2;
    g_ram[o + F_STATUS2] = 0; g_ram[o + F_STATUS3] = 0;
    g_ram[o + F_INVULN] = g_ram[o + F_INVINC] = g_ram[o + F_SHOES] = 0;
    wr16(o + F_INTERACT, 0);
    a->input = 0; a->held = 0; a->auto_jump = 0; a->flight = 0;
    if (g_ram[SUPER_FLAG]) default_physics(a->physics);
    else memcpy(a->physics, g_ram + PHYSICS, 6);
    if (fresh) {
        place_beside_leader(a);
        a->mode = MODE_FOLLOW;
        a->settle = SETTLE_FRAMES;
    } else park(a);
}
static void release(Actor *a)
{
    if (a->slot && rd32(a->slot) == CODE_IDLE) memset(g_ram + a->slot, 0, OBJ);
    a->slot = 0; a->mode = MODE_BENCHED; a->displayed = 0;
}
static void release_all(void)
{
    for (unsigned k = 0; k < count; ++k) release(&actors[k]);
    count = 0; published.count = 0;
}

/* The native status byte has standing/pushing bits for two players only.
 * Each extra keeps its own copy of the unused Player 2 bits for every slot. */
static void refresh_slot_codes(void)
{
    for (unsigned n = 0; n < OBJ_SLOTS; ++n) {
        uint32_t code = rd32(OBJ_FIRST + n * OBJ);
        if (code == slot_codes[n]) continue;
        slot_codes[n] = code;
        for (unsigned k = 0; k < count; ++k) actors[k].solid[n] = 0;
    }
}
/* A single-character solid routine touches the bits of the solid object (a0)
 * and, through RideObject_SetRide, of the object the character last rode. */
typedef struct { unsigned n[2], count; uint8_t saved[2]; } SolidSwap;
static int object_index(unsigned address)
{
    address &= 0xFFFF;
    if (address < DYN_FIRST || address >= OBJ_FIRST + OBJ_SLOTS * OBJ) return -1;
    if ((address - OBJ_FIRST) % OBJ) return -1;
    return actor_of(address) >= 0 ? -1 : (int)((address - OBJ_FIRST) / OBJ);
}
static void solid_enter(Actor *a, SolidSwap *s, unsigned object, unsigned ridden)
{
    s->count = 0;
    int n[2] = { object_index(object), (g_ram[a->slot + F_STATUS] & ST_ONOBJ) ? object_index(ridden) : -1 };
    for (unsigned i = 0; i < 2; ++i) {
        if (n[i] < 0 || (i && n[1] == n[0])) continue;
        unsigned at = OBJ_FIRST + (unsigned)n[i] * OBJ + F_STATUS;
        s->n[s->count] = (unsigned)n[i]; s->saved[s->count++] = g_ram[at] & 0x50;
        g_ram[at] = (uint8_t)((g_ram[at] & ~0x50) | a->solid[n[i]]);
    }
}
static void solid_leave(Actor *a, const SolidSwap *s)
{
    for (unsigned i = 0; i < s->count; ++i) {
        unsigned at = OBJ_FIRST + s->n[i] * OBJ + F_STATUS;
        a->solid[s->n[i]] = g_ram[at] & 0x50;
        g_ram[at] = (uint8_t)((g_ram[at] & ~0x50) | s->saved[i]);
    }
}

/* ---- Control ------------------------------------------------------------ */
static int on_screen(unsigned o)
{
    int margin = g_ws_margin > 0 ? g_ws_margin : 0;
    int x = s16(o + F_X) - s16(CAMERA_X), y = s16(o + F_Y) - s16(CAMERA_Y);
    int w = g_ram[o + F_WIDTH];
    return x >= -w - margin && x < 320 + w + margin && y >= -32 && y < 256;
}
/* sub_13EFC: offscreen for five seconds (not riding the same object) despawns. */
static int offscreen_timeout(Actor *a)
{
    unsigned o = a->slot;
    int riding = g_ram[o + F_STATUS] & ST_ONOBJ;
    if (!a->visible) {
        if (!riding || rd16(rd16(o + F_INTERACT)) != a->interact_code) {
            if (++a->flight >= 5 * 60) { park(a); return 1; }
        }
    } else a->flight = 0;
    if (riding) a->interact_code = rd16(rd16(o + F_INTERACT));
    return 0;
}
static uint16_t follow_input(unsigned k)
{
    Actor *a = &actors[k];
    unsigned o = a->slot, p = PLAYER_1;
    if (g_ram[p + F_ROUTINE] >= 6) return a->input;
    if (offscreen_timeout(a)) return 0;
    if ((g_ram[o + F_CONTROL] & 0x80) || (g_ram[p + F_STATUS3] & 0x80)) return a->input;
    int tx, ty; unsigned input, status;
    history_at(a->delay, &tx, &ty, &input, &status);
    /* Tails spindashes whenever move_lock holds him still. Knuckles also gets
     * move_lock from every glide landing, so require him to be grounded and
     * genuinely left behind before revving. */
    if (rd16(o + F_MOVE_LOCK) && !rd16(o + F_GVEL) && !(g_ram[o + F_STATUS] & ST_AIR) &&
        abs(tx + a->gap - s16(o + F_X)) >= 0x60) {
        a->mode = MODE_SPINDASH;
        return a->input;
    }
    if (!(g_ram[p + F_STATUS] & ST_ONOBJ) && s16(p + F_GVEL) < 0x400) tx -= 0x20;
    tx += a->gap;
    unsigned frame = rd16(LEVEL_FRAME) + a->phase;
    int jump = 0;
    if ((g_ram[o + F_STATUS] & ST_PUSH) && !(status & 0x20)) jump = 1;
    int dx = tx - s16(o + F_X);
    if (!jump) {
        if (dx < 0) {
            dx = -dx;
            if (dx >= 0x30) input = (input & 0xF3F3) | 0x0404;
            if (rd16(o + F_GVEL) && (g_ram[o + F_STATUS] & ST_FACING) && !(g_ram[o + F_CONTROL] & 1))
                wr16(o + F_X, rd16(o + F_X) - 1);
        } else if (dx > 0) {
            if (dx >= 0x30) input = (input & 0xF3F3) | 0x0808;
            if (rd16(o + F_GVEL) && !(g_ram[o + F_STATUS] & ST_FACING) && !(g_ram[o + F_CONTROL] & 1))
                wr16(o + F_X, rd16(o + F_X) + 1);
        } else {
            g_ram[o + F_STATUS] = (uint8_t)((g_ram[o + F_STATUS] & ~ST_FACING) | (status & 1));
        }
        /* Native following stops anywhere inside its 48px deadband, so a
         * crowd piles up at the near edge. With the leader's input idle,
         * grounded extras walk on to their own trailing spot. */
        int err = tx - s16(o + F_X);
        if (abs(err) > 6 && abs(err) < 0x30 && !(input & 0x0C00) && !(g_ram[o + F_STATUS] & ST_AIR))
            input |= err < 0 ? 0x0400 : 0x0800;
        if (a->auto_jump) {
            input |= BTN_ABC << 8;
            if (g_ram[o + F_STATUS] & ST_AIR) return (uint16_t)input;
            a->auto_jump = 0;
        }
        if ((frame & 0xFF) && dx >= 0x40) return (uint16_t)input;
        int dy = ty - s16(o + F_Y);
        if (dy >= 0 || -dy < 0x20) return (uint16_t)input;
    }
    if ((frame & 0x3F) || g_ram[o + F_ANIM] == 8) return (uint16_t)input;
    a->auto_jump = 1;
    return (uint16_t)(input | BTN_ABC << 8 | BTN_ABC);
}
/* loc_13F40: stuck behind move_lock -> crouch, rev and release a spindash. */
static uint16_t spindash_input(unsigned k)
{
    Actor *a = &actors[k];
    unsigned o = a->slot;
    if (offscreen_timeout(a)) return 0;
    if (rd16(o + F_MOVE_LOCK)) return a->input;
    unsigned frame = (unsigned)(g_ram[LEVEL_FRAME + 1] + a->phase) & 0xFF;
    const uint16_t down = BTN_DOWN << 8 | BTN_DOWN;
    if (!g_ram[o + F_SPINDASH]) {
        if (rd16(o + F_GVEL)) return a->input;
        g_ram[o + F_STATUS] &= ~ST_FACING;
        if (s16(o + F_X) >= s16(PLAYER_1 + F_X)) g_ram[o + F_STATUS] |= ST_FACING;
        if (!(frame & 0x7F)) { a->mode = MODE_FOLLOW; return 0; }
        if (g_ram[o + F_ANIM] == 8) return (uint16_t)(down | (BTN_ABC << 8 | BTN_ABC));
        return down;
    }
    if (!(frame & 0x7F)) { a->mode = MODE_FOLLOW; return 0; }
    if (!(frame & 0x1F)) return (uint16_t)(down | (BTN_ABC << 8 | BTN_ABC));
    return down;
}
static void glide_pose(unsigned o)
{
    g_ram[o + F_ANIM] = 0x20; g_ram[o + F_FRAME] = 0xC0;
    g_ram[o + F_RENDER] = (uint8_t)((g_ram[o + F_RENDER] & ~1) | (g_ram[o + F_STATUS] & ST_FACING));
}
/* Tails_Catch_Up_Flying ($13B26), with Knuckles gliding in instead. */
static void enter_catchup(Actor *a)
{
    unsigned o = a->slot, p = PLAYER_1;
    int y = s16(p + F_Y) + (g_ram[REVERSE_GRAVITY] ? 0xC0 : -0xC0);
    wr16(o + F_X, (unsigned)(s16(p + F_X) + a->gap)); wr16(o + F_Y, (unsigned)y);
    wr16(o + F_ART, rd16(o + F_ART) | 0x8000); wr16(o + F_PRIORITY, 0x100);
    wr16(o + F_XVEL, 0); wr16(o + F_YVEL, 0); wr16(o + F_GVEL, 0);
    static const uint8_t cleared[] = { F_FLIP_TYPE, F_DJ_FLAG, F_FLIPS, F_FLIP_SPEED, F_INVULN, F_INVINC,
        F_SHOES, F_STATUS3, F_SCROLL_DELAY, F_NEXT_TILT, F_NEXT_TILT + 1, F_CONVEX, F_SPINDASH,
        F_JUMPING, F_41 };
    for (unsigned i = 0; i < sizeof cleared; ++i) g_ram[o + cleared[i]] = 0;
    wr16(o + F_MOVE_LOCK, 0); wr16(o + F_SPINDASH_COUNT, 0);
    g_ram[o + F_STATUS] = ST_AIR; g_ram[o + F_STATUS2] = 0; g_ram[o + F_AIR] = 30;
    g_ram[o + F_CONTROL] = 0x83; g_ram[o + F_ROUTINE] = 2;
    a->mode = MODE_CATCHUP; a->flight = 0;
    glide_pose(o);
}
/* Tails_FlySwim_Unknown ($13BF8): home on the delayed leader position. */
static void catchup_move(Actor *a)
{
    unsigned o = a->slot, p = PLAYER_1;
    if (!a->visible) {
        if (++a->flight >= 5 * 60) { park(a); return; }
    } else a->flight = 0;
    int tx, ty; unsigned status;
    history_at(a->delay, &tx, &ty, NULL, &status);
    tx += a->gap;
    int dx = s16(o + F_X) - tx;
    if (dx) {
        int d = abs(dx) >> 4;
        if (d > 12) d = 12;
        int8_t xv = (int8_t)g_ram[p + F_XVEL];
        d += abs(xv) + 1;
        if (dx > 0) {
            g_ram[o + F_STATUS] |= ST_FACING;
            if (d >= dx) { d = dx; dx = 0; }
            wr16(o + F_X, (unsigned)(s16(o + F_X) - d));
        } else {
            g_ram[o + F_STATUS] &= ~ST_FACING;
            if (d >= -dx) { d = -dx; dx = 0; }
            wr16(o + F_X, (unsigned)(s16(o + F_X) + d));
        }
    }
    int dy = s16(o + F_Y) - ty;
    if (dy) {
        /* Native homing is 1px/frame; a glide descends a little faster. */
        int d = abs(dy) >> 4; if (d < 1) d = 1; if (d > 6) d = 6;
        if (d > abs(dy)) d = abs(dy);
        wr16(o + F_Y, (unsigned)(s16(o + F_Y) + (dy > 0 ? -d : d)));
    }
    glide_pose(o);
    a->displayed = 1;
    if ((status & 0x80) || dx || dy || g_ram[p + F_ROUTINE] >= 6) return;
    a->mode = MODE_FOLLOW;
    g_ram[o + F_CONTROL] = 0; g_ram[o + F_ANIM] = 0;
    wr16(o + F_XVEL, 0); wr16(o + F_YVEL, 0); wr16(o + F_GVEL, 0);
    g_ram[o + F_STATUS] = (uint8_t)((g_ram[o + F_STATUS] & ST_UNDERWATER) | ST_AIR);
    wr16(o + F_MOVE_LOCK, 0);
    wr16(o + F_ART, (rd16(o + F_ART) & 0x7FFF) | (rd16(p + F_ART) & 0x8000));
    g_ram[o + F_TOP_SOLID] = g_ram[p + F_TOP_SOLID];
    g_ram[o + F_LRB_SOLID] = g_ram[p + F_LRB_SOLID];
}

/* ---- One native Knuckles tick for an extra --------------------------------*/
typedef struct {
    uint8_t ctrl[4], locked, physics[6], distance[2], dust[OBJ], bubbles[OBJ];
    uint8_t super, hud_timer, chain[2], glide, angles[4], collision[4], prev_frame;
    uint8_t shake[2], water, scroll_lock, hscroll[2], debug_art[2];
} Globals;
static void save_globals(Globals *g)
{
    memcpy(g->ctrl, g_ram + CTRL_1_LOGICAL, 4); g->locked = g_ram[CTRL_1_LOCKED];
    memcpy(g->physics, g_ram + PHYSICS, 6); memcpy(g->distance, g_ram + DISTANCE_TOP, 2);
    memcpy(g->dust, g_ram + DUST, OBJ); memcpy(g->bubbles, g_ram + BUBBLES, OBJ);
    g->super = g_ram[SUPER_FLAG]; g->hud_timer = g_ram[HUD_TIMER];
    memcpy(g->chain, g_ram + CHAIN, 2); g->glide = g_ram[GLIDE_FLAGS];
    memcpy(g->angles, g_ram + ANGLES, 4); memcpy(g->collision, g_ram + COLLISION_ADDR, 4);
    g->prev_frame = g_ram[PREV_FRAME]; memcpy(g->shake, g_ram + GLIDE_SHAKE, 2);
    g->water = g_ram[WATER_ENTERED]; g->scroll_lock = g_ram[SCROLL_LOCK_P2];
    memcpy(g->hscroll, g_ram + HSCROLL_P2, 2); memcpy(g->debug_art, g_ram + DEBUG_ART, 2);
}
static void restore_globals(const Globals *g)
{
    memcpy(g_ram + CTRL_1_LOGICAL, g->ctrl, 4); g_ram[CTRL_1_LOCKED] = g->locked;
    memcpy(g_ram + PHYSICS, g->physics, 6); memcpy(g_ram + DISTANCE_TOP, g->distance, 2);
    memcpy(g_ram + DUST, g->dust, OBJ); memcpy(g_ram + BUBBLES, g->bubbles, OBJ);
    g_ram[SUPER_FLAG] = g->super; g_ram[HUD_TIMER] = g->hud_timer;
    memcpy(g_ram + CHAIN, g->chain, 2); g_ram[GLIDE_FLAGS] = g->glide;
    memcpy(g_ram + ANGLES, g->angles, 4); memcpy(g_ram + COLLISION_ADDR, g->collision, 4);
    g_ram[PREV_FRAME] = g->prev_frame; memcpy(g_ram + GLIDE_SHAKE, g->shake, 2);
    g_ram[WATER_ENTERED] = g->water; g_ram[SCROLL_LOCK_P2] = g->scroll_lock;
    memcpy(g_ram + HSCROLL_P2, g->hscroll, 2); memcpy(g_ram + DEBUG_ART, g->debug_art, 2);
}
static void tick_native(unsigned k, uint16_t input)
{
    Actor *a = &actors[k];
    Globals g; save_globals(&g);
    wr16(CTRL_1_LOGICAL, input); wr16(CTRL_1, 0); g_ram[CTRL_1_LOCKED] = 1;
    memcpy(g_ram + PHYSICS, a->physics, 6);
    /* Super/Hyper state and its ring drain belong to Player 1. Update_HUD_timer
     * is zero so Knux_Test_For_Glide can never start a transformation. */
    g_ram[SUPER_FLAG] = 0; g_ram[HUD_TIMER] = 0;
    a->displayed = 0;
    inside_actor = (int)k;
    M68KState cpu = g_cpu;
    g_cpu.A[0] = 0xFFFF0000u | a->slot;
    native(PC_KNUCKLES);
    g_cpu = cpu;
    inside_actor = -1;
    memcpy(a->physics, g_ram + PHYSICS, 6);
    restore_globals(&g);
}
static void tick(unsigned k)
{
    Actor *a = &actors[k];
    unsigned o = a->slot;
    a->visible = (uint8_t)on_screen(o);
    g_ram[o + F_RENDER] = (uint8_t)((g_ram[o + F_RENDER] & 0x7F) | (a->visible ? 0x80 : 0));
    uint8_t pad = 0;
    int driven = human(k, &pad);
    a->driven = (uint8_t)driven;
    uint8_t pressed = (uint8_t)(pad & ~a->held);
    a->held = pad;
    switch (a->mode) {
    case MODE_PARKED: {
        unsigned p = PLAYER_1;
        int go = driven ? (pressed & (BTN_ABC | BTN_START)) != 0 :
            !((rd16(LEVEL_FRAME) + a->phase) & 0x3F) && !(g_ram[p + F_CONTROL] & 0x80) &&
            !(g_ram[p + F_STATUS] & 0x80) && g_ram[p + F_ROUTINE] < 6;
        if (go) enter_catchup(a);
        return;
    }
    case MODE_CATCHUP:
        catchup_move(a);
        return;
    case MODE_SPINDASH:
        a->input = driven ? (uint16_t)(pad << 8 | pressed) : spindash_input(k);
        break;
    default:
        settle(a);
        a->input = driven ? (uint16_t)(pad << 8 | pressed) : follow_input(k);
        if (driven && offscreen_timeout(a)) return;
        break;
    }
    if (a->mode == MODE_PARKED) return;
    a->inputs_seen |= a->input;
    tick_native(k, a->input);
}

/* Extras are ordinary guest RAM, so a moment-in-time quickstate restores them
 * without this host table. Adopt restored extras (or clear them when the
 * crowd is off) instead of leaking their slots. */
static void adopt_restored(int keep)
{
    for (unsigned o = DYN_FIRST; o < DYN_END; o += OBJ) {
        if (rd32(o) != CODE_IDLE || actor_of(o) >= 0) continue;
        Actor *a = NULL;
        for (unsigned k = 0; keep && k < count && !a; ++k) if (!actors[k].slot) a = &actors[k];
        if (!a) { memset(g_ram + o, 0, OBJ); continue; }
        a->slot = (uint16_t)o; a->input = 0; a->held = 0; a->auto_jump = 0; a->flight = 0;
        memset(a->solid, 0, sizeof a->solid);
        default_physics(a->physics);
        if (g_ram[o + F_CONTROL] & 0x80) {
            if (rd16(o + F_X) == 0x7F00) park(a);
            else a->mode = MODE_CATCHUP;
        } else a->mode = MODE_FOLLOW;
    }
}
static void step(void)
{
    if (!armed()) {
        if (count) release_all();
        if ((g_ram[GAME_MODE] & 0x7F) == 0x0C) adopt_restored(0);
        return;
    }
    if (!count) {
        count = s3_army_valid_size(s3_army.size) ? s3_army.size : 16;
        for (unsigned k = 0; k < count; ++k) {
            memset(&actors[k], 0, sizeof actors[k]);
            configure_actor(k);
        }
        memset(slot_codes, 0, sizeof slot_codes);
    }
    refresh_slot_codes();
    adopt_restored(1);
    if (shift_age < 64) ++shift_age;
    /* A level (re)load clears object RAM. Losing every slot at once means the
     * level restarted: the crowd reappears beside Player 1. */
    unsigned held = 0;
    for (unsigned k = 0; k < count; ++k) {
        Actor *a = &actors[k];
        if (a->slot && rd32(a->slot) != CODE_IDLE) { a->slot = 0; a->mode = MODE_BENCHED; }
        held += a->slot != 0;
    }
    int fresh = !held && g_ram[PLAYER_1 + F_ROUTINE] == 2 && rd32(PLAYER_1) != 0;
    if (fresh) shift_age = 64;
    /* Never starve the level: keep a reserve of free dynamic slots, benching
     * the last extras first and returning them when room reappears. */
    enum { RESERVE = 16 };
    unsigned free = free_slots();
    for (int k = (int)count - 1; k >= 0 && free < RESERVE; --k)
        if (actors[k].slot) { release(&actors[k]); ++free; }
    for (unsigned k = 0; k < count && free > RESERVE; ++k) {
        if (actors[k].slot) continue;
        int o = free_slot(); if (!o) break;
        spawn(k, (unsigned)o, fresh); --free;
    }
    /* Process_Sprites freezes the world while Player 1 dies; so do extras. */
    unsigned routine = g_ram[PLAYER_1 + F_ROUTINE];
    if ((routine >= 6 && routine != 0x0C) || rd16(DEBUG_PLACEMENT)) return;
    for (unsigned k = 0; k < count; ++k) if (actors[k].slot) tick(k);
}

/* ---- Solid objects -------------------------------------------------------- */
static const ArmySite *site_for_return(uint32_t ret)
{
    for (unsigned i = 0; i < sizeof army_sites / sizeof army_sites[0]; ++i)
        if (army_sites[i].ret == ret) return &army_sites[i];
    return NULL;
}
static int solid_callee(uint32_t pc)
{
    for (unsigned i = 0; i < sizeof army_callees / sizeof army_callees[0]; ++i)
        if (army_callees[i] == pc) return 1;
    return 0;
}
static unsigned reactions[4], solid_calls;  /* telemetry */
static void react(const ArmySite *s, const M68KState *result, unsigned o)
{
    unsigned obj = result->A[0] & 0xFFFF;
    int go = 0;
    switch (s->kind) {
    case REACT_STANDING: go = (g_ram[obj + F_STATUS] & 0x10) != 0; break;
    case REACT_BELOW: go = (int16_t)result->D[4] == -2; break;
    case REACT_SIDE: {
        if (!((result->D[6] >> 16) & 2)) break;
        unsigned d1 = g_ram[obj + F_STATUS];
        if (rd16(obj + F_X) >= rd16(o + F_X)) d1 ^= 1;   /* sub.w; bcs */
        go = !(d1 & 1);
        break;
    }
    default: break;
    }
    if (!go) return;
    ++reactions[s->kind];
    M68KState cpu = g_cpu;
    g_cpu = *result; g_cpu.A[1] = 0xFFFF0000u | o;
    native(s->reaction);
    g_cpu = cpu;
}
static int solid_extras(uint32_t pc)
{
    if (inside_solid || inside_actor >= 0 || !s3_army_active()) return 0;
    if ((g_cpu.A[1] & 0xFFFF) != PLAYER_1 || (g_cpu.D[6] & 0xFF) != 3) return 0;
    const ArmySite *site = site_for_return(rd32(g_cpu.A[7]) & 0xFFFFFF);
    M68KState input = g_cpu;
    inside_solid = 1;
    recomp_call_addr(pc);                 /* Player 1, at the caller's own slot */
    M68KState result = g_cpu;
    for (unsigned k = 0; k < count; ++k) {
        Actor *a = &actors[k];
        if (!a->slot || (a->mode != MODE_FOLLOW && a->mode != MODE_SPINDASH) ||
            g_ram[a->slot + F_ROUTINE] >= 6) continue;
        SolidSwap swap;
        solid_enter(a, &swap, input.A[0], rd16(a->slot + F_INTERACT));
        g_cpu = input; g_cpu.A[1] = 0xFFFF0000u | a->slot;
        g_cpu.D[6] = (g_cpu.D[6] & ~0xFFu) | 4;
        M68KState extra = native(pc);
        ++solid_calls;
        if (site && site->kind != REACT_NONE) react(site, &extra, a->slot);
        solid_leave(a, &swap);
    }
    inside_solid = 0;
    /* Callers read Player 1's result registers; keep them exactly. */
    g_cpu = result;
    return 1;
}

/* ---- Hooks ---------------------------------------------------------------- */
static void capture(void)
{
    published.count = 0;
    if (!s3_army_active()) return;
    published.camera_x = s16(CAMERA_X_COPY); published.camera_y = s16(CAMERA_Y_COPY);
    published.wrap = rd16(SCREEN_Y_WRAP);
    for (unsigned k = 0; k < count; ++k) {
        const Actor *a = &actors[k];
        if (!a->slot || !a->displayed) continue;
        unsigned o = a->slot;
        Shown *s = &published.actor[published.count++];
        s->x = s16(o + F_X); s->y = s16(o + F_Y);
        s->art = rd16(o + F_ART); s->frame = g_ram[o + F_FRAME];
        s->flip = g_ram[o + F_RENDER] & 3;
    }
}
static int title_enabled(void) { return s3_army.enabled && art_ready; }
int s3_army_owns(uint32_t pc)
{
    switch (pc) {
    case PC_STEP: case PC_RENDER: case PC_DRAW: case PC_DPLC: case PC_DEATH:
    case PC_ALLOC: case PC_ALLOC_AFTER: case PC_TRANSITION: case ARMY_PC_SK_TITLE_FRAMES:
        return 1;
    default:
        return solid_callee(pc);
    }
}
int s3_army_hook(uint32_t pc)
{
    switch (pc) {
    case PC_STEP:
        if (inside_actor < 0 && !inside_solid) step();
        return 0;
    case PC_RENDER:
        capture();
        return 0;
    case PC_DRAW: {
        /* Extras are host-rendered: their art is not in Player 1's VRAM. */
        int k = actor_of(g_cpu.A[0]);
        if (k < 0) return s3_title_hook(pc, title_enabled());
        actors[k].displayed = 1;
        return 1;
    }
    case PC_DPLC:
        return inside_actor >= 0 && actor_of(g_cpu.A[0]) == inside_actor;
    case PC_DEATH: {
        if (inside_actor < 0 || actor_of(g_cpu.A[0]) != inside_actor) return 0;
        /* No life is lost: fall past the camera, then catch up like Tails. */
        Actor *a = &actors[inside_actor];
        unsigned o = a->slot;
        int y = s16(o + F_Y), cy = s16(CAMERA_Y);
        g_ram[o + F_SPINDASH] = 0;
        int gone = g_ram[REVERSE_GRAVITY] ? cy - 0x10 >= y : cy + 0x100 < y;
        if (gone) { park(a); wr16(o + F_ART, rd16(o + F_ART) & 0x7FFF); }
        return 1;
    }
    case PC_ALLOC:
    case PC_ALLOC_AFTER:
        /* Immediate relief if the reserve was exhausted within one frame. */
        if (count && !free_slots()) {
            for (int k = (int)count - 1; k >= 0; --k)
                if (actors[k].slot && actors[k].slot > (g_cpu.A[0] & 0xFFFF)) { release(&actors[k]); break; }
        }
        return 0;
    case ARMY_PC_SK_TITLE_FRAMES:
        return s3_title_hook(pc, title_enabled());
    case PC_TRANSITION:
        if (count) { shift_x = (int16_t)g_cpu.D[0]; shift_y = (int16_t)g_cpu.D[1]; shift_age = 0; }
        return 0;
    default:
        return solid_callee(pc) ? solid_extras(pc) : 0;
    }
}

/* ---- Presentation ---------------------------------------------------------- */
static unsigned lag_ticks, vblank_ticks;
void s3_army_vblank(void)
{
    /* The SAT built with this capture is uploaded by this V-int. */
    shown = published;
    s3_title_vblank(&g_machine.vdp, title_enabled());
    /* Lag telemetry, sampled at the IRQ like the family renderer's. */
    static unsigned last_tick; static int was_active;
    unsigned tick = rd16(LEVEL_FRAME);
    /* Running gameplay only: loading ($8C), pause and Player 1's own death
     * legitimately hold the level frame counter. */
    /* Counted with the crowd off too, so stock lag is comparable. */
    int active = g_ram[GAME_MODE] == 0x0C && !g_ram[PAUSED] && g_ram[PLAYER_1 + F_ROUTINE] < 6 &&
        rd32(PLAYER_1) != 0;
    if (active && was_active) { ++vblank_ticks; lag_ticks += tick == last_tick; }
    was_active = active; last_tick = tick;
}
/* Each extra is a full native Knuckles tick charged at 68K speed; a crowd
 * would otherwise turn every frame into a lag frame. LevelLoop still waits
 * for the real V-int, so this is enhanced-simulation headroom only (the same
 * contract as the custom-width renderer). */
unsigned s3_sk_main_cpu_divisor(void)
{
    unsigned video = s3_video_main_cpu_divisor();
    /* Extras tick whenever the crowd exists (intros included), so headroom
     * follows the crowd rather than Level_started_flag. */
    if (!s3_army_active()) return video;
    /* The renderer's expanded activation and the crowd are independent
     * costs, so their headroom adds. One native frame per 4 extras plus two
     * of margin: 1 + count/8 sufficed natively but lagged widescreen S&K's
     * MHZ intro (before Level_started_flag gates the renderer's own share).
     * Unused headroom costs nothing: LevelLoop still waits for V-int. */
    return video + 2 + (count + 3) / 4;
}
/* Draw the latched crowd into one output row. `origin` is the output column of
 * camera_x; earlier extras win, all of them behind every native sprite. */
static void draw_line(const GVDPSpriteLayer *layer, int origin, int camera_x, int camera_y)
{
    for (unsigned n = 0; n < shown.count; ++n) {
        const Shown *s = &shown.actor[n];
        if (s->frame >= art.count) continue;
        const KnuxFrame *f = &art.frames[s->frame];
        int sy = (int)(((unsigned)(s->y - camera_y + 128) & shown.wrap)) - 128;
        int top = (s->flip & 2) ? -(f->y + f->height) : f->y;
        int dy = layer->line - sy;
        if (dy < top || dy >= top + f->height) continue;
        int left = (s->flip & 1) ? -(f->x + f->width) : f->x;
        unsigned pal = (s->art >> 13) & 3, high = s->art >> 15;
        for (int dx = left; dx < left + f->width; ++dx) {
            int col = origin + s->x - camera_x + dx;
            if (col < 0 || col >= layer->total || layer->opaque[col]) continue;
            uint8_t p = knux_art_pixel(&art, s->frame, dx, dy, s->flip);
            if (!p) continue;
            layer->index[col] = (uint8_t)((((pal + (p >> 4)) & 3) << 4) | (p & 15));
            layer->opaque[col] = 1;
            layer->high[col] = (uint8_t)(high | (p >> 7));
        }
    }
}
static void host_sprites(void *user, const GVDP *v, const GVDPSpriteLayer *layer)
{
    (void)user;
    if ((v->reg[12] & 6) == 6) return;  /* not in interlace mode 2 */
    if (shown.count) draw_line(layer, layer->offset, shown.camera_x, shown.camera_y);
    s3_title_draw(v, layer, art_ready ? &art : NULL);
}
void s3_army_draw_wide(const GVDP *v, int line, uint32_t *out, int width, int origin,
                       const uint32_t *palette, uint8_t *priority, int camera_x, int camera_y)
{
    (void)v;
    if (!shown.count) return;
    enum { MAXW = 4096 };
    static uint8_t index[MAXW], opaque[MAXW], high[MAXW], before[MAXW];
    if (width > MAXW) width = MAXW;
    for (int x = 0; x < width; ++x) before[x] = opaque[x] = (priority[x] & 2) != 0;
    GVDPSpriteLayer layer = { line, width, origin, index, opaque, high };
    draw_line(&layer, origin, camera_x, camera_y);
    for (int x = 0; x < width; ++x) {
        if (before[x] || !opaque[x]) continue;
        priority[x] |= 2;
        if (high[x] || !(priority[x] & 1)) out[x] = palette[index[x]];
    }
}

/* S&K-code specs compose this mod in front of the family renderer. The
 * renderer's default case rewrites D0 at unlisted PCs, so it must never see
 * this mod's sites; Render_Sprites is the one site both capture at. */
int s3_sk_instruction_hook(uint32_t pc)
{
    if (!s3_army_owns(pc)) return s3_video_hook(pc);
    int replaced = pc == PC_RENDER ? s3_video_hook(pc) : 0;
    return s3_army_hook(pc) || replaced;
}
void s3_army_init(void)
{
    s3_video_set_actor_overlay(s3_army_draw_wide);
    if (!art_ready) {
        char error[160];
        if (knux_art_decode(g_rom, sizeof g_rom, &knux_art_stock, &art, error, sizeof error)) art_ready = 1;
        else fprintf(stderr, "Knuckles & Knuckles unavailable: %s\n", error);
    }
    gvdp_set_host_sprites(host_sprites, NULL);
}
/* Bounded JSON append: a full buffer truncates, never overruns. */
typedef struct { char *p; size_t size, len; } Json;
static void jappend(Json *j, const char *fmt, ...)
{
    if (j->len >= j->size) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(j->p + j->len, j->size - j->len, fmt, ap);
    va_end(ap);
    j->len = n < 0 ? j->size : j->len + (size_t)n;
}
void s3_army_command(int id, const char *json)
{
    (void)json;
    static char buf[16384];
    Json j = { buf, sizeof buf - 3, 0 };
    jappend(&j, "{\"id\":%d,\"ok\":true,\"enabled\":%d,\"art\":%d,\"active\":%d,"
        "\"count\":%u,\"free_slots\":%u,\"shown\":%u,\"frame\":%u,\"vblanks\":%u,\"lag\":%u,\"divisor\":%u,"
        "\"solid_calls\":%u,\"reactions\":{\"standing\":%u,\"below\":%u,\"side\":%u},"
        "\"p1\":{\"x\":%d,\"y\":%d,\"routine\":%u,"
        "\"status\":%u,\"control\":%u},\"actors\":[", id, s3_army.enabled, art_ready,
        s3_army_active(), count, free_slots(), shown.count, rd16(LEVEL_FRAME), vblank_ticks, lag_ticks,
        s3_sk_main_cpu_divisor(), solid_calls, reactions[REACT_STANDING], reactions[REACT_BELOW],
        reactions[REACT_SIDE], s16(PLAYER_1 + F_X),
        s16(PLAYER_1 + F_Y), g_ram[PLAYER_1 + F_ROUTINE], g_ram[PLAYER_1 + F_STATUS], g_ram[PLAYER_1 + F_CONTROL]);
    for (unsigned k = 0; k < count; ++k) {
        const Actor *a = &actors[k];
        jappend(&j, "%s{\"slot\":%u,\"mode\":%u,\"x\":%d,\"y\":%d,\"routine\":%u,\"visible\":%u,"
            "\"delay\":%u,\"flight\":%u,\"shown\":%u,\"seat\":%u,\"human\":%u,\"input\":%u,\"inputs_seen\":%u}", k ? "," : "",
            a->slot, a->mode, a->slot ? s16(a->slot + F_X) : 0, a->slot ? s16(a->slot + F_Y) : 0,
            a->slot ? g_ram[a->slot + F_ROUTINE] : 0, a->visible, a->delay, a->flight, a->displayed,
            s3_army_seat(k), a->driven, a->input, a->inputs_seen);
    }
    for (unsigned k = 0; k < count; ++k) actors[k].inputs_seen = 0;
    if (j.len > j.size) j.len = j.size;
    memcpy(buf + j.len, "]}", 3);
    cmd_send_response(buf);
}
