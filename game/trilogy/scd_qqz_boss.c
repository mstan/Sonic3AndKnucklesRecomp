/*
 * scd_qqz_boss.c — Quartz Quadrant act 3 (Sonic CD R53C / R53D): Eggman's
 * press over the treadmill, its claw and bombs, the spike wall, the capsule;
 * and the act's level-wide pieces (its camera events, its belts' palette
 * cycle). Transcribed from the Mega-CD program (the routines' R53C addresses
 * in brackets; names from the PC port's BOSS_5.C; R53D's code is the same).
 *
 * The housing on top ($33, placed) shuts the arena (3264-3584) and builds
 * the press ($34) under it; the press builds Eggman ($32, his claw $37 on a
 * lever), its drill ($35), a pipe ($36) and the treadmill ($22, no art: it
 * runs the player and the belt's colours). One spin into the press starts
 * it: it comes down onto the belt, and the player running against the belt
 * grinds it (its hit points, 2400, fall by the belt's speed: up to one a
 * frame), stepping down 8 px at 1800, 1200 and 600. The claw drops a bomb
 * ($38) every three seconds, which bursts into four ($39); the spike wall
 * ($21) closes in from the left. At 0 Eggman flees, the press bursts
 * ($23, sparks $20) and the way on opens to the capsule ($15).
 *
 * State the disc keeps for others:
 *   bossFlag ($F7A7): 5 from the camera's look up on, to the act's end: the
 *     floor's belts stop (the player code's belt), so does the palette cycle;
 *     the level's events hold the camera's bottom at $1F8 (scd_qqz3_bossflag)
 *   bossStart ($F7AA): 5 while the arena is shut: the bombs' touch (S3K's
 *     Boss_flag here, at the same address: also the player's right bound)
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (named below), CD's $10 long
 * S3K's $18. Touch: the press is a CD enemy with hits left (S3K's boss touch:
 * Touch_Enemy clears the type on a hit, the press polls); the spike wall
 * hurts (S3K's); the bombs' special touch ($FD, while bossStart is 5) is an
 * S1 box test here (o_s1_touch, CD's size $3D).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>

#define QQZ_ROUND       3                  /* (ROUND_INDEX: Quartz Quadrant) */

/* asset ids (-> trilogy_obj.h) */

/* the objects' variables */
#define B_TIME   0x30                      /* oVar2A (byte) */
#define B_TIME2  0x31                      /* oVar2B */
#define B_FLAGS  0x32                      /* oVar2C */
#define B_PHASE  0x33                      /* oVar2D */
#define B_PARENT 0x34                      /* oVar2E (a slot) */
#define B_CHILD  0x36                      /* oVar30 (a slot): the housing's press, the press's
                                              Eggman, Eggman's claw */
#define M2_BELT  0x38                      /* oVar32 (a slot): the press's treadmill */
#define M2_JIG   0x3A                      /* oVar34 */
#define M2_UP    0x3C                      /* oVar36: the press's way up to */
#define M2_DOWN  0x3E                      /* oVar38: and down to */
#define M2_HP    0x44                      /* oVar3E (word): 2400 */
#define M1_SHAKE 0x44                      /* oVar3E (byte) */
#define BL_SPEED 0x38                      /* oVar32: the belt's speed */
#define BL_GRIND 0x3A                      /* oVar34: its run, toward a hit point */
#define BL_XV    0x3C                      /* oVar36: the player's x speed, last on it */
#define BL_YV    0x3E                      /* oVar38 */
#define BL_COL   0x44                      /* oVar3E (byte): its colours' step */
#define BM_ANG   0x38                      /* oVar32: the bomb's swing */
#define BM_BOB   0x3E                      /* oVar38 */
#define F_VX     0x40                      /* oVar3A (long): sparks', shrapnel's x speed */
#define F_VY     0x18                      /* (CD $10, long): their y speed */

#define P_OBJCTRL 0x2E

#define FM_BOSSHIT  0xAC                   /* (egg5meca2_ue: the press spun into) */
#define FM_THUMP    0xB4                   /* (egg5meca2_down2: the press down, and up; egg5bomb_3: a
                                              bomb on the belt) */
#define FM_EXPLODE  0x9E                   /* (egg5bomb_hit: a bomb bursts; bom_set: each blast) */
#define FM_BUZZ     0xC4                   /* (egg5meca2_down2: the press's first bite; egg5catch_go2:
                                              the claw stops; held till another sound takes it) */
#define FM_GRAB     0xCB                   /* (egg5catch_wait: the claw takes a bomb; a loop till
                                              another sound takes it) */

#define RAM_Distance_from_top 0xFFEE2Cu    /* (CD's scra_vline, $F73E) */

void tri_scd_level_music(void);
void cd_capsule(uint32_t o);               /* scd_ppz_boss.c: Palmtree Panic's $15, the same code */
int scd_qqz_colrev(void);                  /* scd_qqz.c: colrevflag */

static int s_base3;
static uint8_t s_bossflag;                 /* bossFlag */
static uint8_t s_bossstart;                /* bossStart */
static uint16_t s_right;                   /* (the level's right edge: CamBounds) */
static uint16_t s_bottom, s_dest;          /* bottomBound, destBottomBound (from bossFlag on) */
static int8_t s_cyc_time;                  /* the act's palette cycle ($F7B7, $F7B0) */
static uint8_t s_cyc_step;

static int qqz3(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == QQZ_ROUND && tri_act() == 2;
}

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }
static uint32_t lk(uint32_t o, unsigned f)
{
    uint16_t w = o ? OB16(o, f) : 0;
    return w ? 0xFF0000u | w : 0;
}
/* (a link read as CD reads it: a deleted object's cleared slot reads 0; S3K
 * reuses slots, so one that is no longer that object is none) */
