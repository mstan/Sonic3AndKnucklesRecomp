/*
 * s1_obj_boss.c — Sonic 1 bosses. Field offsets follow S1 (obBossX $30.l,
 * parent $34.l, obBossY $38.l, $3C timer, $3E flash, $3F sway angle), all of
 * which are free scratch in the S3K table; hits are collision_property and
 * S3K's Touch_Enemy applies them exactly as S1's ReactToItem did.
 */
#include "trilogy_obj.h"
#include <stdlib.h>

#define R2        0x2D
#define BOSS_X    0x30
#define PARENT    0x34
#define BOSS_Y    0x38
#define BTIME     0x3C
#define FLASH     0x3E
#define SWAY      0x3F

#define FLASH_COLOUR  (RAM_Normal_palette + 0x20 + 11 * 2)   /* S1 line1 idx1, permuted */

static uint32_t parent_of(uint32_t o) { return 0xFF0000u | OB16(o, PARENT + 2); }

static void boss_move(uint32_t o)
{
    SET32(o, BOSS_X, r32(o + BOSS_X) + ((int32_t)OBS16(o, OBJ_x_vel) << 8));
    SET32(o, BOSS_Y, r32(o + BOSS_Y) + ((int32_t)OBS16(o, OBJ_y_vel) << 8));
}

/* BossDefeated: an explosion every 8 frames around the boss */
void s1_boss_defeated(uint32_t o)
{
    if (r8(RAM_Level_frame_counter + 1) & 7) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);                  /* no animal, no break sound */
    o_sfx(0xB4);                               /* sfx_Bomb (S3K: boss explosion) */
    int r = o_random();
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((r & 0xFF) >> 2) - 0x20);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + (((r >> 8) & 0xFF) >> 3));
}

static void zone_music(void)
{
    tri_music_zone();
}

/* Shared ship tail: sway, hit flash, defeat check (loc_177E6) */
static void ship_common(uint32_t o)
{
    int16_t s, c;
    o_sincos(OB8(o, SWAY), &s, &c);
    SET16(o, OBJ_y_pos, OB16(o, BOSS_Y) + (s >> 6));
    SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
    SET8(o, SWAY, OB8(o, SWAY) + 2);
    if (OB8(o, R2) >= 8) return;
    if (OB8(o, OBJ_status) & 0x80) {           /* defeated */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, R2, 8);
        SET16(o, BTIME, 0xB3);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, FLASH)) {
        SET8(o, FLASH, 0x20);
        o_sfx(0x6E);                           /* sfx_BossHit */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0x0EEE);
    SET8(o, FLASH, OB8(o, FLASH) - 1);
    if (!OB8(o, FLASH))
        SET8(o, OBJ_coll_flags, 0x0F);
}

/* ------------------------------------------------------------------------
 * Object 3D - Green Hill boss
 * ---------------------------------------------------------------------- */
#define GHZ_X   0x2960
#define GHZ_Y   0x300
#define GHZ_END (GHZ_X + 0x160)

static void bghz_display(uint32_t o)
{
    uint32_t p = parent_of(o);
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    o_animate(o, o_asset_anim(S1A_EGGMAN));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    o_draw(o);
}

static void boss_ghz(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_upload_asset(S1A_EGGMAN);
        o_upload_asset(S1A_BOSSITEMS);
        o_upload_asset(S1A_PRISON);
        static const uint8_t data[6] = { 2, 0, 4, 1, 6, 7 };
        uint32_t a1 = o;
        for (int i = 0; i < 3; i++) {
            if (i) { a1 = o_alloc_after(o); if (!a1) break; }
            SET8(a1, OBJ_routine, data[i * 2]);
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x3D * 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            o_init(a1, S1A_EGGMAN, 4, 3, 0x20, 0x20);
            SET8(a1, OBJ_anim, data[i * 2 + 1]);
            SET32(a1, PARENT, RAMPTR(o));
        }
        SET32(o, BOSS_X, (uint32_t)OB16(o, OBJ_x_pos) << 16);
        SET32(o, BOSS_Y, (uint32_t)OB16(o, OBJ_y_pos) << 16);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
    }
    /* fall through */
    case 2:
        switch (OB8(o, R2)) {
        case 0:   /* BGHZ_ShipStart */
            SET16(o, OBJ_y_vel, 0x100);
            boss_move(o);
            if (OB16(o, BOSS_Y) == GHZ_Y + 0x38) {
                SET16(o, OBJ_y_vel, 0);
                SET8(o, R2, 2);
            }
            break;
        case 2:   /* BGHZ_MakeBall */
            SET16(o, OBJ_x_vel, -0x100);
            SET16(o, OBJ_y_vel, -0x40);
            boss_move(o);
            if (OB16(o, BOSS_X) == GHZ_X + 0xA0) {
                SET16(o, OBJ_x_vel, 0);
                SET16(o, OBJ_y_vel, 0);
                SET8(o, R2, 4);
                uint32_t a1 = o_alloc_after(o);
                if (a1) {
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x48 * 4);
                    SET16(a1, OBJ_x_pos, OB16(o, BOSS_X));
                    SET16(a1, OBJ_y_pos, OB16(o, BOSS_Y));
                    SET32(a1, PARENT, RAMPTR(o));
                }
                SET16(o, BTIME, 0x77);
            }
            break;
        case 4: { /* BGHZ_ShipMove */
            int16_t t = (int16_t)(OBS16(o, BTIME) - 1);
            SET16(o, BTIME, t);
            if (t < 0) {
                SET8(o, R2, 6);
                SET16(o, BTIME, 0x3F);
                SET16(o, OBJ_x_vel, 0x100);
                if (OB16(o, BOSS_X) == GHZ_X + 0xA0) {
                    SET16(o, BTIME, 0x7F);
                    SET16(o, OBJ_x_vel, 0x40);
                }
            }
            if (!(OB8(o, OBJ_status) & 1))
                SET16(o, OBJ_x_vel, -OBS16(o, OBJ_x_vel));
            break;
        }
        case 6: { /* BGHZ_ChgDir */
            int16_t t = (int16_t)(OBS16(o, BTIME) - 1);
            SET16(o, BTIME, t);
            if (t >= 0) boss_move(o);
            else {
                SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
                SET16(o, BTIME, 0x3F);
                SET8(o, R2, 4);
                SET16(o, OBJ_x_vel, 0);
            }
            break;
        }
        case 8: { /* BGHZ_Explode */
            int16_t t = (int16_t)(OBS16(o, BTIME) - 1);
            SET16(o, BTIME, t);
            if (t >= 0) { s1_boss_defeated(o); break; }
            SET8(o, OBJ_status, (OB8(o, OBJ_status) | 1) & 0x7F);
            SET16(o, OBJ_x_vel, 0);
            SET8(o, R2, 10);
            SET16(o, BTIME, (uint16_t)-0x26);
            break;
        }
        case 10: { /* BGHZ_Recover */
            int16_t t = (int16_t)(OBS16(o, BTIME) + 1);
            SET16(o, BTIME, t);
            if (t < 0) SET16(o, OBJ_y_vel, OBS16(o, OBJ_y_vel) + 0x18);
            else if (t == 0) SET16(o, OBJ_y_vel, 0);
            else if (t < 0x30) SET16(o, OBJ_y_vel, OBS16(o, OBJ_y_vel) - 8);
            else if (t == 0x30) { SET16(o, OBJ_y_vel, 0); zone_music(); }
            else if (t >= 0x38) SET8(o, R2, 12);
            boss_move(o);
            break;
        }
        default: { /* BGHZ_Escape */
            SET16(o, OBJ_x_vel, 0x400);
            SET16(o, OBJ_y_vel, -0x40);
            w8(RAM_Scroll_lock, 0);
            if (r16(RAM_Camera_max_X_pos) != GHZ_END)
                w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
            else if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            boss_move(o);
            break;
        }
        }
        ship_common(o);
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
        o_touch(o);
        o_draw(o);
        return;
    case 4: { /* BGHZ_FaceMain */
        uint32_t p = parent_of(o);
        int d0 = OB8(p, R2) - 4, d1 = 1;
        if (d0 == 0) {
            if (OB16(p, BOSS_X) == GHZ_X + 0xA0) d1 = 4;
            goto chk;
        }
        d0 -= 6;
        if (d0 >= 0) { d1 = 0x0A; goto set; }
    chk:
        if (!OB8(p, OBJ_coll_flags)) d1 = 5;
        else if (r8(RAM_Player_1 + OBJ_routine) >= 4) d1 = 4;
    set:
        SET8(o, OBJ_anim, d1);
        if (d0 - 2 == 0) {
            SET8(o, OBJ_anim, 6);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        }
        if (!r32(p)) { o_delete(o); return; }
        bghz_display(o);
        return;
    }
    case 6: { /* BGHZ_FlameMain */
        uint32_t p = parent_of(o);
        if (!r32(p)) { o_delete(o); return; }
        SET8(o, OBJ_anim, 7);
        if (OB8(p, R2) == 12) {
            SET8(o, OBJ_anim, 0x0B);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        } else if (OB16(p, OBJ_x_vel))
            SET8(o, OBJ_anim, 8);
        bghz_display(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 48 - Eggman's swinging ball (GHZ). The base hangs under the ship;
 * five chain links and the ball place themselves from the base's angle
 * (S1 Swing_Move2), each growing to its slot radius as in GBall_Base.
 * ---------------------------------------------------------------------- */
#define GB_BASEY   0x38     /* objoff_38: base y */
#define GB_BASEX   0x3A     /* objoff_3A: base x */
#define GB_RADIUS  0x3C
#define GB_DIR     0x3D
#define GB_SPEED   0x3E     /* word */
#define GB_DROP    0x32     /* word */
#define GB_BASE    0x40     /* child -> base (word) */
#define GB_INDEX   0x42     /* child index 0..5 */
#define GB_TICKS   0x43
static const uint8_t gball_pos[6] = { 0, 0x10, 0x20, 0x30, 0x40, 0x60 };

static void gball_follow(uint32_t o)        /* sub_17C2A */
{
    uint32_t p = parent_of(o);
    SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 0x20);
    if (OB8(o, OBJ_anim_frame) == 0) SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
    SET16(o, GB_BASEX, OB16(p, OBJ_x_pos));
    SET16(o, GB_BASEY, OB16(p, OBJ_y_pos) + OB16(o, GB_DROP));
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    if (OB8(p, OBJ_status) & 0x80) {
        SET32(o, OBJ_code, S3K_Obj_Explosion);
        SET8(o, OBJ_routine, 6);                  /* no animal, no break sound */
        o_sfx(0xB4);                               /* sfx_Bomb (S3K: boss explosion) */
    }
}

static void gball_place(uint32_t o, uint32_t base)
{
    int16_t s, c;
    o_sincos(OB8(base, OBJ_angle), &s, &c);
    int r = OB8(o, GB_RADIUS);
    SET16(o, OBJ_y_pos, OB16(base, GB_BASEY) + ((r * s) >> 8));
    SET16(o, OBJ_x_pos, OB16(base, GB_BASEX) + ((r * c) >> 8));
}

static void boss_ball(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_angle, 0x4080);
        SET16(o, GB_SPEED, (uint16_t)-0x200);
        o_init(o, S1A_BOSSITEMS, 4, 6, 8, 8);
        for (int i = 1; i < 6; i++) {
            uint32_t a1 = o_alloc_after(o);
            if (!a1) break;
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x48 * 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, GB_BASE, o & 0xFFFF);
            SET8(a1, GB_INDEX, i);
            SET32(a1, PARENT, r32(o + PARENT));
            if (i < 5) {
                SET8(a1, OBJ_routine, 6);
                o_init(a1, S1A_SWINGCHAIN, 4, 6, 8, 8);
                SET8(a1, OBJ_frame, 1);
            } else {
                SET8(a1, OBJ_routine, 8);
                o_init(a1, S1A_GBALL, 4, 5, 0x18, 0x18);
                SET8(a1, OBJ_frame, 1);
                SET8(a1, OBJ_coll_flags, 0x81);
            }
        }
        return;
    }
    case 2:   /* GBall_Base */
        if (OB8(o, GB_TICKS) < 0x60) SET8(o, GB_TICKS, OB8(o, GB_TICKS) + 1);
        else if (OB8(parent_of(o), R2) == 6) SET8(o, OBJ_routine, 4);
        if (OB16(o, GB_DROP) != 0x20) SET16(o, GB_DROP, OB16(o, GB_DROP) + 1);
        gball_follow(o);
        o_draw(o);
        return;
    case 4: { /* GBall_Base2: Obj48_Move */
        gball_follow(o);
        int16_t v = OBS16(o, GB_SPEED);
        if (!OB8(o, GB_DIR)) {
            v += 8;
            if (v == 0x200) SET8(o, GB_DIR, 1);
        } else {
            v -= 8;
            if (v == -0x200) SET8(o, GB_DIR, 0);
        }
        SET16(o, GB_SPEED, v);
        SET16(o, OBJ_angle, OB16(o, OBJ_angle) + v);
        o_draw(o);
        return;
    }
    case 6:   /* GBall_Link */
    case 8: { /* GBall_Ball */
        uint32_t base = 0xFF0000u | OB16(o, GB_BASE);
        uint32_t boss = parent_of(o);
        uint8_t target = gball_pos[OB8(o, GB_INDEX)];
        if (OB8(o, GB_RADIUS) != target) SET8(o, GB_RADIUS, OB8(o, GB_RADIUS) + 1);
        if (!r32(base)) { o_delete(o); return; }
        gball_place(o, base);
        if (OB8(o, OBJ_routine) == 8) {
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) ? 0 : 1);
            if (OB8(boss, OBJ_status) & 0x80) {
                SET8(o, OBJ_coll_flags, 0);
                s1_boss_defeated(o);
                SET8(o, GB_TICKS, OB8(o, GB_TICKS) - 1);
                if ((int8_t)OB8(o, GB_TICKS) < 0) {
                    SET32(o, OBJ_code, S3K_Obj_Explosion);
                    SET8(o, OBJ_routine, 6);                  /* no animal, no break sound */
                    o_sfx(0xB4);                               /* sfx_Bomb (S3K: boss explosion) */
                }
            }
            o_touch(o);
        } else if (OB8(boss, OBJ_status) & 0x80) {
            SET32(o, OBJ_code, S3K_Obj_Explosion);
            SET8(o, OBJ_routine, 6);                  /* no animal, no break sound */
            o_sfx(0xB4);                               /* sfx_Bomb (S3K: boss explosion) */
        }
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 3E - prison capsule (transcribed). Subtype 0 = body, 1 = switch.
 * Its art is boss art (uploaded with the boss), so it stays hidden until
 * then. The act ends through the Sonic 3 results, as the signpost does.
 * ---------------------------------------------------------------------- */
