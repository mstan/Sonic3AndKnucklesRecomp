/*
 * s2_obj_dez.c — Sonic 2 Death Egg objects, transcribed from s2disasm:
 * Eggman on foot, his door and dust (C6), Silver Sonic with its LEDs, back
 * spikes, spike balls and Eggman's window (AF), and the Death Egg Robot
 * (C7: body, limbs, head, jet, targeting sensor / lock, bombs, debris, the
 * run to the ending).
 *
 * S3K's Touch_Enemy writes $25 (and $1C) of a boss it hits, so the boss
 * parts keep their secondary routine elsewhere ($45 Silver Sonic, $2E robot)
 * and animate through a wrapper that moves it into $25 for the $FA/$FD
 * commands. The robot's head uses S2 touch size $2A: S3K's $2D is the same box.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"
#include <stdlib.h>

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define OBJ_prev_anim   0x21
#define P_OBJCTRL       0x2E
#define ST_YFLIP_       0x02
#define ST_MISC         0x80
#define FLASH_COLOUR    (RAM_Normal_palette + 0x20 + 11 * 2)   /* S2 line 1 idx 1, permuted */
#define MUS_BOSS        0x19
#define MUS_ENDBOSS     0x30          /* S3K mus_FinalBoss (S2: MusID_EndBoss) */
#define SFX_FIRE        0xDC          /* SndID_Fire */
#define SFX_BUZZ        0xEE          /* SndID_MechaSonicBuzz */
#define SFX_DASH        0xBC          /* SndID_SpindashRelease */
#define SFX_LASERBEAM   0xB0          /* SndID_LaserBeam */
#define SFX_SPIKEBALLS  0xA2          /* SndID_SpikeSwitch */
#define SFX_BOSSHIT     0xAC          /* SndID_BossHit */
#define SFX_EXPLODE     0xC4          /* SndID_BossExplosion */
#define SFX_RUMBLE      0xB7          /* SndID_Rumbling */
#define SFX_RUMBLE2     0xE1          /* SndID_Rumbling2 */
#define SFX_SMASH       0xB9          /* SndID_Smash */
#define SFX_HAMMER      0xBD          /* SndID_Hammer */
#define SFX_BEEP        0xA8          /* SndID_Beep */

uint16_t s2_dez_eggman;                      /* DEZ_Eggman */
uint8_t s2_dez_shake;                        /* Screen_Shaking_Flag (DEZ) */
int16_t s2_dez_shake_timer;                  /* DEZ_Shake_Timer */
void tri_s2_complete(void);

static uint32_t ptr(uint16_t w) { return 0xFF0000u | w; }
static int on_screen(uint32_t o) { return (OB8(o, OBJ_render) & 0x80) != 0; }

static void gone(uint32_t o)                         /* MarkObjGone */
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void explosion_at(uint32_t o, int spread, int range)   /* Obj58 near o */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);
    o_sfx_s2(SFX_EXPLODE);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    if (spread) {
        uint32_t r = (uint32_t)o_random();
        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (int16_t)(((r & 0xFF) >> 2) - range));
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + (int16_t)((((r >> 8) & 0xFF) >> 2) - range));
    }
}

static void boss_explosion(uint32_t o)               /* Boss_LoadExplosion */
{
    if (!(o_frame_counter() & 7)) explosion_at(o, 1, 0x20);
}

static void flash(void) { w16(FLASH_COLOUR, r16(FLASH_COLOUR) ? 0 : 0xEEE); }

/* AnimateSprite with the S2 commands moving a relocated secondary routine */
static void animate_2nd(uint32_t o, int asset, int f2nd)
{
    uint8_t keep = OB8(o, OBJ_2nd);
    SET8(o, OBJ_2nd, OB8(o, f2nd));
    o_animate_s2(o, o_asset_anim(asset));
    SET8(o, f2nd, OB8(o, OBJ_2nd));
    SET8(o, OBJ_2nd, keep);
}

/* ========================================================================
 * Object C6 - Eggman (DEZ): runs off past Silver Sonic's room, his door
 * slides open, he jumps into the Death Egg Robot
 * ====================================================================== */
#define EG_2A    0x40                 /* objoff_2A (word) */
#define EG_PAR   0x42                 /* objoff_2C */
#define EG_DOOR  0x3E

static uint32_t eggman_child(uint32_t o, int field, uint8_t sub)    /* LoadChildObject */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    if (field) SET16(o, field, (uint16_t)a1);
    SET32(a1, OBJ_code, S2(0xC6));
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, EG_PAR, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    return a1;
}

static void eggman_run(uint32_t o)                   /* ObjC6_State2 */
{
    switch (OB8(o, OBJ_2nd)) {
    case 0: {
        SET8(o, OBJ_2nd, 2);
        uint32_t a1 = eggman_child(o, EG_DOOR, 0xA8);
        if (a1) { SET16(a1, OBJ_x_pos, 0x3F8); SET16(a1, OBJ_y_pos, 0x160); }
        s2_dez_eggman = (uint16_t)o;
        break;
    }
    case 2: {
        int16_t d2;
        s2_orient(o, NULL, NULL, &d2, NULL);
        if ((uint16_t)(d2 + 0x5C) < 0xB8) {
            SET8(o, OBJ_2nd, 4);
            SET16(o, EG_2A, 0x18);
            SET8(o, OBJ_frame, 1);
        }
        break;
    }
    case 4:
        SET16(o, EG_2A, OB16(o, EG_2A) - 1);
        if ((int16_t)OB16(o, EG_2A) < 0) {
            SET8(o, OBJ_2nd, 6);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_MISC);
            SET16(o, OBJ_x_vel, 0x200);
            SET16(o, EG_2A, 0x10);
        }
        break;
    case 6:
        if (OB16(o, OBJ_x_pos) >= 0x810) {             /* loc_3CFC0 */
            SET8(o, OBJ_frame, 2);
            SET16(o, OBJ_x_vel, 0);
            if (on_screen(o)) {
                SET8(o, OBJ_2nd, 8);
                SET16(o, OBJ_x_vel, 0x80);
                SET16(o, OBJ_y_vel, (uint16_t)-0x200);
                SET16(o, EG_2A, 0x50);
                SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_P1_STAND);   /* (the robot's head waits for it) */
            }
            break;
        }
        {
            int16_t d2;
            s2_orient(o, NULL, NULL, &d2, NULL);
            if ((uint16_t)(d2 + 0x50) < 0xA0)
                SET16(o, OBJ_x_pos, OB16(s2_orient_player, OBJ_x_pos) + 0x50);
        }
        SET16(o, EG_2A, OB16(o, EG_2A) - 1);
        if ((int16_t)OB16(o, EG_2A) < 0) {              /* loc_3D00C: a puff of dust */
            SET16(o, EG_2A, 0x20);
            uint32_t a1 = eggman_child(o, 0x3C, 0xAA);
            if (a1) {
                SET8(a1, OBJ_frame, 5);
                SET16(a1, OBJ_x_vel, (uint16_t)-0x100);
                SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 0x18);
                SET16(a1, EG_2A, 8);
            }
        }
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_ROBOTNIK));
        break;
    default:
        SET16(o, EG_2A, OB16(o, EG_2A) - 1);
        if ((int16_t)OB16(o, EG_2A) < 0) { o_delete(o); return; }
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
        o_move(o);
        break;
    }
    o_draw(o);
}

static void eggman_door(uint32_t o)                  /* ObjC6_State3 */
{
    switch (OB8(o, OBJ_2nd)) {
    case 0:
        if (OB8(ptr(OB16(o, EG_PAR)), OBJ_status) & ST_MISC) { SET8(o, OBJ_2nd, 2); o_draw(o); return; }
        break;
    case 2:
        o_animate_s2(o, o_asset_anim(S2A_DEZDOOR));     /* $FA: on to 4 */
        break;
    default:
        SET8(RAM_Player_1, OBJ_status, OB8(RAM_Player_1, OBJ_status) & ~ST_P1_PUSH);
        o_delete(o);
        return;
    }
    o_solid(o, 0x13, 0x20, 0x20, OB16(o, OBJ_x_pos));
    o_draw(o);
}

static void eggman_dust(uint32_t o)                  /* ObjC6_State4 */
{
    SET16(o, EG_2A, OB16(o, EG_2A) - 1);
    if ((int16_t)OB16(o, EG_2A) < 0) { o_delete(o); return; }
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
    o_move(o);
    gone(o);
}

