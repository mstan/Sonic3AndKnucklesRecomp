/*
 * s2_obj_ehz.c — Sonic 2 Emerald Hill objects, transcribed from s2disasm.
 *
 * S2 object variables at objoff_2A/2C would land on S3K's status/subtype, so
 * they live in S3K's free scratch bytes ($30-$47) here.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25

/* Obj_GetOrientationToPlayer: the closer player (horizontally). d0 = 2 if the
 * player is left of the object, d1 = 2 if above; dx/dy = object - player. */
uint32_t s2_orient_player;

int s2_orient(uint32_t o, int *d0, int *d1, int16_t *dx, int16_t *dy)
{
    uint32_t a1 = RAM_Player_1, a2 = RAM_Player_2;
    int16_t d2 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(a1, OBJ_x_pos));
    int16_t d3 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(a2, OBJ_x_pos));
    uint16_t d4 = (uint16_t)(d2 < 0 ? -d2 : d2), d5 = (uint16_t)(d3 < 0 ? -d3 : d3);
    if (d4 > d5) { a1 = a2; d2 = d3; }                     /* sidekick is closer */
    s2_orient_player = a1;
    if (d0) *d0 = d2 < 0 ? 2 : 0;
    int16_t yd = (int16_t)(OB16(o, OBJ_y_pos) - OB16(a1, OBJ_y_pos));
    if (d1) *d1 = (uint16_t)OB16(o, OBJ_y_pos) < (uint16_t)OB16(a1, OBJ_y_pos) ? 2 : 0;
    if (dx) *dx = d2;
    if (dy) *dy = yd;
    return 0;
}

/* ------------------------------------------------------------------------
 * Object 98 - badnik projectiles (Coconuts' coconut, Spiny's shot)
 * ---------------------------------------------------------------------- */
#define PROJ_KIND  S2_PROJ_KIND
#define PROJ_COCONUT S2_PROJ_COCONUT

static void projectile(uint32_t o)
{
    if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }  /* Obj98_Main */
    switch (OB8(o, PROJ_KIND)) {
    case PROJ_COCONUT:                                          /* Obj98_CoconutFall */
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x20);
        o_move(o);
        break;
    case S2_PROJ_SPINY:                                         /* Obj98_SpinyShotFall */
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x20);
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_SPINYSHOT));
        break;
    case S2_PROJ_MOVE:                                          /* ObjectMove (Rexon's fireball) */
        o_move(o);
        break;
    case S2_PROJ_NEBULA:                                        /* Obj98_NebulaBombFall: blinks lines 1/0 */
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) ^ 0x2000);
        o_fall(o);
        break;
    case S2_PROJ_TURTLOID:                                      /* Obj98_TurtloidShotMove */
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_TURTLESHOT));
        break;
    case S2_PROJ_ANIM:                                          /* Obj98_CluckerShotMove / _WallTurretShotMove */
        o_move(o);
        o_animate_s2(o, o_asset_anim(OB8(o, PROJ_KIND + 1)));
        break;
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 4B - Buzzer (EHZ bee badnik), its exhaust flame and stinger shot
 * ---------------------------------------------------------------------- */
#define BZ_TURN    0x30               /* Obj4B_turn_delay (word) */
#define BZ_MOVE    0x32               /* Obj4B_move_timer (word) */
#define BZ_SHOT    0x34               /* Obj4B_shot_timer (word) */
#define BZ_NOSHOOT 0x36               /* Obj4B_shooting_flag */
#define BZ_PARENT  0x3C               /* Obj4B_parent (long) */

static void buzzer_check_players(uint32_t o)        /* Obj4B_ChkPlayers */
{
    if (OB8(o, BZ_NOSHOOT)) return;
    uint32_t a1 = (r16(RAM_Level_frame_counter) & 1) ? RAM_Player_2 : RAM_Player_1;
    int16_t d1 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(a1, OBJ_x_pos));
    int16_t d0 = d1 < 0 ? (int16_t)-d1 : d1;
    if (d0 < 0x28 || d0 > 0x30) return;
    int flip = OB8(o, OBJ_render) & 1;
    if (d1 >= 0 ? flip : !flip) return;                 /* facing away */
    SET8(o, BZ_NOSHOOT, 0xFF);
    SET8(o, OBJ_2nd, 2);
    SET8(o, OBJ_anim, 3);
    SET16(o, BZ_SHOT, 0x32);
}