#define PRI_ORIGY   0x30        /* pri_origY */
#define PRI_TIME    0x1E        /* obTimeFrame (free word in the S3K table) */

static int s_prison_open;       /* v_bossstatus = 2 */

static const uint8_t pri_var[4][4] = {     /* routine, width, priority, frame */
    { 2, 0x20, 4, 0 }, { 4, 0x0C, 5, 1 }, { 6, 0x10, 4, 3 }, { 8, 0x10, 3, 5 },
};

int s1_animals_alive(void)
{
    for (uint32_t s = 0; s < 110; s++)
        if (r32(0xFFB000u + s * 0x4Au) == S3K_Obj_Animal) return 1;
    return 0;
}

void s1_spawn_animal(uint32_t o, int dx, int delay)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Animal);
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + dx));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, 0x38, 1);                         /* capsule animal: wait, no score */
    SET16(a1, 0x36, (uint16_t)delay);
}

static void unstand_players(uint32_t o)
{
    for (int n = 0; n < 2; n++) {
        if (!(OB8(o, OBJ_status) & (8 << n))) continue;
        uint32_t p = o_player(n);
        w8(p + OBJ_status, (uint8_t)((r8(p + OBJ_status) & ~8) | 2));
    }
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~0x18);
}

static void prison(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                  /* Pri_Main */
        int sub = OB8(o, OBJ_subtype) & 3;
        if (sub == 0) s_prison_open = 0;
        SET16(o, PRI_ORIGY, OB16(o, OBJ_y_pos));
        o_init(o, S1A_PRISON, 4, pri_var[sub][2], pri_var[sub][1], 0x20);
        SET8(o, OBJ_routine, pri_var[sub][0]);
        SET8(o, OBJ_frame, pri_var[sub][3]);
        if (sub == 2) { SET8(o, OBJ_coll_flags, 6); SET8(o, OBJ_coll_prop, 8); }
        return;
    }
    case 2:                                    /* Pri_BodyMain */
        if (!s_prison_open) {
            o_solid(o, 0x2B, 0x18, 0x18, OB16(o, OBJ_x_pos));
        } else {
            unstand_players(o);
            SET8(o, OBJ_frame, 2);             /* destroyed prison */
        }
        break;
    case 4:                                    /* Pri_Switched */
        o_solid(o, 0x17, 8, 8, OB16(o, OBJ_x_pos));
        o_animate(o, o_asset_anim(S1A_PRISON));
        SET16(o, OBJ_y_pos, OB16(o, PRI_ORIGY));
        if (OB8(o, OBJ_status) & 0x18) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
            SET8(o, OBJ_routine, 0x0A);
            SET16(o, PRI_TIME, 60);
            w8(RAM_Update_HUD_timer, 0);       /* stop time counter */
            w8(RAM_Scroll_lock, 0);            /* clr.b (f_lockscreen) */
            unstand_players(o);
        }
        break;
    case 6: case 8: case 0x0A:                 /* Pri_Explosion */
        if (!(r8(RAM_Level_frame_counter + 1) & 7)) {
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, S3K_Obj_Explosion);
                SET8(a1, OBJ_routine, 6);                  /* no animal, no break sound */
                o_sfx(0xB4);                               /* sfx_Bomb (S3K: boss explosion) */
                int r = o_random();
                SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + ((r & 0xFF) >> 2) - 0x20));
                SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + (((r >> 8) & 0xFF) >> 3)));
            }
        }
        SET16(o, PRI_TIME, OB16(o, PRI_TIME) - 1);
        if (OB16(o, PRI_TIME) == 0) {          /* .makeanimal */
            s_prison_open = 1;
            SET8(o, OBJ_routine, 0x0C);
            SET8(o, OBJ_frame, 6);
            SET16(o, PRI_TIME, 150);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x20);
            for (int i = 0, dx = -0x1C, d5 = 0x9A; i < 8; i++, dx += 7, d5 -= 8)
                s1_spawn_animal(o, dx, d5);
        }
        break;
    case 0x0C:                                 /* Pri_Animals */
        if (!(r8(RAM_Level_frame_counter + 1) & 7)) {
            int r = o_random();
            int d0 = (r & 0x1F) - 6;
            if ((int32_t)D(1) < 0) d0 = -d0;
            s1_spawn_animal(o, d0, 0x0C);
        }
        SET16(o, PRI_TIME, OB16(o, PRI_TIME) - 1);
        if (OB16(o, PRI_TIME) == 0) {
            SET8(o, OBJ_routine, 0x0E);
            SET16(o, PRI_TIME, 180);
        }
        break;
    case 0x0E:                                 /* Pri_EndAct -> Obj_EndSignResults */
        if (!s1_animals_alive() && !(r8(RAM_Player_1 + OBJ_status) & 2)) {
            SET8(o, OBJ_routine, 0x10);
            A(1) = RAMPTR(RAM_Player_1);
            A(0) = RAMPTR(o);
            tri_jsr(S3K_Set_PlayerEndingPose);
            uint32_t a1 = o_alloc();
            if (a1) SET32(a1, OBJ_code, S3K_Obj_LevelResults);
        }
        break;
    case 0x10:                                 /* Obj_EndSignAfter */
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Check_TailsEndPose);
        break;
    }
    if (OB8(o, OBJ_routine) <= 4 && o_out_of_range(o, OB16(o, OBJ_x_pos))) {
        o_delete_respawnable(o);
        return;
    }
    if (o_asset_ready(S1A_PRISON))
        o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 73 - Marble Zone boss; Object 74 - the lava it drops
 * ---------------------------------------------------------------------- */
