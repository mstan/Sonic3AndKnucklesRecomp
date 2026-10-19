/*
 * scd_mmz_boss.c — Metallic Madness act 3 (Sonic CD R83C / R83D): the dark
 * room, the final boss, Amy, and the act's own objects. Transcribed from the
 * Mega-CD program (the routines' R83C addresses in brackets; names from the
 * PC port's HOTA8C.C, BOSS_8.C, EMIE8.C, SHUT.C, KONBEA83.C, JETTOGE8.C and
 * TRAP_R83.C; R83D's code is R83C's but for the dark room's colours).
 *
 *   $39 the dark room (subtype $FE, placed at $C30): once player 1 is past
 *       $BD0 it freezes the arena's curtain art (shut_flag), shuts the room's
 *       far side with a door ($29, Metallic Madness' own, at $CA8), raises the
 *       camera and holds its bottom at $10C (bossFlags); the room goes dark;
 *       a firefly comes, then
 *       two: each flies to near the player (leaving after-images), flashes the
 *       room's colours and rains sparks ($39 subtype $FF) a while, and again,
 *       till it is spun into (an enemy only while it fires). With both gone
 *       the room lights again (the good future; the bad stays dark), the door
 *       opens and Eggman ($3F) is set in the arena, out of sight above
 *   $3F Eggman's pod: with player 1 at $F00 the camera locks ($E60) and the
 *       pod builds its shell ($41) and four blades ($40); 240 frames later it
 *       comes down, the final boss song starts, the curtain ($3D) parts and
 *       line 1 fades in; then it runs a script by the blades it has left:
 *       sweeps, a spin across the arena stepping the blades round, a lunge at
 *       the player, a cartwheel hop, the blades flung up and rained down on
 *       the player or on a pattern, and the shell opening out. A spin into
 *       the pod or the shell knocks a blade off (the one nearest the player's
 *       side); 120 frames' grace (blinking). Four hits; then it burns 90
 *       frames, Amy ($43) comes and it flies off right, exploding
 *   $40 a blade: orbits the shell at its radius (the shell's angle steps,
 *       its set of mappings by what the shell does), always hurts
 *   $41 the shell: the blades' state (stopped, rolling, spread, fired)
 *   $42 sparks (two) at the pod when hit and as it flies
 *   $43 Amy: takes the player's pad (CD's ctrlLocked): he runs right to $F90,
 *       back to $F70 and waits; she runs in, hops twice, walks off right while
 *       the screen goes to white (the camera's bottom shaking): there Sonic
 *       CD's ending begins (timeZone bit 7, levelRestart: the main program
 *       plays the good ending with all seven Time Stones or every act 3 in a
 *       good future, else the bad). Here the colours come back and S3K's
 *       results tally ends the act, as at Palmtree Panic's capsule; the
 *       trilogy's flow goes on from there
 *   $3D the curtain's edges (two, placed at $F00): when the song starts they
 *       slide apart 2 px a frame, clearing the curtain's blocks (block 0) a
 *       16 px column at a time, 12 rows from 96 px above them
 *   $2A flip panel (act 3's own conveyor, KONBEA83): a top-solid panel; 120
 *       frames after it was last flat it turns over (throwing off whoever
 *       stands on it) and back
 *   $2B jet spike column: drifts $5000 (5/16 px) a frame, 410 frames each
 *       way, turning; hurts while player 1 is on the front layer
 *   $36 act 3's block: positive subtype a solid block with a spring (0) or
 *       upright spikes (1) on top; negative, spikes ($FF up: stood on, they
 *       hurt; others hang: touch); solid only with player 1 within 32 px up
 *       or down (act 2's has other kinds)
 * The level's events for act 3 (zone8chk [$203622]: with bossFlags the
 * camera's bottom is $10C) run here, from these objects: nothing else needs
 * it. Act 3's other objects are acts 1-2's (scd_mmz*.c) and Collision Chaos'
 * bumper ($1D).
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (named below), CD's $10 long
 * (a y speed) S3K's $18, CD's $10 / $12 words S3K's x_vel / y_vel. Routines
 * keep CD's even numbers. Collision: the pod and the shell are CD enemies
 * with hits left ($21 = 2, renewed 120 frames after a hit), S3K's enemy
 * touch with collision_property: Touch_Enemy clears the type on a hit, as
 * CD's does, and the pod polls both.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>

/* this act's asset ids (scd_level.py CD_ASSETS; -> trilogy_obj.h) */

/* the pod ($3F) */
#define E_TIME   0x30                  /* oVar2A (word; byte $2A) */
#define E_TIME2  0x31                  /* oVar2B */
#define E_FLAGS  0x32                  /* oVar2C: 0 next action, 1 (cleared), 3 can be hit */
#define E_STEP   0x33                  /* oVar2D */
#define E_MECA   0x34                  /* oVar2E: the shell */
#define E_DX     0x36                  /* oVar30: the target x; (a hop) the last x step */
#define E_DY     0x38                  /* oVar32: (a hop, the flight) the last y step */
#define E_VX     0x3A                  /* oVar34 (long): x speed */
#define E_PARAM  0x3E                  /* oVar38 (word): the action's parameter; (spin) steps */
#define E_PARAMB 0x3F                  /* (its low byte: a wait's frames) */
#define E_HITS   0x40                  /* oVar3A: blades left */
#define E_SCRIPT 0x41                  /* oVar3B */
#define E_ANIMT  0x42                  /* oVar3C: the hit / laugh animation's time */
#define E_CYC    0x43                  /* oVar3D: PalCycleBoss8's step */
#define E_CYCT   0x44                  /* oVar3E: its timer */
#define E_VY     0x18                  /* (CD $10, long): y speed */
/* the shell ($41) */
#define M_TICK   0x30                  /* oVar2A: frames to the blades' next step */
#define M_CNT    0x31                  /* oVar2B */
#define M_FLAGS  0x32                  /* oVar2C: 3 resetting, 4 toggle, 5 / 6 step right / left */
#define M_STEP   0x33                  /* oVar2D */
#define M_EGG    0x34                  /* oVar2E */
#define M_SET    0x3A                  /* oVar34: the blades' mapping set */
#define M_FRAME  0x3B                  /* oVar35: blade 0's frame */
#define M_TARGET 0x3C                  /* oVar36: the frame to stop on */
#define M_SPEED  0x3D                  /* oVar37: frames a step */
#define M_RAD    0x3E                  /* oVar38 (word): the blades' radius, $100 = out */
#define M_STATE  0x40                  /* oVar3A: 7 stopped, 6 rolling, 5 out */
#define M_DROP   0x41                  /* oVar3B: the blade knocked off ($FF none) */
#define M_BLINK  0x42                  /* oVar3C */
#define M_FIRED  0x43                  /* oVar3D */
#define M_UP     0x44                  /* oVar3E */
#define M_PAT    0x45                  /* oVar3F: the rain's pattern */
/* a blade ($40) */
#define H_TIME   0x30                  /* oVar2A */
#define H_CNT    0x31                  /* oVar2B */
#define H_FLAGS  0x32                  /* oVar2C: 3 knocked off */
#define H_STEP   0x33                  /* oVar2D */
#define H_MECA   0x34                  /* oVar2E */
#define H_DX     0x36                  /* oVar30 */
#define H_DY     0x38                  /* oVar32 */
#define H_VX     0x3A                  /* oVar34 (long) */
#define H_NUM    0x3E                  /* oVar38: which blade (0 leads) */
#define H_VY     0x18                  /* (CD $10, long) */
/* sparks ($42) */
#define S_TIME   0x30
#define S_CNT    0x31
#define S_STEP   0x33
#define S_PAR    0x34
#define S_SIDE   0x3A                  /* oVar34 */
/* Amy ($43) */
#define A_TIME   0x30
#define A_STEPS  0x31
#define A_PAD    0x33                  /* oVar2D: the player's pad script */
#define A_SHAKE  0x34                  /* oVar2E */
/* the room and its fireflies ($39) */
#define F_N      0x31                  /* oVar2B: (a firefly) its flights */
#define F_TX     0x32                  /* oVar2C (word) */
#define F_TY     0x34                  /* oVar2E (word) */
#define R_FLY2   0x36                  /* oVar30: (the room) the second firefly */
#define F_Y0     0x38                  /* oVar32 */
#define R_FLY    0x3A                  /* oVar34: (the room) the firefly */
#define F_X0     0x3C                  /* oVar36 */
#define F_TIME   0x40                  /* oVar3A (word; byte) */
#define F_TIME2  0x41                  /* oVar3B */
#define F_IDX    0x42                  /* oVar3C */
#define R_DOOR   0x44                  /* oVar3E: (the room) the door */
#define F_BEAM   0x45                  /* oVar3F */

#define P_ROUTINE 0x05
#define P_ANIM    0x20
#define P_ANIMF   0x23
#define P_INVULN  0x34                 /* (invulnerability_timer: CD's oPlayerHurt) */
#define RAM_Distance_from_top 0xFFEE2Cu   /* (CD's camYCenter) */

#define FM_BOSSHIT   0xAC              /* (egg8_colichk: a hit) */
#define FM_THUMP     0xB4              /* (egg8_tobi_d, egg8_tobi: it lands; egg8hane_fire1: a blade in the floor) */
#define FM_BOMB      0x9E              /* (bom_set: FM_EXPLODE) */
#define FM_SPREAD    0xC9              /* (egg8_hane_chg2: the blades spread) */
#define FM_TICK      0xBA              /* (rol_sound: a blade steps round) */

/* (to be provided: a Sonic CD block of the act's layout set at run time, its
 * picture and collision too: CD's block_wrt; as scd_ccz.c's) */
void tri_scd_block_write(int x, int y, uint16_t block);
int scd_mmz_back(void);                /* scd_mmz.c: CD's layer (player 1 on the back) */
void cd_bumper(uint32_t o);            /* scd_ccz.c: Collision Chaos' $1D bumper, the same code here */
#define MMZ12_DOOR (NATIVE_CD_BASE + (0x20u + 6 * 0x80u + 0x29u) * 4u)   /* (acts 1-2's door, scd_mmz_trap.c) */

static int s_base3;
static uint8_t s_flags;                /* bossFlags */
static uint8_t s_fight;                /* bossFight */
static uint8_t s_shut;                 /* shut_flag ($FF1959): the curtain's art held */
static int8_t s_shake;                 /* (Amy's: on the camera's bottom this frame) */
static uint16_t s_frame = 0xFFFF;      /* (the events: once a frame) */
static uint16_t s_pal[64];             /* (the colours before the ending's white) */
static uint8_t s_pal_saved;

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }
static int is(uint32_t o, int id) { return o && r32(o + OBJ_code) == native3(id); }
static uint32_t lk(uint32_t o, unsigned f)
{
    uint16_t w = o ? OB16(o, f) : 0;
    return w ? 0xFF0000u | w : 0;
}
static void set_lk(uint32_t o, unsigned f, uint32_t a) { if (o) SET16(o, f, (uint16_t)a); }
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }
static int32_t L(uint32_t o, unsigned f) { return (int32_t)r32(o + f); }
static void setL(uint32_t o, unsigned f, int32_t v) { SET32(o, f, (uint32_t)v); }
static void addL(uint32_t o, unsigned f, int32_t d) { SET32(o, f, (uint32_t)(L(o, f) + d)); }
static void bset(uint32_t o, unsigned f, unsigned b) { SET8(o, f, OB8(o, f) | (1u << b)); }
static void bclr(uint32_t o, unsigned f, unsigned b) { SET8(o, f, OB8(o, f) & ~(1u << b)); }
static int btst(uint32_t o, unsigned f, unsigned b) { return (OB8(o, f) >> b) & 1; }
static int bclr_was(uint32_t o, unsigned f, unsigned b) { int v = btst(o, f, b); bclr(o, f, b); return v; }

static void init(uint32_t o, int asset, int render, int prio, int width, int yrad)
{
    o_init(o, asset, render, prio, width, yrad > 0x20 ? yrad : 0x20);
    SET8(o, OBJ_y_radius, yrad);
}

static void anim_set(uint32_t o, int anim, int prev)          /* move.w #anim<<8|prev, oAnim */
{
    SET8(o, OBJ_anim, anim);
    SET8(o, OBJ_anim + 1, prev);
}

static void set_coll(uint32_t o, uint8_t cd, uint8_t hits)    /* CD's touch type as S3K's */
{
    if (!o) return;
    SET8(o, OBJ_coll_flags, cd ? tri_scd_coll(cd) : 0);
    SET8(o, OBJ_coll_prop, hits);
}

