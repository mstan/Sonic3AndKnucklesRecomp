/*
 * scd_ttz_boss.c — Tidal Tempest act 3 (Sonic CD R43C / R43D): the chase
 * after Eggman's pod, then the bubble-shield boss underwater. Transcribed
 * from the Mega-CD program (the routines' R43C addresses in brackets; names
 * from the PC port's BOSS_4.C / BOSS_4_2.C; R43D's code is the same).
 *
 *   $4A the pod     waits till player 1 passes x $478, then flies a loop of
 *                   19 steps through the shaft (down past the level's 2048 px
 *                   wrap), bobbing; it slows to wait for a far player and
 *                   speeds up near one. Four hits: after the fourth it smokes
 *                   and, at the next branch, leaves the loop for one of two
 *                   ways out (closing the gate behind it), dives at x $878
 *                   and is gone (the boss's art comes in: here resident)
 *   $4B Eggman in it  follows it (hurt face on a hit); it makes the two
 *                   gates ($47, subtype 1: scd_ttz_blk2.c); once the pod is
 *                   gone it waits for player 1 at the arena (x >= $840,
 *                   y >= $550): bossFlag, the gates go, so does it
 *   $4C Eggman      the arena locks (2480) as player 1 comes; at x $A50 the
 *                   boss music, and he blows 16 bubbles ($4F) that rise and
 *                   orbit him; then he swims half ellipses to and fro, firing
 *                   four shots ($4E) on each; the ring flips over at each end
 *                   and re-forms (evenly spaced, he laughs) as it thins. One
 *                   hit (through the ring) beats him: explosions, he flees
 *                   right, the camera opens, the level's music and genecolor
 *   $4D his machine  follows him
 *   $4E a shot      rides 45 frames at its place on him, then flies (hurts)
 *   $4F a bubble    rises from y $5B8 (subtype-like flag: one that only rises
 *                   and goes), joins the ring; rolled into, it pops: the
 *                   player gets air and is knocked down and away
 * Also placed in act 3: $15 the capsule (Palmtree Panic's, scd_ppz_boss.c),
 * $2F animals (scd_ttz_badnik.c's, the same code), $34 / $47 blocks and the
 * chase gate (scd_ttz_blk2.c), $16 the goal post (scd_obj.c).
 *
 * State the disc keeps: bossstart ($F7AA: 4 from the chase till the boss has
 * fled; the bubbles' and shots' touch need it), bossflag ($F7A7: the head's,
 * the camera's bottom), bossMusic. S3K's Boss_flag (the same address) is set
 * while the arena is locked (its right bound for the player, no jingles).
 * The level's events (zone43chk) run here too: nothing else in act 3 does.
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (named below), CD's $10 long
 * S3K's $18, CD's $25 S3K's $2D. Collision: the pod and Eggman are CD enemies
 * with hits left ($21), S3K's boss touch: Touch_Enemy clears the type on a
 * hit, as CD's does, and they poll. A bubble ($FF: special) is S3K's special
 * touch, which counts touches in collision_property; a shot ($FE: special,
 * it hurts while bossstart is 4) is S3K's hurt touch.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include <stdlib.h>
#include "scd.h"


#define TTZ_ROUND       2              /* (ROUND_INDEX: Tidal Tempest) */
#define P_ROUTINE       0x05
#define P_ANIM          0x20
#define P_AIR           0x2C           /* air_left */
#define P_INVULN        0x34
#define P_JUMPING       0x40
#define P_DEF_YRAD      0x44           /* default_y_radius */
#define P_DEF_XRAD      0x45

#define FM_BOSSHIT      0xAC           /* (egg4air_hitchk: the pod hit) */
#define FM_EXPLODE      0x9E           /* (egg4_make_bom: the pod smokes; bom_set: Eggman's blasts) */
#define FM_BLOW         0xB6           /* (egg4_awademo1: he blows a bubble; the drowning sound) */
#define FM_BREATHE      0xAD           /* (pcolspecial: a bubble breathed in, a big bubble's) */

/* the pod ($4A) */
#define PD_BOMB         0x30           /* oVar2A: smoke timer */
#define PD_REARM        0x31           /* oVar2B: its touch back in */
#define PD_SLOW         0x32           /* oVar2C: its speeds back in */
#define PD_FLASH        0x33           /* oVar2D: hit flash */
#define PD_FAR          0x34           /* oVar2E: bit 5 waiting for a far player */
#define PD_ANG          0x35           /* oVar2F: its bob */
#define PD_HEAD         0x36           /* oVar30: Eggman in it (word) */
#define PD_BOB          0x3E           /* oVar38 (word) */
#define PD_SPD          0x42           /* oVar3C: speed along x or y (long) */
#define PD_DSPD         0x18           /* (CD $10: speed on a diagonal, long) */
#define PD_STEP         0x2D           /* (CD $25: bit 7 way out 1, 6 way out 2; the step) */
/* Eggman in it ($4B) */
#define HD_HURT         0x30           /* oVar2A */
#define HD_POD          0x36           /* oVar30 (word) */
#define HD_GATE1        0x38           /* oVar32 (word) */
#define HD_GATE2        0x3A           /* oVar34 (word) */
#define W4_GO           0x3F           /* (the gate's: scd_ttz_blk2.c) */
/* Eggman ($4C) */
#define EG_TIME         0x30           /* oVar2A */
#define EG_LAUGH        0x31           /* oVar2B */
#define EG_FLAGS        0x32           /* oVar2C: bit 3 going right, 4 the ring flips, 5 it re-forms,
                                        * 6 a bubble popped this frame, 7 beaten */
#define EG_COUNT        0x33           /* oVar2D: bubbles */
#define EG_MECA         0x36           /* oVar30 (word) */
#define EG_SLOT         0x38           /* oVar32: the re-formed ring's next place */
#define EG_STEP         0x39           /* oVar33: awaposi_cnt_tbl's */
/* (posiset: Eggman's and a bubble's) */
#define PS_ANG          0x3A           /* oVar34 (word) */
#define PS_DX           0x3E           /* oVar38 (word) */
#define PS_DY           0x40           /* oVar3A (word) */
#define PS_RX           0x42           /* oVar3C (word; a bubble's long) */
#define PS_RY           0x18           /* (CD $10 likewise) */
/* the parts */
#define C_PARENT        0x34           /* oVar2E: Eggman (word) */
#define B_FLAGS         0x32           /* oVar2C: bit 1 its touch has been set */
#define B_INDEX         0x33           /* oVar2D: its place in the ring */
#define B_ASPD          0x3C           /* oVar36 (word) */
#define B_PHASE         0x2D           /* (CD $25: one that only rises; ychg's half) */
#define T_TIME          0x30           /* oVar2A */
#define T_VX            0x42           /* oVar3C (long) */
#define T_VY            0x18           /* (CD $10, long) */

