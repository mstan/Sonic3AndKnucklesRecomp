/*
 * scd_ppz_boss.c — Palmtree Panic act 3 (Sonic CD R13C / R13D): Egg-HVC-001,
 * the capsule and its flowers. Transcribed from the Mega-CD program (the
 * routines' R13C addresses in brackets; names from the PC port's BOSS_1.C).
 *
 * The walker is nine objects the controller ($2A, Eggman) builds: a body
 * ($2B), two legs of three parts ($2C-$2E: hip, shin, foot) and a shoulder
 * ($2F) with two arms of three ($30-$32: upper arm, forearm, hand). The near
 * limbs come first; the far ones (CD's $2C bit 2) sit 10 px left, behind.
 * Each part links to its parent (CD $2E) and children ($30, $32); a planted
 * leg's swing moves the body instead of itself, which is the walk. Eggman
 * takes the hits ($FC); the hands only clang the player away ($FF); shins and
 * feet hurt. Three hits: an arm falls off at each of the first two.
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (C2A..C3C below); CD's $10
 * long (a second speed in the parts) is S3K's $18, CD's $25 S3K's $2D.
 * Collision: CD's boss touch ($C0 | size while bossFight) is S3K's enemy
 * touch with hits left (collision_property): S3K's Touch_Enemy clears the
 * type on a hit, as CD's does, and the boss polls for that.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>

#define C2A 0x30
#define C2B 0x31
#define C2C 0x32                       /* flags */
#define C2D 0x33
#define C2E 0x34                       /* parent */
#define C30 0x36                       /* child */
#define C32 0x38                       /* child 2 */
#define C34 0x3A
#define C35 0x3B
#define C36 0x3C
#define C38 0x3E
#define C3A 0x40
#define C3C 0x42
#define C10 0x18                       /* (CD $10, long) */
#define C25 0x2D                       /* (CD $25: routine 2) */

#define RAM_Distance_from_top 0xFFEE2Cu
#define FM_CLANG     0x98              /* (a hand's clang: the spring's) */
#define FM_BOMB      0x9E              /* (FM_EXPLODE) */
#define CDDA_BOSS    24

void tri_scd_level_music(void);
void tri_scd_load_palette(unsigned id);

static int s_base3;                    /* act 3's native slots (scd_obj.c) */
static uint8_t s_bossflag;             /* bossFight */
static uint16_t s_right;               /* (the level's right edge: CD's $2027FA table) */

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }

static uint32_t lk(uint32_t o, unsigned f)
{
    uint16_t w = o ? OB16(o, f) : 0;
    return w ? 0xFF0000u | w : 0;
}
static void set_lk(uint32_t o, unsigned f, uint32_t a) { if (o) SET16(o, f, (uint16_t)a); }

static int btst(uint32_t o, unsigned bit) { return o && (OB8(o, C2C) >> bit) & 1; }
static int bset(uint32_t o, unsigned bit)
{
    if (!o) return 0;
    uint8_t v = OB8(o, C2C);
    SET8(o, C2C, v | (1u << bit));
    return (v >> bit) & 1;
}
static int bclr(uint32_t o, unsigned bit)
{
    if (!o) return 0;
    uint8_t v = OB8(o, C2C);
    SET8(o, C2C, v & ~(1u << bit));
    return (v >> bit) & 1;
}
static int bchg(uint32_t o, unsigned bit)
{
    uint8_t v = OB8(o, C2C);
    SET8(o, C2C, v ^ (1u << bit));
    return (v >> bit) & 1;
}
static void routine(uint32_t o, int r) { if (o) SET8(o, OBJ_routine, r); }
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }
static void add_xy(uint32_t o, int32_t dx, int32_t dy)       /* (16.16, both) */
{
    SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + (uint32_t)dx);
    SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)dy);
}
static int32_t L(uint32_t o, unsigned f) { return (int32_t)r32(o + f); }
static void addL(uint32_t o, unsigned f, int32_t d) { SET32(o, f, (uint32_t)(L(o, f) + d)); }

static void anim_reset(uint32_t o, int anim)
{
    SET8(o, OBJ_frame, 0);
    SET8(o, OBJ_anim_frame, 0);
    SET8(o, OBJ_anim_timer, 0);
    SET8(o, 0x25, 0);
    SET8(o, OBJ_anim, anim);
}

/* the size fields: CD's $19 (display width) and $16 (height radius) */
static void part_init(uint32_t o, int asset, int width, int height)
{
    o_asset(o, asset);
    SET8(o, OBJ_status, 0);
    SET8(o, OBJ_render, 4);
    SET8(o, OBJ_width_px, width);
    SET8(o, OBJ_height_px, height > 0x20 ? height : 0x20);
    SET8(o, OBJ_y_radius, height);
}

/* CD's touch type as S3K's (the boss's: an enemy with hits left) */
static void set_coll(uint32_t o, uint8_t cd, uint8_t hits)
{
    if (!o) return;
    uint8_t t = cd ? tri_scd_coll(cd) : 0;
    if ((cd & 0xC0) == 0xC0) t &= 0x3F;
    SET8(o, OBJ_coll_flags, t);
    SET8(o, OBJ_coll_prop, hits);
}
static int hit(uint32_t o) { return o && !OB8(o, OBJ_coll_flags); }

static void part_draw(uint32_t o)
{
    uint8_t t = OB8(o, OBJ_coll_flags);
    if (t && ((t & 0xC0) || s_bossflag)) o_touch(o);         /* (CD's boss touch: only while bossFight) */
    o_draw(o);
}

/* (CD reads a deleted parent's cleared slot; S3K reuses slots: an arm part
 * whose parent is gone goes too) */
