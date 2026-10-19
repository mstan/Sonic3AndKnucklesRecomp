/*
 * scd_ttz_gim.c — Tidal Tempest's gimmicks (Sonic CD; decomp: R4\SWBLK4.C,
 * HARID4.C, WALLS.C, SCREW_A.C, SCREW_C.C, TEKKYU*.C, SWGUN4.C, OSUMIZU.C;
 * the disc's code, R41A-R42D).
 *
 *   $30 button lift    (act 1 present) a 128 px column, a 64 px part under
 *                      its left side and a button ($31) on top: pressed,
 *                      the column rises (to 1 px a frame) till 64 px of
 *                      ceiling is left; let go, it sinks to the floor
 *   $32 spike crusher  (act 1 present) player 1 64-191 px under it: it
 *                      slides 20 frames, waits 30, then falls (faster above
 *                      the water) and sinks 32 px into the floor
 *   $33 toothed walls  four rows of a pair of solid teeth (subtype: their
 *                      lengths), opening 96 px, waiting, slamming shut at
 *                      2 px a frame, waiting, in turn; pushed into a wall by
 *                      one, the player is crushed
 *   $35 screw          (switch cue) turning or still, flipped by its switch
 *                      (Sonic CD's subtype 2, scd_level.py: subtype bits 2-5,
 *                      +1); in the futures it only changes its picture
 *   $36 spike ball     on a five-link chain round its pivot (one turn in 256
 *                      frames; subtype set: the other way)
 *   $37 spike ball     still
 *   $38 spike balls    four on a circle (radius 256 >> subtype, subtype
 *                      negative: the other way)
 *   $3F prize buttons  (act 1 futures) six buttons; the one stood on picks
 *                      a prize by the clock's seconds: the other five turn
 *                      to spikes, give 1000 each, blow two big bubbles each,
 *                      or the first drops 21 rings ($40) from above
 *   $42 water jet      (act 2 present) a nozzle; a jet every 30 frames, 3 px
 *                      a frame, setting player 1's ground speed to its own
 * Each is player 1's in Sonic CD (the buttons, the crusher's eye, the jet);
 * S3K's solidity serves both players.
 */
#include "trilogy_obj.h"


#define P_YRAD          0x1E
#define FM_SWITCH       0xBF               /* (swgun4_move: a prize button stood on, once each) */
#define RAM_Timer_second 0xFFFE24u         /* (Timer + 2: CD's pltime.b.b3, $FF1516) */

extern uint8_t scd_ttz_switch[32];         /* scd_ttz.c */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void dims(uint32_t o, int xr, int width, int yr)       /* (CD's oXRadius, oWidth, oYRadius) */
{
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_width_px, width);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_height_px, yr);
}

static void art(uint32_t o, int asset, int prio)
{
    o_asset(o, asset);
    SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
    SET16(o, OBJ_priority, (uint16_t)(prio << 7));
}

static void full_solid(uint32_t o, uint16_t x0)
{
    o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, x0);
}

static uint32_t ram(uint16_t a) { return 0xFF0000u | a; }

/* AnimateObject on a script kept here (one anim): [duration, frames..., $FF loop] */
static void animate(uint32_t o, const uint8_t *s)
{
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
    if (t >= 0) { SET8(o, OBJ_anim_timer, t); return; }
    SET8(o, OBJ_anim_timer, s[0]);
    unsigned i = OB8(o, OBJ_anim_frame);
    uint8_t f = s[1 + i];
    if (f == 0xFF) { i = 0; f = s[1]; }
    SET8(o, OBJ_frame, f);
    SET8(o, OBJ_anim_frame, i + 1);
}

