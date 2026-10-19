/*
 * trilogy_obj.c — native object dispatch and engine-service wrappers.
 */
#include "trilogy_obj.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TriObjFn s_s1[256], s_s2[256], s_cd[NATIVE_CD_SLOTS];
static int s_registered;

static void obj_register(void)
{
    if (s_registered)
        return;
    s_registered = 1;
    s1_objects_register(s_s1);
    s2_objects_register(s_s2);
    extern void scd_objects_register(TriObjFn *table);
    scd_objects_register(s_cd);
}

/* Check every placement, including objects far outside a startup camera.
 * A stage pack must never quietly discard an unported layout object. */
int tri_obj_audit_bank(void)
{
    obj_register();
    unsigned missing=0, count=0;
    for(int z=TRI_ZONE_BASE;z<TRI_CD_END;z++) for(int a=0;a<4;a++) {
        if(tri_cd_zone(z) && (tri_bank_current()!=4 || !tri_scd_map((uint16_t)(z<<8|a))))continue;
        uint32_t e=tri_dir_for(z,a);if(!e)continue;
        uint32_t layout=tri_rd32(e+TD_OBJECTS),table=tri_rd32(e+TD_OBJPTRS);
        for(unsigned n=0;n<4096;n++,layout+=6) {
            if(tri_rd16(layout)==0xFFFF)break;
            uint32_t code=tri_rd32(table+(uint32_t)tri_rd8(layout+4)*4u);
            int supported=1;
            if(code>=NATIVE_S1_BASE && code<NATIVE_S1_BASE+0x400)
                supported=s_s1[(code-NATIVE_S1_BASE)/4]!=NULL;
            else if(code>=NATIVE_S2_BASE && code<NATIVE_S2_BASE+0x400)
                supported=s_s2[(code-NATIVE_S2_BASE)/4]!=NULL;
            else if(code>=NATIVE_CD_BASE && code<NATIVE_CD_BASE+NATIVE_CD_SLOTS*4)
                supported=s_cd[(code-NATIVE_CD_BASE)/4]!=NULL;
            count++;
            if(!supported) {
                fprintf(stderr,"[stage-pack] unported object $%06X in $%02X%02X at $%04X\n",code,z,a,tri_rd16(layout));
                missing++;
            }
        }
    }
    fprintf(stderr,"[stage-pack] object coverage: %u placements, %u unported\n",count,missing);
    return missing==0;
}

/* Dispatch a native object id. a0 = object; the caller's d7/a0 survive. */
int tri_obj_dispatch(uint32_t addr)
{
    if (addr < NATIVE_S1_BASE || addr >= NATIVE_CD_BASE + NATIVE_CD_SLOTS * 4u || (addr & 3))
        return 0;
    if (addr >= NATIVE_S2_BASE + 0x400u && addr < NATIVE_CD_BASE)
        return 0;
    obj_register();
    TriObjFn fn = NULL;
    if (addr >= NATIVE_CD_BASE)
        fn = s_cd[(addr - NATIVE_CD_BASE) >> 2];
    else if (addr < NATIVE_S1_BASE + 0x400u)
        fn = s_s1[(addr - NATIVE_S1_BASE) >> 2];
    else if (addr >= NATIVE_S2_BASE)
        fn = s_s2[(addr - NATIVE_S2_BASE) >> 2];
    uint32_t o = A(0) & 0xFFFFFF;
    uint32_t d7 = D(7), a0 = A(0);
    static long trace = -2;                   /* development: TRILOGY_OBJTRACE=<native address, hex> */
    if (trace == -2) trace = getenv("TRILOGY_OBJTRACE") ? strtol(getenv("TRILOGY_OBJTRACE"), NULL, 16) : -1;
    if (trace >= 0 && addr == (uint32_t)trace)
        fprintf(stderr, "[obj] f=%u o=%06X r=%02X x=%04X y=%04X fr=%02X anim=%02X rend=%02X st=%02X maps=%06X tile=%04X sub=%02X\n",
                (unsigned)r16(RAM_Level_frame_counter), o, r8(o + OBJ_routine), r16(o + OBJ_x_pos), r16(o + OBJ_y_pos),
                r8(o + OBJ_frame), r8(o + OBJ_anim), r8(o + OBJ_render), r8(o + OBJ_status), r32(o + OBJ_mappings),
                r16(o + OBJ_art_tile), r8(o + OBJ_subtype));
    if (fn)
        fn(o);
    else {
        fprintf(stderr,"[stage-pack] unported spawned object $%06X in $%04X\n",addr,r16(RAM_Current_zone_and_act));
        o_delete(o);
    }
    D(7) = d7;
    A(0) = a0;
    return 1;
}

/* ---- engine services ---------------------------------------------------- */
static inline void with_a0(uint32_t o) { A(0) = RAMPTR(o); }

void o_draw(uint32_t o)   { with_a0(o); tri_jsr(S3K_Draw_Sprite); }
/* S3K's loader marks every layout object in the respawn table and never
 * reloads a marked one; S2's marks only the objects its layout flags (y word
 * bit 15; s2_level.py carries it as bit 12), so an S2 object deleted with a plain DeleteObject - most of them,
 * leaving the screen - comes back when the camera returns. In an S2 zone an
 * unflagged layout object's mark is cleared as it goes. Sonic CD's layout has
 * the same flag (scd_level.py: bit 12 too). (Index: the respawn byte's offset
 * in Object_respawn_table = its entry from TD_OBJECTS.) */
#define RAM_Object_respawn_table 0xFFEB00u
static void s2_unmark(uint32_t o)
{
    uint16_t r = OB16(o, OBJ_respawn);
    uint32_t e = tri_dir();
    if (!r || !e || (tri_rd8(e + TD_GAME) != TRI_GAME_S2 && tri_rd8(e + TD_GAME) != TRI_GAME_CD)) return;
    uint32_t idx = (uint32_t)(r - (uint16_t)RAM_Object_respawn_table);
    if (idx >= 0x300) return;
    uint32_t lay = tri_rd32(e + TD_OBJECTS);
    if (lay && !(tri_rd16(lay + idx * 6u + 2u) & 0x1000))      /* (S2's "remember", moved to bit 12) */
        w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
}

