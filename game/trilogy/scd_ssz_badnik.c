/*
 * scd_ssz_badnik.c — Stardust Speedway's badniks, acts 1 and 2 (Sonic CD;
 * decomp: R7\KANABUN.C, KABASIRA.C, HOTARU7.C, DANGO7.C; the disc's code is
 * followed).
 *
 *   $21 Kanabun   a beetle flying to and fro (64 px either side), bobbing,
 *                 to the front and back (only near the front can it be hit)
 *   $22 Kabasira  a mosquito with three ghosts weaving round it; with the
 *                 player near it fires a sting at him (every 240 frames);
 *                 the sting hurts (an invincible player breaks it), or
 *                 pops on a ball (subtype2 $FF: a
 *                 ghost, $FE: a sting; kept in a byte of their own)
 *   $23 Hotaru    a pair of fireflies (the placed one is unseen and makes
 *                 them; subtype & $C: where they are, bit 0: damaged); with
 *                 the player near they fly to the screen's middle, stay
 *                 there (screen-fixed), light up, and a line of sparks runs
 *                 between them (not the damaged ones); then they fly away up.
 *                 Destroyed ones stay destroyed (respawn bits 0, 1)
 *   $2A Dango     a pill bug walking; facing the player near it, it curls up
 *                 and rolls at him; a ball hitting it bounces off ($700)
 *                 and sends it rolling the other way
 * Subtype 0 is the robot as built, any other its damaged self (slower); a
 * good future blows those up (DestroyOnGoodFuture).
 */
#include "trilogy_obj.h"


#define OBJ_prev_anim   0x21               /* (what o_animate compares) */
#define OBJ_status2     0x2B               /* (status_secondary: bit 1 invincible) */
#define P_JUMPING       0x40
#define ST_AIR          0x02
#define FM_STING        0xA0               /* (act_move: Kabasira fires a sting, on screen) */
#define FM_EXPLODE      0x9E               /* (kabasira: a sting pops) */

void scd_ssz_block_prio(uint32_t o, int x, int y);   /* scd_ssz.c: GetLevelBlock's sprite priority */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void badnik_init(uint32_t o, int asset, int prio, int xr, int yr, uint8_t coll)
{
    o_init(o, asset, OB8(o, OBJ_render) | 4, prio, xr, yr);
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_coll_flags, coll ? tri_scd_coll(coll) : 0);
}

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

static void badnik_end(uint32_t o, uint16_t home)   /* DrawObject + CheckObjDespawn, touchable */
{
    if (cd_despawn(o, home)) return;
    o_touch(o);
    o_draw(o);
}

/* (in place) the explosion Sonic CD's turns into with routine 2 set: no
 * flower, no points; the sound its own */
static void pop(uint32_t o)
{
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    uint8_t render = OB8(o, OBJ_render);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 6);
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
    SET8(o, OBJ_render, render & 0x80);
    o_sfx_cd(FM_EXPLODE);
}

/* (in place) destroyed as a badnik by an invincible player (CD's
 * TouchEnemy): 100 points, S3K's explosion (its sound CD's FM_DESTROY) */
static void badnik_kill(uint32_t o)
{
    SET8(o, OBJ_coll_flags, 0);
    D(0) = 10;                             /* (100 points) */
    tri_jsr(S3K_HUD_AddToScore);
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
}

