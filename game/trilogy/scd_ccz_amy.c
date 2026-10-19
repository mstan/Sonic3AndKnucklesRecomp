/*
 * scd_ccz_amy.c — Collision Chaos act 1 (present): Metal Sonic carries Amy
 * off (Sonic CD; the disc's R31A program; decomp: R3\MECASNC.C, MSNCCG.C).
 *
 *   $32 Amy          waits for player 1 to come up to her, then runs right
 *                    (hearts behind her) to x $3C0, where Metal Sonic ($31)
 *                    comes for her from the right; held, she struggles in his
 *                    arms
 *   $31 Metal Sonic  waits a second, flies in left to Amy, grabs her, hovers,
 *                    rises, waits (80 frames with player 1 near) and flies off
 *                    right with her, his jet ($34) burning
 *   $30 barriers     (in his way) solid, until Metal Sonic flies into one:
 *                    then it breaks into four pieces that fall; subtype 1 a
 *                    bed of spikes (stood on, they hurt)
 *   $33 hearts       Amy's: up, wavering, then still, then gone
 * Once he is off (x $528), or player 1 is at x $4C0, it is over: Amy is
 * captured for the act (Sonic CD's amyCaptured: through deaths and time
 * warps, cleared as act 1 begins), her colours (line 1 while she is there)
 * give way to the level's. Sonic CD's time attack has none of it; here there
 * is no time attack.
 */
#include "trilogy_obj.h"
#include "scd.h"

/* this zone's asset ids (scd_level.py CD_ASSETS; -> trilogy_obj.h) */

#define CCZ_ROUND       1
#define FM_METAL        0xCA               /* (mecasnc3_matu1: Metal Sonic's jet, as he flies in) */
#define FM_BREAK        0xA3               /* (brknset: a barrier breaks) */
#define O_PREV_ANIM     0x21               /* oPrevAnim */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static uint8_t s_captured;                      /* amyCaptured */
static uint8_t s_line_saved;
static uint16_t s_amy_line[16];                 /* (line 1 as it was) */

void scd_ccz_amy_level(void)                    /* (scd_ccz.c, TH_LEVEL_ENTRY: act 1 begun afresh) */
{
    static int last = -1;
    int z = r8(RAM_Current_zone_and_act), key = tri_cd_zone(z) ? ((z - TRI_CD_BASE) >> 2) * 4 + tri_act() : -1;
    if (key == CCZ_ROUND * 4 && key != last) s_captured = 0;
    last = key;
    s_line_saved = 0;
}

static void amy_pal(int on)                     /* (her colours on line 1: in, or the level's back) */
{
    uint32_t pal = o_asset_anim(SCDA_AMYPAL3);
    if (on && !s_line_saved) {
        if (!pal) return;
        s_line_saved = 1;
        for (unsigned k = 0; k < 16; k++) s_amy_line[k] = r16(RAM_Target_palette + 0x20 + k * 2u);
    }
    if (!s_line_saved) return;
    for (unsigned k = 0; k < 16; k++) {
        uint16_t c = on ? tri_rd16(pal + k * 2u) : s_amy_line[k];
        w16(RAM_Normal_palette + 0x20 + k * 2u, c);
        w16(RAM_Target_palette + 0x20 + k * 2u, c);
    }
}

static void captured(uint32_t o)                /* (it is over: emie3end, the level's line 1; gone) */
{
    s_captured = 0xFF;
    amy_pal(0);
    o_delete(o);
}

/* AnimateObjSimple: per animation [frame | flips << 5, time]..., $FF */
static void animate_pairs(uint32_t o, const uint8_t *const *anims)
{
    uint8_t a = OB8(o, OBJ_anim);
    if (a != OB8(o, O_PREV_ANIM)) {
        SET8(o, O_PREV_ANIM, a);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
    SET8(o, OBJ_anim_timer, t);
    if (t >= 0) return;
    const uint8_t *s = anims[a];
    if (s[OB8(o, OBJ_anim_frame)] & 0x80) SET8(o, OBJ_anim_frame, 0);
    uint8_t f = s[OB8(o, OBJ_anim_frame)];
    SET8(o, OBJ_frame, f & 0x1F);
    uint8_t flips = (uint8_t)(((f >> 5) ^ OB8(o, OBJ_status)) & 3);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | flips);
    SET8(o, OBJ_anim_timer, s[OB8(o, OBJ_anim_frame) + 1]);
    SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 2);
}

