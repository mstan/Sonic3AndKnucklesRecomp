/*
 * scd_ssz_obj.c — Stardust Speedway's platforms and gimmicks, acts 1 and 2
 * (Sonic CD; decomp: R7\BRANKO7.C, WALL7.C, TEKKYU7.C, TEKKYU7J.C, MOVIE7.C,
 * FRIEND7.C, SLIGHT7.C; the disc's code is followed).
 *
 *   $20 swing        a pendulum of links (subtype: links, low nibble; swing,
 *                    high), a solid seat at the end that hurts from below
 *   $24 wall         a solid metal block (subtype: its size)
 *   $25 iron ball    shuttles to and fro or up and down (subtype 1), faster
 *                    in the past, slower in the future
 *   $26 launcher     a pipe: every 150 frames an iron ball creeps out, waits,
 *                    shoots out at 6 px and comes back into it (subtype bit 0:
 *                    down / right, bit 1: sideways)
 *   $27 projector    Metal Sonic's (the past): a ball breaks it, the show
 *                    ends and the act's animals are free
 *   $28 animals      free in the past and present once the projector is gone,
 *                    and in a good future (subtype bit 7: a hologram)
 *   $38 searchlights big beams swinging over the screen's middle, seen every
 *                    other frame or every 4th (CD's subtype 2, carried in the
 *                    subtype's high nibble by scd_level.py)
 * The robot generator ($29) is scd_ppz.c's cd_generator (the code matches).
 */
#include "trilogy_obj.h"

#define FM_EXPLODE      0x9E               /* (movie7: each of the projector's explosions) */

int  scd_proj_destroyed(void);             /* scd_time.c */
void scd_proj_destroyed_set(void);
int  scd_good_future(void);
void scd_ssz_block_prio(uint32_t o, int x, int y);   /* scd_ssz.c: GetLevelBlock's sprite priority */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void draw_despawn(uint32_t o)       /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

static int time_zone(void)                 /* timeZone: 0 past, 1 present, 2 future */
{
    int p = cd_period();
    return p == TRI_CD_PAST ? 0 : p == TRI_CD_PRESENT ? 1 : 2;
}

/* ---- $20 swing (branko7) ------------------------------------------------ */
#define SW_VEL          0x30               /* oVar30 */
#define SW_Y0           0x32               /* oVar32: the pivot */
#define SW_MAX          0x34               /* oVar34 */
#define SW_X0           0x36               /* oVar36 */
#define SW_MIN          0x38               /* oVar38 */
#define SW_ANG          0x3A               /* oVar3A (the high byte: the angle) */
#define SW_LINK         0x3C               /* oVar3C: 16 px each; 0 the pivot */
#define SW_DIR          0x3D               /* oVar3D */
#define SW_ACC          0x3E               /* oVar3E */
#define SW_N            0x40               /* oVar2F: links (the seat's number) */
static void sw_move(uint32_t o)            /* (the swing, then its place) */
{
    int16_t v = OBS16(o, SW_VEL);
    if (!OB8(o, SW_DIR)) {
        v = (int16_t)(v + OBS16(o, SW_ACC));
        SET8(o, SW_DIR, v == OBS16(o, SW_MAX) ? 0xFF : 0);
    } else {
        v = (int16_t)(v - OBS16(o, SW_ACC));
        if (v == OBS16(o, SW_MIN)) SET8(o, SW_DIR, 0);
    }
    SET16(o, SW_VEL, v);
    SET16(o, SW_ANG, OB16(o, SW_ANG) + v);
    int16_t s, c;
    o_sincos(OB8(o, SW_ANG), &s, &c);
    int r = (int8_t)OB8(o, SW_LINK) << 4;
    SET16(o, OBJ_x_pos, OB16(o, SW_X0) + ((s * r) >> 8));
    SET16(o, OBJ_y_pos, OB16(o, SW_Y0) + ((c * r) >> 8));
}

