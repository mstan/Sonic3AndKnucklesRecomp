/*
 * trilogy_titlecard.c — Sonic 3 title cards for Sonic 1 / Sonic 2 zones.
 *
 * The retail card objects run unchanged; the zone-name letters and the name
 * mapping frame come from the bank (glyphs cut from S3K's own card font).
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"

static uint32_t act3_numeral(void)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + TRI_BLOB_ACT3_NUMERAL * 4u) : 0;
}

int tri_titlecard_hook(uint32_t pc)
{
    switch (pc) {
    case TH_TC_LEVELGFX: {          /* after movea.l (a1,d0.w),a1 — letter KosM */
        if (!tri_dir()) return 0;
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_TITLEART);
        uint32_t a = t ? tri_rd32(t + (uint32_t)(r8(RAM_Apparent_zone) - TRI_ZONE_BASE) * 4u) : 0;
        if (a) A(1) = a;
        return 0;
    }
    case TH_TC_BONUS:               /* bhs.s — keep the full card, not the bonus one */
        if (tri_dir()) g_cpu.SR |= 1u;
        return 0;
    case TH_TC_NAME: {              /* after add.b d0,mapping_frame(a0) */
        if (!tri_dir()) return 0;
        uint32_t m = tri_rd32(TRI_BANK_BASE + TB_TITLEMAP);
        if (!m) return 0;
        uint32_t a0 = A(0);
        w8(a0 + OBJ_frame, (uint8_t)(r8(RAM_Apparent_zone) - TRI_ZONE_BASE + 1));
        w32(a0 + OBJ_mappings, m);
        return 0;
    }
    case TH_TC_ACTNUM:              /* a1 = digit KosM for $53D */
        if (tri_dir() && r8(RAM_Apparent_act) == 2 && act3_numeral())
            A(1) = act3_numeral();
        return 0;
    case TH_RESULTS_ACTNUM:         /* move.w d0,subtype(a0) ; a1 = digit KosM for $568 */
        {
            extern void tri_ext_tiles(uint32_t tile, uint32_t n, int on);
            tri_ext_tiles(0x520, 0x90, 0);          /* the results art takes $520-$5AF: S3K's own colours */
        }
        if (tri_dir() && r8(RAM_Apparent_act) == 2 && act3_numeral()) {
            A(1) = act3_numeral();
            D(0) = 0xFFFFFFFFu;       /* as the 2 */
        }
        return 0;
    case TH_TC_ACT:                 /* Final Zone: no act number (as Sky Sanctuary) */
        if (tri_dir() && r8(RAM_Apparent_zone) == 0x26) {
            uint32_t a0 = A(0) & 0xFFFFFF;
            uint32_t p = 0xFF0000u | r16(a0 + 0x48);          /* parent2 */
            w16(p + 0x30, (uint16_t)(r16(p + 0x30) - 1));
            tri_jsr(S3K_Delete_Current_Sprite);
            return 1;
        }
        return 0;
    case TH_TC_DONE: {              /* card finished: S3K loads enemy art here */
        extern void tri_obj_titlecard_done(void);
        if (!tri_dir()) return 0;
        /* loc_2D8AE..loc_2D8CA, which zones >= $13 skip: reload spikes and
         * springs, then the explosion + animal art (unless an in-level card) */
        if (!r16((A(0) & 0xFFFFFF) + 0x3E)) {
            uint32_t a0 = A(0);
            A(1) = S3K_PLC_SpikesSprings;
            tri_jsr(S3K_Load_PLC_Raw);
            tri_jsr(S3K_PLCLoad_AnimalsAndExplosion);
            if (r8(RAM_Water_flag)) {              /* breathing bubbles, air countdown */
                uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
                A(1) = tri_rd32(t + TRI_BLOB_BUBBLES_PLC * 4u);
                tri_jsr(S3K_Load_PLC_Raw);
            }
            A(0) = a0;
        }
        tri_obj_titlecard_done();
        return 0;
    }
    default:
        return 0;
    }
}
