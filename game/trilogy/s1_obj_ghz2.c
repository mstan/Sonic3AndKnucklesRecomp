/*
 * s1_obj_ghz2.c — Sonic 1 Green Hill gimmicks: platforms (18), swinging
 * platforms (15), spiked pole helix (17), collapsing ledge (1A), smashable
 * wall (3C), spikes (36). S1 behaviour, S3K two-player solidity.
 */
#include "trilogy_obj.h"

#define R2  0x2D

static uint8_t osc_byte(int off) { return r8(RAM_Oscillating_table + (uint32_t)off); }

static int sin8(uint8_t a) { int16_t s, c; o_sincos(a, &s, &c); return s; }

/* players standing on o (S3K status bits 3/4) */
static int standing(uint32_t o) { return OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND); }

/* Drop every player standing on o (object stops being solid). */
static void release_players(uint32_t o, int keep_yvel)
{
    for (int i = 0; i < 2; i++) {
        uint8_t bit = i ? ST_P2_STAND : ST_P1_STAND;
        if (!(OB8(o, OBJ_status) & bit)) continue;
        uint32_t p = o_player(i);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
        if (keep_yvel) SET16(p, OBJ_y_vel, OB16(o, OBJ_y_vel));
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
    }
}

/* ------------------------------------------------------------------------
 * Object 18 - platforms
 * ---------------------------------------------------------------------- */
#define PL_Y      0x30    /* objoff_2C: working y */
#define PL_BASEY  0x32    /* objoff_34 */
#define PL_BASEX  0x34    /* objoff_32 */
#define PL_NUDGE  0x36    /* objoff_38 */
#define PL_TIME   0x38    /* objoff_3A */

static void plat_move(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    int d1;
    switch (sub & 0x0F) {
    case 0: case 9: return;
    case 5: d1 = (int8_t)(uint8_t)(-(int8_t)OB8(o, OBJ_angle) + 0x40); goto xmove;
    case 1: d1 = (int8_t)(uint8_t)(OB8(o, OBJ_angle) - 0x40);
    xmove:
        SET16(o, OBJ_x_pos, OB16(o, PL_BASEX) + d1);
        break;
    case 0x0C: d1 = (int8_t)(uint8_t)(-(int8_t)osc_byte(0x0E) + 0x30); goto ymove;
    case 0x0B: d1 = (int8_t)(uint8_t)(osc_byte(0x0E) - 0x30); goto ymove;
    case 6: d1 = (int8_t)(uint8_t)(-(int8_t)OB8(o, OBJ_angle) + 0x40); goto ymove;
    case 2: d1 = (int8_t)(uint8_t)(OB8(o, OBJ_angle) - 0x40);
    ymove:
        SET16(o, PL_Y, OB16(o, PL_BASEY) + d1);
        break;
    case 3:
        if (!OB16(o, PL_TIME)) {
            if (standing(o)) SET16(o, PL_TIME, 30);
            return;
        }
        if (OB16(o, PL_TIME) - 1 == 0) {
            SET16(o, PL_TIME, 32);
            SET8(o, OBJ_subtype, sub + 1);
        } else SET16(o, PL_TIME, OB16(o, PL_TIME) - 1);
        return;
    case 4: {
        if (OB16(o, PL_TIME)) {
            SET16(o, PL_TIME, OB16(o, PL_TIME) - 1);
            if (!OB16(o, PL_TIME)) {
                release_players(o, 1);
                SET8(o, OBJ_routine, 8);
            }
        }
        int32_t y = (int32_t)r32(o + PL_Y) + ((int32_t)OBS16(o, OBJ_y_vel) << 8);
        SET32(o, PL_Y, y);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        if ((uint16_t)(r16(RAM_Camera_max_Y_pos) + 0xE0) < OB16(o, PL_Y))
            SET8(o, OBJ_routine, 6);
        return;
    }
    case 7:                                  /* rises once switch (type >> 4) is pressed */
        if (!OB16(o, PL_TIME)) {
            if (r8(RAM_Level_trigger_array + (uint32_t)(sub >> 4))) SET16(o, PL_TIME, 60);
            return;
        }
        SET16(o, PL_TIME, OB16(o, PL_TIME) - 1);
        if (!OB16(o, PL_TIME)) SET8(o, OBJ_subtype, sub + 1);
        return;
    case 8:
        SET16(o, PL_Y, OB16(o, PL_Y) - 2);
        if ((uint16_t)(OB16(o, PL_BASEY) - 0x200) == OB16(o, PL_Y)) SET8(o, OBJ_subtype, 0);
        return;
    case 0x0A:
        d1 = (int8_t)(uint8_t)(OB8(o, OBJ_angle) - 0x40);
        SET16(o, PL_Y, OB16(o, PL_BASEY) + (d1 >> 1));
        break;
    default:
        return;
    }
    SET8(o, OBJ_angle, osc_byte(0x1A));    /* .chgmotion */
}

