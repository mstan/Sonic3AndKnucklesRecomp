/*
 * scd_qqz_badnik.c — Quartz Quadrant's badniks (Sonic CD; decomp: R5\DENDEN.C,
 * SASORI.C, KEMUSI.C, KUMO.C; the disc's code, the same in both acts and every
 * period).
 *
 *   $20 Denden   a snail: crawls to and fro on the floor; with the player
 *                near, its spike is out (an unseen touch above it)
 *   $21 Sasori   a scorpion: walks to and fro; the player near, it faces
 *                him, raises its tail and fires a shot along the floor
 *   $22 Kemusi   a caterpillar of four pieces, inching along the floor (the
 *                head's three segments hurt; the damaged one's don't)
 *   $23 Kumo     a spider: hops; seeing the player in front it turns its
 *                back and shoots a web behind it, which holds him still for
 *                two seconds
 * Subtype 0 is the robot as built, any other its damaged self (slower, no
 * attack): placed in the futures, and in the present where the futures'
 * robots are; a good future blows those up (DestroyOnGoodFuture). The
 * children are Sonic CD's subtype / subtype2 ones; here they keep a subtype
 * with bit 7 (no robot placed here has it). CD's object variables oVar2A..
 * sit at S3K's $30.. (oVarNN at $NN + 6).
 */
#include "trilogy_obj.h"

int scd_time_warping(void);

/* asset ids (-> trilogy_obj.h) */

#define P_ROUTINE       0x05
#define P_OBJCTRL       0x2E
#define P_INVULN        0x34
#define FM_SHOT         0xA0               /* (body_waitb1: Sasori's shot; kumo_tama1: Kumo's web) */
#define FM_SPIKE        0xB7               /* (m_move: Denden's spike comes out) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

/* ---- shared ------------------------------------------------------------- */
/* CD's DrawObject checks oWidth across and oYRadius up and down: S3K's
 * width_px / height_px */
static void radii(uint32_t o, int xr, int yr)
{
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_width_px, xr);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_height_px, yr);
}

static void badnik_init(uint32_t o, int asset, int prio, int xr, int yr, uint8_t coll)
{
    o_init(o, asset, OB8(o, OBJ_render) | 4, prio, xr, yr);
    radii(o, xr, yr);
    SET8(o, OBJ_coll_flags, coll ? tri_scd_coll(coll) : 0);
}

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

/* DrawObject + CheckObjDespawn2 (home: where it was placed), touchable */
static void badnik_end(uint32_t o, uint16_t home)
{
    if (cd_despawn(o, home)) return;
    o_touch(o);
    o_draw(o);
}

static int32_t lval(uint32_t o, int f) { return (int32_t)r32(o + (uint32_t)f); }

/* along the floor: on it if it is no more than 7 px off (1), else turn (0) */
static int on_floor(uint32_t o)
{
    int16_t d1 = o_floor_dist(o);
    if ((uint16_t)(d1 + 7) >= 14) return 0;
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
    return 1;
}

static int far_from(uint32_t o, uint16_t home, int d)
{
    int16_t v = (int16_t)(OB16(o, OBJ_x_pos) - home);
    return (v < 0 ? -v : v) >= d;
}

/* within a box round o: (player - o + h) < 2h, unsigned, both ways */
static int near(uint32_t o, uint32_t p, int w, int h)
{
    return (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + h) < (unsigned)(2 * h) &&
           (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + w) < (unsigned)(2 * w);
}

/* ---- $20 Denden --------------------------------------------------------- */
#define DD_X0    0x30                  /* oVar2A: where it was placed */
#define DD_SPD   0x32                  /* oVar2C (long) */
#define DD_LINK  0x44                  /* oVar3E: its spike (the spike's: its snail) */

/* the spike: a touch 20 px above the snail, its frame empty. (Sonic CD
 * spawns it without render flags: drawn in screen space, but DrawObjects
 * marks all it draws on screen, so it touches wherever it is) */
