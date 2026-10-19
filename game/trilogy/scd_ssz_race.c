/*
 * scd_ssz_race.c — Stardust Speedway act 3 (Sonic CD R73C / R73D): the race
 * with Metal Sonic, Eggman's laser behind, Amy at the end. Transcribed from
 * the Mega-CD program (the routines' R73C addresses in brackets; R73D's code
 * is the same); names from the PC port's BOSS_7.C, WALL73.C, HARI73.C,
 * EMIE7.C, GAITOU73.C (where the disc differs, the disc is followed).
 *
 * Eggman ($2C) builds the rest: his two jets ($2D), a laser of four links
 * ($2E, subtype 0; the bottom one makes the smoke, subtype 1, and the debris,
 * $37) and Metal Sonic ($2F). As the player comes (x $BE0) the boss music
 * starts and the camera is held at $B40; Eggman shows off, the laser flickers,
 * Metal Sonic takes his stance, fires up and the start gate ($2B) rises:
 * the race (the bad future's song, in both futures). Eggman chases with the
 * laser at the camera's left edge (it follows him) and kills the player it
 * reaches (16 px); his speed by how far behind he is. Metal Sonic runs on the
 * level's floor, hopping at fixed places, 2.5 then 4 px a frame by turns;
 * near the player for long he dashes ($BC) or, the player behind him, gives
 * off an electric field ($30, $BD); from x $3500 he speeds up or slows down
 * instead. He smashes the spike blocks ($32) in his way. At x $3E90 the race
 * is won: the finish barrier ($2B) drops and the loser is held behind x
 * $3E48. Sonic first: Metal Sonic crashes into it in pieces ($31), Eggman
 * stops the laser and flies off; Amy's colours go on line 1. Metal Sonic
 * first: he waits at the post and the laser catches the player (Sonic CD
 * kills him: kept). Amy ($33) waits in a capsule: a ball breaks it; she runs
 * to the player and hugs him if he walks by (hearts, $34): that ends the act
 * (results), as do 600 frames after the capsule broke.
 * $36: the street lamps in front, by the camera (one placed, it makes two).
 *
 * State the disc keeps for others: bossFlags ($F7A7): +7 as Eggman starts;
 * bit 7 the gates move (each move of the last ends it). bossFight ($F7AA, 7)
 * no object here reads. S3K's Boss_flag is set while he races.
 * The camera: Sonic CD's holds the player at scra_hline; while he is within
 * 160 px of the left edge the race makes that his place, so the camera keeps
 * to the edge. S3K's camera has no such point: it is moved as far as Sonic
 * CD's would go right (16 px a frame), then follows its own rule.
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (named below), CD's $28 /
 * $29 S3K's $2C / $2D. Collision: CD's hurt types ($BC-$BE) are S3K's.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"

/* this act's asset ids (scd_level.py CD_ASSETS; -> trilogy_obj.h) */

#define FM_EXPLODE      0x9E            /* (make_bakuha [$20D094]: he crashes; emie7_init [$20EFC6]: the capsule) */
#define FM_SMASH        0xA3            /* (msnc_hit [$20EE0A]: he smashes a spike block) */
#define FM_STANCE       0xC9            /* (msnc_act_ini [$20C254]: his stance) */
#define FM_JET          0xCA            /* (msnc_f_atc_tobi [$20C2A4]: his dash) */
#define FM_FIELD        0xD0            /* (msnc_b_atc_atack [$20C330]: his electric field) */
#define FM_GOAL         0xBB            /* (goal_chk [$20BFDE / $20C042]: the race won, the gates move) */

#define P_STATUS2       0x2B            /* status_secondary (bit 1 invincible) */
#define P_INVULN        0x34            /* invulnerability_timer */
#define OBJ_prev_anim   0x21

/* CD $30 / $32 / $34: the speeds' steps and top (add_spd2 / add_spd3) */
#define AX              0x36            /* oVar30 */
#define AY              0x38            /* oVar32 */
#define MAX             0x3A            /* oVar34 */
#define TIME            0x30            /* oVar2A (a word; a byte for b_timer) */
#define STEP            0x33            /* oVar2D */

void tri_scd_level_music(void);
uint32_t tri_scd_palpoint(unsigned id);
int scd_good_future(void);

static int s_base3;                     /* act 3's native slots (scd_obj.c) */
static uint8_t s_flags;                 /* bossFlags */
static uint16_t s_right;                /* (the level's right edge: CD's CamBounds) */

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }
static uint32_t lk(uint32_t o, unsigned f)
{
    uint16_t w = OB16(o, f);
    return w ? 0xFF0000u | w : 0;
}
/* (CD tests a link's id byte; S3K reuses slots: its code) */
static int alive(uint32_t a, int id) { return a && r32(a) == native3(id); }
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }
static void flag(uint32_t o, unsigned f, uint8_t set, uint8_t clr) { SET8(o, f, (OB8(o, f) | set) & ~clr); }

static void add_spd(uint32_t o)                               /* [$20CEB0] */
{
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, (int32_t)OBS16(o, OBJ_y_vel) * 256);
}
static void add_spd2(uint32_t o)                              /* [$20CEA0] */
{
    SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, AX));
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, AY));
    add_spd(o);
}
static void add_spd3(uint32_t o)                              /* [$20CE68]: x speed to its top */
{
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, AY));
    int16_t ax = OBS16(o, AX), top = OBS16(o, MAX);
    SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + (uint16_t)ax);
    int16_t vx = OBS16(o, OBJ_x_vel);
    if (ax > 0 ? top <= vx : ax < 0 && top >= vx) SET16(o, OBJ_x_vel, top);
    add_spd(o);
}
static void w_timer(uint32_t o)                               /* [$20CECA] */
{
    SET16(o, TIME, OB16(o, TIME) - 1);
    if (!OB16(o, TIME)) SET8(o, STEP, OB8(o, STEP) + 1);
}
static void b_timer(uint32_t o)                               /* [$20CED2] */
{
    SET8(o, TIME, OB8(o, TIME) - 1);
    if (!OB8(o, TIME)) SET8(o, STEP, OB8(o, STEP) + 1);
}
/* [$20CEE0]: at the frame of the step's entry, its call (and the next step) */
typedef struct { int16_t t; void (*fn)(uint32_t o); } Event;
static void event_timer(uint32_t o, const Event *ev, unsigned n)
{
    unsigned k = OB8(o, STEP);
    int16_t t = OBS16(o, TIME);
    SET16(o, TIME, t + 1);
    if (k >= n || ev[k].t != t) return;
    SET8(o, STEP, k + 1);
    ev[k].fn(o);
}

/* the floor under them: egg_move_tbl [$20CABA]: near these x (64 px), a hop
 * by dx, dy instead of a fall or a climb */
static const int16_t hop_tbl[][3] = {
    { 0x0C30, 0, 0 }, { 0x1610, 0, -2 }, { 0x1858, 0, -16 }, { 0x2220, 0, -2 }, { 0x2490, 0, -16 },
    { 0x2820, 0, -2 }, { 0x2A79, 0, -16 }, { 0x2C60, 0, -16 }, { 0x2E60, 0, -16 }, { 0x2FC0, 0, 0 },
    { 0x3020, 0, -2 }, { 0x3030, 0, -2 }, { 0x3E30, 0, 0 }, { 0x3FB0, 0, 0 },
};
static int hop(uint32_t o)                                    /* hight_set_retry [$20CA8E]: 1 = hopped */
{
    for (unsigned k = 0; k < sizeof hop_tbl / sizeof hop_tbl[0]; k++) {
        int16_t d2 = (int16_t)(hop_tbl[k][0] - wx(o));
        if (d2 <= 0) d2 = (int16_t)-d2;
        if (d2 > 0x40) continue;
        set_xy(o, wx(o) + hop_tbl[k][1], wy(o) + hop_tbl[k][2]);
        return 1;
    }
    return 0;
}
static void hight_set(uint32_t o)                             /* [$20CA64]: on the floor (8 px) */
{
    int16_t d1 = o_floor_dist(o);
    if ((d1 > 8 || d1 < -8) && hop(o)) d1 = 0;
    SET16(o, OBJ_y_pos, wy(o) + d1);
}
static void egg_hight(uint32_t o)                             /* [$20C9FC]: 128 px over the floor */
{
    SET16(o, OBJ_y_pos, wy(o) + 0x80);
    hight_set(o);
    SET16(o, OBJ_y_pos, wy(o) - 0x80);
}
#define M_FALL          0x2C            /* (CD's $28, a word: Metal Sonic's fall speed) */
static void msnc_hight(uint32_t o)                            /* hight_set2 [$20CA20]: or falls */
{
    int16_t d1 = o_floor_dist(o);
    if (d1 > 8) {
        if (!hop(o)) {
            SET16(o, M_FALL, OB16(o, M_FALL) + 0x30);
            SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)((int32_t)OBS16(o, M_FALL) << 8));
            return;
        }
        d1 = 0;
    }
    SET16(o, M_FALL, 0);
    SET16(o, OBJ_y_pos, wy(o) + d1);
}

