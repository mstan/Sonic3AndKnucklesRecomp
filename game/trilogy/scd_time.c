/*
 * scd_time.c — Sonic CD's time travel (dobby: Level/_Objects/Monitor and Time
 * Post, Level/_Objects/Sonic ObjSonic_TimeWarp / SaveTimeWarpData,
 * Level/Main.asm RunTimeWarp).
 *
 * A time post (monitor subtype 8 past, 9 future; scd_level.py makes them
 * object $80) sets the direction. Then running at $600 or faster keeps the
 * warp going: dropping under it in the first 90 frames waits for the speed
 * again, after that ends it. From the first fast frame there are sparkles
 * and badniks break on touch (here S3K's invincibility stars, with no time
 * on them so no music). At 210 frames the screen holds and the new period
 * is chosen (the future is good once the act's generator is gone); at 230
 * the music fades, the level goes white and the time warp cutscene plays
 * (trilogy_scdwarp.c); then the act reloads in that period, where the player
 * was, still running: no title card (S3K's Act3_flag), the level fading in
 * from white as Sonic CD's does.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_Respawn_table_keep  0xFFFF97u
#define RAM_Extra_life_flags    0xFFFE1Bu
#define RAM_Update_HUD_rings    0xFFFE1Du
#define RAM_Invincibility_stars 0xFFCD7Cu
#define S3K_Obj_Invincibility   0x0187F0u
#define OBJ_parent              0x42
#define OBJ_status2             0x2B   /* status_secondary (bit 1: invincible) */
#define OBJ_invinc_time         0x35
#define SLOT_TIMEICON           0x3A1  /* (scd_obj.c registers it; the post is $3A0) */
#define RAM_Act3_flag           0xFFEE5Eu      /* (S3K: no title card this load) */
#define S3K_Pal_FillWhite       0x003C62u
#define OBJ_invuln              0x34
static int s_arrive;                   /* 1: arriving from a warp (white, no card), 2: card's work done */

static int8_t s_dir;                   /* timeWarpDir: -1 past, 1 future */
static uint16_t s_timer;               /* timeWarpTimer */
static uint8_t s_held, s_stars;        /* (the new period chosen, screen held; our sparkles) */
static uint32_t s_icon;
static struct {
    int on;
    uint16_t za, x, y, cx, cy, maxy, rings;
    int16_t gvel, xvel, yvel;
    uint8_t status, lives;
    uint32_t time;
} s_warp;
static uint8_t s_good[7][3];           /* the act's generator destroyed (good future), per round */

int scd_time_dir(void) { return s_dir; }

static int round_of(int z) { return (z - TRI_CD_BASE) >> 2; }
static int time_zone_of(int period) { return period == TRI_CD_PAST ? 0 : period == TRI_CD_PRESENT ? 1 : 2; }

int scd_good_future(void)              /* goodFuture: the future is good (this act's) */
{
    extern uint8_t tri_cd_stones(void);
    if (!tri_cd_zone(tri_zone()) || tri_act() >= 3) return 0;
    return tri_cd_stones()==127 || cd_period() == TRI_CD_GOOD || s_good[round_of(tri_zone())][tri_act()];
}

int scd_act3_good(int round)            /* goodFutureFlags = %11: act 3 in the good future */
{
    extern uint8_t tri_cd_stones(void);
    return round>=0 && round<7 && (tri_cd_stones()==127 || (s_good[round][0] && s_good[round][1]));
}