void o_delete(uint32_t o) { s2_unmark(o); with_a0(o); tri_jsr(S3K_Delete_Current_Sprite); }
void o_remember(uint32_t o) { with_a0(o); tri_jsr(S3K_Sprite_OnScreen_Test); }
void o_touch(uint32_t o)
{
    if (OB8(o, OBJ_coll_flags)) { with_a0(o); tri_jsr(S3K_Add_SpriteToCollisionResponseList); }
}
void o_move(uint32_t o)   { with_a0(o); tri_jsr(S3K_MoveSprite2); }
void o_fall(uint32_t o)   { with_a0(o); tri_jsr(S3K_MoveSprite); }

int o_out_of_range(uint32_t o, uint16_t x)
{
    uint16_t d0 = x & 0xFF80;
    uint16_t cam = (uint16_t)((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80);
    return (uint16_t)(d0 - cam) > 0x280;
}

void o_delete_respawnable(uint32_t o)
{
    uint16_t r = OB16(o, OBJ_respawn);
    if (r)
        w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
    o_delete(o);
}

int16_t o_floor_dist(uint32_t o)
{
    with_a0(o);
    tri_jsr(S3K_ObjCheckFloorDist);
    return (int16_t)D(1);
}

/* S1 AnimateSprite (transcribed). S3K's Animate_Sprite keeps the frame byte
 * whole, but S1 scripts carry flips in bits 5/6 (aniXFlip/aniYFlip), which
 * S1 strips into the render flags. ob2ndRout ($FA/$FB) is byte $25. */
#define OBJ_prev_anim  0x21
#define OBJ_2nd        0x25
void o_animate(uint32_t o, uint32_t anim)
{
    if (!anim) return;
    uint8_t d0 = OB8(o, OBJ_anim);
    if (d0 != OB8(o, OBJ_prev_anim)) {
        SET8(o, OBJ_prev_anim, d0);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) return;
    uint32_t a1 = anim + tri_rd16(anim + d0 * 2u);
    SET8(o, OBJ_anim_timer, tri_rd8(a1));
    uint8_t d1 = OB8(o, OBJ_anim_frame);
    uint8_t f = tri_rd8(a1 + 1 + d1);
    if (f & 0x80) {
        switch (f) {
        case 0xFF:                             /* restart */
            SET8(o, OBJ_anim_frame, 0);
            f = tri_rd8(a1 + 1);
            break;
        case 0xFE: {                           /* jump back */
            uint8_t b = tri_rd8(a1 + 2 + d1);
            SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) - b);
            d1 = (uint8_t)(d1 - b);
            f = tri_rd8(a1 + 1 + d1);
            break;
        }
        case 0xFD:                             /* change animation */
            SET8(o, OBJ_anim, tri_rd8(a1 + 2 + d1));
            return;
        case 0xFC:                             /* next routine */
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
            return;
        case 0xFB:
            SET8(o, OBJ_anim_frame, 0);
            SET8(o, OBJ_2nd, 0);
            return;
        case 0xFA:
            SET8(o, OBJ_2nd, OB8(o, OBJ_2nd) + 2);
            return;
        default:
            return;
        }
    }
    SET8(o, OBJ_frame, f & 0x1F);              /* Anim_Next */
    uint8_t fl = (uint8_t)((((f << 3) | (f >> 5)) ^ OB8(o, OBJ_status)) & 3);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | fl);
    SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
}

/* S2 AnimateSprite: 7-bit frame numbers, the render flips follow the status
 * flips at every new frame, and $F9 advances the second routine. */
void o_animate_s2(uint32_t o, uint32_t anim)
{
    if (!anim) return;
    uint8_t d0 = OB8(o, OBJ_anim);
    if (d0 != OB8(o, OBJ_prev_anim)) {
        SET8(o, OBJ_prev_anim, d0);
        SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_anim_timer, 0);
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) return;
    uint32_t a1 = anim + tri_rd16(anim + d0 * 2u);
    SET8(o, OBJ_anim_timer, tri_rd8(a1));
    uint8_t d1 = OB8(o, OBJ_anim_frame);
    uint8_t f = tri_rd8(a1 + 1 + d1);
    if (f & 0x80) {
        switch (f) {
        case 0xFF:                             /* Anim_End_FF: restart */
            SET8(o, OBJ_anim_frame, 0);
            f = tri_rd8(a1 + 1);
            break;
        case 0xFE: {                           /* Anim_End_FE: jump back */
            uint8_t b = tri_rd8(a1 + 2 + d1);
            d1 = (uint8_t)(d1 - b);
            SET8(o, OBJ_anim_frame, d1);
            f = tri_rd8(a1 + 1 + d1);
            break;
        }
        case 0xFD:                             /* Anim_End_FD: change animation */
            SET8(o, OBJ_anim, tri_rd8(a1 + 2 + d1));
            return;
        case 0xFC:                             /* Anim_End_FC: next routine */
            SET8(o, OBJ_routine, OB8(o, OBJ_routine) + 2);
            SET8(o, OBJ_anim_timer, 0);
            SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
            return;
        case 0xFB:                             /* Anim_End_FB */
            SET8(o, OBJ_anim_frame, 0);
            SET8(o, OBJ_2nd, 0);
            return;
        case 0xFA: case 0xF9:                  /* Anim_End_FA / F9 (Obj89's arrow routine: our 2nd) */
            SET8(o, OBJ_2nd, OB8(o, OBJ_2nd) + 2);
            return;
        default:
            return;
        }
    }
    SET8(o, OBJ_frame, f & 0x7F);              /* Anim_Next */
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
    SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
}

static uint32_t alloc_result(void)
{
    /* AllocateObject*: Z set (bne fails) when a slot was found */
    return (g_cpu.SR & 4u) ? (A(1) & 0xFFFFFF) : 0;
}
uint32_t o_alloc(void) { tri_jsr(S3K_AllocateObject); return alloc_result(); }
uint32_t o_alloc_after(uint32_t o) { with_a0(o); tri_jsr(S3K_AllocateObjectAfterCurrent); return alloc_result(); }

uint32_t g_o_d6;

