/*
 * s1_obj_mz.c — Sonic 1 Marble Zone objects, transcribed from the REV01
 * object code: lava ball maker/lava ball (13/14), large grassy platforms and
 * their burning grass (2F/35), glass blocks (30), chained stompers (31),
 * buttons (32), pushable blocks (33), bricks (46), lava geysers (4C/4D),
 * lava wall (4E), smashable green block (51), moving blocks (52), collapsing
 * floors (53), lava tag (54), Basaran (55), Caterkiller (78).
 *
 * S1 scratch fields that collide with S3K's object layout ($2A status,
 * $2B shield_reaction, $2C subtype, $25/$1E/$1F) are moved to $30-$46; the
 * rest keep their S1 offsets. Lava hurts through S3K's fire reaction bit, so
 * the S3 fire shield protects as it does in S3K.
 */
#include "trilogy_obj.h"

#define S1_MZ          0x22
#define SHIELD_FIRE    0x10           /* shield_reaction bit 4 */
#define OBJ_shield_reaction 0x2B

static uint16_t s_obj31ypos;          /* v_obj31ypos (shared stomper/push block) */

static int zone_act(void) { return r16(RAM_Current_zone_and_act); }
static uint8_t *trig(int n) { (void)n; return 0; }
#define TRIGGER(n)      (RAM_Level_trigger_array + (uint32_t)((n) & 0x0F))

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

/* RememberState / out_of_range.w DeleteObject for layout objects */
static int chk_del(uint32_t o, uint16_t x)
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return 1; }
    return 0;
}

/* ------------------------------------------------------------------------
 * Object 13 - lava ball maker (MZ, SLZ)
 * ---------------------------------------------------------------------- */
#define LM_RATE   0x30
#define LM_TIMER  0x31
static void lava_maker(uint32_t o)
{
    static const uint8_t rates[6] = { 30, 60, 90, 120, 150, 180 };
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, LM_RATE, rates[((OB8(o, OBJ_subtype) >> 4) & 0x0F) % 6]);
        SET8(o, LM_TIMER, OB8(o, LM_RATE));
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
    }
    SET8(o, LM_TIMER, OB8(o, LM_TIMER) - 1);
    if (!OB8(o, LM_TIMER)) {
        SET8(o, LM_TIMER, OB8(o, LM_RATE));
        if (o_visible(o)) {
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x14 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            }
        }
    }
    chk_del(o, OB16(o, OBJ_x_pos));
}

/* ------------------------------------------------------------------------
 * Object 14 - lava ball. $29 nonzero = made by the boss (drawn further back).
 * ---------------------------------------------------------------------- */
#define LB_ORIGY  0x30
#define LB_BOSS   0x29
static void lava_ball(uint32_t o)
{
    static const int16_t speeds[9] = { -0x400, -0x500, -0x600, -0x700, -0x200, 0x200, -0x200, 0x200, 0 };
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_x_radius, 8);
        o_init(o, S1A_FIRE, 4, OB8(o, LB_BOSS) ? 5 : 3, 8, 8);
        SET8(o, OBJ_coll_flags, 0x8B);
        SET8(o, OBJ_shield_reaction, SHIELD_FIRE);
        SET16(o, LB_ORIGY, OB16(o, OBJ_y_pos));
        int sub = OB8(o, OBJ_subtype) % 9;
        SET16(o, OBJ_y_vel, speeds[sub]);
        if (sub >= 6) {
            SET8(o, OBJ_width_px, 0x10);
            SET8(o, OBJ_anim, 2);
            SET16(o, OBJ_x_vel, OB16(o, OBJ_y_vel));
            SET16(o, OBJ_y_vel, 0);
        }
        o_sfx(0x70);                           /* sfx_Fireball (S3K: lava ball) */
    }
    /* fall through */
    case 2: {
        uint8_t st = OB8(o, OBJ_status);
        switch (OB8(o, OBJ_subtype)) {
        case 0: case 1: case 2: case 3:        /* fly up and fall back */
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            if (OB16(o, LB_ORIGY) < OB16(o, OBJ_y_pos))
                SET8(o, OBJ_routine, 4);
            st &= ~2;
            if (OBS16(o, OBJ_y_vel) < 0) st |= 2;
            SET8(o, OBJ_status, st);
            break;
        case 4:                                /* up until the ceiling */
            SET8(o, OBJ_status, st | 2);
            if (o_ceiling_dist(o) < 0) {
                SET8(o, OBJ_subtype, 8); SET8(o, OBJ_anim, 1); SET16(o, OBJ_y_vel, 0);
            }
            break;
        case 5:                                /* down until the floor */
            SET8(o, OBJ_status, st & ~2);
            if (o_floor_dist(o) < 0) {
                SET8(o, OBJ_subtype, 8); SET8(o, OBJ_anim, 1); SET16(o, OBJ_y_vel, 0);
            }
            break;
        case 6:                                /* left until a wall */
            SET8(o, OBJ_status, st | 1);
            if (o_wall_left(o, -8) < 0) {
                SET8(o, OBJ_subtype, 8); SET8(o, OBJ_anim, 3); SET16(o, OBJ_x_vel, 0);
            }
            break;
        case 7:                                /* right until a wall */
            SET8(o, OBJ_status, st & ~1);
            if (o_wall_right(o, 8) < 0) {
                SET8(o, OBJ_subtype, 8); SET8(o, OBJ_anim, 3); SET16(o, OBJ_x_vel, 0);
            }
            break;
        default:
            break;
        }
        o_move(o);
        o_animate(o, o_asset_anim(S1A_FIRE));
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 2F - large grass-covered platforms; Object 35 - burning grass.
 * S1 $2A/$2C (origin x/y) live at $40/$44; flames are registered in the
 * platform's list at $36 (count) + $37.. (slot indices).
 * ---------------------------------------------------------------------- */
#define LG_ORIGX  0x40
#define LG_ORIGY  0x44
#define LG_HMAP   0x30
#define LG_SINK   0x34
#define LG_FIRE   0x35
#define LG_KIDS   0x36
#define GF_PARENT 0x38
#define GF_YOFF   0x3C

static void lgrass_add_child(uint32_t parent, uint32_t child)  /* sub_B09C */
{
    uint8_t n = OB8(parent, LG_KIDS);
    if (n >= 8) return;
    SET8(parent, LG_KIDS, n + 1);
    SET8(parent, LG_KIDS + 1 + n, o_slot_index(child));
}

static void lgrass(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        static const uint8_t data[3][2] = { { 0, 0x40 }, { 1, 0x40 }, { 2, 0x20 } };
        static const int hm[3] = { TRI_BLOB_S1_MZ_PFM1, TRI_BLOB_S1_MZ_PFM3, TRI_BLOB_S1_MZ_PFM2 };
        SET8(o, OBJ_routine, 2);
        int idx = ((OB8(o, OBJ_subtype) >> 2) & 0x1C) >> 2;
        if (idx > 2) idx = 2;
        o_init(o, S1A_LGRASS, 4 | 0x10, 5, data[idx][1], 0x40);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET16(o, LG_ORIGY, OB16(o, OBJ_y_pos));
        SET16(o, LG_ORIGX, OB16(o, OBJ_x_pos));
        SET32(o, LG_HMAP, blob(hm[idx]));
        SET8(o, OBJ_frame, data[idx][0]);
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
    }
    /* LGrass_Types */
    int16_t d1 = 0;
    int moved = 1;
    switch (OB8(o, OBJ_subtype) & 7) {
    case 1: d1 = 0x20; SET16(o, 0x3E, o_osc(2)); break;
    case 2: d1 = 0x30; SET16(o, 0x3E, o_osc(6)); break;
    case 3: d1 = 0x40; SET16(o, 0x3E, o_osc(0x0A)); break;
    case 4: d1 = 0x60; SET16(o, 0x3E, o_osc(0x0E)); break;
    case 5: {
        moved = 0;
        int d0 = OB8(o, LG_SINK);
        if (!o_standing(o)) { d0 -= 2; if (d0 < 0) d0 = 0; }
        else { d0 += 4; if (d0 >= 0x40) d0 = 0x40; }
        SET8(o, LG_SINK, d0);
        int16_t s, c;
        o_sincos((uint8_t)d0, &s, &c);
        int16_t sink = (int16_t)((uint16_t)s >> 4);
        SET16(o, OBJ_y_pos, OB16(o, LG_ORIGY) + sink);
        if (OB8(o, LG_SINK) == 0x20 && !OB8(o, LG_FIRE)) {
            SET8(o, LG_FIRE, 1);
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x35 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x40);
                SET16(a1, LG_ORIGY, OB16(o, LG_ORIGY) + 8 - 3);
                SET32(a1, LG_HMAP, r32(o + LG_HMAP));
                SET16(a1, GF_PARENT, o & 0xFFFF);
                lgrass_add_child(o, a1);
            }
        }
        for (int i = 0; i < OB8(o, LG_KIDS); i++)
            SET16(o_slot(OB8(o, LG_KIDS + 1 + i)), GF_YOFF, sink);
        break;
    }
    default: moved = 0; break;
    }
    if (moved) {
        int16_t d0 = (int16_t)OB16(o, 0x3E);
        if (OB8(o, OBJ_subtype) & 8) d0 = (int16_t)(d1 - d0);
        SET16(o, OBJ_y_pos, OB16(o, LG_ORIGY) - d0);
    }
    o_solid_sloped(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_frame) == 2 ? 0x30 : 0x20,
                   OB16(o, OBJ_x_pos), r32(o + LG_HMAP));
    o_draw(o);
    /* LGrass_ChkDel */
    if (OB8(o, LG_FIRE) && !(OB8(o, OBJ_render) & 0x80)) {      /* LGrass_DelFlames */
        int n = OB8(o, LG_KIDS);
        SET8(o, LG_KIDS, 0);
        for (int i = 0; i < n; i++) {
            o_delete(o_slot(OB8(o, LG_KIDS + 1 + i)));
            SET8(o, LG_KIDS + 1 + i, 0);
        }
        SET8(o, LG_FIRE, 0);
        SET8(o, LG_SINK, 0);
        return;
    }
    chk_del(o, OB16(o, LG_ORIGX));
}

