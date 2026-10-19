/*
 * scd_ccz.c — Collision Chaos (Sonic CD round 3; the disc's R31A-R32D
 * programs; decomp: R3\PLAYSP3.C, BOBIN.C, BOBINB.C, BANPA.C, POCKET.C,
 * TRAP_R3.C drumr3).
 *
 * The zone's player code (playsp: the disc's Sonic object calls it after his
 * move, in his normal and hurt routines, unless he is object-controlled) keys
 * its pinball floors on Sonic CD's own 16 px block numbers (tri_scd_block_at):
 * each period has a list of up to 16 bumper blocks, and the block met at the
 * player's leading edge (two probes: his corners in the way he is going)
 * throws him by its kind, at $700:
 *   0, 1   a round bumper of 2 x 2 blocks: away from its middle; it bursts
 *          (its four blocks become 0, the burst $29 gives 100 points)
 *   2, 3   small round: away from the block's middle, at half speed
 *   5, 13, 14  flat, across y; 6, 7 flat, across x
 *   9-11   a slope: off it by the way he came (each its own slope inside the
 *          block: above it he passes)
 *   4, 8, 12, 15  corners: one of those by the half of the block met
 * Sonic CD's jump off a flipper ($1E) is its own (ObjSonic_ChkFlipper): away
 * from a point 24 px under the flipper's pivot, harder the further out from it
 * ($A00 - $1400); standing on one he slides 1 px a frame to its tip
 * (ObjMove). Both are done here for both players: the jump replaces the
 * speeds S3K's jump gave (TH_LVL_PRE_SPRITES keeps them from before it).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <string.h>

/* trilogy_scdlevel.c: a Sonic CD block of the act's layout set at run time, its
 * picture and collision too (CD's block_wrt; scd_level.py BLOCK_ROUNDS) */
void tri_scd_block_write(int x, int y, uint16_t block);

/* this zone's asset ids (scd_level.py CD_ASSETS; -> trilogy_obj.h) */

#define CCZ_ROUND       1                  /* (ROUND_INDEX: Collision Chaos) */
#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_OBJCTRL       0x2E               /* object_control: CD's oPlayerCtrl */
#define P_INVULN        0x34               /* invulnerability_timer: CD's oPlayerHurt */
#define P_STICK         0x3C               /* stick_to_convex: CD's oPlayerStick */
#define P_JUMPING       0x40               /* jumping: CD's oPlayerJump */
#define P_INTERACT      0x42               /* interact: CD's oPlayerStandObj */
#define ST_AIR          0x02
#define ST_ROLL         0x04
#define ST_ONOBJ        0x08
#define ST_ROLLJUMP     0x10
#define ST_PUSH         0x20
#define FM_SPRING       0x98               /* (ObjSonic_ChkFlipper: a jump off a flipper) */
#define FM_BUMPER       0xBE               /* (bycol_sub0: the small round bumpers) */
#define FM_BUMPWALL     0xB5               /* (bycol_sub0: the others; the bar bumper's act_move) */
#define FM_POCKET       0xD7               /* (to_a_move1: the pocket catches him) */
#define FM_POCKETOUT    0x9F               /* (a_move7: it fires him out) */

static int s_base, s_base3 = -1;                /* (acts 1-2; act 3's: its bumpers, flippers) */
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)
#define CD_CODE3(id)    (NATIVE_CD_BASE + (uint32_t)(s_base3 + (id)) * 4u)

static uint32_t act_code(int id)                /* (this act's: 1-2, or 3) */
{
    return tri_act() == 2 && s_base3 >= 0 ? CD_CODE3(id) : CD_CODE(id);
}

static int ccz(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == CCZ_ROUND;
}

static void add_points(int tens)                /* AddPoints */
{
    D(0) = (uint32_t)tens;
    tri_jsr(S3K_HUD_AddToScore);
}

/* throw the player off: in the air, no roll-jump, not pushing, not jumping */
static void bounce_off(uint32_t p)
{
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~(ST_ROLLJUMP | ST_PUSH));
    SET8(p, P_JUMPING, 0);
}

/* ======================================================================
 * The tile bumpers (playsp / boyo)
 * ==================================================================== */
static const int16_t s_bumpers[4][16] = {                     /* by period: past, present, bad, good */
    { 325, 326, -1 },
    { 316, 326, 411, 430, 131, 132, 137, 138, 119, 118, 128, 127, 126, 125, 124, 130 },
    { 316, 326, 357, 358, 131, 132, 137, 138, 119, 118, 128, 127, 126, 125, 124, 130 },
    { 316, 326, 0, 0, 131, 132, 137, 138, 119, 118, 128, 127, 126, 125, 124, 130 },
};
static const uint8_t s_slopes[3][16] = {                      /* (bob3: the empty part's height by x) */
    { 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6 },
    { 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11 },
    { 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 15, 16, 16, 16 },
};
#define MAX_BROKEN      64
static struct { int16_t x, y; } s_broken[MAX_BROKEN];       /* (the burst bumpers' blocks, 16 px cells) */
static int s_nbroken;
static uint8_t s_passed;                                      /* col_y: through the empty part of a slope */