static void set_flip(uint32_t o, int on)        /* dir_left (on) / dir_right */
{
    SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | (on ? 1 : 0));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (on ? 1 : 0));
}

static void face_player(uint32_t o)             /* (left if he is not right of it) */
{
    set_flip(o, OB16(o, OBJ_x_pos) >= OB16(RAM_Player_1, OBJ_x_pos));
}

static int over(void) { return OB16(RAM_Player_1, OBJ_x_pos) >= 0x4C0; }   /* (player 1 past it all) */

/* ---- $32 Amy ------------------------------------------------------------- */
#define AM_METAL 0x3A                  /* oVar34: her Metal Sonic */
#define AM_HEART 0x41                  /* oVar3B: to the next heart */
static const uint8_t am_ani0[] = { 3, 0x27, 4, 9, 5, 0x27, 4, 9, 0xFF };   /* (waiting) */
static const uint8_t am_ani1[] = { 0, 3, 1, 2, 2, 3, 1, 2, 0xFF };         /* (running) */
static const uint8_t am_ani2[] = { 6, 0x0D, 7, 0x0D, 0xFF };               /* (held) */
static const uint8_t am_ani3[] = { 8, 0x13, 9, 9, 0xFF };
static const uint8_t *const am_anims[] = { am_ani0, am_ani1, am_ani2, am_ani3 };

static void amy_heart_new(uint32_t o)
{
    uint8_t t = OB8(o, AM_HEART);
    SET8(o, AM_HEART, t + 8);
    if (t + 8 < 0x100) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, CD_CODE(0x33));
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + (OB8(o, OBJ_status) & 1 ? -14 : 12)));
    SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) - 12));
}

static void amy(uint32_t o)
{
    if (s_captured) { captured(o); return; }
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* (till player 1 is up to her) */
        o_init(o, SCDA_AMY3, OB8(o, OBJ_render) | 4, 1, 0x10, 0x18);
        amy_pal(1);
        face_player(o);
        if (OB16(p, OBJ_x_pos) >= OB16(o, OBJ_x_pos)) SET8(o, OBJ_routine, 2);
        animate_pairs(o, am_anims);
        break;
    case 2: {                                                 /* running */
        uint32_t m = 0xFF0000u | OB16(o, AM_METAL);
        if (OB16(o, AM_METAL) && OB8(m, 0x43)) {              /* (in his arms) */
            SET8(o, OBJ_routine, 4);
            tri_scd_voice(SCD_PCM_AMYYELP);
            animate_pairs(o, am_anims);
            amy_heart_new(o);
            break;
        }
        face_player(o);
        if ((OB8(o, OBJ_status) & 1) && OB16(o, OBJ_x_pos) < 0x80) {
            SET8(o, OBJ_anim, 0);
            SET16(o, OBJ_x_vel, 0);
        } else {
            int run = OB16(o, OBJ_x_pos) < 0x3C0;
            if (run) {
                int16_t v = (int16_t)(OBS16(o, OBJ_x_vel) + (OB8(o, OBJ_status) & 1 ? -0x20 : 0x20));
                if ((v < 0 ? -v : v) >= 0x200) v = v < 0 ? -0x200 : 0x200;
                SET16(o, OBJ_x_vel, (uint16_t)v);
                cd_move(o, (int32_t)v << 8, 0);
                SET8(o, OBJ_anim, 1);
                run = OB16(o, OBJ_x_pos) < 0x3C0;
            }
            if (!run) {                                       /* (there: he comes for her) */
                SET8(o, OBJ_anim, 0);
                if (!OB16(o, AM_METAL)) {
                    uint32_t a1 = o_alloc();
                    if (a1) {
                        SET32(a1, OBJ_code, CD_CODE(0x31));
                        SET16(a1, OBJ_x_pos, 0x500);
                        SET16(a1, OBJ_y_pos, 0x3E8);
                        SET16(a1, 0x3A, (uint16_t)o);         /* (his oVar34: her) */
                        SET16(o, AM_METAL, (uint16_t)a1);
                    }
                }
            }
        }
        animate_pairs(o, am_anims);
        amy_heart_new(o);
        break;
    }
    default: {                                                /* held, struggling */
        uint32_t m = 0xFF0000u | OB16(o, AM_METAL);
        if (r32(m) != CD_CODE(0x31)) { o_delete(o); return; }
        int left = !(OB8(m, OBJ_status) & 1);
        set_flip(o, left);
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(m, OBJ_x_pos) + (left ? 8 : -8)));
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(m, OBJ_y_pos) + 4));
        SET8(o, OBJ_anim, 2);
        animate_pairs(o, am_anims);
        break;
    }
    }
    if (over()) { captured(o); return; }
    o_draw(o);
}

