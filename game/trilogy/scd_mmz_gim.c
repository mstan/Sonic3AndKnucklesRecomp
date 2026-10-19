/*
 * scd_mmz_gim.c — Metallic Madness's gimmicks, acts 1-2 (Sonic CD; decomp:
 * R8\LEGMECA8.C, NOKOGIRI.C, PROPERA8.C, KONBEA.C, OKUSIESO.C; the disc's
 * code (R81A-D, R82A-D) is the truth where they differ).
 *
 *   $26 legmeca   (act 2) a cartwheel platform: two legs, seven links between
 *                 them; player 1 standing on the leg that is not the pivot
 *                 (the left one) sets it swinging over the other, 64 px round
 *                 it at 2/256 of a turn a frame, until past the top it meets
 *                 the floor; then the legs swap roles; at most 12 times. Both
 *                 legs are platforms (CD's top-only SolidObject)
 *   $27 nokogiri  (act 1) a buzz saw on an arm out of its mount: a rest of 60
 *                 frames, then 73 px out along its diagonal at 1 px a frame
 *                 (subtype bit 0: leftwards, bit 1: upwards), a rest, back;
 *                 it hurts; it buzzes every 32 frames on screen. Subtype bit
 *                 7: it starts out (its mount 80 px back)
 *   $28 propera   (act 1) an indestructible walker: harmless, it falls to the
 *                 floor, then walks 128 px each way along it; two rotors (one
 *                 above, one below, flipped) take turns: 73 frames spinning,
 *                 harmful, 73 folded, 5 between
 *   $2A konbea    (act 1) subtype 0: a panel (a platform) that flips over every
 *                 120 frames, throwing off who stands on it; other subtypes:
 *                 five panels round a conveyor loop (its 446 steps, the act's
 *                 own table), platforms on the top run, turning over (not
 *                 solid) for 216 steps along the bottom
 *   $35 okusieso  (act 1) a seesaw in the background with a ball, and a
 *                 platform in front: player 1 on (or under) the platform tips
 *                 it down, the ball flies up into the distance and comes down
 *                 bigger, in front and harmful, onto the seesaw: then all of
 *                 it stops. Till then, the player off the platform, the
 *                 seesaw rocks
 * The code of each is the same in every period ($35's in both acts; it is
 * placed in act 1 only). CD's object variables oVar2A.. sit at S3K's $30..;
 * routines keep CD's even numbers. CD's ridechk (a top-only SolidObject) ->
 * o_platform, its full SolidObject -> o_solid; CD checks player 1 only: so do
 * the triggers here, while the solidity is both players' as S3K's objects.
 * None of these test the layer (prio_flag) or mini Sonic: their touches are
 * the front layer's, which act 1 always is.
 */
#include "trilogy_obj.h"


#define ST_AIR          0x02
#define ST_ONOBJ        0x08
#define OBJ_prev_anim   0x21               /* (S3K's Animate_Sprite: CD's oPrevAnim) */
#define FM_SAW          0xB1               /* (_soundset: its buzz, every 32 frames on screen) */

static int s_base;                         /* this round's native slots (scd_obj.c) */
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static uint32_t word_obj(uint32_t o, int f) { return 0xFF0000u | OB16(o, f); }

