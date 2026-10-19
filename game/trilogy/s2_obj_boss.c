/*
 * s2_obj_boss.c — Sonic 2 bosses and the egg capsule, transcribed from
 * s2disasm. S2's objoff_2A-2E land on S3K's status/subtype bytes, so the
 * variables live at $30-$3F here (S2 offset noted per field).
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define MUS_BOSS        0x19          /* S3K boss theme, as for S1's bosses */
#define FLASH_COLOUR    (RAM_Normal_palette + 0x20 + 11 * 2)   /* S2 line 1 idx 1, permuted */

int s2_boss_defeated_flag;            /* Boss_defeated_flag (s2dle) */

static uint32_t parent_of(uint32_t o) { return r32(o + 0x34) & 0xFFFFFF; }

static void move_and_fall(uint32_t o) { o_fall(o); }        /* ObjectMoveAndFall */

static void floor_snap(uint32_t o)
{
    int16_t d1 = o_floor_dist(o);
    if (d1 < 0) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
}

/* ------------------------------------------------------------------------
 * Object 56 - EHZ boss (drill car)
 * main: the flying pod's bottom; children: top (Eggman), propeller, ground
 * vehicle, three wheels and the drill spike
 * ---------------------------------------------------------------------- */
#define EB_TIMER   0x30               /* objoff_2A (word) */
#define EB_RT3     0x32               /* objoff_2C */
#define EB_FLAGS   0x33               /* objoff_2D: 0 on ground, 1 active, 2 flying off, 3 spike loose */
#define EB_PARENT  0x34               /* objoff_34 (long) */
#define EB_WHEELY  0x38               /* objoff_2E: summed y of the front wheels */
#define EB_DEFEAT  0x3C               /* objoff_3C (word) */
#define EB_INV     0x3E               /* objoff_3E */

static uint32_t eb_child(uint32_t o, int asset, int routine, int w, int prio)
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET32(a1, OBJ_code, S2(0x56));
    SET32(a1, EB_PARENT, RAMPTR(o));
    o_init(a1, asset, 4, prio, w, 0x20);
    SET8(a1, OBJ_routine, routine);
    return a1;
}

static void eb_turn(uint32_t o)                         /* loc_2F484 */
{
    int16_t x = (int16_t)OB16(o, OBJ_x_pos);
    if (x <= 0x28A0 || x >= 0x2B08) {
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
        SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
    }
}

static void eb_hits(uint32_t o)                         /* loc_2F4A6 */
{
    if (OB8(o, OBJ_2nd) >= 6) return;
    if (OB8(o, OBJ_status) & 0x80) {                    /* defeated */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, OBJ_2nd, 6);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, (uint16_t)-0x180);
        SET16(o, EB_DEFEAT, 0xB3);
        SET8(o, EB_FLAGS, OB8(o, EB_FLAGS) | 8);
        uint32_t top = parent_of(o);
        SET8(top, OBJ_anim, 4);
        SET8(top, OBJ_frame, 6);
        o_upload_asset(S2A_CAPSULE);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, EB_INV)) {
        SET8(o, EB_INV, 0x20);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, EB_INV, OB8(o, EB_INV) - 1);
    if (!OB8(o, EB_INV)) SET8(o, OBJ_coll_flags, 0x0F);
}

static void eb_main(uint32_t o)                         /* loc_2F262 */
{
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* approaching diagonally */
        SET8(o, OBJ_coll_flags, 0);
        if ((int16_t)OB16(o, OBJ_x_pos) > 0x29D0) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        } else {
            SET16(o, OBJ_x_pos, 0x29D0);
            SET8(o, OBJ_2nd, 2);
        }
        break;
    case 2:
        if (!OB8(o, EB_RT3)) {                          /* down onto the ground vehicle */
            if ((int16_t)OB16(o, OBJ_y_pos) < 0x41E) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
            else {
                SET8(o, EB_RT3, 2);
                SET8(o, EB_FLAGS, OB8(o, EB_FLAGS) | 1);
                SET16(o, EB_TIMER, 60);
            }
        } else {
            SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
            if ((int16_t)OB16(o, EB_TIMER) < 0) {
                SET16(o, OBJ_x_vel, (uint16_t)-0x200);
                SET8(o, OBJ_2nd, 4);
                SET8(o, OBJ_coll_flags, 0x0F);
                SET8(o, EB_FLAGS, OB8(o, EB_FLAGS) | 2);
            }
        }
        break;
    case 4: {                                           /* back and forth */
        if (getenv("TRILOGY_BOSSKILL")) SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);   /* development */
        eb_hits(o);
        eb_turn(o);
        SET16(o, OBJ_y_pos, (uint16_t)((OB16(o, EB_WHEELY) >> 1) - 0x14));
        SET16(o, EB_WHEELY, 0);
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + ((int32_t)(int16_t)OB16(o, OBJ_x_vel) << 8));
        break;
    }
    case 6:                                             /* defeated: falling / lying */
        SET16(o, EB_DEFEAT, OB16(o, EB_DEFEAT) - 1);
        if ((int16_t)OB16(o, EB_DEFEAT) < 0) {
            SET16(o, OBJ_x_vel, 0);
            SET8(o, OBJ_2nd, 8);
            SET16(o, EB_DEFEAT, (uint16_t)-0x26);
            SET16(o, EB_TIMER, 0x0C);
            break;
        }
        s1_boss_defeated(o);
        move_and_fall(o);
        {
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) { SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1); SET16(o, OBJ_y_vel, 0); }
        }
        break;
    case 8:
        SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
        if ((int16_t)OB16(o, EB_TIMER) < 0) { SET8(o, OBJ_2nd, 0x0A); SET8(o, EB_RT3, 0); }
        break;
    default:                                            /* flying off */
        switch (OB8(o, EB_RT3)) {
        case 0: {
            SET8(o, EB_FLAGS, OB8(o, EB_FLAGS) & ~1);
            uint32_t a1 = eb_child(o, S2A_CHOPPERS, 0x0C, 0x20, 3);   /* propeller again */
            if (a1) {
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x0C);
                SET8(a1, OBJ_status, OB8(o, OBJ_status));
                SET8(a1, OBJ_render, OB8(o, OBJ_render));
                SET8(a1, OBJ_anim, 2);
                SET16(a1, EB_TIMER, 0x10);
                SET16(o, EB_TIMER, 0x32);
                SET8(o, EB_RT3, 2);
                tri_music_zone();
                s2_boss_defeated_flag = 1;
            }
            break;
        }
        case 2:
            SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
            if ((int16_t)OB16(o, EB_TIMER) < 0) {
                SET8(o, EB_FLAGS, OB8(o, EB_FLAGS) | 4);
                SET16(o, EB_TIMER, 0x60);
                SET8(o, EB_RT3, 4);
            }
            break;
        default:
            SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
            if ((int16_t)OB16(o, EB_TIMER) < 0) {
                SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
                SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
                SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 6);
            } else SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 1);
            if (r16(RAM_Camera_max_X_pos) < 0x2AB0) {
                w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
            } else if (!(OB8(o, OBJ_render) & 0x80)) {
                o_delete(parent_of(o));                 /* the top */
                o_delete(o);
                return;
            }
            break;
        }
        break;
    }
    o_touch(o);
    o_draw(o);
}

static void eb_propeller(uint32_t o)                    /* loc_2F54E */
{
    uint32_t a1 = parent_of(o);
    if (!OB8(o, OBJ_2nd)) {
        if (r32(a1) != S2(0x56)) { o_delete(o); return; }
        if (OB8(a1, EB_FLAGS) & 1) {                    /* landed */
            SET8(o, OBJ_anim, 1);
            SET16(o, EB_TIMER, 0x18);
            SET8(o, OBJ_2nd, 2);
            D(0) = 0xE4;                                /* MusID_StopSFX -> cmd_StopSFX */
            tri_jsr(S3K_Play_Music);
        } else if (!(o_frame_counter() & 0x1F)) {
            o_sfx_s2(0xDE);                             /* SndID_Helicopter */
        }
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        SET8(o, OBJ_status, OB8(a1, OBJ_status));
        SET8(o, OBJ_render, OB8(a1, OBJ_render));
    } else {
        SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
        if ((int16_t)OB16(o, EB_TIMER) < 0) {
            if ((int16_t)OB16(o, EB_TIMER) <= -0x10) { o_delete(o); return; }
            SET16(o, OBJ_priority, 4 << 7);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
            o_draw(o);
            return;
        }
    }
    o_animate_s2(o, o_asset_anim(S2A_CHOPPERS));
    o_draw(o);
}

static void eb_wheel(uint32_t o)                        /* loc_2F664 */
{
    static const int16_t stop[3] = { 0x29EC, 0x29C4, 0x29A4 };
    uint32_t a1 = parent_of(o);
    switch (OB8(o, OBJ_2nd)) {
    case 0:
        if (r16(RAM_Camera_min_X_pos) < 0x28F0) { o_draw(o); return; }
        SET16(o, OBJ_y_vel, 0x100);
        if ((int16_t)OB16(o, OBJ_x_pos) > stop[OB8(o, OBJ_subtype) % 3]) SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
        else { SET16(o, OBJ_x_pos, (uint16_t)stop[OB8(o, OBJ_subtype) % 3]); SET8(o, OBJ_2nd, 2); }
        move_and_fall(o);
        floor_snap(o);
        if (OB8(o, OBJ_2nd)) SET16(o, OBJ_x_vel, (uint16_t)-0x200);
        break;
    case 2:
        if (r32(a1) != S2(0x56)) { o_delete(o); return; }
        if (!(OB8(a1, EB_FLAGS) & 2)) { o_draw(o); return; }
        SET8(o, OBJ_2nd, 4);
        if (OB16(o, OBJ_priority) == (2 << 7)) SET16(a1, EB_WHEELY, OB16(a1, EB_WHEELY) + OB16(o, OBJ_y_pos));
        o_draw(o);
        return;
    case 4:
        if (r32(a1) != S2(0x56)) { o_delete(o); return; }
        SET8(o, OBJ_status, OB8(a1, OBJ_status));
        SET8(o, OBJ_render, OB8(a1, OBJ_render));
        if (OB8(o, OBJ_status) & 0x80) SET8(o, OBJ_2nd, 6);
        eb_turn(o);
        move_and_fall(o);
        floor_snap(o);
        SET16(o, OBJ_y_vel, 0x100);
        if (OB16(o, OBJ_priority) == (2 << 7)) SET16(a1, EB_WHEELY, OB16(a1, EB_WHEELY) + OB16(o, OBJ_y_pos));
        break;
    case 6:
        SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
        if ((int16_t)OB16(o, EB_TIMER) >= 0) { o_draw(o); return; }
        SET8(o, OBJ_2nd, 8);
        SET16(o, EB_TIMER, 0x0A);
        SET16(o, OBJ_y_vel, (uint16_t)-0x300);
        if (OB16(o, OBJ_priority) != (2 << 7)) SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        o_draw(o);
        return;
    default:                                            /* bouncing away */
        SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
        if ((int16_t)OB16(o, EB_TIMER) >= 0) { o_draw(o); return; }
        move_and_fall(o);
        {
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) { SET16(o, OBJ_y_vel, (uint16_t)-0x200); SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1); }
        }
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
        o_draw(o);                                      /* (MarkObjGone: no animation) */
        return;
    }
    o_animate_s2(o, o_asset_anim(S2A_EHZBOSS1));
    o_draw(o);
}

static void eb_spike(uint32_t o)                        /* loc_2F7F4 */
{
    if (!OB8(o, OBJ_2nd)) {
        if (r16(RAM_Camera_min_X_pos) >= 0x28F0) {
            if ((int16_t)OB16(o, OBJ_x_pos) > 0x299A) SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
            else { SET16(o, OBJ_x_pos, 0x299A); SET8(o, OBJ_2nd, 2); }
        }
        o_draw(o);
        return;
    }
    uint32_t a1 = parent_of(o);
    if (r32(a1) != S2(0x56)) { o_delete(o); return; }
    if (OB8(a1, EB_FLAGS) & 8) {                        /* loose: flies on */
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_status) & 1) ? 3 : -3));
    } else {
        if (OB8(a1, OBJ_coll_prop) == 1) {              /* loc_2F8AA: last hit - shoot when facing Sonic */
            int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - r16(RAM_Player_1 + OBJ_x_pos));
            if ((d0 < 0) == ((OB8(a1, OBJ_status) & 1) != 0)) SET8(a1, EB_FLAGS, OB8(a1, EB_FLAGS) | 8);
        }
        if (!(OB8(a1, EB_FLAGS) & 2)) { o_draw(o); return; }
        SET8(o, OBJ_coll_flags, 0x8B);
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x10);
        SET8(o, OBJ_status, OB8(a1, OBJ_status));
        SET8(o, OBJ_render, OB8(a1, OBJ_render));
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_status) & 1) ? 0x36 : -0x36));
    }
    o_animate_s2(o, o_asset_anim(S2A_EHZBOSS1));
    o_touch(o);
    o_draw(o);
}

static void eb_ground(uint32_t o)                       /* loc_2F5F6 */
{
    if (!OB8(o, OBJ_2nd)) {
        if (r16(RAM_Camera_min_X_pos) >= 0x28F0) {
            if ((int16_t)OB16(o, OBJ_x_pos) > 0x29D0) SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
            else { SET16(o, OBJ_x_pos, 0x29D0); SET8(o, OBJ_2nd, 2); }
        }
        o_draw(o);
        return;
    }
    uint32_t a1 = parent_of(o);
    if ((OB8(a1, EB_FLAGS) & 2) && !(OB8(a1, EB_FLAGS) & 4)) {
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 8);
        SET8(o, OBJ_status, OB8(a1, OBJ_status));
        if (!(OB8(o, OBJ_status) & 0x80)) SET8(o, OBJ_render, OB8(a1, OBJ_render));
    }
    o_draw(o);
}

static void eb_top(uint32_t o)                          /* loc_2F8DA */
{
    uint32_t a1 = parent_of(o);
    SET32(o, OBJ_x_pos, r32(a1 + OBJ_x_pos));
    SET32(o, OBJ_y_pos, r32(a1 + OBJ_y_pos));
    SET8(o, OBJ_status, OB8(a1, OBJ_status));
    SET8(o, OBJ_render, OB8(a1, OBJ_render));
    if (OB8(a1, EB_INV) == 0x1F) SET8(o, OBJ_anim, 2);         /* just hit */
    if ((r8(RAM_Player_1 + OBJ_routine) == 4 || r8(RAM_Player_2 + OBJ_routine) == 4) && OB8(o, OBJ_anim) != 2)
        SET8(o, OBJ_anim, 3);                                   /* laughs */
    o_animate_s2(o, o_asset_anim(S2A_EGGPOD));
    o_draw(o);
}

static void ehz_boss(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj56_Init */
        o_upload_asset(S2A_EGGPOD);
        o_upload_asset(S2A_EGGPOD1);
        o_upload_asset(S2A_EHZBOSS);
        o_upload_asset(S2A_EHZBOSS1);
        o_upload_asset(S2A_CHOPPERS);
        o_init(o, S2A_EGGPOD1, 4, 4, 0x20, 0x20);
        SET8(o, OBJ_subtype, 0x81);
        SET16(o, OBJ_x_pos, 0x29D0);
        SET16(o, OBJ_y_pos, 0x426);
        SET8(o, OBJ_y_radius, 0x14);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        SET8(o, OBJ_routine, 2);
        uint32_t a1 = eb_child(o, S2A_EGGPOD, 0x0E, 0x20, 4);    /* top */
        if (a1) {
            SET32(o, EB_PARENT, RAMPTR(a1));            /* top and bottom point at each other */
            SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
            SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
            SET8(a1, OBJ_anim, 1);
        }
        a1 = eb_child(o, S2A_EHZBOSS, 6, 0x30, 3);              /* vehicle on the ground */
        if (a1) {
            SET8(a1, OBJ_y_radius, 0x10);
            SET16(a1, OBJ_x_pos, 0x2AF0);
            SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
        }
        static const int16_t wx[3] = { 0x1C, -0x0C, -0x2C };
        for (int i = 0; i < 3; i++) {                           /* wheels */
            a1 = eb_child(o, S2A_EHZBOSS1, 8, 0x10, i < 2 ? 2 : 3);
            if (!a1) continue;
            SET8(a1, OBJ_y_radius, 0x10);
            SET8(a1, 0x1F, 0x10);
            SET16(a1, OBJ_x_pos, (uint16_t)(0x2AF0 + wx[i]));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x0C);
            SET8(a1, OBJ_frame, i < 2 ? 4 : 6);
            SET8(a1, OBJ_anim, i < 2 ? 1 : 2);
            SET16(a1, EB_TIMER, 0x0A);
            SET8(a1, OBJ_subtype, i);
        }
        a1 = eb_child(o, S2A_EHZBOSS1, 0x0A, 0x20, 2);          /* drill spike */
        if (a1) {
            SET16(a1, OBJ_x_pos, (uint16_t)(0x2AF0 - 0x36));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
            SET8(a1, OBJ_frame, 1);
            SET8(a1, OBJ_anim, 0);
        }
        SET16(o, OBJ_x_pos, 0x2AF0);
        SET16(o, OBJ_y_pos, 0x2F8);
        a1 = eb_child(o, S2A_CHOPPERS, 4, 0x40, 3);             /* propeller */
        if (a1) {
            SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
            SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
            SET16(a1, EB_TIMER, 0x1E);
        }
        return;
    }
    case 2:    eb_main(o); return;
    case 4:    eb_propeller(o); return;
    case 6:    eb_ground(o); return;
    case 8:    eb_wheel(o); return;
    case 0x0A: eb_spike(o); return;
    case 0x0C:                                          /* propeller after defeat */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 1);
        SET16(o, EB_TIMER, OB16(o, EB_TIMER) - 1);
        if ((int16_t)OB16(o, EB_TIMER) < 0) {
            SET8(o, OBJ_routine, 4);
            o_animate_s2(o, o_asset_anim(S2A_CHOPPERS));
        }
        o_draw(o);
        return;
    default:   eb_top(o); return;
    }
}

/* ------------------------------------------------------------------------
 * Object 3E - egg capsule (S2)
 * body: routine 2, button 4, lock 6, animal launcher 8 -> $A ends the act
 * ---------------------------------------------------------------------- */
#define CP_Y0      0x30               /* objoff_30 */
#define CP_PRESSED 0x32               /* objoff_32 (button) */
#define CP_TIMER   0x34               /* objoff_34 */
#define CP_KIDS    0x38               /* objoff_38: button, lock, launcher (words) */
#define CP_DUR     0x3E               /* anim_frame_duration (word; clear of S3K's $25) */

static void capsule(uint32_t o)
{
    static const uint8_t ld[4][5] = {     /* y offset, routine, width, priority, frame */
        { 0, 2, 0x20, 4, 0 }, { 0x28, 4, 0x10, 5, 4 }, { 0x18, 6, 8, 3, 5 }, { 0, 8, 0x20, 4, 0 } };
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_upload_asset(S2A_CAPSULE);
        uint32_t a1 = o;
        for (int i = 0; i < 4; i++) {
            if (i) {
                a1 = o_alloc();
                if (!a1) break;
                SET16(o, CP_KIDS + (i - 1) * 2, a1 & 0xFFFF);
                SET32(a1, OBJ_code, S2(0x3E));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + ld[0][0]);
            }
            o_init(a1, S2A_CAPSULE, 0x84, ld[i][3], ld[i][2], 0x20);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - ld[i][0]);
            SET16(a1, CP_Y0, OB16(a1, OBJ_y_pos));
            SET8(a1, OBJ_routine, ld[i][1]);
            SET8(a1, OBJ_frame, ld[i][4]);
        }
        return;
    }
    case 2: {                                           /* body */
        uint32_t btn = 0xFF0000u | OB16(o, CP_KIDS), lock = 0xFF0000u | OB16(o, CP_KIDS + 2);
        switch (OB8(o, OBJ_2nd)) {
        case 0:
            if (OB16(btn, CP_PRESSED)) {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, S3K_Obj_Explosion);
                    SET8(a1, OBJ_routine, 6);
                    SET16(a1, OBJ_x_pos, OB16(lock, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(lock, OBJ_y_pos));
                    o_sfx_s2(0xC1); /* SndID_Explosion */
                }
                SET16(lock, OBJ_y_vel, (uint16_t)-0x400);
                SET16(lock, OBJ_x_vel, 0x800);
                SET8(lock, OBJ_2nd, OB8(lock, OBJ_2nd) + 2);
                SET16(o, CP_TIMER, 0x1D);
                SET8(o, OBJ_2nd, 2);
            }
            break;
        case 2:
            SET16(o, CP_TIMER, OB16(o, CP_TIMER) - 1);
            if ((int16_t)OB16(o, CP_TIMER) >= 0) break;
            SET8(o, OBJ_anim, 1);                       /* open */
            for (int i = 0, dx = -0x1C, d5 = 0x9A; i < 8; i++, dx += 7, d5 -= 8)
                s1_spawn_animal(o, dx, d5);
            {
                uint32_t spawner = 0xFF0000u | OB16(o, CP_KIDS + 4);
                SET16(spawner, CP_DUR, 0xB4);
                SET8(spawner, OBJ_2nd, OB8(spawner, OBJ_2nd) + 2);
            }
            SET8(o, OBJ_2nd, 4);
            break;
        default:
            break;
        }
        o_solid(o, 0x2B, 0x18, 0x18, OB16(o, OBJ_x_pos));
        o_animate_s2(o, o_asset_anim(S2A_CAPSULE));
        o_remember(o);
        return;
    }
    case 4:                                             /* button */
        o_solid(o, 0x1B, 8, 8, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(o, CP_Y0));
        if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
            w8(RAM_Update_HUD_timer, 0);
            SET16(o, CP_PRESSED, 1);
        }
        o_remember(o);
        return;
    case 6:                                             /* lock: flies off */
        if (OB8(o, OBJ_2nd)) {
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            move_and_fall(o);
        }
        o_remember(o);
        return;
    case 8:                                             /* animal launcher */
        if (OB8(o, OBJ_2nd)) {
            if (!(r16(RAM_Level_frame_counter) & 7)) {
                int r = o_random();
                int d0 = (r & 0x1F) - 6;
                if ((int32_t)D(1) < 0) d0 = -d0;
                s1_spawn_animal(o, d0, 0x0C);
            }
            SET16(o, CP_DUR, OB16(o, CP_DUR) - 1);
            if (!OB16(o, CP_DUR)) {
                SET8(o, OBJ_routine, 0x0A);
                SET16(o, CP_DUR, 0xB4);
            }
        }
        o_remember(o);
        return;
    case 0x0A:                                          /* all animals gone -> Load_EndOfAct */
        if (s1_animals_alive() || (r8(RAM_Player_1 + OBJ_status) & 2)) { o_remember(o); return; }
        SET8(o, OBJ_routine, 0x0C);
        A(1) = RAMPTR(RAM_Player_1);
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Set_PlayerEndingPose);
        {
            uint32_t a1 = o_alloc();
            if (a1) SET32(a1, OBJ_code, S3K_Obj_LevelResults);
        }
        return;
    default:                                            /* Obj_EndSignAfter */
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Check_TailsEndPose);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 5D - CPZ boss (Eggman pumping Mega Mack)
 * main: the eggmobile (routine 2); children by routine: 4 pipe control,
 * 6 pump, 8 pipe retract, $A dripper, $C gunk, $E pipe segment,
 * $10 container (+ floor, extend), $12 pump top, $14 falling part,
 * $16 Eggman, $18 jet flame. Cross-object flags are the main object's
 * CB_STATUS / CB_STATUS2 bits, exactly as in S2.
 * ---------------------------------------------------------------------- */
#define CB_TIMER2   0x40              /* Obj5D_timer2 (objoff_2A, word) */
#define CB_SEGS     0x42              /* Obj5D_pipe_segments (objoff_2C, word) */
#define CB_STATUS   0x44              /* Obj5D_status (objoff_2D) */
#define CB_STATUS2  0x46              /* Obj5D_status2 (objoff_2E) */
#define CB_XVEL     0x46              /* Obj5D_x_vel (objoff_2E, word: container offset) */
#define CB_XNEXT    0x30              /* Obj5D_x_pos_next (long) */
#define CB_TIMER    0x30              /* Obj5D_timer (byte, children) */
#define CB_YOFF     0x31              /* Obj5D_y_offset */
#define CB_TIMER3   0x32
#define CB_PARENT   0x34              /* Obj5D_parent (long) */
#define CB_YNEXT    0x38              /* Obj5D_y_pos_next (long; children: word offset) */
#define CB_DEFEAT   0x3C              /* Obj5D_defeat_timer / flag / timer4 */
#define CB_INV      0x3E
#define CB_HOVER    0x3F
#define CB_DEAD     0x80              /* status.npc.no_balancing: destroyed */

static uint32_t cb_parent(uint32_t o) { return r32(o + CB_PARENT) & 0xFFFFFF; }

static uint32_t cb_new(uint32_t after, int asset, int routine, int prio, int h)
{
    uint32_t a1 = after ? o_alloc_after(after) : o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, S2(0x5D));
    o_init(a1, asset, 4, prio, 0x20, h);
    SET8(a1, OBJ_routine, routine);
    return a1;
}

static void cb_copy_pos(uint32_t o, uint32_t a1)
{
    SET32(o, OBJ_x_pos, r32(a1 + OBJ_x_pos));
    SET32(o, OBJ_y_pos, r32(a1 + OBJ_y_pos));
}

