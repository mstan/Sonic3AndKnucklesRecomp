/*
 * scd_wwz_boss.c — Wacky Workbench act 3 (Sonic CD R63C / R63D): Eggman's
 * capsule up the three-floor shaft, and the act's own objects for it.
 * Transcribed from the Mega-CD program (the routines' R63C addresses in
 * brackets; names from the PC port's BOSS_6.C; R63D's code is the same).
 *
 * The capsule ($34, Eggman) comes down the shaft with its machine under it
 * ($35), a rotor on top ($36) and a jet under ($37). On a floor it hovers 300
 * frames (or till hit), then rises to the next and holds an 840-frame event:
 * rubble ($39) falls; four floor blocks ($3D) drop, then spring up (on one, a
 * jump carries the player a floor up); a beam ($40) crosses the player's
 * floor, whose emitters ($3E, subtype bit 7) and live blocks then hurt there
 * till the next. On top, spikes ($38) fall and any hit ends it: it drops,
 * Eggman flees, the top right door ($3E) opens. No hit counter.
 *
 * State the disc keeps for others (the player code reads it, scd_wwz.c):
 *   bossFlags ($F7A7): bit 7 the shaft's sparks are off (no live floor);
 *     6 asks for that (UpdateAnimTiles makes it 7 at its next step and blanks
 *     the tiles); 5 palette cycle 4 black; 4 the arena (the level's events);
 *     0-3 started
 *   bossFight ($F7AA): 6, and bits 5-7 the beam on floor 0-2
 *   bossMusic ($FF1958): the boss's song has started
 * The level's events for it (zone6chk: the camera's bottom by floor, the left
 * edge following the player out) run here: nothing else in act 3 does.
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (named below), CD's $10 / $12
 * S3K's x_vel / y_vel. Collision: the capsule and its machine are CD enemies
 * with hits left ($21 = 2, renewed 30 frames after a hit), S3K's boss touch:
 * Touch_Enemy clears the type on a hit, as CD's does, and the capsule polls.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>


/* the capsule ($34) */
#define B_TIME   0x30                  /* oVar2A (word) */
#define B_FLAGS  0x32                  /* oVar2C: bit 0 can be hit, 1 genecolor done */
#define B_FLOOR  0x33                  /* oVar2D: 0 bottom .. 3 above the top */
#define B_LINK   0x34                  /* oVar2E: its machine (a part's: its parent) */
#define B_AX     0x36                  /* oVar30: x accel (add_spd2) */
#define B_AY     0x38                  /* oVar32: y accel */
#define B_ROTOR  0x3A                  /* oVar34: the rotor: 0 still, 1 start, 2 spinning, -1 gone */
#define B_FLAME  0x3B                  /* oVar35: the jet's animation (-1 gone) */
#define B_LAUNCH 0x3C                  /* oVar36: the floor blocks spring */
#define B_COOL   0x3D                  /* oVar37: after a hit */
#define B_SHAKE  0x3E                  /* oVar38: the camera shakes */
#define B_HITS   0x3F                  /* oVar39 */
#define B_BOB    0x40                  /* oVar3A (word) */
#define B_ANG    0x42                  /* oVar3C */
#define B_SIDE   0x43                  /* oVar3D: the spikes start left (1) */
#define B_ETIME  0x44                  /* oVar3E */
#define B_EIDX   0x45                  /* oVar3F */
/* the parts */
#define C_LINK   0x34                  /* oVar2E: the capsule */
#define R_TIME   0x30                  /* (the rotor's CD $1E) */
#define R_RELOAD 0x31                  /* (CD $1F) */
#define T_TIME   0x30                  /* oVar2A (byte) */
#define T_COUNT  0x31                  /* oVar2B */
#define E_TIME   0x30                  /* oVar2A (word) */
#define M_TIME   0x30                  /* oVar2A (byte) */
#define Y_FLOOR  0x30                  /* (CD's subtype 2: its floor - 1) */
#define Y_NEAR   0x3E                  /* oVar38: the player was near (the lower jump) */
#define Y_GROW   0x42                  /* oVar3C */
#define D_MOVED  0x30                  /* oVar2A (word) */
#define D_TARGET 0x3A                  /* oVar34 */
#define D_DX     0x3C                  /* oVar36: the solid's place from it */
#define D_DY     0x3E                  /* oVar38 */

#define P_ROUTINE 0x05
#define P_ANIM    0x20
#define P_OBJCTRL 0x2E
#define P_INVULN  0x34

#define FM_BOSSHIT  0xAC               /* (egg6_hitchk [$20E73E]) */
#define FM_SETTLE   0xB1               /* (egg6_down [$20E8AC]: at a floor) */
#define FM_RISE     0xA3               /* (egg6_up [$20E994]: up through the ceiling, not to the top) */
#define FM_WARN     0xB2               /* (beamdemo_start [$20E9FA]: three beeps, as the beams') */
#define FM_THUMP    0xB4               /* (egg6yuka [$20F286]: a floor block lands) */
#define FM_DEFEAT   0xC7               /* (rakkabutu's defeat [$20EAE4]: the hit above the top) */
#define FM_BOMB     0x9E               /* (FM_EXPLODE: make_bakuha [$20F886], make_bakuha2 [$20F8B2]) */

void tri_scd_level_music(void);
void cd_animal(uint32_t o);            /* scd_ppz.c */
void cd_capsule(uint32_t o);           /* scd_ppz_boss.c: Palmtree Panic's $15, the same code */

static int s_base3;
static uint8_t s_flags;                /* bossFlags */
static uint8_t s_fight;                /* bossFight */
static uint8_t s_music;                /* bossMusic */
static uint8_t s_dead;                 /* (the player's status bit 7: killed by the boss's hazards) */
static uint16_t s_right;               /* (the level's right edge) */
static uint16_t s_bottom, s_dest;      /* bottomBound, destBottomBound (once bossFlags bit 4) */
static int s_palcyc = -1;              /* (what colchg_set wrote to the cycles' timers this frame) */
static int8_t s_tick;                  /* (UpdateAnimTiles' sparks: their timer) */
static uint16_t s_frame = 0xFFFF;
static uint8_t s_fade, s_fade_in, s_held;

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }
static uint32_t lk(uint32_t o) { uint16_t w = o ? OB16(o, C_LINK) : 0; return w ? 0xFF0000u | w : 0; }
static void set_lk(uint32_t o, uint32_t a) { SET16(o, C_LINK, (uint16_t)a); }
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }

