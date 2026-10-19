/*
 * scd_qqz.c — Quartz Quadrant (Sonic CD; decomp: R5\PLAYSP5.C, R5\BELTSW5.C,
 * R5\SHOOT5.C; the disc's R51*, R52* programs, the same code in every act and
 * period).
 *
 * The zone's player code (playsp: belt) runs the conveyor belts in the
 * terrain, keyed on Sonic CD's own 16 px block numbers (scramapad;
 * tri_scd_block_at reads the act's CD chunks):
 *   belt     the block 2 px under either foot (x -/+ the x radius) is in the
 *            period's list of belt blocks: the player is carried 0.5 px a
 *            frame in the past, 1 in the present, 2 in the futures, to the
 *            right; while the lever's flag is set (colrevflag, $31) to the
 *            left; a wall ahead (Player_CalcRoomInFront, flat, where his speed
 *            takes him, 8 px under his middle) pushes him back out of it
 * Sonic CD runs it after the player's own move each frame, and in his hurt
 * routine too, unless an object holds him (oPlayerCtrl bit 0); here from
 * TH_LVL_POST_SPRITES, for both players. The flag (here s_colrev) also turns
 * the zone's two-way tubes ($07), the platforms' belt tops ($30) and the
 * swing's ($2A); the level's start clears it (every start: a warp, a life).
 * Its other objects: scd_qqz_badnik.c, scd_qqz_gim.c.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t tri_scd_block_at(int x, int y);
int scd_qqz3_bossflag(void);                         /* scd_qqz_boss.c: bossFlag ($F7A7) */

#define QQZ_ROUND       3                  /* (ROUND_INDEX: Quartz Quadrant) */
#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_OBJCTRL       0x2E               /* object_control: CD's oPlayerCtrl */
#define P_INVULN        0x34               /* invulnerability_timer: CD's oPlayerHurt */

static int qqz(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == QQZ_ROUND;
}

static uint8_t s_colrev;                   /* colrevflag: the belts run left (bit 0) */
int scd_qqz_colrev(void) { return qqz() ? s_colrev : 0; }   /* (the palette cycle's belt script: scd_level.py) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- the belts (belt, belt_effect, belt_chk) ----------------------------- */
/* the belts' blocks, by period (belt_tbl: present, past, good, bad future) */
static const int16_t belt_present[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15, 17, 18, 19, 35, 36, 37, 38, 39, 49, 61, 62, 63, 64, 71, 72, 73, 74,
    400, 403, 404, 405, 406, 407, 408, 409, 410, 411, 413, 415, 422, 423, 424, 448, 450, -1 };
static const int16_t belt_past[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15, 17, 18, 19, 35, 36, 37, 38, 39, 49, 59, 60, 61, 62, 63, 64,
    71, 72, 73, 74, 400, 403, 404, 405, 406, 407, 408, 409, 410, 411, 413, 415, 422, 423, 424, 483, 484,
    485, -1 };
static const int16_t belt_good[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 35, 36, 37, 38, 39, 49, 61, 62, 63, 64,
    71, 72, 73, 74, 75, 400, 403, 404, 405, 406, 407, 408, 409, 410, 411, 413, 414, 415, 423, 424, 487,
    488, 490, 493, -1 };
static const int16_t belt_bad[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15, 17, 18, 19, 35, 36, 37, 38, 39, 49, 61, 62, 63, 64, 71, 72,
    73, 74, 75, 400, 403, 404, 405, 406, 407, 408, 409, 410, 411, 413, 414, 415, 423, 424, 488, 489, 491,
    500, -1 };

static int belt_block(uint16_t b)                             /* belt_chk */
{
    static const int16_t *const lists[] = { belt_present, belt_past, belt_good, belt_bad };
    for (const int16_t *t = lists[cd_period() & 3]; *t >= 0; t++)
        if (b == (uint16_t)*t) return 1;
    return 0;
}

/* Player_CalcRoomInFront, angle $C0 (right) or $40 (left): flat, so 8 px
 * below his middle; from where his speed takes him; 10 px out */
static int16_t room_in_front(uint32_t p, int left)
{
    int16_t nx = (int16_t)((r32(p + OBJ_x_pos) + ((uint32_t)(int32_t)OBS16(p, OBJ_x_vel) << 8)) >> 16);
    int16_t ny = (int16_t)((r32(p + OBJ_y_pos) + ((uint32_t)(int32_t)OBS16(p, OBJ_y_vel) << 8)) >> 16);
    uint16_t y0 = OB16(p, OBJ_y_pos);
    int dx = (int16_t)(nx - OBS16(p, OBJ_x_pos));
    SET16(p, OBJ_y_pos, (uint16_t)(ny + 8));
    int16_t d1 = left ? o_wall_left(p, dx - 10) : o_wall_right(p, dx + 10);   /* (S&K's left check has
                                                                  CalcRoomInFront's eori #$F: the same) */
    SET16(p, OBJ_y_pos, y0);
    return d1;
}

