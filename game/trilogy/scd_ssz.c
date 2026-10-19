/*
 * scd_ssz.c — Stardust Speedway, acts 1 and 2, and act 3's player rules (Sonic CD; decomp: R7\CHGBAN.C,
 * CHGWALL7.C, PLAYSP7.C, FCOL.C st_starDataChk; the disc's code is the truth:
 * where they differ, it is followed and the difference noted).
 *
 * Two layers. The flip panels ($1D) toggle the layer flag (prio_flag; the
 * disc's "layer"). The disc's GetLevelBlock reads a layout chunk flagged as a
 * loop chunk (layout bit 7) by these rules, for any object asking:
 *   chunks 4 and 6    Sonic 1's loops: chunk + 1 behind (oSprFlags bit 6)
 *   with the layer    chunks $16 $28 $2F $37 $3C read chunk + 1
 * and it sets the asker's sprite priority: low outside loop chunks; in one,
 * high, unless it is chunk 4 or 6, or the layer is set. scd_level.py gives
 * a loop chunk's other chunk as the secondary collision path, so behind or on
 * the back layer here is S3K's secondary path (top_solid_bit $E), set before
 * the player moves (TH_LVL_PRE_SPRITES; trilogy_player.c's Sonic 1 loop rule
 * runs after him and is overruled here); his priority (art_tile bit 15) is set
 * after he moves (TH_LVL_POST_SPRITES), from the chunk at his feet.
 * ObjSonic_SpecialChunks keeps bit 6: Sonic 1's rule in loop chunk 4 ($84);
 * chunk 6 ($86) has Stardust Speedway's own (by quarter of the chunk, only
 * while not rising).
 * PLAYSP7 (ObjSonic_SpecialCol): every 4th frame at $A00 or faster over one
 * of the period's metal blocks (Sonic CD's block numbers), sparks ($09).
 * The back-layer floors ($35) are solid only on the back layer.
 * Sonic CD has all this for player 1; here both players follow the layer.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include <string.h>

#define SSZ_ROUND       5                  /* (ROUND_INDEX: Stardust Speedway) */

#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_OBJCTRL       0x2E
#define P_INVULN        0x34
#define P_JUMPING       0x40
#define ST_AIR          0x02
#define OBJ_prev_anim   0x21               /* (what o_animate compares) */
#define FM_THROW        0xCE               /* (spr_r_move2: a rotating spring throws him; gun7set: the kick) */
#define FM_CLANK        0xC0               /* (spr_r_move6: a cage's bar shuts) */

void cd_spin_tag(uint32_t o);              /* scd_ppz.c: Palmtree Panic's, Stardust Speedway's speed in it */
void cd_generator(uint32_t o);
void scd_ssz_obj_register(TriObjFn *t, int base);      /* scd_ssz_obj.c */
void scd_ssz_badnik_register(TriObjFn *t, int base);   /* scd_ssz_badnik.c */

static int s_base;                         /* this round's native slots (scd_obj.c) */
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static uint8_t s_layer;                    /* layer: the back layer (the flip panels) */
static uint8_t s_behind[2];                /* oSprFlags bit 6: behind loop chunks 4 and 6 */

static int ssz(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == SSZ_ROUND;   /* (act 3's player code, its GetLevelBlock
                                                                    *  and loop chunks are acts 1-2's) */
}

/* ---- Sonic CD's level blocks (scd_level.py BLOCK_ROUNDS: header +$E8:
 * u16 chunks, u16 FG width, height; the FG rows as laid out, bit 7 kept;
 * the chunks) ---------------------------------------------------------------- */
static uint32_t cd_blocks(void)
{
    uint32_t e = tri_dir();
    return e ? tri_rd32(e + 0xE8) : 0;
}

static int layout_at(int x, int y)         /* the FG layout byte there; -1 off the map */
{
    uint32_t t = cd_blocks();
    if (!t || x < 0 || y < 0) return -1;
    unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4), col = (unsigned)x >> 8, row = (unsigned)y >> 8;
    if (col >= fw || row >= fh) return -1;
    return tri_rd8(t + 6 + row * fw + col);
}

static int layer_chunk(int c) { return c == 0x16 || c == 0x28 || c == 0x2F || c == 0x37 || c == 0x3C; }

/* GetLevelBlock's chunk (st_starDataChk): a loop chunk's other one, behind
 * (4, 6) or on the back layer */
static int chunk_read(int b, int behind)
{
    int c = b & 0x7F;
    if (b & 0x80) {
        if (c == 4 || c == 6) { if (behind) c++; }
        else if (s_layer && layer_chunk(c)) c++;
    }
    return c;
}