static uint32_t lk_is(uint32_t o, unsigned f, int id)
{
    uint32_t a = lk(o, f);
    return a && r32(a + OBJ_code) == native3(id) ? a : 0;
}
static void set_lk(uint32_t o, unsigned f, uint32_t a) { if (o) SET16(o, f, (uint16_t)a); }

static int btst(uint32_t o, unsigned bit) { return o && (OB8(o, B_FLAGS) >> bit) & 1; }
static int bset(uint32_t o, unsigned bit)
{
    if (!o) return 0;
    uint8_t v = OB8(o, B_FLAGS);
    SET8(o, B_FLAGS, v | (1u << bit));
    return (v >> bit) & 1;
}
static int bclr(uint32_t o, unsigned bit)
{
    if (!o) return 0;
    uint8_t v = OB8(o, B_FLAGS);
    SET8(o, B_FLAGS, v & ~(1u << bit));
    return (v >> bit) & 1;
}
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }
static int32_t L(uint32_t o, unsigned f) { return (int32_t)r32(o + f); }
static void addL(uint32_t o, unsigned f, int32_t d) { SET32(o, f, (uint32_t)(L(o, f) + d)); }

/* render 4, priority, CD's $19 (width) and $16 (y radius) */
static void init(uint32_t o, int asset, int prio, int width, int yrad)
{
    o_init(o, asset, 4, prio, width, yrad > 0x20 ? yrad : 0x20);
    SET8(o, OBJ_y_radius, yrad);
}

static void anim_set(uint32_t a1, int anim)                   /* [$20C400] */
{
    if (!a1) return;
    SET8(a1, OBJ_anim, anim);
    SET8(a1, OBJ_frame, 0);
    SET8(a1, OBJ_anim_frame, 0);
    SET8(a1, OBJ_anim_timer, 0);
}

static uint32_t spawn(uint32_t o, int id, int dx, int dy)     /* FindObjSlot: a part, its parent o */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, native3(id));
    set_lk(a1, B_PARENT, o);
    set_xy(a1, wx(o) + dx, wy(o) + dy);
    return a1;
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

static void set_right(uint16_t x) { w16(RAM_Camera_max_X_pos, x); w16(RAM_Camera_target_max_X_pos, x); }
static void set_left(uint16_t x) { w16(RAM_Camera_min_X_pos, x); w16(RAM_Camera_target_min_X_pos, x); }

static void solid(uint32_t o)                                 /* SolidObject (CD's $19, $16) */
{
    int w = OB8(o, OBJ_width_px), h = OB8(o, OBJ_y_radius);
    o_solid(o, w + 10, h, h + 1, OB16(o, OBJ_x_pos));
}

static void draw_touch(uint32_t o)
{
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

/* [$20C420] off the screen (CD's $19, $16 round it): 1 */
static int off_screen(uint32_t o)
{
    int w = OB8(o, OBJ_width_px), h = OB8(o, OBJ_y_radius);
    int16_t d = (int16_t)(wx(o) - r16(RAM_Camera_X_pos));
    if ((int16_t)(d + w) < 0 || (int16_t)(d - w) >= 0x140) return 1;
    d = (int16_t)(wy(o) - r16(RAM_Camera_Y_pos));
    return (int16_t)(d + h) < 0 || (int16_t)(d - h) >= 0xE0;
}

/* [$20BC7C] a bomb bursts: CD's explosion $18 (routine 2 $FF: no flower) */
static void burst(uint32_t o)
{
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 6);                                  /* (no animal, no sound of its own) */
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
    o_sfx_cd(FM_EXPLODE);
}

/* ---- sparks and explosions the press makes ----------------------------- */
static void spark1(void)                                      /* [$20C196] make_hibana1: one along the belt */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, native3(0x20));
    set_xy(a1, 0xDE8, 0x2B8);
}

static void sparks(uint32_t o)                                /* [$20C1C4] make_hibana2: eight, falling */
{
    static const int8_t dy[6] = { 0x34, 0x34, 0x2C, 0x20, 0x1C, 0x1C };
    static const int8_t at[8][2] = { { -24, 0 }, { -6, 0 }, { 6, 0 }, { 24, 0 },
                                     { -12, -8 }, { 12, -8 }, { -12, 8 }, { 12, 8 } };
    uint8_t f = OB8(o, OBJ_frame);
    int d4 = dy[f < 6 ? f : 5];
    for (int k = 0; k < 8; k++) {
        uint32_t a1 = spawn(o, 0x20, at[k][0], at[k][1] + d4);
        if (!a1) return;
        SET8(a1, B_PHASE, 1);
    }
}

static void bom_set(uint32_t o)                               /* [$20C238] */
{
    static const int8_t at[4][2] = { { -16, -8 }, { -32, 8 }, { -16, 24 }, { 32, 8 } };
    uint8_t t = OB8(o, B_TIME);
    if (t & 3) return;
    int k = (t >> 2) & 3;
    uint32_t a1 = spawn(o, 0x23, at[k][0], at[k][1]);
    if (!a1) return;
    SET8(a1, B_TIME, k);
    o_sfx_cd(FM_EXPLODE);
}

/* ---- $32 Eggman: in the press; its first press bites, he laughs; ground
 * near through, he panics; through, he flees --------------------------- */
