/*
 * scd_mmz.c — Metallic Madness, acts 1 and 2 (Sonic CD round 8; decomp:
 * R8\PLAYSP8.C, BEAM.C, SHOOT.C, SHUT.C, PIPE8.C, SW8.C, HAGURUMA.C; the
 * disc's code is what is transcribed).
 *
 * The zone's player-level rules (the disc's player code and GetLevelBlock):
 *   layer     the rotating cylinders ($3E, act 2) swap the player between the
 *             front and the back layer (CD's `layer`, prio_flag). On the back
 *             layer the act's flagged chunks $46 $48 $4A $10 $63 $43 read as
 *             $6A-$6F for collision: the converter gives those chunks the back
 *             chunk's shapes as their secondary collision, so here the back
 *             layer is S3K's secondary path (top_solid $E / lrb $F), set
 *             before the players move. The player's sprite is low priority in
 *             an ordinary chunk and on the back layer, high in a flagged chunk
 *             on the front layer (GetLevelBlock, while his collision runs).
 *   conveyor  (act 2, playsp) Sonic CD blocks 270-276 under either foot carry
 *             the player 1 px a frame: right, left if the block is flipped,
 *             block 276 the other way.
 *   mini      the shrink ray ($1D) makes Sonic miniature (CD's miniSonic):
 *             radii $A/5 standing, 8/5 curled (CD moves him 2 px where the
 *             full size moves 5), no other physics change. Kept at a
 *             checkpoint and through a time warp. The drawing (CD's own small
 *             frames) is the renderer's: scd_mmz_mini() says when.
 *   warp      while an object holds the player (object_control bit 0) the time
 *             warp's run is reset (MMZ only: timeWarpTimer = 0, sparkles off).
 * These run from TH_LVL_PRE_SPRITES (the layer) and TH_LVL_POST_SPRITES (after
 * the players' own move, as CD's player code runs them after its mode).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include <string.h>

#define MMZ_ROUND       6                  /* (ROUND_INDEX: Metallic Madness) */
#define P_ROUTINE       0x05
#define P_ARTTILE       0x0A
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_PREVANIM      0x21
#define P_OBJCTRL       0x2E
#define P_CHAR          0x38               /* character_id (1: Tails) */
#define P_STICK         0x3C               /* stick_to_convex (CD's oPlayerStick) */
#define P_DEFYRAD       0x44               /* default_y_radius */
#define P_DEFXRAD       0x45
#define ANIM_HURT       0x1A               /* (S3K's) */
#define ANIM_TWIRL      0x0F               /* (S3K's twirl: CD's turn into or out of the screen) */
#define ANIM_WAIT       0x05
#define RAM_Ctrl_1      0xFFF604u          /* the pad as read (held, pressed) */

/* SCDA_* (-> trilogy_obj.h) */

int scd_time_warping(void);
void scd_time_hold(void);                  /* (scd_time.c: timeWarpTimer = 0, sparkles off, direction kept) */

static int mmz(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == MMZ_ROUND && tri_act() < 2;
}

static int s_base;
static uint8_t s_back;                     /* layer (prio_flag): on the back layer */
static uint8_t s_mini, s_mini_saved;       /* miniSonic; savedMiniSonic (the last checkpoint's) */
static uint8_t s_mini_on, s_def_y, s_def_x;   /* (player 1 made small; his own default radii) */
static uint8_t s_switch[32];               /* switchFlags: bit 7 held, 6 ever, 5 toggled */
static uint8_t s_turn;                     /* (a cylinder turning the player: CD's anim >= $33) */

int scd_mmz_back(void) { return mmz() && s_back; }
int scd_mmz_mini(void) { return mmz() && s_mini; }
uint8_t *scd_mmz_switch(int n) { return &s_switch[n & 31]; }

#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- the act's level data: Sonic CD's own layout (header +$E8, as
 * tri_scd_block_at reads it; scd_level.py BLOCK_ROUNDS) ---------------------- */
static uint32_t cd_layout(void) { uint32_t w = tri_dir(); return w ? tri_rd32(w + 0xE8) : 0; }

static int cd_chunk_at(int x, int y)            /* the FG layout byte (bit 7: flagged), 0 none */
{
    uint32_t t = cd_layout();
    if (!t || x < 0 || y < 0) return 0;
    unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4), col = ((unsigned)x >> 8) & 0x7F, row = ((unsigned)y >> 8) & 15;
    return col < fw && row < fh ? tri_rd8(t + 6 + row * fw + col) : 0;
}

static uint16_t cd_block_word(int x, int y)     /* the chunk's block word there (flips included) */
{
    uint32_t t = cd_layout();
    unsigned ch = (unsigned)cd_chunk_at(x, y) & 0x7F;
    if (!ch || ch > tri_rd16(t)) return 0;
    unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4);
    uint32_t chunks = t + 6 + ((fw * fh + 1u) & ~1u);
    return tri_rd16(chunks + (ch - 1u) * 0x200u + ((((unsigned)y >> 4) & 15u) * 16u + (((unsigned)x >> 4) & 15u)) * 2u);
}

/* ======================================================================
 * The player
 * ==================================================================== */
static int playing(uint32_t p)
{
    return o_player_active(p) && OB8(p, P_ROUTINE) == 2 && !r16(RAM_Debug_placement_mode);
}

static int on_belt(uint16_t w)                  /* (playsp_chk: blocks 270-276) */
{
    unsigned b = w & 0x7FF;
    return b >= 270 && b <= 276;
}