static void cb_follow(uint32_t o, uint32_t a1)          /* position, status, render of a1 */
{
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
    SET8(o, OBJ_status, OB8(a1, OBJ_status));
    SET8(o, OBJ_render, OB8(a1, OBJ_render));
}

static void cb_animate(uint32_t o, int asset) { o_animate_s2(o, o_asset_anim(asset)); }

static int16_t cb_rnd_xvel(int32_t *hi)                 /* RandomNumber; asr.w #8; asr.w #6 */
{
    uint32_t r = (uint32_t)o_random();
    if (hi) *hi = (int32_t)(r >> 16);
    return (int16_t)((int16_t)r >> 14);
}

static void cb_explosion(uint32_t o, int spread)        /* Obj58 at o (randomly offset) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);
    o_sfx_s2(0xC4); /* SndID_BossExplosion */
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    if (spread) {
        uint32_t r = (uint32_t)o_random();
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (int16_t)(((r & 0xFF) >> 2) - 0x20));
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + (int16_t)((((r >> 8) & 0xFF) >> 2) - 0x20));
    }
}

/* turn o into a falling part (Obj5D_FallingParts) with a random drift */
static void cb_fall_part(uint32_t o, int timer_from_hi)
{
    int32_t hi;
    SET8(o, OBJ_routine, 0x14);
    SET16(o, OBJ_x_vel, (uint16_t)cb_rnd_xvel(&hi));
    SET16(o, OBJ_y_vel, (uint16_t)-0x380);
    if (timer_from_hi) SET8(o, CB_TIMER, (uint8_t)(((uint8_t)hi + 0x1E) & 0x7F));
}

static void cb_main_move(uint32_t o)                    /* Obj5D_Main_Move */
{
    SET32(o, CB_XNEXT, r32(o + CB_XNEXT) + ((int32_t)(int16_t)OB16(o, OBJ_x_vel) << 8));
    SET32(o, CB_YNEXT, r32(o + CB_YNEXT) + ((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8));
}

static void cb_pos_and_collision(uint32_t o)            /* Obj5D_Main_Pos_and_Collision */
{
    int16_t s, c;
    o_sincos(OB8(o, CB_HOVER), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + OB16(o, CB_YNEXT)));
    SET16(o, OBJ_x_pos, OB16(o, CB_XNEXT));
    SET8(o, CB_HOVER, OB8(o, CB_HOVER) + 2);
    if (OB8(o, OBJ_2nd) >= 8) return;
    if (OB8(o, OBJ_status) & CB_DEAD) {                 /* Obj5D_Defeated */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, OBJ_2nd, 8);
        SET16(o, CB_DEFEAT, 60 * 3 - 1);
        SET8(cb_parent(o), OBJ_anim, 4);
        o_upload_asset(S2A_CAPSULE);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, CB_INV)) {
        SET8(o, CB_INV, 32);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, CB_INV, OB8(o, CB_INV) - 1);
    if (!OB8(o, CB_INV)) {
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, CB_STATUS, OB8(o, CB_STATUS) & ~2);
    }
}

static int cb_main(uint32_t o)                          /* 0 = deleted */
{
    if (OB8(o, OBJ_2nd) < 8) {                          /* Obj5D_LookAtChar */
        if ((int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos)) > 0)
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        else
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
    }
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* Descend */
        SET16(o, OBJ_y_vel, 0x100);
        cb_main_move(o);
        if (OB16(o, CB_YNEXT) == 0x4C0) {
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_2nd, 2);
        }
        cb_pos_and_collision(o);
        break;
    case 2: {                                           /* MoveTowardTarget */
        int16_t d1 = (OB8(o, CB_STATUS) & 8) ? 0x2A50 : 0x2B30;
        int16_t xn = (int16_t)OB16(o, CB_XNEXT);
        int16_t d0 = (int16_t)(d1 - xn);
        if (d0 < 0) d0 = (int16_t)-d0;
        if (d0 <= 3) {                                  /* _Stop */
            if (OB16(o, CB_YNEXT) == 0x4C0) {
                SET16(o, OBJ_x_vel, 0);
                SET16(o, OBJ_y_vel, 0);
                SET8(o, OBJ_2nd, 4);
                SET8(o, CB_STATUS, OB8(o, CB_STATUS) ^ 8);
                SET8(o, CB_STATUS2, OB8(o, CB_STATUS2) | 1);
            }
        } else {
            SET16(o, OBJ_x_vel, d1 > xn ? 0x300 : (uint16_t)-0x300);
            cb_main_move(o);
        }
        cb_pos_and_collision(o);
        break;
    }
    case 4:                                             /* Wait */
        if (!(OB8(o, CB_STATUS2) & 1)) SET8(o, OBJ_2nd, 6);
        cb_pos_and_collision(o);
        break;
    case 6: {                                           /* FollowPlayer */
        int16_t d0 = (int16_t)(OB16(RAM_Player_1, OBJ_x_pos) + 76);
        int16_t xn = (int16_t)OB16(o, CB_XNEXT);
        if (d0 > xn) {
            SET32(o, CB_XNEXT, r32(o + CB_XNEXT) + 0x10000);
            if ((int16_t)OB16(o, CB_XNEXT) >= 0x2B70) SET16(o, CB_XNEXT, 0x2B70);
        } else if (d0 < xn) {
            SET32(o, CB_XNEXT, r32(o + CB_XNEXT) - 0x10000);
            if ((int16_t)OB16(o, CB_XNEXT) <= 0x2A28) SET16(o, CB_XNEXT, 0x2A28);
        }
        cb_pos_and_collision(o);
        break;
    }
    case 8:                                             /* Explode */
        SET16(o, CB_DEFEAT, OB16(o, CB_DEFEAT) - 1);
        if ((int16_t)OB16(o, CB_DEFEAT) >= 0) {
            if (!(r8(RAM_Level_frame_counter + 1) & 7)) cb_explosion(o, 1);
            break;
        }
        SET8(o, OBJ_status, (OB8(o, OBJ_status) | 1) & ~CB_DEAD);
        SET16(o, OBJ_x_vel, 0);
        SET8(o, OBJ_2nd, 0x0A);
        SET16(o, CB_DEFEAT, (uint16_t)-0x26);
        break;
    case 0x0A: {                                        /* StopExploding */
        SET16(o, CB_DEFEAT, OB16(o, CB_DEFEAT) + 1);
        int16_t t = (int16_t)OB16(o, CB_DEFEAT);
        if (t == 0) SET16(o, OBJ_y_vel, 0);
        else if (t < 0) SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        else if (t < 0x30) SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
        else if (t == 0x30) { SET16(o, OBJ_y_vel, 0); tri_music_zone(); }
        else if (t >= 0x38) SET8(o, OBJ_2nd, 0x0C);
        cb_main_move(o);
        cb_pos_and_collision(o);
        break;
    }
    default:                                            /* Retreat */
        SET8(o, CB_STATUS2, OB8(o, CB_STATUS2) | 0x40);
        SET16(o, OBJ_x_vel, 0x400);
        SET16(o, OBJ_y_vel, (uint16_t)-0x40);
        if (r16(RAM_Camera_max_X_pos) < 0x2C30) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
        } else if (!(OB8(o, OBJ_render) & 0x80)) {
            o_delete(cb_parent(o));
            o_delete(o);
            return 0;
        }
        cb_main_move(o);
        cb_pos_and_collision(o);
        break;
    }
    return 1;
}

static void cb_pipe_segment(uint32_t o)                 /* Obj5D_PipeSegment */
{
    uint32_t a1 = cb_parent(o), a2 = cb_parent(a1);
    if (OB8(a2, OBJ_status) & CB_DEAD) {                /* _End: break loose (no animate: S2) */
        cb_fall_part(o, 1);
        o_draw(o);
        return;
    }
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
    if (OB8(o, OBJ_2nd) == 4) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x18);
    if (OB8(o, OBJ_status) & CB_DEAD) { o_delete(o); return; }
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + OB16(o, CB_YNEXT));
    cb_animate(o, S2A_CPZBOSS);
    o_draw(o);
}

static void cb_pipe(uint32_t o)                         /* routine 4: Obj5D_Pipe */
{
    uint32_t a1;
    if (!OB8(o, OBJ_2nd)) {                             /* _Wait for the vehicle's action 0 */
        a1 = cb_parent(o);
        if (!(OB8(a1, CB_STATUS2) & 1)) return;
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x18);
        SET16(o, CB_SEGS, 0x0C);
        SET8(o, OBJ_2nd, 2);
        a1 = o;
    } else {                                            /* _Extend: one segment per frame */
        a1 = o_alloc_after(o);
        if (!a1) return;
        SET32(a1, CB_PARENT, RAMPTR(o));
    }
    SET16(o, CB_SEGS, OB16(o, CB_SEGS) - 1);
    if ((int16_t)OB16(o, CB_SEGS) < 0) {                /* _Extend_End */
        SET8(o, OBJ_2nd, 0);
        SET8(o, OBJ_routine, 6);
        cb_pipe_segment(o);
        return;
    }
    SET32(a1, OBJ_code, S2(0x5D));
    o_init(a1, S2A_CPZBOSS, 4, 5, 0x20, 0x20);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    uint16_t d0 = (uint16_t)((0x0B - (int16_t)OB16(o, CB_SEGS)) * 8);
    SET16(a1, CB_YNEXT, d0);
    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + d0);
    SET8(a1, OBJ_anim, 1);
    if (OB8(a1, OBJ_2nd) != 2) SET8(a1, OBJ_routine, 0x0E);
    cb_pipe_segment(o);
}

static void cb_pipe_pump(uint32_t o)                    /* routine 6: Obj5D_Pipe_Pump */
{
    switch (OB8(o, OBJ_2nd)) {
    case 0: {
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            SET8(o, OBJ_routine, 0x0E);                 /* the control object is a segment for now */
            SET32(a1, OBJ_code, S2(0x5D));
            o_init(a1, S2A_CPZBOSS, 4, 4, 0x20, 0x20);
            SET8(a1, OBJ_routine, 6);
            SET8(a1, OBJ_2nd, 2);
            SET8(a1, CB_TIMER3, 2);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, CB_YOFF, 0x0B * 8);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x0B * 8);
            SET8(a1, OBJ_anim, 2);
            SET32(a1, CB_PARENT, RAMPTR(o));
            SET8(a1, CB_TIMER, 0x12);
            uint32_t a2 = o_alloc_after(o);             /* the dripper */
            if (a2) {
                SET32(a2, OBJ_code, S2(0x5D));
                SET8(a2, OBJ_routine, 0x0A);
                SET32(a2, CB_PARENT, r32(o + CB_PARENT));
            }
        }
        cb_pipe_segment(o);
        return;
    }
    case 2: {                                           /* the pumping blob climbs the pipe */
        uint32_t a1 = cb_parent(o), a2 = cb_parent(a1);
        if (OB8(a2, OBJ_status) & CB_DEAD) { o_delete(o); return; }
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        SET8(o, CB_TIMER, OB8(o, CB_TIMER) - 1);
        if (!OB8(o, CB_TIMER)) {
            SET8(o, CB_TIMER, 0x12);
            SET8(o, CB_YOFF, OB8(o, CB_YOFF) - 8);
            int8_t yo = (int8_t)OB8(o, CB_YOFF);
            if (yo < 0) {
                SET8(o, CB_TIMER, 6);
                SET8(o, OBJ_2nd, 4);
                return;
            }
            if (yo == 0) {
                SET8(o, OBJ_anim, 3);
                SET8(o, CB_TIMER, 0x0C);
            }
        }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + OB8(o, CB_YOFF));
        cb_animate(o, S2A_CPZBOSS);
        o_draw(o);
        return;
    }
    default: {
        SET8(o, CB_TIMER, OB8(o, CB_TIMER) - 1);
        if (OB8(o, CB_TIMER)) return;
        SET8(o, CB_TIMER3, OB8(o, CB_TIMER3) - 1);
        if (OB8(o, CB_TIMER3)) {                        /* (S2 resets, then retracts anyway) */
            SET8(o, OBJ_anim, 2);
            SET8(o, CB_TIMER, 0x12);
            SET8(o, OBJ_2nd, 2);
            SET8(o, CB_YOFF, 0x0B * 8);
        }
        uint32_t a1 = cb_parent(o);
        SET8(a1, OBJ_routine, 8);
        SET8(a1, CB_YOFF, 0x0B * 8);
        o_delete(o);
        return;
    }
    }
}

static void cb_pipe_retract(uint32_t o)                 /* routine 8: one segment a frame */
{
    uint32_t a1 = 0;
    if (OB8(o, CB_DEFEAT)) {
        a1 = o;
    } else {
        uint16_t d0 = (uint16_t)(OB8(o, CB_YOFF) + OB16(o, OBJ_y_pos));
        for (int i = 0; i < 110; i++) {                 /* first object at that height */
            uint32_t s = o_slot(i);
            if (OB16(s, OBJ_y_pos) != d0) continue;
            if (r32(s) == S2(0x5D)) a1 = s;             /* (S2 stalls a frame on any other object) */
            break;
        }
    }
    if (a1) {
        SET8(a1, OBJ_status, OB8(a1, OBJ_status) | CB_DEAD);
        SET8(o, CB_YOFF, OB8(o, CB_YOFF) - 8);
        if (!OB8(o, CB_YOFF)) SET8(o, CB_DEFEAT, 0xFF);
    }
    cb_pipe_segment(o);
}

static void cb_dripper(uint32_t o)                      /* routine $A */
{
    if (OB8(o, OBJ_status) & CB_DEAD) { o_delete(o); return; }
    uint32_t a1 = cb_parent(o);
    switch (OB8(o, OBJ_2nd)) {
    case 0:
        SET8(o, OBJ_2nd, 2);
        o_init(o, S2A_CPZBOSS3, 4, 4, 0x20, 0x20);
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        SET8(o, CB_TIMER, 0x0F);
        SET8(o, OBJ_anim, 4);
        /* fall through */
    case 2:
        SET8(o, CB_TIMER, OB8(o, CB_TIMER) - 1);
        if (!OB8(o, CB_TIMER)) {
            SET8(o, OBJ_anim, 5);
            SET8(o, CB_TIMER, 4);
            SET8(o, OBJ_2nd, 4);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x24);
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 2);
            return;
        }
        cb_follow(o, a1);
        cb_animate(o, S2A_CPZBOSS3);
        o_draw(o);
        return;
    default:
        SET8(o, CB_TIMER, OB8(o, CB_TIMER) - 1);
        if (!OB8(o, CB_TIMER)) {
            SET8(o, OBJ_2nd, 0);
            SET8(a1, CB_STATUS2, OB8(a1, CB_STATUS2) | 2);
            SET8(o, CB_DEFEAT, OB8(o, CB_DEFEAT) + 1);
            if ((int8_t)OB8(o, CB_DEFEAT) >= 0x0C) o_delete(o);
            return;
        }
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 2 + ((OB8(o, OBJ_render) & 1) ? 4 : 0));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 0x24);
        cb_animate(o, S2A_CPZBOSS3);
        o_draw(o);
        return;
    }
}

static void cb_gunk_offscreen(uint32_t o)               /* Obj5D_Gunk_OffScreen */
{
    uint32_t m = cb_parent(cb_parent(o));
    SET8(m, CB_STATUS2, OB8(m, CB_STATUS2) | 4 | 0x10);
    SET8(m, OBJ_2nd, 2);
    o_delete(o);
}

static void cb_gunk(uint32_t o)                         /* routine $C: the Mega Mack blob */
{
    uint32_t m = cb_parent(cb_parent(o));
    switch (OB8(o, OBJ_2nd)) {
    case 0:
        SET8(o, OBJ_2nd, 2);
        SET8(o, OBJ_y_radius, 0x20);
        SET8(o, OBJ_anim, 0x19);
        SET16(o, OBJ_y_vel, 0);
        if (OB8(m, CB_STATUS) & 4) {
            SET8(m, CB_STATUS, OB8(m, CB_STATUS) & ~4);
            SET8(o, OBJ_2nd, 6);
            SET16(o, CB_TIMER2, 9);
        }
        /* fall through: Obj5D_Gunk_Main */
    case 2: {
        o_fall(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {                                   /* splat */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(m, CB_STATUS2, OB8(m, CB_STATUS2) | 4 | 0x10);
            SET8(m, OBJ_2nd, 2);
            SET8(o, OBJ_2nd, OB8(o, OBJ_2nd) + 2);
            SET8(o, OBJ_subtype, 0);
            o_sfx_s2(0xE6);                                /* SndID_MegaMackDrop */
            o_touch(o);
            o_draw(o);
            return;
        }
        if ((int16_t)OB16(o, OBJ_y_pos) >= 0x518) { cb_gunk_offscreen(o); return; }
        cb_animate(o, S2A_CPZBOSS3);
        o_touch(o);
        o_draw(o);
        return;
    }
    case 4:                                             /* splashed into droplets */
        if (!OB8(o, OBJ_subtype)) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x18);
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_render) & 1) ? 0x0C - 0x18 : 0x0C));
            SET8(o, OBJ_y_radius, 4);
            SET8(o, OBJ_x_radius, 4);
            SET8(o, OBJ_subtype, 1);
            SET8(o, OBJ_frame, 9);
            SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)(OB16(o, OBJ_y_vel) >> 1));
            int16_t d0 = (int16_t)((int16_t)o_random() >> 6);
            if (d0 >= 0) d0 = (int16_t)(d0 + 0x200);
            SET16(o, OBJ_x_vel, (uint16_t)(d0 - 0x100));
            SET8(o, OBJ_coll_flags, 0);
            for (int k = 0; k < 4; k++) {
                uint32_t a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, S2(0x5D));
                SET32(a1, CB_PARENT, RAMPTR(o));
                o_init(a1, S2A_CPZBOSS3, 4, 2, 0x20, 8);
                cb_copy_pos(a1, o);
                SET8(a1, OBJ_y_radius, 4);
                SET8(a1, OBJ_x_radius, 4);
                SET8(a1, OBJ_frame, 9);
                SET8(a1, OBJ_routine, 0x0C);
                SET8(a1, OBJ_2nd, 4);
                SET8(a1, OBJ_subtype, 1);
                SET16(a1, OBJ_y_vel, OB16(o, OBJ_y_vel));
                SET8(a1, OBJ_coll_flags, OB8(o, OBJ_coll_flags));
                uint32_t r = (uint32_t)o_random();
                int16_t xv = (int16_t)((int16_t)r >> 6);
                if (xv >= 0) xv = (int16_t)(xv + 0x80);
                SET16(a1, OBJ_x_vel, (uint16_t)(xv - 0x80));
                SET16(a1, OBJ_y_vel, OB16(a1, OBJ_y_vel) - ((r >> 16) & 0x3FF));
            }
            o_draw(o);
            return;
        }
        o_fall(o);                                      /* _Droplets_Move */
        if (o_floor_dist(o) < 0) { o_delete(o); return; }
        o_remember(o);
        return;
    case 6:
        SET16(o, CB_TIMER2, OB16(o, CB_TIMER2) - 1);
        if ((int16_t)OB16(o, CB_TIMER2) >= 0) {
            o_move(o);
            cb_animate(o, S2A_CPZBOSS3);
            o_touch(o);
            o_draw(o);
            return;
        }
        SET16(o, OBJ_priority, 2 << 7);
        SET8(o, OBJ_frame, 0x25);
        SET16(o, OBJ_x_pos, OB16(m, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(m, OBJ_y_pos));
        SET8(o, OBJ_2nd, 8);
        SET8(o, OBJ_anim_timer, 8);
        /* fall through */
    default:
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            SET8(o, OBJ_anim_timer, 8);
            if (OB8(o, OBJ_frame) > 0x27) { cb_gunk_offscreen(o); return; }
            if (OB8(o, OBJ_frame) == 0x27) SET8(o, OBJ_anim_timer, 8 + 0x0C);
        }
        SET16(o, OBJ_x_pos, OB16(m, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(m, OBJ_y_pos));
        o_touch(o);
        o_draw(o);
        return;
    }
}

static void cb_floor_end(uint32_t o)                    /* Obj5D_Container_Floor_End */
{
    cb_follow(o, cb_parent(o));
    cb_animate(o, OB8(o, OBJ_art_tile) >> 5 == 3 ? S2A_CPZBOSS3 : S2A_CPZBOSS);
    o_draw(o);
}

static void cb_container_drop(uint32_t o, uint32_t a1)  /* loc_2E464: open the floor */
{
    SET8(a1, CB_STATUS2, (OB8(a1, CB_STATUS2) | 0x20) & ~4);
    SET16(o, CB_TIMER2, 0x12);
    uint32_t c = cb_new(o, S2A_CPZBOSS, 0x10, 5, 0x40);
    if (!c) return;
    SET32(c, CB_PARENT, RAMPTR(o));
    SET8(c, OBJ_2nd, 8);
    cb_copy_pos(c, o);
    SET8(c, OBJ_anim, 0x0B);
    SET16(c, CB_TIMER2, 0x24);
}

static void cb_container_aim(uint32_t o, uint32_t a1)   /* loc_2E3F2 */
{
    if (OB8(a1, CB_STATUS2) & (8 | 0x10)) return;
    int16_t xv = (int16_t)OB16(o, CB_XVEL);
    if (xv >= -0x14) {
        if (!(OB8(a1, CB_STATUS) & 2)) return;
        SET8(a1, CB_STATUS, (OB8(a1, CB_STATUS) & ~2) | 4);
        cb_container_drop(o, a1);
        return;
    }
    if (xv >= -0x40) return;
    int16_t d1 = (int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - 8);
    if (OB8(o, OBJ_render) & 1) {
        d1 = (int16_t)(d1 + xv - OB16(o, OBJ_x_pos));
        if (d1 > 0 || d1 < -0x18) return;
    } else {
        d1 = (int16_t)(d1 - xv - OB16(o, OBJ_x_pos));
        if (d1 < 0 || d1 > 0x18) return;
    }
    cb_container_drop(o, a1);
}

static uint32_t cb_container_slide(uint32_t o, uint32_t a1)  /* loc_2E4CE; returns S2's a1 */
{
    int d0 = (OB8(a1, CB_STATUS2) & 0x10) ? 1 : -1;
    if ((int16_t)OB16(o, CB_XVEL) == -0x10 && (OB8(a1, CB_STATUS2) & 0x10)) {
        SET8(a1, CB_STATUS2, OB8(a1, CB_STATUS2) & ~(0x10 | 4));
        SET8(o, OBJ_2nd, 0);
        uint32_t p = cb_new(o, S2A_CPZBOSS, 4, 4, 0x20);        /* a new pipe */
        if (!p) return 0;
        SET32(p, CB_PARENT, r32(o + CB_PARENT));
        cb_copy_pos(p, o);
        return p;
    }
    int16_t d1 = (int16_t)OB16(o, CB_XVEL);
    if (d1 >= -0x28) SET8(o, OBJ_anim, 6);
    else if (d1 >= -0x40) SET8(o, OBJ_anim, 7);
    else {
        SET8(o, OBJ_anim, 8);
        if (d1 < -0x58) {
            SET16(o, CB_XVEL, (OB8(o, OBJ_render) & 1) ? 0x58 : (uint16_t)-0x58);
        } else if (d1 == -0x58 && !(OB8(a1, CB_STATUS2) & 0x10)) {
            return a1;
        }
    }
    SET16(o, CB_XVEL, OB16(o, CB_XVEL) + d0);
    return a1;
}

