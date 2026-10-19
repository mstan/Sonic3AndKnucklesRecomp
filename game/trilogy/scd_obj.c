/*
 * scd_obj.c — Sonic CD's objects (decomp: C:\Users\hyper\scd_ref\decomp\src).
 *
 * Native slots (tools/trilogy/scd_level.py writes them into each level's
 * object pointer table): Sonic CD's common ids ($01-$1F) at their own id, a
 * round's own (ids from $20, and some below in some rounds; each act's object
 * index names its own) at SCD_ROUND_SLOT(round index, id).
 */
#include "trilogy_obj.h"

#define SCD_ROUND_SLOT(r, id)  (0x20 + (r) * 0x80 + (id))
#define SCD_ACT3_SLOT(r, id)   (0x400 + (r) * 0x80 + (id))   /* (act 3: its own ids) */
#define PLC_EndSignStuff       0x085BC8u

/* ---- shared ------------------------------------------------------------- */
int cd_period(void) { return tri_cd_zone(tri_zone()) ? (tri_zone() - TRI_CD_BASE) & 3 : TRI_CD_PRESENT; }

/* goodFuture: in a good future, and wherever the act's robot generator is
 * destroyed (scd_time.c) */
int scd_good_future(void);
#define good_future scd_good_future

/* DestroyOnGoodFuture: robots explode where the future is good, except the
 * present's own (subtype 0), with FM_EXPLODE (not a destroyed badnik's $96:
 * so S3K's explosion skips its own sound, routine 6, and its animal is made
 * here as its routine 0 would) */
int cd_destroy_good_future(uint32_t o)
{
    if (!good_future() || (cd_period() == TRI_CD_PRESENT && !OB8(o, OBJ_subtype))) return 0;
    if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return 1; }   /* (unseen: no sound, as Sonic CD) */
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    uint8_t render = OB8(o, OBJ_render);
    for (unsigned k = 4; k < OBJ_SIZE; k++) w8(o + k, 0);
    SET32(o, OBJ_code, S3K_Obj_Explosion);
    SET8(o, OBJ_routine, 6);
    SET16(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_pos, y);
    SET8(o, OBJ_render, render & 0x80);
    uint32_t a1 = o_alloc();                                  /* (Obj_Explosion's loc_1E5F6) */
    if (a1) {
        SET32(a1, OBJ_code, S3K_Obj_Animal);
        SET16(a1, OBJ_x_pos, x);
        SET16(a1, OBJ_y_pos, y);
    }
    o_sfx_cd(0x9E);                                           /* (FM_EXPLODE) */
    return 1;
}

/* CheckObjDespawn2: off screen and its place (x) out of range */
int cd_despawn(uint32_t o, uint16_t x)
{
    if ((OB8(o, OBJ_render) & 0x80) || !o_out_of_range(o, x)) return 0;
    o_delete_respawnable(o);
    return 1;
}

/* GetOffObject: whoever stands on o is in the air; one charging (S3K's spin
 * dash: spin_dash_flag) stops his charge sound (FM $AB, charge stop) */
void cd_get_off(uint32_t o)
{
    for (int i = 0; i < 2; i++) {
        uint8_t bit = i ? ST_P2_STAND : ST_P1_STAND;
        if (!(OB8(o, OBJ_status) & bit)) continue;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        uint32_t p = o_player(i);
        if (OB8(p, 0x3D) & 1) o_sfx_cd(0xAB);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
    }
}

int cd_stood_on(uint32_t o) { return (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) != 0; }

void cd_move(uint32_t o, int32_t vx, int32_t vy)
{
    SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + (uint32_t)vx);
    SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)vy);
}

/* ---- $17 signpost (acts 1 and 2) ---------------------------------------
 * Sonic CD's spins when Sonic passes it, then the results. Here S3K's own end
 * sign drops on that spot as he passes (its results, its after-act flow);
 * the camera holds it in view. */
static void cd_signpost(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    int x = OBS16(o, OBJ_x_pos);
    if (OBS16(p, OBJ_x_pos) < x) return;              /* (waits, unseen) */
    uint32_t s = o_alloc();
    if (!s) return;
    w32(s, S3K_Obj_EndSign);
    SET16(s, OBJ_x_pos, (uint16_t)x);
    uint16_t lock = (uint16_t)(x - 160 > 0 ? x - 160 : 0);
    if ((int16_t)r16(RAM_Camera_min_X_pos) < (int16_t)lock) w16(RAM_Camera_min_X_pos, lock);
    w16(RAM_Camera_max_X_pos, lock);
    A(1) = PLC_EndSignStuff;
    tri_jsr(S3K_Load_PLC_Raw);
    o_delete(o);
}