static void eggman(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* ObjC6_Init */
        uint8_t sub = OB8(o, OBJ_subtype);
        if (!o_asset_ready(S2A_ROBOTNIK)) o_upload_asset(S2A_ROBOTNIK);   /* (PLC_DEZ2; boss art elsewhere) */
        if (sub == 0xA8) {
            o_init(o, S2A_DEZDOOR, OB8(o, OBJ_render) | 4, 1, 8, 0x20);
        } else {
            o_init(o, S2A_ROBOTNIK, OB8(o, OBJ_render) | 4, 5, sub == 0xAA ? 4 : 0x18, 0x20);
        }
        SET8(o, OBJ_routine, (uint8_t)(sub - 0xA4));
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 2: eggman_run(o); return;
    case 4: eggman_door(o); return;
    default: eggman_dust(o); return;
    }
}

/* ========================================================================
 * Object AF - Silver Sonic (Mecha Sonic). Children: its LEDs ($E-$12), its
 * back spikes' hurt box ($14-$18), Eggman's window ($1A-$22); its spike
 * balls are Obj98 shots.
 * ====================================================================== */
#define MS_2A    0x40                 /* objoff_2A (byte timer) */
#define MS_2C    0x41                 /* objoff_2C: dashes left */
#define MS_2D    0x42                 /* objoff_2D: direction toggle */
#define MS_2E    0x43                 /* objoff_2E: spike balls thrown */
#define MS_2F    0x44                 /* objoff_2F: attack index */
#define MS_2ND   0x45                 /* routine_secondary */
#define MS_PAR   0x46                 /* children: objoff_2C */
#define MS_FLASH 0x30                 /* objoff_30 */
#define MS_DEAD  0x32                 /* objoff_32 (word) */
#define MS_WIN   0x3A                 /* objoff_3A: the window */
#define MS_SPIKE 0x3C                 /* objoff_3C: the spike hurt box */
#define MS_LED   0x3E                 /* parent(a0): the LEDs */
#define ANI_MECHA_LED 1

/* AnimateSprite_Checked: no flips; 1 when an end command ran */
static int anim_checked(uint32_t o, int asset, int f2nd)
{
    uint32_t ani = o_asset_anim(asset);
    if (!ani) return 0;
    uint8_t d0 = OB8(o, OBJ_anim);
    if (d0 != OB8(o, OBJ_prev_anim)) {
        SET8(o, OBJ_prev_anim, d0);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) return 0;
    uint32_t a1 = ani + tri_rd16(ani + d0 * 2u);
    SET8(o, OBJ_anim_timer, tri_rd8(a1));
    uint8_t f = tri_rd8(a1 + 1 + OB8(o, OBJ_anim_frame));
    if (!(f & 0x80)) {
        SET8(o, OBJ_frame, f);
        SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
        return 0;
    }
    switch (f) {
    case 0xFF:
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_frame, tri_rd8(a1 + 1));
        SET8(o, OBJ_anim_frame, 1);
        return 1;
    case 0xFE:
        SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
        SET8(o, OBJ_anim_timer, 0);
        SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
        return 1;
    case 0xFD:
        SET8(o, f2nd, OB8(o, f2nd) + 2);
        return 1;
    case 0xFC:
        SET8(o, OBJ_anim_timer, 1);
        return 1;
    default:
        return 1;
    }
}

static void mecha_align(uint32_t o, uint32_t a1, int16_t dx, int16_t dy)   /* Obj_AlignChildXY */
{
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
}

static void mecha_spike_box(uint32_t o)              /* loc_39D4A */
{
    mecha_align(o, ptr(OB16(o, MS_SPIKE)), (OB8(o, OBJ_render) & 1) ? -0x0C : 0x0C, -0x0C);
}

static void mecha_coll_by_frame(uint32_t o)          /* loc_39D24: the spikes hurt while it spins */
{
    uint8_t d0 = OB8(o, OBJ_frame);
    SET8(o, OBJ_coll_flags, (d0 == 6 || d0 == 7 || d0 == 8) ? 0x9A : 0x1A);
}

static void mecha_dash(uint32_t o, int16_t d0)       /* loc_39D60 */
{
    if (!OB8(o, MS_2D)) d0 = (int16_t)-d0;
    SET8(o, MS_2D, ~OB8(o, MS_2D));
    SET16(o, OBJ_x_vel, (uint16_t)d0);
}

static void mecha_brake(uint32_t o)                  /* loc_39D72 */
{
    SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + ((int16_t)OB16(o, OBJ_x_vel) < 0 ? 0x20 : -0x20));
}

static void mecha_spikeballs(uint32_t o)             /* loc_39D82: eight spike balls */
{
    static const int8_t t[8][5] = {
        { 0, -0x18, 0, -3, 0x0F }, { -0x10, -0x10, -2, -2, 0x10 }, { -0x18, 0, -3, 0, 0x11 },
        { -0x10, 0x10, -2, 2, 0x12 }, { 0, 0x18, 0, 3, 0x13 }, { 0x10, 0x10, 2, 2, 0x14 },
        { 0x18, 0, 3, 0, 0x15 }, { 0x10, -0x10, 2, -2, 0x16 },
    };
    for (int k = 0; k < 8; k++) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) return;
        SET32(a1, OBJ_code, S2(0x98));
        SET8(a1, OBJ_routine, 2);
        o_init(a1, S2A_MECHA, 0x80 | 4, 5, 4, 4);
        SET8(a1, OBJ_coll_flags, 0x98);
        SET8(a1, S2_PROJ_KIND, S2_PROJ_MOVE);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + t[k][0]);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + t[k][1]);
        SET16(a1, OBJ_x_vel, (uint16_t)(t[k][2] * 0x100));
        SET16(a1, OBJ_y_vel, (uint16_t)(t[k][3] * 0x100));
        SET8(a1, OBJ_frame, t[k][4]);
    }
}

static void mecha_led_say(uint32_t o, uint8_t routine, int anim)
{
    uint32_t a1 = ptr(OB16(o, MS_LED));
    SET8(a1, OBJ_routine, routine);
    if (anim >= 0) SET8(a1, OBJ_anim, anim);
}

static void mecha_idle(uint32_t o)                   /* loc_399D6 */
{
    SET8(o, OBJ_routine, 8);
    SET8(o, OBJ_anim, 0);
    SET8(o, MS_2A, 0x64);
    SET16(o, OBJ_x_vel, 0);
    mecha_led_say(o, 0x12, -1);
    SET8(ptr(OB16(o, MS_SPIKE)), OBJ_routine, 0x18);
    o_sfx_s2(SFX_BUZZ);
}

static void mecha_turn(uint32_t o)                   /* loc_39A7C */
{
    SET8(o, MS_2ND, OB8(o, MS_2ND) + 2);
    SET8(o, OBJ_anim, 5);
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET16(o, OBJ_x_vel, 0);
    SET16(o, OBJ_y_vel, 0);
}

static void mecha_hits(uint32_t o)                   /* loc_39CAE */
{
    if (!OB8(o, OBJ_coll_prop)) {                       /* loc_39CF0: beaten */
        D(0) = 100;                                     /* AddPoints: 1000 */
        tri_jsr(S3K_HUD_AddToScore);
        SET16(o, MS_DEAD, 0xFF);
        SET8(o, OBJ_routine, 0x0C);
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_MISC);
        o_delete(ptr(OB16(o, MS_SPIKE)));
        o_delete(ptr(OB16(o, MS_LED)));
        return;
    }
    if (OB8(o, OBJ_coll_flags)) return;
    if (!OB8(o, MS_FLASH)) {
        SET8(o, MS_FLASH, 0x20);
        o_sfx_s2(SFX_BOSSHIT);
    }
    flash();
    SET8(o, MS_FLASH, OB8(o, MS_FLASH) - 1);
    if (OB8(o, MS_FLASH)) return;
    w16(FLASH_COLOUR, 0);
    mecha_coll_by_frame(o);
}

