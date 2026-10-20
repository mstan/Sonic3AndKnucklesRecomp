/*
 * trilogy_flow.c — act/zone progression for Sonic 1 / Sonic 2 campaigns.
 *
 * Act ends: the game's own signpost / prison capsule (transcribed), then the
 * retail Sonic 3 results tally. When the tally is done
 * we load the next act of the campaign as a fresh level (Restart_level_flag),
 * instead of S3K's seamless act-2 transition.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>

/* (zone<<8|act) sequence of the Sonic 1 campaign.
 * S1 internals: SBZ act 3 is "LZ act 4", Final Zone is "SBZ act 3". */
static const uint16_t s1_order[] = {
    0x2000, 0x2001, 0x2002,     /* Green Hill */
    0x2200, 0x2201, 0x2202,     /* Marble */
    0x2400, 0x2401, 0x2402,     /* Spring Yard */
    0x2100, 0x2101, 0x2102,     /* Labyrinth */
    0x2300, 0x2301, 0x2302,     /* Star Light */
    0x2500, 0x2501, 0x2103,     /* Scrap Brain 1, 2, 3 (= LZ4) */
    0x2502,                     /* Final Zone */
};

/* Sonic 2 campaign (S2 level order; MTZ act 3 is S2's zone 5) */
static const uint16_t s2_order[] = {
    0x2800, 0x2801,             /* Emerald Hill */
    0x2900, 0x2901,             /* Chemical Plant */
    0x2A00, 0x2A01,             /* Aquatic Ruin */
    0x2B00, 0x2B01,             /* Casino Night */
    0x2C00, 0x2C01,             /* Hill Top */
    0x2D00, 0x2D01,             /* Mystic Cave */
    0x2E00, 0x2E01,             /* Oil Ocean */
    0x2F00, 0x2F01, 0x2F02,     /* Metropolis */
    0x3000,                     /* Sky Chase */
    0x3100,                     /* Wing Fortress */
    0x3200,                     /* Death Egg */
};

uint16_t tri_apparent(uint16_t za);
/* ------------------------------------------------------------------------
 * Giant ring at the end of an act (S1): the ring's flash ends with Sonic
 * gone; the act-clear tally runs (started here if the signpost was never
 * reached) and only when it finishes does the special stage begin. After
 * the special stage the next act starts, as in Sonic 1.
 * ---------------------------------------------------------------------- */
static int s_ss_pending;          /* tally running, special stage queued */
static int s_ss_advance;          /* back from the special stage: next act */
int tri_sbz2_open;                /* SBZ2 tally done: boundary opening (s1dle) */
static int s_sbz2_ss;             /* SBZ2's giant ring entered: its stage before SBZ3 */
static int s_results_started, s_results_finished;
static int s_runoff;              /* the tally's run off screen: bit 0 Player 1, bit 1 Player 2;
                                   * bits 2, 3: gone (off screen, held there) */

static uint32_t find_obj(uint32_t code)
{
    for (uint32_t s = 0; s < 110; s++) {
        uint32_t o = 0xFFB000u + s * OBJ_SIZE;
        if (r32(o) == code) return o;
    }
    return 0;
}

static void start_act_clear(void)
{
    if (s_results_started) return;
    if (find_obj(S3K_Obj_LevelResults)) return;      /* tally already running */
    uint32_t sign = find_obj(NATIVE_S1_BASE + 0x0D * 4);
    if (sign && OB8(sign, OBJ_routine) >= 8) return; /* sign spawned it */
    if (sign) SET8(sign, OBJ_routine, 8);            /* sign: straight to "after" */
    w8(RAM_Update_HUD_timer, 0);                     /* GotThroughAct */
    w16(RAM_Camera_min_X_pos, r16(RAM_Camera_max_X_pos));
    w8(RAM__unkFABE, 0xFF);
    w8(RAM_Ctrl_2_locked, 0xFF);
    uint32_t a1 = o_alloc();
    if (a1) { s_results_started = 1; SET32(a1, OBJ_code, S3K_Obj_LevelResults); }
}