static void grass_fire(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_FIRE, 4, 1, 8, 0x10);
        SET16(o, LG_ORIGX, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_coll_flags, 0x8B);
        SET8(o, OBJ_shield_reaction, SHIELD_FIRE);
        o_sfx(0x43);                           /* sfx_Burning (S3K: fire attack) */
        if (OB8(o, OBJ_subtype)) { SET8(o, OBJ_routine, 4); goto move; }
        /* fall through */
    case 2: {                                  /* GFire_Spread */
        uint32_t hm = r32(o + LG_HMAP);
        uint16_t d1 = (uint16_t)(OB16(o, OBJ_x_pos) - OB16(o, LG_ORIGX) + 0x0C);
        int16_t d0 = (int16_t)(uint8_t)tri_rd8(hm + (d1 >> 1));
        d0 = (int16_t)(-d0 + OB16(o, LG_ORIGY));
        int16_t d2 = d0;
        SET16(o, OBJ_y_pos, d0 + OB16(o, GF_YOFF));
        if (d1 < 0x84) {
            SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + 0x10000u);
            if (d1 < 0x80 && !((r32(o + OBJ_x_pos) + 0x80000u) & 0xFFFFFu)) {
                uint32_t a1 = o_alloc_after(o);
                if (a1) {
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x35 * 4);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, LG_ORIGY, d2);
                    SET16(a1, GF_YOFF, OB16(o, GF_YOFF));
                    SET8(a1, OBJ_subtype, 1);
                    lgrass_add_child(0xFF0000u | OB16(o, GF_PARENT), a1);
                }
            }
        }
        break;
    }
    case 4:
    move:                                      /* GFire_Move */
        SET16(o, OBJ_y_pos, OB16(o, LG_ORIGY) + OB16(o, GF_YOFF));
        break;
    }
    o_animate(o, o_asset_anim(S1A_GFIRE));
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 30 - large green glass blocks
 * ---------------------------------------------------------------------- */
#define GL_ORIGY  0x30
#define GL_DIST   0x32
#define GL_PARENT 0x3C

static void glass_types(uint32_t o)
{
    int16_t d0, d1;
    uint8_t sub = OB8(o, OBJ_subtype);
    switch (sub & 7) {
    case 1:
        d0 = o_osc(0x12); d1 = 0x40;
        goto b514;
    case 2:
        d0 = o_osc(0x12); d1 = 0x40;
        d0 = (int16_t)(d1 - d0);
    b514:
        if (sub & 8) { d0 = (int16_t)(d1 - d0); d0 = (int16_t)(((uint8_t)d0 >> 1) + 0x20); }
        break;
    case 3:
        if (sub & 8) { d0 = (int16_t)(o_osc(0x12) - 0x10); break; }
        if (!(OB8(o, OBJ_status) & ST_P1_STAND)) {
            SET8(o, 0x34, OB8(o, 0x34) & ~1);
        } else if (!OB8(o, 0x34)) {
            SET8(o, 0x34, 1);
            uint8_t f = OB8(o, 0x35);
            SET8(o, 0x35, f | 1);
            if (f & 1) {
                SET8(o, 0x34, OB8(o, 0x34) | 0x80);
                SET16(o, 0x36, 0x10);
                SET8(o, 0x38, 0x0A);
                if (OB16(o, GL_DIST) == 0x40) SET16(o, 0x36, 0x40);
            }
        }
        if (OB8(o, 0x34) & 0x80) {
            int go = 1;
            if (OB8(o, 0x38)) {
                SET8(o, 0x38, OB8(o, 0x38) - 1);
                if (OB8(o, 0x38)) go = 0;
            }
            if (go) {
                if (OB16(o, GL_DIST)) {
                    SET16(o, GL_DIST, OB16(o, GL_DIST) - 1);
                    SET16(o, 0x36, OB16(o, 0x36) - 1);
                    if (OB16(o, 0x36)) go = 0;
                }
                if (go) SET8(o, 0x34, OB8(o, 0x34) & 0x7F);
            }
        }
        d0 = (int16_t)OB16(o, GL_DIST);
        break;
    case 4:
        if (sub & 8) { d0 = (int16_t)(o_osc(0x12) - 0x10); break; }
        if (!OB8(o, 0x34) && r8(TRIGGER(sub >> 4))) SET8(o, 0x34, 1);
        if (OB8(o, 0x34) && OB16(o, GL_DIST)) SET16(o, GL_DIST, OB16(o, GL_DIST) - 2);
        d0 = (int16_t)OB16(o, GL_DIST);
        break;
    default:
        return;
    }
    SET16(o, OBJ_y_pos, OB16(o, GL_ORIGY) - d0);
}

