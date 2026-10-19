/*
 * trilogy_scdwarp.c — Sonic CD's time warp cutscene (WARP__.MMD; dobby: Time
 * Warp Cutscene), between a level and its other period (scd_time.c).
 *
 * Sonic CD's level fades to white and hands over to this program: 90 frames
 * of white while the music dies away, then (with its whoosh, FM $D8: Sonic
 * CD's sound set, trilogy_music.c, is in for the scene) it fades in a
 * scrolling tunnel (plane B, 4 px a frame up) whose greens cycle, Sonic rising
 * into it with three afterimages, sparkles falling from him, for 240 frames
 * (Start cuts it short), and fades to white again; the level in the new period
 * fades in from that white, with no title card. Here it runs as a native scene
 * off GameLoop (as trilogy_scdss.c's stages): its own VRAM layout (plane B
 * $E000, sprites $F000), its data from scdss.bin (items 46-50,
 * scd_special.py). The program's own bug is kept: its "behind Sonic" sparkle
 * loop starts at the front sparkles, so those run twice and the last three
 * slots never do.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "trilogy_obj.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t *tri_scdss_item(int i, uint32_t *len);             /* trilogy_scdss.c */
enum { IT_WARP_ART = 46, IT_WARP_BG, IT_WARP_PAL, IT_WARP_SONIC, IT_WARP_SPARKLE, IT_WARP_END };

#define GM_SCENE        0x10
#define GM_LEVEL        0x0C
#define V_INT_QUIET     0x14
#define POLL_CONTROLLERS 0x0010DEu
#define S3K_Pal_ToWhite 0x003D50u

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint8_t *vram(void) { return g_machine.vdp.vram; }
static void set_reg(int r, uint8_t v) { g_machine.vdp.reg[r] = v; }

/* ---- sprites: each object type's animations (scdss.bin: u8 animations; u8
 * frames, u8 speed, frames x (u8 pieces, pieces x 5 bytes)) ---------------- */
typedef struct { int frames, speed; const uint8_t *frame[16]; } Anim;
static Anim s_anim[2];                                    /* 0 Sonic, 1 sparkle */

static int parse_anim(int it, Anim *a)
{
    uint32_t len;
    const uint8_t *p = tri_scdss_item(it, &len);
    if (!p || len < 3 || !p[0]) return 0;
    a->frames = p[1];
    a->speed = p[2];
    if (a->frames > 16) return 0;
    const uint8_t *q = p + 3;
    for (int f = 0; f < a->frames; f++) {
        a->frame[f] = q;
        q += 1 + q[0] * 5;
        if (q > p + len) return 0;
    }
    return 1;
}

/* ---- objects (the program's: a word id, flags, routine, tile, an animation,
 * a timer, 16.16 place and speed) ----------------------------------------- */
typedef struct {
    uint16_t id; uint8_t flags, routine;
    uint16_t tile; int type;
    uint8_t anim, frame, time, time2;
    uint16_t timer;
    int32_t x, y, xv, yv;
} Obj;
enum { N_FRONT = 3, N_BACK = 25, N_SPARKLES = N_FRONT + N_BACK };
static Obj s_sonic[4];                                    /* Sonic, then his three afterimages */
static Obj s_spark[N_SPARKLES];                           /* three in front of Sonic, then behind */
static uint8_t s_sprites[80 * 8];
static int s_nspr;
static uint32_t s_seed;
static uint16_t s_vint, s_trail_next, s_vscroll, s_timer, s_pc_timer, s_pc_index;

static uint32_t rnd(void)                                 /* Random */
{
    uint32_t d1 = s_seed ? s_seed : 0x2A6D365Au, d0 = d1;
    d1 = (d1 << 2) + d0;
    d1 = (d1 << 3) + d0;
    d0 = (d0 & 0xFFFF0000u) | (d1 & 0xFFFFu);
    d1 = d1 >> 16 | d1 << 16;
    d0 = (d0 & 0xFFFF0000u) | ((d0 + d1) & 0xFFFFu);
    d1 = (d1 & 0xFFFF0000u) | (d0 & 0xFFFFu);
    s_seed = d1 >> 16 | d1 << 16;
    return d0;
}

static void set_anim(Obj *o, int a)                       /* SetObjAnim */
{
    o->anim = (uint8_t)a;
    o->frame = 0;
    o->time = o->time2 = (uint8_t)s_anim[o->type].speed;
}

