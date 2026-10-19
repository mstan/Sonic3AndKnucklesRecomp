/*
 * scd_ppz.c — Palmtree Panic (Sonic CD; dobby: Level/Palmtree Panic/Objects,
 * Level/_Objects/Sonic; decomp: TREE.C, PLAYER.C, ACTION.C).
 *
 * The 3D ramps of act 1's present. Their walls are solid only to the side,
 * so a player running up one leaves the ground: while the player is inside
 * one of the ramps' 256 px chunks (the level's own layout), rising faster
 * than $800 skips gravity, the air angle stays, and there is no steering in
 * the air; the booster at the ramp's foot sets the rise to $C00. Leaving the
 * ramp's chunks upwards that fast launches the player sideways at $600.
 * The player routine of S3K runs between this file's two hooks (TH_LVL_PRE /
 * POST_SPRITES): the ramp is checked before it moves, gravity is undone
 * after it (MoveSprite adds it after moving, so undoing it is exact).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>

#define RAM_Ctrl_1      0xFFF604u      /* the pad as read (held, pressed) */
#define ST_AIR          0x02

static int ppz1(void) { return tri_zone() == TRI_CD_ZONE(0, TRI_CD_PRESENT) && tri_act() == 0; }

/* ---- the player on a 3D ramp (oPlayerCtrl bit 1) ------------------------ */
static uint8_t s_ramp[2];
static struct { int16_t yvel, xvel; uint8_t air, angle, on, locked; } s_pre[2];

int scd_ramp(int n) { return ppz1() && s_ramp[n & 1]; }

/* ObjSonic_Handle3DRamp */
static void handle_ramp(int n, uint32_t p)
{
    static const uint8_t chunks[] = { 6, 7, 8, 0x49, 0x4C };
    uint32_t e = tri_dir(), info = e ? tri_rd32(e + TD_EXTRA) : 0;
    unsigned col = (OB16(p, OBJ_x_pos) >> 8) & 0x7F, row = (OB16(p, OBJ_y_pos) >> 8) & 7;
    uint8_t c = info && col < 64 ? tri_rd8(info + row * 64u + col) : 0;
    for (unsigned k = 0; k < sizeof chunks; k++)
        if (c == chunks[k]) { s_ramp[n] = 1; return; }
    if (!s_ramp[n]) return;
    s_ramp[n] = 0;
    int16_t vy = OBS16(p, OBJ_y_vel);
    if (vy >= -0x800) return;                                 /* (launching off it?) */
    SET16(p, OBJ_x_vel, OB8(p, OBJ_status) & ST_XFLIP ? -0x600 : 0x600);
}

static int playing(uint32_t p)
{
    return o_player_active(p) && OB8(p, OBJ_routine) == 2 && !r16(RAM_Debug_placement_mode);
}

static void pre_sprites(void)
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        s_pre[n].on = 0;
        if (!playing(p)) { s_ramp[n] = 0; continue; }
        handle_ramp(n, p);
        s_pre[n].yvel = OBS16(p, OBJ_y_vel);
        s_pre[n].xvel = OBS16(p, OBJ_x_vel);
        s_pre[n].air = OB8(p, OBJ_status) & ST_AIR;
        s_pre[n].angle = OB8(p, OBJ_angle);
        s_pre[n].on = 1;
    }
    /* ObjSonic_MoveAir: no steering on a ramp; over the first ramp's launch
     * none at all (not even the air drag: post_sprites) */
    uint32_t p = RAM_Player_1;
    uint16_t x = OB16(p, OBJ_x_pos);
    s_pre[0].locked = s_pre[0].on && s_pre[0].air && x >= 0x6C8 && x < 0x840;
    if (s_pre[0].on && s_pre[0].air && (s_ramp[0] || s_pre[0].locked))
        w8(RAM_Ctrl_1, r8(RAM_Ctrl_1) & ~0x0Cu);
}

static void post_sprites(void)
{
    uint32_t p1 = RAM_Player_1;
    int16_t xv = s_pre[0].xvel, drag = (int16_t)(xv >> 5);
    if (s_pre[0].locked && playing(p1) && (OB8(p1, OBJ_status) & ST_AIR) && drag &&
        OBS16(p1, OBJ_x_vel) == xv - drag) {                  /* (Sonic_ChgJumpDir's drag, undone) */
        SET16(p1, OBJ_x_vel, xv);
        SET32(p1, OBJ_x_pos, r32(p1 + OBJ_x_pos) + (uint32_t)((int32_t)drag * 256));
    }
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!s_pre[n].on || !s_ramp[n] || !playing(p) || !(OB8(p, OBJ_status) & ST_AIR))
            continue;
        /* ObjMoveGrv: rising faster than $800, no gravity */
        if (s_pre[n].air && s_pre[n].yvel < -0x800 && OBS16(p, OBJ_y_vel) == s_pre[n].yvel + 0x38)
            SET16(p, OBJ_y_vel, s_pre[n].yvel);
        SET8(p, OBJ_angle, s_pre[n].angle);                   /* (ObjSonic_JumpAngle: kept) */
    }
    static int log = -1;
    if (log < 0) log = getenv("TRILOGY_RAMPLOG") != NULL;        /* development */
    if (log) {
        uint32_t p = RAM_Player_1;
        fprintf(stderr, "[ramp] f=%u x=%04X y=%04X xv=%d yv=%d gv=%d st=%02X ang=%02X anim=%02X ramp=%d\n",
                (unsigned)r16(RAM_Level_frame_counter), OB16(p, OBJ_x_pos), OB16(p, OBJ_y_pos), OBS16(p, OBJ_x_vel),
                OBS16(p, OBJ_y_vel), OBS16(p, OBJ_ground_vel), OB8(p, OBJ_status), OB8(p, OBJ_angle),
                OB8(p, OBJ_anim), s_ramp[0]);
    }
}

void scd_ppz3_level_entry(void);       /* scd_ppz_boss.c */

void scd_ppz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) scd_ppz3_level_entry();
    if (!ppz1()) { s_ramp[0] = s_ramp[1] = 0; return; }
    if (pc == TH_LVL_PRE_SPRITES) pre_sprites();
    else if (pc == TH_LVL_POST_SPRITES) post_sprites();
}

/* AnimateObject: [duration, frames..., $FF loop] */
#define O_PREV_ANIM 0x3F
#define O_PREV_ANIM_S3K 0x21          /* (prev_anim: S3K's Animate_Sprite) */
static void animate(uint32_t o, const uint8_t *const *anims)
{
    uint8_t a = OB8(o, OBJ_anim);
    if (a != OB8(o, O_PREV_ANIM)) {
        SET8(o, O_PREV_ANIM, a);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
    if (t >= 0) { SET8(o, OBJ_anim_timer, t); return; }
    const uint8_t *s = anims[a];
    SET8(o, OBJ_anim_timer, s[0]);
    unsigned i = OB8(o, OBJ_anim_frame);
    uint8_t f = s[1 + i];
    if (f == 0xFF) { i = 0; f = s[1]; }
    SET8(o, OBJ_frame, f);
    SET8(o, OBJ_anim_frame, i + 1);
}

/* the ramp's 3D: how far along its 256 px chunk player 1 is (0-$BF) */
static unsigned chunk_pos(unsigned x, int flip)
{
    unsigned d0 = x & 0xFF;
    if (flip) d0 = 0xFF - d0;
    if (d0 >= 0xC0) d0 = d0 >= 0xF0 ? 0 : 0xBF;
    return d0;
}

/* ---- $2A booster (Obj3DRamp) -------------------------------------------- */
#define B_BASE   0x30                  /* oVar2A: where it stands */
#define B_TIMER  0x32                  /* oVar2E: boosting */
static const uint8_t b_ani0[] = { 0, 4, 4, 3, 3, 3, 5, 5, 2, 2, 2, 0xFF };
static const uint8_t b_ani1[] = { 2, 4, 7, 5, 6, 0xFF };
static const uint8_t b_ani2[] = { 2, 8, 0, 9, 0, 0xFF };
static const uint8_t *const b_anims[] = { b_ani0, b_ani1, b_ani2 };

/* Obj3DFall (subtype2: bit 7 here): running back left at the top of the
 * second ramp, the player stops and falls away into it */
static void fall_away(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
    }
    if ((uint16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos) + 0x40) < 0x80 &&
        (uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos) + 0x20) < 0x40 &&
        OBS16(p, OBJ_x_vel) < 0 && OB16(o, OBJ_x_pos) >= OB16(p, OBJ_x_pos)) {
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
        SET8(p, OBJ_anim, 2);                                 /* (Sonic CD's fall-away pose) */
        SET8(o, OBJ_y_radius, 0x0E);                          /* (the original sets these on itself) */
        SET8(o, OBJ_x_radius, 7);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 5);
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

static void booster(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { fall_away(o); return; }
    uint32_t p1 = RAM_Player_1;
    int flip = OB8(o, OBJ_subtype) != 0;
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BOOSTER, 4 | (flip ? 1 : 0), 1, 0x20, 0x20);
        SET16(o, B_BASE, OB16(o, OBJ_x_pos));
        if (flip) SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_XFLIP);
    }
    int timer = OB8(o, B_TIMER), ramp = s_ramp[0] && ppz1(), d1 = 0;
    if (timer) {
        SET8(o, OBJ_anim, ramp ? 1 : 2);
        animate(o, b_anims);
    } else
        SET8(o, OBJ_frame, 0);
    if (timer || ramp) {
        unsigned d0 = chunk_pos(OB16(p1, OBJ_x_pos), flip);
        if (!timer) SET8(o, OBJ_frame, d0 / 0x30);
        d1 = (int)(d0 >> 2) + (int)(d0 >> 3);
        if (flip) d1 = -d1;
    }
    SET16(o, OBJ_x_pos, OB16(o, B_BASE) + d1);
    if (timer) SET8(o, B_TIMER, timer - 1);
    for (int n = 0; n < 2; n++) {                             /* (Sonic CD: Sonic alone) */
        uint32_t p = o_player(n);
        if (!playing(p) || (!timer && (OB8(p, OBJ_status) & ST_AIR))) continue;
        int dx = OBS16(p, OBJ_x_pos) - OBS16(o, OBJ_x_pos) + 0x20;
        int dy = OBS16(p, OBJ_y_pos) - OBS16(o, OBJ_y_pos) + 0x20;
        if (dx < 0 || dx >= 0x40 || dy < 0 || dy >= 0x40) continue;
        if (!OB8(o, B_TIMER)) SET8(o, B_TIMER, 60);
        SET16(p, OBJ_y_vel, OBS16(p, OBJ_y_vel) < 0 ? -0xC00 : 0xC00);
    }
    if (o_out_of_range(o, OB16(o, B_BASE))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ---- $2C plants (Obj3DPlant): stars that slide with the ramp's 3D ------- */
#define P_BASE   0x30                  /* oVar2A: the group's place */
#define P_HOME   0x32                  /* oVar2C: its own */
static void plants(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        static const int16_t big[] = { 0x40, 0x80, -0x40, -0x80 }, small[] = { 0, 0x60, -0x60 };
        uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
        int n = OB8(o, OBJ_subtype) & 1 ? 2 : 4;
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & ~1u);
        for (int i = 0; i < n + 3; i++) {
            uint32_t a1 = o;
            if (i) {
                if (!(a1 = o_alloc())) break;
                SET32(a1, OBJ_code, r32(o + OBJ_code));
            }
            int sm = i >= n;
            SET8(a1, OBJ_routine, 2);
            o_init(a1, SCDA_PLANT, OB8(a1, OBJ_render) | 4, sm ? 4 : 0, sm ? 0x0C : 0x18, sm ? 0x0C : 0x14);
            SET16(a1, OBJ_x_pos, (uint16_t)(x + (sm ? small[i - n] : big[i])));
            SET16(a1, OBJ_y_pos, y);
            SET16(a1, P_BASE, x);
            SET16(a1, P_HOME, OB16(a1, OBJ_x_pos));
            if (sm) { SET8(a1, OBJ_subtype, 1); SET8(a1, OBJ_frame, 1); }
        }
    }
    if (!OB8(o, OBJ_subtype)) {
        int d0 = 0;
        if (s_ramp[0] && ppz1()) {
            uint16_t px = OB16(RAM_Player_1, OBJ_x_pos);
            int left = px < OB16(o, P_BASE);
            d0 = (int)(chunk_pos(px, left) >> 1);
            if (left) d0 = -d0;
        }
        SET16(o, OBJ_x_pos, OB16(o, P_HOME) + d0);
    }
    if (o_out_of_range(o, OB16(o, P_BASE))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ---- badniks ------------------------------------------------------------ */
static void badnik_init(uint32_t o, int asset, int prio, int xr, int yr, uint8_t coll)
{
    o_init(o, asset, OB8(o, OBJ_render) | 4, prio, xr, yr);
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_coll_flags, tri_scd_coll(coll));
}

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