void tri_scd_level_music(void);
void cd_capsule(uint32_t o);           /* scd_ppz_boss.c: Palmtree Panic's $15, the same code */
void scd_ttz_animal(uint32_t o);       /* scd_ttz_badnik.c: act 3's $2F is the same code */

static int s_base3;
static uint8_t s_start;                /* bossstart */
static uint8_t s_flag;                 /* bossflag */
static uint8_t s_music;                /* bossMusic */
static uint16_t s_right;               /* (the level's right edge: CD's scr_dir_tbl) */

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }
static uint32_t lk(uint32_t o, unsigned f)
{
    uint16_t w = o ? OB16(o, f) : 0;
    return w ? 0xFF0000u | w : 0;
}
static void set_lk(uint32_t o, unsigned f, uint32_t a) { SET16(o, f, (uint16_t)a); }
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }
static int32_t L(uint32_t o, unsigned f) { return (int32_t)r32(o + f); }
static void addL(uint32_t o, unsigned f, int32_t d) { SET32(o, f, (uint32_t)(L(o, f) + d)); }
static uint8_t flags(uint32_t o) { return OB8(o, EG_FLAGS); }

static void anim_reset(uint32_t o, int anim)
{
    SET8(o, OBJ_frame, 0);
    SET8(o, OBJ_anim_frame, 0);
    SET8(o, OBJ_anim_timer, 0);
    SET8(o, OBJ_anim, anim);
}

/* the size fields: CD's $19 (display width) and $16 (height radius) */
static void init(uint32_t o, int asset, int prio, int width, int yrad)
{
    o_init(o, asset, 4, prio, width, yrad > 0x20 ? yrad : 0x20);
    SET8(o, OBJ_y_radius, yrad);
}

static void set_coll(uint32_t o, uint8_t cd) { SET8(o, OBJ_coll_flags, tri_scd_coll(cd)); }

static void part_draw(uint32_t o)
{
    o_touch(o);                                               /* (when it has a touch type) */
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

static void explode(uint32_t o, int dx, int dy)               /* (CD's $18, st oRoutine2: no flower) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);
    set_xy(a1, wx(o) + dx, wy(o) + dy);
    o_sfx_cd(FM_EXPLODE);
}

/* (CD reads a deleted parent's cleared slot; S3K reuses slots: a part whose
 * Eggman is gone goes too) */
static uint32_t parent(uint32_t o)
{
    uint32_t e = lk(o, C_PARENT);
    if (e && r32(e + OBJ_code) == native3(0x4C)) return e;
    o_delete(o);
    return 0;
}

/* ---- $4A the pod ------------------------------------------------------- */
typedef struct { uint8_t vec, etc; int16_t x, y; } Step;
static const Step route1[19] = {                              /* [$20C062] egg4_air_tbl1: the loop */
    { 5, 0, 1232, 1056 }, { 7, 0, 1248, 1072 }, { 5, 0, 1248, 1120 }, { 0, 2, 1596, 1120 },
    { 5, 0, 1596, 1312 }, { 1, 0, 1376, 1312 }, { 5, 0, 1376, 1496 }, { 7, 0, 1396, 1516 },
    { 5, 0, 1396, 2176 }, { 5, 0, 1396, 296 }, { 7, 0, 1528, 428 }, { 0, 0, 1652, 428 },
    { 7, 0, 1732, 508 }, { 5, 0, 1732, 624 }, { 7, 0, 1764, 656 }, { 5, 1, 1764, 688 },
    { 5, 0, 1764, 748 }, { 6, 0, 1696, 816 }, { 1, 0, 1232, 816 },
};
static const Step route2[3] = {                               /* [$20C0D4]: out past gate 1 */
    { 0, 0, 1952, 688 }, { 7, 0, 2000, 736 }, { 0, 3, 2168, 736 },
};
static const Step route3[4] = {                               /* [$20C0E6]: out past gate 2 */
    { 2, 0, 1596, 1088 }, { 0, 0, 1856, 1088 }, { 4, 0, 1956, 988 }, { 0, 3, 2168, 988 },
};

static void speeds(uint32_t o, int32_t spd, int32_t dspd) { SET32(o, PD_SPD, spd); SET32(o, PD_DSPD, dspd); }

static void yure(uint32_t o)                                  /* [$20BEC4] a bob */
{
    uint8_t a = (uint8_t)(OB8(o, PD_ANG) + 2);
    SET8(o, PD_ANG, a);
    int16_t s, c;
    o_sincos(a, &s, &c);
    int16_t b = (int16_t)(s >> 5);
    SET16(o, PD_BOB, b);
    SET16(o, OBJ_y_pos, wy(o) + b);
}
static void yurenai(uint32_t o) { SET16(o, OBJ_y_pos, wy(o) + OBS16(o, PD_BOB)); }   /* [$20BEE0] */

static void gate_go(uint32_t head, unsigned f)
{
    uint32_t g = lk(head, f);
    if (g && r32(g + OBJ_code) == native3(0x47)) SET8(g, W4_GO, 1);
}

static void step_done(uint32_t o, const Step *e)              /* [$20BD6C] */
{
    if (!e->etc || OB8(o, OBJ_coll_prop)) {                   /* (hits left: round the loop) */
        uint8_t r = (uint8_t)(OB8(o, PD_STEP) + 1);
        if ((r & 0x3F) >= 19) r &= 0xC0;
        SET8(o, PD_STEP, r);
        return;
    }
    if (e->etc == 1) { SET8(o, PD_STEP, 0x80); gate_go(lk(o, PD_HEAD), HD_GATE1); }
    else if (e->etc == 2) { SET8(o, PD_STEP, 0x40); gate_go(lk(o, PD_HEAD), HD_GATE2); }
    else {                                                    /* (the dive) */
        SET32(o, PD_SPD, 0x38000);
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_anim, 0);
    }
}

static int dev_autohit(void)                                  /* development: a hit every N frames */
{
    static int n = -1;
    if (n < 0) n = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
    return n > 0 && !(r16(RAM_Level_frame_counter) % n);
}