/* ---- $21 Kanabun -------------------------------------------------------- */
#define KN_Y0           0x30               /* oVar2E (long: its line, + $8000) */
#define KN_ANG          0x34               /* oVar32 */
#define KN_ASPD         0x36               /* oVar34 */
#define KN_X0           0x38               /* oVar3A */
#define KN_VX           0x3A               /* oVar3C (long) */
static void kanabun(uint32_t o)
{
    static const uint8_t coll[4] = { 0x2F, 0x30, 0x30, 0 };   /* (by depth: frames / 2) */
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_KANABUN2 : SCDA_KANABUN;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x10, 0x10, 0);
        SET16(o, KN_X0, OB16(o, OBJ_x_pos));
        SET32(o, KN_Y0, (uint32_t)OB16(o, OBJ_y_pos) << 16 | 0x8000);
        SET16(o, KN_ASPD, dmg ? (uint16_t)-0x100 : (uint16_t)-0x200);
        SET32(o, KN_VX, (uint32_t)-0x4000);
    }
    uint32_t d6 = r32(o + OBJ_x_pos) + r32(o + KN_VX);
    uint16_t xf = (uint16_t)d6;            /* (CD's d0 keeps it over CalcSine: in the bob's fraction) */
    int16_t d0 = (int16_t)((d6 >> 16) - OB16(o, KN_X0));
    if ((d0 < 0 ? -d0 : d0) > 0x40) {      /* (64 px out: back, from the far side) */
        SET32(o, KN_VX, (uint32_t)-(int32_t)r32(o + KN_VX));
        d6 += r32(o + KN_VX);
        turn(o);
        SET16(o, KN_ANG, 0);
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_prev_anim, 0xFF);
    }
    SET32(o, OBJ_x_pos, d6);
    SET16(o, KN_ANG, OB16(o, KN_ANG) + OB16(o, KN_ASPD));
    int16_t s, c;
    o_sincos((uint8_t)(OB16(o, KN_ANG) >> 8), &s, &c);
    int32_t v = (int32_t)((uint32_t)(uint16_t)s << 16 | xf) >> 2;
    SET32(o, OBJ_y_pos, r32(o + KN_Y0) + (uint32_t)v);
    o_animate(o, o_asset_anim(asset));
    uint8_t t = coll[(OB8(o, OBJ_frame) >> 1) & 3];
    SET8(o, OBJ_coll_flags, t ? tri_scd_coll(t) : 0);
    badnik_end(o, OB16(o, KN_X0));
}

/* ---- $22 Kabasira ------------------------------------------------------- */
#define KS_X0           0x30               /* oVar2A (long, + $8000); a sting's x speed */
#define KS_Y0           0x34               /* oVar2E (long, + $8000); a sting's y speed */
#define KS_XANG         0x38               /* oVar32 */
#define KS_YANG         0x3A               /* oVar34 */
#define KS_XSPD         0x3C               /* oVar36 */
#define KS_YSPD         0x3E               /* oVar38 */
#define KS_TIME         0x40               /* oVar3A: to the next sting */
#define KS_PARENT       0x42               /* oVar3E */
#define KS_ROLE         0x44               /* oSubtype2: 0 the mosquito, $FF a ghost, $FE a sting */
static void ks_setup(uint32_t a1)
{
    SET8(a1, OBJ_routine, 2);
    o_init(a1, SCDA_KABASIRA, OB8(a1, OBJ_render) | 4, 3, 0x0C, 8);
    SET8(a1, OBJ_x_radius, 0x0C);
    SET8(a1, OBJ_y_radius, 8);
}