/* ... and what it does to the asker's sprite priority */
static void block_prio(uint32_t o, int b)
{
    if (b <= 0) return;                    /* (no chunk: as it was) */
    int c = b & 0x7F;
    uint16_t t = OB16(o, OBJ_art_tile);
    if (!(b & 0x80) || c == 4 || c == 6 || s_layer) t &= 0x7FFF;
    else t |= 0x8000;
    SET16(o, OBJ_art_tile, t);
}

/* for an object's floor or wall check at (x, y) (scd_ssz_badnik.c, scd_ssz_obj.c) */
void scd_ssz_block_prio(uint32_t o, int x, int y)
{
    if (ssz()) block_prio(o, layout_at(x, y));
}

static uint16_t block_at(int x, int y, int behind)   /* Sonic CD's block number there */
{
    uint32_t t = cd_blocks();
    int b = layout_at(x, y);
    if (b <= 0) return 0;
    unsigned c = (unsigned)chunk_read(b, behind);
    if (!c || c > tri_rd16(t)) return 0;
    unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4);
    uint32_t chunks = t + 6 + ((fw * fh + 1u) & ~1u);
    uint32_t a = chunks + (c - 1u) * 0x200u + ((((unsigned)y >> 4) & 15u) * 16u + (((unsigned)x >> 4) & 15u)) * 2u;
    return (uint16_t)(tri_rd16(a) & 0x7FF);
}

/* ---- the player -------------------------------------------------------- */
/* ObjSonic_SpecialChunks (loop chunks: TD_EXTRA's special chunks; no roll
 * tunnels in this zone) */
static void special_chunks(int n, uint32_t p)
{
    uint32_t info = tri_rd32(tri_dir() + TD_EXTRA);
    if (!info) return;
    int b = layout_at(OBS16(p, OBJ_x_pos), OBS16(p, OBJ_y_pos));
    uint8_t xl = (uint8_t)OB16(p, OBJ_x_pos), yl = (uint8_t)OB16(p, OBJ_y_pos), ang = OB8(p, OBJ_angle);
    if (b < 0 || (b != tri_rd8(info + 512) && b != tri_rd8(info + 513))) { s_behind[n] = 0; return; }
    if (b == tri_rd8(info + 513)) {        /* (chunk 6: Stardust Speedway's rule) */
        if (OBS16(p, OBJ_y_vel) < 0) return;
        if (yl >= 0x38 && yl < 0x80) return;
        s_behind[n] = (xl < 0x80) == (yl < 0x38);
        return;
    }
    if (xl < 0x2C) { s_behind[n] = 0; return; }   /* (chunk 4: Sonic 1's) */
    if (xl >= 0xE0) { s_behind[n] = 1; return; }
    if (!s_behind[n]) { if (ang && ang <= 0x80) s_behind[n] = 1; }
    else if (ang > 0x80) s_behind[n] = 0;
}

/* PLAYSP7: the blocks that spark, by period (the disc's lists; the past's
 * is the present's and 154, where the decomp has 154, 155, 164, 165 too) */
static void sparks_check(uint32_t p, int behind, int *prio_y)
{
    static const int16_t past[] = { 10, 190, 334, 335, 344, 345, 154, -1 };
    static const int16_t present[] = { 10, 190, 334, 335, 344, 345, -1 };
    static const int16_t bad[] = { 10, 210, 354, 355, 364, 365, -1 };
    static const int16_t good[] = { 618, 517, 527, 571, 581, -1 };
    if (o_frame_counter() & 3) return;
    int16_t vx = OBS16(p, OBJ_x_vel);
    if ((vx < 0 ? -vx : vx) < 0xA00) return;
    int x = OBS16(p, OBJ_x_pos), y = OBS16(p, OBJ_y_pos) + 8;
    *prio_y = y;                           /* (the frame's last GetLevelBlock) */
    uint16_t b = block_at(x, y, behind);
    int per = cd_period();
    const int16_t *t = per == TRI_CD_PAST ? past : per == TRI_CD_PRESENT ? present : per == TRI_CD_BAD ? bad : good;
    for (; *t >= 0; t++) {
        if (b != (uint16_t)*t) continue;
        uint32_t a = o_alloc();
        if (!a) return;
        SET32(a, OBJ_code, CD_CODE(0x09));
        SET16(a, OBJ_x_pos, OB16(p, OBJ_x_pos));
        SET16(a, OBJ_y_pos, OB16(p, OBJ_y_pos) + (int8_t)OB8(p, P_YRAD));
        if (vx < 0) SET8(a, OBJ_status, OB8(a, OBJ_status) | ST_XFLIP);
        return;
    }
}

static void pre_sprites(void)              /* (the path he moves on) */
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || r16(RAM_Debug_placement_mode)) continue;
        int b = layout_at(OBS16(p, OBJ_x_pos), OBS16(p, OBJ_y_pos)), c = b & 0x7F;
        int behind = b > 0 && (b & 0x80) && (c == 4 || c == 6) ? s_behind[n] : s_layer;
        w8(p + OBJ_top_solid, behind ? 0x0E : 0x0C);
        w8(p + OBJ_lrb_solid, behind ? 0x0F : 0x0D);
    }
}

