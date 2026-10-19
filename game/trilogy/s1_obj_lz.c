/*
 * s1_obj_lz.c — Sonic 1 Labyrinth Zone objects, transcribed from the REV01
 * object code: pole that breaks (0B), flapping door (0C), harpoon (16), water
 * surface (1B), Jaws (2C), Burrobot (2D), Orbinaut (60), blocks (61),
 * gargoyle (62), conveyor platforms (63), waterfalls (65). Bubble makers (64)
 * are S3K's Obj_Bubbler (S2's port of the same object); splashes and the air
 * countdown are S3K's own player features.
 *
 * Jaws ($0A) and the pole ($E1) use S1 touch sizes S3K's table lacks, so they
 * test S1's ReactToItem box themselves (o_s1_touch).
 */
#include "trilogy_obj.h"

#define OBJ_2nd          0x25
#define OBJ_prev_anim    0x21
#define OBJ_object_ctrl  0x2E
#define LZ_Ctrl_1_pressed (RAM_Ctrl_1_held_logical + 1)
#define S3K_ObjCheckFloorDist2 0x00F93Cu
#define ANI_HANG         0x11

static int chk_del(uint32_t o, uint16_t x)
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return 1; }
    return 0;
}

/* RememberState */
static void remember(uint32_t o)
{
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

static uint8_t s_obj63[16];           /* v_obj63: conveyor platform sets loaded */
static uint8_t s_conveyrev;           /* f_conveyrev */

uint8_t s1_lz_conveyrev(void) { return s_conveyrev; }

void s1_obj_lz_reset(void)
{
    for (int i = 0; i < 16; i++) s_obj63[i] = 0;
    s_conveyrev = 0;
}

/* ------------------------------------------------------------------------
 * Object 1B - water surface (two, spawned by the level; follow the camera)
 * ---------------------------------------------------------------------- */
#define SURF_ORIGX  0x30
static uint16_t s_surf[2];

static void surface(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SURF, 4, 0, 0x80, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);   /* Tile_Prio */
        SET16(o, SURF_ORIGX, OB16(o, OBJ_x_pos));
    }
    uint16_t d1 = (uint16_t)((r16(RAM_Camera_X_pos) & 0xFFE0) + OB16(o, SURF_ORIGX));
    if (o_frame_counter() & 1) d1 = (uint16_t)(d1 + 0x20);
    SET16(o, OBJ_x_pos, d1);
    SET16(o, OBJ_y_pos, r16(RAM_Water_level));
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 7);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) >= 3) SET8(o, OBJ_frame, 0);
    }
    o_draw(o);
}

/* Level start (S1 Level: v_watersurface1/2 at x $60 / $120) */
void tri_lz_surfaces(void)
{
    static const uint16_t base[2] = { 0x60, 0x120 };
    for (int i = 0; i < 2; i++) {
        uint32_t o = s_surf[i] ? (0xFF0000u | s_surf[i]) : 0;
        if (o && r32(o) == NATIVE_S1_BASE + 0x1B * 4 && OB16(o, SURF_ORIGX) == base[i]) continue;
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x1B * 4);
        SET16(a1, OBJ_x_pos, base[i]);
        SET16(a1, SURF_ORIGX, base[i]);
        s_surf[i] = (uint16_t)(a1 & 0xFFFF);
    }
}

/* ------------------------------------------------------------------------
 * Object 0B - pole that breaks
 * ---------------------------------------------------------------------- */
#define POLE_TIME   0x30
#define POLE_GRAB   0x32
#define POLE_TOUCH  0x33              /* obColProp ($E1 special touch) */

static void pole_release(uint32_t o)
{
    SET8(o, OBJ_routine, 4);
    SET8(RAM_Player_1, OBJ_object_ctrl, 0);    /* clr.b (f_playerctrl).w */
    tri_lz_wtunnel_allow = 0;
    SET8(o, POLE_GRAB, 0);
}

static void pole(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_POLE, 4, 4, 8, 0x20);
        SET16(o, POLE_TIME, OB8(o, OBJ_subtype) * 60);
        /* fall through */
    case 2:
        if (o_s1_touch(o, 4, 0x20) & 1) SET8(o, POLE_TOUCH, OB8(o, POLE_TOUCH) + 1);
        if (OB8(o, POLE_GRAB)) {
            if (OB16(o, POLE_TIME)) {
                SET16(o, POLE_TIME, OB16(o, POLE_TIME) - 1);
                if (!OB16(o, POLE_TIME)) {
                    SET8(o, OBJ_frame, 1);             /* break the pole */
                    pole_release(o);
                    break;
                }
            }
            uint16_t d0 = (uint16_t)(OB16(o, OBJ_y_pos) - 0x18);
            uint8_t held = r8(RAM_Ctrl_1_held_logical);
            if (held & 1) {
                SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 1);
                if (d0 >= OB16(p, OBJ_y_pos)) SET16(p, OBJ_y_pos, d0);
            }
            d0 = (uint16_t)(d0 + 0x24);
            if (held & 2) {
                SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 1);
                if (d0 < OB16(p, OBJ_y_pos)) SET16(p, OBJ_y_pos, d0);
            }
            if (r8(LZ_Ctrl_1_pressed) & 0x70) pole_release(o);
            break;
        }
        if (!OB8(o, POLE_TOUCH)) break;                /* .grab */
        if ((uint16_t)(OB16(o, OBJ_x_pos) + 0x14) >= OB16(p, OBJ_x_pos)) break;
        SET8(o, POLE_TOUCH, 0);
        if (OB8(p, OBJ_routine) >= 4) break;
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x14);
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
        SET8(p, OBJ_anim, ANI_HANG);
        SET8(p, OBJ_object_ctrl, 1);                   /* lock controls */
        tri_lz_wtunnel_allow = 1;
        SET8(o, POLE_GRAB, 1);
        break;
    default:
        break;
    }
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 0C - flapping door
 * ---------------------------------------------------------------------- */
