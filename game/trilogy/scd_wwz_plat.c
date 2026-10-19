/*
 * scd_wwz_plat.c — Wacky Workbench's platforms (Sonic CD; dobby: Level/Wacky
 * Workbench/Objects/Platform, Seesaw; decomp: R6\DAIR6.C, TOBIDAI6.C,
 * KDAI6.C, SEESAW6.C).
 *
 *   $22 platform           slides to and fro (subtypes 0 right, 1 left) or up
 *                          and down (2 up, 3 down), 64 px each way on one sine
 *                          of the level's frame counter: all of them in step
 *   $23 bouncing platform  hops on the floor at -$600 while the zone's floor
 *                          palette cycle (the fourth) is off its dark step (90
 *                          in the present, 60 in the past; futures always);
 *                          a player on it rises with it, so a jump off it goes
 *                          higher
 *   $28 spinning platform  moves as $22; player 1 on it is spun round its
 *                          middle (Sonic CD's "Spinning Top" pictures,
 *                          scd_wwz_spin_frame), left/right winds him out or
 *                          in, a jump lets go
 *   $2C seesaw             two sides; standing on the high side tips it (the
 *                          low side comes up 96 px), two seconds after that it
 *                          slides down until the low side is on the floor
 * The code of these four is the same in every act and period (acts 1, 2 and
 * act 3's $22/$28), and only subtypes 0-3 ($22, $28) and 0 ($23, $2C) are
 * placed. CD's object variables oVar2A.. sit at S3K's $30.. (free in S3K's
 * object); routines keep CD's even numbers. TopSolidObject -> o_platform,
 * SolidObject -> o_solid_1p, GetOffObject -> cd_get_off; objPlayerSlot ->
 * Player 1 (the solid platforms carry Player 2 as S3K's do).
 */
#include "trilogy_obj.h"


int tri_scd_palcycle_step(int k);          /* trilogy_scdlevel.c */

#define OBJ_prev_anim   0x21               /* (S3K's Animate_Sprite: CD's oPrevAnim) */
#define ST_AIR          0x02
#define P_CHAR          0x38               /* character_id (1: Tails) */
#define P_OBJCTRL       0x2E               /* object_control: CD's oPlayerCtrl */
#define P_STICK         0x3C               /* stick_to_convex: CD's oPlayerStick */
#define P_JUMPING       0x40               /* jumping: CD's oPlayerJump */
#define P_DEF_YRAD      0x44               /* default_y_radius (CD: $13) */
#define P_DEF_XRAD      0x45               /* default_x_radius (CD: 9) */

static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ObjGetFloorDist2: the floor below o (y + y radius) at x */
static int16_t floor_at(uint32_t o, uint16_t x)
{
    uint16_t x0 = OB16(o, OBJ_x_pos);
    SET16(o, OBJ_x_pos, x);
    int16_t d1 = o_floor_dist(o);
    SET16(o, OBJ_x_pos, x0);
    return d1;
}

static void roll_up(uint32_t p)        /* (into a ball, as Sonic CD's jump makes one) */
{
    if (OB8(p, OBJ_status) & 4) return;
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 4);
    SET8(p, OBJ_y_radius, 0x0E);
    SET8(p, OBJ_x_radius, 7);
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + (OB8(p, P_CHAR) == 1 ? 1 : 5));   /* (Tails stands $F high) */
    SET8(p, OBJ_anim, 2);
}

/* ---- $22 platform, $28's moves: one sine for all ------------------------- */
#define PT_Y            0x32               /* oVar32: its place */
#define PT_X            0x36               /* oVar36 */
#define PT_COUNT        0x3A               /* oVar3A (counted, never read) */
static int16_t pt_offset(uint32_t o)                          /* ObjPlatform_GetOffset: +-64 px */
{
    int16_t s, c;
    o_sincos((uint8_t)r16(RAM_Level_frame_counter), &s, &c);   /* (levelFrames & $FF) */
    SET8(o, PT_COUNT, OB8(o, PT_COUNT) + 1);
    return (int16_t)((int16_t)(s * 4) >> 4);
}