static void egg5(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20B330] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3EGGMAN, 5, 0x10, 0x10);
        set_lk(o, B_CHILD, spawn(o, 0x37, -0x80, -0x44));     /* (the claw) */
        /* fall through */
    case 2: {                                                 /* [$20B35E] */
        if (!btst(o, 2)) {
            uint32_t m = lk_is(o, B_PARENT, 0x34);
            if (m) SET16(o, OBJ_y_pos, OB16(m, OBJ_y_pos));
            if (!m || OB8(m, OBJ_frame) < 4) break;
            if (!OB8(o, B_TIME)) {
                anim_set(o, 2);
                SET8(o, B_TIME, 0x44);
            } else {
                SET8(o, B_TIME, OB8(o, B_TIME) - 1);
                if (OB8(o, B_TIME) == 0x14) anim_set(o, 0);
            }
            break;
        }
        SET16(o, OBJ_priority, 3 << 7);                       /* [$20B3A6] the press ground through */
        SET8(o, B_TIME, 0);
        SET8(o, OBJ_routine, 4);
        anim_set(o, 3);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 6);
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
    }   /* fall through */
    case 4:                                                   /* [$20B3C6] away */
        cd_move(o, 0x12000, 0);
        if (wx(o) >= 0xE20) { o_delete(o); return; }
        break;
    }
    if (OB8(o, OBJ_anim)) o_animate(o, o_asset_anim(SCDA_Q3EGGMAN));   /* (anim 0: the frame he is given) */
    o_draw(o);
}

/* ---- $33 the housing on top: the camera looks up, the arena shuts; after,
 * the way on opens. Its press's thumps shake the camera's bottom ------- */
static void meca1(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20B404] */
        line1(SCDA_Q3BOSSPAL);                                /* (LoadPalette 6) */
        s_right = r16(RAM_Camera_max_X_pos);
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3MECA1, 4, 0x20, 0x18);
        set_lk(o, B_CHILD, spawn(o, 0x34, 0, 0x38));          /* [$20C2AE] the press */
        /* fall through */
    case 2: {                                                 /* [$20B444] */
        if (!btst(o, 1)) {
            if ((int16_t)r16(RAM_Distance_from_top) >= 0xC8) {
                s_bossflag = 5;                               /* (the level's events take the bottom) */
                s_bottom = r16(RAM_Camera_max_Y_pos);
                s_dest = r16(RAM_Camera_target_max_Y_pos);
                bset(o, 1);
            }
            w16(RAM_Distance_from_top, (uint16_t)(r16(RAM_Distance_from_top) + 6));
        }
        set_right(0xCC0);
        int16_t d0 = (int16_t)(wx(p) - 0xA0);                 /* (the left edge follows: no way back) */
        if (d0 < (int16_t)r16(RAM_Camera_min_X_pos)) break;
        if (wx(p) >= 0xD60) {                                 /* (in: shut, the boss's song) */
            bclr(o, 1);
            scd_cd_play(SCD_CD_BOSS, 1);
            s_bossstart = 5;
            w8(RAM_Boss_flag, 1);
            SET8(o, OBJ_routine, 6);
            set_right(0xCC0);
            d0 = 0xCC0;
        }
        set_left((uint16_t)d0);
        break;
    }
    case 4: {                                                 /* [$20B4C0] the way on opens */
        uint16_t r = (uint16_t)(r16(RAM_Camera_max_X_pos) + 6);
        w16(RAM_Camera_max_X_pos, r);
        w16(RAM_Camera_target_max_X_pos, (uint16_t)(r16(RAM_Camera_target_max_X_pos) + 6));
        if ((int16_t)s_right > (int16_t)r) break;
        s_bossstart = 0;
        w8(RAM_Boss_flag, 0);
        set_right(s_right);
        tri_scd_level_music();                                /* (the period's song again) */
        line1(SCDA_GENEPAL);                                  /* genecolor */
        SET8(o, OBJ_routine, 6);
        break;
    }
    case 6: {                                                 /* [$20B50E] */
        uint32_t m = lk_is(o, B_CHILD, 0x34);
        if (bclr(m, 7)) SET8(o, M1_SHAKE, 8);
        if (!OB8(o, M1_SHAKE)) break;
        SET8(o, M1_SHAKE, OB8(o, M1_SHAKE) - 1);
        int d0 = OB8(o, M1_SHAKE) & 1 ? -2 : 2;               /* (the events set it back next frame) */
        s_bottom = (uint16_t)(r16(RAM_Camera_max_Y_pos) + d0);
        s_dest = (uint16_t)(r16(RAM_Camera_target_max_Y_pos) + d0);
        w16(RAM_Camera_max_Y_pos, s_bottom);
        w16(RAM_Camera_target_max_Y_pos, s_dest);
        break;
    }
    }
    if (!btst(lk_is(o, B_CHILD, 0x34), 3)) o_draw(o);        /* (gone with its press) */
}

/* ---- $34 the press -------------------------------------------------------- */
/* S3K's bounce off a boss turns the hitter's speeds; CD's turns and halves x
 * and y, and leaves the ground speed */
static void hit_bounce(uint32_t o)
{
    uint32_t p = OB8(o, 0x1C) == (uint8_t)o_player(1) ? o_player(1) : RAM_Player_1;  /* (Touch_Enemy: $1C) */
    SET16(p, OBJ_x_vel, OBS16(p, OBJ_x_vel) >> 1);
    SET16(p, OBJ_y_vel, OBS16(p, OBJ_y_vel) >> 1);
    SET16(p, OBJ_ground_vel, (uint16_t)-OBS16(p, OBJ_ground_vel));
}

static void press(uint32_t o)                                 /* [$20B656] down, up a little, down: thumps */
{
    uint8_t ph = OB8(o, B_PHASE);
    if (ph == 1) {
        if (wy(o) >= OBS16(o, M2_UP)) { cd_move(o, 0, -0x8000); return; }
        o_sfx_cd(FM_THUMP);
        SET8(o, B_PHASE, 2);
        return;
    }
    if (wy(o) < OBS16(o, M2_DOWN)) { cd_move(o, 0, 0x13000); return; }
    o_sfx_cd(FM_THUMP);
    bset(o, 7);                                               /* (the housing shakes the camera) */
    sparks(o);
    if (!ph) { SET8(o, B_PHASE, 1); return; }
    if (!OB8(o, OBJ_frame)) {                                 /* (the first: it bites) */
        o_sfx_cd(FM_BUZZ);
        SET8(o, OBJ_frame, 1);
        anim_set(lk_is(o, B_CHILD, 0x32), 0);
    }
    SET8(o, OBJ_routine, 8);
    SET8(o, B_PHASE, 0);
}