int16_t o_solid(uint32_t o, int d1, int d2, int d3, int d4)
{
    with_a0(o);
    D(1) = (uint32_t)d1; D(2) = (uint32_t)d2; D(3) = (uint32_t)d3; D(4) = (uint32_t)d4;
    tri_jsr(S3K_SolidObjectFull);
    g_o_d6 = D(6);
    return (int16_t)D(4);
}

/* GetArcTan (S2 CalcAngle): angle of (x, y) */
uint8_t o_atan(int16_t x, int16_t y)
{
    D(1) = (uint32_t)(uint16_t)x;
    D(2) = (uint32_t)(uint16_t)y;
    tri_jsr(S3K_GetArcTan);
    return (uint8_t)D(0);
}

/* SolidObjectFull_1P / SolidObjectFull2_1P (S2 SolidObject_Always_SingleCharacter):
 * one player (0/1); "always" skips the on-screen test. Returns d4, d6 in g_o_d6. */
int16_t o_solid_1p(uint32_t o, int n, int d1, int d2, int d3, int d4, int always)
{
    with_a0(o);
    A(1) = RAMPTR(o_player(n));
    D(6) = n ? 4u : 3u;
    D(1) = (uint32_t)d1; D(2) = (uint32_t)d2; D(3) = (uint32_t)d3; D(4) = (uint32_t)d4;
    tri_jsr(always ? S3K_SolidObjectFull2_1P : S3K_SolidObjectFull_1P);
    g_o_d6 = D(6);
    return (int16_t)D(4);
}

void o_platform_sloped(uint32_t o, int d1, int d3, int d4, uint32_t slope)
{
    with_a0(o);
    D(1) = (uint32_t)d1; D(3) = (uint32_t)d3; D(4) = (uint32_t)d4;
    A(2) = slope;
    tri_jsr(S3K_SolidObjectTopSloped);
}

void o_spike_hurt(uint32_t o, uint32_t player)
{
    with_a0(o);
    A(1) = RAMPTR(player);
    tri_jsr(0x024280u);              /* sub_24280: S3K spike hurt */
}

uint8_t s1_ani0_frame, s1_ani1_frame, s1_ani2_frame;
static int8_t s1_ani0_time, s1_ani1_time, s1_ani2_time;
void s1_synchro_animate(void)
{
    if (--s1_ani0_time < 0) { s1_ani0_time = 11; s1_ani0_frame = (uint8_t)((s1_ani0_frame - 1) & 7); }
    if (--s1_ani1_time < 0) { s1_ani1_time = 7; s1_ani1_frame = (uint8_t)((s1_ani1_frame + 1) & 3); }
    if (--s1_ani2_time < 0) { s1_ani2_time = 7; if (++s1_ani2_frame >= 6) s1_ani2_frame = 0; }
}

/* SolidObjectTop_1P (S2 PlatformObject_SingleCharacter): one player (0/1) */
void o_platform_1p(uint32_t o, int n, int d1, int d3, int d4)
{
    with_a0(o);
    A(1) = RAMPTR(o_player(n));
    D(6) = n ? 4u : 3u;
    D(1) = (uint32_t)d1; D(3) = (uint32_t)d3; D(4) = (uint32_t)d4;
    tri_jsr(0x01E2BCu);              /* SolidObjectTop_1P */
}

void o_platform(uint32_t o, int d1, int d3, int d4)
{
    with_a0(o);
    D(1) = (uint32_t)d1; D(3) = (uint32_t)d3; D(4) = (uint32_t)d4;
    tri_jsr(S3K_SolidObjectTop);
}

/* In a Sonic 2 zone the driver plays the S2 sound set (trilogy_music.c), where
 * S2's effects also take ids in the driver's "continuous" range ($BC+): there
 * asking for the effect already playing would only extend it, but S2 restarts
 * them (one gloop per blue ball...), so the last continuous effect is forgotten
 * first (zContinuousSFX, Z80 $1C25) - except for the S3K ones that do loop.
 * The same in Sonic CD's set (its driver restarts every effect). Development:
 * TRILOGY_SFXLOG=1 logs each request (the S3K id; the set; the Z80 pointer). */
#define Z80_CONTINUOUS_SFX 0x1C25u
#define Z80_SFX_PTRS       0x167Eu
void o_sfx(int id)
{
    static int log = -1;
    if (log < 0) log = getenv("TRILOGY_SFXLOG") != NULL;
    if (log && id >= 0x33 && id <= 0xDF) {
        const uint8_t *z = g_machine.bus.z80_ram;
        fprintf(stderr, "[sfx] f=%u $%02X %s ptr $%04X\n", (unsigned)r16(RAM_Level_frame_counter), id,
                tri_scd_sound_set_on() ? "CD" : tri_sound_set_foreign() ? "S2" : "S3K",
                (unsigned)(z[Z80_SFX_PTRS + (id - 0x33) * 2] | z[Z80_SFX_PTRS + (id - 0x33) * 2 + 1] << 8));
    }
    if (id >= 0xBC && id != 0xBC && id != 0xC8 && tri_sound_set_foreign())
        g_machine.bus.z80_ram[Z80_CONTINUOUS_SFX] = 0;
    D(0) = (uint32_t)id;
    tri_jsr(S3K_Play_SFX);
}

/* a Sonic 2 sound by its own id (SndID_*, $A0-$F0), from Sonic 2's objects */
void o_sfx_s2(int sndid)
{
    int id = tri_s2_sfx_id(sndid);
    if (id) o_sfx(id);
}

/* a Sonic CD FM sound by its own id ($90-$DF: _Sound IDs.i, FM_*), from Sonic
 * CD's objects and scenes. In the CD set (scd_sfx.py) the id it plays at;
 * without it (an scd.bin with no sound set, S3K's set in), S3K's own sound for
 * the ones scd_sfx.py puts at S3K ids (S3K_TO_CD), else nothing */