static void pt_move(uint32_t o)                               /* ObjPlatform_Move* (subtype) */
{
    uint32_t old = r32(o + OBJ_x_pos);
    switch (OB8(o, OBJ_subtype)) {
    case 0: SET16(o, OBJ_x_pos, OB16(o, PT_X) + pt_offset(o)); break;   /* MoveX */
    case 1: SET16(o, OBJ_x_pos, OB16(o, PT_X) - pt_offset(o)); break;   /* MoveX2 */
    case 2: SET16(o, OBJ_y_pos, OB16(o, PT_Y) - pt_offset(o)); return;  /* MoveY */
    case 3: SET16(o, OBJ_y_pos, OB16(o, PT_Y) + pt_offset(o)); return;  /* MoveY2 */
    default: return;                                          /* (none other placed) */
    }
    SET16(o, OBJ_x_vel, (uint16_t)((r32(o + OBJ_x_pos) - old) >> 8));  /* (lsr.l #8) */
}

static void pt_init(uint32_t o, int asset, int width)
{
    SET8(o, OBJ_routine, 2);
    o_init(o, asset, OB8(o, OBJ_render) | 4, 1, width, 32);
    SET16(o, PT_X, OB16(o, OBJ_x_pos));
    SET16(o, PT_Y, OB16(o, OBJ_y_pos));
    SET8(o, OBJ_y_radius, 12);
}

static void obj_platform(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) pt_init(o, SCDA_WWZ_PLATFORM, 24);   /* (then on into Main) */
    uint16_t x = OB16(o, OBJ_x_pos);
    pt_move(o);
    o_platform(o, 24 + 10, 12, x);
    if (cd_despawn(o, OB16(o, PT_X))) return;                 /* (CheckObjDespawn2: its place) */
    o_draw(o);
}

/* ---- $28 spinning platform: player 1 on it is spun round its middle ------ */
#define SP_ANG          0x30               /* (CD keeps it on the player: oPlayerRotAngle) */
#define SP_DIST         0x31               /* (oPlayerRotDist) */
static uint32_t s_spin, s_spin_code;       /* the platform spinning player 1 (0: none) */
static int8_t s_turn = -1;                 /* the turn it shows him in (0-5) */

/* (-> scd_spin_frame: Sonic CD's "Spinning Top" pictures, while his
 * animation is still the spin's: not once hurt or dead) */
int scd_wwz_spin_frame(int n)
{
    uint32_t p = RAM_Player_1;
    return n == 0 && s_spin && r32(s_spin) == s_spin_code && (OB8(s_spin, OBJ_status) & ST_P1_STAND) &&
           (OB8(p, P_OBJCTRL) & 1) && OB8(p, OBJ_anim) == 0x0F ? s_turn : -1;
}

static void spin_let_go(uint32_t p)                           /* (GetOffObject: control back) */
{
    SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1u);
    s_spin = 0;
}

static void spin_move(uint32_t o, uint32_t p)                 /* ObjSpinPlatform_MoveSonic */
{
    static const uint8_t turns[] = { 0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5 };
    uint8_t a = (uint8_t)(OB8(o, SP_ANG) + 4);
    SET8(o, SP_ANG, a);
    int16_t s, c;
    o_sincos(a, &s, &c);
    SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + (int16_t)(((int32_t)OB8(o, SP_DIST) * c) >> 8));
    s_turn = (int8_t)turns[a >> 4];                           /* (CD: his frame of anim $2D) */
    if (!(a & 0x3F)) SET8(o, SP_DIST, OB8(o, SP_DIST) + 1);   /* (each quarter turn, out a pixel) */
    uint8_t held = r8(RAM_Ctrl_1_held), tap = r8(RAM_Ctrl_1_pressed);   /* (p1CtrlData: the pad as read) */
    /* ObjSpinPlatform_CheckDirs: out the way he is, in the other way */
    int right = OB16(p, OBJ_x_pos) >= OB16(o, OBJ_x_pos);
    if (held & (right ? 8 : 4))
        SET8(o, SP_DIST, OB8(o, SP_DIST) + 1);
    else if (held & (right ? 4 : 8))
        SET8(o, SP_DIST, OB8(o, SP_DIST) ? OB8(o, SP_DIST) - 1 : 0);
    /* ObjSpinPlatform_CheckJump: $680 off the way he stands */
    if (!(tap & 0x70)) return;
    SET8(p, P_OBJCTRL, 0);
    s_spin = 0;
    o_sincos((uint8_t)(OB8(p, OBJ_angle) - 0x40), &s, &c);
    SET16(p, OBJ_x_vel, OBS16(p, OBJ_x_vel) + (int16_t)(((int32_t)c * 0x680) >> 8));
    SET16(p, OBJ_y_vel, OBS16(p, OBJ_y_vel) + (int16_t)(((int32_t)s * 0x680) >> 8));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~0x20);
    SET8(p, P_JUMPING, 1);
    SET8(p, P_STICK, 0);
    SET8(p, OBJ_y_radius, OB8(p, P_DEF_YRAD));
    SET8(p, OBJ_x_radius, OB8(p, P_DEF_XRAD));
    if (OB8(p, OBJ_status) & 4) SET8(p, OBJ_status, OB8(p, OBJ_status) | 0x10);   /* (roll jump) */
    else roll_up(p);
}

