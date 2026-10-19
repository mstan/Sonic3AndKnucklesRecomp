/*
 * scd_ccz_badnik.c — Collision Chaos' badniks, Metal Sonic's projector and the
 * animals (Sonic CD; the disc's R31A-R32D programs; decomp: R3\KAMA.C, GA3.C,
 * TENTOU.C, MOVIE3.C, FRIEND3.C).
 *
 *   $24 Kama-Kama   a mantis: drops to the floor, faces player 1 and, with
 *                   him 96 px off and 32 up or down, throws its two scythes
 *                   ($25), waits, then walks a way and turns at walls and
 *                   edges, and watches again
 *   $25 scythe      held in its paw a while, then thrown along; harmless after
 *                   a second; a rolling (or invincible) player knocks it away
 *   $2D Ga          a moth: hatches from its cocoon and flies at the player in
 *                   a zigzag, dropping pollen (subtype 1: pollen, the ring's
 *                   sparkle, harmless)
 *   $2E Tentou      a ladybird: settles on the floor; with the player near it
 *                   runs from him along the floor dropping bombs (subtype 1:
 *                   a bomb, bursting 4 s after it lands); at a wall it flies
 *                   off up
 *   $38 projector   Metal Sonic's (the past): a rolling player breaks it; its
 *                   show (Metal Sonic, the animals) ends and the act's animals
 *                   are free
 *   $39 animals     free in the past and present once the projector is gone,
 *                   and in a good future; subtype bit 7: one of the
 *                   projector's holograms, subtype (low bits) 0 flying, else
 *                   on the ground (it turns at every other landing)
 * Robots are subtype 0 as built, any other their damaged selves (the Kama-
 * Kama's and Tentou's: bit 7; slower, no attack): a good future blows those up
 * (DestroyOnGoodFuture). The projector and animals are Palmtree Panic's
 * (scd_ppz.c) at this round's ids and places: the animals' ground hop turns
 * at every other landing (as Wacky Workbench's), the projector's show sits
 * elsewhere and it is gone at once if its show cannot be made.
 */
#include "trilogy_obj.h"

/* this zone's asset ids (scd_level.py CD_ASSETS; -> trilogy_obj.h) */

#define P_ANIM          0x20
#define P_STATUS2       0x2B               /* status_secondary: bit 1 invincible */
#define ST_ROLL         0x04
#define FM_SCYTHE       0xB9               /* (wpkama_move: a thrown scythe's whirr, each turn) */
#define FM_EXPLODE      0x9E               /* (ten_b_die: a bomb bursts; m_baku: the projector's blasts) */
#define O_PREV_ANIM_S3K 0x21               /* (prev_anim: what o_animate compares) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

int scd_good_future(void);                     /* scd_time.c */
int scd_proj_destroyed(void);
void scd_proj_destroyed_set(void);

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

static void set_flip(uint32_t o, int on)        /* (render and status bit 0) */
{
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (on ? 1 : 0));
    SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | (on ? 1 : 0));
}

static int32_t lval(uint32_t o, int f) { return (int32_t)r32(o + (uint32_t)f); }

static void move_vel(uint32_t o)                /* ObjMove: x, y by their speeds */
{
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, (int32_t)OBS16(o, OBJ_y_vel) << 8);
}

/* DrawObject + CheckObjDespawn (its own x), touchable */
static void badnik_end(uint32_t o)
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

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
    if (render & 0x80) o_sfx_cd(FM_EXPLODE);
}

/* ObjGetLWallDist as Sonic CD's has it (the d3 one): S&K's left wall check
 * flips the probe's x within its block first (eori #$F); undone here */
static int16_t cd_wall_left(uint32_t o, int d3)
{
    int x = OB16(o, OBJ_x_pos);
    return o_wall_left(o, (((x + d3) & 0xFFFF) ^ 0xF) - x);
}

static int16_t floor_at(uint32_t o, uint16_t x)  /* (ObjFloorDist with its x at x) */
{
    uint16_t x0 = OB16(o, OBJ_x_pos);
    SET16(o, OBJ_x_pos, x);
    int16_t d1 = o_floor_dist(o);
    SET16(o, OBJ_x_pos, x0);
    return d1;
}