static void set_left(int16_t x) { w16(RAM_Camera_min_X_pos, (uint16_t)x); w16(RAM_Camera_target_min_X_pos, (uint16_t)x); }
static void set_right(int16_t x) { w16(RAM_Camera_max_X_pos, (uint16_t)x); w16(RAM_Camera_target_max_X_pos, (uint16_t)x); }

/* line 1 from a palette asset: Sonic CD's LoadPalette writes the palette, its
 * clrset both the palette and the fade's target */
static void line1(int asset, int both)
{
    uint32_t pal = o_asset_anim(asset);
    if (!pal) return;
    for (unsigned k = 0; k < 16; k++) {
        uint16_t c = tri_rd16(pal + k * 2u);
        w16(RAM_Normal_palette + 0x20 + k * 2u, c);
        if (both) w16(RAM_Target_palette + 0x20 + k * 2u, c);
    }
}

/* ======================================================================
 * $2C Eggman
 * ==================================================================== */
#define E_FLAGS  0x32                   /* oVar2C: bit 2 on the screen, 3 jets lit, 4 the chase (the laser
                                         * kills), 5 the laser hurts, 6 the laser shown */
#define E_LINK   0x34                   /* oVar2E: Metal Sonic */
#define E_CAM    0x3E                   /* oVar38: bit 7 the left edge follows him; low: the right edge
                                         * 1 $B40, 2 $3DA0, 3 the level's */
#define E_GOAL   0x3F                   /* oVar39: the race's winner (1 Sonic, 2 Metal Sonic); bit 7 settled */
#define E_KEEP   0x40                   /* oVar3A (word): the catch-up speed's time */
#define E_BEHIND 0x42                   /* oVar3C (word): frames off the screen's left */

/* the jets, the laser, Metal Sonic: their fields */
#define J_LINK   0x34                   /* oVar2E: Eggman */
#define J_DX     0x3C                   /* oVar36 */
#define J_DY     0x3E                   /* oVar38 */
#define B_T1     0x30                   /* oVar2A: the smoke's time (the smoke's, the blast's own time) */
#define B_T2     0x31                   /* oVar2B: the debris' time */
#define B_FLAGS  0x32                   /* oVar2C: Eggman's bits 5, 6 down the laser */
#define B_LINK   0x34                   /* oVar2E: the link above (the first: Eggman; the blast: Metal Sonic) */
#define B_DX     0x3C                   /* oVar36 */
#define B_DY     0x3E                   /* oVar38 */
#define B_EGG    0x40                   /* oVar3A: Eggman */
#define B_PID    0x45                   /* (the id of the one above) */
#define B_LAST   0x2D                   /* (CD's subtype 2: the bottom link) */
#define M_FLAGS  0x32                   /* oVar2C: bit 5 racing, 6 the player ahead */
#define M_LINK   0x34                   /* oVar2E: Eggman */
#define M_MODE   0x3C                   /* oVar36: 1 fast, 2 slow (from x $3500), 0 by turns */
#define M_COUNT  0x3E                   /* oVar38 (word): frames racing */
#define M_NEAR   0x40                   /* oVar3A (word): + frames within 160 px of the player, - beyond */
#define M_CLOSE  0x42                   /* oVar3C (word): frames 16-159 px from him */
#define M_MTIME  0x44                   /* oVar3E (word): the mode's time */

static void make_jet(uint32_t o)                              /* [$20CFBC] */
{
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) return;
        SET16(a1, J_LINK, (uint16_t)o);
        SET32(a1, OBJ_code, native3(0x2D));
        SET8(a1, OBJ_subtype, k);
        SET8(a1, OBJ_anim, k);
    }
}
static void make_msnc(uint32_t o)                             /* [$20CF98] */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET16(a1, M_LINK, (uint16_t)o);
    SET16(o, E_LINK, (uint16_t)a1);
    SET32(a1, OBJ_code, native3(0x2F));
    set_xy(a1, 0xC30, 0x1CD);
}
static void make_beam(uint32_t o)                             /* [$20CFFA]: four links down */
{
    uint32_t up = o;
    for (int d2 = 3; d2 >= 0; d2--) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) return;
        SET16(a1, B_LINK, (uint16_t)up);
        SET16(a1, B_EGG, (uint16_t)o);
        SET8(a1, B_PID, up == o ? 0x2C : 0x2E);
        SET32(a1, OBJ_code, native3(0x2E));
        up = a1;
        if (d2 == 3) { SET16(a1, B_DX, 0); SET16(a1, B_DY, 0x34); continue; }
        if (!d2) SET8(a1, B_LAST, 1);
        SET16(a1, B_DX, 2);
        SET16(a1, B_DY, 0x20);
    }
}

/* the camera's edges [$20C97C] (egg7_hscr) */
static void cam_follow(int16_t hline)                         /* (Sonic CD's camera: the player at hline) */
{
    int16_t cam = (int16_t)r16(RAM_Camera_X_pos), d = (int16_t)(wx(RAM_Player_1) - cam - hline);
    if (d <= 0) return;                                       /* (left: S3K's own) */
    if (d > 16) d = 16;
    cam = (int16_t)(cam + d);
    if (cam > (int16_t)r16(RAM_Camera_max_X_pos)) cam = (int16_t)r16(RAM_Camera_max_X_pos);
    w16(RAM_Camera_X_pos, (uint16_t)cam);
}
static void egg_cam(uint32_t o)
{
    uint8_t m = OB8(o, E_CAM);
    if (m & 0x80) {
        int16_t d0 = (int16_t)(wx(o) - 8);
        if (d0 > 0x3DA0) d0 = 0x3DA0;
        set_left(d0);
        d0 = (int16_t)(wx(RAM_Player_1) - d0);
        if (d0 < 0xA0) cam_follow((int16_t)(d0 & ~1));       /* (scra_hline: his place from the edge) */
    }
    switch (m & 0xF) {
    case 1: set_right(0xB40); break;
    case 2: set_right(0x3DA0); break;
    case 3: set_right((int16_t)s_right); break;
    }
}

static void egg_beamchk(uint32_t o)                           /* [$20C940]: the laser reaches the player */
{
    uint32_t p = RAM_Player_1;
    if (!(OB8(o, E_FLAGS) & 0x10) || !o_player_active(p) || OB8(p, OBJ_routine) >= 6) return;
    if ((int16_t)(wx(p) - wx(o)) > 0x10) return;
    o_kill_player(p);                                         /* (CD marks his status bit 7 first: nothing here reads it) */
}

static void egg_maxspd(uint32_t o, int16_t d1)                /* [$20C8FE]: by how far behind */
{
    static const int16_t spd[4][2] = { { 0x200, 2 }, { 0x300, 2 }, { 0x440, 8 }, { 0x600, 4 } };
    int16_t px = wx(RAM_Player_1);
    int k = d1 <= -0xA0 ? 3 : px >= 0x3100 ? 2 : px >= 0xDC0 ? 1 : 0;
    SET16(o, MAX, spd[k][0]);
    SET16(o, AX, spd[k][1]);
}
static void egg_spdset(uint32_t o)                            /* [$20C8A6] */
{
    int16_t d1 = (int16_t)(wx(o) - (int16_t)r16(RAM_Camera_X_pos) + 0x24);   /* (+ his width) */
    if (d1 < 0) {
        SET16(o, E_BEHIND, OB16(o, E_BEHIND) + 1);
        egg_maxspd(o, d1);
        if (OB16(o, E_KEEP)) { SET16(o, E_KEEP, OB16(o, E_KEEP) - 1); return; }
        flag(o, E_FLAGS, 0, 4);
        return;
    }
    SET16(o, E_BEHIND, 0);
    egg_maxspd(o, d1);
    if (OB8(o, E_FLAGS) & 4) return;
    flag(o, E_FLAGS, 4, 0);                                   /* (on the screen again: the player's speed, 240 frames) */
    SET16(o, E_KEEP, 0xF0);
    int16_t v = OBS16(RAM_Player_1, OBJ_x_vel);
    SET16(o, OBJ_x_vel, v < 0x100 ? 0x100 : v);
}