/* The Sonic CD block word (number, flips) at a point of the act: as
 * trilogy_scdlevel.c's tri_scd_block_at reads it (header +$E8: u16 chunks,
 * u16 w, u16 h, the FG, the chunks), but with its flags: the bumpers need the
 * x flip. A burst bumper's blocks are 0. */
static uint16_t block_at(int x, int y)
{
    int16_t cx = (int16_t)(x & ~15), cy = (int16_t)(y & ~15);
    for (int k = 0; k < s_nbroken; k++)
        if (s_broken[k].x == cx && s_broken[k].y == cy) return 0;
    uint32_t w = tri_dir(), t = w ? tri_rd32(w + 0xE8) : 0;
    if (!t || x < 0 || y < 0) return 0;
    unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4), col = (unsigned)x >> 8, row = (unsigned)y >> 8;
    if (col >= fw || row >= fh) return 0;
    unsigned ch = tri_rd8(t + 6 + row * fw + col) & 0x7F;
    if (!ch || ch > tri_rd16(t)) return 0;
    uint32_t chunks = t + 6 + ((fw * fh + 1u) & ~1u);
    return tri_rd16(chunks + (ch - 1u) * 0x200u + ((((unsigned)y >> 4) & 15u) * 16u + (((unsigned)x >> 4) & 15u)) * 2u);
}

static void block_clear(int x, int y)                        /* block_wrt(0, x, y) */
{
    if (s_nbroken < MAX_BROKEN) {
        s_broken[s_nbroken].x = (int16_t)(x & ~15);
        s_broken[s_nbroken].y = (int16_t)(y & ~15);
        s_nbroken++;
    }
    tri_scd_block_write(x, y, 0);
}

static void throw_from(uint32_t p, int x, int y, int half)   /* away from (x, y) at $700 */
{
    int16_t s, c;
    o_sincos(o_atan((int16_t)(x - OBS16(p, OBJ_x_pos)), (int16_t)(y - OBS16(p, OBJ_y_pos))), &s, &c);
    int32_t vx = ((int32_t)c * -0x700) >> 8, vy = ((int32_t)s * -0x700) >> 8;
    if (half) { vx >>= 1; vy >>= 1; }
    SET16(p, OBJ_x_vel, (uint16_t)vx);
    SET16(p, OBJ_y_vel, (uint16_t)vy);
    bounce_off(p);
}

static void bob_y(uint32_t p)                                 /* bob0: back across y */
{
    SET16(p, OBJ_y_vel, OBS16(p, OBJ_y_vel) < 0 ? 0x700 : -0x700);
    bounce_off(p);
}

static void bob_x(uint32_t p)                                 /* bob1: back across x */
{
    SET16(p, OBJ_x_vel, OBS16(p, OBJ_x_vel) < 0 ? 0x700 : -0x700);
    bounce_off(p);
}

static void bob_center(uint32_t p, int x, int y, int half)    /* bob_s (half), bob_s0: the block's middle */
{
    throw_from(p, (x & ~15) + 8, (y & ~15) + 8, half);
}

static void bob_slope(uint32_t p, int k, int x, int y, int flip)   /* bob3 (k: 9-11; 8 from bob4: none) */
{
    if (k >= 9) {
        int cx = x & 15, cy = y & 15;
        if (!flip) cx = 15 - cx;
        if (cy < s_slopes[k - 9][cx]) { s_passed = 1; return; }
    }
    uint8_t a = o_atan(OBS16(p, OBJ_x_vel), OBS16(p, OBJ_y_vel));
    a = (uint8_t)(-(int8_t)(uint8_t)(a + 0x80) - 0x20);
    if (flip) a += 0x40;
    int16_t s, c;
    o_sincos(a, &s, &c);
    SET16(p, OBJ_x_vel, (uint16_t)(((int32_t)c * -0x700) >> 8));
    SET16(p, OBJ_y_vel, (uint16_t)(((int32_t)s * -0x700) >> 8));
    bounce_off(p);
}

static void bob_break(uint32_t p, int k, int x, int y, int flip)   /* bob */
{
    y &= ~15;
    if (k == 0) y += 16;                                      /* (the top blocks: the middle is under them) */
    x &= ~15;
    if (!flip) x += 16;
    throw_from(p, x, y, 0);
    uint32_t a1 = o_alloc();
    if (a1) {
        SET32(a1, OBJ_code, act_code(0x29));
        SET16(a1, OBJ_x_pos, (uint16_t)x);
        SET16(a1, OBJ_y_pos, (uint16_t)y);
    }
    block_clear(x, y);
    block_clear(x - 16, y);
    block_clear(x - 16, y - 16);
    block_clear(x, y - 16);
    tri_scd_voice(SCD_PCM_BREAK);
}

