/*
 * scd_wwz_gim.c — Wacky Workbench's gimmicks (Sonic CD; dobby: Level/Wacky
 * Workbench/Objects/Launcher, Freezer, Goddess Statue, Electric Beams, Snake
 * Blocks, Eggman Statue; decomp: R6\SHOOT6.C, BEEM6.C, BLOCK6.C, EGG6.C).
 *
 *   $04 launcher        a sled: player 1 landing on it is held (crouched) and
 *                       it runs 912 px right at $C00, carrying him; a jump
 *                       button lets him go, curled, with its speed; at the end
 *                       it throws him on at $C00 and creeps back 4 px a frame
 *   $05 freezer         a nozzle; 120 frames after its last jet, a jet that
 *                       blows gas for a while: player 1 in the gas is frozen
 *                       into an ice block that drops to the floor; from there
 *                       he has 15 frames to break out with a jump button, else
 *                       it hurts him; the block shatters either way
 *   $08 goddess statue  (act 1's past; unseen: the statue is the level's art)
 *                       player 1 at its feet: 50 rings, one every 11 frames
 *   $21 electric beams  orbs; the first one spawns their controller (subtype
 *                       1), which follows player 1 and works the beams by the
 *                       palette: a wait (past 360 frames, present 480, future
 *                       240; never in a good future, nor in act 1 with the
 *                       camera below Y $400), 90 frames of a blinking charge,
 *                       then 120 of flashing, while the beams of that mode are
 *                       live (scd_wwz_beams_live: PLAYER6's sibi2); the modes
 *                       take turns
 *   $2A snake blocks    a block that grows a chain of blocks along one of four
 *                       paths of its subtype's (0-4) in turn, 32 px a block at
 *                       1/2 px a frame, and draws it back in
 *   $2B Eggman statue   (futures; gone in a good one) a rolling player blows
 *                       it up; then ten spike bombs drop and burst at its foot
 * Their code is the same in every period of acts 1 and 2. CD's object
 * variables oVar2A.. sit at S3K's $30.. (free in S3K's object); routines keep
 * CD's even numbers. SolidObject -> o_solid, TopSolidObject -> o_platform,
 * GetOffObject -> cd_get_off; objPlayerSlot -> Player 1.
 */
#include "trilogy_obj.h"
#include "scd.h"

int scd_good_future(void);                 /* scd_time.c */
int scd_time_warping(void);

#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_XRAD          0x1F
#define P_ANIM          0x20
#define P_STATUS2       0x2B               /* status_secondary: bit 1 invincible */
#define P_OBJCTRL       0x2E               /* object_control: CD's oPlayerCtrl */
#define P_INVULN        0x34               /* invulnerability_timer */
#define P_CHAR          0x38               /* character_id (1: Tails) */
#define ST_AIR          0x02
#define ST_ROLL         0x04
#define ST_ONOBJ        0x08
#define ST_PUSHING      0x20
#define BTN_ABC         0x70
#define FM_JUMP         0x92               /* (ObjLauncher, ObjFreezer_IceLanded: let go with a jump button) */
#define FM_EXPLODE      0x9E               /* (ObjEggmanStatue_Explode, ObjSpikeBomb_Explode) */
#define FM_BEAMS        0xB2               /* (ObjElectricBeams: three beeps as a charge starts, on screen) */
#define S3K_Obj_Bouncing_Ring 0x01A64Au
#define RAM_Ring_spill_anim_counter 0xFFFEB6u
#define PAL(off)        (RAM_Normal_palette + (off))   /* (CD's palette: lines 2 and 3 are S3K's own) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

static int time_zone(void)                      /* timeZone: 0 past, 1 present, 2 either future */
{
    int p = cd_period();
    return p == TRI_CD_PAST ? 0 : p == TRI_CD_PRESENT ? 1 : 2;
}

static uint8_t tapped(void) { return r8(RAM_Ctrl_1_pressed_logical); }   /* p1CtrlTap */

/* (both let the player go so:) curled, the jump sound; the caller sets the speeds */
static void jump_ball(uint32_t p)
{
    SET8(p, P_YRAD, 14);
    SET8(p, P_XRAD, 7);
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + (OB8(p, P_CHAR) == 1 ? 1 : 5));   /* (Tails stands $F high) */
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_ROLL) & ~ST_PUSHING);
    SET8(p, P_ANIM, 2);
    o_sfx_cd(FM_JUMP);
}

static void get_off_1p(uint32_t o, int n)       /* GetOffObject, one player */
{
    uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
    if (!(OB8(o, OBJ_status) & bit)) return;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
    uint32_t p = o_player(n);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~ST_ONOBJ);
}

/* ---- $04 launcher (ObjLauncher; catapalt) ------------------------------- */
/* Player 1 held on it takes CD's animation $3A (PLCHG6 only: Sonic braced in a
 * crouch, frames $89/$8A); S3K's nearest is the duck (8), held on its crouch,
 * which all three characters have. Its one sound is the jump's (FM $92, a
 * jump button while it runs): the disc's (R61A-R62D) has none as it takes
 * him, runs or throws him, nor does his own code while he is held. */
