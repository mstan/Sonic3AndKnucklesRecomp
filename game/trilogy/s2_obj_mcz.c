/*
 * s2_obj_mcz.c — Sonic 2 Mystic Cave objects, transcribed from s2disasm:
 * stomper (2A), crate (6A), brick / spike ball on a chain (75), sliding
 * spike block (76), gate (77), vine switch (7F), vine pulley (80),
 * drawbridge (81), Crawlton (9E), Flasher (A3).
 *
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $30+ here. Multi-sprite chains keep their own links past the children
 * ($18 + 6n). ButtonVine_Trigger is S3K's Level_trigger_array.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define OBJ_prev_anim   0x21
#define P_OBJCTRL       0x2E
#define ST_INAIR        0x02
#define ANI_HANG2       0x14          /* S3K's two-handed hang (S2 AniIDSonAni_Hang2) */
#define MS_COUNT        0x16          /* multi-draw: child count, children at $18 + 6n */
#define MS_SUB          0x18
#define BTN_ABC         0x70
#define BTN_DIRS        0x0F
#define BTN_LEFT        0x04
#define BTN_RIGHT       0x08
#define SFX_SWITCH      0xCD          /* SndID_Blip */
#define SFX_PULLEY_GRAB 0xCD          /* SndID_Blip */
#define SFX_DOOR_MOVE   0xE7          /* SndID_DrawbridgeMove */
#define SFX_DOOR_CLOSE  0xBB          /* SndID_DoorSlam */
#define SFX_THUMP       0xE9          /* SndID_DrawbridgeDown */

static uint32_t trigger(int n) { return RAM_Level_trigger_array + (uint32_t)(n & 0xFF); }

static int on_screen(uint32_t o) { return (OB8(o, OBJ_render) & 0x80) != 0; }

static void gone(uint32_t o)                         /* MarkObjGone */
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void gone_x(uint32_t o, uint16_t x)          /* MarkObjGone2 */
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* a multi-sprite chain object: drawn by Render_Sprites, its code only displays it */
static uint32_t chain_new(uint32_t parent, int asset, int w, int h, int count)
{
    uint32_t a1 = o_alloc_after(parent);
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(parent));
    o_asset(a1, asset);
    SET8(a1, OBJ_render, 4 | 0x40);
    SET16(a1, OBJ_priority, 5 << 7);
    SET8(a1, OBJ_width_px, w);
    SET8(a1, OBJ_height_px, h);
    SET16(a1, MS_COUNT, (uint16_t)count);
    return a1;
}

static void chain_child(uint32_t ch, int k, int16_t x, int16_t y, int frame)
{
    uint32_t a = ch + MS_SUB + (uint32_t)k * 6u;
    w16(a, (uint16_t)x);
    w16(a + 2, (uint16_t)y);
    if (frame >= 0) w8(a + 5, (uint8_t)frame);
}

/* player hang (Obj7F / Obj80): S2 Ctrl_1 / Ctrl_2 (held << 8 | pressed) */
static uint16_t pad(int n) { return r16(n ? 0xFFF606u : RAM_Ctrl_1); }

static void hang_grab(uint32_t o, uint32_t p, int dy)
{
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, 0);
    SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(p, OBJ_anim, ANI_HANG2);
    SET8(p, P_OBJCTRL, 1);
}

static int hang_can_grab(uint32_t p)
{
    if ((int8_t)OB8(p, P_OBJCTRL) < 0) return 0;
    if (OB8(p, OBJ_routine) >= 4) return 0;
    if (r16(RAM_Debug_placement_mode)) return 0;
    return 1;
}

/* ------------------------------------------------------------------------
 * Object 2A - stomper: rises slowly, slams down
 * ---------------------------------------------------------------------- */
#define SM_OFF   0x30                 /* objoff_30 (word) */
#define SM_Y0    0x32                 /* objoff_32 */

static void stomper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj2A_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_STOMPER, OB8(o, OBJ_render) | 4, 4, 0x10, 0x50);
        SET16(o, SM_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 0x50);
    }
    if (!OB8(o, OBJ_2nd)) {                             /* Obj2A_Main */
        SET16(o, SM_OFF, OB16(o, SM_OFF) + 1);
        if (OB16(o, SM_OFF) == 0x60) SET8(o, OBJ_2nd, 2);
    } else if (OB16(o, SM_OFF) > 8) {
        SET16(o, SM_OFF, OB16(o, SM_OFF) - 8);
    } else {
        SET16(o, SM_OFF, 0);
        SET8(o, OBJ_2nd, 0);
    }
    SET16(o, OBJ_y_pos, OB16(o, SM_Y0) - OB16(o, SM_OFF));
    o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x40, 0x41, OB16(o, OBJ_x_pos));
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 6A - moving crate (MCZ: always moving round a square; subtype
 * $18 brings two more along the same path)
 * ---------------------------------------------------------------------- */
