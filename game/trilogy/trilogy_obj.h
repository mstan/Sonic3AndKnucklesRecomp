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
#define NATIVE_CD_BASE  0x7E2000u      /* + slot*4 (scd_obj.c: common ids, then per round) */
#define NATIVE_CD_SLOTS 0x800    /* (acts 1, 2 from $20; act 3 from $400; scd_level.py) */
enum {                                 /* Sonic CD asset ids (scd_level.py CD_ASSETS) */
    SCDA_BOOSTER, SCDA_PLANT, SCDA_MOSQUI, SCDA_MOSQUI2, SCDA_PATABATA, SCDA_PATABATA2,
    SCDA_ANTON, SCDA_TAMABBOH, SCDA_TAMABBOH2, SCDA_TAMAMISSILE, SCDA_FLOATBLOCK, SCDA_SPIKES,
    SCDA_PLATFORM, SCDA_LEDGE, SCDA_LEDGE2, SCDA_LEDGEBITS, SCDA_LEDGEBITS2, SCDA_TUNNELPATHS,
    SCDA_TUNNELDOOR, SCDA_DOORSPLASH, SCDA_DOORV, SCDA_SONICHOLE, SCDA_SPINDISC, SCDA_BOULDER,
    SCDA_LOGSHADOW, SCDA_WALL, SCDA_TIMEPOST, SCDA_GENERATOR, SCDA_GENBOOM, SCDA_GOALPOST,
    SCDA_ANIMALFLY, SCDA_ANIMALGROUND, SCDA_HOLOFLY, SCDA_HOLOGROUND, SCDA_PROJECTOR, SCDA_PROJBOOM,
    SCDA_AMY, SCDA_AMYPAL, SCDA_HEART, SCDA_EGGMAN, SCDA_EGGBODY, SCDA_EGGLEG1, SCDA_EGGLEG2,
    SCDA_EGGLEG3, SCDA_EGGARM1, SCDA_EGGARM2, SCDA_EGGARM3, SCDA_EGGARM3B, SCDA_EGGARM4, SCDA_BOSSPAL,
    SCDA_GENEPAL, SCDA_CAPSULE, SCDA_FLOWER, SCDA_FLOWER2, SCDA_SCENERY, SCDA_TAGATAGA, SCDA_TAGATAGA2,
    SCDA_SPRINGBOARD, SCDA_SWING, SCDA_KOMA, SCDA_SWITCH, SCDA_DOOR, SCDA_PISTON, SCDA_TUBEDOOR,
    SCDA_WWZ_PLATFORM, SCDA_WWZ_BOUNCE, SCDA_WWZ_SPIN, SCDA_WWZ_SEESAW,
    SCDA_LAUNCHER = 72, SCDA_FREEZER, SCDA_ICEBLOCK, SCDA_BEAMS, SCDA_SNAKEBLOCK, SCDA_EGGSTATUE, SCDA_SPIKEBOMB,
    SCDA_CRUSHER, SCDA_SPIKEBALL, SCDA_SPIKEBALL2,
    SCDA_CCZ3_LID = 336, SCDA_CCZ3_BODY, SCDA_CCZ3_GEAR, SCDA_CCZ3_EGGMAN, SCDA_CCZ3_EGGMAN2, SCDA_CCZ3_CANNON,
    SCDA_CCZ3_BOMB, SCDA_CCZ3_GATE, SCDA_CCZ3_SPINNER, SCDA_CCZ3_STOPPER, SCDA_CCZ3_LIGHT, SCDA_CCZ3_BOSSPAL,
    SCDA_CCZ3_FLASHPAL,
    SCDA_T3POD = 368, SCDA_T3PODHEAD, SCDA_T3EGGMAN, SCDA_T3MECA, SCDA_T3SHOT, SCDA_T3BUBBLE,
    SCDA_Q3EGGMAN = 400, SCDA_Q3MECA1, SCDA_Q3MECA2, SCDA_Q3MECA3, SCDA_Q3PIPE, SCDA_Q3CATCH, SCDA_Q3BOMB,
    SCDA_Q3HIBANA, SCDA_Q3HARI, SCDA_Q3BAKUHA, SCDA_Q3BOSSPAL,
    SCDA_S7_EGGMAN = 432, SCDA_S7_JET, SCDA_S7_BEAM, SCDA_S7_BOOM, SCDA_S7_MSNC, SCDA_S7_ELE, SCDA_S7_BARA,
    SCDA_S7_WALL, SCDA_S7_SPIKE, SCDA_S7_AMY, SCDA_S7_HEART, SCDA_S7_LAMP, SCDA_S7_HAHEN, SCDA_S7_AMYPAL,
    SCDA_S7_BOSSPAL,
    SCDA_M3EGG = 464, SCDA_M3MECA, SCDA_M3BLADE /* 466-471: the blades' six mapping sets */,
    SCDA_M3SPARK = 472, SCDA_M3AMY, SCDA_M3BOSSPAL, SCDA_M3ENDPAL, SCDA_M3BOSSCYC, SCDA_M3FIREFLY, SCDA_M3ROOMPAL,
    SCDA_M3CURTAIN, SCDA_M3PANEL, SCDA_M3JET, SCDA_M3BLOCK, SCDA_M3SPIKES,
    SCDA_W3EGGMAN = 304, SCDA_W3EGGMAN2, SCDA_W3BOSSPAL, SCDA_W3MECA0, SCDA_W3MECA0B, SCDA_W3ROTOR,
    SCDA_W3FLAME, SCDA_W3SPIKE, SCDA_W3RUBBLE, SCDA_W3RUBBLE2, SCDA_W3FLOOR, SCDA_W3DOOR, SCDA_W3BEAMER,
    SCDA_W3BOOM, SCDA_W3BEAM, SCDA_W3ENKEI,
    SCDA_BUMPER = 96, SCDA_FLIPPER, SCDA_BUMPERBREAK, SCDA_BARBUMPER, SCDA_POCKET, SCDA_POCKETLID, SCDA_DRUM,
    SCDA_CCZSPIKES, SCDA_SWINGBLOCK, SCDA_TRAPBLOCK, SCDA_CCZPLATFORM, SCDA_GATELEDGE, SCDA_TURRET,
    SCDA_SPIKEPIVOT, SCDA_SPIKEBALLS, SCDA_KAMAKAMA, SCDA_KAMAKAMA2, SCDA_SCYTHE, SCDA_GA, SCDA_GA2,
    SCDA_GAPOLLEN, SCDA_TENTOU, SCDA_TENTOU2, SCDA_TENTOUBOMB, SCDA_AMY3, SCDA_AMYPAL3, SCDA_HEART3,
    SCDA_METALSONIC, SCDA_BARRIER, SCDA_PROJECTOR3 = 126,
    SCDA_TTZ_WAVE = 128, SCDA_TTZ_POLE, SCDA_TTZ_SPIKES, SCDA_TTZ_SWITCH, SCDA_TTZ_DOOR,
    SCDA_TONBO, SCDA_TONBO2, SCDA_AMENBO, SCDA_AMENBO2, SCDA_TTZ_TAGATAGA, SCDA_TTZ_TAGATAGA2, SCDA_TTZ_TAGASPIKE,
    SCDA_YAGO, SCDA_YAGO2, SCDA_TTZ_ANIMAL, SCDA_TTZ_ANIMAL2, SCDA_TTZ_ELEVATOR, SCDA_TTZ_ELEVBUTTON,
    SCDA_TTZ_CRUSHER, SCDA_TTZ_WALLS, SCDA_TTZ_SCREW, SCDA_TTZ_SCREW2, SCDA_TTZ_PIVOT, SCDA_TTZ_LINK, SCDA_TTZ_BALL,
    SCDA_TTZ_BALL1, SCDA_TTZ_BALL4, SCDA_TTZ_PRIZE, SCDA_TTZ_NOZZLE, SCDA_TTZ_JET, SCDA_TTZ_CURRENTS,
    SCDA_TTZ_UD1 = 160, SCDA_TTZ_UD3, SCDA_TTZ_UD4, SCDA_TTZ_UD5, SCDA_TTZ_UD8, SCDA_TTZ_UD9, SCDA_TTZ_UDA,
    SCDA_TTZ_LR1, SCDA_TTZ_LR2, SCDA_TTZ_LR3, SCDA_TTZ_LR5, SCDA_TTZ_LR6, SCDA_TTZ_RBLK, SCDA_TTZ_RENKETU,
    SCDA_TTZ_RENKETU2, SCDA_TTZ_ESCAL, SCDA_TTZ_BRUNKO, SCDA_TTZ_BRUNKO2, SCDA_TTZ_FUNSUI, SCDA_TTZ_UKIDAI,
    SCDA_TTZ_BANEIWA, SCDA_TTZ_DAID, SCDA_TTZ_WALL1, SCDA_TTZ_WALL42, SCDA_TTZ_WALL4, SCDA_TTZ_KUZURE,
    SCDA_TTZ_TOBIRAS,
    SCDA_QQZ_DENDEN = 192, SCDA_QQZ_DENDEN2,
    SCDA_QQZ_SASORI = 195, SCDA_QQZ_SASORI2, SCDA_QQZ_SASORITAIL, SCDA_QQZ_SASORISHOT, SCDA_QQZ_KEMUSI, SCDA_QQZ_KUMO,
    SCDA_QQZ_KUMO2, SCDA_QQZ_KUMOWEB, SCDA_QQZ_ROCK, SCDA_QQZ_STALACTITE, SCDA_QQZ_SPIKES, SCDA_QQZ_ROCKRING,
    SCDA_QQZ_LEDGE, SCDA_QQZ_WAVEROCK, SCDA_QQZ_SWING, SCDA_QQZ_SWINGTOP, SCDA_QQZ_PILLAR, SCDA_QQZ_BRIDGE,
    SCDA_QQZ_PLATFORM, SCDA_QQZ_LEVER,
    SCDA_SSZ_KICKER = 224, SCDA_SSZ_CAGE, SCDA_SSZ_SPRING_UP, SCDA_SSZ_SPRING_SIDE, SCDA_SSZ_SPRING_DIAG,
    SCDA_SSZ_SPARKS, SCDA_SSZ_PANEL, SCDA_SSZ_SWING, SCDA_KANABUN, SCDA_KANABUN2, SCDA_KABASIRA, SCDA_HOTARU,
    SCDA_HOTARU2, SCDA_HOTARU_SPARK, SCDA_SSZ_WALL, SCDA_SSZ_IRONBALL, SCDA_SSZ_LAUNCHER, SCDA_SSZ_LAUNCHBALL,
    SCDA_SSZ_DANGO, SCDA_SSZ_DANGO2, SCDA_SEARCHLIGHT, SCDA_SEARCHLIGHT2,
    SCDA_MMZ_SWITCH = 256, SCDA_MMZ_PIPE, SCDA_MMZ_BEAM, SCDA_MMZ_BEAM2, SCDA_MMZ_KAITEN, SCDA_MMZ_ANIMALFLY,
    SCDA_MMZ_ANIMALGROUND,
    SCDA_KABUTO = 266, SCDA_KABUTOHORN, SCDA_HACHI, SCDA_HACHI2, SCDA_SCARAB, SCDA_SCARAB2, SCDA_DANGO,
    SCDA_DANGO2, SCDA_BIGBOM, SCDA_BIGBOMFUSE, SCDA_BIGBOMBOOM, SCDA_BIGBOMFIRE,
    SCDA_MMZ_KOMA = 280, SCDA_MMZ_KUZURE, SCDA_MMZ_DAI, SCDA_MMZ_PISTON,
    SCDA_MMZ_SPIKES = 288, SCDA_MMZ_LAYERBLOCK, SCDA_TOGEITA, SCDA_FUTAGOR, SCDA_TOBIRA1, SCDA_TOBIRA2,
    SCDA_LEGMECA = 296, SCDA_NOKOGIRI, SCDA_NOKOBAR, SCDA_PROPERA, SCDA_PROPROTOR, SCDA_KONBEA, SCDA_OKUBALL,
    SCDA_OKUSIESO,
    SCDA_BATAPYON = 84, SCDA_BATAPYON2, SCDA_POHBEE, SCDA_POHBEE2, SCDA_POHBEEMISSILE, SCDA_SEMI,
    SCDA_SEMI2, SCDA_SEMIBOMB, SCDA_MINOMUSI, SCDA_MINOMUSI2,
};
void     cd_time_post(uint32_t o);             /* scd_time.c */
void     cd_time_icon(uint32_t o);
void     cd_get_off(uint32_t o);               /* GetOffObject (both players) */
int      cd_stood_on(uint32_t o);
void     cd_spikes(uint32_t o);                /* (each round's own id) */
/* Sonic CD objects (scd_obj.c) */
uint8_t  tri_scd_coll(uint8_t type);           /* a CD collision type as S3K collision_flags */
void     tri_scd_voice(int id);                /* a PCM voice / sound of the round's bank */
int      cd_period(void);                      /* TRI_CD_PRESENT .. TRI_CD_BAD */
int      cd_destroy_good_future(uint32_t o);   /* DestroyOnGoodFuture: 1 = it exploded (stop) */
int      cd_despawn(uint32_t o, uint16_t x);   /* CheckObjDespawn2: 1 = deleted (stop) */
void     cd_move(uint32_t o, int32_t vx, int32_t vy);   /* 16.16 steps on x, y */

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
void     o_sfx_cd(int fm_id);        /* a Sonic CD FM sound (FM_*, $90-$DF) in the CD sound set */
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