#define FLAP_WAIT  0x30
#define FLAP_TIME  0x32

static void flap_door(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_FLAP, 4, 0, 0x28, 0x20);
        SET16(o, FLAP_TIME, OB8(o, OBJ_subtype) * 60);
    }
    SET16(o, FLAP_WAIT, OB16(o, FLAP_WAIT) - 1);
    if ((int16_t)OB16(o, FLAP_WAIT) < 0) {
        SET16(o, FLAP_WAIT, OB16(o, FLAP_TIME));
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
        if (OB8(o, OBJ_render) & 0x80) o_sfx(0x91);    /* sfx_Door: S3K door close */
    }
    o_animate(o, o_asset_anim(S1A_FLAP));
    tri_lz_wtunnel_allow = 0;
    if (!OB8(o, OBJ_frame) && r16(RAM_Player_1 + OBJ_x_pos) < OB16(o, OBJ_x_pos)) {
        tri_lz_wtunnel_allow = 1;
        o_solid(o, 0x13, 0x20, 0x21, OB16(o, OBJ_x_pos));
    }
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 16 - harpoon
 * ---------------------------------------------------------------------- */
#define HARP_TIME  0x30

static void harpoon(uint32_t o)
{
    static const uint8_t types[6] = { 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0 };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_HARP, 4, 4, 0x14, 0x14);
        SET8(o, OBJ_anim, OB8(o, OBJ_subtype));
        SET16(o, HARP_TIME, 60);
        /* fall through */
    case 2:
        o_animate(o, o_asset_anim(S1A_HARP));
        SET8(o, OBJ_coll_flags, types[OB8(o, OBJ_frame) % 6]);
        break;
    default:                                   /* Harp_Wait */
        SET16(o, HARP_TIME, OB16(o, HARP_TIME) - 1);
        if ((int16_t)OB16(o, HARP_TIME) < 0) {
            SET16(o, HARP_TIME, 60);
            SET8(o, OBJ_routine, 2);
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
        }
        break;
    }
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 2C - Jaws ($0A: S1 touch box $10 x $C)
 * ---------------------------------------------------------------------- */
#define JAWS_COUNT  0x30
#define JAWS_DELAY  0x32

static void jaws(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_JAWS, 4, 4, 0x10, 0x10);
        uint16_t d0 = (uint16_t)((OB8(o, OBJ_subtype) << 6) - 1);
        SET16(o, JAWS_COUNT, d0);
        SET16(o, JAWS_DELAY, d0);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? 0x40 : -0x40);
    }
    int t = o_s1_touch(o, 0x10, 0x0C);
    for (int i = 0; i < 2; i++) {
        if (!(t & (1 << i))) continue;
        A(0) = RAMPTR(o_player(i));
        A(1) = RAMPTR(o);
        tri_jsr(S3K_Touch_Enemy);
        if (r32(o) != NATIVE_S1_BASE + 0x2C * 4) return;  /* destroyed */
    }
    SET16(o, JAWS_COUNT, OB16(o, JAWS_COUNT) - 1);
    if ((int16_t)OB16(o, JAWS_COUNT) < 0) {
        SET16(o, JAWS_COUNT, OB16(o, JAWS_DELAY));
        SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel));
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        SET8(o, OBJ_prev_anim, 1);
    }
    o_animate(o, o_asset_anim(S1A_JAWS));
    o_move(o);
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 2D - Burrobot
 * ---------------------------------------------------------------------- */
#define BURRO_TIME  0x30
#define BURRO_TOG   0x32

/* Burro_ChkSonic2: face Sonic; returns |dx|, *d1 = +-$80 */
static uint16_t burro_face(uint32_t o, int16_t *d1)
{
    *d1 = 0x80;
    SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
    uint16_t px = r16(RAM_Player_1 + OBJ_x_pos), ox = OB16(o, OBJ_x_pos);
    if (px >= ox) return (uint16_t)(px - ox);
    *d1 = -0x80;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
    return (uint16_t)(ox - px);
}