static void post_sprites(void)
{
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || r16(RAM_Debug_placement_mode)) continue;
        uint8_t r = OB8(p, P_ROUTINE);
        /* (his own move ran, and with it GetLevelBlock: normal and not held, or hurt) */
        if ((r == 2 && !(OB8(p, P_OBJCTRL) & 1)) || r == 4) {
            int x = OBS16(p, OBJ_x_pos), y = OBS16(p, OBJ_y_pos);
            int py = !(OB8(p, OBJ_status) & ST_AIR) && ((OB8(p, OBJ_angle) + 0x20) & 0xC0) == 0
                     ? y + (int8_t)OB8(p, P_YRAD) : y;   /* (on the floor: his feet) */
            sparks_check(p, s_behind[n], &py);
            block_prio(p, layout_at(x, py));
        }
        if (r == 2) special_chunks(n, p);
    }
}

/* ======================================================================
 * Objects (CD's oVar2A.. at S3K's $30..; routines keep CD's numbers)
 * ==================================================================== */
static uint32_t ram_at(uint32_t o, int f)   /* (a slot kept as its RAM word) */
{
    uint16_t w = OB16(o, f);
    return w ? 0xFF0000u | w : 0;
}

static void draw_despawn(uint32_t o)       /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* AnimateObject over a script kept here ([time, frames..., $FF loop / $FC
 * routine + 2]; frame bits 5-6 flip, against the status) */
static void animate_c(uint32_t o, const uint8_t *s)
{
    if (OB8(o, OBJ_anim) != OB8(o, OBJ_prev_anim)) {
        SET8(o, OBJ_prev_anim, OB8(o, OBJ_anim));
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
    SET8(o, OBJ_anim_timer, (uint8_t)t);
    if (t >= 0) return;
    SET8(o, OBJ_anim_timer, s[0]);
    unsigned i = OB8(o, OBJ_anim_frame);
    uint8_t f = s[1 + i];
    if (f == 0xFC) { SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2); return; }
    if (f == 0xFF) { i = 0; f = s[1]; }
    SET8(o, OBJ_frame, f & 0x1F);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (((f >> 5) ^ OB8(o, OBJ_status)) & 3));
    SET8(o, OBJ_anim_frame, (uint8_t)(i + 1));
}

/* the box both CHGBAN.C objects touch the player with (the disc's coli0):
 * half the player's x / y radius plus w / h. It first puts a hurt player back
 * to normal, invulnerable for 120 frames: whenever it runs, touching or not
 * (Sonic CD's: a rotating spring nearby ends a hurt player's knockback) */
static int box(uint32_t o, uint32_t p, int w, int h)
{
    if (OB8(p, P_ROUTINE) == 4) {
        SET8(p, P_ROUTINE, 2);
        SET8(p, P_INVULN, 120);
    }
    if (OB8(p, P_ROUTINE) >= 6) return 0;  /* (CD's bored "giving up" pose: none in S3K) */
    int d1 = (int8_t)OB8(p, P_XRAD) + w, d0 = OBS16(o, OBJ_x_pos) - OBS16(p, OBJ_x_pos) + d1;
    if (d0 < 0 || d0 >= 2 * d1) return 0;
    d1 = (int8_t)OB8(p, P_YRAD) + h;
    d0 = OBS16(o, OBJ_y_pos) - OBS16(p, OBJ_y_pos) + d1;
    return d0 >= 0 && d0 < 2 * d1;
}

/* ---- $08 piston kicker bar (gun7): player 1 running into it on the ground
 * is kicked back the way he came at $1000; the bar jabs out 48 px and back,
 * a piece falls away (subtype set), sparks fly --------------------------- */