#define MZ_X    0x1800
#define MZ_Y    0x210
#define MZ_END  (MZ_X + 0x160)
#define BLAVA   0x34              /* ship: lava-ball timer (byte) */
#define SHIELD_FIRE_BIT 0x10

static int s_bossstatus;          /* v_bossstatus */

/* loc_1833E: ship position, hit flash and defeat check */
static void bmz_common(uint32_t o)
{
    SET16(o, OBJ_y_pos, OB16(o, BOSS_Y));
    SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
    if (OB8(o, R2) >= 4) return;
    if (OB8(o, OBJ_status) & 0x80) {           /* loc_18392 */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, R2, 4);
        SET16(o, BTIME, 0xB4);
        SET16(o, OBJ_x_vel, 0);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, FLASH)) {
        SET8(o, FLASH, 0x28);
        o_sfx(0x6E);                           /* sfx_HitBoss */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0x0EEE);
    SET8(o, FLASH, OB8(o, FLASH) - 1);
    if (!OB8(o, FLASH))
        SET8(o, OBJ_coll_flags, 0x0F);
}

static void bmz_make_lava(uint32_t o)            /* BossMarble_MakeLava */
{
    uint8_t t = OB8(o, BLAVA);
    SET8(o, BLAVA, t - 1);
    if (t != 0) return;                        /* subq / bcc */
    uint32_t a1 = o_alloc();
    if (a1) {
        SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x14 * 4);
        SET16(a1, OBJ_y_pos, MZ_Y + 0xD8);
        SET16(a1, OBJ_x_pos, (uint16_t)((o_random() & 0xFFFF) % 0x50 + MZ_X + 0x78));
        SET8(a1, OBJ_subtype, 0);
        SET8(a1, 0x29, 0xFF);                  /* boss lava ball: drawn further back */
    }
    SET8(o, BLAVA, (uint8_t)((o_random() & 0x1F) + 0x40));
}

static void bmz_chgdir(uint32_t o)               /* BMZ_ChgDir */
{
    if (!OB16(o, OBJ_x_vel)) {
        if (OB16(o, BOSS_Y) != MZ_Y + 0x1C) {
            SET16(o, OBJ_y_vel, OB16(o, BOSS_Y) < MZ_Y + 0x1C ? 0x40 : -0x40);
            boss_move(o);
            return;
        }
        SET16(o, OBJ_x_vel, 0x200);            /* loc_183E6 */
        SET16(o, OBJ_y_vel, 0x100);
        if (!(OB8(o, OBJ_status) & 1)) SET16(o, OBJ_x_vel, -0x200);
    }
    if (OB8(o, FLASH) < 0x18) {                /* loc_183FE */
        boss_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 4);
    }
    bmz_make_lava(o);
    if (OB8(o, OBJ_status) & 1) {              /* loc_1845C */
        if ((int16_t)OB16(o, BOSS_X) < MZ_X + 0x110) return;
        SET16(o, BOSS_X, MZ_X + 0x110);
    } else {
        if ((int16_t)OB16(o, BOSS_X) > MZ_X + 0x30) return;
        SET16(o, BOSS_X, MZ_X + 0x30);
    }
    SET16(o, OBJ_x_vel, 0);                    /* loc_18482 */
    SET16(o, OBJ_y_vel, OB16(o, BOSS_Y) >= MZ_Y + 0x1C ? -0x180 : 0x180);
    SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 2);
}

static void bmz_dropfire(uint32_t o)             /* BMZ_DropFire */
{
    boss_move(o);
    if ((int16_t)(OB16(o, BOSS_Y) - (MZ_Y + 0x1C)) > 0) return;
    if (OB16(o, OBJ_y_vel)) {
        SET16(o, OBJ_y_vel, 0);
        SET16(o, BTIME, 0x50);
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        uint32_t a1 = o_alloc();
        if (a1) {
            SET16(a1, OBJ_x_pos, OB16(o, BOSS_X));
            SET16(a1, OBJ_y_pos, OB16(o, BOSS_Y) + 0x18);
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x74 * 4);
            SET8(a1, OBJ_subtype, 1);
        }
    }
    SET16(o, BTIME, OB16(o, BTIME) - 1);
    if (!OB16(o, BTIME)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 2);
}

static void bmz_child_display(uint32_t o)        /* loc_1864A */
{
    uint32_t p = parent_of(o);
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    o_draw(o);
}