static void platform(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_PLAT, 4, 4, 0x20, 0x10);
        if (tri_zone() == TRI_S1_SLZ) SET8(o, OBJ_subtype, 3);   /* SLZ: all fall when stood on */
        SET16(o, PL_Y, OB16(o, OBJ_y_pos));
        SET16(o, PL_BASEY, OB16(o, OBJ_y_pos));
        SET16(o, PL_BASEX, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_angle, 0);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype) == 0x0A ? 1 : 0);
        /* fall through */
    case 2: {
        uint8_t n = OB8(o, PL_NUDGE);
        if (standing(o)) { if (n != 0x40) n += 4; }
        else if (n) n -= 4;
        SET8(o, PL_NUDGE, n);
        uint16_t oldx = OB16(o, OBJ_x_pos);
        plat_move(o);
        if (OB8(o, OBJ_routine) == 2) {
            SET16(o, OBJ_y_pos, OB16(o, PL_Y) + ((sin8(OB8(o, PL_NUDGE)) * 0x400) >> 16));
            o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
        }
        break;
    }
    case 8:   /* falling, no longer solid */
        plat_move(o);
        SET16(o, OBJ_y_pos, OB16(o, PL_Y));
        break;
    default:
        o_delete(o);
        return;
    }
    if (OB8(o, OBJ_routine) == 6) { o_delete(o); return; }
    if (o_out_of_range(o, OB16(o, PL_BASEX))) { release_players(o, 0); o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 15 - swinging platforms (GHZ: platform or giant spiked ball)
 * ---------------------------------------------------------------------- */
#define SW_ORIGY   0x30
#define SW_ORIGX   0x32
#define SW_RADIUS  0x34
#define SW_COUNT   0x35
#define SW_CHILD   0x36       /* up to 9 words */
#define SW_MAXCH   9

static void swing_move(uint32_t o)
{
    uint8_t d0 = osc_byte(0x1A);
    if (OB8(o, OBJ_status) & ST_XFLIP) d0 = (uint8_t)(-d0 + 0x80);
    int16_t s, c;
    o_sincos(d0, &s, &c);
    int n = OB8(o, SW_COUNT);
    for (int i = 0; i <= n; i++) {
        uint32_t a1 = i < n ? (0xFF0000u | OB16(o, SW_CHILD + i * 2)) : o;
        int r = OB8(a1, SW_RADIUS);
        SET16(a1, OBJ_y_pos, OB16(o, SW_ORIGY) + ((r * s) >> 8));
        SET16(a1, OBJ_x_pos, OB16(o, SW_ORIGX) + ((r * c) >> 8));
    }
}

static void swing(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SWING, 4, 3, 0x18, 0x10);
        SET8(o, OBJ_y_radius, 8);
        if (tri_zone() == TRI_S1_SBZ) {      /* SBZ: a swinging spiked ball */
            SET8(o, OBJ_width_px, 0x18);
            SET8(o, OBJ_y_radius, 0x18);
            SET8(o, OBJ_coll_flags, 0x86);
        }
        if (tri_zone() == TRI_S1_SLZ) {      /* SLZ: wider, hurts from below */
            SET8(o, OBJ_width_px, 0x20);
            SET8(o, OBJ_y_radius, 0x10);
            SET8(o, OBJ_coll_flags, 0x99);
        }
        SET16(o, SW_ORIGY, OB16(o, OBJ_y_pos));
        SET16(o, SW_ORIGX, OB16(o, OBJ_x_pos));
        uint8_t sub = OB8(o, OBJ_subtype);
        int n = sub & 0x0F;
        int d3 = n * 16 + 8;
        SET8(o, SW_RADIUS, d3);
        d3 -= 8;
        int cnt = 0;
        for (int i = 0; i <= n && cnt < SW_MAXCH; i++) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET16(o, SW_CHILD + cnt * 2, a1 & 0xFFFF);
            cnt++;
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x15 * 4);
            SET8(a1, OBJ_routine, 0x0A);
            o_init(a1, S1A_SWINGCHAIN, 4, 4, 8, 8);
            SET8(a1, OBJ_frame, 1);
            SET8(a1, SW_RADIUS, d3);
            d3 -= 0x10;
            if (d3 < 0) {                    /* anchor */
                o_asset(a1, tri_zone() == TRI_S1_SBZ ? S1A_SWINGANCH_SBZ : S1A_SWING);
                SET8(a1, OBJ_frame, 2);
                SET16(a1, OBJ_priority, 3 << 7);
            }
        }
        SET8(o, SW_COUNT, cnt);
        if (sub & 0x10) {                    /* giant spiked ball */
            o_asset(o, S1A_GBALL);
            SET8(o, OBJ_frame, 1);
            SET16(o, OBJ_priority, 2 << 7);
            SET8(o, OBJ_coll_flags, 0x81);
        }
    }
    /* fall through */
    case 2: {
        uint16_t oldx = OB16(o, OBJ_x_pos);
        swing_move(o);
        if (OB8(o, OBJ_coll_flags) != 0x81 && OB8(o, OBJ_coll_flags) != 0x86)  /* not the spiked balls */
            o_platform(o, OB8(o, OBJ_width_px), OB8(o, OBJ_y_radius), oldx);
        o_touch(o);
        if (o_out_of_range(o, OB16(o, SW_ORIGX))) {
            release_players(o, 0);
            for (int i = 0; i < OB8(o, SW_COUNT); i++)
                o_delete(0xFF0000u | OB16(o, SW_CHILD + i * 2));
            o_delete_respawnable(o);
            return;
        }
        o_draw(o);
        return;
    }
    case 0x0A:   /* chain link / anchor: positioned by the parent */
        o_draw(o);
        return;
    default:
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 17 - spiked pole helix
 * ---------------------------------------------------------------------- */