static void denden_spike(uint32_t o)
{
    uint32_t par = 0xFF0000u | OB16(o, DD_LINK);
    if (r32(par) != CD_CODE(0x20) || (OB8(par, OBJ_subtype) & 0x80) || OB16(par, DD_LINK) != (uint16_t)o) {
        o_delete(o);                                          /* (CD: the id, and subtype 2, still its snail's) */
        return;
    }
    SET16(o, OBJ_x_pos, OB16(par, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos) - 20);
    o_touch(o);
}

static void denden_move(uint32_t o, int dmg, int asset)       /* m_move */
{
    int32_t spd = lval(o, DD_SPD);
    cd_move(o, spd, 0);
    if (far_from(o, OB16(o, DD_X0), 0x50) || !on_floor(o)) {  /* (turn: no animation this frame) */
        SET32(o, DD_SPD, (uint32_t)-spd);
        turn(o);
        return;
    }
    if (!dmg) {
        if (near(o, RAM_Player_1, 120, 120)) {                /* m_check: the spike out */
            SET8(o, OBJ_anim, 1);
            if (!OB16(o, DD_LINK)) {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, CD_CODE(0x20));
                    SET8(a1, OBJ_subtype, 0xFF);
                    SET8(a1, OBJ_render, 4);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 20);
                    SET8(a1, OBJ_coll_flags, tri_scd_coll(0xB0));
                    SET16(a1, DD_LINK, (uint16_t)o);
                    SET16(o, DD_LINK, (uint16_t)a1);
                    if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SPIKE);
                }
            }
        } else {                                              /* (away: in again) */
            SET8(o, OBJ_anim, 0);
            if (OB16(o, DD_LINK)) {
                o_delete(0xFF0000u | OB16(o, DD_LINK));
                SET16(o, DD_LINK, 0);
            }
        }
    }
    o_animate(o, o_asset_anim(asset));
}

static void denden(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { denden_spike(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_QQZ_DENDEN2 : SCDA_QQZ_DENDEN;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET16(o, DD_X0, OB16(o, OBJ_x_pos));
        badnik_init(o, asset, 3, 16, 15, 0x2F);
        SET32(o, DD_SPD, (uint32_t)-0x4000);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* m_fall: down onto the floor */
        cd_move(o, 0, 0x10000);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    default:
        denden_move(o, dmg, asset);
        break;
    }
    badnik_end(o, OB16(o, DD_X0));
}

/* ---- $21 Sasori --------------------------------------------------------- */
#define SS_X0    0x30                  /* oVar2A: where it was placed (a child's: its body) */
#define SS_SPD   0x32                  /* oVar2C (long; the shot's too) */
#define SS_TAIL  0x36                  /* oVar30: its tail */
#define SS_TAILX 0x38                  /* oVar32: the tail, across from the body */
#define SS_TIME  0x3A                  /* oVar34 */
#define SS_TAILK 0x80                  /* (subtype: CD's subtype2 1, the tail) */
#define SS_SHOTK 0x81                  /* (CD's subtype2 -1, the shot) */

static uint32_t ss_tail(uint32_t o) { return 0xFF0000u | OB16(o, SS_TAIL); }

static void ss_child(uint32_t o, uint32_t a1, uint8_t kind, int asset)   /* init_sub */
{
    SET32(a1, OBJ_code, CD_CODE(0x21));
    SET8(a1, OBJ_subtype, kind);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
    SET16(a1, SS_X0, (uint16_t)o);
    o_asset(a1, asset);
    SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 4);
}

static void ss_place_tail(uint32_t o)
{
    uint32_t a1 = ss_tail(o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, SS_TAILX));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 16);
}

static void ss_turn(uint32_t o)                               /* (the step back, then about) */
{
    int32_t spd = lval(o, SS_SPD);
    cd_move(o, -spd, 0);
    SET32(o, SS_SPD, (uint32_t)-spd);
    SET16(o, SS_TAILX, (uint16_t)-OBS16(o, SS_TAILX));
    turn(o);
    turn(ss_tail(o));
}