/* SSEntryFlash_GoSS, trilogy act: queue the stage instead of entering it */
static int ss_go(void)
{
    uint32_t a0 = A(0) & 0xFFFFFF;
    uint8_t sub = OB8(a0, OBJ_subtype);
    w32(RAM_Collected_special_ring_array,
        r32(RAM_Collected_special_ring_array) | (1u << (sub & 31)));
    s_ss_pending = 1;
    start_act_clear();
    A(0) = RAMPTR(a0);
    tri_jsr(S3K_Go_Delete_SpriteSlotted2);
    return 1;
}

/* tally finished with a special stage queued (loc_61892 + next act) */
static void ss_enter(uint16_t next)
{
    s_ss_pending = 0;
    extern int tri_ss_half_pipe(int game);
    if (tri_ss_half_pipe(tri_rd8(tri_dir() + TD_GAME))) {   /* SPECIAL STAGES: the half-pipe / UFOs, then the next act */
        extern void tri_ss_half_pipe_then(uint16_t next);
        tri_jsr(S3K_Clear_SpriteRingMem);
        tri_ss_half_pipe_then(next);
        s_ss_advance = 0;
        return;
    }
    extern void tri_ss_prepare_blue_spheres(void);
    tri_ss_prepare_blue_spheres();
    w16(RAM_Current_zone_and_act, next);
    w16(RAM_Apparent_zone_and_act, tri_apparent(next));
    w8(RAM_Last_star_post_hit, 0);
    /* the special stage returns to Saved2_zone_and_act (loc_978E) */
    w16(0xFFFE4A, next);                             /* Saved2_zone_and_act */
    w16(0xFFFF9C, tri_apparent(next));               /* Saved2_apparent_zone_and_act */
    w8(0xFFFE49, 0);                                 /* Saved2_last_star_post_hit */
    o_sfx(0xAF);                                     /* sfx_EnterSS */
    tri_jsr(S3K_Clear_SpriteRingMem);
    w8(RAM_Special_bonus_entry_flag, 1);
    w8(RAM_Game_mode, 0x34);
    w8(RAM_Respawn_table_keep, 1);
    s_ss_advance = 1;
    fprintf(stderr, "[trilogy] special stage, then $%04X\n", next);
}

/* Level entry: back from a queued special stage, start the next act fresh */
void tri_flow_level_entry(void)
{
    s_results_started = s_results_finished = 0;
    /* Each act has its own giant/big rings (subtypes from 0 per act): S3K
     * clears Collected_special_ring_array only in its own act transitions, and
     * the special stage's return restores it, so a new act clears it here;
     * a restart in the same act (death, star post, back from the stage) keeps it. */
    static uint16_t ring_za = 0xFFFF;
    uint16_t za = r16(RAM_Current_zone_and_act);
    if (za != ring_za) {                        /* (a save file's copy too: S3K only saves it cleared) */
        w32(RAM_Collected_special_ring_array, 0);
        ring_za = za;
    }
    tri_sbz2_open = 0;
    s_runoff = 0;
    if (za != 0x2501) s_sbz2_ss = 0;
    w8(RAM_Disable_death_plane, 0);
    if (!s_ss_advance) return;
    s_ss_advance = 0;
    w8(RAM_Special_bonus_entry_flag, 0);
    w8(RAM_Last_star_post_hit, 0);
}

uint16_t tri_next_act(uint16_t za)
{
    static const uint16_t megamix[]={0x3400,0x3401,0x3402,0x3600,0x3601,0x3602,
        0x3800,0x3801,0x3802,0x3500,0x3501,0x3502,0x3700,0x3701,0x3702,0x3900,0x3901,0x3503,0x3902};
    for(unsigned i=0;i+1<sizeof megamix/sizeof megamix[0];i++)if(megamix[i]==za)return megamix[i+1];
    int z = za >> 8, act = za & 0xFF;
    if (tri_cd_zone(z)) {                        /* Sonic CD: acts 1, 2 in the present, then act 3's future */
        int round = (z - TRI_CD_BASE) / 4;
        extern int scd_act3_good(int round);
        if (act == 0) return (uint16_t)((TRI_CD_ZONE(round, TRI_CD_PRESENT) << 8) | 1);   /* (each act starts in the present) */
        if (act == 1)                                /* (Results: good futures in acts 1 and 2 -> act 3's) */
            return (uint16_t)((TRI_CD_ZONE(round, scd_act3_good(round) ? TRI_CD_GOOD : TRI_CD_BAD) << 8) | 2);
        return round < 6 ? (uint16_t)(TRI_CD_ZONE(round + 1, TRI_CD_PRESENT) << 8) : 0;
    }
    for (unsigned i = 0; i + 1 < sizeof s1_order / sizeof s1_order[0]; i++)
        if (s1_order[i] == za)
            return s1_order[i + 1];
    for (unsigned i = 0; i + 1 < sizeof s2_order / sizeof s2_order[0]; i++)
        if (s2_order[i] == za)
            return s2_order[i + 1];
    return 0;
}