static void badnik_end(uint32_t o, uint16_t home)
{
    if (cd_despawn(o, home)) return;
    o_touch(o);
    o_draw(o);
}

static int far_from(uint32_t o, uint16_t home)
{
    int16_t d = (int16_t)(OB16(o, OBJ_x_pos) - home);
    return (d < 0 ? -d : d) >= 0x80;
}

/* $3E Pata-Bata: a butterfly, to and fro in waves */
#define PB_X0    0x30                  /* oVar2A */
#define PB_Y0    0x32                  /* oVar2C (long) */
#define PB_SPD   0x36                  /* oVar30 */
#define PB_ANG   0x3A                  /* oVar34 */
#define PB_ASPD  0x3C                  /* oVar36 */
#define PB_SHIFT 0x3E                  /* oVar38 */
static void pata_bata(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_PATABATA2 : SCDA_PATABATA;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x10, 0x10, 0x2A);
        SET16(o, PB_X0, OB16(o, OBJ_x_pos));
        SET32(o, PB_Y0, (uint32_t)OB16(o, OBJ_y_pos) << 16 | 0x8000);
        SET32(o, PB_SPD, dmg ? -0x4000 : -0x8000);
        SET16(o, PB_ASPD, dmg ? -0x100 : -0x200);
        SET16(o, PB_SHIFT, dmg ? 4 : 3);
        SET8(o, OBJ_anim, dmg);
    }
    int32_t spd = (int32_t)r32(o + PB_SPD);
    cd_move(o, spd, 0);
    if (far_from(o, OB16(o, PB_X0))) {
        SET32(o, PB_SPD, (uint32_t)(spd = -spd));
        cd_move(o, spd, 0);
        turn(o);
        SET16(o, PB_ANG, 0);
    }
    uint16_t ang = (uint16_t)(OB16(o, PB_ANG) + OB16(o, PB_ASPD));
    SET16(o, PB_ANG, ang);
    int16_t s, c;
    o_sincos((uint8_t)(ang >> 8), &s, &c);
    /* (swap d0: the low word is what d0 held above, the speed's high word) */
    int32_t v = (int32_t)((uint32_t)(uint16_t)s << 16 | (uint16_t)((uint32_t)spd >> 16)) >> OB16(o, PB_SHIFT);
    SET32(o, OBJ_y_pos, r32(o + PB_Y0) + (uint32_t)v);
    o_animate(o, o_asset_anim(asset));
    badnik_end(o, OB16(o, PB_X0));
}

/* $3D Mosqui: a mosquito that dives on the player and sticks in the ground */
#define MQ_X0    0x30                  /* oVar2A */
#define MQ_SPD   0x32                  /* oVar2C */
static void mosqui(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_MOSQUI2 : SCDA_MOSQUI;
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x10, 0x10, 0x2B);
        SET16(o, MQ_X0, OB16(o, OBJ_x_pos));
        SET32(o, MQ_SPD, dmg ? -0x8000 : -0x10000);
        /* fall through */
    case 2:
        if (!r16(RAM_Debug_placement_mode) &&
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x30) < 0x70 &&
            (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x30) < 0x60) {
            SET8(o, OBJ_routine, 4);                          /* (in range: dive) */
            SET8(o, OBJ_anim, 1);
            break;
        }
        for (int k = 0; k < 2; k++) {
            int32_t spd = (int32_t)r32(o + MQ_SPD);
            cd_move(o, spd, 0);
            if (!far_from(o, OB16(o, MQ_X0))) break;
            SET32(o, MQ_SPD, (uint32_t)-spd);
            turn(o);
        }
        o_animate(o, o_asset_anim(asset));
        break;
    case 4:                                                   /* (its script ends $FC: routine 6) */
        o_animate(o, o_asset_anim(asset));
        break;
    case 6: {
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 6);
        int16_t d1 = o_floor_dist(o);
        if (d1 > -8) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 + 8);
        SET8(o, OBJ_routine, 8);
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(0xA7);        /* (ObjMosqui_Dive: it lands) */
        break;
    }
    default:
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete_respawnable(o); return; }
        break;
    }
    badnik_end(o, OB16(o, MQ_X0));
}

/* $3F Anton: an ant on a wheel, along the ground */
#define AT_SPD   0x30                  /* oVar2A */
#define AT_X0    0x34                  /* oVar2E */
static void anton(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* (init runs until it is down) */
        badnik_init(o, SCDA_ANTON, 4, 0x18, 0x13, 0x29);
        SET8(o, OBJ_x_radius, 0x18);
        SET16(o, AT_X0, OB16(o, OBJ_x_pos));
        SET32(o, AT_SPD, OB8(o, OBJ_subtype) ? -0x8000 : -0x10000);
        SET8(o, OBJ_anim, OB8(o, OBJ_subtype) ? 1 : 0);
        /* fall through */
    case 2:
        cd_move(o, 0, 0x10000);
        if (o_floor_dist(o) < 0) SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        break;
    default:
        for (int k = 0; k < 2; k++) {
            int32_t spd = (int32_t)r32(o + AT_SPD);
            cd_move(o, spd, 0);
            if (!far_from(o, OB16(o, AT_X0))) {
                int16_t d1 = o_floor_dist(o);
                if (d1 >= -7 && d1 <= 7) {
                    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                    o_animate(o, o_asset_anim(SCDA_ANTON));
                    break;
                }
            }
            SET32(o, AT_SPD, (uint32_t)-spd);                 /* (turn and go again) */
            turn(o);
        }
        break;
    }
    badnik_end(o, OB16(o, AT_X0));
}

/* $22 Tamabboh: walks, and throws two bombs over itself; subtype 1: a bomb */
#define TB_X0    0x30                  /* oVar2A */
#define TB_SPD   0x32                  /* oVar2C */
#define TB_REST  0x36                  /* oVar34 */
#define MS_VX    0x30                  /* oVar2A */
#define MS_VY    0x34                  /* oVar2E */
#define MS_AX    0x38                  /* oVar32 */
#define MS_AY    0x3C                  /* oVar36 */
#define MS_LEFT  0x40                  /* oVar3F */
static void tamabboh_bomb(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        uint16_t tile = OB16(o, OBJ_art_tile);
        SET32(o, OBJ_mappings, o_asset_maps(SCDA_TAMAMISSILE));
        SET16(o, OBJ_art_tile, tile);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xAD));
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_width_px, 8);
        SET8(o, OBJ_height_px, 8);
        SET8(o, OBJ_y_radius, 8);
        SET32(o, MS_AX, 0);
        SET32(o, MS_AY, 0x2000);
        SET32(o, MS_VX, OB8(o, MS_LEFT) ? -0x20000 : 0x20000);
        SET32(o, MS_VY, -0x40000);
    } else {
        if (!(OB8(o, OBJ_render) & 0x80) || o_floor_dist(o) < 0) { o_delete(o); return; }
        cd_move(o, (int32_t)r32(o + MS_VX), (int32_t)r32(o + MS_VY));
        SET32(o, MS_VX, r32(o + MS_VX) + r32(o + MS_AX));
        SET32(o, MS_VY, r32(o + MS_VY) + r32(o + MS_AY));
        o_animate(o, o_asset_anim(SCDA_TAMAMISSILE));
    }
    o_touch(o);
    o_draw(o);
}

static void tamabboh(uint32_t o)
{
    if (OB8(o, OBJ_subtype) == 1) { tamabboh_bomb(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_TAMABBOH2 : SCDA_TAMABBOH;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 4, 0x10, 0x0F, 0x2C);
        SET16(o, TB_X0, OB16(o, OBJ_x_pos));
        SET32(o, TB_SPD, dmg ? -0x5000 : -0xA000);
        /* fall through */
    case 2:
        cd_move(o, 0, 0x10000);
        if (o_floor_dist(o) < 0) SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        break;
    case 4: {
        if (!r16(RAM_Debug_placement_mode) && !OB8(o, OBJ_subtype)) {
            if (OB16(o, TB_REST)) SET16(o, TB_REST, OB16(o, TB_REST) - 1);
            else {
                uint32_t p = RAM_Player_1;
                if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x50) < 0xA0 &&
                    (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x50) < 0xA0) {
                    SET8(o, OBJ_routine, 6);
                    break;
                }
            }
        }
        for (int k = 0; k < 2; k++) {
            int32_t spd = (int32_t)r32(o + TB_SPD);
            cd_move(o, spd, 0);
            if (!far_from(o, OB16(o, TB_X0))) {
                int16_t d1 = o_floor_dist(o);
                if (d1 >= -7 && d1 <= 7) {
                    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                    o_animate(o, o_asset_anim(asset));
                    break;
                }
            }
            SET32(o, TB_SPD, (uint32_t)-spd);
            turn(o);
        }
        break;
    }
    case 6:
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_anim, 1);
        /* fall through */
    case 8:                                                   /* (its script ends $FC: routine 10) */
        o_animate(o, o_asset_anim(asset));
        break;
    default:                                                  /* fire */
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_anim, 0);
        SET16(o, TB_REST, 0x78);
        if (OB8(o, OBJ_subtype)) break;
        for (int k = 0; k < 2; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            if (!k && (OB8(o, OBJ_render) & 0x80)) o_sfx_cd(0xA0);   /* (ObjTamabboh_Fire) */
            SET32(a1, OBJ_code, r32(o + OBJ_code));
            SET8(a1, OBJ_subtype, 1);
            SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
            SET16(a1, OBJ_priority, (uint16_t)(OB16(o, OBJ_priority) + 0x80));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) - 10));
            SET8(a1, MS_LEFT, k ? 0xFF : 0);
        }
        break;
    }
    badnik_end(o, OB16(o, TB_X0));
}

/* ---- $21 platform: swings, sinks a little when stood on, falls, rises to
 * the ceiling, slides (subtype high nibble), 32/64/96 px (low bits) ------ */
#define PL_ANG   0x30                  /* oVar2A */
#define PL_STATE 0x31                  /* oVar2B */
#define PL_DROP  0x32                  /* oVar2C */
#define PL_RANGE 0x33                  /* oVar2D */
#define PL_TIMER 0x34                  /* oVar2E */
#define PL_OLDX  0x36                  /* (where it was: carries who stands on it) */
#define PL_X0    0x38                  /* oVar38 */
#define PL_Y0    0x3A                  /* oVar3A */
static int16_t pl_osc(uint32_t o)                             /* ObjPlatform_DoOsc */
{
    int16_t s, c;
    o_sincos(OB8(o, PL_ANG), &s, &c);
    return (int16_t)((int16_t)(s * OB8(o, PL_RANGE)) >> 4);
}

static int pl_solid(uint32_t o)
{
    o_platform(o, OB8(o, OBJ_width_px) + 10, 8, OB16(o, PL_OLDX));
    return cd_stood_on(o);
}

