/*
 * scd_wwz_badnik.c — Wacky Workbench's badniks (Sonic CD; dobby: Level/Wacky
 * Workbench/Objects/Bata-pyon, Poh-Bee, Semi, Minomusi; decomp: R6\BATTA.C,
 * HACHI6.C, SEMI.C, MINOMUSI.C).
 *
 *   $30 Bata-pyon  a grasshopper: hops along the floor, turns at walls and
 *                  after a run of hops
 *   $31 Poh-Bee    a bee: flies to and fro; seeing the player it stops and
 *                  fires a needle down at him (subtype $FF: the needle)
 *   $32 Semi       a cicada: waits for the player, rises away from him,
 *                  then flies on level, dropping bombs (subtype $80: a bomb)
 *   $33 Minomusi   a bagworm: hangs on its thread, drops on the player
 *                  below and puts out its spikes (subtype $80: the thread,
 *                  $81: the spikes' touch)
 * Their code is the same in both acts and every period. Subtype 0 is the
 * robot as built, any other its damaged self (slower, no attack): placed in
 * the futures, and in the present where the futures' robots are; a good
 * future blows those up (DestroyOnGoodFuture). The children are Sonic CD's
 * subtype / subtype2 ones; here they keep a subtype with bit 7 (no robot
 * placed in Wacky Workbench has it).
 */
#include "trilogy_obj.h"


#define O_PREV_ANIM_S3K 0x21           /* (prev_anim: what o_animate compares) */
#define FM_NEEDLE       0xA0           /* (ObjPohBee_Shoot: the needle, on screen) */
#define FM_SPIKES       0xB7           /* (ObjMinomusi: the spikes come out, on screen) */
#define FM_EXPLODE      0x9E           /* (ObjSemiBomb_Explode, on screen) */

static int s_base;                     /* this round's native slots (scd_obj.c) */
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

/* ---- shared ------------------------------------------------------------- */
/* CD's DrawObject checks oWidth across and oYRadius up and down: S3K's
 * width_px / height_px */
static void radii(uint32_t o, int xr, int yr)
{
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_width_px, xr);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_height_px, yr);
}

static void badnik_init(uint32_t o, int asset, int prio, int xr, int yr, uint8_t coll)
{
    o_init(o, asset, OB8(o, OBJ_render) | 4, prio, xr, yr);
    radii(o, xr, yr);
    SET8(o, OBJ_coll_flags, tri_scd_coll(coll));
}

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

/* DrawObject + CheckObjDespawn (its own x), touchable */
static void badnik_end(uint32_t o)
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

/* subq.w #1,timer + bpl: 1 while it has not run below 0 */
static int count_down(uint32_t o, int f)
{
    SET16(o, f, OB16(o, f) - 1);
    return OBS16(o, f) >= 0;
}

static int32_t lval(uint32_t o, int f) { return (int32_t)r32(o + (uint32_t)f); }

/* ObjGetLWallDist as Sonic CD has it: S&K's left wall check flips the
 * probe's x within its block first (eori #$F, a fix Sonic 1's and CD's lack);
 * undone here, so the distance is CD's */
static int16_t cd_wall_left(uint32_t o, int d3)
{
    int x = OB16(o, OBJ_x_pos);
    return o_wall_left(o, (((x + d3) & 0xFFFF) ^ 0xF) - x);
}

/* ---- $30 Bata-pyon ------------------------------------------------------ */
#define BP_XVEL  0x30                  /* oBataXVel (oVar2A) */
#define BP_YVEL  0x34                  /* oBataYVel (oVar2E) */
#define BP_LEFT  0x38                  /* oBataJumpsLeft (oVar32) */
#define BP_COUNT 0x39                  /* oBataJumpCnt (oVar33) */
#define BP_SENSX 0x3A                  /* oBataSensorX (oVar34) */
#define BP_RWALL 0x3C                  /* oBataWallChk (oVar36): 0 ObjGetLWallDist, 1 ObjGetRWallDist */
#define BP_LAND  0x40                  /* oBataLandTime (oVar3A) */
#define BP_DIST  0x44                  /* oBataColDist (oVar3E) */