static void ks_sting(uint32_t o)
{
    cd_move(o, (int32_t)r32(o + KS_X0), (int32_t)r32(o + KS_Y0));
    /* (CD's $F1: special; on a player not in a ball, as a badnik: it hurts,
     * or an invincible one breaks it; on a ball it pops) */
    int hit = o_s1_touch(o, 5, 5);
    for (int n = 0; n < 2; n++) {
        if (!(hit >> n & 1)) continue;
        uint32_t p = o_player(n);
        if (OB8(p, OBJ_status) & 4) { pop(o); return; }
        if (OB8(p, OBJ_status2) & 2) { badnik_kill(o); return; }
        o_hurt_player(p, o);
    }
    int dy = OBS16(RAM_Player_1, OBJ_y_pos) - OBS16(o, OBJ_y_pos);
    if ((dy < 0 ? -dy : dy) > 0xE0) { o_delete(o); return; }
    o_animate(o, o_asset_anim(SCDA_KABASIRA));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

static void kabasira(uint32_t o)
{
    uint8_t role = OB8(o, KS_ROLE);
    if (role == 0xFE) { ks_sting(o); return; }
    if (role == 0xFF) {                    /* (a ghost: its mosquito gone, so is it) */
        if (r32(0xFF0000u | OB16(o, KS_PARENT)) != CD_CODE(0x22)) { o_delete(o); return; }
    } else if (cd_destroy_good_future(o)) return;
    if (!OB8(o, OBJ_routine)) {
        int dmg = OB8(o, OBJ_subtype) != 0;
        SET32(o, KS_X0, (uint32_t)OB16(o, OBJ_x_pos) << 16 | 0x8000);
        SET32(o, KS_Y0, (uint32_t)OB16(o, OBJ_y_pos) << 16 | 0x8000);
        SET16(o, KS_XSPD, dmg ? 0x100 : 0x200);
        SET16(o, KS_YSPD, dmg ? 0x80 : 0x100);
        SET8(o, OBJ_anim, dmg ? 0 : 4);
        ks_setup(o);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x31));
        uint16_t d5 = dmg ? 0x2000 : 0x1000, d4 = dmg ? 0x1000 : 0x800, d3 = 0, d2 = 0;
        for (int k = 1; k <= 3; k++) {     /* (its ghosts, a step behind each other) */
            uint32_t a1 = o_alloc_after(o);
            if (!a1) { o_delete(o); return; }
            SET32(a1, OBJ_code, CD_CODE(0x22));
            SET16(a1, KS_PARENT, (uint16_t)o);
            SET8(a1, OBJ_anim, OB8(o, OBJ_anim) + k);
            d3 = (uint16_t)(d3 - d5);
            d2 = (uint16_t)(d2 - d4);
            SET16(a1, KS_XANG, d3);
            SET16(a1, KS_YANG, d2);
            SET8(a1, KS_ROLE, 0xFF);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET32(a1, KS_X0, r32(o + KS_X0));
            SET32(a1, KS_Y0, r32(o + KS_Y0));
            SET16(a1, KS_XSPD, OB16(o, KS_XSPD));
            SET16(a1, KS_YSPD, OB16(o, KS_YSPD));
            ks_setup(a1);
        }
    } else {
        if (!OB8(o, OBJ_subtype) && !role) {   /* (the mosquito as built: its sting) */
            if (OB16(o, KS_TIME)) SET16(o, KS_TIME, OB16(o, KS_TIME) - 1);
            else {
                uint32_t p = RAM_Player_1;
                int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
                int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
                if ((uint16_t)(dy + 0x60) < 0xC0 && (uint16_t)(dx + 0x78) < 0xF0) {
                    SET16(o, KS_TIME, 0xF0);
                    uint32_t a1 = o_alloc();
                    if (a1) {
                        SET8(a1, OBJ_anim, 8);
                        SET8(a1, KS_ROLE, 0xFE);
                        SET32(a1, OBJ_code, CD_CODE(0x22));
                        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                        ks_setup(a1);
                        int16_t s, c;   /* (at him, 2 px a frame) */
                        o_sincos(o_atan(dx, dy), &s, &c);
                        SET32(a1, KS_Y0, (uint32_t)((int32_t)s * 0x200));
                        SET32(a1, KS_X0, (uint32_t)((int32_t)c * 0x200));
                        if (c < 0) turn(a1);
                        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_STING);
                    }
                }
            }
        }
        /* (round its line: 16 px across, 64 up and down; CD's d0 / d1 high
         * words, left over from the frame's other code, are taken as 0) */
        int16_t s, c;
        SET16(o, KS_XANG, OB16(o, KS_XANG) + OB16(o, KS_XSPD));
        o_sincos((uint8_t)(OB16(o, KS_XANG) >> 8), &s, &c);
        SET32(o, OBJ_x_pos, r32(o + KS_X0) + (uint32_t)((int32_t)s * 0x1000));
        SET16(o, KS_YANG, OB16(o, KS_YANG) + OB16(o, KS_YSPD));
        o_sincos((uint8_t)(OB16(o, KS_YANG) >> 8), &s, &c);
        SET32(o, OBJ_y_pos, r32(o + KS_Y0) + (uint32_t)((int32_t)c * 0x4000));
    }
    o_animate(o, o_asset_anim(SCDA_KABASIRA));
    badnik_end(o, OB16(o, OBJ_x_pos));
}

