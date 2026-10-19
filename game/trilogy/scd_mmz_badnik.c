/*
 * scd_mmz_badnik.c — Metallic Madness's badniks (Sonic CD; decomp: R8\KABUTO8.C,
 * HACHI8.C, SCARAB.C, DANGO8.C, BIGBOM8.C; the disc's code (R81A-D, R82A-D)
 * is what is transcribed).
 *
 *   $24 Kabuto   a horned beetle tank: rolls to and fro (never turning round),
 *                humming; its horn (a child) rises and falls (damaged: stays)
 *   $2E Hachi    a bee: flies to and fro in waves, a mace on a chain swinging
 *                under it (two as built, the damaged one has one)
 *   $2F Scarab   carries a monitor (its controller, placed, makes both): breaking
 *                the monitor (or touching its front once the monitor is gone),
 *                the scarab grabs player 1, holds him 2 s and throws him off ahead
 *   $2D Dango    a pill bug: walks; seeing the player ahead it curls up and
 *                rolls off; a curled player is bounced away, it rolls back
 *   $23 Bigbom   a giant walking bomb, indestructible: with the player near it
 *                stops, its fuse burns down, it blows up into seven fires
 * Their code is the same in both acts and every period. Subtype 0 is the robot
 * as built, any other its damaged self (slower); a good future blows those up
 * (DestroyOnGoodFuture; the bomb is spared). CD's oVar2A.. sit at S3K's $30..
 * (CD offset + 6). Children, which Sonic CD tells apart by subtype / subtype 2,
 * keep a subtype with bit 7 here (the scarab's: bit 5, see there).
 */
#include "trilogy_obj.h"

int scd_mmz_back(void);                 /* scd_mmz.c: CD's prio_flag (player 1 on the back layer) */


#define O_PREV_ANIM_S3K 0x21            /* (prev_anim: what o_animate compares; CD's $1D) */
#define OBJ_routine2    0x25            /* (CD's oRoutine2, at its own offset) */
#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_OBJCTRL       0x2E            /* object_control: CD's oPlayerCtrl */
#define P_MOVELOCK      0x32            /* move_lock: CD's oPlayerMoveLock ($3E) */
#define P_INVULN        0x34            /* invulnerability_timer: CD's player $30 */
#define P_JUMPING       0x40            /* jumping: CD's player $3C */
#define ST_AIR          0x02
#define ST_ROLL         0x04
#define ST_PUSHING      0x20
#define FM_KABUTO       0xB1            /* (a_move: its hum, every 32 frames on screen) */
#define S3K_Obj_MonitorAnimate 0x01D61Eu   /* (a monitor broken: its last two states) */
#define S3K_Monitor_Broken     0x01D61Au

static int s_base;                      /* this round's native slots (scd_obj.c) */
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

static void set_yrad(uint32_t o, int yr)
{
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

/* DrawObject + CheckObjDespawn2 (x: where it was placed), touchable */
static void badnik_end(uint32_t o, uint16_t home)
{
    if (cd_despawn(o, home)) return;
    o_touch(o);
    o_draw(o);
}

static int32_t lval(uint32_t o, int f) { return (int32_t)r32(o + (uint32_t)f); }
static uint32_t ref(uint32_t o, int f) { return OB16(o, f) ? 0xFF0000u | OB16(o, f) : 0; }

/* ObjGetLWallDist as Sonic CD has it: S&K's left wall check flips the
 * probe's x within its block first (eori #$F, a fix Sonic 1's and CD's lack);
 * undone here, so the distance is CD's */
static int16_t cd_wall_left(uint32_t o, int d3)
{
    int x = OB16(o, OBJ_x_pos);
    return o_wall_left(o, (((x + d3) & 0xFFFF) ^ 0xF) - x);
}

/* ---- $24 Kabuto --------------------------------------------------------- */
#define KB_TIME   0x30                  /* oVar2A: to its turn */
#define KB_SPD    0x32                  /* oVar2C (long) */
#define KB_X0     0x36                  /* oVar30 */
#define KB_HUM    0x38                  /* oVar32: its hum, every 32 on screen */
#define KH_TIME   0x30                  /* oVar2A: (the horn) to its next pose */
#define KH_DX     0x32                  /* oVar2C */
#define KH_DY     0x34                  /* oVar2E */
#define KH_PARENT 0x44                  /* oVar3E: its body (word) */
#define KB_HORN   0x80                  /* (subtype: CD's subtype 2 $FF, the horn) */

/* the horn: on the body's front, up (anim 0) or lowered (1), a pose a second */
static void kabuto_horn(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, SCDA_KABUTOHORN, 3, 0x0C, 0x10, 0xAB);
    }
    uint32_t a1 = ref(o, KH_PARENT);
    if (!a1 || r32(a1) != CD_CODE(0x24) || (OB8(a1, OBJ_subtype) & KB_HORN)) { o_delete(o); return; }
    int set = OB8(o, OBJ_subtype) & 0x7F;                     /* (damaged: its first pose, always) */
    if (!set) {
        SET16(o, KH_TIME, OB16(o, KH_TIME) - 1);
        if (OBS16(o, KH_TIME) < 0) {
            SET16(o, KH_TIME, 60);
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
            set = 1;
        }
    }
    if (set) {
        int16_t dx = -25, dy = 0;
        if (OB8(o, OBJ_anim)) { dx = -19; dy = -15; }
        if (OB8(o, OBJ_render) & 1) dx = (int16_t)-dx;
        SET16(o, KH_DX, dx);
        SET16(o, KH_DY, dy);
    }
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) + OB16(o, KH_DX));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + OB16(o, KH_DY));
    o_animate(o, o_asset_anim(SCDA_KABUTOHORN));
    o_touch(o);
    o_draw(o);
}