static void glass_block(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t v1[2][3] = { { 2, 0, 0 }, { 4, 0, 1 } };
        static const uint8_t v2[2][3] = { { 6, 0, 2 }, { 8, 0, 1 } };
        const uint8_t (*v)[3] = v1;
        uint8_t height = 0x48;
        if (OB8(o, OBJ_subtype) >= 3) { v = v2; height = 0x38; }
        uint32_t a1 = o;
        for (int i = 0; i < 2; i++) {
            if (i) { a1 = o_alloc_after(o); if (!a1) break; }
            SET8(a1, OBJ_routine, v[i][0]);
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x30 * 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + (int8_t)v[i][1]);
            o_init(a1, S1A_GLASS, 4, 4, 0x20, height);
            SET16(a1, OBJ_art_tile, OB16(a1, OBJ_art_tile) | 0x8000);
            SET16(a1, GL_ORIGY, OB16(a1, OBJ_y_pos));
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET8(a1, OBJ_frame, v[i][2]);
            SET32(a1, GL_PARENT, RAMPTR(o));
            if (i) {                           /* the reflection */
                SET8(a1, OBJ_width_px, 0x10);
                SET16(a1, OBJ_priority, 3 << 7);
                SET8(a1, OBJ_subtype, (OB8(a1, OBJ_subtype) + 8) & 0x0F);
            }
        }
        SET16(o, GL_DIST, 0x90);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x10);
        SET8(o, OBJ_height_px, height);
        glass_types(o);
        o_solid(o, 0x2B, height, height + 1, OB16(o, OBJ_x_pos));
        break;
    }
    case 2:
        glass_types(o);
        o_solid(o, 0x2B, 0x48, 0x49, OB16(o, OBJ_x_pos));
        break;
    case 4: {
        uint32_t p = 0xFF0000u | OB16(o, GL_PARENT + 2);
        SET16(o, GL_DIST, OB16(p, GL_DIST));
        glass_types(o);
        break;
    }
    case 6:
        glass_types(o);
        o_solid(o, 0x2B, 0x38, 0x39, OB16(o, OBJ_x_pos));
        break;
    case 8: {
        uint32_t p = 0xFF0000u | OB16(o, GL_PARENT + 2);
        SET16(o, GL_DIST, OB16(p, GL_DIST));
        SET16(o, GL_ORIGY, OB16(p, OBJ_y_pos));
        glass_types(o);
        break;
    }
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) {
        if ((OB8(o, OBJ_routine) & 2)) o_delete_respawnable(o); else o_delete(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 31 - stomping metal blocks on chains. $32 is an 8.8 drop distance
 * whose high byte is the pixel offset.
 * ---------------------------------------------------------------------- */
#define CS_ORIGY  0x30
#define CS_POS    0x32
#define CS_LEN    0x34
#define CS_SWITCH 0x3A
#define CS_PARENT 0x3C

static void cstom_restart(uint32_t o)
{
    SET16(o, OBJ_y_pos, OB16(o, CS_ORIGY) + OB8(o, CS_POS));
}

static void cstom_rise_sound(uint32_t o)
{
    if (!(o_frame_counter() & 0x0F) && (OB8(o, OBJ_render) & 0x80))
        o_sfx(0x94);                           /* sfx_ChainRise (S3K: chain tick) */
}

static void cstom_types(uint32_t o)
{
    switch (OB8(o, OBJ_subtype) & 0x0F) {
    case 0:                                    /* switch-controlled */
        if (r8(TRIGGER(OB8(o, CS_SWITCH)))) {
            if (!((int16_t)s_obj31ypos < 0 && OB8(o, CS_POS) == 0x10) && OB16(o, CS_POS)) {
                cstom_rise_sound(o);
                if (OB16(o, CS_POS) < 0x80) SET16(o, CS_POS, 0);
                else SET16(o, CS_POS, OB16(o, CS_POS) - 0x80);
            }
            SET16(o, OBJ_y_vel, 0);
        } else if (OB16(o, CS_LEN) != OB16(o, CS_POS)) {
            int16_t v = OBS16(o, OBJ_y_vel);
            SET16(o, OBJ_y_vel, v + 0x70);
            SET16(o, CS_POS, OB16(o, CS_POS) + v);
            if (!(OB16(o, CS_LEN) > OB16(o, CS_POS))) {
                SET16(o, CS_POS, OB16(o, CS_LEN));
                SET16(o, OBJ_y_vel, 0);
                if (OB8(o, OBJ_render) & 0x80) o_sfx(0x5D);   /* sfx_ChainStomp (S3K: floor thump) */
            }
        }
        break;
    case 1: case 2: case 4: case 6:            /* timed */
        if (OB16(o, 0x36)) {
            if (OB16(o, 0x38)) { SET16(o, 0x38, OB16(o, 0x38) - 1); break; }
            cstom_rise_sound(o);
            if (OB16(o, CS_POS) < 0x80) {
                SET16(o, CS_POS, 0);
                SET16(o, OBJ_y_vel, 0);
                SET16(o, 0x36, 0);
            } else SET16(o, CS_POS, OB16(o, CS_POS) - 0x80);
        } else if (OB16(o, CS_LEN) != OB16(o, CS_POS)) {
            int16_t v = OBS16(o, OBJ_y_vel);
            SET16(o, OBJ_y_vel, v + 0x70);
            SET16(o, CS_POS, OB16(o, CS_POS) + v);
            if (!(OB16(o, CS_LEN) > OB16(o, CS_POS))) {
                SET16(o, CS_POS, OB16(o, CS_LEN));
                SET16(o, OBJ_y_vel, 0);
                SET16(o, 0x36, 1);
                SET16(o, 0x38, 0x3C);
                if (OB8(o, OBJ_render) & 0x80) o_sfx(0x5D);
            }
        }
        break;
    case 3: case 5: {                          /* wait for Sonic */
        int16_t d0 = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (d0 < 0) d0 = -d0;
        if ((uint16_t)d0 < 0x90) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
        break;
    }
    }
    cstom_restart(o);
}

static void chain_stomp(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t swch[2][2] = { { 0, 0 }, { 1, 0 } };
        static const uint8_t var[4][3] = { { 2, 0, 0 }, { 4, 0x1C, 1 }, { 8, 0xCC, 3 }, { 6, 0xF0, 2 } };
        static const uint8_t lens[7] = { 0x70, 0xA0, 0x50, 0x78, 0x38, 0x58, 0xB8 };
        static const uint8_t var2[3][2] = { { 0x38, 0 }, { 0x30, 9 }, { 0x10, 0x0A } };
        int d0 = OB8(o, OBJ_subtype);
        if (d0 & 0x80) {
            const uint8_t *s = swch[(d0 & 0x7F) & 1];
            SET8(o, CS_SWITCH, s[0]);
            d0 = s[1];
            SET8(o, OBJ_subtype, d0);
        }
        d0 &= 0x0F;
        uint16_t d2 = (uint16_t)(lens[d0 % 7] << 8);
        if (d0 == 0) SET16(o, CS_POS, d2);
        uint32_t a1 = o;
        int vi = 0;
        for (int d1 = 3; d1 >= 0; d1--) {
            if (vi) { a1 = o_alloc_after(o); if (!a1) break; }
        make:
            SET8(a1, OBJ_routine, var[vi][0]);
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x31 * 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + (int8_t)var[vi][1]);
            o_init(a1, S1A_METAL, 4, 4, 0x10, 0x10);
            SET16(a1, CS_ORIGY, OB16(a1, OBJ_y_pos));
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET16(a1, CS_LEN, d2);
            SET8(a1, OBJ_frame, var[vi][2]);
            vi++;
            if (OB8(a1, OBJ_frame) == 1) {     /* spikes */
                if ((OB8(o, OBJ_subtype) & 0xF0) == 0x20) { d1--; goto make; }
                SET8(a1, OBJ_width_px, 0x38);
                SET8(a1, OBJ_coll_flags, 0x90);
            }
            SET32(a1, CS_PARENT, RAMPTR(o));
            if (vi >= 4) break;
        }
        if (a1 && a1 != o) SET16(a1, OBJ_priority, 3 << 7);
        int k = ((OB8(o, OBJ_subtype) >> 3) & 0x0E) >> 1;
        if (k > 2) k = 2;
        SET8(o, OBJ_width_px, var2[k][0]);
        SET8(o, OBJ_frame, var2[k][1]);
    }
    /* fall through */
    case 2:                                    /* CStom_Block */
        cstom_types(o);
        s_obj31ypos = OB16(o, OBJ_y_pos);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        if ((OB8(o, OBJ_status) & ST_P1_STAND) && OB8(o, CS_POS) < 0x10)
            o_kill_player(RAM_Player_1);
        break;
    case 8: {                                  /* CStom_Chain */
        uint32_t p = 0xFF0000u | OB16(o, CS_PARENT + 2);
        SET8(o, OBJ_height_px, 0x80);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x10);
        SET8(o, OBJ_frame, (OB8(p, CS_POS) >> 5) + 3);
    }
    /* fall through */
    case 4: {                                  /* CStom_Spikes */
        uint32_t p = 0xFF0000u | OB16(o, CS_PARENT + 2);
        SET16(o, OBJ_y_pos, OB16(o, CS_ORIGY) + OB8(p, CS_POS));
        o_touch(o);
        break;
    }
    case 6:                                    /* CStom_Ceiling */
        break;
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) {
        if (OB8(o, OBJ_routine) == 2) o_delete_respawnable(o); else o_delete(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 32 - buttons (MZ, SYZ, LZ, SBZ)
 * ---------------------------------------------------------------------- */
static int but_mzblock(uint32_t o)
{
    int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
    for (int s = 0; s < 110; s++) {
        uint32_t a1 = o_slot(s);
        if (!(OB8(a1, OBJ_render) & 0x80)) continue;
        if (r32(a1) != NATIVE_S1_BASE + 0x33 * 4) continue;
        int16_t dx = (int16_t)((int16_t)OB16(a1, OBJ_x_pos) - x);
        if (dx < -0x20 || dx > 0x20) continue;
        int16_t dy = (int16_t)((int16_t)OB16(a1, OBJ_y_pos) - y - 8);
        if (dy < -0x20 || dy > 0x10) continue;
        return 1;
    }
    return 0;
}

static void button(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        int mz = tri_zone() == S1_MZ;
        o_init(o, mz ? S1A_BUTTON_MZ : S1A_BUTTON, 4, 4, 0x10, 0x10);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 3);
    }
    if (OB8(o, OBJ_render) & 0x80) {
        o_solid(o, 0x1B, 5, 5, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) & ~1);
        uint8_t sub = OB8(o, OBJ_subtype);
        uint32_t a3 = TRIGGER(sub);
        int d3 = (sub & 0x40) ? 7 : 0;
        int pressed = o_standing(o) || ((sub & 0x80) && but_mzblock(o));
        if (pressed) {
            if (!r8(a3)) o_sfx(0x5B);          /* sfx_Switch */
            w8(a3, (uint8_t)(r8(a3) | (1 << d3)));
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) | 1);
        } else {
            w8(a3, (uint8_t)(r8(a3) & ~(1 << d3)));
        }
        if (sub & 0x20) {
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
                SET8(o, OBJ_anim_timer, 7);
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 2);
            }
        }
    }
    o_draw(o);
    chk_del(o, OB16(o, OBJ_x_pos));
}