/* AnimateObject: [duration, frames..., $FF loop] (scd_ppz.c's) */
static void animate(uint32_t o, const uint8_t *const *anims, int prev)
{
    uint8_t a = OB8(o, OBJ_anim);
    if (a != OB8(o, prev)) {
        SET8(o, prev, a);
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

static void get_off_1p(uint32_t o, int n)       /* GetOffObject, one player */
{
    uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
    if (!(OB8(o, OBJ_status) & bit)) return;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
    uint32_t p = o_player(n);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~ST_ONOBJ);
}

static int p1_on(uint32_t o) { return (OB8(o, OBJ_status) & ST_P1_STAND) != 0; }

/* ======================================================================
 * $26 legmeca (act 2). The master (subtype 0) is the left leg; FindObjSlot
 * gives it seven links (subtype 1, frame 1) 8 px apart to its right and the
 * other leg (subtype 1, 64 px right). The master runs them all: the pivot
 * leg's routine (leg_center) and the swinging one's (leg_radius), then it
 * draws the legs and the links. The children only check it is there.
 * ==================================================================== */
#define LM_LINKS        0x30               /* CD $30-$3C: (the master's) the links (words), 7 */
#define LM_LEG          0x3E               /* CD $3E: the other leg */
#define LM_MASTER       0x30               /* CD $2E: (a child's) the master */
#define LM_RT2          0x40               /* CD $2A: the master's routine */
#define LM_COUNT        0x41               /* CD $2B: swings left */
#define LM_ANGLE        0x42               /* CD $2C: the swinging leg round the pivot (8.8) */
#define LM_OLDX         0x44               /* (its x before the swing moved it: carries the rider) */

static uint32_t lm_master(uint32_t leg) { return OB8(leg, OBJ_subtype) ? word_obj(leg, LM_MASTER) : leg; }
static uint32_t lm_other(uint32_t m) { return word_obj(m, LM_LEG); }

static void lm_leg_init(uint32_t o)                           /* m_init_leg */
{
    SET8(o, OBJ_frame, 0);
    SET16(o, OBJ_priority, 4 << 7);
    SET8(o, OBJ_x_radius, 0x18);
    SET8(o, OBJ_width_px, 0x18);
    SET8(o, OBJ_y_radius, 0x29);
    SET8(o, OBJ_height_px, 0x29);
}

static void lm_solid(uint32_t leg)                           /* (ridechk with player 1) */
{
    o_platform(leg, 0x18 + 10, OB8(leg, OBJ_y_radius), OB16(leg, LM_OLDX));
}

/* (each leg's routine ends in DrawObject: here the master draws them all
 * once its despawn check has passed, S3K's display list holding no deleted
 * object) */
static void lm_center(uint32_t leg)                          /* leg_center: the pivot */
{
    switch (OB8(leg, OBJ_routine)) {
    case 0:                                                   /* c_wait */
        break;
    case 2:                                                   /* c_roll: centred on its pixel */
        SET8(leg, OBJ_routine, 4);
        SET16(leg, OBJ_x_pos + 2, 0x8000);
        SET16(leg, OBJ_y_pos + 2, 0x8000);
        /* fall through */
    default: {                                                /* c_roll1: the links and the other leg round it */
        uint32_t m = lm_master(leg);
        SET16(m, LM_ANGLE, OB16(m, LM_ANGLE) + 0x200);
        int16_t s, c;
        o_sincos((uint8_t)(OB16(m, LM_ANGLE) >> 8), &s, &c);
        int32_t dx = ((int32_t)c << 16) >> 5, dy = ((int32_t)s << 16) >> 5;   /* (8 px a link) */
        uint32_t x = r32(leg + OBJ_x_pos), y = r32(leg + OBJ_y_pos);
        for (int k = 0; k < 7; k++) {
            uint32_t l = word_obj(m, LM_LINKS + k * 2);
            x += (uint32_t)dx;
            y += (uint32_t)dy;
            SET32(l, OBJ_x_pos, x);
            SET32(l, OBJ_y_pos, y);
        }
        uint32_t sw = OB8(leg, OBJ_subtype) ? m : lm_other(m);   /* (the swinging leg) */
        uint32_t x0 = r32(sw + OBJ_x_pos);
        x += (uint32_t)dx;
        y += (uint32_t)dy;
        SET32(sw, OBJ_x_pos, x);
        SET32(sw, OBJ_y_pos, y);
        SET16(sw, OBJ_x_vel, (uint16_t)((int32_t)(x - x0) >> 8));
        break;
    }
    }
    lm_solid(leg);                                            /* c_wait */
}

static void lm_radius(uint32_t leg)                          /* leg_radius: the other leg */
{
    uint32_t m = lm_master(leg);
    if (!OB8(leg, OBJ_routine)) {                             /* r_wait: player 1 on it, it swings */
        lm_solid(leg);
        if (p1_on(leg) && OB8(m, LM_COUNT)) {
            SET8(m, LM_COUNT, OB8(m, LM_COUNT) - 1);
            SET8(m, OBJ_routine, OB8(m, OBJ_routine) + 2);
            uint32_t l2 = lm_other(m);
            SET8(l2, OBJ_routine, OB8(l2, OBJ_routine) + 2);
        }
        return;
    }
    /* r_roll (CD's _ridechk tries a 2 px deeper box first, without the carry, so
     * that a rider stays on the swinging leg: S3K's keeps him anyway) */
    lm_solid(leg);
    if ((int16_t)(OB16(m, LM_ANGLE) + 0x4000) < 0) return;   /* (rising: up to the top) */
    int16_t d1 = o_floor_dist(leg);
    if (d1 >= 0) return;
    SET16(leg, OBJ_y_pos, OB16(leg, OBJ_y_pos) + d1 + 2);    /* down: the legs swap */
    SET8(m, LM_RT2, OB8(m, LM_RT2) + 2);
    SET16(m, LM_ANGLE, OB16(m, LM_ANGLE) + 0x8000);
    uint32_t l2 = lm_other(m);
    SET8(m, OBJ_routine, 0);
    SET8(l2, OBJ_routine, 0);
    SET16(m, OBJ_x_vel, 0);
    SET16(l2, OBJ_x_vel, 0);
    SET16(m, OBJ_y_vel, 0);
    SET16(l2, OBJ_y_vel, 0);
}

static int lm_init(uint32_t o)                               /* m_init */
{
    uint16_t x = OB16(o, OBJ_x_pos);
    uint32_t a1 = o;
    for (int i = 0; i <= 8; i++) {
        if (i) {
            if (!(a1 = o_alloc())) { o_delete(o); return 0; }
            SET32(a1, OBJ_code, CD_CODE(0x26));
            SET8(a1, OBJ_subtype, 1);
            SET16(a1, OBJ_priority, 3 << 7);
            SET8(a1, OBJ_frame, 1);
            SET8(a1, OBJ_x_radius, 4);
            SET8(a1, OBJ_width_px, 4);
            SET8(a1, OBJ_y_radius, 4);
            SET8(a1, OBJ_height_px, 4);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            x = (uint16_t)(x + 8);
            SET16(a1, OBJ_x_pos, x);
            SET16(o, LM_LINKS + (i - 1) * 2, (uint16_t)a1);  /* (the ninth: LM_LEG) */
            SET16(a1, LM_MASTER, (uint16_t)o);
        }
        SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 4);
        o_asset(a1, SCDA_LEGMECA);
    }
    lm_leg_init(a1);
    lm_leg_init(o);
    SET8(o, LM_RT2, 2);
    SET8(o, LM_COUNT, 12);
    SET16(o, LM_ANGLE, 0x8000);                               /* (the master left of the pivot) */
    return 1;
}