#define CR_Y0    0x30                 /* objoff_30 */
#define CR_X0    0x32                 /* objoff_32 */
#define CR_TIME  0x34                 /* objoff_34 (word) */
#define CR_MOVE  0x36                 /* objoff_36 */
#define CR_IDX   0x38                 /* objoff_38 */
#define CR_3A    0x3A                 /* objoff_3A (word) */
#define CR_LAST  0x3C                 /* objoff_3C: standing bits last frame */
#define CR_TAB   0x3E                 /* objoff_2C: which path table */

static const int16_t cr_paths[3][15] = {
    { 0, 0x400, 0x10,  0x400, -0x200, 0x20,  0, 0x400, 0x10,  -0x400, -0x200, 0x20,  0, 0, 0 },   /* byte_27CDC */
    { 0, 0x100, 0x40,  -0x100, 0, 0x80,  0, -0x100, 0x40,  0x100, 0, 0x80,  0x100, 0, 0x40 },     /* byte_27CF4 */
    { 0, 0x100, 0x40,  0x100, 0, 0x80,  0, -0x100, 0x40,  -0x100, 0, 0x80,  -0x100, 0, 0x40 },    /* byte_27D12 */
};

static void crate_leg(uint32_t o)                    /* loc_27CA2 */
{
    const int16_t *t = cr_paths[OB8(o, CR_TAB)] + OB8(o, CR_IDX) / 2;
    SET16(o, OBJ_x_vel, t[0]);
    SET16(o, OBJ_y_vel, t[1]);
    SET16(o, CR_TIME, t[2]);
    SET16(o, CR_3A, 7);
    SET8(o, CR_MOVE, 0);
    SET8(o, CR_IDX, OB8(o, CR_IDX) + 6);
    if (OB8(o, CR_IDX) >= 6 * 4) SET8(o, CR_IDX, 0);
}

static void crate_sub(uint32_t o, uint32_t a1)       /* Obj6A_InitSubObject */
{
    SET32(a1, OBJ_code, S2(0x6A));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, CR_X0, OB16(o, OBJ_x_pos));
    SET16(a1, CR_Y0, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_status, OB8(o, OBJ_status));
}