static void mecha_attack(uint32_t o)                 /* loc_398C0's secondary routines */
{
    uint8_t s = OB8(o, MS_2ND);
    switch (s) {
    case 0x00:                                          /* loc_3991E: two dashes */
        SET8(o, MS_2ND, 2);
        SET8(o, OBJ_frame, 3);
        SET8(o, MS_2C, 2);
        /* fall through */
    dash_again:                                         /* loc_3992E */
        SET8(o, MS_2A, 0x20);
        mecha_led_say(o, 0x10, 1);
        return;
    case 0x02:
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) >= 0) return;
        SET8(o, MS_2ND, 4);
        SET8(o, MS_2A, 0x40);
        SET8(o, OBJ_anim, 1);
        mecha_dash(o, 0x800);
        SET8(ptr(OB16(o, MS_LED)), OBJ_anim, 2);
        o_sfx_s2(SFX_DASH);
        return;
    case 0x04:
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) < 0) {                /* loc_399C2 */
            SET8(o, MS_2C, OB8(o, MS_2C) - 1);
            if (!OB8(o, MS_2C)) { mecha_idle(o); return; }
            SET8(o, MS_2ND, 2);
            SET16(o, OBJ_x_vel, 0);
            goto dash_again;
        }
        if (OB8(o, MS_2A) == 0x20) {
            SET8(o, OBJ_anim, 2);
            mecha_led_say(o, 0x12, -1);
        }
        mecha_brake(o);
        anim_checked(o, S2A_MECHA, MS_2ND);
        if (OB8(o, OBJ_anim) == 2 && OB8(o, OBJ_anim_frame) == 2 && OB8(o, OBJ_anim_timer) == 3)
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
        return;
    case 0x06: case 0x10: case 0x1E:                    /* loc_39A0A: curl up */
        SET8(o, MS_2ND, s + 2);
        SET8(o, OBJ_frame, 3);
        SET8(o, OBJ_anim, 3);
        return;
    case 0x08: case 0x12: case 0x20:                    /* loc_39A1C */
        if (anim_checked(o, S2A_MECHA, MS_2ND)) {
            SET8(o, MS_2ND, OB8(o, MS_2ND) + 2);
            SET8(o, MS_2A, 0x20);
            SET8(o, OBJ_anim, 4);
            o_sfx_s2(SFX_LASERBEAM);
        }
        return;
    case 0x0A:                                          /* loc_39A44 */
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) < 0) {
            SET8(o, MS_2ND, 0x0C);
            SET8(o, MS_2A, 0x40);
            mecha_dash(o, 0x800);
            return;
        }
        anim_checked(o, S2A_MECHA, MS_2ND);
        return;
    case 0x0C:                                          /* loc_39A68: spinning dash */
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) < 0) { mecha_turn(o); return; }
        mecha_brake(o);
        anim_checked(o, S2A_MECHA, MS_2ND);
        return;
    case 0x0E: case 0x1C: case 0x2A:                    /* loc_39A96 */
        if (anim_checked(o, S2A_MECHA, MS_2ND)) mecha_idle(o);
        return;
    case 0x14: case 0x22:                               /* loc_39AAA */
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) < 0) {
            SET8(o, MS_2ND, s + 2);
            SET8(o, MS_2A, 0x40);
            mecha_dash(o, 0x400);
            return;
        }
        anim_checked(o, S2A_MECHA, MS_2ND);
        return;
    case 0x16: case 0x24:                               /* loc_39ACE: the hop */
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if (OB8(o, MS_2A) == 60) {
            SET8(o, MS_2ND, s + 2);
            SET16(o, OBJ_y_vel, (uint16_t)-0x600);
        }
        anim_checked(o, S2A_MECHA, MS_2ND);
        return;
    case 0x18: case 0x26: {                             /* loc_39AF4 / loc_39B44 */
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) < 0) { mecha_turn(o); return; }
        if (s == 0x26 && !OB8(o, MS_2E) && (int16_t)OB16(o, OBJ_y_vel) >= 0) {
            SET8(o, MS_2E, 0xFF);                       /* at the top: spike balls */
            mecha_spikeballs(o);
            o_sfx_s2(SFX_SPIKEBALLS);
        }
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET8(o, MS_2ND, s + 2);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, OBJ_y_vel, 0);
        }
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        anim_checked(o, S2A_MECHA, MS_2ND);
        return;
    }
    default: {                                          /* $1A / $28: loc_39B28 */
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if ((int8_t)OB8(o, MS_2A) < 0) { mecha_turn(o); return; }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        anim_checked(o, S2A_MECHA, MS_2ND);
        return;
    }
    }
}

static uint32_t mecha_child(uint32_t o, int field, uint8_t sub, uint8_t routine)
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET16(o, field, (uint16_t)a1);
    SET32(a1, OBJ_code, S2(0xAF));
    SET8(a1, OBJ_subtype, sub);
    SET8(a1, OBJ_routine, routine);
    SET16(a1, MS_PAR, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    return a1;
}

static void mecha(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0x00:                                          /* ObjAF_Init */
        o_init(o, S2A_MECHA, OB8(o, OBJ_render) | 4, 4, 0x10, 0x20);
        SET8(o, OBJ_y_radius, 0x1B);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_coll_prop, 8);
        SET8(o, OBJ_routine, 2);
        mecha_child(o, MS_LED, 0x48, 0x0E);
        mecha_child(o, MS_SPIKE, 0x48, 0x14);
        mecha_child(o, MS_WIN, 0xA4, 0x1A);
        return;
    case 0x02:                                          /* loc_397AC */
        if (r16(RAM_Camera_X_pos) >= 0x224) {
            SET8(o, OBJ_routine, 4);
            SET16(o, MS_2A, 60);                        /* (word) */
            SET16(o, OBJ_y_vel, 0x100);
            w16(RAM_Camera_min_X_pos, 0x224);
            w16(RAM_Camera_max_X_pos, 0x224);
            D(0) = 0xE1;                                /* cmd_FadeOut */
            tri_jsr(S3K_Play_Music);
        }
        o_draw(o);
        return;
    case 0x04:
        SET16(o, MS_2A, OB16(o, MS_2A) - 1);
        if ((int16_t)OB16(o, MS_2A) < 0) {
            SET8(o, OBJ_routine, 6);
            D(0) = MUS_BOSS;
            tri_jsr(S3K_Play_Music);
        }
        o_draw(o);
        return;
    case 0x06: {                                        /* loc_397FE: drops in on its jets */
        if (!(o_frame_counter() & 0x1F)) o_sfx_s2(SFX_FIRE);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {                                   /* loc_39830: landed */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_coll_flags, 0x1A);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_YFLIP_);   /* (the window opens) */
            mecha_idle(o);
            o_draw(o);
            return;
        }
        o_move(o);
        mecha_align(o, ptr(OB16(o, MS_LED)), 0, 0);
        mecha_spike_box(o);
        o_draw(o);
        return;
    }
    case 0x08:                                          /* loc_3984A: standing */
        mecha_hits(o);
        if (OB8(o, OBJ_routine) != 8) { o_draw(o); return; }
        if (OB8(o, OBJ_coll_flags)) mecha_coll_by_frame(o);
        SET8(o, MS_2A, OB8(o, MS_2A) - 1);
        if (!OB8(o, MS_2A)) {                           /* loc_39886: pick an attack */
            static const uint8_t next[16] = { 6, 0, 0x10, 6, 6, 0x1E, 0, 0x10, 6, 6, 0x10, 6, 0, 6, 0x10, 0x1E };
            SET8(o, OBJ_routine, 0x0A);
            SET8(o, MS_2ND, next[OB8(o, MS_2F) & 0x0F]);
            SET8(o, MS_2F, OB8(o, MS_2F) + 1);
            SET8(o, MS_2E, 0);
            SET8(ptr(OB16(o, MS_SPIKE)), OBJ_routine, 0x16);
            o_draw(o);
            return;
        }
        if (OB8(o, MS_2A) == 0x32) o_sfx_s2(SFX_BUZZ);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o));
        anim_checked(o, S2A_MECHA, MS_2ND);
        mecha_spike_box(o);
        o_draw(o);
        return;
    case 0x0A:                                          /* loc_398C0: attacking */
        mecha_hits(o);
        if (OB8(o, OBJ_routine) != 0x0A) { o_draw(o); return; }
        if (OB8(o, OBJ_coll_flags)) mecha_coll_by_frame(o);
        mecha_attack(o);
        mecha_align(o, ptr(OB16(o, MS_LED)), 0, 0);
        mecha_spike_box(o);
        o_move(o);
        o_draw(o);
        return;
    case 0x0C:                                          /* loc_39B92: blowing up */
        SET8(o, OBJ_coll_flags, 0);
        SET16(o, MS_DEAD, OB16(o, MS_DEAD) - 1);
        if ((int16_t)OB16(o, MS_DEAD) >= 0) {
            boss_explosion(o);
            o_draw(o);
            return;
        }
        w16(RAM_Camera_max_X_pos, 0x1000);
        w8(RAM_Dynamic_resize_routine, r8(RAM_Dynamic_resize_routine) + 2);
        /* (S2 plays Level_Music as a byte: sound 0, nothing; the music stays off) */
        o_delete(o);
        return;
    /* ---- the LEDs ---- */
    case 0x0E:                                          /* loc_39BBA */
        o_init(o, S2A_MECHALED, 4, 4, 8, 0x10);
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_routine, 0x10);
        return;
    case 0x10: {                                        /* loc_39BCC */
        uint32_t a1 = ptr(OB16(o, MS_PAR));
        uint8_t fl = OB8(a1, OBJ_render) & 3;           /* InheritParentXYFlip */
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | fl);
        SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~3) | fl);
        anim_checked(o, S2A_MECHALED, OBJ_2nd);
        o_draw(o);
        return;
    }
    case 0x12:                                          /* loc_39BE2: hidden */
        SET8(o, OBJ_render, OB8(o, OBJ_render) & ~0x80);
        return;
    /* ---- the back spikes' hurt box ---- */
    case 0x14:                                          /* loc_39BEA */
        o_init(o, S2A_MECHA, 4, 3, 8, 8);
        SET8(o, OBJ_frame, 0x0B);
        SET8(o, OBJ_routine, 0x16);
        return;
    case 0x16:
        SET8(o, OBJ_coll_flags, 0);
        return;
    case 0x18:
        SET8(o, OBJ_coll_flags, 0x98);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x80);
        o_touch(o);
        return;
    /* ---- Eggman's window ---- */
    case 0x1A:                                          /* loc_39C12 */
        o_init(o, S2A_DEZWINDOW, 4, 6, 0x10, 0x20);
        SET8(o, OBJ_frame, 4);
        SET16(o, OBJ_x_pos, 0x2C0);
        SET16(o, OBJ_y_pos, 0x139);
        SET8(o, OBJ_routine, 0x1C);
        return;
    case 0x1C: {                                        /* loc_39C2A: until Silver Sonic lands */
        uint32_t a1 = ptr(OB16(o, MS_PAR));
        if (OB8(a1, OBJ_status) & ST_YFLIP_) {
            SET8(a1, OBJ_status, OB8(a1, OBJ_status) & ~ST_YFLIP_);
            SET8(o, OBJ_routine, 0x1E);
        }
        o_draw(o);
        return;
    }
    case 0x1E:                                          /* loc_39C42: the shutter opens ($FC) */
        o_animate_s2(o, o_asset_anim(S2A_DEZWINDOW));
        o_draw(o);
        return;
    case 0x20: {                                        /* loc_39C50: Eggman watches */
        uint32_t a1 = ptr(OB16(o, MS_PAR));
        if (r32(a1) != S2(0xAF) || (OB8(a1, OBJ_status) & ST_MISC)) {
            SET8(o, OBJ_routine, 0x22);
            SET8(o, OBJ_anim, 1);
            o_draw(o);
            return;
        }
        SET8(o, OBJ_anim, 2);
        if (OB8(RAM_Player_1, OBJ_routine) == 4) SET8(o, OBJ_anim, 3);      /* Sonic hurt: he laughs */
        else if (!OB8(a1, OBJ_coll_flags)) SET8(o, OBJ_anim, 4);            /* hit */
        o_animate_s2(o, o_asset_anim(S2A_DEZWINDOW));
        o_draw(o);
        return;
    }
    default:                                            /* loc_39CA0: the shutter closes */
        o_animate_s2(o, o_asset_anim(S2A_DEZWINDOW));
        gone(o);
        return;
    }
}