#define HEL_PARENTX  0x30
#define HEL_FRAME    0x3E

static void helix_rotate(uint32_t o)
{
    uint8_t f = (uint8_t)((s1_ani0_frame + OB8(o, HEL_FRAME)) & 7);
    SET8(o, OBJ_frame, f);
    SET8(o, OBJ_coll_flags, f ? 0 : 0x84);
}

static void helix(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SPIKEPOLE, 4, 3, 8, 0x10);
        SET8(o, OBJ_status, 7);
        uint16_t y = OB16(o, OBJ_y_pos), x0 = OB16(o, OBJ_x_pos);
        int len = OB8(o, OBJ_subtype);
        SET8(o, OBJ_subtype, 0);
        int d3 = x0 - ((len >> 1) << 4);
        int d6 = 0;
        SET16(o, HEL_PARENTX, x0);
        for (int i = 0; i < len - 1; i++) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x17 * 4);
            SET8(a1, OBJ_routine, 8);
            o_init(a1, S1A_SPIKEPOLE, 4, 3, 8, 0x10);
            SET16(a1, OBJ_y_pos, y);
            SET16(a1, OBJ_x_pos, d3);
            SET16(a1, HEL_PARENTX, x0);
            SET8(a1, HEL_FRAME, d6);
            d6 = (d6 + 1) & 7;
            d3 += 0x10;
            if (d3 == x0) {                  /* centre spike = the parent */
                SET8(o, HEL_FRAME, d6);
                d6 = (d6 + 1) & 7;
                d3 += 0x10;
            }
        }
    }
    if (o_out_of_range(o, OB16(o, HEL_PARENTX))) { o_delete_respawnable(o); return; }
    helix_rotate(o);
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 1A - GHZ collapsing ledge
 * ---------------------------------------------------------------------- */
#define LG_TIME   0x38
#define LG_FLAG   0x3A
static const uint8_t ledge_data1[] = {
    0x1C, 0x18, 0x14, 0x10, 0x1A, 0x16, 0x12, 0x0E, 0x0A, 6, 0x18, 0x14, 0x10, 0x0C, 8, 4,
    0x16, 0x12, 0x0E, 0x0A, 6, 2, 0x14, 0x10, 0x0C, 0 };

static uint32_t ledge_slope(void)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + TRI_BLOB_S1_LEDGE_SLOPE * 4) : 0;
}

