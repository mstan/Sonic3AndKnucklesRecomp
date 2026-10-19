/*
 * scd_wwz.c — Wacky Workbench (Sonic CD; decomp: R6\PLAYER6.C, R6\COL6*.C;
 * dobby: Level/Wacky Workbench).
 *
 * The zone's player code keys its floors on Sonic CD's own 16 px block
 * numbers (scramapad; tri_scd_block_at reads the act's CD chunks):
 *   bfloor   block 543 bounces the player up at -$1600, curled; it is off
 *            while the floor's palette cycle (the fourth) sits on its dark
 *            step (90 in the present, 60 in the past; futures always on)
 *   sibi     blocks 579-581 (bad future 647-649) are live: they hurt, unless
 *            invincible
 *   sibi2    the electric beams' blocks, while their controller ($21) has them
 *            on (st6clrchg: up and down 426, across 449, or the diagonals)
 *   bura     block 345 above the head is a bar: the player hangs from it,
 *            moves 2 px a frame along it, lets go with a jump button
 *   kuru2    block 259 is a pole: the player spins round it (23 px either
 *            side) going up or down at $200, turning at its ends; a jump
 *            button throws him off sideways at $C00 when he next faces out
 * These run after the player's own move each frame (play00move), here from
 * TH_LVL_POST_SPRITES; on a bar or a pole the player's own move is off
 * (object_control bit 0) and the hold runs from TH_LVL_PRE_SPRITES, in its
 * place. Sonic CD has them for player 1 only, and so do we.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t tri_scd_block_at(int x, int y);
int tri_scd_palcycle_step(int k);

#define WWZ_ROUND       4                  /* (ROUND_INDEX: Wacky Workbench) */
#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_STATUS2       0x2B               /* bit 0 shield, bit 1 invincible */
#define P_OBJCTRL       0x2E
#define P_INVULN        0x34
#define P_JUMPING       0x40
#define P_DJUMP         0x2F               /* double_jump_flag (Tails' flight, Knuckles' glide) */
#define P_ARTTILE       0x0A
#define P_ANIMTIME      0x24
#define BTN_ABC         0x70
#define RAM_Ctrl_1_logical 0xFFF602u       /* (held, pressed) */

uint8_t scd_wwz3_boss_flags(void);                 /* scd_wwz_boss.c: act 3's bossFlags (0 elsewhere) */
int scd_wwz3_boss_music(void);
void scd_wwz3_level_entry(void);
int tri_scd_sparks_on(void);                        /* trilogy_scdlevel.c: the good future's sparks live */

static int wwz(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == WWZ_ROUND;
}
static int period(void) { return (tri_zone() - TRI_CD_BASE) & 3; }   /* TRI_CD_PAST.. */

static int playing(uint32_t p)
{
    return o_player_active(p) && OB8(p, P_ROUTINE) == 2 && !OB8(p, P_OBJCTRL) && !r16(RAM_Debug_placement_mode);
}

static void bounce_floor(uint32_t p)                          /* bfloor */
{
    if (scd_wwz3_boss_flags()) return;                        /* (bossflag) */
    int per = period();
    if (per == TRI_CD_PAST || per == TRI_CD_PRESENT) {        /* (time_flag < 2: the cycle's dark step) */
        int dark = per == TRI_CD_PRESENT ? 90 : 60;
        if (tri_scd_palcycle_step(3) == dark) return;
    }
    int x = (int16_t)OB16(p, OBJ_x_pos), y = (int16_t)OB16(p, OBJ_y_pos);
    int foot = y + (int8_t)OB8(p, P_YRAD) + 2, w = (int8_t)OB8(p, P_XRAD);
    if (tri_scd_block_at(x - w, foot) != 543 && tri_scd_block_at(x + w, foot) != 543) return;
    SET16(p, OBJ_y_vel, (uint16_t)-0x1600);
    uint8_t st = OB8(p, OBJ_status);
    st = (uint8_t)((st | 0x02) & ~0x30);                      /* in the air, no roll-jump, not pushing */
    SET8(p, P_JUMPING, 0);
    if (st & 0x04) { SET8(p, OBJ_status, st); return; }
    SET8(p, OBJ_status, st | 0x04);                           /* curled */
    SET8(p, P_YRAD, 14);
    SET8(p, P_XRAD, 7);
    SET16(p, OBJ_y_pos, (uint16_t)(OB16(p, OBJ_y_pos) + 5));
    SET8(p, P_ANIM, 2);
    o_sfx_cd(0xDA);                                           /* (FM $DA: the bounce, as he curls) */
}