static void cb_container(uint32_t o)                    /* routine $10 */
{
    uint32_t a1 = cb_parent(o);
    switch (OB8(o, OBJ_2nd)) {
    case 0: {                                           /* Init: floor + extending tank */
        if (!(OB8(a1, CB_STATUS2) & 0x80)) {
            SET8(a1, CB_STATUS2, OB8(a1, CB_STATUS2) | 0x80);
            uint32_t c = cb_new(o, S2A_CPZBOSS, 0x10, 4, 0x40);
            if (c) {
                SET32(c, CB_PARENT, RAMPTR(o));
                cb_copy_pos(c, o);
                SET8(c, OBJ_2nd, 4);
                SET8(c, OBJ_anim, 9);
            }
        }
        uint32_t c = cb_new(o, S2A_CPZBOSS3, 0x10, 4, 0x40);
        if (c) {
            SET32(c, CB_PARENT, RAMPTR(o));
            cb_copy_pos(c, o);
            SET8(c, OBJ_2nd, 6);
        }
        SET8(o, OBJ_2nd, 2);
    }   /* fall through */
    case 2:                                             /* Main */
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 0x38);
        if (OB8(o, OBJ_status) & CB_DEAD) {
            SET8(o, OBJ_2nd, 0x0A);
        } else if (OB8(a1, CB_STATUS2) & 4) {
            uint32_t s2a1 = cb_container_slide(o, a1);
            if (s2a1) cb_container_aim(o, s2a1);
        } else if (OB8(a1, CB_STATUS2) & 0x20) {
            SET16(o, CB_TIMER2, OB16(o, CB_TIMER2) - 1);
            if (!OB16(o, CB_TIMER2))
                SET8(a1, CB_STATUS2, (OB8(a1, CB_STATUS2) & ~0x20) | 8 | 0x10);
        }
        cb_follow(o, cb_parent(o));
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 0x38);
        {
            int16_t d0 = (int16_t)OB16(o, CB_XVEL);
            if (OB8(o, OBJ_render) & 1) d0 = (int16_t)-d0;
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + d0);
        }
        cb_animate(o, S2A_CPZBOSS);
        o_draw(o);
        return;
    case 4:                                             /* Floor */
        if (OB8(o, OBJ_status) & CB_DEAD) { o_delete(o); return; }
        {
            uint32_t m = cb_parent(a1);
            if ((OB8(m, CB_STATUS2) & 0x20) && OB8(o, OBJ_anim) == 9) SET8(o, OBJ_anim, 0x0A);
        }
        cb_floor_end(o);
        return;
    case 6: {                                           /* Extend: the Mega Mack fills up */
        if (OB8(o, OBJ_status) & CB_DEAD) { o_delete(o); return; }
        uint32_t m = r32(a1 + CB_PARENT) & 0xFFFFFF;
        if (!m) { o_delete(o); return; }
        if (OB8(m, CB_STATUS2) & 8) {                   /* dump it */
            SET8(m, CB_STATUS2, OB8(m, CB_STATUS2) & ~8);
            SET8(o, OBJ_routine, 0x0C);
            SET8(o, OBJ_2nd, 0);
            SET8(o, OBJ_coll_flags, 0x87);
            cb_floor_end(o);
            return;
        }
        if (OB8(m, CB_STATUS2) & 2) {
            SET8(m, CB_STATUS2, OB8(m, CB_STATUS2) & ~2);
            if (!OB8(o, OBJ_anim)) SET8(o, OBJ_anim, 0x0B);
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
            if ((int8_t)OB8(o, OBJ_anim) >= 0x17)
                SET8(m, CB_STATUS2, (OB8(m, CB_STATUS2) & ~1) | 4);
            cb_floor_end(o);
            return;
        }
        if (OB8(o, OBJ_anim)) cb_floor_end(o);
        return;
    }
    case 8:                                             /* Floor2 */
        if (OB8(o, OBJ_status) & CB_DEAD) { o_delete(o); return; }
        SET16(o, CB_TIMER2, OB16(o, CB_TIMER2) - 1);
        if (!OB16(o, CB_TIMER2)) { o_delete(o); return; }
        cb_floor_end(o);
        return;
    default: {                                          /* FallOff: the tank breaks up */
        SET8(o, CB_TIMER, 0x1E);
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 0x38);
        int16_t xv = (int16_t)OB16(o, CB_XVEL);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_render) & 1) ? -xv : xv));
        SET8(o, OBJ_frame, 0x20);
        cb_fall_part(o, 0);
        int d7 = 0;
        int16_t d0 = (int16_t)(xv + 0x18);
        if (d0 < 0) { d7 = 1; d0 += 0x18; if (d0 < 0) { d7 = 2; d0 += 0x18; if (d0 < 0) d7 = 3; } }
        static const int16_t off[3] = { 0x18, 0x30, 0x48 };
        for (d7 = d7 - 1; d7 >= 0; d7--) {
            uint32_t p = cb_new(0, S2A_CPZBOSS, 0x14, 2, 0x20);
            if (!p) { o_delete(o); return; }
            SET8(p, OBJ_frame, 0x21);
            SET8(p, OBJ_render, OB8(o, OBJ_render));
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
            int16_t d3 = off[d7];
            SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + ((OB8(o, OBJ_render) & 1) ? -d3 : d3));
            cb_fall_part(p, 1);
        }
        o_draw(o);
        return;
    }
    }
}

static void cb_falling(uint32_t o)                      /* routine $14: Obj5D_FallingParts */
{
    if ((int8_t)OB8(o, CB_TIMER) != -7) {
        SET8(o, CB_TIMER, OB8(o, CB_TIMER) - 1);
        if ((int8_t)OB8(o, CB_TIMER) > 0) { o_draw(o); return; }
        cb_explosion(o, 0);                             /* Obj5D_Main_Explode2 */
        SET8(o, CB_TIMER, (uint8_t)-7);
        SET16(o, CB_TIMER2, 0x1E);
    }
    SET16(o, CB_TIMER2, OB16(o, CB_TIMER2) - 1);
    if ((int16_t)OB16(o, CB_TIMER2) >= 0) { o_draw(o); return; }
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, OBJ_x_vel));
    uint32_t d3 = r32(o + OBJ_y_pos) + (uint32_t)((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8);
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
    SET32(o, OBJ_y_pos, d3);
    if (d3 >= 0x5800000u) { o_delete(o); return; }
    o_remember(o);
}

static void cb_pump_top(uint32_t o)                     /* routine $12: Obj5D_Pump */
{
    uint32_t a1 = cb_parent(o);
    if (!(OB8(o, OBJ_status) & CB_DEAD)) {
        cb_copy_pos(o, a1);
        SET8(o, OBJ_render, OB8(a1, OBJ_render));
        SET8(o, OBJ_status, OB8(a1, OBJ_status));
        cb_animate(o, S2A_CPZBOSS);
        o_draw(o);
        return;
    }
    /* defeated: three pieces fall off (frames $22-$24) */
    SET8(o, CB_TIMER, 0x78);
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
    SET8(o, OBJ_frame, 0x22);
    cb_fall_part(o, 0);
    for (int k = 0; k < 2; k++) {
        uint32_t p = cb_new(0, S2A_CPZBOSS, 0x14, 2, 0x20);
        if (!p) { o_delete(o); return; }
        SET8(p, OBJ_frame, 0x23 + k);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(p, OBJ_status, OB8(o, OBJ_status));
        SET8(p, OBJ_render, OB8(o, OBJ_render));
        cb_fall_part(p, 1);
    }
    /* (S2 doesn't display this frame) */
}

static void cb_robotnik(uint32_t o)                     /* routine $16 */
{
    uint32_t a1 = cb_parent(o);
    cb_copy_pos(o, a1);
    SET8(o, OBJ_status, OB8(a1, OBJ_status));
    SET8(o, OBJ_render, OB8(a1, OBJ_render));
    if (OB8(a1, CB_INV) == 0x1F) SET8(o, OBJ_anim, 2);
    if (OB8(RAM_Player_1, OBJ_routine) == 4 || OB8(RAM_Player_2, OBJ_routine) == 4) SET8(o, OBJ_anim, 3);
    cb_animate(o, S2A_CPZPOD);
    o_draw(o);
}

static void cb_flame(uint32_t o)                        /* routine $18 */
{
    static const int8_t frames[3] = { 0, -1, 1 };
    if (OB8(o, OBJ_status) & CB_DEAD) return;           /* (S2's smoke trail never shows) */
    uint32_t a1 = cb_parent(o);
    cb_copy_pos(o, a1);
    SET8(o, OBJ_status, OB8(a1, OBJ_status));
    SET8(o, OBJ_render, OB8(a1, OBJ_render));
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 1);
        int d0 = OB8(o, CB_TIMER2) + 1;
        if (d0 > 2) d0 = 0;
        SET8(o, OBJ_frame, (uint8_t)frames[d0]);
        SET8(o, CB_TIMER2, d0);
    }
    if (OB8(o, OBJ_frame) == 0xFF) { SET8(o, OBJ_frame, 0); return; }
    o_draw(o);
}

static void cpz_boss(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj5D_Init */
        o_upload_asset(S2A_CPZPOD);
        o_upload_asset(S2A_CPZPOD1);
        o_upload_asset(S2A_CPZJET);
        o_upload_asset(S2A_CPZBOSS);
        o_upload_asset(S2A_CPZBOSS3);
        o_init(o, S2A_CPZPOD1, 4, 3, 0x20, 0x20);
        SET16(o, OBJ_x_pos, 0x2B80);
        SET16(o, OBJ_y_pos, 0x4B0);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        SET8(o, OBJ_routine, 2);
        SET32(o, CB_XNEXT, r32(o + OBJ_x_pos));
        SET32(o, CB_YNEXT, r32(o + OBJ_y_pos));
        SET8(o, CB_STATUS, OB8(o, CB_STATUS) & ~8);
        uint32_t a1 = cb_new(o, S2A_CPZPOD, 0x16, 3, 0x20);     /* Eggman */
        if (a1) {
            SET32(a1, CB_PARENT, RAMPTR(o));
            SET32(o, CB_PARENT, RAMPTR(a1));
            cb_copy_pos(a1, o);
            SET8(a1, OBJ_anim, 1);
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            if (!(OB8(o, OBJ_subtype) & 0x80)) {
                a1 = cb_new(o, S2A_CPZJET, 0x18, 3, 0x20);      /* exhaust flame */
                if (a1) {
                    SET32(a1, CB_PARENT, RAMPTR(o));
                    cb_copy_pos(a1, o);
                    SET8(a1, OBJ_render, OB8(o, OBJ_render));
                }
                a1 = cb_new(o, S2A_CPZBOSS, 0x12, 2, 0x20);     /* pump on top */
                if (a1) {
                    SET32(a1, CB_PARENT, RAMPTR(o));
                    cb_copy_pos(a1, o);
                }
            }
        }
        a1 = cb_new(o, S2A_CPZBOSS, 0x10, 4, 0x40);             /* the glass container */
        if (a1) {
            SET32(a1, CB_PARENT, RAMPTR(o));
            cb_copy_pos(a1, o);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 0x38);
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 0x10);
            SET16(a1, CB_XVEL, (uint16_t)-0x10);
            SET8(a1, OBJ_anim, 6);
        }
        a1 = cb_new(o, S2A_CPZBOSS, 4, 4, 0x20);                /* the pipe */
        if (a1) {
            SET32(a1, CB_PARENT, RAMPTR(o));
            cb_copy_pos(a1, o);
        }
        return;
    }
    case 2:                                             /* Obj5D_Main */
        if (getenv("TRILOGY_BOSSKILL") && OB8(o, OBJ_2nd) == 6)
            SET8(o, OBJ_status, OB8(o, OBJ_status) | CB_DEAD);  /* development */
        if (!cb_main(o)) return;
        cb_animate(o, S2A_CPZPOD1);
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | (OB8(o, OBJ_status) & 3));
        o_touch(o);
        o_draw(o);
        return;
    case 4:    cb_pipe(o); return;
    case 6:    cb_pipe_pump(o); return;
    case 8:    cb_pipe_retract(o); return;
    case 0x0A: cb_dripper(o); return;
    case 0x0C: cb_gunk(o); return;
    case 0x0E: cb_pipe_segment(o); return;
    case 0x10: cb_container(o); return;
    case 0x12: cb_pump_top(o); return;
    case 0x14: cb_falling(o); return;
    case 0x16: cb_robotnik(o); return;
    default:   cb_flame(o); return;
    }
}

/* ------------------------------------------------------------------------
 * S2's later boss framework (HTZ, ARZ, MCZ, CNZ, MTZ, OOZ): Boss_X/Y_pos
 * and velocities, Boss_Countdown, Boss_AnimationArray driving the main
 * frame and the child sprites (AnimateBoss), Boss_MoveObject, explosions.
 * S2 bosses are multi-sprite objects whose hit count sits at $32 because
 * collision_property overlaps a child sprite; here the boss object keeps
 * collision and hits (collision_property) and a second, multi-draw object
 * shows the child sprites (S3K: $16 count, $18 + 6n x/y/-/frame).
 * ---------------------------------------------------------------------- */
#define MS_COUNT 0x16
#define MS_SUB   0x18

static struct {
    int32_t x, y;                     /* Boss_X_pos / Boss_Y_pos (16.16) */
    int16_t xv, yv;                   /* Boss_X_vel / Boss_Y_vel */
    int16_t countdown;                /* Boss_Countdown */
    uint8_t anim[16];                 /* Boss_AnimationArray */
    uint8_t collision;                /* Boss_CollisionRoutine */
} bfw;
uint8_t s2_screen_shaking;            /* Screen_Shaking_Flag (trilogy_s2bg.inc) */
uint8_t s2_current_boss;              /* Current_Boss_ID (ARZ's tile animation stops) */

static void bfw_move(void)                           /* Boss_MoveObject */
{
    bfw.x += (int32_t)bfw.xv << 8;
    bfw.y += (int32_t)bfw.yv << 8;
}

static uint32_t bfw_child_frame(uint32_t parts, int k) { return parts + MS_SUB + (uint32_t)k * 6u + 5u; }

/* AnimateBoss: entry 0 drives the boss object's frame (when it has one),
 * entries 1.. the parts' child sprites. 0 = the script raised the routine. */
static void bfw_animate(uint32_t o, uint32_t parts, uint32_t ani, int routine_field)
{
    int e = 0;
    int count = parts ? OB16(parts, MS_COUNT) : 0;
    for (int k = -1; k < count; k++, e++) {
        uint32_t fp;
        if (k < 0) {
            if (!OB8(o, OBJ_frame)) continue;           /* no main frame: skip its entry */
            fp = o + OBJ_frame;
        } else {
            fp = bfw_child_frame(parts, k);
        }
        uint8_t b0 = bfw.anim[e * 2], b1 = bfw.anim[e * 2 + 1];
        uint8_t id1 = b0 >> 4, id2 = b0 & 0x0F;
        uint8_t d5 = (uint8_t)(id2 | (id2 << 4));
        uint8_t d1 = b1 >> 4, d0 = b1;
        if (id1 != id2) { d0 = 0; d1 = 0; }
        int8_t t = (int8_t)((d0 & 0x0F) - 1);
        if (t >= 0) {
            d0 = (uint8_t)t;
        } else {
            uint32_t sc = ani + tri_rd16(ani + id2 * 2u);
            d0 = tri_rd8(sc);
            uint8_t f = tri_rd8(sc + 1 + d1);
            if (f & 0x80) {
                switch (f) {
                case 0xFF: d1 = 0; f = tri_rd8(sc + 1); break;
                case 0xFE: SET8(o, routine_field, OB8(o, routine_field) + 2); return;
                case 0xFD:
                    d5 = (uint8_t)((d5 & 0xF0) | tri_rd8(sc + 2 + d1));
                    goto store;
                case 0xFC:
                    d1 = tri_rd8(sc + 2 + d1);
                    f = tri_rd8(sc + 1 + d1);
                    break;
                default: return;
                }
            }
            w8(fp, f & 0x7F);
            d1++;
        }
    store:
        bfw.anim[e * 2 + 1] = (uint8_t)((d1 << 4) | d0);
        bfw.anim[e * 2] = d5;
    }
}

static void bfw_explosion(uint32_t o)                /* Boss_LoadExplosion */
{
    if (!(r8(RAM_Level_frame_counter + 1) & 7)) cb_explosion(o, 1);
}

static uint32_t bfw_parts_new(uint32_t o, int asset, int count, int w, int h)
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o));
    o_asset(a1, asset);
    SET8(a1, OBJ_render, 4 | 0x40);
    SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
    SET8(a1, OBJ_width_px, w);
    SET8(a1, OBJ_height_px, h);
    SET16(a1, MS_COUNT, (uint16_t)count);
    SET8(a1, OBJ_frame, 0);                             /* the boss object draws the main frame */
    return a1;
}

static void bfw_child(uint32_t parts, int k, int16_t x, int16_t y)
{
    uint32_t a = parts + MS_SUB + (uint32_t)k * 6u;
    w16(a, (uint16_t)x);
    w16(a + 2, (uint16_t)y);
}

/* a player overlapping box (x, y, half width w, half height h) is hurt (Boss_DoCollision) */
static void bfw_hurt_box(uint32_t o, int16_t x, int16_t y, int w, int h)
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || OB8(p, OBJ_routine) != 2) continue;
        int16_t pr = (int16_t)(OB8(p, OBJ_y_radius) - 3);
        int16_t px0 = (int16_t)(OB16(p, OBJ_x_pos) - 8), py0 = (int16_t)(OB16(p, OBJ_y_pos) - pr);
        if (px0 + 0x10 <= x - w || px0 >= x + w) continue;
        if (py0 + pr * 2 <= y - h || py0 >= y + h) continue;
        o_hurt_player(p, o);
    }
}

/* ------------------------------------------------------------------------
 * Object 89 - ARZ boss: Eggman with a hammer between two arrow pillars.
 * routines: 2 boss, 4 pillar, 6 arrow, 8 bulging eyes (S2 boss_subtype)
 * ---------------------------------------------------------------------- */
#define AB_ROUTINE  0x30              /* boss_routine (angle) */
#define AB_SINE     0x31              /* boss_sine_count */
#define AB_INV      0x32              /* boss_invulnerable_time */
#define AB_DEFEATED 0x33              /* boss_defeated */
#define AB_TARGET   0x34              /* obj89_target */
#define AB_HFLAGS   0x35              /* obj89_hammer_flags */
#define AB_HYVEL    0x36              /* obj89_hammer_y_vel (word) */
#define AB_HY       0x38              /* obj89_hammer_y_pos (long) */
#define AB_PARTS    0x3C              /* the child sprites */
#define AB_PARENT   0x40              /* pillars/eyes/arrows: the boss (obj89_pillar_parent / arrow_parent) */
#define AB_PTIME    0x44              /* obj89_pillar_shake_time / eyes_timer / arrow_timer */
#define AB_SHAKING  0x46              /* obj89_pillar_shaking */
#define AB_PARENT2  0x34              /* arrow: obj89_arrow_parent2 (its eyes) */
#define AB_CAPSULE  0x47              /* boss: capsule art requested */

static uint32_t ab_parent(uint32_t o) { return r32(o + AB_PARENT) & 0xFFFFFF; }

static void ab_align(uint32_t o)                     /* Obj89_Main_AlignParts */
{
    uint32_t parts = r32(o + AB_PARTS) & 0xFFFFFF;
    if (!parts) return;
    int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
    bfw_child(parts, 0, x, y);                          /* face */
    bfw_child(parts, 2, x, y);                          /* flames */
    if (!OB8(o, AB_DEFEATED)) {
        bfw_child(parts, 1, x, y);                      /* hammer */
        SET16(o, AB_HY, (uint16_t)y);
        return;
    }
    if (bfw.countdown > 0x78) return;                   /* Obj89_Main_DropHammer */
    uint32_t a = parts + MS_SUB + 6u;
    w16(a, r16(a) - 1);
    int32_t hy = (int32_t)r32(o + AB_HY) + ((int32_t)(int16_t)OB16(o, AB_HYVEL) << 8);
    SET16(o, AB_HYVEL, OB16(o, AB_HYVEL) + 0x38);
    SET32(o, AB_HY, (uint32_t)hy);
    w16(a + 2, OB16(o, AB_HY));
    if ((int16_t)r16(a + 2) >= 0x540) SET16(o, AB_HYVEL, 0);
}

static void ab_hover_hits(uint32_t o)                 /* Obj89_Main_HandleHoveringAndHits */
{
    int16_t s, c;
    o_sincos(OB8(o, AB_SINE), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + (int16_t)(bfw.y >> 16)));
    SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
    SET8(o, AB_SINE, OB8(o, AB_SINE) + 2);
    if (OB8(o, AB_ROUTINE) >= 8) return;
    if (!OB8(o, OBJ_coll_prop)) {                       /* Obj89_Main_KillBoss */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        bfw.countdown = 0xB3;
        SET8(o, AB_ROUTINE, 8);
        bfw.anim[2] = 5;
        bfw.anim[3] = 0;
        uint32_t parts = r32(o + AB_PARTS) & 0xFFFFFF;
        if (parts) w8(bfw_child_frame(parts, 0), 5);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, AB_INV)) {
        SET8(o, AB_INV, 64);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, AB_INV, OB8(o, AB_INV) - 1);
    if (!OB8(o, AB_INV)) SET8(o, OBJ_coll_flags, 0x0F);
}

static void ab_face(uint32_t o)                      /* Obj89_Main_HandleFace */
{
    ab_hover_hits(o);
    if (OB8(RAM_Player_1, OBJ_routine) == 4 || OB8(RAM_Player_2, OBJ_routine) == 4)
        bfw.anim[3] = 0x31;                             /* laughing */
    if (OB8(o, AB_INV) == 64 - 1) bfw.anim[3] = 0xC0;   /* hurt */
}

static void ab_main(uint32_t o)
{
    uint32_t parts = r32(o + AB_PARTS) & 0xFFFFFF;
    uint32_t ani = o_asset_anim(S2A_ARZBOSS);
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, AB_ROUTINE) == 6) SET8(o, OBJ_coll_prop, 0);   /* development */
    switch (OB8(o, AB_ROUTINE)) {
    case 0:                                             /* moving down into the arena */
        bfw_move(); ab_face(o); ab_align(o);
        if ((int16_t)(bfw.y >> 16) >= 0x430) {
            bfw.y = (int32_t)0x430 << 16;
            SET8(o, AB_ROUTINE, 2);
            bfw.yv = 0;
            bfw.xv = -0xC8;
            SET8(o, AB_TARGET, 0xFF);
        }
        break;
    case 2:                                             /* to the next pillar */
        bfw_move(); ab_face(o); ab_align(o);
        if (OB8(o, AB_TARGET) ? (int16_t)(bfw.x >> 16) <= 0x2AB0 : (int16_t)(bfw.x >> 16) >= 0x2B10) {
            SET8(o, AB_ROUTINE, 4);
            bfw.xv = 0;
        }
        break;
    case 4:                                             /* wait for the hover's top, then swing */
        bfw_move(); ab_face(o); ab_align(o);
        if (OB8(o, AB_SINE) == 0xC0) {
            bfw.anim[4] = (uint8_t)((bfw.anim[4] & 0xF0) | 3);
            SET8(o, AB_ROUTINE, 6);
            SET8(o, AB_TARGET, (OB8(o, OBJ_render) & 1) ? 0xFF : 0);
            bfw.countdown = 0x1E;
            o_sfx_s2(0xBD);                                /* SndID_Hammer */
        }
        break;
    case 6:                                             /* the hammer strikes a pillar */
        if (bfw.countdown == 0x14) {
            SET8(o, AB_HFLAGS, OB8(o, AB_HFLAGS) | 1);
            bfw.collision = 1;
        }
        if (--bfw.countdown < 0) {
            bfw.collision = 0;
            SET8(o, AB_ROUTINE, 2);
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
            bfw.xv = (OB8(o, OBJ_render) & 1) ? 0xC8 : -0xC8;
        }
        bfw_move(); ab_face(o); ab_align(o);
        break;
    case 8:                                             /* exploding */
        SET8(o, AB_DEFEATED, 0xFF);
        if (--bfw.countdown < 0) {                      /* Obj89_Main_SetupEscapeAnim */
            if (parts) SET16(parts, MS_COUNT, 3);
            bfw.anim[4] = 1; bfw.anim[5] = 0;
            bfw.anim[2] = 0; bfw.anim[3] = 0;
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            bfw.xv = bfw.yv = 0;
            SET8(o, AB_ROUTINE, 0x0A);
            bfw.countdown = -0x12;
        } else {
            bfw_explosion(o);
        }
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        bfw_animate(o, parts, ani, AB_ROUTINE);
        ab_align(o);
        goto draw;
    case 0x0A:                                          /* drop a little, then rise */
        ++bfw.countdown;
        if (bfw.countdown == 0) bfw.yv = 0;
        else if (bfw.countdown < 0) bfw.yv += 0x18;
        else if ((uint16_t)bfw.countdown < 0x18) bfw.yv -= 8;
        else if (bfw.countdown == 0x18) { bfw.yv = 0; tri_music_zone(); }
        else if ((uint16_t)bfw.countdown >= 0x20) SET8(o, AB_ROUTINE, 0x0C);
        goto escape;
    default:                                            /* fly off */
        bfw.xv = 0x400;
        bfw.yv = -0x40;
        if (!OB8(o, AB_CAPSULE)) {                      /* the capsule art, now the pillars are gone */
            SET8(o, AB_CAPSULE, 1);
            o_upload_asset(S2A_CAPSULE);
        }
        if (r16(RAM_Camera_max_X_pos) < 0x2C00) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
        } else if (!(OB8(o, OBJ_render) & 0x80)) {
            if (parts) o_delete(parts);
            o_delete(o);
            return;
        }
    escape:
        bfw_move();
        ab_hover_hits(o);
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        bfw_animate(o, parts, ani, AB_ROUTINE);
        ab_align(o);
        goto draw;
    }
    bfw_animate(o, parts, ani, AB_ROUTINE);
draw:
    if (bfw.collision) {                                /* BossCollision_ARZ: the hammer's head */
        int16_t hx = (int16_t)(OB16(o, OBJ_x_pos) - 0x50 + ((OB8(o, OBJ_render) & 1) ? 0xA0 : 0));
        bfw_hurt_box(o, hx, (int16_t)(OB16(o, OBJ_y_pos) + 4), 0x10, 0x14);
    }
    if (parts) {
        SET8(parts, OBJ_render, (OB8(parts, OBJ_render) & ~1) | (OB8(o, OBJ_render) & 1));
        SET16(parts, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(parts, OBJ_y_pos, OB16(o, OBJ_y_pos));
    }
    o_touch(o);
    o_draw(o);
}

static void ab_pillar_solid(uint32_t o)              /* Obj89_Pillar_SolidObject */
{
    uint16_t y = OB16(o, OBJ_y_pos);
    SET16(o, OBJ_y_pos, y + 4);
    o_solid(o, 0x23, 0x44, 0x45, OB16(o, OBJ_x_pos));
    SET16(o, OBJ_y_pos, y);
}