/* timeZone: 0 past, 1 present, 2 either future */
static int time_zone(void)
{
    int p = cd_period();
    return p == TRI_CD_PAST ? 0 : p == TRI_CD_PRESENT ? 1 : 2;
}

/* ---- $12 floating block: solid in the past; in the present it shows only
 * while stood on; in the future it falls once stood on ----------------- */
#define stood_on cd_stood_on
static void block_top(uint32_t o) { o_platform(o, 0x0C + 10, 0x0C, OB16(o, OBJ_x_pos)); }

static void float_block(uint32_t o)
{
    uint32_t anim = o_asset_anim(SCDA_FLOATBLOCK);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_FLOATBLOCK, OB8(o, OBJ_render) | 4, 1, 0x0C, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        SET8(o, OBJ_frame, 5);
        /* fall through */
    case 2:
        block_top(o);
        if (time_zone() == 0) break;
        if (time_zone() == 2) {
            if (!stood_on(o)) break;
            SET8(o, OBJ_routine, 4);                          /* (falls from now) */
        } else {
            SET8(o, OBJ_frame, 0);
            if (stood_on(o)) { SET8(o, OBJ_routine, 6); SET8(o, OBJ_anim, 1); }
            break;
        }
        /* fall through */
    case 4:
        block_top(o);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 2);
        if ((uint16_t)(r16(RAM_Camera_Y_pos) + 0xE0) < OB16(o, OBJ_y_pos)) { o_delete(o); return; }
        break;
    case 6:                                                   /* appearing (script: $FC to 8) */
        block_top(o);
        if (!stood_on(o)) { SET8(o, OBJ_routine, 2); return; }
        o_animate(o, anim);
        break;
    case 8:                                                   /* shown */
        SET8(o, OBJ_anim, 0);
        block_top(o);
        if (!stood_on(o)) { SET8(o, OBJ_routine, 10); SET8(o, OBJ_anim, 2); return; }
        o_animate(o, anim);
        break;
    case 10:                                                  /* vanishing (script: $FC to 12) */
        block_top(o);
        o_animate(o, anim);
        break;
    default:
        SET8(o, OBJ_routine, 2);
        return;
    }
    o_draw(o);
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete(o);
}

/* ---- spikes (each round's own id): stood on, they hurt; hanging, touched -- */
void cd_spikes(uint32_t o)
{
    int hang = OB8(o, OBJ_render) & 2;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SPIKES, OB8(o, OBJ_render) | 4, 3, hang ? 0x12 : 0x10, 8);
        SET8(o, OBJ_y_radius, 8);
        if (hang) SET8(o, OBJ_coll_flags, tri_scd_coll(0x83));
    }
    o_solid(o, OB8(o, OBJ_width_px) + 10, 8, 9, OB16(o, OBJ_x_pos));
    if (hang)
        o_touch(o);
    else {
        uint8_t st = OB8(o, OBJ_status);
        if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
        if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $1B boulder: solid ---------------------------------------------------- */
static void boulder(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_BOULDER, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
    }
    if (OB8(o, OBJ_render) & 0x80) o_solid(o, 0x10 + 10, 0x10, 0x11, OB16(o, OBJ_x_pos));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $16 goal post: passing it holds the camera's left edge (no way back)
 * and ends any time warp ----------------------------------------------- */
void scd_time_stop(void);
static void goal_post(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_GOALPOST, OB8(o, OBJ_render) | 4, 4, 0x10, 0x20);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 0x20);
        SET8(o, OBJ_frame, 5);
    } else if (OB8(o, OBJ_routine) == 2) {
        int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 128);
        if (dy >= 0 && dy < 256 && OB16(p, OBJ_x_pos) >= OB16(o, OBJ_x_pos)) {
            SET8(o, OBJ_routine, 4);
            w16(RAM_Camera_min_X_pos, r16(RAM_Camera_X_pos));
            scd_time_stop();
        }
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    if (tri_act() != 2) o_draw(o);
}

/* ---- $14 big ring: with 50 rings (while an emerald is left to win), S3K's
 * special stage ring; entering it, the tally, then the stage (Sonic CD's
 * UFOs by the SPECIAL STAGES option). Its flashes (subtype set) are S3K's. -- */
static void big_ring(uint32_t o)
{
    extern int tri_ss_available(void);
    if (OB8(o, OBJ_subtype)) { o_delete(o); return; }
    if (r16(RAM_Ring_count) >= 50 && tri_ss_available()) {
        SET32(o, OBJ_code, S3K_Obj_SSEntryRing);
        SET8(o, OBJ_routine, 0);
        return;
    }
    cd_despawn(o, OB16(o, OBJ_x_pos));
}