#define LN_PARENT       0x30               /* oVar2A: (the side) its launcher */
#define LN_X0           0x32               /* oVar2E: where it waits */
#define ANI_HELD        8
static void launcher(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_init(o, SCDA_LAUNCHER, OB8(o, OBJ_render) | 4, 0, 28, 28);
        SET16(o, LN_X0, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_x_radius, 28);
        SET8(o, OBJ_y_radius, 4);
        SET8(o, OBJ_routine, 2);
        uint32_t a1 = o_alloc_after(o);                       /* (FindNextObjSlot) its back: solid, unseen */
        if (a1) {
            SET32(a1, OBJ_code, CD_CODE(0x04));
            o_init(a1, SCDA_LAUNCHER, 4, 0, 0, 12);           /* (CD gives it no width) */
            SET8(a1, OBJ_x_radius, 4);
            SET8(a1, OBJ_y_radius, 12);
            SET8(a1, OBJ_frame, 1);
            SET16(a1, LN_PARENT, (uint16_t)o);
            SET8(a1, OBJ_routine, 8);
        }
    }   /* fall through */
    case 2:                                                   /* waiting */
        o_platform(o, 28 + 10, 4, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) {               /* (player 1 on it: held, and off) */
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);     /* (facing right) */
            SET8(p, P_ANIM, ANI_HELD);
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_x_vel, 0xC00);
        }
        break;
    case 4: {                                                 /* running */
        uint16_t x0 = OB16(o, OBJ_x_pos);
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, 0);
        o_platform(o, 28 + 10, 4, x0);                        /* (who stands on it goes with it) */
        if (tapped() & BTN_ABC) {                             /* (CD: whether he is on it or not) */
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
            SET16(p, OBJ_y_vel, -0x680);
            SET16(p, OBJ_x_vel, OB16(o, OBJ_x_vel));
            jump_ball(p);                                     /* (CD leaves him on the ground: a roll) */
        }
        uint16_t end = (uint16_t)(OB16(o, LN_X0) + 912);
        if (end >= OB16(o, OBJ_x_pos)) break;
        SET16(o, OBJ_x_pos, end);
        SET8(o, OBJ_routine, 6);
        if (OB8(o, OBJ_status) & ST_P1_STAND) {               /* (thrown on at its speed) */
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
            SET16(p, OBJ_x_vel, OB16(o, OBJ_x_vel));
            SET8(p, P_ANIM, 0);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~ST_ONOBJ);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_STAND);
        }
        get_off_1p(o, 1);                                     /* (S3K: player 2 riding it, off too) */
        break;
    }
    case 6:                                                   /* back */
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 4);
        if (OB16(o, LN_X0) < OB16(o, OBJ_x_pos)) break;
        SET16(o, OBJ_x_pos, OB16(o, LN_X0));
        SET8(o, OBJ_routine, 2);
        break;
    default: {                                                /* 8: its back, while it waits */
        uint32_t par = 0xFF0000u | OB16(o, LN_PARENT);
        if (r32(par) != CD_CODE(0x04) || OB8(par, OBJ_routine) == 8) { o_delete(o); return; }   /* (gone) */
        if (OB8(par, OBJ_routine) >= 4) break;
        SET16(o, OBJ_x_pos, OB16(par, OBJ_x_pos) - 24);
        SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos) - 16);
        o_solid(o, 0 + 10, 12, 13, OB16(o, OBJ_x_pos));
        break;
    }
    }
    uint8_t held = OB8(o, OBJ_routine) == 4 && (OB8(o, OBJ_status) & ST_P1_STAND);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        if (held) SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);   /* (S3K: never leave him held) */
        return;
    }
    o_draw(o);
}

