/* trilogy_s2bg.inc — included by trilogy_screen.c.
 *
 * Sonic 2 backgrounds (the SwScrl_* family, s2disasm). Sonic 2 writes one
 * H-scroll value per scanline into Horiz_Scroll_Buf; we draw the background
 * layout into plane B the way S2's draw routines leave it and write the same
 * per-line values into S3K's H_scroll_buffer. */

typedef struct {
    int16_t layerdef;              /* TempArray_LayerDef (ripple phase) */
    int32_t bgx, bg2x, bgy;        /* Camera_BG_X/BG2_X/BG_Y (16.16) */
    int16_t lastx, lasty;
} S2Bg;
static S2Bg s2;

/* one scanline: FG = -camera x, BG = v */
#define S2_LINE(l, v) w32(RAM_H_scroll_buffer + (uint32_t)(l) * 4u, fg | (uint16_t)(v))

/* ---- Emerald Hill (InitCam_EHZ / SwScrl_EHZ) ---------------------------- */
static const int8_t s2_ripple[66] = {
    1, 2, 1, 3, 1, 2, 2, 1, 2, 3, 1, 2, 1, 2, 0, 0,  2, 0, 3, 2, 2, 3, 2, 2, 1, 3, 0, 0, 1, 0, 1, 3,
    1, 2, 1, 3, 1, 2, 2, 1, 2, 3, 1, 2, 1, 2, 0, 0,  2, 0, 3, 2, 2, 3, 2, 2, 1, 3, 0, 0, 1, 0, 1, 3,
    1, 2,
};

static void s2_ehz_init(void)
{
    memset(&s2, 0, sizeof s2);
}

static void s2_ehz_event(void)
{
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-camx) << 16;
    int16_t d2 = (int16_t)-camx;
    int l = 0;
    /* the background camera stays at (0,0): plane B holds BG x 0-511, y 0-255 */
    bg_band(0, 256, 64);
    for (int i = 0; i < 22; i++) S2_LINE(l++, 0);                 /* sky */
    int16_t d0 = (int16_t)(d2 >> 6);
    for (int i = 0; i < 58; i++) S2_LINE(l++, d0);                /* clouds + far hills */
    int16_t d3 = d0;
    if (!(r16(RAM_Level_frame_counter) & 7)) s2.layerdef--;      /* Vint_runcount & 7 */
    int rp = s2.layerdef & 0x1F;
    for (int i = 0; i < 21; i++) S2_LINE(l++, d3 + s2_ripple[rp + i]);   /* sea */
    for (int i = 0; i < 11; i++) S2_LINE(l++, 0);
    d0 = (int16_t)(d2 >> 4);
    for (int i = 0; i < 16; i++) S2_LINE(l++, d0);
    d0 = (int16_t)((d2 >> 4) + ((d2 >> 4) >> 1));
    for (int i = 0; i < 16; i++) S2_LINE(l++, d0);
    /* near hills: d2/8 rising to d2/2 across the remaining lines */
    int32_t step = ((int32_t)(int16_t)((d2 >> 1) - (d2 >> 3)) << 8) / 0x30;
    step = (int32_t)(int16_t)step << 8;
    int32_t acc = (int32_t)(int16_t)(d2 >> 3) << 16;
    for (int i = 0; i < 15; i++) { S2_LINE(l++, acc >> 16); acc += step; }
    for (int i = 0; i < 9; i++) { S2_LINE(l++, acc >> 16); S2_LINE(l++, acc >> 16); acc += step * 2; }
    for (int i = 0; i < 15; i++) {
        S2_LINE(l++, acc >> 16); S2_LINE(l++, acc >> 16); S2_LINE(l++, acc >> 16);
        acc += step * 3;
    }
    while (l < 224) S2_LINE(l++, acc >> 16);                      /* (fixBugs: the last two lines) */
    w16(RAM_Camera_X_pos_BG_copy, 0);
    w16(RAM_Camera_Y_pos_BG_copy, 0);
}

