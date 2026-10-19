/*
 * scd_qqz_gim.c — Quartz Quadrant's rocks, platforms and machines (Sonic CD;
 * decomp: R5\IWA5.C, HARI5F.C, HARIR5.C, IWA5ROLL.C, KUZURE5.C, IWA5WAVE.C,
 * BURANKO5.C, HASIRA5.C, HASHI5.C, DAI_RD5.C, MOVIE5.C, FRIEND5.C, ET5.C;
 * the disc's code, the same in both acts and every period).
 *
 *   $24 rock           solid; subtype 1: falls on a player passing under it,
 *                      hurts, and breaks on the floor or on him
 *   $25 stalactite     falls on a player passing under it; hurts
 *   $26 spikes         four ways (subtype & 3), solid; upright they hurt who
 *                      stands on them, the others hurt to the touch; subtype
 *                      bit 2: in and out 32 px, a second each way
 *   $27 rock ring      eight blocks turning round its middle, 96 px out (the
 *                      past slowest, the futures fastest; subtype: the other
 *                      way); solid
 *   $28 ledge          crumbles a column at a time from the end behind the
 *                      player who stood on it
 *   $29 wave rocks     two platforms, 128 px apart, drifting left, bobbing
 *                      96 px up and down
 *   $2A swing          (act 2) a seat on a chain of links, round and round or
 *                      to and fro; its top is a belt (2 px a frame, the belts'
 *                      way)
 *   $2B pillar         solid; rolling into it breaks it into eight blocks (and
 *                      halves the roller's speed)
 *   $2C projector      Metal Sonic's (Palmtree Panic's, at this round's ids)
 *   $2D animals        Palmtree Panic's (cd_animal); the projector's holograms
 *   $2E generator      Palmtree Panic's (cd_generator)
 *   $2F bridge         the sagging log bridge: S3K's tension bridge, 8 logs
 *   $30 platform       25 kinds (subtype & $1F): still, to and fro, up and
 *                      down, falling or rising when stood on; belt tops,
 *                      springs riding on top; subtype bit 7: the other way
 * Sonic CD's special touches (oColStatus: the rock's $38, the pillar's $2E,
 * the projector's $3B) are S1 box tests here (o_s1_touch, CD's sizes). CD's
 * object variables oVar2A.. sit at S3K's $30.. (oVarNN at $NN + 6).
 */
#include "trilogy_obj.h"

int scd_qqz_colrev(void);                  /* scd_qqz.c: colrevflag */
int scd_time_warping(void);                /* scd_time.c */
int scd_proj_destroyed(void);
void scd_proj_destroyed_set(void);
void cd_animal(uint32_t o);                /* scd_ppz.c (shared) */
void cd_generator(uint32_t o);

/* asset ids (-> trilogy_obj.h) */

#define P_ANIM          0x20
#define FM_CRUMBLE      0xA3               /* (kuzure5 main_check: the ledge stood on) */
#define FM_PILLAR       0xB0               /* (hasira5 m_wait: the pillar breaks) */
#define FM_EXPLODE      0x9E               /* (iwa5 m_fall: the rock breaks; m_baku: the projector's blasts) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

/* ---- shared ------------------------------------------------------------- */
static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

static void radii(uint32_t o, int xr, int yr)
{
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_width_px, xr);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_height_px, yr);
}

static int32_t lval(uint32_t o, int f) { return (int32_t)r32(o + (uint32_t)f); }

/* the player 1 box test: (player - o - lo) < size, unsigned, down and across */
static int box(uint32_t o, int dy0, int h, int dx0, int w)
{
    uint32_t p = RAM_Player_1;
    return (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - dy0) < (unsigned)h &&
           (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) - dx0) < (unsigned)w;
}

static int below_player(uint32_t o)             /* (224 px or more below player 1: gone) */
{
    return (int16_t)(OB16(RAM_Player_1, OBJ_y_pos) - OB16(o, OBJ_y_pos)) <= -0xE0;
}

/* ObjGetFloorDist2 at (x, y + dy): S3K's floor test adds a signed y radius */
static int16_t floor_at(uint32_t o, int dy)
{
    uint8_t r = OB8(o, OBJ_y_radius);
    SET8(o, OBJ_y_radius, (uint8_t)dy);
    int16_t d1 = o_floor_dist(o);
    SET8(o, OBJ_y_radius, r);
    return d1;
}

/* a still explosion where o is, o's own sound (CD's explosion, oRoutine2 1:
 * no points; S3K's, routine 6: no animal, no sound) */
static void explode(uint32_t o)
{
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    uint8_t render = OB8(o, OBJ_render);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
    SET8(o, OBJ_render, render & 0x80);
    SET8(o, OBJ_routine, 6);
}

/* pcolplay: a hurting touch, unless the player is warping (S3K's own check
 * does invincibility and the flashing) */
static void hurt(uint32_t p, uint32_t o)
{
    if (!scd_time_warping()) o_hurt_player(p, o);
}

/* ---- $24 rock ------------------------------------------------------------ */
#define RK_VX    0x30                  /* oVar2A (long: a piece's) */
#define RK_VY    0x34                  /* oVar2E (long) */
static const struct { int8_t dx, dy; int32_t vx, vy; } rk_pieces[4] = {
    { -8, -8, -0x10000, -0x40000 }, { 8, -8, 0x10000, -0x40000 },
    { -8, 8, -0x20000, -0x30000 }, { 8, 8, 0x20000, -0x30000 },
};

static void rock_break(uint32_t o)
{
    for (int k = 0; k < 4; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, CD_CODE(0x24));
        SET8(a1, OBJ_subtype, 0xFF);
        SET16(a1, OBJ_priority, 3 << 7);
        radii(a1, 8, 8);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + rk_pieces[k].dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + rk_pieces[k].dy);
        SET8(a1, OBJ_render, OB8(o, OBJ_render));
        SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
        SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
        SET32(a1, RK_VX, (uint32_t)rk_pieces[k].vx);
        SET32(a1, RK_VY, (uint32_t)rk_pieces[k].vy);
        SET8(a1, OBJ_frame, 4 - k);
    }
    int seen = OB8(o, OBJ_render) & 0x80;
    explode(o);
    if (seen) o_sfx_cd(FM_EXPLODE);
}