/* ---- $23 Hotaru --------------------------------------------------------- */
#define HT_ROLE         0x3A               /* oSubtype2: 0 the maker, $FF a firefly, $FE a spark */
/* the maker's */
#define HT_OFS          0x30               /* oVar2E: its pair's places */
#define HT_FF1          0x32               /* oVar32 */
#define HT_FF2          0x34               /* oVar34 */
/* a firefly's */
#define HT_PARENT       0x30               /* oVar2A */
#define HT_X0           0x32               /* oVar2C */
#define HT_SPD          0x34               /* oVar2E (long) */
#define HT_PID          0x38               /* oVar36: its maker's respawn entry (slot reused?) */
/* a spark's */
#define HT_A            0x30               /* oVar2A */
#define HT_B            0x32               /* oVar2C */
static int ht_near(uint32_t o, int d1, int d2, int d3, int d4)   /* (the range test, 20E202) */
{
    uint32_t p = RAM_Player_1;
    return (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - d1) < (uint16_t)d2 &&
           (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) - d3) < (uint16_t)d4;
}

static uint32_t ram_at(uint32_t o, int f)
{
    uint16_t w = OB16(o, f);
    return w ? 0xFF0000u | w : 0;
}

static void ht_maker(uint32_t o)
{
    static const int16_t places[4][2] = { { 0x24, 0x3C }, { 0x08, -0x44 }, { 0x38, -0x38 }, { 0x50, 0x10 } };
    uint32_t mark = OB16(o, OBJ_respawn) ? 0xFF0000u | OB16(o, OBJ_respawn) : 0;
    uint8_t flags = mark ? r8(mark) : 0;   /* (bits 0, 1: its fireflies destroyed) */
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        SET8(o, HT_OFS, (OB8(o, OBJ_subtype) >> 2) & 3);
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 1 ? 0xFF : 0);
        o_asset(o, SCDA_HOTARU);           /* ($845D) */
        /* fall through */
    case 2:                                /* (unseen till he is near) */
        if (ht_near(o, -0xF0, 0x1E0, -0xF0, 0x1E0)) SET8(o, OBJ_routine, 4);
        cd_despawn(o, OB16(o, OBJ_x_pos));
        return;
    case 4: {
        SET8(o, OBJ_routine, 6);
        const int16_t *d = places[OB8(o, HT_OFS)];
        for (int k = 0; k < 2; k++) {
            if (flags & (1 << k)) continue;
            uint32_t a1 = o_alloc_after(o);
            if (!a1) { o_delete_respawnable(o); return; }
            SET16(o, k ? HT_FF2 : HT_FF1, (uint16_t)a1);
            SET16(a1, HT_PARENT, (uint16_t)o);
            SET32(a1, OBJ_code, CD_CODE(0x23));
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET8(a1, HT_ROLE, 0xFF);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + (k ? -d[0] : d[0]));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + (k ? -d[1] : d[1]));
            SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
            SET16(a1, HT_PID, OB16(o, OBJ_respawn));
        }
        return;
    }
    default:                               /* (watching them) */
        for (int k = 0; k < 2; k++) {
            uint32_t a = ram_at(o, k ? HT_FF2 : HT_FF1);
            if ((flags & (1 << k)) || !a || r32(a) == CD_CODE(0x23)) continue;
            flags |= (uint8_t)(1 << k);    /* (not a firefly now: destroyed) */
            if (mark) w8(mark, flags);
            SET16(o, k ? HT_FF2 : HT_FF1, 0);
        }
        if (mark && flags == 0x83) { o_delete(o); return; }   /* (both destroyed: not again) */
        if (!OB16(o, HT_FF1) && !OB16(o, HT_FF2)) o_delete_respawnable(o);
        return;
    }
}