static int pl_drop(uint32_t o)                                /* ObjPlatform_DropWhenStoodOn */
{
    if (pl_solid(o)) {
        if (OB8(o, PL_DROP) < 8) SET8(o, PL_DROP, OB8(o, PL_DROP) + 1);
        return 1;
    }
    if (OB8(o, PL_DROP)) SET8(o, PL_DROP, OB8(o, PL_DROP) - 1);
    return 0;
}

static void pl_sag(uint32_t o)                                /* (y: its rest, sunk by half the drop) */
{
    SET16(o, OBJ_y_pos, OB16(o, PL_Y0) + ((int8_t)OB8(o, PL_DROP) >> 1));
}

static int pl_still(uint32_t o) { pl_sag(o); return pl_drop(o); }   /* subtype 4X */

static int pl_slide(uint32_t o, int16_t dx, int step)          /* x from its place, x speed, sag */
{
    uint32_t old = r32(o + OBJ_x_pos);
    SET16(o, OBJ_x_pos, OB16(o, PL_X0) + dx);
    SET8(o, PL_ANG, OB8(o, PL_ANG) + step);
    if (step) pl_sag(o);
    SET16(o, OBJ_x_vel, (uint16_t)((int32_t)(r32(o + OBJ_x_pos) - old) >> 8));
    return pl_drop(o);
}

static void platform(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        uint8_t sub = OB8(o, OBJ_subtype);
        static const uint8_t widths[] = { 0x10, 0x20, 0x30, 0 }, ranges[] = { 2, 3, 4, 6 };
        o_init(o, SCDA_PLATFORM, OB8(o, OBJ_render) | 4, 2, widths[sub & 3], 8);
        SET8(o, OBJ_frame, sub & 3);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, PL_RANGE, ranges[(sub >> 2) & 3]);
        SET16(o, PL_X0, OB16(o, OBJ_x_pos));
        SET16(o, PL_Y0, OB16(o, OBJ_y_pos));
        o_draw(o);
        return;
    }
    SET16(o, PL_OLDX, OB16(o, OBJ_x_pos));
    uint8_t st = OB8(o, PL_STATE);
    switch (OB8(o, OBJ_subtype) >> 4) {
    case 0:                                                   /* up and down */
        SET8(o, PL_ANG, OB8(o, PL_ANG) + 1);
        SET16(o, OBJ_y_pos, OB16(o, PL_Y0) + pl_osc(o));
        pl_solid(o);
        break;
    case 1: {                                                 /* to and fro (sags) */
        uint32_t old = r32(o + OBJ_x_pos);
        SET16(o, OBJ_x_pos, OB16(o, PL_X0) + pl_osc(o));
        SET8(o, PL_ANG, OB8(o, PL_ANG) + 1);
        pl_sag(o);
        SET16(o, OBJ_x_vel, (uint16_t)((int32_t)(r32(o + OBJ_x_pos) - old) >> 8));
        pl_drop(o);
        break;
    }
    case 2: case 3: {                                         /* diagonally */
        uint32_t old = r32(o + OBJ_x_pos);
        SET8(o, PL_ANG, OB8(o, PL_ANG) + 1);
        int16_t d = pl_osc(o);
        SET16(o, OBJ_y_pos, OB16(o, PL_Y0) + d);
        SET16(o, OBJ_x_pos, OB16(o, PL_X0) + (OB8(o, OBJ_subtype) >> 4 == 3 ? -d : d));
        SET16(o, OBJ_x_vel, (uint16_t)((int32_t)(r32(o + OBJ_x_pos) - old) >> 8));
        pl_drop(o);
        break;
    }
    case 4:
        pl_still(o);
        break;
    case 5:                                                   /* falls a moment after it is stood on */
        if (!st) {
            if (!pl_still(o)) break;
            SET8(o, PL_TIMER, 30);
            SET8(o, PL_STATE, 2);
        }
        if (OB8(o, PL_TIMER)) { SET8(o, PL_TIMER, OB8(o, PL_TIMER) - 1); pl_still(o); break; }
        pl_solid(o);
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)((int32_t)OBS16(o, OBJ_y_vel) << 8));
        if (OB16(o, OBJ_y_vel) < 0x400) SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x40);
        if ((uint16_t)(r16(RAM_Camera_Y_pos) + 0x100) < OB16(o, OBJ_y_pos)) {
            cd_get_off(o);
            o_delete(o);
            return;
        }
        break;
    case 6:                                                   /* stood on: goes down a way */
        if (st == 0) { if (!pl_still(o)) break; SET8(o, PL_STATE, st = 2); }
        if (st == 2) {
            if (OB8(o, PL_ANG) < 0x40) {
                SET16(o, OBJ_y_pos, OB16(o, PL_Y0) - pl_osc(o));
                SET8(o, PL_ANG, OB8(o, PL_ANG) + 2);
                pl_solid(o);
                break;
            }
            SET16(o, PL_Y0, OB16(o, OBJ_y_pos));
            SET8(o, PL_STATE, 4);
        }
        pl_still(o);
        break;
    case 7:                                                   /* stood on: rises to the ceiling */
        if (st == 0) {
            if (!pl_still(o)) break;
            SET8(o, PL_STATE, st = 2);
            SET8(o, PL_TIMER, 0x3C);
        }
        if (st == 2) {
            if (OB8(o, PL_TIMER)) { SET8(o, PL_TIMER, OB8(o, PL_TIMER) - 1); pl_still(o); break; }
            o_move(o);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
            int16_t d1 = o_ceiling_dist(o);
            if (d1 >= 0) { pl_drop(o); break; }
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d1);
            SET16(o, PL_Y0, OB16(o, OBJ_y_pos));
            SET8(o, PL_STATE, 4);
        }
        pl_still(o);
        break;
    case 8: case 9:                                           /* stood on: slides a way */
        if (st == 0) {
            if (!pl_still(o)) break;
            SET8(o, PL_STATE, st = 2);
            SET8(o, PL_TIMER, 0x3C);
        }
        if (st == 2) {
            if (OB8(o, PL_TIMER)) { SET8(o, PL_TIMER, OB8(o, PL_TIMER) - 1); pl_still(o); break; }
            if (OB8(o, PL_ANG) < 0x40) {
                int16_t d = pl_osc(o);
                pl_slide(o, OB8(o, OBJ_subtype) >> 4 == 9 ? -d : d, 1);
                break;
            }
            SET16(o, PL_X0, OB16(o, OBJ_x_pos));
            SET8(o, PL_STATE, 4);
        }
        pl_still(o);
        break;
    default:
        break;
    }
    if (o_out_of_range(o, OB16(o, PL_X0))) {
        cd_get_off(o);
        o_delete_respawnable(o);
        return;
    }
    o_draw(o);
}

/* ---- $20 collapsing ledge: stood on, it breaks into pieces that fall ---- */
#define LG_TIMER 0x30                  /* oVar2A */
#define LG_VY    0x32                  /* oVar2C */
#define LG_SOLID 0x3E                  /* oVar3E */
static void ledge_piece(uint32_t o, int asset, int frame, int flip, int16_t x, int16_t y, int timer, int solid, int yr)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    o_init(a1, asset, 4 | (flip ? 1 : 0), 3, solid ? 8 : OB8(o, OBJ_width_px), solid ? yr : 8);
    if (flip) SET8(a1, OBJ_status, ST_XFLIP);
    SET8(a1, OBJ_frame, frame);
    SET32(a1, LG_VY, 0x20000);
    SET8(a1, OBJ_routine, 4);
    SET8(a1, OBJ_subtype, 0xFF);                              /* (a piece) */
    SET8(a1, LG_SOLID, solid ? 0xFF : 0);
    SET8(a1, OBJ_x_radius, 8);
    SET8(a1, OBJ_y_radius, solid ? yr : 8);
    SET16(a1, OBJ_x_pos, (uint16_t)x);
    SET16(a1, OBJ_y_pos, (uint16_t)y);
    SET16(a1, LG_TIMER, (uint16_t)timer);
}

static void ledge_break(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!(sub & 0x80)) {                                      /* rows of pieces, the top row solid */
        int flip = (sub & 0x10) != 0;
        uint32_t tab = o_asset_anim(SCDA_LEDGE), e = tab + tri_rd16(tab + (sub & 0x0F) * 2u);
        int cols = tri_rd8(e), rows = tri_rd8(e + 1), d0 = cols * 8, step = -0x10;
        if (!flip) { d0 = -d0; step = 0x10; }
        e += 2;
        int x0 = OBS16(o, OBJ_x_pos) + d0, y = rows * 8 + OBS16(o, OBJ_y_pos), delay = 9;
        for (int r = rows; r >= 0; r--) {
            int x = x0, t = delay;
            for (int c = cols; c >= 0; c--) {
                int8_t f = (int8_t)tri_rd8(e++);
                if (f >= 0) ledge_piece(o, SCDA_LEDGEBITS, f, flip, (int16_t)x, (int16_t)y, t, r == 0, 9);
                x += step;
                t += 0x0C;
            }
            y -= 0x10;
            delay += 5;
        }
    } else {                                                  /* a row, falling from one end */
        uint32_t tab = o_asset_anim(SCDA_LEDGE2), e = tab + tri_rd16(tab + (sub & 0x1F) * 2u);
        int n = tri_rd8(e), yr = (tri_rd8(e + 1) + 1) * 8 + 2, d4 = -(n * 8), d3 = 0x10, d6 = 1, back;
        e += 2;
        if (sub & 0x40) {
            int16_t v = OBS16(RAM_Player_1, OBJ_x_vel);
            back = (sub & 0x20 ? -v : v) < 0;
        } else
            back = (sub & 0x20) != 0;                         /* (lsl.b #2: bit 5 to the sign) */
        if (back) { e += (uint32_t)n; d4 = -d4; d3 = -d3; d6 = -1; }
        int x = d4 + OBS16(o, OBJ_x_pos), t = 9;
        for (int k = n; k >= 0; k--) {
            ledge_piece(o, SCDA_LEDGEBITS2, tri_rd8(e), 0, (int16_t)x, OBS16(o, OBJ_y_pos), t, 1, yr);
            e += (uint32_t)d6;
            x += d3;
            t += 0x0C;
        }
    }
    o_delete(o);
}

static void ledge(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint8_t sub = OB8(o, OBJ_subtype);
        int asset = sub & 0x80 ? SCDA_LEDGE2 : SCDA_LEDGE;
        uint32_t tab = o_asset_anim(asset), e = tab + tri_rd16(tab + (sub & 0x0F) * 2u);
        int w = (tri_rd8(e) + 1) * 8, h = (int8_t)tri_rd8(e + 1);
        h = ((h < 0 ? -h : h) + 1) * 8 + 2;
        o_init(o, asset, OB8(o, OBJ_render) | 4 | (sub & 0x10 ? 1 : 0), 3, w, h);
        if (sub & 0x10) SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_XFLIP);
        SET8(o, OBJ_frame, sub & 0x0F);
        SET8(o, OBJ_x_radius, w);
        SET8(o, OBJ_y_radius, h);
    }   /* fall through */
    case 2:
        o_platform(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB16(o, OBJ_x_pos));
        if (cd_stood_on(o)) {
            cd_get_off(o);
            o_sfx_cd(0xA3);                                   /* (ObjCollapsePlatform_Main) */
            ledge_break(o);
            return;
        }
        break;
    case 4:                                                   /* a piece, waiting */
        SET16(o, LG_TIMER, OB16(o, LG_TIMER) - 1);
        if (!OB16(o, LG_TIMER)) SET8(o, OBJ_routine, 6);
        if (OB8(o, LG_SOLID)) {
            o_platform(o, 8 + 10, OB8(o, OBJ_y_radius), OB16(o, OBJ_x_pos));
            if (cd_stood_on(o) && !OB16(o, LG_TIMER)) cd_get_off(o);
        }
        o_draw(o);
        return;
    default:                                                  /* falling */
        cd_move(o, 0, (int32_t)r32(o + LG_VY));
        SET32(o, LG_VY, r32(o + LG_VY) + 0x4000);
        if ((int16_t)(OB16(o, OBJ_y_pos) - OB16(RAM_Player_1, OBJ_y_pos)) > 0x200) { o_delete(o); return; }
        o_draw(o);
        return;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- the spin tunnels ----------------------------------------------------- */