/* ------------------------------------------------------------------------
 * Object 33 - pushable blocks. S1's obSolid states 4 (falling) and 6
 * (sliding to a 16px boundary) are PB_STATE; standing/riding is S3K's.
 * ---------------------------------------------------------------------- */
#define PB_SAVEVX 0x30
#define PB_LAVA   0x32
#define PB_ORIGX  0x34
#define PB_ORIGY  0x36
#define PB_STATE  0x3A
#define PB_LAVABLOCK 0x16A          /* first MZ lava block */

static void pushb_land(uint32_t o, int16_t d1, uint16_t blk)
{
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
    SET16(o, OBJ_y_vel, 0);
    if ((blk & 0x3FF) >= PB_LAVABLOCK) {       /* landed in lava: ride it */
        SET16(o, OBJ_x_vel, OBS16(o, PB_SAVEVX) >> 3);
        SET8(o, PB_LAVA, 1);
        SET16(o, OBJ_y_pos + 2, 0);
    }
}

/* loc_C186 */
static void pushb_solid(uint32_t o, int d1, int d2, int d3, int d4)
{
    switch (OB8(o, PB_STATE)) {
    case 4: {                                  /* falling */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        uint16_t blk;
        int16_t f = o_floor_block(o, &blk);
        if (f < 0) { SET8(o, PB_STATE, 0); pushb_land(o, f, blk); }
        return;
    }
    case 6:                                    /* slide to the 16px boundary */
        o_move(o);
        if (!(OB16(o, OBJ_x_pos) & 0x0C)) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) & 0xFFF0);
            SET16(o, PB_SAVEVX, OB16(o, OBJ_x_vel));
            SET16(o, OBJ_x_vel, 0);
            SET8(o, PB_STATE, 4);
        }
        return;
    }
    o_solid(o, d1, d2, d3, d4);
    if (!(g_o_d6 & (1u << 16)) || OB8(o, PB_LAVA))  /* P1 side contact */
        return;
    uint32_t p = RAM_Player_1;
    if (OB8(p, OBJ_status) & 2) return;        /* in the air */
    int right = (int16_t)OB16(p, OBJ_x_pos) < (int16_t)OB16(o, OBJ_x_pos);
    int d0, vel;
    if (right) {
        if (OB8(p, OBJ_status) & 1) return;    /* facing away */
        if (o_wall_right(o, OB8(o, OBJ_width_px)) < 0) return;
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + 0x10000u);
        d0 = 1; vel = 0x40;
    } else {
        if (!(OB8(p, OBJ_status) & 1)) return;
        if (o_wall_left(o, ~(int)OB8(o, OBJ_width_px)) < 0) return;
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) - 0x10000u);
        d0 = -1; vel = -0x40;
    }
    SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + d0);
    SET16(p, OBJ_ground_vel, vel);
    SET16(p, OBJ_x_vel, 0);
    o_sfx(0x69);                               /* sfx_Push */
    if (OB8(o, OBJ_subtype) & 0x80) return;
    int16_t f = o_floor_dist(o);
    if (f > 4) {
        SET16(o, OBJ_x_vel, d0 > 0 ? 0x400 : -0x400);
        SET8(o, PB_STATE, 6);
    } else {
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + f);
    }
}

static void pushb_chklava(uint32_t o)
{
    int16_t d2;
    uint16_t x = OB16(o, OBJ_x_pos);
    if (zone_act() == 0x2201) {
        d2 = -0x20;
        if (x != 0xDD0 && x != 0xCC0 && x != 0xBA0) return;
    } else if (zone_act() == 0x2202) {
        d2 = 0x20;
        if (x != 0x560 && x != 0x5C0) return;
    } else return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x4C * 4);
    SET16(a1, OBJ_x_pos, x + d2);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
    SET32(a1, 0x3C, RAMPTR(o));
}

static void push_block(uint32_t o);

static void pushb_offscreen(uint32_t o)        /* loc_BFE6 */
{
    if (o_out_of_range(o, OB16(o, PB_ORIGX))) {   /* loc_C016 */
        uint16_t r = OB16(o, OBJ_respawn);
        if (r) w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & ~1));
        o_delete(o);
        return;
    }
    SET16(o, OBJ_x_pos, OB16(o, PB_ORIGX));
    SET16(o, OBJ_y_pos, OB16(o, PB_ORIGY));
    SET8(o, OBJ_routine, 4);
    push_block(o);
}

static void push_block(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t var[2][2] = { { 0x10, 0 }, { 0x40, 1 } };
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 0x0F);
        SET8(o, OBJ_x_radius, 0x0F);
        o_init(o, S1A_PUSH, 4, 3, 0x10, 0x10);
        SET16(o, PB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, PB_ORIGY, OB16(o, OBJ_y_pos));
        int i = (OB8(o, OBJ_subtype) * 2 & 0x0E) >> 1;
        if (i > 1) i = 1;
        SET8(o, OBJ_width_px, var[i][0]);
        SET8(o, OBJ_frame, var[i][1]);
        if (OB8(o, OBJ_subtype)) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        uint16_t r = OB16(o, OBJ_respawn);
        if (r) {
            uint32_t a = 0xFF0000u | r;
            uint8_t v = (uint8_t)(r8(a) & 0x7F);
            w8(a, (uint8_t)(v | 1));
            if (v & 1) { o_delete(o); return; }
        }
    }
    /* fall through */
    case 2:
        if (!OB8(o, PB_LAVA)) {
            pushb_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, OB16(o, OBJ_x_pos));
            if (zone_act() == 0x2200) {        /* MZ1: rides the chained stomper */
                SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x7F);
                uint16_t x = OB16(o, OBJ_x_pos);
                if (x >= 0xA20 && x < 0xAA1) {
                    SET16(o, OBJ_y_pos, (uint16_t)(s_obj31ypos - 0x1C));
                    s_obj31ypos |= 0x8000;
                    SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) | 0x80);
                }
            }
        } else {                               /* loc_C046: riding lava */
            uint16_t oldx = OB16(o, OBJ_x_pos);
            if (OB8(o, PB_STATE) < 4) o_move(o);
            if (OB8(o, OBJ_status) & 2) {
                SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
                uint16_t blk;
                int16_t f = o_floor_block(o, &blk);
                if (f < 0) {
                    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~2);
                    pushb_land(o, f, blk);
                }
            } else if (OB16(o, OBJ_x_vel)) {
                int16_t w = OB8(o, OBJ_width_px);
                if (OBS16(o, OBJ_x_vel) > 0 ? o_wall_right(o, w) < 0 : o_wall_left(o, ~w) < 0)
                    SET16(o, OBJ_x_vel, 0);
            } else {
                SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + 0x2001u);   /* sink */
                if (OB8(o, OBJ_y_pos + 3) >= 0xA0) {
                    w8(RAM_Player_1 + OBJ_status, (uint8_t)(r8(RAM_Player_1 + OBJ_status) & ~8));
                    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~8);
                    pushb_offscreen(o);
                    return;
                }
            }
            pushb_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, oldx);
            pushb_chklava(o);
        }
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { pushb_offscreen(o); return; }
        o_draw(o);
        return;
    case 4:                                    /* PushB_ChkVisible */
        if (!o_partly_visible(o)) {
            SET8(o, OBJ_routine, 2);
            SET8(o, PB_LAVA, 0);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
        }
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 46 - solid blocks and blocks that fall from the ceiling
 * ---------------------------------------------------------------------- */