/* ---- $05 freezer (ObjFreezer; gas) --------------------------------------- */
#define FZ_TIME         0x30               /* oVar2A: (the nozzle) to its next jet */
#define FZ_PARENT       0x32               /* oVar2A: (the jet) its nozzle */
#define FZ_BREAK        0x34               /* oVar30: (the block) to break out; (pieces) to their end */
#define FZ_PIECE        0x35               /* oVar31: (pieces) what they break into */
/* dx, dy, time, frame, flips, x speed, y speed (px a frame), what it breaks into */
static const int8_t ice_block_pieces[6][8] = {               /* ObjFreezer_IceBlockPieces */
    { -0x10, -0x0C, 0x0A, 9, 0, -1, -1, 0 }, { -0x10, 0x0C, 0x0A, 9, 2, -1, 1, 0 },
    { 0x10, -0x0C, 0x0A, 9, 1, 1, -1, 0 },   { 0x10, 0x0C, 0x0A, 9, 3, 1, 1, 0 },
    { 0, -0x10, 0x0F, 0x0A, 1, 0, -1, 1 },   { 0, 0x10, 0x0F, 0x0A, 3, 0, 1, 1 },
};
static const int8_t ice_pieces[3][4][8] = {                  /* ObjFreezer_IcePiece .Pieces */
    { { 0, 0, 0x0A, 0x0B, 0, 0, -1, 0 }, { 0, 0, 0x0A, 0x0B, 0, 1, 0, 0 },
      { 0, 0, 0x0A, 0x0B, 0, 0, 1, 0 },  { 0, 0, 0x0A, 0x0B, 0, -1, 0, 0 } },
    { { 0, 0, 0x0A, 0x0B, 0, -1, -1, 0 }, { 0, 0, 0x0A, 0x0B, 0, 1, -1, 0 },
      { 0, 0, 0x0A, 0x0B, 0, 0, 1, 0 },   { 0, 0, 1, 0x0B, 0, -1, 0, 0 } },
    { { 0, 0, 0x0A, 0x0B, 0, -1, -1, 0 }, { 0, 0, 0x0A, 0x0B, 0, 1, -1, 0 },
      { 0, 0, 0x0A, 0x0B, 0, 1, 1, 0 },   { 0, 0, 0x0A, 0x0B, 0, -1, 1, 0 } },
};

static void ice_break(uint32_t o, const int8_t (*t)[8], int n)   /* ObjFreezer_IcePieceBreak */
{
    for (int k = 0; k < n; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET32(a1, OBJ_code, CD_CODE(0x05));
        SET8(a1, OBJ_routine, 0x0C);
        o_init(a1, SCDA_ICEBLOCK, 4 | (t[k][4] & 3), 0, 8, 12);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + t[k][0]);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + t[k][1]);
        SET8(a1, FZ_BREAK, t[k][2]);
        SET8(a1, OBJ_frame, t[k][3]);
        SET16(a1, OBJ_x_vel, t[k][5]);
        SET16(a1, OBJ_y_vel, t[k][6]);
        SET8(a1, FZ_PIECE, t[k][7]);
    }
}

static int in_gas(uint32_t o, uint32_t p)       /* ObjFreezer_CheckSonic */
{
    /* (CD: also not while he gives up from boredom, an animation S3K's players lack) */
    if ((OB8(p, P_STATUS2) & 2) || scd_time_warping() || OB8(p, P_ROUTINE) >= 4 || OB8(p, P_OBJCTRL) ||
        r16(RAM_Debug_placement_mode))
        return 0;
    int d1 = (int8_t)OB8(p, P_XRAD) + 16;
    int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + d1);
    if (d0 < 0 || d0 >= d1 * 2) return 0;
    d1 = (int8_t)OB8(p, P_YRAD) + 32;
    d0 = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + d1);
    return d0 >= 0 && d0 < d1 * 2;
}

static void freeze(uint32_t p)                  /* ObjFreezer_FreezeSonic */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);                /* (CD's bits 0 and 6: held, unseen) */
    SET8(p, P_INVULN, 3);                                     /* (unseen: S3K's Sonic_Display skips him) */
    SET32(a1, OBJ_code, CD_CODE(0x05));
    o_init(a1, SCDA_ICEBLOCK, 4, 0, 0x18, 0x18);
    SET16(a1, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET8(a1, OBJ_x_radius, 0x18);
    SET8(a1, OBJ_y_radius, 0x18);
    SET8(a1, OBJ_frame, 8);
    SET8(a1, OBJ_routine, 8);
}

static void unfreeze(uint32_t p)
{
    SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
    SET8(p, P_INVULN, 0);
}