#define OBJ_object_ctrl  0x2E
#define OBJ_invuln       0x34          /* invulnerability_timer */
#define FM_PATH          0x91          /* (ObjTunnelPath: it takes the player, and at its start) */
#define FM_DOOR          0xA4          /* (the doors open) */
static int s_base;                     /* this round's native slots (scd_obj.c) */
static uint32_t native(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base + id) * 4u; }

static void roll_up(uint32_t p)        /* (into a ball, as Sonic CD's tunnels make one) */
{
    if (OB8(p, OBJ_status) & 4) return;
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 4);
    SET8(p, OBJ_y_radius, 0x0E);
    SET8(p, OBJ_x_radius, 7);
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + (OB8(p, 0x38) == 1 ? 1 : 5));   /* (Tails stands $F high) */
    SET8(p, OBJ_anim, 2);
}

/* $34 spin tunnel tag (unseen): rolls the player through, at speed */
void cd_spin_tag(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) SET8(o, OBJ_routine, 2);
    uint8_t sub = OB8(o, OBJ_subtype);
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!playing(p) || r16(RAM_Debug_placement_mode)) continue;
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x28) >= 0x50 ||
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x28) >= 0x50) continue;
        uint8_t held = r8(n ? RAM_Ctrl_2_held_logical : RAM_Ctrl_1_held_logical);
        int16_t vx = OBS16(p, OBJ_x_vel), vy = OBS16(p, OBJ_y_vel);
        if (sub == 0) {                                       /* along: at least $A00 */
            int16_t cap = tri_zone() >= TRI_CD_ZONE(5, 0) && tri_zone() < TRI_CD_ZONE(6, 0)
                          ? 0xD00 : 0xA00;                    /* (Stardust Speedway: $D00) */
            int16_t d0 = vx < 0 ? -vx : vx;
            if ((uint16_t)d0 < (uint16_t)cap) d0 = cap;
            if (vx < 0) d0 = -d0;
            SET16(p, OBJ_x_vel, d0);
            SET16(p, OBJ_ground_vel, d0);
            if (((OB8(p, OBJ_angle) + 0x20) & 0xC0) == 0x80) SET16(p, OBJ_ground_vel, -d0);
        } else if (sub == 1 || sub == 4) {
            int16_t d0;
            if (sub == 1) {                                   /* up or down: at least $D00 */
                d0 = vy < 0 ? -vy : vy;
                if ((uint16_t)d0 < 0xD00) d0 = 0xD00;
                if (vy < 0) d0 = -d0;
            } else {                                          /* up, held */
                if (vx < 0 || !(held & 1)) { roll_up(p); continue; }
                d0 = -0xA00;
            }
            SET16(p, OBJ_y_vel, d0);
            SET16(p, OBJ_ground_vel, d0);
            SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_AIR);
        } else if (sub == 2 ? vy < 0 : vy >= 0) {                     /* out sideways, as held */
            int16_t d0 = held & 8 ? 0xD00 : held & 4 ? -0xD00 : 0;
            if (d0) {
                if (sub != 2) SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_AIR);
                SET16(p, OBJ_x_vel, d0);
                SET16(p, OBJ_ground_vel, d0);
            }
        }
        roll_up(p);
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

/* $07 tunnel path: carries the player along a route of points (the level's
 * own table), at $1000; subtype bit 6: unseen while carried */
#define TP_STEPS 0x30                  /* oVar2E: frames (high byte) */
#define TP_X     0x36                  /* oVar36 */
#define TP_Y     0x38                  /* oVar38 */
#define TP_INDEX 0x3A                  /* oVar3A */
#define TP_END   0x3B                  /* oVar3B */
#define TP_DATA  0x3C                  /* oVar3C */
static void tp_aim(uint32_t o, uint32_t p)                    /* ObjTunnelPath_SetPlayerSpeed */
{
    int16_t dx = (int16_t)(OB16(o, TP_X) - OB16(p, OBJ_x_pos));
    int16_t dy = (int16_t)(OB16(o, TP_Y) - OB16(p, OBJ_y_pos));
    int16_t d2 = OBS16(p, OBJ_ground_vel), d3 = d2;
    uint16_t ax = (uint16_t)dx, ay = (uint16_t)dy;
    if (dx < 0) { ax = (uint16_t)-dx; d2 = -d2; }
    if (dy < 0) { ay = (uint16_t)-dy; d3 = -d3; }
    if (ay >= ax) {
        int16_t d1 = (int16_t)(((int32_t)dy << 16) / d3), d0 = 0;
        if (dx && d1) d0 = (int16_t)(((int32_t)dx << 16) / d1);
        SET16(p, OBJ_x_vel, (uint16_t)d0);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
        SET16(o, TP_STEPS, (uint16_t)(d1 < 0 ? -d1 : d1));
    } else {
        int16_t d0 = (int16_t)(((int32_t)dx << 16) / d2), d1 = 0;
        if (dy && d0) d1 = (int16_t)(((int32_t)dy << 16) / d0);
        SET16(p, OBJ_y_vel, (uint16_t)d1);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
        SET16(o, TP_STEPS, (uint16_t)(d0 < 0 ? -d0 : d0));
    }
}

static void tunnel_path(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (OB8(p, OBJ_routine) >= 6) return;                     /* (dead) */
    int hide = OB8(o, OBJ_subtype) & 0x40;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint32_t tab = o_asset_anim(SCDA_TUNNELPATHS);
        if (!tab) { o_delete(o); return; }
        uint32_t a2 = tab + tri_rd16(tab + (OB8(o, OBJ_subtype) & 0x3F) * 2u);
        SET8(o, TP_INDEX, tri_rd8(a2));
        SET8(o, TP_END, tri_rd8(a2 + 1));
        SET32(o, TP_DATA, a2 + 2);
        SET16(o, TP_X, tri_rd16(a2 + 2));
        SET16(o, TP_Y, tri_rd16(a2 + 4));
    }   /* fall through */
    case 2:
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 16) >= 32 ||
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 16) >= 32 || OB8(p, OBJ_object_ctrl))
            break;
        if (OB8(p, OBJ_routine) == 4) {                       /* (hurt: back to normal) */
            SET8(p, OBJ_routine, 2);
            SET8(p, OBJ_invuln, 0x78);
        }
        SET8(o, OBJ_routine, 4);
        SET8(p, OBJ_object_ctrl, 0x81);
        SET8(p, OBJ_anim, 2);
        SET16(p, OBJ_ground_vel, 0x1000);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~0x20);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | ST_AIR);
        SET8(p, 0x40, 0);                                     /* (jumping) */
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        o_sfx_cd(FM_PATH);
        break;
    case 4:
        tp_aim(o, p);
        SET8(o, OBJ_routine, 6);
        o_sfx_cd(FM_PATH);
        if (hide) SET8(p, OBJ_invuln, 3);                     /* (Sonic_Display skips him) */
        return;
    default: {
        if (hide) SET8(p, OBJ_invuln, 3);
        int8_t t = (int8_t)(OB8(o, TP_STEPS) - 1);
        SET8(o, TP_STEPS, (uint8_t)t);
        if (t >= 0) {
            SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_x_vel) << 8));
            SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_y_vel) << 8));
            return;
        }
        SET16(p, OBJ_x_pos, OB16(o, TP_X));
        SET16(p, OBJ_y_pos, OB16(o, TP_Y));
        uint8_t d1 = (uint8_t)(OB8(o, TP_INDEX) + 4);
        if (d1 >= OB8(o, TP_END)) {                           /* out */
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
            SET8(o, OBJ_routine, 0);
            SET8(p, OBJ_object_ctrl, 0);
            if (hide) SET8(p, OBJ_invuln, 0);
            return;
        }
        SET8(o, TP_INDEX, d1);
        uint32_t a2 = r32(o + TP_DATA);
        SET16(o, TP_X, tri_rd16(a2 + d1));
        SET16(o, TP_Y, tri_rd16(a2 + d1 + 2));
        tp_aim(o, p);
        return;
    }
    }
    if (OB8(o, OBJ_routine) < 4 && o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

/* $0D spin tunnel door: a flap, solid on top, that a ball rising into it
 * knocks open (subtype 0: with a splash) */
static void tunnel_door(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TUNNELDOOR, OB8(o, OBJ_render) | 4, 1, OB8(o, OBJ_subtype) == 2 ? 0x18 : 0x2C, 8);
        SET8(o, OBJ_y_radius, 8);
        /* fall through */
    case 2: {
        int open = 0;
        for (int n = 0; n < 2 && !open; n++) {
            uint32_t p = o_player(n);
            if (!o_player_active(p) || OBS16(p, OBJ_y_vel) >= 0) continue;
            int w = OB8(o, OBJ_width_px), h = OB8(o, OBJ_y_radius);
            int dx = OBS16(p, OBJ_x_pos) - OBS16(o, OBJ_x_pos) + w, dy = OBS16(p, OBJ_y_pos) - OBS16(o, OBJ_y_pos) + h;
            open = dx >= 0 && dx < 2 * w && dy >= 0 && dy < 2 * h;
        }
        if (!open) {
            o_platform(o, OB8(o, OBJ_width_px) + 10, 8, OB16(o, OBJ_x_pos));
            break;
        }
        SET8(o, OBJ_routine, 4);
        if (OB8(o, OBJ_subtype) || !o_asset_maps(SCDA_DOORSPLASH)) break;
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, native(0x0B));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) - 4));
        o_sfx_cd(FM_DOOR);
        break;
    }
    case 4:                                                   /* (its script ends $FC: routine 6) */
        o_animate(o, o_asset_anim(SCDA_TUNNELDOOR));
        break;
    default:
        SET8(o, 0x21, 1);                                     /* (prev_anim: the script restarts) */
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_routine, 2);
        break;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* $0B the door's splash (its sound: FM $A2, subtypes 2+ $A1) */
static void door_splash(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_DOORSPLASH, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_anim, OB8(o, OBJ_subtype));
        o_sfx_cd(OB8(o, OBJ_subtype) < 2 ? 0xA2 : 0xA1);     /* (ObjTunnelDoorSplash_Init) */
    } else if (OB8(o, OBJ_routine) >= 4) {
        o_delete(o);
        return;
    }
    o_animate(o, o_asset_anim(SCDA_DOORSPLASH));
    o_draw(o);
}

