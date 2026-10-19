/*
 * s2_obj_cpz.c — Sonic 2 Chemical Plant objects, transcribed from s2disasm.
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $30+ here.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define P_OBJCTRL       0x2E
#define P_JUMPING       0x40

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

/* ------------------------------------------------------------------------
 * Object 1E - CPZ spin tube. Each player has a 10-byte state (S2 a4):
 *  +0 mode, +1 path/direction, +2 frames to next waypoint, +4 bytes of path
 *  left, +6 pointer to the next waypoint (in the bank copy of the paths).
 * ---------------------------------------------------------------------- */
#define TB_P1     0x30                /* objoff_2C */
#define TB_P2     0x3A                /* objoff_36 */
#define TB_RANGE  0x44                /* objoff_2A */
#define TUBE_MAIN 0x508               /* off_22E88 - off_22980 */

/* loc_22902: aim the player at (d4,d5) at speed d2 along the major axis */
static void tube_aim(uint32_t s, uint32_t p, int16_t d4, int16_t d5, int16_t d2)
{
    int16_t d3 = d2;
    int32_t dx = (int16_t)(d4 - OB16(p, OBJ_x_pos)), dy = (int16_t)(d5 - OB16(p, OBJ_y_pos));
    uint16_t ax = (uint16_t)dx, ay = (uint16_t)dy;
    if (dx < 0) { ax = (uint16_t)-dx; d2 = (int16_t)-d2; }
    if (dy < 0) { ay = (uint16_t)-dy; d3 = (int16_t)-d3; }
    int16_t q;
    if (ay >= ax) {                                     /* vertical major */
        q = (int16_t)(((int32_t)dy << 16) / d3);
        int16_t xv = dx ? (int16_t)(((int32_t)dx << 16) / q) : 0;
        SET16(p, OBJ_x_vel, (uint16_t)xv);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
    } else {
        q = (int16_t)(((int32_t)dx << 16) / d2);
        int16_t yv = dy ? (int16_t)(((int32_t)dy << 16) / q) : 0;
        SET16(p, OBJ_y_vel, (uint16_t)yv);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
    }
    if (q < 0) q = (int16_t)-q;
    w16(s + 2, (uint16_t)q);                            /* high byte = frames */
}

static void tube_move(uint32_t p)                       /* Obj1E_MoveCharacter */
{
    w32(p + OBJ_x_pos, r32(p + OBJ_x_pos) + ((int32_t)(int16_t)OB16(p, OBJ_x_vel) << 8));
    w32(p + OBJ_y_pos, r32(p + OBJ_y_pos) + ((int32_t)(int16_t)OB16(p, OBJ_y_vel) << 8));
}

static const int8_t tube_next[64] = {
     2,  1, 0, 0,  -1,  3, 0, 0,   4, -2, 0, 0,  -3, -4, 0, 0,
    -5, -5, 0, 0,   7,  6, 0, 0,  -7, -6, 0, 0,   8,  9, 0, 0,
    -8, -9, 0, 0,  11, 10, 0, 0,  12,  0, 0, 0, -11,-10, 0, 0,
   -12,  0, 0, 0,   0, 13, 0, 0, -13, 14, 0, 0,   0,-14, 0, 0,
};
static const uint8_t tube_branch[16] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 0, 1, 2, 1 };

static void tube_start_main(uint32_t s, uint32_t p, int8_t d0)  /* loc_22892 */
{
    uint32_t base = blob(TRI_BLOB_S2_TUBES) + TUBE_MAIN, a2;
    if (d0 < 0) {
        d0 = (int8_t)-d0;
        w8(s + 1, (uint8_t)-4);
        a2 = base + tri_rd16(base + (uint32_t)d0 * 2u);
        uint16_t n = (uint16_t)(tri_rd16(a2) - 4);
        a2 += 2;
        w16(s + 4, n);
        a2 += n;
        SET16(p, OBJ_x_pos, tri_rd16(a2));
        SET16(p, OBJ_y_pos, tri_rd16(a2 + 2));
        a2 += 4 - 8;
    } else {
        a2 = base + tri_rd16(base + (uint32_t)d0 * 2u);
        w16(s + 4, (uint16_t)(tri_rd16(a2) - 4));
        SET16(p, OBJ_x_pos, tri_rd16(a2 + 2));
        SET16(p, OBJ_y_pos, tri_rd16(a2 + 4));
        a2 += 6;
    }
    w32(s + 6, a2);                                     /* loc_228E4 */
    tube_aim(s, p, (int16_t)tri_rd16(a2), (int16_t)tri_rd16(a2 + 2), 0x800);
    o_sfx_s2(0xBE);                                        /* SndID_Roll */
    w8(s, r8(s) + 2);
}

static void tube_player(uint32_t o, uint32_t p, uint32_t s)
{
    if (!o_player_active(p)) return;
    int16_t ox = (int16_t)OB16(o, OBJ_x_pos), oy = (int16_t)OB16(o, OBJ_y_pos);
    switch (r8(s)) {
    case 0: {                                           /* loc_225FC: entry */
        if (r16(RAM_Debug_placement_mode)) return;
        uint16_t d2 = OB16(o, TB_RANGE);
        uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - ox);
        if (d0 >= d2) return;
        uint16_t d1 = (uint16_t)(OB16(p, OBJ_y_pos) - oy);
        if (d1 >= 0x80) return;
        if (OB8(p, OBJ_anim) == 0x20) return;
        int d3 = 0;
        if (d2 != 0xA0) {
            d3 = 8;
            if (d2 != 0x120) { d3 = 4; d0 = (uint16_t)(-(int16_t)d0 + 0x100); }
        }
        int dir;
        if (d0 >= 0x80) {
            dir = tube_branch[(OB8(o, OBJ_subtype) >> 2) & 0x0F];
            if (dir == 2) dir = r8(RAM_Timer + 2) & 1;
        } else dir = d1 >= 0x40 ? 2 : 3;
        w8(s + 1, (uint8_t)dir);
        uint32_t base = blob(TRI_BLOB_S2_TUBES);
        uint32_t a2 = base + tri_rd16(base + (uint32_t)(((dir + d3) * 2) & 0x1E));
        w16(s + 4, (uint16_t)(tri_rd16(a2) - 4));
        SET16(p, OBJ_x_pos, (uint16_t)(tri_rd16(a2 + 2) + ox));
        SET16(p, OBJ_y_pos, (uint16_t)(tri_rd16(a2 + 4) + oy));
        a2 += 6;
        w32(s + 6, a2);
        int16_t d4 = (int16_t)(tri_rd16(a2) + ox), d5 = (int16_t)(tri_rd16(a2 + 2) + oy);
        w8(s, 2);
        SET8(p, OBJ_routine, 2);                        /* (fixBugs: not stuck hurt in a pipe) */
        SET8(p, P_OBJCTRL, 0x81);
        SET8(p, OBJ_anim, 2);                           /* roll */
        SET16(p, OBJ_ground_vel, 0x800);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | 2);
        SET8(p, P_JUMPING, 0);
        SET16(p, OBJ_art_tile, OB16(p, OBJ_art_tile) & 0x7FFF);
        tube_aim(s, p, d4, d5, 0x800);
        o_sfx_s2(0xBE);                                    /* SndID_Roll */
        return;
    }
    case 2: {                                           /* loc_2271A: entry/exit path (relative) */
        w8(s + 2, r8(s + 2) - 1);
        if ((int8_t)r8(s + 2) >= 0) { tube_move(p); return; }
        uint32_t a2 = r32(s + 6);
        SET16(p, OBJ_x_pos, (uint16_t)(tri_rd16(a2) + ox));
        SET16(p, OBJ_y_pos, (uint16_t)(tri_rd16(a2 + 2) + oy));
        a2 += 4;
        if ((int8_t)r8(s + 1) < 0) a2 -= 8;
        w32(s + 6, a2);
        w16(s + 4, (uint16_t)(r16(s + 4) - 4));
        if (r16(s + 4)) {
            tube_aim(s, p, (int16_t)(tri_rd16(a2) + ox), (int16_t)(tri_rd16(a2 + 2) + oy), 0x800);
            return;
        }
        if (r8(s + 1) < 4) {                            /* loc_22784: into the main tube? */
            int i = (OB8(o, OBJ_subtype) & 0xFC) + r8(s + 1);
            w8(s + 1, 4);
            int8_t d0 = tube_next[i & 63];
            if (d0) { tube_start_main(s, p, d0); return; }
        }
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);   /* out */
        w8(s, 6);
        SET8(p, P_OBJCTRL, 0);
        o_sfx_s2(0xBC);                                    /* SndID_SpindashRelease */
        return;
    }
    case 4: {                                           /* loc_227FE: main tube (absolute) */
        w8(s + 2, r8(s + 2) - 1);
        if ((int8_t)r8(s + 2) >= 0) { tube_move(p); return; }
        uint32_t a2 = r32(s + 6);
        SET16(p, OBJ_x_pos, tri_rd16(a2));
        SET16(p, OBJ_y_pos, tri_rd16(a2 + 2));
        a2 += 4;
        if ((int8_t)r8(s + 1) < 0) a2 -= 8;
        w32(s + 6, a2);
        w16(s + 4, (uint16_t)(r16(s + 4) - 4));
        if (r16(s + 4)) {
            tube_aim(s, p, (int16_t)tri_rd16(a2), (int16_t)tri_rd16(a2 + 2), 0x800);
            return;
        }
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
        w8(s, 0);
        o_sfx_s2(0xBC);                                    /* SndID_SpindashRelease */
        return;
    }
    default: {                                          /* loc_2286A: wait to leave the range */
        uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - ox);
        uint16_t d1 = (uint16_t)(OB16(p, OBJ_y_pos) - oy);
        if (d0 >= OB16(o, TB_RANGE) || d1 >= 0x80) w8(s, 0);
        return;
    }
    }
}