/* egg7_demo2's script [$20C658] */
static void msnc_turn(uint32_t o)                             /* [$20C688] Metal Sonic's turn */
{
    uint32_t m = lk(o, E_LINK);
    if (!alive(m, 0x2F)) return;
    SET8(m, OBJ_routine, 4);
    SET16(m, TIME, 0);
    SET8(m, STEP, 0);
}
static void beam_on(uint32_t o) { flag(o, E_FLAGS, 0x60, 0); }        /* [$20C69C] */
static void beam_on2(uint32_t o) { flag(o, E_FLAGS, 0x40, 0x20); }    /* [$20C6AA] (shown, harmless) */
static void beam_off(uint32_t o) { flag(o, E_FLAGS, 0, 0x60); }       /* [$20C6B8] */
static void move_start(uint32_t o)                            /* [$20C6C6] */
{
    flag(o, E_FLAGS, 8, 0);
    SET16(o, OBJ_x_vel, -0x200);
    SET16(o, AX, 0);
}
static void beam_next(uint32_t o)                             /* [$20C6D8]: the chase */
{
    beam_on2(o);
    SET8(o, OBJ_routine, 6);
    flag(o, E_FLAGS, 0x10, 0);
    SET16(o, OBJ_x_vel, 0x200);
    SET16(o, MAX, 0x200);
    SET16(o, AX, 2);
    SET16(o, OBJ_y_vel, 0);
    SET16(o, AY, 0);
    flag(o, OBJ_status, 0, 1);
    SET8(o, STEP, 0);
    SET16(o, TIME, 0);
}
static const Event egg_demo2[] = {
    { 120, msnc_turn }, { 180, beam_on2 }, { 184, beam_on }, { 185, move_start }, { 190, beam_off },
    { 220, beam_on2 }, { 224, beam_on }, { 230, beam_off }, { 260, beam_on2 }, { 264, beam_on },
    { 270, beam_off }, { 420, beam_next },
};

static void egg_demo1(uint32_t o)                             /* [$20C516]: the show before the race */
{
    uint32_t p = RAM_Player_1;
    egg_hight(o);
    switch (OB8(o, STEP)) {
    case 0: {                                                 /* snc_wait [$20C536] */
        SET8(o, E_CAM, (OB8(o, E_CAM) & 0xF0) + 1);
        if (wx(p) >= 0xBE0) {
            SET16(o, OBJ_x_vel, -0x600);
            SET16(o, AX, 0);
            SET8(o, STEP, 1);
            flag(o, OBJ_status, 1, 0);
            set_left(0xB40);
            scd_cd_play(SCD_CD_BOSS, 1);                      /* (SubCPUCmd $67) */
            return;
        }
        int16_t d0 = (int16_t)(wx(p) - 0xA0);                 /* (the left edge follows him to $B40) */
        if (d0 < (int16_t)r16(RAM_Camera_min_X_pos) || d0 > 0xB40) return;
        set_left(d0);
        return;
    }
    case 1:                                                   /* r_l [$20C5A2] */
        add_spd2(o);
        if (0xB18 < wx(o)) return;
        SET16(o, OBJ_x_pos, 0xB18);
        SET16(o, OBJ_x_vel, 0x600);
        SET16(o, AX, 0);
        SET8(o, STEP, 2);
        flag(o, OBJ_status, 0, 1);
        SET16(o, TIME, 0x78);
        return;
    case 3:                                                   /* l_r [$20C5D4] */
        add_spd2(o);
        if (0xCA8 > wx(o)) return;
        SET16(o, OBJ_x_pos, 0xCA8);
        SET16(o, OBJ_x_vel, -0x600);
        SET16(o, AX, 0);
        flag(o, OBJ_status, 1, 0);
        SET8(o, STEP, 4);
        SET16(o, TIME, 0x78);
        return;
    case 5:                                                   /* r_l2 [$20C606] */
        add_spd2(o);
        if (0xC20 < wx(o)) return;
        SET16(o, OBJ_x_pos, 0xC20);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, AX, 0);
        SET8(o, STEP, 0);
        SET16(o, TIME, 0);
        flag(o, E_FLAGS, 0, 8);
        SET8(o, OBJ_routine, 4);
        return;
    default:                                                  /* (2, 4) */
        w_timer(o);
        return;
    }
}

static int egg_goal(uint32_t o)                               /* [$20C73E]: 0 = gone */
{
    switch (OB8(o, STEP)) {
    case 0: {                                                 /* [$20C762] */
        uint8_t g = OB8(o, E_GOAL) & 0xF;
        if (g == 2) SET8(o, STEP, 1);                         /* (Metal Sonic's: he stays, the laser on) */
        else if (g == 1) {                                    /* [$20C780] Sonic's */
            flag(o, E_FLAGS, 0, 0x30);
            SET8(o, E_CAM, OB8(o, E_CAM) & 0xF);
            SET8(o, STEP, 2);
            SET16(o, TIME, 0x1E);
        }
        break;
    }
    case 1: SET8(o, STEP, 1); break;                          /* [$20C778] */
    case 3: {                                                 /* [$20C7A0]: the laser blinks */
        uint16_t t = (uint16_t)(OB16(o, TIME) + 1);
        SET16(o, TIME, t);
        if ((int16_t)t < 0x78) {
            if (!(t % 20)) SET8(o, E_FLAGS, OB8(o, E_FLAGS) ^ 0x40);
            break;
        }
        flag(o, E_FLAGS, 0, 0x40);
        SET8(o, STEP, 4);
        SET16(o, TIME, 0x1E);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0x200);
        break;                                                /* (LoadPLC 4, Amy's art: here an asset, there already) */
    }
    case 5:                                                   /* [$20C7E8] down */
        add_spd(o);
        if (wy(o) < 0x190) break;
        SET16(o, OBJ_x_vel, 0x600);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, STEP, 6);
        SET16(o, TIME, 0x3C);
        break;
    case 7:                                                   /* [$20C80C] away */
        add_spd(o);
        if (wx(o) < 0x3F60) break;
        SET8(o, STEP, 8);
        SET16(o, TIME, 0x3C);
        break;
    case 9:                                                   /* [$20C826] */
        tri_scd_level_music();                                /* (SubCPUCmd $1F / $20: the period's song) */
        SET8(o, E_CAM, 3);
        egg_cam(o);
        /* (genecolor's line 1 goes at once under Amy's) */
        line1(SCDA_S7_AMYPAL, 1);
        w8(RAM_Boss_flag, 0);
        o_delete(o);
        return 0;
    default:                                                  /* (2, 4, 6, 8) */
        w_timer(o);
        break;
    }
    return 1;
}

static int egg_step(uint32_t o)                               /* 0 = gone */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* egg7_ini [$20C4BA] */
        o_init(o, SCDA_S7_EGGMAN, 4, 1, 0x24, 0x24);
        SET8(o, OBJ_y_radius, 0x24);
        s_flags = (uint8_t)(s_flags + 7);                     /* (bossFlags += 7; bossFight = 7) */
        w8(RAM_Boss_flag, 1);
        s_right = r16(RAM_Camera_max_X_pos);
        line1(SCDA_S7_BOSSPAL, 0);                            /* (LoadPalette 6) */
        SET8(o, OBJ_routine, 2);
        make_jet(o);
        flag(o, E_FLAGS, 8, 0);
        make_msnc(o);
        make_beam(o);
        break;
    case 2: egg_demo1(o); break;
    case 4:                                                   /* egg7_demo2 [$20C636] */
        add_spd2(o);
        if (!(0xB00 < wx(o))) SET16(o, OBJ_x_pos, 0xB00);
        egg_hight(o);
        event_timer(o, egg_demo2, sizeof egg_demo2 / sizeof egg_demo2[0]);
        break;
    case 6:                                                   /* egg7_move [$20C710] */
        if (wx(o) < 0x3E50) {
            add_spd3(o);
            egg_spdset(o);
            egg_hight(o);
            break;
        }
        SET16(o, OBJ_x_pos, 0x3E50);
        SET8(o, OBJ_routine, 8);
        SET8(o, STEP, 0);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0);
        break;
    case 8:
        return egg_goal(o);
    }
    return 1;
}

static void egg7(uint32_t o)
{
    if (!egg_step(o)) return;
    egg_cam(o);
    egg_beamchk(o);
    o_animate(o, o_asset_anim(SCDA_S7_EGGMAN));
    o_draw(o);
}

/* ---- $2D a jet: drawn while Eggman's are lit --------------------------- */
static void egg7jet(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20CD62] */
        o_init(o, SCDA_S7_JET, 4, 5, 0x14, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, J_DX, OB8(o, OBJ_subtype) ? -0x30 : -0x33);
        SET16(o, J_DY, OB8(o, OBJ_subtype) ? -0x0E : 0x1A);
        SET8(o, OBJ_routine, 2);
    }
    uint32_t e = lk(o, J_LINK);                               /* [$20CDAC] */
    if (!alive(e, 0x2C)) { o_delete(o); return; }
    if (!(OB8(e, E_FLAGS) & 8)) return;
    SET8(o, OBJ_status, OB8(e, OBJ_status));
    int dx = OBS16(o, J_DX);
    if (OB8(o, OBJ_status) & 1) dx = -dx;
    set_xy(o, wx(e) + dx, wy(e) + OBS16(o, J_DY));
    o_animate(o, o_asset_anim(SCDA_S7_JET));
    o_draw(o);
}