/* ========================================================================
 * Object C7 - the Death Egg Robot. Its part pointers (S2 objoff_2C..3E)
 * sit at $30..$42 here: C7(off) maps them.
 * ====================================================================== */
#define C7(off)    (0x30 + ((off) - 0x2C))
#define RB_2ND     0x2E               /* routine_secondary (hit parts: Touch_Enemy writes $25) */
#define RB_2A      0x44               /* objoff_2A */
#define RB_1F      0x45               /* objoff_1F: group animation step */
#define RB_TARGET  0x46               /* objoff_28 (word): where the sensor locked on */
#define RB_PAR     C7(0x2C)           /* parts: objoff_2C, the body */
#define RB_SAVEY   C7(0x2E)           /* parts: objoff_2E */
#define ST_NOBAL   0x80               /* status.npc.no_balancing: beaten */

static uint32_t rb_part(uint32_t o, int off) { return ptr(OB16(o, C7(off))); }

static uint32_t rb_child(uint32_t o, int off, uint8_t routine)   /* LoadChildObject */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    if (off) SET16(o, C7(off), (uint16_t)a1);
    SET32(a1, OBJ_code, S2(0xC7));
    SET8(a1, OBJ_subtype, routine);
    SET16(a1, RB_PAR, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    return a1;
}

static const int8_t rb_deltas[7][3] = {             /* ObjC7_ChildDeltas */
    { 0x2E, -4, 0x3C }, { 0x30, -0x0C, 8 }, { 0x32, 0x0C, -8 }, { 0x34, 4, 0x24 },
    { 0x3A, -4, 0x3C }, { 0x3C, -0x0C, 8 }, { 0x3E, 4, 0x24 },
};

static void rb_position(uint32_t o)                  /* ObjC7_PositionChildren */
{
    int flip = OB8(o, OBJ_render) & 1;
    for (int k = 0; k < 7; k++) {
        uint32_t a2 = rb_part(o, rb_deltas[k][0]);
        int dx = rb_deltas[k][1];
        SET16(a2, OBJ_x_pos, OB16(o, OBJ_x_pos) + (flip ? -dx : dx));
        SET16(a2, OBJ_y_pos, OB16(o, OBJ_y_pos) + rb_deltas[k][2]);
    }
}

static void rb_follow(uint32_t o, int16_t dx, int16_t dy)      /* loc_3E282 */
{
    uint32_t a2 = ptr(OB16(o, RB_PAR));
    SET16(o, OBJ_x_pos, OB16(a2, OBJ_x_pos) + ((OB8(a2, OBJ_render) & 1) ? -dx : dx));
    SET16(o, OBJ_y_pos, OB16(a2, OBJ_y_pos) + dy);
}

/* ---- group animation (loc_3E1AA): each step nudges a set of parts ---- */
typedef struct { uint8_t max; uint8_t n; struct { uint8_t off; int8_t dx, dy; } p[7]; } RbStep;