static void boss_mz(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t data[4][3] = { { 2, 0, 4 }, { 4, 1, 4 }, { 6, 7, 4 }, { 8, 0, 3 } };
        o_upload_asset(S1A_EGGMAN);
        o_upload_asset(S1A_BOSSITEMS2);
        o_upload_asset(S1A_PRISON);
        SET32(o, BOSS_X, (uint32_t)OB16(o, OBJ_x_pos) << 16);
        SET32(o, BOSS_Y, (uint32_t)OB16(o, OBJ_y_pos) << 16);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        s_bossstatus = 0;
        uint32_t a1 = o;
        for (int i = 0; i < 4; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x73 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            SET8(a1, R2, 0);
            SET8(a1, OBJ_routine, data[i][0]);
            SET8(a1, OBJ_anim, data[i][1]);
            o_init(a1, S1A_EGGMAN, 4, data[i][2], 0x20, 0x20);
            SET32(a1, PARENT, RAMPTR(o));
        }
    }
    /* fall through */
    case 2:                                    /* BossMarble_ShipMain */
        switch (OB8(o, R2)) {
        case 0: {                              /* BMZ_ShipStart */
            uint8_t a = OB8(o, SWAY);
            SET8(o, SWAY, a + 2);
            int16_t s, c;
            o_sincos(a, &s, &c);
            SET16(o, OBJ_y_vel, s >> 2);
            SET16(o, OBJ_x_vel, -0x100);
            boss_move(o);
            if (OB16(o, BOSS_X) == MZ_X + 0x110) {
                SET8(o, R2, 2);
                SET8(o, OBJ_subtype, 0);
                SET16(o, OBJ_x_vel, 0);
                SET16(o, OBJ_y_vel, 0);
            }
            SET8(o, BLAVA, (uint8_t)o_random());
            bmz_common(o);
            break;
        }
        case 2:                                /* BMZ_ShipMove */
            if (OB8(o, OBJ_subtype) & 2) bmz_dropfire(o); else bmz_chgdir(o);
            SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 6);
            bmz_common(o);
            break;
        case 4: {                              /* BMZ_Explode */
            int16_t t = (int16_t)(OBS16(o, BTIME) - 1);
            SET16(o, BTIME, t);
            if (t >= 0) { s1_boss_defeated(o); break; }
            SET8(o, OBJ_status, (OB8(o, OBJ_status) | 1) & 0x7F);
            SET16(o, OBJ_x_vel, 0);
            SET8(o, R2, 6);
            SET16(o, BTIME, (uint16_t)-0x26);
            if (!s_bossstatus) {
                s_bossstatus = 1;
                SET16(o, OBJ_y_vel, 0);
            }
            break;
        }
        case 6: {                              /* BMZ_Recover */
            int16_t t = (int16_t)(OBS16(o, BTIME) + 1);
            SET16(o, BTIME, t);
            if (t == 0) {
                SET16(o, OBJ_y_vel, 0);
            } else if (t < 0) {
                if (OB16(o, BOSS_Y) >= MZ_Y + 0x60) { SET16(o, OBJ_y_vel, 0); SET16(o, BTIME, 0); }
                else SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            } else if (t < 0x30) {
                SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
            } else if (t == 0x30) {
                SET16(o, OBJ_y_vel, 0);
                zone_music();
            } else if (t >= 0x38) {
                SET8(o, R2, 8);
            }
            boss_move(o);
            bmz_common(o);
            break;
        }
        default:                               /* BMZ_Escape */
            SET16(o, OBJ_x_vel, 0x500);
            SET16(o, OBJ_y_vel, -0x40);
            w8(RAM_Scroll_lock, 0);
            if (r16(RAM_Camera_max_X_pos) < MZ_END)
                w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
            else if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            boss_move(o);
            bmz_common(o);
            break;
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
        o_touch(o);
        o_draw(o);
        return;
    case 4: {                                  /* BossMarble_FaceMain */
        uint32_t p = parent_of(o);
        if (!r32(p)) { o_delete(o); return; }
        int d0 = OB8(p, R2) - 2, d1 = 1;
        if (d0 == 0) {
            if ((OB8(p, OBJ_subtype) & 2) && !OB16(p, OBJ_y_vel)) { d1 = 4; goto set; }
            goto chk;
        }
        d0 -= 2;
        if (d0 >= 0) { d1 = 0x0A; goto set; }
    chk:
        if (!OB8(p, OBJ_coll_flags)) d1 = 5;
        else if (r8(RAM_Player_1 + OBJ_routine) >= 4) d1 = 4;
    set:
        SET8(o, OBJ_anim, d1);
        if (d0 - 4 == 0) {
            SET8(o, OBJ_anim, 6);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        bmz_child_display(o);
        return;
    }
    case 6: {                                  /* BossMarble_FlameMain */
        uint32_t p = parent_of(o);
        if (!r32(p)) { o_delete(o); return; }
        SET8(o, OBJ_anim, 7);
        if (OB8(p, R2) >= 8) {
            SET8(o, OBJ_anim, 0x0B);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        } else if (OB16(p, OBJ_x_vel)) {
            SET8(o, OBJ_anim, 8);
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        bmz_child_display(o);
        return;
    }
    case 8: {                                  /* BossMarble_TubeMain */
        uint32_t p = parent_of(o);
        if (!r32(p) || (OB8(p, R2) == 8 && !(OB8(o, OBJ_render) & 0x80))) { o_delete(o); return; }
        o_asset(o, S1A_BOSSITEMS2);
        SET8(o, OBJ_frame, 4);
        bmz_child_display(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* Object 74 - lava dropped by the MZ boss. $29 = timer, R2 = sub-routine. */
#define BF_X    BOSS_X            /* obBossX (word) */
#define BF_X2   0x32
#define BF_Y    BOSS_Y            /* obBossY (word) */
#define BF_TIME 0x29

static void boss_fire_dup2(uint32_t o)           /* BossFire_Duplicate2 */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x74 * 4);
    SET8(a1, OBJ_subtype, 0);
    SET8(a1, BF_TIME, 0x67);
}

static void boss_fire(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_x_radius, 8);
        o_init(o, S1A_FIRE, 4, 5, 8, 8);
        SET8(o, 0x2B, SHIELD_FIRE_BIT);
        SET16(o, BF_Y, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_routine, 2);
        if (!OB8(o, OBJ_subtype)) {
            SET8(o, OBJ_coll_flags, 0x8B);
            SET8(o, OBJ_routine, 4);
            goto tempfire;
        }
        SET8(o, BF_TIME, 0x1E);
        o_sfx(0x70);                           /* sfx_Fireball */
        /* fall through */
    case 2:                                    /* BossFire_Action */
        switch (OB8(o, R2)) {
        case 0:                                /* BossFire_Drop */
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 2);
            SET8(o, BF_TIME, OB8(o, BF_TIME) - 1);
            if ((int8_t)OB8(o, BF_TIME) >= 0) break;
            SET8(o, OBJ_coll_flags, 0x8B);
            SET8(o, OBJ_subtype, 0);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~2);
            if (o_floor_dist(o) < 0) SET8(o, R2, 2);
            break;
        case 2: {                              /* BossFire_MakeFlame */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 2);
            SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
            SET16(o, OBJ_x_vel, 0xA0);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, BF_X, OB16(o, OBJ_x_pos));
            SET16(o, BF_Y, OB16(o, OBJ_y_pos));
            SET8(o, BF_TIME, 3);
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                for (int k = 0; k < OBJ_SIZE; k++) w8(a1 + k, r8(o + k));
                SET16(a1, OBJ_x_vel, -OBS16(a1, OBJ_x_vel));
                SET8(a1, R2, OB8(a1, R2) + 2);
            }
            SET8(o, R2, 4);
            break;
        }
        case 4: {                              /* BossFire_Duplicate */
            if (o_floor_dist(o) >= 0) { SET8(o, R2, 6); break; }
            int16_t x = (int16_t)OB16(o, OBJ_x_pos);
            if (x > MZ_X + 0x140) { SET8(o, OBJ_routine, 4); break; }
            int16_t bx = (int16_t)OB16(o, BF_X);
            if (bx != x && (x & 0x10) != (bx & 0x10)) {
                boss_fire_dup2(o);
                SET16(o, BF_X2, x);
            }
            SET16(o, BF_X, x);
            break;
        }
        case 6: {                              /* BossFire_FallEdge */
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~2);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x24);
            int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, BF_X2));
            if (d0 < 0) d0 = -d0;
            if (d0 == 0x12) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) & 0x7FFF);
            if (o_floor_dist(o) < 0) {
                SET8(o, BF_TIME, OB8(o, BF_TIME) - 1);
                if (!OB8(o, BF_TIME)) { o_delete(o); return; }
                SET16(o, OBJ_y_vel, 0);
                SET16(o, OBJ_x_pos, OB16(o, BF_X2));
                SET16(o, OBJ_y_pos, OB16(o, BF_Y));
                SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
                SET8(o, R2, 4);
            }
            break;
        }
        }
        if (OB8(o, OBJ_routine) == 2) {
            o_move(o);
            o_animate(o, o_asset_anim(S1A_FIRE));
            if (OB16(o, OBJ_y_pos) > MZ_Y + 0xD8) { o_delete(o); return; }
            o_touch(o);
            o_draw(o);
            return;
        }
        /* fall through when switched to TempFire */
    case 4:
    tempfire:                                  /* BossFire_TempFire */
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, BF_TIME, OB8(o, BF_TIME) - 1);
        if (!OB8(o, BF_TIME)) {
            SET8(o, OBJ_anim, 1);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
            SET8(o, OBJ_coll_flags, 0);
        }
        o_animate(o, o_asset_anim(S1A_FIRE));
        if (OB8(o, OBJ_routine) == 6) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    default:                                   /* BossFire_TempFireDel */
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 75 - Spring Yard Zone boss; Object 76 - the blocks he lifts.
 * S1's objoff_29 (carrying flag on the ship, state on a block) collides with
 * S3K's collision_property (= obBossHits here), so it lives at $40. Ship:
 * $34 byte = column under the ship, $36 word = block being lifted.
 * ---------------------------------------------------------------------- */
#define SYZ_X    0x2C00
#define SYZ_Y    0x4CC
#define SYZ_END  (SYZ_X + 0x140)
#define BSYZ_COL   0x34
#define BSYZ_BLOCK 0x36
#define BSYZ_CARRY 0x40
#define BSYZ_SHAKE 0x3D           /* low byte of the $3C timer */

static uint32_t bsyz_block(uint32_t o)
{
    uint16_t b = OB16(o, BSYZ_BLOCK);
    return b ? 0xFF0000u | b : 0;
}

/* loc_19202: column, hit flash and defeat check */
static void bsyz_tail(uint32_t o)
{
    SET8(o, BSYZ_COL, (uint8_t)((uint16_t)(OB16(o, OBJ_x_pos) - SYZ_X) >> 5));
    if (OB8(o, R2) >= 6) return;
    if (OB8(o, OBJ_status) & 0x80) {           /* loc_19258 */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, R2, 6);
        SET16(o, BTIME, 0xB4);
        SET16(o, OBJ_x_vel, 0);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, FLASH)) {
        SET8(o, FLASH, 0x20);
        o_sfx(0x6E);                           /* sfx_HitBoss */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0x0EEE);
    SET8(o, FLASH, OB8(o, FLASH) - 1);
    if (!OB8(o, FLASH))
        SET8(o, OBJ_coll_flags, 0x0F);
}

static void bsyz_move_tail(uint32_t o)          /* loc_191F2 */
{
    boss_move(o);
    SET16(o, OBJ_y_pos, OB16(o, BOSS_Y));
    SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
    bsyz_tail(o);
}

static void bsyz_sway(uint32_t o)               /* loc_191DE */
{
    uint8_t a = OB8(o, SWAY);
    SET8(o, SWAY, a + 2);
    int16_t s, c;
    o_sincos(a, &s, &c);
    SET16(o, OBJ_y_vel, s >> 2);
    bsyz_move_tail(o);
}

static void bsyz_find_block(uint32_t o)         /* BossSpringYard_FindBlocks */
{
    SET16(o, BSYZ_BLOCK, 0);
    uint8_t col = OB8(o, BSYZ_COL);
    for (int i = 0; i < 110; i++) {
        uint32_t a1 = o_slot(i);
        if (r32(a1) != NATIVE_S1_BASE + 0x76 * 4 || OB8(a1, OBJ_subtype) != col) continue;
        if (OB8(a1, OBJ_routine) != 2) continue;   /* not a falling fragment */
        SET16(o, BSYZ_BLOCK, a1 & 0xFFFF);
        return;
    }
}

static void bsyz_child_display(uint32_t o)      /* loc_195DA */
{
    uint32_t p = parent_of(o);
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    o_draw(o);
}

static void bsyz_child_anim(uint32_t o)         /* loc_195BE */
{
    uint32_t p = parent_of(o);
    o_animate(o, o_asset_anim(S1A_EGGMAN));
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    bsyz_child_display(o);
}