static void add_spd(uint32_t o)                                /* [$20F632] */
{
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
}
static void add_spd2(uint32_t o)                               /* [$20F622] */
{
    SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, B_AX));
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, B_AY));
    add_spd(o);
}

static uint32_t spawn(int id)                                  /* FindObjSlot, id */
{
    uint32_t a1 = o_alloc();
    if (a1) SET32(a1, OBJ_code, native3(id));
    return a1;
}

static void init(uint32_t o, int asset, int prio, int width, int yrad, int height)
{
    o_init(o, asset, 4, prio, width, height);
    SET8(o, OBJ_y_radius, yrad);
}

static void set_coll(uint32_t o, uint8_t cd, uint8_t hits)    /* CD's touch type as S3K's */
{
    if (!o) return;
    SET8(o, OBJ_coll_flags, cd ? tri_scd_coll(cd) : 0);
    SET8(o, OBJ_coll_prop, hits);
}

static void part_draw(uint32_t o)
{
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

static void line1(int asset)                                  /* LoadPalette into line 1 (and its fade target) */
{
    uint32_t pal = o_asset_anim(asset);
    if (!pal) return;
    for (unsigned k = 0; k < 16; k++) {
        uint16_t c = tri_rd16(pal + k * 2u);
        w16(RAM_Normal_palette + 0x20 + k * 2u, c);
        w16(RAM_Target_palette + 0x20 + k * 2u, c);
    }
}

static int dead(uint32_t p) { return s_dead || OB8(p, P_ROUTINE) >= 6; }

static void damage(uint32_t o)                                 /* [$20F3D8] mydamage */
{
    uint32_t p = RAM_Player_1;
    if (s_dead) return;
    o_hurt_player(p, o);
    if (OB8(p, P_ANIM) == 0x18) s_dead = 1;                   /* (killed: CD marks his status bit 7) */
}

static int snc_floor(void)                                     /* [$20F956] get_snc_floor */
{
    int16_t y = wy(RAM_Player_1);
    return y <= 0x298 ? 2 : y <= 0x498 ? 1 : 0;
}
static int beam_pos(void)                                      /* [$20F976]: its bossFight bit */
{
    int16_t y = wy(RAM_Player_1);
    return y <= 0x360 ? 7 : y <= 0x560 ? 6 : 5;
}

/* once a frame, from whichever of these runs first: UpdateAnimTiles' sparks
 * step every 5 frames, and a request (bit 6) turns them off (bit 7) there */
static void frame_tick(void)
{
    uint16_t f = r16(RAM_Level_frame_counter);
    if (f == s_frame) return;
    s_frame = f;
    if (--s_tick >= 0) return;
    s_tick = 4;
    if (s_flags & 0x40) s_flags = (uint8_t)((s_flags & ~0x40) | 0x80);
}

/* ---- the level's events (zone6chk [$20363A]) and the camera's bottom -------- */
static int arena_exit(int16_t x0, uint16_t d1)                 /* [$203806] bossclr_scr */
{
    uint32_t p = RAM_Player_1;
    if (wx(p) < x0) return 0;
    s_dest = d1;
    int16_t d = (int16_t)(d1 - s_bottom);
    if ((d < 0 ? -d : d) < 4) s_bottom = s_dest;
    int16_t x = (int16_t)(wx(p) - 0xA0);                       /* (the left edge follows: no way back) */
    if (x < (int16_t)r16(RAM_Camera_min_X_pos)) return 1;
    if (x > (int16_t)r16(RAM_Camera_max_X_pos)) x = (int16_t)r16(RAM_Camera_max_X_pos);
    w16(RAM_Camera_min_X_pos, (uint16_t)x);
    w16(RAM_Camera_target_min_X_pos, (uint16_t)x);
    return 1;
}

static void events(void)
{
    if (!(s_flags & 0x10)) return;                             /* (till then the level's $710, as converted) */
    uint32_t p = RAM_Player_1;
    if (!arena_exit(0xBA0, 0x1D0)) {                           /* (the bottom by the player's floor) */
        int16_t y = wy(p);
        uint16_t d0 = y <= 0x298 ? 0x1D0 : y <= 0x498 ? 0x3D0 : 0x5D0;
        s_dest = d0;
        int16_t d = (int16_t)(d0 - s_bottom);
        if ((d < 0 ? -d : d) <= 2) s_bottom = d0;
    }
    if (s_dest != s_bottom) {                                  /* [$203506]: 4 px a frame (16 falling near it) */
        uint16_t cy = r16(RAM_Camera_Y_pos);
        int16_t d1 = 4;
        if (s_dest > s_bottom) {
            if ((uint16_t)(cy + 8) >= s_bottom && (OB8(p, OBJ_status) & 2)) d1 *= 4;
        } else {
            d1 = -4;
            if (cy > s_dest) s_bottom = cy & 0xFFFE;
        }
        s_bottom = (uint16_t)(s_bottom + d1);
        w8(RAM_Camera_max_Y_pos_changing, 1);                  /* (S3K's MoveCameraY clamps only then) */
    }
    w16(RAM_Camera_max_Y_pos, s_bottom);                       /* (both: S3K's own move stays out) */
    w16(RAM_Camera_target_max_Y_pos, s_bottom);
}

/* ---- the screen to white and back (flashout [$200428] / flashin [$2005AA]):
 * on the disc a 22-frame loop that holds the game; here 22 frames in which
 * the capsule's clock stops and the player is held --------------------------- */
static void hold(int on)
{
    uint32_t p = RAM_Player_1;
    if (on && !s_held && OB8(p, P_ROUTINE) == 2) {
        s_held = 1;
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
    } else if (!on && s_held) {
        s_held = 0;
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
    }
}

static void fade_start(int in)
{
    s_fade = 22;
    s_fade_in = (uint8_t)in;
    if (in)                                                    /* (lines 0-2 white, then toward the target) */
        for (unsigned k = 0; k < 48; k++) w16(RAM_Normal_palette + k * 2u, 0xEEE);
    hold(1);
}

static void fade_frame(void)
{
    for (unsigned k = 0; k < (s_fade_in ? 48u : 64u); k++) {
        uint32_t a = RAM_Normal_palette + k * 2u;
        uint16_t c = r16(a);
        if (!s_fade_in) {                                      /* WhiteOut: red, then green, then blue */
            if (c == 0xEEE) continue;
            if ((c & 0x00E) != 0x00E) c += 0x002;
            else if ((c & 0x0E0) != 0x0E0) c += 0x020;
            else if ((c & 0xE00) != 0xE00) c += 0x200;
        } else {                                               /* WhiteIn: blue, then green, then red */
            uint16_t t = r16(RAM_Target_palette + k * 2u);
            if (c == t) continue;
            if (c >= 0x200 && (uint16_t)(c - 0x200) >= t) c -= 0x200;
            else if (c >= 0x020 && (uint16_t)(c - 0x020) >= t) c -= 0x020;
            else c -= 0x002;
        }
        w16(a, c);
    }
    if (s_fade_in) w16(RAM_Normal_palette + 0x40, 0xEEE);     /* (line 2's colour 0 stays white) */
    if (!--s_fade) hold(0);
}

/* the background toward white and back (enkei_out [$2004D0] / enkei_in
 * [$2004B6]): every 12 frames a step of the level's table, into line 2's
 * colour 0 and some of line 3 (which, by the period) */
static void enkei(uint32_t o, int out)
{
    static const uint8_t good[] = { 32, 55, 56, 57, 58, 59, 60, 61, 62, 63 };
    static const uint8_t bad[] = { 32, 50, 51, 53, 54, 58, 59, 60, 61, 62, 63 };
    int bf = cd_period() == TRI_CD_BAD;
    const uint8_t *ix = bf ? bad : good;
    unsigned n = bf ? sizeof bad : sizeof good, step = n * 2;
    uint8_t t = (uint8_t)(OB8(o, B_ETIME) + 1);
    SET8(o, B_ETIME, t);
    if (t < 12) return;
    SET8(o, B_ETIME, 0);
    int i = OB8(o, B_EIDX);
    if (out) { i += (int)step; if (i == (int)step * 8) i = (int)step * 7; }
    else { i -= (int)step; if (i < 0) i = 0; }
    SET8(o, B_EIDX, i);
    uint32_t tab = o_asset_anim(SCDA_W3ENKEI);
    if (!tab) return;
    for (unsigned k = 0; k < n; k++) w16(RAM_Normal_palette + ix[k] * 2u, tri_rd16(tab + (unsigned)i + k * 2u));
}

/* ---- $34 the capsule ------------------------------------------------------ */
static void coliset(uint32_t o)                                /* [$20E752] egg6_coliset */
{
    set_coll(o, 0x3C, 2);
    set_coll(lk(o), 0x3D, 2);
}

static void hit_chk(uint32_t o)                                /* [$20E6F8] egg6_hitchk */
{
    if (!(OB8(o, B_FLAGS) & 1)) return;
    if (OB8(o, B_COOL)) {
        SET8(o, B_COOL, OB8(o, B_COOL) - 1);
        if (!OB8(o, B_COOL)) coliset(o);
        return;
    }
    uint32_t m = lk(o);
    if (OB8(o, OBJ_coll_flags) && (!m || OB8(m, OBJ_coll_flags))) return;
    SET8(o, B_HITS, OB8(o, B_HITS) + 1);                       /* (either: the capsule or its machine) */
    SET8(o, OBJ_coll_flags, 0);
    if (m) SET8(m, OBJ_coll_flags, 0);
    SET8(o, OBJ_anim, 1);
    SET8(o, OBJ_frame, 0);
    SET8(o, B_COOL, 0x1E);
    o_sfx_cd(FM_BOSSHIT);
}

static void beam_ctrl(void)                                    /* [$20E770] egg6_beam_ctrl */
{
    if (s_fight >> beam_pos() & 1) s_flags &= (uint8_t)~0x80;  /* (the beam on his floor: sparks live) */
    else if (!(s_flags & 0x80)) s_flags |= 0x40;               /* (else off, at the tiles' next step) */
}

static void shake(uint32_t o)                                  /* [$20F996] egg6_jisin */
{
    if (!OB8(o, B_SHAKE)) return;
    SET8(o, B_SHAKE, OB8(o, B_SHAKE) - 1);
    int16_t d0 = OB8(o, B_SHAKE) & 1 ? -4 : -2;
    s_bottom = (uint16_t)(s_bottom + d0);
    s_dest = (uint16_t)(s_dest + d0);
}

static void bob(uint32_t o)                                    /* [$20E90E] fuwafuwa */
{
    if (OB8(o, B_FLOOR) == 3) {                                /* (above the top: it shakes) */
        SET8(o, B_ANG, OB8(o, B_ANG) + 1);
        SET16(o, OBJ_x_pos, wx(o) + (OB8(o, B_ANG) & 1 ? 2 : -2));
        return;
    }
    SET16(o, OBJ_y_pos, wy(o) - OBS16(o, B_BOB));
    int16_t s, c;
    o_sincos(OB8(o, B_ANG), &s, &c);
    SET8(o, B_ANG, OB8(o, B_ANG) + 2);
    int16_t d0 = (int16_t)(int16_t)(s * 4) >> 8;
    SET16(o, B_BOB, d0);
    SET16(o, OBJ_y_pos, wy(o) + d0);
}

static void make_meca(uint32_t o)                              /* [$20F90A] */
{
    uint32_t a1 = spawn(0x35);
    if (!a1) return;
    set_lk(a1, o);
    SET16(o, B_LINK, (uint16_t)a1);
    if (!(a1 = spawn(0x36))) return;
    set_lk(a1, o);
    if (!(a1 = spawn(0x37))) return;
    set_lk(a1, o);
}

static int16_t rubble_y(uint32_t o)                            /* [$20F688] get_gareki_y */
{
    static const int16_t y[3] = { 0x58C, 0x38C, 0x18C };
    return y[(unsigned)(OB8(o, B_FLOOR) - 1) % 3];
}

static void make_rubble(uint32_t o, uint16_t d0)               /* [$20F754] make_gareki */
{
    if (d0 % 16) return;
    uint32_t a1 = spawn(0x39);
    if (!a1) return;
    set_lk(a1, o);
    SET16(a1, OBJ_y_pos, rubble_y(o));
    int16_t r = (int16_t)o_random();
    SET16(a1, OBJ_x_pos, r % 0x50 + 0xAC0);
    SET16(a1, OBJ_x_vel, r % 0x280);
    SET16(a1, OBJ_y_vel, 0x100);
    SET16(a1, B_AY, 8 + (r & 0xF));
}

static void make_floor(uint32_t o, uint16_t d0)                /* [$20F700] make_yuka */
{
    static const int16_t x[3][4] = {
        { 0xA98, 0xAE8, 0xA48, 0xB38 }, { 0xA30, 0xA78, 0xAF0, 0xB50 }, { 0xB10, 0xB58, 0xA70, 0xA28 },
    };
    unsigned q = d0 / 20;
    if (d0 % 20 || q >= 4) return;
    uint32_t a1 = spawn(0x3D);
    if (!a1) return;
    SET8(a1, OBJ_subtype, q);
    set_lk(a1, o);
    SET16(a1, OBJ_y_pos, rubble_y(o));
    unsigned f = (unsigned)(OB8(o, B_FLOOR) - 1) % 3;
    SET8(a1, Y_FLOOR, f);
    SET16(a1, OBJ_x_pos, x[f][q]);
}

static void make_spike(uint32_t o, uint16_t d0)                /* [$20F698] make_toge */
{
    unsigned q = d0 / 35;
    if (d0 % 35) return;
    uint32_t a1 = spawn(0x38);
    if (!a1) return;
    set_lk(a1, o);
    SET16(a1, OBJ_y_pos, rubble_y(o));
    if (!q) SET8(o, B_SIDE, wx(RAM_Player_1) < 0xAC0);         /* (the first: from the player's side) */
    SET16(a1, OBJ_x_pos, OB8(o, B_SIDE) ? 0xA20 + (int)q * 0x20 : 0xB60 - (int)q * 0x20);
}

static void make_beam(int floor)                               /* [$20F846] */
{
    static const int16_t y[3] = { 0x688, 0x488, 0x288 };
    for (int k = 0; k < 2; k++) {                              /* (each its own try) */
        uint32_t a1 = spawn(0x40);
        if (!a1) continue;
        set_xy(a1, k ? 0xB70 : 0xA10, y[floor % 3]);
        SET8(a1, OBJ_subtype, k);
    }
}

static void make_boom(uint32_t o)                              /* [$20F87E] make_bakuha: the one that follows */
{
    uint32_t a1 = spawn(0x3F);
    if (!a1) return;
    o_sfx_cd(FM_BOMB);
    SET8(a1, OBJ_anim, 1);
    set_lk(a1, o);
}

static void make_boom2(uint32_t o)                             /* [$20F8AA] make_bakuha2: a small one, flung */
{
    uint32_t a1 = spawn(0x3F);
    if (!a1) return;
    o_sfx_cd(FM_BOMB);
    set_xy(a1, wx(o), wy(o) - 8);
    uint32_t r = (uint32_t)o_random();
    int32_t d0 = (int16_t)(r & 0xFFFF), d1 = -(int32_t)((r >> 16) & 0x7FFF);
    SET16(a1, OBJ_x_vel, d0 % 0x400);
    SET16(a1, OBJ_y_vel, d1 % 0x400);
}

static void retry(uint32_t o)                                  /* [$20EB26] egg6_event_retry */
{
    SET16(o, B_TIME, 0x168);
    SET8(o, B_ROTOR, 0);
    SET8(o, OBJ_routine, 0xA);
}

static void go_down(uint32_t o)
{
    SET16(o, B_TIME, 0);
    SET8(o, OBJ_routine, 4);
    SET16(o, OBJ_x_vel, 0);
    SET16(o, OBJ_y_vel, 0x300);
    SET16(o, B_AY, 0xFFFF);
}

static void defeat(uint32_t o)                                 /* [$20EAD4] */
{
    SET8(o, B_FLAGS, OB8(o, B_FLAGS) & ~1);
    SET16(o, B_TIME, 0);
    SET8(o, OBJ_routine, 0xC);
    o_sfx_cd(FM_DEFEAT);                                      /* (then make_boom's FM_BOMB, as the disc) */
    SET16(o, OBJ_x_vel, 0x300);
    SET16(o, OBJ_y_vel, 0x100);
    SET16(o, B_AY, 0x10);
    make_boom(o);
    SET8(o, OBJ_anim, 2);
    SET8(o, OBJ_frame, 0);
    SET8(o, B_FLAME, 0xFF);
    SET8(o, B_ROTOR, 0xFF);
}

static void rakkabutu(uint32_t o)                              /* [$20EA5E]: what falls */
{
    int16_t t = OBS16(o, B_TIME);
    if (t <= 0x168) make_rubble(o, (uint16_t)(t - 0x168));
    if (t >= 0xB4) make_floor(o, (uint16_t)(t - 0xB4));
    if (OB8(o, B_FLOOR) != 3) return;
    if (!OB8(o, OBJ_coll_flags)) { defeat(o); return; }        /* (above the top: any hit) */
    if (t > 0xB4) return;
    make_spike(o, (uint16_t)(t - 1));
}

static void hold_player(int mid)                               /* [$20ED3E] dead_snc_pos / [$20ED2A] dead_snc_pos2 */
{
    uint32_t p = RAM_Player_1;
    if (mid) {
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
        SET16(p, OBJ_x_pos, 0xAC0);
        return;
    }
    if (dead(p)) return;
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_ground_vel, 0);
    if (wy(p) > 0x28C) SET16(p, OBJ_y_pos, 0x28C);
    SET16(p, OBJ_x_pos, wx(p) < 0xAC0 ? 0xA70 : 0xB10);
}