static void rock(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* a piece, flying */
        SET32(o, RK_VY, r32(o + RK_VY) + 0x4000);
        cd_move(o, lval(o, RK_VX), lval(o, RK_VY));
        if (below_player(o)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_QQZ_ROCK, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        radii(o, 0x10, 0x10);
        SET8(o, OBJ_frame, 0);
        /* fall through */
    case 2:                                                   /* (subtype 1: player 1 8-95 px under) */
        if (OB8(o, OBJ_subtype) && box(o, 8, 0x58, -8, 0x10)) SET8(o, OBJ_routine, 4);
        o_solid(o, 0x10 + 10, 0x10, 0x11, OB16(o, OBJ_x_pos));
        break;
    default: {                                                /* falling (CD's touch $F8, 16 x 16) */
        int hit = o_s1_touch(o, 0x10, 0x10);
        for (int n = 0; n < 2; n++)
            if (hit >> n & 1) hurt(o_player(n), o);
        if (!hit) {
            cd_move(o, 0, lval(o, RK_VY));
            SET32(o, RK_VY, r32(o + RK_VY) + 0x4000);
            if (o_floor_dist(o) >= 0) {
                if (below_player(o)) { o_delete(o); return; }
                break;
            }
        }
        rock_break(o);                                        /* (on the floor, or on him) */
        return;
    }
    }
    draw_despawn(o);
}

/* ---- $25 stalactite ------------------------------------------------------ */
#define SL_VY    0x30                  /* oVar2A (long) */
#define SL_IN    0x45                  /* oVar3F: its top in a floor last frame */

static void stalactite(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* (its tile: by period, scd_level.py) */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_QQZ_STALACTITE, OB8(o, OBJ_render) | 4, 3, 8, 0x18);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_y_radius, 0x18);
        /* fall through */
    case 2:                                                   /* (player 1 24-127 px under it) */
        if (box(o, 0x18, 0x68, -8, 0x10)) {
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0xB9));
        }
        break;
    default: {                                                /* falling: through what it hangs from,
                                                                  on till its top is through a floor */
        if (!(OB8(o, OBJ_render) & 0x80) && OBS16(o, OBJ_y_pos) >= OBS16(p, OBJ_y_pos)) { o_delete(o); return; }
        int in = floor_at(o, -(int)OB8(o, OBJ_y_radius)) < 0;
        if (!in && (OB8(o, SL_IN) & 0x80)) { o_delete(o); return; }
        SET8(o, SL_IN, in ? 0xFF : 0);
        cd_move(o, 0, lval(o, SL_VY));
        SET32(o, SL_VY, r32(o + SL_VY) + 0x4000);
        break;
    }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

/* ---- $26 spikes ---------------------------------------------------------- */
#define SP_Y0    0x38                  /* oVar32 */
#define SP_RIDE  0x3A                  /* oVar34: what it rides (none placed here) */
#define SP_X0    0x3C                  /* oVar36 */
#define SP_RX    0x3E                  /* oVar38, 39: where on it */
#define SP_RY    0x3F
#define SP_TIME  0x40                  /* oVar3A */
#define SP_OUT   0x41                  /* oVar3B: how far out (0-32) */
#define SP_DIR   0x42                  /* oVar3C: 1 going in */

static void spikes_move(uint32_t o)                           /* harir3_kind (subtype bit 2) */
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!(sub & 4)) return;
    int go = 1;                                               /* harir3_sub: a second out, a second in */
    if (OB8(o, SP_TIME)) { SET8(o, SP_TIME, OB8(o, SP_TIME) - 1); go = !OB8(o, SP_TIME); }
    if (go) {
        if (OB8(o, SP_DIR)) {
            if (OB8(o, SP_OUT) >= 8) SET8(o, SP_OUT, OB8(o, SP_OUT) - 8);
            else { SET8(o, SP_OUT, 0); SET8(o, SP_DIR, 0); SET8(o, SP_TIME, 60); }
        } else {
            SET8(o, SP_OUT, OB8(o, SP_OUT) + 8);
            if (OB8(o, SP_OUT) >= 0x20) { SET8(o, SP_OUT, 0x20); SET8(o, SP_DIR, 1); SET8(o, SP_TIME, 60); }
        }
    }
    int d = OB8(o, SP_OUT);
    switch (sub & 3) {
    case 0: SET16(o, OBJ_y_pos, OB16(o, SP_Y0) + d); break;   /* (in: down) */
    case 1: SET16(o, OBJ_y_pos, OB16(o, SP_Y0) - d); break;
    case 2: SET16(o, OBJ_x_pos, OB16(o, SP_X0) - d); break;
    default: SET16(o, OBJ_x_pos, OB16(o, SP_X0) + d); break;
    }
}

static void spikes(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        static const uint8_t size[4][3] = { { 0x12, 0x0C, 0 }, { 0x12, 0x0C, 0x83 }, { 0x0A, 0x0E, 0x82 }, { 0x0A, 0x0E, 0x82 } };
        const uint8_t *s = size[OB8(o, OBJ_subtype) & 3];
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_QQZ_SPIKES, OB8(o, OBJ_render) | 4, 3, s[0], s[1]);
        SET16(o, SP_X0, OB16(o, OBJ_x_pos));
        SET16(o, SP_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype) & 3);
        SET8(o, OBJ_y_radius, s[1]);
        SET8(o, OBJ_coll_flags, s[2] ? tri_scd_coll(s[2]) : 0);
    }
    spikes_move(o);
    int w = OB8(o, OBJ_width_px), h = OB8(o, OBJ_y_radius);
    o_solid(o, w + 10, h, h + 1, OB16(o, OBJ_x_pos));
    if (!OB8(o, OBJ_frame) && !scd_time_warping()) {          /* (upright: who stands on them) */
        uint8_t st = OB8(o, OBJ_status);
        if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
        if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
    }
    if (OB16(o, SP_RIDE)) {                                   /* (on something: with it) */
        uint32_t a1 = 0xFF0000u | OB16(o, SP_RIDE);
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (int8_t)OB8(o, SP_RX));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + (int8_t)OB8(o, SP_RY));
    }
    if (o_out_of_range(o, OB16(o, SP_X0))) { o_delete_respawnable(o); return; }   /* (seen or not) */
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