static void bp_swap(uint32_t o)                               /* ObjBataPyon_SwapFrames */
{
    int f = OB8(o, OBJ_frame) ? 0 : 1;                        /* (0 crouched, 1 stretched) */
    SET8(o, OBJ_frame, f);
    SET8(o, OBJ_y_radius, f ? 28 : 19);
    SET8(o, OBJ_height_px, f ? 28 : 19);
    SET8(o, OBJ_coll_flags, tri_scd_coll(f ? 0x30 : 0x2F));
}

static void bp_flip(uint32_t o)                               /* ObjBataPyon_Flip */
{
    SET8(o, BP_LEFT, OB8(o, BP_COUNT));
    turn(o);
    SET32(o, BP_XVEL, (uint32_t)-lval(o, BP_XVEL));
    SET16(o, BP_SENSX, (uint16_t)-OBS16(o, BP_SENSX));
    SET8(o, BP_RWALL, OB8(o, BP_RWALL) ^ 1);
}

/* the wall ahead against the floor / ceiling found (BP_DIST): 1 a wall,
 * nearer than the floor (turn), 2 the floor / ceiling, 0 neither */
static int bp_hit(uint32_t o)
{
    int d3 = OBS16(o, BP_SENSX);
    int16_t d1 = OB8(o, BP_RWALL) ? o_wall_right(o, d3) : cd_wall_left(o, d3);
    int16_t dist = OBS16(o, BP_DIST);
    if (d1 < 0) return dist >= 0 || d1 <= dist ? 1 : 2;
    return dist < 0 ? 2 : 0;
}

static void bata_pyon(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, dmg ? SCDA_BATAPYON2 : SCDA_BATAPYON, 3, 16, 0, 0);   /* (y radius, touch: SwapFrames) */
        SET32(o, BP_YVEL, 0x70000);
        SET8(o, BP_RWALL, 0);
        SET16(o, BP_SENSX, -16);
        SET8(o, OBJ_frame, 1);
        bp_swap(o);
        SET32(o, BP_XVEL, dmg ? -0x8000 : -0xA000);
        SET8(o, BP_COUNT, dmg ? 3 : 7);
        SET8(o, BP_LEFT, (dmg ? 3 : 7) + 1);
        /* fall through */
    case 2:                                                   /* falling */
        cd_move(o, lval(o, BP_XVEL), lval(o, BP_YVEL));
        SET16(o, BP_DIST, o_floor_dist(o));
        switch (bp_hit(o)) {
        case 1:
            bp_flip(o);
            break;
        case 2:                                               /* (landed) */
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + OB16(o, BP_DIST));
            SET16(o, BP_LAND, dmg ? 20 : 1);
            break;
        default: {
            int32_t v = lval(o, BP_YVEL) + 0x2000;
            SET32(o, BP_YVEL, v >= 0x70000 ? 0x70000 : v);
            break;
        }
        }
        break;
    case 4:                                                   /* landed */
        if (dmg) {                                            /* (it stutters: up, down, up, down) */
            int t = OBS16(o, BP_LAND), d6 = t == 6 || t == 15 ? 7 : t == 11 || t == 18 ? -7 : 0;
            if (d6) {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d6);
                bp_swap(o);
            }
        }
        SET16(o, BP_LAND, OB16(o, BP_LAND) - 1);
        if (OB16(o, BP_LAND)) break;
        SET8(o, OBJ_routine, 6);                              /* (hop) */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 7);
        bp_swap(o);
        SET32(o, BP_YVEL, dmg ? -0x50000 : -0x60000);
        SET8(o, BP_LEFT, OB8(o, BP_LEFT) - 1);
        if ((int8_t)OB8(o, BP_LEFT) < 0) bp_flip(o);          /* (the run's last: turn) */
        break;
    default: {                                                /* jumping */
        cd_move(o, lval(o, BP_XVEL), lval(o, BP_YVEL));
        SET16(o, BP_DIST, o_ceiling_dist(o));
        int hit = bp_hit(o);
        if (hit == 1) { bp_flip(o); break; }
        if (hit == 2) {                                       /* (the ceiling: down from it) */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - OB16(o, BP_DIST));
            SET32(o, BP_YVEL, 0);
        } else {
            int32_t v = lval(o, BP_YVEL) + 0x2000;
            SET32(o, BP_YVEL, v);
            if (v < 0) break;
        }
        SET8(o, OBJ_routine, 2);                              /* (falling) */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 11);
        bp_swap(o);
        break;
    }
    }
    badnik_end(o);
}