/* bycol_sub0: the block at (x, y) a bumper of the period's? 1: thrown (the
 * other probe is not tried) */
static int bumper_probe(uint32_t p, int x, int y)
{
    uint16_t w = block_at(x, y), b = w & 0x7FF;
    if (!b) return 0;
    int per = cd_period(), list = per == TRI_CD_PAST ? 0 : per == TRI_CD_PRESENT ? 1 : per == TRI_CD_BAD ? 2 : 3;
    int k = 0;
    while (k < 16 && s_bumpers[list][k] >= 0 && s_bumpers[list][k] != b) k++;
    if (k >= 16 || s_bumpers[list][k] < 0) return 0;
    if (OB8(p, P_ROUTINE) == 4) {                             /* (hurt: back to normal) */
        SET8(p, P_ROUTINE, 2);
        SET8(p, P_INVULN, 120);
    }
    s_passed = 0;
    int flip = (w & 0x800) != 0, lo = (x & 15) < 8;
    switch (k) {
    case 0: case 1: bob_break(p, k, x, y, flip); break;
    case 2: case 3: bob_center(p, x, y, 1); break;
    case 4: if (lo != flip) bob_center(p, x, y, 0); else bob_y(p); break;          /* bob5 */
    case 5: case 13: case 14: bob_y(p); break;
    case 6: case 7: bob_x(p); break;
    case 8: if (flip) { if (lo) bob_x(p); else bob_y(p); } else bob_slope(p, 8, x, y, flip); break;   /* bob4 */
    case 9: case 10: case 11: bob_slope(p, k, x, y, flip); break;
    case 12: if (lo == flip) bob_x(p); else bob_y(p); break; /* bob2 (the disc's: not the PC's) */
    default: bob_center(p, x, y, 0); break;                   /* 15: bob_s0 */
    }
    if (k >= 2) o_sfx_cd(k == 2 || k == 3 ? FM_BUMPER : FM_BUMPWALL);   /* (0, 1: the burst's PCM only) */
    return !s_passed;
}

static void tile_bumpers(uint32_t p)                          /* boyo */
{
    int x = OBS16(p, OBJ_x_pos), y = OBS16(p, OBJ_y_pos);
    int xr = (int8_t)OB8(p, P_XRAD), yr = (int8_t)OB8(p, P_YRAD);
    switch ((uint8_t)(o_atan(OBS16(p, OBJ_x_vel), OBS16(p, OBJ_y_vel)) - 0x20) & 0xC0) {
    case 0x00:                                                /* down */
        if (!bumper_probe(p, x - xr, y + yr)) bumper_probe(p, x + xr, y + yr);
        break;
    case 0x40:                                                /* left */
        if (!bumper_probe(p, x - xr, y - (yr - 6))) bumper_probe(p, x - xr, y + yr);
        break;
    case 0x80:                                                /* up */
        if (!bumper_probe(p, x - xr, y - yr)) bumper_probe(p, x + xr, y - yr);
        break;
    default:                                                  /* right */
        if (!bumper_probe(p, x + xr, y - (yr - 6))) bumper_probe(p, x + xr, y + yr);
        break;
    }
}

/* ======================================================================
 * The pinball pieces
 * ==================================================================== */

/* ---- $1D bumper: a touch (CD's special touch $17, 8 x 8) throws the player
 * away from its middle at $700, 100 points the first five times; subtype bits
 * 1 / 0: it moves 64 / 32 px to and fro (both: 96) at 1 px a frame, bit 7
 * along x, else y, bit 6 starting forward -------------------------------- */
#define BM_SPD   0x30                  /* oVar2A (16.16) */
#define BM_TIME  0x34                  /* oVar2E: frames to the turn */
#define BM_RANGE 0x36                  /* oVar30 */
#define BM_X0    0x38                  /* oVar32 */
#define BM_SCORE 0x3A                  /* oVar34 */
static void bumper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BUMPER, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, BM_X0, OB16(o, OBJ_x_pos));
        SET16(o, BM_SCORE, 5);
        uint8_t sub = OB8(o, OBJ_subtype);
        uint16_t d0 = (uint16_t)((sub & 2 ? 0x40 : 0) + (sub & 1 ? 0x20 : 0));
        SET16(o, BM_RANGE, d0);
        SET16(o, BM_TIME, d0 >> 1);
        if (d0 >> 1) SET32(o, BM_SPD, sub & 0x40 ? 0x10000 : (uint32_t)-0x10000);
    }
    int hit = o_s1_touch(o, 8, 8);                            /* (the player's touch: before it moves) */
    int32_t spd = (int32_t)r32(o + BM_SPD);
    if (spd) {
        if (OB8(o, OBJ_subtype) & 0x80) cd_move(o, spd, 0); else cd_move(o, 0, spd);
        SET16(o, BM_TIME, OB16(o, BM_TIME) - 1);
        if (!OB16(o, BM_TIME)) {
            SET16(o, BM_TIME, OB16(o, BM_RANGE));
            SET32(o, BM_SPD, (uint32_t)-spd);
        }
    }
    if (hit) {
        if (OB16(o, BM_SCORE)) {
            SET16(o, BM_SCORE, OB16(o, BM_SCORE) - 1);
            add_points(10);
            o_score(o, 0);
        }
        if (OB8(o, OBJ_render) & 0x80) tri_scd_voice(SCD_PCM_BUMPER);
        for (int n = 0; n < 2; n++)                           /* (Sonic CD: player 1) */
            if (hit >> n & 1) throw_from(o_player(n), OBS16(o, OBJ_x_pos), OBS16(o, OBJ_y_pos), 0);
        SET8(o, OBJ_anim, 1);
        /* (CD also counts the hits in its saved state, up to 10: nothing reads them) */
    }
    o_animate(o, o_asset_anim(SCDA_BUMPER));
    if (cd_despawn(o, OB16(o, BM_X0))) return;
    o_draw(o);
}