#define KB_TIME         0x30               /* oVar2A */
#define KB_X0           0x32               /* oVar2C */
static void kick(uint32_t o, uint32_t p)   /* gun7set */
{
    SET8(p, P_OBJCTRL, 0);
    uint32_t piece = o_alloc(), spark = 0;
    if (piece) {
        SET32(piece, OBJ_code, CD_CODE(0x08));
        SET16(piece, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(piece, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(piece, OBJ_subtype, 1);
        if ((spark = o_alloc()) != 0) {
            SET32(spark, OBJ_code, CD_CODE(0x09));
            SET16(spark, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(spark, OBJ_y_pos, OB16(o, OBJ_y_pos) - 6);
        }
    }
    o_sfx_cd(FM_THROW);
    SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
    SET16(o, KB_X0, OB16(o, OBJ_x_pos));
    SET8(o, KB_TIME, 6);
    int left = OB16(o, OBJ_x_pos) < OB16(p, OBJ_x_pos);      /* (he came from its right: back left) */
    SET16(p, OBJ_x_vel, left ? -0x1000 : 0x1000);
    if (spark) {
        SET8(spark, OBJ_status, left ? OB8(spark, OBJ_status) | ST_XFLIP : OB8(spark, OBJ_status) & ~ST_XFLIP);
        SET16(spark, OBJ_x_pos, OB16(spark, OBJ_x_pos) + (left ? 16 : -16));
    }
    if (piece) SET16(piece, OBJ_x_vel, left ? 0x200 : -0x200);
    SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
    SET16(o, OBJ_x_vel, left ? 0x800 : -0x800);
    SET8(p, OBJ_status, left ? OB8(p, OBJ_status) | ST_XFLIP : OB8(p, OBJ_status) & ~ST_XFLIP);
}

static void kicker(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SSZ_KICKER, OB8(o, OBJ_render) | 4, 0, 0x20, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET8(o, OBJ_frame, 2);
        if (OB8(o, OBJ_subtype)) {        /* (the piece) */
            SET8(o, OBJ_frame, 0);
            SET16(o, OBJ_y_vel, (uint16_t)-0x800);
            SET8(o, OBJ_routine, 6);
            goto fall;
        }
        /* fall through */
    case 2:
        if (!r16(RAM_Debug_placement_mode) && !(OB8(p, OBJ_status) & ST_AIR) && box(o, p, 0x20, 0x10))
            kick(o, p);
        break;
    case 4:                                /* (out for 6 frames, then back to where it was) */
        o_move(o);
        if (OB8(o, KB_TIME)) {
            SET8(o, KB_TIME, OB8(o, KB_TIME) - 1);
            if (!OB8(o, KB_TIME)) SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel));
        } else if (OB16(o, OBJ_x_pos) == OB16(o, KB_X0))
            SET8(o, OBJ_routine, 2);
        break;
    default:
    fall:
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x40);
        o_move(o);
        if ((uint16_t)(r16(RAM_Camera_Y_pos) + 0xE8) < OB16(o, OBJ_y_pos)) { o_delete(o); return; }
        break;
    }
    draw_despawn(o);
}

/* ---- $09 sparks (hibana): a few frames, then gone ----------------------- */
static void sparks(uint32_t o)
{
    if (OB8(o, OBJ_routine) >= 4) { o_delete(o); return; }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SSZ_SPARKS, OB8(o, OBJ_render) | 4, 0, 0, 0);   /* (CD: no width) */
    }
    o_animate(o, o_asset_anim(SCDA_SSZ_SPARKS));   /* (its script ends $FC) */
    o_draw(o);
}

/* ---- $1D layer-swap flip panel (chgban): player 1 touching it flips the
 * layer; it turns over (in front on the front layer), and any panel turns
 * when the layer is flipped elsewhere; 60 frames between (subtype: upside
 * down) ------------------------------------------------------------------ */
#define FP_TIME         0x30               /* oVar2A */
static void flip_panel(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (OB8(o, OBJ_subtype)) SET8(o, OBJ_render, OB8(o, OBJ_render) | 2);
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SSZ_PANEL, OB8(o, OBJ_render) | 4, 0, 0x18, 0x18);
        if (!s_layer) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_x_radius, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        /* fall through */
    case 2: {
        if (OB8(o, FP_TIME)) { SET8(o, FP_TIME, OB8(o, FP_TIME) - 1); break; }
        int d1 = (int8_t)OB8(p, P_XRAD) + (int8_t)OB8(o, OBJ_x_radius);
        int dx = OBS16(o, OBJ_x_pos) - OBS16(p, OBJ_x_pos) + d1;
        int h = (int8_t)OB8(p, P_YRAD) + (int8_t)OB8(o, OBJ_y_radius);
        int dy = OBS16(o, OBJ_y_pos) - OBS16(p, OBJ_y_pos) + h;
        if (dx >= 0 && dx < 2 * d1 && dy >= 0 && dy < 2 * h) {
            SET8(o, FP_TIME, 60);
            SET8(o, OBJ_routine, 4);
            s_layer ^= 1;
        } else if (!(OB16(o, OBJ_art_tile) & 0x8000) == !s_layer) {   /* (flipped elsewhere) */
            SET8(o, FP_TIME, 60);
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    case 4:                                /* (its script ends $FC) */
        o_animate(o, o_asset_anim(SCDA_SSZ_PANEL));
        break;
    default: {
        uint16_t t = OB16(o, OBJ_art_tile) | 0x8000;
        int prio = 4;
        if (s_layer) { t &= 0x7FFF; prio = 0; }
        SET16(o, OBJ_art_tile, t);
        SET16(o, OBJ_priority, (uint16_t)(prio << 7));
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_routine, 2);
        break;
    }
    }
    draw_despawn(o);
}