/* ---- $24 Kama-Kama, $25 its scythes ---------------------------------------- */
#define KM_TIME  0x32                  /* oVar2C */
#define KM_SUB   0x34                  /* oVar2E: its subtype */
#define SC_GRAV  0x30                  /* oVar2A: (a scythe) */
#define SC_TIME  0x32                  /* oVar2C */
#define SC_FACE  0x34                  /* oVar2E: its Kama-Kama's render flags */
#define SC_PAR   0x36                  /* oVar30 */
#define SC_X0    0x38                  /* oVar32 */
#define SC_HURT  0x3A                  /* oVar34: frames it hurts */
static void kama_throw(uint32_t o)
{
    static const int8_t at[2][3] = { { 0x11, -4, 0x40 }, { 9, -6, 0x14 } };   /* dx, dy, wait */
    if (OB8(o, OBJ_subtype) & 0x80) return;                   /* (damaged: none) */
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) continue;
        SET32(a1, OBJ_code, CD_CODE(0x25));
        SET16(a1, SC_PAR, (uint16_t)o);
        SET16(a1, SC_TIME, (uint16_t)at[k][2]);
        SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
        SET8(a1, SC_FACE, OB8(o, OBJ_render));
        SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + at[k][1]));
        SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + (OB8(o, OBJ_render) & 1 ? at[k][0] : -at[k][0])));
    }
}

static void kama_kama(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = (OB8(o, OBJ_subtype) & 0x80) != 0;
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, KM_SUB, OB8(o, OBJ_subtype));
        o_init(o, dmg ? SCDA_KAMAKAMA2 : SCDA_KAMAKAMA, 4, 3, 0x10, 0x14);
        SET8(o, OBJ_frame, dmg ? 2 : 1);
        SET8(o, OBJ_y_radius, 0x14);
        SET16(o, OBJ_y_vel, 0x100);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x06));
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                                   /* down on to the floor */
        move_vel(o);
        if (o_floor_dist(o) < 0) SET8(o, OBJ_routine, 4);
        break;
    case 4: {                                                 /* watching player 1 */
        int near = 0;
        if (OB8(o, OBJ_render) & 0x80) {
            uint16_t d0 = (uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)), d1;
            int right = OB16(o, OBJ_x_pos) < OB16(p, OBJ_x_pos);
            if (right) d0 = (uint16_t)-d0;
            set_flip(o, right);                               /* (facing him) */
            d1 = (uint16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
            if (OB16(o, OBJ_y_pos) < OB16(p, OBJ_y_pos)) d1 = (uint16_t)-d1;
            near = d1 < 0x20 && d0 < 0x60;
        }
        if (!near) { SET8(o, OBJ_anim, 1); break; }
        SET8(o, OBJ_anim, 2);                                 /* (it throws) */
        kama_throw(o);
        SET16(o, KM_TIME, 120);
        SET8(o, OBJ_routine, 6);
    }   /* fall through */
    case 6:
        SET16(o, KM_TIME, OB16(o, KM_TIME) - 1);
        if (OB16(o, KM_TIME)) break;
        SET8(o, OBJ_anim, 3);
        SET16(o, KM_TIME, 60);
        SET8(o, OBJ_routine, 8);
        /* fall through */
    case 8:
        SET16(o, KM_TIME, OB16(o, KM_TIME) - 1);
        if (OB16(o, KM_TIME)) break;
        SET8(o, OBJ_routine, 10);
        /* fall through */
    case 10: {                                                /* walking */
        int16_t v = dmg ? 0x80 : 0x100;
        SET8(o, OBJ_anim, 0);
        SET16(o, OBJ_x_vel, (uint16_t)(OB8(o, OBJ_status) & 1 ? v : -v));
        move_vel(o);                                          /* (and down 1 px: its y speed; the floor sets it back) */
        int16_t d1;
        if (o_wall_left(o, -10) >= 0 && o_wall_right(o, 10) >= 0 &&
            ((d1 = o_floor_dist(o)) == 0 || (d1 < 7 && d1 >= -7))) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            break;
        }
        SET16(o, KM_TIME, 0x48);                              /* (a wall, or an edge: it stops, then turns) */
        SET8(o, OBJ_anim, 0);
        SET8(o, O_PREV_ANIM_S3K, 3);                          /* (move.w #3,oAnim: oPrevAnim 3, the walk restarts) */
        SET8(o, OBJ_routine, 12);
    }   /* fall through */
    default:
        SET16(o, KM_TIME, OB16(o, KM_TIME) - 1);
        if (OB16(o, KM_TIME)) break;
        turn(o);
        SET8(o, OBJ_routine, 10);
        break;
    }
    o_animate(o, o_asset_anim(dmg ? SCDA_KAMAKAMA2 : SCDA_KAMAKAMA));
    badnik_end(o);
}