/* ---- $30 button lift, $31 its button ------------------------------------- */
#define EL_BUTTON       0x30               /* oVar30: its button */
#define EL_Y0           0x32               /* oVar32 */
#define EL_LINK         0x34               /* oVar34: the column's part / the part's column */
#define EL_X0           0x36               /* oVar36: its place (despawn) */
#define EL_PART         0x3C               /* oVar3C: 1 the part */
#define BT_PARENT       0x34               /* (the button's oVar34: the column) */
#define BT_X0           0x36               /* oVar36 */
#define BT_DX           0x38               /* oVar38 */
#define BT_DY           0x39               /* oVar39 */
static void elevator(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        art(o, SCDA_TTZ_ELEVATOR, 3);
        if (!OB8(o, EL_PART)) {
            SET16(o, EL_X0, OB16(o, OBJ_x_pos));
            SET16(o, EL_Y0, OB16(o, OBJ_y_pos));
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x10);
            dims(o, 0, 0x10, 0x40);
            SET8(o, OBJ_frame, 1);
            uint32_t a1 = o_alloc_after(o);                   /* (the part: under its left side) */
            if (!a1) { o_delete(o); return; }
            SET32(a1, OBJ_code, CD_CODE(0x30));
            SET16(a1, EL_X0, OB16(o, EL_X0));
            SET16(a1, EL_Y0, OB16(o, EL_Y0));
            dims(a1, 0, 0x10, 0x20);
            SET8(a1, EL_PART, 1);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x20);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x20);
            SET16(a1, EL_LINK, (uint16_t)o);
            SET16(o, EL_LINK, (uint16_t)a1);
            SET8(a1, OBJ_frame, 2);
            a1 = o_alloc_after(o);                            /* (its button, 68 px up) */
            if (!a1) { o_delete(o); return; }
            SET32(a1, OBJ_code, CD_CODE(0x31));
            SET16(a1, BT_PARENT, (uint16_t)o);
            SET16(o, EL_BUTTON, (uint16_t)a1);
            SET8(a1, BT_DY, 0xBC);
            SET16(a1, BT_X0, OB16(o, EL_X0));
            SET16(a1, EL_Y0, OB16(o, EL_Y0));
        }
    }
    if (!OB8(o, EL_PART)) {                                   /* the column: up to the ceiling, down to the floor */
        uint32_t y0 = r32(o + OBJ_y_pos);
        if (OBS16(o, OBJ_y_vel) < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x40);   /* (64 px over its top) */
            if (o_ceiling_dist(o) < 0) SET16(o, OBJ_y_vel, 0);
        } else if (o_floor_dist(o) < 0) SET16(o, OBJ_y_vel, 0);
        SET32(o, OBJ_y_pos, y0 + (uint32_t)((int32_t)OBS16(o, OBJ_y_vel) << 8));
        uint32_t a1 = OB16(o, EL_LINK) ? ram(OB16(o, EL_LINK)) : 0;
        if (a1 && r32(a1) == CD_CODE(0x30)) SET16(a1, OBJ_y_vel, OB16(o, OBJ_y_vel));
        a1 = OB16(o, EL_BUTTON) ? ram(OB16(o, EL_BUTTON)) : 0;
        if (a1 && r32(a1) == CD_CODE(0x31)) {
            int16_t v = OBS16(o, OBJ_y_vel);
            if (OB8(a1, OBJ_subtype) & 3) { v -= 8; if (v < -0x100) v = -0x100; }
            else { v += 8; if (v > 0x100) v = 0x100; }
            SET16(o, OBJ_y_vel, v);
        }
    }
    full_solid(o, OB16(o, OBJ_x_pos));
    if (OB8(o, EL_PART) && OB16(o, EL_LINK)) {                /* (the part follows) */
        uint32_t a1 = ram(OB16(o, EL_LINK));
        if (r32(a1) != CD_CODE(0x30)) { o_delete(o); return; }
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 0x20);
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x20);
    }
    if (o_out_of_range(o, OB16(o, EL_X0))) { o_delete(o); return; }   /* (CD: gone for good, not remembered) */
    o_draw(o);
}

/* the button: player 1's feet 1-16 px into its top 16 px */
static int lift_pressed(uint32_t o, uint32_t p)
{
    int d1 = (int8_t)OB8(o, OBJ_width_px) + 8;
    int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + d1);
    if (dx < 0 || dx >= d1 * 2) return 0;
    uint16_t d0 = (uint16_t)(OB16(o, OBJ_y_pos) - 0x10 - (OB16(p, OBJ_y_pos) + (int8_t)OB8(p, P_YRAD)));
    return d0 >= 0xFFF0;
}

static void lift_button(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        art(o, SCDA_TTZ_ELEVBUTTON, 3);
        if (!OB16(o, BT_X0)) SET16(o, BT_X0, OB16(o, OBJ_x_pos));
        dims(o, 0, 0x0E, 5);
    }
    if (lift_pressed(o, RAM_Player_1)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) | 1);
    else SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & ~1);
    SET8(o, OBJ_frame, OB8(o, OBJ_subtype) ? 1 : 0);
    full_solid(o, OB16(o, OBJ_x_pos));
    if (OB16(o, BT_PARENT)) {
        uint32_t a1 = ram(OB16(o, BT_PARENT));
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (int8_t)OB8(o, BT_DX));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + (int8_t)OB8(o, BT_DY));
    }
    if (cd_despawn(o, OB16(o, BT_X0))) return;
    o_draw(o);
}