static const RbStep rb_walk_steps[9] = {            /* ObjC7_GroupAni_3E318 */
    { 8, 6, { { 0, -0x20, 0x0C }, { 0x30, -0x20, 0x0C }, { 0x32, -0x20, 0x0C }, { 0x3C, -0x20, 0x0C }, { 0x34, -8, 4 }, { 0x3E, -8, 4 } } },
    { 8, 6, { { 0, -0x14, 0x14 }, { 0x30, -0x14, 0x14 }, { 0x32, -0x14, 0x14 }, { 0x3C, -0x14, 0x14 }, { 0x34, -6, 6 }, { 0x3E, -6, 6 } } },
    { 8, 6, { { 0, -8, 0x14 }, { 0x30, -8, 0x14 }, { 0x32, -8, 0x14 }, { 0x3C, -8, 0x14 }, { 0x34, -2, 4 }, { 0x3E, -2, 4 } } },
    { 8, 6, { { 0, -4, 0x0C }, { 0x30, -4, 0x0C }, { 0x32, -4, 0x0C }, { 0x3C, -4, 0x0C }, { 0x34, 0, 2 }, { 0x3E, 0, 2 } } },
    { 8, 1, { { 0, 0, 0 } } },
    { 8, 6, { { 0, 4, -0x18 }, { 0x30, 4, -0x18 }, { 0x32, 4, -0x18 }, { 0x3C, 4, -0x18 }, { 0x34, 2, -6 }, { 0x3E, 2, -6 } } },
    { 8, 6, { { 0, 0x0C, -0x18 }, { 0x30, 0x0C, -0x18 }, { 0x32, 0x0C, -0x18 }, { 0x3C, 0x0C, -0x18 }, { 0x34, 4, -4 }, { 0x3E, 4, -4 } } },
    { 8, 6, { { 0, 0x18, -0x0C }, { 0x30, 0x18, -0x0C }, { 0x32, 0x18, -0x0C }, { 0x3C, 0x18, -0x0C }, { 0x34, 4, -4 }, { 0x3E, 4, -4 } } },
    { 8, 6, { { 0, 0x18, -4 }, { 0x30, 0x18, -4 }, { 0x32, 0x18, -4 }, { 0x3C, 0x18, -4 }, { 0x34, 6, -2 }, { 0x3E, 6, -2 } } },
};
static const RbStep rb_rise_steps[3] = {            /* ObjC7_GroupAni_3E3D8 */
    { 0x10, 6, { { 0, 0, 4 }, { 0x30, 0, 4 }, { 0x32, 0, 4 }, { 0x3C, 0, 4 }, { 0x34, 0, 4 }, { 0x3E, 0, 4 } } },
    { 0x10, 1, { { 0, 0, 0 } } },
    { 8, 6, { { 0, 0, -8 }, { 0x30, 0, -8 }, { 0x32, 0, -8 }, { 0x3C, 0, -8 }, { 0x34, 0, -8 }, { 0x3E, 0, -8 } } },
};
static const RbStep rb_step_steps[12] = {           /* ObjC7_GroupAni_3E438 */
    { 0x20, 7, { { 0x34, -8, -8 }, { 0x2E, -8, -8 }, { 0, 0, -4 }, { 0x30, 4, -5 }, { 0x32, 3, -5 }, { 0x3C, -4, -5 }, { 0x3E, 0, -2 } } },
    { 0x10, 7, { { 0x34, -0x10, -4 }, { 0x2E, -0x10, -4 }, { 0, -0x10, -4 }, { 0x30, -0x0C, -5 }, { 0x32, -0x0D, -5 }, { 0x3C, -0x14, -5 }, { 0x3E, -8, 0 } } },
    { 0x10, 6, { { 0x34, -8, 4 }, { 0x2E, -8, 4 }, { 0, -8, 4 }, { 0x30, -4, 3 }, { 0x32, -5, 3 }, { 0x3C, -0x0C, 3 } } },
    { 0x10, 7, { { 0x34, -4, 0x10 }, { 0x2E, -8, 0x10 }, { 0, 0, 8 }, { 0x30, -8, 0x0A }, { 0x32, -6, 0x0A }, { 0x3C, 8, 0x0A }, { 0x3E, 0, 8 } } },
    { 0x20, 7, { { 0x34, -2, -2 }, { 0, -0x0C, -4 }, { 0x30, -0x10, -3 }, { 0x32, -0x0F, -3 }, { 0x3C, -8, -3 }, { 0x3E, -0x14, -6 }, { 0x3A, -0x18, -4 } } },
    { 0x20, 5, { { 0x3E, -8, -4 }, { 0x3A, -8, -4 }, { 0x30, -4, -1 }, { 0x32, -3, -1 }, { 0x3C, 4, -1 } } },
    { 0x10, 7, { { 0x3E, -0x10, -4 }, { 0x3A, -0x10, -4 }, { 0, -0x10, -4 }, { 0x30, -0x14, -5 }, { 0x32, -0x13, -5 }, { 0x3C, -0x0C, -5 }, { 0x34, -8, 0 } } },
    { 0x10, 6, { { 0x3E, -8, 4 }, { 0x3A, -8, 4 }, { 0, -8, 4 }, { 0x30, -0x0C, 3 }, { 0x32, -0x0B, 3 }, { 0x3C, -4, 3 } } },
    { 0x10, 7, { { 0x3E, -4, 0x10 }, { 0x3A, -8, 0x10 }, { 0, 0, 8 }, { 0x30, 8, 0x0A }, { 0x32, 6, 0x0A }, { 0x3C, -8, 0x0A }, { 0x34, 0, 8 } } },
    { 0x20, 7, { { 0x3E, -2, -2 }, { 0, -0x0C, -4 }, { 0x30, -8, -3 }, { 0x32, -9, -3 }, { 0x3C, -0x0F, -3 }, { 0x34, -0x14, -6 }, { 0x2E, -0x18, -4 } } },
    { 0x20, 5, { { 0x34, -8, -4 }, { 0x2E, -8, -4 }, { 0x30, 4, -1 }, { 0x32, 3, -1 }, { 0x3C, -4, -1 } } },
    { 0x10, 7, { { 0x3E, 0, 8 }, { 0x3A, 0, 8 }, { 0, 0, 8 }, { 0x30, 0, 8 }, { 0x32, 0, 8 }, { 0x3C, 0, 8 }, { 0x34, 0, 8 } } },
};

/* step lists: >= $C0 ends ($C0 restart + done, $80 flag = run backwards, $40 = sound) */
static const uint8_t rb_crouch[] = { 0, 1, 2, 3, 0xFF, 0 };                     /* off_3E2F6 */
static const uint8_t rb_uncrouch[] = { 5, 6, 7, 8, 0xFF, 0 };                   /* off_3E300 */
static const uint8_t rb_slam[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xC0 };            /* off_3E30A */
static const uint8_t rb_rise[] = { 0, 1, 2, 0xC0 };                             /* off_3E3D0 */
static const uint8_t rb_walk[] = {                                               /* off_3E40C */
    0, 1, 2, 3, 0x40, 1, 4, 5, 6, 7, 8, 0x40, 1, 9, 0x0A, 1, 2, 3, 0x40, 1, 4, 5, 6, 7, 8, 0x40, 1, 0xC0,
};
static const uint8_t rb_walkback[] = { 0x88, 0x87, 0x86, 0x85, 0x0B, 0x40, 1, 0xC0 };   /* off_3E42C */

/* loc_3E1AA: returns 1 when the list ended (a $C0) */
static int rb_group(uint32_t o, const RbStep *steps, const uint8_t *list)
{
    int back = 0;
    uint8_t d0 = list[OB8(o, OBJ_anim_frame)];
    while (d0 & 0xC0) {                                 /* loc_3E23E */
        uint8_t kind = d0 >> 6;
        d0 &= 0x3F;
        if (kind == 1) {                                /* loc_3E252: a sound (once per step) */
            if (OB8(o, RB_1F)) break;
            uint8_t i = (uint8_t)(OB8(o, OBJ_anim_frame) + 1);
            o_sfx_s2(SFX_HAMMER);                  /* (the lists only name SndID_Hammer) */
            i++;
            SET8(o, OBJ_anim_frame, i);
            d0 = list[i];
            continue;
        }
        if (kind == 2) { back = 1; break; }             /* loc_3E27A: run the step backwards */
        SET8(o, OBJ_anim_frame, 0);                     /* loc_3E27E: list ended */
        return 1;
    }
    const RbStep *s = &steps[d0];
    uint8_t d2 = (uint8_t)(OB8(o, RB_1F) + 1);
    if (d2 >= s->max) {
        SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
        d2 = 0;
    }
    SET8(o, RB_1F, d2);
    int flip = OB8(o, OBJ_render) & 1;
    for (int k = 0; k < s->n; k++) {
        uint32_t a3 = s->p[k].off ? rb_part(o, s->p[k].off) : o;
        int32_t dx = (int32_t)s->p[k].dx * 16, dy = (int32_t)s->p[k].dy * 16;
        if (flip) dx = -dx;
        if (back) { dx = -dx; dy = -dy; }
        w32(a3 + OBJ_x_pos, r32(a3 + OBJ_x_pos) + (uint32_t)(dx << 8));
        w32(a3 + OBJ_y_pos, r32(a3 + OBJ_y_pos) + (uint32_t)(dy << 8));
    }
    return 0;
}

static const uint8_t rb_parts[10] = { 0x2C, 0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E };

static void rb_set_collision(uint32_t o, int on)    /* ObjC7_InitCollision / _RemoveCollision */
{
    static const uint8_t coll[10] = { 0, 0x8F, 0x9C, 0, 0x86, 0x2D, 0x8B, 0x8F, 0x9C, 0x8B };   /* head $2A -> $2D */
    for (int k = 0; k < 10; k++) SET8(rb_part(o, rb_parts[k]), OBJ_coll_flags, on ? coll[k] : 0);
}

static void rb_bombs(uint32_t o)                     /* CreateEggmanBombs */
{
    static const int16_t v[2][2] = { { 0x60, -0x800 }, { 0xC0, -0xA00 } };
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = rb_child(o, 0, 0x1C);
        if (!a1) return;
        SET16(a1, OBJ_x_vel, (uint16_t)((OB8(o, OBJ_render) & 1) ? -v[k][0] : v[k][0]));
        SET16(a1, OBJ_y_vel, (uint16_t)v[k][1]);
    }
}

static void rb_face(uint32_t o)                      /* loc_3E168: parts face as the body */
{
    uint8_t d0 = OB8(o, OBJ_render) & 1;
    for (int k = 0; k < 10; k++) {
        uint32_t a2 = rb_part(o, rb_parts[k]);
        SET8(a2, OBJ_render, (OB8(a2, OBJ_render) & ~1) | d0);
        SET8(a2, OBJ_status, (OB8(a2, OBJ_status) & ~1) | d0);
    }
}

static int rb_toward(uint32_t o)                     /* Obj_GetOrientationToPlayer, x-flip adjusted: 0 = facing Sonic */
{
    int d0;
    s2_orient(o, &d0, NULL, NULL, NULL);
    if (OB8(o, OBJ_render) & 1) d0 -= 2;
    return d0;
}