static void burro_stop_or_jump(uint32_t o)           /* loc_AD84 */
{
    if (o_frame_counter() & 4) {
        SET8(o, OBJ_2nd, OB8(o, OBJ_2nd) - 2);
        SET16(o, BURRO_TIME, 59);
        SET16(o, OBJ_x_vel, 0);
        SET8(o, OBJ_anim, 0);
    } else {
        SET8(o, OBJ_2nd, OB8(o, OBJ_2nd) + 2);
        SET16(o, OBJ_y_vel, -0x400);
        SET8(o, OBJ_anim, 2);
    }
}

static void burrobot(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 0x13);
        SET8(o, OBJ_x_radius, 8);
        o_init(o, S1A_BURRO, 4, 4, 0x0C, 0x14);
        SET8(o, OBJ_coll_flags, 5);
        SET8(o, OBJ_2nd, OB8(o, OBJ_2nd) + 6);
        SET8(o, OBJ_anim, 2);
    }
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                        /* .changedir */
        SET16(o, BURRO_TIME, OB16(o, BURRO_TIME) - 1);
        if ((int16_t)OB16(o, BURRO_TIME) < 0) {
            SET8(o, OBJ_2nd, 2);
            SET16(o, BURRO_TIME, 255);
            SET16(o, OBJ_x_vel, 0x80);
            SET8(o, OBJ_anim, 1);
            uint8_t st = OB8(o, OBJ_status);
            SET8(o, OBJ_status, st ^ 1);
            if (st & 1) SET16(o, OBJ_x_vel, -0x80);
        }
        break;
    case 2: {                                      /* Burro_Move */
        SET16(o, BURRO_TIME, OB16(o, BURRO_TIME) - 1);
        if ((int16_t)OB16(o, BURRO_TIME) < 0) { burro_stop_or_jump(o); break; }
        o_move(o);
        uint8_t tog = OB8(o, BURRO_TOG);
        SET8(o, BURRO_TOG, tog ^ 1);
        if (tog & 1) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
            break;
        }
        uint16_t d3 = (uint16_t)(OB16(o, OBJ_x_pos) + 0x0C);
        if (!(OB8(o, OBJ_status) & 1)) d3 = (uint16_t)(d3 - 0x18);
        A(0) = RAMPTR(o);
        D(3) = d3;
        tri_jsr(S3K_ObjCheckFloorDist2);
        if ((int16_t)D(1) >= 0x0C) burro_stop_or_jump(o);
        break;
    }
    case 4: {                                      /* Burro_Jump */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if ((int16_t)OB16(o, OBJ_y_vel) < 0) break;
        SET8(o, OBJ_anim, 3);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_anim, 1);
        SET16(o, BURRO_TIME, 255);
        SET8(o, OBJ_2nd, 2);
        int16_t v;
        burro_face(o, &v);
        break;
    }
    default: {                                     /* Burro_ChkSonic */
        int16_t v;
        if (burro_face(o, &v) >= 0x60) break;
        uint16_t py = r16(RAM_Player_1 + OBJ_y_pos), oy = OB16(o, OBJ_y_pos);
        if (py >= oy) break;
        if ((uint16_t)(py - oy) < 0xFF80) break;
        if (r16(RAM_Debug_placement_mode)) break;
        SET8(o, OBJ_2nd, 4);
        SET16(o, OBJ_x_vel, (uint16_t)v);
        SET16(o, OBJ_y_vel, -0x400);
        break;
    }
    }
    o_animate(o, o_asset_anim(S1A_BURRO));
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 60 - Orbinaut (LZ art; SLZ/SBZ later)
 * ---------------------------------------------------------------------- */
#define ORB_DIR     0x36              /* parent: +-1 spin direction */
#define ORB_COUNT   0x37              /* parent: satellites left; slots at $38.. */
#define ORB_PARENT  0x3C

static void orb_chkdel(uint32_t o)
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) {
        uint16_t r = OB16(o, OBJ_respawn);
        if (r) w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
        int n = OB8(o, ORB_COUNT);
        for (int i = 0; i < n && i < 4; i++) {
            uint32_t a1 = o_slot(OB8(o, ORB_COUNT + 1 + i));
            if (r32(a1) == NATIVE_S1_BASE + 0x60 * 4) o_delete(a1);
        }
        o_delete(o);
        return;
    }
    o_touch(o);
    o_draw(o);
}