/* ---- $32 spike crusher (act 1 present); subtype $FF: its spikes ---------- */
#define HD_VY           0x40               /* oVar2A (long) */
#define HD_SINK         0x44               /* oVar2E (long) */
#define HD_TIME         0x32               /* oVar32 */
#define HD_GRAV         0x34               /* oVar34 (long) */
#define HD_SPIKES       0x3C               /* oVar3C */
#define HD_PARENT       0x3E               /* oVar3E (the spikes') */
static void crusher(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* the spikes under it */
        if (r32(ram(OB16(o, HD_PARENT))) != CD_CODE(0x32)) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    uint32_t p = RAM_Player_1;
    uint8_t r = OB8(o, OBJ_routine);
    if (!r || (OB8(o, OBJ_render) & 0x80)) {                  /* (seen, it acts) */
        uint32_t sp = OB16(o, HD_SPIKES) ? ram(OB16(o, HD_SPIKES)) : 0;
        int32_t v = (int32_t)r32(o + HD_VY);
        switch (r) {
        case 0: {
            SET8(o, OBJ_routine, 2);
            art(o, SCDA_TTZ_CRUSHER, 3);
            dims(o, 0x10, 0x10, 0x40);
            SET32(o, HD_GRAV, (int16_t)OB16(o, OBJ_y_pos) <= (int16_t)r16(RAM_Mean_water_level) ? 0x3000 : 0x1000);
            uint32_t a1 = o_alloc_after(o);
            if (!a1) break;
            SET32(a1, OBJ_code, CD_CODE(0x32));
            SET8(a1, OBJ_subtype, 0xFF);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x30);
            SET16(a1, HD_PARENT, (uint16_t)o);
            SET16(o, HD_SPIKES, (uint16_t)a1);
            SET8(a1, OBJ_coll_flags, tri_scd_coll(0xB0));
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
            o_asset(a1, SCDA_TTZ_CRUSHER);
            dims(a1, 0x10, 0x10, 0x10);
            SET8(a1, OBJ_frame, 1);
            break;
        }
        case 2:                                               /* waiting for player 1 under it */
            if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - 0x40) >= 0x80) break;
            if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x10) >= 0x20) break;
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_y_vel, 0x100);                       /* (its rider's carry, as CD's SolidObject has it) */
            SET16(o, HD_TIME, 20);
            break;
        case 4:                                               /* a start */
            SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)v);
            if (sp) SET32(sp, OBJ_y_pos, r32(sp + OBJ_y_pos) + (uint32_t)v);
            SET32(o, HD_VY, (uint32_t)(v + 0x2000));
            SET16(o, HD_TIME, OB16(o, HD_TIME) - 1);
            if (OBS16(o, HD_TIME) < 0) {
                SET8(o, OBJ_routine, 6);
                SET32(o, HD_VY, 0);
                SET16(o, HD_TIME, 30);
            }
            break;
        case 6:
            SET16(o, HD_TIME, OB16(o, HD_TIME) - 1);
            if (OBS16(o, HD_TIME) < 0) SET8(o, OBJ_routine, 8);
            break;
        case 8: {                                             /* falling */
            SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)v);
            if (sp) SET32(sp, OBJ_y_pos, r32(sp + OBJ_y_pos) + (uint32_t)v);
            SET32(o, HD_VY, (uint32_t)(v + (int32_t)r32(o + HD_GRAV)));
            int16_t d1 = o_floor_dist(o);
            if (d1 >= 0) break;
            SET8(o, OBJ_routine, 0x0A);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, OBJ_y_vel, 0);
            SET32(o, HD_SINK, 0x200000);
            if (sp) o_delete(sp);
            SET16(o, HD_SPIKES, 0);
            break;
        }
        case 0x0A: {                                          /* into the floor, 32 px */
            SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)v);
            int32_t s = (int32_t)r32(o + HD_SINK) - v;
            SET32(o, HD_SINK, (uint32_t)s);
            if (s < 0) SET8(o, OBJ_routine, 0x0C);
            break;
        }
        default:
            break;
        }
        full_solid(o, OB16(o, OBJ_x_pos));
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $33 toothed walls; subtype $FE / $FF: a tooth (left, right) -------- */
#define WL_TIMER        0x40               /* oVar2A */
#define WL_SPEED        0x42               /* oVar2C (long): the right ones' leftward speed */
#define WL_X0           0x30               /* oVar30 */
#define WL_PARENT       0x32               /* oVar32 */
#define WL_STEP         0x34               /* oVar34 */
#define WL_TEETH        0x38               /* oVar38..3F: the eight teeth (object slots) */
static void crush_check(uint32_t o)                           /* (CD's SolidObject: pushed into a wall) */
{
    int16_t vx = OBS16(o, OBJ_x_vel);
    if (!vx) return;
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!(g_o_d6 & (1u << (16 + n))) || !o_player_active(p) || OB8(p, OBJ_routine) >= 4) continue;
        int16_t d1 = vx > 0 ? o_wall_right(p, 10) : o_wall_left(p, -10);
        if (d1 < 0) o_kill_player(p);
    }
}