/* ---- $27 rock ring: eight blocks in four pairs, 96 px from the middle ---- */
#define RR_PARENT 0x30                 /* oVar2A: the first block (0: this is it) */
#define RR_CX    0x32                  /* oVar2C: the middle (long) */
#define RR_CY    0x36                  /* oVar30 (long) */
#define RR_ANG   0x3A                  /* oVar34: the angle (high byte) */
#define RR_SPD   0x3C                  /* oVar36 */

static void rr_block(uint32_t o, uint32_t a1, uint16_t ang)
{
    SET16(a1, RR_PARENT, (uint16_t)o);
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_routine, 2);
    o_init(a1, SCDA_QQZ_ROCKRING, OB8(o, OBJ_render) | 4, 3, 8, 8);
    radii(a1, 8, 8);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET32(a1, RR_CX, r32(o + OBJ_x_pos));
    SET32(a1, RR_CY, r32(o + OBJ_y_pos));
    SET16(a1, RR_SPD, OB16(o, RR_SPD));
    SET16(a1, RR_ANG, ang);
}

static void rock_ring(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* iwa5roll_init: this block, then seven */
        static const int16_t speed[] = { 0xC0, 0x80, 0x100, 0x100 };   /* (present, past, futures) */
        int16_t v = speed[cd_period() & 3];
        SET16(o, RR_SPD, OB8(o, OBJ_subtype) ? -v : v);
        uint32_t a1 = o;
        for (int k = 0; k < 8; k++) {
            if (k && !(a1 = o_alloc())) break;
            rr_block(o, a1, (uint16_t)((k >> 1) * 0x4000 + (k & 1) * 0x800));
        }
        SET16(o, RR_PARENT, 0);
    }
    uint16_t par = OB16(o, RR_PARENT);
    if (par && r32(0xFF0000u | par) != CD_CODE(0x27)) { o_delete(o); return; }
    SET16(o, RR_ANG, OB16(o, RR_ANG) + OB16(o, RR_SPD));      /* iwa5roll_move */
    int16_t s, c;
    o_sincos(OB8(o, RR_ANG), &s, &c);
    int32_t d1 = (int32_t)((uint32_t)(uint16_t)c << 16) >> 2, d0 = (int32_t)((uint32_t)(uint16_t)s << 16) >> 2;
    uint16_t oldx = OB16(o, OBJ_x_pos);
    int32_t nx = (d1 >> 1) + d1 + lval(o, RR_CX), ny = (d0 >> 1) + d0 + lval(o, RR_CY);
    SET16(o, OBJ_x_vel, (uint16_t)((nx - lval(o, OBJ_x_pos)) >> 8));
    SET32(o, OBJ_x_pos, (uint32_t)nx);
    SET32(o, OBJ_y_pos, (uint32_t)ny);
    /* (CD: SolidObject with no speed, 10 high, to find the player; again with
     * the speed, 8 high, to carry him) */
    o_solid(o, 8 + 10, 10, 9, oldx);
    if (par) { o_draw(o); return; }
    if (cd_despawn(o, OB16(o, RR_CX))) return;                /* (CheckObjDespawn2: the middle) */
    o_draw(o);
}

/* ---- $28 ledge: subtype columns of 16 px ---------------------------------- */
#define LG_TIME  0x30                  /* oVar2A */
#define LG_STEP  0x32                  /* oVar2C: 8, -8: to where player 1 ran */
#define LG_VY    0x32                  /* oVar2C: a piece's fall (long) */

static void ledge_top(uint32_t o)
{
    o_platform(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB16(o, OBJ_x_pos));
}

static void ledge_piece(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        radii(o, 8, 8);
        SET16(o, OBJ_priority, 3 << 7);
        break;
    case 2:
        SET16(o, LG_TIME, OB16(o, LG_TIME) - 1);
        if (OBS16(o, LG_TIME) < 0) SET8(o, OBJ_routine, 4);
        break;
    default: {
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        int32_t v = lval(o, LG_VY) + 0x4000;
        if (v > 0x160000) v = 0x160000;
        SET32(o, LG_VY, (uint32_t)v);
        cd_move(o, 0, v);
        break;
    }
    }
    o_draw(o);
}

static void ledge_break(uint32_t o)                           /* (a column: top and bottom pieces) */
{
    int d3 = (OB8(o, OBJ_subtype) - 1) * 8;
    if (OBS16(o, LG_STEP) >= 0) d3 = -d3;                     /* (the end behind the player) */
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, r32(o + OBJ_code));
        SET8(a1, OBJ_render, OB8(o, OBJ_render));
        SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
        SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
        SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
        SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
        SET8(a1, OBJ_subtype, 0xFF);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + (k ? 8 : -8));
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + d3);
        SET16(a1, LG_TIME, k ? 0 : 4);
        SET8(a1, OBJ_frame, 8 + k);
    }
}

static void ledge(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { ledge_piece(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        int n = OB8(o, OBJ_subtype);
        o_init(o, SCDA_QQZ_LEDGE, 4, 3, n * 8, 0x11);         /* (render 4: the layout's flips dropped) */
        SET8(o, OBJ_y_radius, 0x11);
        SET8(o, OBJ_frame, n - 1);
        SET8(o, OBJ_x_radius, n * 8);
    }   /* fall through */
    case 2:
        ledge_top(o);
        if (cd_stood_on(o)) {
            SET8(o, OBJ_routine, 4);                          /* (CD clears its saved-state slot: once
                                                                  broken, broken; o_delete keeps it so) */
            SET16(o, LG_TIME, 8);
            SET16(o, LG_STEP, OBS16(RAM_Player_1, OBJ_x_vel) < 0 ? -8 : 8);
            o_sfx_cd(FM_CRUMBLE);
        }
        draw_despawn(o);
        return;
    case 4:
        ledge_top(o);
        SET16(o, LG_TIME, OB16(o, LG_TIME) - 1);
        if (OBS16(o, LG_TIME) < 0) SET8(o, OBJ_routine, 6);
        break;
    default:
        ledge_break(o);
        ledge_top(o);
        if (cd_stood_on(o)) cd_get_off(o);
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 1);
        if ((int8_t)OB8(o, OBJ_subtype) <= 0) { o_delete(o); return; }
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
        SET8(o, OBJ_x_radius, OB8(o, OBJ_x_radius) - 8);
        SET8(o, OBJ_width_px, OB8(o, OBJ_width_px) - 8);
        SET16(o, LG_TIME, 7);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, LG_STEP));
        ledge_top(o);
        break;
    }
    o_draw(o);
}