/* ---- $31 Poh-Bee -------------------------------------------------------- */
#define PH_TIME  0x30                  /* oPohTime (oVar2A) */
#define PH_XVEL  0x32                  /* oPohXVel (oVar2C) */
#define PH_SHOTX 0x36                  /* oPohShootX (oVar30) */
#define PH_CHK   0x38                  /* oPohChkTime (oVar32) */
#define PM_TIME  0x30                  /* oPohMissTime (oVar2A) */
#define PM_XVEL  0x32                  /* oPohMissXVel (oVar2C) */
#define PM_YVEL  0x36                  /* oPohMissYVel (oVar30) */

static void ph_turn(uint32_t o)
{
    SET32(o, PH_XVEL, (uint32_t)-lval(o, PH_XVEL));
    SET16(o, PH_SHOTX, (uint16_t)-OBS16(o, PH_SHOTX));
    turn(o);
}

/* ObjPohBee_CheckPlayer: within 96 px up / down and 120 px across (it
 * turns to face him) */
static int ph_sees(uint32_t o, uint32_t p)
{
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 96) >= 192) return 0;
    int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if ((uint16_t)(dx + 120) >= 240) return 0;
    if ((dx >= 0) != ((OB8(o, OBJ_render) & 1) != 0)) ph_turn(o);
    return 1;
}

static void ph_shoot(uint32_t o)                              /* (ObjPohBee_Shoot: the needle) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_subtype, 0xFF);
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET32(a1, OBJ_mappings, o_asset_maps(SCDA_POHBEEMISSILE));
    SET16(a1, OBJ_priority, 1 << 7);
    radii(a1, 16, 16);
    SET8(a1, OBJ_coll_flags, tri_scd_coll(0xB3));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 23);
    SET32(a1, PM_YVEL, 0x20000);
    int right = OB8(o, OBJ_render) & 1;
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + (right ? 7 : -7));
    SET32(a1, PM_XVEL, right ? 0x20000 : -0x20000);
    if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_NEEDLE);
}

/* ObjPohBeeMissile: a moment still, a short way out, then down and away */
static void poh_missile(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        SET16(o, PM_TIME, 3);
        /* fall through */
    case 2:
        if (count_down(o, PM_TIME)) break;
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, 1);
        SET16(o, PM_TIME, 10);
        break;
    case 4:
        cd_move(o, lval(o, PM_XVEL), lval(o, PM_YVEL));
        if (!count_down(o, PM_TIME)) SET8(o, OBJ_routine, 6);
        break;
    default:
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        cd_move(o, lval(o, PM_XVEL), lval(o, PM_YVEL));
        o_animate(o, o_asset_anim(SCDA_POHBEEMISSILE));
        break;
    }
    badnik_end(o);
}

