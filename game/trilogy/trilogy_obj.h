/*
 * trilogy_obj.h — native object API for Sonic 1 / Sonic 2 objects running in
 * the S3K object system.
 *
 * Object logic is transcribed from the original games; everything engine-
 * facing (drawing, collision response, solidity for BOTH players, despawn,
 * animation) goes through the retail S3K routines so S3 characters interact
 * with these objects exactly as with native S3K ones.
 */
#pragma once
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "trilogy_assets.h"

#define NATIVE_S1_BASE  0x7E0000u      /* + id*4 */
#define NATIVE_S2_BASE  0x7E1000u

typedef void (*TriObjFn)(uint32_t o);

/* field access on an object at 68K RAM address o */
#define OB8(o, f)       r8((o) + (f))
#define OB16(o, f)      r16((o) + (f))
#define OBS16(o, f)     ((int16_t)r16((o) + (f)))
#define SET8(o, f, v)   w8((o) + (f), (uint8_t)(v))
#define SET16(o, f, v)  w16((o) + (f), (uint16_t)(v))
#define SET32(o, f, v)  w32((o) + (f), (uint32_t)(v))

/* S3K status bits */
#define ST_XFLIP        0x01
#define ST_YFLIP        0x02
#define ST_P1_STAND     0x08
#define ST_P2_STAND     0x10
#define ST_P1_PUSH      0x20
#define ST_P2_PUSH      0x40

/* engine services (all take the object explicitly) */
void     o_draw(uint32_t o);
void     o_delete(uint32_t o);
void     o_remember(uint32_t o);                /* Sprite_OnScreen_Test */
int      o_out_of_range(uint32_t o, uint16_t x); /* S1 out_of_range: 1 = out */
void     o_delete_respawnable(uint32_t o);      /* clear respawn bit + delete */
void     o_touch(uint32_t o);                   /* add to collision response list */
void     o_move(uint32_t o);                    /* MoveSprite2 (SpeedToPos) */
void     o_fall(uint32_t o);                    /* MoveSprite  (ObjectFall) */
int16_t  o_floor_dist(uint32_t o);              /* ObjCheckFloorDist -> d1 */
void     o_animate(uint32_t o, uint32_t anim);  /* Animate_Sprite */
void     o_animate_s2(uint32_t o, uint32_t anim);  /* S2 AnimateSprite (flips follow status) */
uint32_t o_alloc(void);                         /* AllocateObject (0 = none) */
uint32_t o_alloc_after(uint32_t o);             /* AllocateObjectAfterCurrent */
int16_t  o_solid(uint32_t o, int d1, int d2, int d3, int d4);  /* SolidObjectFull, returns d4 */
int16_t  o_solid_1p(uint32_t o, int n, int d1, int d2, int d3, int d4, int always);
uint8_t  o_atan(int16_t x, int16_t y);          /* GetArcTan (S2 CalcAngle) */
extern uint32_t s2_orient_player;               /* the player s2_orient picked (S2 leaves it in a1) */
void     o_platform(uint32_t o, int d1, int d3, int d4);       /* SolidObjectTop */
void     o_platform_1p(uint32_t o, int n, int d1, int d3, int d4);  /* SolidObjectTop_1P */
void     o_platform_sloped(uint32_t o, int d1, int d3, int d4, uint32_t slope);
extern uint32_t g_o_d6;                         /* d6 after the last solid call */
void     o_spike_hurt(uint32_t o, uint32_t player); /* S3K spike hurt (sub_24280) */
/* Sonic 1 synchronised animation (SynchroAnimate) */
extern uint8_t s1_ani0_frame, s1_ani1_frame, s1_ani2_frame;
void     s1_synchro_animate(void);
void     o_sfx(int id);
void     o_sfx_s2(int sndid);        /* a Sonic 2 sound (SndID_*) in the S2 sound set */
int      o_random(void);                        /* Random_Number */
void     o_sincos(uint8_t angle, int16_t *s, int16_t *c);
uint32_t o_player(int n);                       /* Player_1 / Player_2 */
int      o_player_active(uint32_t p);