static void crate(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj6A_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, tri_zone() == TRI_S2_MCZ ? S2A_CRATE : S2A_MTZBLOCK, OB8(o, OBJ_render) | 4, 4, 0x20, 0x20);
        SET8(o, OBJ_y_radius, 0x0C);
        SET8(o, CR_TAB, 0);
        SET8(o, OBJ_frame, 1);
        int link = 1;
        if (tri_zone() == TRI_S2_MCZ) {
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_y_radius, 0x20);
            SET8(o, CR_TAB, (OB8(o, OBJ_status) & ST_XFLIP) ? 2 : 1);
            SET8(o, OBJ_frame, 0);
            if (OB8(o, OBJ_subtype) != 0x18) {
                link = 0;                               /* bne loc_27BD0: keep the path origin */
            } else {
                int flip = (OB8(o, OBJ_status) & ST_XFLIP) != 0;
                uint32_t a1 = o_alloc_after(o);
                if (a1) {
                    crate_sub(o, a1);
                    SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + 0x40);
                    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x40);
                    SET8(a1, OBJ_subtype, flip ? 0x0C : 6);
                    a1 = o_alloc_after(o);
                    if (a1) {
                        crate_sub(o, a1);
                        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 0x40);
                        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x40);
                        SET8(a1, OBJ_subtype, flip ? 6 : 0x0C);
                    }
                }
            }
        }
        if (link) {                                     /* loc_27BC4 */
            SET16(o, CR_X0, OB16(o, OBJ_x_pos));
            SET16(o, CR_Y0, OB16(o, OBJ_y_pos));
        }
        SET8(o, CR_IDX, OB8(o, OBJ_subtype));
        crate_leg(o);
        return;
    }
    case 2: {                                           /* loc_27BDE: moves once stepped off */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        if (OB16(o, CR_MOVE)) {
            o_move(o);
            SET16(o, CR_TIME, OB16(o, CR_TIME) - 1);
            if (!OB16(o, CR_TIME)) crate_leg(o);
        } else {
            uint8_t d1 = OB8(o, CR_LAST), d0 = OB8(o, OBJ_status);
            int done = 0;
            if (d0 & ST_P1_STAND) SET8(o, CR_LAST, d0);
            else if (d1 & ST_P1_STAND) { SET8(o, CR_MOVE, 1); SET8(o, CR_LAST, 0); done = 1; }
            if (!done) {
                if (d0 & ST_P2_STAND) SET8(o, CR_LAST, d0);
                else if (d1 & ST_P2_STAND) { SET8(o, CR_MOVE, 1); SET8(o, CR_LAST, 0); }
            }
        }
        if (on_screen(o)) {
            int d2 = OB8(o, OBJ_y_radius);
            o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
        }
        gone_x(o, OB16(o, CR_X0));
        return;
    }
    default: {                                          /* loc_27C66: MCZ, always moving */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        o_move(o);
        SET16(o, CR_TIME, OB16(o, CR_TIME) - 1);
        if (!OB16(o, CR_TIME)) crate_leg(o);
        if (on_screen(o)) {
            int d2 = OB8(o, OBJ_y_radius);
            o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
        }
        gone_x(o, OB16(o, CR_X0));
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 75 - MCZ brick ($xF) / spike ball circling on a chain
 * ---------------------------------------------------------------------- */
#define BR_X0    0x30                 /* objoff_30 */
#define BR_Y0    0x32                 /* objoff_32 */
#define BR_SPEED 0x34                 /* objoff_34 (word) */
#define BR_CHAIN 0x3C                 /* objoff_3C (long) */

static void brick(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the chain */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj75_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_MCZBRICK, 4, 5, 0x10, 0x10);
        SET16(o, BR_X0, OB16(o, OBJ_x_pos));
        SET16(o, BR_Y0, OB16(o, OBJ_y_pos));
        uint8_t sub = OB8(o, OBJ_subtype);
        int n = sub & 0x0F;
        SET16(o, BR_SPEED, (uint16_t)((int16_t)(int8_t)(sub & 0xF0) << 3));
        uint8_t st = OB8(o, OBJ_status);                /* ror.b #2; andi #$C0 */
        SET8(o, OBJ_angle, (uint8_t)(((st & 1) << 6) | ((st & 2) << 6)));
        if (n == 0x0F) {
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_priority, 4 << 7);
            SET8(o, OBJ_frame, 2);
            return;
        }
        SET8(o, OBJ_coll_flags, 0x9A);
        uint32_t ch = chain_new(o, S2A_MCZBRICK, 0x40, 0x40, n);
        if (ch) {
            for (int k = 0; k < n; k++) chain_child(ch, k, (int16_t)OB16(o, OBJ_x_pos), (int16_t)OB16(o, OBJ_y_pos), 1);
            SET16(ch, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(ch, OBJ_y_pos, OB16(o, OBJ_y_pos));
        }
        SET32(o, BR_CHAIN, ch);
    }   /* fall through */
    case 2: {                                           /* Obj75_Main */
        SET16(o, OBJ_angle, OB16(o, OBJ_angle) + OB16(o, BR_SPEED));
        int16_t s, c;
        o_sincos(OB8(o, OBJ_angle), &s, &c);
        int16_t y0 = (int16_t)OB16(o, BR_Y0), x0 = (int16_t)OB16(o, BR_X0);
        uint32_t ch = r32(o + BR_CHAIN) & 0xFFFFFF;
        if (ch) {
            int n = OB16(ch, MS_COUNT);
            int32_t ds = (int32_t)s << 12, dc = (int32_t)c << 12, y = 0, x = 0;
            for (int k = 0; k < n; k++) {
                chain_child(ch, k, (int16_t)(x0 + (x >> 16)), (int16_t)(y0 + (y >> 16)), -1);
                y += ds; x += dc;
            }
            SET16(o, OBJ_x_pos, (uint16_t)(x0 + (x >> 16)));
            SET16(o, OBJ_y_pos, (uint16_t)(y0 + (y >> 16)));
            uint32_t s6 = ch + MS_SUB + 4u * 6u;        /* sub6 */
            SET16(ch, OBJ_x_pos, r16(s6));
            SET16(ch, OBJ_y_pos, r16(s6 + 2));
        }
        if ((uint16_t)((OB16(o, BR_X0) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80)) > 0x280) {
            if (ch) o_delete(ch);
            o_delete_respawnable(o);
            return;
        }
        o_touch(o);
        o_draw(o);
        return;
    }
    default:                                            /* loc_28D6C: the brick */
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        gone(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 76 - spike block that slides out of the wall when Sonic is near
 * ---------------------------------------------------------------------- */
#define SS_Y0    0x30                 /* objoff_30 */
#define SS_X0    0x34                 /* objoff_34 */
#define SS_LEFT  0x36                 /* sliding_spikes_remaining_movement (word) */

static void slide_check(uint32_t o, uint32_t p)      /* Obj76_CheckPlayer */
{
    if (OB8(p, OBJ_status) & ST_INAIR) return;
    uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0xC0);
    if (OB8(o, OBJ_status) & ST_XFLIP) d0 = (uint16_t)(d0 - 0x100);
    if (d0 >= 0x80) return;
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x10) >= 0x20) return;
    SET8(o, OBJ_subtype, 2);
    SET16(o, SS_LEFT, 0x80);
}