/* $35 vertical spin tunnel door: solid, it opens to a player just left of it */
#define DV_TIMER 0x3A                  /* oVar3A */
static void door_v(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_DOORV, OB8(o, OBJ_render) | 4, 1, 4, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        /* fall through */
    case 2: {
        int16_t dy = (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
        uint16_t dx = (uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
        if ((dy < 0 ? -dy : dy) < 0x20 && OB16(o, OBJ_x_pos) >= OB16(p, OBJ_x_pos) && dx < 0x30) {
            SET8(o, DV_TIMER, 0);
            SET8(o, OBJ_routine, 4);
            if (OB8(o, OBJ_render) & 0x80) { o_sfx_cd(FM_DOOR); SET8(o, OBJ_frame, 1); }
        }
        o_solid(o, 4 + 10, 0x18, 0x19, OB16(o, OBJ_x_pos));
        break;
    }
    default: {
        unsigned t = OB8(o, DV_TIMER) + 8u;
        SET8(o, DV_TIMER, (uint8_t)t);
        if (t > 0xFF) { SET8(o, OBJ_routine, 2); SET8(o, OBJ_frame, 0); }
        break;
    }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* $31 Sonic hole: the player-shaped hole a tunnel ride leaves in the wall
 * (remembered: respawn bit 6) */
static void sonic_hole(uint32_t o)
{
    uint32_t p = RAM_Player_1, mark = 0xFF0000u | OB16(o, OBJ_respawn);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SONICHOLE, OB8(o, OBJ_render) | 4, 6, 0x10, 0x10);
        if (OB16(o, OBJ_respawn) && (r8(mark) & 0x40)) { SET8(o, OBJ_routine, 4); break; }
        /* fall through */
    case 2:
        if (!OB8(p, OBJ_object_ctrl) ||
            (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x20) >= 0x40 ||
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x20) >= 0x40) {
            if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
            return;
        }
        if (OB16(o, OBJ_respawn)) w8(mark, r8(mark) | 0x40);
        o_sfx_cd(0xA3);                                       /* (ObjSonicHole) */
        SET8(o, OBJ_routine, 4);
        break;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $09 spinning disc: the player stood on it is spun round its middle --- */
#define SD_ANG   0x30                  /* (the player's oPlayerRotAngle) */
#define SD_DIST  0x31                  /* (oPlayerRotDist) */
static int8_t s_spin[2] = { -1, -1 };  /* the turn the disc shows player 1 in (0-5), or -1 */
static uint32_t s_spin_disc;

int scd_wwz_spin_frame(int n);          /* scd_wwz_plat.c: Wacky Workbench's spinning platform */
int scd_spin_frame(int n)               /* (trilogy_origins.c: Sonic CD's "Spinning Top" pictures) */
{
    int t = scd_wwz_spin_frame(n);
    if (t >= 0) return t;
    return n == 0 && s_spin_disc && (OB8(RAM_Player_1, OBJ_object_ctrl) & 1) ? s_spin[0] : -1;
}

static void spin_disc(uint32_t o)
{
    static const uint8_t turns[] = { 0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5 };
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SPINDISC, 4, 4, 0x10, 8);
        SET8(o, OBJ_y_radius, 8);
    } else if (OB8(o, OBJ_render) & 0x80) {
        o_solid(o, 0x10 + 10, 8, 9, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) {
            if (!(OB8(p, OBJ_object_ctrl) & 1)) {             /* (on: from where the player stands) */
                SET8(p, OBJ_object_ctrl, OB8(p, OBJ_object_ctrl) | 1);
                SET8(p, OBJ_anim, 0x0F);                      /* (S3K's twirl) */
                int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
                SET8(o, SD_ANG, dx < 0 ? 0x80 : 0);
                SET8(o, SD_DIST, (uint8_t)(dx < 0 ? -dx : dx));
                s_spin_disc = o;
            }
            if (OB8(p, OBJ_routine) < 6 && s_spin_disc == o) {
                uint8_t a = (uint8_t)(OB8(o, SD_ANG) + 8);
                SET8(o, SD_ANG, a);
                int16_t s, c;
                o_sincos(a, &s, &c);
                SET16(p, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + (int16_t)(((uint32_t)OB8(o, SD_DIST) * (uint32_t)(int32_t)c) >> 8)));
                s_spin[0] = (int8_t)turns[a >> 4];
                if (!(a & 0x3F)) SET8(o, SD_DIST, OB8(o, SD_DIST) + 1);
                uint8_t held = r8(RAM_Ctrl_1_held_logical), tap = r8(RAM_Ctrl_1_held_logical + 1);
                int right = OB16(p, OBJ_x_pos) >= OB16(o, OBJ_x_pos);
                if (held & (right ? 8 : 4)) SET8(o, SD_DIST, OB8(o, SD_DIST) + 1);     /* (out) */
                else if (held & (right ? 4 : 8) && OB8(o, SD_DIST)) SET8(o, SD_DIST, OB8(o, SD_DIST) - 1);
                if (tap & 0x70) {                             /* jump off */
                    SET8(p, OBJ_object_ctrl, 0);
                    s_spin_disc = 0;
                    o_sincos((uint8_t)(OB8(p, OBJ_angle) - 0x40), &s, &c);
                    SET16(p, OBJ_x_vel, (uint16_t)(OBS16(p, OBJ_x_vel) + ((c * 0x680) >> 8)));
                    SET16(p, OBJ_y_vel, (uint16_t)(OBS16(p, OBJ_y_vel) + ((s * 0x680) >> 8)));
                    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~0x20);
                    SET8(p, 0x40, 1);                         /* (jumping) */
                    o_sfx_cd(0xA0);                           /* (not the jump's: FM $A0) */
                    roll_up(p);
                }
            }
        }
    }
    if (s_spin_disc == o && !(OB8(p, OBJ_object_ctrl) & 1)) s_spin_disc = 0;
    o_animate(o, o_asset_anim(SCDA_SPINDISC));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        if (s_spin_disc == o) { SET8(p, OBJ_object_ctrl, 0); s_spin_disc = 0; }
        return;
    }
    o_draw(o);
}

/* ---- $33 log shadow (scenery) -------------------------------------------- */
static void log_shadow(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_LOGSHADOW, OB8(o, OBJ_render) | 4, 6, 0x10, 0x10);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $36 breakable wall: a ball breaks it, into six falling pieces ------- */
#define BW_VX    0x30                  /* oVar2A */
#define BW_VY    0x34                  /* oVar2E */
static void wall(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_WALL, OB8(o, OBJ_render) | 4, 1, 0x10, 0x18);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x18);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
    }
    if (OB8(o, OBJ_routine) == 2) {
        int hit = o_s1_touch(o, 0x16, 0x1A);                  /* (CD's touch size $2F) */
        uint32_t p = 0;
        for (int n = 0; n < 2 && !p; n++)
            if ((hit >> n & 1) && OB8(o_player(n), OBJ_anim) == 2) p = o_player(n);
        if (!p) {
            o_solid(o, 0x10 + 10, 0x18, 0x19, OB16(o, OBJ_x_pos));
            if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
            o_draw(o);
            return;
        }
        o_sfx_cd(0xB0);                                       /* (ObjBreakableWall .BreakUp) */
        SET16(p, OBJ_x_vel, (uint16_t)(OBS16(p, OBJ_x_vel) >> 1));
        int left = OBS16(p, OBJ_x_vel) < 0;
        uint32_t tab = o_asset_anim(SCDA_WALL);
        uint32_t frames = tab + OB8(o, OBJ_subtype) * 8u, deltas = tab + 0x40, speeds = tab + 0x58;
        uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
        for (int k = 0; k < 6; k++) {
            uint32_t a1 = k ? o_alloc() : o;
            if (!a1) break;
            if (k) {
                SET32(a1, OBJ_code, r32(o + OBJ_code));
                SET8(a1, OBJ_render, OB8(o, OBJ_render));
                SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
                SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
                SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
                SET16(a1, OBJ_x_pos, x);
                SET16(a1, OBJ_y_pos, y);
            }
            SET8(a1, OBJ_routine, 4);
            SET8(a1, OBJ_x_radius, 8);
            SET8(a1, OBJ_width_px, 8);
            SET8(a1, OBJ_height_px, 8);
            SET8(a1, OBJ_y_radius, 8);
            SET8(a1, OBJ_frame, tri_rd8(frames + (uint32_t)k));
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + tri_rd16(deltas + k * 4u));
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + tri_rd16(deltas + k * 4u + 2));
            int32_t vx = (int32_t)tri_rd32(speeds + k * 8u);
            SET32(a1, BW_VY, tri_rd32(speeds + k * 8u + 4));
            SET32(a1, BW_VX, (uint32_t)(left ? -vx : vx));
        }
    }
    SET32(o, BW_VY, r32(o + BW_VY) + 0x4000);                 /* falling */
    cd_move(o, (int32_t)r32(o + BW_VX), (int32_t)r32(o + BW_VY));
    if ((int16_t)(OB16(RAM_Player_1, OBJ_y_pos) - OB16(o, OBJ_y_pos)) <= -0xE0) { o_delete(o); return; }
    o_draw(o);
}

/* ---- $2D robot generator: in the past a ball breaks it, and the act's
 * future is good from then (scd_time.c); elsewhere it only stands ---------- */
#define RG_COUNT 0x30                  /* oVar2A (its high byte counts the explosions) */
#define RG_LOCS  0x32                  /* oVar2C: the next explosion (frame, x, y) */
#define RG_Y0    0x36                  /* oVar30 */
#define RG_DIR   0x38                  /* oVar32 */
void scd_good_future_set(void);
int scd_good_future(void);