void o_sfx_cd(int fm)
{
    static const struct { uint8_t fm, s3k; } k_s3k[] = {
        { 0x90, 0x36 }, { 0x91, 0xB6 }, { 0x92, 0x62 }, { 0x93, 0x35 }, { 0x94, 0xB9 }, { 0x95, 0x33 },
        { 0x96, 0x3D }, { 0x97, 0x3A }, { 0x98, 0xB1 }, { 0x9A, 0xB0 }, { 0x9C, 0x3C }, { 0x9D, 0xB8 },
        { 0x9E, 0xB4 }, { 0xA2, 0x39 }, { 0xA8, 0x34 }, { 0xAC, 0x6E }, { 0xAD, 0x38 }, { 0xAE, 0x63 },
        { 0xAF, 0xB3 }, { 0xB6, 0x3B }, { 0xB7, 0x52 }, { 0xBD, 0x5B }, { 0xC8, 0xAF }, { 0xDF, 0xA9 },
    };
    int id = tri_scd_sound_set_on() ? tri_scd_sfx_id(fm) : 0;
    if (!id && !tri_sound_set_foreign())
        for (size_t k = 0; k < sizeof k_s3k / sizeof k_s3k[0]; k++)
            if (k_s3k[k].fm == fm) id = k_s3k[k].s3k;
    if (id) o_sfx(id);
}

int o_random(void) { tri_jsr(S3K_Random_Number); return (int)D(0); }

void o_sincos(uint8_t angle, int16_t *s, int16_t *c)
{
    D(0) = angle;
    tri_jsr(S3K_GetSineCosine);
    *s = (int16_t)D(0);
    *c = (int16_t)D(1);
}

uint32_t o_player(int n) { return n ? RAM_Player_2 : RAM_Player_1; }
int o_player_active(uint32_t p) { return r32(p) != 0; }

/* ---- shared S1 helpers ------------------------------------------------------ */
int o_visible(uint32_t o)
{
    int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - r16(RAM_Camera_X_pos));
    int16_t dy = (int16_t)(OB16(o, OBJ_y_pos) - r16(RAM_Camera_Y_pos));
    return dx >= 0 && dx < 320 && dy >= 0 && dy < 224;
}

int o_partly_visible(uint32_t o)
{
    int16_t w = OB8(o, OBJ_width_px);
    int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - r16(RAM_Camera_X_pos) + w);
    if (dx < 0) return 0;
    if ((int16_t)(dx - 2 * w) >= 320) return 0;
    int16_t dy = (int16_t)(OB16(o, OBJ_y_pos) - r16(RAM_Camera_Y_pos));
    return dy >= 0 && dy < 224;
}

int16_t o_ceiling_dist(uint32_t o) { with_a0(o); tri_jsr(S3K_ObjCheckCeilingDist); return (int16_t)D(1); }
int16_t o_wall_right(uint32_t o, int d3) { with_a0(o); D(3) = (uint32_t)d3; tri_jsr(S3K_ObjCheckRightWallDist); return (int16_t)D(1); }
int16_t o_wall_left(uint32_t o, int d3) { with_a0(o); D(3) = (uint32_t)d3; tri_jsr(S3K_ObjCheckLeftWallDist); return (int16_t)D(1); }

int16_t o_floor_block(uint32_t o, uint16_t *blk)
{
    with_a0(o);
    tri_jsr(S3K_ObjCheckFloorDist);
    if (blk) *blk = r16(A(1));               /* FindFloor leaves a1 on the block word */
    return (int16_t)D(1);
}

/* sub_1DD0E: S1's SolidObject2F (sloped full solid) + ExitPlatform +
 * SlopeObject2 for both players. a2 = heightmap (2 px per byte). */
int16_t o_solid_sloped(uint32_t o, int d1, int d2, int d4, uint32_t hmap)
{
    with_a0(o);
    D(1) = (uint32_t)d1; D(2) = (uint32_t)d2; D(4) = (uint32_t)d4;
    A(2) = hmap;
    tri_jsr(0x01DD0Eu);
    g_o_d6 = D(6);
    return (int16_t)D(4);
}

uint32_t o_slot(int index) { return 0xFFB000u + (uint32_t)index * OBJ_SIZE; }
int o_slot_index(uint32_t o) { return (int)(((o & 0xFFFFu) - 0xB000u) / OBJ_SIZE); }

/* SmashObject: o's current frame breaks into one object per mapping piece
 * (S3K single-piece render mode, bit 5); o itself becomes the first. */
void o_smash(uint32_t o, int count, const int16_t *spd, int gravity, int routine)
{
    uint32_t maps = r32(o + OBJ_mappings);
    uint32_t fa = maps + tri_rd16(maps + OB8(o, OBJ_frame) * 2u);
    uint32_t piece = fa + 2;
    uint8_t render = (uint8_t)(OB8(o, OBJ_render) | 0x20);
    SET8(o, OBJ_render, render);
    uint32_t code = r32(o), a1 = o;
    for (int k = 0; k < count; k++) {
        if (k) { a1 = o_alloc(); if (!a1) break; piece += 6; }
        SET8(a1, OBJ_routine, routine);
        SET32(a1, OBJ_code, code);
        SET32(a1, OBJ_mappings, piece);
        SET8(a1, OBJ_render, render);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
        SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
        SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
        SET8(a1, OBJ_height_px, 0x10);
        SET16(a1, OBJ_x_vel, spd[k * 2]);
        SET16(a1, OBJ_y_vel, spd[k * 2 + 1]);
        if ((a1 & 0xFFFF) < (o & 0xFFFF)) {     /* already run this frame */
            o_move(a1);
            SET16(a1, OBJ_y_vel, OB16(a1, OBJ_y_vel) + gravity);
            o_draw(a1);
        }
    }
    o_sfx(0x59);                               /* sfx_WallSmash (S3K: collapse) */
}

void o_score(uint32_t o, int frame)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_EnemyScore);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_frame, frame);
}

void o_hurt_player(uint32_t p, uint32_t o)
{
    A(0) = RAMPTR(p);
    A(1) = RAMPTR(o);
    tri_jsr(S3K_Touch_ChkHurt);
}

/* S1 ReactToItem proximity (for sizes whose S3K Touch_Sizes entry differs,
 * $0A and $21): object half-size (w, h) against each player in normal
 * control, using the S3K player box (x-8, y-(y_radius-3), $10 x 2*(y_radius-3)).
 * Returns bit 0 = player 1, bit 1 = player 2. */