static void kabuto(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & KB_HORN) { kabuto_horn(o); return; }
    if (cd_destroy_good_future(o)) return;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET16(o, KB_X0, OB16(o, OBJ_x_pos));
        badnik_init(o, SCDA_KABUTO, 4, 0x18, 0x0E, 0x2A);
        SET32(o, KB_SPD, (uint32_t)-0x5000);
        SET8(o, OBJ_routine, 2);
        uint32_t a1 = o_alloc_after(o);                       /* (FindNextObjSlot) its horn */
        if (!a1) { o_delete(o); return; }
        SET32(a1, OBJ_code, CD_CODE(0x24));
        SET8(a1, OBJ_subtype, KB_HORN | OB8(o, OBJ_subtype));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET16(a1, KH_PARENT, (uint16_t)o);
        /* (CD flips both here when the body's subtype 2 is set: none placed so) */
    }   /* fall through */
    case 2: {                                                 /* down to the floor */
        cd_move(o, 0, 0x10000);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    default:                                                  /* 128 px one way, 128 back */
        if (OB8(o, OBJ_render) & 0x80) {
            SET16(o, KB_HUM, OB16(o, KB_HUM) + 1);
            if (!(OB16(o, KB_HUM) & 0x1F)) o_sfx_cd(FM_KABUTO);
        }
        cd_move(o, lval(o, KB_SPD), 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        SET16(o, KB_TIME, OB16(o, KB_TIME) - 1);
        if (OBS16(o, KB_TIME) < 0) {                          /* (0 at first: it starts back right) */
            SET16(o, KB_TIME, 0x199);
            SET32(o, KB_SPD, (uint32_t)-lval(o, KB_SPD));
        }
        break;
    }
    badnik_end(o, OB16(o, KB_X0));
}

/* ---- $2E Hachi ---------------------------------------------------------- */
#define HB_SPD    0x30                  /* oVar2A (long) */
#define HB_TURN   0x34                  /* oVar2E: frames each way */
#define HB_TIME   0x36                  /* oVar30 */
#define HB_ANG    0x38                  /* oVar32: its wave */
#define HB_ASTEP  0x3A                  /* oVar34 */
#define HB_SHIFT  0x3C                  /* oVar36 */
#define HB_X0     0x3E                  /* oVar38 */
#define HB_Y0     0x40                  /* oVar3A */
#define HM_ANG    0x30                  /* oVar2A: (the mace) its swing, $4000 straight down */
#define HM_VEL    0x32                  /* oVar2C */
#define HM_ACC    0x34                  /* oVar2E */
#define HM_LINKS  0x36                  /* oVar30: its chain's links - 1 */
#define HM_SLOTS  0x38                  /* oVar32..: the links (slots, bytes) */
#define H_PARENT  0x44                  /* oVar3E: (mace, link) the body (word) */
#define HC_LINK   0x80                  /* (subtype: CD's subtype 2 1, a link) */
#define HC_MACE   0x81                  /* (CD's $FF: the mace on a 10-link chain) */
#define HC_MACE2  0x82                  /* (CD's $FE: on 4 links; the undamaged bee's second) */

static uint32_t hachi_body(uint32_t o)                        /* (its id still the bee's: CD's test) */
{
    uint32_t a1 = ref(o, H_PARENT);
    return a1 && r32(a1) == CD_CODE(0x2E) && !(OB8(a1, OBJ_subtype) & 0x80) ? a1 : 0;
}

/* a child (FindNextObjSlot): the bee's art and place */
static uint32_t hachi_child(uint32_t o, uint32_t body, uint8_t kind)
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET32(a1, OBJ_code, CD_CODE(0x2E));
    SET16(a1, H_PARENT, (uint16_t)body);
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
    SET8(a1, OBJ_subtype, kind);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    return a1;
}

static void hachi_link(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        radii(o, 8, 8);
        SET16(o, OBJ_priority, 3 << 7);
        SET8(o, OBJ_frame, 2);
    }
    if (!hachi_body(o)) { o_delete(o); return; }
    o_draw(o);
}

/* the mace: a pendulum (its pull turns each time it passes straight down)
 * hung 12 px under the bee, its links 8 px apart */
static void hachi_mace(uint32_t o)
{
    uint32_t body = hachi_body(o);
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        radii(o, 12, 12);
        SET16(o, OBJ_priority, 2 << 7);
        SET8(o, OBJ_frame, 3);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xAF));
        int lng = OB8(o, OBJ_subtype) == HC_MACE;
        SET16(o, HM_ANG, lng ? 0x800 : 0x1000);
        SET16(o, HM_ACC, 8);
        SET16(o, HM_LINKS, lng ? 9 : 3);
        for (int k = 0; k <= OBS16(o, HM_LINKS); k++) {
            uint32_t a1 = hachi_child(o, body, HC_LINK);
            if (!a1) {                                        /* (CD: the bee deleted) */
                if (body) o_delete(body);
                return;
            }
            SET8(o, HM_SLOTS + k, (uint8_t)o_slot_index(a1));
        }
    }
    if (!body) { o_delete(o); return; }
    SET16(o, HM_VEL, OB16(o, HM_VEL) + OB16(o, HM_ACC));
    int16_t old = OBS16(o, HM_ANG);
    SET16(o, HM_ANG, OB16(o, HM_ANG) + OB16(o, HM_VEL));
    if ((old < 0x4000) == (OBS16(o, HM_ANG) >= 0x4000))      /* (past straight down, either way) */
        SET16(o, HM_ACC, (uint16_t)-OBS16(o, HM_ACC));
    int32_t y = lval(body, OBJ_y_pos) + 0xC0000, x = lval(body, OBJ_x_pos);
    int16_t s, c;
    o_sincos((uint8_t)(OB16(o, HM_ANG) >> 8), &s, &c);
    int32_t dy = (int32_t)((uint32_t)(uint16_t)s << 16) >> 5, dx = (int32_t)((uint32_t)(uint16_t)c << 16) >> 5;
    for (int k = 0; k <= OBS16(o, HM_LINKS); k++) {
        y += dy;
        x += dx;
        uint32_t a1 = o_slot(OB8(o, HM_SLOTS + k));
        if (r32(a1) != CD_CODE(0x2E) || OB8(a1, OBJ_subtype) != HC_LINK) continue;
        SET32(a1, OBJ_y_pos, (uint32_t)y);
        SET32(a1, OBJ_x_pos, (uint32_t)x);
    }
    SET32(o, OBJ_y_pos, (uint32_t)(y + dy));
    SET32(o, OBJ_x_pos, (uint32_t)(x + dx));
    o_touch(o);
    o_draw(o);
}

