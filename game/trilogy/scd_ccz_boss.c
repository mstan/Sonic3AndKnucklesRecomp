/*
 * scd_ccz_boss.c — Collision Chaos act 3 (Sonic CD R33C / R33D): Eggman's
 * pinball table. Transcribed from the Mega-CD program (the routines' R33C
 * addresses in brackets; names from the PC port's BOSS_3.C, GATE.C,
 * SIKAKE33.C, ROLLPLAT.C, LIGHT.C, STOPPER.C; R33D's code is the same).
 *
 * A lid ($30, egg3dai_s) closes the way up into the table; hit from below it
 * rises and blows up. Up the table's middle column (x $340-$3C0) the big
 * platform ($31, egg3dai_l) carries Eggman ($33), a gear ($32) and a cannon
 * ($3D) that drops harmless punch bombs ($3E: they knock the player away).
 * The flippers launch the player into the platform's underside: each hit
 * raises it 72 px, and gates ($3F) close the column under the player as he
 * climbs; the third hit wrecks it (1000 points; the level's song again) and
 * Eggman flees. A gate behind ($3F at $420,$540) shuts when the player enters
 * the table (the boss's song starts). Below the table the drain ($36): a
 * spinner that lights a lamp each time the player falls through it; three
 * lamps close the ball saver under it, which throws him back up; a fourth
 * pass resets it. The capsule ($15) is Palmtree Panic's.
 *
 * State the disc keeps for others: bossFlags ($F7A7) 3 once the column's top
 * gate shuts after a hit; then the level's events (zone33chk) take the
 * camera's bottom up to $60 and make its left edge follow the player: they
 * run here, from whichever of the act's objects runs first in a frame (the
 * gates stay for good).
 *
 * CD's object fields $2A-$3F are S3K's $30-$45 (named below), CD's $10 / $12
 * S3K's x_vel / y_vel. Collision: the lid and the platform are CD enemies
 * with hits left ($3E 48 x 16, two; $3F 64 x 16, four); S3K has no touch
 * size of either, so their touch (the player's ObjCollide, pcolnomal) is
 * done here with CD's own box: hit while attacking, the player bounces back
 * at half speed and the type clears, which the object polls; else he is
 * hurt. The punch bombs are CD's special touch $17 (S3K's own: it counts).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "scd.h"
#include <stdio.h>
#include <stdlib.h>


/* the lid ($30) and the platform ($31) */
#define B_TIME   0x30                  /* oVar2A (byte) */
#define B_FLASH  0x31                  /* oVar2B: the flash's frames (bit 7: its phase) */
#define B_FLAGS  0x32                  /* oVar2C: bit 0 blown up, 1 the player is in (the lid), 2 moving */
#define B_TOP    0x34                  /* oVar2E: where it stops */
#define B_YSPD   0x36                  /* oVar30 (16.16) */
#define B_GEAR   0x3A                  /* oVar34: its gear */
#define B_EGG    0x3C                  /* oVar36: Eggman (the platform's) */
#define B_GATES  0x40                  /* oVar3A: gates shut */
#define B_HITS   0x41                  /* oVar3B */
#define B_BOMB   0x44                  /* oVar3E (word): the explosions' clock */
#define B_COLINO  OBJ_coll_flags       /* (CD's $20 and $21: its touch here, S3K's never added) */
#define B_COLICNT OBJ_coll_prop
/* the parts: Eggman, the gear, the cannon (B_TOP, B_YSPD as above) */
#define C_LINK   0x3A                  /* oVar34: the platform (or the lid) */
#define K_TIME   0x30                  /* oVar2A (word): the cannon's clock */
/* the punch bomb */
#define PB_TIME  0x30                  /* oVar2A (word) */
#define PB_AX    0x3E                  /* oVar38 */
#define PB_AY    0x40                  /* oVar3A */
#define PB_VMAX  0x42                  /* oVar3C */
/* the gate */
#define G_LAST   0x30                  /* oVar2A: its last frame */
/* the drain */
#define D_SPIN   0x30                  /* oVar2A (word): frames it keeps turning */
#define D_STOP   0x32                  /* oVar2C: the stopper */
#define D_LIT    0x34                  /* oVar2E (word): lamps lit (-2, 0, 2, 4) */
#define D_LAMP   0x36                  /* oVar30 / 32 / 34: the lamps, newest first */
#define D_IN     0x44                  /* oVar3E: the player is in it */
#define D_PARENT 0x3C                  /* oVar36: a lamp's / the stopper's spinner */

#define P_ANIM    0x20
#define P_STATUS2 0x2B                 /* status_secondary: bit 1 invincible */
#define P_DJUMP   0x2F                 /* double_jump_flag */
#define P_CHAR    0x38                 /* character_id */
#define P_JUMPING 0x40                 /* jumping: CD's oPlayerJump */
#define ST_AIR      0x02
#define ST_ONOBJ    0x08
#define ST_ROLLJUMP 0x10
#define ST_PUSH     0x20

#define RAM_Distance_from_top 0xFFEE2Cu   /* (CD's $F73E: where the player sits on screen) */
#define FM_BOSSHIT  0xAC               /* (egg3dai_s_chk, egg3dai_l_hit2: the lid, the platform hit) */
#define FM_EXPLODE  0x9E               /* (bom_set: each blast; punchbom_kemini: a punch bomb bursts) */

void tri_scd_level_music(void);
void cd_capsule(uint32_t o);           /* scd_ppz_boss.c: Palmtree Panic's $15, the same code */