static void pod_hit_chk(uint32_t o)                           /* [$20BEEA] egg4air_hitchk */
{
    if (OB8(o, PD_SLOW)) {
        SET8(o, PD_SLOW, OB8(o, PD_SLOW) - 1);
        if (!OB8(o, PD_SLOW)) speeds(o, 0x1C000, 0x13C68);
    }
    if (!OB8(o, OBJ_coll_prop)) return;
    if (OB8(o, PD_REARM)) {
        SET8(o, PD_REARM, OB8(o, PD_REARM) - 1);
        if (!OB8(o, PD_REARM)) set_coll(o, 0x3D);
        return;
    }
    if (OB8(o, OBJ_coll_flags) && dev_autohit()) {           /* (as Touch_Enemy: a hit spent) */
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) - 1);
    }
    if (OB8(o, OBJ_coll_flags)) return;
    o_sfx_cd(FM_BOSSHIT);                                     /* (hit: away it rushes) */
    uint32_t h = lk(o, PD_HEAD);
    if (h) SET8(h, HD_HURT, 0x28);
    SET8(o, PD_SLOW, 0x46);
    speeds(o, 0x48000, 0x32C80);
    SET8(o, PD_FLASH, 0x10);
    if (OB8(o, OBJ_coll_prop) == 1) {
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_coll_prop, 0);
    } else
        SET8(o, PD_REARM, 0x1E);
}

static void pod_route(uint32_t o)                             /* [$20BB06] egg4air_01 */
{
    SET16(o, OBJ_y_pos, wy(o) - OBS16(o, PD_BOB));
    pod_hit_chk(o);
    uint8_t r = OB8(o, PD_STEP);
    const Step *e = (r & 0x80 ? route2 : r & 0x40 ? route3 : route1) + (r & 0x3F);
    int32_t spd = L(o, PD_SPD), dspd = L(o, PD_DSPD);
    int n = 0;
    switch (e->vec) {
    case 0:                                                   /* right */
        SET8(o, OBJ_anim, 1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        addL(o, OBJ_x_pos, spd);
        if (e->x > wx(o)) { yure(o); return; }
        SET16(o, OBJ_x_pos, e->x);
        break;
    case 1:                                                   /* left */
        SET8(o, OBJ_anim, 1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        addL(o, OBJ_x_pos, -spd);
        if (e->x < wx(o)) { yure(o); return; }
        SET16(o, OBJ_x_pos, e->x);
        break;
    case 5:                                                   /* down (2176: through the wrap, to 128) */
        SET8(o, OBJ_anim, 0);
        addL(o, OBJ_y_pos, spd);
        if (e->y > wy(o)) { yurenai(o); return; }
        SET16(o, OBJ_y_pos, e->y == 0x880 ? 0x80 : e->y);
        break;
    case 6:                                                   /* down left */
    case 7:                                                   /* down right */
        SET8(o, OBJ_anim, 2);
        SET8(o, OBJ_status, e->vec == 6 ? OB8(o, OBJ_status) | 1 : OB8(o, OBJ_status) & ~1);
        addL(o, OBJ_y_pos, dspd);
        if (e->y <= wy(o)) { SET16(o, OBJ_y_pos, e->y); n++; }
        if (e->vec == 6) {
            addL(o, OBJ_x_pos, -dspd);
            if (e->x >= wx(o)) { SET16(o, OBJ_x_pos, e->x); n++; }
        } else {
            addL(o, OBJ_x_pos, dspd);
            if (e->x <= wx(o)) { SET16(o, OBJ_x_pos, e->x); n++; }
        }
        if (n != 2) { yurenai(o); return; }
        break;
    case 2:                                                   /* up (-192: through the wrap, to 1856) */
        SET8(o, OBJ_anim, 0);
        addL(o, OBJ_y_pos, -spd);
        if (e->y < wy(o)) { yurenai(o); return; }
        SET16(o, OBJ_y_pos, e->y == -0xC0 ? 0x740 : e->y);
        break;
    case 3:                                                   /* up left (CD: at the axis speed) */
    case 4:                                                   /* up right */
        if (e->vec == 3) { SET8(o, OBJ_anim, 2); SET8(o, OBJ_status, OB8(o, OBJ_status) | 1); }
        addL(o, OBJ_y_pos, -spd);
        if (e->y >= wy(o)) { SET16(o, OBJ_y_pos, e->y); n++; }
        if (e->vec == 3) {
            addL(o, OBJ_x_pos, -spd);
            if (e->x >= wx(o)) { SET16(o, OBJ_x_pos, e->x); n++; }
        } else {
            addL(o, OBJ_x_pos, spd);
            if (e->x <= wx(o)) { SET16(o, OBJ_x_pos, e->x); n++; }
            SET8(o, OBJ_anim, 2);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        }
        if (n != 2) { yurenai(o); return; }
        break;
    default:
        return;
    }
    step_done(o, e);
}

static void pod_smoke(uint32_t o)                             /* [$20BF80] egg4air_bom */
{
    uint32_t h = lk(o, PD_HEAD);
    if (h) SET8(h, HD_HURT, 0x1E);                            /* (his hurt face, while it smokes) */
    if (!OB8(o, PD_BOMB)) {
        SET8(o, PD_BOMB, 0x20);
        explode(o, 0, 0);
    }
    SET8(o, PD_BOMB, OB8(o, PD_BOMB) - 1);
}

static void pod_chase(uint32_t o)                             /* [$20BDE6] egg4air_03: beaten, by the distance */
{
    pod_smoke(o);
    uint32_t p = RAM_Player_1;
    int16_t dx = (int16_t)(wx(p) - wx(o)), dy = (int16_t)(wy(p) - wy(o));
    uint16_t ax = (uint16_t)(dx < 0 ? -dx : dx), ay = (uint16_t)(dy < 0 ? -dy : dy);
    int32_t d = (int32_t)((uint32_t)ax * ax + (uint32_t)ay * ay);
    int far = 0;
    if (OB8(o, PD_FAR) & 0x20) {
        if (d >= 0x5100) far = 1;
        else SET8(o, PD_FAR, OB8(o, PD_FAR) & ~0x20);
    }
    if (!far && d >= 0x11040) far = 1;
    if (far) {                                                /* [$20BEAA]: it waits, bobbing */
        SET8(o, PD_FAR, OB8(o, PD_FAR) | 0x20);
        SET16(o, OBJ_y_pos, wy(o) - OBS16(o, PD_BOB));
        SET8(o, OBJ_anim, 0);
        yure(o);
        return;
    }
    if (d < 0x1E40) {
        if (OBS16(p, OBJ_y_vel) > 0x800 || OBS16(p, OBJ_x_vel) > 0x800) speeds(o, 0x100000, 0xB4E88);
        else speeds(o, 0x80000, 0x5A550);
    } else if (d < 0x4840)
        speeds(o, 0x30000, 0x21EF8);
    else
        speeds(o, 0x1C000, 0x13C68);
    pod_route(o);
}

static int pod_step(uint32_t o)                               /* the routine; 0: gone */
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* [$20BA9E] egg4air_ini */
        set_coll(o, 0x3D);
        SET8(o, OBJ_coll_prop, 5);
        SET8(o, OBJ_status, 1);
        SET8(o, OBJ_routine, 8);
        init(o, SCDA_T3POD, 6, 0x24, 0x38);
        speeds(o, 0x1C000, 0x13C68);
        line1(SCDA_BOSSPAL);                                  /* (LoadPalette 5) */
        uint32_t a1 = o_alloc();                              /* [$20BFEC] make_airhead */
        if (a1) {
            set_lk(o, PD_HEAD, a1);
            set_lk(a1, HD_POD, o);
            SET32(a1, OBJ_code, native3(0x4B));
            set_xy(a1, wx(o), wy(o));
        }
        break;
    }
    case 2: pod_route(o); break;
    case 4:                                                   /* [$20C0FE] egg4air_02: the dive */
        addL(o, OBJ_y_pos, L(o, PD_SPD));
        addL(o, PD_SPD, 0x3000);
        if (wy(o) >= 0x580) {
            uint32_t h = lk(o, PD_HEAD);
            if (h && r32(h + OBJ_code) == native3(0x4B)) SET8(h, OBJ_routine, OB8(h, OBJ_routine) + 2);
            /* (CD: LoadPLC 4, the boss's art over the pod's; here resident) */
            o_delete(o);
            return 0;
        }
        break;
    case 6: pod_chase(o); break;
    case 8:                                                   /* [$20BFCA] egg4air_04: the player comes */
        if (wx(RAM_Player_1) >= 0x478) {
            SET8(o, OBJ_routine, 2);
            s_start = 4;
            SET32(o, PD_DSPD, 0x32C80);
        }
        break;
    }
    return 1;
}