/* camera deltas since last frame, in pixels (Camera_X/Y_pos_diff) */
static void s2_delta(int *dx, int *dy)
{
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    *dx = (int16_t)(cx - s2.lastx);
    *dy = (int16_t)(cy - s2.lasty);
    s2.lastx = (int16_t)cx;
    s2.lasty = (int16_t)cy;
}

/* ---- Chemical Plant (InitCam_CPZ / SwScrl_CPZ) --------------------------- */
static void s2_cpz_init(void)
{
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    memset(&s2, 0, sizeof s2);
    s2.bgy = (int32_t)(cy >> 2) << 16;
    s2.bg2x = (int32_t)(cx >> 1) << 16;
    s2.bgx = (int32_t)(cx >> 3) << 16;
    s2.lastx = (int16_t)cx;
    s2.lasty = (int16_t)cy;
}

static void s2_cpz_event(void)
{
    int dx, dy;
    s2_delta(&dx, &dy);
    s2.bgx += (int32_t)dx << 13;                  /* diff<<8 (8.8) <<5 */
    s2.bgy += (int32_t)dy << 14;                  /* <<6 */
    s2.bg2x += (int32_t)dx << 15;                 /* <<7 */
    if (!(r16(RAM_Level_frame_counter) & 7)) s2.layerdef--;
    int bgy = (int16_t)(s2.bgy >> 16);
    int16_t bx = (int16_t)(s2.bgx >> 16), b2x = (int16_t)(s2.bg2x >> 16);
    int16_t rows[64];
    for (int k = 0; k < 64; k++) rows[k] = (int16_t)-(k <= 18 ? bx : b2x);   /* line block 18: ripple */
    s1_rows_apply(rows, 64, 0, bgy);
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-camx) << 16;
    int rp = s2.layerdef & 0x1F;
    for (int l = 0; l < 224; l++) {
        int y = bgy + l;
        if (((y & 0x3F0) >> 4) == 18)
            S2_LINE(l, (int16_t)(-bx + s2_ripple[rp + (y & 15)]));
    }
}

/* ---- Aquatic Ruin (InitCam_ARZ / SwScrl_ARZ) ----------------------------
 * Plane B is drawn at the one BG camera x (Camera_BG_X_pos, trailing
 * Camera_ARZ_BG_X_pos by at most 16px a frame); 16 rows of arbitrary heights
 * scroll at their own speeds, as S2 does it. bgx = Camera_BG_X_pos (pixels),
 * bg2x = Camera_ARZ_BG_X_pos (16.16), bgy = Camera_BG_Y_pos (16.16). */
static const uint8_t s2_arz_rows[16] = { 176, 112, 48, 96, 21, 12, 14, 6, 12, 31, 48, 192, 240, 240, 240, 240 };

static void s2_arz_init(void)
{
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    memset(&s2, 0, sizeof s2);
    if (tri_act()) s2.bgy = (int32_t)(uint16_t)((uint16_t)(cy - 0xE0) >> 1) << 16;
    else s2.bgy = (int32_t)(int16_t)(cy - 0x180) << 16;
    int16_t d1 = (int16_t)((cx * 0x119) >> 8);
    s2.bgx = d1;
    s2.bg2x = (int32_t)d1 << 16;
    s2.lastx = (int16_t)cx;
    s2.lasty = (int16_t)cy;
}