#define BR_ORIGY  0x30
static void brick(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 0x0F);
        SET8(o, OBJ_x_radius, 0x0F);
        o_init(o, S1A_BRICK, 4, 3, 0x10, 0x10);
        SET16(o, BR_ORIGY, OB16(o, OBJ_y_pos));
        SET16(o, 0x32, 0x5C0);
    }
    if (OB8(o, OBJ_render) & 0x80) {
        switch (OB8(o, OBJ_subtype) & 7) {
        case 2: {
            int16_t d0 = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
            if (d0 < 0) d0 = -d0;
            if ((uint16_t)d0 < 0x90) { SET8(o, OBJ_subtype, 3); break; }
        }
        /* fall through */
        case 1: {
            int16_t d0 = o_osc(0x16);
            if (OB8(o, OBJ_subtype) & 8) d0 = (int16_t)(-d0 + 0x10);
            SET16(o, OBJ_y_pos, OB16(o, BR_ORIGY) - d0);
            break;
        }
        case 3: {
            o_move(o);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            uint16_t blk;
            int16_t f = o_floor_block(o, &blk);
            if (f < 0) {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + f);
                SET16(o, OBJ_y_vel, 0);
                SET16(o, BR_ORIGY, OB16(o, OBJ_y_pos));
                SET8(o, OBJ_subtype, 4);
                if ((blk & 0x3FF) < PB_LAVABLOCK) SET8(o, OBJ_subtype, 0);
            }
            break;
        }
        case 4:
            SET16(o, OBJ_y_pos, OB16(o, BR_ORIGY) - (o_osc(0x12) >> 3));
            break;
        }
        o_solid(o, 0x1B, 0x10, 0x11, OB16(o, OBJ_x_pos));
    }
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 4C - lava geyser / lavafall maker; Object 4D - geyser / lavafall
 * ---------------------------------------------------------------------- */
#define GM_TIMER  0x32
#define GM_TIME   0x34
#define GM_PARENT 0x3C
#define GY_ORIGY  0x30

static uint32_t ptr_obj(uint32_t o, int f)
{
    uint16_t a = OB16(o, f + 2);
    return a ? (0xFF0000u | a) : 0;
}

static void geyser_maker(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_GEYSER, 4, 1, 0x38, 0x20);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET16(o, GM_TIME, 120);
        /* fall through */
    case 2: {                                  /* GMake_Wait */
        int16_t t = (int16_t)(OB16(o, GM_TIMER) - 1);
        SET16(o, GM_TIMER, t);
        if (t < 0) {
            SET16(o, GM_TIMER, OB16(o, GM_TIME));
            uint16_t py = r16(RAM_Player_1 + OBJ_y_pos), y = OB16(o, OBJ_y_pos);
            if (py < y && py >= (uint16_t)(y - 0x170))
                SET8(o, OBJ_routine, 4);
        }
        break;
    }
    case 4:                                    /* GMake_ChkType */
        if (!OB8(o, OBJ_subtype)) goto display;
        SET8(o, OBJ_routine, 6);
        break;
    case 6: {                                  /* GMake_MakeLava */
        SET8(o, OBJ_routine, 8);
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x4D * 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET32(a1, GM_PARENT, RAMPTR(o));
        }
        SET8(o, OBJ_anim, 1);
        if (OB8(o, OBJ_subtype)) {
            SET8(o, OBJ_anim, 4);
        } else {
            uint32_t p = ptr_obj(o, GM_PARENT);  /* launch the push block */
            if (p) {
                SET8(p, OBJ_status, OB8(p, OBJ_status) | 2);
                SET16(p, OBJ_y_vel, -0x580);
            }
        }
        goto display;
    }
    case 8:
    display:                                   /* GMake_Display */
        o_animate(o, o_asset_anim(S1A_GEYSER));
        o_draw(o);
        break;
    case 0x0A:                                 /* GMake_Delete */
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_routine, 2);
        if (!OB8(o, OBJ_subtype)) { o_delete(o); return; }
        break;
    }
    chk_del(o, OB16(o, OBJ_x_pos));
}

static void geyser_piece(uint32_t o, uint32_t a1)   /* .makelava */
{
    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x4D * 4);
    o_init(a1, S1A_GEYSER, 4, 1, 0x20, 0x20);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
    SET8(a1, OBJ_anim, OB8(o, OBJ_subtype) ? 2 : 5);
}

static void lava_geyser(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const int16_t speeds[2] = { (int16_t)0xFB00, 0 };
        SET8(o, OBJ_routine, 2);
        SET16(o, GY_ORIGY, OB16(o, OBJ_y_pos));
        if (OB8(o, OBJ_subtype)) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x250);
        SET16(o, OBJ_y_vel, speeds[OB8(o, OBJ_subtype) & 1]);
        geyser_piece(o, o);
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            geyser_piece(o, a1);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x60);   /* .activate */
            SET16(a1, GY_ORIGY, OB16(o, GY_ORIGY) + 0x60);
            SET8(a1, OBJ_coll_flags, 0x93);
            SET8(a1, OBJ_shield_reaction, SHIELD_FIRE);
            SET8(a1, OBJ_height_px, 0x80);
            SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 0x10);
            SET8(a1, OBJ_routine, OB8(a1, OBJ_routine) + 4);
            SET32(a1, GM_PARENT, RAMPTR(o));
            if (OB8(o, OBJ_subtype)) {
                uint32_t a2 = o_alloc_after(o);
                if (a2) {
                    geyser_piece(o, a2);
                    SET8(a2, OBJ_routine, OB8(a2, OBJ_routine) + 2);
                    SET16(a2, OBJ_art_tile, OB16(a2, OBJ_art_tile) | 0x1000);   /* bset #4,obGfx */
                    SET16(a2, OBJ_y_pos, OB16(a2, OBJ_y_pos) + 0x100);
                    SET16(a2, OBJ_priority, 0);
                    SET16(a2, GY_ORIGY, OB16(o, GY_ORIGY));
                    SET32(a2, GM_PARENT, r32(o + GM_PARENT));
                }
                SET8(o, OBJ_subtype, 0);
            }
        }
        o_sfx(0x43);                           /* sfx_Burning */
    }
    /* fall through */
    case 2: {                                  /* Geyser_Action */
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if (OB16(o, GY_ORIGY) < OB16(o, OBJ_y_pos)) {
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 4);
            uint32_t p = ptr_obj(o, GM_PARENT);
            if (p) SET8(p, OBJ_anim, OB8(o, OBJ_subtype) ? 1 : 3);
        }
        o_move(o);
        o_animate(o, o_asset_anim(S1A_GEYSER));
        break;
    }
    case 4: {                                  /* Geyser_Middle */
        uint32_t p = ptr_obj(o, GM_PARENT);
        if (!p || OB8(p, OBJ_routine) == 6) { o_delete(o); return; }
        int16_t d0 = (int16_t)(OB16(p, OBJ_y_pos) + 0x60);
        SET16(o, OBJ_y_pos, d0);
        d0 = (int16_t)-(d0 - (int16_t)OB16(o, GY_ORIGY));
        int d1 = 8;
        if (d0 < 0x40) d1 = 0x0B;
        if (d0 > 0x80) d1 = 0x0E;
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_anim_timer, 7);
            SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
            if (OB8(o, OBJ_anim_frame) >= 2) SET8(o, OBJ_anim_frame, 0);
        }
        SET8(o, OBJ_frame, OB8(o, OBJ_anim_frame) + d1);
        o_touch(o);
        break;
    }
    default:
        o_delete(o);
        return;
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 4E - advancing wall of lava
 * ---------------------------------------------------------------------- */