static void ht_spawn_line(uint32_t o)      /* (firefly 1, both on screen: three sparks between them) */
{
    uint32_t par = ram_at(o, HT_PARENT), a5 = ram_at(par, HT_FF1), a6 = ram_at(par, HT_FF2);
    if (!a5 || !a6 || a5 != o || !(OB8(a5, OBJ_render) & 0x80) || !(OB8(a6, OBJ_render) & 0x80)) return;
    int16_t x = OBS16(a5, OBJ_x_pos), y = (int16_t)(OBS16(a5, OBJ_y_pos) + 7);
    int16_t dx = (int16_t)((int16_t)(OB16(a6, OBJ_x_pos) - OB16(a5, OBJ_x_pos)) >> 2);
    int16_t dy = (int16_t)((int16_t)(OB16(a6, OBJ_y_pos) - OB16(a5, OBJ_y_pos)) >> 2);
    for (int k = 0; k < 3; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET16(a1, HT_A, (uint16_t)a5);
        SET16(a1, HT_B, (uint16_t)a6);
        SET32(a1, OBJ_code, CD_CODE(0x23));
        SET8(a1, OBJ_subtype, OB8(a5, OBJ_subtype));
        SET8(a1, HT_ROLE, 0xFE);
        SET16(a1, OBJ_art_tile, OB16(a5, OBJ_art_tile));
        x = (int16_t)(x + dx);
        y = (int16_t)(y + dy);
        SET16(a1, OBJ_x_pos, (uint16_t)x);
        SET16(a1, OBJ_y_pos, (uint16_t)y);
    }
}

static void ht_spark(uint32_t o)
{
    uint32_t a = ram_at(o, HT_A), b = ram_at(o, HT_B);
    if (!a || !b || r32(a) != CD_CODE(0x23) || r32(b) != CD_CODE(0x23) ||
        OB8(a, OBJ_routine) != 0x10) { o_delete(o); return; }   /* (CD tests the first firefly's routine twice) */
    if (OB8(o, OBJ_routine) >= 4) { o_delete(o); return; }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        uint16_t tile = OB16(o, OBJ_art_tile);
        o_init(o, SCDA_HOTARU_SPARK, OB8(o, OBJ_render) | 4, 2, 0x0C, 0x0C);
        SET16(o, OBJ_art_tile, tile);
        SET8(o, OBJ_x_radius, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xB4));
    }
    o_animate(o, o_asset_anim(SCDA_HOTARU_SPARK));
    o_touch(o);
    o_draw(o);
}