static void door_open(void)                                    /* [$20ECC6]: the top right one */
{
    for (int s = 0; s < 110; s++) {
        uint32_t a1 = o_slot(s);
        if (r32(a1) == native3(0x3E) && OB8(a1, OBJ_routine) == 4) { SET8(a1, OBJ_routine, 8); return; }
    }
}

static void gene_check(uint32_t o)                             /* [$20ED08] genecolor_setchk */
{
    if (OB8(o, B_FLAGS) & 2) return;
    if (wx(RAM_Player_1) < 0xC00) return;
    SET8(o, B_FLAGS, OB8(o, B_FLAGS) | 2);
    line1(SCDA_GENEPAL);
}

static int egg6_step(uint32_t o)                               /* the routine; 0: not drawn */
{
    static const int16_t down_y[3] = { 0x620, 0x428, 0x228 }, up_y[3] = { 0x428, 0x228, 0x130 };
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg6_start [$20E792] */
        if (wx(p) < 0xA80) return 0;
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                                   /* egg6_ini [$20E7A8] */
        if (!(s_flags & 0xF)) {
            line1(SCDA_W3BOSSPAL);                            /* (palette 6; PLC $13: resident here) */
            make_meca(o);
            init(o, SCDA_W3EGGMAN, 4, 0x18, 0x18, 0x40);
            SET8(o, B_FLAGS, OB8(o, B_FLAGS) | 1);
            coliset(o);
            s_flags = (uint8_t)((((s_flags & 0xF0) + 6) | 0x10));
            s_fight = 6;
            w8(RAM_Boss_flag, 1);                             /* (S3K's: a boss on) */
            s_bottom = r16(RAM_Camera_max_Y_pos);
            s_dest = r16(RAM_Camera_target_max_Y_pos);
            s_right = r16(RAM_Camera_max_X_pos);
            w16(RAM_Camera_min_X_pos, 0x9E0);                 /* (the shaft: 2528-2656) */
            w16(RAM_Camera_target_min_X_pos, 0x9E0);
            w16(RAM_Camera_max_X_pos, 0xA60);
            w16(RAM_Camera_target_max_X_pos, 0xA60);
            break;
        }
        SET16(o, B_TIME, OB16(o, B_TIME) + 1);
        if (OB16(o, B_TIME) != 0x78) break;
        go_down(o);
        s_music = 1;
        scd_cd_play(SCD_CD_BOSS, 1);
        break;
    case 4: {                                                 /* egg6_down [$20E87A] */
        add_spd2(o);
        int16_t d0 = down_y[OB8(o, B_FLOOR) % 3];
        if (d0 > wy(o)) break;
        SET16(o, OBJ_y_pos, d0);
        SET8(o, OBJ_routine, 6);
        SET16(o, B_TIME, 0);
        SET8(o, B_FLAME, 1);
        SET8(o, B_ROTOR, 1);
        o_sfx_cd(FM_SETTLE);
        break;
    }
    case 6:                                                   /* egg6_hover [$20E8C6] */
        if (snc_floor() < OB8(o, B_FLOOR)) { retry(o); break; }   /* (he fell below: the event again) */
        bob(o);
        if (!OB8(o, B_HITS)) {
            SET16(o, B_TIME, OB16(o, B_TIME) + 1);
            if (OBS16(o, B_TIME) < 0x12C) break;
        }
        SET8(o, B_FLAME, 0);
        SET8(o, OBJ_routine, 8);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0xFF00);
        SET16(o, B_AY, 0xFFF0);
        SET16(o, B_TIME, 0);
        break;
    case 8: {                                                 /* egg6_up [$20E956] */
        add_spd2(o);
        int16_t d0 = up_y[OB8(o, B_FLOOR) % 3];
        if (d0 < wy(o)) break;
        SET16(o, OBJ_y_pos, d0);
        SET8(o, B_ROTOR, 0);
        SET8(o, OBJ_routine, 0xA);
        SET8(o, B_SHAKE, 0xF0);
        SET8(o, B_FLOOR, OB8(o, B_FLOOR) + 1);
        if (OB8(o, B_FLOOR) != 3) {
            SET8(o, B_FLAME, 1);
            o_sfx_cd(FM_RISE);
        }
        SET8(o, B_HITS, 0);
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_frame, 0);
        break;
    }
    case 0xA: {                                               /* egg6_event [$20E9BE] */
        bob(o);
        uint16_t t = (uint16_t)(OB16(o, B_TIME) + 1);
        SET16(o, B_TIME, t);
        switch (t) {
        case 0x1F2:                                           /* beamdemo_start: the warning, the beam */
            o_sfx_cd(FM_WARN);
            make_beam(snc_floor());
            break;
        case 0x21C: {                                         /* beam_start: his floor's emitters on */
            int f = snc_floor();
            s_fight = (uint8_t)((s_fight & 0x1F) | (f == 1 ? 0x40 : f == 2 ? 0x80 : 0x20));
            break;
        }
        case 0x23A: s_flags &= (uint8_t)~0x20; break;         /* (palette cycle 4 runs) */
        case 0x258: SET8(o, B_LAUNCH, 1); break;              /* (the floor blocks spring) */
        case 0x276: s_flags |= 0x20; SET8(o, B_LAUNCH, 0); break;
        case 0x348:                                           /* event_end [$20EAB0] */
            if (OB8(o, B_FLOOR) == 3) retry(o);
            else go_down(o);
            return 1;
        }
        rakkabutu(o);
        break;
    }
    case 0xC:                                                 /* egg6_dead1 [$20EB38]: it drops */
        hold_player(0);
        if (wy(o) < 0x2A0) {
            add_spd2(o);
            SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel));  /* (jolting) */
            break;
        }
        SET8(o, OBJ_routine, 0xE);
        SET8(o, OBJ_anim, 3);
        SET8(o, OBJ_frame, 0);
        SET16(o, OBJ_y_pos, 0x2A0);
        SET16(o, B_TIME, 0);
        break;
    case 0xE: {                                               /* egg6_dead2 [$20EB6C] */
        uint16_t t = OB16(o, B_TIME);
        if (t == 0x78) s_fight &= 0x1F;                       /* (the beams off) */
        hold_player(t > 0x146);
        SET16(o, B_TIME, ++t);
        if (t == 0xB4) { door_open(); break; }
        if (t >= 0xF0) {
            s_palcyc = 0x80;                                  /* colchg_stop: the cycles held */
            if (t <= 0x144) { enkei(o, 1); break; }
        }
        if (t == 0x145) { fade_start(0); break; }
        if (t == 0x146) {                                     /* egg6_cgchg [$20EC32]: Eggman out */
            o_upload_asset(SCDA_W3EGGMAN2);                   /* (CD: PLC $14, over the fight's art) */
            o_upload_asset(SCDA_W3MECA0B);
            SET8(o, OBJ_anim, 4);
            SET8(o, OBJ_frame, 0);
            o_asset(o, SCDA_W3EGGMAN2);
            set_xy(o, 0xB30, 0x288);
            uint32_t m = lk(o);
            if (m) {
                SET16(m, OBJ_y_pos, wy(m) + 0x10);
                SET16(m, OBJ_priority, 1 << 7);
                SET8(m, OBJ_routine, OB8(m, OBJ_routine) + 2);
            }
            break;
        }
        if (t == 0x17F) { fade_start(1); break; }
        if (t >= 0x180 && t <= 0x1D4) { enkei(o, 0); break; }
        if (t != 0x1D5) break;
        s_palcyc = 0;                                         /* dead2_end [$20EC6C]: colchg_start */
        SET16(o, OBJ_y_vel, 0);
        SET16(o, B_AY, 0);
        SET16(o, OBJ_x_vel, 0x100);
        SET16(o, B_AX, 0xC);
        SET8(o, OBJ_routine, 0x10);
        SET16(o, B_TIME, 0);
        w16(RAM_Camera_max_X_pos, s_right);
        w16(RAM_Camera_target_max_X_pos, s_right);
        tri_scd_level_music();                                /* (the period's song again) */
        w8(RAM_Boss_flag, 0);
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        break;
    }
    case 0x10:                                                /* egg6_dead3 [$20ECE4]: away he flies */
        gene_check(o);
        add_spd2(o);
        if (wx(o) < 0xEF0) break;
        SET8(o, OBJ_routine, 0x12);
        s_fight = 0;
        /* fall through */
    case 0x12:                                                /* egg6_dead4 */
        gene_check(o);
        return 0;
    }
    return 1;
}