static void walls(uint32_t o)
{
    static const uint8_t lengths[] = {                        /* (four rows, the right one first; read backwards) */
        0x02, 0x01, 0x01, 0x02, 0x02, 0x01, 0x01, 0x02, 0x00, 0x03, 0x01, 0x02, 0x02, 0x01, 0x03, 0x00,
        0x03, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x03, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01,
        0x02, 0x01, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x00, 0x03, 0x02, 0x00, 0x02, 0x00, 0x00, 0x03,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0x00, 0x03,
        0x01, 0x02, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01 };
    static const int32_t steps[4][2] = { { 60, 0 }, { 48, 0x20000 }, { 60, 0 }, { 192, -0x8000 } };
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* a tooth */
        if (r32(ram(OB16(o, WL_PARENT))) != CD_CODE(0x33)) { o_delete(o); return; }
        uint32_t x = r32(o + OBJ_x_pos) - ((uint32_t)(int32_t)OBS16(o, OBJ_x_vel) << 8);
        full_solid(o, (uint16_t)(x >> 16));                   /* (its rider carried by its speed) */
        crush_check(o);
        o_draw(o);
        return;
    }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET16(o, WL_X0, OB16(o, OBJ_x_pos));
        art(o, SCDA_TTZ_WALLS, 3);
        dims(o, 0x10, 0x10, 0x10);
        SET8(o, OBJ_frame, 4);
        SET16(o, WL_PARENT, (uint16_t)o);
        SET16(o, WL_TIMER, 0xC0);
        SET32(o, WL_SPEED, (uint32_t)-0x8000);
        unsigned a5 = (OB8(o, OBJ_subtype) + 1u) * 8u, k = 0;
        for (int row = 3; row >= 0; row--)
            for (int side = 1; side >= 0; side--) {
                uint32_t a1 = o_alloc_after(o);
                if (!a1) { o_delete(o); return; }
                SET8(o, WL_TEETH + k++, (uint8_t)o_slot_index(a1));
                SET32(a1, OBJ_code, CD_CODE(0x33));
                SET8(a1, OBJ_subtype, side ? 0xFF : 0xFE);
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x30 + row * 0x20);
                uint8_t len = a5 && a5 <= sizeof lengths ? lengths[--a5] : 0;
                SET8(a1, OBJ_frame, len);
                dims(a1, (len + 1) * 16, (len + 1) * 16, OB8(o, OBJ_y_radius));
                int d1 = 0x40, d0 = len << 4;
                if (!side) { d1 = -d1; d0 = -d0; }
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + d1 - d0);
                SET16(a1, WL_PARENT, (uint16_t)o);
                SET8(a1, OBJ_render, OB8(o, OBJ_render));
                SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
                SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
                SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
                SET8(a1, OBJ_routine, 2);
            }
    }
    int32_t d5 = (int32_t)r32(o + WL_SPEED);
    for (int k = 0; k < 8; k++) {                             /* (the right one of a row against it, the left with it) */
        uint32_t a1 = o_slot(OB8(o, WL_TEETH + k));
        int32_t v = k & 1 ? d5 : -d5;
        SET32(a1, OBJ_x_pos, r32(a1 + OBJ_x_pos) + (uint32_t)v);
        SET16(a1, OBJ_x_vel, (uint16_t)(int16_t)(v >> 8));
    }
    SET16(o, WL_TIMER, OB16(o, WL_TIMER) - 1);
    if (!OB16(o, WL_TIMER)) {
        const int32_t *s = steps[(OB16(o, WL_STEP) >> 3) & 3];
        SET16(o, WL_TIMER, (uint16_t)s[0]);
        SET32(o, WL_SPEED, (uint32_t)s[1]);
        SET16(o, WL_STEP, (OB16(o, WL_STEP) + 8) & 0x1F);
    }
    if (cd_despawn(o, OB16(o, WL_X0))) return;                /* (unseen itself: frame 4 is empty) */
}

/* ---- $35 screw ------------------------------------------------------------ */
#define SC_SEEN         0x3E               /* oVar3E: its switch seen */
#define SC_TURN         0x3F               /* oVar3F */
static void screw(uint32_t o)
{
    static const uint8_t turning[] = { 1, 0, 1, 2, 3, 4, 0xFF };
    int future = cd_period() == TRI_CD_GOOD || cd_period() == TRI_CD_BAD;   /* (SCREW_C: pictures, no despawn) */
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        int up = sub & 2;
        art(o, up ? SCDA_TTZ_SCREW2 : SCDA_TTZ_SCREW, 3);
        SET8(o, OBJ_width_px, up ? 8 : 0x10);
        SET8(o, OBJ_y_radius, up ? 0x10 : 8);
        SET8(o, OBJ_height_px, up ? 0x10 : 8);
        if (sub & 0x80) {
            if (future) SET8(o, OBJ_frame, 1);
            else SET8(o, SC_TURN, 0xFF);
        }
        if (sub & 1) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 3);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 3);
        }
    }
    int sw = ((sub >> 2) & 0x0F) - 1;                         /* (Sonic CD's subtype 2; -1 none) */
    if (sw >= 0 && !OB8(o, SC_SEEN) && scd_ttz_switch[sw]) {
        SET8(o, SC_SEEN, 0xFF);
        if (future) SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
        else SET8(o, SC_TURN, (uint8_t)~OB8(o, SC_TURN));
    }
    if (future) { o_draw(o); return; }
    if (!OB8(o, SC_TURN)) SET8(o, OBJ_frame, 0);
    else animate(o, turning);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $36 spike ball on a chain; subtype $FF a link, $FE the ball --------- */