static void s2_arz_event(void)
{
    int dx, dy;
    s2_delta(&dx, &dy);
    s2.bg2x += dx * 256 * 281;                          /* SetHorizScrollFlagsBG_ARZ */
    int16_t d0 = (int16_t)((int16_t)(s2.bg2x >> 16) - (int16_t)s2.bgx);
    if (d0 < 0) { if (d0 <= -16) d0 = -16; }
    else if ((uint16_t)d0 >= 16) d0 = 16;
    s2.bgx = (int16_t)(s2.bgx + d0);
    s2.bgy += tri_act() ? (int32_t)dy << 15 : (int32_t)dy << 16;
    int16_t bgx = (int16_t)s2.bgx;
    extern uint8_t s2_screen_shaking;
    int shy = 0;
    if (s2_screen_shaking) {                            /* the boss's pillars: shake the screen */
        int k = r16(RAM_Level_frame_counter) & 0x3F;
        int8_t sy = s2_ripple[k], sx = s2_ripple[k + 1];
        w16(RAM_Camera_Y_pos_copy, r16(RAM_Camera_Y_pos_copy) + sy);
        w16(RAM_Camera_X_pos_copy, r16(RAM_Camera_X_pos_copy) + sx);
        shy = sy;                                       /* (S2 shakes the BG's v-scroll too) */
    }
    int camx = (int16_t)r16(RAM_Camera_X_pos);
    int16_t spd[16];
    int32_t c0 = (int32_t)(int16_t)(((int32_t)camx << 4) / 10) << 12, c1 = c0;
    spd[3] = (int16_t)(c1 >> 16);
    c1 = c1 * 2 + c0;
    for (int k = 4; k <= 9; k++) { spd[k] = (int16_t)(c1 >> 16); c1 += c0; }
    spd[10] = (int16_t)(c1 >> 16);
    spd[0] = spd[1] = spd[2] = spd[11] = spd[12] = spd[13] = spd[14] = spd[15] = bgx;
    int bgy = (int16_t)(s2.bgy >> 16) + shy;
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, bgx);
    int fgx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-fgx) << 16;
    int row = 0, left = bgy;                            /* the row at the top of the screen */
    while (row < 15 && left >= s2_arz_rows[row]) { left -= s2_arz_rows[row]; row++; }
    left = s2_arz_rows[row] - left;
    for (int l = 0; l < 224; l++) {
        S2_LINE(l, -spd[row]);
        if (--left <= 0 && row < 15) left = s2_arz_rows[++row];
    }
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}

/* ---- Casino Night (InitCam_CNZ / SwScrl_CNZ) ----------------------------
 * Plane B is drawn at BG x 0 (the background repeats every 512px); rows of
 * 16 lines scroll from camera speed down to 1/16, a rippling 16-line band,
 * then the rest at 1/8. */
static void s2_cnz_init(void) { memset(&s2, 0, sizeof s2); }

static void s2_cnz_event(void)
{
    int16_t x = (int16_t)r16(RAM_Camera_X_pos);
    int bgy = (uint16_t)r16(RAM_Camera_Y_pos) >> 6;
    int16_t spd[10];                                    /* SwScrl_CNZ_GenerateScrollValues */
    int32_t d0 = (int32_t)(int16_t)((x >> 3) - x) << 13;
    uint32_t d3 = (uint32_t)(uint16_t)x << 16;
    for (int k = 0; k < 7; k++) { spd[k] = (int16_t)(d3 >> 16); d3 += (uint32_t)d0; }
    spd[9] = (int16_t)(x >> 3);
    spd[7] = spd[8] = (int16_t)(x >> 4);
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, 0);
    int fgx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-fgx) << 16;
    static const uint8_t heights[10] = { 16, 16, 16, 16, 16, 16, 16, 16, 0, 240 };
    int row = 0, left = bgy;
    while (row < 9 && heights[row] && left >= heights[row]) { left -= heights[row]; row++; }
    left = heights[row] ? heights[row] - left : 16 - left;
    int ripple = heights[row] == 0;
    int rp = (-(int)(r8(RAM_Level_frame_counter + 1) >> 3)) & 0x1F;
    int ri = ripple ? 16 - left : 0;
    for (int l = 0; l < 224; l++) {
        int16_t v = (int16_t)-spd[row];
        if (ripple) v = (int16_t)(v + s2_ripple[rp + ri++]);
        S2_LINE(l, v);
        if (--left <= 0 && row < 9) {
            row++;
            ripple = heights[row] == 0;
            left = ripple ? 16 : heights[row];
            ri = 0;
        }
    }
    w16(RAM_Camera_X_pos_BG_copy, 0);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}