static void legmeca(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {                                /* slave: a link or the other leg */
        uint32_t m = word_obj(o, LM_MASTER);
        if (r32(m) != CD_CODE(0x26) || OB8(m, OBJ_subtype)) o_delete(o);
        return;
    }
    if (!OB8(o, LM_RT2) && !lm_init(o)) return;              /* (then on into m0move) */
    uint32_t l2 = lm_other(o);
    SET16(o, LM_OLDX, OB16(o, OBJ_x_pos));
    SET16(l2, LM_OLDX, OB16(l2, OBJ_x_pos));
    switch (OB8(o, LM_RT2)) {
    case 4: SET8(o, LM_RT2, 6); /* fall through */            /* m1reset */
    case 6: lm_center(o); lm_radius(l2); break;               /* m1move: the master the pivot */
    case 8: SET8(o, LM_RT2, 2); /* fall through */            /* m0reset */
    default: lm_center(l2); lm_radius(o); break;              /* m0move: the other leg the pivot */
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;           /* m_move_com (CheckObjDespawn) */
    o_draw(o);
    o_draw(l2);
    for (int k = 0; k < 7; k++) o_draw(word_obj(o, LM_LINKS + k * 2));
}

/* ======================================================================
 * $27 nokogiri (act 1). The mount (FindObjSlot: CD's subtype2 $FF) keeps
 * the saw's subtype here with bit 6, free in every placed one (0-3, $81,
 * $82).
 * ==================================================================== */
#define NK_TIME         0x30               /* CD $2A (word) */
#define NK_VX           0x32               /* CD $2C (long) */
#define NK_VY           0x36               /* CD $30 (long) */
#define NK_STEPS        0x3A               /* CD $34 (word) */
#define NK_X0           0x3C               /* CD $36: its place */
#define NK_BUZZ         0x3E               /* CD $38 (word) */
#define NK_PARENT       0x30               /* CD $2A: (the mount's) the saw */
#define NK_MOUNT        0x40               /* (subtype: CD's subtype2 $FF) */

static void nk_buzz(uint32_t o)                              /* _soundset */
{
    if (!(OB8(o, OBJ_render) & 0x80)) return;
    SET16(o, NK_BUZZ, OB16(o, NK_BUZZ) + 1);
    if (!(OB16(o, NK_BUZZ) & 0x1F)) o_sfx_cd(FM_SAW);
}

static int nk_tick(uint32_t o)                               /* (addi.w #-1: 1 = run out) */
{
    SET16(o, NK_TIME, OB16(o, NK_TIME) - 1);
    return !OB16(o, NK_TIME);
}

static void nk_step(uint32_t o, int sign)                    /* (out +1, in -1: the arm a step longer / shorter) */
{
    if (nk_tick(o)) {
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + sign);
        SET16(o, NK_TIME, 9);
        SET16(o, NK_STEPS, OB16(o, NK_STEPS) - 1);
        if (!OB16(o, NK_STEPS)) {
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
            SET16(o, NK_TIME, 60);
        }
    }
    int32_t vx = (int32_t)r32(o + NK_VX), vy = (int32_t)r32(o + NK_VY);
    cd_move(o, sign * vx, sign * vy);
}