/* Apparent (title card / results) zone-act for an internal one. */
uint16_t tri_apparent(uint16_t za)
{
    if (za == 0x2103) return 0x2502;          /* SBZ3 shows as Scrap Brain act 3 */
    if (za == 0x2502) return 0x2600;          /* Final Zone */
    if (za == 0x3503)return 0x3902;
    if (za == 0x3902)return 0x3A00;
    return za;
}

/* Final Zone: Eggman got away (id_Ending). Sonic 1 is done: on to Sonic 2's
 * Emerald Hill, emeralds and lives carried over (they stay in RAM). */
void tri_s1_complete(void)
{
    if(tri_rd8(tri_dir()+TD_GAME)==TRI_GAME_MEGAMIX) {
        extern void tri_save_megamix_complete(void);
        tri_save_megamix_complete();
        w8(RAM_Game_mode,4);
        fprintf(stderr,"[megamix] Final Fight escape complete: campaign finished\n");
        return;
    }
    extern int s3_stage_pack_sonic2,s3_stage_pack_cd;
    uint16_t next=s3_stage_pack_sonic2?0x2800:s3_stage_pack_cd?0x4000:0;
    fprintf(stderr, "[stage-pack] Sonic 1 complete: next campaign %04X\n",next);
    tri_ach_act_clear(0x2502);
    w16(RAM_Current_zone_and_act, next);
    w16(RAM_Apparent_zone_and_act, next);
    w8(RAM_Last_star_post_hit, 0);
    w16(RAM_Restart_level_flag, 1);
    extern void tri_save_progress(uint16_t next_za);
    tri_save_progress(next);
}

/* SBZ2: the tally does not end the act (Got_SBZ2 / Got_Boundary). Control
 * comes back, the Final Zone theme starts and the right boundary opens up
 * to $2100 for the Eggman cutscene; the act ends when Sonic falls through
 * the collapsing floor (s1dle). */
static void sbz2_after_tally(void)
{
    w8(RAM__unkFAA8, 0);
    w8(RAM__unkFABE, 0);
    w8(RAM_Ctrl_1_locked, 0);
    w8(RAM_Ctrl_2_locked, 0);
    for (int i = 0; i < 2; i++) {
        uint32_t p = o_player(i);
        if (!o_player_active(p)) continue;
        A(1) = RAMPTR(p);
        tri_jsr(S3K_Restore_PlayerControl2);
    }
    uint32_t sign = find_obj(NATIVE_S1_BASE + 0x0D * 4);
    if (sign) SET8(sign, OBJ_routine, 10);           /* no more end poses */
    tri_music_play(TRI_SLOT_ZONE, 0x8D);              /* bgm_FZ */
    tri_sbz2_open = 1;
    fprintf(stderr, "[trilogy] SBZ2 tally done: on to Eggman\n");
}

/* SBZ2's floor gives way (trilogy_s1dle.c): with its giant ring entered, the
 * special stage comes before SBZ3. Returns 1 if it took over. */
int tri_sbz2_fall(void)
{
    if (!s_sbz2_ss) return 0;
    s_sbz2_ss = 0;
    ss_enter(tri_source_stage(0x2103));
    return 1;
}