static void buzzer(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj4B_Init */
        o_init(o, S2A_BUZZER, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0x0A);
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, 0x1F, 0x18);
        SET8(o, OBJ_routine, 2);
        uint32_t a1 = o_alloc_after(o);                 /* exhaust flame */
        if (a1) {
            SET32(a1, OBJ_code, S2(0x4B));
            SET8(a1, OBJ_routine, 4);
            o_init(a1, S2A_BUZZER, OB8(o, OBJ_render), 4, 0x10, 0x10);
            SET8(a1, OBJ_status, OB8(o, OBJ_status));
            SET8(a1, OBJ_anim, 1);
            SET32(a1, BZ_PARENT, RAMPTR(o));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(o, BZ_MOVE, 0x100);
            SET16(o, OBJ_x_vel, (OB8(o, OBJ_render) & 1) ? 0x100 : (uint16_t)-0x100);
        }
        return;
    }
    case 2:                                             /* Obj4B_Main */
        if (!OB8(o, OBJ_2nd)) {                         /* Obj4B_Roaming */
            buzzer_check_players(o);
            SET16(o, BZ_TURN, OB16(o, BZ_TURN) - 1);
            int16_t d0 = (int16_t)OB16(o, BZ_TURN);
            if (d0 == 0x0F) {                           /* Obj4B_TurnAround */
                SET8(o, BZ_NOSHOOT, 0);
                SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
                SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
                SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
                SET16(o, BZ_MOVE, 0x100);
            } else if (d0 < 0) {
                SET16(o, BZ_MOVE, OB16(o, BZ_MOVE) - 1);
                if ((int16_t)OB16(o, BZ_MOVE) > 0) o_move(o);
                else SET16(o, BZ_TURN, 0x1E);
            }
        } else {                                        /* Obj4B_Shooting */
            int16_t d0 = (int16_t)(OB16(o, BZ_SHOT) - 1);
            if (d0 < 0) SET8(o, OBJ_2nd, 0);
            else {
                SET16(o, BZ_SHOT, (uint16_t)d0);
                if (d0 == 0x14) {                       /* Obj4B_ShootProjectile */
                    uint32_t a1 = o_alloc_after(o);
                    if (a1) {
                        SET32(a1, OBJ_code, S2(0x4B));
                        SET8(a1, OBJ_routine, 6);
                        o_init(a1, S2A_BUZZER, OB8(o, OBJ_render), 4, 0x10, 0x10);
                        SET8(a1, OBJ_coll_flags, 0x98);
                        SET8(a1, OBJ_status, OB8(o, OBJ_status));
                        SET8(a1, OBJ_anim, 2);
                        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x18);
                        int16_t dx = 0x0D;
                        SET16(a1, OBJ_y_vel, 0x180);
                        SET16(a1, OBJ_x_vel, (uint16_t)-0x180);
                        if (OB8(a1, OBJ_render) & 1) { SET16(a1, OBJ_x_vel, 0x180); dx = -dx; }
                        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + dx);
                    }
                }
            }
        }
        o_animate_s2(o, o_asset_anim(S2A_BUZZER));
        o_touch(o);
        o_remember(o);
        return;
    case 4: {                                           /* Obj4B_Flame */
        uint32_t a1 = r32(o + BZ_PARENT) & 0xFFFFFF;
        if (r32(a1) != S2(0x4B)) { o_delete(o); return; }   /* (fixed parent check) */
        if ((int16_t)OB16(a1, BZ_TURN) >= 0) return;    /* hovering: no flame */
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        SET8(o, OBJ_status, OB8(a1, OBJ_status));
        SET8(o, OBJ_render, OB8(a1, OBJ_render));
        o_animate_s2(o, o_asset_anim(S2A_BUZZER));
        o_remember(o);
        return;
    }
    default:                                            /* Obj4B_Projectile */
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_BUZZER));
        o_touch(o);
        o_remember(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 5C - Masher (jumping piranha)
 * ---------------------------------------------------------------------- */
#define MS_Y0  0x30

static void masher(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj5C_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_MASHER, 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 9);
        SET16(o, OBJ_y_vel, (uint16_t)-0x400);
        SET16(o, MS_Y0, OB16(o, OBJ_y_pos));
    }
    o_animate_s2(o, o_asset_anim(S2A_MASHER));             /* Obj5C_Main */
    o_move(o);
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
    uint16_t d0 = OB16(o, MS_Y0);
    if (d0 < OB16(o, OBJ_y_pos)) {
        SET16(o, OBJ_y_pos, d0);
        SET16(o, OBJ_y_vel, (uint16_t)-0x500);
    }
    SET8(o, OBJ_anim, 1);
    d0 = (uint16_t)(d0 - 0xC0);
    if (d0 < OB16(o, OBJ_y_pos)) {
        SET8(o, OBJ_anim, 0);
        if ((int16_t)OB16(o, OBJ_y_vel) >= 0) SET8(o, OBJ_anim, 2);
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 9D - Coconuts (monkey badnik)
 * ---------------------------------------------------------------------- */
#define CN_TIMER   0x30               /* Obj9D_timer */
#define CN_CLIMB   0x32               /* Obj9D_climb_table_index (word) */
#define CN_ATTACK  0x34               /* Obj9D_attack_timer */

static const int8_t cn_climb[12] = { -1, 0x20, 1, 0x18, -1, 0x10, 1, 0x28, -1, 0x20, 1, 0x10 };

static void coconuts_climb_dir(uint32_t o)           /* Obj9D_SetClimbingDirection */
{
    uint16_t d0 = OB16(o, CN_CLIMB);
    if (d0 >= 0x0C) d0 = 0;
    SET16(o, CN_CLIMB, d0 + 2);
    SET8(o, OBJ_y_vel, (uint8_t)cn_climb[d0]);       /* high byte: speed */
    SET8(o, OBJ_y_vel + 1, 0);
    SET8(o, CN_TIMER, (uint8_t)cn_climb[d0 + 1]);
}

static void coconuts(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj9D_Init (LoadSubObject) */
        o_init(o, S2A_COCONUTS, OB8(o, OBJ_render) | 4, 5, 0x0C, 0x10);
        SET8(o, OBJ_coll_flags, 9);
        SET8(o, CN_TIMER, 0x10);
        SET8(o, OBJ_routine, 2);
        break;
    case 2: {                                           /* Obj9D_Idle */
        int d0, d1;
        int16_t d2;
        s2_orient(o, &d0, &d1, &d2, NULL);
        SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        if (d0) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        }
        if ((uint16_t)(d2 + 0x60) < 0xC0) {
            if (!OB8(o, CN_ATTACK)) {                   /* Obj9D_StartThrowing */
                SET8(o, OBJ_routine, 6);
                SET8(o, OBJ_frame, 1);
                SET8(o, CN_TIMER, 8);
                SET8(o, CN_ATTACK, 0x20);
                break;
            }
            SET8(o, CN_ATTACK, OB8(o, CN_ATTACK) - 1);
        }
        SET8(o, CN_TIMER, OB8(o, CN_TIMER) - 1);
        if ((int8_t)OB8(o, CN_TIMER) < 0) {             /* Obj9D_StartClimbing */
            SET8(o, OBJ_routine, 4);
            coconuts_climb_dir(o);
        }
        break;
    }
    case 4:                                             /* Obj9D_Climbing */
        SET8(o, CN_TIMER, OB8(o, CN_TIMER) - 1);
        if (!OB8(o, CN_TIMER)) {
            SET8(o, OBJ_routine, 2);
            SET8(o, CN_TIMER, 0x10);
            break;
        }
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_COCONUTS));
        break;
    default:                                            /* Obj9D_Throwing */
        SET8(o, CN_TIMER, OB8(o, CN_TIMER) - 1);
        if ((int8_t)OB8(o, CN_TIMER) >= 0) break;
        if (!OB8(o, OBJ_2nd)) {                         /* hand raised -> lowered, throw */
            SET8(o, OBJ_2nd, 2);
            SET8(o, CN_TIMER, 8);
            SET8(o, OBJ_frame, 2);
            uint32_t a1 = o_alloc();                    /* Obj9D_CreateCoconut */
            if (a1) {
                SET32(a1, OBJ_code, S2(0x98));
                SET8(a1, OBJ_routine, 2);
                o_init(a1, S2A_COCONUTS, 4 | 0x80, 4, 8, 8);
                SET8(a1, OBJ_coll_flags, 0x8B);
                SET8(a1, PROJ_KIND, PROJ_COCONUT);
                SET8(a1, OBJ_frame, 3);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x0D);
                int left = OB8(o, OBJ_render) & 1;
                SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (left ? -0x0B : 0x0B));
                SET16(a1, OBJ_x_vel, left ? 0x100 : (uint16_t)-0x100);
                SET16(a1, OBJ_y_vel, (uint16_t)-0x100);
            }
        } else {                                        /* lowered: back to climbing */
            SET8(o, OBJ_2nd, 0);
            SET8(o, OBJ_routine, 4);
            SET8(o, CN_TIMER, 8);
            coconuts_climb_dir(o);
        }
        break;
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 49 - EHZ waterfall (frame follows the players)
 * ---------------------------------------------------------------------- */