/* the tail up (or down: dy, the tail 5 px from the body's back) */
static void ss_tail_step(uint32_t o, int dy)
{
    uint32_t a1 = ss_tail(o);
    int d0 = lval(o, SS_SPD) < 0 ? 5 : -5;
    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + dy);
    SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (dy < 0 ? d0 : -d0));
}

static int ss_wait(uint32_t o)                                /* (a timer's frame: 1 when it is out) */
{
    SET16(o, SS_TIME, OB16(o, SS_TIME) - 1);
    return !OB16(o, SS_TIME);
}

static void sasori_child(uint32_t o)
{
    uint32_t par = 0xFF0000u | OB16(o, SS_X0);
    if (r32(par) != CD_CODE(0x21) || (OB8(par, OBJ_subtype) & 0x80)) { o_delete(o); return; }
    if (OB8(o, OBJ_subtype) == SS_TAILK) { o_draw(o); return; }   /* (its body moves it) */
    cd_move(o, lval(o, SS_SPD), 0);                           /* the shot */
    o_animate(o, o_asset_anim(SCDA_QQZ_SASORISHOT));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

static void sasori(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { sasori_child(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_QQZ_SASORI2 : SCDA_QQZ_SASORI;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* body_init */
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x18, 0x0C, 0x31);
        SET16(o, SS_X0, OB16(o, OBJ_x_pos));
        SET32(o, SS_SPD, dmg ? (uint32_t)-0x8000 : (uint32_t)-0x10000);
        SET16(o, SS_TAILX, dmg ? 7 : 3);
        break;
    case 2: {                                                 /* body_fall: down; then its tail */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET8(o, OBJ_routine, 4);
        uint32_t a1 = o_alloc_after(o);
        if (!a1) { o_delete(o); return; }
        SET16(o, SS_TAIL, (uint16_t)a1);
        ss_child(o, a1, SS_TAILK, SCDA_QQZ_SASORITAIL);
        radii(a1, 0x18, 0x10);
        SET8(a1, OBJ_frame, OB8(o, OBJ_subtype));             /* (damaged: its frame 1) */
        SET16(a1, OBJ_priority, OB16(o, OBJ_priority) - 0x80);
        break;
    }
    case 4: {                                                 /* body_move */
        cd_move(o, lval(o, SS_SPD), 0);
        if (far_from(o, OB16(o, SS_X0), 0x50) || !on_floor(o)) { ss_turn(o); break; }
        ss_place_tail(o);
        uint32_t p = RAM_Player_1;
        if (!near(o, p, 0x50, 0x28)) break;
        SET8(o, OBJ_routine, 6);                              /* (the player near: it faces him) */
        if (((int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos)) < 0) != (lval(o, SS_SPD) < 0)) ss_turn(o);
        break;
    }
    case 6:                                                   /* body_waita: a pause, tail up */
        SET8(o, OBJ_routine, 8);
        SET16(o, SS_TIME, 30);
        ss_place_tail(o);
        /* fall through */
    case 8:
        if (ss_wait(o)) { SET8(o, OBJ_routine, 10); ss_tail_step(o, -5); }
        break;
    case 10:                                                  /* body_waitb: fire */
        SET8(o, OBJ_routine, 12);
        SET16(o, SS_TIME, 10);
        /* fall through */
    case 12:
        if (!ss_wait(o)) break;
        SET8(o, OBJ_routine, 14);
        if (!dmg) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            ss_child(o, a1, SS_SHOTK, SCDA_QQZ_SASORISHOT);
            radii(a1, 0x10, 0x10);
            SET8(a1, OBJ_coll_flags, tri_scd_coll(0xB2));
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 24);
            int left = lval(o, SS_SPD) < 0;
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (left ? -4 : 4));
            SET32(a1, SS_SPD, left ? (uint32_t)-0x30000 : 0x30000u);
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SHOT);
        }
        break;
    case 14:                                                  /* body_waitc: tail down */
        SET8(o, OBJ_routine, 16);
        SET16(o, SS_TIME, 30);
        /* fall through */
    case 16:
        if (ss_wait(o)) { SET8(o, OBJ_routine, 18); ss_tail_step(o, 5); }
        break;
    case 18:                                                  /* body_waitd: a pause, then on */
        SET8(o, OBJ_routine, 20);
        SET16(o, SS_TIME, 30);
        /* fall through */
    default:
        if (ss_wait(o)) SET8(o, OBJ_routine, 4);
        break;
    }
    o_animate(o, o_asset_anim(asset));
    badnik_end(o, OB16(o, SS_X0));
}