static void live_floor(uint32_t p)                            /* sibi */
{
    if (OB8(p, P_STATUS2) & 2) return;                        /* (invincible; a shield is lost) */
    if (OB8(p, P_INVULN)) return;
    int x = (int16_t)OB16(p, OBJ_x_pos), y = (int16_t)OB16(p, OBJ_y_pos);
    if (x >= 2432 && x < 2592) return;
    if (scd_wwz3_boss_flags() & 0x80) return;                 /* (the boss beaten) */
    if (tri_act() == 2) {                                     /* (act 3: the shaft's, while the boss is on; never in the good future) */
        if (x >= 2576 && (!scd_wwz3_boss_music() || period() == TRI_CD_GOOD)) return;
    } else if (period() == TRI_CD_GOOD && !tri_scd_sparks_on())
        return;                                               /* (the good future's sparks: on 30 frames, off 90) */
    uint16_t b = tri_scd_block_at(x, y);
    int lo = period() == TRI_CD_BAD ? 647 : 579;
    if (b >= lo && b < lo + 3) o_hurt_player(p, p);
}

/* ---- the beams, the bar, the pole (player 1) ---------------------------- */
int scd_wwz_beams_live(void);                                 /* scd_wwz_gim.c: st6clrchg (0 off) */
int scd_time_warping(void);
void scd_time_cancel(void);

static void beams(uint32_t p)                                 /* sibi2 */
{
    static const int16_t up[] = { 426, -1 }, across[] = { 449, -1 },
                         diag[] = { 444, 475, 468, 465, 419, 409, 408, 418, -1 };
    static const int16_t *const modes[] = { up, across, diag };
    int mode = scd_wwz_beams_live();
    if (!mode || OB8(p, P_ROUTINE) >= 4 || scd_time_warping() || (OB8(p, P_STATUS2) & 2) || OB8(p, P_INVULN))
        return;
    uint16_t b = tri_scd_block_at((int16_t)OB16(p, OBJ_x_pos), (int16_t)OB16(p, OBJ_y_pos));
    for (const int16_t *t = modes[(mode - 1) % 3]; *t >= 0; t++)
        if (b == *t) { o_hurt_player(p, p); return; }
}

static struct { uint8_t on, off, angle, ani; uint16_t center; } s_pole, s_bar;

static void hold_end(uint32_t p)
{
    if (s_pole.on || s_bar.on) SET8(p, P_OBJCTRL, 0);
    memset(&s_pole, 0, sizeof s_pole);
    memset(&s_bar, 0, sizeof s_bar);
}

static void pole_check(uint32_t p)                            /* ObjSonic_CheckRotPole */
{
    if (s_pole.on || OB8(p, P_ROUTINE) >= 4 || OB8(p, P_OBJCTRL) || !(OB8(p, OBJ_status) & 2)) return;
    if (tri_scd_block_at((int16_t)OB16(p, OBJ_x_pos), (int16_t)OB16(p, OBJ_y_pos)) != 259) return;
    s_pole.on = 1;
    s_pole.off = 0;
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, (int16_t)OB16(p, OBJ_y_vel) < 0 ? -0x200 : 0x200);
    s_pole.angle = 0x40;
    s_pole.center = (uint16_t)((OB16(p, OBJ_x_pos) & 0xFFF0) + 8);
    SET16(p, OBJ_x_pos, s_pole.center);
    SET8(p, P_ANIM, 2);                                       /* (curled) */
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 4);
    SET8(p, P_YRAD, 14);
    SET8(p, P_XRAD, 7);
    SET8(p, P_JUMPING, 0);
    SET8(p, P_DJUMP, 0);
    SET8(p, P_OBJCTRL, 1);                                    /* (his own move off: the pole moves him) */
}