static void slide_spikes(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj76_Init (Obj76_InitData entry 0) */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SLIDESPIKE, OB8(o, OBJ_render) | 4, 4, 0x40, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_frame, 0);
        SET16(o, SS_X0, OB16(o, OBJ_x_pos));
        SET16(o, SS_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);                 /* Obj76_Main */
    if (OB8(o, OBJ_subtype) == 0) {
        slide_check(o, RAM_Player_1);
        slide_check(o, RAM_Player_2);
    } else if (OB16(o, SS_LEFT)) {                      /* Obj76_SlideOut */
        SET16(o, SS_LEFT, OB16(o, SS_LEFT) - 1);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_status) & ST_XFLIP) ? 1 : -1));
    }
    if (on_screen(o)) {
        int d2 = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
        uint32_t side = (g_o_d6 >> 16) & 3;
        if (side & 1) { o_hurt_player(RAM_Player_1, o); SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH); }
        if (side & 2) { o_hurt_player(RAM_Player_2, o); SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P2_PUSH); }
    }
    gone_x(o, OB16(o, SS_X0));
}

/* ------------------------------------------------------------------------
 * Object 77 - MCZ gate: swings open when its vine is pulled
 * ---------------------------------------------------------------------- */
#define GT_DONE  0x34                 /* objoff_34 */

static void gate(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_GATELOG, OB8(o, OBJ_render) | 4, 4, 0x80, 0x20);
    }
    if (!OB8(o, GT_DONE) && (r8(trigger(OB8(o, OBJ_subtype))) & 1)) {
        SET8(o, GT_DONE, 1);
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
        if (on_screen(o)) o_sfx_s2(SFX_DOOR_CLOSE);
    }
    o_animate_s2(o, o_asset_anim(S2A_GATELOG));
    if (!OB8(o, OBJ_frame)) {
        o_solid(o, 0x4B, 8, 9, OB16(o, OBJ_x_pos));
        gone(o);
        return;
    }
    uint8_t st = OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND);   /* Obj77_DropCharacters */
    if (st) {
        if (st & ST_P1_STAND) SET8(RAM_Player_1, OBJ_status, OB8(RAM_Player_1, OBJ_status) & ~0x08);
        if (st & ST_P2_STAND) SET8(RAM_Player_2, OBJ_status, OB8(RAM_Player_2, OBJ_status) & ~0x08);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 7F - vine switch: hang from it to set its trigger
 * ---------------------------------------------------------------------- */
#define VS_HOLD  0x30                 /* objoff_30/31: hanging (P1/P2) */
#define VS_TIME  0x32                 /* objoff_32/33: regrab delay */

static void vine_switch_frame(uint32_t o) { SET8(o, OBJ_frame, OB16(o, VS_HOLD) ? 1 : 0); }

static void vine_switch_act(uint32_t o, int n)       /* Obj7F_Action */
{
    uint32_t p = o_player(n), a2 = o + VS_HOLD + (uint32_t)n;
    uint16_t d0 = pad(n);
    if (r8(a2)) {
        if (!(d0 & BTN_ABC)) return;
        SET8(p, P_OBJCTRL, 0);
        w8(a2, 0);
        w8(a2 + 2, 18);
        if ((d0 >> 8) & BTN_DIRS) w8(a2 + 2, 60);
        SET16(p, OBJ_y_vel, (uint16_t)-0x300);
        w8(trigger(OB8(o, OBJ_subtype) & 0x0F), r8(trigger(OB8(o, OBJ_subtype) & 0x0F)) & ~1);
        vine_switch_frame(o);
        return;
    }
    if (r8(a2 + 2)) {
        w8(a2 + 2, r8(a2 + 2) - 1);
        if (r8(a2 + 2)) return;
    }
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x0C) >= 0x18) return;
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - 0x28) >= 0x10) return;
    if (!hang_can_grab(p)) return;
    hang_grab(o, p, 0x30);
    w8(a2, 1);
    w8(trigger(OB8(o, OBJ_subtype) & 0x0F), r8(trigger(OB8(o, OBJ_subtype) & 0x0F)) | 1);
    o_sfx_s2(SFX_PULLEY_GRAB);
    vine_switch_frame(o);
}

static void vine_switch(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_VINESWITCH, 4, 4, 8, 0x30);
    }
    vine_switch_act(o, 0);
    vine_switch_act(o, 1);
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 80 - MCZ vine pulley: lowers while hung from (subtype < 0: pulls
 * a trigger; subtype $x0 bits $70: starts down and rises while held)
 * ---------------------------------------------------------------------- */