static int nk_init(uint32_t o)                               /* act_init */
{
    uint8_t sub = OB8(o, OBJ_subtype);
    SET16(o, NK_X0, OB16(o, OBJ_x_pos));
    o_init(o, SCDA_NOKOGIRI, OB8(o, OBJ_render) | 4, 3, 0x50, 0x50);
    SET8(o, OBJ_y_radius, 0x50);
    SET8(o, OBJ_coll_flags, tri_scd_coll(0xA9));
    SET32(o, NK_VX, 0x10000);
    SET32(o, NK_VY, 0x10000);
    if (sub & 1) {
        SET32(o, NK_VX, (uint32_t)-(int32_t)r32(o + NK_VX));
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
    }
    if (sub & 2) {
        SET32(o, NK_VY, (uint32_t)-(int32_t)r32(o + NK_VY));
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 2);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 2);
    }
    SET16(o, NK_TIME, 60);
    SET8(o, OBJ_routine, 8);
    if (sub & 0x80) {                                         /* (out already) */
        SET8(o, OBJ_anim, 9);
        SET8(o, OBJ_routine, 4);
    }
    uint32_t a1 = o_alloc();
    if (!a1) { o_delete(o); return 0; }
    SET16(a1, NK_PARENT, (uint16_t)o);
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET8(a1, OBJ_subtype, sub | NK_MOUNT);
    SET16(a1, OBJ_priority, OB16(o, OBJ_priority) + 0x80);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    if (sub & 0x80) {                                         /* (its mount: 80 px back) */
        int d0 = sub & 1 ? -0x50 : 0x50, d1 = sub & 2 ? -0x50 : 0x50;
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) - d0);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - d1);
    }
    return 1;
}

static void nk_mount(uint32_t o)                             /* bar */
{
    uint32_t par = word_obj(o, NK_PARENT);
    if (r32(par) != CD_CODE(0x27) || (OB8(par, OBJ_subtype) & NK_MOUNT)) { o_delete(o); return; }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        o_asset(o, SCDA_NOKOBAR);
        SET8(o, OBJ_width_px, 0x20);
        SET8(o, OBJ_height_px, 0x20);
        SET8(o, OBJ_y_radius, 0x20);
    }
    o_draw(o);
}

static void nokogiri(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & NK_MOUNT) { nk_mount(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (!nk_init(o)) return;
        /* fall through: act_open runs once (60 -> 59, a pixel out) */
    case 2:                                                   /* act_open: out, a step every 9 */
        nk_step(o, 1);
        break;
    case 4:                                                   /* act_opend: out, 60 */
        if (nk_tick(o)) {
            SET8(o, OBJ_routine, 6);
            SET16(o, NK_STEPS, 9);
            SET16(o, NK_TIME, 1);
        }
        break;
    case 6:                                                   /* act_close: in */
        nk_step(o, -1);
        break;
    default:                                                  /* 8 act_closed: in, 60 */
        if (nk_tick(o)) {
            SET8(o, OBJ_routine, 2);
            SET16(o, NK_STEPS, 9);
            SET16(o, NK_TIME, 1);
        }
        break;
    }
    nk_buzz(o);
    o_animate(o, o_asset_anim(SCDA_NOKOGIRI));                /* (anim n: the arm n steps out, the saw spinning) */
    if (cd_despawn(o, OB16(o, NK_X0))) return;
    o_touch(o);
    o_draw(o);
}

/* ======================================================================
 * $28 propera (act 1). Its rotors (FindNextObjSlot: subtype 1 below,
 * flipped; $FF above) sit on it every frame.
 * ==================================================================== */
#define PR_TIME         0x30               /* CD $2A (word) */
#define PR_VX           0x32               /* CD $2C (long) */
#define PR_X0           0x36               /* CD $30: its place */
#define PR_DY           0x38               /* CD $3C: (a rotor's) its height on the body */
#define PR_PARENT       0x3A               /* CD $3E: (a rotor's) the body */

static void pr_rotor_init(uint32_t r)                        /* a_init_sub */
{
    SET8(r, OBJ_render, OB8(r, OBJ_render) | 4);
    o_asset(r, SCDA_PROPROTOR);
    SET16(r, OBJ_priority, 3 << 7);
    SET8(r, OBJ_x_radius, 0x20);
    SET8(r, OBJ_width_px, 0x20);
    SET8(r, OBJ_y_radius, 0x1C);
    SET8(r, OBJ_height_px, 0x1C);
}

static int pr_init(uint32_t o)                               /* a_init */
{
    SET16(o, PR_X0, OB16(o, OBJ_x_pos));
    SET8(o, OBJ_routine, 2);
    o_init(o, SCDA_PROPERA, OB8(o, OBJ_render) | 4, 4, 8, 0x2E);
    SET16(o, PR_TIME, 0x80);
    SET32(o, PR_VX, 0x10000);
    SET8(o, OBJ_x_radius, 8);
    SET8(o, OBJ_y_radius, 0x2E);
    uint32_t a2 = o_alloc_after(o);
    if (!a2) { o_delete(o); return 0; }
    SET32(a2, OBJ_code, r32(o + OBJ_code));
    SET16(a2, PR_PARENT, (uint16_t)o);
    SET8(a2, OBJ_subtype, 1);
    uint32_t a1 = o_alloc_after(o);
    if (!a1) { o_delete(o); return 0; }
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET16(a1, PR_PARENT, (uint16_t)o);
    SET8(a1, OBJ_subtype, 0xFF);
    SET16(a1, PR_DY, (uint16_t)-44);                          /* (above) */
    SET16(a2, PR_DY, 20);                                     /* (below, upside down) */
    SET8(a2, OBJ_render, OB8(a2, OBJ_render) | 2);
    SET8(a2, OBJ_status, OB8(a2, OBJ_status) | 2);
    pr_rotor_init(a1);
    pr_rotor_init(a2);
    return 1;
}