static void ab_pillar_shoot(uint32_t o, uint32_t boss)   /* Obj89_Pillar_Shoot */
{
    static const uint16_t heights[4] = { 0x458, 0x478, 0x498, 0x4B8 };
    uint32_t eyes = o_alloc();
    if (!eyes) return;
    SET32(eyes, OBJ_code, S2(0x89));
    SET8(eyes, OBJ_routine, 8);
    o_init(eyes, S2A_ARZPILLARS, 4, 0, 0x10, 0x10);   /* (S2 never sets its priority: 0, over the pillar) */
    SET8(eyes, OBJ_frame, 2);
    int right = OB8(boss, AB_TARGET) != 0;
    SET16(eyes, OBJ_x_pos, right ? 0x2B56 : 0x2A6A);
    if (right) SET8(eyes, OBJ_render, OB8(eyes, OBJ_render) | 1);
    SET16(eyes, AB_PTIME, 0x28);
    SET16(eyes, OBJ_y_pos, heights[o_random() & 3]);
    uint32_t ar = o_alloc();
    if (!ar) return;
    SET32(ar, OBJ_code, S2(0x89));
    SET8(ar, OBJ_routine, 6);
    SET32(ar, AB_PARENT2, RAMPTR(eyes));
    SET8(ar, OBJ_subtype, right ? 0xFF : 0);
    SET32(ar, AB_PARENT, RAMPTR(boss));
}

static void ab_pillar(uint32_t o)                    /* routine 4 */
{
    uint32_t boss = ab_parent(o);
    if (OB8(boss, AB_ROUTINE) >= 8) SET8(o, OBJ_2nd, 4);
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* rise out of the floor */
        ab_pillar_solid(o);
        if (!(r8(RAM_Level_frame_counter + 1) & 0x1F)) o_sfx_s2(0xE1);   /* SndID_Rumbling2 */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 1);
        if ((int16_t)OB16(o, OBJ_y_pos) <= 0x488) {
            SET8(o, OBJ_2nd, 2);
            s2_screen_shaking = 0;
        }
        break;
    case 2: {                                           /* fire when the hammer strikes this side */
        ab_pillar_solid(o);
        if (OB8(boss, AB_HFLAGS) & 1) {
            int right_target = !OB8(boss, AB_TARGET);
            if (right_target ? !(OB8(o, OBJ_render) & 1) : (OB8(o, OBJ_render) & 1)) {
                SET8(boss, AB_HFLAGS, OB8(boss, AB_HFLAGS) & ~1);
                ab_pillar_shoot(o, boss);
                SET8(o, AB_SHAKING, 0xFF);
            }
        }
        if (OB8(o, AB_SHAKING)) {                       /* Obj89_Pillar_ChkShake */
            if ((int16_t)OB16(o, AB_PTIME) <= 0) SET16(o, AB_PTIME, 0x1F);
            SET16(o, AB_PTIME, OB16(o, AB_PTIME) - 1);
            if ((int16_t)OB16(o, AB_PTIME) > 0) {
                static const int16_t sh[2] = { 1, -1 };
                int16_t d1 = OB8(boss, AB_TARGET) ? 0x2B70 : 0x2A50;
                int k = r8(RAM_Level_frame_counter + 1) & 1;
                SET16(o, OBJ_x_pos, (uint16_t)(d1 + sh[k]));
                SET16(o, OBJ_y_pos, (uint16_t)(0x488 + sh[k]));
            } else {
                SET8(o, AB_SHAKING, 0);
                SET16(o, AB_PTIME, 0);
                SET16(o, OBJ_x_pos, OB8(boss, AB_TARGET) ? 0x2B70 : 0x2A50);
                SET16(o, OBJ_y_pos, 0x488);
            }
        }
        break;
    }
    default:                                            /* sink back down */
        s2_screen_shaking = 1;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        if ((int16_t)OB16(o, OBJ_y_pos) >= 0x510) {
            s2_screen_shaking = 0;
            o_delete(o);
            return;
        }
        break;
    }
    o_draw(o);
}

static void ab_arrow(uint32_t o)                     /* routine 6 */
{
    uint32_t boss = ab_parent(o);
    if (OB8(boss, AB_ROUTINE) >= 8 && OB8(o, OBJ_2nd) < 6) SET8(o, OBJ_2nd, 6);
    switch (OB8(o, OBJ_2nd)) {
    case 0: {                                           /* Obj89_Arrow_Init */
        uint32_t eyes = r32(o + AB_PARENT2) & 0xFFFFFF;
        o_init(o, S2A_ARZPILLARS, 4, 4, 0x10, 8);
        SET8(o, OBJ_2nd, 2);
        SET16(o, OBJ_x_pos, OB16(eyes, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(eyes, OBJ_y_pos) + 9);
        SET16(o, OBJ_y_vel, 4);
        SET8(o, OBJ_frame, 4);
        if (OB8(o, OBJ_subtype)) {
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            SET16(o, OBJ_x_vel, (uint16_t)-3);
        } else {
            SET16(o, OBJ_x_vel, 3);
        }
        SET8(o, OBJ_coll_flags, 0xB0);
        return;
    }
    case 2: {                                           /* flying across */
        if (OB8(o, OBJ_status) & 0x80) { SET8(o, OBJ_2nd, 8); break; }
        int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) + OB16(o, OBJ_x_vel));
        int stop = 0;
        if ((int16_t)OB16(o, OBJ_x_vel) < 0) { if (d0 <= 0x2A77) { d0 = 0x2A77; stop = 1; } }
        else if (d0 >= 0x2B49) { d0 = 0x2B49; stop = 1; }
        SET16(o, OBJ_x_pos, (uint16_t)d0);
        if (stop) { SET8(o, OBJ_2nd, 4); o_sfx_s2(0xDD); /* SndID_ArrowStick */ }   /* (S2: ArrowStick) */
        o_touch(o);
        break;
    }
    case 4:                                             /* stuck: a little platform */
        SET8(o, OBJ_coll_flags, 0);
        if (OB8(o, OBJ_status) & 0x80) SET8(o, OBJ_2nd, 6);
        if (!OB16(o, AB_PTIME)) {                       /* Obj89_Arrow_Platform (retail) */
            o_platform(o, 0x1B, 2, OB16(o, OBJ_x_pos));
            if (OB8(o, OBJ_status) & ST_P1_STAND) SET16(o, AB_PTIME, 0x1F);
        }
        if (OB16(o, AB_PTIME)) {
            SET16(o, AB_PTIME, OB16(o, AB_PTIME) - 1);
            if (!OB16(o, AB_PTIME)) SET8(o, OBJ_2nd, 6);
        }
        o_animate_s2(o, o_asset_anim(S2A_ARZPILLARS));
        break;
    case 6: {                                           /* fall away */
        for (int n = 0; n < 2; n++) {                   /* Obj89_Arrow_ChkDropPlayers */
            uint8_t bit = (uint8_t)(ST_P1_STAND << n);
            if (!(OB8(o, OBJ_status) & bit)) continue;
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
            uint32_t p = o_player(n);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
        }
        int16_t d0 = (int16_t)(OB16(o, OBJ_y_pos) + OB16(o, OBJ_y_vel));
        if (d0 > 0x4F0) { o_delete(o); return; }
        SET16(o, OBJ_y_pos, (uint16_t)d0);
        break;
    }
    default:
        o_delete(o);
        return;
    }
    o_draw(o);
}

static void arz_boss(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the boss's child sprites */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj89_Init */
        if (o_player_active(RAM_Player_2)) {            /* Sonic & Tails: wait until both are in */
            int16_t px = (int16_t)OB16(RAM_Player_1, OBJ_x_pos);
            if (px < 0x2A60 || px > 0x2B60) return;     /* Obj89_Init_Standard */
            if (OB8(RAM_Player_2, 0x2E) != 0x81) {      /* (obj_control $81: flying in) */
                px = (int16_t)OB16(RAM_Player_2, OBJ_x_pos);
                if (px < 0x2A60 || px > 0x2B60) return;
            }
        }
        o_upload_asset(S2A_ARZBOSS);
        o_upload_asset(S2A_ARZPILLARS);
        s2_screen_shaking = 1;
        o_init(o, S2A_ARZBOSS, 4, 2, 0x20, 0x20);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_pos, 0x2AE0);
        SET16(o, OBJ_y_pos, 0x388);
        bfw.x = (int32_t)0x2AE0 << 16;
        bfw.y = (int32_t)0x388 << 16;
        bfw.xv = 0;
        bfw.yv = 0x100;
        bfw.collision = 0;
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        SET8(o, OBJ_frame, 8);                          /* mainspr_mapframe: the Eggmobile */
        SET16(o, AB_HYVEL, (uint16_t)-0x380);
        uint32_t parts = bfw_parts_new(o, S2A_ARZBOSS, 3, 0x20, 0x40);
        SET32(o, AB_PARTS, parts);
        if (parts) {
            static const uint8_t frames[3] = { 0, 9, 6 };   /* face, hammer, flames */
            for (int k = 0; k < 3; k++) {
                bfw_child(parts, k, 0x2AE0, 0x488);
                w8(bfw_child_frame(parts, k), frames[k]);
            }
        }
        static const uint8_t init_anim[8] = { 4, 0, 0, 0, 2, 0, 1, 0 };   /* Obj89_Init_AnimationArray */
        memset(bfw.anim, 0, sizeof bfw.anim);
        memcpy(bfw.anim, init_anim, sizeof init_anim);
        for (int k = 0; k < 2; k++) {                   /* the two pillars */
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET32(a1, OBJ_code, S2(0x89));
            SET8(a1, OBJ_routine, 4);
            o_init(a1, S2A_ARZPILLARS, 4 | (k ? 1 : 0), 2, 0x10, 0x48);
            SET16(a1, OBJ_x_pos, k ? 0x2B70 : 0x2A50);
            SET16(a1, OBJ_y_pos, 0x510);
            SET32(a1, AB_PARENT, RAMPTR(o));
            SET8(a1, OBJ_frame, 0);
            SET8(a1, OBJ_y_radius, 0x20);
        }
        return;
    }
    case 2:    ab_main(o); return;
    case 4:    ab_pillar(o); return;
    case 6:    ab_arrow(o); return;
    default:                                            /* Obj89_Pillar_BulgingEyes */
        SET16(o, AB_PTIME, OB16(o, AB_PTIME) - 1);
        if (!OB16(o, AB_PTIME)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 51 - CNZ boss: Eggman's zapper. Routines: 2 boss, 4 electric ball
 * (S2 boss_subtype). Child sprites (parts): sub2..sub5.
 * ---------------------------------------------------------------------- */
#define CZ_ROUTINE  0x30              /* boss_routine */
#define CZ_SINE     0x31              /* boss_sine_count */
#define CZ_INV      0x32              /* boss_invulnerable_time */
#define CZ_DEFEATED 0x33              /* boss_defeated */
#define CZ_DIR      0x34              /* objoff_38 */
#define CZ_ZAPS     0x35              /* objoff_2D */
#define CZ_COOL     0x36              /* objoff_3F */
#define CZ_DIVE     0x37              /* objoff_3E */
#define CZ_V5       0x38              /* objoff_2E: falling generator (sub5) velocity */
#define CZ_V2       0x3A              /* objoff_30: (sub2) */
#define CZ_Y5       0x3C              /* objoff_3A (long) */
#define CZ_Y2       0x40              /* objoff_34 (long) */
#define CZ_PARTS    0x44
#define CZ_CAPSULE  0x48
#define BL_PARENT   0x40              /* ball: objoff_34 */
#define BL_OFF      0x44              /* ball: objoff_28 (drop offset) */

void s2_cnz_boss_wall(uint8_t chunk);  /* trilogy_s2dle.c: Level_Layout+$C54 */

static uint32_t cz_parts(uint32_t o) { return r32(o + CZ_PARTS) & 0xFFFFFF; }

static void cz_hover_hits(uint32_t o)               /* loc_31CDC */
{
    int16_t s, c;
    o_sincos(OB8(o, CZ_SINE), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + (int16_t)(bfw.y >> 16)));
    SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
    SET8(o, CZ_SINE, OB8(o, CZ_SINE) + 2);
    if (OB8(o, CZ_ROUTINE) >= 6) return;
    if (!OB8(o, OBJ_coll_prop)) {                       /* loc_31D42 */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        bfw.countdown = 0xB3;
        SET8(o, CZ_ROUTINE, 6);
        if (!OB8(o, CZ_CAPSULE)) { SET8(o, CZ_CAPSULE, 1); o_upload_asset(S2A_CAPSULE); }
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, CZ_INV)) {
        SET8(o, CZ_INV, 48);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, CZ_INV, OB8(o, CZ_INV) - 1);
    if (!OB8(o, CZ_INV)) SET8(o, OBJ_coll_flags, 0x0F);
}

static void cz_align(uint32_t o)                    /* loc_31E76 */
{
    uint32_t parts = cz_parts(o);
    if (!parts) return;
    int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
    bfw_child(parts, 1, x, y);                          /* sub3 */
    bfw_child(parts, 2, x, y);                          /* sub4 */
    if (!OB8(o, CZ_DEFEATED)) {
        bfw_child(parts, 3, x, y);                      /* sub5 */
        bfw_child(parts, 0, x, y);                      /* sub2 */
        SET16(o, CZ_Y5, (uint16_t)y);
        SET16(o, CZ_Y2, (uint16_t)y);
        return;
    }
    if (bfw.countdown > 0x78) return;                   /* the two generators drop off */
    uint32_t a5 = parts + MS_SUB + 3u * 6u, a2 = parts + MS_SUB;
    w16(a5, (uint16_t)(r16(a5) - 1));
    int32_t y5 = (int32_t)r32(o + CZ_Y5) + ((int32_t)(int16_t)OB16(o, CZ_V5) << 8);
    SET16(o, CZ_V5, OB16(o, CZ_V5) + 0x38);
    SET32(o, CZ_Y5, (uint32_t)y5);
    w16(a5 + 2, OB16(o, CZ_Y5));
    if ((int16_t)r16(a5 + 2) >= 0x6F0) SET16(o, CZ_V5, 0);
    if (bfw.countdown > 60) return;
    w16(a2, (uint16_t)(r16(a2) + 1));
    int32_t y2 = (int32_t)r32(o + CZ_Y2) + ((int32_t)(int16_t)OB16(o, CZ_V2) << 8);
    SET16(o, CZ_V2, OB16(o, CZ_V2) + 0x38);
    SET32(o, CZ_Y2, (uint32_t)y2);
    w16(a2 + 2, OB16(o, CZ_Y2));
    if ((int16_t)r16(a2 + 2) >= 0x6F0) SET16(o, CZ_V2, 0);
}

static void cz_face(uint32_t o)                     /* loc_31C92 */
{
    if (OB8(o, CZ_INV) == 48 - 1) { bfw.anim[6] = (uint8_t)((bfw.anim[6] & 0xF0) | 6); return; }
    if (OB8(RAM_Player_1, OBJ_routine) == 4 || OB8(RAM_Player_2, OBJ_routine) == 4)
        if ((bfw.anim[6] & 0x0F) != 6) bfw.anim[6] = (uint8_t)((bfw.anim[6] & 0xF0) | 5);
}

static void cz_zap_mode(uint32_t o, int on)         /* the arc under the zapper: on / off */
{
    SET8(o, OBJ_frame, on ? 0x0F : 0x0C);
    bfw.collision = on ? 2 : 1;
    bfw.anim[3] = bfw.anim[9] = on ? 0x20 : 0;
    bfw.anim[0] = on ? 9 : 4;
}

static void cz_drop_ball(uint32_t o)                /* loc_31BF2 */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x51));
    SET8(a1, OBJ_routine, 4);
    SET32(a1, BL_PARENT, RAMPTR(o));
}

static void cz_main(uint32_t o)
{
    uint32_t parts = cz_parts(o);
    uint32_t ani = o_asset_anim(S2A_CNZBOSS);
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, CZ_ROUTINE) == 0) SET8(o, OBJ_coll_prop, 0);   /* development */
    if (bfw.collision && !(r8(RAM_Level_frame_counter + 1) & 0x1F)) o_sfx_s2(0xD0);   /* SndID_CNZBossZap */
    switch (OB8(o, CZ_ROUTINE)) {
    case 0: {                                           /* loc_31A36: patrol */
        if (!OB8(o, CZ_DIR)) {
            if ((int16_t)(bfw.x >> 16) <= 0x28C0) {
                bfw.x = (int32_t)0x28C0 << 16; bfw.yv = 0; bfw.xv = 0x180;
                SET8(o, CZ_DIR, 2);
                SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
                SET8(o, CZ_ZAPS, 0);
            }
        } else if ((int16_t)(bfw.x >> 16) >= 0x29C0) {
            bfw.x = (int32_t)0x29C0 << 16; bfw.yv = 0; bfw.xv = -0x180;
            SET8(o, CZ_DIR, 0);
            SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
            SET8(o, CZ_ZAPS, 0);
        }
        bfw_move();                                     /* loc_31AA4 */
        if (OB8(o, CZ_COOL)) { SET8(o, CZ_COOL, OB8(o, CZ_COOL) - 1); goto blink; }
        uint16_t d0 = (uint16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x10);
        if (d0 >= 0x20) goto blink;
        if (OB16(RAM_Player_1, OBJ_y_pos) >= 0x6B0) {    /* Sonic below: drop a ball */
            if (OB8(o, CZ_ZAPS) >= 3) goto blink;
            SET8(o, CZ_ZAPS, OB8(o, CZ_ZAPS) + 1);
            SET8(o, CZ_ROUTINE, 2);
            bfw.anim[0] = 8; bfw.anim[3] = 0; bfw.anim[9] = 0;
            bfw.collision = 0;
            cz_drop_ball(o);
            bfw.countdown = 0x50;
            break;
        }
        if (OB16(RAM_Player_1, OBJ_y_pos) < 0x67C) goto blink;
        cz_zap_mode(o, 1);                              /* loc_31B06: dive at him, arc on */
        SET8(o, CZ_ROUTINE, 4);
        bfw.xv = 0; bfw.yv = 0x180;
        SET8(o, CZ_DIVE, 0);
        break;
    blink:                                              /* loc_31B46: arc on/off every $40 frames */
        bfw.countdown++;
        if (bfw.countdown & 0x3F) break;
        cz_zap_mode(o, (bfw.countdown & 0x40) != 0);
        break;
    }
    case 2:                                             /* loc_31BA8: dropping the ball */
        bfw.collision = 0;
        if (--bfw.countdown == 0) { bfw.anim[3] = bfw.anim[9] = 0x20; break; }
        if (bfw.countdown > -0x14) break;
        bfw.anim[3] = bfw.anim[9] = 0;
        SET8(o, CZ_ROUTINE, 0);
        bfw.countdown = -1;
        SET8(o, CZ_COOL, 0x40);
        break;
    case 4:                                             /* loc_31C22: dive and back up */
        bfw_move();
        if (!OB8(o, CZ_DIVE)) {
            if (OB16(o, OBJ_y_pos) < 0x680) break;
            bfw.xv = 0; bfw.yv = -0x180;
            SET8(o, CZ_DIVE, 0xFF);
            bfw.collision = 1;
            bfw.anim[3] = bfw.anim[9] = 0;
            bfw.anim[0] = 4;
            break;
        }
        if (OB16(o, OBJ_y_pos) >= 0x654) break;
        SET8(o, CZ_ROUTINE, 0);
        bfw.yv = 0; bfw.xv = -0x180;
        if (OB8(o, OBJ_render) & 1) { bfw.xv = 0x180; SET8(o, OBJ_frame, 0x0C); }
        break;
    case 6:                                             /* loc_31D5C: exploding */
        SET8(o, CZ_DEFEATED, 0xFF);
        if (--bfw.countdown >= 0) {
            bfw.collision = 0;
            SET8(o, OBJ_frame, 0);
            bfw_explosion(o);
        } else {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            bfw.xv = bfw.yv = 0;
            SET8(o, CZ_ROUTINE, 8);
            bfw.anim[6] = (uint8_t)((bfw.anim[6] & 0xF0) | 3);
            bfw.anim[0] = 8;
            s2_cnz_boss_wall(0xDD);                     /* the arena's right wall opens */
            bfw.countdown = -0x12;
        }
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        cz_align(o);
        goto draw;
    case 8:                                             /* loc_31DCC: sag, then rise */
        ++bfw.countdown;
        if (bfw.countdown == 0) bfw.yv = 0;
        else if (bfw.countdown < 0) bfw.yv += 0x18;
        else if (bfw.countdown < 0x18) bfw.yv -= 8;
        else if (bfw.countdown == 0x18) { bfw.yv = 0; tri_music_zone(); s2_boss_defeated_flag = 1; }
        else if (bfw.countdown >= 0x20) SET8(o, CZ_ROUTINE, 0x0A);
        bfw_move();
        cz_hover_hits(o);
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        cz_align(o);
        goto draw;
    default:                                            /* loc_31E2A: fly off */
        bfw.xv = 0x400;
        bfw.yv = -0x40;
        if (r16(RAM_Camera_max_X_pos) != 0x2B20) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
        } else if (!(OB8(o, OBJ_render) & 0x80)) {
            if (parts) o_delete(parts);
            o_delete(o);
            return;
        }
        bfw_move();
        cz_hover_hits(o);
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        cz_align(o);
        bfw_animate(o, parts, ani, CZ_ROUTINE);
        goto draw;
    }
    cz_hover_hits(o);                                   /* loc_31C08 */
    cz_align(o);
    cz_face(o);
    bfw_animate(o, parts, ani, CZ_ROUTINE);
draw:
    if (bfw.collision) {                                /* BossCollision_CNZ: the arc */
        if (bfw.collision == 1)
            bfw_hurt_box(o, (int16_t)OB16(o, OBJ_x_pos), (int16_t)(OB16(o, OBJ_y_pos) + 0x28), 0x10, 8);
        else
            bfw_hurt_box(o, (int16_t)(OB16(o, OBJ_x_pos) + 4), (int16_t)(OB16(o, OBJ_y_pos) + 0x20), 0x20, 8);
    }
    if (parts) {
        SET8(parts, OBJ_render, (OB8(parts, OBJ_render) & ~1) | (OB8(o, OBJ_render) & 1));
        SET16(parts, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(parts, OBJ_y_pos, OB16(o, OBJ_y_pos));
    }
    o_touch(o);
    o_draw(o);
}

static void cz_ball_move(uint32_t o)                /* loc_31FF8 */
{
    int32_t x = (int32_t)((uint32_t)OB16(o, OBJ_x_pos) << 16) + ((int32_t)(int16_t)OB16(o, OBJ_x_vel) << 8);
    int32_t y = (int32_t)((uint32_t)OB16(o, OBJ_y_pos) << 16) + ((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8);
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
    SET16(o, OBJ_x_pos, (uint16_t)(x >> 16));
    SET16(o, OBJ_y_pos, (uint16_t)(y >> 16));
}

static void cz_ball(uint32_t o)                     /* routine 4 (loc_31F24) */
{
    uint32_t boss = r32(o + BL_PARENT) & 0xFFFFFF;
    if (OB8(boss, CZ_ROUTINE) >= 6) { o_delete(o); return; }
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* loc_31F48 */
        o_init(o, S2A_CNZBOSS, 4, 7, 0x10, 0x10);
        SET8(o, OBJ_2nd, 2);
        SET16(o, OBJ_x_pos, OB16(boss, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(boss, OBJ_y_pos) + 0x30);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_coll_flags, 0x98);
        return;
    case 2: {                                           /* loc_31F96: grows out under the zapper */
        SET16(o, OBJ_x_pos, OB16(boss, OBJ_x_pos));
        int16_t d0 = (int16_t)OB16(o, BL_OFF);
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(boss, OBJ_y_pos) + d0));
        if (++d0 >= 0x2E) d0 = 0x2E;
        SET16(o, BL_OFF, (uint16_t)d0);
        if (!bfw.countdown) {
            SET8(o, OBJ_2nd, 4);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
        }
        break;
    }
    case 4: {                                           /* loc_31FDC: falls, splits on the floor */
        cz_ball_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + d1));
            o_sfx_s2(0xC4);                                /* SndID_BossExplosion */
            SET8(o, OBJ_anim, 7);
            SET16(o, OBJ_y_vel, (uint16_t)-0x300);
            SET16(o, OBJ_x_vel, (uint16_t)-0x100);
            SET8(o, OBJ_2nd, 6);
            SET8(o, OBJ_coll_flags, 0x98);
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                for (uint32_t k = 0; k < OBJ_SIZE; k += 2) w16(a1 + k, r16(o + k));
                SET16(a1, OBJ_x_vel, 0x100);
            }
        }
        break;
    }
    default:                                            /* loc_32080: sparks */
        cz_ball_move(o);
        o_animate_s2(o, o_asset_anim(S2A_CNZBOSS));
        if (OB16(o, OBJ_y_pos) >= 0x705) { o_delete(o); return; }
        break;
    }
    o_touch(o);
    o_draw(o);
}

static void cnz_boss(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the boss's child sprites */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj51_Init */
        o_upload_asset(S2A_CNZBOSS);
        o_init(o, S2A_CNZBOSS, 4, 3, 0x20, 0x40);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_pos, 0x2A46);
        SET16(o, OBJ_y_pos, 0x654);
        SET8(o, OBJ_frame, 0);
        SET8(o, CZ_ROUTINE, 0);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        bfw.x = (int32_t)0x2A46 << 16;
        bfw.y = (int32_t)0x654 << 16;
        bfw.collision = 0;
        bfw.yv = 0;
        bfw.xv = -0x180;
        bfw.countdown = 1;
        SET8(o, CZ_DIR, 0);
        SET8(o, CZ_ZAPS, 0);
        uint32_t parts = bfw_parts_new(o, S2A_CNZBOSS, 4, 0x20, 0x40);
        SET32(o, CZ_PARTS, parts);
        if (parts) {
            static const uint8_t frames[4] = { 5, 1, 6, 2 };   /* sub2..sub5 */
            for (int k = 0; k < 4; k++) {
                bfw_child(parts, k, 0x2A46, 0x654);
                w8(bfw_child_frame(parts, k), frames[k]);
            }
        }
        static const uint8_t init_anim[10] = { 8, 0, 1, 0, 0x10, 0, 3, 0, 2, 0 };   /* loc_319D6 */
        memset(bfw.anim, 0, sizeof bfw.anim);
        memcpy(bfw.anim, init_anim, sizeof init_anim);
        return;
    }
    case 2: cz_main(o); return;
    default: cz_ball(o); return;
    }
}