/* ---- Hill Top (InitCam_HTZ / SwScrl_HTZ) -------------------------------- */
static int16_t s2_htz_layer[16];          /* TempArray_LayerDef: cloud rows (Dynamic_HTZ reads them) */
static int16_t s2_htz_l22;                /* TempArray_LayerDef+$22 */

static void s2_htz_init(void)
{
    memset(&s2, 0, sizeof s2);
    memset(s2_htz_layer, 0, sizeof s2_htz_layer);
    s2_htz_l22 = 0;
}

/* HTZ_Screen_Shake: plane B is the moving terrain, one scroll for all lines */
static void s2_htz_quake_event(void)
{
    extern int16_t s2_htz_bgx, s2_htz_bgy, s2_htz_dx, s2_htz_dy;
    extern uint8_t s2_screen_shaking;
    s2_htz_bgx += s2_htz_dx;                            /* SetHorizScrollFlagsBG */
    s2_htz_bgy += s2_htz_dy;                            /* SetVertiScrollFlagsBG */
    int d0 = 0, d2 = 0;
    if (s2_screen_shaking) {
        int k = r16(RAM_Level_frame_counter) & 0x3F;
        d0 = (uint8_t)s2_ripple[k];
        d2 = (uint8_t)s2_ripple[k + 1];
        w16(RAM_Camera_Y_pos_copy, r16(RAM_Camera_Y_pos_copy) + d0);
        w16(RAM_Camera_X_pos_copy, r16(RAM_Camera_X_pos_copy) + d2);
    }
    int bgx = s2_htz_bgx + d2, bgy = s2_htz_bgy + d0;
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, bgx);
    uint32_t fg = (uint32_t)(uint16_t)-(int16_t)(r16(RAM_Camera_X_pos) + d2) << 16;
    for (int l = 0; l < 224; l++) S2_LINE(l, (uint16_t)-bgx);
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}

static void s2_htz_event(void)
{
    extern uint8_t s2_htz_quake;
    if (s2_htz_quake) { s2_htz_quake_event(); return; }
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-camx) << 16;
    int l = 0;
    bg_band(0, 256, 64);                                /* the BG camera stays at (0,0) */
    int16_t d2 = (int16_t)-camx;
    for (int i = 0; i < 128; i++) S2_LINE(l++, (int16_t)(d2 >> 3));   /* mountains */
    int16_t d0 = s2_htz_l22;
    s2_htz_l22 += 4;
    d2 = (int16_t)(d2 - d0);
    int16_t d1 = (int16_t)(d2 >> 4);                    /* (REV01: remainder discarded) */
    d0 = (int16_t)((d2 >> 1) - d1);
    int32_t n = (int32_t)d0 << 8, q = n / 112;          /* divs.w #112 (an overflow leaves d0 alone) */
    int16_t qw = (q > 32767 || q < -32768) ? (int16_t)n : (int16_t)q;
    int32_t step = (int32_t)qw << 8;                    /* 16.16 */
    int32_t acc = (int32_t)(uint16_t)d1 << 16;          /* d3: integer in the low word */
    int k = 0;
    for (int i = 0; i < 3; i++) { acc += step; s2_htz_layer[k++] = (int16_t)(acc >> 16); }
    s2_htz_layer[k++] = (int16_t)(acc >> 16);
    acc += step * 2;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) s2_htz_layer[k++] = (int16_t)(acc >> 16);
        acc += step * 3;
    }
    step *= 4;
    static const uint8_t runs[6] = { 3, 5, 7, 8, 10, 15 }, adds[6] = { 1, 1, 2, 2, 3, 3 };
    for (int r = 0; r < 6; r++) {                       /* cloud bands, faster towards the bottom */
        for (int i = 0; i < runs[r]; i++) S2_LINE(l++, (int16_t)(acc >> 16));
        acc += step * adds[r];
    }
    for (int r = 0; r < 3; r++) {
        for (int i = 0; i < 16; i++) S2_LINE(l++, (int16_t)(acc >> 16));
        acc += step * 4;
    }
    w16(RAM_Camera_X_pos_BG_copy, 0);
    w16(RAM_Camera_Y_pos_BG_copy, 0);
}

