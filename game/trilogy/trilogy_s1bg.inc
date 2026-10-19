/* trilogy_s1bg.inc — included by trilogy_screen.c.
 *
 * Sonic 1 row-scrolled backgrounds (the Bg_Scroll_X family, REV01).
 * S1 builds one H-scroll value per 16px background row; we draw every visible
 * BG row at exactly that row's offset, so plane B always agrees with the line
 * scroll whatever the band speeds are. */

typedef struct {
    int32_t bg1x, bg2x, bg3x, bgy;     /* 16.16 (S1 v_bg*screenpos*) */
    int16_t last_camx, last_camy;
    uint16_t lz_deform;
} S1Bg;
static S1Bg s1;

static void s1_bg_reset(int bgy)
{
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy), camy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    memset(&s1, 0, sizeof s1);
    s1.bg1x = s1.bg2x = s1.bg3x = (int32_t)camx << 16;
    s1.bgy = (int32_t)bgy << 16;
    s1.last_camx = (int16_t)camx;
    s1.last_camy = (int16_t)camy;
}

/* camera deltas as S1 v_scrshiftx/y (pixels << 8) */
static void s1_shift(int32_t *sx, int32_t *sy)
{
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy), camy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    *sx = (int32_t)(int16_t)(camx - s1.last_camx) << 8;
    *sy = (int32_t)(int16_t)(camy - s1.last_camy) << 8;
    s1.last_camx = (int16_t)camx;
    s1.last_camy = (int16_t)camy;
}

/* rows[k] = S1 buffer word (already negated camera) for BG y = base + k*16 */
static void s1_rows_apply(const int16_t *rows, int nrows, int base, int bgy)
{
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    uint32_t fg = (uint32_t)(uint16_t)(-camx) << 16;
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) {
        int k = (y - base) >> 4;
        if (k < 0) k = 0;
        if (k >= nrows) k = nrows - 1;
        bg_band(y, y + 16, -rows[k]);
    }
    for (int l = 0; l < 224; l++) {
        int k = (bgy + l - base) >> 4;
        if (k < 0) k = 0;
        if (k >= nrows) k = nrows - 1;
        w32(RAM_H_scroll_buffer + (uint32_t)l * 4u, fg | (uint16_t)rows[k]);
    }
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)(-rows[0]));
}

/* interpolated rows: start (integer), per-row step (16.16) */
static int s1_interp(int16_t *out, int n, int start, int32_t step)
{
    int32_t acc = (int32_t)start << 16;
    for (int i = 0; i < n; i++) { out[i] = (int16_t)(acc >> 16); acc += step; }
    return n;
}

/* ext.l; asl.l #sh1; divs.w #div; ext.l; asl.l #4; asl.l #8 */
static int32_t s1_step(int d0, int div, int sh1)
{
    int32_t v = ((int32_t)d0 << sh1) / div;
    return (int32_t)(int16_t)v << 12;
}

static void s1_lz_init(void)  { s1_anim_reset(); s1_bg_reset((int16_t)r16(RAM_Camera_Y_pos_copy) >> 1); }
static void s1_lz_event(void)
{
    int32_t sx, sy;
    s1_shift(&sx, &sy);
    s1.bg1x += sx << 7;
    s1.bgy += sy << 7;
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy), camy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    int bgx = (int16_t)(s1.bg1x >> 16), bgy = (int16_t)(s1.bgy >> 16);
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t wob = t ? tri_rd32(t + TRI_BLOB_S1_LZ_WOBBLE * 4) : 0;
    uint32_t scr = t ? tri_rd32(t + TRI_BLOB_S1_LZ_SCROLL * 4) : 0;
    uint8_t d2 = (uint8_t)(s1.lz_deform >> 8), d3 = d2;
    s1.lz_deform += 0x80;
    d2 = (uint8_t)(d2 + bgy);
    d3 = (uint8_t)(d3 + camy);
    int water = (int16_t)r16(RAM_Water_level);
    int y = camy;
    for (int y0 = bgy & ~15; y0 < bgy + 240; y0 += 16) bg_band(y0, y0 + 16, bgx);
    for (int l = 0; l < 224; l++, y++, d2++, d3++) {
        int fg = -camx, bg = -bgx;
        if (y >= water && wob && scr) {
            fg += (int8_t)tri_rd8(scr + d3);
            bg += (int8_t)tri_rd8(wob + d2);
        }
        w32(RAM_H_scroll_buffer + (uint32_t)l * 4u, ((uint32_t)(uint16_t)fg << 16) | (uint16_t)bg);
    }
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)bgy);
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bgx);
}