static void scythe_hit(uint32_t o)              /* (a rolling player knocks it off) */
{
    for (int n = 0; n < 2; n++) {                             /* (Sonic CD: player 1) */
        uint32_t p = o_player(n);
        if (!o_player_active(p)) continue;
        if (!(OB8(p, P_STATUS2) & 2) && !(OB8(p, OBJ_status) & ST_ROLL)) continue;
        int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x1C);
        int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x1C);
        if (dx < 0 || dx >= 0x38 || dy < 0 || dy >= 0x38) continue;
        SET8(o, OBJ_coll_flags, 0);
        SET8(o, OBJ_anim, 1);
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(p, OBJ_x_vel));
        SET16(o, OBJ_y_vel, (uint16_t)-0x800);
        SET16(o, SC_GRAV, 0x40);
        return;
    }
}

static void scythe(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SCYTHE, 4, 1, 4, 4);
        SET8(o, OBJ_x_radius, 4);
        SET8(o, OBJ_y_radius, 4);
        SET16(o, SC_X0, OB16(o, OBJ_x_pos));
        int right = OB8(o, SC_FACE) & 1;
        if (!right) set_flip(o, 1);                           /* (thrown left: flipped) */
        SET16(o, OBJ_x_vel, right ? 0x300 : -0x300);
        break;
    }
    case 2:                                                   /* in its paw */
        SET16(o, SC_TIME, OB16(o, SC_TIME) - 1);
        if (!OB16(o, SC_TIME)) {
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x87));
            SET8(o, SC_HURT, 60);
        }
        if (r32(0xFF0000u | OB16(o, SC_PAR)) != CD_CODE(0x24)) { o_delete(o); return; }
        o_animate(o, o_asset_anim(SCDA_SCYTHE));
        break;
    default:                                                  /* thrown */
        move_vel(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, SC_GRAV));
        if (!OB8(o, OBJ_frame) && (OB8(o, OBJ_render) & 0x80)) o_sfx_cd(FM_SCYTHE);
        scythe_hit(o);
        SET8(o, SC_HURT, OB8(o, SC_HURT) - 1);
        if (!OB8(o, SC_HURT)) { SET8(o, SC_HURT, 1); SET8(o, OBJ_coll_flags, 0); }
        o_animate(o, o_asset_anim(SCDA_SCYTHE));
        break;
    }
    if (cd_despawn(o, OB16(o, SC_X0))) return;
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

/* ---- $2D Ga ------------------------------------------------------------------ */
#define GA_TIME  0x30                  /* oVar2A */
#define GA_VX    0x32                  /* oVar2C (16.16) */
#define GA_VY    0x36                  /* oVar30 (16.16) */
#define GA_DROPS 0x45                  /* oVar3F: drops pollen */
static void ga_pollen(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_GAPOLLEN, OB8(o, OBJ_render) | 4, 1, 6, 6);
        SET8(o, OBJ_x_radius, 6);
        SET8(o, OBJ_y_radius, 6);
        SET16(o, GA_TIME, 0xB4);
    }
    SET16(o, GA_TIME, OB16(o, GA_TIME) - 1);
    if (!OB16(o, GA_TIME)) { o_delete(o); return; }
    cd_move(o, 0, 0x10000);
    o_animate(o, o_asset_anim(SCDA_GAPOLLEN));
    o_draw(o);
}