static void egg4air(uint32_t o)
{
    if (!pod_step(o)) return;
    uint32_t an = o_asset_anim(SCDA_T3POD);
    if (OB8(o, PD_FLASH)) {                                   /* (hit: the flashing anims, 3 on) */
        SET8(o, PD_FLASH, OB8(o, PD_FLASH) - 1);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 3);
        o_animate(o, an);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) - 3);
    } else
        o_animate(o, an);
    part_draw(o);
}

/* ---- $4B Eggman in the pod ------------------------------------------------- */
static uint32_t gate(uint32_t o, unsigned f, int x, int y)    /* [$20C014] make_gate */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    set_lk(o, f, a1);
    SET32(a1, OBJ_code, native3(0x47));
    SET8(a1, OBJ_subtype, 1);
    set_xy(a1, x, y);
    return a1;
}

static void egg4airhead(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20C144] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_T3PODHEAD, 6, 0x24, 0x38);
        if (gate(o, HD_GATE1, 0x760, 0x2C0)) gate(o, HD_GATE2, 0x6C0, 0x460);
        /* fall through */
    case 2: {                                                 /* [$20C172] with the pod */
        if (OB8(o, HD_HURT)) {
            SET8(o, HD_HURT, OB8(o, HD_HURT) - 1);
            if (OB8(o, HD_HURT)) SET8(o, OBJ_anim, 1);
            else anim_reset(o, 0);
        }
        uint32_t pod = lk(o, HD_POD);
        if (pod && r32(pod + OBJ_code) == native3(0x4A)) {
            set_xy(o, wx(pod), wy(pod));
            SET8(o, OBJ_status, OB8(pod, OBJ_status));
        }
        o_animate(o, o_asset_anim(SCDA_T3PODHEAD));
        o_draw(o);
        break;
    }
    case 4: {                                                 /* [$20C1C6] (unseen) the player at the arena */
        uint32_t p = RAM_Player_1;
        if (wx(p) < 0x840 || wy(p) < 0x550) break;
        s_flag = 4;
        for (unsigned f = HD_GATE1; f <= HD_GATE2; f += 2) {  /* (the gates, a frame each, then itself) */
            uint32_t g = lk(o, f);
            SET16(o, f, 0);
            if (g && r32(g + OBJ_code) == native3(0x47)) { o_delete(g); return; }
        }
        o_delete(o);
        break;
    }
    }
}

/* ---- $4C Eggman ------------------------------------------------------------- */
static void posiset(uint32_t o)                               /* [$20CE72] egg4_posiset */
{
    int16_t s, c;
    o_sincos(OB8(o, PS_ANG), &s, &c);
    int16_t dy = (int16_t)((int16_t)(s * OBS16(o, PS_RY)) >> 8);   /* (muls, asr.w: its low word) */
    int16_t dx = (int16_t)((int16_t)(c * OBS16(o, PS_RX)) >> 8);
    SET16(o, PS_DX, dx);
    SET16(o, PS_DY, dy);
    set_xy(o, wx(o) + dx, wy(o) + dy);
}

static void laugh(uint32_t o)                                 /* [$20C7C8] egg4_warai */
{
    SET8(o, EG_LAUGH, 0x78);
    anim_reset(o, 1);
}

static void laugh_chk(uint32_t o)                             /* [$20C7AA] when the player is hurt or dead */
{
    if (OB8(o, OBJ_anim)) return;
    uint32_t p = RAM_Player_1;
    if (OB8(p, P_INVULN) || OB8(p, P_ROUTINE) >= 6) laugh(o);
}

static uint32_t spawn(uint32_t o, int id, int y)
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, native3(id));
    set_lk(a1, C_PARENT, o);
    set_xy(a1, wx(o), y);
    return a1;
}

static void make_tama(uint32_t o)                             /* [$20CEFA] four shots */
{
    static const struct { int8_t x, y; int32_t vx, vy; } t[4] = {
        { -29, 23, -0x13C68, 0x13C68 }, { -6, 31, 0, 0x1C000 }, { 16, 28, 0xE000, 0x183F7 }, { 29, 17, 0x13C68, 0x13C68 },
    };
    for (int k = 0; k < 4; k++) {
        uint32_t a1 = spawn(o, 0x4E, wy(o));
        if (!a1) break;
        int fl = OB8(o, OBJ_status) & 1;
        SET16(a1, PS_DX, fl ? -t[k].x : t[k].x);
        SET16(a1, PS_DY, t[k].y);
        SET32(a1, T_VX, (uint32_t)(fl ? -t[k].vx : t[k].vx));
        SET32(a1, T_VY, (uint32_t)t[k].vy);
    }
}

