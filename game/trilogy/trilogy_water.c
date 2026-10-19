/*
 * trilogy_water.c — Sonic 1 Labyrinth Zone water (LZ1-3 and SBZ3 = LZ act 4)
 * on S3K's water system.
 *
 * S3K already does everything the player sees underwater (H-int palette
 * split, physics, drowning, splashes). We supply S1's set-up (start height,
 * underwater palette), and LZWaterFeatures, transcribed: per-act dynamic
 * water (DynWater_*), the oscillating surface, wind tunnels and water slides.
 * S3K names: Mean_water_level = v_waterpos2, Target_water_level = v_waterpos3,
 * Water_level = v_waterpos1, Water_speed = f_water.
 *
 * Sonic 2 water (CPZ2, ARZ) runs on the same set-up with S2's own start
 * height, underwater palette and DynamicWater (S2 info block, S2I_*).
 */
#include "trilogy_obj.h"

#define RAM_H_int_jump        0xFFFFF6u
#define RAM_H_int_addr        0xFFFFF8u
#define RAM_V_blank_cycles    0xFFFFD6u
#define RAM_Competition_mode  0xFFFFE8u
#define S1_LZ                 TRI_S1_LZ

#define OBJ_double_jump  0x2F
#define OBJ_move_lock    0x32
#define OBJ_status2      0x2B         /* status_secondary: bit 7 = on a water slide */
#define ANI_WALK         0x00
#define ANI_FLOAT        0x0F         /* S3K HCZ water tunnel pose */
#define ANI_SLIDE        0x1B         /* S3K HCZ water slide pose */

/* S1 info block (TD_EXTRA): raw FG layout 64x8, loop ids, water */
#define S1I_WATER_PAL   0x204
#define S1I_WATER_Y     0x206
#define S1I_LZ3_SWAP    0x208

static uint8_t s_wtr_routine;          /* v_wtr_routine */
static uint8_t s_lz3_chunk;            /* row 2 column 6 patched to this S1 chunk (0 = layout's) */
static uint8_t s_tunnel_mode[2];       /* f_wtunnelmode, per player */
uint8_t tri_lz_wtunnel_allow;          /* f_wtunnelallow (LZ1 door, object 56) */

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

static int lz_water(void)
{
    return tri_dir() && tri_rd8(tri_dir() + TD_GAME) == TRI_GAME_S1 && tri_zone() == S1_LZ;
}

static uint32_t s1info(void) { return tri_rd32(tri_dir() + TD_EXTRA); }

/* Sonic 2 act with water (Level_InitWater: CPZ2, ARZ) */
static int s2_water(void)
{
    uint32_t e = tri_dir();
    if (!e || tri_rd8(e + TD_GAME) != TRI_GAME_S2) return 0;
    uint32_t inf = tri_rd32(e + TD_EXTRA);
    return inf && tri_rd8(inf + S2I_WATER_PAL);
}

static uint8_t water_pal_id(void)
{
    return s2_water() ? tri_rd8(s1info() + S2I_WATER_PAL) : tri_rd8(s1info() + S1I_WATER_PAL);
}

/* LoadWaterPalette: S1's underwater palette (also called again after the
 * level's objects are spawned, where S3K would pick a zone palette itself) */
static void load_water_palette(void)
{
    uint8_t pal = water_pal_id();
    D(0) = pal;
    tri_jsr(S3K_LoadPalette2);
    D(0) = pal;
    tri_jsr(S3K_LoadPalette2_Immediate);
    if (r8(RAM_Last_star_post_hit))
        w8(RAM_Water_full_screen_flag, r8(RAM_Saved_water_full_screen_flag));
}