static void ga(uint32_t o)
{
    if ((uint8_t)(OB8(o, OBJ_subtype) - 1) < 0x80) { ga_pollen(o); return; }   /* (1-$80) */
    if (cd_destroy_good_future(o)) return;
    int dmg = (OB8(o, OBJ_subtype) & 0x80) != 0, asset = dmg ? SCDA_GA2 : SCDA_GA;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, asset, 4, 1, 0x10, 8);
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, GA_TIME, 0x3D);
        if (!dmg) SET8(o, GA_DROPS, 0xFF);
        /* fall through */
    case 2:                                                   /* a cocoon */
        SET16(o, GA_TIME, OB16(o, GA_TIME) - 1);
        if (OB16(o, GA_TIME)) break;
        SET8(o, OBJ_routine, 4);
        SET16(o, GA_TIME, 0x3D);
        break;
    case 4:                                                   /* hatching */
        SET16(o, GA_TIME, OB16(o, GA_TIME) - 1);
        if (!OB16(o, GA_TIME)) {
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x25));
            SET8(o, OBJ_routine, 6);
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
            SET16(o, GA_TIME, 0x33);
        }
        o_animate(o, o_asset_anim(asset));
        break;
    case 6:                                                   /* out */
        SET16(o, GA_TIME, OB16(o, GA_TIME) - 1);
        if (!OB16(o, GA_TIME)) {
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x26));
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 1);
            SET32(o, GA_VY, 0x8000);
            SET16(o, GA_TIME, dmg ? 0x30 : 0x20);
            SET32(o, GA_VX, dmg ? (uint32_t)-0x4000 : (uint32_t)-0x8000);
            if ((int16_t)(OB16(o, OBJ_x_pos) - OB16(RAM_Player_1, OBJ_x_pos)) < 0) {   /* (at the player) */
                SET32(o, GA_VX, (uint32_t)-lval(o, GA_VX));
                set_flip(o, 1);
            }
        }
        o_animate(o, o_asset_anim(asset));
        break;
    default:                                                  /* flying, in a zigzag */
        cd_move(o, lval(o, GA_VX), lval(o, GA_VY));
        SET16(o, GA_TIME, OB16(o, GA_TIME) - 1);
        if (!OB16(o, GA_TIME)) {
            SET16(o, GA_TIME, dmg ? 0x61 : 0x41);
            SET32(o, GA_VY, (uint32_t)-lval(o, GA_VY));
            if (OB8(o, GA_DROPS)) {
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, CD_CODE(0x2D));
                    SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
                    SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
                    SET8(a1, OBJ_subtype, 1);
                }
            }
        }
        o_animate(o, o_asset_anim(asset));
        break;
    }
    badnik_end(o);
}

/* ---- $2E Tentou ---------------------------------------------------------------- */
#define TE_VX    0x30                  /* oVar2A (16.16) */
#define TE_BOB   0x34                  /* oVar2E (16.16) */
#define TE_Y     0x38                  /* oVar32: its place, between bobs (16.16) */
#define TE_COUNT 0x3C                  /* oVar36 */
#define TE_DROPS 0x45                  /* oVar3F */
#define TB_TIME  0x30                  /* oVar2A: (a bomb) */
static void te_bob(uint32_t o)
{
    SET16(o, TE_COUNT, OB16(o, TE_COUNT) + 1);
    cd_move(o, 0, lval(o, TE_BOB));
    if (!(OB16(o, TE_COUNT) & 0x0F)) SET32(o, TE_BOB, (uint32_t)-lval(o, TE_BOB));
}

static void tentou_bomb(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TENTOUBOMB, OB8(o, OBJ_render) | 4, 2, 6, 6);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x8B));
        SET8(o, OBJ_x_radius, 6);
        SET8(o, OBJ_y_radius, 6);
        /* fall through */
    case 2: {                                                 /* (CD bursts it touched too: its touch
                                                                 status, never set by a hurting touch) */
        cd_move(o, 0, 0x10000);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, TB_TIME, 120);
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    case 4:
        SET16(o, TB_TIME, OB16(o, TB_TIME) - 1);
        if (OB16(o, TB_TIME)) break;
        SET16(o, TB_TIME, 120);
        SET8(o, OBJ_routine, 6);
        break;
    case 6:                                                   /* flashing */
        SET16(o, TB_TIME, OB16(o, TB_TIME) - 1);
        if (!OB16(o, TB_TIME)) SET8(o, OBJ_routine, 8);
        o_animate(o, o_asset_anim(SCDA_TENTOUBOMB));
        break;
    default:
        explode_here(o);
        return;
    }
    o_touch(o);
    o_draw(o);
}