static void draw(Obj *o)                                  /* DrawObject */
{
    const Anim *a = &s_anim[o->type];
    if (!(o->flags & 2) && (int8_t)--o->time < 0) {
        o->time = (uint8_t)a->speed;
        if (++o->frame >= a->frames) o->frame = 0;
    }
    if (o->flags & 4) return;
    const uint8_t *f = a->frame[o->frame < a->frames ? o->frame : 0];
    int x = (int16_t)(o->x >> 16), y = (int16_t)(o->y >> 16);
    for (int k = 0; k < f[0]; k++) {
        if (s_nspr >= 0x50) return;
        const uint8_t *pc = f + 1 + k * 5;
        uint8_t *s = s_sprites + s_nspr * 8;
        uint16_t sy = (uint16_t)((int8_t)pc[0] + y), tile = (uint16_t)(be16(pc + 2) + o->tile);
        uint16_t sx = (uint16_t)(((int8_t)pc[4] + x) & 0x1FF);
        if (!sx) sx = 1;
        s_nspr++;
        s[0] = (uint8_t)(sy >> 8); s[1] = (uint8_t)sy;
        s[2] = pc[1]; s[3] = (uint8_t)s_nspr;
        s[4] = (uint8_t)(tile >> 8); s[5] = (uint8_t)tile;
        s[6] = (uint8_t)(sx >> 8); s[7] = (uint8_t)sx;
    }
}

static void sonic(Obj *o)                                 /* ObjSonic */
{
    switch (o->routine) {
    case 0:
        o->tile = 0x8000;
        o->type = 0;
        o->x = (160 + 128) << 16;
        o->y = (288 + 128) << 16;
        set_anim(o, 0);
        o->routine = 1;
        /* fall through */
    case 1:
        o->y -= 8 << 16;
        if ((o->y >> 16) == 120 + 128) { o->routine = 2; o->timer = 120 << 8; }
        break;
    case 2:
        o->timer = (uint16_t)(o->timer - 0x100);           /* (subq.b on the word's high byte) */
        if (!(o->timer >> 8)) o->routine = 3;
        break;
    case 3:
        o->y -= 8 << 16;
        if ((o->y >> 16) == -32 + 128) o->routine = 4;
        break;
    default:
        break;
    }
}

static void sparkle(Obj *o)                               /* ObjSparkle */
{
    if (!o->routine) {
        o->tile = 0;
        o->type = 1;
        set_anim(o, 0);
        o->routine = 1;
        uint32_t d0 = rnd();
        o->yv = (int32_t)(d0 & 0x3FFFFu);
        o->x = (int32_t)((uint32_t)(uint16_t)((s_sonic[0].x >> 16) - 0x10 + (d0 & 0x1F)) << 16 | (o->x & 0xFFFF));
        o->y = (int32_t)((uint32_t)(uint16_t)((s_sonic[0].y >> 16) - 0x18) << 16 | (o->y & 0xFFFF));
        o->timer = 45;
    }
    o->x += o->xv;
    o->y += o->yv;
    if (!--o->timer) o->flags |= 1;
}

static void run_sparkle(Obj *o)                           /* RunSparkleObject */
{
    if (!o->id) return;
    sparkle(o);
    if (o->flags & 1) memset(o, 0, sizeof *o);
    else draw(o);
}

static void run_objects(void)                             /* RunObjects */
{
    if (!(s_vint & 3)) {
        s_sonic[s_trail_next].id = 1;
        s_trail_next = (uint16_t)((s_trail_next + 1) & 3);
        for (int k = 0; k < N_SPARKLES; k++)
            if (!s_spark[k].id) { s_spark[k].id = 2; break; }
    }
    s_nspr = 0;
    for (int k = 0; k < N_FRONT; k++) run_sparkle(&s_spark[k]);
    if (s_sonic[0].id) { sonic(&s_sonic[0]); draw(&s_sonic[0]); }
    for (int k = 1; k < 4; k++) {
        Obj *t = &s_sonic[k];
        if (!t->id) continue;
        sonic(t);
        t->anim = s_sonic[0].anim; t->frame = s_sonic[0].frame;   /* (sync animation) */
        t->time = s_sonic[0].time; t->time2 = s_sonic[0].time2;
        draw(t);
    }
    for (int k = 0; k < N_BACK; k++) run_sparkle(&s_spark[k]);   /* (sic: from the front ones) */
    if (s_nspr < 0x50) memset(s_sprites + s_nspr * 8, 0, 4);       /* (the last slot: ends the list) */
}

/* ---- palette ---------------------------------------------------------------- */
static uint16_t s_pal[64], s_fade_pal[64];
static int s_pal_dirty;