void scd_ppz_register(TriObjFn *t, int base);               /* scd_ppz.c */
void scd_ppz3_register(TriObjFn *t, int base3);             /* scd_ppz_boss.c */
void scd_wwz_register(TriObjFn *t, int base);               /* scd_wwz.c */
void scd_wwz_plat_register(TriObjFn *t, int base);          /* scd_wwz_plat.c */
void scd_wwz_badnik_register(TriObjFn *t, int base);        /* scd_wwz_badnik.c */
void scd_wwz_gim_register(TriObjFn *t, int base);           /* scd_wwz_gim.c */
void scd_mmz_register(TriObjFn *t, int base);               /* scd_mmz*.c */
void scd_mmz3_register(TriObjFn *t, int base3);             /* scd_mmz_boss.c: act 3, the final boss */
void scd_ssz_register(TriObjFn *t, int base);               /* scd_ssz*.c */
void scd_ssz_act3_register(TriObjFn *t, int base3);         /* (act 3: the Metal Sonic race, scd_ssz_race.c) */
void scd_qqz_register(TriObjFn *t, int base);               /* scd_qqz*.c */
void scd_qqz_gim3_register(TriObjFn *t, int base3);         /* (act 3's ledge, platforms) */
void scd_qqz3_register(TriObjFn *t, int base3);             /* scd_qqz_boss.c: act 3's press */
void scd_ccz_register(TriObjFn *t, int base);               /* scd_ccz*.c: Collision Chaos acts 1-2 */
void scd_ccz3_register(TriObjFn *t, int base3);             /* (act 3's bumpers, flippers, spikes, tags) */
void scd_ccz3_boss_register(TriObjFn *t, int base3);        /* scd_ccz_boss.c: act 3's pinball table boss */
void scd_ttz_register(TriObjFn *t, int base);               /* scd_ttz.c, _gim.c, _badnik.c: water, gimmicks */
void scd_ttz_blk_register(TriObjFn *t, int base);           /* scd_ttz_blk*.c: blocks and platforms */
void scd_ttz_blk3_register(TriObjFn *t, int base3);
void scd_ttz3_register(TriObjFn *t, int base3);             /* scd_ttz_boss.c: act 3's chase and boss */
void scd_wwz_act3_register(TriObjFn *t, int base3);         /* scd_wwz.c: act 3's tunnel path, tube door */
void scd_wwz3_register(TriObjFn *t, int base3);             /* scd_wwz_boss.c */

void scd_objects_register(TriObjFn *t)
{
    t[0x12] = float_block;
    t[0x14] = big_ring;
    t[0x16] = goal_post;
    t[0x17] = cd_signpost;
    t[0x1B] = boulder;
    t[0x3A0] = cd_time_post;                                /* (scd_level.py: object $80) */
    t[0x3A1] = cd_time_icon;
    scd_ppz_register(t, SCD_ROUND_SLOT(0, 0));
    scd_ppz3_register(t, SCD_ACT3_SLOT(0, 0));
    scd_wwz_register(t, SCD_ROUND_SLOT(4, 0));
    scd_wwz_plat_register(t, SCD_ROUND_SLOT(4, 0));
    scd_wwz_plat_register(t, SCD_ACT3_SLOT(4, 0));          /* (act 3's platforms, $22 / $28) */
    scd_wwz_badnik_register(t, SCD_ROUND_SLOT(4, 0));
    scd_wwz_gim_register(t, SCD_ROUND_SLOT(4, 0));
    scd_mmz_register(t, SCD_ROUND_SLOT(6, 0));
    scd_mmz3_register(t, SCD_ACT3_SLOT(6, 0));
    scd_ssz_register(t, SCD_ROUND_SLOT(5, 0));
    scd_ssz_act3_register(t, SCD_ACT3_SLOT(5, 0));
    scd_qqz_register(t, SCD_ROUND_SLOT(3, 0));
    scd_qqz_gim3_register(t, SCD_ACT3_SLOT(3, 0));
    scd_qqz3_register(t, SCD_ACT3_SLOT(3, 0));
    scd_ccz_register(t, SCD_ROUND_SLOT(1, 0));
    scd_ccz3_register(t, SCD_ACT3_SLOT(1, 0));
    scd_ccz3_boss_register(t, SCD_ACT3_SLOT(1, 0));
    scd_ttz_register(t, SCD_ROUND_SLOT(2, 0));
    scd_ttz_blk_register(t, SCD_ROUND_SLOT(2, 0));
    scd_ttz_blk3_register(t, SCD_ACT3_SLOT(2, 0));
    scd_ttz3_register(t, SCD_ACT3_SLOT(2, 0));
    scd_wwz_act3_register(t, SCD_ACT3_SLOT(4, 0));
    scd_wwz3_register(t, SCD_ACT3_SLOT(4, 0));
}