static void spin_tube(uint32_t o)
{
    static const uint16_t range[3] = { 0xA0, 0x100, 0x120 };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET16(o, TB_RANGE, range[(OB8(o, OBJ_subtype) & 3) % 3]);
    }
    tube_player(o, RAM_Player_1, o + TB_P1);
    tube_player(o, RAM_Player_2, o + TB_P2);
    if (!(r8(o + TB_P1) + r8(o + TB_P2)) && o_out_of_range(o, OB16(o, OBJ_x_pos)))   /* MarkObjGone3 */
        o_delete_respawnable(o);
}


/* S2 Oscillating_Data+off: S3K's table keeps the control word in front */
static uint8_t osc8(int off) { return o_osc(off + 2); }
static uint16_t osc16(int off) { return r16(RAM_Oscillating_table + 2u + (uint32_t)off); }

/* ------------------------------------------------------------------------
 * Object 6B - CPZ blocks / stair blocks (MTZ "immobile platform")
 * ---------------------------------------------------------------------- */
#define B6_FLIP  0x30                 /* obj6B_base_flip */
#define B6_Y0    0x32                 /* obj6B_y_origin */
#define B6_X0    0x34                 /* obj6B_x_origin */
#define B6_ACC   0x36                 /* obj6B_acceleration */

static void block_move(uint32_t o)                      /* Obj6B_Types */
{
    int type = OB8(o, OBJ_subtype) & 0x0F, flip = OB8(o, OBJ_status) & 1;
    int16_t d0, d1;
    switch (type) {
    case 1: case 2: case 3: case 4: {                   /* horizontal / vertical, short / long */
        int lng = type == 2 || type == 4;
        d1 = lng ? 0x80 : 0x40;
        d0 = osc8(lng ? 0x1C : 8);
        if (flip) d0 = (int16_t)(-d0 + d1);
        if (type <= 2) SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, B6_X0) - d0));
        else SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, B6_Y0) - d0));
        return;
    }
    case 5:                                             /* sinking: bobbing until stood on */
        SET16(o, OBJ_y_pos, (uint16_t)((osc8(0) >> 1) + OB16(o, B6_Y0)));
        if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
        return;
    case 6:                                             /* sink */
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + ((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8));
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 8);
        if ((uint16_t)(r16(RAM_Camera_max_Y_pos) + 224) < OB16(o, OBJ_y_pos)) SET8(o, OBJ_subtype, 0);
        return;
    case 7:                                             /* descend */
        if (!OB8(o, B6_ACC)) {
            if (!(OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND))) return;
            SET8(o, B6_ACC, 8);
        }
        o_move(o);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) & 0x7FF);
        if (OB16(o, OBJ_y_vel) == 0x2A8) SET8(o, B6_ACC, (uint8_t)-(int8_t)OB8(o, B6_ACC));
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (int8_t)OB8(o, B6_ACC));
        if (!OB16(o, OBJ_y_vel)) SET8(o, OBJ_subtype, 0);
        return;
    case 8: case 9: case 0x0A: case 0x0B: {             /* stairs round a square */
        static const uint8_t rad[4] = { 0x10, 0x30, 0x50, 0x70 };
        int k = type - 8;
        d1 = rad[k];
        d0 = osc8(0x28 + k * 4);
        if (!k) d0 = (int16_t)(d0 >> 1);
        uint16_t d3 = osc16(0x2A + k * 4);
        if (!d3) SET8(o, B6_FLIP, (OB8(o, B6_FLIP) + 1) & 3);
        int d2 = OB8(o, B6_FLIP) & 3;
        int16_t x0 = (int16_t)OB16(o, B6_X0), y0 = (int16_t)OB16(o, B6_Y0);
        switch (d2) {
        case 0: SET16(o, OBJ_x_pos, (uint16_t)(d0 - d1 + x0)); SET16(o, OBJ_y_pos, (uint16_t)(y0 - d1)); break;
        case 1: SET16(o, OBJ_y_pos, (uint16_t)(-(d0 - (d1 - 1)) + y0)); SET16(o, OBJ_x_pos, (uint16_t)(d1 + x0)); break;
        case 2: SET16(o, OBJ_x_pos, (uint16_t)(-(d0 - (d1 - 1)) + x0)); SET16(o, OBJ_y_pos, (uint16_t)(d1 + y0)); break;
        default: SET16(o, OBJ_y_pos, (uint16_t)(d0 - d1 + y0)); SET16(o, OBJ_x_pos, (uint16_t)(x0 - d1)); break;
        }
        return;
    }
    default:
        return;
    }
}

static void block(uint32_t o)
{
    static const uint8_t props[2][3] = { { 32, 12, 1 }, { 16, 16, 0 } };
    if (!OB8(o, OBJ_routine)) {                         /* Obj6B_Init */
        SET8(o, OBJ_routine, 2);
        int k = ((OB8(o, OBJ_subtype) >> 2) & 0x1C) >> 2;
        o_init(o, tri_zone() == TRI_S2_MTZ ? S2A_MTZBLOCK : S2A_CPZBLOCK, 4, 3, props[k & 1][0], props[k & 1][1]);
        SET8(o, OBJ_y_radius, props[k & 1][1]);
        SET8(o, OBJ_frame, props[k & 1][2]);
        SET16(o, B6_X0, OB16(o, OBJ_x_pos));
        SET16(o, B6_Y0, OB16(o, OBJ_y_pos));
        SET8(o, B6_FLIP, OB8(o, OBJ_status));
        int t = (OB8(o, OBJ_subtype) & 0x0F) - 8;
        if (t >= 0 && (int16_t)osc16(0x2A + t * 4) < 0) SET8(o, B6_FLIP, OB8(o, B6_FLIP) ^ 1);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    block_move(o);
    if (OB8(o, OBJ_render) & 0x80)
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, oldx);
    if (o_out_of_range(o, OB16(o, B6_X0))) { o_delete_respawnable(o); return; }   /* MarkObjGone2 */
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 19 - CPZ elevator platforms
 * ---------------------------------------------------------------------- */
#define EL_X0  0x30
#define EL_Y0  0x32

static void elevator_move(uint32_t o)                   /* Obj19_Move */
{
    int type = OB8(o, OBJ_subtype) & 0x0F, flip = OB8(o, OBJ_status) & 1;
    int16_t d0, d1;
    switch (type) {
    case 0: case 1:
        d0 = osc8(type ? 0x0C : 8);
        d1 = type ? 0x60 : 0x40;
        if (flip) d0 = (int16_t)(-d0 + d1);
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, EL_X0) - d0));
        return;
    case 2:
        d0 = osc8(0x1C);
        if (flip) d0 = (int16_t)(-d0 + 0x80);
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, EL_Y0) - d0));
        return;
    case 3:
        if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
        return;
    case 4: case 6: case 7: {
        o_move(o);
        int16_t acc = (uint16_t)(OB16(o, EL_Y0) - 0x60) >= OB16(o, OBJ_y_pos) ? 8 : -8;
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + acc);
        if (type == 4 && !OB16(o, OBJ_y_vel)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
        return;
    }
    case 5:
        return;
    default: {                                          /* 8-$F: circling */
        int16_t x = (int8_t)(osc8(0x38) - 0x40), y = (int8_t)(osc8(0x3C) - 0x40);
        int d0b = type * 2;
        if (d0b & 4) { x = (int16_t)-x; y = (int16_t)-y; }
        if (d0b & 2) { int16_t t = (int16_t)-x; x = y; y = t; }
        if (type >= 0x0C) x = (int16_t)-x;
        SET16(o, OBJ_x_pos, (uint16_t)(x + OB16(o, EL_X0)));
        SET16(o, OBJ_y_pos, (uint16_t)(y + OB16(o, EL_Y0)));
        return;
    }
    }
}