static void touch_draw(uint32_t o)
{
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

static uint32_t spawn(int id)                                  /* FindObjSlot, id */
{
    uint32_t a1 = o_alloc();
    if (a1) SET32(a1, OBJ_code, native3(id));
    return a1;
}
static uint32_t spawn_after(uint32_t o, int id)               /* FindNextObjSlot, id */
{
    uint32_t a1 = o_alloc_after(o);
    if (a1) SET32(a1, OBJ_code, native3(id));
    return a1;
}

static void add_points(int tens)                               /* AddPoints */
{
    D(0) = (uint32_t)tens;
    tri_jsr(S3K_HUD_AddToScore);
}

/* ---- palettes. S3K keeps CD's line 1 in its own order (scd_level.py
 * l1_order): CD's line 1 colour i is S3K's line 1 colour L1_PERM[i] ---------- */
static const uint8_t L1_PERM[16] = { 0, 11, 2, 3, 4, 7, 1, 12, 13, 14, 8, 9, 5, 6, 15, 10 };

static uint32_t pal_at(int cd)                                 /* CD's palette RAM index -> S3K's */
{
    if (cd >= 16 && cd < 32) cd = 16 + L1_PERM[cd - 16];
    return RAM_Normal_palette + (uint32_t)cd * 2u;
}

static void line1(int asset, uint32_t to)                      /* a palette asset into line 1 (S3K's order) */
{
    uint32_t pal = o_asset_anim(asset);
    if (!pal) return;
    for (unsigned k = 0; k < 16; k++) w16(to + 0x20 + k * 2u, tri_rd16(pal + k * 2u));
}

static uint16_t fade_to(uint16_t c, uint16_t t)                /* [$200344]: blue, else green, else red */
{
    if (c == t) return c;
    if ((uint16_t)(c + 0x200) <= t) return (uint16_t)(c + 0x200);
    if ((uint16_t)(c + 0x020) <= t) return (uint16_t)(c + 0x020);
    return (uint16_t)(c + 2);
}

static void fade_boss8(void)                                   /* [$200370]: line 1 a step toward its target */
{
    for (unsigned k = 0; k < 16; k++) {
        uint32_t a = RAM_Normal_palette + 0x20 + k * 2u;
        w16(a, fade_to(r16(a), r16(RAM_Target_palette + 0x20 + k * 2u)));
    }
}

static void palcycle_boss8(uint32_t o)                         /* [$200250] while bossFight: CD line 1, 8-11 */
{
    if (!s_fight) return;
    SET8(o, E_CYCT, OB8(o, E_CYCT) + 1);
    if (OB8(o, E_CYCT) < 4) return;
    SET8(o, E_CYCT, 0);
    unsigned d0 = OB8(o, E_CYC) + 8u;
    if (d0 >= 0x50) d0 = 0;
    SET8(o, E_CYC, d0);
    uint32_t tab = o_asset_anim(SCDA_M3BOSSCYC);
    if (!tab) return;
    for (unsigned k = 0; k < 4; k++) w16(pal_at(16 + 8 + (int)k), tri_rd16(tab + d0 + k * 2u));
}

/* ---- the level's events (zone8chk, act 3): with bossFlags the camera's
 * bottom is $10C (both: S3K's own move stays out); Amy shakes it. Once a
 * frame, from whichever of the room, the pod and Amy runs first ------------- */
static void events(void)
{
    uint16_t f = r16(RAM_Level_frame_counter);
    if (f == s_frame) return;
    s_frame = f;
    if (!s_flags) return;                                      /* (till then the level's $310, as converted) */
    uint16_t b = (uint16_t)(0x10C + s_shake);
    w16(RAM_Camera_max_Y_pos, b);
    w16(RAM_Camera_target_max_Y_pos, b);
}

/* ======================================================================
 * $39 the dark room, its fireflies, their sparks and after-images
 * ==================================================================== */
static void room_colours(int d0)                               /* [$20F81E] the room's colours, entry at d0 */
{
    uint32_t tab = o_asset_anim(SCDA_M3ROOMPAL);
    if (!tab) return;
    tab += (uint32_t)d0;
    if (cd_period() == TRI_CD_BAD) {                           /* (R83D: line 3, colours 4-15) */
        for (unsigned k = 0; k < 12; k++) w16(pal_at(48 + 4 + (int)k), tri_rd16(tab + k * 2u));
        return;
    }
    w16(pal_at(48 + 1), tri_rd16(tab));                        /* (R83C: line 3, colours 1-2 and 12-15) */
    w16(pal_at(48 + 2), tri_rd16(tab + 2));
    for (unsigned k = 0; k < 4; k++) w16(pal_at(48 + 12 + (int)k), tri_rd16(tab + 4 + k * 2u));
}

/* [$20F83C] the room's colours a step along a script of (entry, delay) */
static void room_fade(uint32_t o, const uint8_t *script)
{
    if (OB8(o, F_TIME) == 0x10) return;
    if (OB8(o, F_TIME2)) { SET8(o, F_TIME2, OB8(o, F_TIME2) - 1); return; }
    unsigned i = OB8(o, F_TIME);
    SET8(o, F_TIME2, script[i + 1]);
    room_colours(script[i]);
    SET8(o, F_TIME, i + 2);
}

static void fly_init(uint32_t o, int prio)                     /* [$20F364] */
{
    o_init(o, SCDA_M3FIREFLY, OB8(o, OBJ_render) | 4, prio, 8, 0x20);
    SET16(o, F_X0, OB16(o, OBJ_x_pos));
    SET16(o, F_Y0, OB16(o, OBJ_y_pos));
}

static void fly_animate(uint32_t o) { o_animate(o, o_asset_anim(SCDA_M3FIREFLY)); }

static void room(uint32_t o)                                   /* subtype $FE [$20F67E]: not drawn */
{
    static const uint8_t dark[16] = { 0x54, 0x40, 0x48, 4, 0x3C, 5, 0x30, 6, 0x24, 7, 0x18, 8, 0x0C, 9, 0, 0 };
    static const uint8_t light[16] = { 0, 0x40, 0x0C, 9, 0x18, 8, 0x24, 7, 0x30, 6, 0x3C, 5, 0x48, 4, 0x54, 0x20 };
    static const uint8_t dark_bf[16] = { 0xA8, 0x40, 0x90, 4, 0x78, 5, 0x60, 6, 0x48, 7, 0x30, 8, 0x18, 9, 0, 0 };
    int bf = cd_period() == TRI_CD_BAD;
    uint32_t p = RAM_Player_1;
    events();
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* [$20F6A0] */
        fly_init(o, 1);
        if (wx(p) < 0xBD0) break;
        s_shut = 1;
        uint32_t a1 = o_alloc();                              /* (the door: Metallic Madness' $29, at */
        if (!a1) break;                                       /*  acts 1-2's slot: its own code checks) */
        SET32(a1, OBJ_code, MMZ12_DOOR);
        set_xy(a1, 0xCA8, 0x1D0);
        SET16(o, R_DOOR, (uint16_t)a1);
        SET8(o, OBJ_routine, 2);
        break;
    }
    case 2:                                                   /* [$20F6E0] the camera looks up; bossFlags */
        if (wx(p) < 0xBD0) break;
        if (OBS16(RAM_Distance_from_top, 0) >= 0xCC) s_flags = 8;
        w16(RAM_Distance_from_top, r16(RAM_Distance_from_top) + 6);
        if (s_flags == 8) {
            SET8(o, OBJ_routine, 4);
            SET16(o, F_TIME, 0);
        }
        break;
    case 4: {                                                 /* [$20F712] dark; the first firefly */
        room_fade(o, bf ? dark_bf : dark);
        if (OB8(o, F_TIME) != 0x10) break;
        SET16(o, F_TIME, 0);
        uint32_t a1 = spawn(0x39);
        if (!a1) break;
        set_xy(a1, 0xBE0, 0x190);
        SET16(o, R_FLY, (uint16_t)a1);
        SET8(o, F_TIME, 0x3C);
        SET8(o, OBJ_routine, 6);
        break;
    }
    case 6: {                                                 /* [$20F752] it gone, 60 frames dark: two */
        if (is(lk(o, R_FLY), 0x39)) break;
        room_colours(0);
        SET8(o, F_TIME, OB8(o, F_TIME) - 1);
        if (OB8(o, F_TIME)) break;
        uint32_t a1 = spawn(0x39);
        if (!a1) break;
        set_xy(a1, 0xBE0, 0x188);
        SET16(o, R_FLY, (uint16_t)a1);
        SET8(a1, OBJ_subtype, 1);
        SET8(o, OBJ_routine, 8);
    }   /* fall through */
    case 8: {                                                 /* [$20F794] */
        uint32_t a1 = spawn(0x39);
        if (!a1) break;
        set_xy(a1, 0xC80, 0x188);
        SET16(o, R_FLY2, (uint16_t)a1);
        SET8(a1, OBJ_subtype, 0);
        SET16(o, F_TIME, 0);
        SET8(o, OBJ_routine, 0xA);
        break;
    }
    case 0xA:                                                 /* [$20F7C2] both gone */
        if (is(lk(o, R_FLY), 0x39) || is(lk(o, R_FLY2), 0x39)) break;
        SET8(o, OBJ_routine, 0xC);
        break;
    case 0xC: {                                               /* [$20F7E2] light (the bad future: dark), the door, Eggman */
        if (bf) room_colours(0);
        else {
            room_fade(o, light);
            if (OB8(o, F_TIME) != 0x10) break;
        }
        uint32_t d = lk(o, R_DOOR);
        if (d) SET8(d, 0x3F, 0xFF);                           /* (st.b oVar3F: Metallic Madness' door opens) */
        uint32_t a1 = spawn(0x3F);
        if (!a1) break;
        set_xy(a1, 0xF00, 0x13C);
        o_delete(o);
        return;
    }
    }
}

static void spark(uint32_t o)                                  /* subtype $FF [$20F59E]: a falling spark */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20F5B8] */
        fly_init(o, 3);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_width_px, 4);
        set_coll(o, 0x98, 0);
        break;
    case 2:                                                   /* [$20F5D6] down 8 a frame to the floor */
        SET8(o, OBJ_anim, 8);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
        fly_animate(o);
        if (o_floor_dist(o) < 0) {
            SET8(o, OBJ_routine, 4);
            SET16(o, F_TIME, 0);
        }
        break;
    case 4:                                                   /* [$20F5FE] splashes twice */
        if (!OB8(o, F_TIME)) {
            SET8(o, F_TIME, 2);
            SET8(o, OBJ_anim, 9);
            if (OB8(o, F_TIME2)) break;
        }
        fly_animate(o);
        SET8(o, F_TIME, OB8(o, F_TIME) - 1);
        if (OB8(o, F_TIME)) break;
        SET8(o, F_TIME2, OB8(o, F_TIME2) + 1);
        if (OB8(o, F_TIME2) >= 2) { o_delete(o); return; }
        break;
    }
    touch_draw(o);
}

static void afterimage(uint32_t o)                             /* subtype $FD [$20F644] */
{
    if (!OB8(o, OBJ_routine)) {
        fly_init(o, 4);
        SET8(o, F_TIME, 0x18);
        SET8(o, OBJ_anim, 4);
        SET8(o, OBJ_routine, 2);
    }
    fly_animate(o);
    SET8(o, F_TIME, OB8(o, F_TIME) - 1);
    if (!OB8(o, F_TIME)) { o_delete(o); return; }
    o_draw(o);
}

