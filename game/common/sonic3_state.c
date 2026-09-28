/* Save only at the native gameplay Wait_VSync boundary. Restoring its
 * per-frame loop avoids the stage initialization in GameLoop. */
#include "sonic3_state.h"
#include "sonic3_state_io.h"
#include "genesis_runtime.h"
#include "audio/event_queue.h"

/* Verified against the byte-matched pinned skdisasm listings:
 * LevelLoop / instruction after its BSR Wait_VSync / Pause_Loop's return.
 * All resume targets are existing discovered labels, not generated-C edits. */
#ifdef SONIC3_STANDALONE
enum { LEVEL_LOOP = 0x4B0C, LEVEL_WAIT_RETURN = 0x4B20, PAUSE_WAIT_RETURN = 0x16C0 };
#else
enum { LEVEL_LOOP = 0x650C, LEVEL_WAIT_RETURN = 0x6520, PAUSE_WAIT_RETURN = 0x143A };
#endif

static void state(S3StateIO *io)
{
    unsigned version = 1;
    if (s3_state_peek_unsigned(io, version) != version) io->ok = 0;
    S3_STATE(io, version);
#ifndef SONIC3_STANDALONE
    s3_army_state(io);
#endif
    s3_video_state(io);
    size_t audio_size = audio_event_state_size();
    if (io->data) {
        if (io->pos > io->size || audio_size > io->size - io->pos) { io->ok = 0; return; }
        int ok = io->mode ? audio_event_state_load(io->data + io->pos, audio_size, io->mode == 2) :
            audio_event_state_save(io->data + io->pos, audio_size);
        if (!ok) io->ok = 0;
    }
    io->pos += audio_size;
}
size_t s3_state_size(void) { S3StateIO io = {0}; state(&io); return io.pos; }
static int ready(void)
{
    if (g_ram[0xF600] != 0x0C || !g_ram[0xF711]) return 0;
#ifndef SONIC3_STANDALONE
    if (!s3_army_state_ready()) return 0;
#endif
    return 1;
}
int s3_state_at_boundary(void)
{
    unsigned sp = g_cpu.A[7] & 65535;
    if (!ready() || sp > 65532) return 0;
    uint32_t caller = ((uint32_t)g_ram[sp] << 24) | ((uint32_t)g_ram[sp+1] << 16) |
        ((uint32_t)g_ram[sp+2] << 8) | g_ram[sp+3];
    return caller == LEVEL_WAIT_RETURN || caller == PAUSE_WAIT_RETURN;
}
uint32_t s3_state_resume_pc(uint8_t mode) { return mode == 0x0C ? LEVEL_LOOP : 0; }
int s3_state_save(void *data, size_t size)
{
    if (!ready() || size != s3_state_size()) return 0;
    S3StateIO io = { data, size, 0, 0, 1 }; state(&io);
    return io.ok && io.pos == size;
}
int s3_state_load(const void *data, size_t size, int apply)
{
    if (!data || size != s3_state_size()) return 0;
    S3StateIO io = { (uint8_t *)data, size, 0, 1, 1 }; state(&io);
    if (!io.ok || io.pos != size) return 0;
    if (apply) { io.pos = 0; io.mode = 2; state(&io); }
    return io.ok;
}
