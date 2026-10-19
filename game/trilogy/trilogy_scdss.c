/*
 * trilogy_scdss.c — Sonic CD's special stage (the UFO stages), transcribed
 * from the Mega-CD programs (dobby: Special Stage/Main.asm, Sub.asm and
 * Objects/), run off S3K's game loop as a native scene like the S2 half-pipe.
 *
 * On the Mega-CD the Sub CPU runs the stage: the objects (Sonic, the UFOs,
 * ...), the stamp map's animation and the graphics chip, which draws the
 * floor - the 4096 px stamp map seen in perspective - into a 256x96 picture
 * in Word RAM, line by line from a trace table. The Main CPU takes the
 * picture, the sprite list and Sonic's art from Word RAM once per update and
 * shows them; one update is three frames (20 per second), the picture double
 * buffered side by side in plane A. Here both halves run in turn in one
 * frame function; the graphics chip's work (stamp lookup, rotation, flip,
 * the image's column-major tiles) is done in C.
 *
 * Everything the stage uses is the disc's (scdss.bin, tools/trilogy/
 * scd_special.py): both programs as they are, so objects read their
 * pointer-linked data (animations, sprites, UFO paths) at the Sub CPU's own
 * addresses, and the stamps, stamp maps, art and tilemaps decoded.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "genesis_machine.h"
#include "video/genesis_vdp.h"
#include "scd.h"
#include "scd_pack.h"
extern const ScdPack *tri_scd_pack(void);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- scdss.bin -------------------------------------------------------------- */
enum { IT_SUB, IT_MAIN, IT_LABELS, IT_BLOCK, IT_STAMPS = 11, IT_STAMPMAP = 19, IT_ART = 27, IT_BGART = 28,
       IT_RESART = 36, IT_MAPS = 37, IT_COMMONMAPS = 45, IT_COUNT };
enum { SSL_STAMPTYPES, SSL_STAMPANIMS, SSL_UFOPATHS, SSL_TIMEUFOPATH, SSL_STARTPOS, SSL_SONICART,
       SSL_MAP_SONIC, SSL_MAP_SHADOW, SSL_MAP_UFORINGS, SSL_MAP_UFOSHOES, SSL_MAP_SPLASH, SSL_MAP_ITEM,
       SSL_MAP_DUST, SSL_MAP_EXPLOSION, SSL_MAP_PRESSSTART, SSL_MAP_TIMESTONE, SSL_MAP_TTLTEXT, SSL_MAP_TTLBAR,
       SSL_PAL_STAGE, SSL_PAL_RESULTS, SSL_TILEMAPS, SSL_STAGEPAL };
static const uint8_t *s_blob;


static const uint8_t *item(int i, uint32_t *len)
{
    const uint8_t *t = s_blob + 8 + i * 8;
    uint32_t o = (uint32_t)t[0] << 24 | t[1] << 16 | t[2] << 8 | t[3];
    if (len) *len = (uint32_t)t[4] << 24 | t[5] << 16 | t[6] << 8 | t[7];
    return s_blob + o;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t label(int i) { return be32(item(IT_LABELS, NULL) + i * 4); }
int tri_scdss_available(void);
const uint8_t *tri_scdss_item(int i, uint32_t *len)   /* (trilogy_scdwarp.c: the time warp's items) */
{
    if (!tri_scdss_available() || i < 0 || i >= be16(s_blob + 6)) return NULL;
    return item(i, len);
}

int tri_scdss_available(void)
{
    const ScdPack *p=tri_scd_pack();s_blob=p->scenes;
    return tri_scd_available() && s_blob!=NULL;
}

/* ---- the Sub CPU: its program, its variables, Word RAM ---------------------- */
#define SUB_BASE    0x10000u
#define VARSSTART   0x3C000u           /* (objects at their own addresses: pointers between them work) */
#define OSIZE       0x80
#define NOBJ        48
static uint8_t s_objram[NOBJ * OSIZE];
enum {                                 /* object slots (Sub.asm's variables) */
    O_SONIC, O_SONICSHADOW, O_SPLASH, O_TTLBAR, O_TTLTEXT, O_TIMESTONE, O_SPARKLE1, O_SPARKLE2,
    O_DUST1, O_ITEM = O_DUST1 + 8, O_RING1, O_EXPL1 = O_RING1 + 7, O_UFO1 = O_EXPL1 + 8,
    O_TIMEUFO = O_UFO1 + 7, O_UFOSHADOW1, O_TIMEUFOSHADOW = O_UFOSHADOW1 + 7,
};
#define PRIO1       O_SPLASH
#define OBJ1COUNT   14
#define PRIO2       O_ITEM
#define OBJ2COUNT   24
#define PRIO3       O_UFOSHADOW1
#define OBJ3COUNT   8
#define OA(slot)    (VARSSTART + (uint32_t)(slot) * OSIZE)

static const uint8_t *sub_p(uint32_t a)
{
    uint32_t len;
    const uint8_t *p = item(IT_SUB, &len);
    a &= 0xFFFFFF;
    return a - SUB_BASE < len ? p + (a - SUB_BASE) : p;
}
static uint8_t sub_b(uint32_t a) { return *sub_p(a); }
static uint16_t sub_w(uint32_t a) { return (uint16_t)((sub_b(a)<<8)|sub_b(a+1)); }
static uint32_t sub_l(uint32_t a) { return ((uint32_t)sub_w(a)<<16)|sub_w(a+2); }

/* an object's fields (a = its Sub CPU address) */
static uint8_t *ob(uint32_t a) { return &s_objram[((a & 0xFFFFFF) - VARSSTART) % (NOBJ * OSIZE)]; }
static uint8_t B(uint32_t a, int f) { return ob(a)[f]; }
static void SB(uint32_t a, int f, int v) { ob(a)[f] = (uint8_t)v; }
static uint16_t W(uint32_t a, int f) { return be16(ob(a) + f); }
static int16_t WS(uint32_t a, int f) { return (int16_t)W(a, f); }
static void SW(uint32_t a, int f, int v) { ob(a)[f] = (uint8_t)(v >> 8); ob(a)[f + 1] = (uint8_t)v; }
static uint32_t L(uint32_t a, int f) { return be32(ob(a) + f); }
static int32_t LS(uint32_t a, int f) { return (int32_t)L(a, f); }
static void SL(uint32_t a, int f, uint32_t v) { SW(a, f, (int)(v >> 16)); SW(a, f + 2, (int)v); }
static int btst_o(uint32_t a, int f, int bit) { return (B(a, f) >> bit) & 1; }
static int bset_o(uint32_t a, int f, int bit) { int o = btst_o(a, f, bit); SB(a, f, B(a, f) | 1 << bit); return o; }
static int bclr_o(uint32_t a, int f, int bit) { int o = btst_o(a, f, bit); SB(a, f, B(a, f) & ~(1 << bit)); return o; }

enum {                                 /* object fields */
    oID = 0, oSpriteFlag = 1, oFlags = 2, oRoutine = 3, oTile = 4, oMap = 6, oAnim = 0xA, oAnimFrame = 0xB,
    oAnimTime = 0xC, oAnimTime2 = 0xD, oX = 0x1C, oY = 0x20, oZ = 0x24, oSprX = 0x28, oSprY = 0x2C,
    oXVel = 0x30, oYVel = 0x34, oTimer = 0x50,
    /* Sonic */
    oPlayerStampC = 0xE, oPlayerStampTL, oPlayerStampTR, oPlayerStampBR, oPlayerStampBL, oPlayerStampOri,
    oPlayerSpeed = 0x14, oPlayerBumpTime = 0x16, oPlayerTopSpeed = 0x18, oPlayerSprYVel = 0x40,
    oPlayerUFOCol = 0x4F, oPlayerPitch = 0x50, oPlayerYaw = 0x54, oPlayerTilt = 0x60, oPlayerTimer = 0x61,
    oPlayerShoes = 0x62,
    /* UFOs */
    oUFOPlayerCol = 0x4F, oUFOAnim = 0x52, oUFOExplodeDir = 0x53, oUFOShadow = 0x54, oUFOPathStart = 0x58,
    oUFOPath = 0x5C, oUFOPathTime = 0x60, oUFOItem = 0x62, oUFOUnk = 0x63, oUFODrawDelay = 0x64,
    /* shadows, items, dust */
    oShadowSprite = 0x52, oShadowParent = 0x54, oItemXVel = 0x3C, oItemYVel = 0x40, oItemSpawnType = 0x51,
    oItemType = 0x52, oDustXVel = 0x3C,
};

/* Sub CPU variables */
static struct {
    uint16_t camx, camy, camz, pitch, pitch_sin, pitch_cos, yaw, yaw_sin, yaw_cos, yaw_sin_n, yaw_cos_n, fov, center;
    int32_t ps_ys_fov, ps_yc_fov, pc_fov;
    int16_t ys_fov, yc_fov, center_x, center_y, pc_ys, pc_yc;
} G;                                   /* gfxVars */
static uint16_t s_zbuf[64 * 8 + 1];
static uint32_t s_cur_spr;             /* (offset into s_sprites) */
static uint8_t s_sprite_count;
static uint32_t s_stamp_types;
static uint32_t s_rng;
static uint8_t s_stage_over, s_got_stone, s_timer_speedup, s_timer_frames, s_stage_inactive, s_time_stopped;
static uint16_t s_ufo_ring_bonus, s_jump_timer;
static uint8_t s_ctrl_hold, s_ctrl_tap, s_lost_ring_dir;
static uint32_t s_hazard_data, s_fan_data;
static uint16_t s_hazard_count, s_fan_count, s_hazard_delay, s_hazard_frame, s_fan_frame;

/* the communication registers' share */
static uint8_t s_stage_id, s_time_stones, s_flags, s_ufo_count;
static uint16_t s_rings;
static uint32_t s_timer;               /* specStageTimer (seconds; time attack: 0:mm:ss:ff) */

/* Word RAM */
#define IMGW        256
#define IMGH        96
#define IMGHTILE    12
#define IMGLEN      (IMGW / 8 * IMGHTILE * 32)
static uint16_t s_stampmap[128 * 128];
static const uint8_t *s_stamps;        /* (Word RAM $200: stamp n at n * $80 - $200) */
static uint32_t s_stamps_len;
static int16_t s_trace[IMGH][4];
static uint8_t s_image[IMGLEN];        /* (the chip's picture) */
static uint8_t s_sub_sprites[0x280];
static uint8_t s_snd_queue[3], s_sub_scroll_flags;
static uint8_t s_sub_sonic_art[0x300];

/* ---- maths (Sub.asm) ---------------------------------------------------------- */
static const int16_t k_sine[128] = {
    0x000, 0x003, 0x006, 0x009, 0x00C, 0x00F, 0x012, 0x016, 0x019, 0x01C, 0x01F, 0x022, 0x025, 0x028, 0x02B, 0x02F,
    0x032, 0x035, 0x038, 0x03B, 0x03E, 0x041, 0x044, 0x047, 0x04A, 0x04D, 0x050, 0x053, 0x056, 0x059, 0x05C, 0x05F,
    0x062, 0x065, 0x068, 0x06A, 0x06D, 0x070, 0x073, 0x076, 0x079, 0x07B, 0x07E, 0x081, 0x084, 0x086, 0x089, 0x08C,
    0x08E, 0x091, 0x093, 0x096, 0x099, 0x09B, 0x09E, 0x0A0, 0x0A2, 0x0A5, 0x0A7, 0x0AA, 0x0AC, 0x0AE, 0x0B1, 0x0B3,
    0x0B5, 0x0B7, 0x0B9, 0x0BC, 0x0BE, 0x0C0, 0x0C2, 0x0C4, 0x0C6, 0x0C8, 0x0CA, 0x0CC, 0x0CE, 0x0D0, 0x0D1, 0x0D3,
    0x0D5, 0x0D7, 0x0D8, 0x0DA, 0x0DC, 0x0DD, 0x0DF, 0x0E0, 0x0E2, 0x0E3, 0x0E5, 0x0E6, 0x0E7, 0x0E9, 0x0EA, 0x0EB,
    0x0EC, 0x0EE, 0x0EF, 0x0F0, 0x0F1, 0x0F2, 0x0F3, 0x0F4, 0x0F5, 0x0F6, 0x0F7, 0x0F7, 0x0F8, 0x0F9, 0x0FA, 0x0FA,
    0x0FB, 0x0FB, 0x0FC, 0x0FC, 0x0FD, 0x0FD, 0x0FE, 0x0FE, 0x0FE, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x100,
};
static int16_t get_sine(int v)                                /* GetSine: a $200-step circle */
{
    v &= 0x1FF;
    int i = v & 0x80 ? ~v & 0x7F : v & 0x7F;
    return v & 0x100 ? (int16_t)-k_sine[i] : k_sine[i];
}
static int16_t get_cosine(int v) { return get_sine(v + 0x80); }

static uint32_t random32(void)                                /* Random */
{
    uint32_t d1 = s_rng ? s_rng : 0x2A6D365A, d0 = d1;
    d1 = (d1 << 2) + d0;
    d1 = (d1 << 3) + d0;
    uint16_t lo = (uint16_t)((uint16_t)d1 + (uint16_t)(d1 >> 16));   /* (move.w d1,d0; swap d1; add.w d1,d0; */
    s_rng = (uint32_t)lo << 16 | (d1 & 0xFFFF);                        /*  move.w d0,d1; swap d1) */
    return (d0 & 0xFFFF0000u) | lo;
}

/* GetAngle: the angle (0-$3F in its quadrant) between two points, and the quadrant */
static int get_angle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int *quad)
{
    int d2 = 0;
    uint16_t sx = (uint16_t)(x1 ^ x2);
    uint16_t dx = (uint16_t)(x1 - x2);
    int borrow = (uint16_t)x1 < (uint16_t)x2;
    if (borrow ? !(sx & 0x8000) : (sx & 0x8000)) { d2 |= 1; dx = (uint16_t)-dx; }
    int16_t dy = (int16_t)(y1 - y2);
    if (dy < 0 && y2 >= 0) { d2 |= 2; dy = (int16_t)-dy; }
    uint16_t a = dx, b = (uint16_t)dy;
    if (b >= a) { d2 |= 4; uint16_t t = a; a = b; b = t; }
    int r = 0;
    if (a) {                                                  /* (divu.w: an overflow leaves the dividend) */
        uint32_t n = (uint32_t)(int32_t)(int16_t)b << 6, q = n / a;
        r = (int)((q > 0xFFFF ? n : q) & 0xFF);
    }
    if (r >= 0x40) r = 0x3F;
    *quad = d2;
    return r;
}