#define VN_MAX   0x3E                 /* objoff_2E (word) */
#define VN_HOLD  0x30                 /* objoff_30/31 */
#define VN_TIME  0x32                 /* objoff_32/33 */
#define VN_TRIG  0x34                 /* objoff_34 */
#define VN_INV   0x36                 /* objoff_36 */
#define VN_OFF   0x38                 /* objoff_38 (word) */
#define VN_SPEED 0x3A                 /* objoff_3A (word) */
#define VN_Y0    0x3C                 /* objoff_3C */

static void vine_act(uint32_t o, int n)               /* Obj80_Action */
{
    uint32_t p = o_player(n), a2 = o + VN_HOLD + (uint32_t)n;
    uint16_t d0 = pad(n);
    if (r8(a2)) {
        if (!on_screen(p) || OB8(p, OBJ_routine) >= 4) {   /* loc_29B42 */
            SET8(p, P_OBJCTRL, 0);
            w8(a2, 0);
            w8(a2 + 2, 60);
            return;
        }
        if (!(d0 & BTN_ABC)) {                          /* loc_29B50: hold on */
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x94);
            return;
        }
        SET8(p, P_OBJCTRL, 0);
        w8(a2, 0);
        w8(a2 + 2, 18);
        uint16_t held = (uint16_t)(d0 & (BTN_DIRS << 8));
        if (held) w8(a2 + 2, 60);
        if (held & (BTN_LEFT << 8)) SET16(p, OBJ_x_vel, (uint16_t)-0x200);
        if (held & (BTN_RIGHT << 8)) SET16(p, OBJ_x_vel, 0x200);
        SET16(p, OBJ_y_vel, (uint16_t)-0x380);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_INAIR);
        if (OB8(o, VN_TRIG))
            w8(trigger(OB8(o, OBJ_subtype) & 0x0F), r8(trigger(OB8(o, OBJ_subtype) & 0x0F)) & ~1);
        return;
    }
    if (r8(a2 + 2)) {                                   /* loc_29B5E */
        w8(a2 + 2, r8(a2 + 2) - 1);
        if (r8(a2 + 2)) return;
    }
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x10) >= 0x20) return;
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) - 0x88) >= 0x18) return;
    if (!hang_can_grab(p)) return;
    hang_grab(o, p, 0x94);
    w8(a2, 1);
    if (OB8(o, VN_TRIG)) {
        w8(trigger(OB8(o, OBJ_subtype) & 0x0F), r8(trigger(OB8(o, OBJ_subtype) & 0x0F)) | 1);
        o_sfx_s2(SFX_PULLEY_GRAB);
    }
}