static void waterfall(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_WATERFALL, 4, 0, 0x20, 0x80);
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    uint16_t x1 = (uint16_t)(OB16(o, OBJ_x_pos) - 0x40), x2 = (uint16_t)(OB16(o, OBJ_x_pos) + 0x40);
    uint8_t d3 = OB8(o, OBJ_subtype);
    uint16_t px = r16(RAM_Player_1 + OBJ_x_pos);
    SET8(o, OBJ_frame, 0);
    if (px >= x1 && px < x2) SET8(o, OBJ_frame, 1);
    else {
        px = r16(RAM_Player_2 + OBJ_x_pos);
        if (px >= x1 && px < x2) SET8(o, OBJ_frame, 1);
    }
    SET8(o, OBJ_frame, OB8(o, OBJ_frame) + d3);
    o_draw(o);
}


/* ------------------------------------------------------------------------
 * Object 11 - EHZ log bridge: S3K's Obj_TensionBridge is S2's Obj11 grown
 * up (same subsprite logs, same sag); run it with the S2 bridge art.
 * ---------------------------------------------------------------------- */
static void bridge(uint32_t o)
{
    A(0) = RAMPTR(o);
    tri_jsr(S3K_Obj_TensionBridge);
    uint32_t maps = o_asset_maps(S2A_BRIDGE);
    uint16_t tile = o_asset_tile(S2A_BRIDGE);
    SET32(o, OBJ_mappings, maps);
    SET16(o, OBJ_art_tile, tile);
    for (int k = 0; k < 2; k++) {
        uint32_t c = r32(o + 0x30 + k * 4) & 0xFFFFFF;
        if (c >= 0xFF0000u) {
            SET32(c, OBJ_mappings, maps);
            SET16(c, OBJ_art_tile, tile);
        }
    }
}