static const uint16_t k_distance[65] = {
    0x0FF, 0x100, 0x100, 0x100, 0x100, 0x100, 0x101, 0x101, 0x101, 0x102, 0x103, 0x103, 0x104, 0x105, 0x106,
    0x106, 0x107, 0x108, 0x109, 0x10B, 0x10C, 0x10D, 0x10E, 0x110, 0x111, 0x112, 0x114, 0x115, 0x117, 0x119,
    0x11A, 0x11C, 0x11E, 0x120, 0x121, 0x123, 0x125, 0x127, 0x129, 0x12B, 0x12D, 0x130, 0x132, 0x134, 0x136,
    0x138, 0x13B, 0x13D, 0x140, 0x142, 0x144, 0x147, 0x149, 0x14C, 0x14E, 0x151, 0x154, 0x156, 0x159, 0x15C,
    0x15E, 0x161, 0x164, 0x167, 0x16A,
};
static uint32_t get_distance(int angle, int quad, int16_t x1, int16_t y1, int16_t x2, int16_t y2)   /* GetDistance */
{
    uint32_t d0 = k_distance[angle & 0x3F];
    int16_t d = quad & 4 ? (int16_t)(y2 - y1) : (int16_t)(x2 - x1);
    uint16_t m = (uint16_t)(d < 0 ? -d : d);
    return (d0 * m) >> 8;
}

/* ---- the graphics chip ------------------------------------------------------- */
static void get_gfx_sines(void)                               /* GetGfxSines */
{
    G.pitch_sin = (uint16_t)get_sine(G.pitch);
    G.pitch_cos = (uint16_t)get_cosine(G.pitch);
    G.yaw_sin = (uint16_t)get_sine(G.yaw);
    G.yaw_cos = (uint16_t)get_cosine(G.yaw);
    G.yaw_sin_n = (uint16_t)get_sine(G.yaw + 0x100);
    G.yaw_cos_n = (uint16_t)get_cosine(G.yaw + 0x100);
}

static int16_t divs(int32_t n, int16_t d)                     /* divs.w (quotient; on overflow the 68000 leaves n) */
{
    if (!d) return (int16_t)n;
    int32_t q = n / d;
    if (q > 32767 || q < -32768) return (int16_t)n;
    return (int16_t)q;
}

static void gen_trace_table(void)                             /* GenGfxTraceTbl */
{
    int16_t d0 = (int16_t)(G.camx << 3), d1 = (int16_t)(G.camy << 3), d2 = -3;
    int16_t psin = (int16_t)G.pitch_sin, pcos = (int16_t)G.pitch_cos, ysin = (int16_t)G.yaw_sin, ycos = (int16_t)G.yaw_cos;
    int16_t fov = (int16_t)G.fov;
    G.pc_ys = (int16_t)((pcos * ysin) >> 5);
    G.pc_yc = (int16_t)((pcos * ycos) >> 5);
    G.ps_ys_fov = (int32_t)((int16_t)(fov * psin) * ysin) >> 5;    /* (muls.w twice: the first product's word) */
    G.ps_yc_fov = (int32_t)((int16_t)(fov * psin) * ycos) >> 5;
    G.pc_fov = fov * pcos;
    int32_t a1 = (int32_t)(-128 * ycos) << 3, a2 = (int32_t)(-128 * ysin) << 3;
    int32_t a3 = (int32_t)(127 * ycos) << 3, a4 = (int32_t)(127 * ysin) << 3;
    int16_t cz = (int16_t)(G.fov + G.center);
    G.center_x = (int16_t)((cz * (int16_t)((psin * ysin) >> 5)) >> 8);
    G.center_y = (int16_t)((cz * (int16_t)((psin * ycos) >> 5)) >> 8);
    for (int l = 0; l < 3; l++) {                             /* (the first three lines blank) */
        s_trace[l][0] = 0;
        s_trace[l][1] = (int16_t)0xFFF8;
        s_trace[l][2] = 0;
        s_trace[l][3] = 0;
    }
    for (int l = 3; l < IMGH; l++, d2--) {
        int32_t z = (int32_t)(d2 * psin + G.pc_fov) >> 5;
        int16_t d3 = z ? (int16_t)z : 1;
        int16_t xs = (int16_t)(divs((int32_t)(int16_t)((a1 - G.pc_ys * d2 + G.ps_ys_fov) >> 8) * (int16_t)G.camz, d3) + d0 - G.center_x);
        int16_t ys = (int16_t)(divs((int32_t)(int16_t)((a2 + G.pc_yc * d2 - G.ps_yc_fov) >> 8) * (int16_t)G.camz, d3) + d1 + G.center_y);
        int16_t xe = (int16_t)(divs((int32_t)(int16_t)((a3 - G.pc_ys * d2 + G.ps_ys_fov) >> 8) * (int16_t)G.camz, d3) + d0 - G.center_x);
        int16_t ye = (int16_t)(divs((int32_t)(int16_t)((a4 + G.pc_yc * d2 - G.ps_yc_fov) >> 8) * (int16_t)G.camz, d3) + d1 + G.center_y);
        s_trace[l][0] = xs;
        s_trace[l][1] = ys;
        s_trace[l][2] = (int16_t)(xe - xs);
        s_trace[l][3] = (int16_t)(ye - ys);
    }
}

/* The chip's pass: each line walks the stamp map from its start by its
 * deltas (13.11 fixed point), 256 dots; a 4096 px map of 32 px stamps, not
 * repeated (outside: colour 0). A stamp: 16 cells by columns; its map entry
 * flips it (bit 15) then turns it (bits 13-14, a quarter each, anticlockwise). */
