#pragma once
#include <stdint.h>
struct RecompLauncherCModProvider;
typedef struct {
    int infinite_lives,infinite_time;
    int easy_level_select,debug_mode;
    int checkpoint_bonus;
    int shield_transform;
    int tails_ring_follow;
    int competition_title_cards;
    int original_zone_order;
    int early_zone_names;
} S3GameplayConfig;
extern S3GameplayConfig s3_gameplay;
void s3_gameplay_load(const char *settings_path,const char *mode);
int s3_gameplay_save(void);
int s3_gameplay_netplay_allowed(void);
void s3_gameplay_apply(uint8_t ram[65536]);
/* All private gameplay hook sites; the renderer must not consume them. */
int s3_gameplay_owns(uint32_t pc);
/* Opt-in unused Competition card; return its original loader continuation. */
uint32_t s3_gameplay_title_card(uint32_t pc,uint8_t ram[65536],uint32_t actor);
int s3_gameplay_title_card_sprite(uint32_t pc,const uint8_t ram[65536],uint32_t actor);
/* Keep the HCZ1 fixed splash slot out of ordinary native allocation. */
void s3_gameplay_allocation_hook(uint32_t pc,const uint8_t ram[65536],
                                 uint32_t candidate,uint16_t *sr);
/* Unconditional native Nemesis FIFO correctness repair at its audited RTS. */
void s3_gameplay_nemesis_pop(uint32_t pc,uint8_t ram[65536],uint16_t *sr);
/* Complete retail's byte-sized palette-index reset after depowering. */
void s3_gameplay_palette_reset(uint32_t pc,uint8_t ram[65536]);
/* Keep original SRAM zone codes while changing the three campaign links. */
void s3_gameplay_zone_order(uint32_t pc,const uint8_t ram[65536],uint32_t *d0);
uint32_t s3_gameplay_zone_departure(uint32_t pc,uint8_t ram[65536],uint32_t actor);
/* Audited timer branch/formatter sites; only their consumed CCR bits or
 * display argument change. Native timer/result/death routines still execute. */
void s3_gameplay_timer_hook(uint32_t pc,const uint8_t ram[65536],
                           uint32_t *d1,uint16_t *sr);
/* Audited native Sonic elemental BEQ sites. When true, set only CCR.Z before
 * executing the original branch, eventually reaching native CheckTransform.
 * Return0 from the instruction hook in both cases: no routine/stack is replaced.
 * The caller must exclude alternate character implementations. */
int s3_gameplay_skip_shield(uint32_t pc,const uint8_t ram[65536],
                            uint32_t actor,int native_sonic);
const struct RecompLauncherCModProvider *s3_gameplay_mods(
    const struct RecompLauncherCModProvider *base);