#define TK_KIDS         0x30               /* oVar2E..38: the five links, the ball (object slots) */
#define TK_ANGLE        0x40               /* oVar2A (8.8) */
#define TK_SPEED        0x42               /* oVar2C */
#define TC_X            0x30               /* (a link's oVar2A: its place, 16.16) */
#define TC_Y            0x34               /* oVar2E */
#define TC_PARENT       0x3E               /* oVar3E */
static void chain_ball(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub & 0x80) {                                         /* a link, the ball */
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            if (sub == 0xFF) art(o, SCDA_TTZ_LINK, 3);
            else {
                art(o, SCDA_TTZ_BALL, 2);
                SET8(o, OBJ_coll_flags, tri_scd_coll(0xA9));
            }
        }
        SET16(o, OBJ_x_pos, OB16(o, TC_X));
        SET16(o, OBJ_y_pos, OB16(o, TC_Y));
        if (r32(ram(OB16(o, TC_PARENT))) != CD_CODE(0x36)) { o_delete(o); return; }
        if (sub != 0xFF) o_touch(o);
        o_draw(o);
        return;
    }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        art(o, SCDA_TTZ_PIVOT, 3);
        dims(o, 0, 8, 8);
        SET16(o, TK_SPEED, sub ? (uint16_t)-0x100 : 0x100);
        for (int k = 0; k < 6; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) { o_delete(o); return; }
            SET8(o, TK_KIDS + k, (uint8_t)o_slot_index(a1));
            SET32(a1, OBJ_code, CD_CODE(0x36));
            SET8(a1, OBJ_subtype, k == 5 ? 0xFE : 0xFF);
            dims(a1, 0, 8, 8);
            SET16(a1, TC_PARENT, (uint16_t)o);
        }
    }
    SET16(o, TK_ANGLE, OB16(o, TK_ANGLE) + OB16(o, TK_SPEED));
    int16_t s, c;
    o_sincos((uint8_t)(OB16(o, TK_ANGLE) >> 8), &s, &c);
    int32_t fs = (int32_t)s * 0x4000, fc = (int32_t)c * 0x4000;   /* (64 px; then 32, 16) */
    int32_t hs = fs >> 1, hc = fc >> 1, qs = hs >> 1, qc = hc >> 1;
    const int32_t dy[6] = { qs, hs, qs + hs, fs, qs + fs, hs + fs }, dx[6] = { qc, hc, qc + hc, fc, qc + fc, hc + fc };
    for (int k = 0; k < 6; k++) {
        uint32_t a1 = o_slot(OB8(o, TK_KIDS + k));
        SET32(a1, TC_Y, r32(o + OBJ_y_pos) + (uint32_t)dy[k]);
        SET32(a1, TC_X, r32(o + OBJ_x_pos) + (uint32_t)dx[k]);
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $37 spike ball, still -------------------------------------------------- */
static void still_ball(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        art(o, SCDA_TTZ_BALL1, 3);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xA9));
        dims(o, 0x10, 0x10, 0x10);
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