static void belt(uint32_t p)
{
    if (scd_qqz3_bossflag()) return;                          /* (act 3: the boss's own belt) */
    int x = OBS16(p, OBJ_x_pos), foot = OBS16(p, OBJ_y_pos) + (int8_t)OB8(p, P_YRAD) + 2;
    int w = (int8_t)OB8(p, P_XRAD);
    if (!belt_block(tri_scd_block_at(x - w, foot)) && !belt_block(tri_scd_block_at(x + w, foot))) return;
    static const int32_t speed[] = { 0x10000, 0x8000, 0x20000, 0x20000 };   /* (present, past, futures) */
    int left = s_colrev != 0;
    int32_t v = speed[cd_period() & 3];
    SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (uint32_t)(left ? -v : v));
    int16_t d1 = room_in_front(p, left);
    if (d1 < 0) SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + (left ? -d1 : d1));   /* (out of the wall) */
}

/* ======================================================================
 * Objects. CD's object variables oVar2A.. sit at S3K's $30.. (oVarNN at
 * $NN + 6); routines keep CD's even numbers.
 * ==================================================================== */

/* ---- $31 lever: the player passing flips the belts' way (colrevflag);
 * it shows the way, and turns when another lever turns it ---------------- */
#define LV_TIME         0x40               /* oVar3A */
#define LV_SEEN         0x42               /* oVar3C: the flag as it last saw it */
#define LV_TURN         0x43               /* oVar3D (set, never read) */
#define FM_LEVER        0xBF               /* (beltsw5_move1: the lever thrown) */

static unsigned dist(uint16_t a, uint16_t b) { return (uint16_t)(a >= b ? a - b : b - a); }

static void lever(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_QQZ_LEVER, OB8(o, OBJ_render) | 4, 3, 0x18, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        if (s_colrev & 1) SET8(o, OBJ_frame, 2);
        SET8(o, LV_SEEN, s_colrev);
        /* fall through */
    case 2:
        if (!r16(RAM_Debug_placement_mode) && dist(OB16(p, OBJ_y_pos), OB16(o, OBJ_y_pos)) < 0x10 &&
            dist(OB16(p, OBJ_x_pos), OB16(o, OBJ_x_pos)) < 8) {
            s_colrev ^= 1;                                    /* (touched: the belts turn) */
            SET8(o, OBJ_routine, 4);
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_LEVER);
            break;
        }
        if (s_colrev != OB8(o, LV_SEEN)) {                    /* (another lever turned them) */
            SET8(o, LV_TURN, 0xFF);
            SET8(o, OBJ_routine, 4);
        }
        SET8(o, LV_SEEN, s_colrev);
        break;
    case 4:                                                   /* turning: the middle frame, then the way */
        SET8(o, LV_TIME, OB8(o, LV_TIME) + 1);
        if (OB8(o, LV_TIME) == 7) SET8(o, OBJ_frame, 1);
        else if (OB8(o, LV_TIME) == 14) {
            SET8(o, LV_TIME, 0);
            SET8(o, OBJ_frame, s_colrev & 1 ? 2 : 0);
            SET8(o, OBJ_routine, 6);
            SET8(o, LV_TURN, 0);
        }
        break;
    default:                                                  /* (till the player is away: cmpi.b, the
                                                                  distances' low bytes) */
        if ((uint8_t)dist(OB16(p, OBJ_y_pos), OB16(o, OBJ_y_pos)) < 0x20 &&
            (uint8_t)dist(OB16(p, OBJ_x_pos), OB16(o, OBJ_x_pos)) < 0x10)
            break;
        SET8(o, OBJ_routine, 2);
        SET8(o, LV_SEEN, s_colrev);
        break;
    }
    draw_despawn(o);
}

/* ---- $07 transport tube: carries player 1 along a route of points (the
 * act's own table, 22 routes, absolute) at $A00. Subtype bit 7: a two-way
 * tube's one way, there only while the belts run its way (bit 0 set: while
 * colrevflag is set) ------------------------------------------------------ */
#define TB_STEPS        0x34               /* oVar2E: frames (high byte) */
#define TB_X            0x3C               /* oVar36 */
#define TB_Y            0x3E               /* oVar38 */
#define TB_INDEX        0x40               /* oVar3A */
#define TB_END          0x41               /* oVar3B */
#define TB_DATA         0x42               /* oVar3C */
#define FM_TUBE         0x91               /* (shootermove: in; shootermove2: off along the route) */