static void firefly(uint32_t o)
{
    static const uint8_t flash[32] = {                         /* [$20F4F0] (entries x 12) */
        0, 0, 0, 0x0C, 0x0C, 0x0C, 0x24, 0x24, 0x24, 0, 0, 0, 0x0C, 0x0C, 0x0C, 0x24, 0x24, 0x24,
        0, 0, 0, 0x0C, 0x0C, 0x0C, 0x24, 0x24, 0x24, 0x30, 0x3C, 0x48, 0x54, 0xFF,
    };
    uint32_t p = RAM_Player_1;
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub == 0xFD) { afterimage(o); return; }
    if (sub == 0xFE) { room(o); return; }
    if (sub == 0xFF) { spark(o); return; }
    SET8(o, OBJ_subtype, sub & 0x7F);
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20F38A] */
        fly_init(o, 1);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_width_px, 8);
        SET8(o, OBJ_routine, 2);
        SET8(o, F_TIME, 0x3C);
        /* fall through */
    case 2:                                                   /* [$20F3A4] appearing, 60 frames */
        SET8(o, OBJ_anim, 4);
        fly_animate(o);
        SET8(o, F_TIME, OB8(o, F_TIME) - 1);
        if (OB8(o, F_TIME)) break;
        SET8(o, OBJ_anim, 1);
        SET8(o, F_N, OB8(o, OBJ_subtype));
        SET8(o, OBJ_routine, 4);
        break;
    case 4: {                                                 /* [$20F3CC] where to: odd flights near the player */
        int16_t px = (int16_t)(wx(p) & 0xFFF8);
        if ((OB8(o, F_N) & 1) && px >= 0xBC0 && px < 0xCA0) {
            SET16(o, F_TX, px);
            SET16(o, F_TY, 0x158);
        } else {
            SET16(o, F_TX, (r16(RAM_Level_frame_counter) & 0x7F) + 0xBF0);
            SET16(o, F_TY, ((wx(p) * 5) & 0x1F) + 0x160);
        }
        SET16(o, OBJ_y_vel, (OBS16(o, F_TY) - wy(o)) * 8);
        SET16(o, OBJ_x_vel, (OBS16(o, F_TX) - wx(o)) * 8);
        SET8(o, F_N, OB8(o, F_N) + 1);
        SET8(o, F_TIME2, 0x20);
        SET8(o, OBJ_routine, 6);
        set_coll(o, 0, 0);
        break;
    }
    case 6:                                                   /* [$20F45E] 32 frames there, after-images */
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
        if ((OB8(o, F_TIME2) & 3) == 3) {                     /* [$20F48E] */
            uint32_t a1 = spawn(0x39);
            if (a1) {
                set_xy(a1, wx(o), wy(o));
                SET8(a1, OBJ_subtype, 0xFD);
            }
        }
        SET8(o, F_TIME2, OB8(o, F_TIME2) - 1);
        if (!OB8(o, F_TIME2)) SET8(o, OBJ_routine, 8);
        fly_animate(o);
        break;
    case 8: {                                                 /* [$20F4BE] the room flashes */
        static const uint8_t flash_bf[32] = {                  /* [R83D $20F578] (entries x 24) */
            0, 0, 0, 0x18, 0x18, 0x18, 0x48, 0x48, 0x48, 0, 0, 0, 0x18, 0x18, 0x18, 0x48, 0x48, 0x48,
            0, 0, 0, 0x18, 0x18, 0x18, 0x48, 0x48, 0x48, 0x60, 0x78, 0x90, 0xA8, 0xFF,
        };
        SET8(o, OBJ_anim, 0);
        fly_animate(o);
        uint8_t i = OB8(o, F_IDX), d0 = (cd_period() == TRI_CD_BAD ? flash_bf : flash)[i & 31];
        SET8(o, F_IDX, i + 1);
        if (d0 != 0xFF) { room_colours(d0); break; }
        SET8(o, OBJ_routine, 0xA);
        SET8(o, F_IDX, 0);
        break;
    }
    case 0xA:                                                 /* [$20F510] 20 frames of sparks: an enemy now */
        if (!OB8(o, F_IDX)) {
            SET8(o, F_IDX, 0x14);
            SET8(o, OBJ_anim, 2);
            set_coll(o, 0x0B, 0);
        }
        {
            uint8_t b = OB8(o, F_BEAM);
            SET8(o, F_BEAM, b + 0x80);
            if (b >= 0x80) {                                  /* (the carry: every other frame) [$20F548] */
                uint32_t a1 = spawn(0x39);
                if (a1) {
                    SET8(a1, OBJ_subtype, 0xFF);
                    set_xy(a1, wx(o), wy(o));
                }
            }
        }
        fly_animate(o);
        if (OB8(o, F_IDX) == 10 && getenv("TRILOGY_BOSSAUTOHIT")) {   /* development: as Touch_Enemy's kill */
            SET32(o, 0, S3K_Obj_Explosion);
            SET8(o, OBJ_routine, 0);
            return;
        }
        SET8(o, F_IDX, OB8(o, F_IDX) - 1);
        if (!OB8(o, F_IDX)) SET8(o, OBJ_routine, 0xC);
        break;
    case 0xC:                                                 /* [$20F56A] 16 frames, the room dark again */
        if (!OB8(o, F_IDX)) {
            SET8(o, F_IDX, 0x10);
            SET8(o, OBJ_anim, 0);
        }
        fly_animate(o);
        SET8(o, F_IDX, OB8(o, F_IDX) - 1);
        if (OB8(o, F_IDX)) break;
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_anim, 1);
        room_colours(0);
        break;
    }
    touch_draw(o);                                            /* (no despawn: it is the room's) */
}

/* ======================================================================
 * $3D the curtain's edges
 * ==================================================================== */
#define C_X0     0x30                  /* oVar2A */
#define C_COUNT  0x32                  /* oVar2C */
static void curtain_set(uint32_t o, uint32_t a1)               /* [$20D2E2] */
{
    SET16(a1, C_X0, OB16(o, C_X0));
    set_xy(a1, OB16(o, C_X0) + (OB8(a1, OBJ_subtype) ? -8 : 8), wy(o));
    SET8(a1, OBJ_routine, OB8(a1, OBJ_routine) + 2);
    o_init(a1, SCDA_M3CURTAIN, OB8(a1, OBJ_render) | 4, 2, 8, 0x60);
}

static void curtain(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* [$20D2C0] this the left edge, another the right */
        SET16(o, C_X0, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_subtype, 1);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
        curtain_set(o, o);
        uint32_t a1 = o_alloc();
        if (!a1) { o_delete_respawnable(o); return; }
        SET32(a1, OBJ_code, native3(0x3D));
        curtain_set(o, a1);
        break;
    }
    case 2:                                                   /* [$20D328] the song: shut_flag off */
        if (!(s_fight & 0x80)) break;
        s_shut = 0;
        SET8(o, OBJ_routine, 4);
        break;
    case 4: {                                                 /* [$20D33A] apart, clearing a column each 16 px */
        int x = (int16_t)(wx(o) - 8);
        if (!(x & 15)) {
            SET8(o, C_COUNT, OB8(o, C_COUNT) + 1);
            if (OB8(o, C_COUNT) >= 0x11) { o_delete_respawnable(o); return; }
            for (int k = 0, y = wy(o) - 0x60; k < 12; k++, y += 16) tri_scd_block_write(x, y, 0);
        }
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + (OB8(o, OBJ_subtype) ? -2 : 2));
        break;
    }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ======================================================================
 * $3F the pod
 * ==================================================================== */
static void explode_at(int x, int y)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);                                 /* (no animal: CD's $25 = $FF) */
    set_xy(a1, x, y);
    o_sfx_cd(FM_BOMB);
}

static void bom_set(uint32_t o)                                /* [$20CF94] every other frame, in the arena */
{
    if (OB8(o, E_TIME) & 1) return;
    uint32_t r = (uint32_t)o_random();
    int16_t lo = (int16_t)(r & 0xFFFF), hi = (int16_t)(r >> 16);
    explode_at(hi % 0x80 + 0xF00, lo % 0x50 + 0x180);          /* (divs: the remainder keeps the sign) */
}

static void make_sparks(uint32_t o)                            /* [$20CF62] */
{
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = spawn(0x42);
        if (!a1) return;
        set_lk(a1, S_PAR, o);
        set_xy(a1, wx(o), wy(o));
        SET8(a1, S_SIDE, k);
    }
}

static void spd_set(uint32_t o, uint32_t m)                    /* [$20BFA4] by the blades left */
{
    static const int32_t vx[5] = { 0x14000, 0x1C000, 0x1A000, 0x18000, 0x16000 };
    static const int32_t vy[5] = { 0, 0x10000, 0xE000, 0xC000, 0xA000 };
    static const uint8_t roll[5] = { 4, 2, 3, 4, 5 };
    unsigned n = OB8(o, E_HITS) % 5;
    setL(o, E_VX, vx[n]);
    setL(o, E_VY, vy[n]);
    if (m) SET8(m, M_SPEED, roll[n]);
}

static void meca_clear(uint32_t m, int routine)               /* (clr oVar2D/2A/2B; the shell's routine) */
{
    SET8(m, M_STEP, 0);
    SET8(m, M_TICK, 0);
    SET8(m, M_CNT, 0);
    SET8(m, OBJ_routine, routine);
}
static void meca_clear2(uint32_t m, int routine)              /* (clr oVar2D/2B only) */
{
    SET8(m, M_STEP, 0);
    SET8(m, M_CNT, 0);
    SET8(m, OBJ_routine, routine);
}

static void next_action(uint32_t o)                            /* [$20BFF6] the script by the blades left */
{
    static const uint16_t t1[] = { 18, 316, 20, 0, 16, 2, 30, 0, 32, 0, 6, 120, 20, 0, 16, 2, 44, 0, 46, 0, 48, 0,
                                   20, 0, 0, 0 };
    static const uint16_t t2[] = { 18, 316, 20, 0, 16, 2, 22, 0, 24, 0, 6, 60, 20, 0, 30, 0, 32, 0, 6, 120, 20, 0,
                                   38, 0, 26, 0, 6, 60, 28, 0, 40, 0, 20, 0, 38, 0, 26, 0, 6, 60, 28, 0, 40, 0, 0, 0 };
    static const uint16_t t3[] = { 18, 316, 20, 0, 16, 2, 42, 0, 34, 0, 20, 0, 14, 348, 6, 120, 16, 2, 22, 0, 24, 0,
                                   6, 40, 20, 0, 30, 0, 32, 0, 6, 120, 20, 0, 38, 0, 26, 0, 6, 90, 28, 0, 40, 0,
                                   20, 0, 38, 0, 26, 0, 6, 90, 28, 0, 40, 0, 0, 0 };
    static const uint16_t t4[] = { 18, 316, 20, 0, 16, 2, 42, 0, 34, 0, 20, 0, 14, 348, 6, 120, 16, 2, 22, 0, 24, 0,
                                   6, 40, 20, 0, 30, 0, 32, 0, 6, 120, 20, 0, 38, 0, 26, 0, 6, 120, 28, 0, 40, 0,
                                   20, 0, 38, 0, 26, 0, 6, 120, 28, 0, 40, 0, 0, 0 };
    static const uint16_t *const tbl[5] = { NULL, t1, t2, t3, t4 };
    const uint16_t *t = tbl[OB8(o, E_HITS) % 5];
    if (!t) return;                                           /* (none for 0: it is dead by then) */
    const uint16_t *e = t + OB8(o, E_SCRIPT) * 2;
    if (!e[0]) {
        SET8(o, E_SCRIPT, 0);
        e = t;
    }
    SET8(o, E_SCRIPT, OB8(o, E_SCRIPT) + 1);
    SET8(o, OBJ_routine, e[0]);
    SET16(o, E_PARAM, e[1]);
    SET8(o, E_STEP, 0);
}

static void warai_chk(uint32_t o)                              /* [$20B416] he laughs when the player is hurt */
{
    if (OB8(o, OBJ_anim)) return;
    uint32_t p = RAM_Player_1;
    if (!OB8(p, P_INVULN) && OB8(p, P_ROUTINE) != 6) return;
    SET8(o, E_ANIMT, 0x78);
    SET8(o, OBJ_anim, 4);
}

static void coll_on(uint32_t o, uint32_t m)                    /* [$20B2C8] */
{
    set_coll(o, 0x32, 2);
    set_coll(m, 0x33, 2);
}

static int dev_autohit(void)                                  /* development: a hit every N frames */
{
    static int n = -1;
    if (n < 0) n = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
    return n > 0 && !(r16(RAM_Level_frame_counter) % n);
}

static void colichk(uint32_t o, uint32_t m)                    /* [$20B29E] a spin into the pod or the shell */
{
    uint32_t p = RAM_Player_1;
    if (!btst(o, E_FLAGS, 3) || !m) return;
    SET8(m, M_DROP, 0xFF);
    if (OB8(m, M_BLINK)) {
        SET8(m, M_BLINK, OB8(m, M_BLINK) - 1);
        if (!OB8(m, M_BLINK)) coll_on(o, m);
        return;
    }
    if (dev_autohit()) SET8(o, OBJ_coll_flags, 0);
    if (OB8(o, OBJ_coll_flags) && OB8(m, OBJ_coll_flags)) return;
    if (OB8(o, OBJ_routine) == 0x1E) { coll_on(o, m); return; }   /* (firing: no) */
    set_coll(o, 0, 0);
    set_coll(m, 0, 0);
    make_sparks(o);
    SET8(o, E_ANIMT, 0x96);
    SET8(o, OBJ_anim, 1);
    SET8(m, M_BLINK, 0x78);
    SET8(o, E_SCRIPT, 0);
    SET8(o, E_STEP, 0);
    bset(o, E_FLAGS, 0);
    o_sfx_cd(FM_BOSSHIT);
    uint8_t n = (uint8_t)(OB8(o, E_HITS) - 1);
    SET8(o, E_HITS, n);
    spd_set(o, m);
    if (!n) {                                                 /* (the last: dead) */
        SET8(o, OBJ_routine, 0x32);
        bclr(o, E_FLAGS, 0);
        SET8(o, E_TIME, 0);
        SET8(o, E_TIME2, 0);
        SET8(m, M_DROP, 0);
        return;
    }
    uint8_t f = OB8(m, M_FRAME);                              /* (the blade off: the one at the player's side) */
    int left = wx(p) < wx(o);
    uint8_t d = 0;
    switch (n) {
    case 3:
        if (left) d = f <= 3 ? 2 : f <= 7 ? 1 : f <= 0xB ? 0 : 3;
        else d = f >= 0xD || f == 0 ? 2 : f <= 4 ? 1 : f <= 8 ? 0 : 3;
        break;
    case 2:
        if (left) d = f >= 0xD || f <= 2 ? 2 : f <= 7 ? 1 : 0;
        else d = f >= 0xC || f == 0 ? 2 : f <= 4 ? 1 : 0;
        break;
    case 1:                                                   /* (PLC 5: Amy's art, over blade sets 3-5') */
        o_upload_asset(SCDA_M3AMY);
        d = left ? 1 : 0;
        break;
    default:
        SET8(o, E_HITS, 0);
        return;
    }
    SET8(m, M_DROP, d);
}