static void conveyor(uint32_t p)                /* playsp */
{
    int x = (int16_t)OB16(p, OBJ_x_pos), r = (int8_t)OB8(p, P_XRAD);
    int foot = (int16_t)OB16(p, OBJ_y_pos) + (int8_t)OB8(p, P_YRAD) + 2;
    uint16_t w = cd_block_word(x - r, foot);
    if (!on_belt(w)) {
        w = cd_block_word(x + r, foot);
        if (!on_belt(w)) return;
    }
    int dx = w & 0x800 ? -1 : 1;
    if ((w & 0x7FF) == 276) dx = -dx;
    SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + dx);
}

static void layer_path(uint32_t p)              /* (the back layer: the secondary collision) */
{
    SET8(p, OBJ_top_solid, s_back ? 0x0E : 0x0C);
    SET8(p, OBJ_lrb_solid, s_back ? 0x0F : 0x0D);
}

static void layer_priority(uint32_t p)          /* GetLevelBlock: oTile's priority, by the chunk at his feet */
{
    int c = cd_chunk_at((int16_t)OB16(p, OBJ_x_pos), (int16_t)OB16(p, OBJ_y_pos) + (int8_t)OB8(p, P_YRAD));
    if (!c) return;                                           /* (a blank chunk: as it was) */
    uint16_t t = OB16(p, P_ARTTILE);
    SET16(p, P_ARTTILE, (c & 0x80) && !s_back ? t | 0x8000 : t & 0x7FFF);
}

/* miniSonic: his default radii $A/5 while small; S3K's own curled size
 * ($E/7, set in place of the defaults) is made CD's small one (8/5) */
static void mini_radii(uint32_t p)
{
    uint8_t st = OB8(p, OBJ_status);
    if (st & 4) {
        if (OB8(p, P_YRAD) != 0x0E) return;
        SET8(p, P_YRAD, 8);
        SET8(p, P_XRAD, 5);
        int dy = st & 2 ? 6                             /* (a jump: S3K kept his foot; so do we) */
               : OB8(p, P_CHAR) == 1 ? 1 : -3;          /* (a roll: CD's +2 where S3K's +5, Tails' +1) */
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + dy);
    } else if (OB8(p, P_YRAD) == s_def_y && s_def_y != 10) {
        if (!(st & 2)) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + s_def_y - 10);
        SET8(p, P_YRAD, 10);
        SET8(p, P_XRAD, 5);
    }
}

static void mini_frame(uint32_t p)
{
    if (!o_player_active(p) || OB8(p, P_ROUTINE) >= 6) return;
    if (s_mini && !s_mini_on) {
        s_mini_on = 1;
        s_def_y = OB8(p, P_DEFYRAD);
        s_def_x = OB8(p, P_DEFXRAD);
        SET8(p, P_DEFYRAD, 10);
        SET8(p, P_DEFXRAD, 5);
    } else if (!s_mini && s_mini_on) {
        s_mini_on = 0;
        SET8(p, P_DEFYRAD, s_def_y);
        SET8(p, P_DEFXRAD, s_def_x);
        if (!(OB8(p, OBJ_status) & 4) && OB8(p, P_YRAD) == 10) {
            if (!(OB8(p, OBJ_status) & 2)) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - (s_def_y - 10));
            SET8(p, P_YRAD, s_def_y);
            SET8(p, P_XRAD, s_def_x);
        }
        return;
    }
    if (s_mini_on) mini_radii(p);
}

/* ======================================================================
 * Objects (CD's object variables oVar2A.. at S3K's $30..; routines keep CD's
 * even numbers; SolidObject -> o_solid, TopSolidObject -> o_platform)
 * ==================================================================== */

/* ---- $21 floor switch: switchFlags[subtype] while stood on --------------- */
#define SW_RIDE         0x31               /* (who stood on it last frame) */
#define SW_PRV          0x3E               /* oVar3E */
#define SW_NOW          0x3F               /* oVar3F */
static void mmz_switch(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_MMZ_SWITCH, OB8(o, OBJ_render) | 4, 1, 16, 8);
        SET8(o, OBJ_x_radius, 16);
        SET8(o, OBJ_y_radius, 8);
    }
    SET8(o, SW_PRV, OB8(o, SW_NOW));
    o_solid(o, 16 + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
    uint8_t *f = &s_switch[OB8(o, OBJ_subtype) & 31];
    int on = cd_stood_on(o);
    SET8(o, SW_NOW, on ? 0xFF : 0);
    if (on) *f |= 0xC0; else *f &= (uint8_t)~0x80;
    if (!OB8(o, SW_PRV) && on) {                              /* (pressed) */
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(0xBF);        /* (act_move: pressed, on screen) */
        *f ^= 0x20;
        for (int n = 0; n < 2; n++)                           /* (its rider down with it) */
            if (OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))
                SET16(o_player(n), OBJ_y_pos, OB16(o_player(n), OBJ_y_pos) + 8);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) - 4);
    } else if (OB8(o, SW_PRV) && !on) {                       /* (let go: CD moves the player up 8 too) */
        for (int n = 0; n < 2; n++)
            if (OB8(o, SW_RIDE) & (n ? ST_P2_STAND : ST_P1_STAND))
                SET16(o_player(n), OBJ_y_pos, OB16(o_player(n), OBJ_y_pos) - 8);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) + 4);
    }
    SET8(o, SW_RIDE, OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND));
    draw_despawn(o);
}

/* ---- $20 pipe-top hatch: two rims, solid on top, and between them a hatch
 * that opens while its switch is held (until player 1 is $100 / $A0 px away)
 * or for player 1 coming up the pipe (it shuts behind him); placed once the
 * player is within $140 px ------------------------------------------------- */