static void tb_aim(uint32_t o, uint32_t p)                    /* shooterspdset */
{
    int16_t dx = (int16_t)(OB16(o, TB_X) - OB16(p, OBJ_x_pos));
    int16_t dy = (int16_t)(OB16(o, TB_Y) - OB16(p, OBJ_y_pos));
    int16_t d2 = OBS16(p, OBJ_ground_vel), d3 = d2;
    uint16_t ax = (uint16_t)dx, ay = (uint16_t)dy;
    if (dx < 0) { ax = (uint16_t)-dx; d2 = -d2; }
    if (dy < 0) { ay = (uint16_t)-dy; d3 = -d3; }
    if (ay >= ax) {
        int16_t d1 = (int16_t)(((int32_t)dy << 16) / d3), d0 = 0;
        if (dx && d1) d0 = (int16_t)(((int32_t)dx << 16) / d1);
        SET16(p, OBJ_x_vel, (uint16_t)d0);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
        SET16(o, TB_STEPS, (uint16_t)(d1 < 0 ? -d1 : d1));
    } else {
        int16_t d0 = (int16_t)(((int32_t)dx << 16) / d2), d1 = 0;
        if (dy && d0) d1 = (int16_t)(((int32_t)dy << 16) / d0);
        SET16(p, OBJ_y_vel, (uint16_t)d1);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
        SET16(o, TB_STEPS, (uint16_t)(d0 < 0 ? -d0 : d0));
    }
}

static void tube(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    uint8_t sub = OB8(o, OBJ_subtype);
    /* (Sonic CD also clears the tube's saved state in the period a warp under
     * way goes to; the time travel here keeps its own) */
    if (sub & 0x80) {                                         /* (a two-way tube: this way now?) */
        int on = s_colrev ? (sub & 1) : !(sub & 1);
        if (!on) { cd_despawn(o, OB16(o, OBJ_x_pos)); return; }
    }
    /* (CD: none while the player gives up from boredom, anim $2B: not S3K's) */
    if (OB8(p, P_ROUTINE) >= 6) return;                       /* (dead) */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* shooterinit */
        uint32_t tab = o_asset_anim(SCDA_TUNNELPATHS);
        if (!tab) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_width_px, 0x10);
        uint32_t a2 = tab + tri_rd16(tab + (sub & 0x7F) * 2u);
        SET8(o, TB_INDEX, tri_rd8(a2));
        SET8(o, TB_END, tri_rd8(a2 + 1));
        SET32(o, TB_DATA, a2 + 2);
        SET16(o, TB_X, tri_rd16(a2 + 2));
        SET16(o, TB_Y, tri_rd16(a2 + 4));
    }   /* fall through */
    case 2:                                                   /* shootermove: in, from a 32 px box */
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 16) >= 32 ||
            (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 16) >= 32 || OB8(p, P_OBJCTRL))
            break;
        if (OB8(p, P_ROUTINE) == 4) {                         /* (hurt: back to normal) */
            SET8(p, P_ROUTINE, 2);
            SET8(p, P_INVULN, 0x78);
        }
        SET8(o, OBJ_routine, 4);
        SET8(p, P_OBJCTRL, 0x81);
        SET8(p, P_ANIM, 2);
        SET16(p, OBJ_ground_vel, 0xA00);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~0x20);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | 2);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        o_sfx_cd(FM_TUBE);
        break;
    case 4:                                                   /* shootermove2 */
        tb_aim(o, p);
        SET8(o, OBJ_routine, 6);
        o_sfx_cd(FM_TUBE);
        return;
    default: {                                                /* shootermove3 */
        int8_t t = (int8_t)(OB8(o, TB_STEPS) - 1);
        SET8(o, TB_STEPS, (uint8_t)t);
        if (t >= 0) {
            SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_x_vel) << 8));
            SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_y_vel) << 8));
            return;
        }
        SET16(p, OBJ_x_pos, OB16(o, TB_X));
        SET16(p, OBJ_y_pos, OB16(o, TB_Y));
        uint8_t i = (uint8_t)(OB8(o, TB_INDEX) + 4);
        if (i >= OB8(o, TB_END)) {                            /* out, with the last leg's speed */
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
            SET8(o, OBJ_routine, 0);
            SET8(p, P_OBJCTRL, 0);
            return;
        }
        SET8(o, TB_INDEX, i);
        uint32_t a2 = r32(o + TB_DATA);
        SET16(o, TB_X, tri_rd16(a2 + i));
        SET16(o, TB_Y, tri_rd16(a2 + i + 2u));
        tb_aim(o, p);
        return;
    }
    }
    if (OB8(o, OBJ_routine) < 4) cd_despawn(o, OB16(o, OBJ_x_pos));   /* (unseen) */
}