/* the seat: solid 8 high, or (missing) 12; stood on, he sits 2 px down in it */
static void seat_solid(uint32_t o, uint16_t ox)
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p)) continue;
        uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
        int16_t r = o_solid_1p(o, n, 0x20 + 10, 8, 9, ox, 0);
        int hit = (OB8(o, OBJ_status) & bit) || r == -2;
        if (!hit) {
            r = o_solid_1p(o, n, 0x20 + 10, 0x0C, 0x0D, ox, 0);
            hit = (OB8(o, OBJ_status) & bit) || r == -2;
        }
        if (hit && OB16(o, OBJ_y_pos) >= OB16(p, OBJ_y_pos))
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - 8 - (int8_t)OB8(p, OBJ_y_radius) + 2);
    }
}

static void swing(uint32_t o)
{
    static const int16_t init[8][4] = {     /* angle, ease, top speed, bottom speed */
        { -0x4000, 2, 0x100, -0x100 }, { -0x4000, 2, 0x100, -0x100 }, { -0x4000, -2, -0x100, 0x100 },
        { -0x6000, 3, 0x180, -0x180 }, { -0x8000, -2, -0x100, 0x100 }, { -0x8000, 2, 0x100, -0x100 },
        { -0x8000, -1, -0x100, 0 }, { -0x8000, 1, 0x100, 0 },
    };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SSZ_SWING, OB8(o, OBJ_render) | 4, 3, 0x20, 8);
        SET8(o, OBJ_y_radius, 8);
        if (!OB8(o, SW_LINK)) {            /* (the pivot: the links, the seat last) */
            SET16(o, SW_X0, OB16(o, OBJ_x_pos));
            SET16(o, SW_Y0, OB16(o, OBJ_y_pos));
            int n = OB8(o, OBJ_subtype) & 0xF;
            SET8(o, SW_N, n);
            for (int k = n; k > 0; k--) {
                uint32_t a1 = o_alloc();
                if (!a1) continue;
                SET32(a1, OBJ_code, CD_CODE(0x20));
                SET16(a1, SW_X0, OB16(o, SW_X0));
                SET16(a1, SW_Y0, OB16(o, SW_Y0));
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
                SET8(a1, SW_N, n);
                SET8(a1, SW_LINK, k);
                SET8(a1, OBJ_frame, 2);
                if (k == n) {
                    SET8(a1, OBJ_frame, 1);
                    SET8(a1, OBJ_coll_flags, tri_scd_coll(0xA9));
                }
            }
        }
        const int16_t *t = init[(OB8(o, OBJ_subtype) >> 4) & 7];
        SET16(o, SW_ANG, (uint16_t)t[0]);
        SET16(o, SW_ACC, (uint16_t)t[1]);
        SET16(o, SW_MAX, (uint16_t)t[2]);
        SET16(o, SW_MIN, (uint16_t)t[3]);
    }
    uint32_t ox = r32(o + OBJ_x_pos), oy = r32(o + OBJ_y_pos);
    sw_move(o);
    if (OB8(o, SW_N) == OB8(o, SW_LINK)) { /* (the seat: its speed carries the rider) */
        SET16(o, OBJ_x_vel, (uint16_t)((int32_t)(r32(o + OBJ_x_pos) - ox) >> 8));
        SET16(o, OBJ_y_vel, (uint16_t)((int32_t)(r32(o + OBJ_y_pos) - oy) >> 8));
        seat_solid(o, (uint16_t)(ox >> 16));
        o_touch(o);
    }
    if (cd_despawn(o, OB16(o, SW_X0))) return;
    o_draw(o);
}

/* ---- $24 wall (wall7): solid ------------------------------------------- */
static void wall(uint32_t o)
{
    static const uint8_t size[6][2] = { { 12, 17 }, { 28, 17 }, { 44, 17 }, { 60, 17 }, { 12, 33 }, { 12, 49 } };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        unsigned k = OB8(o, OBJ_subtype) % 6;
        o_init(o, SCDA_SSZ_WALL, OB8(o, OBJ_render) | 4, 3, size[k][0], size[k][1]);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        SET8(o, OBJ_x_radius, size[k][0]);
        SET8(o, OBJ_y_radius, size[k][1]);
    }
    int h = OB8(o, OBJ_y_radius);
    o_solid(o, OB8(o, OBJ_width_px) + 10, h, h + 1, OB16(o, OBJ_x_pos));
    draw_despawn(o);
}