static int pr_tick(uint32_t o)                               /* (addi.w #-1: 1 = run out) */
{
    SET16(o, PR_TIME, OB16(o, PR_TIME) - 1);
    return !OB16(o, PR_TIME);
}

/* propera1: folded (frame 7, harmless) 73 frames, frame 6 for 5, spinning (its
 * script, touch $A8) 73, frame 6 for 5; the one above starts unfolding */
static void pr_rotor(uint32_t o)
{
    uint32_t par = word_obj(o, PR_PARENT);
    if (r32(par) != CD_CODE(0x28) || OB8(par, OBJ_subtype)) { o_delete(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* b_init */
        if ((int8_t)OB8(o, OBJ_subtype) < 0) {
            SET8(o, OBJ_routine, 0x0A);
            goto open;
        }
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                                   /* b_close */
    close:
        SET8(o, OBJ_frame, 6);
        SET16(o, PR_TIME, 5);
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, 0);
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    case 4:                                                   /* b_close1 */
        if (!pr_tick(o)) break;
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    case 6:                                                   /* b_closed */
        SET8(o, OBJ_frame, 7);
        SET16(o, PR_TIME, 0x49);
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    case 8:                                                   /* b_closed1 */
        if (!pr_tick(o)) break;
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    case 0x0A:                                                /* b_open */
    open:
        SET8(o, OBJ_frame, 6);
        SET16(o, PR_TIME, 5);
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    case 0x0C:                                                /* b_open1 */
        if (!pr_tick(o)) break;
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    case 0x0E:                                                /* b_opend: its script from the start */
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_prev_anim, 0xFF);
        SET16(o, PR_TIME, 0x49);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xA8));
        SET8(o, OBJ_coll_prop, 0);
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        /* fall through */
    default:                                                  /* $10 b_opend1 */
        if (!pr_tick(o)) {
            o_animate(o, o_asset_anim(SCDA_PROPROTOR));
            break;
        }
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) - 14);
        goto close;
    }
    SET16(o, OBJ_x_pos, OB16(par, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos) + OB16(o, PR_DY));
    o_touch(o);
    o_draw(o);
}

static void propera(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) { pr_rotor(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (!pr_init(o)) return;
        break;
    case 2: {                                                 /* a_fall: 1 px a frame to the floor */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    default:                                                  /* a_move: along the floor, 128 px each way */
        cd_move(o, (int32_t)r32(o + PR_VX), 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        if (!pr_tick(o)) break;
        SET32(o, PR_VX, (uint32_t)-(int32_t)r32(o + PR_VX));
        SET16(o, PR_TIME, 0x80);
        break;
    }
    o_animate(o, o_asset_anim(SCDA_PROPERA));
    if (cd_despawn(o, OB16(o, PR_X0))) return;
    o_draw(o);
}

/* ======================================================================
 * $2A konbea (act 1). The panels a panel makes (FindObjSlot; CD's subtype2
 * $FF) keep its subtype with bit 7 here (only 0 and 1 are placed); each
 * checks its first panel is still the one it came from.
 * ==================================================================== */
#define KB_PARENT       0x30               /* CD $2A: (a child's) the first panel */
#define KB_TIME         0x32               /* CD $2C: (a flip panel's) */
#define KB_X0           0x34               /* CD $2E / $2C: the first panel's place */
#define KB_Y0           0x36               /* CD $30 / $2E */
#define KB_INDEX        0x38               /* CD $32 (its high word): the step on the loop */
#define KB_ROLL         0x3A               /* CD $36: (on the loop) turning over */
#define KB_STOOD        0x3C               /* CD $3E */
#define KB_SUB          0x3D               /* (CD's oFrame low byte: the turn's 8.8 frame) */
#define KB_PREV         0x3E               /* (the flip script's prev anim) */
#define KB_CHILD        0x80
#define KB_STEPS        0x1BE              /* (the loop's table: 446 dx, dy) */

static const uint8_t kb_flip_ani[] = { 1, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 0, 0xFF };   /* (CD's, kept here) */
static const uint8_t *const kb_anims[] = { kb_flip_ani };
/* a_tbl0 (by CD's subtype2, 0 for every one placed): the time, the count of
 * other panels, then each one's time, x, y */
static const int16_t kb_set0[] = { 60, 0 }, kb_set1[] = { 60, 1, 60, 32, 0 };
static const int16_t *const kb_sets[] = { kb_set0, kb_set1 };

static void kb_yrad(uint32_t o, int r)
{
    SET8(o, OBJ_y_radius, r);
    SET8(o, OBJ_height_px, r);
}

static void kb_init_sub(uint32_t o, uint32_t a6)             /* a_init_sub / b_init_sub */
{
    SET8(a6, OBJ_routine, 2);
    o_init(a6, SCDA_KONBEA, OB8(a6, OBJ_render) | 4, 3, 0x10, 5);
    SET8(a6, OBJ_x_radius, 0x10);
    SET8(a6, OBJ_y_radius, 5);
    SET16(a6, KB_X0, OB16(o, OBJ_x_pos));
    SET16(a6, KB_Y0, OB16(o, OBJ_y_pos));
}

static uint32_t kb_child(uint32_t o)                         /* FindObjSlot: a panel of o's */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET16(a1, KB_PARENT, (uint16_t)o);
    SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype) | KB_CHILD);
    return a1;
}