static void bom_set(uint32_t o)                               /* [$20C928] */
{
    static const int8_t spot[10][2] = {
        { -48, -16 }, { 48, 16 }, { -16, -16 }, { 16, 16 }, { -32, 0 },
        { 48, -16 }, { -48, 16 }, { -16, 16 }, { 16, -16 }, { 32, 0 },
    };
    uint8_t n = OB8(o, EG_TIME);
    if (n & 3) return;
    int k = (n >> 2) % 10;
    explode(o, spot[k][0], spot[k][1]);
}

static void posiini(uint32_t o)                               /* [$20C514] egg4_posiini: down to his place */
{
    if (wy(o) < 0x520) { addL(o, OBJ_y_pos, 0x4650); return; }
    SET32(o, OBJ_x_pos, 0xAB00000);
    SET32(o, OBJ_y_pos, 0x5200000);
}

static void die(uint32_t o)                                   /* [$20C828] egg4_die */
{
    uint8_t t = (uint8_t)(OB8(o, EG_TIME) + 1);
    SET8(o, EG_TIME, t);
    if (t < 0x3C) bom_set(o);
    if (t == 0x3C) {
        anim_reset(o, 3);
        SET8(o, EG_LAUGH, 0xFF);
        SET8(o, OBJ_frame, 1);
    } else if (t == 0x3D) {                                   /* (he turns to flee right) */
        SET8(o, EG_TIME, 0);
        SET8(o, OBJ_routine, 0x14);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        uint32_t m = lk(o, EG_MECA);
        if (m) { SET8(m, OBJ_render, OB8(m, OBJ_render) | 1); SET8(m, OBJ_status, OB8(m, OBJ_status) | 1); }
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
    }
}

/* [$20C7E6] egg4_hitchk: his touch on again; hit (one left), beaten (CD then
 * goes on with the routine that called it, as here) */
static void hit_chk(uint32_t o)
{
    if (OB8(o, OBJ_coll_flags) && dev_autohit()) {           /* (as Touch_Enemy: a hit spent) */
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) - 1);
    }
    if (OB8(o, OBJ_coll_flags)) return;
    set_coll(o, 0x3C);
    if (OB8(o, OBJ_coll_prop) != 1) return;
    SET8(o, EG_TIME, 0);
    SET8(o, OBJ_routine, 0x12);
    SET8(o, EG_FLAGS, flags(o) | 0x80);
    anim_reset(o, 2);
    die(o);
}

static void half_end(uint32_t o)                              /* (the end of a half ellipse) */
{
    SET8(o, EG_TIME, 0);
    SET8(o, OBJ_routine, 0x10);
    SET8(o, EG_FLAGS, flags(o) | 0x10);
}