/* ---- $25 iron ball (tekkyu7): to and fro, hurts ------------------------- */
#define IB_X0           0x30               /* oVar2A */
#define IB_MOVE         0x32               /* oVar2C: frames moving */
#define IB_PAUSE        0x34               /* oVar2E: frames still */
#define IB_TIME         0x36               /* oVar30 */
#define IB_VX           0x38               /* oVar32 (long) */
#define IB_VY           0x3C               /* oVar36 (long) */
static void iron_ball(uint32_t o)
{
    static const int16_t tab[3][2][4] = {   /* [timeZone][subtype]: x speed, y speed, frames moving, first leg */
        { { 0x400, 0, 0x18, 0x0C }, { 0, 0x400, 0x10, 0x08 } },
        { { 0x200, 0, 0x30, 0x18 }, { 0, 0x200, 0x20, 0x10 } },
        { { 0x100, 0, 0x60, 0x30 }, { 0, 0x100, 0x40, 0x20 } } };
    static const uint16_t pause[3] = { 60, 40, 20 };
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SSZ_IRONBALL, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xB5));
        SET16(o, IB_X0, OB16(o, OBJ_x_pos));
        int tz = time_zone();
        const int16_t *t = tab[tz][OB8(o, OBJ_subtype) & 1];
        SET16(o, IB_PAUSE, pause[tz]);
        SET32(o, IB_VX, (uint32_t)t[0] << 8);
        SET32(o, IB_VY, (uint32_t)t[1] << 8);
        SET16(o, IB_MOVE, (uint16_t)t[2]);
        SET16(o, IB_TIME, (uint16_t)t[3]);
    }   /* fall through */
    case 2:
        cd_move(o, (int32_t)r32(o + IB_VX), (int32_t)r32(o + IB_VY));
        SET16(o, IB_TIME, OB16(o, IB_TIME) - 1);
        if (!OB16(o, IB_TIME)) {
            SET8(o, OBJ_routine, 4);
            SET16(o, IB_TIME, OB16(o, IB_PAUSE));
            SET32(o, IB_VX, (uint32_t)-(int32_t)r32(o + IB_VX));
            SET32(o, IB_VY, (uint32_t)-(int32_t)r32(o + IB_VY));
        }
        break;
    default:
        SET16(o, IB_TIME, OB16(o, IB_TIME) - 1);
        if (!OB16(o, IB_TIME)) {
            SET8(o, OBJ_routine, 2);
            SET16(o, IB_TIME, OB16(o, IB_MOVE));
        }
        break;
    }
    if (cd_despawn(o, OB16(o, IB_X0))) return;
    o_touch(o);
    o_draw(o);
}

/* ---- $26 launcher (tekkyu7j) and its balls (CD's subtype 2: here bit 7 of
 * a byte of their own) --------------------------------------------------- */
#define LP_TIME         0x30               /* oVar2A (the pipe's) */
#define LB_AXIS         0x30               /* oVar2A: the ball's: which position moves (1: x) */
#define LB_START        0x32               /* oVar2C: where it came out */
#define LB_TIME         0x34               /* oVar2E */
#define LB_SPD          0x36               /* oVar30 (long) */
#define LB_ACC          0x3A               /* oVar34 (long) */
#define LB_NEG          0x3E               /* oVar3F: out the negative way */
#define LB_BALL         0x3F               /* oSubtype2 */
static void lb_step(uint32_t o)
{
    uint32_t a = o + (OB8(o, LB_AXIS) ? OBJ_x_pos : OBJ_y_pos);
    w32(a, r32(a) + r32(o + LB_SPD));
}