/* ------------------------------------------------------------------------
 * Object 1C - scenery (EHZ: subtype 2, the bridge stake)
 * ---------------------------------------------------------------------- */
static void scenery(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj1C_Init: Obj1C_InitData */
        SET8(o, OBJ_routine, 2);
        switch (OB8(o, OBJ_subtype)) {
        case 3:
            if (tri_zone() == TRI_S2_MTZ) {             /* MTZ: the rope end (line 1) */
                o_init(o, S2A_BOLTEND1, OB8(o, OBJ_render) | 4, 6, 0x10, 0x10);
                SET8(o, OBJ_frame, 2);
                break;
            }
            /* fall through */
        case 4: case 5:                                 /* HTZ zipline poles */
            o_init(o, S2A_ZIPLINE, OB8(o, OBJ_render) | 4, 4, 8, 0x10);
            SET8(o, OBJ_frame, OB8(o, OBJ_subtype) == 3 ? 3 : OB8(o, OBJ_subtype) == 4 ? 3 : 4);
            break;
        case 6:                                         /* the zipline's hook, left on the wire */
            o_init(o, S2A_ZIPLINE, OB8(o, OBJ_render) | 4, 1, 0x20, 0x10);
            SET8(o, OBJ_frame, 1);
            break;
        case 7: case 8:                                 /* HTZ level-art scenery */
            o_init(o, S2A_LVL1C, OB8(o, OBJ_render) | 4, 1, 8, 0x10);
            SET8(o, OBJ_frame, OB8(o, OBJ_subtype) - 7);
            break;
        case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:   /* OOZ oil falls */
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: {
            static const uint8_t radii[11] = { 0, 0, 0, 0x30, 0x40, 0x60, 0, 0, 0x30, 0x40, 0x50 };   /* Obj1C_Radii */
            uint8_t sub = OB8(o, OBJ_subtype);
            int w = sub < 0x10 ? 4 : sub < 0x12 ? 0x18 : 8;
            uint8_t h = radii[sub - 0x0A];
            o_init(o, sub < 0x10 ? S2A_OILFALL : S2A_OILFALL2, OB8(o, OBJ_render) | 4, 4, w, h ? h : 0x10);
            SET8(o, OBJ_frame, sub < 0x10 ? sub - 0x0A : sub - 0x10);
            if (h) SET8(o, OBJ_y_radius, h);
            break;
        }
        case 0: case 1:                                 /* MTZ: bolt ends (line 2) */
            o_init(o, S2A_BOLTEND2, OB8(o, OBJ_render) | 4, 6, 4, 0x10);
            SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
            break;
        default:                                        /* EHZ: subtype 2, the bridge stake */
            o_init(o, S2A_BRIDGE, OB8(o, OBJ_render) | 4, 1, 4, 0x10);
            SET8(o, OBJ_frame, 1);
            break;
        }
    }
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 18 - platforms (stationary / moving / falling), EHZ art
 * ---------------------------------------------------------------------- */
#define PF_YACT   0x30                /* obj18_y_actual (long) */
#define PF_Y0     0x34                /* obj18_y_origin */
#define PF_X0     0x36                /* obj18_x_origin */
#define PF_SAG    0x38                /* obj18_y_offset */
#define PF_DELAY  0x3A                /* obj18_delay (word) */

static void platform_drop(uint32_t o, uint32_t p)       /* Obj18_DropPlayer */
{
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
    SET8(p, OBJ_routine, 2);
    SET16(p, OBJ_y_vel, OB16(o, OBJ_y_vel));
}