static void orbinaut(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_init(o, S1A_ORB_LZ, 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET8(o, ORB_COUNT, 0);
        uint8_t d2 = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t a1 = o_alloc_after(o);
            if (!a1) break;
            SET8(o, ORB_COUNT, OB8(o, ORB_COUNT) + 1);
            SET8(o, ORB_COUNT + 1 + i, o_slot_index(a1));
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x60 * 4);
            SET8(a1, OBJ_routine, 6);
            o_init(a1, S1A_ORB_LZ, 4, 4, 8, 8);
            SET8(a1, OBJ_frame, 3);
            SET8(a1, OBJ_coll_flags, 0x98);
            SET8(a1, OBJ_angle, d2);
            d2 = (uint8_t)(d2 + 0x40);
            SET32(a1, ORB_PARENT, RAMPTR(o));
        }
        SET8(o, ORB_DIR, (OB8(o, OBJ_status) & 1) ? 0xFF : 1);
        SET8(o, OBJ_routine, OB8(o, OBJ_subtype) + 2);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? 0x40 : -0x40);
        return;
    }
    case 2: {                                      /* Orb_ChkSonic */
        int16_t dx = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
        int16_t dy = (int16_t)(r16(RAM_Player_1 + OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if (dx < 0) dx = (int16_t)-dx;
        if (dy < 0) dy = (int16_t)-dy;
        if ((uint16_t)dx < 0xA0 && (uint16_t)dy < 0x50 && !r16(RAM_Debug_placement_mode))
            SET8(o, OBJ_anim, 1);
        o_animate(o, o_asset_anim(S1A_ORB_LZ));
        orb_chkdel(o);
        return;
    }
    case 4:                                        /* Orb_Display */
        o_move(o);
        orb_chkdel(o);
        return;
    case 6: {                                      /* Orb_MoveOrb */
        uint32_t a1 = r32(o + ORB_PARENT) & 0xFFFFFF;
        if (r32(a1) != NATIVE_S1_BASE + 0x60 * 4) { o_delete(o); return; }
        if (OB8(a1, OBJ_frame) == 2 && OB8(o, OBJ_angle) == 0x40) {
            SET8(o, OBJ_routine, 8);
            SET8(a1, ORB_COUNT, OB8(a1, ORB_COUNT) - 1);
            if (!OB8(a1, ORB_COUNT)) SET8(a1, OBJ_routine, OB8(a1, OBJ_routine) + 2);
            SET16(o, OBJ_x_vel, (OB8(a1, OBJ_status) & 1) ? 0x200 : -0x200);
        } else {
            int16_t s, c;
            o_sincos(OB8(o, OBJ_angle), &s, &c);
            SET16(o, OBJ_x_pos, (uint16_t)((c >> 4) + OB16(a1, OBJ_x_pos)));
            SET16(o, OBJ_y_pos, (uint16_t)((s >> 4) + OB16(a1, OBJ_y_pos)));
            SET8(o, OBJ_angle, OB8(o, OBJ_angle) + OB8(a1, ORB_DIR));
        }
        o_touch(o);
        o_draw(o);
        return;
    }
    default:                                       /* Orb_ChkDel2 */
        o_move(o);
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 61 - blocks
 * ---------------------------------------------------------------------- */
#define LB_ORIGY    0x30
#define LB_ORIGX    0x34
#define LB_TIME     0x36
#define LB_UNTOUCH  0x38
#define LB_SINK     0x3E

static void lz_block(uint32_t o)
{
    static const uint8_t var[4][2] = { { 0x10, 0x10 }, { 0x20, 0x0C }, { 0x10, 0x10 }, { 0x10, 0x10 } };
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        int v = (OB8(o, OBJ_subtype) >> 4) & 3;
        o_init(o, S1A_LBLOCK, 4, 3, var[v][0], var[v][1]);
        SET8(o, OBJ_y_radius, var[v][1]);
        SET8(o, OBJ_frame, v);
        SET16(o, LB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, LB_ORIGY, OB16(o, OBJ_y_pos));
        uint8_t t = OB8(o, OBJ_subtype) & 0x0F;
        if (t && t != 7) SET8(o, LB_UNTOUCH, 1);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    uint8_t sub = OB8(o, OBJ_subtype);
    switch (sub & 0x0F) {
    case 1: case 3:
        if (!OB16(o, LB_TIME)) {
            if (OB8(o, OBJ_status) & ST_P1_STAND) SET16(o, LB_TIME, 30);
        } else {
            SET16(o, LB_TIME, OB16(o, LB_TIME) - 1);
            if (!OB16(o, LB_TIME)) { SET8(o, OBJ_subtype, sub + 1); SET8(o, LB_UNTOUCH, 0); }
        }
        break;
    case 2: case 6: {
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 8);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 + 1);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_subtype, 0);
        }
        break;
    }
    case 4: {
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
        int16_t d1 = o_ceiling_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d1);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_subtype, 0);
        }
        break;
    }
    case 5:
        if (OB8(o, 0x3F) == 1) { SET8(o, OBJ_subtype, sub + 1); SET8(o, LB_UNTOUCH, 0); }
        break;
    case 7: {
        int16_t d0 = (int16_t)(r16(RAM_Water_level) - OB16(o, OBJ_y_pos));
        if (d0 == 0) break;
        if (d0 < 0) {                              /* rise with the water */
            if (d0 < -2) d0 = -2;
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
            int16_t d1 = o_ceiling_dist(o);
            if (d1 < 0) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d1);
        } else {                                   /* sink with it */
            if (d0 > 2) d0 = 2;
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 + 1);
        }
        break;
    }
    default:
        break;
    }
    if (OB8(o, OBJ_render) & 0x80) {
        int d2 = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
        SET8(o, 0x3F, ((g_o_d6 >> 16) & 3) ? 1 : 0);   /* side contact (S1 d4 = 1) */
        if (OB8(o, LB_UNTOUCH)) {                  /* loc_12180: sink while stood on */
            uint8_t a = OB8(o, LB_SINK);
            int move = 0;
            if (OB8(o, OBJ_status) & ST_P1_STAND) { if (a != 0x40) { SET8(o, LB_SINK, a + 4); move = 1; } }
            else if (a) { SET8(o, LB_SINK, a - 4); move = 1; }
            if (move) {
                int16_t s, c;
                o_sincos(OB8(o, LB_SINK), &s, &c);
                SET16(o, OBJ_y_pos, (uint16_t)(((s * 0x400) >> 16) + OB16(o, LB_ORIGY)));
            }
        }
    }
    if (chk_del(o, OB16(o, LB_ORIGX))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 62 - gargoyle head and its fireballs
 * ---------------------------------------------------------------------- */
#define GAR_RATE  0x30
#define GAR_TIME  0x31

static void gargoyle(uint32_t o)
{
    static const uint8_t rate[8] = { 30, 60, 90, 120, 150, 180, 210, 240 };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_GAR, 4, 3, 0x10, 0x10);
        SET8(o, GAR_RATE, rate[OB8(o, OBJ_subtype) & 7]);
        SET8(o, GAR_TIME, OB8(o, GAR_RATE));
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
        /* fall through */
    case 2:                                        /* Gar_MakeFire */
        SET8(o, GAR_TIME, OB8(o, GAR_TIME) - 1);
        if (!OB8(o, GAR_TIME)) {
            SET8(o, GAR_TIME, OB8(o, GAR_RATE));
            if (o_visible(o)) {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x62 * 4);
                    SET8(a1, OBJ_routine, 4);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                    SET8(a1, OBJ_render, OB8(o, OBJ_render));
                    SET8(a1, OBJ_status, OB8(o, OBJ_status));
                }
            }
        }
        break;
    case 4:                                        /* Gar_FireBall */
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_x_radius, 8);
        o_init(o, S1A_GARFIRE, OB8(o, OBJ_render) | 4, 4, 8, 8);
        SET8(o, OBJ_coll_flags, 0x98);
        SET8(o, OBJ_frame, 2);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? 0x200 : -0x200);
        o_sfx(0x70);                               /* sfx_Fireball: S3K lava ball */
        /* fall through */
    default:                                       /* Gar_AniFire */
        if (!(o_frame_counter() & 7)) SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
        o_move(o);
        if (OB8(o, OBJ_status) & 1 ? o_wall_right(o, 8) < 0 : o_wall_left(o, -8) < 0) {
            o_delete(o);
            return;
        }
        break;
    }
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 63 - platforms on a conveyor belt (and their wheels)
 * ---------------------------------------------------------------------- */