static int s_base3;
static uint8_t s_flags;                /* bossFlags ($F7A7) */
static uint16_t s_bottom, s_dest;      /* bottomBound, destBottomBound (once bossFlags) */
static uint16_t s_frame = 0xFFFF;

static uint32_t native3(int id) { return NATIVE_CD_BASE + (uint32_t)(s_base3 + id) * 4u; }
static uint32_t lk(uint32_t o, unsigned f) { uint16_t w = o ? OB16(o, f) : 0; return w ? 0xFF0000u | w : 0; }
static void set_lk(uint32_t o, unsigned f, uint32_t a) { if (o) SET16(o, f, (uint16_t)a); }
static int16_t wx(uint32_t o) { return OBS16(o, OBJ_x_pos); }
static int16_t wy(uint32_t o) { return OBS16(o, OBJ_y_pos); }
static void set_xy(uint32_t o, int x, int y) { SET16(o, OBJ_x_pos, x); SET16(o, OBJ_y_pos, y); }
static void anim_word(uint32_t o, uint16_t w) { SET16(o, OBJ_anim, w); }   /* (CD's $1C word: anim, previous anim) */
static void set_flag(uint32_t o, uint8_t m, int on)
{
    if (o) SET8(o, B_FLAGS, on ? OB8(o, B_FLAGS) | m : OB8(o, B_FLAGS) & ~m);
}

static uint32_t spawn(int id)                                  /* FindObjSlot, id */
{
    uint32_t a1 = o_alloc();
    if (a1) SET32(a1, OBJ_code, native3(id));
    return a1;
}
static uint32_t spawn_after(uint32_t o, int id)                /* FindNextObjSlot, id */
{
    uint32_t a1 = o_alloc_after(o);
    if (a1) SET32(a1, OBJ_code, native3(id));
    return a1;
}

/* CD's sizes: $19 display width, $16 y radius (S3K's height: at least CD's 32) */
static void init(uint32_t o, int asset, int render, int prio, int width, int yrad)
{
    o_init(o, asset, render, prio, width, yrad > 0x20 ? yrad : 0x20);
    SET8(o, OBJ_y_radius, yrad);
}

static void line1(int asset)                                  /* LoadPalette into line 1 (and its fade target) */
{
    uint32_t pal = o_asset_anim(asset);
    if (!pal) return;
    for (unsigned k = 0; k < 16; k++) {
        uint16_t c = tri_rd16(pal + k * 2u);
        w16(RAM_Normal_palette + 0x20 + k * 2u, c);
        w16(RAM_Target_palette + 0x20 + k * 2u, c);
    }
}

static void explode(uint32_t o, int dx, int dy)               /* (CD: a punch bomb, routine $A: a harmless blast) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Explosion);
    SET8(a1, OBJ_routine, 6);
    set_xy(a1, wx(o) + dx, wy(o) + dy);
    o_sfx_cd(FM_EXPLODE);
}

/* ---- the level's events (zone33chk [$20373E], bossclr_scrset [$20392C],
 * the bottom's easing [$203622]); once a frame ----------------------------- */
static void events(void)
{
    if (!s_flags) return;                                      /* (till then $510, as converted) */
    uint32_t p = RAM_Player_1;
    s_dest = 0x60;
    int16_t d = (int16_t)(s_dest - s_bottom);
    if ((d < 0 ? -d : d) < 4) s_bottom = s_dest;
    int16_t x = (int16_t)(wx(p) - 0xA0);                       /* (the left edge follows: no way back) */
    if (x >= (int16_t)r16(RAM_Camera_min_X_pos)) {
        if (x > (int16_t)r16(RAM_Camera_max_X_pos)) x = (int16_t)r16(RAM_Camera_max_X_pos);
        w16(RAM_Camera_min_X_pos, (uint16_t)x);
        w16(RAM_Camera_target_min_X_pos, (uint16_t)x);
    }
    /* (CD also holds the bottom at the camera while Sonic gives up, anim $2B: none here) */
    if (s_dest != s_bottom) {                                  /* 4 px a frame (16 falling near it) */
        uint16_t cy = r16(RAM_Camera_Y_pos);
        int16_t d1 = 4;
        if (s_dest > s_bottom) {
            if ((uint16_t)(cy + 8) >= s_bottom && (OB8(p, OBJ_status) & ST_AIR)) d1 *= 4;
        } else {
            d1 = -4;
            if (cy > s_dest) s_bottom = cy & 0xFFFE;
        }
        s_bottom = (uint16_t)(s_bottom + d1);
    }
    w16(RAM_Camera_max_Y_pos, s_bottom);                       /* (both: S3K's own move stays out) */
    w16(RAM_Camera_target_max_Y_pos, s_bottom);
}

static void frame_tick(void)
{
    uint16_t f = r16(RAM_Level_frame_counter);
    if (f == s_frame) return;
    s_frame = f;
    events();
}

/* ---- the lid's and the platform's touch: the player's ObjCollide ($207166)
 * on CD's box, run before the object moves, as his; an enemy with hits left
 * (pcolnomal [$207278]) ------------------------------------------------------ */