static void egg6(uint32_t o)
{
    frame_tick();
    if (s_fade) {                                             /* (the disc's loop: nothing else moves) */
        fade_frame();
        o_draw(o);
        events();
        return;
    }
    shake(o);
    hit_chk(o);
    static int autohit = -1;                                  /* development: a hit every N frames */
    if (autohit < 0) autohit = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
    if (autohit > 0 && (OB8(o, B_FLAGS) & 1) && !OB8(o, B_COOL) && !(r16(RAM_Level_frame_counter) % autohit))
        SET8(o, OBJ_coll_flags, 0);
    static int log = -1;                                      /* development */
    if (log < 0) log = getenv("TRILOGY_BOSSLOG") != NULL;
    if (log && !(r16(RAM_Level_frame_counter) & 7))
        fprintf(stderr, "[boss6] f=%u r=%02X t=%u floor=%u hits=%u coll=%02X flags=%02X fight=%02X egg=%04X,%04X p=%04X,%04X cam=%04X,%04X\n",
                (unsigned)r16(RAM_Level_frame_counter), OB8(o, OBJ_routine), OB16(o, B_TIME), OB8(o, B_FLOOR),
                OB8(o, B_HITS), OB8(o, OBJ_coll_flags), s_flags, s_fight, OB16(o, OBJ_x_pos), OB16(o, OBJ_y_pos),
                OB16(RAM_Player_1, OBJ_x_pos), OB16(RAM_Player_1, OBJ_y_pos), r16(RAM_Camera_X_pos), s_bottom);
    if (egg6_step(o)) {
        beam_ctrl();
        o_animate(o, o_asset_anim(SCDA_W3EGGMAN));
        part_draw(o);
    }
    events();
}