static void meca2(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20B5A6] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3MECA2, 4, 0x20, 0x70);
        set_lk(o, B_CHILD, spawn(o, 0x32, 0, 0));             /* [$20C2DC] Eggman */
        spawn(o, 0x35, 0, 0x38);                              /* [$20C35C] the drill */
        spawn(o, 0x36, 0, 0);                                 /* [$20C338] the pipe */
        set_lk(o, M2_BELT, spawn(o, 0x22, 0, 0));             /* [$20C3E2] the treadmill */
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x3E));
        SET8(o, OBJ_coll_prop, 2);
        SET16(o, M2_HP, 0x960);
        /* fall through */
    case 2: {                                                 /* [$20B5F2] till spun into */
        static int autohit = -1;                              /* development: the hit after N frames */
        if (autohit < 0) autohit = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
        if (autohit > 0 && s_bossstart && !(r16(RAM_Level_frame_counter) % autohit)) SET8(o, OBJ_coll_flags, 0);
        if (OB8(o, OBJ_coll_flags)) break;
        if (OB8(o, OBJ_coll_prop) == 1) hit_bounce(o);        /* (Touch_Enemy's hit: 2 -> 1) */
        o_sfx_cd(FM_BOSSHIT);
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_coll_prop, 0);
        break;
    }
    case 4: {                                                 /* [$20B60E] it shudders a second */
        uint8_t t = (uint8_t)(OB8(o, B_TIME) + 1);
        SET8(o, B_TIME, t);
        if (t != 0x3C) {
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + (t & 1 ? 2 : -2)));
            break;
        }
        SET8(o, B_TIME, 0);
        SET8(o, OBJ_routine, 6);                              /* (CD's $10 = $13000: never read) */
        anim_set(lk_is(o, B_CHILD, 0x32), 1);
        SET16(o, M2_DOWN, 0x288);
        SET16(o, M2_UP, 0x280);
    }   /* fall through */
    case 6:                                                   /* [$20B656] */
        press(o);
        break;
    case 8: {                                                 /* [$20B6EC] the belt grinds it */
        solid(o);
        uint16_t hp = OB16(o, M2_HP);
        if (!(hp % 6)) {                                      /* (divu: a spark at each sixth) */
            if (!bset(o, 1)) spark1();
        } else
            bclr(o, 1);
        if ((int16_t)hp <= 0) {                               /* [$20B736] through */
            bset(o, 2);
            bset(lk_is(o, B_CHILD, 0x32), 2);
            SET8(o, OBJ_routine, 0xA);
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            uint32_t m1 = lk_is(o, B_PARENT, 0x33);
            if (m1) SET8(m1, OBJ_routine, 4);
            break;
        }
        if (hp != 0x708 && hp != 0x4B0 && hp != 0x258) break;
        {                                                     /* [$20B75C] a step down */
            static const int16_t step[4] = { 0, 8, 8, 8 };
            sparks(o);
            uint8_t f = OB8(o, OBJ_frame);
            int16_t d0 = step[f < 4 ? f : 3];
            SET16(o, M2_DOWN, (uint16_t)(wy(o) + d0));
            SET16(o, M2_UP, (uint16_t)(wy(o) + ((uint16_t)d0 >> 1)));
            SET8(o, OBJ_routine, 6);
            SET8(o, OBJ_frame, f + 1);
        }
        break;
    }
    case 0xA: {                                               /* [$20B796] a second of sparks */
        solid(o);
        uint8_t t = (uint8_t)(OB8(o, B_TIME) + 1);
        SET8(o, B_TIME, t);
        if (t == 0x3C) {
            SET8(o, B_TIME, 0);
            SET8(o, B_TIME2, 0);
            SET8(o, OBJ_routine, 0xC);                        /* (CD sets the driver's jumpCondition, Z80
                                                                 $1C3E: a held FM_BUZZ fades; the set's
                                                                 holds 255 frames at most) */
            break;
        }
        if (!(t % 0x12)) sparks(o);
        break;
    }
    case 0xC:                                                 /* [$20B7D6] it bursts, flickering; gone */
        if (OB8(o, B_TIME) >= 0x78) { bset(o, 3); break; }
        solid(o);
        SET8(o, B_TIME, OB8(o, B_TIME) + 1);
        bom_set(o);
        if (OB8(o, B_TIME) & 1) bclr(o, 3);
        else bset(o, 3);
        break;
    }
    if (btst(o, 3)) return;                                   /* [$20B550] (hidden) */
    SET16(o, OBJ_x_pos, 0xDD0);                               /* (it judders as the belt speeds) */
    uint32_t b = lk_is(o, M2_BELT, 0x22);
    int16_t sp = b ? OBS16(b, BL_SPEED) : 0;
    if (sp >= 0x400) {
        if (sp >= 0x500) SET16(o, OBJ_x_pos, 0xDD1);
        SET8(o, M2_JIG, OB8(o, M2_JIG) ^ 1);
        if (OB8(o, M2_JIG)) SET16(o, OBJ_x_pos, 0xDCF);
    }
    draw_touch(o);
}