static void pole_move(uint32_t p)                             /* ObjSonic_RotPole */
{
    uint16_t art = OB16(p, P_ARTTILE);
    if (s_pole.off) {
        if (!(s_pole.angle & 0x7F)) {                         /* (facing out: thrown off) */
            SET16(p, OBJ_x_vel, s_pole.angle & 0x80 ? -0xC00 : 0xC00);
            SET16(p, P_ARTTILE, art & 0x7FFF);
            hold_end(p);
            return;
        }
    } else if (r8(RAM_Ctrl_1_logical + 1) & BTN_ABC)
        s_pole.off = 1;
    s_pole.angle += 8;                                        /* (round the pole: in front, then behind) */
    SET16(p, P_ARTTILE, s_pole.angle & 0x80 ? art & 0x7FFF : art | 0x8000);
    int16_t sn, cs;
    o_sincos(s_pole.angle, &sn, &cs);
    SET16(p, OBJ_x_pos, s_pole.center + (int16_t)((cs * 23) >> 8));
    SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)(int16_t)OB16(p, OBJ_y_vel) << 8));
    if (tri_scd_block_at(s_pole.center, (int16_t)OB16(p, OBJ_y_pos)) != 259)
        SET16(p, OBJ_y_vel, (uint16_t)-(int16_t)OB16(p, OBJ_y_vel));   /* (an end: back the other way) */
}

static void bar_check(uint32_t p)                             /* ObjSonic_CheckHangBar */
{
    if (s_bar.on || s_pole.on || OB8(p, P_OBJCTRL) || OB8(p, P_ROUTINE) != 2) return;
    int y = (int16_t)OB16(p, OBJ_y_pos) - 24;
    if (tri_scd_block_at((int16_t)OB16(p, OBJ_x_pos), y) != 345) return;
    scd_time_cancel();
    s_bar.on = 1;
    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~4);             /* (out of a ball) */
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, 0);
    SET8(p, P_ANIM, 0x14);                                    /* (S3K's hanging; CD's $2C) */
    SET8(p, P_JUMPING, 0);
    SET8(p, P_DJUMP, 0);
    SET16(p, OBJ_y_pos, (uint16_t)((y & 0xFFF0) + 24));
    SET8(p, P_OBJCTRL, 1);
}

static void bar_move(uint32_t p)                              /* ObjSonic_HangBar */
{
    if (tri_scd_block_at((int16_t)OB16(p, OBJ_x_pos), (int16_t)OB16(p, OBJ_y_pos) - 24) != 345 ||
        (r8(RAM_Ctrl_1_logical + 1) & BTN_ABC)) {             /* (off the bar, or let go) */
        hold_end(p);
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 16);
        SET8(p, P_YRAD, 19);
        SET8(p, P_XRAD, 9);
        return;
    }
    uint8_t held = r8(RAM_Ctrl_1_logical);
    int dx = held & 4 ? -2 : held & 8 ? 2 : 0;
    SET8(p, P_ANIM, 0x14);
    if (!dx) { SET8(p, P_ANIMTIME, 7); return; }              /* (still: the picture holds) */
    uint8_t face = dx < 0;
    SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~1) | face);
    SET8(p, OBJ_render, (OB8(p, OBJ_render) & ~1) | face);
    int16_t wall = dx < 0 ? o_wall_left(p, -10) : o_wall_right(p, 10);
    if (wall < 2) dx = 0;                                     /* (Sonic CD's air collision: a wall stops him) */
    SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + dx);
}

/* ======================================================================
 * Objects (dobby: Level/Wacky Workbench/Objects). CD's object variables
 * oVar30.. sit at S3K's $30.. (free in S3K's object); routines keep CD's
 * even numbers. SolidObject -> o_solid, TopSolidObject -> o_platform,
 * GetOffObject -> cd_get_off; objPlayerSlot -> Player 1.
 * ==================================================================== */
static int s_base;
static uint8_t s_switch[32];                   /* switchFlags: bit 7 held, 6 ever, 5 toggled */

#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $24 switch: a press plate (switchFlags[subtype]) ------------------ */
#define SW_PRV          0x3E
#define SW_NOW          0x3F
static void obj_switch(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SWITCH, OB8(o, OBJ_render) | 4, 1, 16, 8);
        SET8(o, OBJ_x_radius, 16);
        SET8(o, OBJ_y_radius, 8);
    }
    SET8(o, SW_PRV, OB8(o, SW_NOW));
    o_solid(o, 16 + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
    uint8_t *f = &s_switch[OB8(o, OBJ_subtype) & 31];
    int on = cd_stood_on(o);
    SET8(o, SW_NOW, on ? 0xFF : 0);
    if (on) *f |= 0xC0; else *f &= (uint8_t)~0x80;
    if (OB8(o, SW_PRV) == 0 && on) {                          /* (pressed this frame) */
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(0xBF);        /* (ObjSwitch: FM $BF) */
        *f ^= 0x20;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) - 4);
        for (int n = 0; n < 2; n++)                           /* (the riders go down with it) */
            if (OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))
                SET16(o_player(n), OBJ_y_pos, OB16(o_player(n), OBJ_y_pos) + 8);
    } else if (OB8(o, SW_PRV) && !on) {                       /* (let go) */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) + 4);
    }
    draw_despawn(o);
}