/* The block's frozen player is player 1 (CD keeps him in oFreezerParent). */
static void freezer(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, SCDA_FREEZER, OB8(o, OBJ_render) | 4, 0, 32, 8);
        SET8(o, FZ_TIME, 120);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                                 /* the nozzle */
        if (!OB8(o, FZ_TIME)) break;
        SET8(o, FZ_TIME, OB8(o, FZ_TIME) - 1);
        if (OB8(o, FZ_TIME)) break;
        uint32_t a1 = o_alloc();                              /* (none: no more jets) */
        if (!a1) break;
        SET16(a1, FZ_PARENT, (uint16_t)o);
        SET32(a1, OBJ_code, CD_CODE(0x05));
        o_init(a1, SCDA_FREEZER, 4, 3, 16, 40);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 36);
        SET8(a1, OBJ_routine, 4);
        break;
    }
    case 4:                                                   /* the jet (its script: gas is animation 1) */
        if (OB8(o, OBJ_anim) == 1 && in_gas(o, p)) freeze(p);
        o_animate(o, o_asset_anim(SCDA_FREEZER));             /* (its script ends $FC: routine 6) */
        break;
    case 6: {                                                 /* done: the nozzle counts again */
        uint32_t par = 0xFF0000u | OB16(o, FZ_PARENT);
        if (r32(par) == CD_CODE(0x05) && OB8(par, OBJ_routine) == 2) SET8(par, FZ_TIME, 120);
        o_delete(o);
        return;
    }
    case 8: {                                                 /* the block, dropping */
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) * 256);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            tri_scd_voice(SCD_PCM_BREAK);                     /* (SCMD_BREAKSFX: the glass break) */
            SET8(o, FZ_BREAK, 15);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(o, OBJ_routine, 0x0A);
        }
        SET32(p, OBJ_y_pos, r32(o + OBJ_y_pos));              /* (he goes down in it) */
        SET8(p, P_INVULN, 3);
        break;
    }
    case 0x0A:                                                /* the block, down: break out or be hurt */
        SET8(p, P_INVULN, 3);
        if (OB8(o, FZ_BREAK)) {
            SET8(o, FZ_BREAK, OB8(o, FZ_BREAK) - 1);
            if (!(tapped() & BTN_ABC)) break;
            unfreeze(p);
            SET16(p, OBJ_y_vel, -0x680);                      /* (his x speed is as it was) */
            jump_ball(p);
        } else {
            unfreeze(p);
            o_hurt_player(p, o);
        }
        SET8(o, OBJ_routine, 0x0C);                           /* (it goes as a piece: at its fall speed, */
        SET8(o, OBJ_frame, 0x0A);                             /*  in px a frame, as CD has it) */
        SET8(o, FZ_BREAK, 20);
        SET8(o, FZ_PIECE, 2);
        ice_break(o, ice_block_pieces, 6);
        break;
    default: {                                                /* $C: a piece */
        uint8_t t = (uint8_t)(OB8(o, FZ_BREAK) - 1);
        SET8(o, FZ_BREAK, t);
        if (t) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, OBJ_x_vel));
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + OB16(o, OBJ_y_vel));
            break;
        }
        if (OB8(o, OBJ_frame) != 0x0B) ice_break(o, ice_pieces[OB8(o, FZ_PIECE) % 3], 4);
        o_delete(o);
        return;
    }
    }
    uint8_t rt = OB8(o, OBJ_routine);
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) {
        if (rt == 8 || rt == 0x0A) unfreeze(p);              /* (S3K: never leave him frozen) */
        return;
    }
    o_draw(o);
}

/* ---- $08 goddess statue (ObjGoddessStatue; megami) ------------------------ */
#define GS_TIME         0x30               /* oVar2A */
#define GS_COUNT        0x31               /* oVar2B */
static void spit_ring(uint32_t o)               /* ObjGoddessStatue_SpawnRing: a lost ring (S3K's) */
{
    static const int16_t xvel[8] = { -0x100, -0x80, 0, 0x80, 0x100, 0x180, 0x200, 0x280 };
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Bouncing_Ring);
    SET8(a1, OBJ_routine, 2);
    SET8(a1, OBJ_y_radius, 8);
    SET8(a1, OBJ_x_radius, 8);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + 24);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 16);
    SET32(a1, OBJ_mappings, S3K_Map_Ring);
    SET16(a1, OBJ_art_tile, 0xA6BC);                          /* (make_art_tile(ArtTile_Ring,1,1); CD's $A7AE) */
    SET16(a1, OBJ_priority, 3 << 7);
    SET8(a1, OBJ_render, 4);
    SET8(a1, OBJ_coll_flags, 0x47);
    SET8(a1, OBJ_width_px, 8);
    w8(RAM_Ring_spill_anim_counter, 0xFF);                    /* (ringLossAnimTimer) */
    SET16(a1, OBJ_y_vel, -0x200);
    SET16(a1, OBJ_x_vel, xvel[o_random() & 7]);
}

static void goddess(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, GS_COUNT, 50);
        /* fall through */
    case 2:
        /* (addi/bcs, then cmpi/bcc: 0-15 px right of it and 0-31 px below,
         *  not the box either side the decomp's code would make) */
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos)) >= 16) break;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos)) >= 32) break;
        SET8(o, OBJ_routine, 4);
        break;
    case 4: {
        int8_t t = (int8_t)(OB8(o, GS_TIME) - 1);
        SET8(o, GS_TIME, (uint8_t)t);
        if (t >= 0) break;
        SET8(o, GS_TIME, 10);
        int8_t n = (int8_t)(OB8(o, GS_COUNT) - 1);
        SET8(o, GS_COUNT, (uint8_t)n);
        if (n >= 0) spit_ring(o);
        else SET8(o, OBJ_routine, 6);
        break;
    }
    default:                                                  /* done */
        break;
    }
    cd_despawn(o, OB16(o, OBJ_x_pos));
}

/* ---- $21 electric beams (ObjElectricBeams; beem6) ------------------------- */
/* The beams are the level's art in colours 2-4 of line 3, one colour a mode;
 * the controller blinks a mode's colour while it charges, then flashes it,
 * and the backdrop (line 2 colour 0 in the past, line 3 colour 13 else). */
