/*
 * trilogy_level.c — level-load redirection.
 *
 * Every hook here sits right after (or on) the instruction where the retail
 * code fetches a per-zone table entry, and repoints the register at the
 * matching S3K-format structure in the expansion bank. The retail routine
 * then runs unchanged on our data.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "sonic3_video.h"
#include <stdio.h>
#include <stdlib.h>

static int s_plc_id = -1;

/* Development entry: TRILOGY_ZONE=2000 (hex zone/act) replaces the first
 * level the game loads. */
static void dev_force_zone(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    const char *z = getenv("TRILOGY_ZONE");
    if (!z || !*z)
        return;
    uint16_t za = (uint16_t)strtoul(z, NULL, 16);
    w16(RAM_Current_zone_and_act, za);
    w16(RAM_Apparent_zone_and_act, tri_apparent(za));
    fprintf(stderr, "[trilogy] dev: level forced to $%04X\n", za);
    const char *ch = getenv("TRILOGY_CHAR");           /* development: Player_mode (0 S+T, 1 S, 2 T, 3 K) */
    if (ch && *ch) { w16(RAM_Player_mode, (uint16_t)atoi(ch)); w16(0xFFFF0Au, (uint16_t)atoi(ch)); }   /* (+ Player_option, which S3K copies over) */
    /* TRILOGY_START=x,y[,maxy[,dle routine]]: begin as if respawning at a star post there */
    const char *st = getenv("TRILOGY_START");
    unsigned x = 0, y = 0, my = 0x1000, rt = 0;
    if (st && *st && sscanf(st, "%x,%x,%x,%x", &x, &y, &my, &rt) >= 2) {
        w8(0xFFFE2B, 1);                       /* Saved_last_star_post_hit */
        w8(RAM_Last_star_post_hit, 1);
        w16(0xFFFE2C, za);                     /* Saved_zone_and_act */
        w16(0xFFFF9A, tri_apparent(za));       /* Saved_apparent_zone_and_act */
        w16(0xFFFE2E, (uint16_t)x);            /* Saved_X_pos */
        w16(0xFFFE30, (uint16_t)y);            /* Saved_Y_pos */
        const char *rg = getenv("TRILOGY_RINGS");   /* development: start with rings */
        w16(0xFFFE32, (uint16_t)(rg && *rg ? atoi(rg) : 0));  /* Saved_ring_count */
        w32(0xFFFE34, 0);                      /* Saved_timer */
        w16(0xFFFE38, 0x0680);                 /* Saved_art_tile */
        w16(0xFFFE3A, 0x0C0D);                 /* Saved_solid_bits */
        w16(0xFFFE3C, (uint16_t)(x > 0xA0 ? x - 0xA0 : 0));  /* Saved_camera_X_pos */
        w16(0xFFFE3E, (uint16_t)(y > 0x60 ? y - 0x60 : 0));  /* Saved_camera_Y_pos */
        w16(0xFFFE44, (uint16_t)my);           /* Saved_camera_max_Y_pos */
        w8(0xFFFE46, (uint8_t)rt);             /* Saved_dynamic_resize_routine */
        extern uint16_t tri_water_start_y(void);
        w16(0xFFFE40, tri_water_start_y());    /* Saved_mean_water_level */
        fprintf(stderr, "[trilogy] dev: start at %X,%X\n", x, y);
    }
}

/* Development: the object audit (tools/trilogy/audit_objs.py). TRILOGY_AUDIT
 * names a file of hex "x y" points; the act restarts as if from a star post
 * at each in turn, and 90 frames after its title card the object table goes to
 * TRILOGY_AUDIT_OUT_nn.txt (slot, code, respawn entry, x, y, frame, render,
 * routine, subtype, status) with a screenshot beside it. Exits after the last. */