/* ---- $1E flipper: a sloped floor (its height across it: the tables; from
 * under it the other); it flips (animation 1) when a player jumps off it ---- */
static const int8_t s_flip_y[129] = {                         /* frip_posi, frip_posi_r (+ the byte after) */
    14, 13, 12, 11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -3,
    -3, -4, -4, -5, -5, -6, -6, -7, -7, -8, -8, -9, -9, -10, -10, -11, -11, -11, -12, -12, -12, -12, -12, -12,
    -11, -11, -10, -10, -9, -8, -6, -4,
    18, 19, 20, 20, 20, 20, 20, 20, 20, 19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17,
    17, 16, 16, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 12, 12,
    12, 12, 11, 11, 10, 10, 9, 8, 6, 4, 0,
};
static struct { uint8_t on; uint32_t flipper; int16_t x, y, xvel, yvel; } s_pre[2];   /* (before S3K's jump) */

static void get_off_1p(uint32_t o, int n)       /* GetOffObject (ride_on_clr), one player */
{
    uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
    if (!(OB8(o, OBJ_status) & bit)) return;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
    uint32_t p = o_player(n);
    if (!(OB8(p, OBJ_status) & ST_ONOBJ) || OB16(p, P_INTERACT) != (uint16_t)o) return;
    SET8(p, P_STICK, 0);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~ST_ONOBJ);
}

static void ride_1p(uint32_t o, int n)          /* StandOnObject (ride_on_set), one player */
{
    uint32_t p = o_player(n);
    uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
    if (OB8(p, P_ROUTINE) == 4) {                             /* (hurt: back to normal) */
        SET8(p, P_ROUTINE, 2);
        SET8(p, P_INVULN, 120);
    }
    SET8(p, P_JUMPING, 0);
    if (!(OB8(o, OBJ_status) & bit)) {
        SET8(o, OBJ_status, OB8(o, OBJ_status) | bit);
        uint8_t st = (uint8_t)(OB8(p, OBJ_status) & ~ST_ROLLJUMP);
        if (st & ST_ROLL) {                                   /* (out of a ball: walking) */
            st &= (uint8_t)~ST_ROLL;
            SET8(p, P_YRAD, OB8(p, 0x44));                    /* (default_y_radius, x) */
            SET8(p, P_XRAD, OB8(p, 0x45));
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - (OB8(p, 0x38) == 1 ? 1 : 5));   /* (Tails stands $F high) */
            SET8(p, P_ANIM, 0);
        }
        SET8(p, OBJ_status, st);
    }
    if (OB8(p, OBJ_status) & ST_ONOBJ) {
        if (OB16(p, P_INTERACT) == (uint16_t)o) return;       /* (on us already) */
        uint32_t old = 0xFF0000u | OB16(p, P_INTERACT);       /* (off what he stood on) */
        SET8(old, OBJ_status, OB8(old, OBJ_status) & ~bit);
    }
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ONOBJ);
    SET16(p, P_INTERACT, (uint16_t)o);
    SET8(p, OBJ_angle, 0);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~ST_AIR);
}

static void flip_check(uint32_t o, int n)       /* frip_chk */
{
    uint32_t p = o_player(n);
    if (!o_player_active(p) || OB8(p, P_ROUTINE) >= 6 || r16(RAM_Debug_placement_mode)) { get_off_1p(o, n); return; }
    int w = OB8(o, OBJ_width_px);
    int d0 = OBS16(p, OBJ_x_pos) - OBS16(o, OBJ_x_pos) + w;
    if (d0 < 0 || d0 >= 2 * w) { get_off_1p(o, n); return; }
    if (OB8(o, OBJ_status) & ST_XFLIP) d0 = 64 - d0;
    int up = OBS16(p, OBJ_y_vel) < 0;
    int16_t top = (int16_t)(s_flip_y[(up ? 64 : 0) + d0] + OBS16(o, OBJ_y_pos));
    int r = (int8_t)OB8(p, P_YRAD);
    if (up) r = -r;
    int d3 = r + OBS16(p, OBJ_y_pos) - top + 8;
    if (d3 < 0 || d3 >= 16) { get_off_1p(o, n); return; }
    SET16(p, OBJ_y_pos, (uint16_t)(top - r));
    if (!up)
        ride_1p(o, n);
    else {                                                    /* (from under it: his head stops) */
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
    }
}