static int orphan(uint32_t o)
{
    uint32_t b = lk(o, C2E), c = b ? r32(b + OBJ_code) : 0;
    if (c >= native3(0x2A) && c <= native3(0x32)) return 0;
    o_delete(o);
    return 1;
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

static void explode(uint32_t o, int dx, int dy)               /* (CD's $18, no animal) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);
    set_xy(a1, wx(o) + dx, wy(o) + dy);
    o_sfx_cd(FM_BOMB);
}

/* a part off the walker: up, then falling until below the arena */
static int fall_off(uint32_t o, int32_t vx, int32_t vy, int32_t ax, int32_t ay)
{
    if (!bset(o, 1)) {
        SET32(o, C3C, (uint32_t)vx);
        SET32(o, C10, (uint32_t)vy);
    } else {
        addL(o, C3C, ax);
        addL(o, C10, ay);
    }
    add_xy(o, L(o, C3C), L(o, C10));
    if (wy(o) >= 0x240) { o_delete(o); return 1; }
    return 0;
}

/* ---- $2A the controller: Eggman --------------------------------------- */
static void egg1coli_set_body(uint32_t o)                     /* [$20BC38] */
{
    set_coll(o, 0xFC, 2);
    uint32_t b = lk(o, C30), a1 = lk(lk(b, C30), C30);
    set_coll(a1, 0xBD, 2);
    set_coll(lk(a1, C30), 0xBE, 2);
    a1 = lk(lk(b, C32), C30);
    set_coll(a1, 0xBD, 2);
    set_coll(lk(a1, C30), 0xBE, 2);
}
static void egg1coli_set_arm(uint32_t arm2) { set_coll(lk(lk(arm2, C30), C30), 0xFF, 2); }   /* [$20BC92] */
static void egg1coli_set_arm_l(uint32_t o) { egg1coli_set_arm(lk(lk(o, C32), C30)); egg1coli_set_body(o); }
static void egg1coli_set_arm_r(uint32_t o) { egg1coli_set_arm(lk(lk(o, C32), C32)); egg1coli_set_arm_l(o); }

static void egg1coli_set(uint32_t o)                          /* [$20BC06] */
{
    switch (OB8(o, C2B)) {
    case 3: egg1coli_set_arm_r(o); break;
    case 2: egg1coli_set_arm_l(o); break;
    case 1: egg1coli_set_body(o); break;
    }
}

static void sonic_hajiku(uint32_t a1)                         /* [$20BBCE] the player, knocked away */
{
    uint32_t p = RAM_Player_1;
    int16_t d1 = 0x400, d2 = -0x400;
    SET16(p, OBJ_ground_vel, 0x400);
    if (!(OB8(p, OBJ_status) & 2)) {
        SET8(p, OBJ_angle, OB8(p, OBJ_angle) ^ 0x80);
        d2 = 0;
    }
    if (OB16(p, OBJ_x_pos) < OB16(a1, OBJ_x_pos)) d1 = -d1;
    SET16(p, OBJ_x_vel, d1);
    SET16(p, OBJ_y_vel, d2);
}

static void egg1_warai_chk(uint32_t o)                        /* [$20BAC0] he laughs when the player is hurt */
{
    if (OB8(o, OBJ_anim)) return;
    uint32_t p = RAM_Player_1;
    if (!OB8(p, 0x34) && OB8(p, OBJ_routine) != 4 && OB8(p, OBJ_routine) != 6) return;
    anim_reset(o, 1);
    SET8(o, C2A, 0x3C);
}

static void egg1_coliude(uint32_t o, uint32_t hand)           /* [$20BAA8] a hand: clang */
{
    bset(hand, 4);
    sonic_hajiku(hand);
    egg1coli_set(o);
    o_sfx_cd(FM_CLANG);
    egg1_warai_chk(o);
}

static void egg1_defeat(uint32_t o)                           /* [$20BB4C] */
{
    anim_reset(o, 2);
    SET8(o, C2A, 0);
    bclr(o, 3);
    SET8(o, C25, 0);
    SET8(o, OBJ_routine, 6);
    set_coll(o, 0, 0);
    routine(lk(o, C32), 4);
    uint32_t b = lk(o, C30);
    routine(b, 0xE);
    for (uint32_t l1 = lk(b, C30), k = 0; k < 2; k++, l1 = lk(b, C32)) {
        routine(l1, 0x18);
        uint32_t l2 = lk(l1, C30);
        routine(l2, 0xE);
        set_coll(l2, 0, 0);
        uint32_t l3 = lk(l2, C30);
        routine(l3, 0xC);
        set_coll(l3, 0, 0);
    }
}

static void egg1_coli(uint32_t o, uint32_t a1)                /* [$20BAF8] Eggman hit */
{
    SET8(o, C34, 0x14);
    sonic_hajiku(a1);
    anim_reset(o, 2);
    SET8(o, C2A, 0x78);
    uint8_t n = (uint8_t)(OB8(o, C2B) - 1);
    SET8(o, C2B, n);
    if (!n) { egg1_defeat(o); return; }
    if (n == 2) { bset(lk(o, C32), 5); egg1coli_set_arm_l(o); }      /* (the far arm falls) */
    else { bset(lk(o, C32), 6); egg1coli_set_body(o); }              /* (the near one) */
}

static void egg1_hit_chk(uint32_t o)                          /* [$20B9FC] */
{
    if (OB8(o, C34)) {
        SET8(o, C34, OB8(o, C34) - 1);
        if (!OB8(o, C34)) egg1coli_set(o);
        return;
    }
    if (!btst(o, 3)) return;
    if (hit(o)) { egg1_coli(o, o); return; }
    for (int k = 0; k < 2; k++) {                             /* (shins and feet: CD checks them too) */
        uint32_t a1 = lk(lk(lk(o, C30), k ? C32 : C30), C30);
        if (hit(a1)) { egg1_coli(o, a1); return; }
        a1 = lk(a1, C30);
        if (hit(a1)) { egg1_coli(o, a1); return; }
    }
    uint8_t n = OB8(o, C2B);
    if (n == 3) {
        uint32_t h = lk(lk(lk(lk(o, C32), C32), C30), C30);
        if (hit(h)) { egg1_coliude(o, h); return; }
    } else if (n != 2)
        return;
    uint32_t h = lk(lk(lk(lk(o, C32), C30), C30), C30);
    if (hit(h)) egg1_coliude(o, h);
}

static uint32_t part(uint32_t o, int id, int prio)            /* [$20BD1C] a part, where the controller is */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, native3(id));
    set_xy(a1, wx(o), wy(o));
    SET16(a1, OBJ_priority, (uint16_t)(prio << 7));
    return a1;
}

static void egg1_make_act(uint32_t o)                         /* [$20BD36] */
{
    uint32_t body = part(o, 0x2B, 6);
    if (!body) return;
    set_lk(o, C30, body); set_lk(body, C2E, o);
    uint32_t prev = body;
    for (int side = 0; side < 2; side++) {                    /* legs: hip, shin, foot */
        static const uint8_t prio[2][3] = { { 6, 3, 2 }, { 7, 6, 5 } };
        uint32_t up = body;
        for (int k = 0; k < 3; k++) {
            uint32_t a1 = part(o, 0x2C + k, prio[side][k]);
            if (!a1) return;
            set_lk(up, k || !side ? C30 : C32, a1);
            set_lk(a1, C2E, up);
            if (side) bset(a1, 2);
            up = a1;
        }
        set_lk(up, C30, body);
        prev = up;
    }
    (void)prev;
    uint32_t sh = part(o, 0x2F, 3);                           /* shoulder */
    if (!sh) return;
    set_lk(sh, C2E, body); set_lk(o, C32, sh);
    for (int side = 0; side < 2; side++) {                    /* arms: upper arm, forearm, hand */
        static const uint8_t prio[2][3] = { { 5, 4, 3 }, { 7, 7, 6 } };
        uint32_t up = sh;
        for (int k = 0; k < 3; k++) {
            uint32_t a1 = part(o, 0x30 + k, prio[side][k]);
            if (!a1) return;
            set_lk(up, k || !side ? C30 : C32, a1);
            set_lk(a1, C2E, up);
            if (!k && !side) SET8(a1, C2A, 0x80);
            if (side) bset(a1, 2);
            up = a1;
        }
        set_lk(up, C30, body);
    }
    SET8(o, C2B, 3);
    egg1coli_set(o);
}