#define LW_FLAG   0x36
#define LW_PARENT 0x3C
static void lava_wall(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 4);
        uint32_t a1 = o;
        for (int i = 0; i < 2; i++) {
            if (i) { a1 = o_alloc_after(o); if (!a1) break; }
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x4E * 4);
            o_init(a1, S1A_LWALL, 4, 1, 0x50, 0x20);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_anim, 0);
            SET8(a1, OBJ_coll_flags, 0x94);
            SET8(a1, OBJ_shield_reaction, SHIELD_FIRE);
            SET32(a1, LW_PARENT, RAMPTR(o));
        }
        if (a1 && a1 != o) {
            SET8(a1, OBJ_routine, 6);
            SET8(a1, OBJ_frame, 4);
        }
    }
    /* fall through */
    case 4: {                                  /* LWall_Action */
        int16_t dx = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (dx < 0) dx = -dx;
        int16_t dy = (int16_t)(r16(RAM_Player_1 + OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if (dy < 0) dy = -dy;
        if ((uint16_t)dx < 0xC0 && (uint16_t)dy < 0x60) {
            SET8(o, LW_FLAG, 1);
        } else if (OB8(o, LW_FLAG)) {
            SET16(o, OBJ_x_vel, 0x180);
            SET8(o, OBJ_routine, 2);
        }
    }
    /* fall through */
    case 2: {                                  /* LWall_Solid */
        uint8_t rt = OB8(o, OBJ_routine);
        o_solid(o, 0x2B, 0x18, 0x19, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_routine, rt);
        if (OB16(o, OBJ_x_pos) == 0x6A0) {
            SET16(o, OBJ_x_vel, 0);
            SET8(o, LW_FLAG, 0);
        }
        o_animate(o, o_asset_anim(S1A_LWALL));
        if (r8(RAM_Player_1 + OBJ_routine) < 4) o_move(o);
        o_touch(o);
        o_draw(o);
        if (!OB8(o, LW_FLAG) && o_out_of_range(o, OB16(o, OBJ_x_pos))) {
            uint16_t r = OB16(o, OBJ_respawn);
            if (r) w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
            SET8(o, OBJ_routine, 8);
        }
        return;
    }
    case 6: {                                  /* LWall_Move */
        uint32_t p = ptr_obj(o, LW_PARENT);
        if (!p || OB8(p, OBJ_routine) == 8) { o_delete(o); return; }
        SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos) - 0x80);
        o_touch(o);
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 51 - smashable green block
 * ---------------------------------------------------------------------- */
static void smash_block(uint32_t o)
{
    static const int16_t speeds[8] = { -0x200, -0x200, -0x100, -0x100, 0x200, -0x200, 0x100, -0x100 };
    static const uint16_t scores[4] = { 10, 20, 50, 100 };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SMAB, 4, 4, 0x10, 0x10);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        /* fall through */
    case 2: {
        uint16_t count = r16(RAM_Chain_bonus_counter);
        uint8_t anim[2] = { r8(RAM_Player_1 + OBJ_anim), r8(RAM_Player_2 + OBJ_anim) };
        o_solid(o, 0x1B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        int hit = 0;
        for (int n = 0; n < 2; n++) {
            if (!(OB8(o, OBJ_status) & (ST_P1_STAND << n)) || anim[n] != 2) continue;
            uint32_t p = o_player(n);
            w16(RAM_Chain_bonus_counter, count);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | 4 | 2) & ~8);
            SET8(p, OBJ_y_radius, 0x0E);
            SET8(p, OBJ_x_radius, 7);
            SET8(p, OBJ_anim, 2);
            SET16(p, OBJ_y_vel, -0x300);
            SET8(p, OBJ_routine, 2);
            hit = 1;
        }
        if (!hit) { o_remember(o); return; }
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
        SET8(o, OBJ_frame, 1);
        o_smash(o, 4, speeds, 0x38, 4);
        uint16_t d2 = r16(RAM_Chain_bonus_counter);
        w16(RAM_Chain_bonus_counter, d2 + 2);
        if (d2 >= 6) d2 = 6;
        uint16_t pts = scores[d2 >> 1];
        if (r16(RAM_Chain_bonus_counter) >= 0x20) { pts = 1000; d2 = 10; }
        D(0) = pts;
        tri_jsr(S3K_HUD_AddToScore);
        o_score(o, d2 >> 1);
        return;
    }
    default:                                   /* Smab_Points: a fragment */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        o_draw(o);
        if (!(OB8(o, OBJ_render) & 0x80)) o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 52 - moving platform blocks (MZ; LZ/SBZ art later)
 * ---------------------------------------------------------------------- */
#define MB_ORIGX  0x30
#define MB_ORIGY  0x32
static void moving_block(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        static const uint8_t var[5][2] = { { 0x10, 0 }, { 0x20, 1 }, { 0x20, 2 }, { 0x40, 3 }, { 0x30, 4 } };
        SET8(o, OBJ_routine, 2);
        int mb = S1A_MBLOCK;
        if (tri_zone() == TRI_S1_LZ) mb = S1A_MBLOCK_LZ;
        if (tri_zone() == TRI_S1_SBZ)          /* 5228 short block, 523x long sliding floor */
            mb = OB8(o, OBJ_subtype) == 0x28 ? S1A_MBLOCK_SBZS : S1A_MBLOCK_SBZL;
        o_init(o, mb, 4, 4, 0x10, 0x10);
        if (tri_zone() == TRI_S1_LZ) SET8(o, OBJ_y_radius, 7);
        int i = ((OB8(o, OBJ_subtype) >> 3) & 0x1E) >> 1;
        if (i > 4) i = 4;
        SET8(o, OBJ_width_px, var[i][0]);
        SET8(o, OBJ_frame, var[i][1]);
        SET16(o, MB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, MB_ORIGY, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    int w = OB8(o, OBJ_width_px);
    switch (OB8(o, OBJ_subtype) & 0x0F) {
    case 1: {
        int16_t d0 = o_osc(0x0E);
        if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(0x60 - d0);
        SET16(o, OBJ_x_pos, OB16(o, MB_ORIGX) - d0);
        break;
    }
    case 2: case 4: case 9:
        if (o_standing(o)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
        break;
    case 3:
        if (o_wall_right(o, w) < 0) SET8(o, OBJ_subtype, 0);
        else { SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 1); SET16(o, MB_ORIGX, OB16(o, OBJ_x_pos)); }
        break;
    case 5:
        if (o_wall_right(o, w) < 0) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
        else { SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 1); SET16(o, MB_ORIGX, OB16(o, OBJ_x_pos)); }
        break;
    case 6: {
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        int16_t f = o_floor_dist(o);
        if (f < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + f);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_subtype, 0);
        }
        break;
    }
    case 7:                                    /* hidden until switch 2 */
        if (r8(TRIGGER(2))) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 3);
        chk_del(o, OB16(o, MB_ORIGX));
        return;
    case 8: {
        int16_t d0 = o_osc(0x1E);
        if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(0x80 - d0);
        SET16(o, OBJ_y_pos, OB16(o, MB_ORIGY) - d0);
        break;
    }
    case 0x0A: {
        int16_t d3 = (int16_t)(w * 2), d1 = 8;
        if (OB8(o, OBJ_status) & 1) { d1 = -d1; d3 = -d3; }
        if (!OB16(o, 0x36)) {
            if ((int16_t)(OB16(o, OBJ_x_pos) - OB16(o, MB_ORIGX)) == d3) {
                SET16(o, 0x34, OB16(o, 0x34) - 1);
                if (!OB16(o, 0x34)) SET16(o, 0x36, 1);
            } else {
                SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + d1);
                SET16(o, 0x34, 300);
            }
        } else if (OB16(o, OBJ_x_pos) != OB16(o, MB_ORIGX)) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - d1);
        } else {
            SET16(o, 0x36, 0);
            SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 1);
        }
        break;
    }
    default:
        break;
    }
    o_platform(o, w, 8, oldx);
    if (chk_del(o, OB16(o, MB_ORIGX))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 53 - collapsing floors (MZ; SLZ/SBZ art later)
 * ---------------------------------------------------------------------- */
#define CF_TIME   0x38
#define CF_FLAG   0x3A
static void release_p(uint32_t o)
{
    for (int n = 0; n < 2; n++) {
        if (!(OB8(o, OBJ_status) & (ST_P1_STAND << n))) continue;
        uint32_t p = o_player(n);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~(8 | 0x20)) | 2);
        SET8(p, 0x21, 0);                      /* obPrevAni: restart animation */
    }
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
}