/* ---- $29 wave rocks ------------------------------------------------------ */
#define WV_PARENT 0x30                 /* oVar2A: (the second's) the first */
#define WV_X0    0x32                  /* oVar2C */
#define WV_LEFT  0x34                  /* oVar2E: the drift's ends (long) */
#define WV_RIGHT 0x38                  /* oVar32 (long) */
#define WV_Y0    0x3C                  /* oVar36 */
#define WV_ANG   0x3E                  /* oVar38: the bob's angle (high byte) */

static void wave_init(uint32_t o, uint32_t a1)                /* (a_init's common part) */
{
    SET8(a1, OBJ_routine, 2);
    o_init(a1, SCDA_QQZ_WAVEROCK, OB8(a1, OBJ_render) | 4, 3, 0x20, 0x10);
    radii(a1, 0x20, 0x10);
    SET16(a1, WV_Y0, OB16(o, OBJ_y_pos));
}

static void wave_rocks(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* a_init: this one 64 px right, then one left */
        uint16_t x = OB16(o, OBJ_x_pos);
        SET16(o, WV_X0, x);
        SET32(o, WV_LEFT, ((uint32_t)x << 16) - 0x800000u);
        SET32(o, WV_RIGHT, r32(o + WV_LEFT) + 0x1000000u);
        uint16_t ang = OB8(o, OBJ_subtype) ? 0 : 0x8000;
        wave_init(o, o);
        SET16(o, OBJ_x_pos, x + 0x40);
        SET16(o, WV_ANG, ang);
        uint32_t a1 = o_alloc();
        if (a1) {
            SET16(a1, WV_PARENT, (uint16_t)o);
            SET32(a1, OBJ_code, r32(o + OBJ_code));
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET32(a1, WV_LEFT, r32(o + WV_LEFT));
            SET32(a1, WV_RIGHT, r32(o + WV_RIGHT));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            wave_init(o, a1);
            SET16(a1, OBJ_x_pos, x - 0x40);
            SET16(a1, WV_ANG, ang);
        }
    }
    /* a_move: left 0.5 px a frame, back to the right end at the left */
    uint32_t x0 = r32(o + OBJ_x_pos), x1 = x0 - 0x8000;
    uint16_t oldx = OB16(o, OBJ_x_pos);
    if ((int32_t)x1 <= lval(o, WV_LEFT)) { x0 = x1 = r32(o + WV_RIGHT); oldx = (uint16_t)(x1 >> 16); }
    SET32(o, OBJ_x_pos, x1);
    int32_t dv = (int32_t)(x1 - x0) >> 8;
    SET16(o, OBJ_x_vel, (uint16_t)dv);
    SET16(o, WV_ANG, OB16(o, WV_ANG) + 0x100);
    int16_t s, c;
    o_sincos((uint8_t)(OBS16(o, WV_ANG) >> 8), &s, &c);
    /* (swap d1: its low word is what d1 held, the speed's high word) */
    int32_t d1 = (int32_t)(((uint32_t)(uint16_t)c << 16) | ((uint32_t)dv >> 16)) >> 2;
    int32_t d0 = (d1 >> 1) + d1 + 0x8000;
    SET16(o, OBJ_y_pos, OB16(o, WV_Y0) + (uint16_t)((uint32_t)d0 >> 16));
    uint16_t par = OB16(o, WV_PARENT);
    if (par && r32(0xFF0000u | par) != CD_CODE(0x29)) { o_delete(o); return; }
    /* (CD: TopSolidObject with no speed, 18 high, to find the player; again
     * with the speed, 16 high, to carry him) */
    o_platform(o, 0x20 + 10, 0x10, oldx);
    if (par) { o_draw(o); return; }
    if (cd_despawn(o, OB16(o, WV_X0))) return;
    o_draw(o);
}

/* ---- $2A swing (act 2): links 16 px apart, the seat on the end ------------ */
#define SW_ANG   0x30                  /* oVar2A: the angle (long: its third byte) */
#define SW_LIMIT 0x34                  /* oVar2E: to and fro, 0 to it (long; 0: round) */
#define SW_SPEED 0x38                  /* oVar32 (long) */
#define SW_LINKS 0x3C                  /* oVar36 */
#define SW_SLOTS 0x3E                  /* oVar38..: its children's slots (links, the seat) */
#define SW_PARENT 0x30                 /* oVar2A: (a child's) the pivot */
#define SW_OLDX  0x32                  /* (the seat's: where it was, to carry who stands on it) */
#define SW_LINK  0x80                  /* (subtype: CD's subtype2 1, a link) */
#define SW_SEAT  0x81                  /* (CD's subtype2 -1, the seat) */

static void swing_child(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* (the first frame: unseen) */
        int seat = OB8(o, OBJ_subtype) == SW_SEAT;
        SET8(o, OBJ_routine, 2);
        o_init(o, seat ? SCDA_QQZ_SWINGTOP : SCDA_QQZ_SWING, OB8(o, OBJ_render) | 4, seat ? 2 : 3, seat ? 0x20 : 8, 8);
        radii(o, seat ? 0x20 : 8, 8);
        SET8(o, OBJ_frame, seat ? 2 : 1);
        if (!seat) return;                                    /* (the seat: on at once; its pivot ran first) */
    }
    if (OB8(o, OBJ_subtype) == SW_SEAT) {                     /* (CD: as the wave rocks' two tests) */
        o_platform(o, 0x20 + 10, 8, OB16(o, SW_OLDX));
        int d = scd_qqz_colrev() ? -2 : 2;                    /* (a belt on top) */
        for (int n = 0; n < 2; n++)
            if (OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))
                SET16(o_player(n), OBJ_x_pos, OB16(o_player(n), OBJ_x_pos) + d);
    }
    uint32_t par = 0xFF0000u | OB16(o, SW_PARENT);
    if (r32(par) != CD_CODE(0x2A) || (OB8(par, OBJ_subtype) & 0x80)) { o_delete(o); return; }
    o_draw(o);
}