static uint8_t stamp_dot(int px, int py)
{
    if ((unsigned)px >= 4096 || (unsigned)py >= 4096) return 0;
    uint16_t e = s_stampmap[(py >> 5) * 128 + (px >> 5)];
    unsigned num = e & 0x7FC;
    if (!num) return 0;
    int x = px & 31, y = py & 31;
    if (e & 0x8000) x = 31 - x;
    for (int r = (e >> 13) & 3; r; r--) { int t = x; x = 31 - y; y = t; }
    uint32_t o = num * 0x80u - 0x200u + (uint32_t)(((x >> 3) * 4 + (y >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1));
    if (o >= s_stamps_len) return 0;
    uint8_t b = s_stamps[o];
    return x & 1 ? b & 15 : b >> 4;
}

static void run_gfx(void)                                     /* RunGfxOperation: the chip draws the picture */
{
    memset(s_image, 0, sizeof s_image);
    for (int l = 0; l < IMGH; l++) {
        int32_t x = (int32_t)(uint16_t)s_trace[l][0] << 8, y = (int32_t)(uint16_t)s_trace[l][1] << 8;
        int32_t dx = s_trace[l][2], dy = s_trace[l][3];
        for (int i = 0; i < IMGW; i++, x += dx, y += dy) {
            uint8_t c = stamp_dot((x >> 11) & 0x1FFF, (y >> 11) & 0x1FFF);
            if (!c) continue;
            uint8_t *p = &s_image[((i >> 3) * IMGHTILE + (l >> 3)) * 32 + (l & 7) * 4 + ((i & 7) >> 1)];
            *p |= i & 1 ? c : (uint8_t)(c << 4);
        }
    }
}

static void init_gfx(void)                                    /* InitGfxOperation */
{
    G.fov = 0x80;
    G.center = (uint16_t)-0x40;
}

/* ---- the stamp map's animation (Sub.asm AnimateStamps) ----------------------- */
static void init_stamp_anim(void)
{
    uint32_t t = label(SSL_STAMPANIMS) + s_stage_id * 0xCu;
    s_hazard_data = sub_l(t);
    s_hazard_count = sub_w(t + 4);
    s_fan_data = sub_l(t + 6);
    s_fan_count = sub_w(t + 0xA);
}

static void map_set(uint16_t off, uint16_t v) { s_stampmap[(off >> 1) & 0x3FFF] = v; }

static void animate_stamps(void)
{
    s_fan_frame += 2;
    if (s_fan_frame >= 6) s_fan_frame = 0;
    s_hazard_delay++;
    if (!(s_hazard_delay & 1)) {
        s_hazard_frame = (s_hazard_frame + 2) & 7;
        uint32_t a = s_hazard_data;
        for (int k = 0; k <= s_hazard_count; k++, a += 0xA) map_set(sub_w(a), sub_w(a + 2 + s_hazard_frame));
    }
    uint32_t a = s_fan_data;
    for (int k = 0; k <= s_fan_count; k++, a += 8) map_set(sub_w(a), sub_w(a + 2 + s_fan_frame));
}

/* GetStampAtPos: (raw entry, stamp number / 4) */
static uint16_t stamp_at(int16_t x, int16_t y, uint16_t *raw)
{
    uint16_t off = (uint16_t)((((uint16_t)x >> 5) << 1) + (((uint16_t)y >> 5) << 8));
    uint16_t e = s_stampmap[(off >> 1) & 0x3FFF];
    if (raw) *raw = e;
    return (uint16_t)((e & 0x7FF) >> 2);
}

/* ---- objects: animation, sprites, depth ------------------------------------- */
static void obj_anim(uint32_t a, int id)                      /* SetObjAnim */
{
    SB(a, oAnim, id);
    SB(a, oAnimFrame, 0);
    uint32_t an = sub_l(L(a, oMap) + (uint32_t)(uint8_t)id * 4u);
    SB(a, oAnimTime, sub_b(an + 1));
    SB(a, oAnimTime2, sub_b(an + 1));
}

static void obj_chg_anim(uint32_t a, int id)                  /* ChgObjAnim: the frame kept */
{
    SB(a, oAnim, id);
    uint32_t an = sub_l(L(a, oMap) + (uint32_t)(uint8_t)id * 4u);
    SB(a, oAnimTime, sub_b(an + 1));
    SB(a, oAnimTime2, sub_b(an + 1));
}

static int put_sprite(int y, uint8_t size, uint16_t tile, int x)   /* one entry; 0 = off screen (none) */
{
    if (y <= -32 + 128 || y >= 256 + 128) return 0;
    if (x <= -32 + 128 || x >= 320 + 128) return 0;
    x &= 0x1FF;
    if (!x) x = 1;
    uint8_t *p = &s_sub_sprites[s_cur_spr];
    s_sprite_count++;
    p[0] = (uint8_t)(y >> 8); p[1] = (uint8_t)y;
    p[2] = size; p[3] = s_sprite_count;
    p[4] = (uint8_t)(tile >> 8); p[5] = (uint8_t)tile;
    p[6] = (uint8_t)(x >> 8); p[7] = (uint8_t)x;
    s_cur_spr += 8;
    return 1;
}

/* Special/warp scenes use the original CD Sonic art for every selected
 * character. Saved identity and partner selection are preserved on return. */
static void draw_object(uint32_t a)                           /* DrawObject */
{
    uint32_t an = sub_l(L(a, oMap) + (uint32_t)B(a, oAnim) * 4u);
    uint8_t nfr = sub_b(an), spd = sub_b(an + 1);
    if (!btst_o(a, oFlags, 1)) {
        int8_t t = (int8_t)(B(a, oAnimTime) - 1);
        SB(a, oAnimTime, t);
        if (t < 0) {
            SB(a, oAnimTime, spd);
            SB(a, oAnimFrame, B(a, oAnimFrame) + 1);
            if (!(nfr > B(a, oAnimFrame))) SB(a, oAnimFrame, 0);
        }
    }
    if (btst_o(a, oFlags, 2)) return;
    int x0 = WS(a, oSprX), y0 = WS(a, oSprY);
    uint32_t fe = an + 2 + (uint32_t)B(a, oAnimFrame) * 4u;
    uint8_t flip = sub_b(fe);
    uint32_t sp = sub_l(fe) & 0xFFFFFF;
    int8_t n = (int8_t)sub_b(sp);
    if (n < 0) return;
    SB(a, oSpriteFlag, sub_b(sp + 1));
    sp += 5;
    uint16_t base = W(a, oTile);
    for (int k = 0; k <= n; k++, sp += 5) {
        if (s_sprite_count >= 0x50) break;
        int8_t yo = (int8_t)sub_b(sp), xo = (int8_t)sub_b(sp + 4);
        uint8_t size = sub_b(sp + 1);
        uint8_t hi = sub_b(sp + 2);
        uint16_t tile = (uint16_t)(((flip == 1 ? hi ^ 8 : hi) << 8 | sub_b(sp + 3)) + base);
        int y = y0 + yo, x = x0 + xo;
        if (flip == 1)                                        /* (the piece's width: byte arithmetic) */
            x = x0 + (int8_t)(uint8_t)-(uint8_t)(xo + (uint8_t)((uint8_t)(size * 2 + 8) & 0xF8));
        if (flip == 3) x = x0 - xo - (((size * 2) & 0x18) + 8);
        if (flip >= 2) {
            y = y0 - yo - (((size << 3) & 0x18) + 8);
            tile ^= flip == 3 ? 0x1800 : 0x1000;
        }
        put_sprite(y, size, tile, x);
    }
}

static void zbuf_init(void) { for (int l = 0; l < 64; l++) s_zbuf[l * 8] = 0; }   /* InitZBuffer */

static void set_3d_draw(uint32_t a, uint32_t d0)              /* Set3DObjectDraw (d0: the distance) */
{
    if (d0 >= 64u * 0x40u) d0 = 64u * 0x40u - 1;
    int i = ((((uint16_t)d0 >> 2) & 0x3F0) >> 1);
    int k = 0;
    while (k < 6 && s_zbuf[i + k]) k++;                       /* (the 7th taken: written over) */
    s_zbuf[i + k] = (uint16_t)a;
    s_zbuf[i + k + 1] = 0;
}

static void draw_3d_objects(void)                             /* Draw3DObjects: far to near */
{
    for (int l = 0; l < 64; l++)
        for (int k = 0; k < 8 && s_zbuf[l * 8 + k]; k++)
            draw_object(0x30000u | s_zbuf[l * 8 + k]);
}

static void init_3d_pos(void)                                 /* Init3DSpritePos */
{
    G.yc_fov = (int16_t)(((int16_t)G.fov * (int16_t)G.yaw_cos) >> 8);
    G.ys_fov = (int16_t)(((int16_t)G.fov * (int16_t)G.yaw_sin) >> 8);
}

static void set_3d_pos(uint32_t a)                            /* Set3DSpritePos: the 3D place on screen */
{
    uint32_t s = OA(O_SONIC);
    int16_t dx = (int16_t)(W(s, oX) - W(a, oX)), dy = (int16_t)(W(s, oY) - W(a, oY));
    int32_t d2 = (uint16_t)(G.fov + G.center);
    d2 -= ((int32_t)(int16_t)G.yaw_sin * dx) >> 8;
    d2 += ((int32_t)(int16_t)G.yaw_cos * dy) >> 8;
    if (!d2) d2 = 1;
    int32_t d3 = (int32_t)G.yc_fov * dx + (int32_t)G.ys_fov * dy;
    SW(a, oSprX, divs(d3, (int16_t)d2) + 256);
    d3 = ((int32_t)(int16_t)G.fov * WS(a, oZ)) >> 3;
    SW(a, oSprY, divs(d3, (int16_t)d2) + 256);
}

/* ---- sounds: Sonic CD's own FM sounds (its sound set, trilogy_music.c: in
 * from the stage's start, whatever the zone) ------------------------------- */
static void play_fm(uint8_t id)                               /* PlayFMSound (three queue slots) */
{
    for (int k = 0; k < 3; k++)
        if (!s_snd_queue[k]) { s_snd_queue[k] = id; return; }
}
static void flush_sounds(void)                                /* PlaySubFMSounds */
{
    for (int k = 0; k < 3; k++) {
        if (s_snd_queue[k]) o_sfx_cd(s_snd_queue[k]);
        s_snd_queue[k] = 0;
    }
}

/* ---- Sonic (Objects/Sonic) ---------------------------------------------------- */
#define SPR_GROUND (216 + 128)
static uint32_t find_slot(int first, int n)                   /* FindDustObjSlot / FindExplosionObjSlot */
{
    for (int k = 0; k < n; k++)
        if (!W(OA(first + k), oID)) return OA(first + k);
    return 0;
}

static void delete_splash(void)                               /* DeleteSplash */
{
    if (B(OA(O_SPLASH), oID)) bset_o(OA(O_SPLASH), oFlags, 0);
}

static void sonic_rotate(uint32_t a, uint32_t speed)          /* ObjSonic_Rotate */
{
    if (s_ctrl_hold & 8) { SL(a, oPlayerYaw, (L(a, oPlayerYaw) - speed) & 0x1FFFFFF); s_sub_scroll_flags |= 8; }
    if (s_ctrl_hold & 4) { SL(a, oPlayerYaw, (L(a, oPlayerYaw) + speed) & 0x1FFFFFF); s_sub_scroll_flags |= 4; }
}

static void sonic_check_jump(uint32_t a)                      /* ObjSonic_CheckJump */
{
    if (s_stage_inactive || !(s_ctrl_tap & 0x70)) return;
    SB(a, oRoutine, 2);
    SL(a, oPlayerSprYVel, (uint32_t)-0x80000);
    s_jump_timer = 20;
    bset_o(a, oFlags, 7);
    SW(a, oPlayerBumpTime, 0);
    play_fm(0x92);
    delete_splash();
}

static void sonic_check_win(void)                             /* ObjSonic_CheckWin */
{
    if (s_ufo_count) return;
    SB(OA(O_SONIC), oRoutine, 8);
    if (B(OA(O_TIMEUFO), oID)) bset_o(OA(O_TIMEUFO), oFlags, 0);
    if (B(OA(O_TIMEUFOSHADOW), oID)) bset_o(OA(O_TIMEUFOSHADOW), oFlags, 0);
}

static void sonic_get_stamps(uint32_t a)                      /* ObjSonic_GetStamps */
{
    static const int8_t off[5][2] = { { 0, 0 }, { -8, -8 }, { 8, -8 }, { 8, 8 }, { -8, 8 } };
    for (int k = 0; k < 5; k++) {
        uint16_t raw;
        uint16_t n = stamp_at((int16_t)(W(a, oX) + off[k][0]), (int16_t)(W(a, oY) + off[k][1]), &raw);
        SB(a, oPlayerStampC + k, sub_b(s_stamp_types + n));
        if (!k) SB(a, oPlayerStampOri, (raw >> 12) & 0xF);
    }
}

static void sonic_set_bump(uint32_t a, int32_t vx, int32_t vy)
{
    SL(a, oXVel, (uint32_t)vx);
    SL(a, oYVel, (uint32_t)vy);
    SW(a, oPlayerBumpTime, 0x10);
    SB(a, oRoutine, 4);
    bset_o(a, oFlags, 5);
    play_fm(0xB5);
    obj_anim(a, 0);
}

static void sonic_check_bumper(uint32_t a)                    /* ObjSonic_CheckBumper */
{
    int32_t d1 = ((int32_t)W(a, oPlayerSpeed) << 8) + 0x20000, d2 = -d1, d3 = 0;
    int f = (B(a, oPlayerStampTL) == 1) | (B(a, oPlayerStampTR) == 1) << 1 | (B(a, oPlayerStampBR) == 1) << 2 |
            (B(a, oPlayerStampBL) == 1) << 3;
    enum { NONE, DR, DL, DN, UL, UR, LF, RT, UP };
    static const uint8_t way[16] = { NONE, DR, DL, DN, UL, UR, LF, DL, UR, RT, UL, DR, UP, UR, UL, UL };
    switch (way[f]) {
    case DR: sonic_set_bump(a, d1, d1); break;
    case DL: sonic_set_bump(a, d2, d1); break;
    case DN: sonic_set_bump(a, d3, d1); break;
    case UL: sonic_set_bump(a, d2, d2); break;
    case UR: sonic_set_bump(a, d1, d2); break;
    case LF: sonic_set_bump(a, d2, d3); break;
    case RT: sonic_set_bump(a, d1, d3); break;
    case UP: sonic_set_bump(a, d3, d2); break;
    }
}

static void sonic_boost(uint32_t a, int speed, int time, int routine)   /* ObjSonic_Big/SmallBooster */
{
    static const int8_t dir[8][2] = { { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    int k = (B(a, oPlayerStampOri) & 0xE) >> 1;
    SW(a, oXVel, dir[k][0] * speed);
    SW(a, oYVel, dir[k][1] * speed);
    SW(a, oPlayerBumpTime, time);
    SB(a, oRoutine, routine);
}

static void sonic_stamp_collide(uint32_t a)                   /* ObjSonic_StampCollide */
{
    sonic_check_bumper(a);
    if (!s_ufo_count) return;
    uint8_t t = B(a, oPlayerStampC);
    if (t >= 10) t = 0;
    switch (t) {
    case 2:                                                   /* fan */
        SB(a, oRoutine, 3);
        SL(a, oPlayerSprYVel, (uint32_t)-0x40000);
        bset_o(a, oFlags, 6);
        play_fm(0xB8);
        delete_splash();
        break;
    case 3:                                                   /* water */
        if (s_time_stopped) break;
        SB(OA(O_SPLASH), oID, 8);
        if (s_flags & 2) SW(a, oPlayerTopSpeed, 0x500);
        break;
    case 4:                                                   /* rough ground */
        SW(a, oPlayerTopSpeed, 0x500);
        if (W(a, oPlayerSpeed) < 0x100) break;
        {
            uint32_t d = find_slot(O_DUST1, 8);
            if (d) SB(d, oID, 7);
        }
        play_fm(0xD6);
        break;
    case 5:                                                   /* spring */
        SB(a, oRoutine, 2);
        SL(a, oPlayerSprYVel, (uint32_t)-0x100000);
        bset_o(a, oFlags, 7);
        play_fm(0x98);
        delete_splash();
        break;
    case 6: {                                                 /* hazard: half the rings, up to 7 dropped */
        if (B(a, oRoutine) == 4 || B(a, oRoutine) == 7) break;
        SB(a, oPlayerTimer, 46);
        SB(a, oRoutine, 7);
        obj_anim(a, 0xD);
        uint16_t keep = s_rings >> 1, lost = (uint16_t)(s_rings - keep);
        s_rings = keep;
        if (lost > 7) lost = 7;
        for (int k = 7 - lost; k < 7; k++) SB(OA(O_RING1 + k), oID, 0xD);
        break;
    }
    case 7:                                                   /* big booster */
        play_fm(0xCE);
        obj_anim(a, 0xE);
        sonic_boost(a, 0x18, 0x14, 0x13);
        break;
    case 8:                                                   /* small booster */
        play_fm(0xC3);
        sonic_boost(a, 0x10, 8, 4);
        break;
    }
}

static void sonic_handle_speed(uint32_t a)                    /* ObjSonic_HandleSpeed */
{
    if (s_stage_inactive) return;
    if ((s_ctrl_hold & 0xF) == 2) {                           /* (down alone: slow down) */
        int16_t v = (int16_t)(W(a, oPlayerSpeed) - 0x40);
        SW(a, oPlayerSpeed, v < 0x200 ? 0x200 : v);
        return;
    }
    uint16_t top;
    if (W(a, oPlayerShoes)) { SW(a, oPlayerShoes, W(a, oPlayerShoes) - 1); top = 0xE00; }
    else top = B(a, oRoutine) == 7 ? 0x200 : W(a, oPlayerTopSpeed);
    SW(a, oPlayerSpeed, W(a, oPlayerSpeed) + 0x20);
    if (top < W(a, oPlayerSpeed)) SW(a, oPlayerSpeed, top);
}

static void sonic_move(uint32_t a)                            /* ObjSonic_Move */
{
    int d0 = (W(a, oPlayerYaw) + 0x180) & 0x1FF;
    int16_t spd = (int16_t)W(a, oPlayerSpeed);
    SL(a, oX, (uint32_t)(LS(a, oX) + (int32_t)get_cosine(d0) * spd) & 0xFFFFFFF);
    SL(a, oY, (uint32_t)(LS(a, oY) + (int32_t)get_sine(d0) * spd) & 0xFFFFFFF);
}

static void sonic_tilt(uint32_t a)                            /* ObjSonic_Tilt (its untilting is instant, as on the disc) */
{
    if (s_ctrl_hold & 4) { int8_t t = (int8_t)(B(a, oPlayerTilt) - 1); SB(a, oPlayerTilt, t < 0 ? 0 : t); }
    else if (s_ctrl_hold & 8) { uint8_t t = (uint8_t)(B(a, oPlayerTilt) + 1); SB(a, oPlayerTilt, t >= 0xA ? 9 : t); }
    else SB(a, oPlayerTilt, 5);
}

static void sonic_animate(uint32_t a)                         /* ObjSonic_Animate */
{
    static const uint8_t walk[5][4] = {
        { 0x04, 0x19, 0x1A, 0x1B }, { 0x03, 0x16, 0x17, 0x18 }, { 0x00, 0x10, 0x11, 0x12 },
        { 0x02, 0x13, 0x14, 0x15 }, { 0x05, 0x1C, 0x1D, 0x1E },
    };
    if (s_stage_inactive) return;
    int d0, ground = 0;
    uint16_t spd = W(a, oPlayerSpeed);
    if (btst_o(a, oFlags, 7)) d0 = 6;
    else if (btst_o(a, oFlags, 6)) d0 = 0xB;
    else if (!spd) d0 = 0xA;
    else {
        const uint8_t *w = walk[((B(a, oPlayerTilt) * 2) & 0x1C) >> 2];
        ground = 1;
        if (spd < 0x300) d0 = w[3];
        else if (spd < 0x540) d0 = w[2];
        else if (spd < 0x780) d0 = w[1];
        else if (spd < 0xB00) d0 = w[0];
        else { d0 = 1; ground = 0; goto reset; }          /* (sprinting: as the air ones, no flag change) */
    }
    if (ground) {
        if (bset_o(a, oFlags, 4)) {                           /* (already a ground animation: keep the frame) */
            if (B(a, oAnim) != d0) obj_chg_anim(a, d0);
            return;
        }
        goto reset;
    }
    bclr_o(a, oFlags, 4);
reset:
    if (B(a, oAnim) != d0) obj_anim(a, d0);
}

static void sonic_load_art(uint32_t a)                        /* ObjSonic_LoadArt */
{
    uint32_t art = sub_l(label(SSL_SONICART) + (uint32_t)B(a, oSpriteFlag) * 4u);
    for(unsigned k=0;k<sizeof s_sub_sonic_art;k++)s_sub_sonic_art[k]=sub_b(art+k);
}

static void sonic_air(uint32_t a, int32_t grav, int jump)     /* ObjSonic_Jump / _Float */
{
    SL(a, oSprY, (uint32_t)(LS(a, oSprY) + LS(a, oPlayerSprYVel)));
    SL(a, oPlayerSprYVel, (uint32_t)(LS(a, oPlayerSprYVel) + grav));
    if (jump && s_jump_timer) {                               /* (holding a jump button: the full height) */
        SL(a, oPlayerSprYVel, (uint32_t)(LS(a, oPlayerSprYVel) + grav));
        s_jump_timer--;
        if (s_ctrl_hold & 0x70) SL(a, oPlayerSprYVel, (uint32_t)(LS(a, oPlayerSprYVel) - grav));
    }
    if (W(a, oSprY) >= SPR_GROUND) {
        SB(a, oRoutine, 1);
        SL(a, oSprY, (uint32_t)SPR_GROUND << 16);
        SL(a, oPlayerSprYVel, 0);
        bclr_o(a, oFlags, jump ? 7 : 6);
        SW(a, oZ, 0x160);
    }
    if (!s_stage_inactive) sonic_rotate(a, 0x60000);
    SW(a, oZ, ((SPR_GROUND - WS(a, oSprY)) << 2) + 0x160);
}

static void obj_sonic(uint32_t a)                             /* ObjSonic */
{
    sonic_handle_speed(a);
    bclr_o(a, oFlags, 5);
    bclr_o(a, oFlags, 2);
    SW(a, oPlayerTopSpeed, 0xA00);
    int r = B(a, oRoutine);
    switch (r) {
    case 0: {                                                 /* ObjSonic_Init */
        SW(a, oTile, 0x85E0);
        SL(a, oMap, label(SSL_MAP_SONIC));
        SW(a, oSprX, 128 + 128);
        SW(a, oSprY, SPR_GROUND);
        obj_anim(a, 9);
        SB(a, oRoutine, 0x14);
        SW(a, oPlayerSpeed, 0);
        uint32_t sp = label(SSL_STARTPOS) + s_stage_id * 6u;
        SW(a, oX, sub_w(sp));
        SW(a, oY, sub_w(sp + 2));
        SW(a, oPlayerYaw, sub_w(sp + 4));
        SW(a, oZ, 0x160);
        SW(a, oPlayerPitch, 0x80);
        break;
    }
    case 1:                                                   /* ground */
        if (!s_stage_inactive) sonic_rotate(a, 0x60000);
        sonic_check_jump(a);
        sonic_check_win();
        break;
    case 2: sonic_air(a, 0xA000, 1); break;
    case 3: sonic_air(a, 0x2000, 0); break;
    case 4:                                                   /* bumped (or a small booster) */
        SW(a, oPlayerBumpTime, W(a, oPlayerBumpTime) - 1);
        if (!W(a, oPlayerBumpTime)) { SB(a, oRoutine, 1); SL(a, oXVel, 0); SL(a, oYVel, 0); }
        else { SL(a, oX, L(a, oX) + L(a, oXVel)); SL(a, oY, L(a, oY) + L(a, oYVel)); }
        if (!s_stage_inactive) sonic_rotate(a, 0x60000);
        sonic_check_jump(a);
        sonic_check_win();
        break;
    case 5:                                                   /* (unused: sinking) */
        SW(a, oSprY, W(a, oSprY) + 4);
        if (W(a, oSprY) >= 320 + 128) SB(a, oRoutine, 6);
        break;
    case 7:                                                   /* hurt */
        SB(a, oPlayerTimer, B(a, oPlayerTimer) - 1);
        if (!B(a, oPlayerTimer)) { SB(a, oRoutine, 1); obj_anim(a, 0); }
        break;
    case 8:                                                   /* every UFO down: the time stone */
        if (W(a, oSprY) < SPR_GROUND) {
            if (!s_stage_inactive) sonic_rotate(a, 0x60000);
            sonic_check_jump(a);
            break;
        }
        SB(a, oPlayerTimer, 60);
        obj_anim(a, 0xA);
        SB(a, oRoutine, 9);
        s_stage_inactive = 1;
        SW(a, oPlayerSpeed, 0);
        break;
    case 9:
        SB(a, oPlayerTimer, B(a, oPlayerTimer) - 1);
        if (B(a, oPlayerTimer)) break;
        SB(OA(O_TIMESTONE), oID, 0xE);
        SB(OA(O_SPARKLE1), oID, 0xF);
        SB(OA(O_SPARKLE2), oID, 0x10);
        SB(a, oRoutine, 0xA);
        SB(a, oPlayerTimer, 6);
        break;
    case 0xA: case 0xB: case 0xC: case 0xD: case 0xE: case 0xF: case 0x10: {   /* turning, to face the screen */
        static const uint8_t next_time[7] = { 4, 5, 4, 5, 4, 5, 0 };
        static const uint8_t next_anim[7] = { 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A };
        s_sub_scroll_flags |= 4;
        SW(a, oPlayerYaw, (W(a, oPlayerYaw) - 8) & 0x1FF);
        SB(a, oPlayerTimer, B(a, oPlayerTimer) - 1);
        if (B(a, oPlayerTimer)) break;
        SB(a, oRoutine, r + 1);
        if (next_time[r - 0xA]) SB(a, oPlayerTimer, next_time[r - 0xA]);
        obj_anim(a, next_anim[r - 0xA]);
        break;
    }
    case 0x12: obj_anim(a, 0x2B); break;                      /* holding the stone */
    case 0x13:                                                /* boosted */
        SW(a, oPlayerBumpTime, W(a, oPlayerBumpTime) - 1);
        if (!W(a, oPlayerBumpTime)) { SB(a, oRoutine, 1); SL(a, oXVel, 0); SL(a, oYVel, 0); obj_anim(a, 0); }
        else { SL(a, oX, L(a, oX) + L(a, oXVel)); SL(a, oY, L(a, oY) + L(a, oYVel)); }
        if (!s_stage_inactive) sonic_rotate(a, 0x60000);
        sonic_check_jump(a);
        break;
    case 0x15:                                                /* the start pose */
        obj_anim(a, 0x2C);
        SB(a, oRoutine, 0x16);
        SB(a, oPlayerTimer, 5);
        break;
    case 0x16:
        SB(a, oPlayerTimer, B(a, oPlayerTimer) - 1);
        if (B(a, oPlayerTimer)) break;
        obj_anim(a, 0xA);
        SB(a, oRoutine, 0x17);
        break;
    }
    if (!btst_o(a, oFlags, 3)) {
        if (!btst_o(a, oFlags, 7) && !btst_o(a, oFlags, 6) && !s_stage_inactive) {
            sonic_get_stamps(a);
            sonic_stamp_collide(a);
        }
        if (!btst_o(a, oFlags, 5)) sonic_move(a);
        if (W(a, oX) < 0x340) SW(a, oX, 0x340);
    }
    if (W(a, oX) >= 0x1000 - 0x340) SW(a, oX, 0x1000 - 0x340);
    if (W(a, oY) < 0x340) SW(a, oY, 0x340);
    if (W(a, oY) >= 0x1000 - 0x340) SW(a, oY, 0x1000 - 0x340);
    G.camx = W(a, oX);
    G.camy = W(a, oY);
    G.camz = W(a, oZ);
    G.pitch = W(a, oPlayerPitch);
    G.yaw = W(a, oPlayerYaw);
    if (B(a, oRoutine) != 7 && B(a, oRoutine) != 0x13) {
        sonic_tilt(a);
        sonic_animate(a);
    }
    draw_object(a);
    sonic_load_art(a);
    SB(a, oPlayerUFOCol, 0);
}

/* ---- UFOs (Objects/UFO) ---------------------------------------------------- */
static void ufo_follow_path(uint32_t a)                       /* ObjUFO_FollowPath */
{
    for (int guard = 0; guard < 4; guard++) {
        uint32_t p = L(a, oUFOPath);
        int16_t t = (int16_t)sub_w(p);
        SW(a, oUFOPathTime, t);
        if (t < 0) { SL(a, oUFOPath, L(a, oUFOPathStart)); continue; }   /* (the end: round again) */
        int16_t x0 = (int16_t)sub_w(p + 2), y0 = (int16_t)sub_w(p + 4);
        int16_t x1 = (int16_t)sub_w(p + 6), y1 = (int16_t)sub_w(p + 8);
        SW(a, oX, x0);
        SW(a, oY, y0);
        int16_t div = t ? t : 1;
        int32_t vx = divs((int32_t)(int16_t)(x1 - x0) << 4, div);
        int32_t vy = divs((int32_t)(int16_t)(y1 - y0) << 4, div);
        SL(a, oXVel, (uint32_t)(vx << 12));
        SL(a, oYVel, (uint32_t)(vy << 12));
        SL(a, oUFOPath, p + 10);
        return;
    }
}

static void ufo_draw(uint32_t a)                              /* ObjUFO_Draw: depth, and the picture by distance */
{
    static const uint8_t frames[82] = {
        0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 0,
    };
    uint32_t s = OA(O_SONIC);
    int quad;
    int16_t ux = (int16_t)W(a, oX), uy = (int16_t)W(a, oY), sx = (int16_t)W(s, oX), sy = (int16_t)W(s, oY);
    int ang = get_angle(ux, uy, sx, sy, &quad);
    uint32_t d = get_distance(ang, quad, ux, uy, sx, sy);
    set_3d_draw(a, d);
    if (d >= 0x500) d = 0x500;
    uint8_t f = frames[(d & 0xFFFF) >> 4];
    if (f != B(a, oUFOAnim)) { SB(a, oUFOAnim, f); obj_chg_anim(a, f); }
}

static void ufo_check_col(uint32_t a)                         /* ObjUFO_CheckPlayerCol */
{
    uint32_t s = OA(O_SONIC);
    if (!B(s, oID) || B(s, oPlayerUFOCol)) return;
    static int any = -1;                                      /* development: TRILOGY_SCDSSUFO=1, a jump reaches every UFO */
    if (any < 0) any = getenv("TRILOGY_SCDSSUFO") != NULL;
    if (any) goto height;
    int16_t l = (int16_t)(WS(a, oX) - 16), r = (int16_t)(l + 32);
    int16_t sl = (int16_t)(WS(s, oX) - 16), sr = (int16_t)(sl + 32);
    if (sl > r || sr < l) return;
    int16_t t = (int16_t)(WS(a, oY) - 12), b = (int16_t)(t + 24);
    int16_t st = (int16_t)(WS(s, oY) - 16), sb = (int16_t)(st + 32);
    if (st > b || sb < t) return;
height:
    if (W(s, oZ) < 0x210 || W(s, oZ) >= 0x270) return;        /* (only in the air, at the UFO's height) */
    SB(s, oPlayerUFOCol, B(a, oID));
    SB(a, oUFOPlayerCol, B(s, oID));
}

static void add_rings(uint16_t n)                             /* AddRings */
{
    s_rings = (uint16_t)(s_rings + n);
    if (s_rings > 999) s_rings = 999;
}

static void ufo_explode_start(uint32_t a)
{
    SB(a, oRoutine, 2);
    bset_o(L(a, oUFOShadow), oFlags, 0);                      /* (its shadow goes) */
    SW(a, oTimer, 60);
    SB(a, oUFOExplodeDir, random32() & 1);
    obj_anim(a, 0);
}

static void ufo_explode(uint32_t a)                           /* ObjUFO_Explode / ObjTimeUFO_Explode */
{
    SW(a, oSprX, W(a, oSprX) - 4);
    if (!B(a, oUFOExplodeDir)) SW(a, oSprX, W(a, oSprX) + 8);
    SW(a, oSprY, W(a, oSprY) + 1);
    bclr_o(a, oFlags, 2);
    SW(a, oTimer, W(a, oTimer) - 1);
    if (!W(a, oTimer)) bset_o(a, oFlags, 0);
    if (B(a, oTimer + 1) & 1) return;                         /* (every other frame) */
    uint32_t e = find_slot(O_EXPL1, 8);
    if (!e) return;
    SB(e, oID, 0xC);
    SW(e, oSprX, W(a, oSprX) - 16);
    SW(e, oSprY, W(a, oSprY));
    uint16_t r = (uint16_t)random32();
    SW(e, oSprX, W(e, oSprX) + (r & 0x1F));
    SW(e, oSprY, W(e, oSprY) - (r & 0x1F));                   /* (the disc's: the same bits again) */
}

static void ufo_move(uint32_t a)
{
    SL(a, oX, L(a, oX) + L(a, oXVel));
    SL(a, oY, L(a, oY) + L(a, oYVel));
    SW(a, oUFOPathTime, W(a, oUFOPathTime) - 1);
    if (!W(a, oUFOPathTime)) ufo_follow_path(a);
    ufo_draw(a);
    set_3d_pos(a);
    ufo_check_col(a);
}

static void ufo_after(uint32_t a)                             /* (both kinds, after their routine) */
{
    SW(a, oZ, W(OA(O_SONIC), oZ) - 0x140);
    if (B(a, oUFODrawDelay)) { SB(a, oUFODrawDelay, B(a, oUFODrawDelay) - 1); bset_o(a, oFlags, 2); }
}

static void obj_ufo(uint32_t a)                               /* ObjUFO */
{
    switch (B(a, oRoutine)) {
    case 0:
        SW(a, oTile, 0xE440);
        ufo_follow_path(a);
        SL(a, oMap, label(B(a, oUFOItem) ? SSL_MAP_UFOSHOES : SSL_MAP_UFORINGS));
        SW(a, oZ, W(OA(O_SONIC), oZ) - 0x140);
        SB(a, oUFOAnim, 0);
        obj_anim(a, 0);
        SB(a, oUFODrawDelay, 2);
        SB(a, oRoutine, 1);
        /* fall through */
    case 1:
        ufo_move(a);
        if (!B(a, oUFOPlayerCol)) break;
        if (s_ufo_count < 2) s_time_stopped = 1;              /* (the last one: the clock stops) */
        if (s_ufo_count) s_ufo_count--;                       /* DecUFOCount */
        ufo_explode_start(a);
        {
            uint32_t it = OA(O_ITEM);
            SB(it, oID, 4);
            SW(it, oSprX, W(a, oSprX));
            SW(it, oSprY, W(a, oSprY));
            SB(it, oItemSpawnType, B(a, oUFOItem));
        }
        if ((B(a, oUFOItem) & 3) == 1) {                      /* speed shoes */
            SW(OA(O_SONIC), oPlayerShoes, 200);
            s_ufo_ring_bonus = 20;
        } else {                                              /* rings: the bonus, doubling */
            uint16_t n = s_ufo_ring_bonus;
            s_ufo_ring_bonus = (uint16_t)(n * 2);
            add_rings(n);
        }
        break;
    case 2:
        ufo_explode(a);
        break;
    }
    ufo_after(a);
}

static void obj_time_ufo(uint32_t a)                          /* ObjTimeUFO */
{
    switch (B(a, oRoutine)) {
    case 0:
        SW(a, oTile, 0x8440);
        ufo_follow_path(a);
        SL(a, oMap, label(SSL_MAP_UFORINGS));
        SW(a, oZ, W(OA(O_SONIC), oZ) - 0x140);
        SB(a, oRoutine, 1);
        SB(a, oUFODrawDelay, 2);
        play_fm(0xBC);
        /* fall through */
    case 1:
        ufo_move(a);
        if (!B(a, oUFOPlayerCol) || s_time_stopped) break;
        ufo_explode_start(a);
        s_timer += 30;                                        /* (thirty seconds more) */
        {
            uint32_t it = OA(O_ITEM);
            SB(it, oID, 4);
            SB(it, oItemSpawnType, 3);
            SW(it, oSprX, W(a, oSprX));
            SW(it, oSprY, W(a, oSprY));
        }
        break;
    case 2:
        ufo_explode(a);
        break;
    }
    ufo_after(a);
}

static void spawn_ufo(uint32_t ufo, uint32_t path, int id)
{
    uint32_t sh = ufo + (uint32_t)(O_UFOSHADOW1 - O_UFO1) * OSIZE;
    SB(ufo, oID, id);
    SB(ufo, oUFOItem, sub_b(path));
    SB(ufo, oUFOUnk, sub_b(path + 1));
    SL(ufo, oUFOPathStart, path + 2);
    SL(ufo, oUFOPath, path + 2);
    SL(ufo, oUFOShadow, sh);
    SB(sh, oID, 5);
    SL(sh, oShadowParent, ufo);
}

static void spawn_ufos(void)                                  /* SpawnUFOs */
{
    uint32_t t = sub_l(label(SSL_UFOPATHS) + s_stage_id * 4u);
    uint16_t n = sub_w(t);
    s_ufo_count = (uint8_t)n;
    for (int k = 0; k < n && k < 6; k++) spawn_ufo(OA(O_UFO1 + k), sub_l(t + 2 + k * 4u), 2);
}

static void spawn_time_ufo(void)                              /* SpawnTimeUFO: under 20 seconds left */
{
    if (s_timer >= 20 || B(OA(O_TIMEUFO), oID)) return;
    spawn_ufo(OA(O_TIMEUFO), label(SSL_TIMEUFOPATH), 3);
}

/* ---- shadows (Objects/Shadow) ------------------------------------------------ */
static void ufo_on_screen(uint32_t a)                         /* ObjUFO_ChkOnScreen */
{
    bclr_o(a, oFlags, 2);
    if (W(a, oSprX) >= 384 + 128 || WS(a, oSprY) < 128 + 128 || WS(a, oSprY) >= 320 + 128) bset_o(a, oFlags, 2);
}

static void obj_ufo_shadow(uint32_t a)                        /* ObjUFOShadow */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0xE6DC);
        SL(a, oMap, label(SSL_MAP_SHADOW));
        SB(a, oShadowSprite, 0);
        obj_anim(a, 0);
        SB(a, oRoutine, 1);
    }
    uint32_t u = L(a, oShadowParent);
    SW(a, oX, W(u, oX));
    SW(a, oY, W(u, oY));
    bset_o(u, oFlags, 2);                                     /* (the UFO shows when its shadow does) */
    if (!btst_o(a, oFlags, 2)) bclr_o(u, oFlags, 2);
    SW(a, oZ, W(OA(O_SONIC), oZ));
    ufo_draw(a);
    set_3d_pos(a);
    ufo_on_screen(a);
    draw_object(a);
}