int o_s1_touch(uint32_t o, int w, int h)
{
    int hit = 0;
    if (!(OB8(o, OBJ_render) & 0x80)) return 0;
    for (int i = 0; i < 2; i++) {
        uint32_t p = o_player(i);
        if (!o_player_active(p) || OB8(p, OBJ_routine) != 2) continue;
        uint16_t d2 = (uint16_t)(OB16(p, OBJ_x_pos) - 8);
        uint16_t d5 = (uint16_t)(OB8(p, OBJ_y_radius) - 3);
        uint16_t d3 = (uint16_t)(OB16(p, OBJ_y_pos) - d5);
        d5 = (uint16_t)(d5 * 2);
        uint16_t a = (uint16_t)(OB16(o, OBJ_x_pos) - w);
        if (a >= d2 ? (uint16_t)(a - d2) > 0x10 : (uint32_t)(uint16_t)(a - d2) + (uint32_t)(2 * w) <= 0xFFFF)
            continue;
        a = (uint16_t)(OB16(o, OBJ_y_pos) - h);
        if (a >= d3 ? (uint16_t)(a - d3) > d5 : (uint32_t)(uint16_t)(a - d3) + (uint32_t)(2 * h) <= 0xFFFF)
            continue;
        hit |= 1 << i;
    }
    return hit;
}

void o_kill_player(uint32_t p)
{
    A(0) = RAMPTR(p);
    tri_jsr(S3K_Kill_Character);
}

/* ---- assets ------------------------------------------------------------- */
/* An object's asset in the current zone (ZONE_ASSET in s1_objects.py) */
static int zone_asset(int asset)
{
    static const uint8_t map[][3] = TRI_ZONE_ASSETS;
    int z = tri_source_zone();
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i][0] == z && map[i][1] == asset) return map[i][2];
    return asset;
}

static uint32_t asset_rec(int asset)
{
    asset = zone_asset(asset);
    uint32_t e = tri_dir();
    if (!e) return 0;
    uint32_t t = tri_rd32(e + TD_ANIM);
    return t ? t + (uint32_t)asset * 16u : 0;
}
uint32_t tri_signpost_mapping(void);
uint32_t tri_signpost_animation(void);
uint32_t o_asset_maps(int asset) {
    uint32_t p=asset==S1A_SIGN?tri_signpost_mapping():0;
    uint32_t r=asset_rec(asset);return p?p:r?tri_rd32(r):0;
}
uint16_t o_asset_tile(int asset) {
    if(asset==S1A_SIGN && tri_signpost_mapping())return 0;
    uint32_t r=asset_rec(asset);return r?tri_rd16(r+4):0;
}
uint32_t o_asset_anim(int asset) {
    uint32_t p=asset==S1A_SIGN?tri_signpost_animation():0;
    uint32_t r=asset_rec(asset);return p?p:r?tri_rd32(r+8):0;
}
void o_asset(uint32_t o, int asset)
{
    SET32(o, OBJ_mappings, o_asset_maps(asset));
    SET16(o, OBJ_art_tile, o_asset_tile(asset));
}

static int s_reupload;
static uint64_t s_deferred_ready[4];  /* deferred assets uploaded this level (bit per id) */
#define DEFERRED(i)  ((s_deferred_ready[(i) >> 6] >> ((i) & 63)) & 1)
#define ASSET_META   (TRI_ASSET_COUNT - 2)            /* last two records: retired mask */

/* Deferred (boss) art is only in VRAM once uploaded; regular art always. */
int o_asset_ready(int asset)
{
    asset = zone_asset(asset);
    uint32_t r = asset_rec(asset);
    if (!r || !(tri_rd16(r + 6) & 0x8000)) return 1;
    return (int)DEFERRED(asset);
}

/* Write every object asset's art for the current act into VRAM. The title
 * card's art lives in $500-$57F until the card is gone, so that window is
 * only written once it has finished (tri_obj_titlecard_done). */
static int s_card_done;
static void upload_record(uint32_t r)
{
    if(tri_rd16(r+6)&0x4000)return; /* immutable host sprite-pattern bank */
    uint16_t nseg = tri_rd16(r + 6) & 0x7FFF;
    uint32_t seg = tri_rd32(r + 12);
    for (uint16_t s = 0; seg && s < nseg; s++) {
        uint32_t src = tri_rd32(seg + s * 8u);
        uint32_t dst = (uint32_t)tri_rd16(seg + s * 8u + 4) * 32u;
        uint16_t cnt = tri_rd16(seg + s * 8u + 6);
        uint32_t n = (uint32_t)(cnt & 0x7FFF) * 32u;
        uint32_t t0 = dst / 32u;
        if (getenv("TRILOGY_ASSETLOG"))                   /* development */
            fprintf(stderr, "[asset] tiles $%03X-$%03X ext=%d card_done=%d\n", t0, t0 + (cnt & 0x7FFF) - 1, cnt >> 15, s_card_done);
        if (!s_card_done && t0 + n / 32u > 0x500u && t0 < 0x580u)
            continue;                                    /* (the card's own tiles keep their colours) */
        tri_ext_tiles(t0, cnt & 0x7FFF, cnt >> 15);      /* line-0 art: the extended sprite line */
        for (uint32_t k = 0; k < n && dst + k < 0x10000u; k++)
            g_machine.vdp.vram[dst + k] = tri_rd8(src + k);
    }
}

/* Sonic CD: the window's art segments resident in the camera's section (as
 * its LoadSectionArt / UpdateSectionArt swap object art by section). */
static int s_cd_section = -1;
static void upload_cd(void)
{
    extern uint32_t tri_scd_art_segments(void);
    extern int tri_scd_section(void);
    uint32_t seg = tri_scd_art_segments();
    s_cd_section = tri_scd_section();
    for (; seg && (tri_rd16(seg + 6) & 0x7FFF); seg += 12) {
        if (!(tri_rd16(seg + 8) >> s_cd_section & 1)) continue;
        uint32_t src = tri_rd32(seg), t0 = tri_rd16(seg + 4);
        uint16_t cnt = tri_rd16(seg + 6), n = cnt & 0x7FFF;
        if (!s_card_done && t0 + n > 0x500u && t0 < 0x580u)
            continue;                                    /* (the card's own tiles keep their colours) */
        tri_ext_tiles(t0, n, cnt >> 15);
        for (uint32_t k = 0; k < n * 32u && t0 * 32u + k < 0x10000u; k++)
            g_machine.vdp.vram[t0 * 32u + k] = tri_rd8(src + k);
    }
}