/* both fireflies on toward the screen's middle (each of them moves both) */
static void ht_gather(uint32_t o, uint32_t a3, uint32_t a4)
{
    int32_t spd = (int32_t)r32(o + HT_SPD);
    uint32_t v = (uint32_t)(spd < 0 ? -spd : spd);
    int xok = 0, yok = 0;
    int16_t cx = (int16_t)(((int16_t)(OB16(a3, OBJ_x_pos) + OB16(a4, OBJ_x_pos)) >> 1) - r16(RAM_Camera_X_pos));
    if (cx < 0x98 || cx > 0xA8) {
        int right = cx < 0x98;
        for (int k = 0; k < 2; k++) {
            uint32_t f = k ? a4 : a3;
            SET8(f, OBJ_render, right ? OB8(f, OBJ_render) | 1 : OB8(f, OBJ_render) & ~1);
            SET8(f, OBJ_status, right ? OB8(f, OBJ_status) | 1 : OB8(f, OBJ_status) & ~1);
            SET32(f, OBJ_x_pos, r32(f + OBJ_x_pos) + (right ? v : (uint32_t)-(int32_t)v));
        }
    } else
        xok = 1;
    int16_t cy = (int16_t)(((int16_t)(OB16(a3, OBJ_y_pos) + OB16(a4, OBJ_y_pos)) >> 1) - r16(RAM_Camera_Y_pos));
    if (cy < 0x68 || cy > 0x78) {
        uint32_t d = cy < 0x68 ? v : (uint32_t)-(int32_t)v;
        SET32(a3, OBJ_y_pos, r32(a3 + OBJ_y_pos) + d);
        SET32(a4, OBJ_y_pos, r32(a4 + OBJ_y_pos) + d);
    } else
        yok = 1;
    if (xok && yok) { SET8(a3, OBJ_routine, 6); SET8(a4, OBJ_routine, 6); }
}

/* (gone: its maker forgets it; the first word if it is that, else the second) */
static void ht_forget(uint32_t par, uint32_t o)
{
    SET16(par, ram_at(par, HT_FF1) == o ? HT_FF1 : HT_FF2, 0);
}

static void ht_firefly(uint32_t o)
{
    uint32_t par = ram_at(o, HT_PARENT);
    if (!par || r32(par) != CD_CODE(0x23) || OB16(par, OBJ_respawn) != OB16(o, HT_PID)) { o_delete(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_HOTARU2 : SCDA_HOTARU;
    int anim = 1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint16_t tile = OB16(o, OBJ_art_tile);
        badnik_init(o, asset, 1, 0x14, 0x0C, 0x32);
        SET16(o, OBJ_art_tile, tile);
        SET16(o, HT_X0, OB16(o, OBJ_x_pos));
        SET32(o, HT_SPD, dmg ? (uint32_t)-0x8000 : (uint32_t)-0x10000);
        if (OBS16(RAM_Player_1, OBJ_x_pos) >= OBS16(o, OBJ_x_pos)) {   /* (he is on its right: that way) */
            SET32(o, HT_SPD, (uint32_t)-(int32_t)r32(o + HT_SPD));
            turn(o);
        }
        return;
    }
    case 2: {                              /* to and fro, 80 px either side */
        cd_move(o, (int32_t)r32(o + HT_SPD), 0);
        int16_t d = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, HT_X0));
        if ((d < 0 ? -d : d) >= 0x50) {
            cd_move(o, -(int32_t)r32(o + HT_SPD), 0);
            SET32(o, HT_SPD, (uint32_t)-(int32_t)r32(o + HT_SPD));
            turn(o);
        }
        if (ht_near(o, -0x38, 0x70, -0x50, 0xA0)) {
            uint32_t a = ram_at(par, HT_FF1), b = ram_at(par, HT_FF2);
            if (a) SET8(a, OBJ_routine, 4);
            if (b) SET8(b, OBJ_routine, 4);
        }
        anim = 0;
        break;
    }
    case 4: {
        uint32_t a = ram_at(par, HT_FF1), b = ram_at(par, HT_FF2);
        if (a && b) { ht_gather(o, a, b); anim = 0; break; }
        SET8(o, OBJ_routine, 6);           /* (one is gone: straight on) */
    }   /* fall through */
    case 6:                                /* (fixed to the screen now) */
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_x_radius, 0x0C);
        SET8(o, OBJ_width_px, 0x0C);
        SET8(o, OBJ_y_radius, 0x14);
        SET8(o, OBJ_height_px, 0x14);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x33));
        SET8(o, OBJ_render, 0);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - r16(RAM_Camera_X_pos) + 0x80);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - r16(RAM_Camera_Y_pos) + 0x80);
        /* fall through */
    case 8:                                /* (its script ends $FC) */
        o_animate(o, o_asset_anim(asset));
        o_draw(o);
        return;
    case 0x0A:                             /* (back into the level) */
        SET8(o, OBJ_routine, 0x0C);
        SET8(o, OBJ_anim, 1);
        SET8(o, OBJ_prev_anim, 0xFF);
        SET8(o, OBJ_render, 4);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x80 + r16(RAM_Camera_X_pos));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x80 + r16(RAM_Camera_Y_pos));
        break;
    case 0x0E:                             /* (lit: the sparks) */
        SET8(o, OBJ_routine, 0x10);
        SET8(o, OBJ_anim, 2);
        SET8(o, OBJ_prev_anim, 0xFF);
        if (!dmg) ht_spawn_line(o);
        break;
    case 0x12:
        SET8(o, OBJ_routine, 0x14);
        SET8(o, OBJ_anim, 3);
        SET8(o, OBJ_prev_anim, 0xFF);
        break;
    case 0x14:                             /* (away, up) */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 2);
        if (!(OB8(o, OBJ_render) & 0x80) &&
            (int16_t)(OB16(RAM_Player_1, OBJ_y_pos) - OB16(o, OBJ_y_pos)) > 0xA0) {
            o_delete(o);
            ht_forget(par, o);
            return;
        }
        break;
    default:                               /* ($C, $10: its scripts end $FC) */
        break;
    }
    if (anim) o_animate(o, o_asset_anim(asset));
    o_touch(o);
    o_draw(o);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) ht_forget(par, o);
}