/* ---- $2E the laser: a link under the one above; subtype 1 its smoke, 2 the
 * blast where Metal Sonic crashes ----------------------------------------- */
static void make_kemuri(uint32_t o)                           /* [$20D04A] */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET16(a1, B_LINK, (uint16_t)o);
    SET8(a1, B_PID, 0x2E);
    SET32(a1, OBJ_code, native3(0x2E));
    SET8(a1, OBJ_subtype, 1);
    SET16(a1, B_DX, 2);
    SET16(a1, B_DY, 0x10);
}
static void make_hahen(uint32_t o)                            /* [$20D0A8]: debris, at Eggman's speed */
{
    uint32_t a1 = o_alloc_after(o), e = lk(o, B_EGG);
    if (!a1) return;
    SET32(a1, OBJ_code, native3(0x37));
    set_xy(a1, wx(o) + 2, wy(o) + 0x10);
    SET16(a1, OBJ_x_vel, alive(e, 0x2C) ? OB16(e, OBJ_x_vel) : 0);
}
static void make_bakuha(uint32_t o)                           /* [$20D070] */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET16(a1, B_LINK, (uint16_t)o);
    SET8(a1, B_PID, 0x2F);
    SET32(a1, OBJ_code, native3(0x2E));
    SET8(a1, OBJ_subtype, 2);
    set_xy(a1, wx(o), wy(o));
    o_sfx_cd(FM_EXPLODE);
}

static int beam_posi(uint32_t o, uint32_t a2)                 /* [$20CC0E]: 1 = shown */
{
    int dx = OBS16(o, B_DX);
    set_xy(o, wx(a2), wy(a2) + OBS16(o, B_DY));
    if (OB8(a2, OBJ_status) & 1) { flag(o, OBJ_status, 1, 0); dx = -dx; }
    else flag(o, OBJ_status, 0, 1);
    SET16(o, OBJ_x_pos, wx(o) + dx);
    uint8_t f = (uint8_t)((OB8(o, B_FLAGS) & ~0x60) | (OB8(a2, B_FLAGS) & 0x60));
    SET8(o, B_FLAGS, f);
    return (f & 0x40) != 0;
}

static void egg7beam(uint32_t o)
{
    uint32_t a2 = lk(o, B_LINK);
    if (!alive(a2, OB8(o, B_PID))) { o_delete(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20CB34] */
        if (!OB8(o, OBJ_subtype)) {
            o_init(o, SCDA_S7_BEAM, 4, 2, 8, 0x10);
            SET8(o, OBJ_y_radius, 0x10);
            SET8(o, OBJ_routine, 2);
            return;
        }
        o_init(o, SCDA_S7_BOOM, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_routine, OB8(o, OBJ_subtype) == 2 ? 6 : 4);
        SET8(o, B_T1, OB8(o, OBJ_subtype) == 2 ? 0x1E : 0x0F);
        return;
    case 2: {                                                 /* [$20CBB2] */
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, 0);
        if (OB8(a2, B_FLAGS) & 0x20) {
            SET8(o, OBJ_coll_flags, tri_scd_coll(0xBE));
            SET8(o, OBJ_coll_prop, 2);
        }
        if (!beam_posi(o, a2)) { SET16(o, B_T1, 0); return; }
        if (OB8(o, B_LAST)) {                                 /* (the bottom one: smoke, debris) */
            uint8_t t = OB8(o, B_T1);
            SET8(o, B_T1, t - 1);
            if (!t) { make_kemuri(o); SET8(o, B_T1, 0x0A); }
            t = OB8(o, B_T2);
            SET8(o, B_T2, t - 1);
            if (!t) { make_hahen(o); SET8(o, B_T2, 9); }
        }
        o_animate(o, o_asset_anim(SCDA_S7_BEAM));
        o_touch(o);
        o_draw(o);
        return;
    }
    case 4:                                                   /* [$20CC74] the smoke, with the laser */
        beam_posi(o, a2);
        b_timer(o);
        if (OB8(o, STEP)) {
            SET8(o, STEP, 0);
            SET8(o, OBJ_routine, 6);
            SET8(o, B_T1, 0x0F);
        }
        break;
    default:                                                  /* [$20CC94] then on its own */
        b_timer(o);
        if (OB8(o, STEP)) { o_delete(o); return; }
        break;
    }
    o_animate(o, o_asset_anim(SCDA_S7_BOOM));
    o_draw(o);
}

/* ---- $37 debris off the laser's foot ------------------------------------ */
static void hahen73(uint32_t o)
{
    if (!(OB8(o, OBJ_routine) & 1)) {                         /* [$20CCC0] */
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) | 1);
        o_init(o, SCDA_S7_HAHEN, 4, 5, 4, 4);
        SET8(o, OBJ_y_radius, 4);
        int16_t r = (int16_t)o_random();
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + (uint16_t)(int16_t)(r % 0x1C0));
        SET16(o, OBJ_y_vel, -0x180);
        SET16(o, AY, 0x10 + (r & 0xF));
        SET8(o, OBJ_frame, r & 3);
        return;
    }
    add_spd2(o);
    if ((int16_t)(wy(o) - (int16_t)(r16(RAM_Camera_Y_pos) + 0xE8)) > 0) { o_delete(o); return; }
    o_draw(o);
}

/* ======================================================================
 * $2F Metal Sonic
 * ==================================================================== */
static void speed_set(uint32_t o, int16_t d0, int routine, int anim)   /* [$20C188 / $20C1C8] */
{
    SET16(o, MAX, d0);
    if (d0 >= OBS16(o, OBJ_x_vel)) d0 = (int16_t)-d0;         /* (either way: at that speed next frame) */
    SET16(o, AX, d0);
    SET8(o, OBJ_routine, routine);
    SET8(o, OBJ_anim, anim);
    SET8(o, STEP, 0);
    SET16(o, TIME, 0);
}
static void hi_set(uint32_t o)
{
    uint8_t m = OB8(o, M_MODE);
    speed_set(o, m == 1 ? 0x800 : m == 2 ? 0x2AA : 0x400, 8, 6);
}
static void low_set(uint32_t o)
{
    uint8_t m = OB8(o, M_MODE);
    speed_set(o, m == 1 ? 0x500 : m == 2 ? 0x1AA : 0x280, 6, 5);
}
static void to(uint32_t o, int routine)                       /* [$20C208..$20C226] */
{
    SET8(o, OBJ_routine, routine);
    SET16(o, M_CLOSE, 0);
    SET8(o, STEP, 0);
    SET16(o, TIME, 0);
}

static void make_ele(uint32_t o)                              /* [$20CF84] */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET16(a1, 0x34, (uint16_t)o);
    SET32(a1, OBJ_code, native3(0x30));
}
static void make_bara(uint32_t o)                             /* [$20CF14]: ten pieces */
{
    for (int d3 = 9; d3 >= 0; d3--) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) return;
        SET8(a1, OBJ_frame, d3 % 5);
        SET16(a1, 0x34, (uint16_t)o);
        SET32(a1, OBJ_code, native3(0x31));
        set_xy(a1, wx(o), wy(o));
        int16_t r = (int16_t)o_random();
        int32_t d1 = (int16_t)((uint16_t)r | 0x8000);
        SET16(a1, OBJ_x_vel, (uint16_t)(int16_t)(d1 % 0x280));
        SET16(a1, OBJ_y_vel, -0x200);
        SET16(a1, AY, 0x0C + (r & 0xF));
    }
}

static void loser(uint32_t a)                                 /* loser_posiset [$20C072] */
{
    if (wx(a) > 0x3E48) SET16(a, OBJ_x_pos, 0x3E48);
}

static void goal_chk(uint32_t o, uint32_t e)                  /* [$20BFAC] */
{
    uint32_t p = RAM_Player_1;
    uint8_t g = OB8(e, E_GOAL);
    if (g & 0x80) return;
    if (!g) {
        if (wx(o) >= 0x3E90) {                                /* [$20C032] Metal Sonic first */
            SET16(o, OBJ_x_pos, 0x3E90);
            SET8(e, E_GOAL, 0x82);
            s_flags |= 0x80;
            o_sfx_cd(FM_GOAL);
            if (o_player_active(p)) loser(p);
            flag(o, M_FLAGS, 0, 0x20);
            SET8(o, OBJ_routine, 0x10);
            SET16(o, TIME, 0);
            SET8(o, STEP, 0);
            return;
        }
        if (wx(p) < 0x3E90) return;
    }
    if (!OB8(e, E_GOAL)) {                                    /* [$20BFCC] Sonic first */
        SET8(e, E_GOAL, 1);
        s_flags |= 0x80;
        o_sfx_cd(FM_GOAL);
        loser(o);
        SET16(o, MAX, 0x800);
        SET16(o, AX, 0x10);
    }
    if (wx(o) < 0x3E48) return;
    SET8(e, E_GOAL, OB8(e, E_GOAL) | 0x80);                   /* (into the barrier) */
    flag(o, M_FLAGS, 0, 0x20);
    SET8(o, OBJ_routine, 0x12);
    make_bara(o);
    SET16(o, TIME, 0);
    SET8(o, STEP, 0);
}