/* (each frame: a new section, its art at the next VBlank) */
static void cd_section_check(void);

/* Regular assets first; boss art already brought in (it may reuse retired
 * badnik tiles) is re-applied last so it always wins. */
static void upload_now(void)
{
    uint32_t e = tri_dir();
    if (!e) return;
    if (tri_cd_zone(tri_zone())) { upload_cd(); return; }
    uint32_t t = tri_rd32(e + TD_ANIM);
    if (!t) return;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ASSET_META; i++) {
            uint32_t r = t + (uint32_t)i * 16u;
            int deferred = (tri_rd16(r + 6) & 0x8000) != 0;
            if (pass == 0 ? deferred : !(deferred && DEFERRED(i)))
                continue;
            upload_record(r);
        }
}

/* Level load: upload now, and again on the first gameplay frame because the
 * level art's last Kos module is DMA'd at the next VBlank. */
void tri_obj_upload_art(void)
{
    tri_ext_level();
    memset(s_deferred_ready, 0, sizeof s_deferred_ready);
    s_card_done = 0;
    upload_now();
    s_reupload = 1;
}

/* Boss art may reuse the tiles of "retired" assets: ones whose every placement
 * lies left of the locked arena (the last asset record holds their id mask). Remove
 * any such object still alive before its tiles are overwritten. */
static void evict_retired(uint32_t t)
{
    uint32_t m = t + (uint32_t)ASSET_META * 16u, mask[8], any = 0;
    for (int k = 0; k < 8; k++) any |= mask[k] = tri_rd32(m + (uint32_t)k * 4u);
    if (!any) return;
    for (uint32_t s = 0; s < 110; s++) {
        uint32_t o = 0xFFB000u + s * 0x4Au;
        uint32_t code = r32(o);
        if (code < NATIVE_S1_BASE || code >= NATIVE_S2_BASE + 0x400u)   /* S1 and S2 native objects */
            continue;
        uint32_t maps = r32(o + OBJ_mappings);
        for (int i = 0; i < ASSET_META; i++)
            if (((mask[i >> 5] >> (i & 31)) & 1) && maps == tri_rd32(t + (uint32_t)i * 16u)) {
                o_delete(o);
                break;
            }
    }
}

/* Upload one (deferred) asset now, e.g. boss art when the boss appears. */
void o_upload_asset(int asset)
{
    asset = zone_asset(asset);
    uint32_t e = tri_dir();
    if (!e) return;
    uint32_t t = tri_rd32(e + TD_ANIM);
    if (!t) return;
    uint32_t r = t + (uint32_t)asset * 16u;
    if (tri_rd16(r + 6) & 0x8000) {
        evict_retired(t);
        s_deferred_ready[asset >> 6] |= 1ull << (asset & 63);
    }
    upload_record(r);
}

/* Level entry (every start, death restarts included): a new title card is
 * coming, so the card window is the card's again and the last load's
 * extended-line flags (which would colour the card's tiles) are cleared. */
void tri_obj_level_entry(void)
{
    extern void tri_ext_clear_tiles(void);
    s_card_done = 0;
    tri_ext_clear_tiles();
    extern void tri_sound_set_for_zone(void);
    tri_sound_set_for_zone();                            /* (the Sonic 2 sound set in Sonic 2 zones) */
}

int tri_obj_card_done(void) { return s_card_done; }

void tri_obj_titlecard_done(void)
{
    s_card_done = 1;
    upload_now();
}

static void cd_section_check(void)
{
    extern int tri_scd_section(void);
    if (tri_cd_zone(tri_zone()) && tri_dir() && s_cd_section >= 0 && tri_scd_section() != s_cd_section)
        s_reupload = 1;
}

/* Development: TRILOGY_RINGS=n sets the ring count on the first gameplay
 * frame after a level load (testing giant rings / Super Sonic). */