static void tentou(uint32_t o)
{
    if ((uint8_t)(OB8(o, OBJ_subtype) - 1) < 0x80) { tentou_bomb(o); return; }   /* (1-$80) */
    if (cd_destroy_good_future(o)) return;
    int dmg = (OB8(o, OBJ_subtype) & 0x80) != 0, asset = dmg ? SCDA_TENTOU2 : SCDA_TENTOU;
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, asset, OB8(o, OBJ_render) | 0x84, 1, 0x0E, 0x0E);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x06));
        SET8(o, OBJ_y_radius, 0x0E);
        if (!dmg) SET8(o, TE_DROPS, 0xFF);
        /* fall through */
    case 2: {                                                 /* settling */
        cd_move(o, 0, 0x10000);
        int16_t d1 = (int16_t)(o_floor_dist(o) - 8);
        if (d1 > 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d1);
        SET32(o, TE_Y, r32(o + OBJ_y_pos));
        SET32(o, TE_BOB, 0x3000);
        SET16(o, TE_COUNT, 8);
        SET8(o, OBJ_routine, lval(o, TE_VX) ? 6 : 4);         /* (back down off an edge: on its way) */
        break;
    }
    case 4: {                                                 /* waiting for the player */
        te_bob(o);
        if ((uint16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos) + 0x50) >= 0xA0) break;
        int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
        if ((uint16_t)(dx + 0x50) >= 0xA0) break;
        SET8(o, OBJ_routine, 6);
        SET32(o, TE_VX, (uint32_t)-0xC000);
        if (dx >= 0) { turn(o); SET32(o, TE_VX, 0xC000); }    /* (away from him) */
        break;
    }
    case 6: {                                                 /* off along the floor */
        cd_move(o, lval(o, TE_VX), 0);
        SET32(o, OBJ_y_pos, r32(o + TE_Y));
        int d3 = OB8(o, OBJ_x_radius);
        if ((lval(o, TE_VX) < 0 ? cd_wall_left(o, d3) : o_wall_right(o, d3)) < 0) { SET8(o, OBJ_routine, 8); break; }
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0x10) {                                     /* (an edge) */
            SET8(o, OBJ_routine, 10);
            te_bob(o);
            SET32(o, TE_Y, r32(o + OBJ_y_pos));
            break;
        }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 - 8);
        SET32(o, TE_Y, r32(o + OBJ_y_pos));
        te_bob(o);
        if (!OB8(o, TE_DROPS)) break;
        SET16(o, TE_COUNT, OB16(o, TE_COUNT) & 0x7F);         /* (a bomb every 128 frames) */
        if (OB16(o, TE_COUNT)) break;
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, CD_CODE(0x2E));
        SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
        SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x10);
        SET8(a1, OBJ_subtype, 1);
        break;
    }
    case 8:                                                   /* a wall: up and away */
        cd_move(o, 0, -0x8000);
        break;
    default: {                                                /* over an edge: till its back is off too */
        cd_move(o, lval(o, TE_VX), 0);
        int w = OB8(o, OBJ_width_px);
        uint16_t x = (uint16_t)(OB16(o, OBJ_x_pos) + (lval(o, TE_VX) < 0 ? w : -w));
        if (floor_at(o, x) >= 0x10) SET8(o, OBJ_routine, 2);  /* (then down: settling) */
        break;
    }
    }
    o_animate(o, o_asset_anim(asset));
    badnik_end(o);
}