static void bom_set(uint32_t o)                               /* [$20D72A] */
{
    static const int8_t spot[10][2] = {
        { -48, -16 }, { 48, 16 }, { -16, -16 }, { 16, 16 }, { -32, 0 },
        { 48, -16 }, { -48, 16 }, { -16, 16 }, { 16, -16 }, { 32, 0 },
    };
    uint8_t n = OB8(o, C2B);
    if (n & 3) return;
    int k = (n >> 2) % 10;
    explode(o, spot[k][0], spot[k][1]);
}

static int egg1_step(uint32_t o)                              /* the routine; 0: not drawn */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg1_ini [$20BCA8] */
        w8(RAM_Boss_flag, 1);
        s_bossflag = 0;
        s_right = r16(RAM_Camera_max_X_pos);
        SET8(o, OBJ_routine, 2);
        part_init(o, SCDA_EGGMAN, 0x14, 8);
        SET16(o, OBJ_priority, 6 << 7);
        anim_reset(o, 1);
        set_xy(o, 0xC52, 0x78);
        line1(SCDA_BOSSPAL);
        break;
    case 2: {                                                 /* egg1_01 [$20BF44]: the arena closes behind */
        w16(RAM_Camera_max_X_pos, 0xAC0);
        w16(RAM_Camera_target_max_X_pos, 0xAC0);
        if (wx(p) < 0xA6A) break;
        int16_t d0 = (int16_t)(wx(p) - 0xA0);
        if (d0 < (int16_t)r16(RAM_Camera_min_X_pos)) break;
        if (wx(p) >= 0xB60) {
            SET8(o, OBJ_routine, 0xC);
            w16(RAM_Camera_max_X_pos, 0xAC0);
            w16(RAM_Camera_target_max_X_pos, 0xAC0);
            d0 = 0xAC0;
        }
        w16(RAM_Camera_min_X_pos, (uint16_t)d0);
        w16(RAM_Camera_target_min_X_pos, (uint16_t)d0);
        break;
    }
    case 4: {                                                 /* egg1_02 [$20BFE0]: the body's script */
        static const uint8_t script[] = { 2, 0, 4, 0, 6, 0, 8, 5, 0xA, 6, 0xC, 0xA, 8, 0xA, 0x10, 0x32, 0xA, 0xA, 0xFF, 0xFF };
        uint32_t b = lk(o, C30);
        if (!bclr(b, 0)) break;
        if (OB8(o, C25) == 2) {                               /* (on its feet: the fight) */
            anim_reset(o, 0);
            SET8(o, OBJ_anim + 1, 0);
            SET8(o, C2B, 3);
            bset(o, 3);
            egg1coli_set(o);
            b = lk(o, C30);
        }
        SET8(o, C25, OB8(o, C25) + 2);
        bclr(b, 1);
        if (script[OB8(o, C25)] & 0x80) SET8(o, C25, 6);
        routine(b, script[OB8(o, C25)]);
        SET8(b, C2D, script[OB8(o, C25) + 1]);
        break;
    }
    case 6: {                                                 /* egg1_03 [$20C062]: blowing up */
        uint8_t n = (uint8_t)(OB8(o, C2B) + 1);
        SET8(o, C2B, n);
        bom_set(o);
        if (n == 0x5E) {
            SET16(o, C3C, OB16(o, OBJ_x_pos));
            SET16(o, C10, OB16(o, OBJ_y_pos));
            anim_reset(o, 3);
            bset(lk(o, C30), 0);
        }
        if (n >= 0x78) {
            SET8(o, C2B, 0);
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_width_px, 0x20);
            SET8(o, OBJ_y_radius, 0x20);
            D(0) = 100;
            tri_jsr(S3K_HUD_AddToScore);
        }
        break;
    }
    case 8:                                                   /* egg1_04 [$20C0C6]: away he flies */
        if (OB8(o, C25)) {
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) - OBS16(o, C38)));
            uint8_t a = (uint8_t)(OB8(o, C2B) + 3);
            SET8(o, C2B, a);
            int16_t s, c;
            o_sincos(a, &s, &c);
            SET16(o, C38, s >> 5);
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + (s >> 5)));
            add_xy(o, 0x28000, 0);
            if (wx(o) >= 0xC80) {                             /* [$20C16C] */
                SET8(o, C2B, 0);
                tri_scd_level_music();
                line1(SCDA_GENEPAL);
                s_bossflag = 0;
                w8(RAM_Boss_flag, 0);
                SET8(o, OBJ_routine, 0xA);
            }
            break;
        }
        SET8(o, C2B, OB8(o, C2B) + 1);
        {
            int16_t x = wx(o), y = wy(o);
            set_xy(o, OB16(o, C3C), OB16(o, C10));            /* (explosions where the walker fell) */
            bom_set(o);
            set_xy(o, x, y);
        }
        add_xy(o, 0x8000, -0x20000);
        if (wy(o) > 0x158) break;
        SET8(o, C25, OB8(o, C25) + 1);
        anim_reset(o, 4);
        SET8(o, C2B, 0x40);
        SET16(o, C38, 8);
        break;
    case 0xA: {                                               /* egg1_05 [$20C19A]: the way on opens */
        uint16_t r = (uint16_t)(r16(RAM_Camera_max_X_pos) + 6);
        w16(RAM_Camera_max_X_pos, r);
        w16(RAM_Camera_target_max_X_pos, (uint16_t)(r16(RAM_Camera_target_max_X_pos) + 6));
        if ((int16_t)s_right <= (int16_t)r) {
            w16(RAM_Camera_max_X_pos, s_right);
            w16(RAM_Camera_target_max_X_pos, s_right);
            o_delete(o);
        }
        return 0;
    }
    case 0xC: {                                               /* egg1_06 [$20BF92]: the camera looks up */
        uint16_t b = (uint16_t)(r16(RAM_Distance_from_top) + 6);
        w16(RAM_Distance_from_top, b);
        if ((int16_t)b < 0xC8) break;
        scd_cd_play(CDDA_BOSS, 1);
        s_bossflag = 1;
        SET8(o, OBJ_routine, 0xE);
        break;
    }
    case 0xE:                                                 /* egg1_wait [$20BFB8] */
        SET8(o, C2B, OB8(o, C2B) + 1);
        if (OB8(o, C2B) != 0x3C) break;
        SET8(o, C2B, 0);
        SET8(o, OBJ_routine, 4);
        set_xy(o, 0xBD2, 0x78);
        egg1_make_act(o);
        break;
    }
    return 1;
}