static void rb_beaten(uint32_t o)                    /* ObjC7_Beaten */
{
    static const uint8_t brk[8] = { 0x2C, 0x2E, 0x30, 0x32, 0x34, 0x3A, 0x3C, 0x3E };
    static const int16_t spd[8][2] = { { 0x200, -0x400 }, { -0x100, -0x100 }, { 0x300, -0x300 }, { -0x100, -0x400 },
                                       { 0x180, -0x200 }, { -0x200, -0x300 }, { 0, -0x400 }, { 0x100, -0x300 } };
    D(0) = 100;                                         /* AddPoints: 1000 */
    tri_jsr(S3K_HUD_AddToScore);
    SET8(o, OBJ_anim_timer, 0);
    SET8(o, RB_2ND, 0x0E);
    SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_NOBAL);
    SET8(o, OBJ_anim, 0);
    SET8(o, OBJ_coll_flags, 0);
    SET16(o, OBJ_x_vel, 0);
    SET16(o, OBJ_y_vel, 0);
    rb_set_collision(o, 0);
    for (int k = 0; k < 8; k++) {                       /* ObjC7_Break */
        uint32_t a3 = rb_part(o, brk[k]);
        SET8(a3, OBJ_routine, 0x1E);
        SET8(a3, RB_2ND, 0);
        SET16(a3, RB_2A, 0x80);
        SET16(a3, OBJ_x_vel, (uint16_t)spd[k][0]);
        SET16(a3, OBJ_y_vel, (uint16_t)spd[k][1]);
    }
    o_delete(rb_part(o, 0x38));                         /* the jet */
}

/* 1: beaten (the caller returns) */
static int rb_check_hit(uint32_t o)                  /* ObjC7_CheckHit */
{
    if (!OB8(o, OBJ_coll_prop)) { rb_beaten(o); return 1; }
    if (!OB8(o, RB_2A)) {
        if (OB8(o, OBJ_coll_flags)) {
            uint32_t head = rb_part(o, 0x36);
            if (OB8(head, OBJ_coll_flags)) return 0;      /* not hit (rb_body's tail registers the touches) */
            SET8(o, OBJ_coll_flags, 0);                 /* the head was hit */
            SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) - 1);
            if (!OB8(o, OBJ_coll_prop)) { rb_beaten(o); return 1; }
        }
        SET8(o, RB_2A, 60);
        o_sfx_s2(SFX_BOSSHIT);
    }
    flash();                                            /* ObjC7_Flashing */
    SET8(o, RB_2A, OB8(o, RB_2A) - 1);
    if (!OB8(o, RB_2A)) {
        w16(FLASH_COLOUR, 0);
        SET8(o, OBJ_coll_flags, 0x16);
        SET8(rb_part(o, 0x36), OBJ_coll_flags, 0x2D);
    }
    return 0;
}

static void rb_touch_parts(uint32_t o)
{
    for (int k = 0; k < 10; k++) {
        uint32_t a = rb_part(o, rb_parts[k]);
        if (r32(a) == S2(0xC7) && OB8(a, OBJ_coll_flags)) o_touch(a);
    }
}

static void rb_body_fight(uint32_t o)                /* loc_3D684 */
{
    if (rb_check_hit(o)) return;
    uint8_t mode = OB8(o, OBJ_anim), st = OB8(o, OBJ_prev_anim);
    if (mode == 0) {                                    /* loc_3D6AA: walk at Sonic */
        switch (st) {
        case 0: case 4:
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) < 0) SET8(o, OBJ_prev_anim, st + 2);
            return;
        case 2:
            if (rb_group(o, rb_step_steps, rb_walk)) { SET8(o, OBJ_prev_anim, 4); SET8(o, OBJ_anim_timer, 0x40); }
            return;
        default:
            if (rb_group(o, rb_step_steps, rb_walkback)) { SET8(o, RB_2ND, OB8(o, RB_2ND) - 2); SET8(o, OBJ_anim_timer, 0x40); }
            return;
        }
    }
    if (mode == 2) {                                    /* loc_3D702: jet jump and slam */
        switch (st) {
        case 0: case 6:
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) < 0) SET8(o, OBJ_prev_anim, st + 2);
            return;
        case 2:
            if (rb_group(o, rb_rise_steps, rb_rise)) {
                SET8(o, OBJ_prev_anim, 4);
                SET8(o, OBJ_anim_timer, 0x80);
                SET16(o, OBJ_x_vel, 0);
                SET16(o, OBJ_y_vel, (uint16_t)-0x200);
            }
            return;
        case 4:
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) {
                if (!(o_frame_counter() & 0x1F)) o_sfx_s2(SFX_FIRE);
                o_move(o);
                rb_position(o);
                return;
            }
            SET8(o, OBJ_prev_anim, 6);
            SET16(o, OBJ_y_vel, 0);
            rb_child(o, 0, 0x18);                       /* the targeting sensor */
            SET16(o, OBJ_x_vel, 0);
            SET16(o, RB_TARGET, 0);
            return;
        case 8: {
            uint16_t d0 = OB16(o, RB_TARGET);
            if (!d0) return;
            SET8(o, OBJ_prev_anim, 0x0A);
            SET16(o, OBJ_x_pos, d0);
            SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (d0 < 0x780 ? 1 : 0));
            rb_face(o);
            SET16(o, OBJ_y_vel, 0x800);
            SET8(o, OBJ_anim_timer, 0x20);
            return;
        }
        case 0x0A:
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) { o_move(o); rb_position(o); return; }
            SET8(o, OBJ_prev_anim, 0x0C);
            SET16(o, OBJ_y_vel, 0);
            s2_dez_shake = 1;
            s2_dez_shake_timer = 0x40;
            SET8(rb_part(o, 0x38), RB_2ND, 6);
            o_sfx_s2(SFX_SMASH);
            return;
        case 0x0C:
            if (!rb_group(o, rb_walk_steps, rb_slam)) return;
            rb_position(o);
            if (!rb_toward(o)) { SET8(o, RB_2ND, OB8(o, RB_2ND) - 2); return; }
            SET8(o, OBJ_prev_anim, 0x0E);
            SET8(o, OBJ_anim_timer, 0x60);
            rb_bombs(o);
            return;
        default:                                        /* $E */
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) < 0) SET8(o, RB_2ND, OB8(o, RB_2ND) - 2);
            return;
        }
    }
    switch (st) {                                       /* loc_3D83C: punch / bombs */
    case 0: case 4: case 8:
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) SET8(o, OBJ_prev_anim, st + 2);
        return;
    case 2:
        SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_P2_PUSH);    /* (hides the front forearm: it's out) */
        if (!rb_group(o, rb_walk_steps, rb_crouch)) return;
        if (!rb_toward(o)) {
            SET8(o, OBJ_prev_anim, 4);
            SET8(o, OBJ_anim_timer, 0x40);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_P2_STAND);   /* front forearm: punch */
            return;
        }
        SET8(o, OBJ_prev_anim, 8);
        SET8(o, OBJ_anim_timer, 0x20);
        rb_bombs(o);
        return;
    case 6:
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) return;
        SET8(o, OBJ_prev_anim, 8);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_P1_PUSH);    /* back forearm: punch */
        SET8(o, OBJ_anim_timer, 0x40);
        return;
    default:                                            /* $A */
        if (!rb_group(o, rb_walk_steps, rb_uncrouch)) return;
        SET8(o, RB_2ND, OB8(o, RB_2ND) - 2);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P2_PUSH);
        return;
    }
}