/* ---- $38 Metal Sonic's projector -------------------------------------------- */
#define PJ_COUNT 0x30                  /* oVar2A */
#define PJ_LOCS  0x32                  /* oVar2C */
#define AN_PARENT 0x44                 /* oVar3E: (a hologram) the projector */
#define PJ_GONE  0x45                  /* oVar3F */
static int holo_parent_gone(uint32_t o)
{
    uint32_t pj = 0xFF0000u | OB16(o, AN_PARENT);
    return r32(pj) != CD_CODE(0x38) || OB8(pj, PJ_GONE);
}

static int spawn_holo(uint32_t o, int id, int dx, int dy, uint8_t sub)
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, CD_CODE(id));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, AN_PARENT, (uint16_t)o);
    return 1;
}

static void projector(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {                                /* Metal Sonic's hologram */
        if (holo_parent_gone(o)) { o_delete(o); return; }
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            int small = OB8(o, OBJ_subtype) & 0x80;
            o_init(o, SCDA_PROJECTOR3, OB8(o, OBJ_render) | 4, 4, small ? 8 : 0x14, small ? 4 : 0x18);
            SET8(o, OBJ_x_radius, small ? 8 : 0x14);
            SET8(o, OBJ_y_radius, small ? 4 : 0x18);
            SET8(o, OBJ_anim, small ? 0 : 1);
        }
        o_animate(o, o_asset_anim(SCDA_PROJECTOR3));
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (scd_proj_destroyed() || !o_asset_maps(SCDA_PROJECTOR3)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_PROJECTOR3, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_x_radius, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET32(o, PJ_LOCS, o_asset_anim(SCDA_PROJBOOM));
        if (!spawn_holo(o, 0x38, -0x15, -7, 0xFF) || !spawn_holo(o, 0x38, -0x58, -4, 1) ||
            !spawn_holo(o, 0x39, -0x58, -0x18, 0x80) || !spawn_holo(o, 0x39, -0x62, 0, 0x81)) {
            o_delete(o);                                      /* (its show not made: gone) */
            return;
        }
        /* fall through */
    case 2: {
        int hit = o_s1_touch(o, 0x12, 0x11), broken = 0;      /* (CD's touch size $3B, rolling) */
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), P_ANIM) == 2) broken = 1;
        if (broken) {
            SET8(o, OBJ_coll_flags, 0);
            SET8(o, OBJ_routine, 4);
        }
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        break;
    }
    case 4:                                                   /* broken: the show ends */
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_frame, 1);
        SET8(o, PJ_GONE, 0xFF);
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        cd_get_off(o);
        /* fall through */
    case 6: {                                                 /* exploding */
        uint32_t a6 = r32(o + PJ_LOCS);
        uint8_t when = tri_rd8(a6);
        if (when & 0x80) { SET8(o, OBJ_routine, 8); SET16(o, PJ_COUNT, 60); break; }
        SET8(o, PJ_COUNT, OB8(o, PJ_COUNT) + 1);
        if (OB8(o, PJ_COUNT) != when) break;
        int8_t dx = (int8_t)tri_rd8(a6 + 1), dy = (int8_t)tri_rd8(a6 + 2);
        SET32(o, PJ_LOCS, a6 + 3);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);                             /* (no animal) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
        o_sfx_cd(FM_EXPLODE);                                 /* (each blast; no jingle) */
        break;
    }
    default:                                                  /* gone */
        SET16(o, PJ_COUNT, OB16(o, PJ_COUNT) - 1);
        if (OB16(o, PJ_COUNT)) break;
        scd_proj_destroyed_set();
        o_delete(o);
        return;
    }
    if (OB8(o, OBJ_routine) <= 2 && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $39 animals ---------------------------------------------------------------- */
#define AN_X0    0x30                  /* oVar2A */
#define AN_Y0    0x32                  /* oVar2C (flying) */
#define AN_VX    0x32                  /* oVar2C (on the ground, 16.16) */
#define AN_ANG   0x34                  /* oVar2E (flying) */
#define AN_STEP  0x35                  /* oVar2F */
#define AN_VY    0x36                  /* oVar30 (on the ground, 16.16) */
static void animal(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    int holo = sub & 0x80, flying = !(sub & 0x7F);
    if (!holo) {                                              /* CheckAnimalPrescence */
        if (cd_period() == TRI_CD_PAST || cd_period() == TRI_CD_PRESENT) {
            if (!scd_proj_destroyed()) { cd_despawn(o, OB16(o, OBJ_x_pos)); return; }
        } else if (!scd_good_future()) { o_delete(o); return; }
    }
    int asset = flying ? (holo ? SCDA_HOLOFLY : SCDA_ANIMALFLY) : (holo ? SCDA_HOLOGROUND : SCDA_ANIMALGROUND);
    if (!OB8(o, OBJ_routine)) {
        if (!o_asset_maps(asset)) { o_delete(o); return; }
        if (flying) {
            SET8(o, OBJ_routine, holo ? 4 : 2);
            o_init(o, asset, 4, holo ? 3 : 1, 8, 8);
            SET8(o, OBJ_x_radius, 8);
            SET8(o, OBJ_y_radius, 8);
            SET16(o, AN_X0, OB16(o, OBJ_x_pos));
            SET16(o, AN_Y0, OB16(o, OBJ_y_pos));
            turn(o);
            if (holo) SET8(o, OBJ_anim, 1);
            else {
                SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
                SET8(o, AN_ANG, 1);
                SET8(o, AN_STEP, 1);
            }
        } else {
            SET8(o, OBJ_routine, holo ? 8 : 2);
            o_init(o, asset, OB8(o, OBJ_render) | 4, 4, 8, 0x0C);
            SET8(o, OBJ_x_radius, 8);
            SET8(o, OBJ_y_radius, 0x0C);
            if (holo) turn(o);
            else {
                SET32(o, AN_VX, 0x10000);
                SET32(o, AN_VY, (uint32_t)-0x40000);
            }
        }
        return;                                               /* (the first frame: unseen) */
    }
    int16_t s, c;
    if (flying) {
        if (holo) {                                           /* (the projector's: its show) */
            if (holo_parent_gone(o)) { o_delete(o); return; }
            o_sincos(OB8(o, AN_ANG), &s, &c);
            SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> 3));
            SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> 4));
            SET8(o, AN_ANG, OB8(o, AN_ANG) + 4);
            if (!(OB8(o, AN_ANG) & 0x7F)) { turn(o); return; }
            o_animate(o, o_asset_anim(asset));
            o_draw(o);
            return;
        }
        o_sincos(OB8(o, AN_ANG), &s, &c);
        SET16(o, OBJ_x_pos, OB16(o, AN_X0) + (c >> 1));
        SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + (s >> 1));
        uint8_t d0 = (uint8_t)(OB8(o, AN_ANG) + OB8(o, AN_STEP));
        if ((uint8_t)(d0 - 1) >= 0x7F) {                      /* (round the loop: turn back) */
            d0 = OB8(o, AN_ANG);
            SET8(o, AN_STEP, (uint8_t)-(int8_t)OB8(o, AN_STEP));
            turn(o);
        }
        SET8(o, AN_ANG, d0);
        o_animate(o, o_asset_anim(asset));
        if (cd_despawn(o, OB16(o, AN_X0))) return;
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 2: case 4: {                                         /* hopping */
        cd_move(o, lval(o, AN_VX), lval(o, AN_VY));
        SET32(o, AN_VY, r32(o + AN_VY) + 0x2000);
        SET8(o, OBJ_frame, lval(o, AN_VY) < 0 ? 0 : 1);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);    /* (every other landing: it turns) */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET32(o, AN_VY, (uint32_t)-0x40000);
        }
        break;
    }
    case 6:                                                   /* (turns, the frame after) */
        SET8(o, OBJ_routine, 2);
        SET32(o, AN_VX, (uint32_t)-lval(o, AN_VX));
        turn(o);
        break;
    default:                                                  /* 8: a hologram */
        if (holo_parent_gone(o)) { o_delete(o); return; }
        o_animate(o, o_asset_anim(asset));
        o_draw(o);
        return;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

void scd_ccz_badnik_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x24] = kama_kama;
    t[base + 0x25] = scythe;
    t[base + 0x2D] = ga;
    t[base + 0x2E] = tentou;
    t[base + 0x38] = projector;
    t[base + 0x39] = animal;
}