static void launcher(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!OB8(o, LB_BALL)) {                /* the pipe */
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            o_init(o, SCDA_SSZ_LAUNCHER, OB8(o, OBJ_render) | 4, 1, 0x10, 0x10);
            SET8(o, OBJ_frame, sub);
            SET16(o, LP_TIME, 150);
            SET8(o, OBJ_x_radius, 0x10);
            SET8(o, OBJ_y_radius, 0x10);
        }
        SET16(o, LP_TIME, OB16(o, LP_TIME) - 1);
        if (!OB16(o, LP_TIME)) {
            SET16(o, LP_TIME, 150);
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, CD_CODE(0x26));
                SET8(a1, OBJ_subtype, sub);
                SET8(a1, LB_BALL, 0xFF);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
        }
        draw_despawn(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {         /* a ball (no despawn: it always comes back) */
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SSZ_LAUNCHBALL, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xB5));
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, LB_TIME, 0x20);
        SET8(o, LB_NEG, sub & 1 ? 0 : 0xFF);
        SET32(o, LB_SPD, sub & 1 ? 0x8000 : (uint32_t)-0x8000);
        SET8(o, LB_AXIS, (int8_t)sub >= 2);
        SET16(o, LB_START, OB8(o, LB_AXIS) ? OB16(o, OBJ_x_pos) : OB16(o, OBJ_y_pos));
        /* fall through */
    case 2:                                /* (out of the pipe, slowly: 16 px) */
        lb_step(o);
        SET16(o, LB_TIME, OB16(o, LB_TIME) - 1);
        if (!OB16(o, LB_TIME)) { SET8(o, OBJ_routine, 4); SET16(o, LB_TIME, 30); }
        break;
    case 4:
        SET16(o, LB_TIME, OB16(o, LB_TIME) - 1);
        if (!OB16(o, LB_TIME)) {
            SET8(o, OBJ_routine, 6);
            SET32(o, LB_SPD, sub & 1 ? 0x60000 : (uint32_t)-0x60000);
            SET32(o, LB_ACC, sub & 1 ? (uint32_t)-0x4000 : 0x4000);
        }
        break;
    default: {                             /* (shot, slowing, back: gone past where it came out) */
        SET32(o, LB_SPD, r32(o + LB_SPD) + r32(o + LB_ACC));
        lb_step(o);
        uint16_t at = OB8(o, LB_AXIS) ? OB16(o, OBJ_x_pos) : OB16(o, OBJ_y_pos);
        if (((int16_t)(at - OB16(o, LB_START)) < 0 ? 0xFF : 0) != OB8(o, LB_NEG)) { o_delete(o); return; }
        break;
    }
    }
    o_touch(o);
    o_draw(o);
}

/* ---- $27 Metal Sonic's projector (movie7; subtype: a hologram) ----------- */
#define PJ_COUNT        0x30               /* oVar2A */
#define PJ_LOCS         0x32               /* oVar2C: the next explosion (frame, x, y) */
#define PJ_GONE         0x3F               /* oVar3F: broken (the holograms go) */
#define HL_PARENT       0x3E               /* oVar3E: the holograms' projector */
static int holo_gone(uint32_t o)
{
    uint32_t pj = 0xFF0000u | OB16(o, HL_PARENT);
    return r32(pj) != CD_CODE(0x27) || OB8(pj, PJ_GONE);
}

static int spawn_holo(uint32_t o, int id, int dx, int dy, uint8_t sub)
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, CD_CODE(id));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, HL_PARENT, (uint16_t)o);
    return 1;
}