static void elevator(uint32_t o)
{
    static const uint8_t props[4][2] = { { 0x20, 0 }, { 0x18, 1 }, { 0x40, 2 }, { 0x20, 3 } };
    if (!OB8(o, OBJ_routine)) {                         /* Obj19_Init */
        SET8(o, OBJ_routine, 2);
        int k = ((OB8(o, OBJ_subtype) >> 3) & 0x1E) >> 1;
        o_init(o, tri_zone() == TRI_S2_OOZ ? S2A_OOZELEV : tri_zone() == TRI_S2_WFZ ? S2A_WFZFLOAT : S2A_ELEVATOR,
               4, 4, props[k & 3][0], 0x10);
        if (tri_zone() == TRI_S2_WFZ) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_frame, props[k & 3][1]);
        SET16(o, EL_X0, OB16(o, OBJ_x_pos));
        SET16(o, EL_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
        int t = OB8(o, OBJ_subtype);
        if ((t == 3 && (OB8(o, OBJ_status) & 1)) || t == 7) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0xC0);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    elevator_move(o);
    o_platform(o, OB8(o, OBJ_width_px), 0x11, oldx);
    if (o_out_of_range(o, OB16(o, EL_X0))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 1B - speed booster
 * ---------------------------------------------------------------------- */
#define SB_SPEED   0x30
#define P_MOVELOCK 0x32

static void booster_give(uint32_t o, uint32_t p)       /* Obj1B_GiveBoost */
{
    int16_t d0 = (int16_t)OB16(p, OBJ_x_vel);
    if (OB8(o, OBJ_status) & 1) d0 = (int16_t)-d0;
    if (d0 < 0x1000) {
        SET16(p, OBJ_x_vel, OB16(o, SB_SPEED));
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
        if (OB8(o, OBJ_status) & 1) {
            SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
            SET16(p, OBJ_x_vel, (uint16_t)-(int16_t)OB16(p, OBJ_x_vel));
        }
        SET16(p, P_MOVELOCK, 0x0F);
        SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_PUSH | ST_P2_PUSH));
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~0x20);
    }
    o_sfx_s2(0xCC);                                        /* SndID_Spring */
}

static void booster(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_BOOSTER, OB8(o, OBJ_render) | 4, 1, 0x20, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET16(o, SB_SPEED, (OB8(o, OBJ_subtype) & 2) ? 0xA00 : 0x1000);
    }
    SET8(o, OBJ_frame, r8(RAM_Level_frame_counter + 1) & 2);
    uint16_t x0 = (uint16_t)(OB16(o, OBJ_x_pos) - 0x10), x1 = (uint16_t)(OB16(o, OBJ_x_pos) + 0x10);
    uint16_t y0 = (uint16_t)(OB16(o, OBJ_y_pos) - 0x10), y1 = (uint16_t)(OB16(o, OBJ_y_pos) + 0x10);
    for (int i = 0; i < 2; i++) {
        uint32_t p = o_player(i);
        if (!o_player_active(p) || (OB8(p, OBJ_status) & 2)) continue;
        uint16_t px = OB16(p, OBJ_x_pos), py = OB16(p, OBJ_y_pos);
        if (px >= x0 && px < x1 && py >= y0 && py < y1) booster_give(o, p);
    }
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Objects A5/A6 - Spiny (on floor / on wall). S2 writes the move timer and
 * the no-look delay as one word ($80 -> timer 0, delay $80), so the first
 * turn comes after 256 frames.
 * ---------------------------------------------------------------------- */
#define SP_TIMER   0x30               /* objoff_2A */
#define SP_DELAY   0x31               /* objoff_2B */

static void spiny_shoot(uint32_t o, int wall)        /* loc_38C22 / loc_38C6E */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x98));
    SET8(a1, OBJ_routine, 2);
    o_init(a1, S2A_SPINYSHOT, 0x80 | 4, 5, 4, 4);        /* ObjA6_SubObjData */
    SET8(a1, OBJ_coll_flags, 0x98);
    SET8(a1, S2_PROJ_KIND, S2_PROJ_SPINY);
    SET8(a1, OBJ_frame, 6);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    int16_t d1;
    if (!wall) {
        SET16(a1, OBJ_y_vel, (uint16_t)-0x300);
        d1 = 0x100;
        if (OB16(o, OBJ_x_pos) >= OB16(RAM_Player_1, OBJ_x_pos)) d1 = (int16_t)-d1;
    } else {
        d1 = 0x300;
        if (OB8(o, OBJ_render) & 1) d1 = (int16_t)-d1;
    }
    SET16(a1, OBJ_x_vel, (uint16_t)d1);
}

static void spiny_common(uint32_t o, int wall)
{
    int vel = wall ? OBJ_y_vel : OBJ_x_vel;
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjA5_Init / ObjA6_Init */
        o_init(o, wall ? S2A_SPINYW : S2A_SPINY, OB8(o, OBJ_render) | 4, 4, 8, 8);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET8(o, OBJ_routine, 2);
        SET16(o, vel, (uint16_t)-0x40);
        SET16(o, SP_TIMER, 0x80);
        return;
    case 2: {
        int look = 1;
        if (OB8(o, SP_DELAY)) { SET8(o, SP_DELAY, OB8(o, SP_DELAY) - 1); look = 0; }
        if (look) {
            int d0, d1;
            int16_t d2;
            s2_orient(o, &d0, &d1, &d2, NULL);
            if ((uint16_t)(d2 + 0x60) < 0xC0) {         /* player within $60: stop and shoot */
                SET8(o, OBJ_routine, 4);
                SET8(o, SP_DELAY, 0x28);
                SET8(o, OBJ_frame, wall ? 5 : 2);
                break;
            }
        }
        SET8(o, SP_TIMER, OB8(o, SP_TIMER) - 1);
        if (!OB8(o, SP_TIMER)) {
            SET16(o, SP_TIMER, 0x80);
            SET16(o, vel, (uint16_t)-(int16_t)OB16(o, vel));
        }
        o_move(o);
        o_animate_s2(o, o_asset_anim(wall ? S2A_SPINYW : S2A_SPINY));
        break;
    }
    default:
        SET8(o, SP_DELAY, OB8(o, SP_DELAY) - 1);
        if ((int8_t)OB8(o, SP_DELAY) < 0) {
            SET8(o, OBJ_routine, 2);
            SET8(o, SP_DELAY, 0x40);
        } else if (OB8(o, SP_DELAY) == 0x14) {
            spiny_shoot(o, wall);
        }
        break;
    }
    o_touch(o);
    o_remember(o);
}

static void spiny(uint32_t o) { spiny_common(o, 0); }
static void spiny_wall(uint32_t o) { spiny_common(o, 1); }

/* ------------------------------------------------------------------------
 * Objects A7-AA - Grabber (spider badnik): body A7, legs A8 (the grabbing
 * collision, special $D7), box A9, string AA. Children keep their parent at
 * GB_PARENT; the body keeps the children at GB_STRING/GB_LEGS/GB_BOX.
 * ---------------------------------------------------------------------- */
#define GB_TIMER   0x40               /* objoff_2A (word: turn timer; bytes: flash timers) */
#define GB_TIMER2  0x41               /* objoff_2B */
#define GB_COUNT   0x42               /* objoff_2C */
#define GB_SAVEDX  0x44               /* objoff_2E */
#define GB_GRAB    0x30               /* objoff_30 */
#define GB_MASHING 0x31               /* objoff_31 */
#define GB_PLAYER  0x32               /* objoff_32 */
#define GB_CTRL    0x34               /* objoff_34 */
#define GB_LASTDIR 0x36               /* objoff_36 */
#define GB_MASHT   0x37               /* objoff_37 */
#define GB_MASHN   0x38               /* objoff_38 */
#define GB_STRING  0x3A               /* objoff_3A */
#define GB_LEGS    0x3C               /* objoff_3C */
#define GB_BOX     0x3E               /* objoff_3E */
#define GB_PARENT  0x46               /* child: objoff_2C */
#define P_ANIM_FLOAT 0x0E
#define P_ANIM_WALK  0x00