static void flip_launch(uint32_t o, int n)      /* ObjSonic_ChkFlipper: S3K jumped; CD's speeds instead */
{
    uint32_t p = o_player(n);
    o_sfx_cd(FM_SPRING);
    SET8(o, OBJ_anim, 1);
    uint8_t a = o_atan((int16_t)(OBS16(o, OBJ_x_pos) - s_pre[n].x), (int16_t)(OBS16(o, OBJ_y_pos) + 24 - s_pre[n].y));
    int d3 = s_pre[n].x - OBS16(o, OBJ_x_pos) + OB8(o, OBJ_width_px);
    if (!(OB8(o, OBJ_status) & ST_XFLIP)) d3 = 64 - d3;
    int16_t spd = (int16_t)(-0xA00 + (int16_t)((-0xA00 * d3) / 64));
    int16_t s, c;
    o_sincos(a, &s, &c);
    SET16(p, OBJ_x_vel, (uint16_t)(s_pre[n].xvel + (int16_t)(((int32_t)c * spd) >> 8)));
    SET16(p, OBJ_y_vel, (uint16_t)(s_pre[n].yvel + (int16_t)(((int32_t)s * spd) >> 8)));
}

static void flipper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_FLIPPER, 4, 1, 0x20, 0x10);           /* (its flips: the status's, by the animation) */
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);      /* (no balancing on it: S3K's test, as CD's) */
    }
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
        if (s_pre[n].on && s_pre[n].flipper == o) {
            if ((OB8(p, OBJ_status) & ST_AIR) && OB8(p, P_JUMPING)) flip_launch(o, n);   /* (jumped off it) */
            s_pre[n].on = 0;
        }
        if (!(OB8(o, OBJ_status) & bit)) continue;
        if (OB8(p, OBJ_status) & ST_AIR) { get_off_1p(o, n); continue; }   /* (S3K: off it at once) */
        if (!(OB8(p, OBJ_status) & ST_ONOBJ) || OB16(p, P_INTERACT) != (uint16_t)o) continue;
        SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (OB8(o, OBJ_status) & ST_XFLIP ? 0x10000u : (uint32_t)-0x10000));
    }
    if (!OB8(o, OBJ_anim)) {
        flip_check(o, 0);
        flip_check(o, 1);                                     /* (Sonic CD: player 1) */
    }
    o_animate(o, o_asset_anim(SCDA_FLIPPER));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        get_off_1p(o, 0);
        get_off_1p(o, 1);
        return;
    }
    o_draw(o);
}

/* ---- $29 a tile bumper's burst: 100 points, two frames, gone ------------- */
static void bumper_break(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BUMPERBREAK, 4, 3, 0x10, 0x10);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        add_points(10);
        o_score(o, 0);
        /* fall through */
    case 2:                                                   /* (its script ends $FC: routine 4) */
        o_animate(o, o_asset_anim(SCDA_BUMPERBREAK));
        o_draw(o);
        return;
    default:
        o_delete(o);
        return;
    }
}

/* ---- $2A bar bumper: a touch (CD's $27, 32 x 16) throws the player off by
 * where he meets it: level with it, he stops; over or under its middle, up or
 * down at $700; at its ends, off it diagonally at $4F3 each way. It moves
 * 1 px a frame to and fro, 192 px (subtype bit 7: along x; else 160 along y),
 * bit 6 starting forward ------------------------------------------------- */