static int kb_solid(uint32_t o, uint16_t x0)                 /* ridechk: 1 = player 1 on it */
{
    o_platform(o, 0x10 + 10, OB8(o, OBJ_y_radius), x0);
    return p1_on(o);
}

static void kb_flip(uint32_t o)                              /* subtype 0: a_act_tbl */
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* a_init */
        const int16_t *t = kb_sets[0];                        /* (CD: by subtype2) */
        SET16(o, KB_TIME, *t++);
        kb_init_sub(o, o);
        for (int n = *t++; n > 0; n--) {
            uint32_t a1 = kb_child(o);
            if (!a1) { o_delete(o); return; }
            SET16(a1, KB_TIME, *t++);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + *t++);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + *t++);
            kb_init_sub(o, a1);
        }
        break;
    }
    case 2:                                                   /* a_stop: 120 frames a platform */
        kb_yrad(o, 5);
        SET16(o, KB_TIME, 120);
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4:                                                   /* a_stop1 */
        SET8(o, KB_STOOD, kb_solid(o, OB16(o, OBJ_x_pos)) ? 0xFF : 0);
        SET16(o, KB_TIME, OB16(o, KB_TIME) - 1);
        if (OB16(o, KB_TIME)) break;
        kb_yrad(o, 0x10);
        SET8(o, OBJ_anim, 0);                                 /* (its script from the start) */
        SET8(o, KB_PREV, 0xFF);
        SET8(o, OBJ_routine, 6);
        if (OB8(o, KB_STOOD)) cd_get_off(o);                  /* (CD: player 1; here both, S3K's would stay on) */
        break;
    default:                                                  /* 6 a_move: flips, back at frame 0 */
        animate(o, kb_anims, KB_PREV);
        if (!OB8(o, OBJ_frame)) SET8(o, OBJ_routine, 2);
        break;
    }
}

static void kb_move(uint32_t o)                              /* b_move: the next step of the loop */
{
    uint32_t path = o_asset_anim(SCDA_KONBEA);
    uint16_t x0 = OB16(o, OBJ_x_pos);
    unsigned i = OB16(o, KB_INDEX);
    int dx = path ? (int8_t)tri_rd8(path + i * 2u) : 0, dy = path ? (int8_t)tri_rd8(path + i * 2u + 1) : 0;
    uint16_t x = (uint16_t)(OB16(o, KB_X0) + dx);
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, OB16(o, KB_Y0) + dy);
    SET16(o, OBJ_x_vel, (uint16_t)((uint16_t)(x - x0) << 8));
    i++;
    SET16(o, KB_INDEX, (int16_t)i >= KB_STEPS ? 0 : i);
}

static void kb_loop(uint32_t o)                              /* other subtypes: b_act_tbl */
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* b_init: four more, 89 steps apart */
        kb_init_sub(o, o);
        uint16_t at = 89;
        for (int n = 0; n < 4; n++) {
            uint32_t a1 = kb_child(o);
            if (!a1) break;                                   /* (CD: CheckObjDespawn, then on) */
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, KB_INDEX, at);
            at = (uint16_t)(at + 89);
            kb_init_sub(o, a1);
        }
        break;
    }
    case 2:                                                   /* b_stop */
        kb_yrad(o, 5);
        SET8(o, OBJ_routine, 4);
        /* fall through */
    case 4: {                                                 /* b_stop1: along the top, a platform */
        uint16_t x0 = OB16(o, OBJ_x_pos);
        kb_move(o);
        SET8(o, KB_STOOD, kb_solid(o, x0) ? 0xFF : 0);
        if (OB16(o, KB_INDEX) != 0x158) break;
        SET8(o, OBJ_routine, 6);                              /* (round the end: it turns over) */
        kb_yrad(o, 0x10);
        SET16(o, KB_ROLL, 0xD8);
        if (OB8(o, KB_STOOD)) cd_get_off(o);
        break;
    }
    default: {                                                /* 6 b_roll: frames 0-5, one each 2 */
        kb_move(o);
        SET16(o, KB_ROLL, OB16(o, KB_ROLL) - 1);
        if ((int16_t)OB16(o, KB_ROLL) < 0) {
            SET8(o, OBJ_frame, 0);
            SET8(o, KB_SUB, 0);
            SET8(o, OBJ_routine, 2);
            break;
        }
        int16_t w = (int16_t)((OB8(o, OBJ_frame) << 8 | OB8(o, KB_SUB)) + 0x80);
        if (w > 0x500) w = 0;
        SET8(o, OBJ_frame, (uint8_t)(w >> 8));
        SET8(o, KB_SUB, (uint8_t)w);
        break;
    }
    }
}