void tri_obj_frame(void)
{
    static int done, god = -1;
    {   extern void tri_sound_set_for_zone(void); tri_sound_set_for_zone(); }   /* (kept through S3K's driver reloads) */
    {   extern void tri_audit_frame(void); tri_audit_frame(); }      /* (development: TRILOGY_AUDIT) */
    cd_section_check();                                    /* (Sonic CD: object art by section) */
    if (god < 0) { const char *g = getenv("TRILOGY_GOD"); god = g && *g; }  /* development: Sonic can't be hurt */
    if (god) w8(RAM_Player_1 + 0x34, 0x1C);           /* (visible: bit 2 clear after the decrement) */
    {                                                      /* development: TRILOGY_VRAMDUMP=n - VRAM to vram.bin */
        static int at = -2;                                /* at level frame n */
        if (at == -2) { const char *g = getenv("TRILOGY_VRAMDUMP"); at = g && *g ? atoi(g) : -1; }
        if (at >= 0 && r16(RAM_Level_frame_counter) == (unsigned)at) {
            FILE *f = fopen("vram.bin", "wb");
            if (f) { fwrite(g_machine.vdp.vram, 1, 0x10000, f); fclose(f); }
            at = -1;
        }
    }
    {   extern void s2_cnz_bumpers(void); if ((r8(RAM_Game_mode) & 0x7F) == 0x0C) s2_cnz_bumpers(); }   /* CNZ wall bumpers */
    {                                                      /* development: TRILOGY_TOUCHMAX - peak entries in S3K's */
        static int on = -1, mx;                            /* Collision_response_list ($FFE380, 63 max) per 600 frames */
        static unsigned n;
        if (on < 0) on = getenv("TRILOGY_TOUCHMAX") != NULL;
        if (on) {
            int c = r16(0xFFE380u) / 2;
            if (c > mx) mx = c;
            if (++n % 600 == 0) {
                fprintf(stderr, "[TOUCH] zone %04X max %d x %04X\n", r16(RAM_Current_zone_and_act), mx, r16(RAM_Player_1 + OBJ_x_pos));
                mx = 0;
            }
        }
    }
    {                                                      /* development: every Sonic 2 sound in turn (from SndID hex, default $A0), */
        static int sfxt = -2;                              /* one per 90 frames from level frame 300, all sound stopped before each */
        if (sfxt == -2) {
            const char *g = getenv("TRILOGY_SFXTEST");
            sfxt = g ? (int)strtol(g, NULL, 16) : -1;
            if (sfxt >= 0xA0) sfxt -= 0xA0; else if (sfxt > 0) sfxt = 0;
        }
        int fc = r16(RAM_Level_frame_counter);
        if (sfxt >= 0 && sfxt <= 0x50 && fc >= 290 && (fc - 300 + 90) % 90 == 80) { D(0) = 0xE2; tri_jsr(S3K_Play_Music); }   /* cmd_Stop */
        if (sfxt >= 0 && sfxt <= 0x50 && fc >= 300 && (fc - 300) % 90 == 0) { o_sfx_s2(0xA0 + sfxt); sfxt++; }
    }
    {
        static int ss_dev = -2;                            /* development: S2 special stage n (1-7), five seconds into the act */
        if (ss_dev == -2) { const char *g = getenv("TRILOGY_S2SS"); ss_dev = g && *g ? atoi(g) - 1 : -1; }
        if (ss_dev >= 0 && r16(RAM_Level_frame_counter) >= 300) {
            extern void tri_ss_dev(int stage);
            tri_ss_dev(ss_dev);
            ss_dev = -1;
        }
    }
    {
        static int pos = -1;                               /* development: TRILOGY_POSLOG=n, Player 1 every n frames */
        if (pos < 0) { const char *g = getenv("TRILOGY_POSLOG"); pos = g && *g ? atoi(g) : 0; }
        if (pos > 0 && !(r16(RAM_Level_frame_counter) % pos))
            fprintf(stderr, "[POS] f %u x %04X y %04X xv %04X yv %04X gv %04X st %02X rt %02X an %02X oc %02X iv %02X ang %02X ts %02X\n", r16(RAM_Level_frame_counter),
                    r16(RAM_Player_1 + 0x10), r16(RAM_Player_1 + 0x14), r16(RAM_Player_1 + 0x18), r16(RAM_Player_1 + 0x1A),
                    r16(RAM_Player_1 + 0x1C), r8(RAM_Player_1 + 0x2A), r8(RAM_Player_1 + 0x05), r8(RAM_Player_1 + 0x20), r8(RAM_Player_1 + 0x2E), r8(RAM_Player_1 + 0x34),
                    r8(RAM_Player_1 + 0x26), r8(RAM_Player_1 + 0x46));
    }
    if(tri_cd_zone(tri_zone())) {extern void tri_ss_cd_practice_tick(void);tri_ss_cd_practice_tick();}
#if GEN_ENABLE_TRACE
    {
        static int ufo_dev = -2;                           /* development: Sonic CD UFO stage n (1-8), five seconds in */
        if (ufo_dev == -2) { const char *g = getenv("TRILOGY_SCDSS"); ufo_dev = g && *g ? atoi(g) - 1 : -1; }
        if (ufo_dev >= 0 && ufo_dev < 8 && r16(RAM_Level_frame_counter) >= 300) {
            extern void tri_ss_dev_ufo(int stage);
            tri_ss_dev_ufo(ufo_dev);
            ufo_dev = -1;
        }
    }
#endif
    {
        /* tools: TRILOGY_GLIDE=1 carries a hidden, frozen player along the act's
         * ground so the camera glides through it (the Data Select's zone cards
         * are recorded from this: tools/trilogy/previews.py) */
        static int glide = -1;
        static int32_t gy;
        if (glide < 0) { const char *g = getenv("TRILOGY_GLIDE"); glide = g && *g ? atoi(g) : 0; }
        if (glide == 2) {                                  /* 2: the player runs the act (the script steers), unseen and unhurt */
            SET8(RAM_Player_1, 0x34, 0x18);
        } else if (glide && r16(RAM_Level_frame_counter) >= 2) {
            uint32_t p = RAM_Player_1;
            int x = (int16_t)OB16(p, OBJ_x_pos) + 4;
            if (r16(RAM_Level_frame_counter) == 2) gy = (int32_t)(int16_t)OB16(p, OBJ_y_pos) << 8;
            int y = gy >> 8, target = y;
            for (int sy = y - 160; sy < y + 480; sy += 8) {
                if (sy < 16) continue;
                int ok = 1;
                for (int k = 0; k < 2 && ok; k++) {
                    int yy = sy - k * 16, lx = x + 64;
                    uint16_t row = r16(RAM_Level_layout_header + 8u + (uint32_t)(yy >> 7) * 4u);
                    uint8_t ch = r8(0xFF0000u | (uint32_t)(uint16_t)(row + (lx >> 7)));
                    uint16_t cw = r16(0xFF0000u + (uint32_t)ch * 0x80u + (uint32_t)(((yy & 0x70) >> 4) * 16 + ((lx & 0x70) >> 4) * 2));
                    int solid = (cw & 0x1000) != 0;
                    if (k == 0 ? !solid : solid) ok = 0;
                }
                if (ok) { target = sy - 20; break; }
            }
            gy += ((int32_t)target * 256 - gy) / 8;
            SET16(p, OBJ_x_pos, x);
            SET16(p, OBJ_y_pos, gy >> 8);
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, 0);
            SET16(p, OBJ_ground_vel, 0);
            SET8(p, 0x2E, 0x81);                            /* object_control */
            SET8(p, 0x34, 0x18);                            /* (flickered out every frame) */
        }
    }
    {
        static int st = -1;                                /* development: the star circle counts as touched */
        if (st < 0) st = getenv("TRILOGY_STARTOUCH") != NULL;
        if (st)
            for (uint32_t s = 0; s < 110; s++) {
                uint32_t o = 0xFFB000u + s * OBJ_SIZE;
                if (r32(o) == S3K_Obj_StarPost && OB8(o, OBJ_routine) == 8) { SET8(o, OBJ_coll_prop, 1); break; }
            }
    }
    {
        static int kf = -2, kv;                            /* development: TRILOGY_KICK=frame:speed (hex), Player 1's ground speed then */
        if (kf == -2) { const char *g = getenv("TRILOGY_KICK"); kf = g && sscanf(g, "%d:%x", &kf, &kv) == 2 ? kf : -1; }
        if (kf >= 0 && r16(RAM_Level_frame_counter) == (unsigned)kf) {
            SET16(RAM_Player_1, OBJ_ground_vel, (uint16_t)kv);
            SET16(RAM_Player_1, OBJ_x_vel, (uint16_t)kv);
        }
    }
    {
        static int pop = -1;                               /* development: a points popup every 20 frames */
        if (pop < 0) pop = getenv("TRILOGY_SCOREPOP") != NULL;
        if (pop && !(r16(RAM_Level_frame_counter) % 20)) {
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, S3K_Obj_EnemyScore);
                SET16(a1, OBJ_x_pos, OB16(RAM_Player_1, OBJ_x_pos) + 24);
                SET16(a1, OBJ_y_pos, OB16(RAM_Player_1, OBJ_y_pos) - 24);
                SET8(a1, OBJ_frame, (r16(RAM_Level_frame_counter) / 20) & 3);
            }
        }
    }
    if (done) return;
    done = 1;
    {
        const char *g = getenv("TRILOGY_S2END");          /* development: straight into Sonic 2's ending */
        if (g && *g) {
            extern void tri_s2_complete(void);
            const char *ch = getenv("TRILOGY_CHAR");       /* Player_mode for the ending */
            if (ch && *ch) { w16(RAM_Player_mode, (uint16_t)atoi(ch)); w16(0xFFFF0Au, (uint16_t)atoi(ch)); }   /* + Player_option */
            tri_s2_complete();
        }
    }
    {
        static int em = -1;                                /* development: TRILOGY_EMERALDS=1 holds all 14 (Hyper Sonic) */
        if (em < 0) { const char *g = getenv("TRILOGY_EMERALDS"); em = g && *g; }
        if (em) {
            w8(RAM_Chaos_emerald_count, 7);
            w8(RAM_Super_emerald_count, 7);
            for (uint32_t i = 0; i < 7; i++) w8(RAM_Collected_emeralds_array + i, 3);
        }
    }
    const char *e = getenv("TRILOGY_RINGS");
    if (e && *e) {
        w16(RAM_Ring_count, (uint16_t)atoi(e));
        w8(RAM_Update_HUD_ring_count, 0x80);
    }
}