/* ---- Mystic Cave (InitCam_MCZ / SwScrl_MCZ) -----------------------------
 * The background camera stays at x 0 (plane B holds BG x 0-511); its y
 * follows the camera at 1/3 (act 1) or 1/6 (act 2). 24 row bands scroll at
 * 1/10 .. 9/10 of the camera, mirrored about the middle of the 512 lines. */
static const uint8_t s2_mcz_rows[24] = { 37, 23, 18, 7, 7, 2, 2, 48, 13, 19, 32, 64,
                                         32, 19, 13, 48, 2, 2, 7, 7, 32, 18, 23, 37 };
static const uint8_t s2_mcz_speed[24] = { 9, 8, 7, 5, 4, 3, 2, 1, 5, 7, 8, 9,     /* TempArray_LayerDef: */
                                          8, 7, 5, 1, 2, 3, 4, 5, 6, 7, 8, 9 };   /* n/10 of camera x */

static void s2_mcz_init(void) { memset(&s2, 0, sizeof s2); }

static void s2_mcz_event(void)
{
    uint16_t cy = r16(RAM_Camera_Y_pos);
    int bgy = tri_act() ? (int16_t)(cy / 6 - 16) : (int16_t)(cy / 3 - 320);
    extern uint8_t s2_screen_shaking;
    int shy = 0;
    if (s2_screen_shaking) {                            /* the boss digging: shake the screen */
        int k = r16(RAM_Level_frame_counter) & 0x3F;
        uint8_t sy = (uint8_t)s2_ripple[k], sx = (uint8_t)s2_ripple[k + 1];
        w16(RAM_Camera_Y_pos_copy, r16(RAM_Camera_Y_pos_copy) + sy);
        w16(RAM_Camera_X_pos_copy, r16(RAM_Camera_X_pos_copy) + sx);
        shy = sy;                                       /* (fixBugs: the row bands follow the v-scroll) */
    }
    int camx = (int16_t)r16(RAM_Camera_X_pos);
    int32_t d0 = (int32_t)(int16_t)(((int32_t)camx << 4) / 10) << 12;   /* camera x / 10 (16.16) */
    int16_t v[10];
    int32_t d1 = 0;
    for (int k = 1; k <= 9; k++) { d1 += d0; v[k] = (int16_t)(d1 >> 16); }
    bgy += shy;
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, 0);
    int fgx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-fgx) << 16;
    int row = 0;
    uint16_t left = (uint16_t)bgy;
    while (row < 23 && left >= s2_mcz_rows[row]) { left = (uint16_t)(left - s2_mcz_rows[row]); row++; }
    int n = s2_mcz_rows[row] - left;
    for (int l = 0; l < 224; l++) {
        S2_LINE(l, -v[s2_mcz_speed[row]]);
        if (--n <= 0) { row = (row + 1) % 24; n = s2_mcz_rows[row]; }
    }
    w16(RAM_Camera_X_pos_BG_copy, 0);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}

/* ---- Oil Ocean (InitCam_OOZ / SwScrl_OOZ) -------------------------------
 * The background camera follows at 1/8. S2 fills the lines bottom-up: the sea, cloud bands at 1/8,
 * 1/16, 1/4, the sun's rippling glare, more clouds, then the sky. */