static void egg_chk(uint32_t o, uint32_t e)                   /* [$20BF54]: Eggman close behind */
{
    if (!(OB8(o, M_FLAGS) & 0x20) || (int16_t)(wx(o) - wx(e)) > 0x58) return;
    if (wx(o) >= 0x3500) {
        SET16(o, OBJ_x_vel, OB16(e, OBJ_x_vel));
        SET16(o, AX, 8);
        SET16(o, MAX, 0x600);
        return;
    }
    if (OBS16(o, OBJ_x_vel) < OBS16(e, OBJ_x_vel)) {
        SET16(o, OBJ_x_vel, OB16(e, OBJ_x_vel));
        SET16(o, AX, 8);
        SET16(o, MAX, 0x600);
    }
    to(o, 0xC);
}

static void normal_move(uint32_t o, int16_t d5);
static void atc_move(uint32_t o, int16_t d5)                  /* [$20C0FE] */
{
    if (wx(o) < 0x3500) { to(o, 0xA); return; }
    to(o, OB8(o, OBJ_routine));                               /* (msnc_wk_clr) */
    SET8(o, M_MODE, OB8(o, M_FLAGS) & 0x40 ? 1 : 2);
    SET16(o, M_MTIME, 0xF0);
    normal_move(o, d5);
}
static void normal_move(uint32_t o, int16_t d5)               /* [$20C14A] */
{
    if (!OB16(o, M_MTIME) && OBS16(o, M_NEAR) <= -0x78) {     /* (far from the player for long) */
        if (d5 >= 0) { atc_move(o, d5); return; }
    } else if ((uint16_t)OB16(o, M_COUNT) % 60 < 30) {        /* (by turns: half a second fast) */
        if (OB8(o, OBJ_routine) != 8) hi_set(o);
        return;
    }
    if (OB8(o, OBJ_routine) != 6) low_set(o);
}
static void low_move(uint32_t o, uint32_t e)                  /* [$20C086] (routines 6, 8) */
{
    add_spd3(o);
    msnc_hight(o);
    if (!e || OB8(e, E_GOAL)) return;
    SET16(o, M_COUNT, OB16(o, M_COUNT) + 1);
    int16_t d5 = 13;                                          /* (in a mode: what ObjGetFloorDist left in d5) */
    if (!OB8(o, M_MODE)) {
        d5 = (int16_t)(wx(RAM_Player_1) - wx(o));
        int16_t d4 = d5;
        if (d5 >= 0) flag(o, M_FLAGS, 0x40, 0);
        else { flag(o, M_FLAGS, 0, 0x40); d4 = (int16_t)-d4; }
        if (d4 < 0xA0) {
            if (OBS16(o, M_NEAR) < 0) SET16(o, M_NEAR, 0);
            SET16(o, M_NEAR, OB16(o, M_NEAR) + 1);
            SET16(o, M_CLOSE, d4 < 0x10 ? 0 : OB16(o, M_CLOSE) + 1);
        } else {
            if (OBS16(o, M_NEAR) >= 0) SET16(o, M_NEAR, 0);
            SET16(o, M_NEAR, OB16(o, M_NEAR) - 1);
        }
        if (OBS16(o, M_CLOSE) >= 0x32) { atc_move(o, d5); return; }
    } else {                                                  /* baisoku [$20C13C] */
        int16_t t = (int16_t)(OB16(o, M_MTIME) - 1);
        SET16(o, M_MTIME, t);
        if (t <= 0) { SET8(o, M_MODE, 0); SET16(o, M_MTIME, 0); }
    }
    normal_move(o, d5);
}

/* the attacks */
static void atc_ini(uint32_t o)                               /* [$20C24E] his stance */
{
    SET8(o, OBJ_anim, 4);
    o_sfx_cd(FM_STANCE);
}
static void atc_next(uint32_t o)                              /* [$20C268] */
{
    if (wx(RAM_Player_1) - wx(o) >= 0) { flag(o, M_FLAGS, 0x40, 0); to(o, 0xC); }
    else { flag(o, M_FLAGS, 0, 0x40); to(o, 0xE); }
}
static const Event msnc_atc_ev[] = { { 0, atc_ini }, { 60, atc_next } };
static void f_tobi(uint32_t o)                                /* [$20C2A4] the dash */
{
    o_sfx_cd(FM_JET);
    SET16(o, AX, 8);
    SET16(o, MAX, 0x600);
    SET8(o, OBJ_anim, 8);
}
static void f_atack(uint32_t o) { SET8(o, OBJ_anim, 9); }     /* [$20C2CA] */
static void f_coli(uint32_t o)                                /* [$20C2D2] */
{
    SET8(o, OBJ_coll_flags, tri_scd_coll(0xBC));
    SET8(o, OBJ_coll_prop, 2);
}
static void f_end(uint32_t o)                                 /* [$20C2E0] (the disc: against the player) */
{
    SET8(o, OBJ_coll_flags, 0);
    SET8(o, OBJ_coll_prop, 0);
    if (wx(RAM_Player_1) > wx(o)) low_set(o);
    else hi_set(o);
}
static const Event msnc_f_ev[] = { { 0, f_tobi }, { 30, f_atack }, { 40, f_coli }, { 180, f_end } };
static void b_atack(uint32_t o)                               /* [$20C31A] the field */
{
    SET16(o, AX, -8);
    SET16(o, MAX, 0x200);
    make_ele(o);
    SET8(o, OBJ_anim, 0xA);
    o_sfx_cd(FM_FIELD);
}
static void b_coli(uint32_t o)                                /* [$20C344] */
{
    SET8(o, OBJ_coll_flags, tri_scd_coll(0xBD));
    SET8(o, OBJ_coll_prop, 2);
}
static void b_kill(uint32_t o)                                /* [$20C352] */
{
    SET8(o, OBJ_coll_flags, 0);
    SET8(o, OBJ_coll_prop, 0);
    hi_set(o);
}
static const Event msnc_b_ev[] = { { 0, b_atack }, { 10, b_coli }, { 150, b_kill } };