static void konbea(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub & KB_CHILD) {                                     /* (its first panel gone, or another: off) */
        uint32_t par = word_obj(o, KB_PARENT);
        if (r32(par) != CD_CODE(0x2A) || (OB8(par, OBJ_subtype) & KB_CHILD) ||
            OB16(o, KB_X0) != OB16(par, KB_X0) || OB16(o, KB_Y0) != OB16(par, KB_Y0)) {
            cd_get_off(o);
            o_delete(o);
            return;
        }
    }
    if (sub & 0x7F) kb_loop(o); else kb_flip(o);
    if (!r32(o)) return;                                      /* (deleted) */
    if (!(sub & KB_CHILD) && cd_despawn(o, OB16(o, KB_X0))) return;
    o_draw(o);
}

/* ======================================================================
 * $35 okusieso (act 1). FindNextObjSlot gives it its ball and its platform;
 * the three tell themselves apart by their flags (CD's $3E). CD reads the
 * seesaw through the others' pointer unchecked: here they go with it.
 * ==================================================================== */
#define OK_T0           0x30               /* CD $30 */
#define OK_T1           0x31               /* CD $31 */
#define OK_PARENT       0x32               /* CD $32: (the ball's, the platform's) the seesaw */
#define OK_Y0           0x36               /* CD $36: the seesaw's y */
#define OK_FLAGS        0x3E               /* CD $3E */
#define OKF_BALL        0x01
#define OKF_DAI         0x02               /* (the platform) */
#define OKF_ON          0x04               /* (the seesaw's) player 1 on the platform */
#define OKF_DONE        0x10               /* the ball is down: all still */
#define OKF_GO          0x20               /* (the seesaw's) set off */
#define OKF_ROCK        0x40
#define OKF_STARTED     0x80

static int ok_borrow(uint32_t o, int f)                      /* (subq.b #1 / bcc: 1 = it was 0) */
{
    uint8_t v = OB8(o, f);
    SET8(o, f, v - 1);
    return v == 0;
}

static void ok_flags(uint32_t o, uint8_t set, uint8_t clr)
{
    SET8(o, OK_FLAGS, (OB8(o, OK_FLAGS) | set) & ~clr);
}

static void ok_init(uint32_t o)                              /* okusieso_init */
{
    SET8(o, OBJ_routine, 2);
    o_init(o, SCDA_OKUSIESO, OB8(o, OBJ_render) | 4, 5, 0x28, 0x38);
    SET8(o, OBJ_frame, 0);
    SET8(o, OBJ_y_radius, 0x38);
    SET8(o, OK_T0, 1);
    SET16(o, OK_FLAGS, 0);
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) return;
        SET32(a1, OBJ_code, r32(o + OBJ_code));
        SET8(a1, OBJ_routine, 2);
        SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
        SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
        SET16(a1, OK_Y0, OB16(o, OBJ_y_pos));
        SET16(a1, OK_PARENT, (uint16_t)o);
        SET16(a1, OBJ_y_vel, 0);
        SET16(a1, OK_FLAGS, 0);
        if (!k) {                                             /* the ball (its own line: CD's tile $43A0) */
            o_init(a1, SCDA_OKUBALL, 4, 6, 0x10, 0x10);
            SET8(a1, OBJ_frame, 5);
            SET8(a1, OBJ_y_radius, 0x10);
            ok_flags(a1, OKF_BALL, 0);
        } else {                                              /* the platform, 44 px up */
            o_init(a1, SCDA_OKUSIESO, 4, 3, 0x28, 0x0C);
            SET8(a1, OBJ_frame, 8);
            SET8(a1, OBJ_y_radius, 0x0C);
            ok_flags(a1, OKF_DAI, 0);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 44);
        }
    }
}

static void ok_seesaw(uint32_t o)                            /* okusieso_move (the seesaw's) */
{
    static const uint8_t rock[4] = { 3, 2, 1, 2 };
    uint8_t f = OB8(o, OK_FLAGS);
    if (!(f & OKF_GO)) return;
    /* (CD: none of it while time_stop runs, a monitor's power never placed) */
    SET16(o, OBJ_y_vel, 0);
    if (!(f & OKF_STARTED)) {                                 /* (two frames, then down) */
        if (!ok_borrow(o, OK_T0)) return;
        SET8(o, OK_T1, 5);
        ok_flags(o, OKF_STARTED, 0);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        return;
    }
    if (!(f & OKF_ROCK)) {                                    /* (down a frame every 6, to 4) */
        if (!ok_borrow(o, OK_T1)) return;
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET8(o, OK_T1, 5);
        if ((int8_t)OB8(o, OBJ_frame) < 4) return;
        ok_flags(o, OKF_ROCK, 0);
        SET8(o, OK_T0, 0);
        SET8(o, OK_T1, 10);
        return;
    }
    if (f & OKF_ON) {                                         /* (stood on: down) */
        if (OB8(o, OBJ_frame) == 4) return;
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET8(o, OK_T1, 10);
        return;
    }
    if (!ok_borrow(o, OK_T1)) return;                         /* (else it rocks: a frame every 11) */
    SET8(o, OK_T0, OB8(o, OK_T0) + 1);
    int d1 = (int8_t)OB8(o, OK_T0);
    if (d1 == 4) { SET8(o, OK_T0, 0); d1 = 0; }
    SET8(o, OBJ_frame, rock[d1 & 3]);
    SET8(o, OK_T1, 10);
}