/* ---- set-up (CheckLevelForWater + LoadWaterPalette) ---------------------- */
static void check_water(void)
{
    uint32_t inf = s1info();
    w8(RAM_Water_flag, 1);
    w16(RAM_Competition_mode, 0);
    w16(RAM_H_int_jump, 0x4EF9);
    w32(RAM_H_int_addr, r16(RAM_V_blank_cycles) >= 0x1000 ? S3K_HInt4 : S3K_HInt3);
    w32(RAM_Water_palette_data_addr, blob(TRI_BLOB_WATER_XFER));
    uint16_t y = tri_rd16(inf + (s2_water() ? S2I_WATER_Y : S1I_WATER_Y));
    w16(RAM_Water_level, y);
    w16(RAM_Mean_water_level, y);
    w16(RAM_Target_water_level, y);
    w8(RAM_Water_entered_counter, 0);
    w8(RAM_Water_full_screen_flag, 0);
    w8(RAM_Water_speed, 1);
    load_water_palette();
    s_wtr_routine = 0;
    s_lz3_chunk = 0;
    s_tunnel_mode[0] = s_tunnel_mode[1] = 0;
    tri_lz_wtunnel_allow = 0;
}

/* ---- LZ3: S1 layout row 2 column 6 changes ($4B: water rises, 7: switch $F) */
void tri_lz3_patch(uint8_t chunk)
{
    uint32_t inf = s1info();
    uint32_t src = inf + S1I_LZ3_SWAP + (chunk == 7 ? 4u : 0u);
    uint32_t hdr = RAM_Level_layout_header;
    for (int q = 0; q < 4; q++) {
        int cy = 2 * 2 + (q >> 1), cx = 6 * 2 + (q & 1);
        uint16_t row = r16(hdr + 8 + (uint32_t)cy * 4u);
        w8(0xFF0000u | (uint16_t)(row + cx), tri_rd8(src + (uint32_t)q));
    }
    s_lz3_chunk = chunk;
    tri_refresh_fg();
}

/* S1 layout chunk under (x, y), as LZWaterSlides reads v_lvllayout */
static uint8_t s1_chunk(uint16_t x, uint16_t y)
{
    int r = y >> 8, c = (x >> 8) & 0x7F;
    if (r >= 8 || c >= 64) return 0;
    if (s_lz3_chunk && r == 2 && c == 6) return s_lz3_chunk;
    return tri_rd8(s1info() + (uint32_t)(r * 64 + c));
}

uint8_t tri_lz_s1_chunk(int col, int row)
{
    return s1_chunk((uint16_t)(col << 8), (uint16_t)(row << 8));
}

/* ---- DynWater_* --------------------------------------------------------- */
static void dyn_lz1(void)
{
    uint16_t d0 = r16(RAM_Camera_X_pos), d1;
    uint16_t py = r16(RAM_Player_1 + OBJ_y_pos);
    if (s_wtr_routine == 0) {
        d1 = 0xB8;
        if (d0 >= 0x600) {
            d1 = 0x108;
            if (py < 0x200) {                          /* .sonicishigh */
                if (d0 >= 0xC80) {
                    d1 = 0xE8;
                    if (d0 >= 0x1500) d1 = 0x108;
                }
            } else if (d0 >= 0xC00) {
                d1 = 0x318;
                if (d0 >= 0x1080) {
                    w8(RAM_Level_trigger_array + 5, 0x80);
                    d1 = 0x5C8;
                    if (d0 >= 0x1380) {
                        d1 = 0x3A8;
                        if (d1 == r16(RAM_Mean_water_level)) s_wtr_routine = 1;
                    }
                }
            }
        }
        w16(RAM_Target_water_level, d1);
        return;
    }
    if (s_wtr_routine != 1 || py >= 0x2E0) return;
    d1 = 0x3A8;
    if (d0 >= 0x1300) { d1 = 0x108; s_wtr_routine = 2; }
    w16(RAM_Target_water_level, d1);
}

static void dyn_lz2(void)
{
    uint16_t d0 = r16(RAM_Camera_X_pos);
    uint16_t d1 = 0x328;
    if (d0 >= 0x500) { d1 = 0x3C8; if (d0 >= 0xB00) d1 = 0x428; }
    w16(RAM_Target_water_level, d1);
}