static void vine(uint32_t o)
{
    int wfz = tri_zone() == TRI_S2_WFZ, sh = wfz ? 4 : 5;  /* WFZ's hook: frames every 16 px */
    if (!OB8(o, OBJ_routine)) {                         /* Obj80_Init / Obj80_MCZ_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, wfz ? S2A_WFZHOOK : S2A_VINE, 4, 4, 0x10, 0x80);
        SET8(o, OBJ_y_radius, 0x80);
        SET16(o, VN_Y0, OB16(o, OBJ_y_pos));
        uint8_t sub = OB8(o, OBJ_subtype);
        SET16(o, VN_MAX, wfz ? ((sub & 0x0F) ? 0x60 : 0xA0) : 0xB0);
        if ((sub & 0x80) && !wfz) SET8(o, VN_TRIG, 1);
        SET16(o, VN_SPEED, 2);
        if (sub & 0x70) {
            uint16_t d = OB16(o, VN_MAX);
            SET16(o, VN_OFF, d);
            SET8(o, VN_INV, 1);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d);
            SET8(o, OBJ_frame, (d >> sh) + 1);
        }
    }
    int held = OB16(o, VN_HOLD) != 0;                   /* Obj80_MCZ_Main */
    int down = OB8(o, VN_INV) ? !held : held;
    uint16_t d2 = OB16(o, VN_OFF);
    int moved = 0;
    if (down) {
        if (d2 != OB16(o, VN_MAX)) { d2 = (uint16_t)(d2 + OB16(o, VN_SPEED)); moved = 1; }
    } else if (d2) {
        d2 = (uint16_t)(d2 - OB16(o, VN_SPEED));
        moved = 1;
    }
    if (moved) {
        SET16(o, VN_OFF, d2);
        SET16(o, OBJ_y_pos, OB16(o, VN_Y0) + d2);
        SET8(o, OBJ_frame, d2 ? (d2 >> sh) + 1 : 0);
    }
    vine_act(o, 0);
    vine_act(o, 1);
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 81 - MCZ drawbridge: a log column that falls flat when its
 * trigger is set
 * ---------------------------------------------------------------------- */
#define DB_X0    0x30                 /* objoff_30 */
#define DB_Y0    0x32                 /* objoff_32 */
#define DB_SPEED 0x34                 /* objoff_34 (word) */
#define DB_MOVE  0x36                 /* objoff_36 */
#define DB_CHAIN 0x3C                 /* objoff_3C (long) */

static void drawbridge_links(uint32_t o)             /* loc_2A1EA */
{
    if (!OB8(o, DB_MOVE)) return;
    uint32_t ch = r32(o + DB_CHAIN) & 0xFFFFFF;
    if (!ch) return;
    int16_t s, c;
    o_sincos(OB8(o, OBJ_angle), &s, &c);
    int16_t y0 = (int16_t)OB16(o, DB_Y0), x0 = (int16_t)OB16(o, DB_X0);
    int32_t ds = (int32_t)s << 12, dc = (int32_t)c << 12, y = ds, x = dc;
    int n = OB16(ch, MS_COUNT);
    for (int k = 0; k < n; k++) {
        chain_child(ch, k, (int16_t)(x0 + (x >> 16)), (int16_t)(y0 + (y >> 16)), -1);
        y += ds; x += dc;
    }
    uint32_t s6 = ch + MS_SUB + 4u * 6u;
    SET16(ch, OBJ_x_pos, r16(s6));
    SET16(ch, OBJ_y_pos, r16(s6 + 2));
}

static void drawbridge(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the logs */
    if (!OB8(o, OBJ_routine)) {                         /* Obj81_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_DRAWBRIDGE, 4, 5, 8, 0x40);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);    /* no balancing */
        SET16(o, DB_X0, OB16(o, OBJ_x_pos));
        SET16(o, DB_Y0, OB16(o, OBJ_y_pos));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x48);
        SET8(o, OBJ_angle, 0xC0);
        int d4 = -0x10;
        if (OB8(o, OBJ_status) & ST_YFLIP) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x90);
            SET8(o, OBJ_angle, 0x40);
            d4 = 0x10;
        }
        SET16(o, DB_SPEED, (OB8(o, OBJ_status) & ST_XFLIP) ? (uint16_t)-0x100 : 0x100);
        uint32_t ch = chain_new(o, S2A_DRAWBRIDGE, 0x40, 0x40, 8);
        if (ch) {
            int16_t x = (int16_t)OB16(o, DB_X0), y = (int16_t)OB16(o, DB_Y0);
            for (int k = 0; k < 8; k++) { y = (int16_t)(y + d4); chain_child(ch, k, x, y, 1); }
            uint32_t s6 = ch + MS_SUB + 4u * 6u;
            SET16(ch, OBJ_x_pos, r16(s6));
            SET16(ch, OBJ_y_pos, r16(s6 + 2));
        }
        SET32(o, DB_CHAIN, ch);
    }
    if (OB8(o, OBJ_routine) == 2) {                     /* Obj81_BridgeUp */
        if ((r8(trigger(OB8(o, OBJ_subtype))) & 1) && !OB8(o, DB_MOVE)) {
            SET8(o, DB_MOVE, 1);
            o_sfx_s2(SFX_DOOR_MOVE);
            if (OB8(o, OBJ_status) == (0x80 | ST_XFLIP))
                SET16(o, OBJ_x_pos, OB16(o, DB_X0) - 0x48);
        }
        if (OB8(o, DB_MOVE)) {
            int d1 = 0x48, flat = 0;
            if (OB8(o, OBJ_angle) == 0) flat = 1;
            else if (OB8(o, OBJ_angle) == 0x80) { d1 = -0x48; flat = 1; }
            if (flat) {                                 /* loc_2A154: down */
                SET16(o, OBJ_y_pos, OB16(o, DB_Y0));
                SET16(o, OBJ_x_pos, OB16(o, DB_X0) + d1);
                SET8(o, OBJ_width_px, 0x40);
                SET8(o, DB_MOVE, 0);
                o_sfx_s2(SFX_THUMP);
                SET8(o, OBJ_routine, 4);
            } else {
                SET16(o, OBJ_angle, OB16(o, OBJ_angle) + OB16(o, DB_SPEED));
            }
        }
        drawbridge_links(o);
    }
    int d1 = 0x13, d2 = 0x40, d3 = 0x41;                /* loc_2A18A */
    uint8_t a = OB8(o, OBJ_angle);
    if (a == 0 || (a != 0x40 && a < 0xC0)) { d1 = 0x4B; d2 = 8; d3 = 9; }
    o_solid(o, d1, d2, d3, OB16(o, OBJ_x_pos));
    if ((uint16_t)((OB16(o, DB_X0) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80)) > 0x280) {
        uint32_t ch = r32(o + DB_CHAIN) & 0xFFFFFF;
        if (ch) o_delete(ch);
        o_delete_respawnable(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 9E - Crawlton: lunges its head at Sonic, the body follows
 * ---------------------------------------------------------------------- */
#define CW_TIME  0x3A                 /* objoff_3A */
#define CW_NEXT  0x39                 /* objoff_39 */
#define CW_RT    0x3B                 /* objoff_3B: routine */
#define CW_PARENT 0x42                /* parent (the body: past its seven children) */

static void crawlton_body(uint32_t o)                /* loc_37EFC */
{
    uint32_t a1 = 0xFF0000u | OB16(o, CW_PARENT);
    if (r32(a1) != S2(0x9E)) { o_delete(o); return; }
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (OB8(a1, OBJ_render) & 1));
    SET8(o, OBJ_height_px, 0x80);
    SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos));
    if (OB8(a1, CW_RT) == 6) {
        int16_t d2 = (int16_t)OB16(a1, OBJ_x_vel) >> 8, d3 = (int16_t)OB16(a1, OBJ_y_vel) >> 8;
        uint8_t d0 = OB8(a1, CW_TIME);
        int d1 = 0x18;
        for (int k = 0; k < 7; k++) {
            uint32_t c = o + MS_SUB + (uint32_t)k * 6u;
            if (d0 < d1) {
                w16(c, (uint16_t)(r16(c) + d2));
                w16(c + 2, (uint16_t)(r16(c + 2) + d3));
            }
            d1 -= 4;
            if (d1 < 0) break;
        }
    }
    o_draw(o);
}