static void platform_move(uint32_t o)                   /* Obj18_Move */
{
    int8_t a = (int8_t)OB8(o, OBJ_angle);
    int8_t d1;
    switch (OB8(o, OBJ_subtype) & 0x0F) {
    case 1: d1 = (int8_t)(a - 0x40); SET16(o, OBJ_x_pos, OB16(o, PF_X0) + d1); break;
    case 5: d1 = (int8_t)(-a + 0x40); SET16(o, OBJ_x_pos, OB16(o, PF_X0) + d1); break;
    case 2: d1 = (int8_t)(a - 0x40); SET16(o, PF_YACT, OB16(o, PF_Y0) + d1); break;
    case 6: d1 = (int8_t)(-a + 0x40); SET16(o, PF_YACT, OB16(o, PF_Y0) + d1); break;
    case 0x0C: d1 = (int8_t)(o_osc(0x0C + 2) - 0x30); SET16(o, PF_YACT, OB16(o, PF_Y0) + d1); break;
    case 0x0D: d1 = (int8_t)(-o_osc(0x0C + 2) + 0x30); SET16(o, PF_YACT, OB16(o, PF_Y0) + d1); break;
    case 0x0A: d1 = (int8_t)(a - 0x40); SET16(o, PF_YACT, OB16(o, PF_Y0) + (d1 >> 1)); break;
    case 0x0B: d1 = (int8_t)(-a + 0x40); SET16(o, PF_YACT, OB16(o, PF_Y0) + (d1 >> 1)); break;
    case 3:                                             /* Obj18_Falling */
        if (!OB16(o, PF_DELAY)) {
            if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) SET16(o, PF_DELAY, 30);
        } else {
            SET16(o, PF_DELAY, OB16(o, PF_DELAY) - 1);
            if (!OB16(o, PF_DELAY)) {
                SET16(o, PF_DELAY, 0x20);
                SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
            }
        }
        return;
    case 4:                                             /* Obj18_Fall */
        if (OB16(o, PF_DELAY)) {
            SET16(o, PF_DELAY, OB16(o, PF_DELAY) - 1);
            if (!OB16(o, PF_DELAY)) {
                if (OB8(o, OBJ_status) & ST_P1_STAND) { SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_STAND); platform_drop(o, RAM_Player_1); }
                if (OB8(o, OBJ_status) & ST_P2_STAND) { SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P2_STAND); platform_drop(o, RAM_Player_2); }
                SET8(o, OBJ_routine, 6);
            }
        }
        SET32(o, PF_YACT, r32(o + PF_YACT) + ((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8));
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        if ((uint16_t)(r16(RAM_Camera_max_Y_pos) + 224 + 0x40) < OB16(o, PF_YACT)) SET8(o, OBJ_routine, 4);
        return;
    default:
        return;                                         /* 0, 9 stationary; 7, 8 button (S1 leftover) */
    }
    SET8(o, OBJ_angle, o_osc(0x18 + 2));                /* Obj18_UpdateAngle */
}

static void platform_sag(uint32_t o)
{
    if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) {
        if (OB8(o, PF_SAG) != 0x40) SET8(o, PF_SAG, OB8(o, PF_SAG) + 4);
    } else if (OB8(o, PF_SAG)) SET8(o, PF_SAG, OB8(o, PF_SAG) - 4);
}

static void platform_nudge(uint32_t o)                  /* Obj18_Nudge */
{
    int16_t sn, cs;
    o_sincos(OB8(o, PF_SAG), &sn, &cs);
    SET16(o, OBJ_y_pos, (uint16_t)(((int32_t)sn * 0x400) >> 16) + OB16(o, PF_YACT));
}

static void platform(uint32_t o)
{
    static const uint8_t init[5][2] = { { 0x20, 0 }, { 0x20, 1 }, { 0x20, 2 }, { 0x40, 3 }, { 0x30, 4 } };
    uint16_t oldx;
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj18_Init */
        uint8_t sub = OB8(o, OBJ_subtype);
        int k = (sub >> 4) & 7;
        int arz = tri_zone() == TRI_S2_ARZ;             /* (ARZ: its own mappings, a lower full-solid block) */
        o_init(o, arz ? S2A_PLAT_ARZ : S2A_PLAT_EHZ, 4, 4, init[k % 5][0], 0x10);
        SET8(o, OBJ_frame, init[k % 5][1]);
        SET16(o, PF_YACT, OB16(o, OBJ_y_pos));
        SET16(o, PF_YACT + 2, 0);
        SET16(o, PF_Y0, OB16(o, OBJ_y_pos));
        SET16(o, PF_X0, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_angle, 0);
        SET8(o, OBJ_subtype, sub & 0x0F);
        if (sub & 0x80) {
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_y_radius, arz ? 0x28 : 0x30);
            SET8(o, OBJ_height_px, arz ? 0x28 : 0x30);
            goto full;
        }
        SET8(o, OBJ_routine, 2);
    }
    /* fall through */
    case 2:                                             /* Obj18_TopSolid */
        platform_sag(o);
        oldx = OB16(o, OBJ_x_pos);
        platform_move(o);
        platform_nudge(o);
        o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
        break;
    case 4:
        o_delete(o);
        return;
    case 6:                                             /* Obj18_NonSolid */
        platform_move(o);
        platform_nudge(o);
        break;
    default:
    full:                                               /* Obj18_FullSolid */
        platform_sag(o);
        oldx = OB16(o, OBJ_x_pos);
        platform_move(o);
        platform_nudge(o);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, oldx);
        break;
    }
    if (OB8(o, OBJ_routine) == 4) { o_delete(o); return; }
    if (o_out_of_range(o, OB16(o, PF_X0))) { o_delete_respawnable(o); return; }   /* Obj18_Despawn */
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 36 - spikes (upright / sideways / upside down, moving types 1-2)
 * ---------------------------------------------------------------------- */