/* Results finished (loc_2DCE2): go to the next act as a fresh level. */
static int results_done(void)
{
    /* Multiple signposts can share an X coordinate (SYZ2's two routes).
     * A second tally must never advance the next, still-unloaded act. */
    if (s_results_finished) { o_delete(A(0) & 0xFFFFFF); return 1; }
    s_results_finished = 1;
    uint16_t za = r16(RAM_Current_zone_and_act);
    tri_ach_act_clear(za);
    if (za == 0x2501 || za==0x3901) {             /* SBZ2 / Metallic Base 2 cutscene */
        s_sbz2_ss = s_ss_pending;                    /* waits for the fall into SBZ3 (tri_sbz2_fall) */
        s_ss_pending = 0;
        sbz2_after_tally();
        o_delete(A(0) & 0xFFFFFF);
        return 1;
    }
    uint16_t next = tri_next_act(za);
    if (!next && tri_cd_zone(za >> 8)) {
        extern void scd_audio_reset(void);
        extern void tri_save_progress(uint16_t);
        fprintf(stderr, "[trilogy] Sonic CD complete after $%04X -> Angel Island\n", za);
        scd_audio_reset();
        s_ss_pending = 0;
        w16(RAM_Current_zone_and_act,0);w16(RAM_Apparent_zone_and_act,0);
        w8(RAM_Last_star_post_hit,0);w16(RAM_Restart_level_flag,1);w8(RAM_Game_mode,0x0C);
        tri_save_progress(0);
        o_delete(A(0) & 0xFFFFFF);
        return 1;
    }
    if (!next) {
        fprintf(stderr, "[trilogy] campaign complete after $%04X\n", za);
        next = za;                               /* ending handled later */
    }
    if (s_ss_pending) {                          /* giant ring: special stage first */
        ss_enter(next);
        o_delete(A(0) & 0xFFFFFF);
        return 1;
    }
    w16(RAM_Current_zone_and_act, next);
    w16(RAM_Apparent_zone_and_act, tri_apparent(next));
    w8(RAM_Last_star_post_hit, 0);
    w16(RAM_Restart_level_flag, 1);
    o_delete(A(0) & 0xFFFFFF);
    fprintf(stderr, "[trilogy] act clear $%04X -> $%04X\n", za, next);
    {
        extern int tri_za_to_code(uint16_t za);
        extern void tri_save_progress(uint16_t next_za);
        if (tri_za_to_code(next) != tri_za_to_code(za)) tri_save_progress(next);   /* a zone cleared */
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * The act's end with Sonic 1 / 2 / CD sprites in their games' levels: those
 * games' players run off to the right during the tally (S1 Sign_SonicRun,
 * S2 Obj0D, CD's goal post holding right) instead of S3K's ending pose. The
 * right boundary gives them $40 more outside boss arenas, as in S1 and S2's
 * Sonic_LevelBound (and the wider screen's extra). Off the screen (past
 * its right edge, or fallen below it) a runner is gone: held where he is,
 * out of play; nothing kills him meanwhile (Disable_death_plane), so a pit
 * past the sign never restarts the act.
 * ---------------------------------------------------------------------- */
static int runoff_wanted(void)
{
    return tri_dir() && tri_rd8(tri_dir()+TD_GAME)==TRI_GAME_CD;
}

static int runoff_gone(uint32_t p)                   /* (off the screen: right, or below) */
{
    extern int s3_video_canvas_width(void);
    int extra = s3_video_canvas_width() - 320;
    int dx = (int16_t)(OB16(p, OBJ_x_pos) - r16(RAM_Camera_X_pos));
    int dy = (int16_t)(OB16(p, OBJ_y_pos) - r16(RAM_Camera_Y_pos));
    return dx >= 320 + (extra > 0 ? extra : 0) + 24 || dy >= 224 + 48;
}

static void runoff_tick(void)
{
    if (!s_runoff) return;
    w8(RAM_Disable_death_plane, 1);
    for (int n = 0; n < 2; n++) {
        uint32_t p = n ? RAM_Player_2 : RAM_Player_1;
        if (!(s_runoff & (1 << n))) continue;
        if (!(s_runoff & (4 << n)) && runoff_gone(p)) s_runoff |= 4 << n;
        if (s_runoff & (4 << n)) {                   /* gone: still, out of play */
            SET8(p, 0x2E, 0x81);                     /* object_control */
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, 0);
            SET16(p, 0x1C, 0);                       /* ground_vel */
            continue;
        }
        if (n) {
            w8(RAM_Ctrl_2_locked, 0xFF);             /* (negative: no CPU steering) */
            w16(0xFFF66Au, 0x0800);                  /* Ctrl_2_logical: right held */
        } else {
            w8(RAM_Ctrl_1_locked, 1);
            w16(0xFFF602u, 0x0800);                  /* Ctrl_1_logical: right held */
        }
        SET8(p, 0x2E, 0);                            /* object_control */
    }
}

int tri_flow_hook(uint32_t pc)
{
    switch (pc) {
    case TH_ENDING_POSE: {
        if(!tri_cd_zone(tri_zone()))return 0;
        uint32_t a1 = A(1) & 0xFFFFFFu;
        int bit = a1 == RAM_Player_1 ? 1 : a1 == RAM_Player_2 ? 2 : 0;
        if (!bit || !runoff_wanted()) return 0;
        s_runoff |= bit;
        SET8(a1, 0x3D, 0);                           /* spin_dash_flag */
        SET8(a1, OBJ_status, OB8(a1, OBJ_status) & ~0x20);   /* (not pushing) */
        runoff_tick();
        return 1;                                    /* (no pose: rts) */
    }
    case TH_RIGHT_BOUND: {                           /* d0 = Camera_max_X_pos + $128 */
        uint32_t a0 = A(0) & 0xFFFFFFu;
        int bit = a0 == RAM_Player_1 ? 1 : a0 == RAM_Player_2 ? 2 : 0;
        extern int scd_mmz3_ending_margin(void);
        int cd_margin = bit == 1 ? scd_mmz3_ending_margin() : 0;
        if (cd_margin) {
            /* MMZ's pad script turns at $F90. S3K's $E60+$128
             * clamp is $F88, so it could never reach the turn. Supply
             * CD's original player margin without moving the camera. */
            D(0) = (D(0) & 0xFFFF0000u) | (uint16_t)(D(0) + cd_margin);
        }
        if ((s_runoff & bit) && !r8(RAM_Boss_flag)) {
            extern int s3_video_canvas_width(void);
            int extra = s3_video_canvas_width() - 320;
            D(0) = (D(0) & 0xFFFF0000u) | (uint16_t)(D(0) + 0x40 + (extra > 0 ? extra : 0));
        }
        return 0;
    }
    case TH_LVL_PRE_SPRITES:
        runoff_tick();
        return 0;
    case TH_RESULTS_DONE:
        if (!tri_dir()) return 0;
        return results_done();
    case TH_SS_GO:
        if (!tri_dir()) {                            /* S3K's own rings: the half-pipe if chosen */
            extern int tri_ss_ring_half_pipe(void);
            return tri_ss_ring_half_pipe();
        }
        return ss_go();
    case TH_STARPOST_STARS:         /* blo.s: S1 has giant rings; S2's stars come at 50 rings */
        if (tri_dir()) {
            extern int tri_ss_stars_wanted(void);
            if (tri_rd8(tri_dir() + TD_GAME) == TRI_GAME_S2 && tri_ss_stars_wanted()) g_cpu.SR &= ~1u;
            else g_cpu.SR |= 1u;
            if (getenv("TRILOGY_MOVELOG"))              /* development */
                fprintf(stderr, "[STARS] game %d wanted %d SR %04X\n", tri_rd8(tri_dir() + TD_GAME),
                        tri_ss_stars_wanted(), g_cpu.SR);
        }
        return 0;
    case TH_RESULTS_MUSIC:          /* the tally's jingle: the act's game's own */
        if (tri_dir()) {
            int g = tri_rd8(tri_dir() + TD_GAME);
            if (g == TRI_GAME_CD) {
                scd_pcm_cmd(SCD_PCM_STOP);               /* (the past's song) */
                scd_cd_play(SCD_CD_RESULTS, 0);
                return 1;                                /* (skips jmp Play_Music) */
            }
            if (g == TRI_GAME_S1 || g == TRI_GAME_S2) {
                tri_music_play(TRI_SLOT_ZONE, g == TRI_GAME_S1 ? 0x8E : 0x9A);   /* bgm_GotThrough, Mus_EndLevel */
                return 1;
            }
        }
        return 0;
    case TH_SPRING_NEAR:            /* Sonic 1 and CD sideways springs fire only when pushed, */
        if (tri_dir()) {            /* not on a run-up within $28 pixels (S2 added that)      */
            int g = tri_rd8(tri_dir() + TD_GAME);
            if (g == TRI_GAME_CD) return 1;
        }
        return 0;
    case TH_STARS_TOUCH:            /* the star circle: Sonic 2's special stage, not a bonus stage */
        if (tri_dir()) {
            extern int tri_ss_stars_touch(void);
            return tri_ss_stars_touch();
        }
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------
 * Object 0D - Sonic 1 signpost (transcribed): stands in the level, spins
 * when passed, then hands over to the Sonic 3 results tally exactly as
 * Obj_EndSign does once it has landed.
 * ---------------------------------------------------------------------- */
#define SIGN_SPINTIME   0x30    /* spintime */
#define SIGN_SPARKTIME  0x32    /* sparkletime */
#define SIGN_SPARKID    0x34    /* sparkle_id */

static const int8_t sign_spark_pos[16] = {
    -0x18, -0x10,  8, 8,  -0x10, 0,  0x18, -8,  0, -8,  0x10, 0,  -0x18, 8,  0x18, 0x10,
};

/* Sign_Spin's sparkles: S3K's ring object in its sparkle routine, which is
 * what S1 used too (id_Rings, routine 6). */
static void sign_sparkle(uint32_t o)
{
    if ((int16_t)(OB16(o, SIGN_SPARKTIME) - 1) >= 0) {
        SET16(o, SIGN_SPARKTIME, OB16(o, SIGN_SPARKTIME) - 1);
        return;
    }
    SET16(o, SIGN_SPARKTIME, 0x0B);
    int d0 = OB8(o, SIGN_SPARKID);
    SET8(o, SIGN_SPARKID, (uint8_t)((d0 + 2) & 0x0E));
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Ring);
    SET8(a1, OBJ_routine, 6);                 /* Obj_RingSparkle */
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + sign_spark_pos[d0]));
    SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + sign_spark_pos[d0 + 1]));
    SET32(a1, OBJ_mappings, S3K_Map_Ring);
    SET16(a1, OBJ_art_tile, 0x2000 | 0x6BC);  /* make_art_tile(ArtTile_Ring,1,0) */
    SET8(a1, OBJ_render, 4);
    SET16(a1, OBJ_priority, 2 << 7);
    SET8(a1, OBJ_width_px, 8);
}