#define BM_CHARGE       0x30               /* oVar2E: the charge blink */
#define BM_CTIME        0x32               /* oVar30: charging */
#define BM_TIME         0x34               /* oVar3A: to the next charge; flashing */
#define BM_MODE         0x36               /* oVar3C: 0 up and down, 1 across, 2 the diagonals */
#define BM_FLASH        0x38               /* oVar3E */
#define BM_BGFLASH      0x39               /* oVar3F */
static uint8_t s_beam_mode;                /* wwzBeamMode (st6clrchg): the live mode + 1, or 0 */
static uint32_t s_beam_owner;              /* (its controller) */

/* PLAYER6's sibi2: the blocks of this mode hurt player 1 (scd_wwz.c) */
int scd_wwz_beams_live(void)
{
    uint32_t h = s_beam_owner;
    if (!s_beam_mode || !h || r32(h) != CD_CODE(0x21) || !OB8(h, OBJ_subtype) || OB8(h, OBJ_routine) != 4)
        return 0;
    return s_beam_mode;
}

static void beams_restore(void)                 /* ObjElectricBeams_RestorePal */
{
    int tz = time_zone();
    uint16_t c = 0x626, bg = 0x646;
    if (tz == 1) { c = 0x222; bg = 0x680; }
    else if (tz == 2) {
        if (scd_good_future()) return;
        c = 0x402; bg = 0x246;
    }
    for (unsigned k = 0; k < 3; k++) w16(PAL(0x64 + k * 2u), c);
    w16(PAL(tz ? 0x7A : 0x40), bg);
}

static void beams_charge(uint32_t o, int mode)  /* ObjElectricBeams_ChargePal */
{
    w16(PAL(0x64 + mode * 2u), OB8(o, BM_CHARGE) ? 0 : 0x80);
    SET8(o, BM_CHARGE, OB8(o, BM_CHARGE) ^ 1);
}

static void beams_flash(uint32_t o, int mode)   /* ObjElectricBeams_FlashPal */
{
    static const int16_t beam[8] = { 0, 0xEE0, 0, 0x0EE, 0, 0xE0E, 0, -1 };   /* (every period's) */
    static const int16_t back[3][5] = {
        { 0x846, 0x84A, 0x846, 0x84A, -1 }, { 0xA60, 0xAA0, 0xA60, 0xAA0, -1 }, { 0x244, 0x248, 0x244, 0x248, -1 },
    };
    int tz = time_zone();
    unsigned i = OB8(o, BM_FLASH) % 7;
    w16(PAL(0x64 + mode * 2u), (uint16_t)beam[i]);
    SET8(o, BM_FLASH, beam[i + 1] == -1 ? 0 : i + 1);
    i = OB8(o, BM_BGFLASH) % 4;
    w16(PAL(tz ? 0x7A : 0x40), (uint16_t)back[tz][i]);
    SET8(o, BM_BGFLASH, back[tz][i + 1] == -1 ? 0 : i + 1);
}

static void beams_follow(uint32_t o)            /* ObjElectricBeams_StickToPlayer */
{
    SET16(o, OBJ_x_pos, OB16(RAM_Player_1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(RAM_Player_1, OBJ_y_pos));
}

static void beams_handler(void)                 /* ObjElectricBeams_SpawnHandler: one in the act */
{
    for (int k = 3; k < 3 + 90; k++) {                        /* (S3K's dynamic objects) */
        uint32_t a1 = o_slot(k);
        if (r32(a1) == CD_CODE(0x21) && OB8(a1, OBJ_subtype)) return;
    }
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, CD_CODE(0x21));
    SET8(a1, OBJ_subtype, 1);
    SET16(a1, OBJ_x_pos, OB16(RAM_Player_1, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(RAM_Player_1, OBJ_y_pos));
    s_beam_mode = 0;                                          /* (a new act's: GAME.C clears st6clrchg) */
    s_beam_owner = 0;
}

static void beams(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BEAMS, OB8(o, OBJ_render) | 4, 4, 16, 24);
        SET8(o, OBJ_y_radius, 16);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));              /* (the controller's: empty) */
        beams_handler();
        /* fall through */
    case 2: {                                                 /* waiting */
        if (!OB8(o, OBJ_subtype)) break;                      /* (an orb: only seen) */
        beams_follow(o);
        if (!OB16(o, BM_TIME)) {
            beams_restore();
            int tz = time_zone();
            if (tz == 2 && scd_good_future()) break;
            if (tri_act() == 0 && r16(RAM_Camera_Y_pos) >= 0x400) break;
            SET16(o, BM_TIME, tz == 0 ? 360 : tz == 1 ? 480 : 240);
            break;
        }
        SET16(o, BM_TIME, OB16(o, BM_TIME) - 1);
        if (OB16(o, BM_TIME)) break;
        SET8(o, OBJ_routine, 4);
        SET16(o, BM_TIME, 120);
        SET16(o, BM_CTIME, 90);
        SET8(o, BM_CHARGE, 0);
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_BEAMS);
    }   /* fall through */
    default: {                                                /* 4: charging, then flashing */
        beams_follow(o);
        int mode = OB8(o, BM_MODE) % 3;
        if (OB16(o, BM_CTIME)) {
            beams_charge(o, mode);
            SET16(o, BM_CTIME, OB16(o, BM_CTIME) - 1);
            if (OB16(o, BM_CTIME)) break;
            beams_restore();
        }
        s_beam_mode = (uint8_t)(mode + 1);                    /* (live) */
        s_beam_owner = o;
        beams_flash(o, mode);
        SET16(o, BM_TIME, OB16(o, BM_TIME) - 1);
        if (OB16(o, BM_TIME)) break;
        SET8(o, OBJ_routine, 2);
        s_beam_mode = 0;
        if (mode + 1 < 3) SET8(o, BM_MODE, mode + 1);
        else {
            SET8(o, BM_MODE, 0);
            SET16(o, BM_FLASH, 0);                            /* (and BM_BGFLASH) */
        }
        break;
    }
    }
    draw_despawn(o);
}