/* ---- $22 Kemusi: the head and three segments 12 px apart; each inches by
 * its own phase (oVar2A 3, 2, 1, 0, swapped with 3 every half-cycle of 36
 * frames: 0 and 3, 1 and 2) --------------------------------------------- */
#define KM_STEP  0x30                  /* oVar2A: subtracted from the step counter */
#define KM_LEN0  0x32                  /* oVar2C: a half-cycle (36 frames) */
#define KM_LEN   0x34                  /* oVar2E */
#define KM_PER0  0x36                  /* oVar30: the step counter's start (6; damaged 12) */
#define KM_PER   0x38                  /* oVar32 */
#define KM_X0    0x3A                  /* oVar34: the head's place */
#define KM_DOWN  0x3C                  /* oVar36: pieces on the floor (the head's) */
#define KM_LINK  0x40                  /* oVar3A, 3C, 3E: the other three pieces */
#define KM_SEG   0x80                  /* (subtype: CD's subtype2 -1, a segment) */

static uint32_t km_link(uint32_t o, int k) { return 0xFF0000u | OB16(o, KM_LINK + k * 2); }

static void km_common(uint32_t o, uint32_t a1)                /* kemusi_com */
{
    SET8(a1, OBJ_routine, 2);
    o_asset(a1, SCDA_QQZ_KEMUSI);
    SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 4);
    SET16(a1, OBJ_priority, 3 << 7);
    SET8(a1, OBJ_x_radius, 8);
    SET8(a1, OBJ_width_px, 8);
    SET16(a1, KM_X0, OB16(o, OBJ_x_pos));
}

static void km_init(uint32_t o)                               /* kemusi_init */
{
    int dmg = OB8(o, OBJ_subtype) != 0;
    SET16(o, KM_STEP, 3);
    SET8(o, OBJ_frame, 1);
    SET8(o, OBJ_y_radius, 0x0C);
    SET8(o, OBJ_height_px, 0x0C);
    SET8(o, OBJ_coll_flags, tri_scd_coll(0x33));
    km_common(o, o);
    SET16(o, KM_LEN0, 36);
    SET16(o, KM_PER0, dmg ? 12 : 6);
    uint32_t seg[3];
    uint16_t x = OB16(o, OBJ_x_pos);
    for (int k = 0; k < 3; k++) {                             /* (a segment's phase: 2, 1, 0) */
        uint32_t a1 = o_alloc_after(o);                       /* (FindNextObjSlot) */
        if (!a1) { o_delete(o); return; }
        seg[k] = a1;
        SET16(o, KM_LINK + k * 2, (uint16_t)a1);
        SET32(a1, OBJ_code, CD_CODE(0x22));
        SET16(a1, KM_STEP, 2 - k);
        x = (uint16_t)(x + 12);
        SET16(a1, OBJ_x_pos, x);
        SET8(a1, OBJ_frame, dmg ? 3 : 2);
        SET8(a1, OBJ_y_radius, dmg ? 7 : 0x0B);
        SET8(a1, OBJ_height_px, dmg ? 7 : 0x0B);
        SET8(a1, OBJ_coll_flags, tri_scd_coll(dmg ? 0x33 : 0xB4));
        SET8(a1, OBJ_subtype, KM_SEG | OB8(o, OBJ_subtype));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET16(a1, KM_LEN0, OB16(o, KM_LEN0));
        SET16(a1, KM_PER0, OB16(o, KM_PER0));
        km_common(o, a1);
    }
    for (int k = 0; k < 3; k++) {                             /* (each knows the other three: the head first) */
        SET16(seg[k], KM_LINK, (uint16_t)o);
        for (int j = 0, n = 1; j < 3; j++)
            if (j != k) SET16(seg[k], KM_LINK + 2 * n++, (uint16_t)seg[j]);
    }
}