static void make_meca(uint32_t o)                              /* [$20CF00] */
{
    uint32_t a1 = spawn_after(o, 0x41);
    if (!a1) return;
    set_lk(a1, M_EGG, o);
    set_lk(o, E_MECA, a1);
    set_xy(a1, wx(o), wy(o));
}

/* the moves: toward the parameter ([$20B602] [$20B622] [$20B642] [$20B662]) */
static void move_r(uint32_t o)
{
    addL(o, OBJ_x_pos, L(o, E_VX));
    if (OBS16(o, E_PARAM) <= wx(o)) { SET16(o, OBJ_x_pos, OB16(o, E_PARAM)); bset(o, E_FLAGS, 0); }
}
static void move_l(uint32_t o)
{
    addL(o, OBJ_x_pos, -L(o, E_VX));
    if (OBS16(o, E_PARAM) >= wx(o)) { SET16(o, OBJ_x_pos, OB16(o, E_PARAM)); bset(o, E_FLAGS, 0); }
}
static void move_d(uint32_t o)
{
    addL(o, OBJ_y_pos, L(o, E_VY));
    if (OBS16(o, E_PARAM) <= wy(o)) { SET16(o, OBJ_y_pos, OB16(o, E_PARAM)); bset(o, E_FLAGS, 0); }
}
static void move_u(uint32_t o)
{
    addL(o, OBJ_y_pos, -L(o, E_VY));
    if (OBS16(o, E_PARAM) >= wy(o)) { SET16(o, OBJ_y_pos, OB16(o, E_PARAM)); bset(o, E_FLAGS, 0); }
}

static void tobi_set(uint32_t o)                               /* [$20BF4A] a hop: half a turn round an ellipse */
{
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - OB16(o, E_DX));
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - OB16(o, E_DY));
    uint16_t a = (uint16_t)(OB16(o, E_TIME) + 0x230);
    if (a >= 0x8000) a = 0x8000;
    SET16(o, E_TIME, a);
    int16_t s, c;
    o_sincos((uint8_t)(a >> 8), &s, &c);
    if (!(OB8(o, E_STEP) & 1)) c = (int16_t)-c;
    int16_t d1 = (int16_t)((int16_t)(c * 0x29) >> 8), d0 = (int16_t)((int16_t)(s * -0x68) >> 8);
    SET16(o, E_DX, d1);
    SET16(o, E_DY, d0);
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + d1);
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
}

static int32_t lunge_speed(int16_t d)                          /* (d * 16 / 90) << 12 */
{
    return (int32_t)((int16_t)((int32_t)(int16_t)(d * 16) / 0x5A)) * 0x1000;
}