static int attacking(uint32_t p, uint32_t o)                  /* (S3K's Touch_Enemy, for its characters) */
{
    if (OB8(p, P_STATUS2) & 2) return 1;                       /* (invincible) */
    uint8_t a = OB8(p, P_ANIM);
    if (a == 2 || a == 9) return 1;                            /* (rolling, spin dash) */
    uint8_t c = OB8(p, P_CHAR), g = OB8(p, P_DJUMP);
    if (c == 2) return g == 1 || g == 3;                       /* (Knuckles gliding, sliding) */
    if (c == 1 && g && !(OB8(p, OBJ_status) & 0x40)) {        /* (Tails flying, from below) */
        uint8_t ang = o_atan((int16_t)(wx(p) - wx(o)), (int16_t)(wy(p) - wy(o)));
        return (uint8_t)(ang - 0x20) < 0x40;
    }
    return 0;
}

static void boss_touch(uint32_t o, int w, int h)
{
    if (!OB8(o, B_COLINO)) return;
    int hit = o_s1_touch(o, w, h);
    for (int n = 0; n < 2 && OB8(o, B_COLINO); n++) {         /* (Sonic CD: player 1) */
        if (!(hit >> n & 1)) continue;
        uint32_t p = o_player(n);
        if (!attacking(p, o)) { o_hurt_player(p, o); continue; }
        if (!OB8(o, B_COLICNT)) continue;                      /* (none: its counts stay up while it has a type) */
        SET16(p, OBJ_x_vel, (uint16_t)(-OBS16(p, OBJ_x_vel) >> 1));   /* (back, at half speed) */
        SET16(p, OBJ_y_vel, (uint16_t)(-OBS16(p, OBJ_y_vel) >> 1));
        SET8(o, B_COLINO, 0);
        SET8(o, B_COLICNT, OB8(o, B_COLICNT) - 1);
        if (!OB8(o, B_COLICNT)) SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);
        if (OB8(p, P_CHAR) == 2 && OB8(p, P_DJUMP) == 1) {    /* (S3K: Knuckles' glide ends) */
            SET8(p, P_DJUMP, 2);
            SET8(p, P_ANIM, 0x21);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~1) | (OBS16(p, OBJ_x_vel) < 0 ? 0 : 1));
            SET8(p, OBJ_y_radius, OB8(p, 0x44));
            SET8(p, OBJ_x_radius, OB8(p, 0x45));
        }
    }
}

static void dev_autohit(uint32_t o)                            /* development: a hit every N frames */
{
    static int n = -1;
    if (n < 0) n = getenv("TRILOGY_BOSSAUTOHIT") ? atoi(getenv("TRILOGY_BOSSAUTOHIT")) : 0;
    if (n <= 0 || !OB8(o, B_COLINO) || r16(RAM_Level_frame_counter) % n) return;
    SET8(o, B_COLINO, 0);
    if (OB8(o, B_COLICNT)) SET8(o, B_COLICNT, OB8(o, B_COLICNT) - 1);
}

/* ---- the explosions (bom_set [$20E190]): every tbl[0] frames of the clock,
 * the next of tbl[1] places ---------------------------------------------------- */
static const int16_t BOM_DAI[] = { 4, 10, -48, -16, 48, 16, -16, -16, 16, 16, -32, 0, 48, -16, -48, 16, -16, 16, 16, -16, 32, 0 };
static const int16_t BOM_CANNON[] = { 1, 3, 0, 0, -32, 0, 32, 0 };
static const int16_t BOM_CTRL[] = { 1, 3, 0, -56, -32, -56, 32, -56 };
static const int16_t BOM_CTRL2[] = { 1, 2, -16, -56, 16, -56 };

static void bom_set(uint32_t o, const int16_t *t)
{
    unsigned d2 = OB16(o, B_BOMB);
    if (d2 % (unsigned)t[0]) return;
    unsigned k = (d2 / (unsigned)t[0]) % (unsigned)t[1];
    explode(o, t[2 + k * 2], t[3 + k * 2]);
}

static void bom_dai(uint32_t o)                                /* [$20E0CE]: 120 frames, then it is gone */
{
    uint16_t n = (uint16_t)(OB16(o, B_BOMB) + 1);
    SET16(o, B_BOMB, n);
    if (n == 0x78) { set_flag(o, 1, 1); set_flag(lk(o, B_GEAR), 1, 1); }
    else if (n < 0x78) bom_set(o, BOM_DAI);
}

static void bom_short(uint32_t o, const int16_t *t, unsigned n0)   /* [$20E122 / $20E148 / $20E16E] */
{
    uint16_t n = (uint16_t)(OB16(o, B_BOMB) + 1);
    SET16(o, B_BOMB, n);
    if (n < n0) bom_set(o, t);
}

/* ---- $30 the lid -------------------------------------------------------------- */
static void lid_enter(uint32_t o)                              /* [$20DC84] jaba_s_chk: the player is in */
{
    uint32_t p = RAM_Player_1;
    if ((OB8(o, B_FLAGS) & 2) || wx(p) < 0x480 || wy(p) > 0x51C) return;
    set_flag(o, 2, 1);
    scd_cd_play(SCD_CD_BOSS, 1);                               /* (SubCPUCmd $67: and bossMusic) */
    w8(RAM_Boss_flag, 1);
    uint32_t a1 = spawn(0x3F);                                 /* (the gate behind him: to frame 5) */
    if (!a1) return;
    SET8(a1, OBJ_routine, 6);
    set_xy(a1, 0x420, 0x540);
}