static void dyn_lz3(void)
{
    uint16_t d0 = r16(RAM_Camera_X_pos), d1;
    uint16_t py = r16(RAM_Player_1 + OBJ_y_pos);
    switch (s_wtr_routine) {
    case 0:
        d1 = 0x900;
        if (d0 >= 0x600 && py >= 0x3C0 && py < 0x600) {
            d1 = 0x4C8;
            tri_lz3_patch(0x4B);
            s_wtr_routine = 1;
            o_sfx(0x6F);                               /* sfx_Rumble2 (S1 sfx_Rumbling) */
        }
        w16(RAM_Target_water_level, d1);
        w16(RAM_Mean_water_level, d1);             /* change water height instantly */
        return;
    case 1:
        d1 = 0x4C8;
        if (d0 >= 0x770) {
            d1 = 0x308;
            if (d0 >= 0x1400) {
                if (r16(RAM_Target_water_level) == 0x508 || py >= 0x600 || py < 0x280) {
                    d1 = 0x508;                        /* .sonicislow */
                    w16(RAM_Mean_water_level, d1);
                    if (d0 >= 0x1770) s_wtr_routine = 2;
                }
            }
        }
        w16(RAM_Target_water_level, d1);
        return;
    case 2:
        d1 = 0x508;
        if (d0 >= 0x1860) {
            d1 = 0x188;
            if (d0 >= 0x1AF0 || d1 == r16(RAM_Mean_water_level)) s_wtr_routine = 3;
        }
        w16(RAM_Target_water_level, d1);
        return;
    case 3:
        d1 = 0x188;
        if (d0 >= 0x1AF0) {
            d1 = 0x900;
            if (d0 >= 0x1BC0) {
                s_wtr_routine = 4;
                w16(RAM_Target_water_level, 0x608);
                w16(RAM_Mean_water_level, 0x7C0);
                w8(RAM_Level_trigger_array + 8, 1);
                return;
            }
        }
        w16(RAM_Target_water_level, d1);
        w16(RAM_Mean_water_level, d1);
        return;
    default:
        if (d0 >= 0x1E00) w16(RAM_Target_water_level, 0x128);
        return;
    }
}

static void dyn_sbz3(void)
{
    w16(RAM_Target_water_level, r16(RAM_Camera_X_pos) >= 0xF00 ? 0x4C8 : 0x228);
}

/* ---- LZWindTunnels (both players) ---------------------------------------- */
static void wind_tunnels(int act)
{
    static const uint16_t data[5][4] = {           /* left, top, right, bottom */
        { 0xA80, 0x300, 0xC10, 0x380 }, { 0xF80, 0x100, 0x1410, 0x180 },
        { 0x460, 0x400, 0x710, 0x480 }, { 0xA20, 0x600, 0x1610, 0x6E0 },
        { 0xC80, 0x600, 0x13D0, 0x680 } };
    if (r16(RAM_Debug_placement_mode)) return;
    int first = act == 0 ? 0 : act + 1, n = act == 0 ? 2 : 1;
    for (int i = 0; i < 2; i++) {
        uint32_t p = o_player(i);
        if (!o_player_active(p)) continue;
        uint16_t held = r8(i ? RAM_Ctrl_2_held_logical : RAM_Ctrl_1_held_logical);
        int in = 0;
        for (int k = first; k < first + n; k++) {
            uint16_t px = OB16(p, OBJ_x_pos), py = OB16(p, OBJ_y_pos);
            if (px < data[k][0] || px >= data[k][2] || py < data[k][1] || py >= data[k][3]) continue;
            in = 1;
            if (i == 0 && !(r8(RAM_Level_frame_counter + 1) & 0x3F))
                o_sfx(0x4B);                           /* sfx_Waterfall: S3K waterfall splash */
            if (tri_lz_wtunnel_allow) return;
            if (OB8(p, OBJ_routine) >= 4) { s_tunnel_mode[i] = 0; break; }
            s_tunnel_mode[i] = 1;
            /* the S1 tunnel-curve adjustment never runs (d0 is overwritten) */
            SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + 4);
            SET16(p, OBJ_x_vel, 0x400);
            SET16(p, OBJ_y_vel, 0);
            SET8(p, OBJ_anim, ANI_FLOAT);
            SET8(p, OBJ_status, OB8(p, OBJ_status) | 2);
            SET8(p, OBJ_double_jump, 0);
            if (held & 1) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 1);
            if (held & 2) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 1);
            break;
        }
        if (!in && s_tunnel_mode[i]) {
            SET8(p, OBJ_anim, ANI_WALK);
            s_tunnel_mode[i] = 0;
        }
    }
}