/* ---- $33 Amy's hearts --------------------------------------------------- */
#define HT_T     0x40                  /* oVar3A */
#define HT_STILL 0x42                  /* oVar3C */
static void heart(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_HEART3, OB8(o, OBJ_render) | 4, 3, 8, 8);
        SET16(o, OBJ_y_vel, (uint16_t)-0x60);
        SET8(o, OBJ_frame, 0x0A);
    }
    if (!OB8(o, HT_STILL)) {                                  /* (a waver) */
        int16_t s, c;
        o_sincos((uint8_t)(OB8(o, HT_T) * 3), &s, &c);
        SET16(o, OBJ_x_vel, (uint16_t)(s >> 2));
    }
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
    uint8_t t = (uint8_t)(OB8(o, HT_T) + 1);
    SET8(o, HT_T, t);
    if (t == 0x14) SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
    if (t == 0x6E) {
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, OBJ_x_vel, 0);
        SET8(o, HT_STILL, 0xFF);
    }
    if (t == 0x78) { o_delete(o); return; }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $31 Metal Sonic, $34 his jet ---------------------------------------- */
#define MS_COUNT 0x36                  /* oVar30 */
#define MS_Y0    0x38                  /* oVar32 (16.16: its fraction CD's oVar34, Amy's slot) */
#define MS_AMY   0x3A                  /* oVar34 */
#define MS_ANG   0x40                  /* oVar3A: a wait, then the hover's angle */
#define MS_RISE  0x41                  /* oVar3B */
#define MS_GRAB  0x43                  /* oVar3D: she is held */
#define MS_ACC   0x44                  /* oVar3E */
#define JT_PAR   0x3A                  /* (the jet's oVar34) */
static const uint8_t ms_ani0[] = { 0, 0, 1, 0, 0xFF };
static const uint8_t ms_ani1[] = { 6, 1, 7, 1, 8, 1, 9, 1, 0xFF };
static const uint8_t ms_ani2[] = { 0x0F, 1, 0xFF };              /* (holding her) */
static const uint8_t *const ms_anims[] = { ms_ani0, ms_ani1, ms_ani2 };

static void ms_hover(uint32_t o)                /* (4 px up and down about its place) */
{
    int16_t s, c;
    o_sincos(OB8(o, MS_ANG), &s, &c);
    SET32(o, OBJ_y_pos, r32(o + MS_Y0) + (uint32_t)(((int32_t)(int16_t)(s * 4)) << 8));
}