static void egg1(uint32_t o)
{
    if (OB8(o, C2A)) {                                        /* egg1_anime [$20B9A6] */
        SET8(o, C2A, OB8(o, C2A) - 1);
        if (!OB8(o, C2A)) anim_reset(o, 0);
    }
    uint16_t bottom = 0x310;                                  /* egg1_jisin [$20B9CA]: the arena; stomps shake it */
    if (s_bossflag) {
        bottom = 0x100;
        if (OB8(o, C35)) {
            SET8(o, C35, OB8(o, C35) - 1);
            bottom = (uint16_t)(bottom + (OB8(o, C35) & 1 ? -1 : 1));
        }
    }
    w16(RAM_Camera_max_Y_pos, bottom);
    w16(RAM_Camera_target_max_Y_pos, bottom);
    egg1_warai_chk(o);
    egg1_hit_chk(o);
    static int log = -1;
    if (log < 0) log = getenv("TRILOGY_BOSSLOG") != NULL;     /* development */
    if (log && !(r16(RAM_Level_frame_counter) & 7))
        fprintf(stderr, "[boss] f=%u r=%02X hits=%u coll=%02X/%u egg=%04X,%04X body=%02X p=%04X,%04X st=%02X anim=%02X cam=%04X,%04X\n",
                (unsigned)r16(RAM_Level_frame_counter), OB8(o, OBJ_routine), OB8(o, C2B), OB8(o, OBJ_coll_flags),
                OB8(o, OBJ_coll_prop), OB16(o, OBJ_x_pos), OB16(o, OBJ_y_pos), lk(o, C30) ? OB8(lk(o, C30), OBJ_routine) : 0xFF,
                OB16(RAM_Player_1, OBJ_x_pos), OB16(RAM_Player_1, OBJ_y_pos), OB8(RAM_Player_1, OBJ_status),
                OB8(RAM_Player_1, OBJ_anim), r16(RAM_Camera_X_pos), r16(RAM_Camera_max_Y_pos));
    static int autohit = -1;                                  /* development: a hit every N frames */
    if (autohit < 0) autohit = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
    if (autohit > 0 && btst(o, 3) && !OB8(o, C34) && !(r16(RAM_Level_frame_counter) % autohit))
        SET8(o, OBJ_coll_flags, 0);
    if (!egg1_step(o)) return;
    o_animate(o, o_asset_anim(SCDA_EGGMAN));
    part_draw(o);
}

/* ---- $2B the body ------------------------------------------------------- */
static void chain(uint32_t a1, unsigned bit, int on)          /* [$20C8D0 / $20C8F4 / $20C918 / $20C93C] */
{
    for (int k = 0; k < 3 && a1; k++, a1 = lk(a1, C30))
        on ? bset(a1, bit) : bclr(a1, bit);
}
static void plant(uint32_t l1, int on) { chain(l1, 5, on); chain(l1, 4, on); }

static void arms_set(uint32_t o, const uint8_t r[4], int frame)   /* [$20C42E / $20C474 / $20C4BA] */
{
    uint32_t sh = lk(lk(o, C2E), C32);
    for (int k = 0; k < 2; k++) {
        uint32_t a2 = lk(sh, k ? C30 : C32);
        if (!a2) continue;
        routine(a2, r[k * 2]);
        uint32_t a3 = lk(a2, C30);
        routine(a3, r[k * 2 + 1]);
        SET8(a3, OBJ_frame, frame);
    }
}
static const uint8_t ARMS_A[4] = { 2, 8, 6, 6 }, ARMS_B[4] = { 6, 6, 2, 8 }, ARMS_C[4] = { 2, 0xA, 2, 0xA };

static void legs_speed(uint32_t o, int up)                    /* spd_normal [$20C5AE] / spd_up [$20C500] */
{
    for (int k = 0; k < 2; k++) {
        uint32_t l1 = lk(o, k ? C32 : C30), l2 = lk(l1, C30), l3 = lk(l2, C30);
        SET8(l1, C3C, up ? 8 : 2);
        SET32(l2, C3C, up ? 0x10000 : 0x8000);
        SET32(l2, C10, up ? 0x8000 : 0x4000);
        SET32(l3, C3C, up ? 0xC000 : 0x4000);
        SET32(l3, C10, up ? 0x18000 : 0x8000);
    }
    uint32_t sh = lk(lk(o, C2E), C32);
    for (int k = 0; k < 2; k++) {                             /* (fast: the forearms stop following the player) */
        uint32_t a2 = lk(sh, k ? C30 : C32);
        if (!a2) continue;
        uint32_t a3 = lk(a2, C30), a4 = lk(a3, C30);
        up ? bset(a3, 7) : bclr(a3, 7);
        up ? bset(a4, 7) : bclr(a4, 7);
    }
}

/* the legs this step: lead (CD $2C bit 6: the far one leads) and trail */
static void step_legs(uint32_t o, uint32_t *a1, uint32_t *a2)
{
    *a1 = lk(o, btst(o, 6) ? C32 : C30);
    *a2 = lk(o, btst(o, 6) ? C30 : C32);
}
static void step_done(uint32_t a1, uint32_t a2)
{
    /* (CD clears the lead's foot but reads the trail's link twice: its shin) */
    uint32_t a3 = lk(lk(a1, C30), C30), a4 = lk(a2, C30);
    bclr(a1, 0); bclr(a3, 0); bclr(a2, 0); bclr(a4, 0);
}