/* ---- $25 door: opens while its switch is held, shuts behind the player -- */
#define DR_SWITCH       0x30
#define DR_Y            0x32
#define DR_PX           0x38
#define DR_OFF          0x3A
#define DR_DIR          0x3C
static void door_move(uint32_t o)
{
    if (OB8(o, DR_DIR)) {                                     /* closing */
        int v = OB8(o, DR_OFF) - 4;
        SET8(o, DR_OFF, v < 0 ? 0 : v);
    } else {
        int v = OB8(o, DR_OFF) + 4;
        SET8(o, DR_OFF, v >= 64 ? 64 : v);
    }
    SET16(o, OBJ_y_pos, OB16(o, DR_Y) - OB8(o, DR_OFF));
}
static void door_solid(uint32_t o)
{
    o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
}
static void obj_door(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_DOOR, OB8(o, OBJ_render) | 4, 3, 8, 32);
        SET16(o, DR_Y, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 32);
        SET8(o, DR_SWITCH, OB8(o, OBJ_subtype) & 0xF);
        SET8(o, DR_DIR, 0xFF);
        /* fall through */
    case 2:
        if (s_switch[OB8(o, DR_SWITCH)] & 0x80) SET8(o, DR_DIR, 0);
        SET16(o, DR_PX, OB16(p, OBJ_x_pos));
        door_move(o);
        door_solid(o);
        if (OB8(o, DR_OFF) == 64) SET8(o, OBJ_routine, 4);
        break;
    case 4: {                                                 /* open: until the player is through */
        int dx = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, DR_PX)), r = (int8_t)OB8(p, OBJ_x_radius);
        int close;
        if (dx >= 0) {                                        /* (coming from the left) */
            int d = (int16_t)(OB16(p, OBJ_x_pos) - r - OB16(o, OBJ_x_pos));
            close = d >= 0 && d >= OB8(o, OBJ_width_px);
        } else {
            int d = (int16_t)(OB16(p, OBJ_x_pos) + r - OB16(o, OBJ_x_pos));
            close = d < 0 && -d >= OB8(o, OBJ_width_px);
        }
        if (close) SET8(o, OBJ_routine, 6);
        break;
    }
    case 6:
        SET8(o, DR_DIR, 0xFF);
        door_move(o);
        if (!OB8(o, DR_OFF)) SET8(o, OBJ_routine, 2);
        door_solid(o);
        break;
    }
    draw_despawn(o);
}

/* ---- $20 piston: rises 80 px fast, holds, sinks slowly; solid sides ------ */
#define PS_Y            0x32
#define PS_PARENT       0x34
#define PS_TIME         0x3A
#define PS_OFF          0x3B
#define PS_DIR          0x3C
static void piston_side(uint32_t o, int dx)
{
    uint32_t a = o_alloc();
    if (!a) return;
    SET32(a, OBJ_code, CD_CODE(0x20));
    SET32(a, PS_PARENT, o);
    SET8(a, OBJ_subtype, 0xFF);
    SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a, OBJ_y_pos, OB16(o, OBJ_y_pos));
    o_init(a, SCDA_PISTON, 4, 3, 8, 40);
    SET8(a, OBJ_y_radius, 40);
    SET8(a, OBJ_frame, 1);
    SET8(a, OBJ_routine, 2);
}
static void obj_piston(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* a side */
        uint32_t par = r32(o + PS_PARENT);
        if (r32(par) != CD_CODE(0x20)) { o_delete(o); return; }
        SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos));
        o_solid(o, 8 + 10, 40, 41, OB16(o, OBJ_x_pos));
        o_draw(o);
        return;
    }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_PISTON, OB8(o, OBJ_render) | 4, 3, 32, 40);
        SET8(o, OBJ_y_radius, 40);
        SET16(o, PS_Y, OB16(o, OBJ_y_pos));
        piston_side(o, -32);
        piston_side(o, 32);
    }
    int moving = 1;                                           /* ObjPiston_Move */
    if (OB8(o, PS_TIME)) { SET8(o, PS_TIME, OB8(o, PS_TIME) - 1); moving = !OB8(o, PS_TIME); }
    if (moving) {
        if (OB8(o, PS_DIR)) {
            int v = OB8(o, PS_OFF) - 1;
            if (v < 0) { SET8(o, PS_OFF, 0); SET8(o, PS_DIR, 0); SET8(o, PS_TIME, 60); }
            else SET8(o, PS_OFF, v);
        } else {
            int v = OB8(o, PS_OFF) + 8;
            if (v >= 80) { SET8(o, PS_OFF, 80); SET8(o, PS_DIR, 1); SET8(o, PS_TIME, 60); }
            else SET8(o, PS_OFF, v);
        }
    }
    SET16(o, OBJ_y_pos, OB16(o, PS_Y) - OB8(o, PS_OFF));
    if ((!OB8(o, PS_DIR) || OB8(o, PS_TIME)) && OB8(o, PS_OFF) >= 0x21)
        cd_get_off(o);                                        /* (rising or high: nothing stands on it) */
    else
        o_platform(o, 32 + 10, 40, OB16(o, OBJ_x_pos));
    draw_despawn(o);
}