static void collapse_floor(uint32_t o)
{
    static const uint8_t data2[8] = { 0x1E, 0x16, 0x0E, 6, 0x1A, 0x12, 0x0A, 2 };
    static const uint8_t data3[8] = { 0x16, 0x1E, 0x1A, 0x12, 6, 0x0E, 0x0A, 2 };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_CFLO, OB8(o, OBJ_render) | 4, 4, 0x44, 0x20);
        if (tri_zone() == TRI_S1_SLZ) SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 2);
        SET8(o, CF_TIME, 7);
        /* fall through */
    case 2:
        if (OB8(o, CF_FLAG)) {
            if (!OB8(o, CF_TIME)) {            /* CFlo_Fragment */
                SET8(o, CF_FLAG, 0);
                const uint8_t *d = (OB8(o, OBJ_subtype) & 1) ? data3 : data2;
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
                uint32_t maps = r32(o + OBJ_mappings);
                uint32_t fa = maps + tri_rd16(maps + OB8(o, OBJ_frame) * 2u);
                uint32_t piece = fa + 2;
                uint8_t render = (uint8_t)(OB8(o, OBJ_render) | 0x20);
                SET8(o, OBJ_render, render);
                uint32_t a1 = o;
                for (int i = 0; i < 8; i++) {
                    if (i) { a1 = o_alloc(); if (!a1) break; piece += 6; }
                    SET8(a1, OBJ_routine, 6);
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x53 * 4);
                    SET32(a1, OBJ_mappings, piece);
                    SET8(a1, OBJ_render, render);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
                    SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
                    SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
                    SET8(a1, OBJ_height_px, 0x10);
                    SET8(a1, CF_TIME, d[i]);
                }
                o_draw(o);
                o_sfx(0x59);                   /* sfx_Collapse */
                return;
            }
            SET8(o, CF_TIME, OB8(o, CF_TIME) - 1);
        }
        o_platform(o, 0x20, 8, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) {
            if (OB8(o, OBJ_subtype) & 0x80) {
                uint8_t r = (uint8_t)(OB8(o, OBJ_render) & ~1);
                if ((int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos)) < 0) r |= 1;
                SET8(o, OBJ_render, r);
            }
        }
        if (o_standing(o)) SET8(o, CF_FLAG, 1);
        o_remember(o);
        return;
    case 6: {                                  /* CFlo_Display (fragment) */
        uint8_t t = OB8(o, CF_TIME);
        if (t) {
            SET8(o, CF_TIME, t - 1);
            if (o_standing(o)) {
                o_platform(o, 0x20, 8, OB16(o, OBJ_x_pos));
                if (t == 1) release_p(o);
            }
            o_draw(o);
            return;
        }
        release_p(o);
        o_fall(o);
        o_draw(o);
        if (!(OB8(o, OBJ_render) & 0x80)) o_delete(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 54 - invisible lava tag
 * ---------------------------------------------------------------------- */
static void lava_tag(uint32_t o)
{
    static const uint8_t cols[3] = { 0x96, 0x94, 0x95 };
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_coll_flags, cols[OB8(o, OBJ_subtype) % 3]);
        SET8(o, OBJ_shield_reaction, SHIELD_FIRE);
        SET8(o, OBJ_render, 0x84);
    }
    int16_t d0 = (int16_t)((OB16(o, OBJ_x_pos) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80));
    if (d0 < 0 || d0 > 0x280) { o_delete_respawnable(o); return; }
    o_touch(o);
}

/* ------------------------------------------------------------------------
 * Object 55 - Basaran
 * ---------------------------------------------------------------------- */
#define BAS_TARGY 0x36
#define BAS_2ND   0x3A

static int bas_chkdist(uint32_t o, int d2, int16_t *d1)   /* carry = closer than d2 */
{
    *d1 = 0x100;
    SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
    int16_t d0 = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if (d0 < 0) {
        d0 = -d0;
        *d1 = -0x100;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
    }
    return (uint16_t)d0 < (uint16_t)d2;
}

static void basaran(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_BASARAN, 4, 2, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_y_radius, 0x0C);
        SET8(o, OBJ_coll_flags, 0x0B);
    }
    uint8_t d7 = (uint8_t)D(7);
    int16_t d1;
    switch (OB8(o, BAS_2ND)) {
    case 0:                                    /* .dropcheck */
        if (bas_chkdist(o, 0x80, &d1)) {
            uint16_t py = r16(RAM_Player_1 + OBJ_y_pos);
            SET16(o, BAS_TARGY, py);
            int32_t dy = (int32_t)py - (int32_t)OB16(o, OBJ_y_pos);
            if (dy >= 0 && dy < 0x80 && !r16(RAM_Debug_placement_mode)
                && !((uint8_t)(o_frame_counter() + d7) & 7)) {
                SET8(o, OBJ_anim, 1);
                SET8(o, BAS_2ND, 2);
            }
        }
        break;
    case 2: {                                  /* .dropfly */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        bas_chkdist(o, 0x80, &d1);
        int32_t d0 = (int32_t)OB16(o, BAS_TARGY) - (int32_t)OB16(o, OBJ_y_pos);
        if (d0 < 0) {
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            break;
        }
        if (d0 < 0x10) {
            SET16(o, OBJ_x_vel, d1);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_anim, 2);
            SET8(o, BAS_2ND, 4);
        }
        break;
    }
    case 4: {                                  /* .flapsound */
        if (!(o_frame_counter() & 0x0F)) o_sfx(0xBA);   /* sfx_Basaran (S3K: flying flap) */
        o_move(o);
        int16_t d0 = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (d0 < 0) d0 = -d0;
        if ((uint16_t)d0 >= 0x80 && !((uint8_t)(o_frame_counter() + d7) & 7))
            SET8(o, BAS_2ND, 6);
        break;
    }
    case 6: {                                  /* .flyup */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 0x18);
        int16_t c = o_ceiling_dist(o);
        if (c < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - c);
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) & 0xFFF8);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_anim, 0);
            SET8(o, BAS_2ND, 0);
        }
        break;
    }
    }
    o_animate(o, o_asset_anim(S1A_BASARAN));
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 78 - Caterkiller. Body segments use S3K's special touch (size $17,
 * 8x8 like S1's $0B): touching one hurts that player and breaks the
 * Caterkiller apart, as S1's React_Caterkiller did.
 * ---------------------------------------------------------------------- */
#define CK_HIST    0x30       /* S1 $2C.. : 16-byte y history */
#define CK_TIMER   0x40       /* S1 $2A */
#define CK_FLAGS   0x41       /* S1 $2B */
#define CK_PARENT  0x42       /* S1 cat_parent (address) */
#define CK_IDX     0x44       /* S1 cat_parent top byte (history index) */
#define CK_2ND     0x45       /* S1 ob2ndRout */
#define CK_DEAD    0x46       /* S1 obStatus bit 7 */
#define CK_BODY_COLL 0xD7

static const uint8_t ani_cat[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,
    6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0xFF, 7, 7, 0xFF,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6,
    6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4,
    4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0, 0, 0xFF,
};

/* objoff_2C(a0,idx): index 16 lands on S1's cat_parent byte (the index) */
static void ck_hist_set(uint32_t o, int idx, uint8_t v)
{
    if (idx == 16) SET8(o, CK_IDX, v);
    else SET8(o, CK_HIST + (idx & 0x0F), v);
}

static void ck_chk_gone(uint32_t o)
{
    uint16_t r = OB16(o, OBJ_respawn);
    if (r) w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
    SET8(o, OBJ_routine, 0x0A);
}

static void ck_fragment_init(uint32_t o)        /* loc_16C96 */
{
    static const int16_t frag[4] = { -0x200, -0x180, 0x180, 0x200 };
    int r = OB8(o, OBJ_routine);
    int16_t vx = frag[((r >> 1) - 1) & 3];
    if (OB8(o, OBJ_status) & 1) vx = -vx;
    SET16(o, OBJ_x_vel, vx);
    SET16(o, OBJ_y_vel, -0x400);
    SET8(o, OBJ_routine, 0x0C);
    SET8(o, OBJ_frame, OB8(o, OBJ_frame) & 0xF8);
}

static int ck_touched(uint32_t o)                 /* body: S3K special touch */
{
    uint8_t n = OB8(o, OBJ_coll_prop);
    if (!n) return 0;
    SET8(o, OBJ_coll_prop, 0);
    if (n & 1) o_hurt_player(RAM_Player_1, o);
    if (n >= 2) o_hurt_player(RAM_Player_2, o);
    return 1;
}

static void cater_floor(uint32_t o)               /* Cat_Floor (REV01) */
{
    SET8(o, CK_TIMER, OB8(o, CK_TIMER) - 1);
    if ((int8_t)OB8(o, CK_TIMER) < 0) {
        SET8(o, CK_2ND, 0);
        SET8(o, CK_TIMER, 7);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_ground_vel, 0);
        return;
    }
    if (!OB16(o, OBJ_x_vel)) return;
    uint32_t d2 = r32(o + OBJ_x_pos), d3 = d2;
    int32_t d0 = OBS16(o, OBJ_x_vel);
    if (OB8(o, OBJ_status) & 1) d0 = -d0;
    SET32(o, OBJ_x_pos, d2 + (uint32_t)(d0 << 8));
    if ((uint16_t)(d3 >> 16) == OB16(o, OBJ_x_pos)) return;
    int16_t d1 = o_floor_dist(o);
    if (d1 >= -8 && d1 < 0x0C) {
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        int idx = OB8(o, CK_IDX);
        SET8(o, CK_IDX, (idx + 1) & 0x0F);
        ck_hist_set(o, idx, (uint8_t)d1);
        return;
    }
    int idx = OB8(o, CK_IDX);                      /* .loc_16B70: turn around */
    ck_hist_set(o, idx, 0x80);
    uint16_t frac = (uint16_t)-OB16(o, OBJ_x_pos + 2);
    SET16(o, OBJ_x_pos + 2, frac);
    if (frac && (OB8(o, OBJ_status) & 1)) {
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
        SET8(o, CK_IDX, OB8(o, CK_IDX) + 1);
        ck_hist_set(o, OB8(o, CK_IDX), 0);
    }
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | (OB8(o, OBJ_status) & 3));
    SET8(o, CK_IDX, (OB8(o, CK_IDX) + 1) & 0x0F);
}

