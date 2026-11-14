/* Separate PSG state for imported music; the game's native SFX stay intact. */
#define psg_init s3_vgm_psg_init
#define psg_advance s3_vgm_psg_advance
#define psg_write s3_vgm_psg_write
#define psg_render s3_vgm_psg_render
#define psg_render_pair s3_vgm_psg_render_pair
#define psg_set_audition_channels s3_vgm_psg_mask
#define psg_peek_audition s3_vgm_psg_peek
#define psg_samples_available s3_vgm_psg_available
#define psg_sample_rate s3_vgm_psg_rate
#define psg_reset_leftover s3_vgm_psg_reset_leftover
#define psg_save_state s3_vgm_psg_save
#define psg_load_state s3_vgm_psg_load
#define psg_rb_save s3_vgm_psg_rb_save
#define psg_rb_load s3_vgm_psg_rb_load
#define psg_cosim_dump s3_vgm_psg_dump
#define psg_cosim_hash s3_vgm_psg_hash
#include "../../segagenesisrecomp/runner/audio/sn76489.c"