/* ---- $27 tube door: slides open as the player comes, shuts behind -------- */
#define TD_TIME         0x3A
#define TD_CLOSE        0x3C
#define FM_DOOR         0xA4               /* (ObjTubeDoor: it opens, on screen; it shuts) */
static void tube_door_move(uint32_t o)
{
    int carry = OB8(o, TD_TIME) + 0x40 > 0xFF;
    SET8(o, TD_TIME, OB8(o, TD_TIME) + 0x40);
    if (!carry) return;
    if (!OB8(o, TD_CLOSE)) { SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1); return; }
    int f = OB8(o, OBJ_frame) - 1;
    SET8(o, OBJ_frame, f < 0 ? 0 : f);
}
/* within 64 px vertically and 0-63 px on the side the door opens from (1),
 * or 64 px or more past it once open (closing: 1) */
static int tube_door_check(uint32_t o, int closing)
{
    uint32_t p = RAM_Player_1;
    int dy = (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
    if (dy < 0) dy = -dy;
    if (dy >= 64) return 0;
    int d = OB8(o, OBJ_subtype) ? (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos))
                                : (int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
    if (closing) d = -d;
    if (d < 0) return 0;
    return closing ? d >= 64 : d < 64;
}
static void obj_tube_door(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TUBEDOOR, OB8(o, OBJ_render) | 4, 3, 4, 32);
        SET8(o, OBJ_y_radius, 32);
        if (OB8(o, OBJ_subtype)) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        }
        /* fall through */
    case 2:
        if (tube_door_check(o, 0)) {
            SET16(o, TD_TIME, 0);
            SET8(o, OBJ_routine, 4);
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_DOOR);
        }
        break;
    case 4:
        SET8(o, TD_CLOSE, 0);
        tube_door_move(o);
        if (OB8(o, OBJ_frame) == 3) SET8(o, OBJ_routine, 6);
        break;
    case 6:
        if (tube_door_check(o, 1)) {
            SET16(o, TD_TIME, 0);
            SET8(o, OBJ_routine, 8);
            o_sfx_cd(FM_DOOR);                                /* (the same sound, on screen or not) */
        }
        break;
    case 8:
        SET8(o, TD_CLOSE, 1);
        tube_door_move(o);
        if (!OB8(o, OBJ_frame)) SET8(o, OBJ_routine, 2);
        break;
    }
    if (!OB8(o, OBJ_frame)) o_solid(o, 4 + 10, 32, 33, OB16(o, OBJ_x_pos));
    draw_despawn(o);
}

/* ---- $07 tunnel path: carries player 1 along a route of points (relative to
 * it; the act's own table) at $800; at a point flagged, a jump button held
 * takes the branch it names (one more leg, then out). Subtype bit 7: in
 * front (not kept); bit 6 (scd_level.py: Sonic CD's subtype 2): unseen while
 * carried ------------------------------------------------------------------ */