#define BB_SPD   0x30                  /* oVar2A (16.16) */
#define BB_TIME  0x34                  /* oVar2E */
#define BB_RANGE 0x36                  /* oVar30 */
#define BB_X0    0x44                  /* oVar3E */
static void bar_bumper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET16(o, BB_X0, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BARBUMPER, 4, 1, 0x20, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        int across = OB8(o, OBJ_subtype) & 0x80;
        SET16(o, BB_RANGE, across ? 0xC0 : 0xA0);
        SET16(o, BB_TIME, across ? 0x60 : 0x50);
        SET32(o, BB_SPD, OB8(o, OBJ_subtype) & 0x40 ? 0x10000 : (uint32_t)-0x10000);
    }
    int hit = o_s1_touch(o, 0x20, 0x10);
    if (hit) {
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_BUMPWALL);
        for (int n = 0; n < 2; n++) {                         /* (Sonic CD: player 1) */
            if (!(hit >> n & 1)) continue;
            uint32_t p = o_player(n);
            int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
            int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
            if ((uint16_t)(dy + 8) < 0x10) { SET16(p, OBJ_x_vel, 0); continue; }
            if ((uint16_t)(dx + 0x18) < 0x30)
                SET16(p, OBJ_y_vel, dy >= 0 ? 0x700 : -0x700);
            else {
                SET16(p, OBJ_x_vel, dx >= 0 ? 0x4F3 : -0x4F3);
                SET16(p, OBJ_y_vel, dy >= 0 ? 0x4F3 : -0x4F3);
            }
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~(ST_ROLLJUMP | ST_PUSH));
        }
    }
    int32_t spd = (int32_t)r32(o + BB_SPD);
    if (OB8(o, OBJ_subtype) & 0x80) cd_move(o, spd, 0); else cd_move(o, 0, spd);
    SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
    if (!OB16(o, BB_TIME)) {
        SET16(o, BB_TIME, OB16(o, BB_RANGE));
        SET32(o, BB_SPD, (uint32_t)-spd);
    }
    if (cd_despawn(o, OB16(o, BB_X0))) return;
    o_draw(o);
}

/* ---- $2F kick-out pocket (gone in a good future): player 1 falling on to it
 * (within 24 px above, 24 either side) is caught, curled and hidden; it shuts,
 * counts 100 points twice (seven times in all), opens and fires him up at
 * $800; then it rests 2 s. Subtype $FF: the pocket, else its lid ------------ */
#define PK_TIME  0x30                  /* oVar2A */
#define PK_LID   0x32                  /* oVar2C */
#define PK_PAR   0x34                  /* oVar2E: (the lid) its pocket */
#define PK_SCORE 0x36                  /* oVar30 */
#define PK_HELD  0x44                  /* oVar3E */
static int pk_tick(uint32_t o, int next, int time, int frame)  /* (a step: on when its time is out) */
{
    SET16(o, PK_TIME, OB16(o, PK_TIME) - 1);
    if (OBS16(o, PK_TIME) >= 0) return 0;
    SET8(o, OBJ_routine, next);
    SET16(o, PK_TIME, (uint16_t)time);
    if (frame >= 0) SET8(o, OBJ_frame, frame);
    return 1;
}

static void pocket_lid(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_POCKETLID, 4, 0, 0x18, 8);
        SET8(o, OBJ_y_radius, 8);
        /* fall through */
    case 2: {
        uint32_t par = 0xFF0000u | OB16(o, PK_PAR);
        if (r32(par) != CD_CODE(0x2F) || !(OB8(par, OBJ_subtype) & 0x80)) { o_delete(o); return; }
        o_animate(o, o_asset_anim(SCDA_POCKETLID));
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
        return;
    }
}

static void pocket(uint32_t o)
{
    if (!(OB8(o, OBJ_subtype) & 0x80)) { pocket_lid(o); return; }
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (cd_period() == TRI_CD_GOOD) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_POCKET, OB8(o, OBJ_render) | 4, 0, 0x18, 8);
        SET16(o, PK_SCORE, 7);
        /* fall through */
    case 2: {                                                 /* waiting */
        if (r16(RAM_Debug_placement_mode)) break;
        int r = OB8(p, P_ROUTINE);
        if (OBS16(p, OBJ_y_vel) < 0 || r == 4 || r == 6 || !o_player_active(p) ||
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x18) >= 0x10 ||
            (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x18) >= 0x30)
            break;
        SET8(o, PK_HELD, 0xFF);                               /* (caught) */
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
        SET8(p, P_YRAD, 14);
        SET8(p, P_XRAD, 7);
        SET8(p, P_ANIM, 2);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, (uint16_t)-0x800);                /* (the speed he leaves with) */
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - 16);
        SET8(o, OBJ_routine, 4);
        SET16(o, PK_TIME, 8);
        SET8(o, OBJ_frame, 1);
        o_sfx_cd(FM_POCKET);
        break;
    }
    case 4: {                                                 /* shutting: the lid */
        if (!pk_tick(o, 6, 6, 2)) break;
        uint32_t a1 = o_alloc();
        if (!a1) {                                            /* (CD: gone, him still held; let him go) */
            if (OB8(o, PK_HELD)) SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
            o_delete(o);
            return;
        }
        SET16(o, PK_LID, (uint16_t)a1);
        SET16(a1, PK_PAR, (uint16_t)o);
        SET32(a1, OBJ_code, CD_CODE(0x2F));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        break;
    }
    case 6:  pk_tick(o, 8, 2, 3); break;
    case 8:  pk_tick(o, 10, 30, 0); break;
    case 10: case 12:                                         /* (100 points, while it has them) */
        if (pk_tick(o, OB8(o, OBJ_routine) + 2, 30, -1) && OB16(o, PK_SCORE)) {
            SET16(o, PK_SCORE, OB16(o, PK_SCORE) - 1);
            add_points(10);
            o_score(o, 0);
        }
        break;
    case 14: pk_tick(o, 16, 4, 3); break;
    case 16: pk_tick(o, 18, 6, 2); break;
    case 18:                                                  /* open: out he goes */
        if (!pk_tick(o, 20, 120, 0)) break;
        o_sfx_cd(FM_POCKETOUT);
        SET8(0xFF0000u | OB16(o, PK_LID), OBJ_routine, 4);
        if (OB8(o, PK_HELD)) {
            SET8(o, PK_HELD, 0);
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
        }
        break;
    default:                                                  /* resting */
        SET16(o, PK_TIME, OB16(o, PK_TIME) - 1);
        if (OBS16(o, PK_TIME) < 0) { SET8(o, OBJ_routine, 2); SET8(o, PK_HELD, 0); }
        break;
    }
    if (OB8(o, PK_HELD) && OB8(o, OBJ_routine) >= 6) SET8(p, P_INVULN, 3);   /* (oPlayerCtrl bit 6: unseen) */
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        if (OB8(o, PK_HELD)) SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
        return;
    }
    o_draw(o);
}