/* ---- $35 the machine: under the capsule; wrecked, a platform --------------- */
static void egg6meca0(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20ED92] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3MECA0, 5, 0x40, 0x30, 0x30);
        break;
    case 2: {                                                 /* [$20EDC0] */
        uint32_t b = lk(o);
        set_xy(o, wx(b), wy(b) - 0x10);
        break;
    }
    case 4:                                                   /* [$20EDD8] */
        o_asset(o, SCDA_W3MECA0B);
        SET8(o, OBJ_frame, 1);
        SET8(o, OBJ_width_px, 0x40);
        SET8(o, OBJ_y_radius, 0x10);
        o_solid(o, 0x40 + 10, 0x10, 0x11, wx(o));
        break;
    }
    part_draw(o);
}

/* ---- $36 the rotor: spins up from a frame each 17 to each one -------------- */
static void egg6meca1(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20EE14] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3ROTOR, 5, 0x10, 0x10, 0x10);
    } else {                                                  /* [$20EE42] */
        uint32_t b = lk(o);
        set_xy(o, wx(b), wy(b) - 0x40);
        int8_t st = (int8_t)OB8(b, B_ROTOR);
        if (st < 0) { o_delete(o); return; }
        if (st == 1) {
            SET8(o, R_RELOAD, 0x14);
            SET8(o, R_TIME, 1);
            SET8(b, B_ROTOR, 2);
        } else if (st) {
            SET8(o, R_TIME, OB8(o, R_TIME) - 1);
            if (!OB8(o, R_TIME)) {
                uint8_t f = (uint8_t)(OB8(o, OBJ_frame) + 1);
                SET8(o, OBJ_frame, f >= 4 ? 0 : f);           /* ($20FBF2: four frames, then $FF) */
                int8_t d0 = (int8_t)(OB8(o, R_RELOAD) - 3);
                if (d0 < 1) d0 = 1;
                SET8(o, R_RELOAD, d0);
                SET8(o, R_TIME, d0);
            }
        }
    }
    o_draw(o);
}