static void lid(uint32_t o)
{
    frame_tick();
    boss_touch(o, 0x30, 0x10);                                 /* (CD's $3E) */
    dev_autohit(o);
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                  /* [$20DB6A] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_LID, 4, 4, 0x30, 0x10);
        SET32(o, B_YSPD, (uint32_t)-0x28000);
        SET16(o, B_TOP, (uint16_t)(wy(o) - 0x30));
        SET8(o, B_COLINO, 0x3E);
        SET8(o, B_COLICNT, 2);
        uint32_t a1 = spawn(0x32);
        if (a1) {
            set_lk(a1, C_LINK, o);
            set_xy(a1, wx(o) - 0x30, wy(o));
            set_lk(o, B_GEAR, a1);
        }
    }   /* fall through */
    case 2:                                                    /* [$20DBDC] hit: up */
        if (!OB8(o, B_COLINO)) {
            o_sfx_cd(FM_BOSSHIT);
            SET8(o, OBJ_routine, 4);
            set_flag(o, 4, 1);
            SET8(o, OBJ_anim, 1);
            SET8(o, B_COLICNT, 0);
        }
        o_draw(o);
        return;
    case 4: {                                                  /* [$20DC08] */
        lid_enter(o);
        SET8(o, B_TIME, OB8(o, B_TIME) + 1);
        if (OB8(o, B_TIME) >= 0x10) {
            SET8(o, OBJ_anim, 0);
            o_animate(o, o_asset_anim(SCDA_CCZ3_LID));
        }
        cd_move(o, 0, (int32_t)r32(o + B_YSPD));
        int16_t top = OBS16(o, B_TOP);
        if (top >= wy(o)) {
            set_flag(o, 4, 0);
            SET16(o, OBJ_y_pos, (uint16_t)top);
            SET8(o, OBJ_routine, 6);
        }
        o_draw(o);
        return;
    }
    case 6:                                                    /* [$20DC52]: blows up, then waits for him */
        lid_enter(o);
        if (OB16(o, B_BOMB) < 0x78) {
            bom_dai(o);
            o_draw(o);
        }
        if (OB8(o, B_FLAGS) & 2) {
            uint32_t g = lk(o, B_GEAR);
            if (g) o_delete(g);
            o_delete(o);
        }
        return;
    }
}