/* ---- $2A snake blocks (ObjSnakeBlocks; block6) ---------------------------- */
/* The first block (subtype 0-4: its paths) stays; each of its four paths in
 * turn is a chain of blocks, each grown from the one before (0 up, 1 right, 2
 * down, 3 left): out 32 px, a wait (none for subtype 2), the next; the last
 * comes back at once, the others once the one after them is back, and the
 * first block grows the next chain as the last one turns. */
#define SN_PARENT       0x30               /* oVar2A: the first block */
#define SN_ID           0x32               /* (the first block's layout entry: CD checks oSubtype2) */
#define SN_TIME         0x34               /* oVar30 */
#define SN_INDEX        0x36               /* oVar32: (the first block) its next path */
#define SN_PREV         0x36               /* oVar36: (the chain) the block it grew from */
#define SN_PATH         0x38               /* oVar2C: in sn_dirs, the next block's way */
#define SN_SPAWN        0x39               /* oVar34: (the first block) grow a chain */
#define SN_SOLIDV       0x3A               /* oVar34: (the chain) its y speed for CD's solidity */
#define SN_XVEL         0x3C               /* oVar38 */
#define SN_YVEL         0x40               /* oVar3C */
static const uint8_t sn_dirs[] = {                           /* ObjSnakeBlocks_Paths' chains */
    0, 0, 0, 0xFF,  1, 1, 1, 0xFF,  2, 2, 2, 0xFF,  3, 3, 3, 0xFF,     /* 0, 4, 8, 12 */
    0, 0, 1, 0xFF,  1, 0, 1, 0xFF,  2, 2, 1, 0xFF,                     /* 16, 20, 24 */
    0, 0, 0xFF,  2, 2, 0xFF,                                           /* 28, 31 */
    1, 1, 0, 0xFF,  2, 3, 3, 0xFF,  3, 0, 3, 0xFF,  0, 1, 0, 0xFF,     /* 34, 38, 42, 46 */
};
static const uint8_t sn_paths[5][4] = {
    { 0, 4, 8, 12 }, { 16, 20, 24, 12 }, { 28, 31, 28, 31 }, { 12, 8, 4, 0 }, { 34, 38, 42, 46 },
};

static int sn_grow(uint32_t o)                  /* ObjSnakeBlocks_Spawn: 1 = a block grew from o */
{
    unsigned at = OB8(o, SN_PATH);
    if (at >= sizeof sn_dirs || (sn_dirs[at] & 0x80)) return 0;
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, CD_CODE(0x2A));                       /* (CD copies o's first $2C bytes) */
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
    SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
    SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
    SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
    SET8(a1, OBJ_height_px, OB8(o, OBJ_height_px));
    SET8(a1, OBJ_x_radius, OB8(o, OBJ_x_radius));
    SET8(a1, OBJ_y_radius, OB8(o, OBJ_y_radius));
    SET8(a1, OBJ_frame, OB8(o, OBJ_frame));
    SET16(a1, SN_PARENT, OB16(o, SN_PARENT));
    SET16(a1, SN_ID, OB16(o, SN_ID));
    SET8(a1, OBJ_subtype, 0xFF);
    SET16(a1, SN_PREV, (uint16_t)o);
    SET8(a1, SN_PATH, at + 1);
    SET16(a1, OBJ_priority, OB16(o, OBJ_priority) + 0x80);
    return 1;
}

static int sn_tick(uint32_t o)                  /* (subq.w #1, bpl: 1 = time's up) */
{
    int16_t t = (int16_t)(OB16(o, SN_TIME) - 1);
    SET16(o, SN_TIME, (uint16_t)t);
    return t < 0;
}