/* Sonic 2 (Obj0D) is the same sign with its own art; S2 has none in second
 * acts (a capsule ends those) except Metropolis 2. */
static int sign_asset(void)
{
    return tri_rd8(tri_dir() + TD_GAME) == TRI_GAME_S2 ? S2A_SIGNPOST : S1A_SIGN;
}

static void s1_signpost(uint32_t o)
{
    if (!OB8(o, OBJ_routine) && tri_rd8(tri_dir() + TD_GAME) == TRI_GAME_S2
        && tri_act() && !(tri_zone() == TRI_S2_MTZ && tri_act() == 1)) {
        o_delete(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                     /* Sign_Main */
        SET8(o, OBJ_routine, 2);
        o_init(o, sign_asset(), 4, 4, 0x18, 0x20);
        /* fall through */
    case 2: {                                   /* Sign_Touch */
        int16_t dx = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (dx >= 0 && dx < 0x20) {
            o_sfx(0xB8);                        /* sfx_Signpost */
            w8(RAM_Update_HUD_timer, 0);        /* stop time counter */
            w16(RAM_Camera_min_X_pos, r16(RAM_Camera_max_X_pos));
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    case 4:                                     /* Sign_Spin */
        SET16(o, SIGN_SPINTIME, OB16(o, SIGN_SPINTIME) - 1);
        if ((int16_t)OB16(o, SIGN_SPINTIME) < 0) {
            SET16(o, SIGN_SPINTIME, 60);
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
            if (OB8(o, OBJ_anim) == 3) {
                SET8(o, OBJ_routine, 6);
                w8(RAM__unkFABE, 0xFF);         /* as Obj_EndSignLanded */
                w8(RAM_Ctrl_2_locked, 0xFF);
            }
        }
        sign_sparkle(o);
        break;
    case 6:                                     /* -> Obj_EndSignResults */
        if (!(r8(RAM_Player_1 + OBJ_status) & 2)) {
            SET8(o, OBJ_routine, 8);
            if (s_results_started) break;
            uint32_t a1 = o_alloc();
            if (!a1) { SET8(o, OBJ_routine, 6); break; }
            s_results_started = 1;
            SET32(a1, OBJ_code, S3K_Obj_LevelResults);
            A(1) = RAMPTR(RAM_Player_1);
            A(0) = RAMPTR(o);
            tri_jsr(S3K_Set_PlayerEndingPose);
        }
        break;
    case 8:                                     /* Obj_EndSignAfter */
        A(0) = RAMPTR(o);
        tri_jsr(S3K_Check_TailsEndPose);
        break;
    default:                                    /* SBZ2: the act goes on */
        break;
    }
    o_animate(o, o_asset_anim(sign_asset()));
    if (o_asset_ready(sign_asset())) o_draw(o);         /* S2: art comes in at the camera lock */
    if (OB8(o, OBJ_routine) == 2 && o_out_of_range(o, OB16(o, OBJ_x_pos)))
        o_delete_respawnable(o);
}

/* ------------------------------------------------------------------------
 * Object 4B - Sonic 1 giant ring (GRing_Main): at its S1 spot, it appears
 * once the player holds 50 rings. It then becomes the Sonic 3 special
 * stage ring (Blue Spheres / Super Emerald ring), whose subtype
 * is this ring's index within the act (Collected_special_ring_array bit).
 * ---------------------------------------------------------------------- */
static void s1_giant_ring(uint32_t o)
{
    /* Emeralds are the game's own (S3K Chaos + Super): with all 7 Chaos
     * Emeralds the ring is a Super Emerald ring, as on S&K levels. */
    extern int tri_ss_available(void);
    if (r16(RAM_Ring_count) >= 50 && tri_ss_available()) {   /* GRing_Okay (while there is an emerald left to win) */
        SET32(o, OBJ_code, S3K_Obj_SSEntryRing);
        SET8(o, OBJ_routine, 0);
        return;
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos)))
        o_delete_respawnable(o);
}

/* ------------------------------------------------------------------------
 * Object 49 - GHZ waterfall sound (WaterSound): every 64 frames while in
 * range. S3K has no S1 waterfall sound; its waterfall splash stands in.
 * ---------------------------------------------------------------------- */
static void s1_water_sound(uint32_t o)
{
    if (!(r8(RAM_Level_frame_counter + 1) & 0x3F))
        o_sfx(0x4B);                            /* sfx_WaterfallSplash */
    if (o_out_of_range(o, OB16(o, OBJ_x_pos)))
        o_delete_respawnable(o);
}

/* ------------------------------------------------------------------------
 * Object 7D - hidden points at the end of a level (HiddenBonus).
 * ---------------------------------------------------------------------- */
#define BONUS_TIME  0x30        /* bonus_timelen */

static void s1_hidden_bonus(uint32_t o)
{
    static const uint16_t points[4] = { 0, 1000, 100, 1 };   /* REV01 (1 = the bug) */
    if (OB8(o, OBJ_routine) == 0) {             /* Bonus_Main */
        uint16_t dx = (uint16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x10);
        uint16_t dy = (uint16_t)(r16(RAM_Player_1 + OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x10);
        if (dx < 0x20 && dy < 0x20 && !r16(RAM_Debug_placement_mode)) {
            SET8(o, OBJ_routine, 2);
            o_init(o, S1A_BONUS, 4, 0, 0x10, 0x10);
            SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);   /* Tile_Prio */
            SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
            SET16(o, BONUS_TIME, 119);
            o_sfx(0xB0);                        /* sfx_Bonus (S3K: register) */
            D(0) = points[OB8(o, OBJ_subtype) & 3];
            tri_jsr(S3K_HUD_AddToScore);
        }
        if (o_out_of_range(o, OB16(o, OBJ_x_pos)))
            o_delete(o);
        return;
    }
    SET16(o, BONUS_TIME, OB16(o, BONUS_TIME) - 1);   /* Bonus_Display */
    if ((int16_t)OB16(o, BONUS_TIME) < 0 || o_out_of_range(o, OB16(o, OBJ_x_pos))) {
        o_delete(o);
        return;
    }
    o_draw(o);
}

void s1_obj_flow_register(TriObjFn *t)
{
    t[0x49] = s1_water_sound;
    t[0x7D] = s1_hidden_bonus;
    t[0x0D] = s1_signpost;
    t[0x4B] = s1_giant_ring;
}