/* ---- $28 drum (act 2): a slat round a turning drum (the drum is the
 * level's own art); 18 steps of 4 ticks, a tick (moving it its step's speed
 * down or up) every 11th frame, as the drum's art turns; standing it only on
 * steps 2-5 (the top); frozen in the past. Subtype: its first step ---------- */
#define DR_Y0    0x36                  /* oVar30 */
#define DR_X0    0x38                  /* oVar32 */
#define DR_SPD   0x3C                  /* oVar36 (its high byte: px a tick) */
#define DR_COUNT 0x40                  /* oVar3A */
#define DR_STEP  0x41                  /* oVar3B */
static int8_t s_drum_clock;                     /* (the drum art's timer: $F66C) */

static void drum_step(uint32_t o)               /* drumr3_cntset */
{
    static const int8_t steps[18][3] = {                      /* speed, y radius, frame */
        { 1, 8, 0 }, { 5, 8, 1 }, { 6, 12, 2 }, { 8, 11, 3 }, { 8, 11, 4 }, { 8, 11, 5 }, { 6, 11, 6 },
        { 5, 8, 7 }, { 1, 8, 8 }, { -1, 4, 9 }, { -5, 4, 10 }, { -6, 8, 10 }, { -8, 8, 10 }, { -8, 8, 10 },
        { -8, 8, 10 }, { -6, 8, 10 }, { -5, 8, 10 }, { -1, 4, 11 },
    };
    static const uint8_t prio[12] = { 5, 4, 3, 2, 2, 2, 3, 4, 5, 6, 6, 6 };
    const int8_t *t = steps[OB8(o, DR_STEP) % 18];
    SET8(o, DR_SPD, t[0]);
    SET8(o, OBJ_y_radius, t[1]);
    SET8(o, OBJ_frame, t[2]);
    SET8(o, DR_COUNT, 4);
    SET16(o, OBJ_priority, (uint16_t)(prio[t[2]] << 7));
}

static int drum_tick(uint32_t o)                /* 1 = a tick moved it (drumr3_addspd) */
{
    if (cd_period() == TRI_CD_PAST || s_drum_clock) return 0;
    int16_t v = (int16_t)OB16(o, DR_SPD);
    SET16(o, OBJ_y_vel, (uint16_t)v);
    cd_move(o, 0, (int32_t)v << 8);
    return 1;
}

static void drum_solid(uint32_t o)              /* drumr3_ridechk */
{
    if (!(OB8(o, OBJ_render) & 0x80)) return;
    if (cd_period() == TRI_CD_PAST) {
        int f = OB8(o, OBJ_frame);
        if (f == 2) SET8(o, OBJ_y_radius, 8);
        if (f > 2 && f < 7) SET8(o, OBJ_y_radius, 5);
    }
    o_platform(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB16(o, OBJ_x_pos));
}