static void rb_body(uint32_t o)                      /* ObjC7_Body */
{
    switch (OB8(o, RB_2ND)) {
    case 0x00:
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 3);
        SET16(o, OBJ_priority, 5 << 7);
        rb_child(o, 0x2C, 4);                           /* shoulder */
        rb_child(o, 0x30, 8);                           /* front forearm */
        rb_child(o, 0x2E, 6);                           /* front lower leg */
        rb_child(o, 0x32, 0x0A);                        /* arm */
        rb_child(o, 0x34, 0x0C);                        /* front thigh */
        rb_child(o, 0x36, 0x0E);                        /* head */
        rb_child(o, 0x38, 0x10);                        /* jet */
        rb_child(o, 0x3A, 0x12);                        /* back lower leg */
        rb_child(o, 0x3C, 0x14);                        /* back forearm */
        rb_child(o, 0x3E, 0x16);                        /* back thigh */
        rb_position(o);
        break;
    case 0x02:
        if (OB8(o, OBJ_status) & ST_MISC) {             /* Eggman's in */
            SET8(o, RB_2ND, 4);
            SET8(o, OBJ_anim_timer, 60);
            D(0) = 0xE1;
            tri_jsr(S3K_Play_Music);
        }
        break;
    case 0x04:
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) break;
        SET8(o, RB_2ND, 6);
        SET8(o, OBJ_anim_timer, 0x79);
        SET16(o, OBJ_y_vel, (uint16_t)-0x100);
        SET8(rb_part(o, 0x38), RB_2ND, 4);
        D(0) = MUS_ENDBOSS;
        tri_jsr(S3K_Play_Music);
        break;
    case 0x06:                                          /* rises into the room */
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if (OB8(o, OBJ_anim_timer)) {
            o_sfx_s2(SFX_RUMBLE);
            o_move(o);
            rb_position(o);
            break;
        }
        SET8(o, RB_2ND, 8);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_anim_timer, 0x1F);
        SET8(o, OBJ_coll_flags, 0x16);
        SET8(o, OBJ_coll_prop, 0x0C);
        rb_set_collision(o, 1);
        SET8(rb_part(o, 0x38), RB_2ND, 6);
        break;
    case 0x08:
        if (getenv("TRILOGY_BOSSKILL")) SET8(o, OBJ_coll_prop, 0);   /* development */
        if (rb_check_hit(o)) break;
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) SET8(o, RB_2ND, 0x0A);
        break;
    case 0x0A: {                                        /* loc_3D640: next move */
        static const uint8_t move[4] = { 2, 0, 2, 4 };
        if (rb_check_hit(o)) break;
        SET8(o, RB_2ND, 0x0C);
        SET8(o, OBJ_anim_timer, 0x20);
        uint8_t d0 = (uint8_t)(OB8(o, OBJ_angle) + 1);
        SET8(o, OBJ_angle, d0);
        d0 = move[d0 & 3];
        SET8(o, OBJ_anim, d0);
        SET8(o, OBJ_prev_anim, 0);
        if (d0 == 2) {
            uint32_t jet = rb_part(o, 0x38);
            SET8(jet, RB_2ND, 4);
            SET8(jet, OBJ_anim, 2);
        }
        break;
    }
    case 0x0C:
        rb_body_fight(o);
        break;
    default:                                            /* $E: loc_3D8D2, beaten */
        switch (OB8(o, OBJ_anim)) {
        case 0:
            boss_explosion(o);
            o_fall(o);
            if (OB16(o, OBJ_y_pos) < 0x15C) break;
            SET16(o, OBJ_y_pos, 0x15C);
            {
                int16_t v = (int16_t)OB16(o, OBJ_y_vel);
                if (v >= 0) {
                    uint16_t d0 = (uint16_t)v >> 2;
                    if (d0 >= 0x100) { SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)d0); break; }
                }
            }
            SET8(o, OBJ_anim, 2);
            SET8(o, OBJ_anim_timer, 0x40);
            break;
        case 2:
            SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
            if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) { boss_explosion(o); break; }
            SET8(o, OBJ_anim, 4);
            w8(RAM_Ctrl_1_locked, 0xFF);
            w16(RAM_Camera_max_X_pos, 0x1000);
            break;
        default:                                        /* Sonic runs off */
            w16(RAM_Ctrl_1_held_logical, 0x0808);
            if (r16(RAM_Camera_X_pos) < 0x840) break;
            SET8(o, OBJ_routine, 0x20);
            SET8(o, RB_2ND, 0);
            SET16(o, RB_2A, 0x20);
            s2_dez_shake = 1;
            s2_dez_shake_timer = 0x1000;
            o_delete(rb_part(o, 0x36));
            break;
        }
        break;
    }
    rb_touch_parts(o);
    if (r32(o) == S2(0xC7) && OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

static void rb_ending(uint32_t o)                    /* ObjC7_SetupEnding */
{
    if (!(o_frame_counter() & 0x1F)) {
        o_sfx_s2(SFX_RUMBLE2);
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
    }
    uint32_t p = RAM_Player_1;
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos) - OB16(o, RB_2A));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    {                                                   /* loc_3DFBA: the Death Egg blows up around Sonic */
        uint32_t a1 = o_alloc();
        if (a1) {
            uint32_t r = (uint32_t)o_random();
            SET32(a1, OBJ_code, S3K_Obj_Explosion);
            SET8(a1, OBJ_routine, 6);
            SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + (int16_t)(((r & 0xFF) >> 2) - 0x30)));
            SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + (int16_t)((((r >> 8) & 0xFF) >> 2) - 0x30)));
            if (!(o_frame_counter() & 7)) o_sfx_s2(SFX_EXPLODE);
        }
    }
    if (!OB8(o, RB_2ND)) {
        if (OB16(p, OBJ_x_pos) < 0xEC0) return;
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_anim_timer, 0x16);
        return;
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if (OB8(o, OBJ_anim_timer)) {                       /* Pal_FadeToWhite.UpdateColour, all 64 colours */
        for (uint32_t a = RAM_Normal_palette; a < RAM_Normal_palette + 0x80; a += 2) {
            uint16_t c = r16(a);
            if (c == 0xEEE) continue;
            if ((c & 0x00E) != 0x00E) w16(a, c + 2);
            else if ((c & 0x0E0) != 0x0E0) w16(a, c + 0x20);
            else if ((c & 0xE00) != 0xE00) w16(a, c + 0x200);
        }
        return;
    }
    for (uint32_t a = RAM_Normal_palette; a < RAM_Normal_palette + 0x80; a += 2) w16(a, 0xEEE);
    D(0) = 0xE1;
    tri_jsr(S3K_Play_Music);
    o_delete(o);
    tri_s2_complete();                                  /* GameModeID_EndingSequence */
}

static void rb_part_draw(uint32_t o) { if (r32(o)) o_draw(o); }

static void rb_forearm(uint32_t o, int back)         /* ObjC7_FrontForearm / BackForearm: the punch */
{
    uint32_t body = ptr(OB16(o, RB_PAR));
    switch (OB8(o, RB_2ND)) {
    case 0:
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 6);
        if (back) SET16(o, OBJ_priority, 5 << 7);
        break;
    case 2: {
        uint8_t bit = back ? ST_P1_PUSH : ST_P2_STAND;
        if (!(OB8(body, OBJ_status) & bit)) break;
        SET8(body, OBJ_status, OB8(body, OBJ_status) & ~bit);
        SET8(o, RB_2ND, 4);
        SET16(o, RB_2A, 0x10);
        SET16(o, RB_SAVEY, OB16(o, OBJ_y_pos));
        break;
    }
    case 4:                                             /* loc_3DACC: wind up */
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) >= 0) {
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x20);
            o_move(o);
            break;
        }
        SET8(o, RB_2ND, 6);
        SET16(o, RB_2A, 0x20);
        {
            static const int16_t yv[4] = { 0x200, 0x100, 0x80, 0 };
            int d1;
            int16_t d2;
            s2_orient(o, NULL, &d1, &d2, NULL);
            int ad = d2 < 0 ? -d2 : d2;
            if (ad >= 0x100) ad = 0xFF;
            int16_t v = yv[(ad & 0xC0) >> 6];
            if (!d1) v = (int16_t)-v;
            SET16(o, OBJ_y_vel, (uint16_t)v);
            SET16(o, OBJ_x_vel, (OB8(o, OBJ_render) & 1) ? 0x800 : (uint16_t)-0x800);
            o_sfx_s2(SFX_DASH);
        }
        break;
    case 6:                                             /* loc_3DB32: out */
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) >= 0) { o_move(o); break; }
        SET8(o, RB_2ND, 8);
        SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        SET16(o, RB_2A, 0x20);
        SET16(o, OBJ_y_vel, (uint16_t)((int16_t)(OB16(o, RB_SAVEY) - OB16(o, OBJ_y_pos)) << 3));
        break;
    default:                                            /* loc_3DB5A: back */
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) >= 0) { o_move(o); break; }
        SET8(o, RB_2ND, 2);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0);
        break;
    }
    if (!back && (OB8(o, OBJ_status) & ST_P2_PUSH)) return;    /* (S2 checks its own bit: never set) */
    rb_part_draw(o);
}

static void rb_head(uint32_t o)                      /* ObjC7_Head */
{
    switch (OB8(o, RB_2ND)) {
    case 0:
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 0x15);
        break;
    case 2:
        if (!s2_dez_eggman || !(OB8(ptr(s2_dez_eggman), OBJ_status) & ST_P1_STAND)) break;
        SET8(o, RB_2ND, 4);
        SET16(o, RB_2A, 0x40);
        break;
    case 4:
        animate_2nd(o, S2A_DEZBOSS, RB_2ND);            /* Ani_objC7_a: Eggman climbs in ($FA) */
        break;
    case 6:
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) < 0) {
            SET8(o, RB_2ND, 8);
            uint32_t body = ptr(OB16(o, RB_PAR));
            SET8(body, OBJ_status, OB8(body, OBJ_status) | ST_MISC);
        }
        break;
    default:
        SET8(o, OBJ_coll_prop, 0xFF);
        break;
    }
    rb_follow(o, 0, -0x34);
    rb_part_draw(o);
}