static uint8_t s_proj[7][3];           /* projDestroyed: the act's projector gone (animals free) */
void scd_time_export(uint16_t *generators,uint16_t *projectors)
{
    *generators=*projectors=0;
    for(unsigned r=0;r<7;r++)for(unsigned a=0;a<2;a++) {
        if(s_good[r][a])*generators|=(uint16_t)(1u<<(r*2+a));
        if(s_proj[r][a])*projectors|=(uint16_t)(1u<<(r*2+a));
    }
}
void scd_time_import(uint16_t generators,uint16_t projectors)
{
    memset(s_good,0,sizeof s_good);memset(s_proj,0,sizeof s_proj);
    for(unsigned r=0;r<7;r++)for(unsigned a=0;a<2;a++) {
        s_good[r][a]=(uint8_t)((generators>>(r*2+a))&1);
        s_proj[r][a]=(uint8_t)((projectors>>(r*2+a))&1);
    }
}
int scd_proj_destroyed(void)
{
    return tri_cd_zone(tri_zone()) && tri_act() < 3 && s_proj[round_of(tri_zone())][tri_act()];
}
void scd_proj_destroyed_set(void)
{
    if (tri_cd_zone(tri_zone()) && tri_act() < 3) s_proj[round_of(tri_zone())][tri_act()] = 1;
    extern void tri_cd_save_flush(void);tri_cd_save_flush();
}

void scd_time_stop(void);               /* (the goal post: the warp off) */

void scd_good_future_set(void)          /* (the robot generator, destroyed) */
{
    if (tri_cd_zone(tri_zone()) && tri_act() < 3) s_good[round_of(tri_zone())][tri_act()] = 1;
    extern void tri_cd_save_flush(void);tri_cd_save_flush();
}

static void stars(int on)
{
    uint32_t p = RAM_Player_1;
    if (on && !s_stars) {
        s_stars = 1;
        if (OB8(p, OBJ_status2) & 2) return;                  /* (already invincible) */
        s_stars = 2; /* Only invincibility introduced here belongs to the warp. */
        SET8(p, OBJ_status2, OB8(p, OBJ_status2) | 2);
        SET8(p, OBJ_invinc_time, 0);                          /* (no time: S3K never ends it itself) */
        if (!r32(RAM_Invincibility_stars)) {
            w32(RAM_Invincibility_stars, S3K_Obj_Invincibility);
            w16(RAM_Invincibility_stars + OBJ_parent, (uint16_t)p);
        }
    } else if (!on && s_stars) {
        int owned=s_stars==2;
        s_stars = 0;
        if (owned && !OB8(p, OBJ_invinc_time) && !r8(RAM_Super_Sonic_Knux_flag))
            SET8(p, OBJ_status2, OB8(p, OBJ_status2) & ~2);
    }
}

static void stop(void)
{
    s_timer = 0;
    s_dir = 0;
    stars(0);
}

void scd_time_stop(void) { stop(); }
void scd_time_hold(void) { s_timer = 0; stars(0); }   /* (MMZ: held by an object, the warp waits; its direction kept) */
void scd_time_cancel(void)              /* (Wacky Workbench's bars: a warp under way ends; nearly
                                         *  there, no more till the next post) */
{
    if (!s_timer) return;
    if (s_timer >= 90) s_dir = 0;
    s_timer = 0;
    stars(0);
}
int scd_time_warping(void) { return s_stars; }   /* timeWarp */

/* SaveTimeWarpData, then the level reloads as at a star post */
static void warp(void)
{
    uint32_t p = RAM_Player_1;
    int z = tri_zone(), tz = time_zone_of(cd_period()) + s_dir;
    if (tz < 0) tz = 0;
    if (tz > 2) tz = 2;
    int period = tz == 0 ? TRI_CD_PAST : tz == 1 ? TRI_CD_PRESENT
               : scd_good_future() ? TRI_CD_GOOD : TRI_CD_BAD;
    s_warp.za = (uint16_t)(TRI_CD_ZONE(round_of(z), period) << 8 | tri_act());
    s_warp.x = OB16(p, OBJ_x_pos);
    s_warp.y = OB16(p, OBJ_y_pos);
    s_warp.gvel = OBS16(p, OBJ_ground_vel);
    s_warp.xvel = OBS16(p, OBJ_x_vel);
    s_warp.yvel = OBS16(p, OBJ_y_vel);
    s_warp.status = OB8(p, OBJ_status) & ~8;                  /* (not on an object) */
    s_warp.cx = r16(RAM_Camera_X_pos);
    s_warp.cy = r16(RAM_Camera_Y_pos);
    s_warp.maxy = r16(RAM_Camera_target_max_Y_pos);
    s_warp.rings = r16(RAM_Ring_count);
    s_warp.lives = r8(RAM_Extra_life_flags);
    s_warp.time = r32(RAM_Timer);
    if (s_warp.time >= 0x00050000u) s_warp.time = 0x00050000u;  /* (5:00 at most) */
    s_held = 1;
    w8(RAM_Scroll_lock, 1);
}