#define LC_BASEX   0x30
#define LC_TX      0x34
#define LC_TY      0x36
#define LC_IDX     0x38
#define LC_LEN     0x39
#define LC_STEP    0x3A
#define LC_REV     0x3B
#define LC_GROUP   0x3C               /* group number (S1 kept a list pointer) */
#define LC_MAKER   0x2F

static const int16_t lcon_data[6][14] = {     /* count*4, base x, then (x, y) targets */
    { 6 * 4, 0x1070, 0x1078, 0x2F0 - 0xD6, 0x1070 + 0x4E, 0x2F0 - 0x90, 0x1070 + 0x4E, 0x2F0 + 0xA3,
      0x1070 + 0x1C, 0x2F0 + 0xD5, 0x1070 - 0x4E, 0x2F0 + 0xA0, 0x1070 - 0x4E, 0x2F0 - 0xAC },
    { 5 * 4, 0x1280, 0x1280 - 0x02, 0x377 - 0xF7, 0x1280 + 0x4E, 0x377 - 0xA7, 0x1280 + 0x4E, 0x377 + 0xF7,
      0x1280 - 0x4E, 0x377 + 0xA9, 0x1280 - 0x4E, 0x377 - 0xAB },
    { 4 * 4, 0xD68, 0xD68 - 0x46, 0x530 - 0xAE, 0xD68 - 0x46, 0x530 + 0xAE, 0xD68 + 0x46, 0x530 + 0xAE,
      0xD68 + 0x46, 0x530 - 0xAE },
    { 4 * 4, 0xDA0, 0xDA0 - 0x3E, 0x440 - 0x9E, 0xDA0 + 0x4E, 0x440 - 0x9E, 0xDA0 + 0x4E, 0x440 + 0x9E,
      0xDA0 - 0x3E, 0x440 + 0x9E },
    { 5 * 4, 0xD00, 0xD00 - 0x54, 0x310 - 0xCE, 0xD00 + 0xDE, 0x310 - 0xCE, 0xD00 + 0xDE, 0x310 + 0xCE,
      0xD00 - 0xAE, 0x310 + 0xCE, 0xD00 - 0xAE, 0x310 - 0x74 },
    { 4 * 4, 0x1300, 0x1300 - 0xAE, 0x264 - 0x5A, 0x1300 + 0xDE, 0x264 - 0x5A, 0x1300 + 0xDE, 0x264 + 0x5A,
      0x1300 - 0xAE, 0x264 + 0x5A },
};
static int16_t lcon_target(int g, int idx, int k)
{
    int n = 2 + idx / 2 + k;                   /* idx = byte offset (4 per pair) */
    return n < 14 ? lcon_data[g][n] : 0;
}