static void obj_sonic_shadow(uint32_t a)                      /* ObjSonicShadow */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0xE6DC);
        SL(a, oMap, label(SSL_MAP_SHADOW));
        SB(a, oShadowSprite, 5);
        obj_anim(a, 5);
        SB(a, oRoutine, 1);
    }
    uint32_t s = OA(O_SONIC);
    SW(a, oX, W(s, oX));
    SW(a, oY, W(s, oY));
    SW(a, oZ, W(s, oZ));
    set_3d_pos(a);
    draw_object(a);
}

/* ---- items, lost rings, explosions, dust, splash (Objects/...) --------------- */
static void obj_item(uint32_t a)                              /* ObjItem: what a UFO held, rising */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0x878F);
        SL(a, oMap, label(SSL_MAP_ITEM));
        SB(a, oItemType, B(a, oItemSpawnType));
        obj_anim(a, B(a, oItemSpawnType));
        SB(a, oRoutine, 1);
        SB(a, oTimer, 0x10);
        SW(a, oItemYVel, -0x10);
        play_fm(0x95);
    }
    SB(a, oTimer, B(a, oTimer) - 1);
    if (!B(a, oTimer)) bset_o(a, oFlags, 0);
    SL(a, oSprY, L(a, oSprY) + L(a, oItemYVel));
    SL(a, oItemYVel, L(a, oItemYVel) + 0x20000);
    draw_object(a);
}