#define SP_X0    0x30
#define SP_Y0    0x32
#define SP_OFF   0x34
#define SP_STATE 0x36
#define SP_TIMER 0x38

static void spikes_move(uint32_t o)                     /* MoveSpikes */
{
    int type = OB8(o, OBJ_subtype);
    if (type != 1 && type != 2) return;
    if (OB16(o, SP_TIMER)) {                            /* MoveSpikes_Delay */
        SET16(o, SP_TIMER, OB16(o, SP_TIMER) - 1);
        if (!OB16(o, SP_TIMER) && (OB8(o, OBJ_render) & 0x80)) o_sfx_s2(0xB6); /* SndID_SpikesMove */
    } else if (OB16(o, SP_STATE)) {
        int v = (int)OB16(o, SP_OFF) - 0x800;
        if (v < 0) { v = 0; SET16(o, SP_STATE, 0); SET16(o, SP_TIMER, 60); }
        SET16(o, SP_OFF, v);
    } else {
        int v = OB16(o, SP_OFF) + 0x800;
        if (v >= 0x2000) { v = 0x2000; SET16(o, SP_STATE, 1); SET16(o, SP_TIMER, 60); }
        SET16(o, SP_OFF, v);
    }
    int off = OB8(o, SP_OFF);
    if (type == 1) SET16(o, OBJ_y_pos, OB16(o, SP_Y0) + off);
    else SET16(o, OBJ_x_pos, OB16(o, SP_X0) + off);
}

static void spikes(uint32_t o)
{
    static const uint8_t init[8][2] = { { 0x10, 0x10 }, { 0x20, 0x10 }, { 0x30, 0x10 }, { 0x40, 0x10 },
                                        { 0x10, 0x10 }, { 0x10, 0x20 }, { 0x10, 0x30 }, { 0x10, 0x40 } };
    if (!OB8(o, OBJ_routine)) {                         /* Obj36_Init */
        uint8_t sub = OB8(o, OBJ_subtype);
        int k = (sub >> 4) & 7;
        SET8(o, OBJ_subtype, sub & 0x0F);
        o_init(o, k >= 4 ? S2A_HSPIKES : S2A_SPIKES, OB8(o, OBJ_render) | 4, 4, init[k][0], init[k][1]);
        SET8(o, OBJ_y_radius, init[k][1]);
        SET8(o, OBJ_frame, k);
        SET8(o, OBJ_routine, k >= 4 ? 4 : 2);
        if (OB8(o, OBJ_status) & ST_YFLIP) SET8(o, OBJ_routine, 6);
        SET16(o, SP_X0, OB16(o, OBJ_x_pos));
        SET16(o, SP_Y0, OB16(o, OBJ_y_pos));
        return;
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    spikes_move(o);
    int d1 = OB8(o, OBJ_width_px) + 0x0B, d2 = OB8(o, OBJ_y_radius);
    switch (OB8(o, OBJ_routine)) {
    case 2: {                                           /* Obj36_Upright */
        o_solid(o, d1, d2, d2 + 1, OB16(o, OBJ_x_pos));
        uint8_t st = OB8(o, OBJ_status);
        if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
        if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
        break;
    }
    case 4: {                                           /* Obj36_Sideways */
        o_solid(o, d1, d2, d2 + 1, oldx);
        uint32_t side = g_o_d6 >> 16;
        if (side & 1) { o_spike_hurt(o, RAM_Player_1); SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH); }
        if (side & 2) { o_spike_hurt(o, RAM_Player_2); SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P2_PUSH); }
        break;
    }
    default: {                                          /* Obj36_Upsidedown */
        o_solid(o, d1, d2, d2 + 1, OB16(o, OBJ_x_pos));
        uint32_t bottom = g_o_d6 >> 16;
        if (bottom & 4) o_spike_hurt(o, RAM_Player_1);
        if (bottom & 8) o_spike_hurt(o, RAM_Player_2);
        break;
    }
    }
    if (o_out_of_range(o, OB16(o, SP_X0))) { o_delete_respawnable(o); return; }   /* MarkObjGone2 */
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 06 - EHZ corkscrew: carries a fast enough player around the
 * spiral, setting flip_angle so S3K's own tumble frames play
 * ---------------------------------------------------------------------- */
#define P_FLIP_ANGLE   0x27
#define P_OBJCTRL      0x2E
#define P_FLIPS_LEFT   0x30
#define P_FLIP_SPEED   0x31