static void egg1body(uint32_t o)
{
    uint32_t a1, a2;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg1body_ini */
        SET8(o, OBJ_routine, 2);
        part_init(o, SCDA_EGGBODY, 0x24, 0x20);
        legs_speed(o, 0);
        break;
    case 2: {                                                 /* egg1body_01 [$20C236]: down onto its feet */
        uint32_t r1 = lk(o, C32), r2 = lk(r1, C30);
        routine(r1, 0x10);
        routine(r2, 4);
        routine(lk(r2, C30), 0xA);
        uint32_t l1 = lk(o, C30);
        chain(l1, 5, 1);
        if (btst(l1, 4)) { bset(o, 0); break; }
        add_xy(o, 0, 0x18000);
        add_xy(lk(o, C2E), 0, 0x18000);
        break;
    }
    case 4: {                                                 /* egg1body_02 [$20C282] */
        bclr(lk(o, C32), 0);
        uint32_t l1 = lk(o, C30);
        if (!bclr(l1, 0)) break;
        if (OB8(l1, OBJ_routine) == 0xC) { routine(l1, 0xA); break; }
        if (OB8(l1, OBJ_routine) == 0xA) { routine(l1, 0xE); break; }
        routine(l1, 2);
        plant(l1, 0);
        uint32_t r1 = lk(o, C32), r2 = lk(r1, C30);
        routine(r1, 8);
        plant(r1, 1);
        routine(r2, 2);
        routine(lk(r2, C30), 4);
        bset(o, 0);
        break;
    }
    case 6:                                                   /* egg1body_03 [$20C2F6] */
        if (!bset(o, 1)) arms_set(o, ARMS_B, 0);
        if (!btst(lk(o, C30), 0) || !btst(lk(o, C32), 0)) break;
        bclr(o, 1);
        bset(o, 6);
        bset(o, 0);
        break;
    case 8:                                                   /* egg1body_04 [$20C32E]: walking left */
        step_legs(o, &a1, &a2);
        if (!btst(a1, 0) || !btst(a2, 0)) break;
        step_done(a1, a2);
        if (wx(o) <= 0xB58) SET8(o, C2D, 1);
        SET8(o, C2D, OB8(o, C2D) - 1);
        if (!OB8(o, C2D)) { bset(o, 0); bclr(o, 1); break; }
        a1 = lk(o, C30); a2 = lk(o, C32);
        if (bchg(o, 6)) {
            routine(a1, 8); plant(a1, 1); routine(lk(a1, C30), 6);
            routine(a2, 2); plant(a2, 0); routine(lk(a2, C30), 8);
            arms_set(o, ARMS_A, 0);
        } else {
            routine(a1, 2); plant(a1, 0); routine(lk(a1, C30), 8);
            routine(a2, 8); plant(a2, 1); routine(lk(a2, C30), 6);
            arms_set(o, ARMS_B, 0);
        }
        break;
    case 0xA:                                                 /* egg1body_05 [$20C65C]: walking right */
        step_legs(o, &a1, &a2);
        if (!btst(a1, 0) || !btst(a2, 0)) break;
        if (bset(o, 1)) {
            step_done(a1, a2);
            if (wx(o) >= 0xBA0) SET8(o, C2D, 1);
            SET8(o, C2D, OB8(o, C2D) - 1);
            if (!OB8(o, C2D)) { bclr(o, 1); bset(o, 0); break; }
        } else
            legs_speed(o, 0);
        a1 = lk(o, C30); a2 = lk(o, C32);
        if (bchg(o, 6)) {
            routine(a1, 0x16); plant(a1, 1); routine(lk(a1, C30), 8);
            routine(a2, 0x12); plant(a2, 0); routine(lk(a2, C30), 6);
            arms_set(o, ARMS_B, 0);
        } else {
            routine(a1, 0x12); plant(a1, 0); routine(lk(a1, C30), 6);
            routine(a2, 0x16); plant(a2, 1); routine(lk(a2, C30), 8);
            arms_set(o, ARMS_A, 0);
        }
        break;
    case 0xC:                                                 /* egg1body_06 [$20C76C]: the fast stomp */
        step_legs(o, &a1, &a2);
        if (!bset(o, 1)) legs_speed(o, 1);
        if (OB8(a1, OBJ_routine) == 4) {
            routine(lk(a1, C30), 0xA);
            routine(lk(a2, C30), 0xC);
        }
        if (!btst(a1, 0) || !btst(a2, 0)) break;
        step_done(a1, a2);
        SET8(o, C2D, OB8(o, C2D) - 1);
        if (!OB8(o, C2D)) { bset(o, 0); bclr(o, 1); break; }
        a1 = lk(o, C30); a2 = lk(o, C32);
        {
            uint32_t up = bchg(o, 6) ? a1 : a2, dn = up == a1 ? a2 : a1;
            bclr(up, 1); plant(up, 1);
            uint32_t s2 = lk(up, C30);
            routine(s2, 0xA);
            routine(lk(s2, C30), 2);
            bclr(dn, 1); routine(dn, 2); plant(dn, 0);
            routine(lk(dn, C30), 0xC);
            arms_set(o, ARMS_C, 1);
        }
        break;
    case 0xE:                                                 /* egg1body_07 [$20C8A6]: wrecked, it sinks */
        if (btst(o, 0)) { o_delete(o); return; }
        if (o_floor_dist(o) > 0) {
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + 2));
            uint32_t c = lk(o, C2E);
            SET16(c, OBJ_y_pos, (uint16_t)(wy(c) + 2));
        }
        break;
    case 0x10:                                                /* egg1body_08 [$20C21C]: a pause */
        SET8(o, C2D, OB8(o, C2D) - 1);
        if (OB8(o, C2D)) break;
        legs_speed(o, 0);
        bset(o, 0);
        bclr(o, 1);
        break;
    }
    part_draw(o);
}

/* ---- $2C the hip: swings its leg by angle C2A --------------------------- */
static uint32_t foot_of(uint32_t o) { return lk(lk(o, C30), C30); }

static void leg1_pos(uint32_t o)                              /* [$20D132] */
{
    int16_t s, c;
    o_sincos(OB8(o, C2A), &s, &c);
    int16_t d0 = s >> 4, d1 = c >> 4;
    if (btst(o, 4)) {
        if (btst(o, 5)) {                                     /* (planted: the swing moves the body) */
            if (!bset(o, 1)) { SET16(o, C34, d1); SET16(o, C38, d0); }
            int16_t d3 = (int16_t)(d1 - OBS16(o, C34)), d2 = (int16_t)(d0 - OBS16(o, C38));
            SET16(o, C34, d1);
            SET16(o, C38, d0);
            uint32_t b = lk(o, C2E);
            set_xy(b, wx(b) - d3, wy(b) - d2);
            b = lk(b, C2E);
            set_xy(b, wx(b) - d3, wy(b) - d2);
            return;
        }
    } else
        bclr(o, 1);
    uint32_t b = lk(o, C2E);
    set_xy(o, wx(b) + 0xC + d1 - (btst(o, 2) ? 0xA : 0), wy(b) + 0x14 + d0);
}

static void leg1_0A(uint32_t o)                               /* [$20CF58] */
{
    uint32_t l3 = foot_of(o);
    if (OB8(l3, OBJ_routine) != 8) routine(l3, 6);
    else if (!OB8(o, C2A)) {
        if (bclr(l3, 0)) bset(o, 0);
        leg1_pos(o);
        return;
    }
    if (OB8(o, C2A)) SET8(o, C2A, OB8(o, C2A) - OB8(o, C3C));
    leg1_pos(o);
}

static void leg1_0C(uint32_t o)                               /* [$20CFA2] */
{
    uint32_t l3 = foot_of(o);
    if (OB8(l3, OBJ_routine) != 4) routine(l3, 2);
    else if ((int8_t)OB8(o, C2A) >= 0x58) {
        if (bclr(l3, 0)) bset(o, 0);
        leg1_pos(o);
        return;
    }
    if ((int8_t)OB8(o, C2A) < 0x58) SET8(o, C2A, OB8(o, C2A) + OB8(o, C3C));
    leg1_pos(o);
}