static int msnc_step(uint32_t o, uint32_t e)                  /* 0 = gone */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* msnc_ini [$20BE5C] */
        o_init(o, SCDA_S7_MSNC, 4, 5, 0x14, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* msnc_demo1 [$20BE86]: he looks at a jump */
        if (OB8(o, OBJ_anim) == 1) {
            if (OB8(o, OBJ_anim_frame) != 7) break;
            SET8(o, OBJ_anim, 0);
        }
        uint8_t st = OB8(p, OBJ_status);
        if ((st & 2) && (st & 4)) {
            if (OB8(o, STEP)) break;
            SET8(o, STEP, 1);
            SET8(o, OBJ_anim, 1);
        } else
            SET8(o, STEP, 0);
        break;
    }
    case 4:                                                   /* msnc_demo2 [$20BEC6]: to the start */
        switch (OB8(o, STEP)) {
        case 0:                                               /* [$20BEE6] */
            SET8(o, OBJ_anim, 2);
            SET8(o, STEP, 1);
            SET8(o, OBJ_frame, 0);
            /* fall through */
        case 1:                                               /* kamae [$20BEF4] */
            if (OB8(o, OBJ_frame) != 6) break;
            SET8(o, OBJ_anim, 3);
            SET8(o, STEP, 2);
            SET16(o, TIME, 0x78);
            break;
        case 3:                                               /* fire_start [$20BF0E] */
            SET8(o, STEP, 4);
            SET8(o, OBJ_anim, 4);
            SET16(o, TIME, 0x96);
            break;
        case 5:                                               /* door_open [$20BF20]: off they go */
            if (e) SET8(e, E_CAM, 0x82);
            s_flags |= 0x80;
            SET8(o, STEP, 6);
            SET16(o, TIME, 0x3C);
            scd_cd_play(SCD_CD_R7D, 1);                       /* (SubCPUCmd $20, in both futures) */
            break;
        case 7:                                               /* [$20BF4A] */
            flag(o, M_FLAGS, 0x20, 0);
            low_set(o);
            break;
        default:                                              /* (2, 4, 6) */
            w_timer(o);
            break;
        }
        break;
    case 6:
    case 8: low_move(o, e); break;
    case 0xA:                                                 /* msnc_atc [$20C234] */
        add_spd3(o);
        msnc_hight(o);
        event_timer(o, msnc_atc_ev, 2);
        break;
    case 0xC:                                                 /* msnc_f_atc [$20C282] */
        add_spd3(o);
        msnc_hight(o);
        event_timer(o, msnc_f_ev, 4);
        break;
    case 0xE:                                                 /* msnc_b_atc [$20C2FA] */
        add_spd3(o);
        msnc_hight(o);
        event_timer(o, msnc_b_ev, 3);
        break;
    case 0x10:                                                /* msnc_win [$20C3E0]: at the post */
        if (o_player_active(p)) loser(p);
        switch (OB8(o, STEP)) {
        case 0:
            SET8(o, OBJ_anim, 0xB);
            set_xy(o, 0x3E90, 0x1CC);
            SET8(o, STEP, 1);
            break;
        case 1:
            if (wx(p) < 0x3DD0) break;
            SET8(o, STEP, 2);
            SET8(o, TIME, 0x1E);
            break;
        case 2: b_timer(o); break;
        default: SET8(o, OBJ_anim, 0xC); break;
        }
        break;
    case 0x12:                                                /* msnc_lose [$20C438]: crashed */
        if (!OB8(o, STEP)) {
            D(0) = 100;                                       /* (1000 points) */
            tri_jsr(S3K_HUD_AddToScore);
            SET8(o, STEP, 1);
            SET8(o, OBJ_anim, 0xD);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, AX, 0);
            SET16(o, OBJ_y_vel, -0x500);
            SET16(o, AY, 0x28);
            make_bakuha(o);
        }
        if (OB16(o, TIME) == 0xF0) { o_delete(o); return 0; }
        add_spd2(o);
        SET16(o, TIME, OB16(o, TIME) + 1);
        break;
    }
    return 1;
}

static void msnc(uint32_t o)
{
    uint32_t e = lk(o, M_LINK);
    if (!alive(e, 0x2C)) e = 0;                               /* (Eggman gone: CD reads his cleared slot) */
    if (e) {
        goal_chk(o, e);
        egg_chk(o, e);
    }
    if (!msnc_step(o, e)) return;
    o_animate(o, o_asset_anim(SCDA_S7_MSNC));
    o_touch(o);
    o_draw(o);
}

/* ---- $30 his electric field: while he shows it (anim $A) ---------------- */
#define L_LINK   0x34                   /* oVar2E: Metal Sonic */
#define L_TIME   0x30                   /* oVar2A */
static void msnc_ele(uint32_t o)
{
    uint32_t m = lk(o, L_LINK);
    if (!OB8(o, OBJ_routine)) {                               /* [$20C374] */
        o_init(o, SCDA_S7_ELE, 4, 5, 0x18, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        SET8(o, OBJ_routine, 2);
    }
    if (!alive(m, 0x2F) || OB8(m, OBJ_anim) != 0xA) { o_delete(o); return; }   /* [$20C39E] */
    set_xy(o, wx(m), wy(m));
    uint8_t t = (uint8_t)(OB8(o, L_TIME) + 1);
    if (t != 5) { SET8(o, L_TIME, t); o_draw(o); return; }
    SET8(o, L_TIME, 0);                                       /* (the next picture: not drawn this frame) */
    SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1 == 2 ? 0 : OB8(o, OBJ_frame) + 1);
}

/* ---- $31 a piece of him, falling ---------------------------------------- */
static void msnc_bara(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20CE1E] (CD sets these every frame) */
        o_init(o, SCDA_S7_BARA, 4, 1, 8, 8);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_routine, 2);
    }
    add_spd2(o);
    if (wy(o) >= 0x270) { o_delete(o); return; }
    o_draw(o);
}

/* ======================================================================
 * $2B a metal wall: subtype its size; the start gate and the finish barrier
 * move (CD's subtype 2: bit 7 up, else down; 6 stays after, else goes; 5
 * keeps its size, else 4 grows, or shrinks; low nibble its moves of 32 px)
 * each frame bossFlags bit 7 is up, which the last move ends.
 * ==================================================================== */
#define W_TARGET 0x30                   /* oVar2A (word): this move's end */
#define W_MOVE   0x2D                   /* (CD's subtype 2: scd_level.py, subtype bits 3-7) */
static const uint8_t wall_size[6][2] = {                      /* [$20EBB4]: width, height radius */
    { 11, 16 }, { 32, 16 }, { 48, 16 }, { 64, 16 }, { 11, 32 }, { 11, 48 },
};
static void wall_size_set(uint32_t o)
{
    const uint8_t *s = wall_size[OB8(o, OBJ_frame) % 6];
    SET8(o, OBJ_x_radius, s[0]);
    SET8(o, OBJ_width_px, s[0]);
    SET8(o, OBJ_y_radius, s[1]);
    SET8(o, OBJ_height_px, s[1]);
}
static int wall_move(uint32_t o)                              /* [$20EBC0]: 1 = gone */
{
    if (!(s_flags & 0x80)) return 0;
    if (!OB16(o, W_TARGET)) {
        int16_t d0 = 0x20, d1 = 0x200;
        if (OB8(o, W_MOVE) & 0x80) { d0 = -d0; d1 = (int16_t)-d1; }
        SET16(o, OBJ_y_vel, d1);
        SET16(o, W_TARGET, wy(o) + d0);
    }
    cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) * 256);
    if (wy(o) != OBS16(o, W_TARGET)) return 0;
    SET16(o, W_TARGET, 0);
    uint8_t m = OB8(o, W_MOVE), n = (uint8_t)((m & 0xF) - 1);
    if (!n) {
        s_flags &= 0x7F;
        if (m & 0x40) { SET8(o, W_MOVE, 0); return 0; }
        o_delete(o);
        return 1;
    }
    m = (uint8_t)((m & 0xF0) + n);
    SET8(o, W_MOVE, m);
    if (m & 0x20) return 0;
    uint8_t f = OB8(o, OBJ_frame);
    if (m & 0x10) {                                           /* (taller, its foot kept) */
        if (++f == 1) f = 4;
        SET16(o, OBJ_y_pos, wy(o) - 0x10);
    } else {
        if (--f == 3) f = 0;
        SET16(o, OBJ_y_pos, wy(o) + 0x10);
    }
    SET8(o, OBJ_frame, f);
    wall_size_set(o);
    return 0;
}
static void wall73(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20EB50] */
        uint8_t sub = OB8(o, OBJ_subtype);
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_S7_WALL, OB8(o, OBJ_render) | 4, 3, 0, 0);
        SET8(o, OBJ_frame, sub & 7);
        SET8(o, W_MOVE, sub & 8 ? (sub & 0xF0) | 2 : 0);
        wall_size_set(o);
    }
    if (OB8(o, W_MOVE) && wall_move(o)) return;
    /* (CD clears player 1's $20 when he touches it: S3K's is his anim, kept) */
    o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ======================================================================
 * $32 a spike block (spikes on top) Metal Sonic smashes passing it; only
 * while he is left of it. Routine 4: one of its four pieces.
 * ==================================================================== */