/* ---- $37 the jet under it: hurts ------------------------------------------ */
static void egg6meca2(uint32_t o)
{
    set_coll(o, 0xBE, 2);
    if (!OB8(o, OBJ_routine)) {                               /* [$20EEF0] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3FLAME, 5, 0x10, 0x10, 0x10);
    } else {                                                  /* [$20EF1E] */
        uint32_t b = lk(o);
        set_xy(o, wx(b), wy(b) + 0x40);
        SET8(o, OBJ_anim, OB8(b, B_FLAME));
        if (OB8(o, OBJ_anim) & 0x80) { o_delete(o); return; }
    }
    o_animate(o, o_asset_anim(SCDA_W3FLAME));
    part_draw(o);
}

/* ---- $38 a spike: falls at 3.5 px, sticks in the floor, blinks out --------- */
static void egg6toge(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20F02C] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3SPIKE, 3, 4, 0xC, 0xC);
        set_coll(o, 0xBF, 2);
        /* fall through */
    case 2: {                                                 /* [$20F064] */
        cd_move(o, 0, 0x38000);
        int16_t d1 = (int16_t)(o_floor_dist(o) - 3);
        if (d1 <= 0) {                                        /* (3 px in) */
            SET16(o, OBJ_y_pos, wy(o) - d1);
            SET8(o, OBJ_routine, 4);
            set_coll(o, 0, 0);
        }
        part_draw(o);
        return;
    }
    case 4:                                                   /* [$20F08C] */
        SET8(o, T_TIME, OB8(o, T_TIME) + 1);
        if ((int8_t)OB8(o, T_TIME) < 5) { o_draw(o); return; }
        if (OB8(o, T_TIME) != 10) return;
        SET8(o, T_COUNT, OB8(o, T_COUNT) + 1);
        if (OB8(o, T_COUNT) == 7) { o_delete(o); return; }
        SET8(o, T_TIME, 0);
        return;
    }
}