static void crawlton(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { crawlton_body(o); return; }
    switch (OB8(o, CW_RT)) {
    case 0: {                                           /* Obj9E_Init (LoadSubObject) */
        o_init(o, S2A_CRAWLTON, OB8(o, OBJ_render) | 4, 4, 0x80, 0x80);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 0x80);
        SET8(o, CW_RT, 2);
        uint32_t a1 = o_alloc();                        /* loc_37F74: the body */
        if (a1) {
            SET32(a1, OBJ_code, S2(0x9E));
            o_asset(a1, S2A_CRAWLTON);
            SET8(a1, OBJ_render, OB8(o, OBJ_render) | 0x40);
            SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
            SET8(a1, OBJ_width_px, 0x80);
            SET8(a1, OBJ_height_px, 0x80);
            SET16(a1, MS_COUNT, 7);
            SET16(a1, CW_PARENT, (uint16_t)o);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            for (int k = 0; k < 7; k++)
                chain_child(a1, k, (int16_t)OB16(o, OBJ_x_pos), (int16_t)OB16(o, OBJ_y_pos), 2);
        }
        return;
    }
    case 2: {                                           /* loc_37E42: watch */
        int d0;
        int16_t d2, d3;
        s2_orient(o, &d0, NULL, &d2, &d3);
        if ((uint16_t)(d2 + 0x80) < 0x100 && (uint16_t)(d3 + 0x80) < 0x100) {
            SET8(o, CW_RT, 4);                          /* loc_37E62: aim */
            SET8(o, CW_TIME, 0x10);
            SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (d0 ? 1 : 0));
            SET16(o, OBJ_x_vel, (uint16_t)(((int16_t)-d2 << 3) & 0xFF00));
            SET16(o, OBJ_y_vel, (uint16_t)(((int16_t)-d3 << 3) & 0xFF00));
        }
        break;
    }
    case 4:                                             /* loc_37E98 */
        SET8(o, CW_TIME, OB8(o, CW_TIME) - 1);
        if ((int8_t)OB8(o, CW_TIME) < 0) {
            SET8(o, CW_RT, 6);
            SET8(o, CW_NEXT, 8);
            SET8(o, CW_TIME, 0x1C);
        }
        break;
    case 6:                                             /* loc_37EB6: lunge / return */
        SET8(o, CW_TIME, OB8(o, CW_TIME) - 1);
        if (OB8(o, CW_TIME)) { o_move(o); break; }
        SET8(o, CW_RT, OB8(o, CW_NEXT));
        SET8(o, CW_TIME, 0x20);
        break;
    default:                                            /* loc_37ED4: rest, then back */
        SET8(o, CW_TIME, OB8(o, CW_TIME) - 1);
        if (OB8(o, CW_TIME)) break;
        SET8(o, CW_RT, 6);
        SET8(o, CW_NEXT, 2);
        SET8(o, CW_TIME, 0x1C);
        SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)OB16(o, OBJ_y_vel));
        break;
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object A3 - Flasher: drifts on a set course, then lights up (harmful)
 * ---------------------------------------------------------------------- */