/* ---- $35 the drill, $36 the pipe: on the press ------------------------- */
static void meca3(uint32_t o)
{
    uint32_t m = lk_is(o, B_PARENT, 0x34);
    if (!OB8(o, OBJ_routine)) {                               /* [$20B83C] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3MECA3, 5, 0x10, 0x10);
    }
    if (m) set_xy(o, wx(m), wy(m));                           /* [$20B866] */
    if (!btst(m, 3)) o_draw(o);
}

static void pipe(uint32_t o)
{
    uint32_t m = lk_is(o, B_PARENT, 0x34);
    if (!OB8(o, OBJ_routine)) {                               /* [$20B89A] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3PIPE, 4, 8, 0x30);
    }
    if (m) SET8(o, OBJ_frame, (uint8_t)((uint16_t)(wy(m) - wy(o)) / 0x20));   /* [$20B8C4] (longer as it goes down) */
    if (!btst(m, 3)) o_draw(o);
}

/* ---- $37 the claw: every three seconds a bomb, after the player to the
 * left; it drops it -------------------------------------------------------- */
static void lever(uint32_t o, int on)                         /* [$20BA30 / $20BA48] Eggman's hand on it */
{
    uint32_t e = lk_is(o, B_PARENT, 0x32);
    if (!e || OB8(e, OBJ_anim)) return;
    anim_set(e, 0);
    SET8(e, OBJ_frame, on);
}

static void claw(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    uint32_t m = lk(lk_is(o, B_PARENT, 0x32), B_PARENT);     /* (Eggman's press) */
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20B906] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3CATCH, 4, 0x10, 0x0C);
        /* fall through */
    case 2:                                                   /* [$20B932] till the press is spun into */
        if (m && OB8(m, OBJ_routine) == 2) break;
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4:                                                   /* [$20B94A] over the press */
        if (wx(o) >= 0xDC0) { SET8(o, OBJ_routine, 6); break; }
        cd_move(o, 0x1E000, 0);
        break;
    case 6: {                                                 /* [$20B964] */
        if ((m ? OBS16(m, M2_HP) : 0) <= 0xD2) { o_delete(o); return; }   /* (near through: no more) */
        uint8_t t = (uint8_t)(OB8(o, B_TIME) + 1);
        SET8(o, B_TIME, t);
        if (t != 0xB4) break;
        SET8(o, B_TIME, 0);
        o_sfx_cd(FM_GRAB);
        SET8(o, OBJ_routine, 8);
        lever(o, 1);
        spawn(o, 0x38, 0, 0x18);                              /* [$20C386] a bomb */
        anim_set(o, 1);
        break;
    }
    case 8:                                                   /* [$20B9A8] */
        cd_move(o, -0xC000, 0);
        if (wx(o) <= 0xDA0) SET8(o, OBJ_routine, 0xA);
        break;
    case 0xA:                                                 /* [$20B9C2] after the player, leftwards */
        if (wx(p) < wx(o) && wx(o) > 0xCE0) { cd_move(o, -0xC000, 0); break; }
        o_sfx_cd(FM_BUZZ);
        lever(o, 0);
        SET8(o, OBJ_routine, 0xC);
        break;
    case 0xC: {                                               /* [$20B9F8] the drop, back */
        uint8_t t = (uint8_t)(OB8(o, B_TIME) + 1);
        SET8(o, B_TIME, t);
        if (t == 0x1E) {
            lever(o, 1);
            bset(o, 6);                                       /* (the bomb goes) */
            anim_set(o, 0);
        } else if (t == 0x78) {
            SET8(o, B_TIME, 0);
            SET8(o, OBJ_routine, 4);
            lever(o, 0);
        }
        break;
    }
    }
    o_animate(o, o_asset_anim(SCDA_Q3CATCH));
    o_draw(o);
}

/* ---- $38 the bomb, $39 its shrapnel ------------------------------------ */
/* CD's special touch $FD (size $3D), while bossStart is 5: player 1 hurt
 * (unless he can't be), the bomb counts it and bursts (CD's player touches
 * before the objects move: the box as it was) */
static void bomb_touch(uint32_t o)
{
    if (s_bossstart != 5 || !OB8(o, OBJ_coll_flags)) return;
    if (!(o_s1_touch(o, 8, 8) & 1)) return;
    o_hurt_player(RAM_Player_1, o);
    SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) + 1);
}

static void shrapnel(uint32_t o)                              /* [$20C3B0] four, their parent the claw */
{
    for (int k = 3; k >= 0; k--) {
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET32(a1, OBJ_code, native3(0x39));
        SET16(a1, B_PARENT, OB16(o, B_PARENT));
        SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
        SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
        SET8(a1, OBJ_subtype, k);
    }
}

static void bomb(uint32_t o)
{
    bomb_touch(o);
    uint32_t c = lk_is(o, B_PARENT, 0x37);                    /* (the claw) */
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20BA7E] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3BOMB, 4, 0x0C, 0x0C);
        SET8(o, OBJ_coll_flags, 0xFD);                        /* (CD's: never put on S3K's touch list) */
        SET8(o, OBJ_coll_prop, 2);
        SET16(o, BM_BOB, (uint16_t)-3);
        SET16(o, BM_ANG, 0xC000);
        /* fall through */
    case 2:                                                   /* [$20BAC0] carried */
        if (c) SET16(o, OBJ_x_pos, OB16(c, OBJ_x_pos));
        if (!c || OB8(c, OBJ_routine) != 0xA) break;
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4:                                                   /* [$20BAD8] swinging under it */
        if (c) SET16(o, OBJ_x_pos, OB16(c, OBJ_x_pos));
        if (!bclr(c, 6)) {
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) - OBS16(o, BM_BOB)));
            uint16_t a = (uint16_t)(OB16(o, BM_ANG) + 0x480);
            SET16(o, BM_ANG, a);
            int16_t s, co;
            o_sincos((uint8_t)(a >> 8), &s, &co);
            int16_t d = (int16_t)(int16_t)(s * 3) >> 8;
            SET16(o, BM_BOB, (uint16_t)d);
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + d));
            break;
        }
        SET8(o, OBJ_routine, 6);
        /* fall through */
    case 6:                                                   /* [$20BB1C] falling */
        if (OB8(o, OBJ_coll_prop) > 2) { burst(o); return; }
        if (btst(c, 2)) { o_delete(o); return; }
        cd_move(o, 0, 0x22000);
        if (wy(o) < 0x2B8) break;
        o_sfx_cd(FM_THUMP);
        SET8(o, OBJ_routine, 8);
        /* fall through */
    case 8:                                                   /* [$20BB54] on the belt, eight frames */
        if (OB8(o, OBJ_coll_prop) > 2) { burst(o); return; }
        if (btst(c, 2)) { o_delete(o); return; }
        SET8(o, B_TIME, OB8(o, B_TIME) + 1);
        if (OB8(o, B_TIME) != 8) break;
        shrapnel(o);
        o_delete(o);
        return;
    }
    o_draw(o);
}