void tri_audit_frame(void)
{
    static int n = -1, idx, pts[256][2];
    static const char *out;
    if (n == 0) return;
    if (n < 0) {
        const char *f = getenv("TRILOGY_AUDIT");
        FILE *fp = f && *f ? fopen(f, "r") : NULL;
        n = 0;
        if (!fp) return;
        while (n < 256 && fscanf(fp, "%x %x", &pts[n][0], &pts[n][1]) == 2) n++;
        fclose(fp);
        out = getenv("TRILOGY_AUDIT_OUT");
        if (!out) out = "audit";
        idx = -1;
    }
    uint16_t fc = r16(RAM_Level_frame_counter);
    extern int tri_obj_card_done(void);
    static int since, pending;                 /* (S3K counts level frames under the title card; S2 doesn't) */
    if (pending && fc < 10) pending = 0;       /* (the restart has happened) */
    since = !pending && tri_obj_card_done() ? since + 1 : 0;
    if (idx >= 0 && since == 90) {
        char path[512];
        snprintf(path, sizeof path, "%s_%02d.txt", out, idx);
        FILE *fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "point %X %X camera %X %X\n", pts[idx][0], pts[idx][1],
                    r16(RAM_Camera_X_pos), r16(RAM_Camera_X_pos + 4));
            for (uint32_t s = 0; s < 110; s++) {
                uint32_t o = RAM_Object_RAM + s * OBJ_SIZE;
                if (!r32(o)) continue;
                fprintf(fp, "%u %06X %04X %04X %04X %02X %02X %02X %02X %02X\n", s, r32(o) & 0xFFFFFF,
                        r16(o + OBJ_respawn), r16(o + OBJ_x_pos), r16(o + OBJ_y_pos), r8(o + OBJ_frame),
                        r8(o + OBJ_render), r8(o + OBJ_routine), r8(o + OBJ_subtype), r8(o + OBJ_status));
            }
            fclose(fp);
        }
        extern int runner_write_screenshot_file(const char *path);
        snprintf(path, sizeof path, "%s_%02d.png", out, idx);
        runner_write_screenshot_file(path);
    }
    if ((idx < 0 && fc == 2) || (idx >= 0 && since == 91)) {
        if (++idx >= n) { fprintf(stderr, "[trilogy] audit: %d points\n", n); exit(0); }
        uint16_t za = r16(RAM_Current_zone_and_act), x = (uint16_t)pts[idx][0], y = (uint16_t)pts[idx][1];
        w8(0xFFFE2B, 1);                       /* Saved_last_star_post_hit */
        w8(RAM_Last_star_post_hit, 1);
        w16(0xFFFE2C, za);                     /* Saved_zone_and_act */
        w16(0xFFFE2E, x);                      /* Saved_X_pos */
        w16(0xFFFE30, y);                      /* Saved_Y_pos */
        w16(0xFFFE32, 0);                      /* Saved_ring_count */
        w32(0xFFFE34, 0);                      /* Saved_timer */
        w16(0xFFFE38, 0x0680);                 /* Saved_art_tile */
        w16(0xFFFE3A, 0x0C0D);                 /* Saved_solid_bits */
        w16(0xFFFE3C, (uint16_t)(x > 0xA0 ? x - 0xA0 : 0));   /* Saved_camera_X_pos */
        w16(0xFFFE3E, (uint16_t)(y > 0x60 ? y - 0x60 : 0));   /* Saved_camera_Y_pos */
        w16(0xFFFE44, 0x1000);                 /* Saved_camera_max_Y_pos */
        w8(0xFFFE46, 0);                       /* Saved_dynamic_resize_routine */
        extern uint16_t tri_water_start_y(void);
        w16(0xFFFE40, tri_water_start_y());    /* Saved_mean_water_level */
        w16(RAM_Restart_level_flag, 1);
        pending = 1;
        since = 0;
    }
}

/* the widescreen renderer draws rings itself: hand it our layout */
static unsigned ring_layout(void)
{
    uint32_t e = tri_dir();
    return e ? tri_rd32(e + TD_RINGS) : 0;
}