static void swing(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { swing_child(o); return; }
    if (!OB8(o, OBJ_routine)) {                               /* c_init */
        static const uint16_t kinds[3][2] = { { 5, 0 }, { 5, 0x8000 }, { 7, 0x4000 } };
        const uint16_t *k = kinds[(OB8(o, OBJ_subtype) >> 1) % 3];
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_QQZ_SWING, OB8(o, OBJ_render) | 4, 3, 8, 8);
        radii(o, 8, 8);
        SET16(o, SW_LINKS, k[0]);
        SET32(o, SW_LIMIT, k[1]);
        SET32(o, SW_SPEED, 0x100);
        SET32(o, SW_ANG, 0);
        if (OB8(o, OBJ_subtype) & 1) {                        /* (the other way) */
            SET32(o, SW_LIMIT, (uint32_t)-lval(o, SW_LIMIT));
            SET32(o, SW_SPEED, (uint32_t)-lval(o, SW_SPEED));
        }
        uint32_t a1 = 0;
        for (int n = 0; n <= k[0]; n++) {                     /* (FindNextObjSlot: after it) */
            if (!(a1 = o_alloc_after(o))) { o_delete(o); return; }
            SET8(o, SW_SLOTS + n, (uint8_t)o_slot_index(a1));
            SET8(a1, OBJ_subtype, SW_LINK);
            SET16(a1, SW_PARENT, (uint16_t)o);
            SET32(a1, OBJ_code, r32(o + OBJ_code));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
        }
        SET8(a1, OBJ_subtype, SW_SEAT);
    }
    int32_t d6 = lval(o, SW_ANG) + lval(o, SW_SPEED), d5 = lval(o, SW_LIMIT);   /* c_move */
    if (d5) {                                                 /* (to and fro: back at the ends) */
        int32_t d1 = d5 < 0 ? -d6 : d6, d0 = d5 < 0 ? -d5 : d5;
        if (d1 < 0 || d1 > d0) {
            d6 = d1 < 0 ? 0 : d5;
            SET32(o, SW_SPEED, (uint32_t)-lval(o, SW_SPEED));
        }
    }
    SET32(o, SW_ANG, (uint32_t)d6);
    int16_t s, c;
    o_sincos(OB8(o, SW_ANG + 2), &s, &c);
    int32_t dx = (int32_t)((uint32_t)(uint16_t)c << 16) >> 4, dy = (int32_t)((uint32_t)(uint16_t)s << 16) >> 4;
    uint32_t x5 = r32(o + OBJ_x_pos), y4 = r32(o + OBJ_y_pos);
    int n = OB16(o, SW_LINKS);
    for (int k = 0; k <= n; k++) {                            /* (the links; the seat last) */
        uint32_t a1 = o_slot(OB8(o, SW_SLOTS + k));
        x5 += (uint32_t)dx;
        y4 += (uint32_t)dy;
        if (k == n) {
            SET16(a1, SW_OLDX, OB16(a1, OBJ_x_pos));
            SET16(a1, OBJ_x_vel, (uint16_t)((int32_t)(x5 - r32(a1 + OBJ_x_pos)) >> 8));
        }
        SET32(a1, OBJ_x_pos, x5);
        SET32(a1, OBJ_y_pos, y4);
    }
    draw_despawn(o);
}

/* ---- $2B pillar: CD's special touch $2E (23 x 48): a rolling player -------- */
#define PI_VX    0x30                  /* oVar2A (long) */
#define PI_VY    0x34                  /* oVar2E (long) */
static const int8_t pi_d[8][2] = {     /* (the first block is the pillar: the rest go from it) */
    { -8, -24 }, { 16, 0 }, { 0, 16 }, { 16, 16 }, { 0, 32 }, { 16, 32 }, { 0, 48 }, { 16, 48 },
};
static const int32_t pi_v[8][2] = {
    { -0x12D2D, -0x20000 }, { 0x12D2D, -0x20000 }, { -0x28000, -0x18000 }, { 0x28000, -0x18000 },
    { -0x50000, 0x8000 }, { 0x50000, 0x8000 }, { -0x28000, 0x10000 }, { 0x28000, 0x10000 },
};
static const uint8_t pi_f[8] = { 2, 5, 3, 5, 4, 5, 7, 5 };

static void pillar(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_QQZ_PILLAR, OB8(o, OBJ_render) | 4, 4, 0x10, 0x30);
        radii(o, 0x10, 0x30);
        /* fall through */
    case 2: {                                                 /* m_wait */
        int hit = o_s1_touch(o, 0x17, 0x30);
        uint32_t p = 0;
        for (int n = 0; n < 2 && !p; n++)
            if ((hit >> n & 1) && OB8(o_player(n), P_ANIM) == 2) p = o_player(n);
        if (p) {                                              /* (rolled into: half his speed) */
            SET16(p, OBJ_x_vel, (uint16_t)(OBS16(p, OBJ_x_vel) >> 1));
            SET16(p, OBJ_ground_vel, (uint16_t)(OBS16(p, OBJ_ground_vel) >> 1));
        }
        o_solid(o, 0x10 + 10, 0x30, 0x31, OB16(o, OBJ_x_pos));
        if (!p) break;
        if (cd_stood_on(o)) cd_get_off(o);
        o_sfx_cd(FM_PILLAR);
        SET8(o, OBJ_routine, 4);
        for (int k = 0; k < 8; k++) {
            uint32_t a1 = o;
            if (k) {
                if (!(a1 = o_alloc())) break;
                SET32(a1, OBJ_code, r32(o + OBJ_code));
                SET8(a1, OBJ_routine, 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_render, OB8(o, OBJ_render));
                SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
                SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
                SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
            }
            radii(a1, 0x10, 0x0C);
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + pi_d[k][0]);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + pi_d[k][1]);
            SET32(a1, PI_VX, (uint32_t)pi_v[k][0]);
            SET32(a1, PI_VY, (uint32_t)pi_v[k][1]);
            SET8(a1, OBJ_frame, pi_f[k]);
        }
    }   /* fall through */
    default:                                                  /* m_down: a block, flying */
        SET32(o, PI_VY, r32(o + PI_VY) + 0x4000);
        cd_move(o, lval(o, PI_VX), lval(o, PI_VY));
        if (below_player(o)) { o_delete(o); return; }
        break;
    }
    draw_despawn(o);
}