static void bomb2(uint32_t o)
{
    static const int32_t tab[4][6] = {                        /* [$20BCA8] x, y; speed x, y; its change */
        { 4, -12, 0xA000, -0x38000, 0, 0x3000 },
        { 4, -8, 0x14000, -0x1C000, 0, 0x1800 },
        { -4, -12, -0xA000, -0x38000, 0, 0x3000 },
        { -4, -8, -0x14000, -0x1C000, 0, 0x1800 },
    };
    bomb_touch(o);
    const int32_t *t = tab[OB8(o, OBJ_subtype) & 3];
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20BB9C] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3BOMB, 1, 8, 8);
        SET8(o, OBJ_coll_flags, 0xFD);
        SET8(o, OBJ_coll_prop, 2);
        SET8(o, OBJ_frame, 1);
        set_xy(o, wx(o) + t[0], wy(o) + t[1]);
        SET32(o, F_VX, (uint32_t)t[2]);
        SET32(o, F_VY, (uint32_t)t[3]);
        /* fall through */
    case 2:                                                   /* [$20BC02] a moment */
        if (OB8(o, OBJ_coll_prop) > 2) { burst(o); return; }
        SET8(o, B_TIME, OB8(o, B_TIME) + 1);
        if (OB8(o, B_TIME) != 4) break;
        SET8(o, B_TIME, 0);
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4:                                                   /* [$20BC22] flying */
        if (OB8(o, OBJ_coll_prop) > 2) { burst(o); return; }
        if (btst(lk_is(o, B_PARENT, 0x37), 2)) { o_delete(o); return; }
        cd_move(o, L(o, F_VX), L(o, F_VY));
        addL(o, F_VX, t[4]);
        addL(o, F_VY, t[5]);
        if (wy(o) >= 0x2C8 || !OB8(o, OBJ_coll_flags)) { o_delete(o); return; }
        break;
    }
    o_draw(o);
}

/* ---- $20 sparks: along the belt (left), or falling from the press ------- */
static void hibana(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20BD1E] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3HIBANA, 2, 8, 4);
        SET32(o, F_VX, (uint32_t)-0x20000);
        SET32(o, F_VY, (uint32_t)-0x20000);
        if (OB8(o, B_PHASE)) { SET8(o, OBJ_routine, 4); goto fall; }
        /* fall through */
    case 2:                                                   /* [$20BD66] */
        addL(o, F_VX, -0x1000);
        cd_move(o, L(o, F_VX), 0);
        if (wx(o) <= 0xD78) { o_delete(o); return; }
        break;
    case 4:                                                   /* [$20BD84] */
    fall:
        addL(o, F_VX, -0x1000);
        addL(o, F_VY, 0x2000);
        cd_move(o, L(o, F_VX), L(o, F_VY));
        break;
    }
    if (off_screen(o)) { o_delete(o); return; }
    o_animate(o, o_asset_anim(SCDA_Q3HIBANA));
    o_draw(o);
}

/* ---- $21 the spike wall: with the player past $C50, up out of the floor,
 * then in to $CD0; solid, its spikes hurt ------------------------------ */
static void hari(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20BDD6] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3HARI, 4, 0x20, 0x20);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xBF));
        SET8(o, OBJ_coll_prop, 2);
        /* fall through */
    case 2:                                                   /* [$20BE0C] */
        if (wx(p) < 0xC50) { draw_touch(o); return; }
        SET8(o, OBJ_routine, 4);                              /* (CD loads PLC 4, the capsule's art: resident here) */
        /* fall through */
    case 4:                                                   /* [$20BE26] up */
        cd_move(o, 0, -0x20000);
        if (wy(o) > 0x2A0) break;
        SET16(o, OBJ_y_pos, 0x2A0);
        SET8(o, OBJ_routine, 6);
        /* fall through */
    case 6:                                                   /* [$20BE44] in */
        cd_move(o, 0x20000, 0);
        if (wx(o) < 0xCD0) break;
        SET8(o, OBJ_routine, 8);
        SET16(o, OBJ_x_pos, 0xCD0);
        break;
    }
    o_solid(o, 0x0C + 10, 0x1E, 0x1F, OB16(o, OBJ_x_pos));   /* [$20BE66] (its body: CD's $C, $1E) */
    draw_touch(o);
}

/* ---- $22 the treadmill: from the first hit it carries player 1 left,
 * at the speed he runs right against it (192..1488), and grinds the press
 * by it; no further right than $D88 till it is through ------------------ */
static void belt_col(uint32_t o)                              /* [$20C09E] the belt's colours (line 2, 10-12) */
{
    static const uint16_t bad[9] = { 0x0CEE, 0x0466, 0x0000, 0x0000, 0x0CEE, 0x0466, 0x0466, 0x0000, 0x0CEE };
    static const uint16_t good[9] = { 0x0020, 0x0080, 0x06C2, 0x0080, 0x06C2, 0x0020, 0x06C2, 0x0020, 0x0080 };
    uint8_t k = (uint8_t)(OB8(o, BL_COL) + 6);
    if (k >= 0x12) k = 0;
    SET8(o, BL_COL, k);
    const uint16_t *c = (cd_period() == TRI_CD_GOOD ? good : bad) + k / 2;   /* (goodFuture) */
    for (unsigned j = 0; j < 3; j++) w16(RAM_Normal_palette + 0x54 + j * 2u, c[j]);
}