static void ok_ball(uint32_t o, uint32_t par)                /* ball_move: y speed at a quarter (lsl #6) */
{
    if (OB8(par, OK_FLAGS) & OKF_GO) {
        if (!(OB8(o, OK_FLAGS) & OKF_STARTED)) {
            SET16(o, OBJ_y_vel, (uint16_t)-0x3000);
            ok_flags(o, OKF_STARTED, 0);
        } else {
            int16_t v = (int16_t)(OB16(o, OBJ_y_vel) + 0x100);
            SET16(o, OBJ_y_vel, (uint16_t)v);
            if (v >= 0 && (uint16_t)(OB16(o, OBJ_y_pos) - OB16(o, OK_Y0)) <= 8) {
                SET16(o, OBJ_y_pos, OB16(o, OK_Y0) + 4);      /* (down on the seesaw: the end) */
                ok_flags(par, OKF_DONE, 0);
                ok_flags(o, OKF_DONE, 0);
                SET8(par, OBJ_frame, 4);
                SET16(o, OBJ_y_vel, 0);
            } else if (!v) {                                  /* (the top: in front now, and harmful) */
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
                SET8(o, OBJ_coll_flags, tri_scd_coll(0xA4));
                SET16(o, OBJ_priority, 4 << 7);
            } else if ((uint16_t)v == 0xE000)
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        }
    }
    SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 6));
}

/* CD's SolidObject (a full one; player 1): 1 = he is on it or hit it from
 * below. On it, CD moves him by its y speed too (a frame's fall of the
 * platform), then out of the floor if that put him in it (off it, then) */
static int ok_dai_solid(uint32_t o)
{
    int16_t r = o_solid_1p(o, 0, 0x28 + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos), 0);
    uint32_t p2 = RAM_Player_2;
    if (o_player_active(p2) && (OB8(p2, OBJ_render) & 0x80))  /* (SolidObjectFull: player 2 on screen) */
        o_solid_1p(o, 1, 0x28 + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos), 0);
    if (!p1_on(o)) return r == -2;
    uint32_t p = RAM_Player_1;
    SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8));
    int16_t d1 = o_floor_dist(p);
    if (d1 >= 0) return 1;
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + d1);
    get_off_1p(o, 0);
    return 0;
}

static void ok_dai(uint32_t o, uint32_t par)                 /* dai_move */
{
    static const uint8_t speed[5] = { 0, 8, 0x10, 8, 0 };
    static const int16_t ofs[5] = { -44, -28, -12, 12, 28 };
    if (!(OB8(par, OK_FLAGS) & OKF_GO) && ok_dai_solid(o)) ok_flags(par, OKF_GO, 0);
    unsigned f = OB8(par, OBJ_frame);
    if (f > 4) f = 4;                                         /* (the seesaw's: 0-4) */
    SET16(o, OBJ_y_pos, OB16(o, OK_Y0) + ofs[f]);
    SET16(o, OBJ_y_vel, (uint16_t)(speed[f] << 8));
    ok_flags(par, 0, OKF_ON);
    if (ok_dai_solid(o)) ok_flags(par, OKF_ON, 0);
}

static void okusieso(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) ok_init(o);
    else {
        uint8_t f = OB8(o, OK_FLAGS);
        if (f & (OKF_BALL | OKF_DAI)) {
            uint32_t par = word_obj(o, OK_PARENT);
            if (r32(par) != CD_CODE(0x35) || (OB8(par, OK_FLAGS) & (OKF_BALL | OKF_DAI))) {
                if (f & OKF_DAI) cd_get_off(o);
                o_delete(o);
                return;
            }
            if (!(f & OKF_DONE)) {
                if (f & OKF_BALL) ok_ball(o, par);
                else ok_dai(o, par);
            }
        } else if (!(f & OKF_DONE))
            ok_seesaw(o);
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) {              /* (CD: DeleteObject, its mark kept as CD's) */
        if (OB8(o, OK_FLAGS) & OKF_DAI) cd_get_off(o);
        o_delete(o);
        return;
    }
    o_touch(o);
    o_draw(o);
}

void scd_mmz_gim_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x26] = legmeca;
    t[base + 0x27] = nokogiri;
    t[base + 0x28] = propera;
    t[base + 0x2A] = konbea;
    t[base + 0x35] = okusieso;
}