static void egg_step(uint32_t o, uint32_t m)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20B442] egg8_ini */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_M3EGG, 4, 5, 0x18, 0x10);
        SET8(o, E_HITS, 4);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x100);
        break;
    case 2: {                                                 /* [$20B47C] egg8_scrset: the arena locks at $F00 */
        w16(RAM_Camera_max_X_pos, 0xE60);
        w16(RAM_Camera_target_max_X_pos, 0xE60);
        int16_t d0 = (int16_t)(wx(p) - 0xA0);
        if (d0 < (int16_t)r16(RAM_Camera_min_X_pos)) break;
        if (wx(p) >= 0xF00) {
            s_flags = 8;
            s_fight = 8;
            w8(RAM_Boss_flag, 1);
            make_meca(o);
            spd_set(o, lk(o, E_MECA));
            bclr(o, E_FLAGS, 1);
            SET8(o, OBJ_routine, 4);
            d0 = 0xE60;
        }
        w16(RAM_Camera_min_X_pos, (uint16_t)d0);
        w16(RAM_Camera_target_min_X_pos, (uint16_t)d0);
        break;
    }
    case 4:                                                   /* [$20B4E4] egg8_open1 */
        switch (OB8(o, E_STEP)) {
        default:
            SET8(o, OBJ_anim, 5);
            SET8(o, E_STEP, OB8(o, E_STEP) + 1);
            line1(SCDA_M3BOSSPAL, RAM_Target_palette);         /* (palette 7: the fade's target) */
            for (unsigned k = 0; k < 16; k++)                 /* (8, 9: black, its last colour $00E) */
                w16(pal_at(16 + (int)k), k == 15 ? 0x00E : 0);
            if (cd_period() == TRI_CD_BAD)                    /* (R83D's 9: line 3 black) */
                for (unsigned k = 0; k < 16; k++) w16(pal_at(48 + (int)k), 0);
            for (int k = SCDA_M3EGG; k <= SCDA_M3SPARK; k++)   /* (PLC 4: the fight's art, over the level */
                o_upload_asset(k);                            /*  objects' now out of sight: scd_level.py) */
            /* fall through */
        case 1:
            SET16(o, E_TIME, OB16(o, E_TIME) + 1);
            if (OB16(o, E_TIME) != 0xF0) break;
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x100);  /* (down into sight; the song) */
            scd_cd_play(SCD_CD_FINAL, 1);
            s_fight |= 0x80;
            SET8(o, E_TIME, 0);
            SET8(o, E_TIME2, 0);
            SET8(o, E_STEP, 2);
            SET8(o, OBJ_anim, 2);
            break;
        case 2:                                               /* line 1 fades in, a step each 3 frames */
            if (!((0x200u + OB8(o, E_TIME)) % 3)) fade_boss8();   /* (d0's $02xx: divu) */
            SET8(o, E_TIME, OB8(o, E_TIME) + 1);
            if (OB8(o, E_TIME) != 0x3F) break;
            SET8(o, E_STEP, 3);
            SET8(o, OBJ_anim, 0);
            SET8(o, E_TIME, 0);
            break;
        case 3:
            SET8(o, E_TIME, OB8(o, E_TIME) + 1);
            if (OB8(o, E_TIME) != 0x28) break;
            bset(o, E_FLAGS, 0);
            SET8(o, E_TIME, 0);
            bset(o, E_FLAGS, 3);
            coll_on(o, m);
            break;
        }
        break;
    case 6:                                                   /* [$20B5E6] egg8_wait */
        SET8(o, E_TIME, OB8(o, E_TIME) + 1);
        if (OB8(o, E_PARAMB) != OB8(o, E_TIME)) break;
        SET8(o, E_TIME, 0);
        bset(o, E_FLAGS, 0);
        break;
    case 8: move_r(o); break;
    case 0xA: move_l(o); break;
    case 0xC: move_d(o); break;
    case 0xE: move_u(o); break;
    case 0x10: {                                              /* [$20B682] egg8_move_c: across, param times */
        uint8_t st = OB8(o, E_STEP);
        int turn = 0;
        if (!st) {
            SET8(o, E_STEP, 2);
            if (wx(o) > 0xF00) { SET8(o, E_STEP, 2); SET16(o, E_DX, 0xE84); }
            else { SET8(o, E_STEP, 3); SET16(o, E_DX, 0xF7C); }
            break;
        }
        if (!(st & 1)) {
            addL(o, OBJ_x_pos, -L(o, E_VX));
            if (OBS16(o, E_DX) < wx(o)) break;
        } else {
            addL(o, OBJ_x_pos, L(o, E_VX));
            if (OBS16(o, E_DX) > wx(o)) break;
        }
        SET16(o, OBJ_x_pos, OB16(o, E_DX));
        if (!OB16(o, E_PARAM)) { bset(o, E_FLAGS, 0); break; }
        SET16(o, E_PARAM, OB16(o, E_PARAM) - 1);
        if (!OB16(o, E_PARAM)) {                              /* (the last: to the middle) */
            SET8(o, E_STEP, OB8(o, E_STEP) ^ 1);
            SET16(o, E_DX, 0xF00);
            break;
        }
        turn = !(OB8(o, E_STEP) & 1);
        SET8(o, E_STEP, OB8(o, E_STEP) ^ 1);
        if (turn) { SET8(o, E_STEP, OB8(o, E_STEP) | 1); SET16(o, E_DX, 0xF7C); }
        else { SET8(o, E_STEP, OB8(o, E_STEP) & ~1); SET16(o, E_DX, 0xE84); }
        break;
    }
    case 0x12:                                                /* [$20B71E] egg8_move_escu: up fast */
        if (!OB8(o, E_STEP)) {
            meca_clear(m, 8);                                 /* (egg8_hane_chg3) */
            SET8(o, E_STEP, 1);
        }
        addL(o, OBJ_y_pos, -0x14000);
        if (OBS16(o, E_PARAM) >= wy(o)) { SET16(o, OBJ_y_pos, OB16(o, E_PARAM)); bset(o, E_FLAGS, 0); }
        break;
    case 0x14:                                                /* [$20B754] egg8_hane_roll */
        if (!btst(m, M_STATE, 6)) {
            bset(m, M_FLAGS, 4);
            SET8(m, M_STEP, 0);
            SET8(m, M_CNT, 0);
        }
        bset(o, E_FLAGS, 0);
        break;
    case 0x16:                                                /* [$20B772] egg8_hane_stop */
        if (!OB8(o, E_STEP)) {
            SET8(o, E_STEP, 1);
            bset(m, M_FLAGS, 4);
            bclr(m, M_STATE, 7);
            SET8(m, M_STEP, 0);
            SET8(m, M_CNT, 0);
            SET8(m, M_TARGET, OB8(o, E_HITS) == 2 ? 8 : 4);
        }
        if (btst(m, M_STATE, 7)) bset(o, E_FLAGS, 0);
        break;
    case 0x18:                                                /* [$20B7BE] egg8_hane_chg1: the shell opens */
        meca_clear(m, 4);
        SET8(m, M_STATE, (OB8(m, M_STATE) | 0xA0) & ~0x40);
        SET8(o, E_TIME, 0);
        SET8(o, E_TIME2, 0);
        bset(o, E_FLAGS, 0);
        break;
    case 0x1A:                                                /* [$20B7F2] egg8_hane_chg2: spread */
        o_sfx_cd(FM_SPREAD);
        meca_clear(m, 6);
        bset(o, E_FLAGS, 0);
        break;
    case 0x1C:                                                /* [$20B816] egg8_hane_chg3: in */
        meca_clear(m, 8);
        bset(o, E_FLAGS, 0);
        break;
    case 0x1E:                                                /* [$20B830] egg8_hane_fire */
        if (OB8(o, E_STEP) == 1) {
            SET16(o, E_PARAM, 0x13C);
            move_u(o);
            if (bclr_was(o, E_FLAGS, 0)) SET8(o, E_STEP, 2);
        } else if (OB8(o, E_STEP) == 2) {
            if ((uint8_t)(OB8(o, E_HITS) * 2) == OB8(m, M_FIRED)) bset(o, E_FLAGS, 0);
        } else {
            SET8(o, E_STEP, OB8(o, E_STEP) + 1);
            meca_clear(m, 0xA);
            SET8(m, M_FIRED, 0);
            SET8(m, M_UP, 0);
        }
        break;
    case 0x20:                                                /* [$20B890] egg8_hane_reset */
        meca_clear(m, 0xC);
        bset(o, E_FLAGS, 0);
        break;
    case 0x22: case 0x24: {                                   /* [$20B978] egg8_spin_r / _l */
        int8_t st = (int8_t)OB8(o, E_STEP);
        if (st >= 5) {                                        /* [$20BAC8] done */
            meca_clear2(m, 2);
            SET8(o, E_STEP, 0);
            SET8(o, E_TIME, 0);
            SET8(o, E_TIME2, 0);
            bset(o, E_FLAGS, 0);
            break;
        }
        if (st < 0) {                                         /* (a rest, 60 frames) */
            SET8(o, E_TIME, OB8(o, E_TIME) + 1);
            if (OB8(o, E_TIME) == 0x3C) {
                SET8(o, E_TIME, 0);
                SET8(o, E_STEP, OB8(o, E_STEP) & 0x7F);
            }
            break;
        }
        if (!st) {
            SET8(o, E_TIME, 0);
            SET8(o, E_PARAM, 0);
            int left = OB8(o, E_HITS) == 4 ? wx(p) < wx(o) : wx(o) >= 0xF00;   /* (step 1: leftward) */
            SET8(o, E_TIME2, left ? 7 : 0);
            SET8(o, E_STEP, left ? 1 : 2);
            meca_clear2(m, 0xE);
        }
        SET8(o, E_TIME, OB8(o, E_TIME) + 1);
        if (OB8(o, E_TIME) != 0x14) break;
        SET8(o, E_PARAM, OB8(o, E_PARAM) + 1);
        if (!(OB8(o, E_PARAM) & 1)) tri_scd_voice(SCD_PCM_STOMP);
        SET8(o, E_TIME, 0);
        {
            static const int8_t a[16][2] = {                  /* [$20BB34] */
                { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 }, { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 },
                { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 }, { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 },
            };
            static const int8_t b[16][2] = {                  /* [$20BAF4] (three blades left) */
                { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 }, { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 },
                { 26, -4 }, { 26, 6 }, { 26, -6 }, { 26, 4 }, { 26, -4 }, { 26, 6 }, { 21, 15 }, { 26, 22 },
            };
            const int8_t *e = (OB8(o, E_HITS) == 3 ? b : a)[OB8(o, E_TIME2) & 15];
            int d0 = e[0], d1 = e[1];
            if (OB8(o, E_STEP) & 1) { bset(m, M_FLAGS, 6); d0 = -d0; d1 = -d1; }
            else bset(m, M_FLAGS, 5);
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + d0);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        }
        {
            uint8_t n = OB8(o, E_PARAM);
            int rest = OB8(o, E_HITS) == 4 ? n == 4 || n == 0xC : n == 4 || n == 8;
            if (rest) {
                if ((int8_t)OB8(o, E_STEP) >= 3) SET8(o, E_STEP, 0x85);
                else { SET8(o, E_STEP, OB8(o, E_STEP) == 1 ? 0x84 : 0x83); break; }
            }
        }
        SET8(o, E_TIME2, OB8(o, E_TIME2) + (OB8(o, E_STEP) & 1 ? -1 : 1));
        break;
    }
    case 0x26: {                                              /* [$20BB74] egg8_target: a lunge at the player */
        if (!OB8(o, E_STEP)) {                                /* [$20BBFA] */
            bset(o, E_STEP, 0);
            int16_t d0 = (int16_t)(wx(p) - wx(o)), d2 = d0, d1 = d0 < 0 ? (int16_t)-d0 : d0;
            bset(m, M_FLAGS, 4);
            bclr(m, M_STATE, 7);
            SET8(m, M_STEP, 0);
            SET8(m, M_CNT, 0);
            if (d1 < 0x40) {
                setL(o, E_VX, lunge_speed(d0));
                setL(o, E_VY, lunge_speed((int16_t)(0x17C - wy(o))));
                SET8(m, M_TARGET, OB8(o, E_HITS) == 2 ? 6 : 4);
            } else {
                d0 = (int16_t)(d2 < 0 ? d0 + 0x10 : d0 - 0x10);
                setL(o, E_VX, lunge_speed(d0));
                setL(o, E_VY, lunge_speed((int16_t)(0x18A - wy(o))));
                SET8(m, M_TARGET, OB8(o, E_HITS) == 2 || d2 < 0 ? 6 : 2);
            }
        }
        if (!btst(o, E_STEP, 1) && btst(m, M_STATE, 7)) bset(o, E_STEP, 1);
        if (!btst(o, E_STEP, 2)) {                            /* [$20BBB4] */
            int16_t d1 = OB8(m, M_TARGET) & 3 ? 0x18A : 0x17C;
            addL(o, OBJ_x_pos, L(o, E_VX));
            addL(o, OBJ_y_pos, L(o, E_VY));
            if (d1 <= wy(o)) {
                SET16(o, OBJ_y_pos, d1);
                setL(o, E_VX, -L(o, E_VX));
                setL(o, E_VY, -L(o, E_VY));
                bset(o, E_STEP, 2);
            }
        }
        if (OB8(o, E_STEP) == 7) bset(o, E_FLAGS, 0);
        break;
    }
    case 0x28:                                                /* [$20BCD2] egg8_targetreset: back up */
        addL(o, OBJ_x_pos, L(o, E_VX));
        addL(o, OBJ_y_pos, L(o, E_VY));
        if (wy(o) > 0x13C) break;
        SET16(o, OBJ_y_pos, 0x13C);
        SET8(o, E_STEP, 0);
        bset(o, E_FLAGS, 0);
        spd_set(o, m);
        break;
    case 0x2A: {                                              /* [$20B8AA] egg8_spin_d: to a side, down */
        uint8_t st = OB8(o, E_STEP);
        int down = 0;
        if (st == 3) down = 2;
        else if (st == 1 || st == 2) {
            if (st == 1) move_r(o); else move_l(o);
            if (bclr_was(o, E_FLAGS, 0)) down = 1;
        } else {
            if (OB8(o, E_HITS) == 4) {
                SET8(m, M_TARGET, 8);
                down = 1;
            } else if (wx(p) < wx(o)) {
                SET8(m, M_TARGET, 8);
                SET16(o, E_PARAM, 0xF7C);
                SET8(o, E_STEP, 1);
                move_r(o);
                if (bclr_was(o, E_FLAGS, 0)) down = 1;
            } else {
                SET8(m, M_TARGET, OB8(o, E_HITS) == 3 ? 0 : 4);
                SET16(o, E_PARAM, 0xE84);
                SET8(o, E_STEP, 2);
                move_l(o);
                if (bclr_was(o, E_FLAGS, 0)) down = 1;
            }
        }
        if (down == 1) {                                      /* [$20B93E] */
            bset(m, M_FLAGS, 4);
            bclr(m, M_STATE, 7);
            SET8(m, M_STEP, 0);
            SET8(m, M_CNT, 0);
            SET16(o, E_PARAM, 0x1A0);
            SET8(o, E_STEP, 3);
            down = 2;
        }
        if (down == 2) {                                      /* [$20B95E] */
            move_d(o);
            if (bclr_was(o, E_FLAGS, 0)) {
                SET8(o, E_STEP, 0);
                bset(o, E_FLAGS, 0);
            }
        }
        break;
    }
    case 0x2C: {                                              /* [$20BD02] egg8_tobi_d: to a side, a hop down */
        uint8_t st = OB8(o, E_STEP);
        if (st < 1 || st > 5) {
            if (wx(p) >= wx(o)) { SET16(o, E_PARAM, 0xE84); SET8(o, E_STEP, 2); }
            else { SET16(o, E_PARAM, 0xF7C); SET8(o, E_STEP, 1); }
            st = OB8(o, E_STEP);
        }
        if (st == 1 || st == 2) {
            if (st == 1) move_r(o); else move_l(o);
            if (!bclr_was(o, E_FLAGS, 0)) break;
            bset(m, M_FLAGS, 4);
            bclr(m, M_STATE, 7);
            SET8(m, M_TARGET, 8);
            SET8(m, M_STEP, 0);
            SET8(m, M_CNT, 0);
            SET8(o, E_STEP, st = 3);
        }
        if (st == 3) {
            if (!btst(m, M_STATE, 7)) break;
            SET16(o, E_PARAM, 0x138);
            SET8(o, E_STEP, st = 7);                          /* (addq #4: 7, and on into 4's code) */
        }
        if (st == 4 || st == 7) {
            move_d(o);
            if (!bclr_was(o, E_FLAGS, 0)) break;
            SET8(o, E_STEP, st = 5);
            SET16(o, E_DX, 0);
            SET16(o, E_DY, 0xFF98);
            SET16(o, E_TIME, 0x4000);
        }
        tobi_set(o);                                          /* (5) */
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - OB16(o, E_DX));
        SET16(o, E_DX, 0);
        if (OB16(o, E_TIME) != 0x8000) break;
        SET8(o, E_STEP, 0);
        bset(o, E_FLAGS, 0);
        o_sfx_cd(FM_THUMP);
        break;
    }
    case 0x2E: {                                              /* [$20BDF6] egg8_tobi: cartwheels across */
        if ((int8_t)OB8(o, E_STEP) >= 5) goto tobi_end;
        if (!OB8(o, E_STEP)) {
            SET16(o, E_DY, 0);
            SET16(o, E_TIME, 0);
            if (wx(o) >= 0xF00) { SET16(o, E_DX, 0x29); SET8(o, E_STEP, 1); }
            else { SET16(o, E_DX, 0xFFD7); SET8(o, E_STEP, 2); }
            meca_clear2(m, 0x12);
            SET8(m, M_SET, 2);
        }
        tobi_set(o);
        if (OB16(o, E_TIME) != 0x8000) break;
        o_sfx_cd(FM_THUMP);
        SET16(o, E_DY, 0);
        SET16(o, E_TIME, 0);
        {
            int16_t d0;
            if (OB8(o, E_STEP) & 1) { SET16(o, E_DX, 0x29); d0 = (int16_t)(wx(o) - 0xE84); }
            else { SET16(o, E_DX, 0xFFD7); d0 = (int16_t)(wx(o) - 0xF7C); }
            if (d0 < 0) d0 = (int16_t)-d0;
            if (d0 >= 0x29) break;
        }
        SET16(o, E_DX, (uint16_t)-OBS16(o, E_DX));
        if ((int8_t)OB8(o, E_STEP) < 3) {
            SET8(o, E_STEP, OB8(o, E_STEP) == 1 ? 4 : 3);
            break;
        }
    tobi_end:                                                 /* [$20BECC] */
        SET8(o, E_STEP, 0);
        bset(o, E_FLAGS, 0);
        meca_clear2(m, 2);
        SET16(m, M_RAD, 0);
        break;
    }
    case 0x30:                                                /* [$20BEEC] egg8_tobi_u: a hop up */
        if (!OB8(o, E_STEP)) {
            SET8(o, E_STEP, 1);
            SET16(o, E_DX, 0);
            SET16(o, E_DY, 0);
            SET16(o, E_TIME, 0);
        }
        tobi_set(o);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - OB16(o, E_DX));
        SET16(o, E_DX, 0);
        if (OBS16(o, E_TIME) < 0x4000) break;
        SET8(o, E_STEP, 0);
        SET8(o, E_TIME, 0);
        SET8(o, E_TIME2, 0);
        SET16(o, E_DX, 0);
        SET16(o, E_DY, 0);
        bset(o, E_FLAGS, 0);
        break;
    case 0x32:                                                /* [$20C1B2] egg8_dead */
        if (!OB8(o, E_TIME)) {
            SET8(o, OBJ_anim, 3);
            SET8(o, E_ANIMT, 0);
            if (m) { SET8(m, OBJ_routine, 0x10); SET8(m, OBJ_frame, 3); }
            set_coll(o, 0, 0);
            set_coll(m, 0, 0);
            bclr(o, E_FLAGS, 3);
        }
        SET8(o, E_TIME, OB8(o, E_TIME) + 1);
        if (OB8(o, E_TIME) < 0x5A) { bom_set(o); break; }
        {
            uint32_t a1 = spawn(0x43);                        /* (Amy) */
            if (!a1) break;
            set_xy(a1, 0xE48, 0x1CC);
            SET8(o, OBJ_routine, 0x34);
        }
        break;
    case 0x34: {                                              /* [$20C222] egg8_esc: off right, burning */
        if (!OB8(o, E_STEP)) {
            add_points(100);
            SET8(o, E_TIME2, 0);
            SET16(o, E_DY, 0);
            SET8(o, E_STEP, 1);
        }
        SET8(o, E_TIME, OB8(o, E_TIME) + 1);
        bom_set(o);
        if (!(OB8(o, E_TIME) % 0x3C)) make_sparks(o);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - OB16(o, E_DY));
        addL(o, OBJ_y_pos, 0x4000);
        uint8_t a = OB8(o, E_TIME2);
        SET8(o, E_TIME2, a + 3);
        int16_t s, c;
        o_sincos(a, &s, &c);
        int16_t d0 = (int16_t)((int16_t)(s * 4) >> 8);
        SET16(o, E_DY, d0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
        addL(o, OBJ_x_pos, L(o, E_VX));
        if (wx(o) < 0xFE0) break;
        line1(SCDA_M3ENDPAL, RAM_Normal_palette);             /* (palette 11: Amy's line 1) */
        SET8(o, OBJ_routine, 0x36);
        SET8(o, E_TIME, 0);
        s_fight = 0;
        w8(RAM_Boss_flag, 0);
        break;
    }
    case 0x36:                                                /* [$20C2C4] egg8_esc2: out of sight, burning */
        SET8(o, E_TIME, OB8(o, E_TIME) + 1);
        bom_set(o);
        break;
    }
}