#define S_LINK   0x30                   /* oVar2A: Metal Sonic */
#define S_BLINK  0x3A                   /* oVar34: a piece shows every other frame */
#define S_STOOD  0x3B                   /* oVar35: player 1 on it */
static uint32_t find_msnc(void)                               /* [$20EEDA] */
{
    for (int i = 2; i < 110; i++) {
        uint32_t a = o_slot(i);
        if (r32(a) == native3(0x2F)) return a;
    }
    return 0;
}
static void spike_hurt(uint32_t o, int n)                     /* (stood on: hurt, off it) */
{
    uint32_t p = o_player(n);
    if ((OB8(p, P_STATUS2) & 2) || OB8(p, P_INVULN) || OB8(p, OBJ_routine) >= 4) return;
    o_spike_hurt(o, p);
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(n ? ST_P2_STAND : ST_P1_STAND));   /* GetOffObject */
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
}
static void spike_smash(uint32_t o)                           /* msnc_hit [$20EE0A] */
{
    static const int16_t spd[4][4] = {
        { 0x240, -0x200, 6, 0x10 }, { 0x220, -0x200, 0, 0x12 }, { 0x200, -0x200, 6, 0x14 }, { 0x1E0, -0x200, 0xA, 0x10 },
    };
    o_sfx_cd(FM_SMASH);
    for (int k = 0, dx = -12; k < 4; k++, dx += 8) {
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, native3(0x32));
        SET8(a1, OBJ_routine, 4);
        o_init(a1, SCDA_S7_SPIKE, 4, 5, 4, 0x10);
        SET8(a1, OBJ_frame, 1);
        SET8(a1, OBJ_y_radius, 0x10);
        set_xy(a1, wx(o) + dx, wy(o));
        SET16(a1, OBJ_x_vel, spd[k][0]);
        SET16(a1, OBJ_y_vel, spd[k][1]);
        SET16(a1, AX, spd[k][2]);
        SET16(a1, AY, spd[k][3]);
    }
    if (OB8(o, S_STOOD)) cd_get_off(o);
    o_delete(o);
}
static void hari73(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* [$20ED16] */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_S7_SPIKE, OB8(o, OBJ_render) | 4, 3, 0x10, 0x0C);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x0C);
        uint32_t m = find_msnc();
        if (!m || (int16_t)(wx(o) - 0x10 - wx(m) - OB8(m, OBJ_width_px)) < 0) { o_delete(o); return; }
        SET16(o, S_LINK, (uint16_t)m);
    }   /* fall through */
    case 2: {                                                 /* [$20ED74] */
        uint32_t m = lk(o, S_LINK);
        if (alive(m, 0x2F)) {                                 /* (CD: whatever is in his slot) */
            int16_t d0 = (int16_t)(wx(o) - wx(m));
            if (d0 < 0) d0 = (int16_t)-d0;
            if (d0 < OB8(o, OBJ_width_px)) { spike_smash(o); return; }
        }
        o_solid(o, 0x10 + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        SET8(o, S_STOOD, 0);
        for (int n = 0; n < 2; n++) {                         /* (CD: player 1) */
            if (!(OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))) continue;
            if (!n) SET8(o, S_STOOD, 1);
            spike_hurt(o, n);
        }
        break;
    }
    default:                                                  /* [$20EEC2] a piece */
        add_spd2(o);
        SET8(o, S_BLINK, OB8(o, S_BLINK) ^ 1);
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
        if (OB8(o, S_BLINK)) o_draw(o);
        return;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ======================================================================
 * $33 Amy: in her capsule (CD's special touch $F5: a player in a ball breaks
 * it); she drops to the road, runs to the player and, behind him while he
 * walks, hugs him: that ends the act, as do 600 frames from the capsule.
 * Leaving the screen, line 1 is the level's again. $34 her hearts.
 * ==================================================================== */
#define A_GOAL   0x30                   /* oVar2A: goal_move2's count */
#define A_TIME   0x36                   /* oVar30 (word): to the act's end */
#define A_HOME   0x3C                   /* oVar36: where she was */
#define A_DONE   0x3E                   /* oVar38: the end begun */
#define A_WAIT   0x40                   /* oVar3A: after a hop, on the road */
#define A_HEART  0x41                   /* oVar3B: to the next heart */
#define A_DOWN   0x42                   /* oVar3C: landed */
#define A_FLAGS  0x44                   /* oVar3E: bit 0 hugging (hearts the other side), 2 by the player, 7 has hugged */
#define A_PAUSE  0x45                   /* oVar3F */
#define T_T      0x40                   /* oVar3A (a heart's) */
#define T_STILL  0x42                   /* oVar3C */

static void line_level(void)                                  /* (line 1 = the level's palette 4 again: the palette) */
{
    uint32_t e = tri_scd_palpoint(TRI_PAL_CD);
    if (!e) return;
    uint32_t src = tri_rd32(e);
    for (unsigned k = 0; k < 16; k++) w16(RAM_Normal_palette + 0x20 + k * 2u, tri_rd16(src + k * 2u));
}

static void amy_animate(uint32_t o)                           /* [$20F308]: [frame | flips << 5, time]..., $FF */
{
    uint32_t tab = o_asset_anim(SCDA_S7_AMY);
    uint8_t a = OB8(o, OBJ_anim);
    if (a != OB8(o, OBJ_prev_anim)) {
        SET8(o, OBJ_prev_anim, a);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
    SET8(o, OBJ_anim_timer, t);
    if (t >= 0 || !tab) return;
    uint32_t s = tab + tri_rd16(tab + a * 2u);
    uint8_t f = tri_rd8(s + OB8(o, OBJ_anim_frame));
    if (f & 0x80) { SET8(o, OBJ_anim_frame, 0); f = tri_rd8(s); }
    SET8(o, OBJ_frame, f & 0x1F);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (((f >> 5) ^ OB8(o, OBJ_status)) & 3));
    SET8(o, OBJ_anim_timer, tri_rd8(s + OB8(o, OBJ_anim_frame) + 1u));
    SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 2);
}

static void amy_face(uint32_t o)                              /* [$20F3B4] */
{
    int flip = OB16(o, OBJ_x_pos) >= OB16(RAM_Player_1, OBJ_x_pos);
    SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | flip);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | flip);
}
static uint16_t udist(uint16_t a, uint16_t b) { return a >= b ? (uint16_t)(a - b) : (uint16_t)(b - a); }
static uint16_t amy_dx(uint32_t o) { return udist(OB16(RAM_Player_1, OBJ_x_pos), OB16(o, OBJ_x_pos)); }

static void amy_hearts(uint32_t o)                            /* [$20F36A] */
{
    uint8_t t = OB8(o, A_HEART);
    SET8(o, A_HEART, t + 6);
    if (t + 6 < 0x100) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, native3(0x34));
    int dx = OB8(o, OBJ_status) & 1 ? -10 : 8;
    if (OB8(o, A_FLAGS) & 1) dx = -dx;
    set_xy(a1, wx(o) + dx, wy(o) - 12);
}

static void amy_goal(uint32_t o)                              /* goal_move2 [$20A15E] (count 1: at once) */
{
    w8(RAM_Update_HUD_timer, 0);
    w8(RAM_Ctrl_1_locked, 1);                                 /* (controls off: none held in this act) */
    w16(RAM_Ctrl_1_held_logical, 0);
    SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
    uint32_t a1 = o_alloc();
    if (!a1) { a1 = o_slot(109); o_delete(a1); }              /* (CD: the last slot, whatever is there) */
    SET32(a1, OBJ_code, S3K_Obj_LevelResults);
}

static void amy_hug_chk(uint32_t o)                           /* dakicheck [$20F24E] */
{
    uint32_t p = RAM_Player_1;
    if (r16(RAM_Debug_placement_mode)) return;
    uint16_t a = OB16(p, OBJ_x_pos), b = OB16(o, OBJ_x_pos);
    if (OB8(p, OBJ_status) & 1) { uint16_t t = a; a = b; b = t; }
    if (a < b || (uint16_t)(a - b) < 8 || (uint16_t)(a - b) >= 0x1C) return;   /* (just behind him) */
    int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 8);
    if (dy < 0 || dy >= 0x10) return;
    if (OB8(p, OBJ_status) & 6) { SET8(o, OBJ_routine, 0xE); return; }      /* (in the air or a ball: she hops) */
    SET8(o, A_FLAGS, OB8(o, A_FLAGS) | 0x81);
    SET16(o, OBJ_y_vel, 0);
    SET16(o, OBJ_x_vel, 0);
    SET8(o, OBJ_frame, 7);
    SET8(o, OBJ_routine, 0xA);
    tri_scd_voice(SCD_PCM_GIGGLE);                            /* (SubCPUCmd $7C) */
}

static void amy_hold(uint32_t o)                              /* (by the player, hugging) */
{
    uint32_t p = RAM_Player_1;
    amy_face(o);
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos) + (OB8(p, OBJ_status) & 1 ? 12 : -12));
    SET8(o, OBJ_frame, 14);
}