/* ---- $38 four spike balls on a circle ---------------------------------------- */
#define T4_CX           0x40               /* oVar2A: the centre */
#define T4_CY           0x42               /* oVar2C */
#define T4_ANGLE        0x44               /* oVar2E (8.8) */
#define T4_SPEED        0x30               /* oVar30 */
#define T4_SHIFT        0x32               /* oVar32 */
#define T4_X0           0x34               /* oVar34 */
#define T4_PARENT       0x3E               /* oVar3E: the first's (0: the first) */
static void four_balls(uint32_t o)
{
    if (OB16(o, T4_PARENT) && r32(ram(OB16(o, T4_PARENT))) != CD_CODE(0x38)) { o_delete(o); return; }
    if (!OB8(o, OBJ_routine)) {
        SET16(o, T4_X0, OB16(o, OBJ_x_pos));
        uint32_t kid[3];
        for (int k = 0; k < 3; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) { o_delete(o); return; }
            SET32(a1, OBJ_code, CD_CODE(0x38));
            SET8(a1, OBJ_routine, 2);
            SET16(a1, T4_PARENT, (uint16_t)o);
            kid[k] = a1;
        }
        int neg = (int8_t)OB8(o, OBJ_subtype) < 0;
        int16_t d0 = neg ? 0x800 : -0x800;                    /* (8 apart, behind it) */
        SET16(kid[2], T4_ANGLE, (uint16_t)d0);
        SET16(kid[1], T4_ANGLE, (uint16_t)(d0 * 2));
        SET16(kid[0], T4_ANGLE, (uint16_t)(d0 * 3));
        SET16(o, T4_SPEED, neg ? (uint16_t)-0x100 : 0x100);
        int8_t sh = (int8_t)OB8(o, OBJ_subtype);
        SET16(o, T4_SHIFT, (uint16_t)((sh < 0 ? -sh : sh) & 0x7F));
        const uint32_t all[4] = { o, kid[2], kid[1], kid[0] };
        for (int k = 0; k < 4; k++) {
            uint32_t a6 = all[k];
            o_asset(a6, SCDA_TTZ_BALL4);
            SET8(a6, OBJ_render, OB8(a6, OBJ_render) | 4);
            SET16(a6, OBJ_priority, (uint16_t)((k + 1) << 7));
            SET8(a6, OBJ_coll_flags, tri_scd_coll(0xA9));
            dims(a6, 0x10, 0x10, 0x10);
            SET8(a6, OBJ_routine, 2);
            SET16(a6, T4_CX, OB16(o, OBJ_x_pos));
            SET16(a6, T4_CY, OB16(o, OBJ_y_pos));
            SET16(a6, T4_SPEED, OB16(o, T4_SPEED));
            SET16(a6, T4_SHIFT, OB16(o, T4_SHIFT));
        }
        return;                                               /* (CD: placed, drawn from the next frame) */
    }
    SET16(o, T4_ANGLE, OB16(o, T4_ANGLE) + OB16(o, T4_SPEED));
    int16_t s, c;
    o_sincos((uint8_t)(OBS16(o, T4_ANGLE) >> 8), &s, &c);
    int sh = OB16(o, T4_SHIFT) & 15;
    SET16(o, OBJ_x_pos, OB16(o, T4_CX) + (int16_t)(c >> sh));
    SET16(o, OBJ_y_pos, OB16(o, T4_CY) + (int16_t)(s >> sh));
    if (!OB16(o, T4_PARENT) && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

/* ---- $3F prize buttons (act 1 futures), $40 their rings, the 1000s: S3K's
 * points ----------------------------------------------------------------------- */
#define PZ_SOUND        0x44               /* oVar2E */
#define PZ_COUNT        0x45               /* oVar2F */
#define PZ_PREV         0x30               /* oVar30 */
#define PZ_NOW          0x31               /* oVar31 */
#define PZ_Y0           0x32               /* oVar32 */
#define PZ_INDEX        0x34               /* oVar34: 0 the first, 1-5 */
#define PZ_PRIZE        0x35               /* oVar35: 0 spikes, 1 points, 2 bubbles, 3 rings */
#define PZ_X0           0x36               /* oVar36 */
#define PZ_T1           0x38               /* oVar38 */
#define PZ_T2           0x39               /* oVar39 */
#define PZ_FIRST        0x3A               /* oVar3A: the first button */
#define PZ_HIT          0x3C               /* oVar3C (the first's): the one stood on */
static void prize_switch(uint32_t o, uint32_t m)              /* switch_move */
{
    static const uint8_t prizes[7][6] = {
        { 2, 1, 0, 3, 3, 2 }, { 1, 0, 3, 3, 2, 2 }, { 0, 3, 3, 2, 2, 1 }, { 2, 2, 1, 0, 3, 3 },
        { 3, 2, 2, 1, 0, 3 }, { 3, 3, 2, 2, 1, 0 }, { 0, 0, 0, 0, 0, 0 } };
    uint32_t p = RAM_Player_1;
    SET8(o, PZ_PREV, OB8(o, PZ_NOW));
    full_solid(o, OB16(o, OBJ_x_pos));
    int on = (OB8(o, OBJ_status) & ST_P1_STAND) != 0;
    SET8(o, PZ_NOW, on ? 0xFF : 0);
    if (on && OBS16(p, OBJ_y_vel) >= 0 && !OB8(o, PZ_SOUND)) {
        SET16(m, PZ_HIT, (uint16_t)o);
        unsigned k = (r8(RAM_Timer_second) / 10u) & 7u;      /* (the clock's tens of seconds) */
        uint8_t pr = prizes[OB8(o, PZ_INDEX) % 7][k < 6 ? k : 0];
        SET8(o, PZ_PRIZE, pr);
        SET8(m, PZ_PRIZE, pr);
    }
    if (!OB8(o, PZ_PREV) && OB8(o, PZ_NOW)) {
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 8);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
        SET8(o, OBJ_frame, 1);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) - 4);
    } else if (OB8(o, PZ_PREV) && !OB8(o, PZ_NOW)) {
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 8);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) + 4);
    }
}