#define TP_STEPS        0x30               /* oVar2E: frames (high byte) */
#define TP_X            0x36               /* oVar36 */
#define TP_Y            0x38               /* oVar38 */
#define TP_INDEX        0x3A               /* oVar3A */
#define TP_END          0x3B               /* oVar3B */
#define TP_DATA         0x3C               /* oVar3C */
#define FM_PATH         0x91               /* (ObjTunnelPath: it takes the player, and at its start) */
static void tp_aim(uint32_t o, uint32_t p)                    /* ObjTunnelPath_SetPlayerSpeed */
{
    int16_t dx = (int16_t)(OB16(o, TP_X) + OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
    int16_t dy = (int16_t)(OB16(o, TP_Y) + OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
    int16_t d2 = (int16_t)OB16(p, OBJ_ground_vel), d3 = d2;
    uint16_t ax = (uint16_t)dx, ay = (uint16_t)dy;
    if (dx < 0) { ax = (uint16_t)-dx; d2 = -d2; }
    if (dy < 0) { ay = (uint16_t)-dy; d3 = -d3; }
    if (ay >= ax) {
        int16_t d1 = (int16_t)(((int32_t)dy << 16) / d3), d0 = 0;
        if (dx && d1) d0 = (int16_t)(((int32_t)dx << 16) / d1);
        SET16(p, OBJ_x_vel, (uint16_t)d0);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
        SET16(o, TP_STEPS, (uint16_t)(d1 < 0 ? -d1 : d1));
    } else {
        int16_t d0 = (int16_t)(((int32_t)dx << 16) / d2), d1 = 0;
        if (dy && d0) d1 = (int16_t)(((int32_t)dy << 16) / d0);
        SET16(p, OBJ_y_vel, (uint16_t)d1);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
        SET16(o, TP_STEPS, (uint16_t)(d0 < 0 ? -d0 : d0));
    }
}
static void tp_target(uint32_t o, unsigned i)
{
    uint32_t a2 = r32(o + TP_DATA);
    SET16(o, TP_X, tri_rd16(a2 + i));
    SET16(o, TP_Y, tri_rd16(a2 + i + 2));
}
static void tunnel_path(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (OB8(p, P_ROUTINE) >= 6) return;                       /* (dead) */
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        uint32_t tab = o_asset_anim(SCDA_TUNNELPATHS);
        if (!tab) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        uint32_t a2 = tab + tri_rd16(tab + (OB8(o, OBJ_subtype) & 0x3F) * 2u);
        SET8(o, TP_INDEX, tri_rd8(a2));
        SET8(o, TP_END, tri_rd8(a2 + 1));
        SET32(o, TP_DATA, a2 + 2);
        tp_target(o, 0);
    }   /* fall through */
    case 2: {
        if (OB8(p, P_ROUTINE) >= 6) break;
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 32) >= 64 ||
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 48) >= 96 || OB8(p, P_OBJCTRL))
            break;
        if (OB8(p, P_ROUTINE) == 4) {                         /* (hurt: back to normal) */
            SET8(p, P_ROUTINE, 2);
            SET8(p, P_INVULN, 0x78);
        }
        SET8(o, OBJ_routine, 4);
        SET8(p, P_OBJCTRL, 0x81);
        SET8(p, P_ANIM, 2);
        SET16(p, OBJ_ground_vel, 0x800);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~0x20);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | 2);
        SET8(p, P_JUMPING, 0);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        o_sfx_cd(FM_PATH);
        break;
    }
    case 4:
        SET8(p, P_ANIM, 2);
        tp_aim(o, p);
        SET8(o, OBJ_routine, 6);
        o_sfx_cd(FM_PATH);
        if (OB8(o, OBJ_subtype) & 0x40) SET8(p, P_INVULN, 3); /* (Sonic_Display skips him) */
        return;
    default: {
        SET8(p, P_ANIM, 2);
        if (OB8(o, OBJ_subtype) & 0x40) SET8(p, P_INVULN, 3);
        int8_t t = (int8_t)(OB8(o, TP_STEPS) - 1);
        SET8(o, TP_STEPS, (uint8_t)t);
        if (t >= 0) {
            SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (uint32_t)((int32_t)(int16_t)OB16(p, OBJ_x_vel) << 8));
            SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)(int16_t)OB16(p, OBJ_y_vel) << 8));
            uint32_t a2 = r32(o + TP_DATA);
            int16_t flag = (int16_t)tri_rd16(a2 + OB8(o, TP_INDEX) + 4u);
            if (flag < 0 && (r8(RAM_Ctrl_1_logical) & BTN_ABC)) {   /* (a branch, taken) */
                unsigned i = (uint8_t)(OB8(o, TP_END) + (flag & 0x7FFF) * 6);
                SET8(o, TP_INDEX, i);
                tp_target(o, i);
                tp_aim(o, p);
            }
            return;
        }
        SET16(p, OBJ_x_pos, OB16(o, TP_X) + OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, TP_Y) + OB16(o, OBJ_y_pos));
        unsigned i = (uint8_t)(OB8(o, TP_INDEX) + 6);
        if (i >= OB8(o, TP_END)) {                            /* out, with the last leg's speed */
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
            SET8(o, OBJ_routine, 0);
            SET8(p, P_OBJCTRL, 0);
            if (OB8(o, OBJ_subtype) & 0x40) SET8(p, P_INVULN, 0);
            return;
        }
        SET8(o, TP_INDEX, i);
        tp_target(o, i);
        tp_aim(o, p);
        return;
    }
    }
    if (OB8(o, OBJ_routine) < 4) cd_despawn(o, OB16(o, OBJ_x_pos));   /* (unseen) */
}