static void s2_ooz_init(void)
{
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    memset(&s2, 0, sizeof s2);
    s2.bgy = (int32_t)(uint16_t)(((uint16_t)cy >> 3) + 0x50) << 16;
    s2.lastx = (int16_t)cx;
    s2.lasty = (int16_t)cy;
}

static int s2_ooz_put(int l, uint32_t fg, int16_t v, int n)   /* .doLines: n lines upward from l */
{
    while (n-- > 0 && l >= 0) S2_LINE(l--, v);
    return l;
}

static void s2_ooz_event(void)
{
    int dx, dy;
    s2_delta(&dx, &dy);
    s2.bgx += (int32_t)(dx * 256) << 5;                 /* Camera_X_pos_diff is pixels * 256 */
    s2.bgy += (int32_t)(dy * 256) << 5;
    int bgx = (int16_t)(s2.bgx >> 16), bgy = (int16_t)(s2.bgy >> 16);
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, bgx);
    int fgx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-fgx) << 16;
    int16_t d7 = (int16_t)-bgx;
    int d1 = (uint16_t)bgy < 80 ? 0 : (uint16_t)bgy - 80;
    d1 = d1 < 176 ? d1 - 176 : 0;
    int l = 223;
    l = s2_ooz_put(l, fg, d7, d1 + 223 + 1);                       /* the sea */
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 3), 8);                  /* medium clouds */
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 4), 8);                  /* slow */
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 2), 8);                  /* fast */
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 4), 7);
    if (!(r16(RAM_Level_frame_counter) & 7)) s2.layerdef--;        /* (S2: Vint_runcount & 7) */
    int rp = s2.layerdef & 0x1F;
    for (int i = 0; i < 33 && l >= 0; i++) S2_LINE(l--, s2_ripple[rp + i]);   /* the sun's glare */
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 3), 8);
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 4), 8);
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 2), 8);
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 4), 8);
    l = s2_ooz_put(l, fg, (int16_t)(d7 >> 3), 8);
    s2_ooz_put(l, fg, d7, 72);                                     /* the sky */
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}

/* ---- Metropolis (InitCam_Std / SwScrl_MTZ): one background scroll, the
 * background camera at 1/8 x and 1/4 y ------------------------------------ */
static void s2_mtz_init(void)
{
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    memset(&s2, 0, sizeof s2);
    s2.bgy = (int32_t)(int16_t)(cy >> 2) << 16;
    s2.bgx = (int32_t)(int16_t)(cx >> 3) << 16;
    s2.lastx = (int16_t)cx;
    s2.lasty = (int16_t)cy;
}

static void s2_mtz_event(void)
{
    int dx, dy;
    s2_delta(&dx, &dy);
    s2.bgx += (int32_t)(dx * 256) << 5;
    s2.bgy += (int32_t)(dy * 256) << 6;
    int bgx = (int16_t)(s2.bgx >> 16), bgy = (int16_t)(s2.bgy >> 16);
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, bgx);
    int fgx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-fgx) << 16;
    for (int l = 0; l < 224; l++) S2_LINE(l, -bgx);
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}

/* ---- Sky Chase (InitCam_SCZ / SwScrl_SCZ): the camera rides the Tornado's
 * velocity (s2_scz_camera, from the resize hook with Scroll_lock set); the
 * background creeps half a pixel a frame while it moves ------------------ */
static void s2_scz_init(void) { memset(&s2, 0, sizeof s2); }

static void s2_scz_camera(void)
{
    extern int16_t s2_tornado_vx, s2_tornado_vy;
    if (r16(RAM_Debug_placement_mode)) { w8(RAM_Scroll_lock, 0); return; }
    w8(RAM_Scroll_lock, 1);
    w16(RAM_Camera_X_pos, (uint16_t)(r16(RAM_Camera_X_pos) + s2_tornado_vx));
    w16(RAM_Camera_Y_pos, (uint16_t)(r16(RAM_Camera_Y_pos) + s2_tornado_vy));
    if (s2_tornado_vx) s2.bgx += 0x8000;                /* $100 << 7 */
}