/* The star post's saved state before the warp: the arrival goes in its place
 * (the level loads from it), then it comes back, so a death after the warp
 * restarts at the last checkpoint (or the act's start) in the new period, as
 * Sonic CD's spawnMode does, not where the warp left off (which may not be
 * in the new period's level at all: Palmtree Panic 1's present goes deeper) */
#define RAM_Saved_block 0xFFFE2Bu                             /* (Saved_last_star_post_hit .. +$1C) */
#define SAVED_LEN       0x1C
static uint8_t s_prev_save[SAVED_LEN], s_prev_hit;

static void reload_data(void)
{
    uint16_t za = s_warp.za;
    for (unsigned k = 0; k < SAVED_LEN; k++) s_prev_save[k] = r8(RAM_Saved_block + k);
    s_prev_hit = r8(RAM_Last_star_post_hit);
    w8(0xFFFE2Bu, 1);                                         /* Saved_last_star_post_hit */
    w8(RAM_Last_star_post_hit, 1);
    w16(0xFFFE2Cu, za);                                       /* Saved_zone_and_act */
    w16(0xFFFF9Au, tri_apparent(za));                         /* Saved_apparent_zone_and_act */
    w16(0xFFFE2Eu, s_warp.x);
    w16(0xFFFE30u, s_warp.y);
    w16(0xFFFE32u, s_warp.rings);
    w32(0xFFFE34u, s_warp.time);
    w16(0xFFFE38u, OB16(RAM_Player_1, OBJ_art_tile));
    w16(0xFFFE3Au, r16(RAM_Player_1 + OBJ_top_solid));
    w16(0xFFFE3Cu, s_warp.cx);
    w16(0xFFFE3Eu, s_warp.cy);
    w16(0xFFFE44u, s_warp.maxy);                              /* Saved_camera_max_Y_pos */
    w8(0xFFFE46u, 0);
    w8(0xFFFE43u, s_warp.lives);
    w8(RAM_Respawn_table_keep, 0);
    extern void tri_level_next(uint16_t za);
    tri_level_next(za);                                       /* (the new period as the level loads) */
    s_warp.on = 1;
    stop();
}

/* the cutscene over: the act again in its new period, from white, no card */
static void arrive(void)
{
    w8(RAM_Act3_flag, 1);
    s_arrive = 1;
    if (r8(RAM_Game_mode) == 0x10) w8(RAM_Game_mode, 0x0C);   /* (from the scene) */
    else w16(RAM_Restart_level_flag, 1);                       /* (no cutscene data: straight on) */
}

static void reload(void)                                      /* (levelRestart: to the time warp) */
{
    extern void tri_scdwarp_start(void (*done)(void));
    reload_data();
    scd_cd_fade();                                            /* FadeOutMusic */
    scd_pcm_cmd(SCD_PCM_FADEOUT);
    D(0) = 0xE2;                                              /* (S3K's own music too: stopped now; a fade
                                                               *  (E1) ends in zStopAllSound, mid-cutscene,
                                                               *  which cut its warp sound off) */
    tri_jsr(S3K_Play_Music);
    tri_scdwarp_start(arrive);
}