static const uint8_t spiral_flip[52] = {
    0x00, 0x00, 0x01, 0x01, 0x16, 0x16, 0x16, 0x16, 0x2C, 0x2C, 0x2C, 0x2C, 0x42, 0x42, 0x42, 0x42, 0x58, 0x58,
    0x58, 0x58, 0x6E, 0x6E, 0x6E, 0x6E, 0x84, 0x84, 0x84, 0x84, 0x9A, 0x9A, 0x9A, 0x9A, 0xB0, 0xB0,
    0xB0, 0xB0, 0xC6, 0xC6, 0xC6, 0xC6, 0xDC, 0xDC, 0xDC, 0xDC, 0xF2, 0xF2, 0xF2, 0xF2, 0x01, 0x01,
    0x00, 0x00,
};
static const int8_t spiral_cos[416] = {
     32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
     32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 30, 30, 30,
     30, 30, 30, 30, 30, 30, 29, 29, 29, 29, 29, 28, 28, 28, 28, 27, 27, 27, 27, 26, 26, 26, 25, 25, 25, 24, 24, 24, 23, 23, 22, 22,
     21, 21, 20, 20, 19, 18, 18, 17, 16, 16, 15, 14, 14, 13, 12, 12, 11, 10, 10,  9,  8,  8,  7,  6,  6,  5,  4,  4,  3,  2,  2,  1,
      0, -1, -2, -2, -3, -4, -4, -5, -6, -7, -7, -8, -9, -9,-10,-10,-11,-11,-12,-12,-13,-14,-14,-15,-15,-16,-16,-17,-17,-18,-18,-19,
    -19,-19,-20,-21,-21,-22,-22,-23,-23,-24,-24,-25,-25,-26,-26,-27,-27,-28,-28,-28,-29,-29,-30,-30,-30,-31,-31,-31,-32,-32,-32,-33,
    -33,-33,-33,-34,-34,-34,-35,-35,-35,-35,-35,-35,-35,-35,-36,-36,-36,-36,-36,-36,-36,-36,-36,-37,-37,-37,-37,-37,-37,-37,-37,-37,
    -37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-37,-36,-36,-36,-36,-36,-36,-36,-35,-35,-35,-35,-35,
    -35,-35,-35,-34,-34,-34,-33,-33,-33,-33,-32,-32,-32,-31,-31,-31,-30,-30,-30,-29,-29,-28,-28,-28,-27,-27,-26,-26,-25,-25,-24,-24,
    -23,-23,-22,-22,-21,-21,-20,-19,-19,-18,-18,-17,-16,-16,-15,-14,-14,-13,-12,-11,-11,-10, -9, -8, -7, -7, -6, -5, -4, -3, -2, -1,
      0,  1,  2,  3,  4,  5,  6,  7,  8,  8,  9, 10, 10, 11, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21,
     21, 22, 22, 23, 23, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29,
     29, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
     32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
};

static void ride(uint32_t o, uint32_t p, int bit)       /* RideObject_SetRide */
{
    D(6) = (uint32_t)bit;
    A(0) = RAMPTR(o);
    A(1) = RAMPTR(p);
    tri_jsr(S3K_RideObject_SetRide);
}

static void spiral_player(uint32_t o, uint32_t p, int bit)
{
    if (!o_player_active(p)) return;
    if (!(OB8(o, OBJ_status) & (1 << bit))) {
        uint8_t ps = OB8(p, OBJ_status);
        if (ps & 2) return;                             /* in the air */
        int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        int on = ps & 8;
        int lo = on ? 0xB0 : 0xC0, hi = on ? 0xC0 : 0xD0;
        if ((int16_t)OB16(p, OBJ_x_vel) < 0) { if (d0 < lo || d0 > hi) return; }
        else if (d0 > -lo || d0 < -hi) return;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - 0x10) >= 0x30) return;
        if (!on && OB8(p, P_OBJCTRL)) return;
        ride(o, p, bit);
        return;
    }
    int16_t gv = (int16_t)OB16(p, OBJ_ground_vel);
    int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0xD0);
    if ((gv < 0 ? -gv : gv) < 0x600 || (OB8(p, OBJ_status) & 2) || d0 < 0 || d0 >= 0x1A0) {
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~8);    /* falls off */
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(1 << bit));
        SET8(p, P_FLIPS_LEFT, 0);
        SET8(p, P_FLIP_SPEED, 4);
        return;
    }
    if (!(OB8(p, OBJ_status) & 8)) return;
    int16_t y = (int16_t)(OB16(o, OBJ_y_pos) + spiral_cos[d0]);
    y = (int16_t)(y - (OB8(p, OBJ_y_radius) - 0x13));
    SET16(p, OBJ_y_pos, (uint16_t)y);
    SET8(p, P_FLIP_ANGLE, spiral_flip[(d0 >> 3) & 0x3F]);
}

/* Obj06_Cylinder (MTZ, subtype < 0): runs the player round a turning cylinder */
#define P_FLIP_TYPE    0x2D            /* S3K flip_type: bit 7 = S2 flip_turned */
extern uint8_t s2_mtz_cyl_angle[2];     /* MTZCylinder_Angle_Sonic / _Tails */