#define PP_X0           0x30               /* oVar2A: its place (the rims') */
#define PP_TIME         0x30               /* oVar2A: the hatch's count (word) */
#define PP_PARENT       0x3C               /* oVar3E */
#define PP_HATCH        0x3F               /* oVar29: the hatch */
static void pipe_setup(uint32_t o, uint32_t a)
{
    SET8(a, OBJ_routine, 6);
    o_init(a, SCDA_MMZ_PIPE, OB8(a, OBJ_render) | 4, 3, 8, 4);
    SET8(a, OBJ_x_radius, 8);
    SET8(a, OBJ_y_radius, 4);
    SET16(a, OBJ_y_pos, OB16(o, OBJ_y_pos));
}

static int pipe_box(uint32_t o, uint32_t p, int dx, int w, int dy, int h)
{
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - dy) >= (uint16_t)h) return 0;
    return (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) - dx) < (uint16_t)w;
}

static int pipe_rising(uint32_t o, uint32_t p, int dx, int w, int dy, int h)
{
    return OBS16(p, OBJ_y_vel) < 0 && pipe_box(o, p, dx, w, dy, h);
}

static void pipe_hatch(uint32_t o)
{
    uint32_t p = RAM_Player_1, par = 0xFF0000u | OB16(o, PP_PARENT);
    if (r32(par) != CD_CODE(0x20)) { o_delete(o); return; }
    uint8_t *f = &s_switch[OB8(o, OBJ_subtype) & 31];
    uint32_t anim = o_asset_anim(SCDA_MMZ_PIPE);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, SCDA_MMZ_PIPE, OB8(o, OBJ_render) | 4, 4, 16, 4);
        SET8(o, OBJ_x_radius, 16);
        SET8(o, OBJ_y_radius, 4);
        /* fall through */
    case 2:                                                   /* shut */
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, 1);
        *f &= (uint8_t)~0x40;
        /* fall through */
    case 4:
        o_platform(o, 16 + 10, 4, OB16(o, OBJ_x_pos));
        if (*f & 0x80) {                                      /* (its switch held) */
            SET8(o, OBJ_routine, 6);
            SET16(o, PP_TIME, 5);
        } else if (pipe_rising(o, p, -8, 0x10, -3, 0x7B)) {  /* (him, coming up) */
            cd_get_off(o);
            SET8(o, OBJ_routine, 0x0A);
            SET16(o, OBJ_frame, 5);                           /* (the disc: a word, so frame 0) */
        }
        break;
    case 6:                                                   /* opening */
        SET16(o, PP_TIME, OB16(o, PP_TIME) - 1);
        if (OBS16(o, PP_TIME) < 0) SET8(o, OBJ_routine, 8);
        o_animate(o, anim);
        break;
    case 8:                                                   /* open till he is away */
        if (!pipe_box(o, p, -0x100, 0x200, -0xA0, 0x140)) SET8(o, OBJ_routine, 2);
        break;
    case 0x0A:                                                /* open: shuts once he is through */
        if (pipe_rising(o, p, -8, 0x10, -0x78, 0x74)) {
            SET8(o, OBJ_routine, 0x0C);
            SET8(o, OBJ_anim, 1);
            SET16(o, PP_TIME, 7);
        }
        break;
    default:                                                  /* shutting */
        SET16(o, PP_TIME, OB16(o, PP_TIME) - 1);
        if (OBS16(o, PP_TIME) < 0) {
            SET8(o, OBJ_routine, 2);
            SET8(o, OBJ_anim, 0);
            SET8(o, OBJ_frame, 1);
            break;
        }
        o_animate(o, anim);
        break;
    }
    if (OB8(o, OBJ_routine) != 4) cd_get_off(o);              /* (open: nothing to stand on) */
    o_draw(o);
}

static void pipe(uint32_t o)
{
    if (OB8(o, PP_HATCH)) { pipe_hatch(o); return; }
    int rim = (int8_t)OB8(o, OBJ_subtype) < 0;                /* (the right rim) */
    if (rim && r32(0xFF0000u | OB16(o, PP_PARENT)) != CD_CODE(0x20)) { o_delete(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET16(o, PP_X0, OB16(o, OBJ_x_pos));
        pipe_setup(o, o);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 5);
        /* fall through */
    case 2:
        if ((uint16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x140) < 0x280) {
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_frame, 0);
        }
        break;
    case 4: {
        uint32_t a = o_alloc();
        if (!a) { o_delete(o); return; }
        SET32(a, OBJ_code, CD_CODE(0x20));
        SET16(a, PP_PARENT, (uint16_t)o);
        SET8(a, OBJ_subtype, 0xFF);
        SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x30);
        SET8(a, OBJ_render, OB8(a, OBJ_render) ^ 1);
        SET8(a, OBJ_status, OB8(a, OBJ_status) ^ 1);
        pipe_setup(o, o);
        pipe_setup(o, a);
        if (!(a = o_alloc())) { o_delete(o); return; }
        SET32(a, OBJ_code, CD_CODE(0x20));
        SET8(a, OBJ_subtype, OB8(o, OBJ_subtype));
        SET8(a, PP_HATCH, 0xFF);
        SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x18);
        SET16(a, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET16(a, PP_PARENT, (uint16_t)o);
    }   /* fall through */
    default:                                                  /* a rim */
        o_platform(o, 8 + 10, 4, OB16(o, OBJ_x_pos));
        break;
    }
    if (rim) { o_draw(o); return; }
    if (cd_despawn(o, OB16(o, PP_X0))) return;
    o_draw(o);
}

/* ---- $09 invisible solid block (the mini passages' walls) ---------------- */
static void chibi_block(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_width_px, 0x10);
        SET8(o, OBJ_height_px, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
    }
    for (int n = 0; n < 2; n++)                               /* (CD draws its empty frame: solid on screen) */
        o_solid_1p(o, n, 0x10 + 10, 0x10, 0x11, OB16(o, OBJ_x_pos), 1);
    cd_despawn(o, OB16(o, OBJ_x_pos));
}