static int egg4_step(uint32_t o)                              /* the routine; 0: not drawn */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20C43A] egg4_ini */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_T3EGGMAN, 4, 0x20, 0x2C);
        SET8(o, OBJ_coll_prop, 2);
        s_right = r16(RAM_Camera_max_X_pos);
        {
            uint32_t a1 = spawn(o, 0x4D, wy(o));              /* [$20CED2] make_egg4meca */
            if (a1) set_lk(o, EG_MECA, a1);
        }
        /* fall through */
    case 2: {                                                 /* [$20C46E] egg4_scrset: the arena closes behind */
        w16(RAM_Camera_max_X_pos, 0x9B0);
        w16(RAM_Camera_target_max_X_pos, 0x9B0);
        int16_t d0 = (int16_t)(wx(p) - 0xA0);
        if (d0 < (int16_t)r16(RAM_Camera_min_X_pos)) break;
        if (wx(p) >= 0xA50) {
            scd_cd_play(SCD_CD_BOSS, 1);                      /* ($67: bossMusic) */
            s_music = 1;
            w8(RAM_Boss_flag, 1);
            SET8(o, OBJ_routine, 6);
            d0 = 0x9B0;
        }
        w16(RAM_Camera_min_X_pos, (uint16_t)d0);
        w16(RAM_Camera_target_min_X_pos, (uint16_t)d0);
        break;
    }
    case 4: {                                                 /* [$20C75A] egg4_scrreset: the way on opens */
        uint16_t r = (uint16_t)(r16(RAM_Camera_max_X_pos) + 6);
        w16(RAM_Camera_max_X_pos, r);
        w16(RAM_Camera_target_max_X_pos, (uint16_t)(r16(RAM_Camera_target_max_X_pos) + 6));
        if ((int16_t)s_right > (int16_t)r) return 0;
        s_start = 0;
        w8(RAM_Boss_flag, 0);
        s_music = 0;
        w16(RAM_Camera_max_X_pos, s_right);
        w16(RAM_Camera_target_max_X_pos, s_right);
        tri_scd_level_music();                                /* ($16 / $17: the future's own) */
        line1(SCDA_GENEPAL);                                  /* (genecolor) */
        o_delete(o);
        return 0;
    }
    case 6: {                                                 /* [$20C4BE] egg4_awademo1: 16 bubbles */
        posiini(o);
        uint8_t t = (uint8_t)(OB8(o, EG_TIME) + 1);
        SET8(o, EG_TIME, t);
        if (t == 0xA) {                                       /* (one that only rises) */
            uint32_t a1 = spawn(o, 0x4F, 0x5B8);
            if (a1) SET8(a1, B_PHASE, 1);
        }
        if (t != 0x14 && t < 0x1E) break;
        o_sfx_cd(FM_BLOW);
        SET8(o, EG_TIME, 0);
        uint32_t a1 = spawn(o, 0x4F, 0x5B8);
        if (a1) SET8(a1, B_INDEX, OB8(o, EG_COUNT));
        SET8(o, EG_COUNT, OB8(o, EG_COUNT) + 1);
        if (OB8(o, EG_COUNT) == 0x10) SET8(o, OBJ_routine, 8);
        break;
    }
    case 8:                                                   /* [$20C53C] egg4_awademo2 */
        posiini(o);
        SET8(o, EG_TIME, OB8(o, EG_TIME) + 1);
        if (OB8(o, EG_TIME) == 0x96) { SET8(o, EG_TIME, 0); SET8(o, OBJ_routine, 0xA); }
        break;
    case 0xA:                                                 /* [$20C556] egg4_awademo3: (the ring forms) */
        SET8(o, EG_TIME, OB8(o, EG_TIME) + 1);
        if (OB8(o, EG_TIME) != 0x96) break;
        SET8(o, OBJ_routine, 0xC);
        SET16(o, PS_RX, 0x60);
        SET16(o, PS_RY, 0x40);
        SET16(o, PS_ANG, 0);
        SET16(o, PS_DX, 0x60);
        SET16(o, PS_DY, 0);
        SET8(o, EG_TIME, 0);
        break;
    case 0xC:                                                 /* [$20C58C] egg4_movel: half an ellipse, leftward */
    case 0xE:                                                 /* [$20C5D4] egg4_mover: rightward */
        {
            int left = OB8(o, OBJ_routine) == 0xC;
            hit_chk(o);
            uint16_t a = (uint16_t)(OB16(o, PS_ANG) + (left ? 0x100 : -0x100));
            SET16(o, PS_ANG, a);
            if (a == (left ? 0x800 : 0x7800)) make_tama(o);
            if (a == (left ? 0x8000 : 0)) { half_end(o); break; }
            set_xy(o, wx(o) - OBS16(o, PS_DX), wy(o) - OBS16(o, PS_DY));
            posiset(o);
        }
        break;
    case 0x10: {                                              /* [$20C61C] egg4_wait: he turns */
        static const int8_t thin[8] = { 14, 12, 10, 8, 6, 4, -1, -1 };   /* awaposi_cnt_tbl */
        SET8(o, EG_FLAGS, flags(o) & ~0x30);
        hit_chk(o);
        uint8_t t = (uint8_t)(OB8(o, EG_TIME) + 1);
        SET8(o, EG_TIME, t);
        if (t == 0x30) {
            uint32_t m = lk(o, EG_MECA);
            if (m) { SET8(m, OBJ_render, OB8(m, OBJ_render) ^ 1); SET8(m, OBJ_status, OB8(m, OBJ_status) ^ 1); }
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
            break;
        }
        if (t != 0x60) break;
        SET8(o, EG_TIME, 0);
        if (OB8(o, OBJ_status) & 1) { SET8(o, EG_FLAGS, flags(o) | 8); SET8(o, OBJ_routine, 0xE); }
        else { SET8(o, EG_FLAGS, flags(o) & ~8); SET8(o, OBJ_routine, 0xC); }
        for (;;) {                                            /* (the ring thinner by 2: across, laughing) */
            int8_t c = thin[OB8(o, EG_STEP) & 7];
            if (c < 0 || c < (int8_t)OB8(o, EG_COUNT)) break;
            SET8(o, EG_STEP, OB8(o, EG_STEP) + 1);
            SET8(o, OBJ_routine, 0x16);
        }
        break;
    }
    case 0x12: die(o); break;
    case 0x14:                                                /* [$20C8A4] egg4_esc: he flees right */
        SET8(o, EG_TIME, OB8(o, EG_TIME) + 1);
        if (OB8(o, EG_TIME) < 0x3C) break;
        addL(o, OBJ_x_pos, 0x1C000);
        if (wx(o) >= 0xB30) {
            SET8(o, OBJ_routine, 4);
            uint32_t m = lk(o, EG_MECA);
            if (m && r32(m + OBJ_code) == native3(0x4D)) o_delete(m);
        }
        break;
    case 0x16: {                                              /* [$20C6B4] egg4_movec: straight across */
        uint8_t t = (uint8_t)(OB8(o, EG_TIME) + 1);
        SET8(o, EG_TIME, t);
        if (t == 0x14) { laugh(o); SET8(o, EG_SLOT, 0); SET8(o, EG_FLAGS, flags(o) | 0x20); }
        else if (t == 0x15) SET8(o, EG_FLAGS, flags(o) & ~0x20);
        else if (t == 0x2D) make_tama(o);
        if (flags(o) & 8) {
            addL(o, OBJ_x_pos, 0x10000);
            if (wx(o) < 0xAB0) break;
        } else {
            addL(o, OBJ_x_pos, -0x10000);
            if (wx(o) > 0x9F0) break;
        }
        SET32(o, PS_DX, (uint32_t)-L(o, PS_DX));              /* (neg.l: the offsets as one long) */
        SET16(o, PS_ANG, OB16(o, PS_ANG) + 0x8000);
        SET32(o, OBJ_x_pos, 0xA500000);
        addL(o, OBJ_x_pos, L(o, PS_DX));
        SET32(o, OBJ_y_pos, 0x5200000);
        half_end(o);
        break;
    }
    }
    return 1;
}

static void egg4(uint32_t o)
{
    SET8(o, EG_FLAGS, flags(o) & ~0x40);
    if (OB8(o, EG_LAUGH)) {
        SET8(o, EG_LAUGH, OB8(o, EG_LAUGH) - 1);
        if (!OB8(o, EG_LAUGH)) anim_reset(o, 0);
    } else
        laugh_chk(o);
    if (!egg4_step(o)) return;
    o_animate(o, o_asset_anim(SCDA_T3EGGMAN));
    part_draw(o);
}

/* ---- $4D his machine ---------------------------------------------------------- */
static void egg4meca(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20C8EC] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_T3MECA, 4, 0x20, 0x1C);
    }
    uint32_t e = parent(o);                                   /* [$20C916] */
    if (!e) return;
    set_xy(o, wx(e), wy(e));
    o_draw(o);
}

/* ---- $4E a shot ------------------------------------------------------------------ */
static void egg4tama(uint32_t o)
{
    uint32_t e = parent(o);
    if (!e) return;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20CFA8] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_T3SHOT, 6, 4, 4);
        /* fall through */
    case 2:                                                   /* [$20CFD2] at its place on him */
        if (flags(e) & 0x80) { o_delete(o); return; }
        set_xy(o, wx(e) + OBS16(o, PS_DX), wy(e) + OBS16(o, PS_DY));
        SET8(o, T_TIME, OB8(o, T_TIME) + 1);
        if (OB8(o, T_TIME) == 0x2D) {
            set_coll(o, 0xBE);                                /* (CD's $FE: it hurts, while bossstart is 4) */
            SET8(o, OBJ_routine, 4);
        }
        break;
    case 4:                                                   /* [$20D018] away */
        if (flags(e) & 0x80) { o_delete(o); return; }
        addL(o, OBJ_x_pos, L(o, T_VX));
        addL(o, OBJ_y_pos, L(o, T_VY));
        if (wx(o) < 0x9A0 || wx(o) > 0xB00 || wy(o) > 0x5D0) { o_delete(o); return; }
        break;
    }
    o_animate(o, o_asset_anim(SCDA_T3SHOT));
    part_draw(o);
}