/* ---- $32 a gear: its parent's height, turning while that moves ---------------- */
static void gear(uint32_t o)
{
    frame_tick();
    if (!OB8(o, OBJ_routine)) {                                /* [$20E2FA] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_GEAR, 4, 3, 0x10, 0x10);
    }
    if (OB8(o, B_FLAGS) & 1) return;                           /* (blown up: not drawn) */
    uint32_t a1 = lk(o, C_LINK);
    if (a1) {
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        if (OB8(a1, B_FLAGS) & 4) o_animate(o, o_asset_anim(SCDA_CCZ3_GEAR));
    }
    o_draw(o);
}

/* ---- $31 the platform ------------------------------------------------------------ */
static int column(uint32_t o)                                  /* [$20DE80] jabaopen_chk: 0 = shut the next gate */
{
    static const int16_t above[4] = { 496, 424, 352, 272 };
    uint32_t p = RAM_Player_1;
    uint8_t n = OB8(o, B_GATES);
    if (n >= 4) return 1;
    int16_t y = wy(p), x = wx(p);
    if (y > above[n] || y < 0x80) return 1;
    return x < 0x340 || x > 0x3C0;
}

static void gate_shut(uint32_t o)                              /* [$20DED8] jaba_open */
{
    static const int16_t at[4] = { 528, 456, 384, 304 };
    uint32_t a1 = spawn(0x3F);
    if (!a1) return;
    uint8_t n = OB8(o, B_GATES);
    SET8(o, B_GATES, n + 1);
    set_xy(a1, 0x380, at[n & 3]);
}

static void gate_last(uint32_t o)                              /* [$20DFD2] jaba_last: the top gate: bossFlags */
{
    if (OB8(o, B_GATES) != 3 || column(o)) return;
    if (!s_flags) s_bottom = r16(RAM_Camera_max_Y_pos);
    s_flags = 3;
    gate_shut(o);
}

static void flash(uint32_t o)                                  /* [$20DCD2] egg3flash: 16 frames */
{
    uint8_t t = OB8(o, B_FLASH);
    if (!t) return;
    int was = t & 0x80;
    t ^= 0x80;
    if (!was) { line1(SCDA_CCZ3_FLASHPAL); SET8(o, OBJ_frame, OB8(o, OBJ_frame) | 1); }
    else { line1(SCDA_CCZ3_BOSSPAL); SET8(o, OBJ_frame, OB8(o, OBJ_frame) & ~1); }
    SET8(o, B_FLASH, t - 1);
}

static void rise(uint32_t o)                                   /* [$20DFEC] egg3dai_l_up */
{
    switch (OB8(o, B_HITS)) {
    case 1: bom_short(o, BOM_CANNON, 4); break;               /* (where the cannon was) */
    case 2: bom_short(o, BOM_CTRL, 4); break;                 /* (about Eggman) */
    case 3: bom_short(o, BOM_CTRL2, 3); break;
    }
    set_flag(o, 4, 1);
    gate_last(o);
    cd_move(o, 0, (int32_t)r32(o + B_YSPD));
    int16_t top = OBS16(o, B_TOP);
    if (top < wy(o)) return;
    set_flag(o, 4, 0);
    SET16(o, OBJ_y_pos, (uint16_t)top);
    SET8(o, B_TIME, 0);
    if (OB8(o, B_COLICNT) != 1) {                              /* (up 72 px: ready again) */
        SET8(o, B_COLINO, 0x3F);
        SET8(o, OBJ_routine, 4);
        SET16(o, B_TOP, (uint16_t)(top - 0x48));
    } else {                                                   /* (wrecked) */
        line1(SCDA_GENEPAL);                                   /* (genecolor: the capsule's colours) */
        SET8(o, OBJ_routine, 8);
        SET16(o, B_TOP, (uint16_t)(top - 0x10));
    }
}

static void platform_hit(uint32_t o)                           /* [$20DF08] */
{
    uint32_t egg = lk(o, B_EGG);
    uint8_t k = OB8(o, B_HITS);
    SET8(o, B_HITS, k + 1);
    if (k < 2) {                                               /* hitact2 [$20DF2E]: Eggman jumps */
        /* (the first: LoadPLC 4, his fleeing art: here resident from the start) */
        if (egg) {
            SET8(egg, OBJ_routine, 4);
            SET8(egg, OBJ_anim, 1);
            SET32(egg, B_YSPD, (uint32_t)-0x58000);
        }
    } else {                                                   /* hitact3 [$20DF4A]: he flees */
        if (egg) {
            SET8(egg, OBJ_routine, 6);
            SET8(egg, B_TIME, 0);
            SET16(egg, B_TOP, (uint16_t)(wy(o) - 0x40));
            o_asset(egg, SCDA_CCZ3_EGGMAN2);
            anim_word(egg, 0x300);
        }
        SET8(o, OBJ_frame, 2);                                 /* (CD sets 4, then 2) */
    }
    o_sfx_cd(FM_BOSSHIT);                                      /* [$20DF7C] */
    SET16(o, B_BOMB, 0);
    SET8(o, OBJ_routine, 6);
    SET8(o, B_FLASH, 0x10);
    if (OB8(o, B_COLICNT) == 1) {                              /* (the last: 1000 points, the level's song) */
        D(0) = 100;
        tri_jsr(S3K_HUD_AddToScore);
        SET8(o, B_COLINO, 0);
        SET16(o, B_TOP, (uint16_t)(wy(o) - 0x70));
        tri_scd_level_music();                                 /* (CD: by the good future, its two songs) */
        w8(RAM_Boss_flag, 0);
    }
    rise(o);
}

static int platform_step(uint32_t o)                           /* the routine; 0: not drawn */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                  /* [$20DD5A] */
        line1(SCDA_CCZ3_BOSSPAL);
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_BODY, 4, 4, 0x50, 0x10);
        uint32_t a1 = spawn_after(o, 0x33);                    /* Eggman, on top */
        if (!a1) break;
        set_lk(o, B_EGG, a1);
        set_lk(a1, C_LINK, o);
        set_xy(a1, wx(o), wy(o) - 0x30);
        if (!(a1 = spawn_after(o, 0x32))) break;               /* the gear */
        set_lk(a1, C_LINK, o);
        set_lk(o, B_GEAR, a1);
        set_xy(a1, wx(o) - 0x40, wy(o));
        if (!(a1 = spawn_after(o, 0x3D))) break;               /* the cannon */
        set_lk(a1, C_LINK, o);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        break;
    }
    case 2: {                                                  /* [$20DE00]: down the shaft with the player */
        uint32_t g = lk(o, B_GEAR);
        if (g) SET8(g, OBJ_anim, 1);
        int16_t y = wy(p);
        if (y >= 0x1C0) {                                      /* [$20DE44]: in place */
            SET16(o, OBJ_y_pos, 0x1C0);
            set_flag(o, 4, 0);
            SET8(o, OBJ_routine, 4);
            SET32(o, B_YSPD, (uint32_t)-0x28000);
            SET16(o, B_TOP, 0x1C0 - 0x48);
            SET8(o, B_COLINO, 0x3F);
            SET8(o, B_COLICNT, 4);
            if (g) SET8(g, OBJ_anim, 0);
            break;
        }
        if (y < 0xDC) y = 0xDC;
        set_flag(o, 4, OBS16(o, B_TOP) != y);
        SET16(o, B_TOP, (uint16_t)y);
        SET16(o, OBJ_y_pos, (uint16_t)y);
        break;
    }
    case 4:                                                    /* [$20DEC4]: the fight */
        if (!column(o)) gate_shut(o);
        if (!OB8(o, B_COLINO)) platform_hit(o);
        break;
    case 6: rise(o); break;
    case 8:                                                    /* [$20E06C]: wrecked, blowing up */
        bom_dai(o);
        gate_last(o);
        SET8(o, B_TIME, OB8(o, B_TIME) + 1);
        if (OB8(o, B_TIME) == 4) SET8(o, OBJ_frame, 6);
        cd_move(o, 0, (int32_t)r32(o + B_YSPD));
        if (OBS16(o, B_TOP) >= wy(o)) SET8(o, OBJ_routine, 0xA);
        break;
    case 0xA:                                                  /* [$20E0A0]: gone, once the top gate is shut */
        bom_dai(o);
        gate_last(o);
        if ((OB8(o, B_FLAGS) & 1) && OB8(o, B_GATES) == 4) {
            uint32_t g = lk(o, B_GEAR);
            if (g) o_delete(g);
            o_delete(o);
            return 0;
        }
        break;
    }
    return 1;
}