/* ---- $1E rotating spring (spring_r). The hub (placed) turns through eight
 * places, one every 6 frames, a spring and two bars on it. Subtype 0: the
 * spring catches player 1 (in a ball), holds him 60 frames, then throws him
 * at $1000 (diagonals $B50, $B50) the way it then points. Subtype 1: a cage:
 * a bar catching him, the other one closes (26 frames), the cage turns with
 * him in it, and a jump button throws him out. Subtype 2 or more: the spring
 * set at place subtype - 1, still. -------------------------------------- */
#define RS_LINK         0x30               /* oVar2A: the hub (spring, bars); the spring (hub) */
#define RS_PLAYER       0x32               /* oVar2E: the player held (spring) */
#define RS_TIME         0x34               /* oVar32 */
#define RS_X0           0x36               /* oVar34: where it was placed */
#define RS_SIDE1        0x38               /* oVar36: the bar on routine $A (hub) */
#define RS_SIDE0        0x3A               /* oVar3A: the bar on routine $C (hub) */
#define RS_DIE          0x3C               /* oVar3E: the hub has gone */
#define RS_ROUT2        0x3D               /* oRoutine2 */
#define RS_STATE        0x3E               /* oSubtype2 */
static const uint8_t hub_ani[] = { 5, 1, 1, 1, 1, 1, 1, 1, 1, 0xFF };
static const uint8_t spring_ani[] = { 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 1, 0xFC };
static const uint8_t sp_map[9] = { 2, 0, 2, 1, 2, 0, 2, 1, 2 };   /* (0 sideways, 1 up, 2 diagonal) */
static const uint8_t sp_rev[3][9] = { { 0, 0, 0, 0, 1, 1, 1, 0, 0 }, { 2, 0, 0, 0, 1, 1, 3, 2, 2 },
                                      { 1, 1, 3, 2, 2, 0, 0, 0, 1 } };
static const int8_t sp_pos[3][18] = {
    { 8, -8, 8, 0, 8, -8, 0, -8, -8, -8, -8, 0, -8, -8, 0, -8, 8, -8 },
    { 8, 8, 8, 0, 8, -8, 0, -8, -8, -8, -8, 0, -8, 8, 0, 8, 8, 8 },
    { -8, -8, -8, 0, -8, 8, 0, 8, 8, 8, 8, 0, 8, -8, 0, -8, -8, -8 } };
static const int8_t pl_pos[16] = { 24, 0, 24, -24, 0, -24, -24, -24, -24, 0, -24, 24, 0, 24, 24, 24 };
static const uint8_t pl_idx0[9] = { 1, 0, 1, 2, 3, 4, 3, 2, 1 };  /* (held by the spring) */
static const uint8_t pl_idxg[2][9] = { { 1, 0, 1, 2, 3, 4, 5, 6, 7 }, { 3, 4, 5, 6, 7, 0, 1, 2, 3 } };   /* (in the cage) */
static const uint8_t sd_frame[9] = { 4, 2, 4, 3, 4, 2, 4, 3, 4 };
static const uint8_t sd_rev[2][2][9] = {   /* [cage][bar] */
    { { 0, 0, 0, 0, 2, 2, 2, 0, 0 }, { 3, 2, 3, 1, 1, 0, 1, 1, 3 } },
    { { 1, 0, 0, 0, 2, 2, 3, 1, 1 }, { 2, 2, 3, 1, 1, 0, 0, 0, 2 } } };
static const uint8_t sd_idx[3][2][9] = {   /* [spring, cage, cage closed][bar] */
    { { 1, 0, 1, 2, 3, 4, 3, 2, 1 }, { 9, 8, 9, 10, 11, 12, 11, 10, 9 } },
    { { 7, 0, 1, 2, 3, 4, 5, 6, 7 }, { 3, 4, 5, 6, 7, 0, 1, 2, 3 } },
    { { 11, 12, 13, 14, 15, 8, 9, 10, 11 }, { 15, 8, 9, 10, 11, 12, 13, 14, 15 } } };
static const int8_t sd_pos[32] = { 24, -16, 4, -28, -16, -24, -28, -4, -24, 16, -4, 28, 16, 24, 28, 4,
                                   24, 16, 28, -4, 16, -24, -4, -28, -24, -16, -28, 4, -16, 24, 4, 28 };
static const int8_t sd_vel[2][18] = { { -1, -1, -1, 0, -1, 1, 0, 1, 1, 1, 1, 0, 1, -1, 0, -1, -1, -1 },
                                      { 1, 1, 1, 0, 1, -1, 0, -1, -1, -1, -1, 0, -1, 1, 0, 1, 1, 1 } };

static unsigned place_of(uint32_t hub)     /* (the hub's animation frame: its place, 0-8) */
{
    unsigned k = OB8(hub, OBJ_anim_frame);
    return k > 8 ? 8 : k;
}