static void obj_lost_ring(uint32_t a)                         /* ObjLostRing */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0xE78F);
        SL(a, oMap, label(SSL_MAP_ITEM));
        SB(a, oItemType, 4);
        obj_anim(a, 4);
        SW(a, oSprX, W(OA(O_SONIC), oSprX));
        SW(a, oSprY, W(OA(O_SONIC), oSprY));
        SB(a, oRoutine, 1);
        SB(a, oTimer, 45);
        uint32_t r = random32();
        int32_t vx = (int32_t)(r & 0x3F000);
        s_lost_ring_dir ^= 1;
        if (!(s_lost_ring_dir & 1)) vx = -vx;                 /* (bchg: the old bit set, left) */
        SL(a, oItemXVel, (uint32_t)vx);
        SW(a, oItemYVel, -0xA - (int)(r & 0xF));
        play_fm(0x94);
    }
    SB(a, oTimer, B(a, oTimer) - 1);
    if (!B(a, oTimer)) bset_o(a, oFlags, 0);
    SL(a, oSprX, L(a, oSprX) + L(a, oItemXVel));
    SL(a, oSprY, L(a, oSprY) + L(a, oItemYVel));
    if (W(a, oSprY) > 216 + 128) {                            /* (a bounce off the bottom) */
        SW(a, oSprY, 216 + 128);
        SL(a, oItemYVel, (uint32_t)-LS(a, oItemYVel));
    } else
        SL(a, oItemYVel, L(a, oItemYVel) + 0x20000);
    draw_object(a);
}

static void obj_explosion(uint32_t a)                         /* ObjExplosion */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0x87AE);
        SL(a, oMap, label(SSL_MAP_EXPLOSION));
        obj_anim(a, 0);
        SW(a, oTimer, 12);
        SB(a, oRoutine, 1);
        play_fm(0xA3);
    }
    SW(a, oTimer, W(a, oTimer) - 1);
    if (!W(a, oTimer)) bset_o(a, oFlags, 0);
    draw_object(a);
}

static void obj_dust(uint32_t a)                              /* ObjDust */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0x87AE);
        SL(a, oMap, label(SSL_MAP_DUST));
        SW(a, oSprX, 112 + 128);
        SW(a, oSprY, 212 + 128);
        obj_anim(a, 0);
        SW(a, oTimer, 6);
        SB(a, oRoutine, 1);
        uint16_t r = (uint16_t)random32() & 0x1F;
        SW(a, oSprX, W(a, oSprX) + r);
        SW(a, oSprY, W(a, oSprY) + (r & 7));                  /* (the disc's: the same bits again) */
        SW(a, oDustXVel, (s_ctrl_hold & 4) ? 3 : (s_ctrl_hold & 8) ? -3 : 0);
    }
    SW(a, oTimer, W(a, oTimer) - 1);
    if (!W(a, oTimer)) bset_o(a, oFlags, 0);
    SL(a, oSprX, L(a, oSprX) + L(a, oDustXVel));
    SW(a, oSprY, W(a, oSprY) - 1);
    draw_object(a);
}

static void obj_splash(uint32_t a)                            /* ObjSplash: water, which speeds the clock */
{
    uint32_t s = OA(O_SONIC);
    switch (B(a, oRoutine)) {
    case 0:
        SW(a, oTile, 0x8582);
        SL(a, oMap, label(SSL_MAP_SPLASH));
        SW(a, oSprX, 128 + 128);
        SW(a, oSprY, 216 + 128);
        obj_anim(a, 0);
        SW(a, oTimer, 14);
        SB(a, oRoutine, 1);
        play_fm(0xA2);
        if (!(s_flags & 2)) s_timer_speedup = 10;
        /* fall through */
    case 1:
        SW(a, oTimer, W(a, oTimer) - 1);
        if (W(a, oTimer)) break;
        if (B(s, oPlayerStampC) != 3) { bset_o(a, oFlags, 0); break; }
        obj_anim(a, 1);
        SB(a, oRoutine, 2);
        break;
    case 2:
        if (B(s, oPlayerStampC) != 3) { bset_o(a, oFlags, 0); break; }
        if (!B(a, oAnimFrame)) s_timer_speedup = 2;
        break;
    }
    draw_object(a);
    if (s_time_stopped) bset_o(a, oFlags, 0);
}

/* ---- the title card (Objects/Title Card): id $A slides the text, $B the bar -- */
static void obj_ttl_text(uint32_t a)                          /* ObjTitleCardText */
{
    switch (B(a, oRoutine)) {
    case 0:
        SW(a, oTile, 0x8516);
        SL(a, oMap, label(SSL_MAP_TTLTEXT));
        SW(a, oSprX, 328 + 128);
        SW(a, oSprY, 112 + 128);
        obj_anim(a, 0);
        SB(a, oRoutine, 1);
        /* fall through */
    case 1:
        SW(a, oSprX, W(a, oSprX) - 32);
        if (W(a, oSprX) > 184 + 128) break;
        SW(a, oSprX, 184 + 128);
        SW(a, oTimer, 80);
        SB(a, oRoutine, 2);
        break;
    case 2:
        SW(a, oTimer, W(a, oTimer) - 1);
        if (!W(a, oTimer)) SB(a, oRoutine, 3);
        break;
    case 3:
        SW(a, oSprX, W(a, oSprX) + 32);
        if (W(a, oSprX) > 336 + 128) bset_o(a, oFlags, 0);
        break;
    }
    draw_object(a);
}

static void obj_ttl_bar(uint32_t a)                           /* ObjTitleCardBar: it lets Sonic go */
{
    switch (B(a, oRoutine)) {
    case 0:
        SW(a, oTile, 0x8516);
        SL(a, oMap, label(SSL_MAP_TTLBAR));
        SW(a, oSprX, 116 + 128);
        SW(a, oSprY, -96 + 128);
        obj_anim(a, 0);
        SB(a, oRoutine, 1);
        /* fall through */
    case 1:
        SW(a, oSprY, W(a, oSprY) + 32);
        if (W(a, oSprY) < 112 + 128) break;
        SW(a, oSprY, 112 + 128);
        SW(a, oTimer, 80);
        SB(a, oRoutine, 2);
        break;
    case 2:
        SW(a, oTimer, W(a, oTimer) - 1);
        if (!W(a, oTimer)) SB(a, oRoutine, 3);
        if (W(a, oTimer) == 50) SB(OA(O_SONIC), oRoutine, 0x15);   /* (his starting pose) */
        break;
    case 3:
        SW(a, oSprY, W(a, oSprY) - 32);
        if (WS(a, oSprY) >= 0) break;
        SW(a, oTimer, 3);
        bset_o(a, oFlags, 2);
        SB(a, oRoutine, 4);
        SB(OA(O_SONIC), oRoutine, 1);
        break;
    case 4:
        SW(a, oTimer, W(a, oTimer) - 1);
        if (W(a, oTimer)) break;
        bset_o(a, oFlags, 0);
        s_stage_inactive = 0;
        break;
    }
    draw_object(a);
}

/* ---- the time stone and its sparkles (Objects/Time Stone) -------------------- */
static void obj_time_stone(uint32_t a)                        /* ObjTimeStone */
{
    switch (B(a, oRoutine)) {
    case 0:
        SW(a, oTile, 0xE424);
        SL(a, oMap, label(SSL_MAP_TIMESTONE));
        SW(a, oSprX, 129 + 128);
        SW(a, oSprY, -16 + 128);
        obj_anim(a, 0);
        SW(a, oTimer, 30);
        SB(a, oRoutine, 1);
        /* fall through */
    case 1:
        SW(a, oTimer, W(a, oTimer) - 1);
        if (!W(a, oTimer)) SB(a, oRoutine, 2);
        break;
    case 2:
        SW(a, oSprY, W(a, oSprY) + 4);
        if (W(a, oSprY) < 208 + 128) break;
        SB(a, oRoutine, 3);
        bset_o(OA(O_SPARKLE1), oFlags, 0);
        bset_o(OA(O_SPARKLE2), oFlags, 0);
        SW(a, oTimer, 60);
        SB(OA(O_SONIC), oRoutine, 0x12);
        play_fm(0xD9);
        break;
    case 3:
        SW(a, oTimer, W(a, oTimer) - 1);
        if (!W(a, oTimer)) s_got_stone = 1;
        break;
    }
    draw_object(a);
}

static void obj_sparkle(uint32_t a, int anim, int dy)         /* ObjSparkle1 / 2 */
{
    if (!B(a, oRoutine)) {
        SW(a, oTile, 0xE424);
        SL(a, oMap, label(SSL_MAP_TIMESTONE));
        obj_anim(a, anim);
        SB(a, oRoutine, 1);
    }
    uint32_t t = OA(O_TIMESTONE);
    SW(a, oSprX, W(t, oSprX));
    SW(a, oSprY, W(t, oSprY) - dy);
    draw_object(a);
}

/* ---- running them (Sub.asm RunObjects) ---------------------------------------- */
static void run_object(int slot)                              /* RunObject */
{
    uint32_t a = OA(slot);
    switch (B(a, oID)) {
    case 0: return;
    case 1: obj_sonic(a); break;
    case 2: obj_ufo(a); break;
    case 3: obj_time_ufo(a); break;
    case 4: obj_item(a); break;
    case 5: obj_ufo_shadow(a); break;
    case 6: obj_sonic_shadow(a); break;
    case 7: obj_dust(a); break;
    case 8: obj_splash(a); break;
    case 0xA: obj_ttl_text(a); break;
    case 0xB: obj_ttl_bar(a); break;
    case 0xC: obj_explosion(a); break;
    case 0xD: obj_lost_ring(a); break;
    case 0xE: obj_time_stone(a); break;
    case 0xF: obj_sparkle(a, 1, 16); break;
    case 0x10: obj_sparkle(a, 2, 32); break;
    default: break;                                           /* ($9: "Press Start", the demo's) */
    }
    if (btst_o(a, oFlags, 0)) memset(ob(a), 0, OSIZE);        /* (deleted: Fill128) */
}

static void run_objects(void)                                 /* RunObjects */
{
    zbuf_init();
    for (int k = 0; k < OBJ1COUNT; k++) run_object(PRIO1 + k);
    run_object(O_SONIC);
    run_object(O_SONICSHADOW);
    for (int k = 0; k < OBJ2COUNT; k++) run_object(PRIO2 + k);
    draw_3d_objects();
    for (int k = 0; k < OBJ3COUNT; k++) run_object(PRIO3 + k);
}

static void timer_tick(void)                                  /* UpdateTimer .Tick */
{
    if (s_time_stopped || s_stage_inactive) return;
    if (s_timer == 0) {                                       /* (subq.l: below zero, over) */
        s_timer_speedup = 0;
        s_stage_over = 1;
    } else
        s_timer--;
    spawn_time_ufo();
    if (s_timer < 0xF) s_snd_queue[0] = 0xDF;                 /* (running out) */
}

static void update_timer(void)                                /* UpdateTimer (a countdown; no time attack here) */
{
    if (!--s_timer_frames) { s_timer_frames = 60 / 3; timer_tick(); }
    if (s_timer_speedup) { s_timer_speedup--; timer_tick(); }
}

static int ufos_left(void)                                    /* CheckUFOPresence */
{
    for (int k = 0; k < 6; k++)
        if (B(OA(O_UFO1 + k), oID)) return 1;
    return 0;
}

/* the stage set up (Sub.asm's start): its map, Sonic, the title card, the UFOs */
static void sub_init(void)
{
    memset(s_objram, 0, sizeof s_objram);
    memset(s_zbuf, 0, sizeof s_zbuf);
    memset(&G, 0, sizeof G);
    s_stage_over = s_got_stone = s_timer_speedup = s_stage_inactive = s_time_stopped = 0;
    s_lost_ring_dir = 0;
    s_hazard_delay = s_hazard_frame = s_fan_frame = 0;
    s_rng = 0;
    s_timer = 0;
    s_rings = 0;
    s_ufo_count = 6;
    init_gfx();
    memset(s_image, 0, sizeof s_image);
    uint32_t n;                                               /* LoadStageMap: stamps and the stamp map */
    s_stamps = item(IT_STAMPS + s_stage_id, &n);
    s_stamps_len = n;
    const uint8_t *m = item(IT_STAMPMAP + s_stage_id, &n);
    for (uint32_t k = 0; k < 128 * 128; k++) s_stampmap[k] = k * 2 + 1 < n ? be16(m + k * 2) : 0;
    s_stamp_types = sub_l(label(SSL_STAMPTYPES) + s_stage_id * 4u);   /* GetStampTypes */
    SB(OA(O_SONIC), oID, 1);
    SB(OA(O_SONICSHADOW), oID, 6);
    SB(OA(O_TTLBAR), oID, 0xA);
    SB(OA(O_TTLTEXT), oID, 0xB);
    spawn_ufos();
    init_stamp_anim();
    s_timer_frames = 60 / 3;
    s_ufo_ring_bonus = 20;
    s_stage_inactive = 1;
    s_timer = 100;                                            /* (a hundred seconds) */
}

/* one update (MainLoop, after the Main CPU's go): the floor drawn from last
 * time's camera, the objects moved, the clock run. 1 = the stage is over. */
static int sub_update(void)
{
    memset(s_snd_queue, 0, sizeof s_snd_queue);
    animate_stamps();
    get_gfx_sines();
    gen_trace_table();
    run_gfx();
    s_sprite_count = 0;
    s_cur_spr = 0;
    init_3d_pos();
    run_objects();
    if (s_cur_spr + 4 <= sizeof s_sub_sprites) memset(&s_sub_sprites[s_cur_spr], 0, 4);
    update_timer();
    if (s_stage_over) return 1;
    if (s_got_stone) { s_time_stones |= (uint8_t)(1u << s_stage_id); return 1; }
    return 0;
}

/* ============================================================================
 * The Main CPU (Main.asm): the VDP, the backgrounds, the HUD, the palette.
 * Its stage loop is three frames per update: the rendered floor goes to the
 * hidden half of plane A over two V-ints, the sprites and Sonic's art with the
 * second, and the halves swap on the next update. Here the VDP is written
 * directly (S3K's V-int $14 sends nothing), so the stage keeps its own VRAM
 * layout: plane A $C000, B $E000, sprites $D400, H scroll $D000.
 * ========================================================================== */
#define GM_SCENE        0x10
#define V_INT_QUIET     0x14                  /* (VInt_14: pads every 16 frames, no DMA) */
#define POLL_CONTROLLERS 0x0010DEu
#define IMGVRAM         0x0020u
static uint8_t *vram(void) { return g_machine.vdp.vram; }
static void vram_w16(uint32_t a, uint16_t v) { vram()[a & 0xFFFF] = (uint8_t)(v >> 8); vram()[(a + 1) & 0xFFFF] = (uint8_t)v; }
static void vram_copy(uint32_t a, const uint8_t *src, uint32_t n) { for (uint32_t k = 0; k < n; k++) vram()[(a + k) & 0xFFFF] = src[k]; }
static void set_reg(int r, uint8_t v) { g_machine.vdp.reg[r] = v; }