static void drum(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_init(o, SCDA_DRUM, OB8(o, OBJ_render) | 4, 3, 0x18, 0x10);
        SET16(o, DR_X0, OB16(o, OBJ_x_pos));
        SET16(o, DR_Y0, OB16(o, OBJ_y_pos));
        uint8_t sub = OB8(o, OBJ_subtype);
        SET8(o, DR_STEP, sub);
        SET8(o, OBJ_routine, sub >= 6 ? 6 : sub >= 2 ? 4 : 2);
        break;
    }
    case 2: case 6: {                                         /* behind, or under: not stood on */
        int last = OB8(o, OBJ_routine) == 2 ? 2 : 18;
        for (;;) {
            if (!OB8(o, DR_COUNT)) { cd_get_off(o); drum_step(o); break; }
            if (!drum_tick(o)) break;
            SET8(o, DR_COUNT, OB8(o, DR_COUNT) - 1);
            if (OB8(o, DR_COUNT)) break;
            SET8(o, DR_STEP, OB8(o, DR_STEP) + 1);
            if (OB8(o, DR_STEP) < last) continue;
            if (last == 18) { SET16(o, DR_COUNT, 0); SET8(o, OBJ_routine, 2); }
            else SET8(o, OBJ_routine, 4);
            break;
        }
        break;
    }
    default:                                                  /* 4: the top: stood on */
        for (;;) {
            drum_solid(o);
            if (!OB8(o, DR_COUNT)) { drum_step(o); break; }
            if (!drum_tick(o)) break;
            SET8(o, DR_COUNT, OB8(o, DR_COUNT) - 1);
            if (OB8(o, DR_COUNT)) break;
            SET8(o, DR_STEP, OB8(o, DR_STEP) + 1);
            if (OB8(o, DR_STEP) < 6) continue;
            SET8(o, OBJ_routine, 6);
            break;
        }
        break;
    }
    o_draw(o);
    if (o_out_of_range(o, OB16(o, DR_X0))) { cd_get_off(o); o_delete_respawnable(o); }
}

/* ======================================================================
 * Registration and the player hook
 * ==================================================================== */
void cd_spin_tag(uint32_t o);                                 /* scd_ppz.c (Collision Chaos' $35: the same code) */
void cd_generator(uint32_t o);                                /* (its $37: the same code) */
void scd_ccz_plat_register(TriObjFn *t, int base);            /* scd_ccz_plat.c */
void scd_ccz_badnik_register(TriObjFn *t, int base);          /* scd_ccz_badnik.c */
void scd_ccz_amy_register(TriObjFn *t, int base);             /* scd_ccz_amy.c */
void scd_ccz_amy_level(void);

void cd_bumper(uint32_t o) { bumper(o); }                    /* (Metallic Madness act 3's $1D: the same code) */

void scd_ccz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x1D] = bumper;
    t[base + 0x1E] = flipper;
    t[base + 0x28] = drum;
    t[base + 0x29] = bumper_break;
    t[base + 0x2A] = bar_bumper;
    t[base + 0x2F] = pocket;
    t[base + 0x35] = cd_spin_tag;
    t[base + 0x37] = cd_generator;
    scd_ccz_plat_register(t, base);
    scd_ccz_badnik_register(t, base);
    scd_ccz_amy_register(t, base);
}

/* Act 3 (R33C, R33D: the boss's pinball table) places the same bumpers,
 * flippers, spikes and spin tags (the same code); its tile bumpers too */
void spikes_ccz(uint32_t o);                                  /* scd_ccz_plat.c */
void scd_ccz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x1D] = bumper;
    t[base3 + 0x1E] = flipper;
    t[base3 + 0x20] = spikes_ccz;
    t[base3 + 0x29] = bumper_break;
    t[base3 + 0x35] = cd_spin_tag;
}

static int bumper_floor(uint32_t p)             /* (playsp runs: his normal routine, or hurt) */
{
    if (!o_player_active(p) || r16(RAM_Debug_placement_mode)) return 0;
    int r = OB8(p, P_ROUTINE);
    return r == 4 || (r == 2 && !(OB8(p, P_OBJCTRL) & 1));
}

void scd_ccz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {
        extern void scd_ccz3_level_entry(void);
        scd_ccz3_level_entry();                               /* (scd_ccz_boss.c: its flags) */
        scd_ccz_amy_level();
        s_nbroken = 0;
        s_drum_clock = 0;
        memset(s_pre, 0, sizeof s_pre);
    }
    if (!tri_dir() || !ccz()) return;
    if (pc == TH_LVL_PRE_SPRITES) {
        if (--s_drum_clock < 0) s_drum_clock = 10;            /* (the drum art's: every 11th frame) */
        for (int n = 0; n < 2; n++) {                         /* (the flipper's jump: from what was before it) */
            uint32_t p = o_player(n), f = 0xFF0000u | OB16(p, P_INTERACT);
            s_pre[n].on = o_player_active(p) && (OB8(p, OBJ_status) & (ST_ONOBJ | ST_AIR)) == ST_ONOBJ &&
                          OB16(p, P_INTERACT) && r32(f) == act_code(0x1E);
            s_pre[n].flipper = f;
            s_pre[n].x = OBS16(p, OBJ_x_pos);
            s_pre[n].y = OBS16(p, OBJ_y_pos);
            s_pre[n].xvel = OBS16(p, OBJ_x_vel);
            s_pre[n].yvel = OBS16(p, OBJ_y_vel);
        }
        return;
    }
    if (pc != TH_LVL_POST_SPRITES) return;
    for (int n = 0; n < 2; n++) {                             /* (Sonic CD: player 1) */
        uint32_t p = o_player(n);
        if (bumper_floor(p)) tile_bumpers(p);
    }
}