static void projector(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {             /* Metal Sonic's hologram */
        if (holo_gone(o)) { o_delete(o); return; }
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            int small = OB8(o, OBJ_subtype) & 0x80;
            o_init(o, SCDA_PROJECTOR, OB8(o, OBJ_render) | 4, 4, small ? 8 : 0x14, small ? 4 : 0x18);
            SET8(o, OBJ_x_radius, small ? 8 : 0x14);
            SET8(o, OBJ_y_radius, small ? 4 : 0x18);
            SET8(o, OBJ_anim, small ? 0 : 1);
        }
        o_animate(o, o_asset_anim(SCDA_PROJECTOR));
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                /* (its PLC 5: the holograms' art; here it is placed) */
        if (scd_proj_destroyed() || !o_asset_maps(SCDA_PROJECTOR)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_PROJECTOR, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_x_radius, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET32(o, PJ_LOCS, o_asset_anim(SCDA_PROJBOOM));
        if (!spawn_holo(o, 0x27, -0x15, -7, 0xFF) || !spawn_holo(o, 0x27, -0x58, -4, 1) ||
            !spawn_holo(o, 0x28, -0x58, -0x18, 0x80) || !spawn_holo(o, 0x28, -0x62, 0, 0x81)) {
            o_delete(o);
            return;
        }
        /* fall through */
    case 2: {                              /* (CD's touch $FB, special: a rolling player breaks it) */
        int hit = o_s1_touch(o, 0x12, 0x11), broken = 0;
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), OBJ_anim) == 2) broken = 1;
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        if (broken) SET8(o, OBJ_routine, 4);
        break;
    }
    case 4:                                /* (PLC 4: the animals' art back; here it never left) */
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_frame, 1);
        SET8(o, PJ_GONE, 0xFF);
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        cd_get_off(o);
        /* fall through */
    case 6: {                              /* exploding */
        uint32_t a6 = r32(o + PJ_LOCS);
        uint8_t when = a6 ? tri_rd8(a6) : 0xFF;
        if (when & 0x80) { SET8(o, OBJ_routine, 8); SET16(o, PJ_COUNT, 60); break; }
        SET8(o, PJ_COUNT, OB8(o, PJ_COUNT) + 1);
        if (OB8(o, PJ_COUNT) != when) break;
        int8_t dx = (int8_t)tri_rd8(a6 + 1), dy = (int8_t)tri_rd8(a6 + 2);
        SET32(o, PJ_LOCS, a6 + 3);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);           /* (CD's explosion, routine 2 set: no flower, no points) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
        o_sfx_cd(FM_EXPLODE);
        break;
    }
    default:                               /* gone */
        SET16(o, PJ_COUNT, OB16(o, PJ_COUNT) - 1);
        if (OB16(o, PJ_COUNT)) break;
        scd_proj_destroyed_set();
        o_delete(o);
        return;
    }
    if (OB8(o, OBJ_routine) <= 2 && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $28 animals (friend7): flying round a loop or hopping (turning at
 * every other landing); subtype bit 7: the projector's holograms of them ---- */
#define AN_X0           0x30               /* oVar2A */
#define AN_Y0           0x32               /* oVar2C (flying) */
#define AN_VX           0x34               /* oVar2C (hopping, long) */
#define AN_ANG          0x38               /* oVar2E */
#define AN_STEP         0x39               /* oVar2F */
#define AN_VY           0x3A               /* oVar30 (long) */
static void fly_place(uint32_t o, int sx, int sy)
{
    int16_t s, c;
    o_sincos(OB8(o, AN_ANG), &s, &c);
    SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> sx));
    SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> sy));
}