/* ---- $26 crusher (act 2): with player 1 up to $88 px to its left (and
 * within $C0 px up or down) it comes down 128 px, eases in, and back up once
 * he is $88 px past it; solid (UDBLK6.C; the disc's code) ------------------ */
#define CR_COUNT        0x3A               /* oVar3A: frames of the move's step */
#define CR_STEP         0x3B               /* oVar3B: the step (2: done) */
#define CR_UP           0x3C               /* oVar3C: 0 down, 1 up */
#define CR_ACCEL        0x3E               /* oVar3E */
static void crusher_move(uint32_t o)
{
    static const int8_t steps[2][2][2] = { { { 0x40, 8 }, { 0x40, -8 } }, { { 0x40, -8 }, { 0x40, 8 } } };
    if (!OB8(o, CR_COUNT)) {                                  /* (a step starts: no move this frame) */
        const int8_t *t = steps[OB8(o, CR_UP) & 1][OB8(o, CR_STEP) & 1];
        SET8(o, CR_COUNT, t[0]);
        SET16(o, CR_ACCEL, (uint16_t)(int16_t)t[1]);
        return;
    }
    SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8));
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, CR_ACCEL));
    SET8(o, CR_COUNT, OB8(o, CR_COUNT) - 1);
    if (!OB8(o, CR_COUNT)) SET8(o, CR_STEP, OB8(o, CR_STEP) + 1);
}
static void crusher(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    int dy = (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
    int near = (dy < 0 ? -dy : dy) < 0xC0;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_CRUSHER, OB8(o, OBJ_render) | 4, 3, 0x30, 0x50);
        SET8(o, OBJ_y_radius, 0x50);
        /* fall through */
    case 2:
        if (near && (uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)) < 0x88) {
            SET8(o, CR_UP, 0);
            SET16(o, CR_COUNT, 0);
            SET8(o, OBJ_routine, 4);
        }
        break;
    case 4:
        SET8(o, CR_UP, 0);
        crusher_move(o);
        if (OB8(o, CR_STEP) == 2) SET8(o, OBJ_routine, 6);
        break;
    case 6: {
        int dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (near && dx >= 0x88) {
            SET16(o, CR_COUNT, 0);
            SET8(o, OBJ_routine, 8);
        }
        break;
    }
    default:
        SET8(o, CR_UP, 1);
        crusher_move(o);
        if (OB8(o, CR_STEP) == 2) SET8(o, OBJ_routine, 2);
        break;
    }
    o_solid(o, 0x30 + 10, 0x50, 0x51, OB16(o, OBJ_x_pos));
    draw_despawn(o);
}

/* ---- $2D spike ball (act 2): five spiked balls on an arm 16-80 px long,
 * turning round its pivot (one turn in 256 frames; subtype set: the other
 * way); the balls are subtype $FF (TOGEBL6A.C; the disc's code) ------------ */