static void hachi(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub & 0x80) { if (sub == HC_LINK) hachi_link(o); else hachi_mace(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = sub != 0, asset = dmg ? SCDA_HACHI2 : SCDA_HACHI;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x10, 0x10, 0x2E);
        SET16(o, HB_X0, OB16(o, OBJ_x_pos));
        SET16(o, HB_Y0, OB16(o, OBJ_y_pos));
        SET32(o, HB_SPD, dmg ? (uint32_t)-0x8000 : (uint32_t)-0x10000);
        SET16(o, HB_TURN, dmg ? 0x100 : 0x80);
        SET16(o, HB_ASTEP, dmg ? 0x300 : 0x400);
        SET16(o, HB_SHIFT, dmg ? 4 : 3);
        if (!hachi_child(o, o, HC_MACE) || (!dmg && !hachi_child(o, o, HC_MACE2))) { o_delete(o); return; }
    }
    cd_move(o, lval(o, HB_SPD), 0);                           /* (the time 0 at first: it starts back right) */
    SET16(o, HB_TIME, OB16(o, HB_TIME) - 1);
    if (OBS16(o, HB_TIME) < 0) {
        SET16(o, HB_TIME, OB16(o, HB_TURN));
        SET32(o, HB_SPD, (uint32_t)-lval(o, HB_SPD));
        turn(o);
    }
    SET16(o, OBJ_y_pos, OB16(o, HB_Y0));                      /* (its height: a sine about where it was) */
    SET16(o, OBJ_y_pos + 2, 0);
    SET16(o, HB_ANG, OB16(o, HB_ANG) + OB16(o, HB_ASTEP));
    int16_t s, c;
    o_sincos((uint8_t)(OB16(o, HB_ANG) >> 8), &s, &c);
    SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)((int32_t)((uint32_t)(uint16_t)s << 16) >> OB16(o, HB_SHIFT)));
    o_animate(o, o_asset_anim(asset));
    badnik_end(o, OB16(o, HB_X0));
}

/* ---- $2F Scarab --------------------------------------------------------- */
/* Placed: the controller (CD's subtype bit 7; bit 4 the scarab faces right,
 * bit 0 damaged; subtype 2 the monitor's type 0-7, other values none). The
 * converter carries subtype 2 as bit 6 (a monitor) + bits 1-3 (its type). The
 * monitor is S3K's own, as Sonic CD's others are here; the controller moves it
 * with the scarab. Children: the scarab (subtype: damaged bit; CD's subtype 0 /
 * 1), its shell (bit 5; CD's subtype 2 1). The touch, CD's special $F0: from
 * behind the scarab is a badnik (S3K's touch), from its front it counts
 * (o_s1_touch, CD's box $30) and grabs once the monitor is gone. */
#define CT_ENEMY  0x34                  /* oVar2E: the scarab (word) */
#define CT_ITEM   0x36                  /* oVar30: the monitor (word) */
#define CT_OWN    0x38                  /* (its flags where it is not remembered) */
#define CT_HELD   0x39                  /* (the scarab holds player 1: let go if it goes) */
#define SC_CTRL   0x30                  /* oVar2A: (the scarab) its controller */
#define SC_X0     0x32                  /* oVar2C */
#define SC_SPD    0x34                  /* oVar2E (long) */
#define SC_HELD   0x38                  /* oVar32: the player held (word) */
#define SC_TIME   0x3A                  /* oVar34: walking 61, then still 30 */
#define SC_HOLD   0x3C                  /* oVar36 */
#define SC_COLL   0x3E                  /* (CD's oColType: $F0 or 0) */
#define SC_TOUCH  0x3F                  /* (CD's oColStatus: touched in front) */
#define SC_DIR    0x40                  /* (CD's subtype 2: $FF facing right) */
#define SC_SHELL  0x42                  /* oVar3C */
#define SH_PARENT 0x30                  /* oVar2A: (the shell) the scarab */
#define SC_IS_CTRL 0x80
#define SC_IS_SHELL 0x20
enum { MON_GONE, MON_HERE, MON_BROKEN };

static const uint8_t cd_monitor[8] = { 1, 3, 7, 8, 4, 3, 8, 8 };   /* (scd_level.py CD_MONITOR) */

/* its flags, as CD keeps them in the respawn table (bit 0 the scarab
 * destroyed, bit 1 the monitor taken): bits S3K's entry leaves free */
static uint32_t ct_flags(uint32_t o)
{
    return OB16(o, OBJ_respawn) ? 0xFF0000u | OB16(o, OBJ_respawn) : o + CT_OWN;
}

static int mon_state(uint32_t m)
{
    uint32_t c = m ? r32(m) : 0;
    return c == S3K_Obj_Monitor ? MON_HERE : c == S3K_Obj_MonitorAnimate || c == S3K_Monitor_Broken ? MON_BROKEN : MON_GONE;
}

static uint32_t sc_enemy_of(uint32_t ct)
{
    uint32_t a1 = ref(ct, CT_ENEMY);
    return a1 && r32(a1) == CD_CODE(0x2F) && !(OB8(a1, OBJ_subtype) & (SC_IS_CTRL | SC_IS_SHELL)) &&
           OB16(a1, SC_CTRL) == (uint16_t)ct ? a1 : 0;
}

static uint32_t sc_ctrl_of(uint32_t o)
{
    uint32_t a1 = ref(o, SC_CTRL);
    return a1 && r32(a1) == CD_CODE(0x2F) && (OB8(a1, OBJ_subtype) & SC_IS_CTRL) ? a1 : 0;
}

static void sc_let_go(uint32_t ct)                            /* (CD: bclr #0 of his oPlayerCtrl) */
{
    if (!OB8(ct, CT_HELD)) return;
    SET8(ct, CT_HELD, 0);
    SET8(RAM_Player_1, P_OBJCTRL, OB8(RAM_Player_1, P_OBJCTRL) & ~0x81u);
}