static void bsyz_attack(uint32_t o)             /* BSYZ_Attack */
{
    uint32_t blk = bsyz_block(o);
    switch (OB8(o, OBJ_subtype)) {
    case 0:                                    /* BSYZ_Descend */
        SET16(o, OBJ_y_vel, 0x180);
        if (OB16(o, BOSS_Y) >= SYZ_Y + 0x8A) {
            SET16(o, BOSS_Y, SYZ_Y + 0x8A);
            SET16(o, BTIME, 0);
            if (blk) {
                SET8(blk, BSYZ_CARRY, 0xFF);
                SET8(o, BSYZ_CARRY, 0xFF);
                SET32(blk, PARENT, RAMPTR(o));
                SET16(o, BTIME, 0x32);
            }
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_subtype, 2);
        }
        bsyz_move_tail(o);
        return;
    case 2: {                                  /* BSYZ_Lift */
        int d0 = 0;
        SET16(o, BTIME, OB16(o, BTIME) - 1);
        if ((int16_t)OB16(o, BTIME) < 0) {
            SET8(o, OBJ_subtype, 4);
            SET16(o, OBJ_y_vel, -0x800);
            if (!blk) SET16(o, OBJ_y_vel, -0x400);
        } else if ((int16_t)OB16(o, BTIME) <= 0x1E) {
            d0 = (OB8(o, BSYZ_SHAKE) & 2) ? -2 : 2;
        }
        SET16(o, OBJ_y_pos, OB16(o, BOSS_Y) + d0);
        SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
        bsyz_tail(o);
        return;
    }
    case 4: {                                  /* BSYZ_LiftStop */
        int16_t d0 = SYZ_Y + 0x0E;
        if (blk) d0 -= 0x18;
        if (d0 < (int16_t)OB16(o, BOSS_Y)) {
            if ((int16_t)OB16(o, OBJ_y_vel) < -0x40)
                SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x0C);
        } else {
            SET16(o, BTIME, blk ? 0x2D : 8);
            SET8(o, OBJ_subtype, 6);
            SET16(o, OBJ_y_vel, 0);
        }
        bsyz_move_tail(o);
        return;
    }
    default: {                                 /* BSYZ_BreakBlock */
        int16_t t = (int16_t)(OBS16(o, BTIME) - 1);
        SET16(o, BTIME, t);
        if (t < 0) {
            if (t == -0x1E) {                  /* loc_193EE */
                SET8(o, BSYZ_CARRY, 0);
                SET8(o, R2, 2);
                SET8(o, BSYZ_SHAKE, 0xFF);
                bsyz_tail(o);
                return;
            }
        } else if (t == 0) {
            if (blk) SET8(blk, BSYZ_CARRY, 0x0A);
            SET16(o, BSYZ_BLOCK, 0);
            blk = 0;
        }
        int d0 = blk ? 2 : 1;                  /* loc_19406 */
        if (OB16(o, BOSS_Y) != SYZ_Y + 0x0E) {
            if ((int16_t)OB16(o, BOSS_Y) > SYZ_Y + 0x0E) d0 = -d0;
            SET16(o, BOSS_Y, OB16(o, BOSS_Y) + d0);
        }
        d0 = 0;                                /* loc_19424 */
        if (blk) d0 = (OB8(o, BSYZ_SHAKE) & 1) ? -2 : 2;
        SET16(o, OBJ_y_pos, OB16(o, BOSS_Y) + d0);
        SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
        bsyz_tail(o);
        return;
    }
    }
}

static void boss_syz(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t data[4][3] = { { 2, 0, 5 }, { 4, 1, 5 }, { 6, 7, 5 }, { 8, 0, 5 } };
        o_upload_asset(S1A_EGGMAN);
        o_upload_asset(S1A_BOSSITEMS2);
        o_upload_asset(S1A_PRISON);
        SET16(o, OBJ_x_pos, SYZ_X + 0x1B0);
        SET16(o, OBJ_y_pos, SYZ_Y + 0x0E);
        SET32(o, BOSS_X, (uint32_t)OB16(o, OBJ_x_pos) << 16);
        SET32(o, BOSS_Y, (uint32_t)OB16(o, OBJ_y_pos) << 16);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        uint32_t a1 = o;
        for (int i = 0; i < 4; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x75 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            SET8(a1, R2, 0);
            SET8(a1, OBJ_routine, data[i][0]);
            SET8(a1, OBJ_anim, data[i][1]);
            o_init(a1, S1A_EGGMAN, 4, data[i][2], 0x20, 0x20);
            SET32(a1, PARENT, RAMPTR(o));
        }
        SET16(o, BSYZ_BLOCK, 0);
        SET8(o, BSYZ_CARRY, 0);
    }
    /* fall through */
    case 2:                                    /* BossSpringYard_ShipMain */
        switch (OB8(o, R2)) {
        case 0:                                /* BSYZ_ShipStart */
            SET16(o, OBJ_x_vel, -0x100);
            if (OB16(o, BOSS_X) < SYZ_X + 0x138) SET8(o, R2, 2);
            bsyz_sway(o);
            break;
        case 2: {                              /* BSYZ_ShipMove */
            uint16_t d0 = OB16(o, BOSS_X);
            int flipdir = 0;
            SET16(o, OBJ_x_vel, 0x140);
            if (!(OB8(o, OBJ_status) & 1)) {
                SET16(o, OBJ_x_vel, -0x140);
                if ((int16_t)d0 <= SYZ_X + 8) flipdir = 1;
            } else if ((int16_t)d0 >= SYZ_X + 0x138) {
                flipdir = 1;
            }
            if (flipdir) {
                SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
                SET8(o, BSYZ_SHAKE, 0);
            }
            int16_t v = (int16_t)(((d0 - (SYZ_X + 0x10)) & 0x1F) - 0x1F);
            if (v < 0) v = (int16_t)-v;
            if (v - 1 <= 0 && !OB8(o, BSYZ_SHAKE)) {
                int16_t col = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - SYZ_X) >> 5;
                if ((uint8_t)col == OB8(o, BSYZ_COL)) {
                    SET16(o, BOSS_X, (uint16_t)(OB8(o, BSYZ_COL) * 32 + SYZ_X + 0x10));
                    bsyz_find_block(o);
                    SET8(o, R2, 4);
                    SET8(o, OBJ_subtype, 0);
                    SET8(o, BSYZ_CARRY, 0);
                    SET16(o, OBJ_x_vel, 0);
                }
            }
            bsyz_sway(o);
            break;
        }
        case 4:
            bsyz_attack(o);
            break;
        case 6: {                              /* BSYZ_Explode */
            int16_t t = (int16_t)(OBS16(o, BTIME) - 1);
            SET16(o, BTIME, t);
            if (t >= 0) { s1_boss_defeated(o); break; }
            SET8(o, R2, 8);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_status, (OB8(o, OBJ_status) | 1) & 0x7F);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, BTIME, 0xFFFF);
            s_bossstatus = 1;
            bsyz_tail(o);
            break;
        }
        case 8: {                              /* BSYZ_Recover */
            int16_t t = (int16_t)(OBS16(o, BTIME) + 1);
            SET16(o, BTIME, t);
            if (t == 0) {
                SET16(o, OBJ_y_vel, 0);
            } else if (t < 0) {
                SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            } else if (t < 0x20) {
                SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
            } else if (t == 0x20) {
                SET16(o, OBJ_y_vel, 0);
                zone_music();
            } else if (t >= 0x2A) {
                SET8(o, R2, 0x0A);
            }
            bsyz_move_tail(o);
            break;
        }
        default:                               /* BSYZ_Escape */
            SET16(o, OBJ_x_vel, 0x400);
            SET16(o, OBJ_y_vel, -0x40);
            w8(RAM_Scroll_lock, 0);
            if (r16(RAM_Camera_max_X_pos) < SYZ_END)
                w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
            else if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            boss_move(o);
            bsyz_sway(o);
            break;
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
        o_touch(o);
        o_draw(o);
        return;
    case 4: {                                  /* BossSpringYard_FaceMain */
        uint32_t p = parent_of(o);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        int d1 = 1;
        switch (OB8(p, R2)) {
        case 6: case 8: d1 = 0x0A; break;
        case 0x0A: d1 = 6; break;
        case 4:
            if (OB8(p, OBJ_subtype) == 2) d1 = 6;
            /* fall through */
        default:
            if (!OB8(p, OBJ_coll_flags)) d1 = 5;
            else if (r8(RAM_Player_1 + OBJ_routine) >= 4) d1 = 4;
            break;
        }
        SET8(o, OBJ_anim, d1);
        bsyz_child_anim(o);
        return;
    }
    case 6: {                                  /* BossSpringYard_FlameMain */
        uint32_t p = parent_of(o);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        SET8(o, OBJ_anim, 7);
        if (OB8(p, R2) == 0x0A) {
            SET8(o, OBJ_anim, 0x0B);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        } else if (OB16(p, OBJ_x_vel)) {
            SET8(o, OBJ_anim, 8);
        }
        bsyz_child_anim(o);
        return;
    }
    case 8: {                                  /* BossSpringYard_SpikeMain */
        uint32_t p = parent_of(o);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        o_asset(o, S1A_BOSSITEMS2);
        SET8(o, OBJ_frame, 5);
        if (OB8(p, R2) == 0x0A && !(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
        int16_t d0 = (int16_t)OB16(o, BTIME);
        int retract = 1;
        if (OB8(p, R2) == 4) {
            uint8_t st = OB8(p, OBJ_subtype);
            if (st == 6) retract = (int16_t)OB16(p, BTIME) < 0;
            else {
                retract = 0;
                if (st == 0 && d0 < 0x94) d0 += 7;
            }
        }
        if (retract && d0 > 0) d0 -= 5;
        SET16(o, BTIME, d0);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + (d0 >> 2));
        SET8(o, OBJ_width_px, 8);
        SET8(o, OBJ_height_px, 0x0C);
        SET8(o, OBJ_coll_flags, 0);
        if (OB8(p, OBJ_coll_flags) && !OB8(p, BSYZ_CARRY))
            SET8(o, OBJ_coll_flags, 0x84);
        o_touch(o);
        bsyz_child_display(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* Object 76 - the row of blocks under the SYZ boss */
static void bsyz_block_break(uint32_t o)        /* BossBlock_Break */
{
    static const int16_t spd[4][2] = { { -0x180, -0x200 }, { 0x180, -0x200 }, { -0x100, -0x100 }, { 0x100, -0x100 } };
    static const int16_t pos[4][2] = { { -8, -8 }, { 0x10, 0 }, { 0, 0x10 }, { 0x10, 0x10 } };
    SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
    SET8(o, OBJ_width_px, 8);
    SET8(o, OBJ_height_px, 8);
    uint32_t a1 = o;
    for (int i = 0; i < 4; i++) {
        if (i) {
            a1 = o_alloc_after(o);
            if (!a1) break;
            for (uint32_t k = 0; k < OBJ_SIZE; k += 2) w16(a1 + k, r16(o + k));
        }
        SET16(a1, OBJ_x_vel, spd[i][0]);
        SET16(a1, OBJ_y_vel, spd[i][1]);
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + pos[i][0]);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + pos[i][1]);
        SET8(a1, OBJ_frame, i + 1);
    }
    o_sfx(0x59);                               /* sfx_WallSmash (S3K: collapse) */
}

static void boss_block(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                  /* BossBlock_Main */
        int x = SYZ_X + 0x10;
        uint32_t a1 = o;
        for (int i = 0; i < 10; i++, x += 0x20) {
            if (i) {
                a1 = o_alloc();
                if (!a1) break;
            }
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x76 * 4);
            o_init(a1, S1A_BOSSBLOCK, 4, 3, 0x10, 0x10);
            SET16(a1, OBJ_x_pos, x);
            SET16(a1, OBJ_y_pos, 0x582);
            SET8(a1, OBJ_subtype, i);
            SET8(a1, BSYZ_CARRY, i);
            SET8(a1, OBJ_routine, 2);
        }
        return;
    }
    case 2: {                                  /* BossBlock_Action */
        uint8_t st = OB8(o, BSYZ_CARRY);
        if (st == OB8(o, OBJ_subtype)) {
            o_solid(o, 0x1B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        } else if (!(st & 0x80)) {
            bsyz_block_break(o);
        } else {
            uint32_t p = 0xFF0000u | OB16(o, PARENT + 2);
            if (!OB8(p, OBJ_coll_prop)) {
                bsyz_block_break(o);
            } else {
                SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
                SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos) + 0x2C);
                if ((p & 0xFFFF) >= (o & 0xFFFF))      /* ship runs after us */
                    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + (OBS16(p, OBJ_y_vel) >> 8));
            }
        }
        o_draw(o);
        return;
    }
    default:                                   /* BossBlock_Frag */
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_fall(o);
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 77 - Labyrinth Zone boss: flees up the shaft as the water rises.
 * $3D = defeated (explosions keep going while he escapes), $3C = wait timer,
 * $3F = sway angle.
 * ---------------------------------------------------------------------- */