static uint32_t ram_ptr(uint32_t o, int f) { return 0xFF0000u | OB16(o, f); }

static void grabber_child(uint32_t o, int field, uint8_t id)    /* LoadChildObject */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET16(o, field, (uint16_t)a1);
    SET32(a1, OBJ_code, S2(id));
    SET16(a1, GB_PARENT, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
}

static int grabber_alive(uint32_t o)
{
    return r32(ram_ptr(o, GB_PARENT)) == S2(0xA7);
}

static void grabber_release(uint32_t p)
{
    SET8(p, P_OBJCTRL, 0);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 2);
}

static void grabber_mash(uint32_t o)                 /* loc_390BC */
{
    uint8_t d0 = r8(ram_ptr(o, GB_CTRL) + 1) & 0x0C; /* pressed left/right */
    if (!OB8(o, GB_MASHING)) {
        if (!d0) return;
        SET8(o, GB_MASHING, 0xFF);
        SET8(o, GB_LASTDIR, d0);
        return;
    }
    SET8(o, GB_MASHT, OB8(o, GB_MASHT) - 1);
    if (OB8(o, GB_MASHT)) {
        if (d0 && d0 != OB8(o, GB_LASTDIR)) {
            SET8(o, GB_LASTDIR, d0);
            SET8(o, GB_MASHN, OB8(o, GB_MASHN) + 1);
        }
        return;
    }
    if (OB8(o, GB_MASHN) >= 4) {                        /* broke free */
        SET8(o, OBJ_2nd, 0x0A);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_coll_flags, 0);
        uint32_t p = ram_ptr(o, GB_PLAYER);
        grabber_release(p);
        SET8(p, OBJ_anim, P_ANIM_WALK);
        SET16(o, GB_PLAYER, 0);
    }
    SET8(o, GB_MASHT, 0x20);
    SET8(o, GB_MASHING, 0);
    SET8(o, GB_MASHN, 0);
}

static int grabber_explode(uint32_t o)               /* ObjA7_CheckExplode: 1 = poofed */
{
    SET8(o, GB_TIMER, OB8(o, GB_TIMER) - 1);
    if (OB8(o, GB_TIMER)) return 0;
    SET8(o, GB_TIMER, OB8(o, GB_TIMER2));
    SET8(o, GB_TIMER2, OB8(o, GB_TIMER2) - 1);
    if (OB8(o, GB_TIMER2)) {
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) ^ 0x2000);
        return 0;
    }
    /* ObjA7_Poof: become an explosion (no animal) */
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 2);
    SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x2000);
    if (OB16(o, GB_PLAYER)) {
        grabber_release(ram_ptr(o, GB_PLAYER));
        SET8(o, OBJ_coll_flags, 0x0B);
    }
    return 1;
}

static void grabber(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* ObjA7_Init */
        o_init(o, S2A_GRABBER, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_render) & 1) ? 0x40 : (uint16_t)-0x40);
        SET16(o, GB_TIMER, 0xFF);
        grabber_child(o, GB_BOX, 0xA9);
        grabber_child(o, GB_LEGS, 0xA8);
        grabber_child(o, GB_STRING, 0xAA);
        return;
    }
    switch (OB8(o, OBJ_2nd)) {                          /* ObjA7_Main */
    case 0: {                                           /* patrol */
        int d0, d1;
        int16_t d2, d3;
        s2_orient(o, &d0, &d1, &d2, &d3);
        if ((uint16_t)(d2 + 0x40) < 0x80 && (uint16_t)d3 > (uint16_t)-0x80) {
            SET8(o, OBJ_2nd, 2);                        /* loc_38E84: player just below - stop */
            SET16(o, GB_SAVEDX, OB16(o, OBJ_x_vel));
            SET16(o, OBJ_x_vel, 0);
            SET8(o, GB_COUNT, 0x10);
            break;
        }
        SET16(o, GB_TIMER, OB16(o, GB_TIMER) - 1);
        if ((int16_t)OB16(o, GB_TIMER) < 0) {
            SET16(o, GB_TIMER, 0xFF);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        }
        break;
    }
    case 2:                                             /* pause before dropping */
        SET8(o, GB_COUNT, OB8(o, GB_COUNT) - 1);
        if ((int8_t)OB8(o, GB_COUNT) < 0) {
            SET8(o, OBJ_2nd, 4);
            SET16(o, OBJ_y_vel, 0x200);
            SET8(o, GB_COUNT, 0x40);
        }
        break;
    case 4:                                             /* drop and rise */
        if (OB8(o, GB_GRAB)) {                          /* ObjA7_GrabCharacter */
            SET8(o, OBJ_2nd, 6);
            uint32_t p = ram_ptr(o, GB_PLAYER);
            SET8(p, P_OBJCTRL, 0x81);
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, 0);
            SET8(p, OBJ_anim, P_ANIM_FLOAT);
            SET8(p, 0x3D, 0);                           /* (fixBugs: drop a charging spin dash) */
            SET8(o, OBJ_frame, 1);
            if ((int16_t)OB16(o, OBJ_y_vel) >= 0) {     /* still dropping: rise for as long as it fell */
                SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)OB16(o, OBJ_y_vel));
                SET8(o, GB_COUNT, (uint8_t)(-(uint8_t)(OB8(o, GB_COUNT) - 0x40) + 1));
            }
            SET8(o, GB_TIMER, 1);
            SET8(o, GB_TIMER2, 0x10);
            SET8(o, GB_MASHT, 0x20);
            break;
        }
        SET8(o, GB_COUNT, OB8(o, GB_COUNT) - 1);
        if (!OB8(o, GB_COUNT)) {
            SET8(o, OBJ_2nd, 0);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, OBJ_x_vel, OB16(o, GB_SAVEDX));
            SET8(o, OBJ_frame, 0);
            break;
        }
        if (OB8(o, GB_COUNT) == 0x20) SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)OB16(o, OBJ_y_vel));
        o_animate_s2(o, o_asset_anim(S2A_GRABBER));
        break;
    case 6:                                             /* rising with the player */
        if (grabber_explode(o)) return;
        grabber_mash(o);
        SET8(o, GB_COUNT, OB8(o, GB_COUNT) - 1);
        if (!OB8(o, GB_COUNT)) {
            SET8(o, OBJ_2nd, 8);
            SET16(o, OBJ_y_vel, 0);
        }
        break;
    case 8:                                             /* holding */
        if (grabber_explode(o)) return;
        grabber_mash(o);
        break;
    default:                                            /* let go: count down to the poof */
        if (grabber_explode(o)) return;
        break;
    }
    o_move(o);
    uint32_t legs = ram_ptr(o, GB_LEGS);                /* Obj_AlignChildXY (0,$10) */
    SET16(legs, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(legs, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
    SET16(ram_ptr(o, GB_BOX), OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(ram_ptr(o, GB_STRING), OBJ_x_pos, OB16(o, OBJ_x_pos));
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) {        /* loc_39182 */
        uint16_t r = OB16(o, OBJ_respawn);
        if (r) w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
        if (OB8(o, GB_GRAB) && OB16(o, GB_PLAYER)) grabber_release(ram_ptr(o, GB_PLAYER));
        o_delete(ram_ptr(o, GB_STRING));
        o_delete(ram_ptr(o, GB_LEGS));
        o_delete(ram_ptr(o, GB_BOX));
        o_delete(o);
        return;
    }
    o_touch(o);
    o_draw(o);
}

static const uint8_t gb_who[4] = { 0, 0, 1, 0 };      /* word_38FE0-6: 1 P1, 2 P2, 3 both -> P1 */

static void grabber_legs(uint32_t o)                 /* ObjA8 */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, S2A_GRABBER, OB8(o, OBJ_render) | 4, 1, 0x10, 0x10);   /* ObjA7_SubObjData2 */
        SET8(o, OBJ_coll_flags, 0xD7);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 3);
        return;
    case 2: {
        if (!grabber_alive(o)) { o_delete(o); return; }
        uint32_t a1 = ram_ptr(o, GB_PARENT);            /* InheritParentXYFlip */
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | (OB8(a1, OBJ_render) & 3));
        SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~3) | (OB8(a1, OBJ_render) & 3));
        SET8(o, OBJ_frame, OB8(a1, OBJ_frame) + 3);
        uint8_t d0 = OB8(o, OBJ_coll_prop);
        if (d0) {
            SET8(o, OBJ_coll_prop, 0);
            if (OB8(a1, OBJ_2nd) == 4 && (d0 & 3)) {
                d0 &= 3;
                SET8(o, OBJ_coll_flags, 0);
                SET8(o, OBJ_routine, 4);
                SET8(a1, GB_GRAB, 0xFF);
                int who = gb_who[d0];
                SET16(a1, GB_PLAYER, (uint16_t)(who ? RAM_Player_2 : RAM_Player_1));
                SET16(a1, GB_CTRL, (uint16_t)(who ? 0xF606u : 0xF604u));   /* Ctrl_2 / Ctrl_1 (raw pad) */
            }
        }
        if (OB8(o, OBJ_coll_flags)) o_touch(o);
        o_draw(o);
        return;
    }
    case 4: {                                           /* carrying the player */
        uint32_t a1 = ram_ptr(o, GB_PARENT);
        uint16_t pw = OB16(a1, GB_PLAYER);
        if (!grabber_alive(o)) {
            if (pw) grabber_release(0xFF0000u | pw);
            o_delete(o);
            return;
        }
        if (!pw) { SET8(o, OBJ_routine, 6); o_draw(o); return; }
        SET16(0xFF0000u | pw, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(0xFF0000u | pw, OBJ_y_pos, OB16(o, OBJ_y_pos));
        o_draw(o);
        return;
    }
    default:
        if (!grabber_alive(o)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
}

static void grabber_box(uint32_t o)                  /* ObjA9 */
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_GRABBER, OB8(o, OBJ_render) | 4, 4, 4, 4);      /* ObjA8_SubObjData */
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 2);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x0C);
        return;
    }
    if (!grabber_alive(o)) { o_delete(o); return; }
    o_draw(o);
}