/* ---- $22 gear-loop governor (unseen): player 1 on the ground within $48
 * px of it ($30: subtype low nibble set) is held to $400-$F00 (subtype bit 7:
 * leftwards) and sticks to the loop; walking, not pushing ---------------- */
#define GR_Y0           0x30               /* oVar30 */
#define GR_X0           0x32               /* oVar32 */
#define GR_DIR          0x36               /* oVar36 */
#define GR_RANGE        0x38               /* oVar38 */
#define GR_ON           0x3A               /* oVar3A */
static void governor(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, 4);
        SET8(o, OBJ_width_px, 8);
        SET16(o, GR_X0, OB16(o, OBJ_x_pos));
        SET16(o, GR_Y0, OB16(o, OBJ_y_pos));
        SET8(o, GR_RANGE, OB8(o, OBJ_subtype) & 0x0F ? 0x30 : 0x48);
        SET16(o, GR_DIR, (uint16_t)((int16_t)(int8_t)(OB8(o, OBJ_subtype) & 0xF0) << 3));
    }
    unsigned r = OB8(o, GR_RANGE);
    int in = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, GR_X0) + r) < 2 * r &&
             (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, GR_Y0) + r) < 2 * r;
    if (!in) {
        if (OB8(o, GR_ON)) { SET8(p, P_STICK, 0); SET8(o, GR_ON, 0); }
    } else if (OB8(p, OBJ_status) & 2) {
        SET8(o, GR_ON, 0);
    } else {
        if (!OB8(o, GR_ON)) {
            SET8(o, GR_ON, 1);
            if (!(OB8(p, OBJ_status) & 4)) SET8(p, P_ANIM, 0);
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~0x20);
            SET8(p, P_PREVANIM, 1);
            SET8(p, P_STICK, 1);
        }
        int16_t g = OBS16(p, OBJ_ground_vel);
        if (OBS16(o, GR_DIR) < 0) {
            if (g > -0x400) g = -0x400;
            else if (g < -0xF00) g = -0xF00;
        } else {
            if (g < 0x400) g = 0x400;
            else if (g > 0xF00) g = 0xF00;
        }
        SET16(p, OBJ_ground_vel, (uint16_t)g);
    }
    cd_despawn(o, OB16(o, OBJ_x_pos));
}

/* ---- $07 tube transport (act 2): carries player 1 along a route of points
 * (the act's own table, absolute) at $1000, behind the level's front; subtype
 * $80 is a junction, where he waits for a direction and goes 16 px that way -- */
#define TP_STEPS        0x30               /* oVar2E: frames (high byte) */
#define TP_X            0x36               /* oVar36 */
#define TP_Y            0x38               /* oVar38 */
#define TP_INDEX        0x3A               /* oVar3A */
#define TP_END          0x3B               /* oVar3B */
#define TP_DATA         0x3C               /* oVar3C */
#define FM_TUBE         0x91               /* (shootermove: it takes him; shootermove2: on along the route) */
static void tp_aim(uint32_t o, uint32_t p)                    /* (SetPlayerSpeed) */
{
    int16_t dx = (int16_t)(OB16(o, TP_X) - OB16(p, OBJ_x_pos));
    int16_t dy = (int16_t)(OB16(o, TP_Y) - OB16(p, OBJ_y_pos));
    int16_t d2 = OBS16(p, OBJ_ground_vel), d3 = d2;
    uint16_t ax = (uint16_t)dx, ay = (uint16_t)dy;
    if (dx < 0) { ax = (uint16_t)-dx; d2 = -d2; }
    if (dy < 0) { ay = (uint16_t)-dy; d3 = -d3; }
    if (ay >= ax) {
        int16_t d1 = d3 ? (int16_t)(((int32_t)dy << 16) / d3) : 0, d0 = 0;
        if (dx && d1) d0 = (int16_t)(((int32_t)dx << 16) / d1);
        SET16(p, OBJ_x_vel, (uint16_t)d0);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
        SET16(o, TP_STEPS, (uint16_t)(d1 < 0 ? -d1 : d1));
    } else {
        int16_t d0 = d2 ? (int16_t)(((int32_t)dx << 16) / d2) : 0, d1 = 0;
        if (dy && d0) d1 = (int16_t)(((int32_t)dy << 16) / d0);
        SET16(p, OBJ_y_vel, (uint16_t)d1);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
        SET16(o, TP_STEPS, (uint16_t)(d0 < 0 ? -d0 : d0));
    }
}

static void tp_catch(uint32_t o, uint32_t p)                  /* (ObjShooter's routine 2) */
{
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 16) >= 32 ||
        (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 16) >= 32 || OB8(p, P_OBJCTRL))
        return;
    if (OB8(p, P_ROUTINE) == 4) {                             /* (hurt: back to normal) */
        SET8(p, P_ROUTINE, 2);
        SET8(p, 0x34, 0x78);                                  /* (invulnerability_timer) */
    }
    SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
    SET8(p, P_OBJCTRL, 0x81);
    SET16(p, P_ARTTILE, OB16(p, P_ARTTILE) & 0x7FFF);         /* (behind the front) */
    SET8(p, P_ANIM, 2);
    SET16(p, OBJ_ground_vel, 0x1000);                         /* (subtypes under $80: a table, all at most $1000) */
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~0x20);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | 2);
    SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
    o_sfx_cd(FM_TUBE);
}