#define LZ_X    0x1DE0
#define LZ_Y    0xC0
#define LZ_END  (LZ_X + 0x250)
#define BLZ_DEAD 0x3D
#define BLZ_WAIT 0x3C

static void blz_tail(uint32_t o)                 /* loc_17F48 */
{
    if (OB8(o, BLZ_DEAD)) { s1_boss_defeated(o); return; }
    if (OB8(o, OBJ_status) & 0x80) {
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, BLZ_DEAD, 0xFF);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, FLASH)) {
        SET8(o, FLASH, 0x20);
        o_sfx(0x6E);                           /* sfx_HitBoss */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0x0EEE);
    SET8(o, FLASH, OB8(o, FLASH) - 1);
    if (!OB8(o, FLASH))
        SET8(o, OBJ_coll_flags, 0x0F);
}

static void blz_move_tail(uint32_t o)            /* loc_17F38 */
{
    boss_move(o);
    SET16(o, OBJ_y_pos, OB16(o, BOSS_Y));
    SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
    blz_tail(o);
}

/* clamp helpers for BLZ_ShipMove*: returns 1 when that axis arrived */
static int blz_clamp_x(uint32_t o, uint16_t x)
{
    if (OB16(o, BOSS_X) < x) return 0;
    SET16(o, BOSS_X, x);
    SET16(o, OBJ_x_vel, 0);
    return 1;
}

static int blz_clamp_y(uint32_t o, int16_t y)
{
    if ((int16_t)OB16(o, BOSS_Y) > y) return 0;
    SET16(o, BOSS_Y, (uint16_t)y);
    SET16(o, OBJ_y_vel, 0);
    return 1;
}

static void blz_display(uint32_t o)              /* BossLabyrinth_Display */
{
    uint32_t p = parent_of(o);
    o_animate(o, o_asset_anim(S1A_EGGMAN));
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    o_draw(o);
}

static void boss_lz(uint32_t o)
{
    uint32_t pl = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t data[3][2] = { { 2, 0 }, { 4, 1 }, { 6, 7 } };
        o_upload_asset(S1A_EGGMAN);
        o_upload_asset(S1A_PRISON);
        SET16(o, OBJ_x_pos, LZ_X + 0x30);
        SET16(o, OBJ_y_pos, LZ_Y + 0x500);
        SET32(o, BOSS_X, (uint32_t)OB16(o, OBJ_x_pos) << 16);
        SET32(o, BOSS_Y, (uint32_t)OB16(o, OBJ_y_pos) << 16);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        uint32_t a1 = o;
        for (int i = 0; i < 3; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x77 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            SET8(a1, R2, 0);
            SET8(a1, OBJ_routine, data[i][0]);
            SET8(a1, OBJ_anim, data[i][1]);
            o_init(a1, S1A_EGGMAN, 4, 4, 0x20, 0x20);
            SET32(a1, PARENT, RAMPTR(o));
        }
        SET8(o, BLZ_DEAD, 0);
    }
    /* fall through */
    case 2:                                    /* BossLabyrinth_ShipMain */
        switch (OB8(o, R2)) {
        case 0:                                /* BLZ_ShipStart */
            if (OB16(pl, OBJ_x_pos) >= LZ_X - 0x40) {
                SET16(o, OBJ_y_vel, -0x180);
                SET16(o, OBJ_x_vel, 0x60);
                SET8(o, R2, 2);
            }
            blz_move_tail(o);
            break;
        case 2: {                              /* BLZ_ShipMove1 */
            int n = blz_clamp_x(o, LZ_X + 0x68) + blz_clamp_y(o, LZ_Y + 0x440);
            if (n == 2) {
                SET16(o, OBJ_x_vel, 0x140);
                SET16(o, OBJ_y_vel, -0x200);
                SET8(o, R2, 4);
            }
            blz_move_tail(o);
            break;
        }
        case 4: {                              /* BLZ_ShipMove2 */
            int n = blz_clamp_x(o, LZ_X + 0x90) + blz_clamp_y(o, LZ_Y + 0x400);
            if (n == 2) {
                SET16(o, OBJ_y_vel, -0x180);
                SET8(o, R2, 6);
                SET8(o, SWAY, 0);
            }
            blz_move_tail(o);
            break;
        }
        case 6: {                              /* BLZ_ShipMove3: climb, keeping ahead of Sonic */
            if ((int16_t)OB16(o, BOSS_Y) <= LZ_Y + 0x40) {
                SET16(o, BOSS_Y, LZ_Y + 0x40);
                SET16(o, OBJ_x_vel, 0x140);
                SET16(o, OBJ_y_vel, -0x80);
                if (OB8(o, BLZ_DEAD)) {
                    SET16(o, OBJ_x_vel, 0x280);
                    SET16(o, OBJ_y_vel, -0x100);
                }
                SET8(o, R2, 8);
                blz_move_tail(o);
                break;
            }
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
            SET8(o, SWAY, OB8(o, SWAY) + 2);
            int16_t s, c;
            o_sincos(OB8(o, SWAY), &s, &c);
            if (c < 0) SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, BOSS_X) + (s >> 4)));
            int16_t d0 = (int16_t)OB16(o, OBJ_y_vel);
            uint16_t py = OB16(pl, OBJ_y_pos), oy = OB16(o, OBJ_y_pos);
            if (py >= oy) {
                uint16_t d1 = (uint16_t)(py - oy);
                if (d1 >= 0x48) {
                    d0 >>= 1;
                    if (d1 >= 0x48 + 0x28) {
                        d0 >>= 1;
                        if (d1 >= 0x48 + 0x50) d0 = 0;
                    }
                }
            }
            int32_t dy = (int32_t)d0 << 8;
            if (OB8(o, BLZ_DEAD)) dy *= 2;
            SET32(o, BOSS_Y, r32(o + BOSS_Y) + (uint32_t)dy);
            SET16(o, OBJ_y_pos, OB16(o, BOSS_Y));
            blz_tail(o);
            break;
        }
        case 8: {                              /* BLZ_ShipAtTop */
            int n = blz_clamp_x(o, LZ_X + 0x16C) + blz_clamp_y(o, LZ_Y);
            if (n == 2) {
                SET8(o, R2, 0x0A);
                SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            }
            blz_move_tail(o);
            break;
        }
        case 0x0A:                             /* BLZ_ShipWait */
            if (!OB8(o, BLZ_DEAD)) {
                if ((int16_t)OB16(pl, OBJ_x_pos) < LZ_X + 0xE8 || (int16_t)OB16(pl, OBJ_y_pos) > LZ_Y + 0x30) {
                    blz_move_tail(o);
                    break;
                }
                SET8(o, BLZ_WAIT, 0x32);
            }
            zone_music();                      /* bgm_LZ */
            w8(RAM_Scroll_lock, 0);            /* REV01: clr.b (f_lockscreen).w */
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
            SET8(o, R2, 0x0C);
            blz_move_tail(o);
            break;
        case 0x0C:                             /* BLZ_Escape1 */
            if (!OB8(o, BLZ_DEAD)) {
                SET8(o, BLZ_WAIT, OB8(o, BLZ_WAIT) - 1);
                if (OB8(o, BLZ_WAIT)) { blz_move_tail(o); break; }
            }
            SET8(o, BLZ_WAIT, 0);
            SET16(o, OBJ_x_vel, 0x400);
            SET16(o, OBJ_y_vel, -0x40);
            SET8(o, BLZ_DEAD, 0);
            SET8(o, R2, 0x0E);
            blz_move_tail(o);
            break;
        default:                               /* BLZ_Escape2 */
            if (r16(RAM_Camera_max_X_pos) < LZ_END)
                w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
            else if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            blz_move_tail(o);
            break;
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
        o_touch(o);
        o_draw(o);
        return;
    case 4: {                                  /* BossLabyrinth_FaceMain */
        uint32_t p = parent_of(o);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        int d1 = 1;
        if (OB8(o, BLZ_DEAD)) d1 = 0x0A;       /* (the face's own flag: never set) */
        else if (!OB8(p, OBJ_coll_flags)) d1 = 5;
        else if (r8(RAM_Player_1 + OBJ_routine) >= 4) d1 = 4;
        SET8(o, OBJ_anim, d1);
        if (OB8(p, R2) == 0x0E) {
            SET8(o, OBJ_anim, 6);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        }
        blz_display(o);
        return;
    }
    case 6: {                                  /* BossLabyrinth_FlameMain */
        uint32_t p = parent_of(o);
        SET8(o, OBJ_anim, 7);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        if (OB8(p, R2) == 0x0E) {
            SET8(o, OBJ_anim, 0x0B);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        }
        blz_display(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* ------------------------------------------------------------------------
 * Object 7A - Star Light Zone boss; Object 7B - the spiked bombs he drops on
 * the seesaws. S1 kept the seesaw list at objoff_2A (S3K status), here $40.
 * ---------------------------------------------------------------------- */
#define SLZ_X    0x2000
#define SLZ_Y    0x210
#define SLZ_END  (SLZ_X + 0x160)
#define BSLZ_SAWS  0x40               /* 3 words: seesaw addresses (0 = none) */
#define SEE_FRAME  0x3A               /* object 5E fields used here */
#define SEE_SPEED  0x38

static void bslz_tail(uint32_t o)                /* loc_189FE */
{
    if (OB8(o, R2) >= 6) return;
    if (OB8(o, OBJ_status) & 0x80) {
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, R2, 6);
        SET8(o, BTIME, 0x78);
        SET16(o, OBJ_x_vel, 0);
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, FLASH)) {
        SET8(o, FLASH, 0x20);
        o_sfx(0x6E);                           /* sfx_HitBoss */
    }
    w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0x0EEE);
    SET8(o, FLASH, OB8(o, FLASH) - 1);
    if (!OB8(o, FLASH))
        SET8(o, OBJ_coll_flags, 0x0F);
}