static void grabber_string(uint32_t o)               /* ObjAA */
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_GRABSTR, OB8(o, OBJ_render) | 4, 5, 4, 0x40);   /* ObjA8_SubObjData2 */
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 8);
        return;
    }
    if (!grabber_alive(o)) { o_delete(o); return; }
    int16_t d0 = (int16_t)(OB16(ram_ptr(o, GB_PARENT), OBJ_y_pos) - OB16(o, OBJ_y_pos));
    if (d0 >= 0) SET8(o, OBJ_frame, (uint16_t)d0 >> 4);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 0B - section of pipe that tips you off
 * ---------------------------------------------------------------------- */
#define TF_CUR    0x30                /* obj0B_duration_current */
#define TF_INIT   0x32                /* obj0B_duration_initial */
#define TF_DELAY  0x36                /* obj0B_delay */

static void tip_floor(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj0B_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_TIPFLOOR, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        uint16_t d0 = (uint16_t)((OB8(o, OBJ_subtype) & 0xF0) + 0x10 - 1);
        SET16(o, TF_CUR, d0);
        SET16(o, TF_INIT, d0);
        SET8(o, TF_DELAY, ((OB8(o, OBJ_subtype) & 0x0F) + 1) << 4);
    }   /* fall through */
    case 2:                                             /* loc_20104: wait for the global phase */
        if ((uint8_t)(r8(RAM_Level_frame_counter + 1) + OB8(o, TF_DELAY))) break;
        SET8(o, OBJ_routine, 4);
        /* fall through */
    default:                                            /* loc_20112 */
        SET16(o, TF_CUR, OB16(o, TF_CUR) - 1);
        if ((int16_t)OB16(o, TF_CUR) < 0) {
            SET16(o, TF_CUR, 0x7F);
            if (OB8(o, OBJ_anim)) SET16(o, TF_CUR, OB16(o, TF_INIT));
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
        }
        o_animate_s2(o, o_asset_anim(S2A_TIPFLOOR));
        break;
    }
    if (!OB8(o, OBJ_frame)) {                           /* flat: a platform */
        o_platform(o, OB8(o, OBJ_width_px), 0x11, OB16(o, OBJ_x_pos));
    } else if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) {
        for (int n = 0; n < 2; n++) {                   /* tipping: drop whoever stood on it */
            if (!(OB8(o, OBJ_status) & (ST_P1_STAND << n))) continue;
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND << n));
            uint32_t p = o_player(n);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~8) | 2);
        }
    }
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 1D - blue balls (chemical droplets) jumping out of pipes
 * ---------------------------------------------------------------------- */
#define BB_Y     0x30                 /* blueballs_y_pos */
#define BB_WAIT  0x32                 /* objoff_32 */
#define BB_YVEL  0x34                 /* blueballs_y_vel */
#define BB_XVEL  0x36                 /* blueballs_x_vel */
#define BB_X     0x38                 /* blueballs_x_pos */
#define BB_OFF   0x3A                 /* blueballs_offset */

static void gloop(uint32_t o)                        /* PlaySoundLocal(SndID_Gloop) */
{
    if (OB8(o, OBJ_render) & 0x80) o_sfx_s2(0xDA);         /* SndID_Gloop */
}

static void blue_balls(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj1D_Init */
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_y_vel, (uint16_t)-0x480);
        int d1 = OB8(o, OBJ_subtype) & 0x0F;            /* number of balls - 1 */
        int d5 = (OB8(o, OBJ_subtype) & 0xF0) ? 6 : 2;
        uint8_t d4 = OB8(o, OBJ_status);
        uint16_t d2 = 0;
        uint32_t a1 = o;
        for (int k = 0; k <= d1; k++) {
            if (k) {
                a1 = o_alloc_after(o);
                if (!a1) continue;
                SET32(a1, OBJ_code, S2(0x1D));
            }
            SET8(a1, OBJ_routine, d5);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            o_init(a1, S2A_BLUEBALL, 4, 3, 8, 8);
            SET8(a1, OBJ_coll_flags, 0x8B);
            SET16(a1, BB_X, OB16(a1, OBJ_x_pos));
            SET16(a1, BB_Y, OB16(a1, OBJ_y_pos));
            SET16(a1, OBJ_y_vel, OB16(o, OBJ_y_vel));
            SET16(a1, BB_YVEL, OB16(a1, OBJ_y_vel));
            SET16(a1, BB_OFF, 0x60);
            SET16(a1, BB_XVEL, 0x0B);
            if (d4 & 1) {
                SET16(a1, BB_XVEL, (uint16_t)-0x0B);
                SET16(a1, BB_OFF, (uint16_t)-0x60);
            }
            SET16(a1, BB_WAIT, d2);
            d2 += 3;
        }
        return;
    }
    case 2: case 6:                                     /* Obj1D_Wait */
        SET16(o, BB_WAIT, OB16(o, BB_WAIT) - 1);
        if ((int16_t)OB16(o, BB_WAIT) < 0) {
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
            SET16(o, BB_WAIT, 0x3B);
            gloop(o);
        }
        break;
    case 4:                                             /* Obj1D_MoveArc */
        o_move(o);
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, BB_XVEL));
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if (!OB16(o, OBJ_y_vel)) SET16(o, BB_XVEL, (uint16_t)-(int16_t)OB16(o, BB_XVEL));
        if (OB16(o, BB_Y) <= OB16(o, OBJ_y_pos)) {
            SET16(o, OBJ_y_vel, OB16(o, BB_YVEL));
            SET16(o, OBJ_x_vel, 0);
            SET8(o, OBJ_routine, 2);
        }
        break;
    default:                                            /* Obj1D_MoveStraight */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if (!OB16(o, OBJ_y_vel)) SET16(o, OBJ_x_pos, OB16(o, BB_OFF) + OB16(o, BB_X));
        if (OB16(o, OBJ_y_vel) == 0x180) gloop(o);
        if (OB16(o, BB_Y) <= OB16(o, OBJ_y_pos)) {
            SET16(o, OBJ_y_vel, OB16(o, BB_YVEL));
            SET16(o, OBJ_x_pos, OB16(o, BB_X));
            gloop(o);
        }
        break;
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 2D - one way barrier (rises while a player waits behind it)
 * ---------------------------------------------------------------------- */