static void hotaru(uint32_t o)
{
    uint8_t role = OB8(o, HT_ROLE);
    if (role == 0xFF) ht_firefly(o);
    else if (role == 0xFE) ht_spark(o);
    else ht_maker(o);
}

/* ---- $2A Dango ---------------------------------------------------------- */
#define DG_SPD          0x30               /* oVar2A (long) */
static void dg_speed(uint32_t o, int32_t d0, int32_t d1)   /* (its way: the way it faces) */
{
    if (OB8(o, OBJ_subtype)) d0 = d1;
    if (!(OB8(o, OBJ_render) & 1)) d0 = -d0;
    SET32(o, DG_SPD, (uint32_t)d0);
}

static int dg_step(uint32_t o)             /* (a step on: 0 at a wall or an edge, not taken) */
{
    int32_t v = (int32_t)r32(o + DG_SPD);
    cd_move(o, v, 0);
    int xr = OB8(o, OBJ_x_radius);
    int16_t d1 = (int16_t)OB16(o, DG_SPD) < 0 ? o_wall_left(o, -xr) : o_wall_right(o, xr);
    scd_ssz_block_prio(o, OBS16(o, OBJ_x_pos) + ((int16_t)OB16(o, DG_SPD) < 0 ? -xr : xr), OBS16(o, OBJ_y_pos));
    if (d1 >= 0) {
        d1 = o_floor_dist(o);
        scd_ssz_block_prio(o, OBS16(o, OBJ_x_pos), OBS16(o, OBJ_y_pos) + (int8_t)OB8(o, OBJ_y_radius));
        if ((uint16_t)(d1 + 7) < 14) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            return 1;
        }
    }
    cd_move(o, -v, 0);
    return 0;
}

/* rolling: a ball (player) hitting it bounces off, away from it at $700, and
 * it rolls the way he came (CD's $ED, special: on a player not in a ball,
 * as a badnik: it hurts, or an invincible one breaks it) */