static void bslz_sway(uint32_t o)                /* loc_189CA */
{
    boss_move(o);
    uint8_t a = OB8(o, SWAY);
    SET8(o, SWAY, a + 2);
    int16_t s, c;
    o_sincos(a, &s, &c);
    SET16(o, OBJ_y_pos, (uint16_t)((s >> 6) + OB16(o, BOSS_Y)));
    SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
    bslz_tail(o);
}

static void bslz_move(uint32_t o)                /* loc_189EE */
{
    boss_move(o);
    SET16(o, OBJ_y_pos, OB16(o, BOSS_Y));
    SET16(o, OBJ_x_pos, OB16(o, BOSS_X));
    bslz_tail(o);
}

static void bslz_child(uint32_t o)               /* loc_18C78 */
{
    uint32_t p = parent_of(o);
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    o_draw(o);
}

static void boss_slz(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const uint8_t data[4][3] = { { 2, 0, 4 }, { 4, 1, 4 }, { 6, 7, 4 }, { 8, 0, 3 } };
        o_upload_asset(S1A_EGGMAN);
        o_upload_asset(S1A_BOSSITEMS2);
        o_upload_asset(S1A_BSBALL);
        o_upload_asset(S1A_BSFRAG);
        o_upload_asset(S1A_PRISON);
        SET16(o, OBJ_x_pos, SLZ_X + 0x188);
        SET16(o, OBJ_y_pos, SLZ_Y + 0x18);
        SET32(o, BOSS_X, (uint32_t)OB16(o, OBJ_x_pos) << 16);
        SET32(o, BOSS_Y, (uint32_t)OB16(o, OBJ_y_pos) << 16);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        uint32_t a1 = o;
        for (int i = 0; i < 4; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x7A * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            SET8(a1, R2, 0);
            SET8(a1, OBJ_routine, data[i][0]);
            SET8(a1, OBJ_anim, data[i][1]);
            o_init(a1, S1A_EGGMAN, 4, data[i][2], 0x20, 0x20);
            SET32(a1, PARENT, RAMPTR(o));
        }
        /* loc_1895C: the arena's seesaws (object 5E, non-zero subtype) */
        int n = 0;
        for (int k = 0; k < 3; k++) SET16(o, BSLZ_SAWS + k * 2, 0);
        for (int i = 0; i < 110 && n < 3; i++) {
            uint32_t s = o_slot(i);
            if (r32(s) == NATIVE_S1_BASE + 0x5E * 4 && OB8(s, OBJ_subtype)) {
                SET16(o, BSLZ_SAWS + n * 2, s & 0xFFFF);
                n++;
            }
        }
    }
    /* fall through */
    case 2:                                    /* BossStarLight_ShipMain */
        switch (OB8(o, R2)) {
        case 0:                                /* BSLZ_ShipStart */
            SET16(o, OBJ_x_vel, -0x100);
            if (OB16(o, BOSS_X) < SLZ_X + 0x120) SET8(o, R2, 2);
            bslz_sway(o);
            break;
        case 2: {                              /* BSLZ_ShipMove: hover over a seesaw end */
            uint16_t d0 = OB16(o, BOSS_X);
            SET16(o, OBJ_x_vel, 0x200);
            if (!(OB8(o, OBJ_status) & 1)) {
                SET16(o, OBJ_x_vel, -0x200);
                if ((int16_t)d0 <= SLZ_X + 8) SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
            } else if ((int16_t)d0 >= SLZ_X + 0x138) {
                SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
            }
            int16_t d4 = (OBS16(o, OBJ_x_vel) < 0) ? -0x28 : 0x28;
            int d2 = 2;
            for (int k = 0; k < 3; k++, d2--) {
                uint16_t w = OB16(o, BSLZ_SAWS + k * 2);
                if (!w) continue;
                uint32_t a3 = 0xFF0000u | w;
                if (OB8(a3, OBJ_status) & ST_P1_STAND) continue;
                if ((uint16_t)(OB16(a3, OBJ_x_pos) + d4) == OB16(o, OBJ_x_pos)) break;
            }
            SET8(o, OBJ_subtype, (uint8_t)d2);
            if (d2 >= 0) {
                SET8(o, R2, 4);
                SET8(o, BTIME, 0x28);
            }
            bslz_sway(o);
            break;
        }
        case 4: {                              /* BSLZ_MakeBall */
            if (OB8(o, BTIME) == 0x28) {
                int8_t sub = (int8_t)OB8(o, OBJ_subtype);
                if (sub < 0) { SET8(o, R2, 2); bslz_sway(o); break; }
                uint16_t w = OB16(o, BSLZ_SAWS + (2 - sub) * 2);
                uint32_t a2 = 0xFF0000u | w;
                int busy = !w;
                for (int i = 0; i < 110 && !busy; i++)
                    if (r32(o_slot(i) + 0x3C) == RAMPTR(a2)) busy = 1;   /* a bomb on it already */
                uint32_t a1 = busy ? 0 : o_alloc_after(a2);
                if (!a1) { SET8(o, R2, 2); bslz_sway(o); break; }
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x7B * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x20);
                SET8(a1, OBJ_status, OB8(a2, OBJ_status));
                SET32(a1, 0x3C, RAMPTR(a2));
            }
            SET8(o, BTIME, OB8(o, BTIME) - 1);
            if (!OB8(o, BTIME)) { SET8(o, R2, 2); bslz_sway(o); break; }
            bslz_tail(o);
            break;
        }
        case 6: {                              /* BSLZ_Explode */
            int8_t t = (int8_t)(OB8(o, BTIME) - 1);
            SET8(o, BTIME, (uint8_t)t);
            if (t >= 0) { s1_boss_defeated(o); break; }
            SET8(o, R2, 8);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_status, (OB8(o, OBJ_status) | 1) & 0x7F);
            SET16(o, OBJ_x_vel, 0);
            SET8(o, BTIME, 0xE8);              /* -$18 */
            s_bossstatus = 1;
            bslz_tail(o);
            break;
        }
        case 8: {                              /* BSLZ_Recover */
            uint8_t t = (uint8_t)(OB8(o, BTIME) + 1);
            SET8(o, BTIME, t);
            if (t == 0) SET16(o, OBJ_y_vel, 0);
            else if (t & 0x80) SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            else if (t < 0x20) SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
            else if (t == 0x20) { SET16(o, OBJ_y_vel, 0); zone_music(); }
            else if (t >= 0x2A) SET8(o, R2, 0x0A);
            bslz_move(o);
            break;
        }
        default:                               /* BSLZ_Escape */
            SET16(o, OBJ_x_vel, 0x400);
            SET16(o, OBJ_y_vel, -0x40);
            if (r16(RAM_Camera_max_X_pos) < SLZ_END)
                w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
            else if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            boss_move(o);
            bslz_sway(o);
            break;
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
        o_touch(o);
        o_draw(o);
        return;
    case 4: {                                  /* BossStarLight_FaceMain */
        uint32_t p = parent_of(o);
        uint8_t d0 = OB8(p, R2);
        int d1 = 1;
        if ((int8_t)(d0 - 6) >= 0) d1 = 0x0A;
        else if (!OB8(p, OBJ_coll_flags)) d1 = 5;
        else if (r8(RAM_Player_1 + OBJ_routine) >= 4) d1 = 4;
        SET8(o, OBJ_anim, d1);
        if (d0 == 0x0A) {
            SET8(o, OBJ_anim, 6);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        bslz_child(o);
        return;
    }
    case 6: {                                  /* BossStarLight_FlameMain */
        uint32_t p = parent_of(o);
        SET8(o, OBJ_anim, 8);
        if (OB8(p, R2) == 0x0A) {
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            SET8(o, OBJ_anim, 0x0B);
        } else if (OB8(p, R2) <= 8 && OB8(p, R2) >= 4) {
            SET8(o, OBJ_anim, 7);
        }
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        bslz_child(o);
        return;
    }
    case 8: {                                  /* BossStarLight_TubeMain */
        uint32_t p = parent_of(o);
        if (OB8(p, R2) == 0x0A && !(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_asset(o, S1A_BOSSITEMS2);
        SET8(o, OBJ_frame, 3);
        bslz_child(o);
        return;
    }
    default:
        o_delete(o);
    }
}

/* Object 7B: $30 = seesaw x, $34 = seesaw y, $3A = side, $3C = seesaw,
 * $40 = countdown (S1 obSubtype word), $3E/$3F = blink delay/timer */
#define BS_SAWX   0x30
#define BS_SAWY   0x34
#define BS_SIDE   0x3A
#define BS_SAW    0x3C
#define BS_COUNT  0x40
#define BS_DELAY  0x3E
#define BS_TIMER  0x3F

static const int16_t bs_yoff[5] = { -8, -0x1C, -0x2F, -0x1C, -8 };   /* BossSpikeball_SeesawYOffset */
extern void s1_seesaw_land(uint32_t board, uint32_t ball, uint8_t d1, int half);

static void bs_blink(uint32_t o)                 /* loc_18E7A */
{
    if (OB16(o, BS_COUNT) == 0x78) SET8(o, BS_DELAY, 5);
    if (OB16(o, BS_COUNT) == 0x3C) SET8(o, BS_DELAY, 2);
    int8_t t = (int8_t)(OB8(o, BS_TIMER) - 1);
    SET8(o, BS_TIMER, (uint8_t)t);
    if (t > 0) return;
    SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
    SET8(o, BS_TIMER, OB8(o, BS_DELAY));
}

static int bs_landed(uint32_t o)                 /* reached the seesaw surface? */
{
    uint32_t a1 = r32(o + BS_SAW) & 0xFFFFFF;
    int k = OB8(a1, OBJ_frame);
    if (OB16(o, OBJ_x_pos) < OB16(o, BS_SAWX)) k += 2;
    return (int16_t)(OB16(o, BS_SAWY) + bs_yoff[k]) <= (int16_t)OB16(o, OBJ_y_pos);
}

static void bs_land(uint32_t o, uint8_t d1)      /* loc_18FA2 */
{
    s1_seesaw_land(r32(o + BS_SAW) & 0xFFFFFF, o, d1, 1);
    SET16(o, OBJ_x_vel, 0);
    SET16(o, OBJ_y_vel, 0);
    SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
    bs_blink(o);
}

static void boss_spikeball(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                  /* BossSpikeball_Main */
        uint32_t a1 = r32(o + BS_SAW) & 0xFFFFFF;
        o_init(o, S1A_BSBALL, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_frame, 1);
        SET8(o, OBJ_coll_flags, 0x8B);
        SET16(o, BS_SAWX, OB16(a1, OBJ_x_pos));
        SET16(o, BS_SAWY, OB16(a1, OBJ_y_pos));
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        if ((int16_t)OB16(o, OBJ_x_pos) <= (int16_t)OB16(a1, OBJ_x_pos)) {
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
            SET8(o, BS_SIDE, 2);
        }
        SET8(o, OBJ_routine, 2);
    }
    /* fall through */
    case 2:                                    /* BossSpikeball_Fall */
        o_fall(o);
        if (bs_landed(o)) {
            SET16(o, BS_COUNT, 0xF0);
            SET8(o, BS_DELAY, 10);
            SET8(o, BS_TIMER, 10);
            bs_land(o, (OB8(o, OBJ_status) & 1) ? 0 : 2);
        }
        break;
    case 4: {                                  /* BossSpikeball_Bounce */
        uint32_t a1 = r32(o + BS_SAW) & 0xFFFFFF;
        int8_t d0 = (int8_t)(OB8(o, BS_SIDE) - OB8(a1, SEE_FRAME));
        if (!d0) {                             /* sits on its end, counting down */
            int k = OB8(a1, OBJ_frame);
            int16_t dx = 0x28;
            if (OB16(o, OBJ_x_pos) < OB16(o, BS_SAWX)) { dx = -0x28; k += 2; }
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, BS_SAWY) + bs_yoff[k]));
            SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, BS_SAWX) + dx));
            SET16(o, OBJ_y_pos + 2, 0);
            SET16(o, OBJ_x_pos + 2, 0);
            SET16(o, BS_COUNT, OB16(o, BS_COUNT) - 1);
            if (!OB16(o, BS_COUNT)) {
                SET16(o, BS_COUNT, 0x20);
                SET8(o, OBJ_routine, 8);
                break;
            }
            bs_blink(o);
            break;
        }
        if (d0 < 0) d0 = (int8_t)-d0;          /* the other end went down: fly */
        int16_t d1 = -0x818, d2 = -0x114;
        if (d0 != 1) {
            d1 = -0x960; d2 = -0xF4;
            if ((int16_t)OB16(a1, SEE_SPEED) >= 0x9C0) { d1 = -0xA20; d2 = -0x80; }
        }
        SET16(o, OBJ_y_vel, (uint16_t)d1);
        SET16(o, OBJ_x_vel, (uint16_t)d2);
        if (OB16(o, OBJ_x_pos) < OB16(o, BS_SAWX)) SET16(o, OBJ_x_vel, (uint16_t)-d2);
        SET8(o, OBJ_frame, 1);
        SET16(o, BS_COUNT, 0x20);
        SET8(o, OBJ_routine, 6);
    }
    /* fall through */
    case 6: {                                  /* BossSpikeball_HitBoss */
        for (int i = 0; i < 110; i++) {
            uint32_t b = o_slot(i);
            if (r32(b) != NATIVE_S1_BASE + 0x7A * 4) continue;
            int16_t bx = (int16_t)OB16(b, OBJ_x_pos), by = (int16_t)OB16(b, OBJ_y_pos);
            int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
            if ((uint16_t)(x + 8) >= (uint16_t)(bx - 0x18) && (uint16_t)(bx + 0x18) >= (uint16_t)(x - 8) &&
                (uint16_t)(y + 8) >= (uint16_t)(by - 0x18) && (uint16_t)(by + 0x18) >= (uint16_t)(y - 8)) {
                SET8(o, OBJ_routine, 8);
                SET16(o, BS_COUNT, 0);
                SET8(b, OBJ_coll_flags, 0);
                SET8(b, OBJ_coll_prop, OB8(b, OBJ_coll_prop) - 1);
                if (!OB8(b, OBJ_coll_prop)) {
                    SET8(b, OBJ_status, OB8(b, OBJ_status) | 0x80);
                    SET16(o, OBJ_x_vel, 0);
                    SET16(o, OBJ_y_vel, 0);
                }
            }
            break;                             /* the first boss object = the ship */
        }
        if ((int16_t)OB16(o, OBJ_y_vel) < 0) {
            o_fall(o);
            if ((int16_t)(OB16(o, BS_SAWY) - 0x2F) <= (int16_t)OB16(o, OBJ_y_pos)) o_fall(o);
            bs_blink(o);
            break;
        }
        o_fall(o);
        if (!bs_landed(o)) { bs_blink(o); break; }
        SET16(o, BS_COUNT, 0);
        bs_land(o, (OBS16(o, OBJ_x_vel) < 0) ? 2 : 0);
        break;
    }
    case 8: {                                  /* BossSpikeball_Explode */
        int frag = OB16(o, BS_COUNT) == 0x20;
        uint16_t sy = OB16(o, BS_SAWY);
        if (frag) SET16(o, OBJ_y_pos, sy);
        SET32(o, OBJ_code, S3K_Obj_Explosion); /* id_Explosion ($3F: no animal) */
        SET8(o, OBJ_routine, 6);
        o_sfx(0xB4);                           /* sfx_Bomb */
        if (!frag) return;
        static const int16_t spd[4][2] = { { -0x100, -0x340 }, { -0xA0, -0x240 }, { 0x100, -0x340 }, { 0xA0, -0x240 } };
        for (int i = 0; i < 4; i++) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x7B * 4);
            SET8(a1, OBJ_routine, 0x0A);
            o_init(a1, S1A_BSFRAG, 4 | 0x80, 3, 0x0C, 0x0C);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, sy);
            SET16(a1, OBJ_x_vel, (uint16_t)spd[i][0]);
            SET16(a1, OBJ_y_vel, (uint16_t)spd[i][1]);
            SET8(a1, OBJ_coll_flags, 0x98);
        }
        return;
    }
    default:                                   /* BossSpikeball_MoveFrag */
        o_move(o);
        SET16(o, BS_SAWX, OB16(o, OBJ_x_pos));
        SET16(o, BS_SAWY, OB16(o, OBJ_y_pos));
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        SET8(o, OBJ_frame, (o_frame_counter() & 4) >> 2);
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        break;
    }
    int16_t d0 = (int16_t)((OB16(o, BS_SAWX) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80));
    if (d0 < 0 || d0 > 0x280) { o_delete(o); return; }
    o_touch(o);
    o_draw(o);
}

void s1_obj_boss_register(TriObjFn *t)
{
    t[0x7A] = boss_slz;
    t[0x7B] = boss_spikeball;
    t[0x77] = boss_lz;
    t[0x75] = boss_syz;
    t[0x76] = boss_block;
    t[0x73] = boss_mz;
    t[0x74] = boss_fire;
    t[0x3D] = boss_ghz;
    t[0x48] = boss_ball;
    t[0x3E] = prison;
}