static void rb_jet(uint32_t o)                       /* ObjC7_Jet */
{
    switch (OB8(o, RB_2ND)) {
    case 0:
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 0x0C);
        break;
    case 2: case 8:
        SET8(o, OBJ_anim, 3);
        animate_2nd(o, S2A_DEZJET, RB_2ND);
        o_draw(o);
        break;
    case 4:
        animate_2nd(o, S2A_DEZJET, RB_2ND);
        o_draw(o);
        break;
    default:                                            /* 6 */
        SET8(o, OBJ_anim, 1);
        animate_2nd(o, S2A_DEZJET, RB_2ND);
        o_draw(o);
        break;
    }
    rb_follow(o, 0x38, 0x18);
}

static void rb_sensor(uint32_t o)                    /* ObjC7_TargettingSensor: the crosshair chases Sonic */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, RB_2ND)) {
    case 0:
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET16(o, OBJ_priority, 1 << 7);
        SET16(o, RB_2A, 0xA0);
        SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
        SET16(o, 0x34, OB16(p, OBJ_x_vel));             /* S2 objoff_30..3F: 4 frames of Sonic's speed */
        SET16(o, 0x36, OB16(p, OBJ_y_vel));
        SET8(o, OBJ_angle, 0);
        SET8(o, 0x27, 0x18);
        o_draw(o);
        return;
    case 2:
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) < 0) {
            SET8(o, RB_2ND, 4);
            SET16(o, RB_2A, 0x40);
            SET8(o, OBJ_angle, 4);
            SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
            SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
            rb_child(o, 0, 0x1A);                       /* the lock */
            SET16(o, OBJ_x_vel, 0);
            o_draw(o);
            return;
        }
        SET16(o, OBJ_y_vel, OB16(o, 0x42));             /* oldest */
        SET16(o, OBJ_x_vel, OB16(o, 0x40));
        for (int k = 3; k > 0; k--) w32(o + 0x34 + (uint32_t)k * 4u, r32(o + 0x34 + (uint32_t)(k - 1) * 4u));
        {
            uint16_t xv = OB16(p, OBJ_x_vel), yv = OB16(p, OBJ_y_vel);
            if (!xv) SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
            if (!yv) SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
            SET16(o, 0x34, xv);
            SET16(o, 0x36, yv);
        }
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_DEZTARGET));
        SET8(o, OBJ_angle, OB8(o, OBJ_angle) - 1);
        if ((int8_t)OB8(o, OBJ_angle) < 0) {
            SET8(o, 0x27, OB8(o, 0x27) - 1);
            SET8(o, OBJ_angle, OB8(o, 0x27));
            o_sfx_s2(SFX_BEEP);
        }
        o_draw(o);
        return;
    default:
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) < 0) {
            SET16(ptr(OB16(o, RB_PAR)), RB_TARGET, OB16(o, OBJ_x_pos));
            o_delete(o);
            return;
        }
        o_animate_s2(o, o_asset_anim(S2A_DEZTARGET));
        SET8(o, OBJ_angle, OB8(o, OBJ_angle) - 1);
        if ((int8_t)OB8(o, OBJ_angle) < 0) {
            SET8(o, OBJ_angle, 4);
            o_sfx_s2(SFX_BEEP);
        }
        o_draw(o);
        return;
    }
}

static void rb_lock(uint32_t o)                      /* ObjC7_TargettingLock */
{
    if (!OB8(o, RB_2ND)) {
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 0x14);
        SET16(o, OBJ_priority, 1 << 7);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET16(o, RB_2A, 4);
        o_draw(o);
        return;
    }
    uint32_t a1 = ptr(OB16(o, RB_PAR));
    if (r32(a1) != S2(0xC7)) { o_delete(o); return; }
    SET16(o, RB_2A, OB16(o, RB_2A) - 1);
    if (!OB16(o, RB_2A)) {
        SET16(o, RB_2A, 4);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) ^ 0x2000);  /* palette_bit_0 */
    }
    o_draw(o);
}

static void rb_bomb(uint32_t o)                      /* ObjC7_EggmanBomb */
{
    uint32_t body = ptr(OB16(o, RB_PAR));
    switch (OB8(o, RB_2ND)) {
    case 0:
        SET8(o, RB_2ND, 2);
        SET8(o, OBJ_frame, 0x0E);
        SET8(o, OBJ_coll_flags, 0x89);
        SET16(o, OBJ_priority, 5 << 7);
        SET8(o, OBJ_width_px, 0x0C);
        rb_follow(o, 0x38, -0x14);
        break;
    case 2:
        if (OB8(body, OBJ_status) & ST_NOBAL) goto boom;
        o_fall(o);
        if (OB16(o, OBJ_y_pos) >= 0x170) {
            SET8(o, RB_2ND, 4);
            SET16(o, OBJ_y_pos, 0x170);
            SET16(o, RB_2A, 0x40);
        }
        break;
    case 4:
        if (OB8(body, OBJ_status) & ST_NOBAL) goto boom;
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) < 0) goto boom;
        break;
    boom:                                               /* loc_3DF4C */
        SET8(o, RB_2ND, 6);
        o_asset(o, S2A_FIERY);
        SET16(o, OBJ_priority, 1 << 7);
        SET8(o, OBJ_anim_timer, 7);
        SET8(o, OBJ_frame, 0);
        o_sfx_s2(SFX_EXPLODE);
        break;
    default:
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_anim_timer, 7);
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            if (OB8(o, OBJ_frame) >= 5) {
                SET8(o, OBJ_coll_flags, 0);
                if (OB8(o, OBJ_frame) == 7) { o_delete(o); return; }
            }
        }
        break;
    }
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

static void eggrobo(uint32_t o)
{
    uint8_t r = OB8(o, OBJ_routine);
    if (!r) {                                           /* ObjC7_Init */
        o_init(o, S2A_DEZBOSS, OB8(o, OBJ_render) | 4, 4, 0x38, 0x40);
        SET8(o, OBJ_routine, OB8(o, OBJ_subtype));
        SET8(o, OBJ_coll_flags, 0);
        if (OB8(o, OBJ_routine) == 0x18 || OB8(o, OBJ_routine) == 0x1A) o_asset(o, S2A_DEZTARGET);
        if (OB8(o, OBJ_routine) == 0x10) o_asset(o, S2A_DEZJET);
        return;
    }
    switch (r) {
    case 0x02: rb_body(o); return;
    case 0x04:                                          /* shoulder */
        if (!OB8(o, RB_2ND)) { SET8(o, RB_2ND, 2); SET8(o, OBJ_frame, 4); }
        rb_follow(o, 0x0C, -0x14);
        rb_part_draw(o);
        return;
    case 0x06: case 0x12:                               /* lower legs */
        if (!OB8(o, RB_2ND)) {
            SET8(o, RB_2ND, 2);
            SET8(o, OBJ_frame, 0x0B);
            if (r == 0x12) SET16(o, OBJ_priority, 5 << 7);
        }
        rb_part_draw(o);
        return;
    case 0x08: rb_forearm(o, 0); return;
    case 0x14: rb_forearm(o, 1); return;
    case 0x0A:                                          /* arm */
        if (!OB8(o, RB_2ND)) { SET8(o, RB_2ND, 2); SET8(o, OBJ_frame, 5); }
        rb_part_draw(o);
        return;
    case 0x0C: case 0x16:                               /* thighs */
        if (!OB8(o, RB_2ND)) {
            SET8(o, RB_2ND, 2);
            SET8(o, OBJ_frame, 0x0A);
            if (r == 0x16) SET16(o, OBJ_priority, 5 << 7);
        }
        rb_part_draw(o);
        return;
    case 0x0E: rb_head(o); return;
    case 0x10: rb_jet(o); return;
    case 0x18: rb_sensor(o); return;
    case 0x1A: rb_lock(o); return;
    case 0x1C: rb_bomb(o); return;
    case 0x1E:                                          /* ObjC7_FallingPieces */
        SET16(o, RB_2A, OB16(o, RB_2A) - 1);
        if ((int16_t)OB16(o, RB_2A) < 0) { o_delete(o); return; }
        o_fall(o);
        o_draw(o);
        return;
    default: rb_ending(o); return;
    }
}

void s2_obj_dez_register(TriObjFn *t)
{
    t[0xC6] = eggman;
    t[0xAF] = mecha;
    t[0xC7] = eggrobo;
}