static void lcon_change_dir(uint32_t o)        /* LCon_ChangeDir */
{
    int16_t d2 = -0x100, d3 = -0x100;
    int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, LC_TX));
    uint16_t d0 = (uint16_t)dx;
    if (OB16(o, OBJ_x_pos) < OB16(o, LC_TX)) { d0 = (uint16_t)-dx; d2 = 0x100; }
    int16_t dy = (int16_t)(OB16(o, OBJ_y_pos) - OB16(o, LC_TY));
    uint16_t d1 = (uint16_t)dy;
    if (OB16(o, OBJ_y_pos) < OB16(o, LC_TY)) { d1 = (uint16_t)-dy; d3 = 0x100; }
    if (d1 >= d0) {                            /* mostly vertical */
        int32_t q = 0, r = 0;
        if (dx) { int32_t n = (int32_t)dx << 8; q = n / (int16_t)d1; r = n % (int16_t)d1; q = -q; }
        SET16(o, OBJ_x_vel, (uint16_t)q);
        SET16(o, OBJ_y_vel, (uint16_t)d3);
        SET16(o, OBJ_x_pos + 2, (uint16_t)r);
        SET16(o, OBJ_y_pos + 2, 0);
    } else {
        int32_t q = 0, r = 0;
        if (dy) { int32_t n = (int32_t)dy << 8; q = n / (int16_t)d0; r = n % (int16_t)d0; q = -q; }
        SET16(o, OBJ_y_vel, (uint16_t)q);
        SET16(o, OBJ_x_vel, (uint16_t)d2);
        SET16(o, OBJ_y_pos + 2, (uint16_t)r);
        SET16(o, OBJ_x_pos + 2, 0);
    }
}

static void lcon_next(uint32_t o)              /* loc_12534 */
{
    uint8_t d1 = (uint8_t)(OB8(o, LC_IDX) + OB8(o, LC_STEP));
    if (d1 >= OB8(o, LC_LEN)) d1 = (d1 & 0x80) ? (uint8_t)(OB8(o, LC_LEN) - 4) : 0;
    SET8(o, LC_IDX, d1);
    int g = OB8(o, LC_GROUP);
    SET16(o, LC_TX, (uint16_t)lcon_target(g, d1, 0));
    SET16(o, LC_TY, (uint16_t)lcon_target(g, d1, 1));
    lcon_change_dir(o);
}

static void lcon_move(uint32_t o)              /* sub_12502 */
{
    if (r8(RAM_Level_trigger_array + 0x0E) && !OB8(o, LC_REV)) {
        SET8(o, LC_REV, 1);
        s_conveyrev = 1;
        SET8(o, LC_STEP, (uint8_t)-(int8_t)OB8(o, LC_STEP));
        lcon_next(o);
    } else if (OB16(o, OBJ_x_pos) == OB16(o, LC_TX) && OB16(o, OBJ_y_pos) == OB16(o, LC_TY)) {
        lcon_next(o);
    }
    o_move(o);
}