/* Called right after the cartridge's VBlank handler (which flushes the DMA
 * queue), so the re-upload lands before the frame is displayed. */
/* VRAM copies that must land with the sprite table (DPLC-style frames): queued
 * from the frame's logic, done right after the V-int's DMA (like S3K's
 * Add_To_DMA_Queue), so no frame shows a sprite with the next frame's tiles */
static struct { uint32_t src, dst, len; } s_vq[48];
static int s_nvq;

static void vram_queue_flush(void)
{
    for (int i = 0; i < s_nvq; i++)
        for (uint32_t k = 0; k < s_vq[i].len; k++)
            g_machine.vdp.vram[(s_vq[i].dst + k) & 0xFFFF] = tri_rd8(s_vq[i].src + k);
    s_nvq = 0;
}

void tri_vram_queue(uint32_t src, uint32_t vram, uint32_t len)
{
    if (s_nvq == (int)(sizeof s_vq / sizeof s_vq[0])) vram_queue_flush();
    s_vq[s_nvq].src = src;
    s_vq[s_nvq].dst = vram;
    s_vq[s_nvq].len = len;
    s_nvq++;
}

void tri_obj_vblank(void)
{
    vram_queue_flush();
    extern void tri_thumb_vblank(void);
    tri_thumb_vblank();
    tri_ext_frame();
    if (tri_zone() == TRI_S2_OOZ) {                     /* NonWaterEffects: OilSlides */
        extern void s2_ooz_vblank(void);
        s2_ooz_vblank();
    }
    if (s_reupload) {
        s_reupload = 0;
        upload_now();
    }
}

/* Touch_Enemy, a boss hit (TH_BOSS_HIT, at its neg.w x_vel(a0)): for S1/S2
 * objects the same hit without the two bytes S3K keeps in the boss ($1C: who
 * hit it, $25: the collision flags to restore) - there S1 and S2 objects keep
 * their own state (ob2ndRout / routine_secondary at $25), and they restore
 * their collision themselves. 1 = done (rts). */
int tri_obj_boss_hit(void)
{
    uint32_t a0 = A(0) & 0xFFFFFFu, a1 = A(1) & 0xFFFFFFu;
    uint32_t code = r32(a1);
    if (code < NATIVE_S1_BASE || code >= NATIVE_S2_BASE + 0x400u) return 0;
    SET16(a0, OBJ_x_vel, (uint16_t)-(int16_t)OB16(a0, OBJ_x_vel));   /* bounce the player straight off */
    SET16(a0, OBJ_y_vel, (uint16_t)-(int16_t)OB16(a0, OBJ_y_vel));
    SET16(a0, 0x1C, (uint16_t)-(int16_t)OB16(a0, 0x1C));               /* ground_vel */
    SET8(a1, OBJ_coll_flags, 0);
    SET8(a1, OBJ_coll_prop, (uint8_t)(OB8(a1, OBJ_coll_prop) - 1));
    if (!OB8(a1, OBJ_coll_prop)) SET8(a1, OBJ_status, OB8(a1, OBJ_status) | 0x80);
    if (OB8(a0, 0x38) == 2 && OB8(a0, 0x2F) == 1) {                    /* Knuckles gliding: stops, falls */
        SET8(a0, 0x2F, 2);
        SET8(a0, OBJ_anim, 0x21);
        SET8(a0, OBJ_status, OB8(a0, OBJ_status) & ~1);
        if ((int16_t)OB16(a0, OBJ_x_vel) >= 0) SET8(a0, OBJ_status, OB8(a0, OBJ_status) | 1);
        SET8(a0, OBJ_y_radius, OB8(a0, 0x44));                         /* default_y_radius */
        SET8(a0, 0x1F, OB8(a0, 0x45));                                 /* default_x_radius */
    }
    return 1;
}