static void snake_chain(uint32_t o)             /* ObjSnakeSub */
{
    uint32_t par = 0xFF0000u | OB16(o, SN_PARENT);
    if (r32(par) != CD_CODE(0x2A) || (OB8(par, OBJ_subtype) & 0x80) || OB16(par, SN_ID) != OB16(o, SN_ID)) {
        cd_get_off(o);                                        /* (its first block is gone) */
        o_delete(o);
        return;
    }
    int quick = OB8(par, OBJ_subtype) == 2;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        SET16(o, SN_TIME, 63);
        int32_t vx = 0, vy = 0;
        switch (sn_dirs[OB8(o, SN_PATH) - 1u]) {
        case 0: vy = -0x8000; break;
        case 1: vx = 0x8000; break;
        case 2: vy = 0x8000; break;
        case 3: vx = -0x8000; break;
        }
        SET32(o, SN_XVEL, (uint32_t)vx);
        SET32(o, SN_YVEL, (uint32_t)vy);
        if (quick) {                                          /* (CD's solidity takes y speed: S3K's not) */
            SET16(o, SN_SOLIDV, vy < 0 ? -1 : 1);
            SET16(o, OBJ_y_vel, OB16(o, SN_SOLIDV));
        }
    }   /* fall through */
    case 2:                                                   /* out */
        cd_move(o, (int32_t)r32(o + SN_XVEL), (int32_t)r32(o + SN_YVEL));
        if (sn_tick(o)) SET8(o, OBJ_routine, 4);
        break;
    case 4:
        SET8(o, OBJ_routine, 6);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, SN_TIME, quick ? 0 : 30);
        /* fall through */
    case 6:                                                   /* a wait, then the next block */
        if (!sn_tick(o)) break;
        SET8(o, OBJ_routine, 8);
        if (sn_grow(o)) break;
        SET8(o, OBJ_routine, 10);                             /* (the last: back, and a new chain) */
        SET8(par, SN_SPAWN, 0xFF);
        break;
    case 8:                                                   /* (till the next one is back) */
        break;
    case 10:
        SET8(o, OBJ_routine, 12);
        SET16(o, SN_TIME, 63);
        SET16(o, OBJ_y_vel, -OBS16(o, SN_SOLIDV));
        /* fall through */
    case 12:                                                  /* back */
        cd_move(o, -(int32_t)r32(o + SN_XVEL), -(int32_t)r32(o + SN_YVEL));
        if (sn_tick(o)) SET8(o, OBJ_routine, 14);
        break;
    case 14:
        SET8(o, OBJ_routine, 16);
        SET16(o, OBJ_y_vel, 0);
        SET16(o, SN_TIME, quick ? 0 : 30);
        /* fall through */
    case 16: {                                                /* a wait, then the one before comes back */
        if (!sn_tick(o)) break;
        uint32_t prev = 0xFF0000u | OB16(o, SN_PREV);
        if (r32(prev) == CD_CODE(0x2A) && (OB8(prev, OBJ_subtype) & 0x80) && OB16(prev, SN_ID) == OB16(o, SN_ID))
            SET8(prev, OBJ_routine, OB8(prev, OBJ_routine) + 2);
        SET8(o, OBJ_routine, 18);
        break;
    }
    default:                                                  /* 18: gone */
        o_solid(o, 16 + 10, 16, 17, OB16(o, OBJ_x_pos));
        cd_get_off(o);
        o_delete(o);
        return;
    }
    o_solid(o, 16 + 10, 16, 17, OB16(o, OBJ_x_pos));          /* (no carry: CD's goes by oXVel, 0) */
    o_draw(o);
}

static void snake(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { snake_chain(o); return; }
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SNAKEBLOCK, OB8(o, OBJ_render) | 4, 1, 16, 16);
        SET8(o, OBJ_x_radius, 16);
        SET8(o, OBJ_y_radius, 16);
        SET16(o, SN_PARENT, (uint16_t)o);
        SET16(o, SN_ID, OB16(o, OBJ_respawn));
        SET8(o, SN_SPAWN, 0xFF);
        SET16(o, SN_INDEX, 0);
    }
    if (OB8(o, SN_SPAWN)) {
        SET8(o, SN_SPAWN, 0);
        unsigned sub = OB8(o, OBJ_subtype), k = OB16(o, SN_INDEX);
        SET8(o, SN_PATH, sn_paths[sub < 5 ? sub : 0][(k >> 1) & 3]);   /* (only 0-4 are placed) */
        SET16(o, SN_INDEX, k + 2 >= 8 ? 0 : k + 2);
        if (!sn_grow(o)) { o_delete(o); return; }             /* (no slot: CD deletes it) */
    }
    o_solid(o, 16 + 10, 16, 17, OB16(o, OBJ_x_pos));
    draw_despawn(o);
}