static void egg1leg1(uint32_t o)
{
    uint32_t l3 = foot_of(o);
    uint8_t a = OB8(o, C2A), sp = OB8(o, C3C);
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg1leg1_ini */
        part_init(o, SCDA_EGGLEG1, 8, 8);
        SET8(o, OBJ_routine, 0xC);
        SET8(o, C2A, 0x58);
        SET8(o, C3C, 2);
        break;
    case 2:                                                   /* [$20CE8C] back */
        if (OB8(l3, OBJ_routine) != 6 && OB8(l3, OBJ_routine) != 8) { bclr(l3, 0); routine(l3, 6); }
        if ((int8_t)a > 0) {
            a = (uint8_t)(a - sp);
            SET8(o, C2A, a);
            if ((int8_t)a > 0) { leg1_pos(o); break; }
        }
        SET8(o, C2A, 0);
        if (OB8(l3, OBJ_routine) == 8 || bclr(l3, 0)) SET8(o, OBJ_routine, 4);
        leg1_pos(o);
        break;
    case 4:                                                   /* [$20CEE8] */
        a = (uint8_t)(a + sp);
        SET8(o, C2A, a);
        if (a >= 0x58) { SET8(o, C2A, 0x58); SET8(o, OBJ_routine, 6); routine(l3, 2); }
        leg1_pos(o);
        break;
    case 6:                                                   /* [$20CF16] */
        if (btst(l3, 4)) bset(o, 0);
        leg1_pos(o);
        break;
    case 8:                                                   /* [$20CF30] */
        if (!btst(o, 0)) {
            a = (uint8_t)(a - sp);
            SET8(o, C2A, a);
            if (a < 0x18) { SET8(o, C2A, 0x18); bset(o, 0); }
        }
        leg1_pos(o);
        break;
    case 0xA: leg1_0A(o); break;
    case 0xC: leg1_0C(o); break;
    case 0xE:                                                 /* [$20CFEC] */
        if ((int8_t)a < 0x18) {
            SET8(o, C2A, a + sp);
            bset(l3, 7);
            bset(foot_of(lk(lk(o, C2E), C32)), 7);
            leg1_0C(o);
        } else if ((int8_t)a > 0x18) {
            SET8(o, C2A, a - sp);
            leg1_0A(o);
        } else {
            if (btst(l3, 0)) bset(o, 0);
            leg1_pos(o);
        }
        break;
    case 0x10: {                                              /* [$20D04C] the far hip copies the near */
        uint32_t m = lk(lk(o, C2E), C30);
        set_xy(o, wx(m) - 0xA, wy(m));
        SET16(o, C2A, OB16(m, C2A));
        break;
    }
    case 0x12:                                                /* [$20D06E] */
        if (OB8(l3, OBJ_routine) != 6) {
            if (OB8(l3, OBJ_routine) != 8) { bclr(l3, 0); routine(l3, 6); }
            else {
                if (a != 0x20) {
                    a = (uint8_t)(a - sp);
                    SET8(o, C2A, a);
                    if ((int8_t)a > 0) { leg1_pos(o); break; }
                }
                SET8(o, C2A, 0x20);
                bclr(l3, 0);
                SET8(o, OBJ_routine, 0x14);
            }
        }
        leg1_pos(o);
        break;
    case 0x14:                                                /* [$20D0C0] */
        a = (uint8_t)(a + sp);
        SET8(o, C2A, a);
        if ((int8_t)a >= 0x30) { SET8(o, C2A, 0x30); SET8(o, OBJ_routine, 6); routine(l3, 2); }
        leg1_pos(o);
        break;
    case 0x16:                                                /* [$20D0EE] */
        if (!btst(o, 0) && OB8(l3, OBJ_routine) != 2) {
            if (OB8(l3, OBJ_routine) != 4) routine(l3, 2);
            else {
                a = (uint8_t)(a + sp);
                SET8(o, C2A, a);
                if ((int8_t)a >= 0x50) bset(o, 0);
            }
        }
        leg1_pos(o);
        break;
    case 0x18:                                                /* [$20D1CC] off */
        if (fall_off(o, 0, -0x28000, btst(o, 4) ? -0x600 : 0x600, 0x1860)) return;
        break;
    }
    part_draw(o);
}

/* ---- $2D the shin --------------------------------------------------------- */
static void leg2_up(uint32_t o, int32_t d1, int32_t d2)       /* [$20D3AC] planted: the hip, body, Eggman move */
{
    if (!btst(o, 5)) return;
    for (uint32_t a3 = lk(o, C2E), k = 0; k < 3 && a3; k++, a3 = lk(a3, C2E))
        add_xy(a3, d1, d2);
}
static void leg2_pos(uint32_t o)                              /* [$20D3DE] */
{
    uint32_t b = lk(o, C2E);
    set_xy(o, wx(b) + 4 + OBS16(o, C34), wy(b) + 0x10 + OBS16(o, C38));
}

static void egg1leg2(uint32_t o)
{
    int32_t d1, d2;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg1leg2_ini */
        SET8(o, OBJ_routine, 2);
        part_init(o, SCDA_EGGLEG2, 8, 0x14);
        SET32(o, C3C, 0x8000);
        SET32(o, C10, 0x4000);
        break;
    case 2: {                                                 /* [$20D2AE] */
        uint32_t b = lk(o, C2E);
        set_xy(o, wx(b) + 4, wy(b) + 0x10);
        break;
    }
    case 4: {                                                 /* [$20D2CA] the far shin copies the near */
        uint32_t m = lk(lk(lk(lk(o, C2E), C2E), C30), C30);
        set_xy(o, wx(m) - 0xA, wy(m));
        break;
    }
    case 6:                                                   /* [$20D2EE] */
        if (OBS16(o, C34) < 8) {
            d1 = L(o, C3C);
            addL(o, C34, d1);
            if (btst(o, 4)) { leg2_up(o, -d1, 0); break; }
        }
        leg2_pos(o);
        break;
    case 8:                                                   /* [$20D312] */
        SET16(o, C38, 0);
        if (OBS16(o, C34) > -8) {
            d1 = L(o, C3C);
            addL(o, C34, -d1);
            if (btst(o, 4)) { leg2_up(o, d1, 0); break; }
        }
        leg2_pos(o);
        break;
    case 0xA:                                                 /* [$20D33A] */
        d1 = d2 = 0;
        if (OBS16(o, C34) > -8) { d1 = L(o, C3C); addL(o, C34, -d1); }
        if (OBS16(o, C38) > -4) { d2 = L(o, C10); addL(o, C38, -d2); }
        if (btst(o, 4)) leg2_up(o, d1, d2);
        else leg2_pos(o);
        break;
    case 0xC:                                                 /* [$20D372] */
        d1 = d2 = 0;
        if (OBS16(o, C34) > -8) { d1 = L(o, C3C); addL(o, C34, -d1); }
        if (OBS16(o, C38) < 4) { d2 = L(o, C10); addL(o, C38, d2); }
        if (btst(o, 4)) leg2_up(o, d1, -d2);
        else leg2_pos(o);
        break;
    case 0xE:                                                 /* [$20D40A] off */
        if (fall_off(o, 0, -0x20000, btst(o, 4) ? -0x600 : 0x600, 0x1A60)) return;
        break;
    }
    part_draw(o);
}

/* ---- $2E the foot ---------------------------------------------------------- */
static void leg3_pos(uint32_t o)                              /* [$20D67A] */
{
    uint32_t b = lk(o, C2E);
    set_xy(o, wx(b) - 0xB + OBS16(o, C34), wy(b) + 0xE + OBS16(o, C38));
}

static void leg3_move(uint32_t o)                             /* [$20D620] planted: the rest moves */
{
    if (!btst(o, 4)) { leg3_pos(o); return; }
    if (!btst(o, 5)) return;
    int32_t d1 = L(o, C3C), d2 = L(o, C10);
    if (OB8(o, OBJ_routine) != 6) { d1 = -d1; d2 = -d2; }
    for (uint32_t a3 = lk(o, C2E), k = 0; k < 4 && a3; k++, a3 = lk(a3, C2E))
        add_xy(a3, -d1, d2);
}