/* c_make: the scarab and the monitor, unless they are gone */
static void sc_make(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    uint32_t fl = ct_flags(o);
    if (!(r8(fl) & 1)) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) { o_delete_respawnable(o); return; }
        SET32(a1, OBJ_code, CD_CODE(0x2F));
        SET16(a1, SC_CTRL, (uint16_t)o);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(a1, OBJ_subtype, sub & 1);
        SET8(a1, SC_DIR, sub & 0x10 ? 0xFF : 0);
        SET16(o, CT_ENEMY, (uint16_t)a1);
    }
    if (r8(fl) & 2) return;
    if (!(sub & 0x40)) { w8(fl, r8(fl) | 2); return; }        /* (no monitor) */
    uint32_t a1 = o_alloc_after(o);
    if (!a1) { o_delete_respawnable(o); return; }
    SET32(a1, OBJ_code, S3K_Obj_Monitor);
    SET8(a1, OBJ_subtype, cd_monitor[(sub >> 1) & 7]);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + (sub & 0x10 ? 24 : -24));   /* (before it) */
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
    SET8(a1, OBJ_y_radius, 0x0E);                             /* (CD's item: on the floor, its radius $E) */
    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + o_floor_dist(a1));
    SET16(o, CT_ITEM, (uint16_t)a1);
}

/* c_move: what is gone (the scarab destroyed, the monitor taken) stays gone */
static void sc_watch(uint32_t o)
{
    uint32_t fl = ct_flags(o);
    uint32_t it = ref(o, CT_ITEM);
    if (!(r8(fl) & 1) && OB16(o, CT_ENEMY) && !sc_enemy_of(o)) {
        w8(fl, r8(fl) | 1);
        SET16(o, CT_ENEMY, 0);
        if (mon_state(it) == MON_HERE) SET16(it, OBJ_x_vel, 0);
        sc_let_go(o);
    }
    if (it && mon_state(it) != MON_HERE) {
        SET16(o, CT_ITEM, 0);
        if (mon_state(it) == MON_BROKEN && !(r8(fl) & 2)) {  /* (taken: CD's itemget, its scarab touched) */
            w8(fl, r8(fl) | 2);
            uint32_t en = sc_enemy_of(o);
            if (en) SET8(en, SC_TOUCH, 1);
        }                                                     /* (else gone out of range: comes back) */
    }
    int f = r8(fl) & 3;
    if (f == 3) { sc_let_go(o); o_delete(o); return; }        /* (both gone: for good) */
    if (!f && !OB16(o, CT_ENEMY) != !OB16(o, CT_ITEM)) {      /* (one out of range: the other too) */
        if (OB16(o, CT_ENEMY)) {
            sc_let_go(o);
            uint32_t en = sc_enemy_of(o);
            if (en) o_delete(en);
            SET16(o, CT_ENEMY, 0);
        } else {
            if (mon_state(ref(o, CT_ITEM)) == MON_HERE) o_delete(ref(o, CT_ITEM));
            SET16(o, CT_ITEM, 0);
        }
    }
    if (!OB16(o, CT_ENEMY) && !OB16(o, CT_ITEM)) { sc_let_go(o); o_delete_respawnable(o); }
}

static void sc_controller(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                                   /* waiting for player 1, 320 px about */
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x140) >= 0x280 ||
            (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x140) >= 0x280) {
            cd_despawn(o, OB16(o, OBJ_x_pos));
            return;
        }
        SET8(o, OBJ_routine, 4);
        sc_make(o);
        return;
    default:
        sc_watch(o);
        return;
    }
}

static void sc_turn(uint32_t o)                               /* (it never turns round: it backs) */
{
    cd_move(o, -lval(o, SC_SPD), 0);
    SET32(o, SC_SPD, (uint32_t)-lval(o, SC_SPD));
}

static void sc_walk(uint32_t o, int asset)                    /* e1_chk_patchg: legs 61 frames, still 30 */
{
    SET16(o, SC_TIME, OB16(o, SC_TIME) - 1);
    if (OBS16(o, SC_TIME) >= 0)
        o_animate(o, o_asset_anim(asset));
    else if (OBS16(o, SC_TIME) <= -30)
        SET16(o, SC_TIME, 60);
}

static void sc_catch(uint32_t o, uint32_t ct, uint32_t p)
{
    SET16(o, SC_HELD, (uint16_t)p);
    SET8(ct, CT_HELD, 1);
    /* CD's oPlayerCtrl bit 0: held, and no object touches him (its
     * Player_ObjCollide skips him): S3K's bits 0 and 7 */
    SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 0x81);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
    SET8(p, P_ANIM, 2);
    SET8(p, P_YRAD, 14);
    SET8(p, P_XRAD, 7);
    SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - 3);
    SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + (OB8(o, SC_DIR) ? 24 : -24));
    SET8(o, OBJ_routine2, 2);
    SET16(o, SC_HOLD, 120);
    SET8(o, SC_COLL, 0xF0);
    SET8(o, SC_TOUCH, 0);
}

static void sc_throw(uint32_t o, uint32_t ct, int dmg)        /* (off the way it faces, rolling) */
{
    uint32_t p = ref(o, SC_HELD);
    SET16(o, SC_HELD, 0);
    if (ct) SET8(ct, CT_HELD, 0);
    if (p) {
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~0x81u);
        SET16(p, OBJ_y_vel, 0);
        int16_t v = dmg ? -0x500 : -0xA00;
        uint8_t st = OB8(p, OBJ_status) | 1;
        if (OB8(o, SC_DIR)) { v = (int16_t)-v; st &= ~1u; }
        SET16(p, OBJ_x_vel, (uint16_t)v);
        SET16(p, P_MOVELOCK, 15);
        SET16(p, OBJ_ground_vel, (uint16_t)v);
        SET8(p, OBJ_angle, 0);
        SET8(p, OBJ_status, st & ~ST_PUSHING);
    }
    SET8(o, OBJ_routine2, 4);
    SET8(o, SC_COLL, 0);
    SET8(o, SC_TOUCH, 0);
    SET8(o, OBJ_frame, 2);
    SET16(o, SC_HOLD, 30);
}