static void place_player(uint32_t ref, uint32_t hub, uint32_t p, const uint8_t *idx)   /* pposiset */
{
    unsigned i = idx[place_of(hub)];
    SET16(p, OBJ_x_pos, OB16(ref, OBJ_x_pos) + pl_pos[i * 2]);
    SET16(p, OBJ_y_pos, OB16(ref, OBJ_y_pos) + pl_pos[i * 2 + 1]);
}

static void launch(uint32_t spr, uint32_t p, unsigned k)   /* plspdset: the way the spring points */
{
    uint8_t r = OB8(spr, OBJ_render), st = OB8(p, OBJ_status);
    int16_t vx, vy;
    if (!(k & 1)) {                        /* (diagonally: CalcSine($E0) x $1000, mulu) */
        int16_t s, c;
        o_sincos(0xE0, &s, &c);
        vy = (int16_t)(((uint32_t)(uint16_t)s * 0x1000u) >> 8);
        vx = (int16_t)(((uint32_t)(uint16_t)c * 0x1000u) >> 8);
        if (r & 2) vy = (int16_t)-vy;
        st &= ~ST_XFLIP;
        if (r & 1) { st |= ST_XFLIP; vx = (int16_t)-vx; }
    } else if (!(k & 2)) {
        vy = 0;
        vx = r & 1 ? -0x1000 : 0x1000;
    } else {
        vx = 0;
        vy = r & 2 ? 0x1000 : -0x1000;
    }
    SET16(p, OBJ_y_vel, (uint16_t)vy);
    SET16(p, OBJ_x_vel, (uint16_t)vx);
    SET8(p, OBJ_status, (st | ST_AIR) & ~0x08);
}

static void hold(uint32_t p)               /* (held in a ball: his own move off) */
{
    SET8(p, P_JUMPING, 0);
    SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 4);
    SET8(p, P_YRAD, 0x0E);
    SET8(p, P_XRAD, 7);
    SET8(p, P_ANIM, 2);
}

static void spring_place(uint32_t o)       /* spr_r_set */
{
    static const int assets[3] = { SCDA_SSZ_SPRING_SIDE, SCDA_SSZ_SPRING_UP, SCDA_SSZ_SPRING_DIAG };
    unsigned t = OB8(o, OBJ_subtype) > 2 ? 2 : OB8(o, OBJ_subtype);
    uint32_t hub = ram_at(o, RS_LINK);
    unsigned k = place_of(hub);
    o_asset(o, assets[sp_map[k]]);         /* (art $520, the diagonal's $4F2) */
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | sp_rev[t][k]);
    SET16(o, OBJ_x_pos, OB16(hub, OBJ_x_pos) + sp_pos[t][k * 2]);
    SET16(o, OBJ_y_pos, OB16(hub, OBJ_y_pos) + sp_pos[t][k * 2 + 1]);
}

static void side_move(uint32_t o, int s);

static void catch_side(uint32_t o, uint32_t hub, uint32_t p, int s)   /* playset_g */
{
    hold(p);
    uint32_t spr = ram_at(hub, RS_LINK);
    SET8(spr, OBJ_routine, OB8(spr, OBJ_routine) + 2);
    SET8(spr, RS_STATE, 1);
    SET16(spr, RS_PLAYER, (uint16_t)p);
    place_player(spr, hub, p, pl_idxg[s]);
    uint32_t other = ram_at(hub, OB8(o, OBJ_routine) == 0x0A ? RS_SIDE0 : RS_SIDE1);
    if (!other) return;
    SET8(other, RS_ROUT2, OB8(other, OBJ_routine));
    int os = OB8(other, OBJ_routine) == 0x0A ? 0 : 1;
    SET8(spr, OBJ_subtype, os ? 1 : 2);   /* (the spring's tables: which bar shut) */
    side_move(other, os);
    SET8(other, OBJ_routine, 0x0E);
    SET8(other, RS_STATE, 1);
    const int8_t *v = sd_vel[OB8(other, RS_ROUT2) == 0x0A ? 0 : 1];   /* gspdset: it shuts, a pixel a frame */
    unsigned k = place_of(hub);
    SET16(other, OBJ_x_vel, (uint16_t)(int16_t)v[k * 2]);
    SET16(other, OBJ_y_vel, (uint16_t)(int16_t)v[k * 2 + 1]);
    SET8(other, RS_TIME, 26);
}