static void conveyor(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub & 0x80) {                          /* platform maker */
            SET8(o, LC_MAKER, sub);
            int k = sub & 0x7F;
            if (s_obj63[k & 15]) { o_delete(o); return; }
            s_obj63[k & 15] = 1;
            uint32_t list = tri_rd32(tri_rd32(TRI_BANK_BASE + TB_BLOBS) + TRI_BLOB_S1_PTFM_LISTS * 4u);
            uint32_t a2 = list + tri_rd16(list + (uint32_t)((k * 2) & 0x1E));
            int n = tri_rd16(a2);
            a2 += 2;
            uint32_t a1 = o;
            for (int i = 0; i <= n; i++, a2 += 6) {
                if (i) {
                    a1 = o_alloc();
                    if (!a1) break;
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x63 * 4);
                }
                SET16(a1, OBJ_x_pos, tri_rd16(a2));
                SET16(a1, OBJ_y_pos, tri_rd16(a2 + 2));
                SET8(a1, OBJ_subtype, tri_rd8(a2 + 5));
            }
            return;
        }
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_LCONV, 4, 4, 0x10, 0x10);
        if (sub == 0x7F) {                         /* wheel */
            SET8(o, OBJ_routine, 6);
            o_init(o, S1A_LWHEEL, 4, 1, 0x10, 0x10);
            goto wheel;
        }
        SET8(o, OBJ_frame, 4);
        int g = (sub >> 4) & 0x0F;
        if (g > 5) g = 5;
        SET8(o, LC_GROUP, g);
        SET8(o, LC_LEN, (uint8_t)lcon_data[g][0]);
        SET16(o, LC_BASEX, (uint16_t)lcon_data[g][1]);
        uint8_t d1 = (uint8_t)((sub & 0x0F) * 4);
        SET8(o, LC_IDX, d1);
        SET8(o, LC_STEP, 4);
        if (s_conveyrev) {
            SET8(o, LC_REV, 1);
            SET8(o, LC_STEP, (uint8_t)-4);
            d1 = (uint8_t)(d1 - 4);
            if (d1 >= OB8(o, LC_LEN)) d1 = (d1 & 0x80) ? (uint8_t)(OB8(o, LC_LEN) - 4) : 0;
            SET8(o, LC_IDX, d1);
        }
        SET16(o, LC_TX, (uint16_t)lcon_target(g, d1, 0));
        SET16(o, LC_TY, (uint16_t)lcon_target(g, d1, 1));
        lcon_change_dir(o);
        /* fall through */
    }
    case 2: {                                      /* LCon_Platform */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        lcon_move(o);
        o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
        break;
    }
    default:                                       /* LCon_Wheel */
    wheel:
        if (!(o_frame_counter() & 3))
            SET8(o, OBJ_frame, (OB8(o, OBJ_frame) + (s_conveyrev ? -1 : 1)) & 3);
        remember(o);
        return;
    }
    /* out_of_range on the group's base x; LZ3 keeps platforms just left */
    uint16_t d0 = (uint16_t)((OB16(o, LC_BASEX) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80));
    if (d0 > 0x280 && !(tri_act() == 2 && d0 >= 0xFF80)) {
        uint8_t m = OB8(o, LC_MAKER);
        if (m & 0x80) s_obj63[m & 15] = 0;
        o_delete(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 65 - waterfalls
 * ---------------------------------------------------------------------- */
extern uint8_t tri_lz_s1_chunk(int col, int row);

static void waterfall(uint32_t o)
{
    uint8_t r = OB8(o, OBJ_routine);
    if (r == 0) {
        r = 4;
        o_init(o, S1A_WFALL, 4, 1, 0x18, 0x20);
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub & 0x80) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_frame, sub & 0x0F);
        if ((sub & 0x0F) == 9) {
            SET16(o, OBJ_priority, 0);
            r = 2;
            if (sub & 0x40) r = 6;
            if (sub & 0x20) r = 8;
        }
        SET8(o, OBJ_routine, r);
    }
    switch (r) {
    case 6:                                        /* WFall_OnWater */
        SET16(o, OBJ_y_pos, r16(RAM_Water_level) - 0x10);
        o_animate(o, o_asset_anim(S1A_WFALL));
        break;
    case 8:                                        /* WFall_Priority */
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) & 0x7FFF);
        if (tri_lz_s1_chunk(6, 2) == 7) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        o_animate(o, o_asset_anim(S1A_WFALL));
        break;
    case 2:
        o_animate(o, o_asset_anim(S1A_WFALL));
        break;
    default:
        break;
    }
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 6F - SBZ spinning platforms that travel round a conveyor loop
 * (LCon_ChangeDir and the platform-maker lists are shared with object 63)
 * ---------------------------------------------------------------------- */
static const int16_t spinc_data[6][10] = {     /* base x, then 4 (x, y) targets */
    { 0xE80, 0xE80 - 0x6C, 0x358 + 0x18, 0xE80 + 0x6F, 0x358 - 0x56, 0xE80 + 0x6F, 0x358 - 0x18, 0xE80 - 0x6C, 0x358 + 0x56 },
    { 0xF80, 0xF80 - 0x6C, 0x2C8 + 0x18, 0xF80 + 0x6F, 0x2C8 - 0x56, 0xF80 + 0x6F, 0x2C8 - 0x18, 0xF80 - 0x6C, 0x2C8 + 0x56 },
    { 0x1080, 0x1080 - 0x6C, 0x228 + 0x48, 0x1080 + 0x6F, 0x228 - 0x26, 0x1080 + 0x6F, 0x228 + 0x18, 0x1080 - 0x6C, 0x228 + 0x86 },
    { 0xF80, 0xF80 - 0x6C, 0x558 + 0x18, 0xF80 + 0x6F, 0x558 - 0x56, 0xF80 + 0x6F, 0x558 - 0x18, 0xF80 - 0x6C, 0x558 + 0x56 },
    { 0x1B80, 0x1B80 - 0x6C, 0x658 + 0x18, 0x1B80 + 0x6F, 0x658 - 0x56, 0x1B80 + 0x6F, 0x658 - 0x18, 0x1B80 - 0x6C, 0x658 + 0x56 },
    { 0x1C80, 0x1C80 - 0x6C, 0x5C8 + 0x18, 0x1C80 + 0x6F, 0x5C8 - 0x56, 0x1C80 + 0x6F, 0x5C8 - 0x18, 0x1C80 - 0x6C, 0x5C8 + 0x56 },
};