static void s1_mz_init(void)  { s1_anim_reset(); s1_bg_reset((int16_t)r16(RAM_Camera_Y_pos_copy)); }
static void s1_mz_event(void)
{
    int32_t sx, sy;
    s1_shift(&sx, &sy);
    s1.bg1x += (sx << 6) * 3;
    s1.bg3x += sx << 6;
    s1.bg2x += sx << 7;
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy), camy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    int bgy = 0x200, d1 = camy - 0x1C8;
    if (d1 >= 0) bgy += (d1 * 3) >> 2;
    int16_t rows[32];
    int n = 0, d2 = -camx;
    n += s1_interp(rows + n, 5, d2 >> 1, s1_step((d2 >> 2) - d2, 5, 3));
    for (int i = 0; i < 2; i++) rows[n++] = (int16_t)-(s1.bg3x >> 16);
    for (int i = 0; i < 9; i++) rows[n++] = (int16_t)-(s1.bg2x >> 16);
    for (int i = 0; i < 16; i++) rows[n++] = (int16_t)-(s1.bg1x >> 16);
    int off = bgy - 0x200;
    if (off >= 0x100) off = 0x100;
    s1_rows_apply(rows, n, bgy - off, bgy);
}

static void s1_slz_init(void)
{
    s1_anim_reset();
    s1_bg_reset(((int16_t)r16(RAM_Camera_Y_pos_copy) >> 1) + 0xC0);
}
static void s1_slz_event(void)
{
    int32_t sx, sy;
    s1_shift(&sx, &sy);
    s1.bgy += sy << 7;
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    int bgy = (int16_t)(s1.bgy >> 16);
    int16_t rows[64];
    int n = 0, d2 = -camx;
    n += s1_interp(rows + n, 0x1C, d2, s1_step((d2 >> 3) - d2, 0x1C, 4));
    int d0 = (d2 >> 3);
    d0 += d0 >> 1;
    for (int i = 0; i < 5; i++) rows[n++] = (int16_t)d0;
    for (int i = 0; i < 5; i++) rows[n++] = (int16_t)(d2 >> 2);
    for (int i = 0; i < 0x1E; i++) rows[n++] = (int16_t)(d2 >> 1);
    s1_rows_apply(rows, n, 0xC0, bgy);
}

static void s1_syz_init(void)
{
    s1_anim_reset();
    int y = (int16_t)r16(RAM_Camera_Y_pos_copy);
    s1_bg_reset(((y * 0x30) >> 8) + 1);
}
static void s1_syz_event(void)
{
    int32_t sx, sy;
    s1_shift(&sx, &sy);
    s1.bgy += (sy << 4) * 3;
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    int bgy = (int16_t)(s1.bgy >> 16);
    int16_t rows[64];
    int n = 0, d2 = -camx;
    n += s1_interp(rows + n, 8, d2 >> 1, s1_step((d2 >> 3) - d2, 8, 3));
    for (int i = 0; i < 5; i++) rows[n++] = (int16_t)(d2 >> 3);
    for (int i = 0; i < 6; i++) rows[n++] = (int16_t)(d2 >> 2);
    n += s1_interp(rows + n, 0x0E, d2 >> 1, s1_step(d2 - (d2 >> 1), 0x0E, 4));
    s1_rows_apply(rows, n, 0, bgy);
}

static void s1_sbz_init(void)
{
    s1_anim_reset();
    int y = (int16_t)r16(RAM_Camera_Y_pos_copy);
    s1_bg_reset(((y & 0x7F8) >> 3) + 1);
}
static void s1_sbz_event(void)
{
    int32_t sx, sy;
    s1_shift(&sx, &sy);
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    int act = tri_act();
    if (act != 0 && !(tri_zone() == TRI_S1_LZ)) {   /* Deform_SBZ2: SBZ2 / Final Zone */
        s1.bg1x += sx << 6;
        s1.bgy += sy << 5;
        int16_t rows[1] = { (int16_t)-(s1.bg1x >> 16) };
        s1_rows_apply(rows, 1, 0, (int16_t)(s1.bgy >> 16));
        return;
    }
    s1.bg1x += sx << 7;
    s1.bg3x += sx << 6;
    s1.bg2x += (sx << 5) * 3;
    s1.bgy += sy << 5;
    int bgy = (int16_t)(s1.bgy >> 16);
    int16_t rows[40];
    int n = 0, d2 = (-camx) >> 2;
    n += s1_interp(rows + n, 4, d2, s1_step((d2 >> 1) - d2, 4, 3));
    for (int i = 0; i < 10; i++) rows[n++] = (int16_t)-(s1.bg3x >> 16);
    for (int i = 0; i < 7; i++) rows[n++] = (int16_t)-(s1.bg2x >> 16);
    for (int i = 0; i < 11; i++) rows[n++] = (int16_t)-(s1.bg1x >> 16);
    s1_rows_apply(rows, n, 0, bgy);
}