static void ledge_fragment(uint32_t o)
{
    SET8(o, LG_FLAG, 0);
    uint8_t frame = (uint8_t)(OB8(o, OBJ_frame) + 2);
    SET8(o, OBJ_frame, frame);
    uint32_t maps = r32(o + OBJ_mappings);
    uint32_t fa = maps + tri_rd16(maps + frame * 2u);
    uint16_t npieces = tri_rd16(fa);
    uint32_t piece = fa + 2;
    uint8_t render = (uint8_t)(OB8(o, OBJ_render) | 0x20);
    SET8(o, OBJ_render, render);
    uint32_t a1 = o;
    for (int i = 0; i < 26 && i < npieces; i++) {
        if (i) {
            a1 = o_alloc();
            if (!a1) break;
            piece += 6;
        }
        SET8(a1, OBJ_routine, 6);
        SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x1A * 4);
        SET32(a1, OBJ_mappings, piece);
        SET8(a1, OBJ_render, render);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
        SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
        SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
        SET8(a1, OBJ_height_px, 0x10);
        SET8(a1, LG_TIME, ledge_data1[i]);
    }
    o_sfx(0x59);   /* sfx_Collapse */
}

static void ledge(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_LEDGE, OB8(o, OBJ_render) | 4 | 0x10, 4, 0x64, 0x40);
        SET8(o, LG_TIME, 7);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        SET8(o, OBJ_y_radius, 0x38);
        /* fall through */
    case 2:   /* Ledge_Touch */
        if (OB8(o, LG_FLAG)) {
            if (!OB8(o, LG_TIME)) {
                ledge_fragment(o);
                return;
            }
            SET8(o, LG_TIME, OB8(o, LG_TIME) - 1);
        }
        o_platform_sloped(o, 0x30, 0, OB16(o, OBJ_x_pos), ledge_slope());
        if (standing(o)) SET8(o, LG_FLAG, 1);
        o_remember(o);
        return;
    case 6: { /* fragment */
        uint8_t t = OB8(o, LG_TIME);
        if (t) {
            SET8(o, LG_TIME, t - 1);
            if (standing(o)) o_platform_sloped(o, 0x30, 0, OB16(o, OBJ_x_pos), ledge_slope());
            o_draw(o);
            return;
        }
        release_players(o, 0);
        o_fall(o);
        o_draw(o);
        if (!(OB8(o, OBJ_render) & 0x80)) o_delete(o);
        return;
    }
    default:
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 3C - smashable wall
 * ---------------------------------------------------------------------- */
static const int16_t smash_spd1[16] = { 0x400, -0x500, 0x600, -0x100, 0x600, 0x100, 0x400, 0x500,
                                        0x600, -0x600, 0x800, -0x200, 0x800, 0x200, 0x600, 0x600 };
static const int16_t smash_spd2[16] = { -0x600, -0x600, -0x800, -0x200, -0x800, 0x200, -0x600, 0x600,
                                        -0x400, -0x500, -0x600, -0x100, -0x600, 0x100, -0x400, 0x500 };

static void smash_wall(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SMASH, 4, 4, 0x10, 0x20);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        /* fall through */
    case 2: {
        int16_t spd[2];
        for (int i = 0; i < 2; i++) spd[i] = OBS16(o_player(i), OBJ_x_vel);
        o_solid(o, 0x1B, 0x20, 0x20, OB16(o, OBJ_x_pos));
        for (int i = 0; i < 2; i++) {
            uint8_t pbit = i ? ST_P2_PUSH : ST_P1_PUSH;
            uint32_t p = o_player(i);
            int push = (OB8(o, OBJ_status) & pbit) != 0;
            if (OB8(p, 0x38) == 2) {                    /* Knuckles breaks it on contact, as S3K's walls: */
                int dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));   /* pushing it, or gliding in */
                int dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
                int glide = OB8(p, 0x2F) == 1 && (OB8(p, OBJ_status) & 2);
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (!push && !(glide && dx <= 0x1B + 0x0C && dy < 0x28)) continue;
            } else {
                if (!push) continue;
                if (OB8(p, OBJ_anim) != 2) continue;
                int s = spd[i] < 0 ? -spd[i] : spd[i];
                if (s < 0x480) continue;
            }
            SET16(p, OBJ_x_vel, spd[i]);
            SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + 4);
            const int16_t *tab = smash_spd1;
            if (!(OB16(o, OBJ_x_pos) < OB16(p, OBJ_x_pos))) {
                SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) - 8);
                tab = smash_spd2;
            }
            SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_PUSH | ST_P2_PUSH));
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~0x20);
            /* SmashObject: 8 single-piece fragments */
            uint32_t maps = r32(o + OBJ_mappings);
            uint32_t fa = maps + tri_rd16(maps + OB8(o, OBJ_frame) * 2u);
            uint32_t piece = fa + 2;
            uint8_t render = (uint8_t)(OB8(o, OBJ_render) | 0x20);
            uint32_t a1 = o;
            for (int k = 0; k < 8; k++) {
                if (k) { a1 = o_alloc(); if (!a1) break; piece += 6; }
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x3C * 4);
                SET8(a1, OBJ_routine, 4);
                SET32(a1, OBJ_mappings, piece);
                SET8(a1, OBJ_render, render);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
                SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
                SET8(a1, OBJ_width_px, 0x10);
                SET8(a1, OBJ_height_px, 0x10);
                SET16(a1, OBJ_x_vel, tab[k * 2]);
                SET16(a1, OBJ_y_vel, tab[k * 2 + 1]);
            }
            o_sfx(0x59);      /* sfx_WallSmash (S3K: collapse, as its breakable walls) */
            {   extern void tri_ach_wall_broken(void); tri_ach_wall_broken(); }   /* (Knuckles' Rock Breaker) */
            return;
        }
        o_remember(o);
        return;
    }
    case 4:   /* Smash_FragMove */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x70);
        o_draw(o);
        if (!(OB8(o, OBJ_render) & 0x80)) o_delete(o);
        return;
    default:
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 36 - spikes (S1 arrangements, S3K hurt rules)
 * ---------------------------------------------------------------------- */