/* ---- $2F bridge: Sonic CD's is Sonic 1's (hashi5: eight 16 px logs from
 * x - 64, sagging up to the log stood on, the same bend tables); S3K's
 * tension bridge is that bridge grown up (two players), so it stands in,
 * wearing these logs. (CD: player 1 alone; on the first log it does not
 * sag) ----------------------------------------------------------------- */
static void bridge(uint32_t o)
{
    SET8(o, OBJ_subtype, 8);                                  /* (logs: always 8) */
    A(0) = RAMPTR(o);
    tri_jsr(S3K_Obj_TensionBridge);                           /* builds the log children */
    uint32_t maps = o_asset_maps(SCDA_QQZ_BRIDGE);
    uint16_t tile = o_asset_tile(SCDA_QQZ_BRIDGE);
    SET32(o, OBJ_mappings, maps);
    SET16(o, OBJ_art_tile, tile);
    SET16(o, OBJ_priority, 3 << 7);
    for (int k = 0; k < 2; k++) {
        uint32_t c = r32(o + 0x30 + k * 4) & 0xFFFFFF;
        if (c >= 0xFF0000u) {
            SET32(c, OBJ_mappings, maps);
            SET16(c, OBJ_art_tile, tile);
            SET16(c, OBJ_priority, 3 << 7);
        }
    }
}

/* ---- $30 platform: 25 kinds -------------------------------------------- */
#define PF_OLDX  0x2E                  /* (where it was: carries who stands on it) */
#define PF_SPR1  0x30                  /* (its springs) */
#define PF_POS   0x32                  /* oVar2C: the swing's way (high byte: px) */
#define PF_VEL   0x34                  /* oVar2E */
#define PF_ACC   0x36                  /* oVar30 */
#define PF_Y0    0x38                  /* oVar32: its rest */
#define PF_SPR2  0x3A
#define PF_X0    0x3C                  /* oVar36 */
#define PF_SPR3  0x3E
#define PF_TIME  0x41                  /* oVar3B */
#define PF_DIR   0x42                  /* oVar3C */
#define PF_STATE 0x43                  /* oVar3D */
#define PF_SINK  0x44                  /* oVar3E: the sink's angle (0-$40: 4 px) */
#define PF_FLAGS 0x45                  /* oVar3F: bit 0 a belt top, bit 7 it sinks */

/* by kind: frame, width; flags; move (0 still, 1 to and fro 96 px, 2 up and
 * down 96 px, 3 to and fro 64 px, 4 falls, 5 rises when stood on); springs */
static const uint8_t pf_frame[25] = { 0, 1, 0, 0, 1, 0, 0, 2, 2, 3, 2, 3, 2, 3, 2, 2, 2, 4, 5, 4, 4, 5, 4, 4, 4 };
static const uint8_t pf_flags[25] = { 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0 };
static const uint8_t pf_move[25] = { 0, 1, 1, 2, 2, 5, 4, 0, 3, 3, 2, 2, 1, 1, 1, 5, 4, 0, 0, 2, 1, 1, 1, 5, 4 };
static const uint8_t pf_nspring[25] = { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0 };
static const int8_t pf_spring_dx[4][3] = { { 0 }, { 0 }, { -16, 16 }, { -32, 0, 32 } };
static uint32_t s_spring_maps;             /* (S3K's spring mappings: a spring is one while it shows them) */

static void pf_springs(uint32_t o, int spawn)                 /* banesetsub; (CD's follow their parent) */
{
    static const uint8_t field[3] = { PF_SPR1, PF_SPR2, PF_SPR3 };
    int n = pf_nspring[OB8(o, OBJ_subtype) & 0x1F];
    for (int k = 0; k < n; k++) {
        int dx = pf_spring_dx[n][k];
        if (spawn) {
            uint32_t a1 = o_alloc();
            SET16(o, field[k], (uint16_t)a1);
            if (!a1) continue;
            SET32(a1, OBJ_code, S3K_Obj_Spring);
            SET8(a1, OBJ_subtype, 2);                         /* (up, CD's subtype 2: yellow) */
        }
        if (!OB16(o, field[k])) continue;
        uint32_t a1 = 0xFF0000u | OB16(o, field[k]);
        if (r32(a1) != S3K_Obj_Spring) {                      /* (initialised: S3K's code; still ours?) */
            if (!s_spring_maps && r32(a1)) s_spring_maps = r32(a1 + OBJ_mappings);
            if (!r32(a1) || r32(a1 + OBJ_mappings) != s_spring_maps || OB8(a1, OBJ_subtype) != 2) {
                SET16(o, field[k], 0);
                continue;
            }
        }
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 24);
    }
}

static void pf_osc(uint32_t o, int d4)                        /* dodai_sub */
{
    int16_t a = OBS16(o, PF_ACC);
    int dir = OB8(o, PF_DIR) != 0;
    int16_t v = (int16_t)(OBS16(o, PF_VEL) + (dir ? -a : a));
    SET16(o, PF_VEL, v);
    SET16(o, PF_POS, OB16(o, PF_POS) + v);
    if (!dir && (uint8_t)d4 <= OB8(o, PF_POS)) SET8(o, PF_DIR, 0xFF);
    else if (dir && (uint8_t)d4 > OB8(o, PF_POS)) SET8(o, PF_DIR, 0);
}