/* e_move: it walks 64 px either side, the monitor and a held player go with it */
static void sc_move(uint32_t o, uint32_t ct, int dmg)
{
    int asset = dmg ? SCDA_SCARAB2 : SCDA_SCARAB;
    uint32_t p = RAM_Player_1;
    int16_t x0 = OBS16(o, OBJ_x_pos), y0 = OBS16(o, OBJ_y_pos);
    if (OBS16(o, SC_TIME) >= 0) {
        cd_move(o, lval(o, SC_SPD), 0);
        int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, SC_X0));
        if (d0 < 0) d0 = (int16_t)-d0;
        if (d0 >= 0x40) sc_turn(o);
        int16_t d1 = o_floor_dist(o);
        if ((uint16_t)(d1 + 7) >= 14) sc_turn(o);             /* (a step: back) */
        else SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
    }
    int16_t dx = (int16_t)(x0 - OBS16(o, OBJ_x_pos)), dy = (int16_t)(y0 - OBS16(o, OBJ_y_pos));
    uint32_t it = ref(ct, CT_ITEM);
    if (mon_state(it) == MON_HERE) {
        SET16(it, OBJ_x_pos, OB16(it, OBJ_x_pos) - dx);
        SET16(it, OBJ_y_pos, OB16(it, OBJ_y_pos) - dy);
        SET16(it, OBJ_x_vel, (uint16_t)((uint32_t)-dx << 8));
    }
    uint32_t held = ref(o, SC_HELD);
    if (held) {
        SET16(held, OBJ_x_pos, OB16(held, OBJ_x_pos) - dx);
        SET16(held, OBJ_y_pos, OB16(held, OBJ_y_pos) - dy);
    }
    switch (OB8(o, OBJ_routine2)) {
    case 0:                                                   /* e1_check */
        if (OB8(o, SC_TOUCH)) {
            if (OB8(p, P_ROUTINE) == 4 || OB8(p, P_ROUTINE) == 6 || OB8(p, P_INVULN) || OB16(ct, CT_ITEM))
                SET8(o, SC_TOUCH, 0);                         /* (hurt, dead, flashing; the monitor still there) */
            else {
                sc_catch(o, ct, p);
                break;
            }
        }
        sc_walk(o, asset);
        break;
    case 2:                                                   /* e1_keep: held 2 s */
        SET16(o, SC_HOLD, OB16(o, SC_HOLD) - 1);
        if (OB16(o, SC_HOLD)) {
            SET8(o, SC_COLL, 0xF0);
            SET8(o, SC_TOUCH, 0);
            sc_walk(o, asset);
        } else
            sc_throw(o, ct, dmg);
        break;
    default:                                                  /* e1_wait: half a second, untouchable */
        SET16(o, SC_HOLD, OB16(o, SC_HOLD) - 1);
        if (OB16(o, SC_HOLD)) break;
        SET8(o, OBJ_routine2, 0);
        SET8(o, O_PREV_ANIM_S3K, 0xFF);                       /* (its script restarts) */
        SET8(o, SC_COLL, 0xF0);
        SET8(o, SC_TOUCH, 0);
        break;
    }
    uint32_t sh = ref(o, SC_SHELL);
    if (sh && r32(sh) == CD_CODE(0x2F) && (OB8(sh, OBJ_subtype) & SC_IS_SHELL) && OB16(sh, SH_PARENT) == (uint16_t)o) {
        SET16(sh, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(sh, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(sh, OBJ_frame, OB8(o, OBJ_frame) + 3);
    }
}

/* CD's touch $F0 (Player_ObjCollide's special $30): player 1 behind it
 * (as it faces) meets a badnik; in front, it counts. Not while held. */
static void sc_touch(uint32_t o)
{
    SET8(o, OBJ_coll_flags, 0);
    if (!OB8(o, SC_COLL)) return;
    uint32_t p = RAM_Player_1;
    uint8_t left = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos)) < 0 ? 0xFF : 0;
    if (!(OB8(o, SC_DIR) ^ left)) { SET8(o, OBJ_coll_flags, tri_scd_coll(0x30)); return; }
    if (!(OB8(p, P_OBJCTRL) & 1) && (o_s1_touch(o, 24, 11) & 1)) SET8(o, SC_TOUCH, OB8(o, SC_TOUCH) + 1);
}

static void sc_shell(uint32_t o)
{
    uint32_t a1 = ref(o, SH_PARENT);
    if (!a1 || r32(a1) != CD_CODE(0x2F) || (OB8(a1, OBJ_subtype) & (SC_IS_CTRL | SC_IS_SHELL))) { o_delete(o); return; }
    if (OB8(o, OBJ_routine)) { o_draw(o); return; }
    SET8(o, OBJ_routine, 2);                                  /* (unseen its first frame) */
    SET16(o, OBJ_priority, 3 << 7);
}

static void scarab(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub & SC_IS_CTRL) { sc_controller(o); return; }
    if (sub & SC_IS_SHELL) { sc_shell(o); return; }
    uint32_t ct = sc_ctrl_of(o);
    if (!ct) {
        if (OB16(o, SC_HELD)) SET8(RAM_Player_1, P_OBJCTRL, OB8(RAM_Player_1, P_OBJCTRL) & ~0x81u);   /* (S3K: never leave him held) */
        o_delete(o);
        return;
    }
    if (cd_destroy_good_future(o)) return;                    /* (a held player: its controller lets go) */
    int dmg = sub & 1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, dmg ? SCDA_SCARAB2 : SCDA_SCARAB, 1, 0x20, 0x0B, 0);
        SET8(o, SC_COLL, 0xF0);
        SET16(o, SC_X0, OB16(o, OBJ_x_pos));
        SET16(o, SC_TIME, 60);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        uint32_t a1 = o_alloc_after(o);                       /* (FindNextObjSlot) its shell */
        if (!a1) { o_delete(o); return; }
        SET32(a1, OBJ_code, CD_CODE(0x2F));
        SET8(a1, OBJ_subtype, SC_IS_SHELL);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(a1, OBJ_render, OB8(o, OBJ_render));
        radii(a1, 0x20, 0x0B);
        SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
        SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
        SET16(o, SC_SHELL, (uint16_t)a1);
        SET16(a1, SH_PARENT, (uint16_t)o);
        if (OB8(o, SC_DIR)) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
            SET8(a1, OBJ_status, OB8(a1, OBJ_status) | 1);
        }
        SET32(o, SC_SPD, dmg ? 0x3000 : 0x6000);
    }
    sc_move(o, ct, dmg);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        SET16(ct, CT_ENEMY, 0);
        sc_let_go(ct);                                        /* (S3K: never leave him held) */
        return;
    }
    sc_touch(o);
    o_touch(o);
    o_draw(o);
}