/* ------------------------------------------------------------------------
 * Object 52 - HTZ boss: Eggman rises from the lava on either side of the
 * arena, flames across it, sinks and lobs lava balls. Routines (S2
 * boss_subtype): 2 the Eggmobile, 4 flamethrower, 6 lava ball, 8 smoke.
 * The nozzle flame is the parts object's child sprite (sub2).
 * ---------------------------------------------------------------------- */
#define HB_ROUTINE 0x30               /* boss_routine */
#define HB_SINE    0x31               /* boss_sine_count */
#define HB_INV     0x32               /* boss_invulnerable_time */
#define HB_SIDE    0x33               /* boss_defeated: 1 = rising on the right */
#define HB_TIMER   0x34               /* objoff_3E */
#define HB_BALL    0x35               /* objoff_38: lava ball thrown this dive */
#define HB_PARTS   0x3C
#define HB_X       0x30               /* lava ball / smoke: objoff_2A (16.16 x) */
#define HB_IDX     0x36               /* lava ball: objoff_2E */
#define HB_FIRE    0x38               /* ground fire: objoff_32 (word) */
#define HB_FIRES   0x3A               /* ground fire: objoff_36 */

static uint32_t hb_parts(uint32_t o) { return r32(o + HB_PARTS) & 0xFFFFFF; }

static void hb_hits(uint32_t o)                      /* loc_300A4 */
{
    if (OB8(o, HB_ROUTINE) >= 8) return;
    if (!OB8(o, OBJ_coll_prop)) {                       /* Obj52_Defeat */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        bfw.countdown = 0xB3;
        SET8(o, HB_ROUTINE, 8);
        o_upload_asset(S2A_CAPSULE);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, HB_INV)) {
        SET8(o, HB_INV, 32);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, HB_INV, OB8(o, HB_INV) - 1);
    if (!OB8(o, HB_INV)) SET8(o, OBJ_coll_flags, 0x16);   /* S2 $32 (32x28): S3K's nearest box */
}

static void hb_align(uint32_t o)                     /* loc_2FEDE */
{
    uint32_t parts = hb_parts(o);
    if (parts) bfw_child(parts, 0, (int16_t)OB16(o, OBJ_x_pos), (int16_t)OB16(o, OBJ_y_pos));
}

static void hb_smoke(uint32_t o)                     /* Obj52_CreateSmoke */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x52));
    SET8(a1, OBJ_routine, 8);
    o_init(a1, S2A_HTZSMOKE, 4, 1, 0x10, 0x10);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x28);
    SET32(a1, HB_X, (uint32_t)OB16(o, OBJ_x_pos) << 16);
    SET16(a1, OBJ_x_vel, (uint16_t)-0x60);
    SET16(a1, OBJ_y_vel, (uint16_t)-0xC0);
    SET8(a1, OBJ_frame, 0);
    SET8(a1, OBJ_anim_timer, 0x11);
}

static void hb_hover(uint32_t o)                     /* Obj52_Mobile_Hover */
{
    int16_t s, c;
    o_sincos(OB8(o, HB_SINE), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((c >> 7) + (int16_t)(bfw.y >> 16)));
    SET8(o, HB_SINE, OB8(o, HB_SINE) + 4);
}

static void hb_draw(uint32_t o)
{
    uint32_t parts = hb_parts(o);
    if (bfw.collision && parts) {                       /* BossCollision_HTZ: the flame */
        static const int16_t off[8] = { 0x1C, 0x20, 0x28, 0x34, 0x3C, 0x44, 0x60, 0x70 };
        static const uint8_t wid[8] = { 4, 4, 8, 0x0C, 0x14, 0x1C, 0x24, 8 };
        int d1 = (int)r8(bfw_child_frame(parts, 0)) - 2;
        if (d1 >= 0 && d1 <= 7) {
            int16_t x = (int16_t)OB16(o, OBJ_x_pos);
            x = (int16_t)(x + ((OB8(o, OBJ_render) & 1) ? off[d1] : -off[d1]));
            bfw_hurt_box(o, x, (int16_t)(OB16(o, OBJ_y_pos) - 0x1C), wid[d1], 4);
        }
    }
    if (parts) {
        SET8(parts, OBJ_render, (OB8(parts, OBJ_render) & ~1) | (OB8(o, OBJ_render) & 1));
        SET16(parts, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(parts, OBJ_y_pos, OB16(o, OBJ_y_pos));
    }
    o_touch(o);
    o_draw(o);
}

static void hb_mobile(uint32_t o)
{
    uint32_t parts = hb_parts(o);
    uint32_t ani = o_asset_anim(S2A_HTZBOSS);
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, HB_ROUTINE) == 2) SET8(o, OBJ_coll_prop, 0);   /* development */
    switch (OB8(o, HB_ROUTINE)) {
    case 0:                                             /* Obj52_Mobile_Raise */
        bfw.collision = 0;
        bfw_move();
        if ((int16_t)(bfw.y >> 16) <= (OB8(o, HB_SIDE) ? 0x4FC : 0x518)) {
            bfw.yv = 0;
            SET8(o, HB_SINE, 4);
            SET8(o, HB_ROUTINE, 2);
            SET8(o, HB_TIMER, 60);
        }
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        hb_hits(o);
        break;
    case 2:                                             /* Obj52_Mobile_Flamethrower */
        SET8(o, HB_TIMER, OB8(o, HB_TIMER) - 1);
        if ((int8_t)OB8(o, HB_TIMER) >= 0) {
            hb_hover(o);
        } else {
            bfw.collision = 1;
            if (parts) SET16(parts, MS_COUNT, 1);
            if ((int8_t)OB8(o, HB_TIMER) == -0x18) {    /* the flame shoots out */
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, S2(0x52));
                    SET8(a1, OBJ_routine, 4);
                    SET8(a1, OBJ_render, OB8(o, OBJ_render) & 1);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                    SET8(o, HB_TIMER, 0x2F);
                }
            } else {
                hb_hover(o);
            }
        }
        hb_hits(o);                                     /* loc_2FDAA */
        hb_align(o);
        bfw_animate(o, parts, ani, HB_ROUTINE);
        break;
    case 4:                                             /* Obj52_Mobile_BeginLower */
        bfw.collision = 0;
        if (parts) SET16(parts, MS_COUNT, 0);
        bfw.anim[2] = 0x10;
        bfw.anim[3] = 0;
        SET8(o, HB_TIMER, OB8(o, HB_TIMER) - 1);
        if (OB8(o, HB_TIMER)) {
            hb_hover(o);
            hb_hits(o);
            hb_align(o);
            bfw_animate(o, parts, ani, HB_ROUTINE);
            break;
        }
        bfw.yv = 0xE0;
        SET8(o, HB_ROUTINE, 6);
        hb_align(o);
        break;
    case 6: {                                           /* Obj52_Mobile_Lower */
        bfw_move();
        int16_t by = (int16_t)(bfw.y >> 16);
        if (by >= (OB8(o, HB_SIDE) ? 0x548 : 0x538) && !OB8(o, HB_BALL)) {   /* Obj52_CreateLavaBall */
            SET8(o, HB_BALL, 0xFF);
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, S2(0x52));
                SET8(a1, OBJ_routine, 6);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                o_sfx_s2(0xAE);                            /* SndID_ArrowFiring/LavaBall */
            }
        }
        int16_t bottom = OB8(o, HB_SIDE) ? 0x580 : 0x5A0;
        if ((int16_t)(bfw.y >> 16) >= bottom) {         /* under the lava: come up on Sonic's side */
            bfw.y = (int32_t)bottom << 16;
            bfw.yv = -0xE0;
            SET8(o, HB_ROUTINE, 0);
            SET8(o, HB_BALL, 0);
            if ((int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - 0x2FC0) >= 0) {
                bfw.y = (int32_t)0x580 << 16;
                SET16(o, OBJ_x_pos, 0x3040);
                SET8(o, HB_SIDE, 0xFF);
            } else {
                SET16(o, OBJ_x_pos, 0x2F40);
                bfw.y = (int32_t)0x5A0 << 16;
                SET8(o, HB_SIDE, 0);
            }
            if ((int16_t)OB16(o, OBJ_x_pos) > (int16_t)OB16(RAM_Player_1, OBJ_x_pos))
                SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
            else
                SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
        }
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        hb_hits(o);
        break;
    }
    default:                                            /* Obj52_Mobile_Defeated */
        if (parts) SET16(parts, MS_COUNT, 0);
        bfw.countdown--;
        if (bfw.countdown >= 0) {
            if (bfw.countdown <= 0x1E) {
                SET8(o, OBJ_frame, 0x10);
                bfw_explosion(o);
                if (!(r8(RAM_Level_frame_counter + 1) & 0x1F)) hb_smoke(o);
            } else {
                bfw_explosion(o);
            }
            break;
        }
        if (!(r8(RAM_Level_frame_counter + 1) & 0x1F)) hb_smoke(o);
        if (bfw.countdown > -0x3C) break;               /* Obj52_Mobile_Flee */
        if (!s2_boss_defeated_flag) { tri_music_zone(); s2_boss_defeated_flag = 1; }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 2);
        if (r16(RAM_Camera_max_X_pos) < 0x3160) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
            break;
        }
        if ((OB8(o, OBJ_render) & 0x80) &&
            (int16_t)OB16(o, OBJ_y_pos) <= (OB8(o, HB_SIDE) ? 0x588 : 0x578))
            break;
        w16(RAM_Camera_max_X_pos, 0x3160);
        if (parts) o_delete(parts);
        o_delete(o);
        return;
    }
    hb_draw(o);
}

static void hb_flamethrower(uint32_t o)              /* routine 4 */
{
    if (!OB8(o, OBJ_2nd)) {                             /* loc_2FF02 */
        uint8_t flip = OB8(o, OBJ_render) & 1;
        o_init(o, S2A_HTZFLAME, 4 | flip, 4, 0x10, 0x10);
        SET8(o, OBJ_2nd, 2);
        SET8(o, OBJ_anim, 5);
        SET8(o, OBJ_coll_flags, 0x98);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x1C);
        int16_t d0 = -0x70, d1 = -4;
        if (flip) { d0 = 0x70; d1 = 4; }
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + d0));
        SET16(o, OBJ_x_vel, (uint16_t)d1);
        return;
    }
    SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + OB16(o, OBJ_x_vel)));
    o_animate_s2(o, o_asset_anim(S2A_HTZFLAME));
    if (!r32(o)) return;
    o_touch(o);
    o_remember(o);
}

static void hb_ball_move(uint32_t o)                 /* Obj52_LavaBall_Move: 4.12 steps */
{
    int32_t x = (int32_t)r32(o + HB_X) + ((int32_t)(int16_t)OB16(o, OBJ_x_vel) << 4);
    int16_t yv = (int16_t)OB16(o, OBJ_y_vel);
    SET16(o, OBJ_y_vel, (uint16_t)(yv + 0x380));
    int32_t y = (int32_t)r32(o + OBJ_y_pos) + ((int32_t)yv << 4);
    SET32(o, HB_X, (uint32_t)x);
    SET32(o, OBJ_y_pos, (uint32_t)y);
    SET16(o, OBJ_x_pos, (uint16_t)(x >> 16));
}

static void hb_lava_ball(uint32_t o)                 /* routine 6 */
{
    if (!OB8(o, OBJ_2nd)) {                             /* loc_2FF78: this ball and one more */
        uint32_t a1 = o;
        for (int d2 = 0; d2 < 2; d2++) {
            if (d2) {
                a1 = o_alloc();
                if (!a1) return;
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
            SET32(a1, OBJ_code, S2(0x52));
            SET8(a1, OBJ_routine, 6);
            o_init(a1, S2A_HTZFLAME, 4, 3, 8, 8);
            SET8(a1, OBJ_2nd, 2);
            SET8(a1, OBJ_anim, 7);
            SET8(a1, OBJ_coll_flags, 0x8B);
            SET8(a1, HB_IDX, d2);
            SET8(a1, OBJ_y_radius, 8);
            SET8(a1, OBJ_x_radius, 8);
            SET32(a1, HB_X, (uint32_t)OB16(a1, OBJ_x_pos) << 16);
            SET16(a1, OBJ_x_vel, d2 ? 0x1C00 : (uint16_t)-0x1C00);
            SET16(a1, OBJ_y_vel, OB16(a1, OBJ_x_pos) == 0x2F40 ? (uint16_t)-0x5400 : (uint16_t)-0x6400);
        }
        return;
    }
    if (OB8(o, OBJ_2nd) == 2) {                         /* loc_30008: arc until it lands */
        hb_ball_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {                                   /* it lands: ground fire (Obj20 routine $A) */
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + d1));
            SET8(o, OBJ_2nd, 4);
            o_asset(o, S2A_HTZFIRE);
            SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
            SET8(o, OBJ_anim, 2);
            SET8(o, OBJ_frame, 0);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, HB_FIRE, 9);
            SET8(o, HB_FIRES, 3);
            o_sfx_s2(0xB3);                                /* SndID_FireBurn */
        } else {
            o_animate_s2(o, o_asset_anim(S2A_HTZFLAME));
        }
    } else {                                            /* loc_231D2: burn, spread twice more */
        SET16(o, HB_FIRE, OB16(o, HB_FIRE) - 1);
        if ((int16_t)OB16(o, HB_FIRE) < 0) {
            SET16(o, HB_FIRE, 0x7F);
            SET8(o, HB_FIRES, OB8(o, HB_FIRES) - 1);
            if ((int8_t)OB8(o, HB_FIRES) >= 0) {
                uint32_t a1 = o_alloc_after(o);
                if (a1) {
                    for (uint32_t k = 0; k < OBJ_SIZE; k += 2) w16(a1 + k, r16(o + k));
                    SET16(a1, HB_FIRE, 9);
                    SET16(a1, OBJ_anim, 0x0200);
                    int16_t d0 = (int16_t)OB16(a1, OBJ_x_vel) < 0 ? -0x0E : 0x0E;
                    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(a1, OBJ_x_pos) + d0));
                    SET16(a1, OBJ_y_pos, (uint16_t)(OB16(a1, OBJ_y_pos) + o_floor_dist(a1)));
                }
            }
        }
        o_animate_s2(o, o_asset_anim(S2A_HTZFIRE));
        if (OB8(o, OBJ_routine) != 6) { o_delete(o); return; }   /* the $FC at the script's end */
    }
    if (!r32(o)) return;
    o_touch(o);
    o_remember(o);
}

static void hb_smoke_obj(uint32_t o)                 /* routine 8 (loc_30210) */
{
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 0x11);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) == 4) { o_delete(o); return; }
    }
    int32_t x = (int32_t)r32(o + HB_X) + ((int32_t)(int16_t)OB16(o, OBJ_x_vel) << 8);
    int32_t y = (int32_t)r32(o + OBJ_y_pos) + ((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8);
    SET32(o, HB_X, (uint32_t)x);
    SET16(o, OBJ_x_pos, (uint16_t)(x >> 16));
    SET32(o, OBJ_y_pos, (uint32_t)y);
    o_draw(o);
}

static void htz_boss(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the nozzle flame (child sprite) */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj52_Init */
        o_upload_asset(S2A_HTZBOSS);
        o_upload_asset(S2A_HTZFLAME);
        o_upload_asset(S2A_HTZSMOKE);
        o_upload_asset(S2A_HTZFIRE);
        o_init(o, S2A_HTZBOSS, 4, 4, 0x48, 0x48);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_pos, 0x3040);
        SET16(o, OBJ_y_pos, 0x580);
        SET8(o, HB_SIDE, 1);
        SET8(o, OBJ_frame, 1);
        SET8(o, OBJ_coll_flags, 0x16);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        bfw.yv = -0xE0;
        bfw.xv = 0;
        bfw.x = (int32_t)0x3040 << 16;
        bfw.y = (int32_t)0x580 << 16;
        bfw.collision = 0;
        SET8(o, HB_INV, 0);
        uint32_t parts = bfw_parts_new(o, S2A_HTZBOSS, 1, 0x48, 0x48);
        SET32(o, HB_PARTS, parts);
        if (parts) {
            bfw_child(parts, 0, 0x3040, 0x580);
            w8(bfw_child_frame(parts, 0), 2);
            SET16(parts, MS_COUNT, 0);
        }
        static const uint8_t init_anim[4] = { 6, 0, 0x10, 0 };   /* loc_2FCEA */
        memset(bfw.anim, 0, sizeof bfw.anim);
        memcpy(bfw.anim, init_anim, sizeof init_anim);
        return;
    }
    case 2: hb_mobile(o); return;
    case 4: hb_flamethrower(o); return;
    case 6: hb_lava_ball(o); return;
    default: hb_smoke_obj(o); return;
    }
}

/* ------------------------------------------------------------------------
 * Object 57 - MCZ boss: the drill Eggmobile digs down through the ceiling
 * (stones and spikes fall), drills across the floor, rises again.
 * Routines (S2 boss_subtype): 2 the Eggmobile, 4 a falling stone / spike.
 * The hover jets are the main frame; sub2/sub5 the drills, sub3 the
 * vehicle, sub4 the face (the parts object's children 0-3).
 * ---------------------------------------------------------------------- */
#define MB_ROUTINE  0x30              /* boss_routine */
#define MB_SINE     0x31              /* boss_sine_count */
#define MB_INV      0x32              /* boss_invulnerable_time */
#define MB_DEFEATED 0x33              /* boss_defeated */
#define MB_HURT     0x34              /* boss_hurt_sonic */
#define MB_CAPSULE  0x35
#define MB_V5       0x36              /* obj57_sub5_y_vel */
#define MB_V2       0x38              /* obj57_sub2_y_vel */
#define MB_Y5       0x3A              /* obj57_sub5_y_pos2 (16.16) */
#define MB_Y2       0x3E              /* obj57_sub2_y_pos2 (16.16) */
#define MB_PARTS    0x42

static uint32_t mb_parts(uint32_t o) { return r32(o + MB_PARTS) & 0xFFFFFF; }

static void mb_transfer(uint32_t o)                  /* Obj57_TransferPositions / Obj57_FallApart */
{
    uint32_t parts = mb_parts(o);
    if (!parts) return;
    int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
    int flip = OB8(o, OBJ_render) & 1;
    bfw_child(parts, 1, x, y);                          /* sub3 */
    bfw_child(parts, 2, x, y);                          /* sub4 */
    uint32_t a5 = parts + MS_SUB + 3u * 6u, a2 = parts + MS_SUB;
    if (!OB8(o, MB_DEFEATED)) {
        bfw_child(parts, 3, (int16_t)(x + (flip ? 0x28 : -0x28)), y);   /* sub5 */
        bfw_child(parts, 0, x, y);                      /* sub2 */
        SET16(o, MB_Y5, (uint16_t)y);
        SET16(o, MB_Y2, (uint16_t)y);
        return;
    }
    if (bfw.countdown > 0x78) return;                   /* the drills drop off */
    w16(a5, (uint16_t)(r16(a5) - 1));
    int32_t y5 = (int32_t)r32(o + MB_Y5) + ((int32_t)(int16_t)OB16(o, MB_V5) << 8);
    SET16(o, MB_V5, OB16(o, MB_V5) + 0x38);
    SET32(o, MB_Y5, (uint32_t)y5);
    w16(a5 + 2, OB16(o, MB_Y5));
    if ((int16_t)r16(a5 + 2) >= 0x6F0) SET16(o, MB_V5, 0);
    w16(a2, (uint16_t)(r16(a2) + 1));
    int32_t y2 = (int32_t)r32(o + MB_Y2) + ((int32_t)(int16_t)OB16(o, MB_V2) << 8);
    SET16(o, MB_V2, OB16(o, MB_V2) + 0x38);
    SET32(o, MB_Y2, (uint32_t)y2);
    w16(a2 + 2, OB16(o, MB_Y2));
    if ((int16_t)r16(a2 + 2) >= 0x6F0) SET16(o, MB_V2, 0);
}

static void mb_spawn_stone_spike(void)               /* Obj57_SpawnStoneSpike */
{
    uint8_t d1 = r8(RAM_Level_frame_counter + 1) & 0x1F;   /* (S2: Vint_runcount) */
    int stone = 0;
    if (d1) {
        if (d1 & 7) return;
        stone = 1;
    }
    uint16_t x;
    do { x = (uint16_t)((o_random() & 0x1FF) + 0x20F0); } while (x > 0x2230);
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x57));
    SET8(a1, OBJ_routine, 4);
    SET16(a1, OBJ_x_pos, x);
    SET16(a1, OBJ_y_pos, 0x5F0);
    o_init(a1, S2A_MCZROCKS, 4, 3, 0x10, 0x10);
    SET8(a1, OBJ_frame, 0x0D);
    if (stone) return;
    SET8(a1, OBJ_frame, 0x14);                          /* spike */
    SET8(a1, OBJ_coll_flags, 0xB1);
}

static void mb_hits_main(uint32_t o)                 /* Obj57_HandleHits_Main */
{
    if (OB8(o, MB_ROUTINE) >= 8) return;
    if (!OB8(o, OBJ_coll_prop)) {                       /* Obj57_FinalDefeat */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        bfw.countdown = 0xB3;
        SET8(o, MB_ROUTINE, 8);
        if (!OB8(o, MB_CAPSULE)) { SET8(o, MB_CAPSULE, 1); o_upload_asset(S2A_CAPSULE); }
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, MB_INV)) {
        SET8(o, MB_INV, 0x20);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, MB_INV, OB8(o, MB_INV) - 1);
    if (!OB8(o, MB_INV)) SET8(o, OBJ_coll_flags, 0x0F);
}

static void mb_hits(uint32_t o)                      /* Obj57_HandleHits */
{
    mb_hits_main(o);
    if (OB8(o, MB_INV) == 0x1F) bfw.anim[7] = 0xC0;     /* face: hit */
}

static void mb_sinus(uint32_t o)                     /* Obj57_AddSinusOffset */
{
    int16_t s, c;
    o_sincos(OB8(o, MB_SINE), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + (int16_t)(bfw.y >> 16)));
    SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
    SET8(o, MB_SINE, OB8(o, MB_SINE) + 2);
    mb_hits_main(o);
}

static void mb_collision(uint32_t o)                 /* BossCollision_MCZ (the drills) */
{
    if (!OB8(o, OBJ_coll_flags)) return;
    SET8(o, MB_HURT, 0);
    int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
    if (bfw.collision >= 1) {                           /* drills to the side */
        bfw_hurt_box(o, (int16_t)(x + ((OB8(o, OBJ_render) & 1) ? 0x30 : -0x30)), (int16_t)(y + 4), 4, 4);
    } else {                                            /* drills up: two */
        bfw_hurt_box(o, (int16_t)(x + 0x14), (int16_t)(y - 0x20), 4, 0x10);
        bfw_hurt_box(o, (int16_t)(x - 0x14), (int16_t)(y - 0x20), 4, 0x10);
    }
    for (int n = 0; n < 2; n++)                         /* Sonic just hurt (invulnerable_time = $78) */
        if (o_player_active(o_player(n)) && r8(o_player(n) + 0x34) == 120) SET8(o, MB_HURT, 0xFF);
}

static void mb_reascend(uint32_t o)                  /* Obj57_Main_Sub6_ReAscend2 */
{
    bfw.xv = 0;
    SET8(o, MB_ROUTINE, 0);
    bfw.anim[2] = (uint8_t)((bfw.anim[2] & 0xF0) | 0x0B);
    bfw.anim[8] = (uint8_t)((bfw.anim[8] & 0xF0) | 0x0B);
    bfw.anim[1] = 0;
    bfw.anim[6] = (uint8_t)((bfw.anim[6] & 0xF0) | 0x0D);
    bfw.countdown = 0x64;
    bfw.yv = -0xC0;
}

static void mb_face_player(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
    if ((int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - (int16_t)(bfw.x >> 16)) >= 0)
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
}