static int s_dbg = -1;                        /* development: TRILOGY_SCDSSLOG */
#define TRACE(...) do { if (s_dbg > 0) { fprintf(stderr, __VA_ARGS__); fflush(stderr); } } while (0)
static int s_active, s_phase, s_step, s_fade, s_sub_over, s_exit, s_results_timer, s_score_tallied;
static int s_paused, s_pause_ok;
static void (*s_done_cb)(int got);
static uint8_t s_saved_regs[24];
static uint16_t s_pal[64], s_fade_pal[64];
static int s_pal_dirty;
static uint8_t s_stage_image[IMGLEN], s_sprites[0x280], s_sonic_art[0x300], s_scroll_flags;
static uint8_t s_ctrl_held, s_ctrl_tapped;
static uint32_t s_scroll_off[6];
static uint16_t s_bgh[256], s_bgf[256];       /* bgHScroll / bgHScrollFrac: (A, B) words per line, 128 lines */
static uint16_t s_stage_hs[2][128];           /* stageHScroll1 / 2 */
static uint16_t s_ss1_shift, s_ss5_shift;     /* (ss5BGScrollShift doubles as ss6BGWaveTableOff) */
static int8_t s_bg_mode;
static uint16_t s_pc_index[4], s_pc_timer;    /* the palette cycles' counters (the program's own words) */
static uint8_t s_ts_pal_delay;
static uint32_t s_time_bonus, s_ring_bonus;
static int s_had_stone;                       /* (the stone held before: no new emerald) */

/* ---- tilemaps and art (scdss.bin) ------------------------------------------- */
static int draw_tilemap_in(int it, int id)
{
    uint32_t len;
    const uint8_t *t = item(it, &len);
    for (int k = 0, n = be16(t); k < n; k++) {
        const uint8_t *e = t + 2 + k * 12;                    /* (u8 id, u8 0, u16 VRAM, u16 w, u16 h, u32 offset) */
        if (e[0] != id) continue;
        uint16_t va = be16(e + 2), w = be16(e + 4), h = be16(e + 6);
        if (be32(e + 8) + (uint32_t)w * h * 2u > len || w > 64 || h > 64) return 0;
        const uint8_t *words = t + be32(e + 8);
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++)
                vram_w16(va + (uint32_t)r * 0x80u + (uint32_t)c * 2u, be16(words + (r * w + c) * 2));
        return 1;
    }
    return 0;
}

static void draw_tilemaps(uint32_t ids)                       /* DrawTilemaps: up to four ids, low byte first */
{
    for (int k = 0; k < 4; k++, ids >>= 8) {
        int id = (int)(ids & 0xFF);
        if (id && !draw_tilemap_in(IT_MAPS + s_stage_id, id)) draw_tilemap_in(IT_COMMONMAPS, id);
    }
}

static void load_art_item(int it)
{
    const uint8_t *t = item(it, NULL);
    int n = be16(t);
    const uint8_t *tiles = t + 2 + n * 4;
    for (int k = 0; k < n; k++) {
        uint16_t va = be16(t + 2 + k * 4), count = be16(t + 4 + k * 4);
        vram_copy(va, tiles, count * 32u);
        tiles += count * 32u;
    }
}

static void load_pal(uint32_t addr, int first, int count)     /* (a palette in the Main program or a stage block) */
{
    for (int k = 0; k < count && first + k < 64; k++) {
        uint32_t a = addr + (uint32_t)k * 2u;
        uint32_t len;
        const uint8_t *p;
        if (a >= 0xFF8C00u) { p = item(IT_BLOCK + s_stage_id, &len); a -= 0xFF8C00u; }
        else { p = item(IT_MAIN, &len); a -= 0xFF2000u; }
        s_pal[first + k] = a + 1 < len ? be16(p + a) : 0;
    }
    s_pal_dirty = 1;
}

static void cram_flush(void)
{
    if (!s_pal_dirty) return;
    s_pal_dirty = 0;
    for (int i = 0; i < 64; i++) {
        uint16_t c = s_pal[i] & 0x0EEE;
        g_machine.vdp.cram[i] = c;
        if (g_machine.vdp.colour_updated) g_machine.vdp.colour_updated(g_machine.vdp.colour_updated_user, (unsigned)i, c);
    }
}

static void fm(uint8_t id) { o_sfx_cd(id); }

/* ---- the controller (ReadController, in the V-int) ---------------------------- */
static void read_controller(void)
{
    uint8_t held = r8(RAM_Ctrl_1_held);
    s_ctrl_tapped = (uint8_t)(held & ~s_ctrl_held);
    s_ctrl_held = held;
    if ((s_ctrl_tapped & 0x80) && s_pause_ok) {
        s_paused ^= 1;
        scd_cd_pause(s_paused);
    }
}

/* ---- the floor's two halves ---------------------------------------------------- */
static int line_scroll(void) { return s_stage_id == 0 || s_stage_id == 4 || s_stage_id == 5; }   /* CheckLineScroll */

static void show_stage_buf(int buf)                           /* ShowStageBuf1 / 2: plane A's lower 96 lines */
{
    if (line_scroll()) {
        for (int l = 0; l < 96; l++) vram_w16(0xD200u + (uint32_t)l * 4u, s_stage_hs[buf][l]);
    } else {
        for (int r = 0; r < 12; r++) vram_w16(0xD200u + (uint32_t)r * 0x20u, buf ? 0x100 : 0);
    }
}

static void copy_image(int buf, int part)                     /* COPYIMG */
{
    uint32_t off = part ? 0x2000u : 0, n = part ? IMGLEN - 0x2000u : 0x2000u;
    vram_copy(IMGVRAM + (uint32_t)buf * IMGLEN + off, s_stage_image + off, n);
}

static void copy_sprites(void)                                /* CopySprites */
{
    vram_copy(0xD400, s_sprites, sizeof s_sprites);
    vram_copy(0xBC00, s_sonic_art, sizeof s_sonic_art);
}

static void draw_stage_map(void)                              /* DrawStageMap: two 32x12 tile pictures */
{
    for (int b = 0; b < 2; b++) {
        uint32_t row = b ? 0xC840u : 0xC800u;
        uint16_t first = b ? 0x2181 : 0x2001;
        for (int r = 0; r < IMGHTILE; r++, row += 0x80, first++) {
            uint16_t t = first;
            for (int c = 0; c < IMGW / 8; c++, t += IMGHTILE)
                vram_w16(row + (uint32_t)c * 2u, (uint16_t)(c == IMGW / 8 - 1 ? t + 0x8000 : t));   /* (the last column high) */
        }
    }
}

/* ---- the backgrounds (BGScroll1-3, CopyHScroll) --------------------------------- */
static void scroll_speed(int32_t *d0, int32_t *d1)            /* GetScrollSpeed */
{
    if (s_scroll_flags & 4) return;
    *d0 = -*d0;
    *d1 = -*d1;
    if (s_scroll_flags & 8) return;
    *d0 = 0;
    *d1 = 0;
}

static uint32_t bg_get(int i) { return (uint32_t)s_bgh[i] << 16 | s_bgf[i]; }
static void bg_set(int i, uint32_t v) { s_bgh[i] = (uint16_t)(v >> 16); s_bgf[i] = (uint16_t)v; }

static void apply_ss1(int line, int32_t d0, int32_t d1, uint16_t d3, int n)   /* ApplyBGScroll_SS1 */
{
    for (int k = 0; k < n; k++, d0 -= d1) {
        int i = (line + k) * 2;
        s_bgh[i] = d3;
        s_bgf[i] = 0;
        bg_set(i + 1, bg_get(i + 1) + (uint32_t)(s_bg_mode ? -d0 : d0));
    }
}

static void apply_ss5(int line, uint16_t d0, int speed, int n)   /* ApplyBGScroll_SS5 */
{
    static const int32_t k_speeds[111] = {
        0x3D000, 0x3E000, 0x3F000, 0x40000, 0x40000, 0x41000, 0x41000, 0x42000, 0x43000, 0x43000, 0x44000, 0x45000,
        0x45000, 0x46000, 0x46000, 0x47000, 0x47000, 0x48000, 0x49000, 0x49000, 0x4A000, 0x4A000, 0x4B000, 0x4B000,
        0x4C000, 0x4C000, 0x4D000, 0x4D000, 0x4E000, 0x4E000, 0x4F000, 0x4F000, 0x4F000, 0x50000, 0x50000, 0x51000,
        0x51000, 0x52000, 0x52000, 0x52000, 0x53000, 0x53000, 0x54000, 0x54000, 0x54000, 0x55000, 0x55000, 0x55000,
        0x56000, 0x56000, 0x56000, 0x57000, 0x57000, 0x57000, 0x58000, 0x58000, 0x58000, 0x59000, 0x59000, 0x59000,
        0x59000, 0x5A000, 0x5A000, 0x5A000, 0x5A000, 0x5B000, 0x5B000, 0x5B000, 0x5B000, 0x5C000, 0x5C000, 0x5C000,
        0x5C000, 0x5C000, 0x5D000, 0x5D000, 0x5D000, 0x5D000, 0x5D000, 0x5D000, 0x5E000, 0x5E000, 0x5E000, 0x5E000,
        0x5E000, 0x5E000, 0x5E000, 0x5F000, 0x5F000, 0x5F000, 0x5F000, 0x5F000, 0x5F000, 0x5F000, 0x5F000, 0x5F000,
        0x5F000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000, 0x60000,
        0x60000, 0x60000, 0x60000,
    };
    if (!s_bg_mode) return;
    for (int k = 0; k < n; k++) {
        int i = (line + k) * 2;
        s_bgh[i] = d0;
        s_bgf[i] = d0;
        if (s_bg_mode == 2) { s_bgh[i + 1] = (uint16_t)-0x80; s_bgf[i + 1] = 0; continue; }
        int32_t v = speed + k < 111 ? k_speeds[speed + k] : 0x60000;
        bg_set(i + 1, bg_get(i + 1) + (uint32_t)(s_bg_mode < 0 ? -v : v));
    }
}

static void apply_ss6(int line, uint16_t d0, int n)            /* ApplyBGScroll_SS6: the wave */
{
    static const int8_t k_wave[128] = {
        0x00, -0x05, -0x0A, -0x0F, -0x14, -0x19, -0x1E, -0x23, -0x28, -0x2C, -0x30, -0x34, -0x38, -0x3C, -0x40, -0x43,
        -0x46, -0x49, -0x4C, -0x4E, -0x50, -0x52, -0x54, -0x56, -0x58, -0x5A, -0x5C, -0x5D, -0x5E, -0x5D, -0x5C, -0x5A,
        -0x58, -0x55, -0x52, -0x50, -0x4E, -0x47, -0x40, -0x38, -0x30, -0x20, -0x10, 0x00, 0x10, 0x15, 0x1A, 0x1E,
        0x22, 0x23, 0x24, 0x25, 0x26, 0x25, 0x24, 0x22, 0x20, 0x1C, 0x18, 0x14, 0x10, 0x0A, 0x05, 0x00,
        -0x04, -0x09, -0x0D, -0x12, -0x16, -0x18, -0x1A, -0x1C, -0x1E, -0x1F, -0x20, -0x21, -0x22, -0x21, -0x20, -0x1F,
        -0x1E, -0x1C, -0x1B, -0x19, -0x18, -0x14, -0x10, -0x0C, -0x08, 0x00, 0x08, 0x0F, 0x16, 0x1E, 0x26, 0x2F,
        0x38, 0x40, 0x48, 0x50, 0x58, 0x5E, 0x64, 0x6A, 0x70, 0x73, 0x76, 0x78, 0x7A, 0x7B, 0x7C, 0x7D,
        0x7E, 0x7D, 0x7C, 0x7A, 0x78, 0x72, 0x6C, 0x66, 0x60, 0x56, 0x4C, 0x42, 0x38, 0x2A, 0x1C, 0x0E,
    };
    uint16_t d2 = s_ss5_shift;
    for (int k = 0; k < n; k++) {
        int i = (line + k) * 2;
        s_bgh[i] = d0;
        s_bgh[i + 1] = (uint16_t)(int16_t)k_wave[d2];
        d2 = (d2 + 1) & 0x7F;
    }
    s_ss5_shift = d2;
}

static void bg_scroll(int sect)                               /* BGScroll1 / 2 / 3 */
{
    if (s_sub_over || s_exit) return;
    int32_t d0, d1;
    switch (s_stage_id) {
    case 0:                                                   /* stage 1: the planets, the crystals */
        if (sect == 1) {
            d0 = 0x80000; d1 = 0;
            scroll_speed(&d0, &d1);
            s_scroll_off[1] += (uint32_t)d0;
            s_scroll_off[0] += (uint32_t)(d0 >> 1);
            if (!s_bg_mode) { if (++s_ss1_shift >= 0x200) { s_ss1_shift = 0x1FF; s_bg_mode = 1; } }
            else if ((int16_t)--s_ss1_shift < 0) { s_ss1_shift = 0; s_bg_mode = 0; }
            apply_ss1(0, 0x10000, 0x400, 0, 24);
        } else if (sect == 2)
            apply_ss1(24, 0xA000, 0x400, (uint16_t)(s_scroll_off[1] >> 16), 72);
        else
            apply_ss1(96, -0x8000, 0x400, (uint16_t)(s_scroll_off[0] >> 16), 32);
        break;
    case 4:                                                   /* stage 5: the buildings */
        if (sect == 1) {
            d0 = 0x40000; d1 = 0;
            scroll_speed(&d0, &d1);
            s_scroll_off[0] += (uint32_t)d0;
            s_bg_mode = 0;
            if (s_scroll_flags & 4) { s_bg_mode = 1; if (!(++s_ss5_shift & 0xF)) s_bg_mode = 2; s_ss5_shift &= 0xF; }
            if (s_scroll_flags & 8) { s_bg_mode = -1; if (!(--s_ss5_shift & 0xF)) s_bg_mode = 2; s_ss5_shift &= 0xF; }
            for (int l = 0; l < 17; l++) { s_bgh[l * 2] = s_bgh[l * 2 + 1] = 0; s_bgf[l * 2] = s_bgf[l * 2 + 1] = 0; }
            apply_ss5(17, 0, 0, 7);
        } else if (sect == 2)
            apply_ss5(24, (uint16_t)(s_scroll_off[0] >> 16), 7, 56);
        else
            apply_ss5(80, (uint16_t)(s_scroll_off[0] >> 16), 63, 48);
        break;
    case 5:                                                   /* stage 6: the wave */
        if (sect == 1) {
            d0 = 0x40000; d1 = 0;
            scroll_speed(&d0, &d1);
            s_scroll_off[0] += (uint32_t)d0;
            s_ss5_shift = (uint16_t)((s_ss5_shift + 1) & 0x7F);
            apply_ss6(0, 0, 24);
        } else if (sect == 2)
            apply_ss6(24, (uint16_t)(s_scroll_off[0] >> 16), 56);
        else
            apply_ss6(80, (uint16_t)(s_scroll_off[0] >> 16), 48);
        break;
    }
}

static void default_bg_scroll(void)                           /* DefaultBGScroll */
{
    int32_t d0 = 0x40000, d1 = 0;
    scroll_speed(&d0, &d1);
    s_scroll_off[2] += (uint32_t)d0;
    d0 >>= 1;
    s_scroll_off[1] += (uint32_t)d0;
    d0 >>= 1;
    s_scroll_off[0] += (uint32_t)d0;
}