static void pf_across(uint32_t o, int d4)                     /* type01, type03 */
{
    SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) | 0x80);
    SET16(o, PF_ACC, 4);
    pf_osc(o, d4);
    int16_t v = OBS16(o, PF_VEL);
    if (OB8(o, OBJ_subtype) & 0x80) v = (int16_t)-v;
    SET16(o, OBJ_x_vel, v);
    cd_move(o, (int32_t)v << 8, 0);
}

static int pf_fall(uint32_t o)                                /* type04: 1 = gone */
{
    if (!OB8(o, PF_STATE)) {
        SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) | 0x80);
        if (!cd_stood_on(o)) return 0;
        SET8(o, PF_TIME, 30);
        SET8(o, PF_STATE, 1);
    }
    if (OB8(o, PF_TIME)) {                                    /* (half a second, then down) */
        SET8(o, PF_TIME, OB8(o, PF_TIME) - 1);
        if (OB8(o, PF_TIME)) return 0;
        SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) & ~0x80);
    }
    uint16_t v = OB16(o, OBJ_y_vel);
    SET8(o, OBJ_y_radius, v >= 0x300 ? 0x14 : v >= 0x200 ? 0x12 : 0x10);   /* (it keeps its rider) */
    if (v < 0x400) SET16(o, OBJ_y_vel, v + 0x20);
    cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) << 8);
    if ((uint16_t)(r16(RAM_Camera_Y_pos) + 0xE0) >= OB16(o, OBJ_y_pos)) return 0;
    cd_get_off(o);
    o_delete(o);
    return 1;
}

static void pf_rise(uint32_t o)                               /* type05, type06 */
{
    switch (OB8(o, PF_STATE)) {
    case 0:
        SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) | 0x80);
        if (!cd_stood_on(o)) return;
        SET8(o, PF_STATE, 2);
        SET8(o, PF_TIME, 30);
        /* fall through */
    case 2: {
        if (OB8(o, PF_TIME)) {
            SET8(o, PF_TIME, OB8(o, PF_TIME) - 1);
            if (OB8(o, PF_TIME)) return;
            SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) & ~0x80);
            SET8(o, OBJ_y_radius, 0x0E);
        }
        uint16_t v = (uint16_t)(OB16(o, OBJ_y_vel) - 8);
        if (v < 0xFD00) v = 0xFD00;                           /* (up to $300) */
        SET16(o, OBJ_y_vel, v);
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
        uint16_t y = OB16(o, OBJ_y_pos);
        SET16(o, OBJ_y_pos, y - 0x30);                        /* (the ceiling, 48 px above) */
        int16_t d1 = o_ceiling_dist(o);
        SET16(o, OBJ_y_pos, y);
        if (d1 >= 0) return;
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, PF_Y0, OB16(o, OBJ_y_pos));
        SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) | 0x80);
        SET8(o, PF_STATE, 4);
        return;
    }
    default:
        return;
    }
}

static void pf_sink(uint32_t o)                               /* dai5sub: 4 px down while stood on */
{
    if (!(OB8(o, PF_FLAGS) & 0x80)) return;
    if (cd_stood_on(o)) {
        if (OB8(o, PF_SINK) == 0x40) return;
        SET8(o, PF_SINK, OB8(o, PF_SINK) + 8);
    } else {
        if (!OB8(o, PF_SINK)) return;
        SET8(o, PF_SINK, OB8(o, PF_SINK) - 8);
    }
    int16_t s, c;
    o_sincos(OB8(o, PF_SINK), &s, &c);
    SET16(o, OBJ_y_pos, OB16(o, PF_Y0) + (int16_t)(((int32_t)s * 0x400) >> 16));
}

static void pf_belt(uint32_t o)                               /* belttask: the rider, the belts' way */
{
    if (!(OB8(o, PF_FLAGS) & 1)) return;
    static const int32_t speed[] = { 0x10000, 0x8000, 0x20000, 0x20000 };   /* (present, past, futures) */
    int32_t v = speed[cd_period() & 3];
    if (scd_qqz_colrev()) v = -v;
    for (int n = 0; n < 2; n++)
        if (OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))
            SET32(o_player(n), OBJ_x_pos, r32(o_player(n) + OBJ_x_pos) + (uint32_t)v);
}

static void platform(uint32_t o)
{
    uint8_t kind = OB8(o, OBJ_subtype) & 0x1F;
    if (kind > 24) kind = 24;                                 /* (none past 24 placed) */
    if (!OB8(o, OBJ_routine)) {                               /* dair5_init (its tile: acts 1-2's) */
        SET8(o, OBJ_routine, 2);
        static const uint8_t width[6] = { 0x10, 0x10, 0x20, 0x20, 0x30, 0x30 };   /* (by frame) */
        o_init(o, SCDA_QQZ_PLATFORM, OB8(o, OBJ_render) | 4, 3, width[pf_frame[kind]], 0x10);
        SET16(o, PF_X0, OB16(o, OBJ_x_pos));
        SET16(o, PF_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_frame, pf_frame[kind]);
        SET8(o, PF_FLAGS, pf_flags[kind]);
        pf_springs(o, 1);
    }
    SET16(o, PF_OLDX, OB16(o, OBJ_x_pos));                    /* dair5_move */
    switch (pf_move[kind]) {
    case 0: SET8(o, PF_FLAGS, OB8(o, PF_FLAGS) | 0x80); break;
    case 1: pf_across(o, 0x30); break;
    case 2:
        SET16(o, PF_ACC, 4);
        pf_osc(o, 0x30);
        cd_move(o, 0, (int32_t)(OB8(o, OBJ_subtype) & 0x80 ? -OBS16(o, PF_VEL) : OBS16(o, PF_VEL)) << 8);
        break;
    case 3: pf_across(o, 0x20); break;
    case 4: if (pf_fall(o)) return; break;
    default: pf_rise(o); break;
    }
    pf_sink(o);
    pf_belt(o);
    o_platform(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB16(o, PF_OLDX));
    pf_springs(o, 0);
    if (cd_despawn(o, OB16(o, PF_X0))) return;                /* (CheckObjDespawn2: its place) */
    o_draw(o);
}

/* ---- $2C Metal Sonic's projector (the past): Palmtree Panic's code
 * (cd_projector), at this round's ids: its holograms are $2C (Metal Sonic)
 * and $2D (the animals); a ball breaks it, its show ends and the act's
 * animals are free. Its holograms' art and the animals' share VRAM in CD
 * (LoadPLC 8, then 7 as it breaks); here each has its own ---------------- */