static void poh_bee(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { poh_missile(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_POHBEE2 : SCDA_POHBEE;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 1, 24, 12, 0x31);
        SET16(o, PH_SHOTX, -8);
        SET32(o, PH_XVEL, dmg ? -0x8000 : -0x10000);
        /* fall through */
    case 2:
        SET8(o, OBJ_routine, 4);
        SET16(o, PH_TIME, dmg ? 0x400 : 0x200);
        /* fall through */
    case 4:                                                   /* flying (damaged: never looks) */
        cd_move(o, lval(o, PH_XVEL), 0);
        if (!dmg) {
            if (OB16(o, PH_CHK))
                SET16(o, PH_CHK, OB16(o, PH_CHK) - 1);
            else if (ph_sees(o, RAM_Player_1)) {
                SET8(o, OBJ_routine, 0xC);
                break;
            }
        }
        if (!count_down(o, PH_TIME)) SET8(o, OBJ_routine, 6);
        break;
    case 6:                                                   /* the way's end: a pause, turn, a pause */
        SET8(o, OBJ_routine, 8);
        SET16(o, PH_TIME, 30);
        /* fall through */
    case 8:
        if (count_down(o, PH_TIME)) break;
        SET8(o, OBJ_routine, 0xA);
        SET16(o, PH_TIME, 30);
        ph_turn(o);
        break;
    case 0xA:
        if (count_down(o, PH_TIME)) break;
        SET8(o, OBJ_routine, 2);
        break;
    case 0xC:                                                 /* seen: a pause, its firing pose */
        SET8(o, OBJ_routine, 0xE);
        SET16(o, PH_TIME, 30);
        /* fall through */
    case 0xE:
        if (count_down(o, PH_TIME)) break;
        SET8(o, OBJ_routine, 0x10);
        SET16(o, PH_TIME, 30);
        SET8(o, OBJ_anim, 1);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x32));
        radii(o, 16, 16);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, PH_SHOTX));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
        break;
    case 0x10:                                                /* fire */
        if (count_down(o, PH_TIME)) break;
        SET8(o, OBJ_routine, 0x12);
        SET16(o, PH_TIME, 30);
        ph_shoot(o);
        break;
    default:                                                  /* done: fly on, not looking for 60 */
        if (count_down(o, PH_TIME)) break;
        SET8(o, OBJ_routine, 2);
        SET16(o, PH_CHK, 60);
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x31));
        radii(o, 24, 12);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - OB16(o, PH_SHOTX));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
        break;
    }
    o_animate(o, o_asset_anim(asset));
    badnik_end(o);
}

/* ---- $32 Semi ----------------------------------------------------------- */
#define SM_XVEL  0x30                  /* oSemiXVel (oVar2A) */
#define SM_YVEL  0x34                  /* oSemiYVel (oVar2E) */
#define SM_TIME  0x38                  /* oSemiTime (oVar32) */
#define SM_PX    0x3A                  /* oSemiPlayerX (oVar34) */
#define SB_YVEL  0x34                  /* oSemiBombYVel (oVar2E) */
#define SB_TIME  0x38                  /* oSemiBombTime (oVar32) */

/* ObjSemi_CheckPlayer: within 96 px up / down and 120 px across (where he
 * is across: SM_PX, once he is in height) */
static int sm_sees(uint32_t o, uint32_t p)
{
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 96) >= 192) return 0;
    int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
    SET16(o, SM_PX, dx);
    return (uint16_t)(dx + 120) < 240;
}

static int32_t sm_xvel(uint32_t o, int dmg)                    /* (away from the player) */
{
    int32_t v = dmg ? 0xC000 : 0x10000;
    return OBS16(o, SM_PX) >= 0 ? -v : v;
}

static void sm_drop(uint32_t o)                               /* (ObjSemi_Attack: a bomb) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
    SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 10);
    SET8(a1, OBJ_subtype, 0x80);                              /* (CD: subtype2 -1) */
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_priority, OB16(o, OBJ_priority) + 0x80);
}

/* ObjSemiBomb: falls to the floor, sits, blinks, explodes */
static void semi_bomb(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xB7));
        radii(o, 6, 6);
        o_asset(o, SCDA_SEMIBOMB);                            /* (its own tile, $84C8) */
        SET32(o, SB_YVEL, 0x8000);
        /* fall through */
    case 2: {
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + SB_YVEL));
        SET32(o, SB_YVEL, r32(o + SB_YVEL) + 0x4000);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) break;
        SET8(o, OBJ_routine, 4);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET16(o, SB_TIME, 120);
        break;
    }
    case 4:
        if (count_down(o, SB_TIME)) break;
        SET8(o, OBJ_routine, 6);
        SET16(o, SB_TIME, 120);
        break;
    case 6:
        if (!count_down(o, SB_TIME)) SET8(o, OBJ_routine, 8);
        o_animate(o, o_asset_anim(SCDA_SEMIBOMB));
        break;
    default:                                                  /* (CD: an explosion not a badnik's: no
                                                                  points, no flower; S3K's: no animal) */
        SET32(o, OBJ_code, S3K_Obj_Explosion);
        SET8(o, OBJ_routine, 6);
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_EXPLODE);
        break;
    }
    badnik_end(o);
}