static void caterkiller(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_y_radius, 7);
        SET8(o, OBJ_x_radius, 8);
        o_fall(o);
        int16_t f = o_floor_dist(o);
        if (f >= 0) return;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + f);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_routine, 2);
        o_asset(o, S1A_CATER);
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 3) | 4);
        SET8(o, OBJ_status, OB8(o, OBJ_render));
        SET16(o, OBJ_priority, 4 << 7);
        SET8(o, OBJ_width_px, 8);
        SET8(o, OBJ_height_px, 8);
        SET8(o, OBJ_coll_flags, 0x0B);
        int16_t d2 = (int16_t)OB16(o, OBJ_x_pos), d5 = 0x0C;
        if (OB8(o, OBJ_status) & 1) d5 = -d5;
        int d6 = 4, d4 = 4;
        uint32_t a2 = o;
        for (int i = 0; i < 3; i++) {
            uint32_t a1 = o_alloc_after(o);
            if (!a1) { ck_chk_gone(o); return; }
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x78 * 4);
            SET8(a1, OBJ_routine, d6);
            d6 += 2;
            SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
            SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
            SET16(a1, OBJ_priority, 5 << 7);
            SET8(a1, OBJ_width_px, 8);
            SET8(a1, OBJ_height_px, 8);
            SET8(a1, OBJ_coll_flags, CK_BODY_COLL);
            d2 = (int16_t)(d2 + d5);
            SET16(a1, OBJ_x_pos, d2);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_status, OB8(o, OBJ_status));
            SET8(a1, OBJ_render, OB8(o, OBJ_status));
            SET8(a1, OBJ_frame, 8);
            SET16(a1, CK_PARENT, a2 & 0xFFFF);
            SET8(a1, CK_IDX, d4);
            d4 += 4;
            a2 = a1;
        }
        SET8(o, CK_TIMER, 7);
        SET8(o, CK_IDX, 0);
    }
    /* fall through */
    case 2: {                                  /* Cat_Head */
        if (OB8(o, CK_DEAD)) { ck_fragment_init(o); goto fragment; }
        if (OB8(o, CK_2ND) == 0) {             /* Cat_Undulate */
            SET8(o, CK_TIMER, OB8(o, CK_TIMER) - 1);
            if ((int8_t)OB8(o, CK_TIMER) < 0) {
                SET8(o, CK_2ND, 2);
                SET8(o, CK_TIMER, 0x10);
                SET16(o, OBJ_x_vel, -0xC0);
                SET16(o, OBJ_ground_vel, 0x40);
                uint8_t f = OB8(o, CK_FLAGS);
                SET8(o, CK_FLAGS, f ^ 0x10);
                if (!(f & 0x10)) {
                    SET16(o, OBJ_x_vel, 0);
                    SET16(o, OBJ_ground_vel, -0x40);
                }
                SET8(o, CK_FLAGS, OB8(o, CK_FLAGS) | 0x80);
                cater_floor(o);
            }
        } else {
            cater_floor(o);
        }
        uint8_t d1 = OB8(o, CK_FLAGS);
        if (d1 & 0x80) {
            int d0 = OB8(o, OBJ_angle) & 0x7F;
            SET8(o, OBJ_angle, OB8(o, OBJ_angle) + 4);
            uint8_t a = ani_cat[d0];
            if (a & 0x80) SET8(o, CK_FLAGS, d1 & 0x7F);
            else SET8(o, OBJ_frame, a + (d1 & 0x10));
        }
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { ck_chk_gone(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    case 6: {                                  /* Cat_BodySeg2 */
        uint32_t p = 0xFF0000u | OB16(o, CK_PARENT);
        SET8(o, CK_FLAGS, OB8(p, CK_FLAGS));
        if (OB8(o, CK_FLAGS) & 0x80) {
            int d0 = OB8(o, OBJ_angle) & 0x7F;
            SET8(o, OBJ_angle, OB8(o, OBJ_angle) + 4);
            if (ani_cat[(d0 + 4) & 0x7F] & 0x80) SET8(o, OBJ_angle, OB8(o, OBJ_angle) + 4);
            SET8(o, OBJ_frame, ani_cat[d0] + 8);
        }
    }
    /* fall through */
    case 4: case 8: {                          /* Cat_BodySeg1 */
        uint32_t p = 0xFF0000u | OB16(o, CK_PARENT);
        if (ck_touched(o)) SET8(o, CK_DEAD, 1);
        if (OB8(o, CK_DEAD)) {
            SET8(p, CK_DEAD, 1);
            ck_fragment_init(o);
            goto fragment;
        }
        SET8(o, CK_FLAGS, OB8(p, CK_FLAGS));
        SET8(o, CK_2ND, OB8(p, CK_2ND));
        if (OB8(o, CK_2ND)) {
            SET16(o, OBJ_ground_vel, OB16(p, OBJ_ground_vel));
            SET16(o, OBJ_x_vel, OB16(p, OBJ_x_vel) + OB16(o, OBJ_ground_vel));
            uint32_t d2 = r32(o + OBJ_x_pos), d3 = d2;
            int32_t d0 = OBS16(o, OBJ_x_vel);
            if (OB8(o, OBJ_status) & 1) d0 = -d0;
            SET32(o, OBJ_x_pos, d2 + (uint32_t)(d0 << 8));
            if ((uint16_t)(d3 >> 16) != OB16(o, OBJ_x_pos)) {
                int idx = OB8(o, CK_IDX);
                uint8_t d1 = (idx == 16) ? OB8(p, CK_IDX) : OB8(p, CK_HIST + (idx & 0x0F));
                if (d1 == 0x80) {
                    ck_hist_set(o, idx, d1);
                    uint16_t frac = (uint16_t)-OB16(o, OBJ_x_pos + 2);
                    SET16(o, OBJ_x_pos + 2, frac);
                    if (frac && (OB8(o, OBJ_status) & 1) && OBS16(o, OBJ_x_vel) == -0xC0) {
                        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
                        SET8(o, CK_IDX, OB8(o, CK_IDX) + 1);
                        ck_hist_set(o, OB8(o, CK_IDX), 0);
                    }
                    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
                    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | (OB8(o, OBJ_status) & 3));
                    SET8(o, CK_IDX, (OB8(o, CK_IDX) + 1) & 0x0F);
                } else {
                    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + (int8_t)d1);
                    SET8(o, CK_IDX, (idx + 1) & 0x0F);
                    ck_hist_set(o, idx, d1);
                }
            }
        }
        if (OB8(p, OBJ_routine) == 0x0C && r32(p) == NATIVE_S1_BASE + 0x78 * 4) {
            SET8(p, CK_DEAD, 1);
            ck_fragment_init(o);
            goto fragment;
        }
        if (r32(p) != NATIVE_S1_BASE + 0x78 * 4                   /* head destroyed */
            || OB8(p, OBJ_routine) == 0x0A) {
            SET8(o, OBJ_routine, 0x0A);
        }
        o_touch(o);
        o_draw(o);
        return;
    }
    case 0x0C:
    fragment: {                                /* Cat_Fragment */
        if (OB8(o, OBJ_coll_flags) == CK_BODY_COLL && ck_touched(o)) { /* still dangerous */ }
        o_fall(o);
        if (OBS16(o, OBJ_y_vel) >= 0) {
            int16_t f = o_floor_dist(o);
            if (f < 0) {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + f);
                SET16(o, OBJ_y_vel, -0x400);
            }
        }
        if (!(OB8(o, OBJ_render) & 0x80)) { ck_chk_gone(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
    }
}

void s1_obj_mz_register(TriObjFn *t)
{
    t[0x13] = lava_maker;
    t[0x14] = lava_ball;
    t[0x2F] = lgrass;
    t[0x30] = glass_block;
    t[0x31] = chain_stomp;
    t[0x32] = button;
    t[0x33] = push_block;
    t[0x35] = grass_fire;
    t[0x46] = brick;
    t[0x4C] = geyser_maker;
    t[0x4D] = lava_geyser;
    t[0x4E] = lava_wall;
    t[0x51] = smash_block;
    t[0x52] = moving_block;
    t[0x53] = collapse_floor;
    t[0x54] = lava_tag;
    t[0x55] = basaran;
    t[0x78] = caterkiller;
    (void)trig;
}