/* Arriving from a warp: the level's start fades from (and holds) white, and
 * the title card's work (spikes and springs, animals, the objects' art) is
 * done on its first frame */
int scd_time_pal_hook(uint32_t pc)
{
    if (!s_arrive) return 0;
    switch (pc) {
    case TH_PAL_TOBLACK: return 1;                            /* (Level's fade out: it is white already) */
    case TH_PAL_FILLBLACK: tri_jsr(S3K_Pal_FillWhite); return 1;
    case TH_PAL_FROMBLACK: tri_jsr(S3K_Pal_FromWhite); return 1;
    case TH_LVL_PRE_SPRITES:
        if (s_arrive == 1) {
            extern void tri_obj_titlecard_done(void);
            uint32_t a0 = A(0), a1 = A(1);
            A(1) = S3K_PLC_SpikesSprings;
            tri_jsr(S3K_Load_PLC_Raw);
            tri_jsr(S3K_PLCLoad_AnimalsAndExplosion);
            A(0) = a0;
            A(1) = a1;
            tri_obj_titlecard_done();
            SET8(RAM_Player_1, OBJ_invuln, 0x78);              /* (LoadPlayer: spawned mid-act) */
            s_arrive = 2;
        } else if (!r16(RAM_Palette_fade_timer))
            s_arrive = 0;
        return 0;
    default:
        return 0;
    }
}

/* RunTimeWarp + ObjSonic_TimeWarp, after the player has moved */
static void frame(void)
{
    uint32_t p = RAM_Player_1;
    if (!o_player_active(p) || OB8(p, OBJ_routine) != 2) return;
    if (s_warp.on && r16(RAM_Current_zone_and_act) == s_warp.za) {   /* (arrived: running on) */
        s_warp.on = 0;
        for (unsigned k = 0; k < SAVED_LEN; k++) w8(RAM_Saved_block + k, s_prev_save[k]);   /* (the checkpoint again, */
        w16(0xFFFE2Cu, s_warp.za);                                                       /*  in this period) */
        w16(0xFFFF9Au, tri_apparent(s_warp.za));
        w8(RAM_Last_star_post_hit, s_prev_hit);
        w16(RAM_Ring_count, s_warp.rings);
        w8(RAM_Update_HUD_rings, 1);
        w8(RAM_Extra_life_flags, s_warp.lives);
        w32(RAM_Timer, s_warp.time);
        SET16(p, OBJ_ground_vel, s_warp.gvel);
        SET16(p, OBJ_x_vel, s_warp.xvel);
        SET16(p, OBJ_y_vel, s_warp.yvel);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~3) | (s_warp.status & 3));
        extern void tri_cd_save_flush(void);tri_cd_save_flush();
    }
#if GEN_ENABLE_TRACE
    {
        static int test = -2;                                 /* development: TRILOGY_WARPTEST=1 / -1, a warp a second in */
        if (test == -2) { const char *g = getenv("TRILOGY_WARPTEST"); test = g && *g ? atoi(g) : 0; }
        if (test && r16(RAM_Level_frame_counter) == 60) { s_dir = (int8_t)test; s_timer = 209; test = 0; }
    }
#endif
    int charging = (r8(p + 0x3D) & 0x7F) != 0;                /* (spin_dash_flag: charging) */
    if (s_timer && !charging) s_timer++;
    if (round_of(tri_zone()) == 0 && tri_act() == 0 && cd_period() != TRI_CD_PRESENT &&
        OB16(p, OBJ_x_pos) < 0x900) {
        stop();                                               /* (PPZ1: the first 3D ramp's stretch) */
        return;
    }
    if (charging || !s_dir) return;
    int16_t g = OBS16(p, OBJ_ground_vel);
    uint16_t speed = (uint16_t)(g < 0 ? -g : g);
    if (!s_timer) s_timer = 1;
    static int log = -1;
    if (log < 0) log = getenv("TRILOGY_TIMELOG") != NULL;     /* development */
    if (log && !(s_timer % 30)) fprintf(stderr, "[time] dir=%d timer=%u speed=%04X\n", s_dir, s_timer, speed);
    if (s_timer >= 230) { reload(); return; }
    if (s_timer >= 210) { if (!s_held) warp(); return; }
    if (s_timer < 90) {
        if (speed >= 0x600) stars(1);
        else { s_timer = 0; stars(0); }
    } else if (speed < 0x600)
        stop();
}