static void semi(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { semi_bomb(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_SEMI2 : SCDA_SEMI;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 1, 19, 16, 0x36);
        SET16(o, SM_TIME, 0);                                 /* (CD: subtype2, a wait; 0 for every Semi placed) */
        /* fall through */
    case 2:
        if (!count_down(o, SM_TIME)) SET8(o, OBJ_routine, 4);
        break;
    case 4:                                                   /* waiting for the player */
        if (sm_sees(o, RAM_Player_1)) SET8(o, OBJ_routine, 6);
        break;
    case 6:                                                   /* away from him, rising (damaged: sinking) */
        SET8(o, OBJ_routine, 8);
        SET32(o, SM_XVEL, sm_xvel(o, dmg));
        SET32(o, SM_YVEL, dmg ? 0x6000 : -0x8000);
        SET16(o, SM_TIME, dmg ? 42 : 96);
        /* fall through */
    case 8:
        if (!count_down(o, SM_TIME)) SET8(o, OBJ_routine, 0xA);
        cd_move(o, lval(o, SM_XVEL), lval(o, SM_YVEL));
        o_animate(o, o_asset_anim(asset));
        break;
    case 0xA:                                                 /* then on, level */
        SET8(o, OBJ_routine, 0xC);
        SET16(o, SM_TIME, 0);
        SET32(o, SM_XVEL, sm_xvel(o, dmg));
        /* fall through */
    default:
        if (!dmg) {                                           /* (a bomb every 64, the player near) */
            SET16(o, SM_TIME, OB16(o, SM_TIME) & 0x3F);
            if (!OB16(o, SM_TIME) && sm_sees(o, RAM_Player_1)) sm_drop(o);
            SET16(o, SM_TIME, OB16(o, SM_TIME) + 1);
        }
        cd_move(o, lval(o, SM_XVEL), 0);
        o_animate(o, o_asset_anim(asset));
        break;
    }
    badnik_end(o);
}

/* ---- $33 Minomusi ------------------------------------------------------- */
#define MN_TIME   0x30                 /* oMinoTime (oVar2A) */
#define MN_YVEL   0x36                 /* oMinoYVel (oVar30) */
#define MN_Y0     0x3A                 /* oMinoStartY (oVar34) */
#define MN_DROPY  0x3C                 /* oMinoDropY (oVar36) */
#define MN_PARENT 0x3E                 /* oMinoParent (oVar38): its body (word) */
#define MN_SILK   0x80                 /* (subtype: CD's subtype2 -1, the thread) */
#define MN_SPIKES 0x81                 /* (CD's subtype2 1, the spikes' touch) */

/* ObjMinomusi_CheckPlayer: 40-159 px below, within 168 px across */
static int mn_sees(uint32_t o, uint32_t p)
{
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - 40) >= 120) return 0;
    return (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 168) < 336;
}

/* a child of the body's: the next slot free, its art (FindNextObjSlot) */
static uint32_t mn_child(uint32_t o, uint8_t kind)
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_subtype, kind);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
    SET16(a1, MN_PARENT, (uint16_t)o);
    return a1;
}

/* the body (CD: its id still the Minomusi's; a body, not another's child) */
static uint32_t mn_parent(uint32_t o)
{
    uint32_t a1 = 0xFF0000u | OB16(o, MN_PARENT);
    return r32(a1 + OBJ_code) == CD_CODE(0x33) && !(OB8(a1, OBJ_subtype) & 0x80) ? a1 : 0;
}

/* ObjMinomusiSilk: the thread, as long as the body hangs (frames 0-8) */
static void mn_silk(uint32_t o)
{
    uint32_t a1 = mn_parent(o);
    if (!a1) { o_delete(o); return; }
    int16_t d0 = (int16_t)(OB16(a1, OBJ_y_pos) - OB16(a1, MN_Y0) - 24) >> 3;
    if (d0 < 0) d0 = 0;
    SET8(o, OBJ_frame, d0);
    SET16(o, OBJ_y_pos, (d0 << 2) + OB16(a1, MN_Y0) + 16);
    o_draw(o);
}