#define PJ_COUNT 0x30                  /* oVar2A */
#define PJ_LOCS  0x32                  /* oVar2C (long) */
#define PJ_GONE  0x45                  /* oVar3F: breaking (its holograms go) */
#define HO_PARENT 0x44                 /* oVar3E: a hologram's projector */
#define AN_X0    0x30                  /* oVar2A */
#define AN_Y0    0x32                  /* oVar2C */
#define AN_ANG   0x34                  /* oVar2E */

static int holo_parent_gone(uint32_t o)
{
    uint32_t pj = 0xFF0000u | OB16(o, HO_PARENT);
    return r32(pj) != CD_CODE(0x2C) || OB8(pj, OBJ_subtype) || OB8(pj, PJ_GONE);
}

static void spawn_holo(uint32_t o, int id, int dx, int dy, uint8_t sub)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, CD_CODE(id));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, HO_PARENT, (uint16_t)o);
}

static void projector(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {                                /* Metal Sonic's hologram */
        if (holo_parent_gone(o)) { o_delete(o); return; }
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            int small = OB8(o, OBJ_subtype) & 0x80;
            o_init(o, SCDA_PROJECTOR, OB8(o, OBJ_render) | 4, 4, small ? 8 : 0x14, small ? 4 : 0x18);
            SET8(o, OBJ_x_radius, small ? 8 : 0x14);
            SET8(o, OBJ_y_radius, small ? 4 : 0x18);
            SET8(o, OBJ_anim, small ? 0 : 1);
        }
        o_animate(o, o_asset_anim(SCDA_PROJECTOR));
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (scd_proj_destroyed() || !o_asset_maps(SCDA_PROJECTOR)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_PROJECTOR, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        radii(o, 0x0C, 0x0C);
        SET32(o, PJ_LOCS, o_asset_anim(SCDA_PROJBOOM));
        spawn_holo(o, 0x2C, -0x15, -7, 0xFF);
        spawn_holo(o, 0x2C, -0x58, -4, 1);
        spawn_holo(o, 0x2D, -0x58, -0x18, 0x80);
        spawn_holo(o, 0x2D, -0x64, 4, 0x81);
        /* fall through */
    case 2: {                                                 /* (CD's touch $3B, 18 x 17: a ball) */
        int hit = o_s1_touch(o, 0x12, 0x11), broken = 0;
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), P_ANIM) == 2) broken = 1;
        if (broken) SET8(o, OBJ_routine, 4);
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        break;
    }
    case 4:                                                   /* StartExploding */
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_frame, 1);
        SET8(o, PJ_GONE, 0xFF);
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        if (cd_stood_on(o)) cd_get_off(o);
        /* fall through */
    case 6: {                                                 /* exploding */
        uint32_t a6 = r32(o + PJ_LOCS);
        uint8_t when = a6 ? tri_rd8(a6) : 0xFF;
        if (when & 0x80) { SET8(o, OBJ_routine, 8); SET16(o, PJ_COUNT, 60); break; }
        SET8(o, PJ_COUNT, OB8(o, PJ_COUNT) + 1);
        if (OB8(o, PJ_COUNT) != when) break;
        int8_t dx = (int8_t)tri_rd8(a6 + 1), dy = (int8_t)tri_rd8(a6 + 2);
        SET32(o, PJ_LOCS, a6 + 3);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);                             /* (no animal) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
        o_sfx_cd(FM_EXPLODE);                                 /* (each blast; no jingle) */
        break;
    }
    default:                                                  /* gone */
        SET16(o, PJ_COUNT, OB16(o, PJ_COUNT) - 1);
        if (OB16(o, PJ_COUNT)) break;
        scd_proj_destroyed_set();
        o_delete(o);
        return;
    }
    if (OB8(o, OBJ_routine) <= 2 && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $2D animals: Palmtree Panic's (cd_animal), but for the projector's
 * holograms of them (subtype bit 7; 0 flying), whose projector is $2C ------ */
static void animal(uint32_t o)
{
    if (!(OB8(o, OBJ_subtype) & 0x80)) { cd_animal(o); return; }
    int flying = !(OB8(o, OBJ_subtype) & 0x7F), asset = flying ? SCDA_HOLOFLY : SCDA_HOLOGROUND;
    if (!OB8(o, OBJ_routine)) {
        if (!o_asset_maps(asset)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 4);
        o_init(o, asset, 4, 3, 8, 8);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, AN_X0, OB16(o, OBJ_x_pos));
        if (flying) {
            SET16(o, AN_Y0, OB16(o, OBJ_y_pos));
            SET8(o, OBJ_anim, 1);
        }
        SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);          /* (turned) */
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        return;                                               /* (the first frame: unseen) */
    }
    if (holo_parent_gone(o)) { o_delete(o); return; }
    if (flying) {                                             /* (round and round) */
        int16_t s, c;
        o_sincos(OB8(o, AN_ANG), &s, &c);
        SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> 3));
        SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> 4));
        SET8(o, AN_ANG, OB8(o, AN_ANG) + 4);
        if (!(OB8(o, AN_ANG) & 0x7F)) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
            return;
        }
    }
    o_animate(o, o_asset_anim(asset));
    o_draw(o);
}

void scd_qqz_gim_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x24] = rock;
    t[base + 0x25] = stalactite;
    t[base + 0x26] = spikes;
    t[base + 0x27] = rock_ring;
    t[base + 0x28] = ledge;
    t[base + 0x29] = wave_rocks;
    t[base + 0x2A] = swing;
    t[base + 0x2B] = pillar;
    t[base + 0x2C] = projector;
    t[base + 0x2D] = animal;
    t[base + 0x2E] = cd_generator;
    t[base + 0x2F] = bridge;
    t[base + 0x30] = platform;
}

void scd_qqz_gim3_register(TriObjFn *t, int base3)            /* (act 3: its ledge, platforms; the same code) */
{
    t[base3 + 0x28] = ledge;
    t[base3 + 0x30] = platform;
}