void cd_generator(uint32_t o)
{
    int past = cd_period() == TRI_CD_PAST, good = scd_good_future();
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (!o_asset_maps(SCDA_GENERATOR)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_GENERATOR, OB8(o, OBJ_render) | 4, 4, 0x22, 0x20);
        SET8(o, OBJ_x_radius, 0x22);
        SET8(o, OBJ_y_radius, 0x20);
        SET32(o, RG_LOCS, o_asset_anim(SCDA_GENBOOM));
        SET16(o, RG_Y0, OB16(o, OBJ_y_pos));
        SET16(o, RG_COUNT, 4);
        SET16(o, RG_DIR, 1);
        SET8(o, OBJ_frame, (good ? 0 : 2) + (past ? 1 : 0));
        if (!good && past) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x10);
        /* fall through */
    case 2: {
        if (good || !past) break;
        SET16(o, RG_COUNT, OB16(o, RG_COUNT) + 1);            /* (floating) */
        if (!(OB16(o, RG_COUNT) & 7)) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + OB16(o, RG_DIR));
        if (!(OB16(o, RG_COUNT) & 0x1F)) SET16(o, RG_DIR, (uint16_t)-OBS16(o, RG_DIR));
        int hit = o_s1_touch(o, 0x28, 0x24), broken = 0;      /* (CD's touch size $3A) */
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), OBJ_anim) == 2) broken = 1;
        o_solid(o, 0x22 + 10, 0x20, 0x21, OB16(o, OBJ_x_pos));
        if (!broken) { o_animate(o, o_asset_anim(SCDA_GENERATOR)); break; }
        SET16(o, RG_COUNT, 0);
        SET8(o, OBJ_frame, 7);
        SET8(o, OBJ_routine, 4);
        scd_good_future_set();
        D(0) = 150;                                           /* (1500 points) */
        tri_jsr(S3K_HUD_AddToScore);
        cd_get_off(o);
        break;
    }
    case 4: {                                                 /* exploding */
        uint32_t a6 = r32(o + RG_LOCS);
        uint8_t when = tri_rd8(a6);
        if (when & 0x80) { SET8(o, OBJ_routine, 6); SET8(o, RG_COUNT, 8); break; }
        SET8(o, RG_COUNT, OB8(o, RG_COUNT) + 1);
        if (OB8(o, RG_COUNT) != when) break;
        int8_t dx = (int8_t)tri_rd8(a6 + 1), dy = (int8_t)tri_rd8(a6 + 2);
        SET32(o, RG_LOCS, a6 + 3);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);                             /* (no animal) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
        o_sfx_cd(0x9E);                                       /* (FM_EXPLODE) */
        break;
    }
    default:                                                  /* broken down: as a good future's */
        SET8(o, RG_COUNT, OB8(o, RG_COUNT) - 1);
        if (OB8(o, RG_COUNT)) break;
        SET8(o, OBJ_routine, 0);
        SET16(o, OBJ_y_pos, OB16(o, RG_Y0));
        o_sfx_cd(0xD9);                                       /* (ObjRobotGenerator_BreakDown: its jingle) */
        break;
    }
    if (OB8(o, OBJ_routine) <= 2 && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $24 animals: free in the past and present once the projector is gone,
 * and in a good future; subtype bit 7: one of the projector's holograms,
 * subtype (low bits) 0 flying, else on the ground. Wacky Workbench's
 * ($29, its projector $2F; scd_wwz.c) differ a little: their ground animals
 * turn at every other landing, its holograms elsewhere --------------------- */
#define AN_X0    0x30                  /* oVar2A */
#define AN_Y0    0x32                  /* oVar2C (flying) */
#define AN_VX    0x34                  /* oVar2C (ground, long) */
#define AN_ANG   0x38                  /* oVar2E */
#define AN_STEP  0x39                  /* oVar2F */
#define AN_VY    0x3A                  /* oVar30 (ground, long) */
#define AN_PARENT 0x3E                 /* oVar3E: the projector */
#define PJ_GONE  0x3F                  /* oVar3F: the projector, destroyed */
int scd_proj_destroyed(void);
void scd_proj_destroyed_set(void);

static int wwz(void) { return tri_cd_zone(tri_zone()) && ((tri_zone() - TRI_CD_BASE) >> 2) == 4; }
static uint32_t native_here(int id)     /* (this round's: Palmtree Panic's, or Wacky Workbench's) */
{
    return NATIVE_CD_BASE + (uint32_t)(0x20 + ((tri_zone() - TRI_CD_BASE) >> 2) * 0x80 + id) * 4u;
}
#define PROJ_ID   (wwz() ? 0x2F : 0x2E)
#define ANIMAL_ID (wwz() ? 0x29 : 0x24)

static int holo_parent_gone(uint32_t o)
{
    uint32_t pj = 0xFF0000u | OB16(o, AN_PARENT);
    return r32(pj) != native_here(PROJ_ID) || OB8(pj, PJ_GONE);
}

void cd_animal(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    int holo = sub & 0x80, flying = !(sub & 0x7F);
    if (!holo) {                                              /* CheckAnimalPrescence */
        if (cd_period() == TRI_CD_PAST || cd_period() == TRI_CD_PRESENT) {
            if (!scd_proj_destroyed()) {
                if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
                return;
            }
        } else if (!scd_good_future()) { o_delete(o); return; }
    }
    int asset = flying ? (holo ? SCDA_HOLOFLY : SCDA_ANIMALFLY) : (holo ? SCDA_HOLOGROUND : SCDA_ANIMALGROUND);
    if (!OB8(o, OBJ_routine)) {
        if (!o_asset_maps(asset)) { o_delete(o); return; }
        SET8(o, OBJ_routine, holo ? 4 : 2);
        o_init(o, asset, 4, wwz() && flying ? 1 : holo ? 3 : 4, 8, 8);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, AN_X0, OB16(o, OBJ_x_pos));
        if (flying) {
            SET16(o, AN_Y0, OB16(o, OBJ_y_pos));
            turn(o);
            if (holo) SET8(o, OBJ_anim, 1);
            else {
                SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
                SET8(o, AN_ANG, 1);
                SET8(o, AN_STEP, 1);
            }
        } else if (holo) {
            if (!wwz()) turn(o);
        } else {
            SET32(o, AN_VX, 0x10000);
            SET32(o, AN_VY, (uint32_t)-0x40000);
            SET8(o, AN_STEP, 0);
        }
        return;                                               /* (the first frame: unseen) */
    }
    int16_t s, c;
    if (holo) {                                               /* (the projector's: its show) */
        if (holo_parent_gone(o)) { o_delete(o); return; }
        if (flying) {
            o_sincos(OB8(o, AN_ANG), &s, &c);
            SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> 3));
            SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> 4));
            SET8(o, AN_ANG, OB8(o, AN_ANG) + 4);
            if (!(OB8(o, AN_ANG) & 0x7F)) { turn(o); return; }
        }
        o_animate(o, o_asset_anim(asset));
        o_draw(o);
        return;
    }
    if (flying) {
        o_sincos(OB8(o, AN_ANG), &s, &c);
        SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> 1));
        SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> 1));
        uint8_t d0 = (uint8_t)(OB8(o, AN_ANG) + OB8(o, AN_STEP));
        if ((uint8_t)(d0 - 1) >= 0x7F) {                      /* (round the loop: turn back) */
            d0 = OB8(o, AN_ANG);
            SET8(o, AN_STEP, (uint8_t)-(int8_t)OB8(o, AN_STEP));
            turn(o);
        }
        SET8(o, AN_ANG, d0);
        o_animate(o, o_asset_anim(asset));
        o_draw(o);
        if (o_out_of_range(o, OB16(o, AN_X0))) o_delete_respawnable(o);
        return;
    }
    if (OB8(o, AN_STEP) == 2) {                               /* (Wacky Workbench's: turns, the frame after) */
        SET8(o, AN_STEP, 0);
        SET32(o, AN_VX, (uint32_t)-(int32_t)r32(o + AN_VX));
        turn(o);
        o_draw(o);
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
        return;
    }
    cd_move(o, (int32_t)r32(o + AN_VX), (int32_t)r32(o + AN_VY));       /* hopping */
    SET32(o, AN_VY, r32(o + AN_VY) + 0x2000);
    SET8(o, OBJ_frame, (int32_t)r32(o + AN_VY) < 0 ? 0 : 1);
    int16_t d1 = o_floor_dist(o);
    if (d1 < 0) {
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET32(o, AN_VY, (uint32_t)-0x40000);
        if (wwz())
            SET8(o, AN_STEP, OB8(o, AN_STEP) + 1);            /* (every second landing) */
        else {
            SET32(o, AN_VX, (uint32_t)-(int32_t)r32(o + AN_VX));
            turn(o);
        }
    }
    o_draw(o);
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

/* ---- $2E Metal Sonic's projector (the past): a ball breaks it, its show
 * ends and the act's animals are free; subtype: the hologram itself -------- */
#define PJ_COUNT 0x30                  /* oVar2A */
#define PJ_LOCS  0x32                  /* oVar2C */
static void spawn_holo(uint32_t o, int id, int dx, int dy, uint8_t sub)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, native_here(id));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, AN_PARENT, (uint16_t)o);
}

void cd_projector(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {                                /* Metal Sonic's hologram */
        if (holo_parent_gone(o)) { o_delete(o); return; }
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            int small = OB8(o, OBJ_subtype) & 0x80;
            o_init(o, SCDA_PROJECTOR, OB8(o, OBJ_render) | 4, 4, small ? 8 : 0x14, small ? 4 : 0x18);
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
        SET8(o, OBJ_x_radius, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET32(o, PJ_LOCS, o_asset_anim(SCDA_PROJBOOM));
        if (wwz()) {                                          /* (Wacky Workbench's show) */
            spawn_holo(o, 0x2F, -0x15, -7, 0xFF);
            spawn_holo(o, 0x2F, -0x48, -4, 1);
            spawn_holo(o, 0x29, -0x48, -0x18, 0x80);
            spawn_holo(o, 0x29, -0x54, 7, 0x81);
        } else {
            spawn_holo(o, 0x2E, -0x15, -7, 0xFF);
            spawn_holo(o, 0x2E, -0x58, -4, 1);
            spawn_holo(o, 0x24, -0x58, -0x18, 0x80);
            spawn_holo(o, 0x24, -0x64, 4, 0x81);
        }
        /* fall through */
    case 2: {
        int hit = o_s1_touch(o, 0x12, 0x11), broken = 0;      /* (CD's touch size $3B) */
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), OBJ_anim) == 2) broken = 1;
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        if (!broken) break;
        SET8(o, OBJ_routine, 6);                              /* (StartExploding) */
        SET8(o, OBJ_frame, 1);
        SET8(o, PJ_GONE, 0xFF);
        cd_get_off(o);
        break;
    }
    case 6: {                                                 /* exploding */
        uint32_t a6 = r32(o + PJ_LOCS);
        uint8_t when = tri_rd8(a6);
        if (when & 0x80) { SET8(o, OBJ_routine, 8); SET16(o, PJ_COUNT, 60); break; }
        SET8(o, PJ_COUNT, OB8(o, PJ_COUNT) + 1);
        if (OB8(o, PJ_COUNT) != when) break;
        int8_t dx = (int8_t)tri_rd8(a6 + 1), dy = (int8_t)tri_rd8(a6 + 2);
        SET32(o, PJ_LOCS, a6 + 3);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
        o_sfx_cd(0x9E);                                       /* (FM_EXPLODE; the disc's projector has no jingle) */
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

/* ---- $2F Amy Rose, $30 her hearts (act 1's present) ----------------------- */
/* She chases the player about where she stands, hugs one who walks slowly by
 * her (the player held till the jump buttons, or 9:50 on the clock), then hops
 * after them. Her colours replace line 1 while she is in the level (Pal_AmyRose;
 * the level's own back when she is gone). */
#define AM_X0    0x30                  /* oVar36: where she stood */
#define AM_T     0x32                  /* oVar3A */
#define AM_HT    0x33                  /* oVar3B: to the next heart */
#define AM_FL    0x34                  /* oVar3E: 0 holding, 2 by the player, 6 hopping, 7 has held */
#define AM_HOPS  0x35                  /* oVar3F */
#define AM_PREV  0x36                  /* oPrevAnim */
#define AM_PAL   0x37                  /* (her colours are on line 1) */
#define OBJ_status2 0x2B               /* status_secondary (0 shield, 1 invincible) */
int scd_time_warping(void);
static uint16_t s_amy_line[16];        /* (line 1 as it was) */

static void amy_pal(uint32_t o, int on)                       /* ObjAmyRose_LoadPal / ResetPal */
{
    uint32_t pal = o_asset_anim(SCDA_AMYPAL);
    if (on && !OB8(o, AM_PAL)) {
        if (!pal) return;
        SET8(o, AM_PAL, 1);
        for (unsigned k = 0; k < 16; k++) s_amy_line[k] = r16(RAM_Target_palette + 0x20 + k * 2u);
    }
    if (!OB8(o, AM_PAL)) return;
    for (unsigned k = 0; k < 16; k++) {
        uint16_t c = on ? tri_rd16(pal + k * 2u) : s_amy_line[k];
        w16(RAM_Normal_palette + 0x20 + k * 2u, c);
        w16(RAM_Target_palette + 0x20 + k * 2u, c);
    }
}

/* AnimateObjSimple: per animation [frame | flips << 5, time]..., $FF */
static void amy_animate(uint32_t o, int anim)
{
    uint32_t tab = o_asset_anim(SCDA_AMY);
    SET8(o, OBJ_anim, anim);
    if (anim != OB8(o, AM_PREV)) {
        SET8(o, AM_PREV, anim);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
    SET8(o, OBJ_anim_timer, t);
    if (t >= 0 || !tab) return;
    uint32_t s = tab + tri_rd16(tab + (unsigned)anim * 2u);
    uint8_t f = tri_rd8(s + OB8(o, OBJ_anim_frame));
    if (f & 0x80) { SET8(o, OBJ_anim_frame, 0); f = tri_rd8(s); }
    SET8(o, OBJ_frame, f & 0x1F);
    uint8_t flips = (uint8_t)(((f >> 5) ^ OB8(o, OBJ_status)) & 3);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | flips);
    SET8(o, OBJ_anim_timer, tri_rd8(s + OB8(o, OBJ_anim_frame) + 1u));
    SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 2);
}

static void amy_face(uint32_t o, uint32_t p)                  /* ObjAmyRose_SetFacing */
{
    int flip = OB16(o, OBJ_x_pos) >= OB16(p, OBJ_x_pos);
    SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | flip);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | flip);
}

static unsigned amy_dx(uint32_t o, uint32_t p)
{
    int d = (int)OB16(p, OBJ_x_pos) - (int)OB16(o, OBJ_x_pos);
    return (unsigned)(d < 0 ? -d : d);
}

static int amy_in_range(uint32_t o)                           /* (about where she stood) */
{
    uint16_t x0 = OB16(o, AM_X0), x = OB16(o, OBJ_x_pos);
    return OBS16(o, OBJ_x_vel) < 0 ? (uint16_t)(x0 - 0x130) < x : (uint16_t)(x0 + 0x90) >= x;
}