/* ---- $2D Dango ---------------------------------------------------------- */
#define DG_SPD    0x30                  /* oVar2A (long) */
#define DG_COLL   0x34                  /* (CD's oColType: $2C, or $ED curled) */
#define DG_HIT    0x35                  /* (CD's oColStatus: a curled player touched it) */

/* CD's touch $ED (Player_ObjCollide's special $2D): a player not curled
 * meets a badnik; a curled one, it counts (o_s1_touch, CD's box $2D) */
static void dg_touch(uint32_t o)
{
    uint8_t c = OB8(o, DG_COLL);
    if (c != 0xED) { SET8(o, OBJ_coll_flags, c ? tri_scd_coll(c) : 0); return; }
    uint32_t p = RAM_Player_1;
    if (!(OB8(p, OBJ_status) & ST_ROLL)) { SET8(o, OBJ_coll_flags, tri_scd_coll(0x2D)); return; }
    SET8(o, OBJ_coll_flags, 0);
    if (!(OB8(p, P_OBJCTRL) & 1) && (o_s1_touch(o, 16, 16) & 1)) SET8(o, DG_HIT, OB8(o, DG_HIT) + 1);
}

static void dg_speed(uint32_t o, int32_t v, int32_t dmgv)     /* (the way it faces) */
{
    if (OB8(o, OBJ_subtype)) v = dmgv;
    if (!(OB8(o, OBJ_render) & 1)) v = -v;
    SET32(o, DG_SPD, (uint32_t)v);
}

/* along the floor; 0: a wall or a step (and it stays) */
static int dg_move(uint32_t o)
{
    int32_t v = lval(o, DG_SPD);
    cd_move(o, v, 0);
    int d3 = OB8(o, OBJ_x_radius);
    int16_t d1 = OBS16(o, DG_SPD) < 0 ? cd_wall_left(o, -d3) : o_wall_right(o, d3);
    if (d1 >= 0) {
        d1 = o_floor_dist(o);
        if ((uint16_t)(d1 + 7) < 14) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            return 1;
        }
    }
    cd_move(o, -v, 0);
    return 0;
}

static void dango(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_DANGO2 : SCDA_DANGO;
    uint32_t anim = o_asset_anim(asset), p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 0x0E, 9, 0);
        SET8(o, DG_COLL, 0x2C);
        break;
    case 2: {                                                 /* down to the floor */
        cd_move(o, 0, 0x10000);
        int16_t d1 = o_floor_dist(o);
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
    case 6: {                                                 /* walking: the player 128 px ahead of it? */
        if (!dg_move(o)) {
            SET32(o, DG_SPD, (uint32_t)-lval(o, DG_SPD));
            turn(o);
            break;
        }
        int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        uint8_t right = dx >= 0 ? 0xFF : 0, face = OB8(o, OBJ_render) & 1 ? 0xFF : 0;
        if ((uint16_t)(dx + 0x80) < 0x100 && (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x80) < 0x100 &&
            !(face ^ right))
            SET8(o, OBJ_routine, 8);
        o_animate(o, anim);
        break;
    }
    case 8:                                                   /* stop (its script: $FC) */
        SET8(o, OBJ_routine, 0x0A);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        break;
    case 0x0C:                                                /* curl (its script: $FC) */
        SET8(o, OBJ_routine, 0x0E);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 6);
        set_yrad(o, 0x10);
        break;
    case 0x10:                                                /* curled: it spins up on the spot */
        SET8(o, OBJ_routine, 0x12);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 3);
        set_yrad(o, 0x0D);
        SET8(o, DG_COLL, 0xED);
        SET32(o, DG_SPD, 0);
        break;
    case 0x12: case 0x16:                                     /* (12: still, its script ends $FC; 16: rolling) */
        if (OB8(o, DG_HIT)) {                                 /* a curled player: bounced off at $700, it rolls back */
            int16_t s, c;
            o_sincos(o_atan((int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)),
                            (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos))), &s, &c);
            int16_t vx = (int16_t)(((int32_t)c * -0x700) >> 8), vy = (int16_t)(((int32_t)s * -0x700) >> 8);
            SET16(p, OBJ_x_vel, (uint16_t)vx);
            SET16(p, OBJ_y_vel, (uint16_t)vy);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~0x30u);
            SET8(p, P_JUMPING, 0);
            SET8(o, DG_HIT, 0);
            SET32(o, DG_SPD, (uint32_t)((int32_t)(int16_t)-vx * 256));
            SET8(o, OBJ_anim, 4);
            SET8(o, OBJ_routine, 0x16);
            break;
        }
        if (dg_move(o)) o_animate(o, anim);
        else SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);   /* (blocked: uncurls) */
        break;
    case 0x14:                                                /* off: $40000 (damaged $10000) */
        SET8(o, OBJ_routine, 0x16);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        dg_speed(o, 0x40000, 0x10000);
        break;
    case 0x18:                                                /* uncurl (its script: $FC) */
        SET8(o, OBJ_routine, 0x1A);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 3);
        set_yrad(o, 0x10);
        SET8(o, DG_COLL, 0x2C);
        break;
    case 0x1C:                                                /* walking again */
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_anim, 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 6);
        set_yrad(o, 9);
        break;
    default:                                                  /* (0A, 0E, 1A: its script runs) */
        o_animate(o, anim);
        break;
    }
    dg_touch(o);
    badnik_end(o, OB16(o, OBJ_x_pos));
}

/* ---- $23 Bigbom --------------------------------------------------------- */
/* Placed: the controller, unseen (CD's subtype 0; subtype 2 kept by the
 * converter in bits 0-1: 0 walks 128 px, 1 paces 16 px, 2 waits on the front
 * layer for the player to pass, then follows him). It makes the bomb, then its
 * fuse, blast and fires; they keep CD's subtype in bits 1-3 + bit 7, subtype 2
 * in bits 4-5. A bomb with subtype 2 2 touches only while player 1 is on the
 * front layer (prio_flag clear); its controller never despawns. */