static void side_move(uint32_t o, int s)   /* g_move0 (s: 0 the bar on routine $A, 1 on $C) */
{
    uint32_t hub = ram_at(o, RS_LINK);
    unsigned k = place_of(hub);
    int cage = OB8(o, OBJ_subtype) != 0;
    SET8(o, OBJ_frame, sd_frame[k]);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~3) | sd_rev[cage][s][k]);
    unsigned i = sd_idx[cage ? (OB8(o, RS_STATE) ? 2 : 1) : 0][s][k];
    SET16(o, OBJ_x_pos, OB16(hub, OBJ_x_pos) + sd_pos[i * 2]);
    SET16(o, OBJ_y_pos, OB16(hub, OBJ_y_pos) + sd_pos[i * 2 + 1]);
    if (!cage) return;
    uint32_t spr = ram_at(hub, RS_LINK);
    if (OB8(spr, RS_STATE)) {              /* (shut: the bar that caught him carries him round) */
        if (OB8(spr, RS_STATE) == 2 && !OB8(o, RS_STATE))
            place_player(spr, hub, ram_at(spr, RS_PLAYER), pl_idxg[s]);
        return;
    }
    if (r16(RAM_Debug_placement_mode)) return;
    if (box(o, RAM_Player_1, 0x18, 0x18)) catch_side(o, hub, RAM_Player_1, s);
    else SET8(o, RS_STATE, 0);
}

static int hub_init(uint32_t o)
{
    SET8(o, OBJ_routine, 2);
    SET16(o, RS_X0, OB16(o, OBJ_x_pos));
    o_init(o, SCDA_SSZ_CAGE, OB8(o, OBJ_render) | 4, 0, 0, 8);   /* (CD: no width) */
    SET8(o, OBJ_x_radius, 8);
    SET8(o, OBJ_y_radius, 8);
    SET8(o, OBJ_frame, 1);
    if (OB8(o, OBJ_subtype) >= 2) {        /* (still, at a place) */
        uint8_t k = (uint8_t)(OB8(o, OBJ_subtype) - 1);
        SET8(o, RS_STATE, k);
        SET8(o, OBJ_anim_frame, k);
        SET8(o, OBJ_subtype, 0);
    }
    static const uint8_t routines[3] = { 4, 0x0A, 0x0C };
    static const int links[3] = { RS_LINK, RS_SIDE1, RS_SIDE0 };
    uint32_t c[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        if (!(c[k] = o_alloc_after(o))) {
            while (k--) o_delete(c[k]);
            o_delete(o);
            return 0;
        }
        uint32_t a = c[k];
        SET32(a, OBJ_code, CD_CODE(0x1E));
        SET8(a, OBJ_render, OB8(a, OBJ_render) | 4);
        SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a, RS_X0, OB16(o, OBJ_x_pos));
        SET16(a, OBJ_y_pos, OB16(o, OBJ_y_pos));
        if (!k) {
            o_asset(a, SCDA_SSZ_SPRING_UP);
            SET16(a, OBJ_priority, 5 << 7);
        } else {
            o_asset(a, SCDA_SSZ_CAGE);
            SET8(a, OBJ_x_radius, 0x20);
            SET8(a, OBJ_y_radius, 0x20);
            SET8(a, OBJ_height_px, 0x20);
        }
        SET16(o, links[k], (uint16_t)a);
        SET8(a, OBJ_subtype, OB8(o, OBJ_subtype));
        SET16(a, RS_LINK, (uint16_t)o);
        SET8(a, OBJ_routine, routines[k]);
    }
    return 1;
}