#define BR_RISE  0x30
#define BR_Y0    0x32
#define BR_LEFT  0x38
#define BR_RIGHT 0x3A

static void barrier_check(uint32_t o, uint32_t p, uint16_t d2, uint16_t d3, uint16_t d4, uint16_t d5)
{
    int16_t x = (int16_t)OB16(p, OBJ_x_pos);
    if (x < (int16_t)d2 || (uint16_t)x >= d3) return;
    uint16_t y = OB16(p, OBJ_y_pos);
    if (y < d4 || y >= d5) return;
    if (OB8(p, P_OBJCTRL) & 0x80) return;
    SET8(o, OBJ_2nd, 2);
}

static void barrier(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj2D_Init (CPZ) */
        SET8(o, OBJ_routine, 2);
        int mtz = tri_zone() == TRI_S2_MTZ;
        o_init(o, tri_zone() == TRI_S2_HTZ || tri_zone() == TRI_S2_WFZ ? S2A_HTZBARRIER : mtz ? S2A_MTZBARRIER :
               tri_zone() == TRI_S2_DEZ ? S2A_DEZBARRIER : S2A_BARRIER,
               OB8(o, OBJ_render) | 4, 4, mtz ? 0x0C : 8, 0x20);
        SET16(o, BR_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        uint16_t d2 = (uint16_t)(OB16(o, OBJ_x_pos) - 0x200), d3 = (uint16_t)(OB16(o, OBJ_x_pos) + 0x18);
        if (OB8(o, OBJ_status) & 1) { d2 = (uint16_t)(d2 + 0x1E8); d3 = (uint16_t)(d3 + 0x1E8); }
        SET16(o, BR_LEFT, d2);
        SET16(o, BR_RIGHT, d3);
    }
    uint16_t d2, d3;                                    /* Obj2D_Main */
    if (!(OB8(o, OBJ_status) & 1)) {
        d2 = OB16(o, BR_LEFT);
        d3 = OB8(o, OBJ_2nd) ? OB16(o, BR_RIGHT) : OB16(o, OBJ_x_pos);
    } else {
        d2 = OB8(o, OBJ_2nd) ? OB16(o, BR_LEFT) : OB16(o, OBJ_x_pos);
        d3 = OB16(o, BR_RIGHT);
    }
    uint16_t d4 = (uint16_t)(OB16(o, BR_Y0) - 0x20), d5 = (uint16_t)(OB16(o, BR_Y0) + 0x20);
    SET8(o, OBJ_2nd, 0);
    barrier_check(o, RAM_Player_1, d2, d3, d4, d5);
    barrier_check(o, RAM_Player_2, d2, d3, d4, d5);
    int set = 1;
    if (OB8(o, OBJ_2nd)) {
        if (OB16(o, BR_RISE) == 0x40) set = 0;
        else SET16(o, BR_RISE, OB16(o, BR_RISE) + 8);
    } else if (OB16(o, BR_RISE)) {
        SET16(o, BR_RISE, OB16(o, BR_RISE) - 8);
    } else set = 0;
    if (set) SET16(o, OBJ_y_pos, OB16(o, BR_Y0) - OB16(o, BR_RISE));
    o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x20, 0x21, OB16(o, OBJ_x_pos));
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 32 - breakable metal block (roll onto it)
 * ---------------------------------------------------------------------- */
#define BK_CHAIN 0x38

static void block_bounce(uint32_t p, int bounce)     /* Obj32_SetCharacterOffBlock / _BouncePlayer */
{
    if (bounce) {
        SET8(p, OBJ_status, OB8(p, OBJ_status) | 4);
        SET8(p, OBJ_y_radius, 0x0E);
        SET8(p, OBJ_x_radius, 7);
        SET8(p, OBJ_anim, 2);
        SET16(p, OBJ_y_vel, (uint16_t)-0x300);
    }
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
    SET8(p, OBJ_routine, 2);
}

static void breakable_block(uint32_t o)
{
    static const int16_t vel[8] = { -0x100, -0x200, 0x100, -0x200, -0xC0, -0x1C0, 0xC0, -0x1C0 };  /* Obj32_VelArray2 */
    static const int16_t vel_htz[12] = { -0x200, -0x200, 0, -0x280, 0x200, -0x200,              /* Obj32_VelArray1 */
                                         -0x1C0, -0x1C0, 0, -0x200, 0x1C0, -0x1C0 };
    int htz = tri_zone() == TRI_S2_HTZ;
    static const uint16_t bonus[4] = { 10, 20, 50, 100 };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        if (htz) o_init(o, S2A_HTZROCK, 4, 4, 0x18, 0x10);   /* HTZ: the rock */
        else o_init(o, S2A_CPZMETAL, 4, 4, 0x10, 0x10);
        /* fall through */
    case 2: {                                           /* Obj32_Main */
        SET16(o, BK_CHAIN, r16(RAM_Chain_bonus_counter));
        uint8_t a1 = r8(RAM_Player_1 + OBJ_anim), a2 = r8(RAM_Player_2 + OBJ_anim);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        uint8_t st = OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND);
        if (!st) { o_remember(o); return; }
        if (st == (ST_P1_STAND | ST_P2_STAND)) {
            if (a1 != 2 && a2 != 2) { o_remember(o); return; }
            block_bounce(RAM_Player_1, a1 == 2);
            block_bounce(RAM_Player_2, a2 == 2);
        } else if (st & ST_P1_STAND) {
            if (a1 != 2) { o_remember(o); return; }
            block_bounce(RAM_Player_1, 1);
        } else {
            if (a2 != 2) { o_remember(o); return; }
            block_bounce(RAM_Player_2, 1);
        }
        /* Obj32_Destroy */
        w16(RAM_Chain_bonus_counter, OB16(o, BK_CHAIN));
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
        o_smash(o, htz ? 6 : 4, htz ? vel_htz : vel, 0x18, 4);
        uint16_t d2 = r16(RAM_Chain_bonus_counter);     /* SmashableObject_LoadPoints */
        w16(RAM_Chain_bonus_counter, d2 + 2);
        if (d2 >= 6) d2 = 6;
        uint16_t pts = bonus[d2 >> 1];
        if (r16(RAM_Chain_bonus_counter) >= 0x20) { pts = 1000; d2 = 10; }
        D(0) = pts;
        tri_jsr(S3K_HUD_AddToScore);
        o_score(o, d2 >> 1);
        return;
    }
    default:                                            /* Obj32_Fragment */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 40 - pressure spring (the red diving board)
 * ---------------------------------------------------------------------- */
static const uint8_t lever_boost[0x48] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,  3, 3, 3, 3, 3, 3, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static void spring_flips(uint32_t p, uint8_t d0, int flips_hi, int flips_lo, int speed)
{
    if (d0 & 1) {
        SET16(p, OBJ_ground_vel, 1);
        SET8(p, 0x27, 1);                               /* flip_angle */
        SET8(p, OBJ_anim, 0);
        SET8(p, 0x30, flips_hi);                        /* flips_remaining */
        SET8(p, 0x31, speed);                           /* flip_speed */
        if (!(d0 & 2)) SET8(p, 0x30, flips_lo);
        if (OB8(p, OBJ_status) & 1) {
            SET8(p, 0x27, (uint8_t)-(int8_t)OB8(p, 0x27));
            SET16(p, OBJ_ground_vel, (uint16_t)-(int16_t)OB16(p, OBJ_ground_vel));
        }
    }
    d0 &= 0x0C;
    if (d0 == 4) { SET8(p, OBJ_top_solid, 0x0C); SET8(p, OBJ_lrb_solid, 0x0D); }
    if (d0 == 8) { SET8(p, OBJ_top_solid, 0x0E); SET8(p, OBJ_lrb_solid, 0x0F); }
    o_sfx_s2(0xCC);                                        /* SndID_Spring */
}