static void animal(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    int holo = sub & 0x80;
    if (!holo) {                           /* CheckAnimalPrescence */
        if (cd_period() == TRI_CD_PAST || cd_period() == TRI_CD_PRESENT) {
            if (!scd_proj_destroyed()) { cd_despawn(o, OB16(o, OBJ_x_pos)); return; }
        } else if (!scd_good_future()) { o_delete(o); return; }
    }
    if (!(sub & 0x7F)) {                   /* flying */
        switch (OB8(o, OBJ_routine)) {
        case 0:
            if (!o_asset_maps(holo ? SCDA_HOLOFLY : SCDA_ANIMALFLY)) { o_delete(o); return; }
            SET8(o, OBJ_routine, 2);
            o_init(o, holo ? SCDA_HOLOFLY : SCDA_ANIMALFLY, 4, 4, 8, 8);
            SET8(o, OBJ_x_radius, 8);
            SET8(o, OBJ_y_radius, 8);
            SET16(o, AN_X0, OB16(o, OBJ_x_pos));
            SET16(o, AN_Y0, OB16(o, OBJ_y_pos));
            turn(o);
            if (holo) {
                SET8(o, OBJ_routine, 4);
                SET8(o, OBJ_anim, 1);
                SET16(o, OBJ_priority, 3 << 7);
            } else {
                SET8(o, AN_ANG, 1);
                SET8(o, AN_STEP, 1);
            }
            return;                        /* (the first frame: unseen) */
        case 2: {
            fly_place(o, 1, 1);
            uint8_t d0 = (uint8_t)(OB8(o, AN_ANG) + OB8(o, AN_STEP));
            if ((uint8_t)(d0 - 1) >= 0x7F) {   /* (round the loop: back) */
                d0 = OB8(o, AN_ANG);
                SET8(o, AN_STEP, (uint8_t)-(int8_t)OB8(o, AN_STEP));
                turn(o);
            }
            SET8(o, AN_ANG, d0);
            o_animate(o, o_asset_anim(SCDA_ANIMALFLY));
            if (cd_despawn(o, OB16(o, AN_X0))) return;
            o_draw(o);
            return;
        }
        default:                           /* (the projector's show) */
            if (holo_gone(o)) { o_delete(o); return; }
            fly_place(o, 3, 4);
            SET8(o, AN_ANG, OB8(o, AN_ANG) + 4);
            if (!(OB8(o, AN_ANG) & 0x7F)) { turn(o); return; }
            o_animate(o, o_asset_anim(SCDA_HOLOFLY));
            o_draw(o);
            return;
        }
    }
    switch (OB8(o, OBJ_routine)) {         /* on the ground */
    case 0:
        if (!o_asset_maps(holo ? SCDA_HOLOGROUND : SCDA_ANIMALGROUND)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, holo ? SCDA_HOLOGROUND : SCDA_ANIMALGROUND, 4, 4, 8, 0x0C);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_y_radius, 0x0C);
        if (holo) { SET8(o, OBJ_routine, 8); turn(o); return; }
        SET32(o, AN_VX, 0x10000);
        SET32(o, AN_VY, (uint32_t)-0x40000);
        return;
    case 2: case 4: {                      /* hopping (the second landing: a turn) */
        cd_move(o, (int32_t)r32(o + AN_VX), (int32_t)r32(o + AN_VY));
        SET32(o, AN_VY, r32(o + AN_VY) + 0x2000);
        SET8(o, OBJ_frame, (int32_t)r32(o + AN_VY) < 0 ? 0 : 1);
        int16_t d1 = o_floor_dist(o);
        scd_ssz_block_prio(o, OBS16(o, OBJ_x_pos), OBS16(o, OBJ_y_pos) + (int8_t)OB8(o, OBJ_y_radius));
        if (d1 < 0) {
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET32(o, AN_VY, (uint32_t)-0x40000);
        }
        break;
    }
    case 6:
        SET8(o, OBJ_routine, 2);
        SET32(o, AN_VX, (uint32_t)-(int32_t)r32(o + AN_VX));
        turn(o);
        break;
    default:                               /* (the projector's show) */
        if (holo_gone(o)) { o_delete(o); return; }
        o_animate(o, o_asset_anim(SCDA_HOLOGROUND));
        o_draw(o);
        return;
    }
    draw_despawn(o);
}

/* ---- $38 searchlights (slight7): fixed to the screen's middle row, a
 * swing of steps (frame, frames, flip, x) by subtype, or still (4-8) ------- */