static void mb_main(uint32_t o)
{
    uint32_t parts = mb_parts(o);
    uint32_t ani = o_asset_anim(S2A_MCZBOSS);
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, MB_ROUTINE) == 2) {   /* development: as if hit the last time */
        SET8(o, OBJ_coll_prop, 0);
        SET8(o, OBJ_coll_flags, 0);
    }
    int animate = 1, hits = 1;
    switch (OB8(o, MB_ROUTINE)) {
    case 0:                                             /* Sub0: rising */
        if (--bfw.countdown < 0) {
            bfw.anim[5] = 0;
            bfw_move();
            if ((int16_t)(bfw.y >> 16) <= 0x560) {
                bfw.yv = 0x100;
                uint16_t d3 = OB16(RAM_Player_1, OBJ_x_pos) < 0x2190 ? 0x2200 : 0x2120;
                bfw.x = (int32_t)d3 << 16;
                SET8(o, MB_ROUTINE, 2);
                mb_face_player(o);
            }
        }
        if (bfw.countdown == 0x28) bfw.collision = 0;
        if ((int16_t)(bfw.y >> 16) < 0x620) {
            s2_screen_shaking = 1;
            mb_spawn_stone_spike();
        }
        break;
    case 2:                                             /* Sub2: digging down, debris */
        bfw_move();
        mb_spawn_stone_spike();
        if ((int16_t)(bfw.y >> 16) >= 0x620) {
            SET8(o, MB_ROUTINE, 4);
            s2_screen_shaking = 0;
        }
        break;
    case 4:                                             /* Sub4: down to the floor */
        bfw_move();
        if ((int16_t)(bfw.y >> 16) < 0x660) break;
        bfw.y = (int32_t)0x660 << 16;
        SET8(o, MB_ROUTINE, 6);
        bfw.anim[2] = (uint8_t)((bfw.anim[2] & 0xF0) | 6);   /* drills turn sideways */
        bfw.anim[8] = (uint8_t)((bfw.anim[8] & 0xF0) | 6);
        bfw.anim[6] = (uint8_t)((bfw.anim[6] & 0xF0) | 0x0D);
        bfw.anim[5] = 0x20;                             /* light on */
        bfw.countdown = 0x64;
        bfw.anim[1] = 0x30;                             /* jets off */
        mb_face_player(o);
        bfw.xv = (OB8(o, OBJ_render) & 1) ? 0x200 : -0x200;
        bfw.yv = 0;
        break;
    case 6:                                             /* Sub6: drilling across */
        --bfw.countdown;
        if (bfw.countdown > 0x28) break;
        bfw.collision = 1;
        if (bfw.countdown >= 0) break;
        if (OB8(o, MB_HURT)) {
            SET8(o, MB_HURT, 0);
            bfw.anim[7] = 0x30;                         /* grin */
            mb_reascend(o);
            break;
        }
        bfw_move();
        if ((int16_t)(bfw.x >> 16) <= 0x2120) { bfw.x = (int32_t)0x2120 << 16; mb_reascend(o); }
        else if ((int16_t)(bfw.x >> 16) >= 0x2200) { bfw.x = (int32_t)0x2200 << 16; mb_reascend(o); }
        break;
    case 8:                                             /* Sub8: exploding */
        SET8(o, MB_DEFEATED, 0xFF);
        s2_screen_shaking = 0;
        animate = hits = 0;
        if (--bfw.countdown >= 0) {
            if (parts) w8(bfw_child_frame(parts, 2), 0x13);    /* burnt face */
            SET8(o, OBJ_frame, 7);
            bfw_explosion(o);
        } else {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            bfw.xv = bfw.yv = 0;
            SET8(o, MB_ROUTINE, 0x0A);
            if (parts) w8(bfw_child_frame(parts, 2), 0x12);
            bfw.countdown = -0x12;
        }
        break;
    case 0x0A:                                          /* SubA: sag, then rise */
        animate = hits = 0;
        ++bfw.countdown;
        if (bfw.countdown == 0) bfw.yv = 0;
        else if (bfw.countdown < 0) {
            if ((int16_t)(bfw.y >> 16) < 0x620) --bfw.countdown;
            bfw.yv += 0x10;
        } else if (bfw.countdown < 0x18) bfw.yv -= 8;
        else if (bfw.countdown == 0x18) { bfw.yv = 0; tri_music_zone(); s2_boss_defeated_flag = 1; }
        else if (bfw.countdown >= 0x20) {
            bfw.anim[7] = 0x0D;
            bfw.anim[0] = 2;
            bfw.anim[1] = 0;                            /* jets fire */
            SET8(o, MB_ROUTINE, 0x0C);
        }
        bfw_move();
        mb_sinus(o);
        break;
    default:                                            /* SubC: escape right */
        hits = 0;
        bfw.xv = 0x400;
        bfw.yv = -0x40;
        if (r16(RAM_Camera_max_X_pos) != 0x2240) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
        } else if (!(OB8(o, OBJ_render) & 0x80)) {
            if (parts) o_delete(parts);
            o_delete(o);
            return;
        }
        bfw_move();
        mb_sinus(o);
        break;
    }
    SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
    SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
    if (hits) mb_hits(o);
    if (animate) bfw_animate(o, parts, ani, MB_ROUTINE);
    mb_transfer(o);
    mb_collision(o);
    if (parts) {                                        /* the parts' own frame repeats the face (sub4) */
        uint32_t s4 = parts + MS_SUB + 2u * 6u;
        SET8(parts, OBJ_render, (OB8(parts, OBJ_render) & ~1) | (OB8(o, OBJ_render) & 1));
        SET16(parts, OBJ_x_pos, r16(s4));
        SET16(parts, OBJ_y_pos, r16(s4 + 2));
        SET8(parts, OBJ_frame, r8(s4 + 5));
    }
    o_touch(o);
    o_draw(o);
}

static void mb_falling(uint32_t o)                   /* Obj57_FallingStuff */
{
    o_fall(o);
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 0x28);     /* less gravity */
    if ((int16_t)OB16(o, OBJ_y_pos) > 0x6F0) { o_delete(o); return; }
    o_touch(o);
    o_draw(o);
}

static void mcz_boss(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the boss's child sprites */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj57_Init */
        o_upload_asset(S2A_MCZBOSS);
        o_upload_asset(S2A_MCZROCKS);
        o_init(o, S2A_MCZBOSS, 4, 3, 0x40, 0x40);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_pos, 0x21A0);
        SET16(o, OBJ_y_pos, 0x560);
        SET8(o, OBJ_frame, 5);
        SET8(o, MB_ROUTINE, 2);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        bfw.x = (int32_t)0x21A0 << 16;
        bfw.y = (int32_t)0x560 << 16;
        bfw.xv = 0;
        bfw.yv = 0xC0;
        bfw.collision = 0;
        s2_screen_shaking = 1;
        uint32_t parts = bfw_parts_new(o, S2A_MCZBOSS, 4, 0x40, 0x40);
        SET32(o, MB_PARTS, parts);
        if (parts) {
            static const uint8_t frames[4] = { 2, 1, 0x0E, 2 };   /* sub2..sub5 */
            for (int k = 0; k < 4; k++) {
                bfw_child(parts, k, (int16_t)(0x21A0 - (k == 3 ? 0x28 : 0)), 0x560);
                w8(bfw_child_frame(parts, k), frames[k]);
            }
        }
        bfw.countdown = 0x28;
        SET16(o, MB_V5, (uint16_t)-0x380);
        SET16(o, MB_V2, (uint16_t)-0x380);
        static const uint8_t init_anim[10] = { 2, 0, 3, 0, 0x10, 0, 0x0D, 0, 3, 0 };   /* Obj57_InitAnimationData */
        memset(bfw.anim, 0, sizeof bfw.anim);
        memcpy(bfw.anim, init_anim, sizeof init_anim);
        return;
    }
    case 2: mb_main(o); return;
    default: mb_falling(o); return;
    }
}

/* ------------------------------------------------------------------------
 * Object 55 - OOZ boss: Eggman surfaces from the oil (hittable), dives,
 * then either a laser shooter on a spring rises and fires three times, or
 * (if he was hit) a spiked chain sweeps the arena. Routines (S2
 * boss_subtype): 2 Eggmobile, 4 laser shooter, 6 spiked chain, 8 laser /
 * floor wave. The parts object holds the child sprites (sub2..sub9).
 * ---------------------------------------------------------------------- */
#define OB_ROUTINE  0x30              /* boss_routine */
#define OB_SINE     0x31              /* boss_sine_count */
#define OB_INV      0x32              /* boss_invulnerable_time */
#define OB_STATUS   0x33              /* Obj55_status: $80 hit, $40 diving */
#define OB_FIRE     0x34              /* Obj55_anim_frame_duration (objoff_2C) */
#define OB_SHOTS    0x35              /* Obj55_shot_count */
#define OB_LASERPOS 0x36              /* Obj55_laser_pos */
#define OB_CAPSULE  0x37
#define OB_PARTS    0x38
#define OB_MODE     0x3C              /* boss_subtype: 2 Eggmobile, 4 shooter, 6 chain */
#define LZ_PARENT   0x40              /* laser: Obj55_Wave_parent */
#define LZ_DELAY    0x44              /* wave: Obj55_Wave_delay (word) */
#define LZ_COUNT    0x46              /* wave: Obj55_Wave_count */

static uint32_t ob_parts(uint32_t o) { return r32(o + OB_PARTS) & 0xFFFFFF; }

static void ob_bounce_box(int16_t x, int16_t y, int w, int h)   /* loc_3FC7A: the shooter's head */
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || OB8(p, OBJ_routine) != 2) continue;
        int16_t pr = (int16_t)(OB8(p, OBJ_y_radius) - 3);
        int16_t px0 = (int16_t)(OB16(p, OBJ_x_pos) - 8), py0 = (int16_t)(OB16(p, OBJ_y_pos) - pr);
        if (px0 + 0x10 <= x - w || px0 >= x + w) continue;
        if (py0 + pr * 2 <= y - h || py0 >= y + h) continue;
        SET16(p, OBJ_x_vel, (uint16_t)-(int16_t)OB16(p, OBJ_x_vel));
        SET16(p, OBJ_y_vel, (uint16_t)-(int16_t)OB16(p, OBJ_y_vel));
    }
}

static void ob_collision(uint32_t o)                 /* BossCollision_OOZ */
{
    int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
    int flip = OB8(o, OBJ_render) & 1;
    if (bfw.collision == 1) {                           /* the laser shooter's head: bounce off */
        ob_bounce_box((int16_t)(x + (flip ? 0x10 : -0x10)), y, 0x0C, 8);
    } else if (bfw.collision == 2) {                    /* the spiked chain's ball */
        int16_t dx, dy;                                 /* word_3FC10 read at (frame-2)*2: a word */
        switch (OB8(o, OBJ_frame)) {                    /* stride over 2-word entries; $15 reads code */
        case 2: dx = 0x14; dy = 0; break;
        case 3: dx = 0; dy = 0x10; break;
        case 4: dx = 0x10; dy = 0x10; break;
        case 0x15: dx = 8; dy = 0x0C; break;
        default: return;
        }
        bfw_hurt_box(o, (int16_t)(x + (flip ? dx : -dx)), (int16_t)(y - dy), 8, 6);
    }
}

static void ob_set_kids(uint32_t o, int n, int frame)
{
    uint32_t parts = ob_parts(o);
    if (!parts) return;
    SET16(parts, MS_COUNT, (uint16_t)n);
    for (int k = 0; k < n; k++) w8(bfw_child_frame(parts, k), (uint8_t)frame);
}

static void ob_hits(uint32_t o)                      /* Obj55_HandleHits (Boss_HandleHits) */
{
    if (OB8(o, OB_ROUTINE) < 8) {
        if (!OB8(o, OBJ_coll_prop)) {                   /* Boss_Defeat */
            D(0) = 100;
            tri_jsr(S3K_HUD_AddToScore);
            bfw.countdown = 0xB3;
            SET8(o, OB_ROUTINE, 8);
            if (!OB8(o, OB_CAPSULE)) { SET8(o, OB_CAPSULE, 1); o_upload_asset(S2A_CAPSULE); }
        } else if (!OB8(o, OBJ_coll_flags)) {
            if (!OB8(o, OB_INV)) {
                SET8(o, OB_INV, 0x20);
                o_sfx_s2(0xAC);                            /* SndID_BossHit */
            }
            w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
            SET8(o, OB_INV, OB8(o, OB_INV) - 1);
            if (!OB8(o, OB_INV)) SET8(o, OBJ_coll_flags, 0x0F);
        }
    }
    if (OB8(o, OB_INV) == 0x1F) {
        bfw.anim[0] = (uint8_t)((bfw.anim[0] & 0xF0) | 3);   /* face: hit */
        SET8(o, OB_STATUS, OB8(o, OB_STATUS) | 0x80);
    }
    if (OB8(o, OB_ROUTINE) >= 8) SET8(o, OB_MODE, 2);   /* (fixBugs) */
}

static void ob_hover(uint32_t o)                     /* Obj55_HoverPos */
{
    int16_t s, c;
    o_sincos(OB8(o, OB_SINE), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((c >> 7) + (int16_t)(bfw.y >> 16)));
    SET8(o, OB_SINE, OB8(o, OB_SINE) + 4);
}

static void ob_face_side(uint32_t o)                 /* face right unless Sonic is left of $293A */
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
    if (OB16(RAM_Player_1, OBJ_x_pos) >= 0x293A) SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
}

static void ob_align(uint32_t o)                     /* Obj55_AlignSprites */
{
    uint32_t parts = ob_parts(o);
    if (parts) bfw_child(parts, 0, (int16_t)OB16(o, OBJ_x_pos), (int16_t)OB16(o, OBJ_y_pos));
}

static void ob_display(uint32_t o)
{
    uint32_t parts = ob_parts(o);
    if (parts) {
        SET8(parts, OBJ_render, (OB8(parts, OBJ_render) & ~1) | (OB8(o, OBJ_render) & 1));
        SET16(parts, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(parts, OBJ_y_pos, OB16(o, OBJ_y_pos));
    }
    ob_collision(o);
    if (!bfw.collision) o_touch(o);                     /* shooter / chain: no normal touch (d0 = 0) */
    o_draw(o);
}

static void ob_main(uint32_t o)                      /* Obj55_Main: the Eggmobile */
{
    uint32_t ani = o_asset_anim(S2A_OOZBOSS);
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, OB_ROUTINE) == 2) {   /* development */
        SET8(o, OBJ_coll_prop, 0);
        SET8(o, OBJ_coll_flags, 0);
    }
    switch (OB8(o, OB_ROUTINE)) {
    case 0:                                             /* Obj55_Main_Init */
        bfw.x = (int32_t)0x2940 << 16;
        SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
        if (OB16(RAM_Player_1, OBJ_x_pos) < 0x293A) SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
        SET16(o, OBJ_y_pos, 0x2D0);
        bfw.y = (int32_t)0x2D0 << 16;
        SET8(o, OBJ_frame, 8);
        ob_set_kids(o, 1, 8);
        SET8(o, OB_ROUTINE, 2);
        bfw.yv = -0x80;
        SET8(o, OBJ_coll_flags, 0x0F);
        ob_align(o);
        SET8(o, OB_SINE, 0);
        SET8(o, OB_STATUS, 0);
        bfw.anim[0] = 5; bfw.anim[1] = 0; bfw.anim[2] = 1; bfw.anim[3] = 0;
        bfw.collision = 0;
        return;
    case 2:                                             /* Obj55_Main_Surface */
        bfw_move();
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        ob_hover(o);
        if ((uint16_t)(bfw.y >> 16) >= 0x290) break;
        bfw.y = (int32_t)0x290 << 16;
        SET8(o, OB_ROUTINE, 4);
        bfw.countdown = 0xA8;
        if (OB8(o, OB_STATUS) & 0x80) break;
        bfw.anim[0] = 0x10; bfw.anim[1] = 0;
        break;
    case 4:                                             /* Obj55_Main_Wait */
        if (!(OB8(o, OB_STATUS) & 0x80)) {
            ob_hover(o);
            if (--bfw.countdown >= 0) break;
            bfw.anim[0] = 5; bfw.anim[1] = 0;
        }
        SET8(o, OB_ROUTINE, 6);
        bfw.yv = -0x40;
        break;
    case 6:                                             /* Obj55_Main_Dive */
        bfw_move();
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        if (!(OB8(o, OB_STATUS) & 0x40)) {
            if ((uint16_t)(bfw.y >> 16) >= 0x28C) break;
            bfw.y = (int32_t)0x28C << 16;
            bfw.yv = 0x80;
            SET8(o, OB_STATUS, OB8(o, OB_STATUS) | 0x40);
            break;
        }
        if ((uint16_t)(bfw.y >> 16) < 0x2D0) break;
        bfw.y = (int32_t)0x2D0 << 16;
        SET8(o, OB_ROUTINE, 0);
        SET8(o, OB_MODE, (OB8(o, OB_STATUS) & 0x80) ? 6 : 4);
        break;
    default:                                            /* Obj55_Main_Defeated */
        w16(FLASH_COLOUR, 0);
        if (--bfw.countdown >= 0) {
            if (bfw.countdown < 0x1E) SET8(o, OBJ_frame, 0x0B);
            else bfw_explosion(o);
            ob_display(o);
            return;
        }
        if (!s2_boss_defeated_flag) { tri_music_zone(); s2_boss_defeated_flag = 1; }
        if (r16(RAM_Camera_max_X_pos) < 0x2A20) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
        } else {
            w16(RAM_Camera_max_X_pos, 0x2A20);
            if (OB16(o, OBJ_y_pos) >= 0x2D0) {
                uint32_t parts = ob_parts(o);
                if (parts) o_delete(parts);
                o_delete(o);
                return;
            }
        }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);   /* Obj55_Defeated_Sink */
        ob_align(o);
        ob_display(o);
        return;
    }
    ob_hits(o);                                         /* Obj55_Main_End */
    bfw_animate(o, ob_parts(o), ani, OB_ROUTINE);
    ob_align(o);
    ob_display(o);
}

static void ob_wind(uint32_t o)                      /* Obj55_LaserShooter_Wind: the spring */
{
    int16_t d5 = (int16_t)(bfw.x >> 16), d6 = (int16_t)(bfw.y >> 16);
    uint8_t d3 = OB8(o, OB_SINE);
    int16_t s, c;
    o_sincos(d3, &s, &c);
    SET16(o, OBJ_x_pos, (uint16_t)((c >> 4) + d5));
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + d6));
    SET8(o, OB_SINE, OB8(o, OB_SINE) + 2);
    uint32_t parts = ob_parts(o);
    for (int k = 0; k < 8 && parts; k++) {
        d6 = (int16_t)(d6 + 0x0F);
        d3 = (uint8_t)(d3 - 0x10);
        o_sincos(d3, &s, &c);
        bfw_child(parts, k, (int16_t)((c >> 4) + d5), (int16_t)((s >> 6) + d6));
    }
}

static void ob_shooter(uint32_t o)                   /* Obj55_LaserShooter */
{
    static const uint16_t targets[4] = { 0x238, 0x230, 0x240, 0x25F };
    switch (OB8(o, OB_ROUTINE)) {
    case 0:                                             /* _Init */
        w16(FLASH_COLOUR, 0);
        bfw.x = (int32_t)0x2940 << 16;
        ob_face_side(o);
        bfw.y = (int32_t)0x2B0 << 16;
        SET16(o, OBJ_y_pos, 0x2B0);
        SET8(o, OB_ROUTINE, 2);
        SET8(o, OBJ_coll_flags, 0x8A);
        SET8(o, OBJ_frame, 5);
        ob_set_kids(o, 8, 7);
        if (ob_parts(o))
            for (int k = 0; k < 8; k++) w16(ob_parts(o) + MS_SUB + (uint32_t)k * 6u + 2u, (uint16_t)(0x2B0 + 0x0F * (k + 1)));
        bfw.yv = -0x80;
        SET8(o, OB_LASERPOS, 0);
        bfw.collision = 1;
        return;
    case 2:                                             /* _Rise */
        bfw_move();
        if ((uint16_t)(bfw.y >> 16) >= 0x240) break;
        bfw.y = (int32_t)0x240 << 16;
        bfw.yv = 0;
        SET8(o, OB_ROUTINE, 4);
        bfw.countdown = 0x80;
        SET8(o, OB_SHOTS, 3);
        break;
    case 4:                                             /* _ChooseTarget */
        SET8(o, OB_FIRE, OB8(o, OB_FIRE) - 1);
        if (!OB8(o, OB_FIRE)) SET8(o, OBJ_frame, 5);
        if (--bfw.countdown) break;
        SET8(o, OB_SHOTS, OB8(o, OB_SHOTS) - 1);
        if ((int8_t)OB8(o, OB_SHOTS) < 0) {             /* _DoneShooting */
            bfw.yv = 0x80;
            SET8(o, OB_ROUTINE, 8);
            break;
        }
        {
            int d0 = o_random();
            do { d0 = (d0 + 1) & 3; } while (OB8(o, OB_LASERPOS) & (1 << d0));
            SET8(o, OB_LASERPOS, OB8(o, OB_LASERPOS) | (1 << d0));
            bfw.countdown = (int16_t)targets[d0];
            SET8(o, OB_ROUTINE, 6);
            bfw.yv = (int16_t)(bfw.countdown - (int16_t)(bfw.y >> 16)) >= 0 ? 0x80 : -0x80;   /* Obj55_MoveTowardTarget */
        }
        break;
    case 6: {                                           /* _Aim */
        bfw_move();
        uint16_t d0 = (uint16_t)bfw.countdown, by = (uint16_t)(bfw.y >> 16);
        if (bfw.yv < 0 ? d0 < by : d0 >= by) break;
        bfw.yv = 0;                                     /* _Fire */
        SET8(o, OB_FIRE, 8);
        SET8(o, OBJ_frame, 6);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S2(0x55));
        SET8(a1, OBJ_routine, 8);
        SET32(a1, LZ_PARENT, RAMPTR(o));
        o_sfx_s2(0xEA);                                    /* SndID_LaserBurst */
        SET8(o, OB_ROUTINE, 4);
        bfw.countdown = 0x28;
        bfw.yv = -0x80;
        break;
    }
    default:                                            /* _Lower */
        SET8(o, OB_FIRE, OB8(o, OB_FIRE) - 1);
        if (!OB8(o, OB_FIRE)) SET8(o, OBJ_frame, 5);
        bfw_move();
        if ((uint16_t)(bfw.y >> 16) < 0x2B0) break;
        bfw.y = (int32_t)0x2B0 << 16;
        bfw.yv = 0;
        SET8(o, OB_ROUTINE, 0);
        SET8(o, OB_MODE, 2);
        return;
    }
    {                                                   /* _FacePlayer (8px margin) */
        int16_t d0 = (int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (d0 < 0) { if (d0 + 8 <= 0) SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1); }
        else if (d0 - 8 >= 0) SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
    }
    ob_wind(o);
    ob_display(o);
}

static void ob_chain(uint32_t o)                     /* Obj55_SpikeChain */
{
    if (!OB8(o, OB_ROUTINE)) {                          /* _Init */
        w16(FLASH_COLOUR, 0);
        bfw.x = (int32_t)0x28C0 << 16;
        SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
        if (OB16(RAM_Player_1, OBJ_x_pos) >= 0x293A) {
            bfw.x = (int32_t)0x29C0 << 16;
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
        }
        bfw.y = (int32_t)0x2A0 << 16;
        SET8(o, OBJ_frame, 2);
        SET8(o, OBJ_coll_flags, 0x8A);
        SET8(o, OB_ROUTINE, 2);
        SET8(o, OBJ_width_px, 0x80);
        SET8(o, OB_SINE, 0);
        ob_set_kids(o, 8, 7);
        bfw.collision = 2;
        return;
    }
    uint8_t d3 = (uint8_t)(OB8(o, OB_SINE) + 0x40);     /* _Move */
    int flip = OB8(o, OBJ_render) & 1;
    int16_t bx = (int16_t)(bfw.x >> 16), by = (int16_t)(bfw.y >> 16);
    int16_t s, c;
    o_sincos(d3, &s, &c);
    int16_t x = (int16_t)(((int32_t)c * 0x68) >> 8), y = (int16_t)(((int32_t)s * 0x68) >> 8);
    SET16(o, OBJ_x_pos, (uint16_t)((flip ? x : -x) + bx));
    SET16(o, OBJ_y_pos, (uint16_t)(y + by));
    SET8(o, OB_SINE, OB8(o, OB_SINE) + 1);
    uint32_t parts = ob_parts(o);
    for (int k = 0; k < 8 && parts; k++) {
        d3 = (uint8_t)(d3 - 6);
        o_sincos(d3, &s, &c);
        x = (int16_t)(((int32_t)c * 0x68) >> 8);
        y = (int16_t)(((int32_t)s * 0x68) >> 8);
        bfw_child(parts, k, (int16_t)((flip ? x : -x) + bx), (int16_t)(y + by));
    }
    if (OB8(o, OB_SINE) >= 0xFE) {
        SET8(o, OB_ROUTINE, 0);
        SET8(o, OB_MODE, 4);
        return;
    }
    uint8_t a = OB8(o, OB_SINE);                        /* _SetAnimFrame */
    SET8(o, OBJ_frame, a < 0x52 ? 0x15 : a < 0x6B ? 3 : a < 0x92 ? 2 : 4);
    ob_display(o);
}

