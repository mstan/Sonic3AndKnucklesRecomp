/* Save only at a resumable native Wait_VSync boundary. Restoring its
 * per-frame loop avoids the stage initialization in GameLoop. */
#include "sonic3_state.h"
#include "sonic3_state_io.h"
#include "sonic3_sound_driver.h"
#include "genesis_runtime.h"
#include "audio/event_queue.h"

/* Verified against the byte-matched pinned skdisasm listings:
 * LevelLoop / instruction after its BSR Wait_VSync / Pause_Loop's return.
 * All resume targets are existing discovered labels, not generated-C edits. */
#ifdef SONIC3_STANDALONE
enum { LEVEL_LOOP = 0x4B0C, LEVEL_WAIT_RETURN = 0x4B20, PAUSE_WAIT_RETURN = 0x16C0,
       SPECIAL_LOOP = 0x77D2, SPECIAL_WAIT_RETURN = 0x77E6 };
#else
enum { LEVEL_LOOP = 0x650C, LEVEL_WAIT_RETURN = 0x6520, PAUSE_WAIT_RETURN = 0x143A,
       SPECIAL_LOOP = 0x84C2, SPECIAL_WAIT_RETURN = 0x84D6 };
#endif

/* A menu resume must preserve the parked guest call, not run its loader.
 * Addresses and instruction lengths match the pinned retail listings. */
typedef struct { unsigned mode, wait, length; } MenuWait;
static const MenuWait menu_waits[] = {
#ifdef SONIC3_STANDALONE
    {0x4C,0xBA8E,6}, {0x38,0x96BE,6}, {0x3C,0xA344,6}, {0x40,0x9C62,6}
#else
    {4,0x3FCE,4}, {4,0x3FF6,4}, {4,0x4064,4}, {4,0x40B0,4},
    {4,0x4130,4}, {4,0x41DA,4}, {4,0x5218,4}, {4,0x52B2,4},
    {4,0x5356,4}, {4,0x5388,4},
    {0x4C,0xC808,6}, {0x38,0xA9EE,6}, {0x3C,0xB4AE,6}, {0x40,0xADF8,6}
#endif
};
static uint32_t menu_resume_sp;
static unsigned menu_mode(unsigned mode)
{
    return mode == 0xC0 ? 0x40 : mode;
}
static const MenuWait *menu_wait(unsigned mode, uint32_t pc)
{
    for (unsigned i=0;i<sizeof menu_waits/sizeof menu_waits[0];++i)
        if (menu_waits[i].mode==menu_mode(mode) && menu_waits[i].wait==pc)
            return &menu_waits[i];
    return NULL;
}
static int menu_boundary(void)
{
    const MenuWait *wait=menu_wait(g_ram[0xF600],g_cpu.PC);
    uint32_t sp=g_cpu.A[7];
    if (!wait || (sp&1u) || sp<0xFFFD00u || sp>0xFFFE00u) return 0;
    unsigned off=sp&65535u;
    uint32_t caller=((uint32_t)g_ram[off]<<24)|((uint32_t)g_ram[off+1]<<16)|
                    ((uint32_t)g_ram[off+2]<<8)|g_ram[off+3];
    return caller==wait->wait+wait->length;
}
int s3_state_menu_instruction(uint32_t pc)
{
    for (unsigned i=0;i<sizeof menu_waits/sizeof menu_waits[0];++i) {
        const MenuWait *wait=&menu_waits[i];
        if (pc!=wait->wait && pc!=wait->wait+wait->length) continue;
        if (wait->mode==menu_mode(g_ram[0xF600]))
            g_cpu.PC=pc==wait->wait ? pc : 0;
        return 1;
    }
    return 0;
}
void s3_state_call_resume(uint32_t pc)
{
    uint32_t sp=menu_resume_sp;
    menu_resume_sp=0;
    if (sp) g_cpu.A[7]=sp;
    recomp_call_addr(pc);
}

static void state(S3StateIO *io)
{
    unsigned version = 5;
    if (s3_state_peek_unsigned(io, version) != version) io->ok = 0;
    S3_STATE(io, version);
    s3_sound_state(io);
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
/* Rewind restores an execution snapshot, including any suspended native actor
 * call. Its simulation statics must follow the restored guest object RAM.
 * Presentation is restored separately by the local rewind renderer. */
static void rollback_state(S3StateIO *io)
{
    s3_sound_state(io);
#ifndef SONIC3_STANDALONE
    s3_army_state(io);
#endif
}
size_t s3_rb_state_save(void *data, size_t capacity)
{
    S3StateIO measure = {0};
    rollback_state(&measure);
    if (!data) return measure.pos;
    if (capacity < measure.pos) return 0;
    S3StateIO io = {data, capacity, 0, 0, 1};
    rollback_state(&io);
    return io.ok ? io.pos : 0;
}
int s3_rb_state_load(const void *data, size_t size)
{
    if (!data || size != s3_rb_state_save(NULL, 0)) return 0;
    S3StateIO io = {(uint8_t *)data, size, 0, 1, 1};
    rollback_state(&io);
    if (!io.ok || io.pos != size) return 0;
    io.pos = 0;
    io.mode = 2;
    rollback_state(&io);
    return io.ok && io.pos == size;
}
size_t s3_state_size(void) { S3StateIO io = {0}; state(&io); return io.pos; }
static int ready(void)
{
    unsigned mode = g_ram[0xF600];
#ifdef SONIC3_STANDALONE
    /* The standalone adapter also maps its Sega/title wait continuations. */
    if (mode == 4) return 1;
#endif
    if (!menu_wait(mode,g_cpu.PC) && mode != 0x34 &&
        (mode != 0x0C || !g_ram[0xF711])) return 0;
#ifndef SONIC3_STANDALONE
    if (!s3_army_state_ready()) return 0;
#endif
    return 1;
}
int s3_state_at_boundary(void)
{
    unsigned sp = g_cpu.A[7] & 65535;
    if (!ready() || sp > 65532) return 0;
    if (menu_wait(g_ram[0xF600],g_cpu.PC)) return menu_boundary();
    uint32_t caller = ((uint32_t)g_ram[sp] << 24) | ((uint32_t)g_ram[sp+1] << 16) |
        ((uint32_t)g_ram[sp+2] << 8) | g_ram[sp+3];
    /* SS initialization/fades also wait for VBlank. Only its established
     * per-frame loop (or Pause_Game within it) is safe to restart. */
    return caller == (g_ram[0xF600] == 0x34 ? SPECIAL_WAIT_RETURN : LEVEL_WAIT_RETURN) ||
           caller == PAUSE_WAIT_RETURN;
}
uint32_t s3_state_resume_pc(uint8_t mode)
{
    menu_resume_sp=0;
    if (menu_wait(mode,g_cpu.PC) && menu_boundary()) {
        /* Re-entering the saved JSR/BSR recreates only its own return slot. */
        menu_resume_sp=g_cpu.A[7]+4;
        return g_cpu.PC;
    }
    return mode == 0x0C ? LEVEL_LOOP : mode == 0x34 ? SPECIAL_LOOP : 0;
}
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
    if (apply && io.ok && io.pos == size) s3_video_state_loaded();
    return io.ok && io.pos == size;
}