static void egg8(uint32_t o)
{
    uint32_t m = lk(o, E_MECA);
    events();
    if (m) {                                                  /* (the shell's steps: this frame's only) */
        bclr(m, M_FLAGS, 5);
        bclr(m, M_FLAGS, 6);
    }
    warai_chk(o);
    if (OB8(o, E_ANIMT)) {
        SET8(o, E_ANIMT, OB8(o, E_ANIMT) - 1);
        if (!OB8(o, E_ANIMT)) SET8(o, OBJ_anim, 0);
    }
    colichk(o, m);
    if (bclr_was(o, E_FLAGS, 0)) next_action(o);
    egg_step(o, m);
    m = lk(o, E_MECA);
    o_animate(o, o_asset_anim(SCDA_M3EGG));
    palcycle_boss8(o);
    static int log = -1;
    if (log < 0) log = getenv("TRILOGY_BOSSLOG") != NULL;     /* development */
    if (log && !(r16(RAM_Level_frame_counter) & 7))
        fprintf(stderr, "[mmz3] f=%u r=%02X st=%02X hits=%u egg=%04X,%04X coll=%02X/%02X p=%04X,%04X\n",
                (unsigned)r16(RAM_Level_frame_counter), OB8(o, OBJ_routine), OB8(o, E_STEP), OB8(o, E_HITS),
                OB16(o, OBJ_x_pos), OB16(o, OBJ_y_pos), OB8(o, OBJ_coll_flags), m ? OB8(m, OBJ_coll_flags) : 0xFF,
                OB16(RAM_Player_1, OBJ_x_pos), OB16(RAM_Player_1, OBJ_y_pos));
    if (m && (OB8(m, M_BLINK) & 1)) return;                   /* (blinking after a hit) */
    touch_draw(o);
}

/* ======================================================================
 * $41 the shell
 * ==================================================================== */
static void make_blades(uint32_t o)                            /* [$20CF28] */
{
    for (int k = 0; k < 4; k++) {
        uint32_t a1 = spawn_after(o, 0x40);
        if (!a1) return;
        set_lk(a1, H_MECA, o);
        SET8(a1, H_NUM, k);
        set_xy(a1, wx(o), wy(o));
        SET8(a1, OBJ_frame, k * 4);
    }
}

static void futa_roll(uint32_t o)                              /* [$20C624] the shell's frame turns */
{
    SET8(o, M_CNT, OB8(o, M_CNT) + 1);
    if (OB8(o, M_CNT) == 6) SET8(o, OBJ_frame, 2);
    else if (OB8(o, M_CNT) == 0xC) { SET8(o, M_CNT, 0); SET8(o, OBJ_frame, 1); }
}

static void roll_tick(uint32_t o)                              /* [$20C608] a blade step each M_SPEED frames */
{
    SET8(o, M_TICK, OB8(o, M_TICK) + 1);
    if ((int8_t)OB8(o, M_TICK) < (int8_t)OB8(o, M_SPEED)) return;
    SET8(o, M_TICK, 0);
    bset(o, M_FLAGS, 6);
}

static void hane_roll(uint32_t o)                              /* [$20C5E0] */
{
    if (!btst(o, M_STATE, 5)) {
        SET8(o, OBJ_frame, 1);
        SET8(o, M_CNT, OB8(o, M_CNT) + 1);
        if (OB8(o, M_CNT) != 0xA) return;
        SET8(o, M_CNT, 0);
        bset(o, M_STATE, 5);
    }
    futa_roll(o);
    roll_tick(o);
}

static void hane_stop(uint32_t o)                              /* [$20C596] */
{
    if (btst(o, M_STATE, 7)) return;
    if (!btst(o, M_STATE, 5)) {
        SET8(o, OBJ_frame, 1);
        SET8(o, M_CNT, OB8(o, M_CNT) + 1);
        if (OB8(o, M_CNT) == 0xA) {
            bset(o, M_STATE, 5);
            SET8(o, M_CNT, 0);
        }
    } else if (OB8(o, M_FRAME) == OB8(o, M_TARGET)) {
        bset(o, M_STATE, 7);
        SET8(o, OBJ_frame, 0);
        return;
    }
    roll_tick(o);
}

static void hane_ctrl(uint32_t o)                              /* [$20C562] */
{
    if (bclr_was(o, M_FLAGS, 4)) {
        SET8(o, M_CNT, 0);
        bclr(o, M_STATE, 7);
        bclr(o, M_STATE, 5);
        int was = btst(o, M_STATE, 6);
        SET8(o, M_STATE, OB8(o, M_STATE) ^ 0x40);
        if (!was) hane_roll(o); else hane_stop(o);
        return;
    }
    if (btst(o, M_STATE, 6)) hane_roll(o); else hane_stop(o);
}

static void meca(uint32_t o)
{
    uint32_t e = lk(o, M_EGG), p = RAM_Player_1;
    if (!is(e, 0x3F)) { o_delete(o); return; }                /* (S3K reuses slots: the pod gone, this too) */
    int follow = 1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20C310] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_M3MECA, 4, 5, 0x24, 0x24);
        SET16(o, M_RAD, 0);
        SET8(o, M_DROP, 0xFF);
        make_blades(o);
        SET8(o, M_STATE, (OB8(o, M_STATE) | 0xA0) & ~0x40);
        follow = 0;
        break;
    case 2:                                                   /* [$20C360] */
        set_xy(o, wx(e), wy(e));
        hane_ctrl(o);
        follow = 0;
        break;
    case 4:                                                   /* [$20C384] the shell opens (it stays put) */
        follow = 0;
        if (!OB8(o, M_STEP)) {
            SET8(o, M_SET, 1);
            SET16(o, M_RAD, 0);
            SET8(o, M_STEP, 1);
        }
        if (OB8(o, M_STEP) != 1 || OBS16(o, M_RAD) > 0xAA) {
            SET8(o, M_STEP, OB8(o, M_STEP) + 1);
            if (OB8(o, M_STEP) == 2) SET8(o, M_SET, 3);
            else if (OB8(o, M_STEP) == 6) SET8(o, M_SET, 4);
        }
        SET16(o, M_RAD, OB16(o, M_RAD) + 8);
        if (OBS16(o, M_RAD) >= 0x100) {
            SET16(o, M_RAD, 0x100);
            SET8(o, M_STEP, 0);
            SET8(o, OBJ_routine, 2);
            SET8(o, M_SET, 4);
        }
        break;
    case 6:                                                   /* [$20C3F8] spread (it stays put) */
        follow = 0;
        if (!OB8(o, M_STEP)) { SET8(o, M_SET, 1); SET8(o, M_STEP, 1); }
        SET16(o, M_RAD, OB16(o, M_RAD) + 0x10);
        if (OBS16(o, M_RAD) >= 0xEC) SET8(o, M_SET, 2);
        if (OBS16(o, M_RAD) >= 0x100) {
            SET16(o, M_RAD, 0x100);
            SET8(o, M_STEP, 0);
            SET8(o, OBJ_routine, 2);
        }
        break;
    case 8:                                                   /* [$20C438] in */
        if (!OB8(o, M_STEP)) { SET8(o, M_SET, 2); SET8(o, M_STEP, 1); }
        SET16(o, M_RAD, OB16(o, M_RAD) - 6);
        if (OBS16(o, M_RAD) < 0xEC) SET8(o, M_SET, 1);
        if (OBS16(o, M_RAD) <= 0) {
            SET16(o, M_RAD, 0);
            SET8(o, M_STEP, 0);
            SET8(o, OBJ_routine, 2);
            SET8(o, M_SET, 0);
        }
        break;
    case 0xA:                                                 /* [$20C488] the blades fired: all landed? */
        if (OB8(e, E_HITS) != OB8(o, M_FIRED)) {
            set_xy(o, wx(e), wy(e));
            hane_roll(o);
            follow = 0;
            break;
        }
        SET8(o, M_PAT, ((OB16(p, OBJ_x_pos) + OB8(p, OBJ_anim_timer)) & 3) * 8);
        SET8(o, OBJ_routine, 2);
        bset(o, M_FLAGS, 4);
        break;
    case 0xC:                                                 /* [$20C4C8] the blades back, from far out */
        if (OB8(o, M_STEP) != 1) {
            bset(o, M_FLAGS, 3);
            SET8(o, M_STEP, OB8(o, M_STEP) + 1);
            SET16(o, M_RAD, 0xFF00);
            SET8(o, M_STATE, (OB8(o, M_STATE) | 0xA0) & ~0x40);
        }
        SET16(o, M_RAD, OB16(o, M_RAD) + 6);
        if (OBS16(o, M_RAD) < 0) break;
        bclr(o, M_FLAGS, 3);
        SET16(o, M_RAD, 0);
        SET8(o, M_TICK, 0);
        SET8(o, M_STEP, 0);
        SET8(o, OBJ_routine, 2);
        break;
    case 0xE:                                                 /* [$20C372] the spin */
        set_xy(o, wx(e), wy(e));
        futa_roll(o);
        follow = 0;
        break;
    case 0x10:                                                /* [$20C528] dead */
        break;
    case 0x12: {                                              /* [$20C536] the hop: radius by height */
        set_xy(o, wx(e), wy(e));
        int16_t d0 = (int16_t)(0x1A0 - wy(e));
        if (d0 > 0x24) d0 = 0x24;
        SET16(o, M_RAD, (uint16_t)(((uint16_t)(d0 << 8)) / 0x24));
        follow = 0;
        break;
    }
    }
    if (follow) set_xy(o, wx(e), wy(e));
    if (OB8(o, M_BLINK) & 1) return;
    touch_draw(o);
}

/* ======================================================================
 * $40 a blade
 * ==================================================================== */
static uint32_t blade_maps(int set) { return o_asset_maps(SCDA_M3BLADE + set); }
static int blade_set_is(uint32_t o, int set) { return r32(o + OBJ_mappings) == blade_maps(set); }

static void offs_set(uint32_t o, uint32_t m)                   /* [$20CBEA] its place round the shell; its touch */
{
    static const int8_t ring[16][5] = {                       /* (x, y, x out, y out, touch) sets 0-2 */
        { 0, -40, 0, -36, 0x34 }, { 19, -36, 18, -31, 0x35 }, { 31, -32, 25, -25, 0x36 }, { 38, -16, 31, -18, 0x37 },
        { 40, 0, 36, 0, 0x38 }, { 38, 16, 31, 18, 0x37 }, { 31, 32, 25, 25, 0x36 }, { 19, 36, 18, 31, 0x35 },
        { 0, 40, 0, 36, 0x34 }, { -19, 36, -18, 31, 0x35 }, { -31, 32, -25, 25, 0x36 }, { -38, 16, -31, 18, 0x37 },
        { -40, 0, -36, 0, 0x38 }, { -38, -16, -31, -18, 0x37 }, { -31, -32, -25, -25, 0x36 }, { -19, -36, -18, -31, 0x35 },
    };
    static const int8_t fold[16][5] = {                       /* set 3 (the shell opening) */
        { 0, -32, 0, -36, 0x39 }, { 0 }, { 0 }, { 0 }, { 32, 0, 36, 0, 0x3A }, { 0 }, { 0 }, { 0 },
        { 0, 32, 0, 36, 0x39 }, { 0 }, { 0 }, { 0 }, { -32, 0, -36, 0, 0x3A }, { 0 }, { 0 }, { 0 },
    };
    static const int8_t open[16][5] = {                       /* sets 4-5 */
        { 0, -32, 0, -36, 0x3B }, { 12, -29, 18, -31, 0x3C }, { 23, -23, 25, -25, 0x3D }, { 29, -12, 31, -18, 0x3E },
        { 32, 0, 36, 0, 0x3F }, { 29, 12, 31, 18, 0x3E }, { 23, 23, 25, 25, 0x3D }, { 12, 29, 18, 31, 0x3C },
        { 0, 32, 0, 36, 0x3B }, { -12, 29, -18, 31, 0x3C }, { -23, 23, -25, 25, 0x3D }, { -29, 12, -31, 18, 0x3E },
        { -32, 0, -36, 0, 0x3F }, { -29, -12, -31, -18, 0x3E }, { -23, -23, -25, -25, 0x3D }, { -12, -29, -18, -31, 0x3C },
    };
    unsigned set = OB8(m, M_SET);
    const int8_t *e = (set < 3 ? ring : set == 3 ? fold : open)[OB8(o, OBJ_frame) & 15];
    int16_t r = OBS16(m, M_RAD);
    SET16(o, H_DX, e[0] + (int16_t)((int16_t)(e[2] * r) >> 8));
    SET16(o, H_DY, e[1] + (int16_t)((int16_t)(e[3] * r) >> 8));
    set_coll(o, e[4] ? (uint8_t)(0x80 | e[4]) : 0, 2);         /* (CD's $B4-$BF: hurt) */
}

static void posiset(uint32_t o, uint32_t m)                    /* [$20C79E] */
{
    offs_set(o, m);
    set_xy(o, wx(m) + OBS16(o, H_DX), wy(m) + OBS16(o, H_DY));
}

static void rol_sound(uint32_t o)                              /* [$20CBCA] blade 0, off the diagonals */
{
    if (!OB8(o, H_NUM) && (OB8(o, OBJ_frame) & 3) != 2) o_sfx_cd(FM_TICK);
}