#define BB_TIME   0x30                  /* oVar2A */
#define BB_X0     0x32                  /* oVar2C: (the controller) where it was placed */
#define BB_BODY   0x42                  /* oVar3C: (the controller) the bomb */
#define BB_SPD    0x32                  /* oVar2C (long): (the bomb) */
#define BB_FTIME  0x36                  /* oVar30: (the bomb) to its next step */
#define BB_CUR    0x40                  /* (CD's oColType) */
#define BB_COLL   0x43                  /* oVar3D: its touch, while it has one */
#define BB_PARENT 0x44                  /* oVar3E: the controller */
#define BF_VX     0x30                  /* oVar2A (long): (a fire) */
#define BF_VY     0x34                  /* oVar2E (long) */
#define BF_AX     0x38                  /* oVar32 (long) */
#define BF_AY     0x3C                  /* oVar36 (long) */
#define BB_CHILD  0x80
#define BB_IS_BODY 2
#define BB_IS_FUSE 4
#define BB_IS_BOOM 6
#define BB_IS_FIRE 8

static int bb_s2(uint32_t o)
{
    uint8_t s = OB8(o, OBJ_subtype);
    return s & BB_CHILD ? (s >> 4) & 3 : s & 3;
}

static uint32_t bb_controller(uint32_t o)                     /* (its id still the bomb's: CD's test) */
{
    uint32_t a1 = ref(o, BB_PARENT);
    return a1 && r32(a1) == CD_CODE(0x23) && !(OB8(a1, OBJ_subtype) & BB_CHILD) ? a1 : 0;
}

static uint32_t bb_body_of(uint32_t o)
{
    uint32_t a1 = ref(o, BB_BODY);
    return a1 && r32(a1) == CD_CODE(0x23) && (OB8(a1, OBJ_subtype) & 0x8E) == (BB_CHILD | BB_IS_BODY) &&
           OB16(a1, BB_PARENT) == (uint16_t)o ? a1 : 0;
}

static uint32_t bb_child(uint32_t o, int kind, int s2)        /* (FindObjSlot: the first free) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, CD_CODE(0x23));
    SET8(a1, OBJ_subtype, BB_CHILD | kind | (s2 << 4));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, BB_PARENT, (uint16_t)o);
    return a1;
}

static void bb_layer(uint32_t o)                              /* (prio_flag: behind, it is not touched) */
{
    SET8(o, BB_CUR, scd_mmz_back() ? 0 : OB8(o, BB_COLL));
}

static int bb_near(uint32_t o)                                /* player 1 within 96 px across, -160..79 down */
{
    uint32_t p = RAM_Player_1;
    return (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0xA0) < 0xF0 &&
           (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x60) < 0xC0;
}

static void bb_step(uint32_t o)                               /* (its legs: 4 frames, by its table) */
{
    static const uint8_t time[4] = { 25, 15, 25, 18 };
    SET16(o, BB_FTIME, OB16(o, BB_FTIME) - 1);
    if (OBS16(o, BB_FTIME) >= 0) return;
    SET8(o, OBJ_frame, (OB8(o, OBJ_frame) + 1) & 3);
    SET16(o, BB_FTIME, time[OB8(o, OBJ_frame)]);
}

static void bb_stop(uint32_t o)                               /* s0_stop: it tells its controller */
{
    SET8(o, OBJ_routine, 0x12);
    uint32_t pa = bb_controller(o);
    if (pa) SET8(pa, OBJ_routine, OB8(pa, OBJ_routine) + 2);
    if (bb_s2(o) == 2) bb_layer(o);
}

static void bb_body(uint32_t o)
{
    int s2 = bb_s2(o);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, SCDA_BIGBOM, OB8(o, OBJ_render) | 4, 1, 0x14, 0x1A);
        SET8(o, OBJ_y_radius, 0x1A);
        SET8(o, BB_COLL, 0xA6);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* down to the floor */
        cd_move(o, 0, 0x10000);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        if (s2 != 2) {
            SET8(o, OBJ_routine, 0x0C);
            SET8(o, BB_CUR, 0xA6);
        } else {
            SET8(o, OBJ_routine, 4);
            turn(o);
        }
        break;
    }
    case 4: {                                                 /* waits for player 1 to pass it */
        if (!bb_near(o) || (int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos)) < 0) break;
    }   /* fall through */
    case 6:                                                   /* then after him, 2.5 px a frame, 231 frames */
        SET8(o, OBJ_routine, 8);
        SET32(o, BB_SPD, 0x28000);
        SET16(o, BB_TIME, 0xE6);
        SET16(o, BB_FTIME, 0x12);
        SET8(o, OBJ_frame, 3);
        /* fall through */
    case 8:
        bb_layer(o);
        cd_move(o, lval(o, BB_SPD), 0);
        bb_step(o);
        SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
        if (OBS16(o, BB_TIME) < 0) SET8(o, OBJ_routine, 0x0A);
        break;
    case 0x0A:                                                /* still; the player near, on its layer: lit */
        bb_layer(o);
        if (!scd_mmz_back() && bb_near(o)) SET8(o, OBJ_routine, 0x10);
        break;
    case 0x0C:
        SET8(o, OBJ_routine, 0x0E);
        SET32(o, BB_SPD, (uint32_t)-0xA000);
        SET16(o, BB_TIME, 0);
        SET16(o, BB_FTIME, 0);
        SET8(o, OBJ_frame, 3);
        /* fall through */
    case 0x0E:                                                /* walking (0 at first: it turns at once) */
        cd_move(o, lval(o, BB_SPD), 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
        if (OBS16(o, BB_TIME) < 0) {
            SET32(o, BB_SPD, (uint32_t)-lval(o, BB_SPD));
            turn(o);
            SET16(o, BB_TIME, s2 ? 0x19 : 0xCC);
        }
        bb_step(o);
        if (!OB8(o, OBJ_frame) && bb_near(o)) bb_stop(o);     /* (on its first step, the player near) */
        break;
    case 0x10:
        bb_stop(o);
        break;
    case 0x14:                                                /* (its controller: the fuse is lit) */
        SET8(o, OBJ_frame, 4);
        SET8(o, OBJ_routine, 0x16);
        /* fall through */
    case 0x12: case 0x16:
        if (s2 == 2) bb_layer(o);
        break;
    default:                                                  /* (its controller: blown up) */
        o_delete(o);
        return;
    }
    uint32_t pa = bb_controller(o);
    if (!pa) { o_delete(o); return; }
    SET16(pa, OBJ_x_pos, OB16(o, OBJ_x_pos));                 /* (its controller goes with it) */
    SET16(pa, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(o, OBJ_coll_flags, OB8(o, BB_CUR) ? tri_scd_coll(OB8(o, BB_CUR)) : 0);
    o_touch(o);
    o_draw(o);
}