static void tp_step(uint32_t o, uint32_t p)                   /* (ObjShooter's routine 6) */
{
    int8_t t = (int8_t)(OB8(o, TP_STEPS) - 1);
    SET8(o, TP_STEPS, (uint8_t)t);
    if (t >= 0) {
        SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_x_vel) << 8));
        SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_y_vel) << 8));
        return;
    }
    SET16(p, OBJ_x_pos, OB16(o, TP_X));
    SET16(p, OBJ_y_pos, OB16(o, TP_Y));
    uint8_t i = (uint8_t)(OB8(o, TP_INDEX) + 4);
    if (i >= OB8(o, TP_END)) {                                /* out, with the last leg's speed */
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
        SET8(o, OBJ_routine, 0);
        SET8(p, P_OBJCTRL, 0);
        return;
    }
    SET8(o, TP_INDEX, i);
    uint32_t a2 = r32(o + TP_DATA);
    SET16(o, TP_X, tri_rd16(a2 + i));
    SET16(o, TP_Y, tri_rd16(a2 + i + 2u));
    tp_aim(o, p);
}

static void tube_junction(uint32_t o, uint32_t p)             /* (subtype $80) */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_render, 4);
        SET8(o, TP_INDEX, 0);
        SET8(o, TP_END, 4);
        SET16(o, TP_X, OB16(o, OBJ_x_pos));
        SET16(o, TP_Y, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_routine, 2);
        break;
    case 2:
        tp_catch(o, p);
        break;
    case 4: {                                                 /* (a direction pressed: that way) */
        uint8_t tap = r8(RAM_Ctrl_1 + 1);
        if (tap & 1) SET16(o, TP_Y, OB16(o, TP_Y) - 0x10);
        else if (tap & 2) SET16(o, TP_Y, OB16(o, TP_Y) + 0x10);
        else if (tap & 4) SET16(o, TP_X, OB16(o, TP_X) - 0x10);
        else if (tap & 8) SET16(o, TP_X, OB16(o, TP_X) + 0x10);
        else break;
        SET8(o, OBJ_routine, 6);
        SET16(p, OBJ_ground_vel, 0x1000);
        tp_aim(o, p);
        break;
    }
    default:
        tp_step(o, p);
        break;
    }
    cd_despawn(o, OB16(o, OBJ_x_pos));
}

static void tube(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (OB8(p, P_ROUTINE) >= 6) return;                       /* (dead; CD: or giving up, bored) */
    if (OB8(o, OBJ_subtype) == 0x80) { tube_junction(o, p); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        uint32_t tab = o_asset_anim(SCDA_TUNNELPATHS);
        if (!tab) { o_delete(o); return; }
        SET8(o, OBJ_render, 4);
        SET8(o, OBJ_routine, 2);
        uint32_t a2 = tab + tri_rd16(tab + (OB8(o, OBJ_subtype) & 0x7Fu) * 2u);
        SET8(o, TP_INDEX, tri_rd8(a2));
        SET8(o, TP_END, tri_rd8(a2 + 1));
        SET32(o, TP_DATA, a2 + 2);
        SET16(o, TP_X, tri_rd16(a2 + 2));
        SET16(o, TP_Y, tri_rd16(a2 + 4));
    }   /* fall through */
    case 2:
        tp_catch(o, p);
        break;
    case 4:
        tp_aim(o, p);
        SET8(o, OBJ_routine, 6);
        o_sfx_cd(FM_TUBE);
        return;
    default:
        tp_step(o, p);
        return;
    }
    if (OB8(o, OBJ_routine) < 4) cd_despawn(o, OB16(o, OBJ_x_pos));   /* (not while carrying) */
}

/* ---- $1D shrink ray (subtype 0) / grow ray (subtype 1): every 30 frames a
 * beam comes down from the emitter at 4 px a frame, growing 16 px at a time
 * (anims 0-7), splashes on the floor and draws up after itself; it hits only
 * a full-size player (subtype 0) or a small one (subtype 1): thrown off as if
 * hurt (no rings lost), and the other size ---------------------------------- */
#define BM_TIME         0x30               /* oVar2A: to the next beam; the splash's end */
#define BM_CHILD        0x32               /* oVar2C: the beam's splash */
static void ray_hit(uint32_t o)                               /* (player 1) */
{
    static const int8_t top[8] = { -12, -28, -44, -60, -76, -92, -108, -124 };
    uint32_t p = RAM_Player_1;
    if (OB8(o, OBJ_subtype) ? !s_mini : s_mini) return;
    if (r16(RAM_Debug_placement_mode) || OB8(p, P_ROUTINE) >= 6) return;   /* (CD: or giving up, bored; dead: ours) */
    int d1 = (int8_t)OB8(p, P_XRAD) + 4;
    int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos) + d1);
    if (d0 < 0 || (uint16_t)d0 >= (uint16_t)(d1 * 2)) return;
    int r = (int8_t)OB8(p, P_XRAD);                           /* (the disc: the word at $16, its low byte) */
    uint16_t py = OB16(p, OBJ_y_pos);
    if ((uint16_t)(OB16(o, OBJ_y_pos) + top[OB8(o, OBJ_anim) & 7]) >= (uint16_t)(py + r)) return;
    if ((uint16_t)(py - r) >= (uint16_t)(OB16(o, OBJ_y_pos) + 4)) return;
    SET8(p, P_ROUTINE, 4);                                    /* (thrown off, as if hurt) */
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 2);
    SET16(p, OBJ_y_vel, (uint16_t)-0x400);
    SET16(p, OBJ_x_vel, OB16(p, OBJ_x_pos) < OB16(o, OBJ_x_pos) ? (uint16_t)-0x200 : 0x200);
    SET16(p, OBJ_ground_vel, 0);
    SET8(p, P_ANIM, ANIM_HURT);                               /* (CD's grow / shrink pictures, then hurt) */
    s_mini ^= 1;
    if (s_mini) { SET8(p, P_YRAD, 10); SET8(p, P_XRAD, 5); }
    else {
        SET8(p, P_YRAD, s_mini_on ? s_def_y : OB8(p, P_DEFYRAD));
        SET8(p, P_XRAD, s_mini_on ? s_def_x : OB8(p, P_DEFXRAD));
    }
    mini_frame(p);
}