static void leg3_landed(uint32_t o)                           /* [$20D556] stomp */
{
    bset(o, 4);
    uint32_t a3 = lk(o, C2E);
    bset(a3, 4);
    a3 = lk(a3, C2E);
    bset(a3, 4);
    a3 = lk(lk(a3, C2E), C2E);
    if (a3) SET8(a3, C35, 8);
    tri_scd_voice(SCD_PCM_STOMP);
}

static void egg1leg3(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg1leg3_ini */
        part_init(o, SCDA_EGGLEG3, 0x20, 0x14);
        SET32(o, C3C, 0x4000);
        SET32(o, C10, 0x8000);
        SET8(o, OBJ_routine, 4);
        bset(o, 0);
        SET16(o, C34, 0xFFF8);
        SET16(o, C38, 0x10);
        leg3_pos(o);
        break;
    case 2: {                                                 /* [$20D4FE] down */
        addL(o, C34, -L(o, C3C));
        addL(o, C38, L(o, C10));
        int lim = btst(o, 4) && btst(o, 7) ? 0xC : 0x10;
        if (OBS16(o, C38) >= lim) { bset(o, 0); SET8(o, OBJ_routine, 4); break; }
        leg3_move(o);
        if (o_floor_dist(o) <= 0) leg3_landed(o);
        break;
    }
    case 4:                                                   /* [$20D5C6] */
        if (!btst(o, 4)) {
            leg3_pos(o);
            if (o_floor_dist(o) <= 0) leg3_landed(o);
        }
        break;
    case 6:                                                   /* [$20D58A] up */
        addL(o, C34, L(o, C3C));
        addL(o, C38, -L(o, C10));
        if (OBS16(o, C38) <= 0) {
            SET32(o, C34, 0);
            SET32(o, C38, 0);
            bset(o, 0);
            SET8(o, OBJ_routine, 8);
        }
        leg3_move(o);
        break;
    case 8: leg3_pos(o); break;                               /* [$20D5E2] */
    case 0xA: {                                               /* [$20D5E8] the far foot copies the near */
        uint32_t m = lk(lk(lk(lk(lk(lk(o, C2E), C2E), C2E), C30), C30), C30);
        set_xy(o, wx(m) - 0xA, wy(m));
        SET16(o, C34, OB16(m, C34));
        SET16(o, C38, OB16(m, C38));
        break;
    }
    case 0xC:                                                 /* [$20D6A8] off */
        if (fall_off(o, 0, -0x23000, btst(o, 4) ? -0x660 : 0x660, 0x1660)) return;
        break;
    }
    part_draw(o);
}

/* ---- $2F the shoulder; $30-$32 an arm --------------------------------------- */
static void egg1arm1(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        part_init(o, SCDA_EGGARM1, 0x10, 0xC);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* [$20C9CE] an arm comes off when hit */
        uint32_t b = lk(o, C2E);
        set_xy(o, wx(b) + 0x18, wy(b) - 0xC);
        if (bclr(o, 6)) {
            routine(lk(o, C30), 0xA);
            SET16(o, C30, 0);
        } else if (bclr(o, 5)) {
            uint32_t a2 = lk(o, C32);
            routine(a2, 0xA);
            SET16(o, C32, 0);
            SET8(a2, OBJ_frame, 0);
        }
        break;
    }
    case 4:                                                   /* [$20CA2A] off */
        if (fall_off(o, -0x10000, -0x20000, -0x600, 0x1800)) return;
        break;
    }
    part_draw(o);
}

static void arm2_pos(uint32_t o)                              /* [$20CB6E] */
{
    uint32_t b = lk(o, C2E);
    int16_t s, c;
    o_sincos((uint8_t)(OB8(o, C2A) + 0x40), &s, &c);
    set_xy(o, wx(b) + (c >> 4) - (btst(o, 2) ? 0xA : 0), wy(b) + (s >> 4));
}

static void egg1arm2(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        part_init(o, SCDA_EGGARM2, 8, 8);
        SET8(o, OBJ_routine, 6);
        /* fall through: (CD runs routine 2's code once) */
    case 2: {                                                 /* [$20CACC] */
        if (orphan(o)) return;
        uint8_t a = OB8(o, C2A);
        SET8(o, C2A, a - 2);
        if (a <= 2) { SET8(o, OBJ_routine, 4); SET8(o, C2A, 0); bset(o, 0); }
        arm2_pos(o);
        break;
    }
    case 6: {                                                 /* [$20CAE8] */
        if (orphan(o)) return;
        uint8_t a = (uint8_t)(OB8(o, C2A) + 2);
        SET8(o, C2A, a);
        if (a >= 0x30) { SET8(o, OBJ_routine, 8); SET8(o, C2A, 0x30); bset(o, 0); }
        arm2_pos(o);
        break;
    }
    case 0xA:                                                 /* [$20CB10] off */
        if (!btst(o, 1)) { SET8(o, C2B, 0); routine(lk(o, C30), 4); }
        if (fall_off(o, 0x10000, -0x20000, 0x600, 0x1F00)) return;
        break;
    default:                                                  /* (4, 8) */
        if (orphan(o)) return;
        arm2_pos(o);
        break;
    }
    if (!btst(o, 2)) part_draw(o);                            /* (the far upper arm: behind the body) */
}

static void arm3_pos(uint32_t o)                              /* (arm2's place) */
{
    uint32_t b = lk(o, C2E);
    set_xy(o, wx(b) - 0x24 + OBS16(o, C34), wy(b));
}

static void egg1arm3(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        part_init(o, btst(o, 2) ? SCDA_EGGARM3B : SCDA_EGGARM3, 0x20, 8);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                                   /* [$20CBEC] the hand follows the player's height */
    track:
        if (orphan(o)) return;
        arm3_pos(o);
        if (!btst(o, 7) && wy(RAM_Player_1) <= wy(o)) {
            if (OBS16(o, C38) > -8) addL(o, C38, -0x10000);
        } else if (OBS16(o, C38) < 8)
            addL(o, C38, 0x10000);
        SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + OBS16(o, C38)));
        break;
    case 4:                                                   /* [$20CCCE] off */
        if (!btst(o, 1)) {
            SET8(o, C2B, 1);
            SET8(o, C2A, 0);
            routine(lk(o, C30), 4);
        }
        if (fall_off(o, 0, 0x10000, -0x620, 0x1220)) return;
        SET8(o, C2A, OB8(o, C2A) + 1);
        if (!(OB8(o, C2A) % 7)) explode(o, 0, 0);
        break;
    case 6:                                                   /* [$20CC52] */
        if (OBS16(o, C34) > 0) addL(o, C34, -0x8000);
        goto track;
    case 8:                                                   /* [$20CC64] */
        if (OBS16(o, C34) < 0x10) addL(o, C34, 0x8000);
        goto track;
    case 0xA:                                                 /* [$20CC78] reaching out */
        if (orphan(o)) return;
        if (OBS16(o, C34) < 8) addL(o, C34, 0x8000);
        else SET8(o, OBJ_frame, 1);
        arm3_pos(o);
        if (OBS16(o, C38) < 8) addL(o, C38, 0x10000);
        SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + OBS16(o, C38)));
        break;
    }
    part_draw(o);
}