static void bb_fuse(uint32_t o)                               /* a second lit, then it burns down 24 px */
{
    uint32_t anim = o_asset_anim(SCDA_BIGBOMFUSE);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BIGBOMFUSE, OB8(o, OBJ_render) | 4, 2, 4, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET16(o, BB_TIME, 60);
        /* fall through */
    case 2:
        SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
        if (!OB16(o, BB_TIME)) {
            SET16(o, BB_TIME, 0x4C);
            SET8(o, OBJ_routine, 4);
        }
        o_animate(o, anim);
        break;
    case 4:
        cd_move(o, 0, 0x5000);
        SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
        if (OBS16(o, BB_TIME) < 0) SET8(o, OBJ_routine, 6);
        o_animate(o, anim);
        break;
    default: {                                                /* burnt: the controller blows the bomb up */
        uint32_t pa = bb_controller(o);
        if (pa) SET8(pa, OBJ_routine, OB8(pa, OBJ_routine) + 2);
        o_delete(o);
        return;
    }
    }
    o_draw(o);
}

static void bb_boom(uint32_t o)                               /* the blast: 40 frames (CD: no sound) */
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BIGBOMBOOM, OB8(o, OBJ_render) | 4, 1, 24, 24);   /* (CD gives it no size: S3K's culling needs one) */
        SET16(o, BB_TIME, 40);
    }
    SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
    if (!OB16(o, BB_TIME)) { o_delete(o); return; }
    o_animate(o, o_asset_anim(SCDA_BIGBOMBOOM));
    o_draw(o);
}

static void bb_fire(uint32_t o)                               /* a fire: thrown up, falls till 224 px under player 1 */
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BIGBOMFIRE, OB8(o, OBJ_render) | 4, 1, 8, 8);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, BB_CUR, 0xA7);
        SET8(o, BB_COLL, 0xA7);
    }
    if (bb_s2(o) == 2) bb_layer(o);
    cd_move(o, lval(o, BF_VX), lval(o, BF_VY));
    SET32(o, BF_VX, r32(o + BF_VX) + r32(o + BF_AX));
    SET32(o, BF_VY, r32(o + BF_VY) + r32(o + BF_AY));
    if ((int16_t)(OB16(o, OBJ_y_pos) - OB16(RAM_Player_1, OBJ_y_pos)) >= 0xE0) { o_delete(o); return; }
    o_animate(o, o_asset_anim(SCDA_BIGBOMFIRE));
    SET8(o, OBJ_coll_flags, OB8(o, BB_CUR) ? tri_scd_coll(OB8(o, BB_CUR)) : 0);
    o_touch(o);
    o_draw(o);
}

static void bigbom(uint32_t o)
{
    static const int32_t fires[7][2] = {                      /* x, y speeds */
        { -0x10000, -0x42000 }, { -0x20000, -0x52000 }, { -0x30000, -0x62000 },
        { 0x10000, -0x42000 },  { 0x20000, -0x52000 },  { 0x30000, -0x62000 }, { 0, -0x32000 },
    };
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub & BB_CHILD) {
        switch (sub & 0x0E) {
        case BB_IS_BODY: bb_body(o); return;
        case BB_IS_FUSE: bb_fuse(o); return;
        case BB_IS_BOOM: bb_boom(o); return;
        default: bb_fire(o); return;
        }
    }
    int s2 = sub & 3;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET16(o, BB_X0, OB16(o, OBJ_x_pos));
        uint32_t a1 = bb_child(o, BB_IS_BODY, s2);
        if (!a1) { o_delete_respawnable(o); return; }
        SET16(o, BB_BODY, (uint16_t)a1);
        SET8(o, OBJ_routine, 2);
        break;
    }
    case 2: case 8:                                           /* (the bomb stops it; the fuse burns down) */
        break;
    case 4:
        SET16(o, BB_TIME, 30);
        SET8(o, OBJ_routine, 6);
        /* fall through */
    case 6: {                                                 /* half a second, then the fuse is lit */
        SET16(o, BB_TIME, OB16(o, BB_TIME) - 1);
        if (OB16(o, BB_TIME)) break;
        uint32_t body = bb_body_of(o);
        if (body) SET8(body, OBJ_routine, OB8(body, OBJ_routine) + 2);
        uint32_t a1 = bb_child(o, BB_IS_FUSE, 0);
        if (!a1) { o_delete(o); return; }
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 31);
        if (body && (OB8(body, OBJ_render) & 1)) turn(a1);
        SET8(o, OBJ_routine, 8);
        break;
    }
    case 0x0A:
        SET16(o, BB_TIME, 0);
        SET8(o, OBJ_routine, 0x0C);
        /* fall through */
    default: {                                                /* the blast (30), the fires (31), the bomb gone (32) */
        SET16(o, BB_TIME, OB16(o, BB_TIME) + 1);
        uint16_t t = OB16(o, BB_TIME);
        if (t == 0x20) {
            uint32_t body = bb_body_of(o);
            if (body) SET8(body, OBJ_routine, OB8(body, OBJ_routine) + 2);
        }
        if (t == 0x1E && !bb_child(o, BB_IS_BOOM, 0)) { o_delete(o); return; }
        if (t == 0x1F)
            for (int k = 0; k < 7; k++) {
                uint32_t a1 = bb_child(o, BB_IS_FIRE, s2);
                if (!a1) { o_delete_respawnable(o); return; }
                SET32(a1, BF_AX, 0);
                SET32(a1, BF_AY, 0x2000);
                SET32(a1, BF_VX, (uint32_t)fires[k][0]);
                SET32(a1, BF_VY, (uint32_t)fires[k][1]);
            }
        if (t == 0x28) { o_delete(o); return; }               /* (gone for good) */
        break;
    }
    }
    if (s2 != 2) cd_despawn(o, OB16(o, BB_X0));
}

void scd_mmz_badnik_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x23] = bigbom;
    t[base + 0x24] = kabuto;
    t[base + 0x2D] = dango;
    t[base + 0x2E] = hachi;
    t[base + 0x2F] = scarab;
}