/* ---- $39 rubble: falls, breaks in four on the floor ----------------------- */
static void rubble_pieces(uint32_t o)                          /* [$20F7BA] make_gareki4 */
{
    static const int16_t v[4][4] = {
        { 0x200, -0x1C0, 0, 0x30 }, { -0x200, -0x1C0, 0, 0x30 }, { 0x100, -0x280, 0, 0x30 }, { -0x100, -0x280, 0, 0x30 },
    };
    uint32_t a1 = o;
    for (int k = 0; k < 4; k++) {
        if (k) {
            if (!(a1 = o_alloc())) return;
            SET32(a1, OBJ_code, r32(o + OBJ_code));
            SET8(a1, OBJ_routine, OB8(o, OBJ_routine));
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
            SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
            SET8(a1, OBJ_height_px, OB8(o, OBJ_height_px));
            SET8(a1, OBJ_y_radius, OB8(o, OBJ_y_radius));
            set_xy(a1, wx(o), wy(o));
        }
        o_asset(a1, SCDA_W3RUBBLE2);
        SET8(a1, OBJ_frame, 3 - k);
        SET16(a1, OBJ_x_vel, v[k][0]);
        SET16(a1, OBJ_y_vel, v[k][1]);
        SET16(a1, B_AX, v[k][2]);
        SET16(a1, B_AY, v[k][3]);
    }
}

static void egg6gareki(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20F0DE] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3RUBBLE, 3, 8, 8, 8);
        break;
    case 2: {                                                 /* [$20F10C] */
        o_animate(o, o_asset_anim(SCDA_W3RUBBLE));
        add_spd2(o);
        int16_t d1 = (int16_t)(o_floor_dist(o) - 1);
        if (d1 > 0) break;
        SET16(o, OBJ_y_pos, wy(o) - d1);
        SET8(o, OBJ_routine, 4);
        rubble_pieces(o);
        break;
    }
    case 4:                                                   /* [$20F134] */
        add_spd2(o);
        break;
    }
    if (wy(o) > 0x6F0) { o_delete(o); return; }               /* [$20F946] frameout_chk (CD: after the draw) */
    o_draw(o);
}

/* ---- $3D a floor block: drops to its floor; at the capsule's word springs up
 * (less high if the player is close), and carries who rides it; from below it
 * is nothing, falling it crushes, at the ceiling it squashes ---------------- */
static const int16_t floor_tab[3][2][4] = {                    /* [$20F21C]: floor, top, speed, gravity */
    { { 0x690, 0x400, -0x600, 0xB }, { 0x690, 0x400, -0x600, 0xE } },
    { { 0x490, 0x2E0, -0x600, 0xB }, { 0x490, 0x2E0, -0x600, 0xE } },
    { { 0x290, 0x110, -0x600, 0xB }, { 0x290, 0x110, -0x600, 0xE } },
};

static int squash(uint32_t o, uint32_t p)                      /* [$20F37E] egg6_yuka_hasami_chk: 0 = may ride */
{
    int16_t d1 = (int16_t)(wx(o) - wx(p));
    if ((d1 < 0 ? -d1 : d1) <= 0x16) {
        if (wy(p) <= 0x108) {                                 /* (against the ceiling) */
            damage(o);
            SET16(p, OBJ_y_vel, 0x200);
            return -1;
        }
        if (OBS16(o, OBJ_y_vel) > 0) {                        /* (coming down on his head) */
            d1 = (int16_t)(wy(p) - (int8_t)OB8(p, OBJ_y_radius) - wy(o));
            if (d1 >= 0 && d1 <= 0x20) { damage(o); return -1; }
        }
    }
    return wy(p) >= wy(o) ? -1 : 0;
}

static void ride(uint32_t o)                                   /* [$20F2E6] egg6_yuka_ridechk */
{
    uint32_t p = RAM_Player_1;
    uint8_t grow = OB16(o, OBJ_y_vel) ? 4 : 0;
    SET8(o, Y_GROW, grow);
    SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) + grow);
    if (o_player_active(p)) {
        if (!squash(o, p)) {
            uint16_t vy = OB16(p, OBJ_y_vel);
            if ((OB8(p, OBJ_status) & 0x0A) == 0x08) SET16(p, OBJ_y_vel, 0);   /* (on it, not in the air) */
            int r = OB8(o, OBJ_y_radius);
            o_solid_1p(o, 0, OB8(o, OBJ_width_px) + 10, r, r + 1, wx(o), 0);
            if (OB8(o, OBJ_status) & ST_P1_STAND)            /* (carried: rising, his speed is its) */
                SET16(p, OBJ_y_vel, OBS16(o, OBJ_y_vel) < 0 ? OB16(o, OBJ_y_vel) : 0);
            else
                SET16(p, OBJ_y_vel, vy);
        } else if (OB8(o, OBJ_status) & ST_P1_STAND) {        /* (not solid now: S3K would keep him on it) */
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_STAND);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
        }
    }
    SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) - grow);
}

static void egg6yuka(uint32_t o)
{
    const int16_t *t = floor_tab[OB8(o, Y_FLOOR) % 3][OB8(o, Y_NEAR) & 1];
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20F1CC] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3FLOOR, 3, 0xE, 0x10, 0x10);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, B_AY, 0xB);
        break;
    case 2:                                                   /* [$20F24C] */
        add_spd2(o);
        if (t[0] <= wy(o)) {                                  /* (on its floor) */
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, OBJ_y_pos, t[0]);
            SET8(o, OBJ_routine, 4);
            o_sfx_cd(FM_THUMP);
        } else if (t[1] >= wy(o))
            SET16(o, OBJ_y_pos, t[1]);
        break;
    case 4: {                                                 /* [$20F29A] */
        uint32_t b = lk(o);
        if (!b || !OB8(b, B_LAUNCH)) break;
        SET8(o, OBJ_routine, 2);
        int16_t d0 = (int16_t)(wx(o) - wx(RAM_Player_1));
        SET8(o, Y_NEAR, (d0 < 0 ? -d0 : d0) < 0x18);
        t = floor_tab[OB8(o, Y_FLOOR) % 3][OB8(o, Y_NEAR) & 1];
        SET16(o, OBJ_y_vel, t[2]);
        SET16(o, B_AY, t[3]);
        break;
    }
    }
    ride(o);
    o_draw(o);
}

/* ---- $3E a door (bit 0: the top right one) or a beam emitter (bit 7; bits
 * 4-6, scd_level.py: Sonic CD's subtype 2, its floor's beam bit) ------------- */
static void beamer(uint32_t o)                                 /* [$20F5DC] */
{
    if (!(s_fight >> ((OB8(o, OBJ_subtype) >> 4) & 7) & 1)) return;
    uint32_t p = RAM_Player_1;
    if (!o_player_active(p)) return;
    int16_t d = (int16_t)(wy(o) - wy(p));
    if ((d < 0 ? -d : d) >= 8) return;                        /* (his height: the beam's) */
    if (OB8(p, P_INVULN) || OB8(p, P_ROUTINE) == 6 || s_dead) return;
    damage(o);
}