static void km_step(uint32_t o)                               /* kemusi_move1_move */
{
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + (OB8(o, OBJ_render) & 1 ? 1 : -1));
}

static void km_move1(uint32_t o)                              /* kemusi_move1 (+ _rev) */
{
    for (;;) {
        SET16(o, KM_PER, OB16(o, KM_PER) - OB16(o, KM_STEP));
        if (OB16(o, KM_PER)) break;
        SET16(o, KM_PER, OB16(o, KM_PER0));
        km_step(o);
        int16_t d = (int16_t)(OB16(o, KM_X0) - OB16(o, OBJ_x_pos));
        if ((d < 0 ? -d : d) < 0x50 && on_floor(o)) break;
        turn(o);                                              /* (about: the step back, and again) */
        km_step(o);
    }
    SET16(o, KM_LEN, OB16(o, KM_LEN) - 1);
    if (!OB16(o, KM_LEN)) SET8(o, OBJ_routine, OB8(o, OBJ_routine) - 2);
}

static void kemusi(uint32_t o)
{
    int seg = OB8(o, OBJ_subtype) & KM_SEG;
    if (!seg && cd_destroy_good_future(o)) return;
    if (OB8(o, OBJ_routine))                                  /* (a piece gone: all go) */
        for (int k = 0; k < 3; k++)
            if (r32(km_link(o, k)) != CD_CODE(0x22)) { o_delete(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        km_init(o);
        if (r32(o) != CD_CODE(0x22)) return;                  /* (no room for its segments) */
        break;
    case 2: {                                                 /* kemusi_fall */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET8(o, OBJ_routine, 4);
        uint32_t head = seg ? km_link(o, 0) : o;
        SET16(head, KM_DOWN, OB16(head, KM_DOWN) + 1);
        break;
    }
    case 4:                                                   /* kemusi_stop: all down, all go */
        if (seg || OB16(o, KM_DOWN) != 4) break;
        for (int k = 0; k < 3; k++) SET8(km_link(o, k), OBJ_routine, OB8(km_link(o, k), OBJ_routine) + 2);
        SET8(o, OBJ_routine, 6);
        /* fall through */
    case 6:                                                   /* kemusi_move */
        SET8(o, OBJ_routine, 8);
        SET16(o, KM_STEP, OB16(o, KM_STEP) ^ 3);
        SET16(o, KM_LEN, OB16(o, KM_LEN0));
        SET16(o, KM_PER, OB16(o, KM_PER0));
        if (!seg) SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
        /* fall through */
    default:
        km_move1(o);
        break;
    }
    if (seg) { o_touch(o); o_draw(o); return; }               /* (the segments go with the head) */
    badnik_end(o, OB16(o, KM_X0));
}

/* ---- $23 Kumo ------------------------------------------------------------ */
#define KU_TIME  0x30                  /* oVar2A (the web's: holding) */
#define KU_WHO   0x32                  /* oVar2C: (the web's) whom it holds */
#define KU_JUMP  0x36                  /* oVar30: the hop's speed (long) */
#define KU_YVEL  0x3A                  /* oVar34 (long; the web's: its x speed) */

/* the web: on until it touches player 1 (CD's special touch $37, 12 x 8:
 * oColStatus), then on him for 120 frames, his x speeds held at 0 */
static void kumo_web(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (!OB16(o, KU_TIME)) {
        int hit = (o_s1_touch(o, 12, 8) & 1) && !(OB8(p, P_OBJCTRL) & 0x80);
        if (!hit || OB8(p, P_ROUTINE) == 4 || OB8(p, P_ROUTINE) == 6 || OB8(p, P_INVULN)) {
            cd_move(o, lval(o, KU_YVEL), 0);
            goto draw;
        }
        SET16(o, KU_WHO, (uint16_t)p);
        SET16(o, KU_TIME, 120);
    }
    SET16(o, KU_TIME, OB16(o, KU_TIME) - 1);
    if (!OB16(o, KU_TIME)) { o_delete(o); return; }
    p = 0xFF0000u | OB16(o, KU_WHO);
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_ground_vel, 0);
draw:
    o_animate(o, o_asset_anim(SCDA_QQZ_KUMOWEB));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* kumo_check: player 1 in a 160 px box and on the side it faces: it turns
 * its back on him */
static int ku_sees(uint32_t o, uint32_t p)
{
    if (!near(o, p, 0x50, 0x50)) return 0;
    int left = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos)) < 0, faces_left = !(OB8(o, OBJ_render) & 1);
    if (left != faces_left) return 0;
    turn(o);
    return 1;
}