static void palette_cycle(void)                           /* PaletteCycle: the tunnel's greens */
{
    static const uint16_t cyc[28] = {
        0x0E0, 0x2E2, 0x4E4, 0x6E6, 0x8E8, 0xAEA, 0xCEC, 0xEEE, 0xCEC, 0xAEA, 0x8E8, 0x6E6, 0x4E4, 0x2E2,
        0x0E0, 0x2E2, 0x4E4, 0x6E6, 0x8E8, 0xAEA, 0xCEC, 0xEEE, 0xCEC, 0xAEA, 0x8E8, 0x6E6, 0x4E4, 0x2E2 };
    if (++s_pc_timer & 1) return;
    uint16_t i = s_pc_index++;
    if (s_pc_index >= 0xE) s_pc_index = 0;
    for (int k = 0; k < 14; k++) s_pal[0x11 + k] = cyc[i + k];
    s_pal_dirty = 1;
}

static void fade_to_white_step(int step)                  /* FadeToWhite: red, green, blue, 8 frames each */
{
    int sh = (step / 8) * 4;
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

/* ---- the frame ------------------------------------------------------------- */
enum { P_LEVEL_WHITE, P_SETUP, P_HOLD, P_FADE_IN, P_LOOP, P_FADE_OUT, P_DONE };
static int s_active, s_phase, s_step;
static uint8_t s_saved_regs[24];
static void (*s_done)(void);

static void vint(void)                                    /* VInterrupt: CRAM, plane B's scroll, sprites, timers */
{
    if (s_pal_dirty) {
        s_pal_dirty = 0;
        for (int i = 0; i < 64; i++) {
            uint16_t c = s_pal[i] & 0x0EEE;
            g_machine.vdp.cram[i] = c;
            if (g_machine.vdp.colour_updated) g_machine.vdp.colour_updated(g_machine.vdp.colour_updated_user, (unsigned)i, c);
        }
    }
    g_machine.vdp.vsram[1] = s_vscroll;
    s_vscroll = (uint16_t)(s_vscroll - 4);
    memcpy(vram() + 0xF000, s_sprites, sizeof s_sprites);
    if (s_timer) s_timer--;
    s_vint++;
}

static void setup(void)                                   /* Start */
{
    memcpy(s_saved_regs, g_machine.vdp.reg, sizeof s_saved_regs);
    static const uint8_t k_regs[19] = { 0x04, 0x34, 0xC000 / 0x400, 0, 0xE000 / 0x2000, 0xF000 / 0x200, 0, 0, 0, 0, 0,
                                        0x00, 0x81, 0xF400 / 0x400, 0, 2, 0x01, 0, 0 };
    for (int r = 0; r < 19; r++) set_reg(r, k_regs[r]);
    set_reg(1, (uint8_t)(k_regs[1] | 0x40));                 /* (on, all white: the program's backdrop, display off) */
    memset(vram(), 0, 0x10000);
    memset(g_machine.vdp.vsram, 0, sizeof g_machine.vdp.vsram);
    const uint8_t *art = tri_scdss_item(IT_WARP_ART, NULL);   /* (u16 count, (u16 VRAM, u16 tiles), tiles) */
    for (int k = 0, n = be16(art); k < n; k++) {
        uint16_t va = be16(art + 2 + k * 4), cnt = be16(art + 4 + k * 4);
        memcpy(vram() + va, art + 2 + n * 4, (size_t)cnt * 32u);
    }
    /* 40 x 28, and its top 4 rows again below; the plane's other 24 columns
     * carry it on past either edge for a wider screen (the plane wraps at 64:
     * columns 40-51 its first 12 again, 52-63 its last 12, the seam 96 px out) */
    const uint8_t *bg = tri_scdss_item(IT_WARP_BG, NULL);
    for (int r = 0; r < 32; r++)
        for (int c = 0; c < 64; c++) {
            int pc = c < 40 ? c : c < 52 ? c - 40 : c - 24;
            const uint8_t *w = bg + ((r % 28) * 40 + pc) * 2;
            uint32_t a = 0xE000u + (uint32_t)r * 0x80u + (uint32_t)c * 2u;
            vram()[a] = w[0];
            vram()[a + 1] = w[1];
        }
    uint32_t pl;
    const uint8_t *pal = tri_scdss_item(IT_WARP_PAL, &pl);
    memset(s_fade_pal, 0, sizeof s_fade_pal);
    for (uint32_t i = 0; i < pl / 2 && i < 64; i++) s_fade_pal[i] = be16(pal + i * 2);
    for (int i = 0; i < 64; i++) s_pal[i] = 0xEEE;             /* PrepFadeFromWhite */
    s_pal_dirty = 1;
    memset(s_sonic, 0, sizeof s_sonic);
    memset(s_spark, 0, sizeof s_spark);
    memset(s_sprites, 0, sizeof s_sprites);
    s_seed = 0;
    s_vint = s_trail_next = s_vscroll = 0;
    s_pc_timer = 0xFFFF;
    s_pc_index = 0;
}

static void finish(void)
{
    s_active = 0;
    memcpy(g_machine.vdp.reg, s_saved_regs, sizeof s_saved_regs);
    memset(vram(), 0, 0x10000);
    memset(g_machine.vdp.vsram, 0, sizeof g_machine.vdp.vsram);
    for (uint32_t a = 0; a < 0x380; a += 2) w16(RAM_H_scroll_buffer + a, 0);
    for (uint32_t a = 0; a < 0x280; a += 2) w16(RAM_Sprite_table + a, 0);
    for (int i = 0; i < 64; i++) {                            /* (white, as the level will fade in from) */
        w16(RAM_Normal_palette + (uint32_t)i * 2u, 0xEEE);
        g_machine.vdp.cram[i] = 0xEEE;
        if (g_machine.vdp.colour_updated) g_machine.vdp.colour_updated(g_machine.vdp.colour_updated_user, (unsigned)i, 0xEEE);
    }
    w32(RAM_V_scroll_value, 0);
    fprintf(stderr, "[trilogy] CD time warp over\n");
    if (s_done) s_done();
}

static void warp_frame(void)
{
    if (getenv("TRILOGY_WARPLOG")) {                          /* development */
        extern uint64_t g_frame_count;
        fprintf(stderr, "[warp] frame %llu phase %d step %d\n", (unsigned long long)g_frame_count, s_phase, s_step);
    }
    tri_jsr(POLL_CONTROLLERS);
    switch (s_phase) {
    case P_LEVEL_WHITE:                                       /* (the level's palette to white, the level frozen) */
        w16(RAM_Palette_fade_info, 0x003F);
        tri_jsr(S3K_Pal_ToWhite);
        w8(RAM_V_int_routine, 6);
        tri_jsr(S3K_Wait_VSync);
        if (++s_step >= 8) { s_phase = P_SETUP; s_step = 0; }
        return;
    case P_SETUP:
        setup();
        s_phase = P_HOLD;
        s_step = 0;
        break;
    case P_HOLD:                                              /* (90 frames: the music fades out) */
        if (++s_step >= 90) {
            s_timer = 240;
            o_sfx_cd(0xD8);                                   /* (its whoosh: FM $D8) */
            s_phase = P_FADE_IN;
            s_step = 0;
        }
        break;
    case P_FADE_IN:
        fade_from_white_step(s_step);
        if (++s_step >= 24) s_phase = P_LOOP;
        break;
    case P_LOOP:
        palette_cycle();
        run_objects();
        if ((r8(RAM_Ctrl_1_pressed) & 0x80) || !s_timer) { s_phase = P_FADE_OUT; s_step = 0; }
        break;
    case P_FADE_OUT:
        fade_to_white_step(s_step);
        if (++s_step >= 24) s_phase = P_DONE;
        break;
    default:
        finish();
        return;
    }
    vint();
    w8(RAM_V_int_routine, V_INT_QUIET);
    tri_jsr(S3K_Wait_VSync);
}

/* ---- the trilogy's side -------------------------------------------------------- */
int tri_scdwarp_available(void) { return tri_scdss_item(IT_WARP_END - 1, NULL) != NULL; }

/* From a level (frozen where it is): to white, the cutscene, then done(), which
 * sets the level up again in the new period */
void tri_scdwarp_start(void (*done)(void))
{
    s_done = done;
    if (!tri_scdwarp_available() || !parse_anim(IT_WARP_SONIC, &s_anim[0]) || !parse_anim(IT_WARP_SPARKLE, &s_anim[1])) {
        fprintf(stderr, "[trilogy] CD time warp: no cutscene data (scdss.bin)\n");
        if (done) done();
        return;
    }
    s_active = 1;
    s_phase = P_LEVEL_WHITE;
    s_step = 0;
    tri_scd_sound_set(1);                                     /* (Sonic CD's sounds, whatever the zone) */
    w8(RAM_Game_mode, GM_SCENE);
    fprintf(stderr, "[trilogy] CD time warp\n");
}

int tri_scdwarp_active(void) { return s_active; }

int tri_scdwarp_hook(uint32_t pc)
{
    if (pc != TH_GAMELOOP || !s_active) return 0;
    if (r8(RAM_Game_mode) != GM_SCENE) { s_active = 0; return 0; }
    A(0) = tri_native(warp_frame);
    return 0;
}