void scd_qqz_badnik_register(TriObjFn *t, int base);          /* scd_qqz_badnik.c */
void scd_qqz_gim_register(TriObjFn *t, int base);             /* scd_qqz_gim.c */

void scd_qqz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x07] = tube;
    t[base + 0x31] = lever;
    scd_qqz_badnik_register(t, base);
    scd_qqz_gim_register(t, base);
}

/* ---- the act's sections (acts 1-2, $FF156C; scd_level.py scenery_sections,
 * header +$F4): by player 1's x, the open middle with its own background
 * (art: trilogy_scdlevel.c's animated entry, flag 16) and palette 4 + set
 * (line 1 and line 2's first 8, into the palette and its fade target); and the
 * zone's colour cycler [$200184]: the belts (the lever's table while the
 * belts run left) and a second by the section's parity -------------------- */
static uint8_t s_section, s_set, s_cyc_time[2], s_cyc_frame[2];

int scd_qqz_bg_frame(void) { return qqz() && tri_act() < 2 ? s_set : 0; }

static uint32_t sections(void)
{
    uint32_t w = tri_dir();
    return w && tri_act() < 2 ? tri_rd32(w + 0xF4) : 0;
}

static void cycle(int k, uint32_t t)                          /* (subq.b #1,(a4); bpl ...) */
{
    if ((int8_t)--s_cyc_time[k] >= 0) return;
    s_cyc_time[k] = tri_rd8(t + 3);
    uint8_t f = (uint8_t)(s_cyc_frame[k] + 1), per = tri_rd8(t + 2);
    if (f >= tri_rd8(t + 1)) f = 0;
    s_cyc_frame[k] = f;
    uint32_t src = t + 4 + f * per * 2u, dst = RAM_Normal_palette + tri_rd8(t) * 2u;
    for (unsigned c = 0; c < per; c++) w16(dst + c * 2u, tri_rd16(src + c * 2u));
}

static void scenery(void)
{
    uint32_t t = sections();
    if (!t) return;
    unsigned n = tri_rd16(t), sec = 0, px = OB16(RAM_Player_1, OBJ_x_pos);
    while (sec < n && tri_rd16(t + 2 + sec * 2u) < px) sec++;
    uint32_t sets = t + 2 + n * 2u, pals = sets + ((n + 2u) & ~1u);
    if (sec != s_section) {
        s_section = (uint8_t)sec;
        uint8_t set = tri_rd8(sets + sec);
        if (set != s_set && set < tri_rd16(pals)) {
            s_set = set;
            extern void tri_scd_set_bg(uint32_t grid, unsigned w, unsigned h);
            uint32_t b2 = pals + 2 + tri_rd16(pals) * 48u + 16;   /* (after the 4 cycles: set 1's background) */
            if (set == 1 && tri_rd32(b2 + 4)) tri_scd_set_bg(tri_rd32(b2 + 4), tri_rd16(b2), tri_rd16(b2 + 2));
            else tri_scd_set_bg(0, 0, 0);
            uint32_t c = pals + 2 + set * 48u;
            for (unsigned k = 0; k < 24; k++) {
                w16(RAM_Normal_palette + 0x20 + k * 2u, tri_rd16(c + k * 2u));
                w16(RAM_Target_palette + 0x20 + k * 2u, tri_rd16(c + k * 2u));
            }
        }
    }
    uint32_t cyc = pals + 2 + tri_rd16(pals) * 48u;
    uint32_t belt = tri_rd32(cyc + (s_colrev ? 4 : 0)), second = tri_rd32(cyc + (s_section & 1 ? 12 : 8));
    if (belt) cycle(0, belt);
    if (second) cycle(1, second);
}

void scd_qqz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {                               /* (LevelStart: clr.b colrevflag; section 0) */
        s_colrev = 0;
        s_section = s_set = 0;
        extern void tri_scd_bg_reset(void);
        tri_scd_bg_reset();
        memset(s_cyc_time, 0, sizeof s_cyc_time);
        memset(s_cyc_frame, 0, sizeof s_cyc_frame);
    }
    if (pc != TH_LVL_POST_SPRITES || !tri_dir() || !qqz()) return;
    scenery();
    if (r16(RAM_Debug_placement_mode)) return;
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        uint8_t r = OB8(p, P_ROUTINE);
        if (!o_player_active(p) || (r != 2 && r != 4) || (OB8(p, P_OBJCTRL) & 1)) continue;
        belt(p);
    }
}