static uint32_t s_hs_at;                                      /* (the auto-increment $20 writes: one per cell row) */
static void hs_rows(uint16_t v, int n) { for (int k = 0; k < n; k++, s_hs_at += 0x20) vram_w16(s_hs_at, v); }
#define OFF(i) ((uint16_t)(s_scroll_off[i] >> 16))

static void copy_hscroll(void)                                /* CopyHScroll */
{
    if (s_sub_over || s_exit) return;
    int32_t d0, d1;
    switch (s_stage_id) {
    case 0: case 4: case 5:                                   /* line scroll */
        for (int l = 0; l < 128; l++) { vram_w16(0xD000u + l * 4u, s_bgh[l * 2]); vram_w16(0xD002u + l * 4u, s_bgh[l * 2 + 1]); }
        break;
    case 1:                                                   /* stage 2: the clouds and the planet */
        default_bg_scroll();
        s_hs_at = 0xD060; hs_rows(OFF(1), 13);
        s_hs_at = 0xD002; hs_rows(OFF(2), 16);
        break;
    case 2:                                                   /* stage 3: the mountains */
        d0 = 0x80000; d1 = 0x10000;
        scroll_speed(&d0, &d1);
        for (int i = 0; i < 6; i++) { s_scroll_off[i] += (uint32_t)d0; d0 -= d1; }
        s_hs_at = 0xD060; hs_rows(OFF(4), 13);
        s_hs_at = 0xD002; hs_rows(0, 6); hs_rows(OFF(1), 3); hs_rows(OFF(2), 1); hs_rows(OFF(3), 4); hs_rows(OFF(5), 2);
        break;
    case 3:                                                   /* stage 4: the islands */
        default_bg_scroll();
        s_hs_at = 0xD060; hs_rows(OFF(1), 13);
        s_hs_at = 0xD002; hs_rows(OFF(0), 11); hs_rows(OFF(2), 5);
        break;
    case 6:                                                   /* stage 7: the city */
        d0 = 0x80000; d1 = 0x18000;
        scroll_speed(&d0, &d1);
        for (int i = 0; i < 4; i++) { s_scroll_off[i] += (uint32_t)d0; d0 -= d1; }
        s_hs_at = 0xD060; hs_rows(OFF(1), 11); hs_rows(OFF(3), 2);
        s_hs_at = 0xD002; hs_rows(OFF(0), 10); hs_rows(OFF(2), 6);
        break;
    }
}

static void copy_hscroll_sects(void) { if (!s_sub_over && !s_exit && !line_scroll()) copy_hscroll(); }

/* ---- the palette's cycles (PaletteCycle) -------------------------------------- */
static uint16_t inc_value(uint16_t *v, int step, int lo, int hi)   /* IncValue */
{
    uint16_t d = (uint16_t)(*v + step);
    if (d > (uint16_t)hi) d = (uint16_t)lo;
    return *v = d;
}