static void s2_scz_event(void)
{
    int bgx = (int16_t)(s2.bgx >> 16);
    for (int y = 0; y < 224 + 16; y += 16) bg_band(y, y + 16, bgx);
    int fgx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-fgx) << 16;
    for (int l = 0; l < 224; l++) S2_LINE(l, -bgx);
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
    w16(RAM_Camera_Y_pos_BG_copy, 0);
}
/* ---- Wing Fortress (LevEvents_WFZ ScrollBG + SwScrl_WFZ): the BG camera
 * chases (camera - Camera_BG_X/Y_offset) at most 16 px a frame; rows pick a
 * scroll by BG line: the BG camera, or one of three cloud accumulators
 * (S2 tallies only the clouds' own speed - they don't follow the camera) */
extern int16_t s2_wfz_bg_xoff, s2_wfz_bg_yoff;
static int32_t s2_wfz_cloud[3];
static const uint8_t s2_wfz_normal[39][2] = {
    { 0xC0, 0 }, { 0xC0, 0 }, { 0x80, 0 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0xC0, 0 }, { 0xC0, 0 }, { 0x80, 0 },
};
static const uint8_t s2_wfz_transition[38][2] = {
    { 0xC0, 0 }, { 0xC0, 0 }, { 0x80, 0 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x80, 4 }, { 0x80, 4 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 },
    { 0x20, 8 }, { 0x30, 0xC }, { 0x30, 0x10 }, { 0xC0, 0 }, { 0xC0, 0 }, { 0x80, 0 },
};

static void s2_wfz_init(void)
{
    memset(&s2, 0, sizeof s2);
    memset(s2_wfz_cloud, 0, sizeof s2_wfz_cloud);
    s2.bgx = (int32_t)(int16_t)r16(RAM_Camera_X_pos_copy) << 16;     /* LevEvents_WFZ_Routine1 */
    s2.bgy = (int32_t)(int16_t)r16(RAM_Camera_Y_pos_copy) << 16;
}

static int s2_clamp16(int d) { return d < -16 ? -16 : d > 16 ? 16 : d; }

static void s2_wfz_event(void)
{
    extern uint8_t s2_wfz_bg_snap;
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    if (s2_wfz_bg_snap) {                               /* LevEvents_WFZ_Routine1 */
        s2_wfz_bg_snap = 0;
        s2.bgx = (int32_t)cx << 16;
        s2.bgy = (int32_t)cy << 16;
    }
    s2.bgx += (int32_t)s2_clamp16((int16_t)(cx - (s2.bgx >> 16) - s2_wfz_bg_xoff)) << 16;   /* ScrollBG */
    s2.bgy += (int32_t)s2_clamp16((int16_t)(cy - (s2.bgy >> 16) - s2_wfz_bg_yoff)) << 16;
    s2_wfz_cloud[0] += 0x8000;
    s2_wfz_cloud[1] += 0x4000;
    s2_wfz_cloud[2] += 0x2000;
    int bgx = (int16_t)(s2.bgx >> 16), bgy = (int16_t)(s2.bgy >> 16);
    int16_t val[5] = { (int16_t)bgx, (int16_t)bgx, (int16_t)(s2_wfz_cloud[0] >> 16),
                       (int16_t)(s2_wfz_cloud[1] >> 16), (int16_t)(s2_wfz_cloud[2] >> 16) };
    const uint8_t (*a)[2] = s2_wfz_normal;
    int n = 39;
    if (r16(RAM_Camera_X_pos) >= 0x2700) { a = s2_wfz_transition; n = 38; }
    int base = bgy & 0x7FF;
    /* each 16-line band's scroll, by its line in the arrays (bands are 16-aligned) */
    int seg = 0, end = a[0][0];
    int16_t band[256 / 16 + 2];
    for (int y = bgy & ~15, k = 0; y < bgy + 224 + 16; y += 16, k++) {
        int pos = base + (y - bgy);
        if (pos < 0) pos = 0;
        while (pos >= end && seg < n - 1) end += a[++seg][0];
        band[k] = val[a[seg][1] >> 2];
        bg_band(y, y + 16, band[k]);
    }
    uint32_t fg = (uint32_t)(uint16_t)(-cx) << 16;
    seg = 0; end = a[0][0];
    for (int l = 0; l < 224; l++) {
        int pos = base + l;
        while (pos >= end && seg < n - 1) end += a[++seg][0];
        S2_LINE(l, (uint16_t)-val[a[seg][1] >> 2]);
    }
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
}
/* ---- Death Egg (SwScrl_DEZ): the BG camera is the camera; star rows drift
 * at their own speeds, Earth's rim scales off one of them; the robot's slam
 * and the escape shake the screen (Screen_Shaking_Flag / DEZ_Shake_Timer) */