static void metal_sonic(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_METALSONIC, OB8(o, OBJ_render) | 4, 2, 0x20, 0x20);
        SET8(o, OBJ_frame, 0x0E);
        SET8(o, OBJ_y_radius, 0x20);
        SET8(o, MS_ANG, 60);
        /* fall through */
    case 2:                                                   /* a second */
        if (OB8(o, MS_ANG)) {
            SET8(o, MS_ANG, OB8(o, MS_ANG) - 1);
            if (OB8(o, MS_ANG)) break;
        }
        set_flip(o, 1);
        SET16(o, MS_ACC, (uint16_t)-0x10);
        SET8(o, OBJ_routine, 4);
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_METAL);
        break;
    case 4: {                                                 /* in for her */
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, 0);
        int16_t v = (int16_t)(OBS16(o, MS_ACC) + OBS16(o, OBJ_x_vel));
        if (v <= -0x300) v = -0x300;
        SET16(o, OBJ_x_vel, (uint16_t)v);
        uint16_t to = 0x3E0;
        if (OB16(o, MS_AMY)) to = (uint16_t)(OB16(0xFF0000u | OB16(o, MS_AMY), OBJ_x_pos) + 0x20);
        if (to >= OB16(o, OBJ_x_pos)) {                       /* (at her: she is his) */
            SET16(o, OBJ_x_vel, 0);
            SET16(o, MS_ACC, 0);
            SET8(o, MS_GRAB, 0xFF);
            SET8(o, OBJ_anim, 2);
            uint32_t a1 = o_alloc();
            if (a1) { SET32(a1, OBJ_code, CD_CODE(0x34)); SET16(a1, JT_PAR, (uint16_t)o); }
            SET16(o, MS_Y0, OB16(o, OBJ_y_pos));
            SET8(o, OBJ_routine, 6);
        }
        animate_pairs(o, ms_anims);
        break;
    }
    case 6:                                                   /* hovering, a while */
        face_player(o);
        ms_hover(o);
        if (OB8(o, MS_ANG) + 4 > 0xFF) {
            SET8(o, OBJ_routine, 8);
            SET16(o, OBJ_y_vel, (uint16_t)-0x2C0);
            SET16(o, MS_ACC, 0x0B);
            SET8(o, MS_RISE, 0x40);
            SET8(o, MS_COUNT, 0x50);
        }
        SET8(o, MS_ANG, OB8(o, MS_ANG) + 4);
        animate_pairs(o, ms_anims);
        break;
    case 8: {                                                 /* up; then waiting for player 1 */
        face_player(o);
        if (OB8(o, MS_RISE)) {
            cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, MS_ACC));
            SET8(o, MS_RISE, OB8(o, MS_RISE) - 1);
            if (OB8(o, MS_RISE)) { animate_pairs(o, ms_anims); break; }
            SET16(o, MS_ACC, 0);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, MS_Y0, OB16(o, OBJ_y_pos));
        }
        ms_hover(o);
        SET8(o, MS_ANG, OB8(o, MS_ANG) + 4);
        uint16_t d0 = (uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
        if (OB16(o, OBJ_x_pos) < OB16(p, OBJ_x_pos) || (uint8_t)d0 < 0xA0) {   /* (he is near: cmpi.b) */
            SET8(o, MS_COUNT, OB8(o, MS_COUNT) - 1);
            if (!OB8(o, MS_COUNT)) {
                set_flip(o, 0);
                SET16(o, OBJ_x_vel, 0);
                SET16(o, MS_ACC, 0x60);
                SET8(o, OBJ_routine, 10);
            }
        }
        animate_pairs(o, ms_anims);
        break;
    }
    default: {                                                /* off right, with her */
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, 0);
        uint16_t v = (uint16_t)(OB16(o, MS_ACC) + OB16(o, OBJ_x_vel));
        if (v >= 0x400) v = 0x400;
        SET16(o, OBJ_x_vel, v);
        if (OB16(o, OBJ_x_pos) >= 0x528) { captured(o); return; }
        animate_pairs(o, ms_anims);
        break;
    }
    }
    if (over()) { captured(o); return; }
    o_draw(o);
}

static void metal_jet(uint32_t o)
{
    static const uint8_t frames[4] = { 0x0C, 0x0E, 0x0D, 0x0E };    /* (its script: 1 frame each, round) */
    uint32_t m = 0xFF0000u | OB16(o, JT_PAR);
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_METALSONIC, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
    }
    if (r32(m) != CD_CODE(0x31)) { o_delete(o); return; }
    SET16(o, OBJ_x_pos, (uint16_t)(OB16(m, OBJ_x_pos) + (OB8(m, OBJ_status) & 1 ? 0x10 : -0x10)));
    SET16(o, OBJ_y_pos, OB16(m, OBJ_y_pos));
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);           /* (AnimateObject: duration 1) */
    SET8(o, OBJ_anim_timer, t);
    if (t < 0) {
        SET8(o, OBJ_anim_timer, 1);
        SET8(o, OBJ_frame, frames[OB8(o, OBJ_anim_frame) & 3]);
        SET8(o, OBJ_anim_frame, (OB8(o, OBJ_anim_frame) + 1) & 3);
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    }
    o_draw(o);
}