static void amy_release(uint32_t p)                           /* Player_Jump2 */
{
    SET8(p, OBJ_object_ctrl, 0);
    int16_t s, c;
    int speed = OB8(p, 0x38) == 2 ? 0x600 : 0x680;            /* (Knuckles' jump is his own) */
    o_sincos((uint8_t)(OB8(p, OBJ_angle) - 0x40), &s, &c);
    SET16(p, OBJ_x_vel, (uint16_t)(OBS16(p, OBJ_x_vel) + ((c * speed) >> 8)));
    SET16(p, OBJ_y_vel, (uint16_t)(OBS16(p, OBJ_y_vel) + ((s * speed) >> 8)));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~0x20);
    SET8(p, 0x40, 1);                                         /* (jumping) */
    SET8(p, 0x3C, 0);                                         /* (stick_to_convex) */
    SET8(p, OBJ_y_radius, OB8(p, 0x44));                      /* (the defaults) */
    SET8(p, OBJ_x_radius, OB8(p, 0x45));
    if (OB8(p, OBJ_status) & 4) SET8(p, OBJ_status, OB8(p, OBJ_status) | 0x10);
    else roll_up(p);
}

static void amy_grab(uint32_t o, uint32_t p)                  /* ObjAmyRose_CheckGrabSonic */
{
    if (!amy_in_range(o) || (r32(RAM_Timer) & 0xFFFFFF) >= 0x093200 || r16(RAM_Debug_placement_mode) ||
        !playing(p))
        return;
    uint16_t a = OB16(p, OBJ_x_pos), b = OB16(o, OBJ_x_pos);
    if (OB8(p, OBJ_status) & ST_XFLIP) { uint16_t t = a; a = b; b = t; }
    if (a < b || (uint16_t)(a - b) < 0x0C || (uint16_t)(a - b) >= 0x18) return;   /* (just behind them) */
    int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 8);
    if (dy < 0 || dy >= 0x10) return;
    int16_t vx = OBS16(p, OBJ_x_vel);
    /* (CD tests oPlayerHurt's high byte, never set: a flashing player is hugged too) */
    if ((OB8(p, OBJ_status) & (ST_AIR | 4)) || (uint16_t)(vx < 0 ? -vx : vx) >= 0x680 ||
        (OB8(p, OBJ_status2) & 3) || scd_time_warping()) {
        SET8(o, OBJ_routine, 6);
        return;
    }
    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~4);
    SET8(o, AM_FL, OB8(o, AM_FL) | 0x81);
    SET16(o, OBJ_y_vel, 0);
    SET16(o, OBJ_x_vel, 0);
    SET8(o, OBJ_frame, 7);
    SET8(o, OBJ_routine, 4);
    tri_scd_voice(SCD_PCM_GIGGLE);
}

static void amy_main(uint32_t o, uint32_t p)                  /* ObjAmyRose_Main */
{
    amy_face(o, p);
    uint8_t fl = OB8(o, AM_FL);
    if (fl & 0x40) goto bump;
    if (fl & 4) {
        if (amy_dx(o, p) < 0x20) goto by_them;
        SET8(o, AM_FL, fl & ~4);
    } else if (!OBS16(p, OBJ_x_vel) && amy_dx(o, p) < 0x0A) {
    by_them:
        SET8(o, AM_FL, OB8(o, AM_FL) | 4);
        SET16(o, OBJ_x_vel, 0);
        goto stand;
    }
    {
        int16_t v = (int16_t)(OBS16(o, OBJ_x_vel) + (OB8(o, OBJ_status) & ST_XFLIP ? -0x10 : 0x10));
        if ((uint16_t)(v < 0 ? -v : v) >= 0x200) v = v < 0 ? -0x200 : 0x200;
        SET16(o, OBJ_x_vel, v);
        if (amy_in_range(o)) {
            int16_t d1 = o_floor_dist(o);
            if (d1 >= -7 && d1 < 7) {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                cd_move(o, (int32_t)v * 256, 0);
                amy_grab(o, p);
                amy_animate(o, 2);
                return;
            }
        }
    }
bump:
    SET16(o, OBJ_x_vel, 0);
    if (!(OB8(o, AM_FL) & 0x80)) {
    stand:
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        amy_animate(o, 1);
        return;
    }
    if (!(OB8(o, AM_FL) & 0x40)) {                            /* (after a hug: hops, three at a time) */
        if (OB8(o, AM_HOPS) >= 3) {
            uint8_t t = OB8(o, AM_T);
            SET8(o, AM_T, t + 4);
            if (t >= 0xFC) SET8(o, AM_HOPS, 0);
            amy_animate(o, 4);
            return;
        }
        SET16(o, OBJ_y_vel, -0x300);
        SET8(o, AM_FL, OB8(o, AM_FL) | 0x40);
    }
    cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) * 256);
    SET16(o, OBJ_y_vel, OBS16(o, OBJ_y_vel) + 0x40);
    SET8(o, OBJ_frame, OBS16(o, OBJ_y_vel) < 0 ? 6 : 4);
    if (o_floor_dist(o) < 0) {
        SET16(o, OBJ_y_vel, 0);
        SET8(o, AM_FL, OB8(o, AM_FL) & ~0x40);
        SET8(o, AM_HOPS, OB8(o, AM_HOPS) + 1);
    }
}

static void amy_hearts(uint32_t o)                            /* ObjAmyRose_MakeHearts */
{
    uint8_t t = OB8(o, AM_HT), step = OB8(o, AM_FL) & 1 ? 0x10 : 6;
    SET8(o, AM_HT, t + step);
    if (t + step < 0x100) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, native(0x30));
    int dx = OB8(o, OBJ_status) & ST_XFLIP ? -0x0A : 8;
    if (OB8(o, AM_FL) & 1) dx = -dx;
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x0C);
}

static void amy(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* (until the player is near) */
        o_init(o, SCDA_AMY, OB8(o, OBJ_render) | 4, 1, 0x0C, 0x18);
        SET8(o, OBJ_x_radius, 0x0C);
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, AM_X0, OB16(o, OBJ_x_pos));
        amy_pal(o, 1);
        for (int k = 0; k < 64; k++) {                        /* (onto the floor) */
            int16_t d1 = o_floor_dist(o);
            if (!d1) break;
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        }
        amy_face(o, p);
        if (amy_dx(o, p) < 0x70) SET8(o, OBJ_routine, 2);
        amy_animate(o, 5);
        break;
    case 2:
        amy_main(o, p);
        break;
    case 4:                                                   /* ObjAmyRose_HoldSonic */
        if (!playing(p)) {                                    /* (not hers to hold any more) */
            SET8(p, OBJ_object_ctrl, OB8(p, OBJ_object_ctrl) & ~1);
            SET8(o, AM_FL, OB8(o, AM_FL) & ~1);
            SET8(o, OBJ_routine, 2);
            break;
        }
        SET8(p, OBJ_object_ctrl, OB8(p, OBJ_object_ctrl) | 1);
        SET8(p, OBJ_anim, 5);
        /* (ObjAmyRose_SlowSonic moves the player by her speed: none while she holds) */
        amy_face(o, p);
        SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos) + (OB8(p, OBJ_status) & ST_XFLIP ? 0x0C : -0x0C));
        SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
        if (r8(RAM_Ctrl_1_pressed_logical) & 0x70) {          /* (jumped out) */
            amy_release(p);
            SET8(o, AM_FL, OB8(o, AM_FL) & ~1);
            SET8(o, OBJ_routine, 6);
        } else if ((r32(RAM_Timer) & 0xFFFFFF) >= 0x093200) {   /* (let go at 9:50) */
            amy_release(p);
            SET8(o, AM_FL, OB8(o, AM_FL) & ~1);
            SET8(o, OBJ_routine, 2);
        } else
            amy_animate(o, 3);
        break;
    case 6: {                                                 /* ObjAmyRose_FollowSonic */
        SET8(o, OBJ_frame, 6);
        SET16(o, OBJ_x_vel, OB8(o, OBJ_status) & ST_XFLIP ? 0x80 : -0x80);
        int d = (int)OB16(o, OBJ_x_pos) - (int)OB16(o, AM_X0);
        if ((d < 0 ? -d : d) >= 0x80) SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, -0x300);
        SET8(o, OBJ_routine, 8);
    }   /* fall through */
    default:                                                  /* ObjAmyRose_WaitLand */
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, (int32_t)OBS16(o, OBJ_y_vel) * 256);
        SET16(o, OBJ_y_vel, OBS16(o, OBJ_y_vel) + 0x40);
        if (OBS16(o, OBJ_y_vel) >= 0) SET8(o, OBJ_frame, 7);
        if (o_floor_dist(o) < 0) {
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
            uint8_t t = OB8(o, AM_T);
            SET8(o, AM_T, t + 0x10);
            if (t >= 0xF0) SET8(o, OBJ_routine, 2);
        }
        break;
    }
    amy_hearts(o);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        if (OB8(o, OBJ_routine) == 4) SET8(p, OBJ_object_ctrl, OB8(p, OBJ_object_ctrl) & ~1);
        amy_pal(o, 0);
        return;
    }
    o_draw(o);
}

#define HT_T     0x30                  /* oVar3A */
#define HT_STILL 0x31                  /* oVar3C */
static void amy_heart(uint32_t o)                             /* ObjAmyHeart */
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_HEART, OB8(o, OBJ_render) | 4, 3, 8, 8);
        SET8(o, OBJ_frame, 8);
        SET16(o, OBJ_y_vel, -0x60);
    }
    if (!OB8(o, HT_STILL)) {                                  /* (a ripple) */
        int16_t s, c;
        o_sincos((uint8_t)(OB8(o, HT_T) * 3), &s, &c);
        SET16(o, OBJ_x_vel, s >> 2);
    }
    o_move(o);
    uint8_t t = (uint8_t)(OB8(o, HT_T) + 1);
    SET8(o, HT_T, t);
    if (t == 0x14) SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
    if (t == 0x6E) {
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, OBJ_x_vel, 0);
        SET8(o, HT_STILL, 0xFF);
    }
    if (t == 0x78 || o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
    o_draw(o);
}

/* ---- $32 scenery: a branch (past and futures) ----------------------------- */
static void scenery(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SCENERY, OB8(o, OBJ_render) | 4, 0, 0x10, 0x18);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $40 Taga-Taga: leaps out of the water and back ---------------------- */
#define TT_X0    0x30                  /* oVar2A */
#define TT_Y0    0x32                  /* oVar2C (long: no fraction) */
#define TT_VY0   0x36                  /* oVar30 */
#define TT_VY    0x3A                  /* oVar34 */
#define TT_GRAV  0x3E                  /* oVar38 */
#define FM_SPLASH 0xA2                 /* (ObjTagaTaga: out of the water and back in) */
static void taga_taga(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_TAGATAGA2 : SCDA_TAGATAGA;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        badnik_init(o, asset, 3, 0x10, 0x16, 0);
        SET16(o, TT_X0, OB16(o, OBJ_x_pos));
        SET32(o, TT_Y0, (uint32_t)OB16(o, OBJ_y_pos) << 16);
        SET32(o, TT_VY0, dmg ? -0x30000 : -0x3C000);
        SET32(o, TT_GRAV, 0x1000);
        /* fall through */
    case 2:                                                   /* under water */
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_anim, 0);
        SET8(o, O_PREV_ANIM_S3K, 0xFF);
        SET8(o, OBJ_coll_flags, 0);
        SET32(o, OBJ_y_pos, r32(o + TT_Y0));
        /* fall through */
    case 4:                                                   /* (its animation's end: the leap) */
        break;
    case 6:
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_anim, 1);
        SET8(o, O_PREV_ANIM_S3K, 0xFF);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x2E));
        SET32(o, OBJ_y_pos, r32(o + TT_Y0));
        SET32(o, TT_VY, r32(o + TT_VY0));
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SPLASH);
        /* fall through */
    case 8:
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + TT_VY));
        SET32(o, TT_VY, r32(o + TT_VY) + r32(o + TT_GRAV));
        if (OBS16(o, OBJ_y_pos) > OBS16(o, TT_Y0)) {
            SET8(o, OBJ_routine, 2);
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SPLASH);
        }
        break;
    }
    o_animate(o, o_asset_anim(asset));
    badnik_end(o, OB16(o, TT_X0));
}