/* ---- $4F a bubble ------------------------------------------------------------------ */
/* plairset [$20612C]: air back to 30 s. (CD: if the countdown had begun, the
 * music back, the boss's while bossstart. Here S3K's countdown song stops as
 * Player_ResetAirTimer stops it, and the disc's track: scd_ttz.c may want
 * this, its air's) */
static void plairset(uint32_t p)
{
    if (OB8(p, P_AIR) <= 12) {
        D(0) = r16(RAM_Current_music);
        tri_jsr(S3K_Play_Music);
        if (s_start) scd_cd_play(SCD_CD_BOSS, 1);
        else tri_scd_level_music();
    }
    SET8(p, P_AIR, 30);
}

/* the player's touch [$2073C2] (bossstart 4, CD's $3F): rolling, he breathes
 * it in; anything else, nothing. S3K's special touch counts it (player 1: 1) */
static void bubble_touch(uint32_t o)
{
    uint8_t n = OB8(o, OBJ_coll_prop);
    SET8(o, OBJ_coll_prop, 0);
    uint32_t p = RAM_Player_1;
    if (!(n & 1) || s_start != 4 || !(OB8(p, OBJ_status) & 0x14)) return;
    if (OB8(p, OBJ_status) & 4) {                             /* (S3K's: its standing size, as its bubbles do) */
        int d = OB8(p, P_DEF_YRAD) - OB8(p, OBJ_y_radius);
        SET8(p, OBJ_y_radius, OB8(p, P_DEF_YRAD));
        SET8(p, OBJ_x_radius, OB8(p, P_DEF_XRAD));
        SET16(p, OBJ_y_pos, wy(p) - d);
    }
    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~0x14);
    SET8(p, P_JUMPING, 0);
    SET8(o, OBJ_coll_flags, 0);
    SET8(p, P_ANIM, 0x15);
    o_sfx_cd(FM_BREATHE);
    plairset(p);
    SET16(p, OBJ_y_vel, 0x400);
    SET16(p, OBJ_x_vel, (uint16_t)wx(p) >= (uint16_t)wx(o) ? 0x200 : -0x200);
}

static void with_egg(uint32_t o, uint32_t e)
{
    SET32(o, OBJ_x_pos, r32(e + OBJ_x_pos));
    SET32(o, OBJ_y_pos, r32(e + OBJ_y_pos));
}

static void ring_turn(uint32_t o, uint32_t e, int16_t d)      /* (round him, his way) */
{
    SET16(o, PS_ANG, OB16(o, PS_ANG) + (flags(e) & 8 ? d : -d));
}

/* [$20CDE0] awa_hitchk: touched (its type cleared), it pops; all pop as he
 * flees. (CD goes on with the routine that called it, as here) */
static void awa_hit(uint32_t o, uint32_t e)
{
    if (OB8(e, OBJ_routine) != 0x14) {
        if (OB8(o, OBJ_coll_flags)) return;
        uint8_t f = flags(e);
        SET8(e, EG_FLAGS, f | 0x40);
        if (f & 0x40) { set_coll(o, 0xFF); return; }         /* (one a frame) */
    }
    SET8(o, OBJ_routine, 0xC);
    anim_reset(o, 4);
    SET8(e, EG_COUNT, OB8(e, EG_COUNT) - 1);
    o_animate(o, o_asset_anim(SCDA_T3BUBBLE));
}