static uint16_t s2_dez_layer[36];
static const uint8_t s2_dez_rows[36] = {
    128, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    3, 5, 8, 16, 128, 128, 128,
};
static void s2_dez_init(void)
{
    extern uint8_t s2_dez_shake;
    memset(&s2, 0, sizeof s2);
    memset(s2_dez_layer, 0, sizeof s2_dez_layer);
    s2_dez_shake = 0;
}

static void s2_dez_event(void)
{
    extern uint8_t s2_dez_shake;
    extern int16_t s2_dez_shake_timer;
    static const uint8_t speed[23] = { 3, 2, 4, 1, 2, 4, 3, 4, 2, 6, 3, 4, 1, 2, 4, 3, 2, 3, 4, 1, 3, 4, 2 };
    int cx = (int16_t)r16(RAM_Camera_X_pos_copy), cy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    uint16_t *a = s2_dez_layer;
    a[0] = (uint16_t)cx;
    for (int k = 0; k < 23; k++) a[1 + k] = (uint16_t)(a[1 + k] + speed[k]);
    a[24] = (uint16_t)(a[24] + 1);
    a[25] = (uint16_t)(a[24] >> 1);
    a[26] = (uint16_t)(a[26] + 3);
    a[27] = (uint16_t)(a[27] + 2);
    a[28] = (uint16_t)(a[28] + 4);
    {                                                   /* Earth */
        uint32_t d1 = (uint32_t)a[24] << 16, d0 = d1;
        d1 >>= 3;
        d0 -= d1; a[31] = (uint16_t)(d0 >> 16);
        d0 -= d1; a[30] = (uint16_t)(d0 >> 16);
        d0 -= d1; a[29] = (uint16_t)(d0 >> 16);
    }
    a[32] = (uint16_t)(a[32] + 1);
    a[33] = a[34] = a[35] = (uint16_t)cx;
    int bgy = cy;
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) bg_band(y, y + 16, cx);
    uint32_t fg = (uint32_t)(uint16_t)(-cx) << 16;
    int seg = 0, end = s2_dez_rows[0];
    for (int l = 0; l < 224; l++) {
        int pos = bgy + l;
        while (pos >= end && seg < 35) end += s2_dez_rows[++seg];
        S2_LINE(l, (uint16_t)-(int16_t)a[seg]);
    }
    int d0 = 0;
    if (s2_dez_shake) {                                 /* (S2 shakes after the H-scroll: vertical only shows) */
        if (--s2_dez_shake_timer < 0) s2_dez_shake = 0;
        d0 = (uint8_t)s2_ripple[r16(RAM_Level_frame_counter) & 0x3F];
        w16(RAM_Camera_Y_pos_copy, (uint16_t)(cy + d0));
    }
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)cx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)(bgy + d0));
}
#undef S2_LINE