static void platform(uint32_t o)
{
    frame_tick();
    boss_touch(o, 0x40, 0x10);                                 /* (CD's $3F) */
    dev_autohit(o);
    if (OB8(o, B_HITS)) {                                      /* [$20DD0C]: the camera looks up */
        uint16_t v = r16(RAM_Distance_from_top);
        if (v >= 0xB0) v = 0xB2;
        else if ((v = (uint16_t)(v + 6)) >= 0xB2) v = 0xB2;
        w16(RAM_Distance_from_top, v);
    }
    static int log = -1;                                       /* development */
    if (log < 0) log = getenv("TRILOGY_BOSSLOG") != NULL;
    if (log && !(r16(RAM_Level_frame_counter) & 7))
        fprintf(stderr, "[boss3] f=%u r=%02X hits=%u col=%02X/%u gates=%u flags=%02X y=%04X top=%04X p=%04X,%04X bottom=%04X\n",
                (unsigned)r16(RAM_Level_frame_counter), OB8(o, OBJ_routine), OB8(o, B_HITS), OB8(o, B_COLINO),
                OB8(o, B_COLICNT), OB8(o, B_GATES), s_flags, OB16(o, OBJ_y_pos), OB16(o, B_TOP),
                OB16(RAM_Player_1, OBJ_x_pos), OB16(RAM_Player_1, OBJ_y_pos), r16(RAM_Camera_max_Y_pos));
    if (!platform_step(o) || (OB8(o, B_FLAGS) & 1)) return;   /* (blown up: not drawn) */
    flash(o);
    o_draw(o);
}

/* ---- $33 Eggman ---------------------------------------------------------------- */
static void eggman(uint32_t o)
{
    uint32_t a1 = lk(o, C_LINK);
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                    /* [$20E21E] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_EGGMAN, 4, 6, 0x10, 0x10);
        SET16(o, B_TOP, OB16(o, OBJ_y_pos));
        /* fall through */
    case 2:                                                    /* [$20E250]: on the platform */
        if (a1) SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        break;
    case 4:                                                    /* [$20E25C]: hit, he jumps */
        cd_move(o, 0, (int32_t)r32(o + B_YSPD));
        SET32(o, B_YSPD, r32(o + B_YSPD) + 0x3000);
        if (!a1 || wy(a1) > wy(o)) break;
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
        SET8(o, OBJ_routine, 2);
        if (OB8(a1, B_HITS) != 2) anim_word(o, 1);
        else {                                                 /* (after the second: in his pod) */
            SET8(a1, OBJ_frame, 2);
            o_asset(o, SCDA_CCZ3_EGGMAN2);
            anim_word(o, 0x300);
        }
        break;
    case 6: {                                                  /* [$20E2A8]: up out of it */
        cd_move(o, 0, -0x40000);
        int16_t top = OBS16(o, B_TOP);
        if (top < wy(o)) break;
        anim_word(o, 0x400);
        SET16(o, OBJ_priority, 3 << 7);
        SET16(o, OBJ_y_pos, (uint16_t)top);
        SET8(o, OBJ_routine, 8);
        SET16(o, B_TOP, (uint16_t)(wx(o) + 0x230));
        break;
    }
    case 8:                                                    /* [$20E2DE]: away right */
        cd_move(o, 0x40000, 0);
        if (OBS16(o, B_TOP) > wx(o)) break;
        o_delete(o);
        return;
    }
    if (OB8(o, OBJ_render) & 0x80) o_animate(o, o_asset_anim(SCDA_CCZ3_EGGMAN));
    o_draw(o);
}

/* ---- $3D the cannon: every 120 frames a punch bomb, left and right by turns,
 * while the player is above y $400 and no gate is shut; gone at the first hit -- */
static void cannon(uint32_t o)
{
    static const uint8_t frames[8] = { 0, 1, 0, 2, 0, 3, 0, 4 };
    uint32_t a2 = lk(o, C_LINK);
    if (a2) SET16(o, OBJ_y_pos, (uint16_t)(wy(a2) + 0x20));
    if (!a2 || OB8(a2, B_HITS)) { o_delete(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                    /* [$20E38E] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_CANNON, 4, 4, 0x18, 0x10);
        /* fall through */
    case 2: {                                                  /* [$20E3BC] */
        if (wy(RAM_Player_1) >= 0x400 || OB8(a2, B_GATES)) break;
        SET16(o, K_TIME, OB16(o, K_TIME) + 1);
        if ((int16_t)OB16(o, K_TIME) < 0x78) break;
        SET16(o, K_TIME, 0);
        SET8(o, OBJ_routine, 4);
        uint32_t a1 = spawn(0x3E);
        if (!a1) break;
        set_xy(a1, wx(o), wy(o) + 8);
        int16_t vx = (int16_t)((o_random() & 0x7FFF) % 0x400), dx = 0xC;
        if (!OB8(o, OBJ_anim_frame)) { vx = (int16_t)-vx; dx = -dx; }
        SET16(a1, OBJ_x_pos, (uint16_t)(wx(a1) + dx));
        SET16(a1, OBJ_x_vel, (uint16_t)vx);
        SET16(a1, OBJ_y_vel, 0x300);
        break;
    }
    case 4: {                                                  /* [$20E434]: the shot's frames */
        int8_t t = (int8_t)(OB8(o, OBJ_anim_timer) - 1);
        SET8(o, OBJ_anim_timer, (uint8_t)t);
        if (t > 0) break;
        uint8_t d0 = (uint8_t)(OB8(o, OBJ_anim_frame) + 1);
        if (d0 == 4) SET8(o, OBJ_routine, 2);
        else if (d0 == 8) { d0 = 0; SET8(o, OBJ_routine, 2); }
        else SET8(o, OBJ_anim_timer, 2);
        SET8(o, OBJ_anim_frame, d0);
        SET8(o, OBJ_frame, frames[d0]);
        break;
    }
    }
    o_draw(o);
}

/* ---- $3E a punch bomb: falls ($20 a frame, to $600) bouncing off floors,
 * ceilings and walls; touched (CD's special $17) it throws the player away
 * from it at $700 and bursts; else it bursts after 240 frames. Its blast is
 * S3K's (CD's harmless explosion; the disc's routines 4-$A) -------------------- */
static void bomb_burst(uint32_t o)
{
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 6);
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
    o_sfx_cd(FM_EXPLODE);
}