void scd_time_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {                               /* (a level starts: no warp under way) */
        if (s_arrive && !tri_cd_zone(r8(RAM_Current_zone_and_act))) s_arrive = 0;
        s_dir = 0;
        s_timer = 0;
        s_held = 0;
        s_stars = 0;
        s_icon = 0;
        int z = r8(RAM_Current_zone_and_act);
        if (!tri_cd_zone(z)) { s_warp.on = 0; return; }
        static int round = -1;                                /* (a round begun afresh: its futures bad) */
        if (round_of(z) != round) {
            round = round_of(z);
            /* Durable per-slot flags are loaded at Data Select, not cleared
             * when visiting another round or returning from a scene. */
        }
        return;
    }
    if (pc == TH_LVL_POST_SPRITES && tri_cd_zone(tri_zone())) frame();
}

/* ---- the time post --------------------------------------------------------- */
#define TP_SPIN 0x30                   /* oTimePostSpin */
void cd_time_post(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    uint32_t mark = OB16(o, OBJ_respawn) ? 0xFF0000u | OB16(o, OBJ_respawn) : 0;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TIMEPOST, OB8(o, OBJ_render) | 4, 3, 0x0F, 0x20);
        SET8(o, OBJ_x_radius, 0x0E);
        SET8(o, OBJ_y_radius, 0x20);
        SET8(o, OBJ_anim, sub);
        SET8(o, OBJ_frame, sub == 8 ? 0x0A : 0x0C);
        if (mark && (r8(mark) & 1)) {                         /* (touched before) */
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            SET8(o, OBJ_routine, 6);
            break;
        }
        /* fall through */
    case 2:
        if (!(o_s1_touch(o, 4, 0x18) & 1)) break;             /* (CD's touch size $1F) */
        SET8(o, TP_SPIN, 60);
        SET8(o, OBJ_routine, 4);
        if (mark) w8(mark, r8(mark) | 1);
        s_dir = sub == 8 ? -1 : 1;
        {   extern void tri_scd_voice(int id);
            tri_scd_voice(sub == 8 ? 0xB2 : 0xB1); }          /* ("Past!" / "Future!") */
        if (!s_icon && (s_icon = o_alloc()) != 0)
            SET32(s_icon, OBJ_code, NATIVE_CD_BASE + SLOT_TIMEICON * 4u);
        break;
    case 4: {
        uint8_t t = (uint8_t)(OB8(o, TP_SPIN) - 1);
        SET8(o, TP_SPIN, t);
        if (t) { o_animate(o, o_asset_anim(SCDA_TIMEPOST)); break; }
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_frame, sub == 8 ? 0x0B : 0x0D);
        break;
    }
    default:
        break;
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ObjTimeIcon: P or F in the HUD while a post's warp is on, flashing at the end */
void cd_time_icon(uint32_t o)
{
    if (!s_dir || o != s_icon) { if (o == s_icon) s_icon = 0; o_delete(o); return; }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_asset(o, SCDA_TIMEPOST);
        SET8(o, OBJ_render, 0);                               /* (on the screen) */
        SET16(o, OBJ_priority, 0);
        SET16(o, OBJ_x_pos, 68 + 128);
        SET16(o, OBJ_y_pos, 210 + 128);
    }
    SET8(o, OBJ_frame, s_dir < 0 ? 0x12 : 0x13);
    if (s_timer >= 90 && (r8(RAM_Level_frame_counter + 1) & 1)) return;
    o_draw(o);
}