static void ob_laser(uint32_t o)                     /* Obj55_Laser (routine 8): laser, floor wave */
{
    switch (OB8(o, OBJ_2nd)) {
    case 0: {                                           /* _Init */
        SET8(o, OBJ_2nd, 2);
        o_init(o, S2A_OOZBOSS, 4, 4, 0x10, 0x10);
        uint32_t a1 = r32(o + LZ_PARENT) & 0xFFFFFF;
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        SET8(o, OBJ_frame, 0x0C);
        int right = OB8(a1, OBJ_render) & 1;
        SET16(o, OBJ_x_vel, right ? 0x400 : (uint16_t)-0x400);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + (right ? 0x20 : -0x20));
        SET8(o, OBJ_coll_flags, 0xAF);
        return;
    }
    case 2: {                                           /* _Main */
        if (OB16(o, OBJ_y_pos) >= 0x250) {              /* _ChkGround: a wave on the floor */
            uint16_t x = OB16(o, OBJ_x_pos);
            int16_t start = 0;
            if ((int16_t)OB16(o, OBJ_x_vel) >= 0) { if (x < 0x2980 && x >= 0x297C) start = 0x2988; }
            else if (x >= 0x2900 && x < 0x2904) start = 0x28F8;
            if (start) {
                uint32_t a1 = o_alloc();                /* _CreateWave */
                if (a1) {
                    SET32(a1, OBJ_code, S2(0x55));
                    SET8(a1, OBJ_routine, 8);
                    SET8(a1, OBJ_2nd, 4);
                    o_init(a1, S2A_OOZBOSS, 4, 2, 0x10, 0x10);
                    SET8(a1, OBJ_coll_flags, 0x8B);
                    SET8(a1, OBJ_anim, 2);
                    SET8(a1, OBJ_frame, 0x0D);
                    SET16(a1, LZ_DELAY, 5);
                    SET8(a1, LZ_COUNT, 7);
                    SET16(a1, OBJ_x_vel, OB16(o, OBJ_x_vel));
                    SET16(a1, OBJ_x_pos, (uint16_t)start);
                    SET16(a1, OBJ_y_pos, 0x250);
                    o_sfx_s2(0xEB);                        /* SndID_LaserFloor/Scatter */
                }
            }
        }
        o_move(o);
        if (OB16(o, OBJ_x_pos) < 0x2870 || OB16(o, OBJ_x_pos) >= 0x2A10) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    case 4:                                             /* Obj55_Wave */
        SET16(o, LZ_DELAY, OB16(o, LZ_DELAY) - 1);
        if ((int16_t)OB16(o, LZ_DELAY) < 0) {
            SET16(o, LZ_DELAY, 0xC7);
            SET8(o, LZ_COUNT, OB8(o, LZ_COUNT) - 1);
            if ((int8_t)OB8(o, LZ_COUNT) >= 0) {
                uint32_t a1 = o_alloc_after(o);
                if (a1) {
                    for (uint32_t k = 0; k < OBJ_SIZE; k += 2) w16(a1 + k, r16(o + k));
                    SET16(a1, OBJ_respawn, 0);
                    SET16(a1, LZ_DELAY, 5);
                    SET8(a1, OBJ_anim, 2);
                    SET8(a1, 0x21, 0);                  /* prev_anim */
                    SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + ((int16_t)OB16(a1, OBJ_x_vel) < 0 ? -0x10 : 0x10));
                    o_sfx_s2(0xEB); /* SndID_LaserFloor/Scatter */
                }
            }
        }
        o_animate_s2(o, o_asset_anim(S2A_OOZBOSS));     /* $FA ends it: routine 6 deletes next frame */
        o_touch(o);
        o_remember(o);
        return;
    default:
        o_delete(o);
        return;
    }
}

static void ooz_boss(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the boss's child sprites */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj55_Init */
        o_upload_asset(S2A_OOZBOSS);
        o_init(o, S2A_OOZBOSS, 4, 3, 0x40, 0x40);
        SET8(o, OBJ_routine, 2);
        SET8(o, OB_MODE, 2);
        SET8(o, OB_ROUTINE, 0);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        uint32_t parts = bfw_parts_new(o, S2A_OOZBOSS, 0, 0x40, 0x40);
        SET32(o, OB_PARTS, parts);
        memset(bfw.anim, 0, sizeof bfw.anim);
        bfw.xv = bfw.yv = 0;
        return;
    }
    case 2:
        switch (OB8(o, OB_MODE)) {
        case 4: ob_shooter(o); return;
        case 6: ob_chain(o); return;
        default: ob_main(o); return;
        }
    default:
        ob_laser(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 54 - MTZ boss: the Eggmobile drifts across the arena ringed by
 * seven orbs (Object 53); every hit throws one off to bounce around, and
 * with none left he swoops down firing lasers. Routines (S2 boss_subtype):
 * 2 the Eggmobile, 4 a laser, 6 the laser shooter riding on top.
 * ---------------------------------------------------------------------- */
#define MZ_ROUTINE  0x30              /* boss_routine */
#define MZ_SINE     0x31              /* boss_sine_count */
#define MZ_INV      0x32              /* boss_invulnerable_time */
#define MZ_FLAGS    0x33              /* objoff_2B: $80 heading right, $40 second pass */
#define MZ_OUT      0x34              /* objoff_2C: orbs bouncing about */
#define MZ_SHOTS    0x35              /* objoff_2D */
#define MZ_SUB      0x36              /* objoff_2E */
#define MZ_DELAY    0x37              /* objoff_2F */
#define MZ_RX       0x38              /* objoff_33: orbit radius */
#define MZ_REL      0x39              /* objoff_38: release an orb */
#define MZ_RY       0x3A              /* objoff_39: orbit height */
#define MZ_MODE     0x3B              /* objoff_3A: orbit squash ($FF in, $80 out) */
#define MZ_LEFT     0x3C              /* objoff_3E: orbs still orbiting */
#define MZ_CAPSULE  0x3D
#define MZ_PARTS    0x40
#define MZ_PARENT   0x44              /* laser / shooter: objoff_34 */

static uint32_t mz_parts(uint32_t o) { return r32(o + MZ_PARTS) & 0xFFFFFF; }

static void mz_align(uint32_t o)                     /* Obj54_AlignSprites */
{
    uint32_t parts = mz_parts(o);
    if (!parts) return;
    bfw_child(parts, 0, (int16_t)OB16(o, OBJ_x_pos), (int16_t)OB16(o, OBJ_y_pos));
    bfw_child(parts, 1, (int16_t)OB16(o, OBJ_x_pos), (int16_t)OB16(o, OBJ_y_pos));
    SET8(parts, OBJ_render, (OB8(parts, OBJ_render) & ~1) | (OB8(o, OBJ_render) & 1));
    SET16(parts, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(parts, OBJ_y_pos, OB16(o, OBJ_y_pos));
}

static void mz_check_hit(uint32_t o)                 /* Obj54_CheckHit */
{
    if (OB8(o, MZ_ROUTINE) >= 0x10) return;
    if (!OB8(o, OBJ_coll_prop)) {                       /* Obj54_Defeated */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        bfw.countdown = 0xEF;
        SET8(o, MZ_ROUTINE, 0x10);
        if (!OB8(o, MZ_CAPSULE)) { SET8(o, MZ_CAPSULE, 1); o_upload_asset(S2A_CAPSULE); }
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, MZ_INV)) {
        SET8(o, MZ_INV, 0x40);
        o_sfx_s2(0xAC);                                    /* SndID_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);
    SET8(o, MZ_INV, OB8(o, MZ_INV) - 1);
    if (!OB8(o, MZ_INV)) SET8(o, OBJ_coll_flags, 0x0F);
}

static void mz_face(uint32_t o)                      /* Obj54_AnimateFace */
{
    mz_check_hit(o);
    if (OB8(o, MZ_INV) == 0x3F) {                       /* just hit: an orb flies off */
        SET8(o, MZ_REL, 0xFF);
        bfw.anim[2] = (uint8_t)((bfw.anim[2] & 0xF0) | 5);
        if (OB8(o, MZ_LEFT)) {
            SET8(o, MZ_ROUTINE, 0x0A);
            bfw.yv = -0x180;
            SET8(o, MZ_LEFT, OB8(o, MZ_LEFT) - 1);
        }
        bfw.xv = 0;
        return;
    }
    if (OB8(RAM_Player_1, OBJ_routine) == 4 || OB8(RAM_Player_2, OBJ_routine) == 4)
        if ((bfw.anim[2] & 0x0F) != 4) bfw.anim[2] = (uint8_t)((bfw.anim[2] & 0xF0) | 4);
}

static void mz_fire(uint32_t o)                      /* Obj54_FireLaser */
{
    if (--bfw.countdown) return;
    if (!OB8(o, MZ_SHOTS)) return;
    SET8(o, MZ_SHOTS, OB8(o, MZ_SHOTS) - 1);
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x54));
    SET8(a1, OBJ_routine, 4);
    SET32(a1, MZ_PARENT, RAMPTR(o));
    bfw.countdown = 0x1E;
    SET8(o, MZ_DELAY, 0x10);
}

static void mz_turn(uint32_t o, int right)
{
    SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) ^ 0x80);
    bfw.xv = right ? 0x100 : -0x100;
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (right ? 1 : 0));
    uint8_t was = OB8(o, MZ_FLAGS) & 0x40;
    SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) | 0x40);
    if (was) { SET8(o, MZ_ROUTINE, 4); bfw.yv = -0x100; }
}

static void mz_show(uint32_t o, uint32_t ani, int face)   /* Obj54_MoveAndShow / Obj54_Display */
{
    if (face) mz_face(o);
    bfw_animate(o, mz_parts(o), ani, MZ_ROUTINE);
    mz_align(o);
    o_touch(o);
    o_draw(o);
}

static void mz_float(uint32_t o)                     /* x = Boss_X; Obj54_Float */
{
    SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
    int16_t s, c;
    o_sincos(OB8(o, MZ_SINE), &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + (int16_t)(bfw.y >> 16)));
    SET8(o, MZ_SINE, OB8(o, MZ_SINE) + 4);
}

static void mz_main(uint32_t o)
{
    uint32_t ani = o_asset_anim(S2A_MTZBOSS);
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, MZ_ROUTINE) == 2) {   /* development */
        SET8(o, OBJ_coll_prop, 0);
        SET8(o, OBJ_coll_flags, 0);
    }
    uint16_t bx = (uint16_t)(bfw.x >> 16), by = (uint16_t)(bfw.y >> 16);
    switch (OB8(o, MZ_ROUTINE)) {
    case 0:                                             /* Sub0: down into the arena */
        bfw_move();
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        if ((uint16_t)(bfw.y >> 16) >= 0x4A0) {
            SET8(o, MZ_ROUTINE, 2);
            bfw.yv = 0;
            bfw.xv = -0x100;
            SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) & ~0x80);
            SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
            if (OB16(RAM_Player_1, OBJ_x_pos) >= (uint16_t)(bfw.x >> 16)) {
                bfw.xv = 0x100;
                SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) | 0x80);
                SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            }
        }
        mz_show(o, ani, 1);
        return;
    case 2:                                             /* Sub2: across and back */
        bfw_move();
        bx = (uint16_t)(bfw.x >> 16);
        if (!(OB8(o, MZ_FLAGS) & 0x80)) { if (bx < 0x2AD0) mz_turn(o, 1); }
        else if (bx >= 0x2BD0) mz_turn(o, 0);
        break;
    case 4:                                             /* Sub4: up to the middle */
        bfw_move();
        if ((uint16_t)(bfw.y >> 16) < 0x470) bfw.yv = 0;
        bx = (uint16_t)(bfw.x >> 16);
        if (!(OB8(o, MZ_FLAGS) & 0x80)) { if (bx < 0x2B50) bfw.xv = 0; }
        else if (bx >= 0x2B50) bfw.xv = 0;
        if (!(bfw.xv | bfw.yv)) SET8(o, MZ_ROUTINE, 6);
        break;
    case 6:                                             /* Sub6: the orbs spread out */
        if (OB8(o, MZ_RX) < 0x68) {
            SET8(o, MZ_RX, OB8(o, MZ_RX) + 1);
            SET8(o, MZ_RY, OB8(o, MZ_RY) + 1);
        } else {
            SET8(o, MZ_RY, OB8(o, MZ_RY) - 1);
            if (!OB8(o, MZ_RY)) SET8(o, MZ_ROUTINE, 8);
        }
        break;
    case 8:                                             /* Sub8: and back in */
        if (OB8(o, MZ_RX) >= 0x27) {
            SET8(o, MZ_RX, OB8(o, MZ_RX) - 1);
        } else {
            SET8(o, MZ_RY, OB8(o, MZ_RY) + 1);
            if (OB8(o, MZ_RY) >= 0x27) {
                bfw.yv = 0x100;
                SET8(o, MZ_ROUTINE, 0);
                SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) & ~0x40);
            }
        }
        break;
    case 0x0A:                                          /* SubA: hit - up, orbs pulled in */
        if (OB8(o, MZ_RY)) SET8(o, MZ_RY, OB8(o, MZ_RY) - 1);
        else SET8(o, MZ_MODE, 0xFF);
        if (OB8(o, MZ_RX) >= 0x27) SET8(o, MZ_RX, OB8(o, MZ_RX) - 1);
        bfw_move();
        if ((uint16_t)(bfw.y >> 16) < 0x420) bfw.yv = 0;
        if (OB8(o, MZ_OUT)) break;
        if (OB8(o, MZ_MODE)) SET8(o, MZ_MODE, 0x80);
        SET8(o, MZ_ROUTINE, 0x0C);
        break;
    case 0x0C:                                          /* SubC */
        if (OB8(o, MZ_LEFT)) {
            if (OB8(o, MZ_MODE)) break;
            if (OB8(o, MZ_RY) < 0x27) { SET8(o, MZ_RY, OB8(o, MZ_RY) + 1); break; }
            bfw.yv = 0x100;
            SET8(o, MZ_ROUTINE, 0);
            SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) & ~0x40);
            break;
        }
        bfw.yv = -0x180;                                /* no orbs left: the laser runs */
        bfw.xv = -0x100;
        SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
        if (OB8(o, MZ_FLAGS) & 0x80) { bfw.xv = 0x100; SET8(o, OBJ_render, OB8(o, OBJ_render) | 1); }
        SET8(o, MZ_ROUTINE, 0x0E);
        SET8(o, MZ_SUB, 0);
        SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) & ~0x40);
        SET8(o, MZ_DELAY, 0);
        break;
    case 0x0E:                                          /* SubE: swoop and fire */
        if (OB8(o, MZ_DELAY)) {
            SET8(o, MZ_DELAY, OB8(o, MZ_DELAY) - 1);
            mz_show(o, ani, 1);
            return;
        }
        switch (OB8(o, MZ_SUB)) {
        case 0:                                         /* loc_32650 */
            bfw_move();
            if ((uint16_t)(bfw.y >> 16) < 0x420) bfw.yv = 0;
            bx = (uint16_t)(bfw.x >> 16);
            if (!(OB8(o, MZ_FLAGS) & 0x80)) {
                if (bx >= 0x2AF0) break;
                SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            } else {
                if (bx < 0x2BB0) break;
                SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
            }
            SET8(o, MZ_SUB, 2);
            bfw.yv = 0x180;
            SET8(o, MZ_SHOTS, 3);
            bfw.countdown = 0x1E;
            break;
        case 2:                                         /* loc_326B8 */
            bfw_move();
            if ((uint16_t)(bfw.y >> 16) >= 0x4A0) {
                bfw.yv = -0x180;
                SET8(o, MZ_SUB, 4);
                SET8(o, MZ_FLAGS, OB8(o, MZ_FLAGS) ^ 0x80);
            } else {
                bx = (uint16_t)(bfw.x >> 16);
                if (!(OB8(o, MZ_FLAGS) & 0x80)) { if (bx < 0x2AD0) bfw.xv = 0; }
                else if (bx >= 0x2BD0) bfw.xv = 0;
            }
            mz_fire(o);
            break;
        default:                                        /* loc_32704 */
            bfw_move();
            if ((uint16_t)(bfw.y >> 16) < 0x470) bfw.xv = (OB8(o, MZ_FLAGS) & 0x80) ? 0x100 : -0x100;
            if ((uint16_t)(bfw.y >> 16) < 0x420) { bfw.yv = 0; SET8(o, MZ_SUB, 0); }
            mz_fire(o);
            break;
        }
        break;
    case 0x10:                                          /* Sub10: exploding */
        --bfw.countdown;
        if ((uint16_t)bfw.countdown >= 60) {
            if (bfw.countdown >= 0) {
                bfw_explosion(o);
                bfw.anim[2] = 7;
            } else {
                SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
                bfw.xv = bfw.yv = 0;
                SET8(o, MZ_ROUTINE, 0x12);
                bfw.countdown = -0x12;
                bfw.anim[2] = 3;
                tri_music_zone();
            }
        }
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        mz_show(o, ani, 0);
        return;
    default: {                                          /* Sub12: off to the right */
        uint32_t parts = mz_parts(o);
        bfw.xv = 0x400;
        bfw.yv = -0x40;
        if (r16(RAM_Camera_max_X_pos) < 0x2BF0) {
            w16(RAM_Camera_max_X_pos, r16(RAM_Camera_max_X_pos) + 2);
        } else if (!(OB8(o, OBJ_render) & 0x80)) {
            if (parts) o_delete(parts);
            o_delete(o);
            return;
        }
        if (!s2_boss_defeated_flag) s2_boss_defeated_flag = 1;
        bfw_move();
        SET8(o, MZ_SINE, OB8(o, MZ_SINE) + 2);
        SET16(o, OBJ_y_pos, (uint16_t)(bfw.y >> 16));
        SET16(o, OBJ_x_pos, (uint16_t)(bfw.x >> 16));
        mz_show(o, ani, 0);
        return;
    }
    }
    (void)bx; (void)by;
    mz_float(o);                                        /* Obj54_MoveAndShow */
    mz_show(o, ani, 1);
}

static void mz_laser(uint32_t o)                     /* Obj54_Laser */
{
    if (!OB8(o, OBJ_2nd)) {
        SET8(o, OBJ_2nd, 2);
        o_init(o, S2A_MTZBOSS, 4, 5, 0x50, 0x10);
        SET8(o, OBJ_frame, 0x12);
        uint32_t a1 = r32(o + MZ_PARENT) & 0xFFFFFF;
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 4);
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 7);
        int16_t v = -0x400;
        if (OB8(a1, OBJ_render) & 1) { v = 0x400; SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 8); }
        SET16(o, OBJ_x_vel, (uint16_t)v);
        SET8(o, OBJ_coll_flags, 0x99);
        o_sfx_s2(0xEA);                                    /* SndID_LaserBurst */
    }
    o_move(o);
    if (OB16(o, OBJ_x_pos) < 0x2AB0 || OB16(o, OBJ_x_pos) >= 0x2BF0) { o_delete(o); return; }
    o_touch(o);
    o_draw(o);
}

static void mz_shooter(uint32_t o)                   /* Obj54_LaserShooter: rides on top */
{
    uint32_t a1 = r32(o + MZ_PARENT) & 0xFFFFFF;
    if (r32(a1) != S2(0x54)) { o_delete(o); return; }
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (OB8(a1, OBJ_render) & 1));
    o_draw(o);
}

static void mtz_boss(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the boss's child sprites */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj54_Init */
        o_upload_asset(S2A_MTZBOSS);
        o_init(o, S2A_MTZBOSS, 4, 3, 0x20, 0x20);
        SET16(o, OBJ_x_pos, 0x2B50);
        SET16(o, OBJ_y_pos, 0x380);
        SET8(o, OBJ_frame, 2);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        SET8(o, MZ_LEFT, 7);
        bfw.x = (int32_t)0x2B50 << 16;
        bfw.y = (int32_t)0x380 << 16;
        bfw.xv = 0;
        bfw.yv = 0x100;
        SET8(o, MZ_FLAGS, 0);
        SET8(o, MZ_OUT, 0);
        SET8(o, MZ_SINE, 0x40);
        SET8(o, MZ_RX, 0x27);
        SET8(o, MZ_RY, 0x27);
        uint32_t parts = bfw_parts_new(o, S2A_MTZBOSS, 2, 0x20, 0x20);
        SET32(o, MZ_PARTS, parts);
        if (parts) {
            w8(bfw_child_frame(parts, 0), 0x0C);
            w8(bfw_child_frame(parts, 1), 0);
        }
        mz_align(o);
        uint32_t a1 = o_alloc();                        /* the laser shooter */
        if (a1) {
            SET32(a1, OBJ_code, S2(0x54));
            SET8(a1, OBJ_routine, 6);
            o_init(a1, S2A_MTZBOSS, 4, 6, 0x20, 0x20);
            SET8(a1, OBJ_frame, 0x13);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET32(a1, MZ_PARENT, RAMPTR(o));
            a1 = o_alloc();                             /* the orbs */
            if (a1) {
                SET32(a1, OBJ_code, S2(0x53));
                SET32(a1, 0x3C, RAMPTR(o));
            }
        }
        static const uint8_t init_anim[6] = { 0x10, 0, 3, 0, 1, 0 };
        memset(bfw.anim, 0, sizeof bfw.anim);
        memcpy(bfw.anim, init_anim, sizeof init_anim);
        return;
    }
    case 2: mz_main(o); return;
    case 4: mz_laser(o); return;
    default: mz_shooter(o); return;
    }
}

/* ------------------------------------------------------------------------
 * Object 53 - MTZ boss orbs: orbit the Eggmobile (in front / behind), then
 * break away and bounce about until smashed
 * ---------------------------------------------------------------------- */
#define OR_A      0x30                /* objoff_28: orbit angle */
#define OR_B      0x31                /* objoff_29: orbit tilt ($40) */
#define OR_Y      0x32                /* objoff_2A: the boss's y - 4 */
#define OR_BC     0x34                /* objoff_2C: bounce phase */
#define OR_Y0     0x36                /* objoff_2E: the floor */
#define OR_DEPTH  0x38                /* objoff_30 (word) */
#define OR_T      0x3A                /* objoff_32 */
#define OR_PARENT 0x3C                /* objoff_34 (long) */
#define OR_X      0x40                /* objoff_38: the boss's x */
#define OR_P      0x42                /* objoff_3A */
#define OR_PH     0x43                /* objoff_3B: vertical phase */
#define OR_Q      0x44                /* objoff_3C: squashed phase */

static void orb_face_leader(uint32_t o)              /* Obj53_FaceLeader */
{
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) |
                        ((int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos)) >= 0 ? 1 : 0));
}

static void orb_orbit(uint32_t o, uint32_t a1)       /* Obj53_OrbitBoss */
{
    int16_t s, c;
    o_sincos(OB8(o, OR_B), &s, &c);
    int16_t d3 = s;
    int16_t d1 = OB8(a1, MZ_RX);
    int16_t d5 = (int16_t)(s * d1), d4 = d5;
    int16_t d2 = OB8(a1, MZ_RY);
    if (OB8(a1, MZ_MODE)) d2 = 0x10;
    d2 = (int16_t)(d2 * d3);
    o_sincos(OB8(o, OR_A), &s, &c);
    SET16(o, OBJ_x_pos, (uint16_t)((((int32_t)d5 * s) >> 16) + (int16_t)OB16(o, OR_X)));
    SET16(o, OR_DEPTH, (uint16_t)(((int32_t)d4 * c) >> 16));
    uint8_t ph = OB8(a1, MZ_MODE) ? OB8(o, OR_Q) : OB8(o, OR_PH);
    o_sincos(ph, &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((((int32_t)d2 * s) >> 16) + (int16_t)OB16(o, OR_Y)));
    SET8(o, OR_A, OB8(o, OR_A) + 4);
    uint8_t m = OB8(a1, MZ_MODE);
    if (!m) { SET8(o, OR_PH, OB8(o, OR_PH) + 8); return; }
    if (m == 0xFF) { if (OB8(o, OR_Q) < 0x40) SET8(o, OR_Q, OB8(o, OR_Q) + 2); return; }
    if (m == 0x80) {
        SET8(o, OR_Q, OB8(o, OR_Q) - 2);
        if ((int8_t)OB8(o, OR_Q) >= 0) return;
        SET8(o, OR_Q, 0);
    }
    SET8(a1, MZ_MODE, 0);
}

static void orb_priority(uint32_t o)                 /* Obj53_SetAnimPriority */
{
    int16_t d = (int16_t)OB16(o, OR_DEPTH);
    int f, p;
    if (d >= 0) { if (d >= 0x0C) { f = 3; p = 1; } else { f = 4; p = 2; } }
    else { if (d >= -0x0C) { f = 4; p = 6; } else { f = 5; p = 7; } }
    SET8(o, OBJ_frame, f);
    SET16(o, OBJ_priority, (uint16_t)(p << 7));
}

static int orb_burst_check(uint32_t o)               /* collision_property <= -2: burst */
{
    if ((int8_t)OB8(o, OBJ_coll_prop) > -2) return 0;
    SET8(o, OBJ_frame, 0x14);
    SET8(o, OBJ_anim, 6);
    return 1;
}

static void orb_arm(uint32_t o)                      /* objoff_32 counts down, then $DA */
{
    if ((int8_t)OB8(o, OR_T) < 0) return;
    SET8(o, OR_T, OB8(o, OR_T) - 1);
    if ((int8_t)OB8(o, OR_T) >= 0) return;
    SET8(o, OBJ_coll_flags, 0x1A);                      /* S2 $DA: Touch_Special $1A -> property -1, */
    SET8(o, OBJ_coll_prop, 0xFF);                       /* Touch_Enemy (S3K hit-count path)          */
}