/* ---- $30 barriers ---------------------------------------------------------- */
#define BR_TIME  0x40                  /* oVar3A: (a piece) frames left */
#define BR_PIECE 0x42                  /* oVar3C: (a piece) which */
#define BR_GRAV  0x44                  /* oVar3E */
static void barrier_fly(uint32_t o)             /* (its pieces' throw) */
{
    static const int16_t spd[4][2] = { { 0x200, -0x200 }, { -0x100, -0x400 }, { -0x100, -0x200 }, { 0x200, -0x400 } };
    const int16_t *v = spd[OB8(o, BR_PIECE) & 3];
    SET16(o, OBJ_x_vel, (uint16_t)v[0]);
    SET16(o, OBJ_y_vel, (uint16_t)v[1]);
    SET16(o, BR_GRAV, 0x60);
    SET8(o, BR_TIME, 0x78);
}

static uint32_t find_metal(void)                /* (Metal Sonic, in any slot of the level's objects) */
{
    for (int k = 3; k < 3 + 90; k++)                          /* (Dynamic_object_RAM: 90 slots) */
        if (r32(o_slot(k)) == CD_CODE(0x31)) return o_slot(k);
    return 0;
}

static void barrier(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (s_captured) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BARRIER, OB8(o, OBJ_render) | 4, 3, 0x12, 8);
        SET8(o, OBJ_y_radius, 8);
        if (sub != 1) {
            SET8(o, OBJ_y_radius, 0x20);
            SET8(o, OBJ_height_px, 0x20);
            if (sub) {
                SET8(o, OBJ_frame, 9);
                SET8(o, OBJ_coll_flags, tri_scd_coll(0x86));
                SET8(o, OBJ_width_px, 0x0C);
                if (sub != 2) SET8(o, OBJ_frame, 0x0E);
            }
        }
        if (OB8(o, BR_PIECE)) {                               /* (a piece: off it goes) */
            SET8(o, OBJ_coll_flags, 0);
            SET8(o, OBJ_routine, 4);
            barrier_fly(o);
            goto fly;
        }
        /* fall through */
    case 2: {
        uint32_t m = sub == 1 ? 0 : find_metal();
        int w = OB8(o, OBJ_width_px), h = OB8(o, OBJ_y_radius);
        if (m && (uint16_t)(OB16(m, OBJ_x_pos) - OB16(o, OBJ_x_pos) + (w + 16)) < (uint16_t)(2 * (w + 16)) &&
            (uint16_t)(OB16(m, OBJ_y_pos) - OB16(o, OBJ_y_pos) + (h + 16)) < (uint16_t)(2 * (h + 16))) {
            static const uint8_t first[3] = { 4, 8, 0x0D }, base[3] = { 0, 4, 9 };   /* (by subtype 0, 2, 3) */
            int k = sub == 0 ? 0 : sub == 2 ? 1 : 2;
            SET8(o, OBJ_routine, 4);                          /* (he flies through it: broken) */
            SET8(o, OBJ_frame, first[k]);
            for (int d1 = 3; d1 > 0; d1--) {
                uint32_t a1 = o_alloc();
                if (!a1) continue;
                SET32(a1, OBJ_code, CD_CODE(0x30));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_frame, d1 + base[k]);
                SET8(a1, BR_PIECE, d1);
            }
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_BREAK);
            barrier_fly(o);
            cd_get_off(o);
            break;
        }
        o_solid(o, w + 10, h, h + 1, OB16(o, OBJ_x_pos));
        if (sub == 1) {                                       /* (spikes: stood on, they hurt) */
            uint8_t st = OB8(o, OBJ_status);
            if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
            if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
        }
        if (OB8(o, OBJ_coll_flags)) o_touch(o);
        break;
    }
    default:                                                  /* a piece, falling */
    fly:
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, BR_GRAV));
        SET8(o, BR_TIME, OB8(o, BR_TIME) - 1);
        if (!OB8(o, BR_TIME)) { o_delete(o); return; }
        if (OB8(o, OBJ_coll_flags)) o_touch(o);               /* (the barrier itself still hurts) */
        break;
    }
    o_draw(o);
}

void scd_ccz_amy_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x30] = barrier;
    t[base + 0x31] = metal_sonic;
    t[base + 0x32] = amy;
    t[base + 0x33] = heart;
    t[base + 0x34] = metal_jet;
}