static void ray(uint32_t o)
{
    uint32_t anim = o_asset_anim(OB8(o, OBJ_subtype) ? SCDA_MMZ_BEAM2 : SCDA_MMZ_BEAM);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, OB8(o, OBJ_subtype) ? SCDA_MMZ_BEAM2 : SCDA_MMZ_BEAM, OB8(o, OBJ_render) | 4, 0, 8, 8);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype) ? 2 : 1);
        SET8(o, BM_TIME, 1);
        /* fall through */
    case 2: {                                                 /* the emitter */
        SET8(o, BM_TIME, OB8(o, BM_TIME) - 1);
        if (OB8(o, BM_TIME)) break;
        uint32_t a = o_alloc_after(o);
        if (!a) break;
        SET32(a, OBJ_code, CD_CODE(0x1D));
        SET8(a, OBJ_routine, 4);
        SET16(a, OBJ_art_tile, OB16(o, OBJ_art_tile));
        SET32(a, OBJ_mappings, r32(o + OBJ_mappings));
        SET8(a, OBJ_render, OB8(a, OBJ_render) | 4);
        SET16(a, OBJ_priority, 2 << 7);
        SET8(a, OBJ_width_px, 8);
        SET8(a, OBJ_height_px, 0x80);                         /* (up to 128 px above its foot) */
        SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a, OBJ_y_pos, OB16(o, OBJ_y_pos) & 0xFFF0);
        SET8(a, OBJ_subtype, OB8(o, OBJ_subtype));
        SET8(a, OBJ_y_radius, 4);
        SET8(o, BM_TIME, 0x1E);
        if (OB8(o, OBJ_render) & 0x80)
            o_sfx_cd(OB8(o, OBJ_subtype) ? 0xBC : 0xC5);      /* (beam_mast: the grow ray's / the shrink ray's beam) */
        break;
    }
    case 4: {                                                 /* coming down */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
        if (!(OB16(o, OBJ_y_pos) & 15) && OB8(o, OBJ_anim) < 7) SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
        o_animate(o, anim);
        if (o_floor_dist(o) < 0) {
            SET8(o, OBJ_routine, 6);
            uint32_t a = o_alloc_after(o);
            if (a) {                                          /* (its splash) */
                SET32(a, OBJ_code, CD_CODE(0x1D));
                SET16(a, OBJ_art_tile, OB16(o, OBJ_art_tile));
                SET32(a, OBJ_mappings, r32(o + OBJ_mappings));
                SET8(a, OBJ_render, OB8(a, OBJ_render) | 4);
                SET8(a, OBJ_width_px, 8);
                SET8(a, OBJ_height_px, 8);
                SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a, OBJ_subtype, OB8(o, OBJ_subtype));
                SET8(a, OBJ_routine, 8);
                SET8(a, OBJ_anim, 8);
                SET16(o, BM_CHILD, (uint16_t)a);
            }
        }
        ray_hit(o);
        break;
    }
    case 6: {                                                 /* drawing up after itself */
        uint16_t y = (uint16_t)(OB16(o, OBJ_y_pos) + 4);
        if (!(y & 15)) {
            y -= 16;
            if (!OB8(o, OBJ_anim)) {
                uint32_t c = OB16(o, BM_CHILD) ? 0xFF0000u | OB16(o, BM_CHILD) : 0;
                if (c && r32(c) == CD_CODE(0x1D) && OB8(c, OBJ_routine) == 8) SET8(c, BM_TIME, 1);
                o_delete(o);
                return;
            }
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) - 1);
        }
        SET16(o, OBJ_y_pos, y);
        o_animate(o, anim);
        ray_hit(o);
        break;
    }
    default:                                                  /* the splash */
        if (OB8(o, BM_TIME)) { o_delete(o); return; }
        o_animate(o, anim);
        break;
    }
    draw_despawn(o);
}

/* ---- $3E rotating cylinder (act 2): the base (solid on top), its top 176
 * px up, three bars round it. Player 1 standing at its middle (0-7 px left of
 * it) is held while he turns into or out of the screen (CD's anims $33/$34,
 * 33 frames; the bars and the top turn with him), then is on the other layer,
 * facing the other way ---------------------------------------------------- */
#define KT_X0           0x30               /* oVar2A: the bars' middle */
#define KT_ANG          0x32               /* oVar2D */
#define KT_ANG2         0x33               /* oVar2E */
#define KT_BUSY         0x34               /* oVar2C bit 0 */
#define KT_TIME         0x35               /* (the turn's frames: CD's anim end) */
static void kt_bar_place(uint32_t o, int radius)
{
    int16_t s, c;
    o_sincos(OB8(o, KT_ANG), &s, &c);
    SET16(o, OBJ_x_pos, OB16(o, KT_X0) + (int16_t)((c * radius) >> 8));
}

static uint32_t kt_part(uint32_t o, uint32_t after)
{
    uint32_t a = o_alloc_after(after);
    if (!a) return 0;
    SET32(a, OBJ_code, CD_CODE(0x3E));
    o_init(a, SCDA_MMZ_KAITEN, 4, 1, 0x28, 8);
    SET8(a, OBJ_y_radius, 8);
    SET16(a, OBJ_x_pos, OB16(o, OBJ_x_pos));
    return a;
}