static void belt_anim(uint32_t o)                             /* [$20C060] faster, the faster it runs */
{
    int16_t sp = OBS16(o, BL_SPEED);
    int n = sp > 0x300 ? 1 : sp > 0x200 ? 2 : 4;
    uint8_t t = (uint8_t)(OB8(o, B_TIME) + 1);
    SET8(o, B_TIME, t);
    if (n > (int8_t)t) return;
    SET8(o, B_TIME, 0);
    belt_col(o);
}

static int belt_state(uint32_t p)                             /* [$20C0F8] -1 hurt, dying; 1 in the air */
{
    uint8_t a = OB8(p, OBJ_anim);                             /* (CD's anims $18-$1A: S3K's death, hurt the same;
                                                                 CD's oFlags bit 7: never the player's) */
    if (a == 0x18 || a == 0x19 || a == 0x1A) return -1;
    return OB8(p, OBJ_status) & 2 ? 1 : 0;
}

static void belt_speed(uint32_t o, uint32_t p, int d6)        /* [$20BF58] */
{
    int16_t sp = OBS16(o, BL_SPEED);
    if (d6 > 0) sp--;
    else if (d6 < 0) sp = 0xC0;
    else {
        int16_t v = OBS16(p, OBJ_x_vel);
        if (v < sp) sp -= 7;
        else if (v != sp) sp += 8;
    }
    if (sp <= 0xC0) sp = 0xC0;
    else if (sp >= 0x5D0) sp = 0x5D0;
    SET16(o, BL_SPEED, (uint16_t)sp);
}

static void belt_carry(uint32_t o, uint32_t m)                /* [$20BF3C] */
{
    uint32_t p = RAM_Player_1;
    int d6 = belt_state(p);
    if (!btst(m, 2)) belt_speed(o, p, d6);
    if (d6 == 1) {                                            /* [$20C01A] off it: the speed it gave him off */
        if (!bset(o, 4)) {
            SET16(p, OBJ_x_vel, (uint16_t)(OBS16(p, OBJ_x_vel) - OBS16(o, BL_XV)));
            SET16(p, OBJ_y_vel, (uint16_t)(OBS16(p, OBJ_y_vel) - OBS16(o, BL_YV)));
        }
    } else {                                                  /* [$20BFC4] */
        bclr(o, 4);
        SET16(o, BL_XV, OB16(p, OBJ_x_vel));
        SET16(o, BL_YV, OB16(p, OBJ_y_vel));
        if (d6 >= 0) {                                        /* (left, by its speed: at most $300 more than his) */
            int32_t d0 = OB16(o, BL_SPEED);
            if ((int16_t)(OB16(o, BL_SPEED) - OB16(p, OBJ_x_vel)) > 0x300)
                d0 = (int16_t)(0x300 + OBS16(p, OBJ_x_vel));
            SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) - (uint32_t)(d0 * 256));
        }
    }
    if (!btst(m, 2) && wx(p) >= 0xD88) SET16(p, OBJ_x_pos, 0xD88);   /* [$20C048] */
}

static void belt(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    uint32_t m = lk_is(o, B_PARENT, 0x34);                    /* (the press) */
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20BEA4] */
        SET8(o, OBJ_routine, 2);
        belt_col(o);
        return;
    case 2:                                                   /* [$20BEAC] till the press is spun into */
        if (m && OB8(m, OBJ_routine) == 2) return;
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4:                                                   /* [$20BEBA] the player put off the press's side */
        belt_anim(o);
        if (wx(p) > 0xD88) {
            SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) - 3);
            if (OBS16(p, OBJ_x_vel) >= 0) SET16(p, OBJ_x_vel, 0);
            return;
        }
        SET16(o, BL_SPEED, 0xC0);
        SET8(o, OBJ_routine, 6);
        return;
    case 6:                                                   /* [$20BF0A] */
        if (btst(m, 2)) {                                     /* (through: it runs down) */
            SET16(o, BL_SPEED, OB16(o, BL_SPEED) - 0x10);
            if (OBS16(o, BL_SPEED) <= 0) { SET8(o, OBJ_routine, 8); return; }
        }
        belt_carry(o, m);
        {                                                     /* [$20BFA8] the press ground */
            uint16_t g = (uint16_t)(OB16(o, BL_GRIND) + OB16(o, BL_SPEED));
            if ((int16_t)g >= 0x5D0) {
                if (m) SET16(m, M2_HP, OB16(m, M2_HP) - 1);
                g = (uint16_t)(g - 0x5D0);
            }
            SET16(o, BL_GRIND, g);
            static int autohit = -1;                          /* development: N more a frame */
            if (autohit < 0) autohit = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
            if (autohit > 0 && m && (int16_t)OB16(m, M2_HP) > 0) SET16(m, M2_HP, OB16(m, M2_HP) - 1);
        }
        belt_anim(o);
        return;
    }
}

/* ---- $23 an explosion off the press: up, 3 px a frame ------------------ */
static void bakuha(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20C154] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_Q3BAKUHA, 3, 0x10, 0x10);
        /* fall through */
    case 2:                                                   /* [$20C17E] */
        cd_move(o, 0, -0x30000);
        break;
    default:                                                  /* (4: its script's end) */
        o_delete(o);
        return;
    }
    o_animate(o, o_asset_anim(SCDA_Q3BAKUHA));
    o_draw(o);
}