static void prize(uint32_t o)
{
    static const int16_t places[6][2] = { { 0, 0 }, { -0x40, 0x40 }, { -0x80, 0x80 }, { 0x40, -0x40 }, { 0x60, -0x80 }, { 0x80, -0x80 } };
    static const int8_t rain[21] = { -32, 0, 32, 64, 96, 127, 96, 64, 32, 0, -32, -32, 0, 32, 64, 96, 127, 96, 64, 32, 0 };
    uint32_t m = OB16(o, PZ_FIRST) ? ram(OB16(o, PZ_FIRST)) : o;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        art(o, SCDA_TTZ_PRIZE, 3);
        dims(o, 0, 0x0E, 8);
        if (OB8(o, PZ_INDEX)) break;
        SET16(o, PZ_X0, OB16(o, OBJ_x_pos));
        SET16(o, PZ_Y0, OB16(o, OBJ_y_pos));
        SET16(o, PZ_FIRST, (uint16_t)o);
        for (int k = 1; k <= 5; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) continue;
            SET32(a1, OBJ_code, CD_CODE(0x3F));
            SET16(a1, PZ_X0, OB16(o, PZ_X0));
            SET16(a1, PZ_FIRST, (uint16_t)o);
            SET8(a1, PZ_INDEX, k);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + places[k][0]);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + places[k][1]);
            SET16(a1, PZ_Y0, OB16(a1, OBJ_y_pos));
        }
        break;
    case 2:                                                   /* a button till one is stood on, 16 frames on */
        prize_switch(o, m);
        if (!OB16(m, PZ_HIT)) break;
        if (!OB8(o, PZ_SOUND)) {
            o_sfx_cd(FM_SWITCH);
            SET8(o, PZ_SOUND, 0xFF);
        }
        SET8(o, PZ_COUNT, OB8(o, PZ_COUNT) + 0x10);
        if (OB8(o, PZ_COUNT)) break;
        SET8(o, OBJ_routine, 4);
        cd_get_off(o);
        SET16(o, PZ_T1, 0);
        SET8(o, OBJ_frame, 2);
        break;
    default: {                                                /* the prize */
        int mine = OB16(m, PZ_HIT) == (uint16_t)o;
        switch (OB8(m, PZ_PRIZE) & 3) {
        case 0:                                               /* the others: spikes (going in and out) */
            if (OB8(o, PZ_T1)) { o_delete(o); return; }
            if (!mine && OB8(o, PZ_INDEX) != 4) {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, CD_CODE(0x22));
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, 0x36, OB16(o, PZ_X0));          /* (the spikes' place: oVar36) */
                    SET16(a1, OBJ_y_pos, OB16(o, PZ_Y0));
                }
            }
            SET8(o, PZ_T1, OB8(o, PZ_T1) + 1);
            break;
        case 1:                                               /* the others: 1000 each */
            if (OB8(o, PZ_T1)) { o_delete(o); return; }
            if (!mine) {
                D(0) = 100;
                tri_jsr(S3K_HUD_AddToScore);
                o_score(o, 3);                                /* (S3K's 1000 is CD's tensu4) */
            }
            SET8(o, PZ_T1, OB8(o, PZ_T1) + 1);
            break;
        case 2:                                               /* the others: a big bubble every 64 frames, twice */
            if (mine) {
                SET8(o, PZ_T1, OB8(o, PZ_T1) + 1);
                if (!OB8(o, PZ_T1)) { o_delete(o); return; }
                break;
            }
            SET8(o, PZ_T2, OB8(o, PZ_T2) + 4);
            if (OB8(o, PZ_T2)) break;
            {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, S3K_Obj_Bubbler);     /* (CD's $20, subtype 2: S3K's big bubble) */
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, PZ_Y0));
                    SET8(a1, OBJ_subtype, 2);
                }
            }
            SET8(o, PZ_T1, OB8(o, PZ_T1) + 1);
            if (OB8(o, PZ_T1) >= 2) { o_delete(o); return; }
            break;
        default:                                              /* the first: 21 rings from 384 px up */
            if (m != o) { o_delete(o); return; }
            SET8(o, PZ_T1, OB8(o, PZ_T1) + 0x10);
            if (OB8(o, PZ_T1)) break;
            if (OB8(o, PZ_T2) >= 21) { o_delete(o); return; }
            {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, CD_CODE(0x40));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x180);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + rain[OB8(o, PZ_T2)]);
                }
            }
            SET8(o, PZ_T2, OB8(o, PZ_T2) + 1);
            break;
        }
        break;
    }
    }
    if (cd_despawn(o, OB16(o, PZ_X0))) return;
    o_draw(o);
}

/* $40: a ring falling, bouncing (the floor from its 49th frame), its spin
 * slowing; 255 frames. S3K's ring art; S3K's touch collects it (routine 4) */