/* ObjMinomusiSpikes: unseen (frame 0, empty), it hurts until the body's
 * spikes go in (its script back at the start) */
static void mn_spikes(uint32_t o)
{
    uint32_t a1 = mn_parent(o);
    if (!a1 || OB8(a1, OBJ_anim_frame) == 1) { o_delete(o); return; }
    o_touch(o);
    o_draw(o);
}

static void minomusi(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub == MN_SILK) { mn_silk(o); return; }
    if (sub == MN_SPIKES) { mn_spikes(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = sub != 0, asset = dmg ? SCDA_MINOMUSI2 : SCDA_MINOMUSI;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 3, 16, 16, 0x34);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
        SET16(o, MN_Y0, OB16(o, OBJ_y_pos));
        SET16(o, MN_DROPY, OB16(o, OBJ_y_pos) + 95);
        uint32_t a1 = mn_child(o, MN_SILK);
        if (!a1) { o_delete(o); return; }
        SET16(a1, OBJ_priority, OB16(o, OBJ_priority) + 0x80);
        radii(a1, 1, 32);
    }   /* fall through */
    case 2:
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, 9);
        SET16(o, MN_TIME, 121);
        /* fall through */
    case 4:                                                   /* hanging: looks every 121 */
        SET16(o, MN_TIME, OB16(o, MN_TIME) - 1);
        if (OB16(o, MN_TIME)) break;
        SET16(o, MN_TIME, 121);
        SET8(o, OBJ_routine, mn_sees(o, RAM_Player_1) ? 6 : 2);
        break;
    case 6:                                                   /* drops 95 px */
        SET8(o, OBJ_routine, 8);
        SET32(o, MN_YVEL, 0x80000);
        /* fall through */
    case 8: {
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + MN_YVEL));
        int16_t d0 = (int16_t)(OB16(o, MN_DROPY) - OB16(o, OBJ_y_pos));
        if (d0 > 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
        SET8(o, OBJ_routine, 0xE);
        break;
    }
    case 0xA:                                                 /* back up */
        SET8(o, OBJ_routine, 0xC);
        SET32(o, MN_YVEL, dmg ? 0x20000 : 0x70000);
        /* fall through */
    case 0xC: {
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) - r32(o + MN_YVEL));
        int16_t d0 = (int16_t)(OB16(o, MN_Y0) - OB16(o, OBJ_y_pos));
        if (d0 < 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
        SET8(o, OBJ_routine, 2);
        break;
    }
    case 0xE:                                                 /* at the bottom: spikes out and in */
        SET8(o, OBJ_routine, 0x10);
        SET16(o, MN_TIME, dmg ? 61 : 230);
        SET8(o, OBJ_anim, dmg);                               /* (move.w #$00FF/$01FF,oAnim: restart) */
        SET8(o, O_PREV_ANIM_S3K, 0xFF);
        /* fall through */
    default:
        SET16(o, MN_TIME, OB16(o, MN_TIME) - 1);
        if (!OB16(o, MN_TIME)) SET8(o, OBJ_routine, 0xA);
        o_animate(o, o_asset_anim(asset));
        if (dmg || OB8(o, OBJ_anim_frame) != 0x1E) break;    /* (30 steps into its script: out) */
        {
            uint32_t a1 = mn_child(o, MN_SPIKES);
            if (!a1) break;
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
            SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
            SET8(a1, OBJ_y_radius, OB8(o, OBJ_y_radius));
            SET8(a1, OBJ_height_px, OB8(o, OBJ_height_px));
            SET8(a1, OBJ_x_radius, OB8(o, OBJ_x_radius));
            SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
            SET8(a1, OBJ_coll_flags, tri_scd_coll(0xB5));
            if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SPIKES);
        }
        break;
    }
    badnik_end(o);
}

void scd_wwz_badnik_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x30] = bata_pyon;
    t[base + 0x31] = poh_bee;
    t[base + 0x32] = semi;
    t[base + 0x33] = minomusi;
}
