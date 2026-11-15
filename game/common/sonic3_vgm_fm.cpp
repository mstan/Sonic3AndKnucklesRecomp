/* Independent music chip. Reuse the qualified wrapper without touching the
 * cartridge/SFX chip or copying a second synthesis implementation. */
#define ym2612_init s3_vgm_fm_init
#define ym2612_advance s3_vgm_fm_advance
#define ym2612_write s3_vgm_fm_write
#define ym2612_render s3_vgm_fm_render
#define ym2612_render_pair s3_vgm_fm_render_pair
#define ym2612_set_audition_channels s3_vgm_fm_mask
#define ym2612_peek_audition s3_vgm_fm_peek
#define ym2612_samples_available s3_vgm_fm_available
#define ym2612_sample_rate s3_vgm_fm_rate
#define ym2612_save_state s3_vgm_fm_save
#define ym2612_load_state s3_vgm_fm_load
#define ym2612_rb_save s3_vgm_fm_rb_save
#define ym2612_rb_load s3_vgm_fm_rb_load
#define ym2612_cosim_hash s3_vgm_fm_hash
#include "../../segagenesisrecomp/runner/audio/ym2612_ymfm.cpp"