static void egg4awa(uint32_t o)
{
    uint32_t an = o_asset_anim(SCDA_T3BUBBLE), e;
    if (OB8(o, OBJ_coll_flags)) bubble_touch(o);
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* [$20C9D2] egg4awa_ini */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_T3BUBBLE, 3, 0x10, 0x10);
        int16_t r = (int16_t)(uint16_t)o_random() % 0x500;    /* (its wobble: a speed either way) */
        SET16(o, B_ASPD, r < 0 ? r - 0x80 : r + 0x80);
        SET16(o, PS_RX, (uint16_t)o_random() % 0x10 + 0xA);
    }
        /* fall through */
    case 2:                                                   /* [$20CA3E] egg4awa_deru: up */
        addL(o, OBJ_y_pos, -0x18000);
        if (OB8(o, B_PHASE)) {
            if (wy(o) < 0x470) { o_delete(o); return; }
        } else {
            if (!(e = parent(o))) return;
            if (wy(e) >= wy(o)) {                             /* (at his height: round him) */
                int16_t d0 = (int16_t)(wx(o) - wx(e));
                SET16(o, PS_ANG, d0 >= 0 ? 0x8000 : 0);
                SET16(o, PS_RX, d0);
                SET16(o, PS_RY, d0);
                SET16(o, PS_DX, -d0);
                SET16(o, PS_DY, 0);
                set_xy(o, wx(e), wy(e));
                SET8(o, OBJ_routine, 4);
                SET8(o, OBJ_anim, 2);
                break;
            }
        }
        SET16(o, PS_ANG, OB16(o, PS_ANG) + OB16(o, B_ASPD));
        SET16(o, OBJ_x_pos, wx(o) - OBS16(o, PS_DX));
        posiset(o);
        if (OB8(o, OBJ_anim_frame) == 4) { SET8(o, OBJ_anim, 1); SET8(o, OBJ_anim_frame, 0); }
        o_animate(o, an);
        break;
    case 4: {                                                 /* [$20CAE2] egg4awa_tuku: round him */
        if (!(e = parent(o))) return;
        set_xy(o, wx(e), wy(e));
        posiset(o);
        int16_t d = OBS16(o, B_ASPD);
        SET16(o, PS_ANG, OB16(o, PS_ANG) + (d < 0 ? d : -d));
        if (OB8(e, OBJ_routine) != 0xA) break;
        SET16(o, PS_DX, 0);                                   /* (the ring: its place by its number) */
        SET16(o, PS_DY, 0);
        SET8(o, OBJ_routine, 6);
        with_egg(o, e);
        SET16(o, PS_ANG, (uint16_t)(OB8(o, B_INDEX) * 0x1000));
        SET16(o, PS_RX, 0x10);
        SET16(o, PS_RY, 0x10);
        break;
    }
    case 6: {                                                 /* [$20CB44] egg4awa_hiro: the ring grows */
        if (!(e = parent(o))) return;
        if (OB8(o, B_FLAGS) & 2) awa_hit(o, e);
        int n = 0;
        with_egg(o, e);
        if (OBS16(o, PS_RX) < 0x30) addL(o, PS_RX, 0x4000); else { SET16(o, PS_RX, 0x30); n++; }
        if (OBS16(o, PS_RY) < 0x30) addL(o, PS_RY, 0x4000); else { SET16(o, PS_RY, 0x30); n++; }
        ring_turn(o, e, 0x480);
        posiset(o);
        if (OB8(o, OBJ_anim_frame) == 2) { SET8(o, OBJ_anim, 3); SET8(o, OBJ_anim_frame, 0); }
        if (n == 2) {
            SET8(o, OBJ_routine, 8);
            uint8_t f = OB8(o, B_FLAGS);
            SET8(o, B_FLAGS, f | 2);
            if (!(f & 2)) { set_coll(o, 0xFF); SET8(o, OBJ_coll_prop, 0); }
        }
        o_animate(o, an);
        break;
    }
    case 8:                                                   /* [$20CBF8] egg4awa_roll */
        if (!(e = parent(o))) return;
        with_egg(o, e);
        if (flags(e) & 0x10) { SET8(o, OBJ_routine, 0xA); goto ychg; }
        if (flags(e) & 0x20) { SET8(o, OBJ_routine, 0x10); SET16(o, OBJ_priority, 3 << 7); goto chi; }
        awa_hit(o, e);
        ring_turn(o, e, 0x180);
        posiset(o);
        break;
    case 0xA:                                                 /* [$20CC50] egg4awa_ychg: the ring flips over */
        if (!(e = parent(o))) return;
    ychg:
        awa_hit(o, e);
        with_egg(o, e);
        addL(o, PS_RX, -0x10000);                             /* (48 to -48 across: round the other way) */
        if (!OB8(o, B_PHASE)) {
            if (L(o, PS_RX) < 0) SET8(o, B_PHASE, 1);
        } else if (L(o, PS_RX) <= -0x300000) {
            SET32(o, PS_RX, 0x300000);
            SET8(o, OBJ_routine, 8);
            SET16(o, PS_ANG, (uint16_t)(0x8000 - OB16(o, PS_ANG)));   /* [$20CCEC] ychg_rad_endproc */
            SET8(o, B_PHASE, 0);
        }
        ring_turn(o, e, 0x480);                               /* [$20CCAA] ychg_ret: behind him on the far side */
        {
            uint8_t a = (uint8_t)(OB8(o, PS_ANG) - 0x40);
            if (flags(e) & 8) a ^= 0x80;
            SET16(o, OBJ_priority, (a & 0x80 ? 5 : 3) << 7);
        }
        posiset(o);
        break;
    case 0xC: o_animate(o, an); break;                        /* [$20CE30] egg4awa_del: popping ($FC: on) */
    case 0xE: o_delete(o); return;                            /* [$20CE3C] egg4awa_out */
    case 0x10: {                                              /* [$20CD40] egg4awa_chi: the ring re-forms */
        if (!(e = parent(o))) return;
    chi:
        awa_hit(o, e);
        with_egg(o, e);
        addL(o, PS_RX, -0x10000);
        addL(o, PS_RY, -0x10000);
        if (L(o, PS_RX) <= 0x100000) {                        /* (in to 16: evenly spaced again) */
            static const uint16_t rad[17] = { 0, 0, 0x8000, 0x5555, 0x4000, 0x3333, 0x2AAA, 0x2492, 0x2000,
                                              0x1C71, 0x1999, 0x1745, 0x1555, 0x13B1, 0x1249, 0x1111, 0x1000 };
            uint8_t c = OB8(e, EG_COUNT);
            SET16(o, PS_ANG, (uint16_t)(rad[c < 17 ? c : 16] * OB8(e, EG_SLOT)));
            SET8(e, EG_SLOT, OB8(e, EG_SLOT) + 1);
            SET32(o, PS_RX, 0x100000);
            SET32(o, PS_RY, 0x100000);
            SET8(o, OBJ_routine, 6);
        }
        awa_hit(o, e);                                        /* (again: the disc's) */
        ring_turn(o, e, 0x480);
        posiset(o);
        break;
    }
    }
    part_draw(o);
}

/* ---- the level's events (zone43chk [$2038DE]) -------------------------------- */
static void events(void)
{
    uint32_t p = RAM_Player_1;
    if (wx(p) >= 0xAF8) {                                     /* [$20396C] bossclr_scr: past the arena */
        w16(RAM_Camera_target_max_Y_pos, 0x4C0);
        int16_t d = (int16_t)(0x4C0 - r16(RAM_Camera_max_Y_pos));
        if ((d < 0 ? -d : d) < 4) w16(RAM_Camera_max_Y_pos, 0x4C0);
        int16_t x = (int16_t)(wx(p) - 0xA0);                  /* (the left edge follows: no way back) */
        if (x < (int16_t)r16(RAM_Camera_min_X_pos)) return;
        if (x > (int16_t)r16(RAM_Camera_max_X_pos)) x = (int16_t)r16(RAM_Camera_max_X_pos);
        w16(RAM_Camera_min_X_pos, (uint16_t)x);
        w16(RAM_Camera_target_min_X_pos, (uint16_t)x);
        return;
    }
    if (s_flag) {                                             /* (the arena's bottom) */
        w16(RAM_Camera_max_Y_pos, 0x4F0);
        w16(RAM_Camera_target_max_Y_pos, 0x4F0);
    }
}

static int ttz3(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == TTZ_ROUND && tri_act() == 2;
}

int scd_ttz3_boss_start(void) { return s_start; }             /* bossstart (4: the chase and the fight) */
int scd_ttz3_boss_music(void) { return s_music; }             /* bossMusic */

/* TH_LEVEL_ENTRY: game_init's (bossstart, bossflag cleared); after the
 * objects each frame, act 3's events */
void scd_ttz3_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {
        s_start = s_flag = s_music = 0;
        return;
    }
    if (pc != TH_LVL_POST_SPRITES || !tri_dir() || !ttz3()) return;
    events();
}

void scd_ttz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x15] = cd_capsule;
    t[base3 + 0x2F] = scd_ttz_animal;
    t[base3 + 0x4A] = egg4air;
    t[base3 + 0x4B] = egg4airhead;
    t[base3 + 0x4C] = egg4;
    t[base3 + 0x4D] = egg4meca;
    t[base3 + 0x4E] = egg4tama;
    t[base3 + 0x4F] = egg4awa;
}