static void mtz_orb(uint32_t o)
{
    uint32_t a1 = r32(o + OR_PARENT) & 0xFFFFFF;
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj53_Init: seven orbs */
        static const uint8_t ph[7] = { 0x24, 0x6C, 0xB4, 0xFC, 0x48, 0x90, 0xD8 };
        static const uint8_t pp[7] = { 0, 1, 1, 0, 1, 1, 0 };
        for (int k = 0; k < 7; k++) {
            uint32_t b = k ? o_alloc() : o;
            if (!b) break;
            SET32(b, OBJ_code, S2(0x53));
            o_init(b, S2A_MTZBOSS, 4, 3, 0x20, 0x20);
            SET32(b, OR_PARENT, RAMPTR(a1));
            SET8(b, OBJ_routine, 2);
            SET8(b, OBJ_frame, 5);
            SET8(b, OR_A, ph[k]);
            SET8(b, OR_PH, ph[k]);
            SET8(b, OR_P, pp[k]);
            SET8(b, OR_B, 0x40);
            SET8(b, OBJ_coll_flags, 0x87);
            SET8(b, OBJ_coll_prop, 2);
            SET8(b, OR_Q, 0);
        }
        return;
    }
    case 2:                                             /* Obj53_Main */
        if (r32(a1) != S2(0x54)) { o_delete(o); return; }
        SET16(o, OR_Y, OB16(a1, OBJ_y_pos) - 4);
        SET16(o, OR_X, OB16(a1, OBJ_x_pos));
        if (OB8(a1, MZ_REL)) {                          /* this one breaks away */
            SET8(a1, MZ_REL, 0);
            SET8(a1, MZ_OUT, OB8(a1, MZ_OUT) + 1);
            SET8(o, OBJ_routine, 4);
            SET8(o, OR_T, 60);
            SET8(o, OBJ_anim, 2);
            SET16(o, OBJ_y_vel, (uint16_t)-0x400);
            int16_t d1 = -0x80;
            if ((int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos)) < 0) d1 = 0x80;
            if (OB16(o, OBJ_x_pos) < 0x2AF0) d1 = 0x80;
            if (OB16(o, OBJ_x_pos) >= 0x2BB0) d1 = -0x80;
            SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (d1 >= 0 ? 1 : 0));
            SET16(o, OBJ_x_vel, (uint16_t)d1);
        } else if (OB8(o, OBJ_coll_prop) != 2) {        /* Obj53_ClearBossCollision */
            SET8(a1, OBJ_coll_flags, 0);
        }
        orb_orbit(o, a1);
        orb_priority(o);
        o_touch(o);
        o_draw(o);
        return;
    case 4:                                             /* Obj53_BreakAway */
        orb_arm(o);
        o_fall(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 0x20);
        if ((int16_t)OB16(o, OBJ_y_vel) >= 0x180) SET16(o, OBJ_y_vel, 0x180);
        if (OB16(o, OBJ_y_pos) >= 0x4AC) {
            SET16(o, OBJ_y_pos, 0x4AC);
            SET16(o, OR_Y0, 0x4AC);
            SET8(o, OR_BC, 1);
            SET8(o, OBJ_routine, 6);
            orb_face_leader(o);
        }
        if (orb_burst_check(o)) SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        o_animate_s2(o, o_asset_anim(S2A_MTZBOSS));
        o_touch(o);
        o_draw(o);
        return;
    case 6:                                             /* Obj53_BounceAround */
        orb_arm(o);
        if (OB8(RAM_Player_1, OBJ_routine) == 4 || OB8(RAM_Player_2, OBJ_routine) == 4) {
            SET8(o, OBJ_frame, 0x14);                   /* Obj53_CheckPlayerHit */
            SET8(o, OBJ_anim, 6);
        }
        if ((int8_t)OB8(o, OBJ_coll_prop) <= -2) { SET8(o, OBJ_frame, 0x14); SET8(o, OBJ_anim, 6); }
        if (OB8(o, OBJ_frame) != 0x0B) {
            if (orb_burst_check(o)) SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
            o_animate_s2(o, o_asset_anim(S2A_MTZBOSS));
            o_touch(o);
            o_draw(o);
            return;
        }
        {
            int16_t s, c;
            o_sincos(OB8(o, OR_BC), &s, &c);
            uint16_t d0 = (uint16_t)(((int16_t)-s >> 2) + OB16(o, OR_Y0));   /* neg.w; asr.w #2 */
            if (d0 >= 0x4AC) {
                SET16(o, OBJ_y_pos, 0x4AC);
                orb_face_leader(o);
                SET8(o, OR_BC, 1);
            } else {
                SET16(o, OBJ_y_pos, d0);
                SET8(o, OR_BC, OB8(o, OR_BC) + 1);
                if (OB8(o, OR_BC) & 1)
                    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_render) & 1) ? 1 : -1));
            }
        }
        o_touch(o);
        o_draw(o);
        return;
    default:                                            /* Obj53_Burst */
        o_sfx_s2(0xC4);                                    /* SndID_BossExplosion */
        if (r32(a1) == S2(0x54)) SET8(a1, MZ_OUT, OB8(a1, MZ_OUT) - 1);
        o_delete(o);
        return;
    }
}

/* ========================================================================
 * Object C5 - WFZ boss: the laser case (the "mother", the part you hit),
 * its laser walls, platform releaser and platforms, laser shooter, laser,
 * Eggman and the platform he stands on. One object id, routine by subtype.
 *
 * S3K's Touch_Enemy writes the hit boss's $25, so every part keeps its
 * secondary routine at W_2ND; S2's objoff_2A/2C/2E/2F sit at $40/$42/$44/$45.
 * The laser's S2 touch boxes ($2B-$2E) differ from S3K's: it is tested here.
 * ====================================================================== */
#define W_2ND     0x46
#define W_2A      0x40                /* objoff_2A (word) */
#define W_PARENT  0x42                /* objoff_2C */
#define W_2E      0x44                /* objoff_2E */
#define W_2F      0x45                /* objoff_2F */
#define W_COLL    0x47                /* the laser's S2 collision_flags */
#define W_LASER   0x3E                /* parent: the laser */
#define W_SHOOTER 0x3C
#define W_DEAD    0x20                /* status bits as S2's flags: p1_pushing - destroyed */
#define W_RESTORE 0x10                /* p2_standing - restore collision after a hit */
#define W_FIRED   0x08                /* p1_standing - the laser is out */
#define W_ONCE    0x40                /* p2_pushing - flash once per hit */
#define W_GO      0x80                /* misc - start the platforms / laser fired */
#define SFX_BOSSHIT 0xAC          /* SndID_BossHit */

static uint32_t w_parent(uint32_t o) { return 0xFF0000u | OB16(o, W_PARENT); }

static void w_animate(uint32_t o)                    /* Ani_objC5 ($FA bumps our W_2ND) */
{
    uint8_t keep = OB8(o, OBJ_2nd);
    SET8(o, OBJ_2nd, OB8(o, W_2ND));
    o_animate_s2(o, o_asset_anim(S2A_WFZBOSS));
    SET8(o, W_2ND, OB8(o, OBJ_2nd));
    SET8(o, OBJ_2nd, keep);
}

static uint32_t w_child(uint32_t o, int field, uint8_t sub)    /* LoadChildObject */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    if (field) SET16(o, field, (uint16_t)a1);
    SET32(a1, OBJ_code, S2(0xC5));
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, W_PARENT, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    return a1;
}

static void w_to_explosion(uint32_t o)
{
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 6);
    o_sfx_s2(0xC4); /* SndID_BossExplosion */
}

static void w_case_xspeed(uint32_t o)                /* ObjC5_CaseXSpeed */
{
    int d0;
    SET8(o, W_2ND, OB8(o, W_2ND) + 2);
    s2_orient(o, &d0, NULL, NULL, NULL);
    SET16(o, OBJ_x_vel, d0 ? 0x100 : (uint16_t)-0x100);
    SET8(o, OBJ_status, OB8(o, OBJ_status) | W_GO);
    SET16(o, W_2A, 0x70);
}

static void w_case_hits(uint32_t o)                  /* ObjC5_HandleHits */
{
    if (!OB8(o, OBJ_coll_prop)) {                       /* ObjC5_NoHitPointsLeft */
        D(0) = 100;                                     /* AddPoints: 1000 */
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, OBJ_coll_flags, 0);
        SET16(o, 0x30, 0xEF);
        SET8(o, W_2ND, 0x1E);
        SET8(o, OBJ_status, (OB8(o, OBJ_status) | W_DEAD) & ~W_ONCE);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) { o_touch(o); return; }
    if (!OB8(o, 0x30)) {
        if (!(OB8(o, OBJ_status) & W_ONCE)) return;
        SET8(o, 0x30, 0x20);
        o_sfx_s2(SFX_BOSSHIT);
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE);    /* ObjC5_FlashSetUp */
    SET8(o, 0x30, OB8(o, 0x30) - 1);
    if (OB8(o, 0x30)) return;
    if (OB8(o, OBJ_status) & W_RESTORE) {
        SET8(o, OBJ_coll_flags, 6);
        o_touch(o);
    }
}

static void w_case(uint32_t o)                       /* ObjC5_LaserCase */
{
    if (getenv("TRILOGY_BOSSKILL") && OB8(o, W_2ND) == 0x12) SET8(o, OBJ_coll_prop, 0);   /* development */
    switch (OB8(o, W_2ND)) {
    case 0x00:                                          /* _CaseBoundary */
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, 8);
        w16(RAM_Camera_max_Y_pos, 0x442);
        w16(RAM_Camera_target_max_Y_pos, 0x442);
        SET16(o, 0x34, OB16(o, OBJ_x_pos) - 0x60);
        SET16(o, 0x36, OB16(o, OBJ_x_pos) + 0x60);
        o_draw(o);
        break;
    case 0x02: {                                        /* _CaseWaitStart */
        int16_t d2;
        s2_orient(o, NULL, NULL, &d2, NULL);
        if ((uint16_t)(d2 + 0x20) < 0x40) {             /* ObjC5_CaseStart */
            uint32_t a1;
            SET8(o, W_2ND, 4);
            SET16(o, OBJ_y_vel, 0x40);
            if ((a1 = w_child(o, 0, 0x94))) {
                SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 0x88);
                SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x60);
            }
            if ((a1 = w_child(o, 0, 0x94))) {
                SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + 0x88);
                SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x60);
            }
            w_child(o, W_SHOOTER, 0x9C);
            w_child(o, 0x3A, 0x96);
            w_child(o, 0x38, 0xA0);
            SET16(o, W_2A, 0x5A);
            D(0) = 0xE1;                                /* cmd_FadeOut */
            tri_jsr(S3K_Play_Music);
        }
        o_draw(o);
        break;
    }
    case 0x04:                                          /* _CaseWaitDown */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if ((int16_t)OB16(o, W_2A) < 0) {
            SET8(o, W_2ND, 6);
            SET16(o, W_2A, 0x60);
            D(0) = MUS_BOSS;
            tri_jsr(S3K_Play_Music);
        }
        o_draw(o);
        break;
    case 0x06:                                          /* _CaseDown */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (!OB16(o, W_2A)) { SET8(o, W_2ND, 8); SET16(o, OBJ_y_vel, 0); }
        else o_move(o);
        o_draw(o);
        break;
    case 0x08:
        w_case_xspeed(o);
        o_draw(o);
        break;
    case 0x0A:                                          /* _CaseBoundaryChk */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if ((int16_t)OB16(o, W_2A) < 0) {
            SET8(o, W_2ND, 0x0C);
            SET8(o, OBJ_anim, 0);
        } else {
            uint16_t x = OB16(o, OBJ_x_pos);
            if ((int16_t)OB16(o, OBJ_x_vel) >= 0 ? x >= OB16(o, 0x36) : x < OB16(o, 0x34))
                SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
            o_move(o);
        }
        o_draw(o);
        break;
    case 0x0C: case 0x1A:                               /* _CaseAnimate */
        w_animate(o);
        o_draw(o);
        break;
    case 0x0E:                                          /* _CaseLSLoad */
        SET8(o, W_2ND, 0x10);
        SET16(o, W_2A, 0x0E);
        SET8(0xFF0000u | OB16(o, W_SHOOTER), W_2ND, 4);
        o_draw(o);
        break;
    case 0x10:                                          /* _CaseLSDown */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (!OB16(o, W_2A)) {                           /* _CaseAddCollision */
            SET8(o, W_2ND, 0x12);
            SET16(o, W_2A, 0x40);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | W_RESTORE | W_ONCE);
            SET8(o, OBJ_coll_flags, 6);
        } else {
            uint32_t a1 = 0xFF0000u | OB16(o, W_SHOOTER);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 1);
        }
        o_draw(o);
        break;
    case 0x12:                                          /* _CaseWaitLoadLaser */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if ((int16_t)OB16(o, W_2A) < 0) {
            SET8(o, W_2ND, 0x14);
            w_child(o, W_LASER, 0x9E);
        }
        o_draw(o);
        break;
    case 0x14:                                          /* _CaseWaitMove */
        if (OB8(0xFF0000u | OB16(o, W_LASER), OBJ_status) & W_GO) {
            int d0;
            SET8(o, W_2ND, 0x16);
            SET16(o, W_2A, 0x80);
            s2_orient(o, &d0, NULL, NULL, NULL);
            SET16(o, OBJ_x_vel, d0 ? 0x80 : (uint16_t)-0x80);
        }
        o_draw(o);
        break;
    case 0x16:                                          /* _CaseBoundaryLaserChk */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if ((int16_t)OB16(o, W_2A) < 0) {               /* _CaseStopLaserDelete */
            SET8(o, W_2ND, 0x18);
            SET16(o, W_2A, 0x0E);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(W_FIRED | W_RESTORE | W_ONCE));
            SET8(o, OBJ_coll_flags, 0);
            o_delete(0xFF0000u | OB16(o, W_LASER));
        } else {
            uint16_t x = OB16(o, OBJ_x_pos);
            if ((int16_t)OB16(o, OBJ_x_vel) >= 0 ? x >= OB16(o, 0x36) : x < OB16(o, 0x34))
                SET16(o, OBJ_x_vel, 0);
            o_move(o);
        }
        o_draw(o);
        break;
    case 0x18:                                          /* _CaseLSUp */
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (!OB16(o, W_2A)) {
            SET8(o, W_2ND, 0x1A);
            SET8(o, OBJ_anim, 1);
        } else {
            uint32_t a1 = 0xFF0000u | OB16(o, W_SHOOTER);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 1);
        }
        o_draw(o);
        break;
    case 0x1C:                                          /* _CaseStartOver */
        SET8(o, W_2ND, 8);
        w_case_xspeed(o);
        o_draw(o);
        break;
    default:                                            /* $1E: _CaseDefeated */
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, 0xFF);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~W_ONCE);
        SET16(o, 0x30, OB16(o, 0x30) - 1);
        if ((int16_t)OB16(o, 0x30) < 0) {               /* ObjC5_End */
            tri_music_zone();
            w16(RAM_Camera_max_Y_pos, 0x720);
            w16(RAM_Camera_target_max_Y_pos, 0x720);
            s2_boss_defeated_flag = 1;
            o_delete(o);
            return;
        }
        bfw_explosion(o);
        o_draw(o);
        break;
    }
    w_case_hits(o);
}

static void w_wall(uint32_t o)                       /* ObjC5_LaserWall */
{
    int draw = 1;
    switch (OB8(o, W_2ND)) {
    case 0:
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_frame, 0x0C);
        break;
    case 2:
        if (OB8(w_parent(o), OBJ_status) & W_DEAD) {
            SET8(o, W_2ND, 4);
            SET8(o, 0x30, 4);
            break;
        }
        SET8(o, W_2F, OB8(o, W_2F) ^ 1);                /* flickers (bchg: shown when the bit was clear) */
        draw = OB8(o, W_2F) & 1;
        break;
    default: {                                          /* _LaserWallDelete: fades out */
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) { draw = 0; break; }
        int8_t d0 = (int8_t)(OB8(o, OBJ_anim_timer) + 2);
        uint8_t d1 = OB8(o, OBJ_anim_frame);
        if (d0 < 0) {
            SET8(o, OBJ_anim_timer, d1);
            SET8(o, 0x30, OB8(o, 0x30) - 1);
            if ((int8_t)OB8(o, 0x30) < 0) {
                SET8(o, 0x30, 0x10);
                d1++;
                if (d1 >= 5) { o_delete(o); return; }
                SET8(o, OBJ_anim_frame, d1);
                SET8(o, OBJ_anim_timer, d1);
            }
        }
        SET8(o, W_2F, OB8(o, W_2F) & ~1);
        break;
    }
    }
    o_solid(o, 0x13, 0x40, 0x80, OB16(o, OBJ_x_pos));
    if (draw) o_draw(o);
}

static void w_releaser(uint32_t o)                   /* ObjC5_PlatformReleaser */
{
    uint32_t a1 = w_parent(o);
    switch (OB8(o, W_2ND)) {
    case 0:
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_frame, 5);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
        break;
    case 2:
        if (OB8(a1, OBJ_status) & W_GO) {
            SET8(o, W_2ND, 4);
            SET16(o, W_2A, 0x40);
            SET16(o, OBJ_y_vel, 0x40);
        }
        break;
    case 4:
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (!OB16(o, W_2A)) {
            SET8(o, W_2ND, 6);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, W_2A, 0x10);
        } else {
            o_move(o);
        }
        break;
    case 6:
        if (OB8(a1, OBJ_status) & W_DEAD) {
            SET8(o, W_2ND, 8);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | W_DEAD);    /* the platforms blow up */
            break;
        }
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (OB16(o, W_2A)) break;
        SET16(o, W_2A, 0x80);
        {
            uint8_t d0 = (uint8_t)(OB8(o, W_2E) + 1);
            if (d0 >= 3) d0 = 0;
            SET8(o, W_2E, d0);
            if (OB8(o, 0x30 + d0)) break;
            SET8(o, 0x30 + d0, 0xFF);
            uint32_t p = w_child(o, 0x3E, 0x98);
            if (p) SET8(p, W_2E, d0);
        }
        break;
    default:
        if (r32(a1) != S2(0xC5)) { o_delete(o); return; }
        bfw_explosion(o);
        break;
    }
    o_draw(o);
}

static void w_platform(uint32_t o)                   /* ObjC5_Platform */
{
    int explode = 0;
    switch (OB8(o, W_2ND)) {
    case 0:
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_anim, 3);
        SET8(o, OBJ_frame, 7);
        SET16(o, OBJ_y_vel, 0x100);
        SET16(o, W_2A, 0x60);
        w_child(o, 0x3C, 0x9A);                         /* its spikes */
        break;
    case 2:
        if (OB8(w_parent(o), OBJ_status) & W_DEAD) { explode = 1; break; }
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (!OB16(o, W_2A)) {
            SET8(o, W_2ND, 4);
            SET16(o, W_2A, 0x60);
            SET16(o, OBJ_x_vel, (uint16_t)-0x100);
            SET16(o, 0x34, OB16(o, OBJ_y_pos));
        }
        goto solid;
    default:
        if (OB8(w_parent(o), OBJ_status) & W_DEAD) { explode = 1; break; }
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if (!OB16(o, W_2A)) {
            SET16(o, W_2A, 0xC0);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        }
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (OB16(o, OBJ_y_pos) < OB16(o, 0x34) ? 4 : -4));
    solid: {
        uint16_t oldx = OB16(o, OBJ_x_pos);
        o_move(o);
        o_platform(o, 0x10, 8, oldx);
        break;
    }
    }
    if (explode) {                                      /* ObjC5_PlatformExplode */
        for (int n = 0; n < 2; n++) {
            uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
            if (!(OB8(o, OBJ_status) & bit)) continue;
            uint32_t p = o_player(n);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x08) | 0x02);
        }
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
        o_delete(0xFF0000u | OB16(o, 0x3C));
        w_to_explosion(o);
        return;
    }
    w_animate(o);
    o_draw(o);
}

static void w_platform_hurt(uint32_t o)              /* ObjC5_PlatformHurt */
{
    if (!OB8(o, W_2ND)) {
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_coll_flags, 0x98);
        return;
    }
    uint32_t a1 = w_parent(o);
    if (OB8(a1, OBJ_status) & W_DEAD) { o_delete(o); return; }
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x0C);
    SET8(o, OBJ_render, OB8(a1, OBJ_render));          /* (on screen with it) */
    o_touch(o);
}

static void w_shooter(uint32_t o)                    /* ObjC5_LaserShooter */
{
    uint32_t a1 = w_parent(o);
    if (OB8(a1, OBJ_status) & W_DEAD) { o_delete(o); return; }
    switch (OB8(o, W_2ND)) {
    case 0: SET8(o, W_2ND, 2); SET8(o, OBJ_frame, 4); break;
    case 2: SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos)); SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos)); break;
    default: SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos)); break;
    }
    o_draw(o);
}

static void w_laser(uint32_t o)                      /* ObjC5_Laser */
{
    static const uint8_t frames[6] = { 0x0E, 0x0F, 0x10, 0x11, 0x12, 0 };
    static const uint8_t colls[6] = { 0x86, 0xAB, 0xAC, 0xAD, 0xAE, 0 };
    static const uint8_t box_h[0x2F] = { [0x06] = 0x10, [0x2B] = 0x20, [0x2C] = 0x30, [0x2D] = 0x40, [0x2E] = 0x50 };
    uint32_t a1 = w_parent(o);
    if (OB8(a1, OBJ_status) & W_DEAD) { o_delete(o); return; }
    switch (OB8(o, W_2ND)) {
    case 0:
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_frame, 0x0D);
        SET16(o, OBJ_priority, 4 << 7);
        SET8(o, W_COLL, 0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10 - 3);
        SET8(o, OBJ_anim_frame, 0x0C);
        break;
    case 2:                                             /* _LaserFlash: charging */
        SET8(o, W_2F, OB8(o, W_2F) | 1);
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) break;
        if ((int8_t)(OB8(o, OBJ_anim_timer) + 2) < 0) {
            uint8_t d0 = (uint8_t)(OB8(o, OBJ_anim_frame) - 1);
            if (!d0) { SET8(o, W_2ND, 4); SET16(o, W_2A, 0x40); break; }
            SET8(o, OBJ_anim_frame, d0);
            SET8(o, OBJ_anim_timer, d0);
        }
        SET8(o, W_2F, OB8(o, W_2F) & ~1);
        break;
    case 4:
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if ((int16_t)OB16(o, W_2A) < 0) {
            SET8(o, W_2ND, 6);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
        }
        break;
    case 6: {                                           /* _LaserShoot */
        uint8_t d0 = (uint8_t)(OB8(o, W_2E) + 1);
        if (d0 >= 5) {
            SET8(o, W_2ND, 8);
            SET16(o, W_2A, 0x80);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | W_GO);
            SET8(a1, OBJ_status, OB8(a1, OBJ_status) | W_FIRED);
            break;
        }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
        SET8(o, W_2E, d0);
        SET8(o, OBJ_frame, frames[d0]);
        SET8(o, W_COLL, colls[d0]);
        break;
    }
    default:
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
        break;
    }
    uint8_t c = OB8(o, W_COLL);                         /* hurts like S2's touch box */
    if (c) {
        int hit = o_s1_touch(o, 0x10, box_h[c & 0x3F]);
        for (int n = 0; n < 2; n++)
            if (hit & (1 << n)) o_hurt_player(o_player(n), o);
    }
    SET8(o, W_2F, OB8(o, W_2F) ^ 1);
    if (OB8(o, W_2F) & 1) o_draw(o);                    /* (bchg: shown when the bit was clear) */
}

static void w_robotnik(uint32_t o)                   /* ObjC5_Robotnik */
{
    switch (OB8(o, W_2ND)) {
    case 0:
        SET8(o, W_2ND, 2);
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_anim, 1);
        SET16(o, OBJ_x_pos, 0x2C60);
        SET16(o, OBJ_y_pos, 0x4E6);
        w_child(o, 0x3E, 0xA2);
        break;
    case 2:
        if (OB8(w_parent(o), OBJ_status) & W_DEAD) {
            SET8(o, W_2ND, 4);
            SET16(o, W_2A, 0xC0);
            break;
        }
        o_animate_s2(o, o_asset_anim(S2A_ROBOTNIK));
        break;
    default:
        SET16(o, W_2A, OB16(o, W_2A) - 1);
        if ((int16_t)OB16(o, W_2A) < 0) {
            o_delete(0xFF0000u | OB16(o, 0x3E));
            o_delete(o);
            return;
        }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        break;
    }
    o_draw(o);
}

static void wfz_boss(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* ObjC5_Init (LoadSubObject) */
        uint8_t sub = OB8(o, OBJ_subtype);
        if (!o_asset_ready(S2A_WFZBOSS)) {              /* (normally in already: LevEvents_WFZ) */
            o_upload_asset(S2A_WFZBOSS);
            o_upload_asset(S2A_ROBOTNIK);
            o_upload_asset(S2A_ROBOPLAT);
        }
        if (sub == 0xA0)
            o_init(o, S2A_ROBOTNIK, OB8(o, OBJ_render) | 4, 5, 0x20, 0x20);
        else if (sub == 0xA2) {
            o_init(o, S2A_ROBOPLAT, OB8(o, OBJ_render) | 4, 5, 0x20, 0x10);
            SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        } else if (sub == 0x94)
            o_init(o, S2A_WFZBOSS, OB8(o, OBJ_render) | 4, 1, 8, 0x40);
        else
            o_init(o, S2A_WFZBOSS, OB8(o, OBJ_render) | 4, sub == 0x92 ? 4 : 5, sub == 0x92 ? 0x20 : 0x10, 0x20);
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_routine, (uint8_t)(sub - 0x90));
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0x02: w_case(o); return;
    case 0x04: w_wall(o); return;
    case 0x06: w_releaser(o); return;
    case 0x08: w_platform(o); return;
    case 0x0A: w_platform_hurt(o); return;
    case 0x0C: w_shooter(o); return;
    case 0x0E: w_laser(o); return;
    case 0x10: w_robotnik(o); return;
    default: {                                          /* $12: Eggman's platform */
        uint32_t a1 = w_parent(o);
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x26);
        o_draw(o);
        return;
    }
    }
}

void s2_obj_boss_register(TriObjFn *t)
{
    t[0xC5] = wfz_boss;
    t[0x56] = ehz_boss;
    t[0x5D] = cpz_boss;
    t[0x89] = arz_boss;
    t[0x51] = cnz_boss;
    t[0x52] = htz_boss;
    t[0x57] = mcz_boss;
    t[0x55] = ooz_boss;
    t[0x54] = mtz_boss;
    t[0x53] = mtz_orb;
    t[0x3E] = capsule;
}