static void obj_spin_platform(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) pt_init(o, SCDA_WWZ_SPIN, 16);  /* (then on into Main) */
    uint16_t x = OB16(o, OBJ_x_pos);
    pt_move(o);
    o_animate(o, o_asset_anim(SCDA_WWZ_SPIN));
    o_platform(o, 16 + 10, 12, x);
    if (OB8(o, OBJ_status) & ST_P1_STAND) {
        SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_XFLIP);   /* (faces left) */
        SET8(p, OBJ_render, (OB8(p, OBJ_render) & 0xFC) | 1);
        if (!(OB8(p, P_OBJCTRL) & 1)) {                       /* (on: from where he stands) */
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);
            SET8(p, OBJ_anim, 0x0F);                          /* (CD's $2D; S3K's twirl) */
            uint16_t dx = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
            SET8(o, SP_ANG, 0);
            if (OB16(p, OBJ_x_pos) < OB16(o, OBJ_x_pos)) {    /* (left of it: half a turn on) */
                dx = (uint16_t)-dx;
                SET8(o, SP_ANG, 0x80);
            }
            SET8(o, SP_DIST, (uint8_t)dx);
            s_spin = o;
            s_spin_code = r32(o + OBJ_code);
        }
        if (s_spin == o && OB8(p, OBJ_routine) < 6) spin_move(o, p);
    } else if (s_spin == o)                                   /* (wound out over its edge, or knocked off) */
        spin_let_go(p);
    if (cd_despawn(o, OB16(o, PT_X))) {
        if (s_spin == o) spin_let_go(p);
        return;
    }
    o_draw(o);
}

/* ---- $23 bouncing platform: hops while the floor's lights are on --------- */
#define BP_YRAD         0x30               /* oVar2E: 4 px deeper while it moves */
#define BP_PYVEL        0x32               /* oVar30: the player's y speed, kept over the test */
#define BP_GRAV         0x3E               /* oVar3E */
static int bp_bounce(void)                                    /* ObjBouncePlatform_CheckFloor */
{
    int per = cd_period();
    if (per != TRI_CD_PAST && per != TRI_CD_PRESENT) return 1;   /* (timeZone 2: always) */
    return tri_scd_palcycle_step(3) != (per == TRI_CD_PRESENT ? 0x3C + 0x1E : 0x3C);
}

static void bp_launch(uint32_t o, int16_t v)
{
    SET16(o, OBJ_y_vel, (uint16_t)v);
    SET16(o, BP_GRAV, 0x10);
}

static void bp_move(uint32_t o)                               /* ObjBouncePlatform_Move: down at most $600 */
{
    int16_t v = (int16_t)(OBS16(o, OBJ_y_vel) + OBS16(o, BP_GRAV));
    if (v >= 0x600) v = 0x600;
    SET16(o, OBJ_y_vel, (uint16_t)v);
    SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)((int32_t)v * 256));
}

/* ObjBouncePlatform_Solid: solid unless the player rises faster than $600;
 * on it (or under it) he is put on top, rising with it */