static void egg6door(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    uint8_t sub = OB8(o, OBJ_subtype);
    frame_tick();
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20F460] */
        if (sub & 0x80) {
            init(o, SCDA_W3BEAMER, 2, 8, 0x10, 0x10);
            SET8(o, OBJ_routine, 0xA);
        } else if (sub & 1) {                                 /* (the way out: opens up) */
            init(o, SCDA_W3DOOR, 2, 8, 0x50, 0x50);
            SET16(o, OBJ_y_vel, 0xFE00);
            SET8(o, OBJ_routine, 4);
            SET16(o, D_TARGET, 0xA000);
            SET16(o, D_DX, 0x14);
            SET16(o, D_DY, 0xFFE6);
        } else {                                              /* (behind him: shuts down) */
            init(o, SCDA_W3DOOR, 2, 8, 0x10, 0x50);
            SET16(o, OBJ_y_vel, 0x200);
            SET8(o, OBJ_routine, 2);
            SET16(o, D_TARGET, 0x4000);
            SET16(o, D_DX, 0xFFEC);
            SET16(o, D_DY, 0xFFF6);
        }
        SET8(o, OBJ_frame, sub & 0xF);
        break;
    case 2:                                                   /* [$20F508] closewait */
        s_flags &= (uint8_t)~0xA0;
        if (wx(p) >= 0x930) s_flags |= 0x40;                  /* (the shaft's sparks off) */
        if (wx(p) >= 0x9B0) s_flags |= 0x20;                  /* (palette cycle 4 black) */
        if (wx(p) >= 0xA60) SET8(o, OBJ_routine, 8);
        break;
    case 4:                                                   /* [$20F5BC] openwait */
        if (o_player_active(p) && wx(p) > 0xB80) SET16(p, OBJ_x_pos, 0xB80);
        beamer(o);
        break;
    case 6:                                                   /* [$20F59C] closed */
        if (o_player_active(p) && wx(p) < 0xA00) SET16(p, OBJ_x_pos, 0xA00);
        beamer(o);
        break;
    case 8: {                                                 /* [$20F546] moving */
        add_spd(o);
        int16_t m = (int16_t)(OB16(o, D_MOVED) + OB16(o, OBJ_y_vel)), tg = OBS16(o, D_TARGET);
        SET16(o, D_MOVED, m);
        if (tg >= 0 ? tg > m : tg < m) break;
        SET8(o, OBJ_routine, tg >= 0 ? 6 : 0xA);
        SET8(o, OBJ_width_px, 9);
        SET8(o, OBJ_y_radius, sub & 1 ? 0x10 : 0x20);
        break;
    }
    case 0xA:
        beamer(o);
        break;
    }
    if (!(sub & 0x80)) {                                      /* (solid where its door edge is) */
        int16_t dx = OBS16(o, D_DX), dy = OBS16(o, D_DY);
        set_xy(o, wx(o) + dx, wy(o) + dy);
        int r = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 10, r, r + 1, wx(o));
        set_xy(o, wx(o) - dx, wy(o) - dy);
    }
    o_draw(o);
}

/* ---- $3F an explosion: anim 1 follows the capsule 420 frames, throwing small
 * ones (anim 0, 24 frames) while its picture shows ------------------------- */
static void egg6bakuha(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20EF5A] */
        int big = OB8(o, OBJ_anim) != 0;
        init(o, SCDA_W3BOOM, big ? 3 : 2, big ? 0xC : 0x10, 0x10, 0x10);
        SET8(o, OBJ_routine, big ? 2 : 4);
    }
    if (OB8(o, OBJ_routine) == 2) {                           /* [$20EFA8] */
        uint32_t b = lk(o);
        SET16(o, E_TIME, OB16(o, E_TIME) + 1);
        if (OB16(o, E_TIME) == 0x1A4) { o_delete(o); return; }
        set_xy(o, wx(b), wy(b) - 0x40);
        o_animate(o, o_asset_anim(SCDA_W3BOOM));
        if ((int8_t)OB8(o, OBJ_frame) >= 7) return;
        make_boom2(o);
        o_draw(o);
        return;
    }
    SET16(o, E_TIME, OB16(o, E_TIME) + 1);                    /* [$20EFF0] */
    if (OB16(o, E_TIME) == 0x18) { o_delete(o); return; }
    add_spd(o);
    o_animate(o, o_asset_anim(SCDA_W3BOOM));
    o_draw(o);
}

/* ---- $40 the beam across a floor (two halves; subtype 1 flipped): 30
 * frames, then five pictures of 3 frames (its tiles: the sparks' art) ------ */
static void egg6beam(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20F14C] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_W3BEAM, 1, 0xD0, 8, 8);
    }
    uint8_t t = (uint8_t)(OB8(o, M_TIME) + 1);                /* [$20F178] */
    SET8(o, M_TIME, t);
    if (t >= 0x1E) {
        unsigned q = (unsigned)(t - 0x1E) / 3;
        if (q == 5) { o_delete(o); return; }
        SET8(o, OBJ_frame, q);
    }
    if (OB8(o, OBJ_subtype)) SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
    o_draw(o);
}

/* ---- for the rest of the game ------------------------------------------- */
uint8_t scd_wwz3_boss_flags(void) { return s_flags; }          /* bossFlags (0 outside act 3) */
int scd_wwz3_boss_music(void) { return s_music; }              /* bossMusic */
/* colchg_set: the value (0x80 hold, 0 restart) to write into the first six
 * palette cycles' timers before they run this frame; -1 none (read once) */
int scd_wwz3_palcycle_timers(void) { int v = s_palcyc; s_palcyc = -1; return v; }
/* palette cycle k (from 0) takes its all-black colours ($200252) this frame */
int scd_wwz3_palcycle_black(int k) { return k == 3 && (s_flags & 0x20); }

void scd_wwz3_level_entry(void)
{
    s_flags = s_fight = s_music = s_dead = 0;
    s_palcyc = -1;
    s_tick = 0;
    s_frame = 0xFFFF;
    s_fade = s_fade_in = s_held = 0;
}

void scd_wwz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x15] = cd_capsule;
    t[base3 + 0x29] = cd_animal;
    t[base3 + 0x34] = egg6;
    t[base3 + 0x35] = egg6meca0;
    t[base3 + 0x36] = egg6meca1;
    t[base3 + 0x37] = egg6meca2;
    t[base3 + 0x38] = egg6toge;
    t[base3 + 0x39] = egg6gareki;
    t[base3 + 0x3D] = egg6yuka;
    t[base3 + 0x3E] = egg6door;
    t[base3 + 0x3F] = egg6bakuha;
    t[base3 + 0x40] = egg6beam;
}