int tri_level_hook(uint32_t pc)
{
    switch (pc) {
    case TH_LEVEL_ENTRY: {
        extern void tri_flow_level_entry(void);
        extern void s1_obj_syz_reset(void);
        extern void s1_obj_lz_reset(void);
        extern void s1_obj_sbz_reset(void);
        extern void s2_slot_machine_reset(void);
        extern void s2_cnz_reset(void);
        extern void s2_obj_htz_reset(void);
        extern void tri_obj_level_entry(void);
        tri_obj_level_entry();
        s3_video_set_ring_layout(ring_layout);
        dev_force_zone();
        tri_flow_level_entry();
        s1_obj_syz_reset();
        s1_obj_lz_reset();
        s1_obj_sbz_reset();
        s2_slot_machine_reset();
        s2_cnz_reset();
        s2_obj_htz_reset();
        return 0;
    }
    case TH_LVL_ARTLOAD:            /* title card done: zone object art */
        if (tri_dir())
            tri_obj_upload_art();
        return 0;
    case TH_LVL_PLC1: {             /* move.b (a2,d1.w),d0  — first PLC of LevelLoadBlock */
        uint32_t e = tri_dir();
        if (e)
            A(2) = tri_rd32(e + TD_LLB) - (uint32_t)(int32_t)(int16_t)D(1);
        return 0;
    }
    case TH_LVL_MUSIC: {            /* move.b (a1,d0.w),d0  — LevelMusic_Playlist */
        uint32_t e = tri_dir();
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
        if (!e) {
            tri_music_restore();    /* S3K level: the driver's own songs */
            return 0;
        }
        tri_music_patch(TRI_SLOT_ZONE, tri_rd8(e + TD_MUSIC));
        if (t)                      /* the playlist read yields slot $20 */
            A(1) = tri_rd32(t + TRI_BLOB_MUSIC_SLOTS * 4u) - (uint32_t)(int32_t)(int16_t)D(0);
        return 0;
    }
    case TH_LLB1: {                 /* after lea (a4,d0.w),a4 */
        uint32_t e = tri_dir();
        if (e)
            A(4) = tri_rd32(e + TD_LLB);
        return 0;
    }
    case TH_LLB2: {                 /* after lea (a2,d0.w),a2 */
        uint32_t e = tri_dir();
        if (e)
            A(2) = tri_rd32(e + TD_LLB);
        return 0;
    }
    case TH_LOAD_LEVEL: {           /* after movea.l (a0,d0.w),a0 */
        uint32_t e = tri_dir();
        if (e)
            A(0) = tri_rd32(e + TD_LAYOUT);
        return 0;
    }
    case TH_LOAD_SOLIDS: {          /* after adda.l d0,a1 — a1 -> long collision ptr */
        uint32_t e = tri_dir();
        if (e)
            A(1) = e + TD_SOLIDS;
        return 0;
    }
    case TH_LEVEL_SIZES: {          /* after lea LevelSizes(pc,d0.w),a0 */
        uint32_t e = tri_dir();
        if (e)
            A(0) = e + TD_SIZES;
        return 0;
    }
    case TH_START_LOC: {            /* after lea (a1,d0.w),a1 */
        uint32_t e = tri_dir();
        if (e)
            A(1) = e + (r16(RAM_Player_mode) == 3 ? TD_START_K : TD_START_S);
        return 0;
    }
    case TH_PALPOINT:
    case TH_PALPOINT_I:
    case TH_PALPOINT2:
    case TH_PALPOINT2_I: {          /* after adda.w d0,a1 ; d0 = id*8 */
        uint32_t id = (D(0) & 0xFFFF) >> 3;
        if (id >= TRI_PAL_BASE) {
            uint32_t t = tri_rd32(TRI_BANK_BASE + TB_PALPOINT);
            uint32_t n = tri_rd16(TRI_BANK_BASE + TB_PALCOUNT);
            if (t && id - TRI_PAL_BASE < n)
                A(1) = t + (id - TRI_PAL_BASE) * 8u;
        }
        return 0;
    }
    case TH_LOAD_PLC_ID:            /* add.w d0,d0 ; d0 = PLC id */
        s_plc_id = (int)(D(0) & 0xFFFF);
        return 0;
    case TH_LOAD_PLC_PTR: {         /* after lea (a1,d0.w),a1 */
        int id = s_plc_id;
        s_plc_id = -1;
        if (id >= TRI_PLC_BASE) {
            uint32_t t = tri_rd32(TRI_BANK_BASE + TB_PLCTABLE);
            uint32_t n = tri_rd16(TRI_BANK_BASE + TB_PLCCOUNT);
            if (t && (uint32_t)(id - TRI_PLC_BASE) < n)
                A(1) = tri_rd32(t + (uint32_t)(id - TRI_PLC_BASE) * 4u);
        }
        return 0;
    }
    case TH_OBJ_INDEX: {            /* move.l d0,(Object_index_addr).w */
        uint32_t e = tri_dir();
        if (e && tri_rd32(e + TD_OBJPTRS))
            D(0) = tri_rd32(e + TD_OBJPTRS);
        return 0;
    }
    case TH_OBJ_LAYOUT: {           /* after movea.l (a0,d0.w),a0 */
        uint32_t e = tri_dir();
        if (e)
            A(0) = tri_rd32(e + TD_OBJECTS);
        return 0;
    }
    case TH_RING_LAYOUT: {          /* after movea.l (a1,d0.w),a1 */
        uint32_t e = tri_dir();
        if (e)
            A(1) = tri_rd32(e + TD_RINGS);
        return 0;
    }
    case TH_ANIMAL_TYPE: {          /* after lea byte_2C7BA(pc),a1 ; d1 = zone*2+n */
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
        if (tri_dir() && t) A(1) = tri_rd32(t + TRI_BLOB_ANIMAL_TYPES * 4);
        return 0;
    }
    case TH_ANIMAL_PLC: {           /* after lea PLCLoad_Animals_Index(pc,d0.w),a1 */
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
        if (tri_dir() && t) {
            uint32_t p = tri_rd32(tri_rd32(t + TRI_BLOB_ANIMAL_PLCS * 4) + (uint32_t)(tri_zone() - TRI_ZONE_BASE) * 4u);
            A(1) = p ? p : tri_rd32(t + TRI_BLOB_EMPTY_LIST * 4);
        }
        return 0;
    }
    case TH_OSC_INIT:               /* after lea (Osc_Data).l,a2 */
    case TH_OSC_DO: {               /* after lea (Osc_Data2).l,a2 */
        uint32_t e = tri_dir();
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
        if (e && t && tri_rd8(e + TD_GAME) == TRI_GAME_S1)
            A(2) = tri_rd32(t + (pc == TH_OSC_INIT ? TRI_BLOB_S1_OSC_INIT : TRI_BLOB_S1_OSC_DATA) * 4u);
        return 0;
    }
    case TH_ENEMY_ART: {            /* after adda.w (a6,d0.w),a6 */
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
        if (tri_dir() && t) A(6) = tri_rd32(t + TRI_BLOB_EMPTY_LIST * 4);
        return 0;
    }
    /* collision shape arrays: the zone's own game supplies them */
    TH_COL_ANGLE_CASES {
        uint32_t e = tri_dir();
        if (e) A(2) = tri_rd32(e + TD_ANGLES);
        return 0;
    }
    TH_COL_HEIGHT_CASES {
        uint32_t e = tri_dir();
        if (e) A(2) = tri_rd32(e + TD_HEIGHTS);
        return 0;
    }
    TH_COL_HEIGHTROT_CASES {
        uint32_t e = tri_dir();
        if (e) A(2) = tri_rd32(e + TD_HEIGHTSR);
        return 0;
    }
    default:
        return 0;
    }
}