static void no_reset(uint32_t o, uint32_t m, uint32_t e)       /* [$20C7C0] a blade knocked off: the rest renumber */
{
    static const int8_t three[4][4] = { { -1, 0, 1, 2 }, { 2, -1, 0, 1 }, { 1, 2, -1, 0 }, { 0, 1, 2, -1 } };
    static const int8_t two[3][4] = { { -1, 0, 1, -1 }, { 0, -1, 1, -1 }, { 0, 1, -1, -1 } };
    uint8_t drop = OB8(m, M_DROP), n = OB8(o, H_NUM) & 3;
    if (drop == OB8(o, H_NUM)) { bset(o, H_FLAGS, 3); return; }
    switch (OB8(e, E_HITS)) {
    case 1: SET8(o, H_NUM, 0); break;
    case 3: SET8(o, H_NUM, (uint8_t)three[drop & 3][n]); break;
    case 2: SET8(o, H_NUM, (uint8_t)(drop < 3 ? two[drop][n] : 0)); break;
    default: SET8(o, H_NUM, 0); break;                        /* (0: the last one's off by now) */
    }
}

static int blade_fire(uint32_t o, uint32_t m, uint32_t e)      /* [$20C842] 0: not drawn */
{
    uint32_t p = RAM_Player_1;
    static const uint16_t rain[16] = { 3776, 3904, 3968, 3712, 3712, 3776, 3840, 3904,
                                       3968, 3904, 3840, 3712, 3760, 3920, 3792, 3856 };
    switch (OB8(o, H_STEP)) {
    case 0:
        SET8(o, H_STEP, 1);
        if (OB8(e, E_HITS) == 1) o_asset(o, SCDA_M3BLADE + 1);
        /* fall through */
    case 1:                                                   /* up off the top */
        addL(o, OBJ_y_pos, -0x10000);
        if (wy(o) <= 0xE0) {
            SET8(o, H_STEP, 2);
            SET8(m, M_UP, OB8(m, M_UP) + 1);
        }
        break;
    case 2:                                                   /* all up */
        if (OB8(e, E_HITS) > OB8(m, M_UP)) break;
        SET8(o, H_TIME, 0);
        SET8(o, H_STEP, 3);
        break;
    case 3: {                                                 /* each in its turn, down */
        SET8(o, H_TIME, OB8(o, H_TIME) + 1);
        if ((uint8_t)((OB8(o, H_NUM) + 1) * 0x1E) != OB8(o, H_TIME)) break;
        SET16(o, OBJ_priority, 3 << 7);
        SET8(o, H_STEP, 4);
        SET8(o, H_TIME, 0);
        o_asset(o, SCDA_M3BLADE + 5);
        SET8(o, OBJ_frame, 0);
        uint8_t n = OB8(e, E_HITS);
        if (n < 3) {
            o_asset(o, SCDA_M3BLADE + 1);
            if (n != 2) {                                     /* (one left: thrown along the floor at him) */
                if (wx(p) < 0xF00) {
                    SET8(o, OBJ_frame, 3);
                    SET16(o, OBJ_y_pos, (int16_t)((((int32_t)(int16_t)(0xFF8 - wx(p))) * -0x6A0A) >> 16) + 0x1E0);
                    SET16(o, OBJ_x_pos, 0xFC8);
                    setL(o, H_VX, (int32_t)0xFFFCC447);
                } else {
                    SET8(o, OBJ_frame, 0xD);
                    SET16(o, OBJ_y_pos, (int16_t)((((int32_t)(int16_t)(wx(p) - 0xE08)) * -0x6A0A) >> 16) + 0x1E0);
                    SET16(o, OBJ_x_pos, 0xE38);
                    setL(o, H_VX, 0x33BB9);
                }
                setL(o, H_VY, 0x156CF);
                break;
            }
            if (OB8(o, H_NUM)) {                              /* (two left: the second onto him) */
                SET16(o, OBJ_x_pos, wx(p));
                setL(o, H_VX, 0);
                setL(o, H_VY, 0x38000);
                break;
            }
        }
        SET16(o, OBJ_x_pos, rain[(OB8(m, M_PAT) / 2 + OB8(o, H_NUM)) & 15]);   /* (on the pattern) */
        setL(o, H_VX, 0);
        setL(o, H_VY, 0x38000);
        break;
    }
    case 4: {                                                 /* falling to the floor */
        addL(o, OBJ_x_pos, L(o, H_VX));
        addL(o, OBJ_y_pos, L(o, H_VY));
        int16_t d0 = OB8(o, OBJ_frame) ? 0x1D0 : blade_set_is(o, 5) ? 0x1C0 : 0x1B8;
        if (d0 > wy(o)) break;
        SET16(o, OBJ_y_pos, d0);
        SET8(o, H_STEP, 5);
        SET8(o, H_TIME, 0);
        o_sfx_cd(FM_THUMP);
        break;
    }
    case 5:                                                   /* 30 frames stuck, still hurting */
        SET8(o, H_TIME, OB8(o, H_TIME) + 1);
        if (OB8(o, H_TIME) != 0x1E) break;
        SET16(o, OBJ_priority, 5 << 7);
        SET8(o, H_STEP, 6);
        set_coll(o, 0, 0);
        SET8(o, H_TIME, 0);
        break;
    case 6:                                                   /* 60 frames blinking, harmless; then gone */
        SET8(o, H_TIME, OB8(o, H_TIME) + 1);
        if (OB8(o, H_TIME) == 0x3C) {
            static const int8_t back[5][4] = {                /* [$20CAC4] the frames they come back on */
                { 0 }, { 8, -1 }, { 8, 12 }, { 4, 8, 12, -1 }, { 0, 4, 8, 12 },
            };
            SET8(m, M_FIRED, OB8(m, M_FIRED) + 1);
            SET8(o, H_TIME, 0);
            SET8(o, H_STEP, 0);
            SET8(o, OBJ_routine, 8);
            const int8_t *b = back[OB8(e, E_HITS) % 5];
            if (!OB8(o, H_NUM)) SET8(m, M_FRAME, (uint8_t)b[0]);
            SET8(o, OBJ_frame, (uint8_t)b[OB8(o, H_NUM) & 3]);
            SET8(m, M_SET, 0);
            return 0;
        }
        return !(OB8(o, H_TIME) & 1);
    }
    return 1;
}

/* the blade's routines: 1 drawn, 0 not, -1 again (CD's bra to its dispatch) */
static int blade_kill(uint32_t o)                              /* [$20CAF6] off: up and away, spinning */
{
    if (!OB8(o, H_STEP)) {
        set_coll(o, 0, 0);
        setL(o, H_VX, OB8(o, OBJ_frame) > 8 ? -0x10000 : 0x10000);
        setL(o, H_VY, -0x30000);
        SET8(o, H_TIME, 0);
        SET8(o, H_CNT, 0);
        SET8(o, H_STEP, 1);
        o_asset(o, SCDA_M3BLADE + (blade_set_is(o, 3) ? 4 : 2));
    }
    addL(o, OBJ_x_pos, L(o, H_VX));
    addL(o, OBJ_y_pos, L(o, H_VY));
    addL(o, H_VY, 0x3000);
    if (wy(o) >= 0x1E0) { o_delete(o); return 0; }
    SET8(o, H_TIME, OB8(o, H_TIME) + 1);
    if (OB8(o, H_TIME) == 4) {
        SET8(o, H_TIME, 0);
        uint8_t f = (uint8_t)(OB8(o, OBJ_frame) + 1);
        SET8(o, OBJ_frame, f > 0xF ? 0 : f);
    }
    SET8(o, H_CNT, OB8(o, H_CNT) + 1);
    return !(OB8(o, H_CNT) & 1);
}

static int blade_normal(uint32_t o, uint32_t m)                /* [$20C72A] round the shell */
{
    if (btst(o, H_FLAGS, 3)) {                                /* (knocked off) */
        SET8(o, H_STEP, 0);
        SET8(o, OBJ_routine, 0xA);
        return -1;
    }
    if (OB8(m, OBJ_routine) == 0xA && !OB8(o, OBJ_frame)) {   /* (the blades fired: each from the top) */
        SET8(o, H_STEP, 0);
        SET8(o, OBJ_routine, 6);
        SET8(m, M_FIRED, OB8(m, M_FIRED) + 1);
        return 1;
    }
    o_asset(o, SCDA_M3BLADE + OB8(m, M_SET) % 6);
    if (btst(m, M_FLAGS, 6)) {                                /* [$20CBA2] a step left */
        int8_t f = (int8_t)(OB8(o, OBJ_frame) - 1);
        SET8(o, OBJ_frame, f < 0 ? 0xF : f);
        rol_sound(o);
    } else if (btst(m, M_FLAGS, 5)) {                         /* [$20CBB4] a step right */
        uint8_t f = (uint8_t)(OB8(o, OBJ_frame) + 1);
        SET8(o, OBJ_frame, f >= 0x10 ? 0 : f);
        rol_sound(o);
    }
    if (!OB8(o, H_NUM)) SET8(m, M_FRAME, OB8(o, OBJ_frame));
    posiset(o, m);
    return 1;
}

static void blade(uint32_t o)
{
    uint32_t m = lk(o, H_MECA), e = lk(m, M_EGG);
    if (!is(m, 0x41) || !is(e, 0x3F)) { o_delete(o); return; }   /* (S3K reuses slots) */
    if ((int8_t)OB8(m, M_DROP) >= 0) no_reset(o, m, e);
    int draw;
    do {
        switch (OB8(o, OBJ_routine)) {
        case 0:                                               /* [$20C6D2] */
            SET8(o, OBJ_routine, 2);
            init(o, SCDA_M3BLADE, 4, 5, 0x20, 0x20);
            draw = blade_normal(o, m);
            if (draw >= 0) set_coll(o, 0, 0);                 /* (not on its first frame) */
            break;
        case 2:                                               /* [$20C70C] the demo: not yet hurting */
            posiset(o, m);
            if (OB8(e, OBJ_routine) == 4) set_coll(o, 0, 0);
            else SET8(o, OBJ_routine, 4);
            draw = 1;
            break;
        case 4: draw = blade_normal(o, m); break;
        case 6: draw = blade_fire(o, m, e); break;            /* [$20C842] fired */
        case 8:                                               /* [$20CADA] landed and gone: waits for the reset */
            if (btst(o, H_FLAGS, 3)) { o_delete(o); return; }
            if (btst(m, M_FLAGS, 3)) SET8(o, OBJ_routine, 4);
            draw = 0;
            break;
        default:
            draw = blade_kill(o);
            if (!r32(o + OBJ_code)) return;                   /* (deleted) */
            break;
        }
    } while (draw < 0);
    if (!draw) return;
    uint8_t f = OB8(o, OBJ_frame), r = OB8(o, OBJ_render) & ~3;   /* (the quarter: its flips) */
    r |= f < 4 ? 0 : f < 8 ? 2 : f < 12 ? 3 : 1;
    SET8(o, OBJ_render, r);
    touch_draw(o);
}

/* ======================================================================
 * $42 sparks
 * ==================================================================== */
static void sparks(uint32_t o)
{
    static const int8_t at[2][3][2] = { { { -16, 0 }, { -16, -8 }, { -4, 0 } }, { { 4, 0 }, { 16, -8 }, { 8, 0 } } };
    if (!OB8(o, OBJ_routine)) {                               /* [$20CE3C] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_M3SPARK, 4, 4, 8, 8);
    }
    uint32_t par = lk(o, S_PAR);                              /* [$20CE66] */
    if (par) set_xy(o, wx(par), wy(par));
    const int8_t *d = at[OB8(o, S_SIDE) & 1][(OB8(o, S_STEP) / 4) % 3];
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + d[0]);
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d[1]);
    SET8(o, S_TIME, OB8(o, S_TIME) + 1);
    if (OB8(o, S_TIME) == 0x96) { o_delete(o); return; }
    SET8(o, S_CNT, OB8(o, S_CNT) + 1);
    if (OB8(o, S_CNT) == 4) {
        SET8(o, S_STEP, OB8(o, S_STEP) + 4);
        if (OB8(o, S_STEP) > 8) SET8(o, S_STEP, 0);
        SET8(o, S_CNT, 0);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) > 2) SET8(o, OBJ_frame, 0);
    }
    if (OB8(o, OBJ_frame) == 2) return;                       /* (frame 2: not drawn) */
    o_draw(o);
}

/* ======================================================================
 * $43 Amy, and the act's end
 * ==================================================================== */
static void flash_out8(void)                                   /* [$20D1D6] every colour's channels a step up */
{
    for (unsigned k = 0; k < 64; k++) {
        uint32_t a = RAM_Normal_palette + k * 2u;
        uint16_t c = r16(a), out = 0;
        for (uint16_t mk = 0xE, s = 2; mk && mk <= 0xE00; mk = (uint16_t)(mk << 4), s = (uint16_t)(s << 4))
            out = (uint16_t)(out + ((c & mk) == mk ? mk : (c & mk) + s));
        w16(a, out);
    }
}