/* ---- LZWaterSlides (both players) ---------------------------------------- */
static void water_slides(void)
{
    static const uint8_t chunks[7] = { 2, 7, 3, 0x4C, 0x4B, 8, 4 };
    static const int8_t speeds[7] = { 10, -11, 10, -10, -11, -12, 11 };
    for (int i = 0; i < 2; i++) {
        uint32_t p = o_player(i);
        if (!o_player_active(p)) continue;
        int k = -1;
        if (!(OB8(p, OBJ_status) & 2)) {
            uint8_t c = s1_chunk(OB16(p, OBJ_x_pos), OB16(p, OBJ_y_pos));
            for (int j = 6; j >= 0; j--) if (chunks[j] == c) { k = j; break; }
        }
        if (k < 0) {
            if (OB8(p, OBJ_status2) & 0x80) {
                SET16(p, OBJ_move_lock, 5);
                SET8(p, OBJ_status2, OB8(p, OBJ_status2) & 0x7F);
            }
            continue;
        }
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
        SET16(p, OBJ_ground_vel, (uint16_t)((uint8_t)speeds[k] << 8));
        if (speeds[k] < 0) SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
        SET8(p, OBJ_anim, ANI_SLIDE);
        SET8(p, OBJ_status2, OB8(p, OBJ_status2) | 0x80);
        if (i == 0 && !(r8(RAM_Level_frame_counter + 1) & 0x1F))
            o_sfx(0x4B);
    }
}

/* DynamicWaterHeight: LZWaterFeatures minus the surface (done below) */
static void dynamic_water(void)
{
    int act = tri_act();
    tri_lz_surfaces();
    wind_tunnels(act);
    water_slides();
    switch (act) {
    case 0: dyn_lz1(); break;
    case 1: dyn_lz2(); break;
    case 2: dyn_lz3(); break;
    default: dyn_sbz3(); break;
    }
    int16_t d1 = r8(RAM_Water_speed);
    uint16_t t = r16(RAM_Target_water_level), m = r16(RAM_Mean_water_level);
    if (t == m) return;
    if (t < m) d1 = (int16_t)-d1;
    w16(RAM_Mean_water_level, (uint16_t)(m + d1));
}

/* The act's starting water height ($1000 = none), for the dev star-post start */
uint16_t tri_water_start_y(void)
{
    if (s2_water()) return tri_rd16(s1info() + S2I_WATER_Y);
    if (lz_water()) return tri_rd16(s1info() + S1I_WATER_Y);
    return 0x1000;
}

/* S2 DynamicWater: the act's routine, then Water_Level_2 -> Water_Level_3 */
static void s2_dynamic_water(void)
{
    extern void s2_water_surfaces(void);
    s2_water_surfaces();
    if (tri_zone() == TRI_S2_CPZ && tri_act() == 1 && r16(RAM_Camera_X_pos) >= 0x1DE0)
        w16(RAM_Target_water_level, 0x510);                 /* DynamicWaterCPZ2 */
    int16_t d1 = r8(RAM_Water_speed);
    uint16_t t = r16(RAM_Target_water_level), m = r16(RAM_Mean_water_level);
    if (t == m) return;
    if (t < m) d1 = (int16_t)-d1;
    w16(RAM_Mean_water_level, (uint16_t)(m + d1));
}

int tri_water_hook(uint32_t pc)
{
    switch (pc) {
    case TH_CHK_WATER:
        if (!lz_water() && !s2_water()) return 0;
        check_water();
        return 1;
    case TH_WATER_PAL:
        if (!lz_water() && !s2_water()) return 0;
        load_water_palette();
        return 1;
    case TH_DYN_WATER:
        if (lz_water()) dynamic_water();
        else if (s2_water()) s2_dynamic_water();
        else return 0;
        return 1;
    case TH_WATER_LEVEL:                       /* v_waterpos1 = v_waterpos2 + osc/2 (S2: not ARZ) */
        if (lz_water() || (s2_water() && tri_zone() != TRI_S2_ARZ))
            D(0) = (D(0) & 0xFFFF0000u) | (uint16_t)(D(0) + (r8(RAM_Oscillating_table + 2) >> 1));
        return 0;
    default:
        return 0;
    }
}