#define SL_PHASE        0x30               /* oVar30: the frame (of 2, or of 4) it is seen on */
#define SL_X0           0x36               /* oVar36 */
#define SL_TIME         0x3A               /* oVar3A */
#define SL_STEP         0x3C               /* oVar3C */
#define SL_STEPS        0x3D               /* oVar3D */
static void sl_seq(uint32_t o, const int8_t *t, int steps)
{
    SET8(o, SL_STEPS, steps);
    if (!OB8(o, SL_TIME)) {
        const int8_t *e = t + OB8(o, SL_STEP) * 4;
        SET8(o, OBJ_frame, e[0]);
        SET8(o, SL_TIME, (uint8_t)e[1]);
        SET16(o, OBJ_x_pos, OB16(o, SL_X0) + e[3]);
        uint8_t f = e[2] ? 1 : 0;
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | f);
        SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | f);
    }
    SET8(o, SL_TIME, OB8(o, SL_TIME) - 1);
    if (OB8(o, SL_TIME)) return;
    SET8(o, SL_STEP, OB8(o, SL_STEP) + 1);
    if (OB8(o, SL_STEP) >= OB8(o, SL_STEPS)) SET8(o, SL_STEP, 0);
}

static void searchlight(uint32_t o)
{
    static const int8_t a[] = { 0, 10, 0, 0, 3, 20, 0, 0, 2, 20, 0, 0x44, 1, 20, 0, 0x0C, 3, 10, 0, 0,
                                2, 30, 1, -4, 1, 20, 0, 0x0C, 3, 20, 0, 0, 1, 10, 1, 0x40, 3, 20, 0, 0,
                                2, 20, 1, -0x10 };
    static const int8_t b[] = { 0, 30, 0, 0, 1, 30, 0, 0x30, 2, 30, 0, 0x5C, 1, 30, 0, 0x30,
                                0, 30, 0, 0, 1, 30, 1, -0x30, 2, 30, 1, -0x5C, 1, 30, 1, -0x30 };
    static const int8_t c[] = { 0, 30, 0, 0, 1, 30, 0, 0x30, 2, 30, 0, 0x5C, 1, 30, 0, 0x30 };
    static const int8_t d[] = { 0, 30, 0, 0, 1, 30, 1, -0x30, 2, 30, 1, -0x5C, 1, 30, 1, -0x30 };
    uint8_t sub = OB8(o, OBJ_subtype) & 0x0F, sub2 = OB8(o, OBJ_subtype) >> 4;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        int low = (sub2 & 8) != 0, line2 = low && cd_period() != TRI_CD_PRESENT;
        o_init(o, line2 ? SCDA_SEARCHLIGHT2 : SCDA_SEARCHLIGHT, OB8(o, OBJ_render) | 4, low ? 3 : 1, 0x7F, 0x7F);
        SET8(o, OBJ_y_radius, 0x7F);
        if (low) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) & 0x7FFF);   /* ($24AF; past, futures $44AF) */
        SET16(o, SL_X0, OB16(o, OBJ_x_pos));
        SET8(o, SL_PHASE, sub2 & 3);
    }
    SET16(o, OBJ_y_pos, r16(RAM_Camera_Y_pos) + 0x70);
    switch (sub) {
    case 0: sl_seq(o, a, 11); break;
    case 1: sl_seq(o, b, 8); break;
    case 2: sl_seq(o, c, 4); break;
    case 3: sl_seq(o, d, 4); break;
    case 4: case 5: case 6:
        SET8(o, OBJ_frame, sub - 4);
        break;
    case 7: case 8:
        SET8(o, OBJ_frame, sub - 6);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        break;
    default:
        break;
    }
    if (cd_despawn(o, OB16(o, SL_X0))) return;
    uint8_t f = (uint8_t)(o_frame_counter() & 3);
    if (sub2 & 4 ? f == OB8(o, SL_PHASE) : (f & 1) == OB8(o, SL_PHASE)) o_draw(o);   /* (the flicker) */
}

void scd_ssz_obj_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x20] = swing;
    t[base + 0x24] = wall;
    t[base + 0x25] = iron_ball;
    t[base + 0x26] = launcher;
    t[base + 0x27] = projector;
    t[base + 0x28] = animal;
    t[base + 0x38] = searchlight;
}