static void amy_pad(uint32_t o)                                /* [$20D168] the player's pad, scripted */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, A_PAD)) {
    case 0:                                                   /* right to $F90 */
        w16(RAM_Ctrl_1_held_logical, 0x0808);
        if (wx(p) < 0xF90) break;
        SET8(o, A_PAD, 2);
        /* fall through */
    case 2:                                                   /* back to $F70 */
        w16(RAM_Ctrl_1_held_logical, 0x0404);
        if (wx(p) > 0xF70) break;
        SET8(o, A_PAD, 4);
        /* fall through */
    case 4:                                                   /* stands; when she hops: waits */
        w16(RAM_Ctrl_1_held_logical, 0);
        if (OB8(o, OBJ_routine) < 6) break;
        SET8(o, A_PAD, 6);
        SET8(p, P_ANIM, 5);
        break;
    default:
        if (OB8(p, P_ANIMF) < 0x10) SET8(p, P_ANIMF, 0x10);
        break;
    }
}

static void amy_move(uint32_t o) { cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8); }

static void amy_end(uint32_t o)                                /* (CD's ending: here S3K's results) */
{
    uint32_t p = RAM_Player_1;
    if (s_pal_saved)
        for (unsigned k = 0; k < 64; k++) w16(RAM_Normal_palette + k * 2u, s_pal[k]);
    s_pal_saved = 0;
    w16(RAM_Ctrl_1_held_logical, 0);
    SET8(o, OBJ_routine, 0x12);
    A(1) = RAMPTR(p);
    A(0) = RAMPTR(o);
    tri_jsr(S3K_Set_PlayerEndingPose);
    uint32_t a1 = o_alloc();
    if (a1) SET32(a1, OBJ_code, S3K_Obj_LevelResults);
}

static void amy(uint32_t o)
{
    uint32_t anim = o_asset_anim(SCDA_M3AMY);
    events();
    if (OB8(o, OBJ_routine) < 0x10) {
        amy_pad(o);
        SET8(o, A_SHAKE, OB8(o, A_SHAKE) - 1);                /* [$20D208] the camera's bottom shakes */
        s_shake = (int8_t)(OB8(o, A_SHAKE) & 1 ? -4 : -2);
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20D044] */
        init(o, SCDA_M3AMY, OB8(o, OBJ_render) | 4, 1, 0x10, 0x14);
        SET8(o, OBJ_routine, 2);
        w8(RAM_Ctrl_1_locked, 1);                             /* (ctrlLocked) */
        /* fall through */
    case 2:                                                   /* [$20D074] till Eggman is away */
        if (s_fight) break;
        SET8(o, OBJ_routine, 4);
        SET16(o, OBJ_x_vel, 0x140);
        /* fall through */
    case 4:                                                   /* [$20D086] runs in */
        o_animate(o, anim);
        amy_move(o);
        if (wx(o) < 0xEA0) break;
        /* fall through */
    case 6: case 0xA:                                         /* [$20D0A4] a hop */
        SET8(o, OBJ_routine, (OB8(o, OBJ_routine) | 6) == 6 ? 8 : 0xC);
        SET8(o, OBJ_frame, 6);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0xFD00);
        /* fall through */
    case 8: case 0xC:                                         /* [$20D0B8] */
        amy_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x40);
        if (OBS16(o, OBJ_y_vel) >= 0) SET8(o, OBJ_frame, 7);
        if (wy(o) < 0x1CC) break;
        SET16(o, OBJ_y_pos, 0x1CC);
        SET16(o, OBJ_x_vel, 0x140);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        anim_set(o, 0, 1);
        break;
    case 0xE:                                                 /* [$20D0F4] off right, the screen to white */
        amy_move(o);
        if (wx(o) >= 0xEE0) {
            SET8(o, A_TIME, OB8(o, A_TIME) + 1);
            if (OB8(o, A_TIME) >= 8) {
                SET8(o, A_TIME, 0);
                SET8(o, A_STEPS, OB8(o, A_STEPS) + 1);
                if (OB8(o, A_STEPS) >= 8) {                   /* (CD: the ending) */
                    SET8(o, OBJ_routine, 0x10);
                    s_shake = 0;
                    break;
                }
                if (!s_pal_saved) {
                    for (unsigned k = 0; k < 64; k++) s_pal[k] = r16(RAM_Normal_palette + k * 2u);
                    s_pal_saved = 1;
                }
                flash_out8();
            }
        }
        o_animate(o, anim);
        break;
    case 0x10:                                                /* (the results, once he is down) */
        if (OB8(RAM_Player_1, OBJ_status) & 2) break;
        amy_end(o);
        break;
    case 0x12:
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Check_TailsEndPose);
        break;
    }
    o_draw(o);
}

/* ======================================================================
 * $2A flip panel (KONBEA83)
 * ==================================================================== */
#define K_PAR    0x30                  /* oVar2A: (a child) the first panel */
#define K_TIME   0x32                  /* oVar2C (word) */
#define K_X0     0x34                  /* oVar2E */
#define K_Y0     0x36                  /* oVar30 */
#define K_STOOD  0x44                  /* oVar3E */
#define K_SUB2   0x45                  /* (CD's subtype 2: $FF a child; the act places none) */
static void panel_init(uint32_t o, uint32_t a)                 /* [$20EEC0] */
{
    SET8(a, OBJ_routine, 2);
    o_init(a, SCDA_M3PANEL, OB8(a, OBJ_render) | 4, 3, 0x10, 0x10);
    SET8(a, OBJ_x_radius, 0x10);
    SET8(a, OBJ_y_radius, 5);
    SET16(a, K_X0, OB16(o, OBJ_x_pos));
    SET16(a, K_Y0, OB16(o, OBJ_y_pos));
}

static void panel(uint32_t o)
{
    if (OB8(o, K_SUB2)) {
        uint32_t a1 = lk(o, K_PAR);
        if (!is(a1, 0x2A) || OB16(o, K_X0) != OB16(a1, K_X0) || OB16(o, K_Y0) != OB16(a1, K_Y0)) { o_delete(o); return; }
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20EE5E] (subtype 2 0: one panel) */
        SET16(o, K_TIME, 0x3C);
        panel_init(o, o);
        break;
    case 2:                                                   /* [$20EF00] flat: 120 frames */
        SET8(o, OBJ_y_radius, 5);
        SET16(o, K_TIME, 0x78);
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4:                                                   /* [$20EF10] */
        o_platform(o, 0x10 + 10, 5, OB16(o, OBJ_x_pos));
        SET8(o, K_STOOD, cd_stood_on(o) ? 0xFF : 0);
        SET16(o, K_TIME, OB16(o, K_TIME) - 1);
        if (OB16(o, K_TIME)) break;
        SET8(o, OBJ_y_radius, 0x10);
        anim_set(o, 0, 0xFF);
        SET8(o, OBJ_routine, 6);
        if (OB8(o, K_STOOD)) cd_get_off(o);                   /* (CD: player 1) */
        break;
    case 6:                                                   /* [$20EF50] turns over and back */
        o_animate(o, o_asset_anim(SCDA_M3PANEL));
        if (!OB8(o, OBJ_frame)) SET8(o, OBJ_routine, 2);
        break;
    }
    if ((int8_t)OB8(o, K_SUB2) < 0) { o_draw(o); return; }
    if (cd_despawn(o, OB16(o, K_X0))) return;
    o_draw(o);
}

/* ======================================================================
 * $2B jet spike column
 * ==================================================================== */
#define J_TIME   0x30                  /* oVar2A (word) */
#define J_VX     0x32                  /* oVar2C (long) */
#define J_X0     0x36                  /* oVar30 */
#define J_TOUCH  0x39                  /* oVar33 */
static void jet(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20E9DA] */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_M3JET, OB8(o, OBJ_render) | 4, 1, 0x10, 0x4C);
        SET16(o, J_X0, OB16(o, OBJ_x_pos));
        SET8(o, J_TOUCH, 0xB1);
        setL(o, J_VX, 0x5000);
        SET8(o, OBJ_y_radius, 0x4C);
        SET8(o, OBJ_x_radius, 0x10);
    }
    addL(o, OBJ_x_pos, L(o, J_VX));                           /* [$20EA0E] */
    set_coll(o, scd_mmz_back() ? 0 : OB8(o, J_TOUCH), 0);     /* (the front layer's) */
    SET16(o, J_TIME, OB16(o, J_TIME) - 1);
    if (OBS16(o, J_TIME) < 0) {
        SET16(o, J_TIME, 0x199);
        setL(o, J_VX, -L(o, J_VX));
        SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
    }
    o_animate(o, o_asset_anim(SCDA_M3JET));
    if (cd_despawn(o, OB16(o, J_X0))) return;
    touch_draw(o);
}

/* ======================================================================
 * $36 act 3's block (togedair8) and spikes (harir8)
 * ==================================================================== */
#define B_Y0     0x38                  /* oVar32 */
#define B_PAR    0x3A                  /* oVar34: (the spikes on a block) the block */
#define B_X0     0x3C                  /* oVar36 */
#define B_DX     0x3E                  /* oVar38 */
#define B_DY     0x3F                  /* oVar39 */
static int near_y(uint32_t o)                                  /* player 1 within 32 px up or down */
{
    int16_t d = (int16_t)(wy(RAM_Player_1) - wy(o));
    return (d < 0 ? -d : d) < 0x20;
}

static void spikes3(uint32_t o)                                /* [$20DDE6] */
{
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) {                               /* [$20DE3E] */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_M3SPIKES, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET16(o, B_X0, OB16(o, OBJ_x_pos));
        SET16(o, B_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 8);
        if (OB8(o, OBJ_subtype) != 0xFF) {                    /* (hanging) */
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_width_px, 0x12);
            set_coll(o, 0x83, 0);
        }
    }
    if (!OB8(o, OBJ_frame)) {                                 /* (upright: stood on, they hurt) */
        if (near_y(o)) {
            o_solid(o, 0x10 + 10, 8, 9, OB16(o, OBJ_x_pos));
            if (OB8(o, OBJ_status) & ST_P1_STAND) o_spike_hurt(o, p);
        }
    } else if ((OB8(o, OBJ_render) & 0x80) && near_y(o))
        o_solid(o, 0x12 + 10, 8, 9, OB16(o, OBJ_x_pos));
    touch_draw(o);
    uint32_t par = lk(o, B_PAR);                              /* (on a block: there, after drawing) */
    if (par) {
        if (!is(par, 0x36)) { o_delete(o); return; }
        set_xy(o, wx(par) + (int8_t)OB8(o, B_DX), wy(par) + (int8_t)OB8(o, B_DY));
    }
    cd_despawn(o, OB16(o, B_X0));
}

static void block3(uint32_t o)
{
    if ((int8_t)OB8(o, OBJ_subtype) < 0) { spikes3(o); return; }
    if (!OB8(o, OBJ_routine)) {                               /* [$20DD20] */
        o_init(o, SCDA_M3BLOCK, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET16(o, B_X0, OB16(o, OBJ_x_pos));
        SET16(o, B_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 0x10);
        uint32_t a1 = o_alloc();
        if (a1) {
            SET8(o, OBJ_routine, 2);
            if (OB8(o, OBJ_subtype) == 1) {                   /* (upright spikes on it) */
                SET32(a1, OBJ_code, native3(0x36));
                SET8(a1, OBJ_subtype, 0xFF);
                set_lk(a1, B_PAR, o);
            } else {                                          /* (a spring on it: S3K's; the block stays) */
                SET32(a1, OBJ_code, S3K_Obj_Spring);
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype) & 2);
            }
            set_xy(a1, wx(o), wy(o) - 0x18);
            if (OB8(o, OBJ_subtype) == 1) SET8(a1, B_DY, 0xE8);
        }
    }
    if ((OB8(o, OBJ_render) & 0x80) && near_y(o)) o_solid(o, 0x10 + 10, 0x10, 0x11, OB16(o, OBJ_x_pos));
    if (cd_despawn(o, OB16(o, B_X0))) return;
    o_draw(o);
}

/* ---- registration ---------------------------------------------------------- */
int scd_mmz3_shut_flag(void) { return s_shut; }               /* (the curtain's animated art: held at frame 0) */

void scd_mmz3_level_entry(void)
{
    s_flags = s_fight = s_shut = 0;
    s_shake = 0;
    s_frame = 0xFFFF;
    s_pal_saved = 0;
}

void scd_mmz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x1D] = cd_bumper;                              /* (R3/BOBIN.C: the disc's code is CCZ's) */
    t[base3 + 0x2A] = panel;
    t[base3 + 0x2B] = jet;
    t[base3 + 0x36] = block3;
    t[base3 + 0x39] = firefly;
    t[base3 + 0x3D] = curtain;
    t[base3 + 0x3F] = egg8;
    t[base3 + 0x40] = blade;
    t[base3 + 0x41] = meca;
    t[base3 + 0x42] = sparks;
    t[base3 + 0x43] = amy;
}