static void kumo(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { kumo_web(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_QQZ_KUMO2 : SCDA_QQZ_KUMO;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* kumo_init (CD: subtype 2 set, facing
                                                                  right: none placed) */
        badnik_init(o, asset, 3, 0x18, 0x18, 0x35);
        SET32(o, KU_JUMP, dmg ? (uint32_t)-0x50000 : (uint32_t)-0x80000);
        SET8(o, OBJ_routine, 4);
        goto fall;
    case 2:                                                   /* kumo_jump */
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 8);
        SET32(o, KU_YVEL, r32(o + KU_JUMP));
        /* fall through */
    case 4:                                                   /* kumo_jump1 */
    fall: {
        SET32(o, KU_YVEL, r32(o + KU_YVEL) + 0x4000);
        cd_move(o, 0, lval(o, KU_YVEL));
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 + 8);
            SET8(o, OBJ_routine, 6);
            SET8(o, OBJ_frame, 1);
        }
        break;
    }
    case 6:                                                   /* kumo_move */
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x36));
        SET16(o, KU_TIME, 180);
        /* fall through */
    case 8:                                                   /* kumo_move1 */
        if (!dmg && ku_sees(o, RAM_Player_1)) {
            SET8(o, OBJ_routine, 10);
            SET8(o, OBJ_frame, 1);
            SET16(o, KU_TIME, 60);
            break;
        }
        SET16(o, KU_TIME, OB16(o, KU_TIME) - 1);
        if (!OB16(o, KU_TIME)) SET8(o, OBJ_routine, 2);       /* (a while still: hop) */
        o_animate(o, o_asset_anim(asset));
        break;
    case 10:                                                  /* kumo_tama */
        SET16(o, KU_TIME, OB16(o, KU_TIME) - 1);
        if (!OB16(o, KU_TIME)) SET8(o, OBJ_routine, 12);
        break;
    default: {                                                /* kumo_tama1: the web, then hop */
        uint32_t a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, CD_CODE(0x23));
            SET8(a1, OBJ_subtype, 0xFF);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            o_asset(a1, SCDA_QQZ_KUMOWEB);
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET16(a1, OBJ_priority, 1 << 7);
            radii(a1, 0x0C, 8);
            int left = OB8(a1, OBJ_render) & 1;
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (left ? -0x18 : 0x18));
            SET32(a1, KU_YVEL, left ? (uint32_t)-0x30000 : 0x30000u);
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SHOT);
        }
        SET8(o, OBJ_routine, 2);
        break;
    }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;            /* (CheckObjDespawn: where it is) */
    o_touch(o);
    o_draw(o);
}

void scd_qqz_badnik_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x20] = denden;
    t[base + 0x21] = sasori;
    t[base + 0x22] = kemusi;
    t[base + 0x23] = kumo;
}