static void kaiten(uint32_t o)
{
    static const uint8_t angles[3] = { 0x40, 0x95, 0xEA };
    uint32_t p = RAM_Player_1, anim = o_asset_anim(SCDA_MMZ_KAITEN);
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* (its four parts: all, or none yet) */
        uint32_t made[4] = { 0 }, a = o;
        int k;
        for (k = 0; k < 4 && (a = kt_part(o, a)) != 0; k++) made[k] = a;
        if (k < 4) {
            for (int j = 0; j < k; j++) o_delete(made[j]);
            o_delete_respawnable(o);
            return;
        }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_MMZ_KAITEN, 4, 1, 0x28, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(made[0], OBJ_y_pos, OB16(o, OBJ_y_pos) - 0xB0);  /* (the top) */
        SET8(made[0], OBJ_subtype, 1);
        SET8(made[0], OBJ_routine, 2);
        for (k = 1; k < 4; k++) {                             /* (the bars) */
            uint32_t b = made[k];
            SET8(b, OBJ_width_px, 8);
            SET8(b, OBJ_height_px, 0x50);
            SET8(b, OBJ_y_radius, 0x50);
            SET8(b, KT_ANG, angles[3 - k]);
            SET8(b, OBJ_frame, 2);
            SET8(b, OBJ_routine, 8);
            SET16(b, KT_X0, OB16(o, OBJ_x_pos));
            SET16(b, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x58);
            kt_bar_place(b, 0x20);
        }
        return;
    }
    case 2:
        if (OB8(o, OBJ_subtype) == 1) {                       /* the top: turns with him */
            if (s_turn) o_animate(o, anim);
            break;
        }
        o_platform(o, 0x28 + 10, 8, OB16(o, OBJ_x_pos));
        if (!(OB8(o, OBJ_status) & ST_P1_STAND) ||
            (uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)) >= 8) {   /* (the disc's test: 0-7 px left) */
            SET8(o, KT_BUSY, 0);
            break;
        }
        if (OB8(o, KT_BUSY)) break;
        SET8(o, KT_BUSY, 1);
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
        SET8(p, P_ANIM, ANIM_TWIRL);
        SET8(o, KT_TIME, 0);
        s_turn = 1;
        SET8(o, OBJ_routine, 4);
        break;
    case 4:                                                   /* turning him */
        o_animate(o, anim);
        if (OB8(p, P_ROUTINE) != 2) {                         /* (not his to turn any more) */
            s_turn = 0;
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
            SET8(o, OBJ_routine, 2);
            break;
        }
        SET8(o, KT_TIME, OB8(o, KT_TIME) + 1);
        if (OB8(o, KT_TIME) < 33) {
            SET8(p, P_ANIM, ANIM_TWIRL);
            o_platform(o, 0x28 + 10, 8, OB16(o, OBJ_x_pos));
            break;
        }
        SET8(p, P_ANIM, ANIM_WAIT);                           /* (CD's anim ends in its wait) */
        s_turn = 0;
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
        o_platform(o, 0x28 + 10, 8, OB16(o, OBJ_x_pos));
        s_back ^= 1;
        SET8(p, OBJ_render, OB8(p, OBJ_render) ^ 1);
        SET8(p, OBJ_status, OB8(p, OBJ_status) ^ 1);
        SET16(p, P_ARTTILE, s_back ? OB16(p, P_ARTTILE) & 0x7FFF : OB16(p, P_ARTTILE) | 0x8000);
        SET8(o, OBJ_routine, 2);
        break;
    case 8:                                                   /* a bar, still */
        if (!s_turn) break;
        SET8(o, OBJ_routine, 10);
        SET8(o, KT_ANG2, OB8(o, KT_ANG) + 0x80);
        break;
    default: {                                                /* a bar, turning */
        if (!s_turn) {
            SET8(o, OBJ_routine, 8);
            SET8(o, KT_ANG, OB8(o, KT_ANG2));
        } else {
            int d = 4, left = OB8(p, OBJ_status) & 1;
            if (left ? !s_back : s_back) d = -4;
            SET8(o, KT_ANG, OB8(o, KT_ANG) + d);
        }
        kt_bar_place(o, 0x18);
        SET16(o, OBJ_priority, (OB8(o, KT_ANG) & 0x80 ? 1 : 4) << 7);   /* (behind on its far side) */
        break;
    }
    }
    if (OB8(o, OBJ_routine) >= 8) {
        if (cd_despawn(o, OB16(o, KT_X0))) return;
    } else if (OB8(o, OBJ_subtype)) {
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    } else if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        if (OB8(o, OBJ_routine) == 4) { s_turn = 0; SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1); }
        return;
    }
    o_draw(o);
}

/* ---- $32 animals: as Palmtree Panic's (cd_animal) without the holograms:
 * free in a good future only here (there is no projector in Metallic
 * Madness); the bird is not high priority, the hopper's y radius is 7 ------- */
#define AN_X0           0x30               /* oVar2A */
#define AN_Y0           0x32               /* oVar2C (flying) */
#define AN_VX           0x34               /* oVar2C (ground, long) */
#define AN_ANG          0x38               /* oVar2E */
#define AN_STEP         0x39               /* oVar2F */
#define AN_VY           0x3A               /* oVar30 (ground, long) */
int scd_proj_destroyed(void);
int scd_good_future(void);
static void an_turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