static void bomb_throw(uint32_t o, uint32_t p)                 /* [$20E584] */
{
    int16_t s, c;
    o_sincos(o_atan((int16_t)(wx(o) - wx(p)), (int16_t)(wy(o) - wy(p))), &s, &c);
    SET16(p, OBJ_x_vel, (uint16_t)(((int32_t)c * -0x700) >> 8));
    SET16(p, OBJ_y_vel, (uint16_t)(((int32_t)s * -0x700) >> 8));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~(ST_ROLLJUMP | ST_PUSH | ST_ONOBJ));
    SET8(p, P_JUMPING, 0);
}

static void bomb(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                                /* [$20E48E] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_BOMB, 4, 5, 8, 8);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0xD7));
        SET16(o, PB_AX, 0);
        SET16(o, PB_AY, 0x20);
        SET16(o, PB_VMAX, 0x600);
    }
    uint8_t hit = OB8(o, OBJ_coll_prop);                       /* [$20E4D2] (S3K's special touch: +1 player 1, +2 player 2) */
    if (hit) {
        SET8(o, OBJ_coll_prop, 0);
        if (hit & 1) bomb_throw(o, o_player(0));               /* (Sonic CD: player 1) */
        if (hit & 2) bomb_throw(o, o_player(1));
        bomb_burst(o);
        return;
    }
    SET16(o, PB_TIME, OB16(o, PB_TIME) + 1);
    if ((int16_t)OB16(o, PB_TIME) >= 0xF0) { bomb_burst(o); return; }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) + 1);
    if (OB8(o, OBJ_anim_timer) & 2) SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
    int16_t ay = OBS16(o, PB_AY), vmax = OBS16(o, PB_VMAX);   /* add_spd3 [$20E65E] */
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (uint16_t)ay);
    SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, PB_AX));
    if (ay > 0 ? vmax <= OBS16(o, OBJ_y_vel) : ay < 0 && vmax >= OBS16(o, OBJ_y_vel))
        SET16(o, OBJ_y_vel, (uint16_t)vmax);
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
    int16_t d1;
    if (OBS16(o, OBJ_y_vel) >= 0) {
        if ((d1 = o_floor_dist(o)) <= 0) {
            SET16(o, OBJ_y_pos, (uint16_t)(wy(o) + d1));
            SET16(o, OBJ_y_vel, (uint16_t)-OBS16(o, OBJ_y_vel));
        }
    } else if ((d1 = o_ceiling_dist(o)) <= 0) {
        SET16(o, OBJ_y_pos, (uint16_t)(wy(o) - d1));
        SET16(o, OBJ_y_vel, (uint16_t)-OBS16(o, OBJ_y_vel));
    }
    if (OBS16(o, OBJ_x_vel) >= 0) {                            /* (CD's x radius: none) */
        if ((d1 = o_wall_right(o, 0)) <= 0) {
            SET16(o, OBJ_x_pos, (uint16_t)(wx(o) + d1));
            SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel));
        }
    } else if ((d1 = o_wall_left(o, 0)) <= 0) {
        SET16(o, OBJ_x_pos, (uint16_t)(wx(o) - d1));
        SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel));
    }
    o_touch(o);
    o_draw(o);
}

/* ---- $3F a gate: shuts in eight frames (from $30's spawn, routine 6: six)
 * and is a floor from above ------------------------------------------------------ */
static void gate(uint32_t o)
{
    frame_tick();
    if (OB8(o, OBJ_routine) != 4) {
        switch (OB8(o, OBJ_routine)) {
        case 6:                                                /* [$20EA1A] */
            SET8(o, G_LAST, 5);
            goto setup;
        case 0:                                                /* [$20EA26] */
            SET8(o, G_LAST, 7);
        setup:
            SET8(o, OBJ_routine, 2);
            init(o, SCDA_CCZ3_GATE, OB8(o, OBJ_render) | 4, 3, 0x48, 0x10);
            /* fall through */
        case 2: {                                              /* [$20EA56] (its script: 0-7 a frame each) */
            uint8_t k = OB8(o, OBJ_anim_frame);
            SET8(o, OBJ_frame, k);
            SET8(o, OBJ_anim_frame, k + 1);
            if (k == OB8(o, G_LAST)) SET8(o, OBJ_routine, 4);
            break;
        }
        }
    }
    if (OB8(o, OBJ_render) & 0x80) o_platform(o, 0x48 + 10, 0x10, OB16(o, OBJ_x_pos));   /* [$20EA70] */
    o_draw(o);
}

/* ---- $36 the drain (sikake33 [$20EB5E]): the spinner (subtype < 0), its
 * lamps (0) and the ball saver (> 0) ----------------------------------------------- */
static int ours36(uint32_t a) { return a && r32(a) == native3(0x36); }
static void routine_of(uint32_t a, int r) { if (ours36(a)) SET8(a, OBJ_routine, r); }