static void spring_cage(uint32_t o)
{
    uint8_t r = OB8(o, OBJ_routine);
    if (r >= 4 && OB8(o, RS_DIE)) { o_delete(o); return; }
    if (!r || OB8(RAM_Player_1, P_ROUTINE) < 6) {   /* (frozen while he is dead) */
        switch (r) {
        case 0:
            if (!hub_init(o)) return;
            /* fall through */
        case 2: {                          /* the hub */
            if (OB8(o, RS_STATE)) break;
            uint32_t spr = ram_at(o, RS_LINK);
            if (spr && (OB8(spr, OBJ_routine) < 6 || OB8(spr, RS_STATE) == 2)) animate_c(o, hub_ani);
            break;
        }
        case 4:                            /* the spring, waiting (a cage's: hidden) */
            if (OB8(o, OBJ_subtype)) break;
            spring_place(o);
            if (!r16(RAM_Debug_placement_mode) && box(o, RAM_Player_1, 0x20, 0x20)) {   /* playset */
                uint32_t p = RAM_Player_1, hub = ram_at(o, RS_LINK);
                hold(p);
                SET16(o, RS_PLAYER, (uint16_t)p);
                SET8(o, RS_TIME, 60);
                SET8(o, OBJ_routine, 6);
                place_player(o, hub, p, pl_idx0);
                launch(o, p, place_of(hub));
            }
            break;
        case 6: {                          /* holding him */
            uint32_t hub = ram_at(o, RS_LINK);
            int release = 0, anim = 0;
            if (OB8(o, OBJ_subtype)) {
                if (OB8(o, RS_STATE) == 4) anim = 1;
                else if (OB8(o, RS_STATE) == 2 && (r8(RAM_Ctrl_1_pressed_logical) & 0x70)) {
                    SET8(o, RS_STATE, 4);
                    launch(o, ram_at(o, RS_PLAYER), place_of(hub));
                    release = 1;
                }
            } else if (!OB8(o, RS_TIME))
                anim = 1;
            else {
                SET8(o, RS_TIME, OB8(o, RS_TIME) - 1);
                release = !OB8(o, RS_TIME);
            }
            if (release) {
                SET8(RAM_Player_1, P_OBJCTRL, OB8(RAM_Player_1, P_OBJCTRL) & ~1);
                o_sfx_cd(FM_THROW);
            }
            if (release || anim) animate_c(o, spring_ani);   /* (its script ends $FC) */
            spring_place(o);
            break;
        }
        case 8: {                          /* (thrown: back to waiting, the bars open) */
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_anim_frame, 0);
            SET8(o, OBJ_frame, 0);
            SET8(o, RS_STATE, 0);
            if (OB8(o, OBJ_subtype)) {
                uint32_t hub = ram_at(o, RS_LINK), a = ram_at(hub, RS_SIDE1), b = ram_at(hub, RS_SIDE0);
                if (a) SET8(a, RS_STATE, 0);
                if (b) SET8(b, RS_STATE, 0);
            }
            break;
        }
        case 0x0A: case 0x0C:              /* the bars */
            side_move(o, r == 0x0C);
            break;
        default: {                         /* a bar shutting */
            SET8(o, RS_TIME, OB8(o, RS_TIME) - 1);
            if (!OB8(o, RS_TIME)) {
                uint32_t spr = ram_at(ram_at(o, RS_LINK), RS_LINK);
                if (spr) SET8(spr, RS_STATE, 2);
                SET8(o, OBJ_routine, OB8(o, RS_ROUT2));
                o_sfx_cd(FM_CLANK);
                break;
            }
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, OBJ_x_vel));
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + OB16(o, OBJ_y_vel));
            break;
        }
        }
    }
    r = OB8(o, OBJ_routine);
    if (!(r == 4 && OB8(o, OBJ_subtype))) o_draw(o);
    if (r != 2) return;
    uint32_t kids[3] = { ram_at(o, RS_LINK), ram_at(o, RS_SIDE1), ram_at(o, RS_SIDE0) };
    if (cd_despawn(o, OB16(o, RS_X0)))     /* (the hub gone: so are its spring and bars) */
        for (int k = 0; k < 3; k++)
            if (kids[k] && r32(kids[k]) == CD_CODE(0x1E)) SET8(kids[k], RS_DIE, 1);
}

/* ---- $35 back-layer floor (chgwall7): unseen, solid to player 1 on the
 * back layer; the layer flipped, he is let go -------------------------- */
#define BF_ON           0x30               /* oVar2A: stood on (or hit from below) */
static void back_floor(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, OBJ_x_radius, 0x20);
        SET8(o, OBJ_width_px, 0x20);
        SET8(o, OBJ_y_radius, 0x11);
        SET8(o, OBJ_height_px, 0x11);
    }
    if (s_layer) {                         /* (never drawn: solid wherever it is, unseen) */
        int16_t r = o_solid_1p(o, 0, 0x20 + 10, 0x11, 0x12, OB16(o, OBJ_x_pos), 1);
        SET8(o, BF_ON, (OB8(o, OBJ_status) & ST_P1_STAND) || r == -2 ? 0xFF : 0);
    } else if (OB8(o, BF_ON)) {
        SET8(o, BF_ON, 0);
        cd_get_off(o);
    }
    cd_despawn(o, OB16(o, OBJ_x_pos));     /* (Sonic CD shows it with the debug cheat only) */
}

void scd_ssz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x08] = kicker;
    t[base + 0x09] = sparks;
    t[base + 0x1D] = flip_panel;
    t[base + 0x1E] = spring_cage;
    t[base + 0x29] = cd_generator;
    t[base + 0x2B] = cd_spin_tag;
    t[base + 0x35] = back_floor;
    scd_ssz_obj_register(t, base);
    scd_ssz_badnik_register(t, base);
}

void scd_ssz3_register(TriObjFn *t, int base3);               /* scd_ssz_race.c */
void scd_ssz3_level_entry(void);

void scd_ssz_act3_register(TriObjFn *t, int base3)            /* (act 3: the race, and the back-layer floor) */
{
    t[base3 + 0x35] = back_floor;
    scd_ssz3_register(t, base3);
}

void scd_ssz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {            /* (the level's start clears it: flowerPosBuf..+$A00) */
        scd_ssz3_level_entry();
        s_layer = 0;
        memset(s_behind, 0, sizeof s_behind);
    }
    if (!tri_dir() || !ssz()) return;
    if (pc == TH_LVL_PRE_SPRITES) pre_sprites();
    else if (pc == TH_LVL_POST_SPRITES) post_sprites();
}