/* ---- $2B Eggman statue, spike bombs (ObjEggmanStatue; eggz6) --------------- */
#define ES_BOOM         0x30               /* oVar2C: the next explosion */
#define ES_TIME         0x31               /* oVar3F */
#define SB_VY           0x30               /* oVar2A: (a bomb) its speed (long) */
#define SB_FLOOR        0x34               /* oVar2E: where it bursts */
static const int8_t es_booms[9][3] = {                       /* ObjEggmanStatue_ExplodeLocs: frame, dx, dy */
    { 1, 0, 0 }, { 5, -0x12, -0x0A }, { 0x0A, -0x0A, 0x0A }, { 0x0F, 0, -0x0A }, { 0x14, -0x0A, -0x0A },
    { 0x19, 0x0D, -0x0A }, { 0x1E, -0x0A, 0x14 }, { 0x23, 0x0D, -0x0A }, { 0x28, -0x0A, 0x0A },
};
static const int8_t es_bombs[10][2] = {                      /* ObjEggmanStatue_BombLocs */
    { -0x18, -0x40 }, { -8, 0x40 }, { 8, 0 }, { 0x18, -0x80 }, { 0x28, -0x80 },
    { 0x38, 0x40 }, { 0x48, 0x40 }, { 0x58, -0x80 }, { 0x68, 0x40 }, { 0x78, -0x40 },
};

static void explode_here(uint32_t o)            /* (CD's $18, not a badnik's: no points, no flower) */
{
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    uint8_t render = OB8(o, OBJ_render);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 6);                                  /* (no animal) */
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
    SET8(o, OBJ_render, render & 0x80);
    o_sfx_cd(FM_EXPLODE);
}

static void spike_bomb(uint32_t o)              /* ObjSpikeBomb */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SPIKEBOMB, OB8(o, OBJ_render) | 4, 3, 6, 8);
        SET8(o, OBJ_y_radius, 6);
        SET8(o, OBJ_x_radius, 6);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xB7));
        SET32(o, SB_VY, 0);
        /* fall through */
    case 2:                                                   /* falling */
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + SB_VY));
        SET32(o, SB_VY, r32(o + SB_VY) + 0x400);
        if (OBS16(o, OBJ_y_pos) >= OBS16(o, SB_FLOOR)) SET8(o, OBJ_routine, 4);
        o_animate(o, o_asset_anim(SCDA_SPIKEBOMB));
        break;
    default:                                                  /* 4: bursts */
        explode_here(o);
        return;
    }
    o_touch(o);
    o_draw(o);
}

static void egg_statue(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) { spike_bomb(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (scd_good_future()) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_EGGSTATUE, OB8(o, OBJ_render) | 4, 3, 20, 28);
        SET8(o, OBJ_x_radius, 20);
        SET8(o, OBJ_y_radius, 28);
        SET8(o, ES_BOOM, 0);
        /* fall through */
    case 2: {                                                 /* (CD's touch $F8: a rolling player, size $38) */
        int hit = o_s1_touch(o, 0x1A, 0x1E), broken = 0;
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), P_ANIM) == 2) broken = 1;
        o_solid(o, 20 + 10, 28, 29, OB16(o, OBJ_x_pos));
        if (!broken) break;
        SET8(o, OBJ_routine, 4);
        cd_get_off(o);
        break;
    }
    case 4: {                                                 /* blowing up */
        unsigned k = OB8(o, ES_BOOM);
        if (k >= 9) {
            SET8(o, OBJ_routine, 6);
            SET8(o, OBJ_frame, 1);
            SET8(o, ES_TIME, 60);
            break;
        }
        SET8(o, ES_TIME, OB8(o, ES_TIME) + 1);
        if (OB8(o, ES_TIME) != (uint8_t)es_booms[k][0]) break;
        SET8(o, ES_BOOM, k + 1);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);                             /* (no animal) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + es_booms[k][1]);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + es_booms[k][2]);
        o_sfx_cd(FM_EXPLODE);
        break;
    }
    case 6:                                                   /* a second */
        SET8(o, ES_TIME, OB8(o, ES_TIME) - 1);
        if (!OB8(o, ES_TIME)) SET8(o, OBJ_routine, 8);
        break;
    default: {                                                /* 8: the bombs drop, it goes */
        uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
        for (unsigned k = 0; k < 10; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET32(a1, OBJ_code, CD_CODE(0x2B));
            SET8(a1, OBJ_subtype, 0xFF);
            SET16(a1, OBJ_x_pos, x + es_bombs[k][0]);
            SET16(a1, OBJ_y_pos, y - 160 + es_bombs[k][1]);
            SET16(a1, SB_FLOOR, y + 38);
        }
        o_delete(o);
        return;
    }
    }
    if (OB8(o, OBJ_routine) <= 2 && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

void scd_wwz_gim_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x04] = launcher;
    t[base + 0x05] = freezer;
    t[base + 0x08] = goddess;
    t[base + 0x21] = beams;
    t[base + 0x2A] = snake;
    t[base + 0x2B] = egg_statue;
}