static void palette_cycle(void)
{
    static const uint16_t c2[8] = { 0xE00, 0xE60, 0xEA6, 0xECA, 0xE00, 0xE60, 0xEA6, 0xECA };
    static const uint16_t c3a[24] = { 0xE00, 0xE42, 0xE84, 0xEC4, 0xE42, 0xE00, 0xEC4, 0xE84, 0xE84, 0xE42, 0xE84, 0xE42,
                                      0xEC4, 0xE84, 0xE42, 0xE00, 0xE84, 0xEC4, 0xE00, 0xE42, 0xE42, 0xE84, 0xE42, 0xE84 };
    static const uint16_t c3b[6] = { 0xE00, 0xEC6, 0xE80, 0xE00, 0xE80, 0xEC6 };
    static const uint16_t c4a[16] = { 0xE20, 0xE42, 0xE84, 0xEC4, 0xEC4, 0xE20, 0xE42, 0xE84,
                                      0xE84, 0xEC4, 0xE20, 0xE42, 0xE42, 0xE84, 0xEC4, 0xE20 };
    static const uint16_t c4b[8] = { 0xEC6, 0xEE8, 0xEEA, 0xEEC, 0xEEE, 0xEEC, 0xEEA, 0xEE8 };
    static const uint16_t c5a[7] = { 0xE20, 0xE40, 0xE60, 0xE80, 0xEA0, 0xE80, 0xE60 };
    static const uint16_t c5b[26] = { 0x0EE, 0x0EC, 0x0EA, 0x0E8, 0x0E6, 0x0E4, 0x0E2, 0x2E0, 0x4E0, 0x6E0, 0x8E0, 0xAE0, 0xCE0,
                                      0xEE0, 0xCE0, 0xAE0, 0x8E0, 0x6E0, 0x4E0, 0x2E0, 0x0E2, 0x0E4, 0x0E6, 0x0E8, 0x0EA, 0x0EC };
    static const uint16_t c5c[26] = { 0xEE0, 0xCE0, 0xAE0, 0x8E0, 0x6E0, 0x4E0, 0x2E0, 0x0E2, 0x0E4, 0x0E6, 0x0E8, 0x0EA, 0x0EC,
                                      0x0EE, 0x0EC, 0x0EA, 0x0E8, 0x0E6, 0x0E4, 0x0E2, 0x2E0, 0x4E0, 0x6E0, 0x8E0, 0xAE0, 0xCE0 };
    static const uint16_t c5d[28] = { 0x00E, 0x00C, 0x00A, 0x008, 0x006, 0x004, 0x002, 0x000, 0x002, 0x004, 0x006, 0x008, 0x00A, 0x00C,
                                      0x00E, 0x00C, 0x00A, 0x008, 0x006, 0x004, 0x002, 0x000, 0x002, 0x004, 0x006, 0x008, 0x00A, 0x00C };
    static const uint16_t c6a[26] = { 0x04E, 0x08E, 0x0CE, 0x4EA, 0x8E8, 0xCE4, 0xEA4, 0xE68, 0xE0A, 0xE2E, 0xA0E, 0x60E, 0x20E,
                                      0x04E, 0x08E, 0x0CE, 0x4EA, 0x8E8, 0xCE4, 0xEA4, 0xE68, 0xE0A, 0xE2E, 0xA0E, 0x60E, 0x20E };
    static const uint16_t c6b[12] = { 0xE00, 0x204, 0xE20, 0x406, 0xE42, 0x204, 0xE64, 0x002, 0xE42, 0x000, 0xE20, 0x002 };
    static const uint16_t c7[12] = { 0xC20, 0xC20, 0xE40, 0xE40, 0xE60, 0xE60, 0xE80, 0xE80, 0xE60, 0xE60, 0xE40, 0xE40 };
    uint16_t d;
    switch (s_stage_id) {
    case 0: {                                                 /* stage 1: 16 colours of line 2, from a 64-row table */
        static const uint8_t k_cyc[63][16] = {
            { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 },
            { 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40 },
            { 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0 },
            { 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0 },
            { 0x00, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80 },
            { 0x00, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60 },
            { 0x00, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40 },
            { 0x00, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20 },
            { 0x00, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00 },
            { 0x00, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00 },
            { 0x00, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00 },
            { 0x00, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
            { 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04 },
            { 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x0C },
            { 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A },
            { 0x00, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A, 0x08 },
            { 0x00, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A, 0x08, 0x06 },
            { 0x00, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04 },
            { 0x00, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x0C, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02 },
            { 0x00, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00 },
            { 0x00, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00 },
            { 0x00, 0x0C, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00 },
            { 0x00, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x0C, 0x0A, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x0C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 },
            { 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40 },
            { 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0 },
            { 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0 },
            { 0x00, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80 },
            { 0x00, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60 },
            { 0x00, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40 },
            { 0x00, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20 },
            { 0x00, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00 },
            { 0x00, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00 },
            { 0x00, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00 },
            { 0x00, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0xA0, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xC0, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        };
        d = inc_value(&s_pc_index[0], 8, 0, 1000) & 0xFFF0;
        const uint8_t *row = k_cyc[(d >> 4) < 63 ? d >> 4 : 62];
        for (int k = 0; k < 16; k++) s_pal[0x20 + k] = row[k] < 0x10 ? row[k] : (uint16_t)(row[k] << 4);
        break;
    }
    case 1:
        d = s_pc_index[0];
        s_pc_index[0] = (uint16_t)((d + 2) & 7);
        s_pal[0x1A] = c2[d >> 1]; s_pal[0x1B] = c2[(d >> 1) + 1];
        s_pal[0x1C] = c2[(d >> 1) + 2]; s_pal[0x1D] = c2[(d >> 1) + 3];
        break;
    case 2:
        d = inc_value(&s_pc_index[0], 2, 0, 0x2E) & 0xFFF8;
        for (int k = 0; k < 4; k++) s_pal[0x1A + k] = c3a[(d >> 1) + k];
        if (!inc_value(&s_pc_timer, 1, 0, 4)) s_pal[0x21] = c3b[inc_value(&s_pc_index[1], 2, 0, 0xA) >> 1];
        break;
    case 3:
        d = s_pc_index[0];
        s_pc_index[0] = (uint16_t)(d + 2);
        d &= 0x18;
        for (int k = 0; k < 4; k++) s_pal[0x1A + k] = c4a[(d >> 1) + k];
        if (!inc_value(&s_pc_timer, 1, 0, 2)) { d = s_pc_index[1]; s_pc_index[1] = (uint16_t)(d + 2); s_pal[0x28] = c4b[(d & 0xE) >> 1]; }
        break;
    case 4:
        s_pal[0x1A] = c5a[inc_value(&s_pc_index[0], 2, 0, 0xC) >> 1];
        s_pal[0x2D] = c5b[inc_value(&s_pc_index[1], 2, 0, 0x32) >> 1];
        s_pal[0x2E] = c5c[(inc_value(&s_pc_index[2], 1, 0, 0x33) & 0xFFFE) >> 1];
        s_pal[0x2F] = c5d[(inc_value(&s_pc_index[3], 1, 0, 0x37) & 0xFFFE) >> 1];
        break;
    case 5:
        d = inc_value(&s_pc_index[0], 2, 0, 0x18);
        for (int k = 0; k < 13; k++) s_pal[0x21 + k] = c6a[(d >> 1) + k];
        d = inc_value(&s_pc_index[1], 2, 0, 0x16) & 0xFFFC;
        s_pal[0x1D] = c6b[d >> 1]; s_pal[0x1E] = c6b[(d >> 1) + 1];
        break;
    case 6:
        s_pal[0x1F] = c7[(inc_value(&s_pc_index[0], 1, 0, 0x17) & 0xFFFE) >> 1];
        break;
    default:
        return;
    }
    s_pal_dirty = 1;
}

static void load_time_stone_pal(void)                         /* LoadTimeStonePal: the stage won */
{
    static const uint16_t k_stone[8][5] = {
        { 0x040, 0xEEE, 0x0E8, 0x0A4, 0x062 }, { 0x040, 0xEEE, 0x06E, 0x04E, 0x008 }, { 0x040, 0xEEE, 0x2EE, 0x088, 0x024 },
        { 0x040, 0xEEE, 0xE66, 0xE44, 0xC22 }, { 0x040, 0xEEE, 0x8E4, 0xAA0, 0x640 }, { 0x040, 0xEEE, 0xE08, 0x804, 0x402 },
        { 0x040, 0xEEE, 0x00E, 0x008, 0x204 }, { 0x040, 0xEEE, 0x0E8, 0x0A4, 0x062 },
    };
    if (s_ufo_count || !s_ts_pal_delay || --s_ts_pal_delay) return;
    for (int k = 0; k < 5; k++) s_pal[0x31 + k] = k_stone[s_stage_id & 7][k];
    s_pal_dirty = 1;
}

/* ---- the HUD (DrawNumber: two tiles a digit, plane A's top rows) ---------------- */
static void draw_number(uint32_t top, uint32_t bottom, uint32_t v, int digits)
{
    static const uint16_t up[10] = { 0x87DE, 0x87DF, 0x87E0, 0x87E1, 0x87E2, 0x87E3, 0x87E4, 0x87E5, 0x87E6, 0x87DE };
    static const uint16_t lo[10] = { 0x87E9, 0x87EA, 0x87EB, 0x87EC, 0x87ED, 0x87EE, 0x87EF, 0x87F0, 0x87F1, 0x87F2 };
    uint32_t div = 1;
    for (int k = 1; k < digits; k++) div *= 10;
    for (int k = 0; k < digits; k++, div /= 10, top += 2, bottom += 2) {
        uint32_t d = v / div;
        v -= d * div;
        if (d > 9) d = 9;
        vram_w16(top, up[d]);
        vram_w16(bottom, lo[d]);
    }
}
static void draw_hud_counts(void)
{
    draw_number(0xC0B6, 0xC136, s_rings, 3);                  /* DrawRingCount */
    draw_number(0xC08A, 0xC10A, s_ufo_count, 2);              /* DrawUFOCount */
}
static void draw_timer(void) { draw_number(0xC0A0, 0xC120, s_timer, 3); }

/* ---- fades (FadeToWhite / FadeFromWhite: a channel at a time, 8 steps each) ---- */
static void fade_to_white_step(int step)                      /* step 0..23 */
{
    int sh = (step / 8) * 4;                                  /* red, green, blue */
    for (int i = 0; i < 64; i++) {
        int c = (s_pal[i] >> sh) & 0xE;
        c = c + 2 > 0xE ? 0xE : c + 2;
        s_pal[i] = (uint16_t)((s_pal[i] & ~(0xE << sh)) | c << sh);
    }
    s_pal_dirty = 1;
}

static void fade_from_white_step(int step)
{
    int sh = (step / 8) * 4, lvl = 0xE - (step % 8) * 2;
    for (int i = 0; i < 64; i++) {
        int c = (s_fade_pal[i] >> sh) & 0xE;
        if (c < lvl) c = lvl;
        s_pal[i] = (uint16_t)((s_pal[i] & ~(0xE << sh)) | c << sh);
    }
    s_pal_dirty = 1;
}

static void prep_fade_from_white(void)
{
    memcpy(s_fade_pal, s_pal, sizeof s_pal);
    for (int i = 0; i < 64; i++) s_pal[i] = 0xEEE;
    s_pal_dirty = 1;
}

/* ---- the results (Results) ---------------------------------------------------- */
static void draw_results_num(uint32_t top, uint32_t bottom, uint32_t v)   /* DrawResultsNum: eight digits, x10 */
{
    static const uint16_t up[10] = { 0x402D, 0x402F, 0x4031, 0x4033, 0x4034, 0x4036, 0x4038, 0x4039, 0x403A, 0x403B };
    static const uint16_t lo[10] = { 0x402E, 0x4030, 0x4032, 0x402E, 0x4035, 0x4037, 0x402E, 0x4030, 0x402E, 0x402E };
    v *= 10;
    int lead = 0;
    uint32_t div = 10000000;
    for (int k = 0; k < 8; k++, div /= 10, top += 2, bottom += 2) {
        uint32_t d = v / div;
        v -= d * div;
        if (d > 9) d = 9;
        if (!lead && !d && k < 7) { vram_w16(top, 0); vram_w16(bottom, 0); continue; }
        lead = 1;
        vram_w16(top, up[d]);
        vram_w16(bottom, lo[d]);
    }
}

static void add_score(uint32_t n)                             /* AddScore: S3K's score (its tens), extra lives */
{
    uint32_t s = r32(0xFFFE26u) + n;
    while (s >= r32(0xFFFFC0u)) {                             /* (Next_extra_life) */
        w32(0xFFFFC0u, r32(0xFFFFC0u) + 5000);
        if (r8(RAM_Life_count) < 99) w8(RAM_Life_count, (uint8_t)(r8(RAM_Life_count) + 1));
    }
    if (s > 999999) s = 999999;
    w32(0xFFFE26u, s);
}

static void results_setup(void)                               /* Results: the H40 tally screen */
{
    set_reg(0, 0x04); set_reg(2, 0x30); set_reg(3, 0); set_reg(4, 7); set_reg(5, 0xF000 >> 9);
    set_reg(7, 0); set_reg(10, 0); set_reg(11, 0); set_reg(12, 0x81); set_reg(13, 0xF400 >> 10);
    set_reg(15, 2); set_reg(16, 1);
    set_reg(1, (uint8_t)(g_machine.vdp.reg[1] & ~0x40));
    memset(vram(), 0, 0x10000);
    w32(RAM_V_scroll_value, 0x00100000u);
    memset(s_pal, 0, sizeof s_pal);
    load_pal(label(SSL_PAL_RESULTS), 0, 64);
    load_art_item(IT_RESART);
    uint8_t stones = s_time_stones;
    draw_tilemaps(!s_time_bonus ? 0x3D : stones == 0x7F ? 0x3F : 0x3E);   /* SPECIAL STAGE / TIME STONES / GOT THEM ALL!! */
    for (int k = 0; k < 7; k++)
        if (stones & (1u << k)) draw_tilemaps((uint32_t)(0x29 + k));
    draw_tilemaps(0x33323130u);
    draw_tilemaps(0x363534u);
    prep_fade_from_white();
    set_reg(1, (uint8_t)(g_machine.vdp.reg[1] | 0x40));
    s_results_timer = 8 * 60;
}

static void results_draw(void)                                /* VInt_Results */
{
    draw_results_num(0xC730, 0xC7B0, r32(0xFFFE26u));
    draw_results_num(0xCA30, 0xCAB0, s_time_bonus);
    draw_results_num(0xC8B0, 0xC930, s_ring_bonus);
}

/* ---- the stage's set-up (Start) -------------------------------------------------- */
static void stage_setup(void)
{
    TRACE("[scdss] setup\n");
    memcpy(s_saved_regs, g_machine.vdp.reg, sizeof s_saved_regs);
    static const uint8_t k_regs[19] = { 0x04, 0x34, 0x30, 0, 7, 0x6A, 0, 0x20, 0, 0, 0, 0x02, 0x00, 0x34, 0, 2, 1, 0, 0 };
    for (int r = 0; r < 19; r++) set_reg(r, k_regs[r]);
    memset(vram(), 0, 0x10000);
    w32(RAM_V_scroll_value, 0);
    for (uint32_t a = 0; a < 0x280; a += 2) w16(RAM_Sprite_table + a, 0);
    memset(s_pal, 0, sizeof s_pal);
    load_pal(label(SSL_PAL_STAGE), 0, 64);
    TRACE("[scdss] art\n");
    load_art_item(IT_ART);                                    /* time stone, UFOs, title card, splash, shadow, items, explosion, HUD */
    TRACE("[scdss] stagemap\n");
    draw_stage_map();
    /* LoadStageData */
    if (line_scroll()) set_reg(11, 0x03);
    TRACE("[scdss] bgart\n");
    load_art_item(IT_BGART + s_stage_id);
    {
        uint32_t len;
        const uint8_t *t = item(IT_MAPS + s_stage_id, &len);
        for (int k = 0, n = be16(t); k < n; k++) draw_tilemap_in(IT_MAPS + s_stage_id, t[2 + k * 12]);
    }
    TRACE("[scdss] maps\n");
    draw_tilemaps(0x12);                                      /* DrawHUDBase */
    memset(s_bgh, 0, sizeof s_bgh);
    memset(s_bgf, 0, sizeof s_bgf);
    if (s_stage_id == 4) for (int l = 0; l < 128; l++) s_bgh[l * 2 + 1] = 0xFF80;   /* (InitBGLineHScroll: $FF80) */
    load_pal(label(SSL_STAGEPAL + s_stage_id), 0x10, 0x20);
    for (int l = 0; l < 128; l++) { s_stage_hs[0][l] = 0; s_stage_hs[1][l] = 0x100; }
    memset(s_scroll_off, 0, sizeof s_scroll_off);
    s_ss1_shift = 0x100;
    s_ss5_shift = 0;
    s_bg_mode = 0;
    memset(s_pc_index, 0, sizeof s_pc_index);
    s_pc_timer = 0;
    s_ts_pal_delay = 20;
    s_sub_over = s_exit = s_paused = 0;
    s_pause_ok = 0;
    s_ctrl_held = (uint8_t)r8(RAM_Ctrl_1_held);
    s_ctrl_tapped = 0;
    TRACE("[scdss] subinit\n");
    sub_init();
}

static void get_sub_data(void)                                /* GetSubCPUData */
{
    memcpy(s_stage_image, s_image, sizeof s_stage_image);
    memcpy(s_sprites, s_sub_sprites, sizeof s_sprites);
    s_scroll_flags = s_sub_scroll_flags;
    s_sub_scroll_flags = 0;
    memcpy(s_sonic_art, s_sub_sonic_art, sizeof s_sonic_art);
    flush_sounds();
}

static void update_stage(void)                                /* UpdateStage: the Sub CPU's turn */
{
    if (s_sub_over) return;
    s_ctrl_hold = s_ctrl_held;
    s_ctrl_tap = s_ctrl_tapped;
    if (sub_update()) s_sub_over = 1;
}

/* V-int routines 0-7 */
static void vint(int r)
{
    cram_flush();
    switch (r) {
    case 0: case 2:                                           /* (show one half, fill the other) */
        show_stage_buf(r == 0 ? 1 : 0);
        copy_hscroll();
        copy_image(r == 0 ? 0 : 1, 0);
        read_controller();
        break;
    case 1: case 3:
        copy_hscroll_sects();
        copy_image(r == 1 ? 0 : 1, 1);
        copy_sprites();
        palette_cycle();
        break;
    case 4: copy_hscroll_sects(); break;
    case 5: results_draw(); read_controller(); break;
    case 7: read_controller(); break;
    default: break;
    }
    cram_flush();
    if (s_results_timer) s_results_timer--;
}

/* ---- the scene ------------------------------------------------------------------- */
enum { P_FADE_OUT, P_SETUP, P_FADE_IN, P_LOOP, P_PAUSED, P_EXIT_FADE, P_RES_FADE_IN, P_RESULTS, P_RES_FADE_OUT, P_DONE };

static void scdss_finish(void)
{
    s_active = 0;
    memcpy(g_machine.vdp.reg, s_saved_regs, sizeof s_saved_regs);
    for (uint32_t a = 0; a < 0x380; a += 2) w16(RAM_H_scroll_buffer + a, 0);
    for (uint32_t a = 0; a < 0x280; a += 2) w16(RAM_Sprite_table + a, 0);
    for (int i = 0; i < 64; i++) w16(RAM_Normal_palette + (uint32_t)i * 2u, 0);
    memset(vram(), 0, 0x10000);
    w32(RAM_V_scroll_value, 0);
    scd_cd_stop();
    int got = (s_time_stones >> s_stage_id) & 1;
    fprintf(stderr, "[trilogy] CD special stage %d over: %s (rings %d, %u s left)\n", s_stage_id + 1,
            got ? "time stone" : "no time stone", s_rings, (unsigned)s_timer);
    if (s_done_cb) s_done_cb(got && !s_had_stone);
}

static void scdss_frame(void)
{
    {
        static int log = -1;                                  /* development: per-update state */
        if (log < 0) log = getenv("TRILOGY_SCDSSLOG") != NULL;
        if (log && s_phase == P_LOOP && !s_step) {
            uint32_t so = OA(O_SONIC);
            fprintf(stderr, "[scdss] st %d x %04X y %04X z %04X sy %04X yaw %03X spd %04X rt %02X ufo %d rings %d t %u stamp %d\n",
                    s_stage_id, W(so, oX), W(so, oY), W(so, oZ), W(so, oSprY), W(so, oPlayerYaw) & 0x1FF, W(so, oPlayerSpeed), B(so, oRoutine),
                    s_ufo_count, s_rings, (unsigned)s_timer, B(so, oPlayerStampC));
        }
    }
    if (s_dbg < 0) s_dbg = getenv("TRILOGY_SCDSSLOG") != NULL;
    TRACE("[scdss] phase %d step %d\n", s_phase, s_step);
    tri_jsr(POLL_CONTROLLERS);
    int v = -1;
    switch (s_phase) {
    case P_FADE_OUT:                                          /* the level to white */
        for (int i = 0; i < 64; i++) {
            uint16_t c = r16(RAM_Normal_palette + (uint32_t)i * 2u);
            for (int sh = 1; sh < 12; sh += 4) if (((c >> sh) & 7) < 7) c = (uint16_t)(c + (1 << sh));
            w16(RAM_Normal_palette + (uint32_t)i * 2u, c);
        }
        if (++s_fade >= 8) { s_phase = P_SETUP; s_fade = 0; }
        w8(RAM_V_int_routine, 6);                             /* (the level's own V-int, palette and all) */
        tri_jsr(S3K_Wait_VSync);
        return;
    case P_SETUP:
        stage_setup();
        update_stage();
        get_sub_data();
        prep_fade_from_white();
        set_reg(1, (uint8_t)(g_machine.vdp.reg[1] | 0x40));
        scd_audio_init();
        if (scd_disc_open()) scd_cd_play(SCD_CD_SPECIAL, 1);
        s_phase = P_FADE_IN;
        s_fade = 0;
        v = 0;
        break;
    case P_FADE_IN:                                           /* FadeFromWhite (V-int 0 meanwhile) */
        fade_from_white_step(s_fade);
        v = 0;
        if (++s_fade >= 24) { s_phase = P_LOOP; s_step = 0; s_pause_ok = 1; }
        break;
    case P_LOOP: {
        int half = s_step >= 3;
        switch (s_step % 3) {
        case 0:
            if (s_paused) { s_phase = P_PAUSED; v = 7; break; }
            update_stage();
            bg_scroll(1);
            v = half ? 2 : 0;
            break;
        case 1:
            draw_hud_counts();
            load_time_stone_pal();
            bg_scroll(2);
            v = half ? 3 : 1;
            break;
        case 2:
            draw_timer();
            bg_scroll(3);
            v = 4;
            break;
        }
        if (s_phase != P_LOOP) break;
        if (s_step % 3 == 2) {
            if (s_sub_over) {                                 /* .Exit: to the results */
                s_exit = 1;
                s_pause_ok = 0;
                s_ring_bonus = (uint32_t)s_rings * 20u;
                s_time_bonus = s_timer * 20u;
                s_phase = P_EXIT_FADE;
                s_fade = 0;
                scd_cd_fade();
            } else
                get_sub_data();
        }
        s_step = (s_step + 1) % 6;
        break;
    }
    case P_PAUSED:
        v = 7;
        if (!s_paused) { s_phase = P_LOOP; v = -1; }
        break;
    case P_EXIT_FADE:                                         /* FadeToWhite (V-int 6) */
        fade_to_white_step(s_fade);
        v = 6;
        if (++s_fade >= 24) { results_setup(); s_phase = P_RES_FADE_IN; s_fade = 0; }
        break;
    case P_RES_FADE_IN:
        fade_from_white_step(s_fade);
        v = 5;
        if (++s_fade >= 24) { s_phase = P_RESULTS; s_score_tallied = 0; }
        break;
    case P_RESULTS:
        if (s_ring_bonus) { fm(0xBD); add_score(20); s_ring_bonus -= 20; }
        else if (s_time_bonus) { s_time_bonus -= 20; fm(0xBD); add_score(20); }
        v = 5;
        if (!s_score_tallied && !s_ring_bonus && !s_time_bonus) { s_score_tallied = 1; fm(0x9A); }
        if ((s_ctrl_tapped & 0x80) || !s_results_timer) {
            add_score(s_ring_bonus + s_time_bonus);
            s_ring_bonus = s_time_bonus = 0;
            fm(0xC8);
            s_phase = P_RES_FADE_OUT;
            s_fade = 0;
        }
        break;
    case P_RES_FADE_OUT:
        fade_to_white_step(s_fade);
        v = 6;
        if (++s_fade >= 24) s_phase = P_DONE;
        break;
    default:
        scdss_finish();
        return;
    }
    if (v >= 0) vint(v);
    w8(RAM_V_int_routine, V_INT_QUIET);
    tri_jsr(S3K_Wait_VSync);
}

/* ---- the trilogy's side ------------------------------------------------------------ */
/* stage: 0-6 (the emerald slot it plays for); stones: the emeralds held, as
 * time stones on the results; done(got): a stone won this time */
void tri_scdss_start(int stage, uint8_t stones, void (*done)(int got))
{
    if (!tri_scdss_available()) {                             /* (no scdss.bin: nothing to play) */
        fprintf(stderr, "[trilogy] CD special stage: scdss.bin missing\n");
        if (done) done(0);
        return;
    }
    s_stage_id = (uint8_t)(stage < 0 || stage > 7 ? 0 : stage);
    s_time_stones = stones;
    s_had_stone = (stones >> s_stage_id) & 1;
    s_done_cb = done;
    s_flags = 0;
    s_active = 1;
    s_phase = P_FADE_OUT;
    s_fade = 0;
    s_results_timer = 0;
    tri_scd_sound_set(1);                                     /* (Sonic CD's sounds, whatever the zone) */
    o_sfx_cd(0xC8);                                           /* FM_SSWARP */
    D(0) = 0xE1;                                              /* (S3K's music fades) */
    tri_jsr(S3K_Play_Music);
    w8(RAM_Game_mode, GM_SCENE);
    fprintf(stderr, "[trilogy] CD special stage %d\n", s_stage_id + 1);
}

int tri_scdss_active(void) { return s_active; }

/* the final picture's rows: the stage is the native width, centred; the
 * renderer's wider canvas would show the planes' other columns beside it */
void tri_scdss_overlay(int line,uint32_t *out,int width)
{
    (void)line;
    if(!s_active || s_phase<=P_SETUP)return;
    int native_width=(g_machine.vdp.reg[12]&0x81)?320:256;
    int left=(width-native_width)/2,right=left+native_width;
    for(int x=0;x<left && x<width;x++)out[x]=0xFF000000u;
    for(int x=right<0?0:right;x<width;x++)out[x]=0xFF000000u;
}

int tri_scdss_hook(uint32_t pc)
{
    if (pc != TH_GAMELOOP || !s_active) return 0;
    if (r8(RAM_Game_mode) != GM_SCENE) { s_active = 0; return 0; }
    A(0) = tri_native(scdss_frame);
    return 0;
}