static int dg_roll_hit(uint32_t o)
{
    int hit = o_s1_touch(o, 0x10, 0x10);
    for (int n = 0; n < 2; n++) {
        if (!(hit >> n & 1)) continue;
        uint32_t p = o_player(n);
        if (!(OB8(p, OBJ_status) & 4)) {
            if (OB8(p, OBJ_status2) & 2) { badnik_kill(o); return -1; }
            o_hurt_player(p, o);
            continue;
        }
        int16_t s, c;
        o_sincos(o_atan((int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)),
                        (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos))), &s, &c);
        int16_t vx = (int16_t)((c * -0x700) >> 8), vy = (int16_t)((s * -0x700) >> 8);
        SET16(p, OBJ_x_vel, (uint16_t)vx);
        SET16(p, OBJ_y_vel, (uint16_t)vy);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~0x30);
        SET8(p, P_JUMPING, 0);
        SET32(o, DG_SPD, (uint32_t)((int32_t)(int16_t)-vx * 0x100));
        SET8(o, OBJ_anim, 4);
        SET8(o, OBJ_routine, 0x16);
        return 1;
    }
    return 0;
}

static void dango(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_SSZ_DANGO2 : SCDA_SSZ_DANGO;
    uint32_t anim = o_asset_anim(asset);
    switch (OB8(o, OBJ_routine)) {
    case 0:                                /* ($23E9 in the past, else $245D: the asset's) */
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x0E, 9, 0x2C);
        break;
    case 2: {                              /* (down onto the floor) */
        cd_move(o, 0, 0x10000);
        int16_t d1 = o_floor_dist(o);
        scd_ssz_block_prio(o, OBS16(o, OBJ_x_pos), OBS16(o, OBJ_y_pos) + (int8_t)OB8(o, OBJ_y_radius));
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    case 4:
        SET8(o, OBJ_routine, 6);
        dg_speed(o, 0x6000, 0x1000);
        break;
    case 6: {                              /* walking: facing him near, it curls up */
        if (!dg_step(o)) {
            SET32(o, DG_SPD, (uint32_t)-(int32_t)r32(o + DG_SPD));
            turn(o);
            break;
        }
        uint32_t p = RAM_Player_1;
        int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if ((uint16_t)(dx + 0x80) < 0x100 && (uint16_t)(dy + 0x80) < 0x100 &&
            ((OB8(o, OBJ_render) & 1) != 0) == (dx >= 0))
            SET8(o, OBJ_routine, 8);
        o_animate(o, anim);
        break;
    }
    case 8: case 0x0C: case 0x10: case 0x14: case 0x18:   /* (the next animation, the next shape) */
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        switch (OB8(o, OBJ_routine)) {
        case 0x0E:                         /* (curling) */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 6);
            SET8(o, OBJ_y_radius, 0x10);
            SET8(o, OBJ_height_px, 0x10);
            break;
        case 0x12:                         /* (a ball) */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 3);
            SET8(o, OBJ_y_radius, 0x0D);
            SET8(o, OBJ_height_px, 0x0D);
            SET8(o, OBJ_coll_flags, 0);
            SET32(o, DG_SPD, 0);
            break;
        case 0x16:                         /* (rolling) */
            dg_speed(o, 0x40000, 0x10000);
            break;
        case 0x1A:                         /* (opening out) */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 3);
            SET8(o, OBJ_y_radius, 0x10);
            SET8(o, OBJ_height_px, 0x10);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x2C));
            break;
        }
        break;
    case 0x12: case 0x16: {                /* a ball: hit, rolls on; stopped, it opens out */
        int r = dg_roll_hit(o);
        if (r < 0) return;
        if (r) break;
        if (dg_step(o)) o_animate(o, anim);
        else SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        break;
    }
    case 0x1C:                             /* (walking again) */
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_anim, 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 6);
        SET8(o, OBJ_y_radius, 9);
        SET8(o, OBJ_height_px, 9);
        break;
    default:                               /* ($A, $E, $1A: its scripts end $FC) */
        o_animate(o, anim);
        break;
    }
    badnik_end(o, OB16(o, OBJ_x_pos));
}

void scd_ssz_badnik_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x21] = kanabun;
    t[base + 0x22] = kabasira;
    t[base + 0x23] = hotaru;
    t[base + 0x2A] = dango;
}