/* ---- the act's own: its camera events and its belts' palette cycle ------ */
static int arena_exit(int16_t x0, uint16_t d1)                /* [$2036F0] bossclr_scr */
{
    uint32_t p = RAM_Player_1;
    if (wx(p) < x0) return 0;
    s_dest = d1;
    int16_t d = (int16_t)(d1 - s_bottom);
    if ((d < 0 ? -d : d) < 4) s_bottom = s_dest;
    int16_t x = (int16_t)(wx(p) - 0xA0);                      /* (the left edge follows: no way back) */
    if (x < (int16_t)r16(RAM_Camera_min_X_pos)) return 1;
    if (x > (int16_t)r16(RAM_Camera_max_X_pos)) x = (int16_t)r16(RAM_Camera_max_X_pos);
    set_left((uint16_t)x);
    return 1;
}

/* zone53chk [$2035A2], then the bottom's move [$2033F0]. The disc runs them
 * after the camera (LevelScroll, this disc's order: the press's shake shows
 * for the frame), here before the objects: the same, a frame round */
static void events(void)
{
    uint32_t p = RAM_Player_1;
    if (!s_bossflag || OB8(p, OBJ_routine) >= 6) return;     /* (till then the level's 800, as converted) */
    if (!arena_exit(0xE10, 0x1F8)) s_bottom = s_dest = 0x1F8;
    if (s_dest != s_bottom) {                                 /* (4 px a frame, 16 falling near it) */
        uint16_t cy = r16(RAM_Camera_Y_pos);
        int16_t d1 = 4;
        if (s_dest > s_bottom) {
            if ((uint16_t)(cy + 8) >= s_bottom && (OB8(p, OBJ_status) & 2)) d1 *= 4;
        } else {
            d1 = -4;
            if (cy > s_dest) s_bottom = cy & 0xFFFE;
        }
        s_bottom = (uint16_t)(s_bottom + d1);
    }
    if (r16(RAM_Camera_max_Y_pos) != s_bottom) w8(RAM_Camera_max_Y_pos_changing, 1);   /* (S3K's MoveCameraY clamps then) */
    w16(RAM_Camera_max_Y_pos, s_bottom);                      /* (both: S3K's own move stays out) */
    w16(RAM_Camera_target_max_Y_pos, s_bottom);
}

/* [$200184] the belts' colours (line 2, 10-12), three steps, one each 5
 * frames; not while bossFlag (the treadmill has them). R53D's two more run
 * only with $FF156C bit 0, which act 3 never sets */
static void palcycle(void)
{
    static const uint16_t good[2][9] = {
        { 0x0020, 0x0080, 0x06C2, 0x06C2, 0x0020, 0x0080, 0x0080, 0x06C2, 0x0020 },
        { 0x0404, 0x0A08, 0x0E0E, 0x0A08, 0x0E0E, 0x0404, 0x0E0E, 0x0404, 0x0A08 },   /* (colrevflag: no lever here) */
    };
    static const uint16_t bad[2][9] = {
        { 0x0CEE, 0x0466, 0x0000, 0x0000, 0x0CEE, 0x0466, 0x0466, 0x0000, 0x0CEE },
        { 0x0A0E, 0x080A, 0x0604, 0x080A, 0x0604, 0x0A0E, 0x0604, 0x0A0E, 0x080A },
    };
    if (s_bossflag) return;
    if (--s_cyc_time >= 0) return;
    s_cyc_time = 4;
    if (++s_cyc_step >= 3) s_cyc_step = 0;
    const uint16_t *c = (cd_period() == TRI_CD_GOOD ? good : bad)[scd_qqz_colrev() ? 1 : 0] + s_cyc_step * 3;
    for (unsigned k = 0; k < 3; k++) w16(RAM_Normal_palette + 0x54 + k * 2u, c[k]);
}

int scd_qqz3_bossflag(void) { return qqz3() ? s_bossflag : 0; }   /* bossFlag (the player code's belts) */

void scd_qqz3_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {
        s_bossflag = s_bossstart = 0;
        s_cyc_time = 0;
        s_cyc_step = 0;
        return;
    }
    if ((pc != TH_LVL_PRE_SPRITES && pc != TH_LVL_POST_SPRITES) || !tri_dir() || !qqz3()) return;
    if (pc == TH_LVL_PRE_SPRITES) {
        events();
        static int log = -1;                                  /* development */
        if (log < 0) log = getenv("TRILOGY_BOSSLOG") != NULL;
        if (log && !(r16(RAM_Level_frame_counter) & 7))
            fprintf(stderr, "[boss5] f=%u flag=%u start=%u p=%04X,%04X xv=%d cam=%04X,%04X l/r=%04X,%04X bottom=%04X\n",
                    (unsigned)r16(RAM_Level_frame_counter), s_bossflag, s_bossstart, OB16(RAM_Player_1, OBJ_x_pos),
                    OB16(RAM_Player_1, OBJ_y_pos), OBS16(RAM_Player_1, OBJ_x_vel), r16(RAM_Camera_X_pos),
                    r16(RAM_Camera_Y_pos), r16(RAM_Camera_min_X_pos), r16(RAM_Camera_max_X_pos),
                    r16(RAM_Camera_max_Y_pos));
    } else
        palcycle();
}

void scd_qqz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x15] = cd_capsule;
    t[base3 + 0x20] = hibana;
    t[base3 + 0x21] = hari;
    t[base3 + 0x22] = belt;
    t[base3 + 0x23] = bakuha;
    t[base3 + 0x32] = egg5;
    t[base3 + 0x33] = meca1;
    t[base3 + 0x34] = meca2;
    t[base3 + 0x35] = meca3;
    t[base3 + 0x36] = pipe;
    t[base3 + 0x37] = claw;
    t[base3 + 0x38] = bomb;
    t[base3 + 0x39] = bomb2;
}