static void egg1arm4(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        part_init(o, SCDA_EGGARM4, 0xC, 0x10);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* [$20CD8C] */
        if (orphan(o)) return;
        uint32_t b = lk(o, C2E);
        int x = wx(b) - 0xC;
        uint8_t f = OB8(b, OBJ_frame);
        if (f) x += f == 1 ? 8 : 0x10;
        if (bclr(o, 4)) x -= 4;                               /* (just hit: jolted back) */
        set_xy(o, x, wy(b));
        break;
    }
    case 4:                                                   /* [$20CDCA] off */
        if (!btst(o, 1)) { SET8(o, C2B, 0); set_coll(o, 0, 0); }
        if (fall_off(o, 0, -0x18000, -0x500, 0x1800)) return;
        break;
    }
    part_draw(o);
}

/* ---- $15 the capsule: a rolling player breaks it, and flowers fly ------- */
static int capsule_touch(uint32_t o, uint32_t p)              /* [$20B116] */
{
    if (!(OB8(p, OBJ_status) & 4)) return 0;
    int d1 = (int8_t)OB8(p, OBJ_x_radius) + 0x20;
    int16_t d0 = (int16_t)(wx(p) - wx(o) + d1);
    if (d0 < 0 || d0 >= d1 * 2) return 0;
    d1 = (int8_t)OB8(p, OBJ_y_radius) + 0x1C;
    d0 = (int16_t)(wy(p) - wy(o) + d1);
    return d0 >= 0 && d0 < d1 * 2;
}

void cd_capsule(uint32_t o)                       /* (Wacky Workbench's act 3 too) */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20AF5C] */
        o_init(o, SCDA_CAPSULE, OB8(o, OBJ_render) | 4, 4, 0x20, 0x20);
        SET8(o, OBJ_x_radius, 0x20);
        SET8(o, OBJ_y_radius, 0x18);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* [$20AF8C] */
        o_animate(o, o_asset_anim(SCDA_CAPSULE));
        if (!capsule_touch(o, p)) break;
        w8(RAM_Update_HUD_timer, 0);
        SET8(o, OBJ_frame, 2);
        SET8(o, C2A, 0x78);
        SET8(o, OBJ_routine, 4);
        int d1 = (int8_t)OB8(p, OBJ_x_radius) + 0x20;
        int16_t d0 = (int16_t)(wx(p) - wx(o) + d1);
        if (d0 >= 0 && d0 < d1 * 2) SET16(p, OBJ_y_vel, (uint16_t)(-OBS16(p, OBJ_y_vel) >> 2));
        else SET16(p, OBJ_x_vel, (uint16_t)(-OBS16(p, OBJ_x_vel) >> 2));
        break;
    }
    case 4: {                                                 /* [$20AFF0] explosions, then the seeds */
        static const int8_t spot[8][2] = {
            { 0, 0 }, { 32, -8 }, { -32, 0 }, { -24, -8 }, { 24, 8 }, { -16, 8 }, { 16, 8 }, { -8, -8 },
        };
        int8_t t = (int8_t)(OB8(o, C2A) - 1);
        SET8(o, C2A, t);
        if (t < 0) {                                          /* [$20B06C] */
            static const int16_t vx[7] = { 0, -0x80, 0x80, -0x100, 0x100, -0x180, 0x180 };
            tri_scd_load_palette(TRI_PAL_CD);                 /* (the level's own colours again) */
            for (int k = 0; k < 7; k++) {
                uint32_t a1 = o_alloc();
                if (!a1) break;
                SET32(a1, OBJ_code, r32(o + OBJ_code));
                set_xy(a1, wx(o), wy(o));
                SET8(a1, OBJ_routine, 0xA);
                o_init(a1, SCDA_CAPSULE, 4, 4, 0x20, 0x20);
                SET8(a1, OBJ_anim, 1);
                SET16(a1, OBJ_y_vel, -0x600);
                SET16(a1, OBJ_x_vel, vx[k]);
            }
            SET8(o, OBJ_routine, 6);
            SET8(o, C2A, 0x3C);
            break;
        }
        if (t & 3) break;
        int k = (t >> 2) & 7;
        explode(o, spot[k][0], spot[k][1]);
        break;
    }
    case 6:                                                   /* [$20B53C] then the results (S3K's, as at a sign) */
        if (OB8(o, C2A)) { SET8(o, C2A, OB8(o, C2A) - 1); return; }
        if (OB8(p, OBJ_status) & 2) return;
        SET8(o, OBJ_routine, 8);
        A(1) = RAMPTR(p);
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Set_PlayerEndingPose);
        {
            uint32_t a1 = o_alloc();
            if (a1) SET32(a1, OBJ_code, S3K_Obj_LevelResults);
        }
        return;
    case 8:
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Check_TailsEndPose);
        return;
    case 0xA:                                                 /* [$20B0E6] a seed, in the air */
        o_animate(o, o_asset_anim(SCDA_CAPSULE));
        o_fall(o);
        if (o_floor_dist(o) < 0) {
            SET32(o, OBJ_code, native3(0x1F));
            SET8(o, OBJ_subtype, 1);
            SET8(o, OBJ_routine, 0);
            return;
        }
        break;
    }
    o_draw(o);
}

/* $1F a flower: a seed falls, grows (line 0), blooms (line 1). Subtype 1:
 * the capsule's (the level's own are remembered by CD; none in this act) */
static void flower(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$205E6E] */
        o_init(o, SCDA_FLOWER, OB8(o, OBJ_render) | 4, 1, 8, 0x10);
        SET8(o, OBJ_y_radius, 0);
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_anim + 1, 2);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* [$205ECA] */
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + d1));
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_anim, 1);
            SET8(o, OBJ_y_radius, 0x30);
        } else
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + 2));
        break;
    }
    case 6:                                                   /* [$205F6A] */
        o_asset(o, SCDA_FLOWER2);
        SET8(o, OBJ_anim, 2);
        break;
    case 8:                                                   /* [$205F78] */
        SET8(o, OBJ_anim, 3);
        SET8(o, OBJ_routine, 4);
        break;
    }
    o_animate(o, o_asset_anim(SCDA_FLOWER));
    o_draw(o);
}

void scd_ppz3_level_entry(void) { s_bossflag = 0; }

void scd_ppz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x15] = cd_capsule;
    t[base3 + 0x1F] = flower;
    t[base3 + 0x2A] = egg1;
    t[base3 + 0x2B] = egg1body;
    t[base3 + 0x2C] = egg1leg1;
    t[base3 + 0x2D] = egg1leg2;
    t[base3 + 0x2E] = egg1leg3;
    t[base3 + 0x2F] = egg1arm1;
    t[base3 + 0x30] = egg1arm2;
    t[base3 + 0x31] = egg1arm3;
    t[base3 + 0x32] = egg1arm4;
}