static void spin_check(uint32_t o)                             /* [$20AAF2] */
{
    static const int16_t lamp_y[3] = { 1176, 1200, 1224 };
    uint32_t p = RAM_Player_1;
    if ((uint16_t)(wy(o) - wy(p) + 16) >= 32 || (uint16_t)(wx(o) - wx(p) + 16) >= 32) {
        SET8(o, D_IN, 0);
        return;
    }
    if (OB8(o, D_IN)) return;
    SET8(o, D_IN, 0xFF);
    anim_word(o, OBS16(p, OBJ_y_vel) < 0 ? 0x1FF : 0xFF);      /* (up through it, the other way) */
    uint16_t was = OB16(o, D_SPIN);
    SET16(o, D_SPIN, 0x80);
    if (was) return;                                           /* (still turning: no count) */
    if (OBS16(o, D_LIT) == 4) {                                /* (the fourth: all out, the saver opens) */
        SET16(o, D_LIT, (uint16_t)-2);
        for (int k = 0; k < 3; k++) routine_of(lk(o, D_LAMP + k * 2), 4);
        routine_of(lk(o, D_STOP), 6);
        return;
    }
    int lit = OBS16(o, D_LIT) + 2;
    SET16(o, D_LIT, (uint16_t)lit);
    SET16(o, D_LAMP + 4, OB16(o, D_LAMP + 2));
    SET16(o, D_LAMP + 2, OB16(o, D_LAMP));
    uint32_t a1 = spawn(0x36);                                 /* (subtype 0: a lamp) */
    if (a1) {
        set_lk(o, D_LAMP, a1);
        set_lk(a1, D_PARENT, o);
        set_xy(a1, 0x380, lamp_y[lit / 2]);
    }
    if (lit == 4) routine_of(lk(o, D_STOP), 4);                /* (three: the saver closes) */
}

static void spinner(uint32_t o)
{
    frame_tick();
    if (!OB8(o, OBJ_routine)) {                                /* [$20AA60] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_SPINNER, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET16(o, D_LIT, (uint16_t)-2);
        if (OB8(o, OBJ_subtype) == 0xFF) {
            SET16(o, D_SPIN, 0x80);
            SET8(o, D_IN, 0xFF);
            SET8(o, D_IN + 1, 0xFF);
        }
        uint32_t a1 = spawn(0x36);
        if (a1) {
            set_lk(o, D_STOP, a1);
            set_lk(a1, D_PARENT, o);
            SET8(a1, OBJ_subtype, 1);
            set_xy(a1, 0x380, 0x4F8);
        }
    }
    spin_check(o);                                             /* [$20AACE] */
    if (OB16(o, D_SPIN)) {
        SET16(o, D_SPIN, OB16(o, D_SPIN) - 1);
        o_animate(o, o_asset_anim(SCDA_CCZ3_SPINNER));
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

static void lamp(uint32_t o)                                   /* [$20A674] */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_LIGHT, OB8(o, OBJ_render) | 4, 3, 8, 8);
        break;
    case 4:
        o_delete(o);
        return;
    }
    if (!ours36(lk(o, D_PARENT))) { o_delete(o); return; }
    o_draw(o);
}

static void stopper(uint32_t o)                                /* [$20AC18] */
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                    /* [$20AC4A] */
        SET8(o, OBJ_routine, 2);
        init(o, SCDA_CCZ3_STOPPER, OB8(o, OBJ_render) | 4, 3, 0x10, 8);
        /* fall through */
    case 2:
    case 0xA:                                                  /* [$20AC74]: shut (frame 2), a floor that throws him up */
        if (OB8(o, OBJ_frame) != 2) break;
        o_platform(o, 0x10 + 10, 8, OB16(o, OBJ_x_pos));
        for (int n = 0; n < 2; n++) {                          /* (Sonic CD: player 1) */
            if (!(OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))) continue;
            uint32_t p = o_player(n);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~(ST_ONOBJ | ST_ROLLJUMP | ST_PUSH));
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, (uint16_t)-0x400);
        }
        break;
    case 4:                                                    /* [$20ACB6] shutting */
        anim_word(o, 0xFF);
        SET8(o, OBJ_routine, 8);
        o_animate(o, o_asset_anim(SCDA_CCZ3_STOPPER));
        break;
    case 6:                                                    /* [$20ACC2] opening */
        anim_word(o, 0x1FF);
        SET8(o, OBJ_routine, 8);
        /* fall through */
    case 8:                                                    /* (its script ends $FC: routine $A) */
        o_animate(o, o_asset_anim(SCDA_CCZ3_STOPPER));
        break;
    }
    if (!ours36(lk(o, D_PARENT))) { o_delete(o); return; }
    o_draw(o);
}

static void drain(uint32_t o)
{
    int8_t s = (int8_t)OB8(o, OBJ_subtype);
    if (s < 0) spinner(o);
    else if (!s) lamp(o);
    else stopper(o);
}

/* ---- registration -------------------------------------------------------------- */
void scd_ccz3_level_entry(void)
{
    s_flags = 0;
    s_frame = 0xFFFF;
}

void scd_ccz3_boss_register(TriObjFn *t, int base3)
{
    s_base3 = base3;
    t[base3 + 0x15] = cd_capsule;
    t[base3 + 0x30] = lid;
    t[base3 + 0x31] = platform;
    t[base3 + 0x32] = gear;
    t[base3 + 0x33] = eggman;
    t[base3 + 0x36] = drain;
    t[base3 + 0x3D] = cannon;
    t[base3 + 0x3E] = bomb;
    t[base3 + 0x3F] = gate;
}