static void spinc_target(uint32_t o, uint8_t d1)
{
    int g = OB8(o, LC_GROUP);
    SET16(o, LC_TX, (uint16_t)spinc_data[g][1 + d1 / 2]);
    SET16(o, LC_TY, (uint16_t)spinc_data[g][2 + d1 / 2]);
    if (d1 == 0) SET8(o, OBJ_anim, 1);
    if (d1 == 8) SET8(o, OBJ_anim, 0);
    lcon_change_dir(o);
}

static void spinc_move(uint32_t o)              /* loc_16424 */
{
    if (OB16(o, OBJ_x_pos) == OB16(o, LC_TX) && OB16(o, OBJ_y_pos) == OB16(o, LC_TY)) {
        uint8_t d1 = (uint8_t)(OB8(o, LC_IDX) + OB8(o, LC_STEP));
        if (d1 >= OB8(o, LC_LEN)) d1 = (d1 & 0x80) ? (uint8_t)(OB8(o, LC_LEN) - 4) : 0;
        SET8(o, LC_IDX, d1);
        spinc_target(o, d1);
    }
    o_move(o);
}

static void spin_convey(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub & 0x80) {                          /* platform maker (SBZ lists) */
            SET8(o, LC_MAKER, sub);
            int k = sub & 0x7F;
            if (s_obj63[k & 15]) { o_delete(o); return; }
            s_obj63[k & 15] = 1;
            uint32_t list = tri_rd32(tri_rd32(TRI_BANK_BASE + TB_BLOBS) + TRI_BLOB_S1_PTFM_LISTS * 4u);
            uint32_t a2 = list + tri_rd16(list + 16u + (uint32_t)((k * 2) & 0x0E));
            int n = tri_rd16(a2);
            a2 += 2;
            uint32_t a1 = o;
            for (int i = 0; i <= n; i++, a2 += 6) {
                if (i) {
                    a1 = o_alloc();
                    if (!a1) break;
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x6F * 4);
                }
                SET16(a1, OBJ_x_pos, tri_rd16(a2));
                SET16(a1, OBJ_y_pos, tri_rd16(a2 + 2));
                SET8(a1, OBJ_subtype, tri_rd8(a2 + 5));
            }
            return;
        }
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SPINC, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        int g = (sub >> 4) & 0x0F;
        if (g > 5) g = 5;
        SET8(o, LC_GROUP, g);
        SET8(o, LC_LEN, 0x10);
        SET16(o, LC_BASEX, (uint16_t)spinc_data[g][0]);
        uint8_t d1 = (uint8_t)((sub & 0x0F) * 4);
        SET8(o, LC_IDX, d1);
        SET8(o, LC_STEP, 4);
        if (s_conveyrev) {
            SET8(o, LC_REV, 1);
            SET8(o, LC_STEP, (uint8_t)-4);
            d1 = (uint8_t)(d1 - 4);
            if (d1 >= 0x10) d1 = (d1 & 0x80) ? 0x0C : 0;
            SET8(o, LC_IDX, d1);
        }
        spinc_target(o, d1);
    }
    o_animate(o, o_asset_anim(S1A_SPINC));     /* SpinC_Solid */
    if (!OB8(o, OBJ_frame)) {
        uint16_t oldx = OB16(o, OBJ_x_pos);
        spinc_move(o);
        o_solid(o, 0x1B, 7, 8, oldx);
    } else {
        for (int i = 0; i < 2; i++) {
            uint8_t bit = i ? ST_P2_STAND : ST_P1_STAND;
            if (!(OB8(o, OBJ_status) & bit)) continue;
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
            uint32_t p = o_player(i);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
        }
        spinc_move(o);
    }
    uint16_t d0 = (uint16_t)((OB16(o, LC_BASEX) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80));
    if (d0 > 0x280 && !(tri_act() == 2 && d0 >= 0xFF80)) {
        uint8_t m = OB8(o, LC_MAKER);
        if (m & 0x80) s_obj63[m & 15] = 0;
        o_delete(o);
        return;
    }
    o_draw(o);
}

void s1_obj_lz_register(TriObjFn *t)
{
    t[0x6F] = spin_convey;
    t[0x0B] = pole;
    t[0x0C] = flap_door;
    t[0x16] = harpoon;
    t[0x1B] = surface;
    t[0x2C] = jaws;
    t[0x2D] = burrobot;
    t[0x60] = orbinaut;
    t[0x61] = lz_block;
    t[0x62] = gargoyle;
    t[0x63] = conveyor;
    t[0x65] = waterfall;
}