static void bp_solid(uint32_t o)
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || (n && !(OB8(p, OBJ_render) & 0x80))) continue;   /* (SolidObjectFull: Tails on screen) */
        if (OBS16(p, OBJ_y_vel) < -0x600) continue;
        SET8(o, BP_YRAD, OB16(o, OBJ_y_vel) ? 4 : 0);
        int h = 16 + OB8(o, BP_YRAD);
        SET16(o, BP_PYVEL, OB16(p, OBJ_y_vel));
        uint8_t st = OB8(p, OBJ_status);
        if ((st & 0x08) && !(st & ST_AIR)) SET16(p, OBJ_y_vel, 0);   /* (on an object: no y speed for the test) */
        int16_t r = o_solid_1p(o, n, 32 + 10, h, h + 1, OB16(o, OBJ_x_pos), 0);
        if (!(OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND)) && r != -2) {   /* (CD: no top / bottom touch) */
            SET16(p, OBJ_y_vel, OB16(o, BP_PYVEL));
            continue;
        }
        if (OB8(p, OBJ_routine) >= 6) { SET16(p, OBJ_y_vel, 0); continue; }
        SET32(p, OBJ_y_pos, r32(o + OBJ_y_pos) - ((uint32_t)((int8_t)OB8(p, OBJ_y_radius) + 16) << 16));
        SET16(p, OBJ_y_vel, OBS16(o, OBJ_y_vel) < 0 ? OB16(o, OBJ_y_vel) : 0);
    }
}

static void obj_bounce_platform(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_WWZ_BOUNCE, OB8(o, OBJ_render) | 4, 3, 32, 16);
        SET8(o, OBJ_y_radius, 16);
        /* fall through */
    case 2:                                                   /* resting: off when the lights come on */
        if (bp_bounce()) {
            bp_launch(o, -0x600);
            SET8(o, OBJ_routine, 4);
        }
        break;
    case 4:                                                   /* bouncing */
        bp_move(o);
        if (o_ceiling_dist(o) < 0) SET16(o, OBJ_y_vel, 0);
        if (o_floor_dist(o) >= 0) break;
        if (bp_bounce()) {
            bp_launch(o, -0x600);
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(0xB4);    /* (ObjBouncePlatform_Bounced: FM $B4, each hop off the floor) */
            break;
        }
        bp_launch(o, -0x180);                                 /* (dark: one last little hop) */
        SET8(o, OBJ_routine, 6);
        break;
    case 6:                                                   /* settling */
        bp_move(o);
        if (OBS16(o, OBJ_y_vel) < 0 || o_floor_dist(o) >= 0) break;
        SET16(o, OBJ_y_vel, 0);
        SET16(o, BP_GRAV, 0);
        SET8(o, OBJ_routine, 2);
        break;
    }
    bp_solid(o);
    draw_despawn(o);
}

/* ---- $2C seesaw: stood on, the high side tips it ------------------------- */
#define SS_PT1          0x30               /* oVar2A: the high side (word address) */
#define SS_PARENT       0x30               /* oVar2A (a side's): the seesaw */
#define SS_PT2          0x32               /* oVar2C: the low side */
#define SS_TIME         0x34               /* oVar2E (word) */
#define SS_STOOD        0x3F               /* oVar3F (the high side's) */
static uint32_t ss_side(uint32_t o, int low) { return 0xFF0000u | OB16(o, low ? SS_PT2 : SS_PT1); }

static uint32_t ss_new_side(uint32_t o)                       /* FindObjSlot + ObjSeesaw_InitSub */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));                   /* (CD: its oID) */
    o_init(a1, SCDA_WWZ_SEESAW, OB8(o, OBJ_render), 3, 16, 16);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_subtype, 0xFF);
    SET8(a1, OBJ_x_radius, 16);
    SET8(a1, OBJ_y_radius, 8);
    SET8(a1, OBJ_frame, 9);
    SET16(a1, SS_PARENT, (uint16_t)o);
    SET16(o, SS_TIME, 120);
    return a1;
}

static void ss_wait(uint32_t o)                               /* ObjSeesaw_CheckSlideDown: 2 s, then down */
{
    if (OBS16(o, SS_TIME) < 0) return;
    SET16(o, SS_TIME, OB16(o, SS_TIME) - 1);
    if (OBS16(o, SS_TIME) < 0)
        SET16(o, OBJ_y_vel, 0x100);
    else if (OB16(o, SS_TIME) != 60)
        return;
    SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
}