#define FL_T     0x30                 /* objoff_30 (word) */
#define FL_COUNT 0x3A                 /* objoff_2A (word) */
#define FL_IDX   0x3C                 /* objoff_2C (word) */
#define FL_ACC   0x3E                 /* objoff_2E (word) */

/* word_38810 then byte_38820, as the ROM lays them out */
static const uint8_t fl_course[34] = {
    0x01, 0x00, 0x01, 0xA0, 0x02, 0x08, 0x02, 0x85, 0x03, 0x00, 0x03, 0x40, 0x03, 0x90, 0x04, 0x40,
    0xF0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1,
};

static void fl_clear_anim(uint32_t o)                /* clr.l mapping_frame; clr.w anim_frame_duration */
{
    SET8(o, OBJ_frame, 0);
    SET8(o, OBJ_anim_frame, 0);
    SET8(o, OBJ_anim, 0);
    SET8(o, OBJ_prev_anim, 0);
    SET8(o, OBJ_anim_timer, 0);
}

static void flasher(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* LoadSubObject */
        o_init(o, S2A_FLASHER, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 6);
        SET8(o, OBJ_routine, 2);
        SET16(o, FL_COUNT, 0x40);
        return;
    case 2:                                             /* loc_38766 */
        SET16(o, FL_COUNT, OB16(o, FL_COUNT) - 1);
        if ((int16_t)OB16(o, FL_COUNT) < 0) {
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_x_vel, (uint16_t)-0x100);
            SET16(o, OBJ_y_vel, 0x40);
            SET16(o, FL_ACC, 2);
            SET16(o, FL_COUNT, 0);
            SET16(o, FL_T, 0x80);
        }
        break;
    case 4: {                                           /* loc_38794: drift */
        SET16(o, FL_T, OB16(o, FL_T) - 1);
        if ((int16_t)OB16(o, FL_T) < 0) {               /* loc_387FC: light up */
            SET8(o, OBJ_routine, 6);
            SET16(o, FL_T, 0x80);
            SET8(o, OBJ_coll_flags, OB8(o, OBJ_coll_flags) | 0x80);
            break;
        }
        uint16_t d0 = OB16(o, FL_COUNT);
        if ((int16_t)d0 < 0) { o_delete(o); return; }
        int right = (int16_t)OB16(o, OBJ_x_vel) >= 0;
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | right);
        SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | right);
        d0++;
        SET16(o, FL_COUNT, d0);
        uint16_t d1 = OB16(o, FL_IDX);
        if (d1 > 32) d1 = 32;                           /* (the count runs negative first) */
        uint16_t lim = (uint16_t)((fl_course[d1] << 8) | fl_course[d1 + 1]);
        if (d0 >= lim) {
            d1 = (uint16_t)(d1 + 2);
            SET16(o, FL_IDX, d1);
            const uint8_t *a1 = fl_course + 16 + (d1 > 16 ? 16 : d1);
            if (a1[0]) SET16(o, FL_ACC, (uint16_t)-(int16_t)OB16(o, FL_ACC));
            if (a1[1]) SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)OB16(o, OBJ_y_vel));
        }
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, FL_ACC));
        o_move(o);
        break;
    }
    case 6: {                                           /* loc_38832: flicker on */
        uint8_t rt = OB8(o, OBJ_routine);
        o_animate_s2(o, o_asset_anim(S2A_FLASHER));
        if (OB8(o, OBJ_routine) != rt) { fl_clear_anim(o); SET8(o, OBJ_frame, 3); }
        break;
    }
    case 8:                                             /* loc_3885C: lit */
        SET16(o, FL_T, OB16(o, FL_T) - 1);
        if ((int16_t)OB16(o, FL_T) < 0) { SET8(o, OBJ_routine, 0x0A); fl_clear_anim(o); break; }
        o_animate_s2(o, o_asset_anim(S2A_FLASHERB));
        break;
    case 0x0A:                                          /* loc_38880: dims */
        o_animate_s2(o, o_asset_anim(S2A_FLASHERC));
        break;
    default:                                            /* loc_3888E */
        SET8(o, OBJ_routine, 4);
        SET16(o, FL_T, 0x80);
        SET8(o, OBJ_coll_flags, OB8(o, OBJ_coll_flags) & 0x7F);
        fl_clear_anim(o);
        break;
    }
    o_touch(o);
    o_remember(o);
}

void s2_obj_mcz_register(TriObjFn *t)
{
    t[0x2A] = stomper;
    t[0x6A] = crate;
    t[0x75] = brick;
    t[0x76] = slide_spikes;
    t[0x77] = gate;
    t[0x7F] = vine_switch;
    t[0x80] = vine;
    t[0x81] = drawbridge;
    t[0x9E] = crawlton;
    t[0xA3] = flasher;
}