#define SP_ORIGX  0x30
#define SP_ORIGY  0x32
#define SP_OFF    0x34    /* word */
#define SP_DIR    0x36
#define SP_WAIT   0x38
static const uint8_t spik_var[12] = { 0, 0x14, 1, 0x10, 2, 4, 3, 0x1C, 4, 0x40, 5, 0x10 };

static void spikes_move(uint32_t o)
{
    int type = OB8(o, OBJ_subtype) & 0x0F;
    if (type != 1 && type != 2) return;
    /* Spik_Wait */
    if (OB16(o, SP_WAIT)) {
        SET16(o, SP_WAIT, OB16(o, SP_WAIT) - 1);
        if (!OB16(o, SP_WAIT) && (OB8(o, OBJ_render) & 0x80))
            o_sfx(0x52);     /* sfx_SpikeMove */
    } else if (OB16(o, SP_DIR)) {
        int v = (int)OB16(o, SP_OFF) - 0x800;
        if (v < 0) { v = 0; SET16(o, SP_DIR, 0); SET16(o, SP_WAIT, 60); }
        SET16(o, SP_OFF, v);
    } else {
        int v = OB16(o, SP_OFF) + 0x800;
        if (v >= 0x2000) { v = 0x2000; SET16(o, SP_DIR, 1); SET16(o, SP_WAIT, 60); }
        SET16(o, SP_OFF, v);
    }
    int off = OB8(o, SP_OFF);
    if (type == 1) SET16(o, OBJ_y_pos, OB16(o, SP_ORIGY) + off);
    else SET16(o, OBJ_x_pos, OB16(o, SP_ORIGX) + off);
}

static void spikes(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SPIKES, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        uint8_t sub = OB8(o, OBJ_subtype);
        SET8(o, OBJ_subtype, sub & 0x0F);
        int k = ((sub & 0xF0) >> 4) % 6;
        SET8(o, OBJ_frame, spik_var[k * 2]);
        SET8(o, OBJ_width_px, spik_var[k * 2 + 1]);
        SET16(o, SP_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, SP_ORIGY, OB16(o, OBJ_y_pos));
    }
    spikes_move(o);
    uint8_t f = OB8(o, OBJ_frame);
    if (f == 1 || f == 5) {                 /* sideways */
        int d2 = f == 1 ? 0x14 : 4;
        o_solid(o, 0x1B, d2, d2 + 1, OB16(o, OBJ_x_pos));
        uint32_t side = g_o_d6 >> 16;
        if (side & 1) { o_spike_hurt(o, RAM_Player_1); SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH); }
        if (side & 2) { o_spike_hurt(o, RAM_Player_2); SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P2_PUSH); }
    } else {
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_YFLIP) {  /* hanging: hurts from below */
            uint32_t bottom = g_o_d6 >> 16;
            if (bottom & 4) o_spike_hurt(o, RAM_Player_1);
            if (bottom & 8) o_spike_hurt(o, RAM_Player_2);
        } else {
            uint8_t st = OB8(o, OBJ_status);
            if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
            if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
        }
    }
    if (o_out_of_range(o, OB16(o, SP_ORIGX))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

void s1_obj_ghz2_register(TriObjFn *t)
{
    t[0x15] = swing;
    t[0x17] = helix;
    t[0x18] = platform;
    t[0x1A] = ledge;
    t[0x36] = spikes;
    t[0x3C] = smash_wall;
}