static void lever_launch(uint32_t o, uint32_t p)     /* loc_2641E */
{
    if (!(OB8(o, OBJ_status) & 1)) {
        if ((uint16_t)(OB16(o, OBJ_x_pos) - 0x10) >= OB16(p, OBJ_x_pos)) return;
    } else {
        if ((uint16_t)(OB16(o, OBJ_x_pos) + 0x10) < OB16(p, OBJ_x_pos)) return;
    }
    if (OB8(o, OBJ_anim) != 1) { SET16(o, OBJ_anim, 0x0100); return; }   /* anim 1, prev_anim 0 */
    if (OB8(o, OBJ_frame)) return;
    int16_t d0 = (int16_t)-(int16_t)(OB16(o, OBJ_x_pos) - 0x1C - OB16(p, OBJ_x_pos));
    if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(~d0 + 0x27);
    if (d0 < 0) d0 = 0;
    uint8_t b = lever_boost[d0 < 0x48 ? d0 : 0x47];
    SET16(p, OBJ_y_vel, (uint16_t)-0x400);
    SET8(p, OBJ_y_vel, OB8(p, OBJ_y_vel) - b);          /* sub.b from the high byte */
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
    if (!(OB8(o, OBJ_status) & 1)) {
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
        b = (uint8_t)-b;
    }
    int16_t xv = (int16_t)OB16(p, OBJ_x_vel);
    if ((xv < 0 ? -xv : xv) >= 0x400) SET8(p, OBJ_x_vel, OB8(p, OBJ_x_vel) - b);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
    SET8(p, OBJ_anim, 0x10);                            /* AniIDSonAni_Spring */
    SET8(p, OBJ_routine, 2);
    SET8(p, 0x3D, 0);                                   /* spindash_flag */
    spring_flips(p, OB8(o, OBJ_subtype), 1, 3, 8);
}

static void lever_spring(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj40_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_LEVERSPRING, OB8(o, OBJ_render) | 4, 4, 0x1C, 0x10);
    }
    o_animate_s2(o, o_asset_anim(S2A_LEVERSPRING));        /* Obj40_Main */
    uint32_t slopes = blob(TRI_BLOB_S2_SLOPES);
    o_solid_sloped(o, 0x27, 8, OB16(o, OBJ_x_pos), slopes + (OB8(o, OBJ_frame) ? 0x28 : 0));
    if (OB8(o, OBJ_status) & ST_P1_STAND) lever_launch(o, RAM_Player_1);
    if (OB8(o, OBJ_status) & ST_P2_STAND) lever_launch(o, RAM_Player_2);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 78 - stairs that sink (or rise) to open the way: four blocks, the
 * first one (the placed object) runs the motion and holds the offsets.
 * ---------------------------------------------------------------------- */
#define ST_TIMER  0x40                /* objoff_2C */
#define ST_TOUCH  0x42                /* objoff_2E */
#define ST_SLOT   0x43                /* objoff_2F: which offset word */
#define ST_X0     0x30
#define ST_Y0     0x32
#define ST_OFFS   0x34                /* four words */
#define ST_PARENT 0x3C

static void stairs_offsets(uint32_t o, int down)     /* loc_29334 / loc_2935E */
{
    int16_t v = (int16_t)OB16(o, ST_OFFS);
    if (v == (down ? 0x80 : -0x80)) return;
    v = (int16_t)(v + (down ? 1 : -1));
    SET16(o, ST_OFFS, v);
    int32_t d1 = (int32_t)((uint32_t)(uint16_t)v << 16);
    if (!down) d1 = (int32_t)v << 16;
    int32_t d2, d3;
    if (down) {
        d1 = (int32_t)((uint32_t)d1 >> 1); d2 = d1; d1 = (int32_t)((uint32_t)d1 >> 1);
    } else {
        d1 >>= 1; d2 = d1; d1 >>= 1;
    }
    d3 = d1 + d2;
    SET16(o, ST_OFFS + 2, (uint16_t)(d3 >> 16));
    SET16(o, ST_OFFS + 4, (uint16_t)(d2 >> 16));
    SET16(o, ST_OFFS + 6, (uint16_t)(d1 >> 16));
}

static void stairs(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj78_Init */
        SET8(o, OBJ_routine, 2);
        int d3 = 0x34, d4 = 2;
        if (OB8(o, OBJ_status) & 1) { d3 = 0x3A; d4 = -2; }
        uint16_t d2 = OB16(o, OBJ_x_pos);
        uint32_t a1 = o;
        for (int k = 0; k < 4; k++) {
            if (k) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET8(a1, OBJ_routine, 4);
            }
            SET32(a1, OBJ_code, S2(0x78));
            o_init(a1, S2A_CPZBLOCK, 4, 3, 0x10, 0x10);
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET16(a1, OBJ_x_pos, d2);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, ST_X0, OB16(o, OBJ_x_pos));
            SET16(a1, ST_Y0, OB16(a1, OBJ_y_pos));
            d2 += 0x20;
            SET8(a1, ST_SLOT, d3);
            SET32(a1, ST_PARENT, RAMPTR(o));
            d3 += d4;
        }
    }
    if (OB8(o, OBJ_routine) == 2) {                     /* Obj78_Main: the motion */
        switch (OB8(o, OBJ_subtype) & 7) {
        case 0: case 4:                                 /* wait to be stood on */
            if (!OB16(o, ST_TIMER)) {
                if (OB8(o, ST_TOUCH) & 0x30) SET16(o, ST_TIMER, 0x1E);   /* touch_top_mask */
            } else {
                SET16(o, ST_TIMER, OB16(o, ST_TIMER) - 1);
                if (!OB16(o, ST_TIMER)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
            }
            break;
        case 2: case 6:                                 /* wait to be hit from below, then shake */
            if (!OB16(o, ST_TIMER)) {
                if (OB8(o, ST_TOUCH) & 0x0C) SET16(o, ST_TIMER, 60);    /* touch_bottom_mask */
            } else {
                SET16(o, ST_TIMER, OB16(o, ST_TIMER) - 1);
                if (!OB16(o, ST_TIMER)) { SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1); break; }
                uint16_t d0 = (OB16(o, ST_TIMER) >> 2) & 1;
                for (int k = 0; k < 4; k++) { SET16(o, ST_OFFS + k * 2, d0); d0 ^= 1; }
            }
            break;
        case 1: case 3: stairs_offsets(o, 1); break;
        default: stairs_offsets(o, 0); break;
        }
    }
    uint32_t a2 = r32(o + ST_PARENT) & 0xFFFFFF;        /* loc_29280 */
    SET16(o, OBJ_y_pos, OB16(a2, OB8(o, ST_SLOT)) + OB16(o, ST_Y0));
    o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, OB16(o, OBJ_x_pos));
    SET8(a2, ST_TOUCH, OB8(a2, ST_TOUCH) | (uint8_t)(g_o_d6 >> 16));
    if (o_out_of_range(o, OB16(o, ST_X0))) { o_delete_respawnable(o); return; }  /* MarkObjGone2 */
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 7A - platforms sliding back and forth on the water: a pair (or a
 * single one) that turn round on meeting each other or their range end.
 * ---------------------------------------------------------------------- */
#define WP_X0    0x30
#define WP_MIN   0x32
#define WP_MAX   0x34
#define WP_DIR   0x36
#define WP_OTHER 0x3C

static void wplat_move(uint32_t o)                   /* loc_294F4 */
{
    uint16_t oldx = OB16(o, OBJ_x_pos), d0;
    if (OB8(o, WP_DIR)) {
        d0 = (uint16_t)(oldx - 1);
        if (d0 == OB16(o, WP_MIN)) SET8(o, WP_DIR, 0);
    } else {
        d0 = (uint16_t)(oldx + 1);
        if (d0 == OB16(o, WP_MAX)) SET8(o, WP_DIR, 1);
    }
    SET16(o, OBJ_x_pos, d0);
    o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
}