/* ---- $28 springboard: a plank that flings the player who lands on it ---- */
#define SB_FLING 0x30                  /* oVar2A */
static int sb_touch(uint32_t o, uint32_t p, int k)            /* ObjSpringBoard_ChkPlayer */
{
    static const int8_t box[8][4] = {
        { 16, -16, 16, -16 }, { 16, -16, 4, -4 }, { 9, -9, 56, 16 }, { 0, -24, 4, -4 },
        { 0, -24, 12, 0 }, { 24, 0, 4, -4 }, { 24, 0, 12, 0 }, { 32, -32, 32, -32 },
    };
    const int8_t *b = box[k & 7];
    int hit = !r16(RAM_Debug_placement_mode) && OB8(p, OBJ_routine) < 6 && OBS16(p, OBJ_y_vel) >= 0;
    if (hit) {
        int16_t x = OBS16(o, OBJ_x_pos), px = OBS16(p, OBJ_x_pos), r = (int8_t)OB8(p, OBJ_x_radius);
        int16_t y = OBS16(o, OBJ_y_pos), py = OBS16(p, OBJ_y_pos), h = (int8_t)OB8(p, OBJ_y_radius);
        hit = (int16_t)((px - r) - (x + b[0])) < 0 && (int16_t)((x + b[1]) - (px + r)) < 0 &&
              (int16_t)((py - h) - (y + b[2])) < 0 && (int16_t)((y + b[3]) - (py + h)) < 0;
    }
    SET8(o, OBJ_status, hit ? OB8(o, OBJ_status) | 8 : OB8(o, OBJ_status) & ~8);
    return hit;
}

static void sb_land(uint32_t o, uint32_t p, int routine, int anim)   /* (on top of it) */
{
    SET32(p, OBJ_y_pos, r32(o + OBJ_y_pos) - ((uint32_t)OB8(p, OBJ_y_radius) << 16));
    SET8(o, OBJ_routine, routine);
    SET8(o, OBJ_anim, anim);
}

static void sb_wait(uint32_t o, uint32_t p, int k, int main, int fling)   /* ObjSpringBoard_*Main2 */
{
    if (sb_touch(o, p, k)) return;
    SET8(o, OBJ_routine, OB8(p, OBJ_status) & ST_AIR ? fling : main);
    if (OB8(o, OBJ_routine) == fling) SET8(o, SB_FLING, 0x40);
}

static void sb_fling(uint32_t o, uint32_t p, int k, int main, int anim)   /* ObjSpringBoard_*Fling */
{
    if (sb_touch(o, p, k)) {
        int16_t v = (int16_t)(OBS16(p, OBJ_y_vel) + 0x100);
        if (v >= 0xA00) v = 0xA00;
        SET16(p, OBJ_y_vel, (uint16_t)-v);
        SET8(o, SB_FLING, 0x40);
        if (OB8(p, OBJ_status) & ST_AIR) SET8(p, 0x40, 0);   /* (jumping) */
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~0x20);
        SET8(p, 0x3C, 0);                                     /* (stick_to_convex) */
        SET8(p, OBJ_y_radius, OB8(p, 0x44));
        SET8(p, OBJ_x_radius, OB8(p, 0x45));
        if (OB8(p, OBJ_status) & 4) SET8(p, OBJ_status, OB8(p, OBJ_status) | 0x10);
        else roll_up(p);
    }
    SET8(o, SB_FLING, OB8(o, SB_FLING) - 1);
    if (OB8(o, SB_FLING)) return;
    SET8(o, SB_FLING, 0x40);
    SET8(o, OBJ_routine, main);
    SET8(o, OBJ_anim, anim);
}

static void springboard(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_init(o, SCDA_SPRINGBOARD, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET8(o, OBJ_x_radius, 0x18);
        SET8(o, OBJ_y_radius, 4);
        int flip = OB8(o, OBJ_subtype) || (OB8(o, OBJ_render) & 1);
        if (flip) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        }
        SET8(o, OBJ_anim, flip ? 4 : 3);
        SET8(o, OBJ_routine, flip ? 4 : 2);
        cd_despawn(o, OB16(o, OBJ_x_pos));
        return;
    }
    case 2: if (sb_touch(o, p, 5)) sb_land(o, p, 0xA, 3); break;
    case 4: if (sb_touch(o, p, 3)) sb_land(o, p, 0xC, 4); break;
    case 6: sb_wait(o, p, 5, 2, 0xA); break;
    case 8: sb_wait(o, p, 3, 4, 0xC); break;
    case 0xA: SET8(o, OBJ_anim, 1); sb_fling(o, p, 6, 2, 3); break;
    case 0xC: SET8(o, OBJ_anim, 2); sb_fling(o, p, 4, 4, 4); break;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_animate(o, o_asset_anim(SCDA_SPRINGBOARD));
    o_draw(o);
}

/* ---- $29 swing (act 2): a pendulum of links, a seat at the end ----------- */
#define SW_VEL   0x30                  /* CD $30 */
#define SW_Y0    0x32
#define SW_MAX   0x34
#define SW_X0    0x36
#define SW_MIN   0x38
#define SW_ANG   0x3A                  /* (word: the high byte the angle) */
#define SW_LINK  0x3C                  /* (16 px each; 0: the pivot) */
#define SW_DIR   0x3D
#define SW_ACC   0x3E
#define SW_N     0x40                  /* CD $2F: links */
#define SW_OLDX  0x42
static void sw_move(uint32_t o)                               /* (the swing, then its place) */
{
    int16_t v = OBS16(o, SW_VEL);
    if (!OB8(o, SW_DIR)) {
        v = (int16_t)(v + OBS16(o, SW_ACC));
        SET8(o, SW_DIR, v == OBS16(o, SW_MAX) ? 0xFF : 0);
    } else {
        v = (int16_t)(v - OBS16(o, SW_ACC));
        if (v == OBS16(o, SW_MIN)) SET8(o, SW_DIR, 0);
    }
    SET16(o, SW_VEL, v);
    SET16(o, SW_ANG, OB16(o, SW_ANG) + v);
    int16_t s, c;
    o_sincos(OB8(o, SW_ANG), &s, &c);
    int r = (int8_t)OB8(o, SW_LINK) << 4;
    SET16(o, OBJ_x_pos, OB16(o, SW_X0) + ((s * r) >> 8));
    SET16(o, OBJ_y_pos, OB16(o, SW_Y0) + ((c * r) >> 8));
}

static void swing(uint32_t o)
{
    static const int16_t init[8][4] = {                       /* angle, ease, top speed, bottom speed */
        { -0x4000, 8, 0x200, -0x200 }, { -0x4000, 2, 0x100, -0x100 }, { -0x4000, -2, -0x200, 0x100 },
        { -0x6000, 3, 0x180, -0x180 }, { -0x8000, -2, -0x100, 0x100 }, { -0x8000, 2, 0x100, -0x100 },
        { -0x8000, -1, -0x100, 0 }, { -0x8000, 1, 0x100, 0 },
    };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SWING, OB8(o, OBJ_render) | 4, 3, 0x18, 8);
        if (!OB8(o, SW_LINK)) {                               /* (the pivot: the links) */
            SET16(o, SW_X0, OB16(o, OBJ_x_pos));
            SET16(o, SW_Y0, OB16(o, OBJ_y_pos));
            int n = OB8(o, OBJ_subtype) & 0xF;
            SET8(o, SW_N, n);
            for (int k = n; k > 0; k--) {
                uint32_t a1 = o_alloc_after(o);
                if (!a1) continue;
                SET32(a1, OBJ_code, r32(o + OBJ_code));
                SET16(a1, SW_X0, OB16(o, SW_X0));
                SET16(a1, SW_Y0, OB16(o, SW_Y0));
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
                SET8(a1, SW_N, n);
                SET8(a1, SW_LINK, k);
                SET8(a1, OBJ_frame, k == n ? 1 : 2);
            }
        }
        const int16_t *t = init[(OB8(o, OBJ_subtype) >> 4) & 7];
        SET16(o, SW_ANG, (uint16_t)t[0]);
        SET16(o, SW_ACC, (uint16_t)t[1]);
        SET16(o, SW_MAX, (uint16_t)t[2]);
        SET16(o, SW_MIN, (uint16_t)t[3]);
    }
    SET16(o, SW_OLDX, OB16(o, OBJ_x_pos));
    sw_move(o);
    if (OB8(o, SW_N) == OB8(o, SW_LINK))                      /* (the seat) */
        o_platform(o, OB8(o, OBJ_width_px) + 10, 8, OB16(o, SW_OLDX));
    if (cd_despawn(o, OB16(o, SW_X0))) return;
    o_draw(o);
}

/* ---- $0F spinning top (futures): back and forth on the floor, a spring on it */
#define KM_Y0    0x32                  /* oVar32: where it came down */
#define KM_X0    0x36                  /* oVar36 */
#define KM_SPR   0x38                  /* (its spring) */
#define KM_MAP   0x3A
static void koma(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_KOMA, OB8(o, OBJ_render) | 4, 4, 8, 7);
        SET8(o, OBJ_y_radius, 7);
        SET16(o, KM_X0, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_x_vel, 0x180);
        uint32_t a1 = o_alloc();
        if (!a1) { o_delete_respawnable(o); return; }
        SET32(a1, OBJ_code, S3K_Obj_Spring);                  /* (CD's: a spring that follows its parent) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x10);
        SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype) & 2);
        SET16(o, KM_SPR, (uint16_t)a1);
    }   /* fall through */
    case 2: {                                                 /* down onto the floor */
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        else {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, KM_Y0, OB16(o, OBJ_y_pos));
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    default:                                                  /* (it turns 12 px up a slope) */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        if ((uint16_t)(OB16(o, KM_Y0) - OB16(o, OBJ_y_pos)) >= 0xC) SET16(o, OBJ_x_vel, -OBS16(o, OBJ_x_vel));
        o_move(o);
        o_animate(o, o_asset_anim(SCDA_KOMA));
        break;
    }
    /* (its spring: S3K's replaces its code after its init; its mappings mark it) */
    uint32_t spr = 0xFF0000u | OB16(o, KM_SPR);
    int has = OB16(o, KM_SPR) && r32(spr + OBJ_code);
    if (has && !OB16(o, KM_MAP)) SET16(o, KM_MAP, (uint16_t)r32(spr + OBJ_mappings));
    if (has && OB16(o, KM_MAP) && (uint16_t)r32(spr + OBJ_mappings) != OB16(o, KM_MAP)) {
        SET16(o, KM_SPR, 0);
        has = 0;
    }
    if (has) {
        SET16(spr, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(spr, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x10);
    }
    if (o_out_of_range(o, OB16(o, KM_X0))) {
        if (has) o_delete(spr);
        o_delete_respawnable(o);
        return;
    }
    if (OB8(o, OBJ_routine) == 4) o_draw(o);
}

void scd_ppz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x24] = cd_animal;
    t[base + 0x2E] = cd_projector;
    t[base + 0x2D] = cd_generator;
    t[base + 0x09] = spin_disc;
    t[base + 0x33] = log_shadow;
    t[base + 0x36] = wall;
    t[base + 0x07] = tunnel_path;
    t[base + 0x0B] = door_splash;
    t[base + 0x0D] = tunnel_door;
    t[base + 0x20] = ledge;
    t[base + 0x21] = platform;
    t[base + 0x22] = tamabboh;
    t[base + 0x26] = cd_spikes;
    t[base + 0x2A] = booster;
    t[base + 0x2C] = plants;
    t[base + 0x2F] = amy;
    t[base + 0x0F] = koma;
    t[base + 0x28] = springboard;
    t[base + 0x29] = swing;
    t[base + 0x32] = scenery;
    t[base + 0x40] = taga_taga;
    t[base + 0x30] = amy_heart;
    t[base + 0x31] = sonic_hole;
    t[base + 0x34] = cd_spin_tag;
    t[base + 0x35] = door_v;
    t[base + 0x3D] = mosqui;
    t[base + 0x3E] = pata_bata;
    t[base + 0x3F] = anton;
}