/* assets of the current zone (generated ids) */
void     o_asset(uint32_t o, int asset);        /* mappings + art_tile */
uint32_t o_asset_anim(int asset);
uint16_t o_asset_tile(int asset);
uint32_t o_asset_maps(int asset);
int      o_asset_ready(int asset);             /* deferred art in VRAM yet? */
void     o_upload_asset(int asset);            /* deferred (boss) art -> VRAM now */

/* ---- shared S1 helpers ------------------------------------------------------ */
static inline int o_standing(uint32_t o) { return (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) != 0; }
static inline uint8_t o_osc(int off) { return r8(RAM_Oscillating_table + (uint32_t)off); }
static inline int o_frame_counter(void) { return r8(RAM_Level_frame_counter + 1); }
int      o_visible(uint32_t o);                 /* ChkObjectVisible: 1 = on screen */
int      o_partly_visible(uint32_t o);          /* ChkPartiallyVisible: 1 = on screen */
int16_t  o_ceiling_dist(uint32_t o);            /* ObjHitCeiling -> d1 */
int16_t  o_wall_right(uint32_t o, int d3);      /* ObjHitWallRight -> d1 */
int16_t  o_wall_left(uint32_t o, int d3);       /* ObjHitWallLeft -> d1 */
int16_t  o_floor_block(uint32_t o, uint16_t *blk); /* ObjFloorDist -> d1, + block word (a1) */
int16_t  o_solid_sloped(uint32_t o, int d1, int d2, int d4, uint32_t hmap); /* SolidObject2F + slope ride */
void     o_smash(uint32_t o, int count, const int16_t *spd, int gravity, int routine);
void     o_score(uint32_t o, int frame);        /* points popup (S3K Obj_EnemyScore) */
void     o_hurt_player(uint32_t p, uint32_t o); /* Touch_ChkHurt (shields, invincibility) */
void     o_kill_player(uint32_t p);             /* KillSonic */
int      o_s1_touch(uint32_t o, int w, int h);  /* S1 ReactToItem box test: bit per player */
uint32_t o_slot(int index);                     /* object RAM slot -> address */
int      o_slot_index(uint32_t o);

/* common init (S1 Main routines) */
static inline void o_init(uint32_t o, int asset, int render, int prio, int width, int height)
{
    o_asset(o, asset);
    SET8(o, OBJ_render, render);
    SET16(o, OBJ_priority, (uint16_t)(prio << 7));
    SET8(o, OBJ_width_px, width);
    SET8(o, OBJ_height_px, height);
}

void s1_objects_register(TriObjFn *table);      /* s1_obj_*.c */
void s1_obj_ghz_register(TriObjFn *table);
void s1_obj_ghz2_register(TriObjFn *table);
void s1_obj_flow_register(TriObjFn *table);
void s1_obj_boss_register(TriObjFn *table);
void s1_obj_mz_register(TriObjFn *table);
void s1_obj_syz_register(TriObjFn *table);
void s1_obj_syz_reset(void);
void s1_obj_lz_register(TriObjFn *table);
void s1_obj_slz_register(TriObjFn *table);
void s1_obj_sbz_register(TriObjFn *table);
void s1_obj_fz_register(TriObjFn *table);
void s2_objects_register(TriObjFn *table);      /* s2_obj_*.c */
int  s2_orient(uint32_t o, int *d0, int *d1, int16_t *dx, int16_t *dy); /* Obj_GetOrientationToPlayer */
#define S2_PROJ_KIND 0x30                       /* Obj98: S2 objoff_2A held its code pointer */
enum { S2_PROJ_COCONUT, S2_PROJ_SPINY, S2_PROJ_MOVE, S2_PROJ_NEBULA, S2_PROJ_TURTLOID, S2_PROJ_ANIM };   /* ANIM: asset at +1 */
int  s1_animals_alive(void);                    /* capsule / prison helpers (s1_obj_boss.c) */
void s1_spawn_animal(uint32_t o, int dx, int delay);
void s1_boss_defeated(uint32_t o);
void tri_lz_surfaces(void);
extern uint8_t tri_lz_wtunnel_allow;