static void ss_slide(uint32_t o)                              /* ObjSeesaw_SlideDown: till the low side lands */
{
    uint32_t hi = ss_side(o, 0), lo = ss_side(o, 1);
    int d0 = OB8(o, OBJ_y_vel);                               /* (its speed's high byte: px) */
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
    SET16(hi, OBJ_y_pos, OB16(hi, OBJ_y_pos) + d0);
    SET16(lo, OBJ_y_pos, OB16(lo, OBJ_y_pos) + d0);
    int w = OB8(lo, OBJ_width_px);
    if (OBS16(lo, OBJ_x_pos) >= OBS16(o, OBJ_x_pos)) w = -w;  /* (under its inner edge) */
    int16_t d1 = floor_at(lo, (uint16_t)(OB16(lo, OBJ_x_pos) + w));
    if (d1 >= 0) return;
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
    SET16(hi, OBJ_y_pos, OB16(hi, OBJ_y_pos) + d1);
    SET16(lo, OBJ_y_pos, OB16(lo, OBJ_y_pos) + d1);
    SET16(o, OBJ_y_vel, 0);
}

static void obj_seesaw(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {                                /* ObjSeesawPtfm: a side (the seesaw runs it) */
        uint32_t par = 0xFF0000u | OB16(o, SS_PARENT);
        if (r32(par + OBJ_code) != r32(o + OBJ_code) ||       /* (and still its side: the slot not reused) */
            (OB16(par, SS_PT1) != (uint16_t)o && OB16(par, SS_PT2) != (uint16_t)o))
            o_delete(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_WWZ_SEESAW, OB8(o, OBJ_render) | 4, 3, 24, 24);
        SET8(o, OBJ_x_radius, 24);
        SET8(o, OBJ_y_radius, 24);
        uint32_t a1 = ss_new_side(o);
        if (!a1) { o_delete(o); return; }
        SET16(o, SS_PT1, (uint16_t)a1);
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 40);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 24);
        if (!(a1 = ss_new_side(o))) { o_delete(o); return; }
        SET16(o, SS_PT2, (uint16_t)a1);
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + 40);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 24);
        SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 1);
        SET8(a1, OBJ_status, OB8(a1, OBJ_status) | ST_XFLIP);
        break;
    }
    case 2: {                                                 /* ObjSeesaw_Main */
        if (!OB16(o, OBJ_y_vel)) ss_wait(o); else ss_slide(o);
        uint32_t lo = ss_side(o, 1), hi = ss_side(o, 0);
        o_platform(lo, 16 + 10, 8, OB16(lo, OBJ_x_pos));
        o_draw(lo);
        o_platform(hi, 16 + 10, 8, OB16(hi, OBJ_x_pos));
        SET8(hi, SS_STOOD, cd_stood_on(hi) ? 0xFF : 0);
        o_draw(hi);
        if (!OB8(hi, SS_STOOD)) {
            o_animate(o, o_asset_anim(SCDA_WWZ_SEESAW));
            break;
        }
        SET8(o, OBJ_routine, 4);                              /* (it tips) */
        SET16(o, SS_TIME, 3);
        SET8(o, OBJ_frame, 8);
        /* (the low side rises 96 px unsolid: CD's SolidObject would drop one
         * left standing on it, S3K's would lift him after; Player 2 only) */
        cd_get_off(lo);
        break;
    }
    case 4: {                                                 /* ObjSeesaw_PushUp: 4 frames */
        uint32_t a1 = ss_side(o, 1);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 24);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 12);
        SET16(o, SS_TIME, OB16(o, SS_TIME) - 1);
        if (OBS16(o, SS_TIME) < 0) {                          /* ObjSeesaw_Swap: the low side is the high one */
            SET8(o, OBJ_routine, 2);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, SS_TIME, 120);
            SET16(o, SS_PT2, OB16(o, SS_PT1));
            SET16(o, SS_PT1, (uint16_t)a1);
            SET8(o, OBJ_anim, (int8_t)OB8(o, OBJ_anim) > 2 ? 0 : 3);
            SET8(o, OBJ_prev_anim, 0xFF);
        }
        o_draw(ss_side(o, 1));
        o_draw(ss_side(o, 0));
        break;
    }
    }
    draw_despawn(o);
}

void scd_wwz_plat_register(TriObjFn *t, int base)
{
    t[base + 0x22] = obj_platform;
    t[base + 0x23] = obj_bounce_platform;
    t[base + 0x28] = obj_spin_platform;
    t[base + 0x2C] = obj_seesaw;
}