static void amy_step(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* emie7_init [$20EF44] (every frame till broken) */
        o_init(o, SCDA_S7_AMY, OB8(o, OBJ_render) | 4, 1, 0x0C, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, A_HOME, OB16(o, OBJ_x_pos));
        if (!scd_good_future()) set_xy(o, 0x3F43, 0x1AB);
        SET8(o, OBJ_anim, 6);
        if (!(o_s1_touch(o, 12, 12) & 1) || OB8(p, OBJ_anim) != 2) { amy_animate(o); return; }   /* (CD's size $35) */
        uint32_t a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, S3K_Obj_Explosion);
            SET8(a1, OBJ_routine, 6);                         /* (CD's $18 with $25 set: no flower) */
            set_xy(a1, wx(o), wy(o));
            o_sfx_cd(FM_EXPLODE);
        }
        SET16(p, OBJ_y_vel, (uint16_t)-OBS16(p, OBJ_y_vel));
        SET8(o, OBJ_routine, 2);
        SET16(o, A_TIME, 600);
        return;
    }
    case 2:                                                   /* emie7_move0 [$20EFEE]: down to the road */
        if (!(OB8(o, A_DOWN) & 0x80)) {
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
            cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) * 256);
            if (OB16(o, OBJ_y_pos) >= 0x1D0) {
                SET16(o, OBJ_y_pos, 0x1D0);
                SET16(o, OBJ_y_vel, 0);
                SET8(o, A_DOWN, 0xFF);
            }
            return;
        }
        amy_face(o);
        SET8(o, A_PAUSE, 0x3C);
        SET8(o, OBJ_routine, 4);
        amy_animate(o);
        return;
    case 4:
    case 6: {                                                 /* emie7_move1 [$20F036] */
        amy_hearts(o);
        amy_face(o);
        if (OB16(o, A_TIME)) {
            SET16(o, A_TIME, OB16(o, A_TIME) - 1);
            if (!OB16(o, A_TIME)) {                           /* [$20F138] the time is up */
                SET8(o, A_DONE, 0xFF);
                amy_goal(o);
                return;
            }
        }
        if (OB8(o, A_PAUSE)) {
            SET8(o, A_PAUSE, OB8(o, A_PAUSE) - 1);
            if (OB8(o, A_PAUSE)) goto run;
        }
        amy_hug_chk(o);
        if (OB8(o, A_FLAGS) & 4) {
            if (amy_dx(o) < 0x20) goto by;
            SET8(o, A_FLAGS, OB8(o, A_FLAGS) & ~4);
            goto run;
        }
        if (OBS16(p, OBJ_x_vel) || amy_dx(o) >= 0x0A) goto run;
    by:
        SET8(o, A_FLAGS, OB8(o, A_FLAGS) | 4);
        SET16(o, OBJ_x_vel, 0);
        goto stand;
    run: {                                                    /* [$20F0A8] */
        int16_t d0 = (int16_t)((OB8(o, OBJ_status) & 1 ? -0x20 : 0x20) + OBS16(o, OBJ_x_vel));
        if ((uint16_t)(d0 < 0 ? -d0 : d0) >= 0x280) d0 = d0 < 0 ? -0x280 : 0x280;
        SET16(o, OBJ_x_vel, d0);
        if (d0 < 0) {                                         /* (not far left of where she was) */
            uint16_t lim = (uint16_t)(OB16(o, A_HOME) - (scd_good_future() ? 0x60 : 0x80));
            if (lim >= OB16(o, OBJ_x_pos)) { SET16(o, OBJ_x_vel, 0); goto stand; }
        }
        int16_t f = o_floor_dist(o);
        if (f < 7 && f >= -7) SET16(o, OBJ_y_pos, wy(o) + f);
        cd_move(o, (int32_t)d0 * 256, 0);
        SET8(o, OBJ_anim, 2);
        amy_animate(o);
        return;
    }
    stand:
        SET8(o, OBJ_anim, 1);
        amy_animate(o);
        return;
    }
    case 0xE: {                                               /* emie7_tobii [$20F150]: a hop */
        SET8(o, OBJ_frame, 6);
        int16_t v = OB8(o, OBJ_status) & 1 ? 0x80 : -0x80;
        SET16(o, OBJ_x_vel, udist(OB16(o, OBJ_x_pos), OB16(o, A_HOME)) < 0x80 ? v : 0);
        SET16(o, OBJ_y_vel, -0x300);
        SET8(o, OBJ_routine, 0x10);
    }   /* fall through */
    case 8:
    case 0x10:                                                /* emie7_tobim [$20F188] */
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, (int32_t)OBS16(o, OBJ_y_vel) * 256);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x40);
        if (OBS16(o, OBJ_y_vel) >= 0) SET8(o, OBJ_frame, 7);
        if (OB16(o, OBJ_y_pos) < 0x1D0) return;
        SET16(o, OBJ_y_pos, 0x1D0);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0);
        if (OB8(o, A_WAIT) + 0x10 >= 0x100) SET8(o, OBJ_routine, 4);
        SET8(o, A_WAIT, OB8(o, A_WAIT) + 0x10);
        return;
    case 0xA:                                                 /* emie7_daki2 [$20F1C6]: the hug */
        amy_hearts(o);
        w8(RAM_Ctrl_1_locked, 1);
        w16(RAM_Ctrl_1_held_logical, 0);
        SET8(p, OBJ_anim, 5);
        amy_hold(o);
        SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
        if (!OB8(o, A_DONE)) amy_goal(o);
        return;
    default:                                                  /* emie7_end [$20F21E] */
        amy_hearts(o);
        amy_hold(o);
        return;
    }
}

static void amy7(uint32_t o)
{
    amy_step(o);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) { line_level(); return; }
    o_draw(o);
}

static void heart7(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                               /* [$20F43A] (no size: CD sets none) */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_S7_HEART, OB8(o, OBJ_render) | 4, 3, 0, 0);
        SET8(o, OBJ_frame, 8);
        SET16(o, OBJ_y_vel, -0x60);
    }
    if (!OB8(o, T_STILL)) {                                   /* [$20F464] a ripple */
        int16_t s, c;
        o_sincos((uint8_t)(OB8(o, T_T) * 3), &s, &c);
        SET16(o, OBJ_x_vel, s >> 2);
    }
    add_spd(o);
    uint8_t t = (uint8_t)(OB8(o, T_T) + 1);
    SET8(o, T_T, t);
    if (t == 0x14) SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
    if (t == 0x6E) {
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, OBJ_x_vel, 0);
        SET8(o, T_STILL, 0xFF);
    }
    if (t == 0x78) { o_delete(o); return; }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ======================================================================
 * $36 the street lamps in front: one every 320 px of the camera, sliding
 * by at 1.8 times its speed (none from x 15680); the head, or (the camera
 * low) the pole; subtype 1 (made by it) the pole under the head.
 * ==================================================================== */
#define G_LINK   0x34                   /* oVar2E: the other */
#define G_X      0x36                   /* oVar30 */
#define G_Y      0x38                   /* oVar32 */
static int16_t lamp_x(void)                                   /* [$20D4FA] */
{
    uint16_t cam = r16(RAM_Camera_X_pos), q = (uint16_t)(cam / 0x140), r = (uint16_t)(cam % 0x140);
    if (q > 0x30) q = 0;                                      /* (the remainder kept) */
    return (int16_t)((uint16_t)((q + 1) * 0x140 + 0x100) - (uint16_t)(r * 0x100u / 0x140));
}
static void lamp_part(uint32_t o, int pole)
{
    SET8(o, OBJ_frame, pole);
    SET8(o, OBJ_width_px, pole ? 8 : 0x18);
    SET8(o, OBJ_y_radius, pole ? 0x70 : 0x0C);
    SET8(o, OBJ_height_px, pole ? 0x70 : 0x0C);
}
static void gaitou73(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* [$20D410] */
        o_init(o, SCDA_S7_LAMP, 4, 0, 0, 0);
        if (!OB8(o, OBJ_subtype)) {
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                SET32(a1, OBJ_code, native3(0x36));
                SET8(a1, OBJ_subtype, 1);
                SET16(o, G_LINK, (uint16_t)a1);
                SET16(a1, G_LINK, (uint16_t)o);
            }
            lamp_part(o, 0);
            SET8(o, OBJ_routine, 2);
        } else {
            lamp_part(o, 1);
            SET8(o, OBJ_routine, 4);
        }
        break;
    case 2: {                                                 /* [$20D484] */
        int16_t x = lamp_x(), cy = (int16_t)r16(RAM_Camera_Y_pos);
        int16_t y = (int16_t)(0x160 - (int16_t)((int32_t)(int16_t)(cy - 0x160) / 3));
        SET16(o, G_X, x);
        SET16(o, G_Y, y);
        int pole = cy >= y + 0x0C;
        set_xy(o, x, pole ? y + 0x15C : y);
        lamp_part(o, pole);
        break;
    }
    default: {                                                /* [$20D536] (the head's never goes) */
        uint32_t a1 = lk(o, G_LINK);
        if (a1) set_xy(o, OBS16(a1, G_X), OBS16(a1, G_Y) + 0x7C);
        break;
    }
    }
    o_draw(o);
}

/* bossFlags clears with the level (Sonic CD's level start clears its RAM) */
void scd_ssz3_level_entry(void) { s_flags = 0; }

void scd_ssz3_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x2B] = wall73;
    t[base3 + 0x2C] = egg7;
    t[base3 + 0x2D] = egg7jet;
    t[base3 + 0x2E] = egg7beam;
    t[base3 + 0x2F] = msnc;
    t[base3 + 0x30] = msnc_ele;
    t[base3 + 0x31] = msnc_bara;
    t[base3 + 0x32] = hari73;
    t[base3 + 0x33] = amy7;
    t[base3 + 0x34] = heart7;
    t[base3 + 0x36] = gaitou73;
    t[base3 + 0x37] = hahen73;
}