static void water_platform(uint32_t o)
{
    static const struct { int8_t n; uint8_t origin; int16_t parent_dx, child_dx; } props[4] = {
        { 0, 0x68, -0x68, 0 }, { 1, 0xA8, -0xB0, 0x40 }, { 1, 0xE8, -0x80, 0x80 }, { 0, 0x68, 0x67, 0 },
    };
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj7A_Init */
        SET8(o, OBJ_routine, 2);
        int idx = (OB8(o, OBJ_subtype) / 6) & 3;
        uint32_t a1 = o;
        for (int k = 0; k <= props[idx].n; k++) {
            if (k) {
                uint32_t c = o_alloc_after(o);
                if (!c) continue;
                a1 = c;
                SET32(a1, OBJ_code, S2(0x7A));
                SET8(a1, OBJ_routine, 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
            if (tri_zone() == TRI_S2_MCZ) {             /* MCZ: level-art log, no priority */
                o_init(a1, S2A_MCZPLAT, 4, 4, 0x18, 8);
            } else {
                o_init(a1, S2A_CPZPLAT, 4, 4, 0x18, 8);
                SET16(a1, OBJ_art_tile, OB16(a1, OBJ_art_tile) | 0x8000);
            }
            SET16(a1, WP_X0, OB16(a1, OBJ_x_pos));
        }
        SET32(a1, WP_OTHER, RAMPTR(o));
        SET32(o, WP_OTHER, RAMPTR(a1));
        if (OB8(o, OBJ_subtype) == 0x0C) SET8(o, WP_DIR, 1);
        uint16_t d0 = (uint16_t)(OB16(o, WP_X0) - props[idx].origin);
        SET16(o, WP_MIN, d0);
        SET16(a1, WP_MIN, d0);
        d0 = (uint16_t)(d0 + props[idx].origin * 2);
        SET16(o, WP_MAX, d0);
        SET16(a1, WP_MAX, d0);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + props[idx].parent_dx);
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + props[idx].child_dx);
    }   /* fall through */
    case 2: {                                           /* Obj7A_Main */
        wplat_move(o);
        if (!o_out_of_range(o, OB16(o, WP_MIN)) || !o_out_of_range(o, OB16(o, WP_MAX))) { o_draw(o); return; }
        uint32_t a1 = r32(o + WP_OTHER) & 0xFFFFFF;
        if (a1 != o) o_delete(a1);
        o_delete_respawnable(o);
        return;
    }
    default: {                                          /* Obj7A_SubObject */
        wplat_move(o);
        uint32_t a1 = r32(o + WP_OTHER) & 0xFFFFFF;     /* loc_2953E: meet the partner -> both turn */
        if ((uint16_t)(OB16(o, OBJ_x_pos) - 0x18) == (uint16_t)(OB16(a1, OBJ_x_pos) + 0x18)) {
            SET8(o, WP_DIR, OB8(o, WP_DIR) ^ 1);
            SET8(a1, WP_DIR, OB8(a1, WP_DIR) ^ 1);
        }
        o_draw(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 7B - warp pipe exit spring
 * ---------------------------------------------------------------------- */
#define TS_SPEED 0x30

static void tube_spring_launch(uint32_t o, uint32_t p)   /* loc_296C2 */
{
    SET16(o, OBJ_anim, 0x0100);
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 4);
    SET16(p, OBJ_y_vel, OB16(o, TS_SPEED));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
    SET8(p, OBJ_anim, 0x10);
    SET8(p, OBJ_routine, 2);
    uint8_t d0 = OB8(o, OBJ_subtype);
    if (d0 & 0x80) SET16(p, OBJ_x_vel, 0);
    spring_flips(p, d0, 0, 1, 4);
}

static void tube_spring(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj7B_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_TUBESPRING, OB8(o, OBJ_render) | 4, 1, 0x10, 0x10);
        SET16(o, TS_SPEED, (OB8(o, OBJ_subtype) & 2) ? (uint16_t)-0xA80 : (uint16_t)-0x1000);
    }
    if (OB8(o, OBJ_frame) != 1) {                       /* Obj7B_Main */
        o_solid(o, 0x1B, 8, 0x10, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) tube_spring_launch(o, RAM_Player_1);
        if (OB8(o, OBJ_status) & ST_P2_STAND) tube_spring_launch(o, RAM_Player_2);
    }
    uint16_t d4 = (uint16_t)(OB16(o, OBJ_x_pos) - 0x10), d5 = (uint16_t)(OB16(o, OBJ_x_pos) + 0x10);
    uint16_t d2 = OB16(o, OBJ_y_pos), d3 = (uint16_t)(d2 + 0x30);
    for (int n = 0; n < 2; n++) {                       /* a player coming up the pipe: open */
        uint32_t p = o_player(n);
        uint16_t px = OB16(p, OBJ_x_pos), py = OB16(p, OBJ_y_pos);
        if (px < d4 || px >= d5 || py < d2 || py >= d3) continue;
        if (n && r16(RAM_Tails_CPU_routine) == 4) continue;   /* flying in */
        if (OB8(o, 0x21) == 2 + n) continue;
        SET8(o, OBJ_anim, 2 + n);
    }
    o_animate_s2(o, o_asset_anim(S2A_TUBESPRING));
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 04 - water surface: two, spawned by the level (Level_ChkWater) and
 * moved with the camera each frame (UpdateWaterSurface)
 * ---------------------------------------------------------------------- */
#define WS_ORIGX  0x30
static uint16_t s_wsurf[2];

static void water_surface(uint32_t o)
{
    static const uint8_t anim[64] = {                   /* Anim_obj04 */
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,  1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2,
        2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1,  1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
    };
    int arz = tri_zone() == TRI_S2_ARZ;
    if (!OB8(o, OBJ_routine)) {                         /* Obj04_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, arz ? S2A_WATERSURF2 : S2A_WATERSURF, 4, 0, 0x80, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
    }
    uint16_t d1 = r16(RAM_Camera_X_pos);                /* UpdateWaterSurface */
    if (o_frame_counter() & 1) d1 = (uint16_t)(d1 + 0x20);
    SET16(o, OBJ_x_pos, (uint16_t)(d1 + OB16(o, WS_ORIGX)));
    SET16(o, OBJ_y_pos, r16(RAM_Water_level));
    if (!arz) {                                         /* Obj04_Action */
        SET8(o, OBJ_frame, anim[OB8(o, OBJ_anim_frame)]);
        SET8(o, OBJ_anim_frame, (OB8(o, OBJ_anim_frame) + 1) & 0x3F);
    } else {                                            /* Obj04_Action2 */
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_anim_timer, 5);
            SET8(o, OBJ_frame, (OB8(o, OBJ_frame) + 1) & 1);
        }
    }
    o_draw(o);
}

/* the level keeps two surfaces alive (re-spawned if anything removed them) */
void s2_water_surfaces(void)
{
    static const uint16_t base[2] = { 0x60, 0x120 };
    for (int i = 0; i < 2; i++) {
        uint32_t o = s_wsurf[i] ? (0xFF0000u | s_wsurf[i]) : 0;
        if (o && r32(o) == S2(0x04) && OB16(o, WS_ORIGX) == base[i]) continue;
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET32(a1, OBJ_code, S2(0x04));
        SET16(a1, WS_ORIGX, base[i]);
        s_wsurf[i] = (uint16_t)(a1 & 0xFFFF);
    }
}

/* ------------------------------------------------------------------------
 * Object 7C - the big pylon passing in front of CPZ (screen-positioned,
 * 3/4 of the camera's speed, once every $400 pixels)
 * ---------------------------------------------------------------------- */
static uint16_t s_pylon;

static void cpz_pylon(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj7C_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_CPZPYLON, 0, 7, 0x10, 0x80);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
    }
    int16_t d1 = (int16_t)(r16(RAM_Camera_X_pos) & 0x3FF);
    if (d1 >= 0x2E0) return;
    d1 >>= 1;
    int16_t d0 = d1;
    d1 >>= 1;
    d0 = (int16_t)-(d0 + d1);
    SET16(o, OBJ_x_pos, (uint16_t)d0);                  /* x_pixel */
    int16_t y = (int16_t)((int16_t)r16(RAM_Camera_Y_pos) >> 1);
    SET16(o, OBJ_y_pos, (uint16_t)(0x100 - (y & 0x3F))); /* y_pixel */
    o_draw(o);
}

/* CPZ: the level loads the pylon (Level: CPZPylon) */
void s2_cpz_pylon(void)
{
    uint32_t o = s_pylon ? (0xFF0000u | s_pylon) : 0;
    if (o && r32(o) == S2(0x7C)) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x7C));
    s_pylon = (uint16_t)(a1 & 0xFFFF);
}

void s2_obj_cpz_register(TriObjFn *t)
{
    t[0x1E] = spin_tube;
    t[0x6B] = block;
    t[0x19] = elevator;
    t[0x1B] = booster;
    t[0xA5] = spiny;
    t[0xA6] = spiny_wall;
    t[0xA7] = grabber;
    t[0xA8] = grabber_legs;
    t[0xA9] = grabber_box;
    t[0xAA] = grabber_string;
    t[0x0B] = tip_floor;
    t[0x1D] = blue_balls;
    t[0x2D] = barrier;
    t[0x32] = breakable_block;
    t[0x40] = lever_spring;
    t[0x78] = stairs;
    t[0x7A] = water_platform;
    t[0x7B] = tube_spring;
    t[0x04] = water_surface;
    t[0x7C] = cpz_pylon;
}