#define RG_SPIN         0x36               /* oVar36 */
#define RG_SPEED        0x38               /* oVar38 */
#define RG_LIFE         0x3A               /* oVar3A */
#define RG_WAIT         0x3B               /* oVar3B */
static void prize_ring(uint32_t o)
{
    static const uint8_t sparkle[] = { 5, 4, 5, 6, 7, 0xFC };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        dims(o, 8, 8, 8);
        SET32(o, OBJ_mappings, S3K_Map_Ring);
        SET16(o, OBJ_art_tile, 0xA6BC);                       /* (make_art_tile(ArtTile_Ring,1,1); CD's $A7AE) */
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET16(o, OBJ_priority, 3 << 7);
        SET8(o, OBJ_coll_flags, 0x47);
        SET16(o, RG_SPEED, 0x800);
        SET16(o, OBJ_y_vel, 0x100);
        SET8(o, RG_LIFE, 0xFF);
        SET8(o, RG_WAIT, 0x30);
        /* fall through */
    case 2: {
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        int floor = 1;
        if (OB8(o, RG_WAIT)) {
            SET8(o, RG_WAIT, OB8(o, RG_WAIT) - 1);
            floor = !OB8(o, RG_WAIT);
        }
        if (floor) {
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) {                                     /* (a bounce, losing a quarter) */
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                int16_t v = OBS16(o, OBJ_y_vel);
                SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)(v - (v >> 2)));
            }
        }
        uint16_t spin = (uint16_t)(OB16(o, RG_SPIN) + OB16(o, RG_SPEED));
        SET16(o, RG_SPIN, spin);
        SET8(o, OBJ_frame, (spin >> 12) & 3);
        SET16(o, RG_SPEED, OB16(o, RG_SPEED) - 8);
        SET8(o, RG_LIFE, OB8(o, RG_LIFE) - 1);
        if (!OB8(o, RG_LIFE) || (uint16_t)(r16(RAM_Camera_max_Y_pos) + 0xE0) < OB16(o, OBJ_y_pos)) {
            o_delete(o);
            return;
        }
        o_touch(o);
        o_draw(o);
        return;
    }
    case 4:                                                   /* collected */
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_coll_flags, 0);
        SET16(o, OBJ_priority, 1 << 7);
        tri_jsr(S3K_GiveRing);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
        /* fall through */
    case 6: {
        int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);      /* (its sparkle, then gone: $FC) */
        if (t >= 0) SET8(o, OBJ_anim_timer, t);
        else {
            unsigned i = OB8(o, OBJ_anim_frame);
            SET8(o, OBJ_anim_timer, sparkle[0]);
            if (sparkle[1 + i] == 0xFC) { o_delete(o); return; }
            SET8(o, OBJ_frame, sparkle[1 + i]);
            SET8(o, OBJ_anim_frame, i + 1);
        }
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
        return;
    }
}

/* ---- $42 water jet (act 2 present); subtype $FF: a jet -------------------- */
#define JT_TIME         0x40               /* oVar2A */
#define JT_VX           0x42               /* oVar2C (long) */
#define JT_X            0x30               /* oVar30: where its jets start */
static void water_jet(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* a jet */
        uint32_t p = RAM_Player_1;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x0C) < 0x18 &&
            (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x14) < 0x28)
            SET16(p, OBJ_ground_vel, (uint16_t)(r32(o + JT_VX) >> 8));
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + JT_VX));
        o_animate(o, o_asset_anim(SCDA_TTZ_JET));
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
        o_draw(o);
        return;
    }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        art(o, SCDA_TTZ_NOZZLE, 3);
        dims(o, 8, 8, 0x10);
        SET32(o, JT_VX, (uint32_t)-0x30000);
        int d6 = 0x20;
        if (OB8(o, OBJ_subtype)) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
            SET32(o, JT_VX, 0x30000);
            d6 = -d6;
        }
        SET16(o, JT_X, OB16(o, OBJ_x_pos) + d6);
    }
    SET16(o, JT_TIME, OB16(o, JT_TIME) - 1);
    if (OBS16(o, JT_TIME) < 0) {
        SET16(o, JT_TIME, 30);
        uint32_t a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, CD_CODE(0x42));
            SET8(a1, OBJ_subtype, 0xFF);
            SET16(a1, OBJ_x_pos, OB16(o, JT_X));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
            SET32(a1, JT_VX, r32(o + JT_VX));
            o_asset(a1, SCDA_TTZ_JET);
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET16(a1, OBJ_priority, OB16(o, OBJ_priority) + 0x80);
            dims(a1, 0x20, 0x20, 6);
            SET8(a1, OBJ_status, OB8(o, OBJ_status));
            SET8(a1, OBJ_routine, 2);
        }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

void scd_ttz_gim_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x30] = elevator;
    t[base + 0x31] = lift_button;
    t[base + 0x32] = crusher;
    t[base + 0x33] = walls;
    t[base + 0x35] = screw;
    t[base + 0x36] = chain_ball;
    t[base + 0x37] = still_ball;
    t[base + 0x38] = four_balls;
    t[base + 0x3F] = prize;
    t[base + 0x40] = prize_ring;
    t[base + 0x42] = water_jet;
}