static void animal(uint32_t o)
{
    int flying = !(OB8(o, OBJ_subtype) & 0x7F);
    if (!(OB8(o, OBJ_subtype) & 0x80)) {                      /* CheckAnimalPrescence */
        if (cd_period() == TRI_CD_PAST || cd_period() == TRI_CD_PRESENT) {
            if (!scd_proj_destroyed()) {
                if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
                return;
            }
        } else if (!scd_good_future()) { o_delete(o); return; }
    }
    int asset = flying ? SCDA_MMZ_ANIMALFLY : SCDA_MMZ_ANIMALGROUND;
    if (!OB8(o, OBJ_routine)) {
        if (!o_asset_maps(asset)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, asset, 4, 4, 8, 8);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_y_radius, flying ? 8 : 7);
        SET16(o, AN_X0, OB16(o, OBJ_x_pos));
        if (flying) {
            SET16(o, AN_Y0, OB16(o, OBJ_y_pos));
            an_turn(o);
            SET8(o, AN_ANG, 1);
            SET8(o, AN_STEP, 1);
        } else {
            SET32(o, AN_VX, 0x10000);
            SET32(o, AN_VY, (uint32_t)-0x40000);
        }
        return;                                               /* (the first frame: unseen) */
    }
    if (flying) {
        int16_t s, c;
        o_sincos(OB8(o, AN_ANG), &s, &c);
        SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> 1));
        SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> 1));
        uint8_t d0 = (uint8_t)(OB8(o, AN_ANG) + OB8(o, AN_STEP));
        if ((uint8_t)(d0 - 1) >= 0x7F) {                      /* (round the loop: turn back) */
            d0 = OB8(o, AN_ANG);
            SET8(o, AN_STEP, (uint8_t)-(int8_t)OB8(o, AN_STEP));
            an_turn(o);
        }
        SET8(o, AN_ANG, d0);
        o_animate(o, o_asset_anim(asset));
        o_draw(o);
        if (o_out_of_range(o, OB16(o, AN_X0))) o_delete_respawnable(o);
        return;
    }
    cd_move(o, (int32_t)r32(o + AN_VX), (int32_t)r32(o + AN_VY));       /* hopping */
    SET32(o, AN_VY, r32(o + AN_VY) + 0x2000);
    SET8(o, OBJ_frame, (int32_t)r32(o + AN_VY) < 0 ? 0 : 1);
    int16_t d1 = o_floor_dist(o);
    if (d1 < 0) {
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET32(o, AN_VY, (uint32_t)-0x40000);
        SET32(o, AN_VX, (uint32_t)-(int32_t)r32(o + AN_VX));
        an_turn(o);
    }
    o_draw(o);
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

/* ====================================================================== */
void cd_generator(uint32_t o);                                /* scd_ppz.c: the same code (its touch $E2, same box) */
void scd_mmz_badnik_register(TriObjFn *t, int base);          /* scd_mmz_badnik.c */
void scd_mmz_plat_register(TriObjFn *t, int base);            /* scd_mmz_plat.c */
void scd_mmz_trap_register(TriObjFn *t, int base);            /* scd_mmz_trap.c */
void scd_mmz_gim_register(TriObjFn *t, int base);             /* scd_mmz_gim.c */

void scd_mmz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x07] = tube;
    t[base + 0x09] = chibi_block;
    t[base + 0x1D] = ray;
    t[base + 0x20] = pipe;
    t[base + 0x21] = mmz_switch;
    t[base + 0x22] = governor;
    t[base + 0x32] = animal;
    t[base + 0x3E] = kaiten;
    t[base + 0x43] = cd_generator;
    scd_mmz_badnik_register(t, base);
    scd_mmz_plat_register(t, base);
    scd_mmz_trap_register(t, base);
    scd_mmz_gim_register(t, base);
}

/* ---- the hook ------------------------------------------------------------- */
static uint8_t s_round_act = 0xFF, s_period, s_post;

static void level_entry(void)
{
    memset(s_switch, 0, sizeof s_switch);
    s_back = 0;
    s_turn = 0;
    s_mini_on = 0;
    if (!mmz()) { s_mini = s_mini_saved = 0; s_round_act = 0xFF; return; }
    uint8_t ra = (uint8_t)(MMZ_ROUND << 4 | tri_act());
    int period = cd_period();
    if (ra != s_round_act || !r8(RAM_Last_star_post_hit))
        s_mini = s_mini_saved = 0;                            /* (the act afresh) */
    else if (period != s_period)
        s_mini_saved = s_mini;                                /* (a time warp: warpMiniSonic) */
    else
        s_mini = s_mini_saved;                                /* (back at a checkpoint: savedMiniSonic) */
    s_round_act = ra;
    s_period = (uint8_t)period;
    s_post = r8(RAM_Last_star_post_hit);
}

void scd_mmz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {
        extern void scd_mmz3_level_entry(void);
        scd_mmz3_level_entry();                       /* (scd_mmz_boss.c: act 3's state) */
    }
    if (pc == TH_LEVEL_ENTRY) { level_entry(); return; }
    if (!tri_dir() || !mmz()) return;
    if (pc == TH_LVL_PRE_SPRITES) {                           /* (before they move: the layer's collision) */
        for (int n = 0; n < 2; n++)
            if (o_player_active(o_player(n))) layer_path(o_player(n));
        return;
    }
    if (pc != TH_LVL_POST_SPRITES) return;
    uint32_t p1 = RAM_Player_1;
    if (r8(RAM_Last_star_post_hit) != s_post) {               /* (a checkpoint: savedMiniSonic) */
        s_post = r8(RAM_Last_star_post_hit);
        s_mini_saved = s_mini;
    }
    mini_frame(p1);
    if (o_player_active(p1) && (OB8(p1, P_OBJCTRL) & 1)) scd_time_hold();
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || (OB8(p, P_OBJCTRL) & 1) || r16(RAM_Debug_placement_mode)) continue;
        if (OB8(p, P_ROUTINE) == 2 || OB8(p, P_ROUTINE) == 4) layer_priority(p);
        if (tri_act() == 1 && playing(p)) conveyor(p);
    }
}