#define SB_ANGLE        0x30               /* oVar2A (8.8) */
#define SB_SPEED        0x32               /* oVar2C */
#define SB_BALLS        0x34               /* oVar2E..: the balls (RAM low words), 5 */
#define SB_X            0x30               /* (a ball's: its place, 16.16, from the pivot) */
#define SB_Y            0x34
#define SB_PARENT       0x3E
static void spike_ball(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* a ball */
        uint32_t par = 0xFF0000u | OB16(o, SB_PARENT);
        if (r32(par) != CD_CODE(0x2D)) { o_delete(o); return; }
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            o_init(o, SCDA_SPIKEBALL2, OB8(o, OBJ_render) | 4, 1, 8, 8);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0xB9));
        }
        SET16(o, OBJ_x_pos, OB16(o, SB_X));
        SET16(o, OBJ_y_pos, OB16(o, SB_Y));
        o_touch(o);
        o_draw(o);
        return;
    }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SPIKEBALL, OB8(o, OBJ_render) | 4, 1, 8, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, SB_SPEED, OB8(o, OBJ_subtype) ? (uint16_t)-0x100 : 0x100);
        for (int k = 0; k < 5; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) { o_delete(o); return; }
            SET32(a1, OBJ_code, CD_CODE(0x2D));
            SET16(a1, SB_PARENT, (uint16_t)o);
            SET8(a1, OBJ_subtype, 0xFF);
            SET8(a1, OBJ_width_px, 8);
            SET8(a1, OBJ_height_px, 8);
            SET16(o, SB_BALLS + k * 2, (uint16_t)a1);
        }
    }
    SET16(o, SB_ANGLE, OB16(o, SB_ANGLE) + OB16(o, SB_SPEED));
    int16_t sn, cs;
    o_sincos((uint8_t)(OB16(o, SB_ANGLE) >> 8), &sn, &cs);
    int32_t y16 = ((int32_t)sn << 16) >> 4, x16 = ((int32_t)cs << 16) >> 4;   /* (16 px a step) */
    static const uint8_t mul[5] = { 1, 2, 3, 4, 5 };
    for (int k = 0; k < 5; k++) {
        uint32_t a1 = 0xFF0000u | OB16(o, SB_BALLS + k * 2);
        SET32(a1, SB_X, r32(o + OBJ_x_pos) + (uint32_t)(x16 * mul[k]));
        SET32(a1, SB_Y, r32(o + OBJ_y_pos) + (uint32_t)(y16 * mul[k]));
    }
    draw_despawn(o);
}

void cd_spin_tag(uint32_t o);                                 /* scd_ppz.c: shared with Palmtree Panic */

void scd_wwz_act3_register(TriObjFn *t, int base3)            /* (act 3: the same code, its own ids) */
{
    t[base3 + 0x07] = tunnel_path;
    t[base3 + 0x27] = obj_tube_door;
}
void cd_animal(uint32_t o);
void cd_projector(uint32_t o);
void cd_generator(uint32_t o);

void scd_wwz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x07] = tunnel_path;
    t[base + 0x26] = crusher;
    t[base + 0x2D] = spike_ball;
    t[base + 0x1E] = cd_spin_tag;
    t[base + 0x29] = cd_animal;
    t[base + 0x2E] = cd_generator;
    t[base + 0x2F] = cd_projector;
    t[base + 0x20] = obj_piston;
    t[base + 0x24] = obj_switch;
    t[base + 0x25] = obj_door;
    t[base + 0x27] = obj_tube_door;
}

void scd_wwz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {
        scd_wwz3_level_entry();
        memset(s_switch, 0, sizeof s_switch);
        memset(&s_pole, 0, sizeof s_pole);
        memset(&s_bar, 0, sizeof s_bar);
    }
    if (!tri_dir() || !wwz()) return;
    uint32_t p1 = RAM_Player_1;
    if (pc == TH_LVL_PRE_SPRITES) {                           /* (on a bar or a pole: its move, not his) */
        if (!s_pole.on && !s_bar.on) return;
        if (!o_player_active(p1) || OB8(p1, P_ROUTINE) != 2 || OB8(p1, P_OBJCTRL) != 1 ||
            r16(RAM_Debug_placement_mode)) { hold_end(p1); return; }
        if (s_pole.on) pole_move(p1);
        else bar_move(p1);
        return;
    }
    if (pc != TH_LVL_POST_SPRITES) return;
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        int held = !n && (s_pole.on || s_bar.on);             /* (Sonic CD's bar and pole leave these on) */
        if (!held && !playing(p)) continue;
        if (held && (OB8(p, P_ROUTINE) != 2 || r16(RAM_Debug_placement_mode))) continue;
        bounce_floor(p);
        live_floor(p);
        if (n) continue;
        beams(p);
        bar_check(p);
        pole_check(p);
    }
}