static void cylinder_player(uint32_t o, uint32_t p, int bit, uint8_t *ang)
{
    if (!o_player_active(p)) return;
    if (!(OB8(o, OBJ_status) & (1 << bit))) {
        int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (d0 < -0xC0 || d0 >= 0xC0) return;
        uint16_t d2 = OB16(p, OBJ_y_pos);
        uint16_t d1 = (uint16_t)(d2 + (int8_t)OB8(p, OBJ_y_radius) + 4);
        uint16_t top = (uint16_t)(OB16(o, OBJ_y_pos) + 60);
        if (top > d1) return;                           /* sub.w d1,d0; bhi */
        uint16_t dd = (uint16_t)(top - d1);
        if (dd < (uint16_t)-0x10) return;
        if (OB8(p, OBJ_routine) >= 6) return;
        SET16(p, OBJ_y_pos, (uint16_t)(d2 + dd + 3));
        SET8(p, P_FLIP_TYPE, 0x80);                     /* face the other way */
        ride(o, p, bit);
        SET8(p, OBJ_anim, 0);                           /* AniIDSonAni_Walk, prev Run */
        SET8(p, 0x21, 1);
        *ang = 0;
        if (!OB16(p, OBJ_ground_vel)) SET16(p, OBJ_ground_vel, 1);
        return;
    }
    int release = 0;
    if (OB8(p, OBJ_status) & 2) {                       /* loc_218C6: jumped off */
        if ((uint8_t)(*ang + 0x20) < 0x40) SET16(p, OBJ_y_vel, (uint16_t)((int16_t)OB16(p, OBJ_y_vel) >> 1));
        else SET16(p, OBJ_y_vel, 0);
        release = 1;
    } else {
        int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0xC0);
        if (d0 < 0 || d0 >= 0x180) release = 1;
    }
    if (release) {                                      /* loc_218A8 */
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~8);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(1 << bit));
        SET8(p, P_FLIPS_LEFT, 0);
        SET8(p, P_FLIP_SPEED, 4);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | 2);
        return;
    }
    if (!(OB8(p, OBJ_status) & 8)) return;              /* loc_218E0 */
    int16_t sn, cs;
    o_sincos(*ang, &sn, &cs);
    int16_t y = (int16_t)(OB16(o, OBJ_y_pos) + (int16_t)(((int32_t)cs * 0x2800) >> 16));
    y = (int16_t)(y - ((int)OB8(p, OBJ_y_radius) - 0x13));
    SET16(p, OBJ_y_pos, (uint16_t)y);
    SET8(p, P_FLIP_ANGLE, *ang);
    *ang = (uint8_t)(*ang + 4);
    if (!OB16(p, OBJ_ground_vel)) SET16(p, OBJ_ground_vel, 1);
}

static void spiral(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, (OB8(o, OBJ_subtype) & 0x80) ? 4 : 2);
        SET8(o, OBJ_width_px, 0xD0);
    }
    if (OB8(o, OBJ_routine) == 4) {
        cylinder_player(o, RAM_Player_1, 3, &s2_mtz_cyl_angle[0]);
        cylinder_player(o, RAM_Player_2, 4, &s2_mtz_cyl_angle[1]);
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
        return;
    }
    spiral_player(o, RAM_Player_1, 3);
    spiral_player(o, RAM_Player_2, 4);
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

void s2_obj_ehz_register(TriObjFn *t)
{
    t[0x06] = spiral;
    t[0x11] = bridge;
    t[0x1C] = scenery;
    t[0x18] = platform;
    t[0x36] = spikes;
    t[0x4B] = buzzer;
    t[0x5C] = masher;
    t[0x9D] = coconuts;
    t[0x98] = projectile;
    t[0x49] = waterfall;
}

void s2_obj_boss_register(TriObjFn *t);
void s2_obj_cpz_register(TriObjFn *t);
void s2_obj_arz_register(TriObjFn *t);
void s2_obj_cnz_register(TriObjFn *t);
void s2_obj_htz_register(TriObjFn *t);
void s2_obj_mcz_register(TriObjFn *t);
void s2_obj_ooz_register(TriObjFn *t);
void s2_obj_mtz_register(TriObjFn *t);
void s2_obj_scz_register(TriObjFn *t);
void s2_obj_wfz_register(TriObjFn *t);
void s2_obj_dez_register(TriObjFn *t);

void s2_objects_register(TriObjFn *t)
{
    s2_obj_ehz_register(t);
    s2_obj_boss_register(t);
    s2_obj_cpz_register(t);
    s2_obj_arz_register(t);
    s2_obj_cnz_register(t);
    s2_obj_htz_register(t);
    s2_obj_mcz_register(t);
    s2_obj_ooz_register(t);
    s2_obj_mtz_register(t);
    s2_obj_scz_register(t);
    s2_obj_wfz_register(t);
    s2_obj_dez_register(t);
}
