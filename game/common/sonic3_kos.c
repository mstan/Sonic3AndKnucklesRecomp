/* Original queued Kosinski program, using the shared explicit-PC native ABI.
 * VBlank may return to Backup_Kos_Registers; its subsequent restoring RTE
 * resumes the compiled instruction named by the original RAM bookmark. */
#include "sonic3_kos.h"
#include "genesis_runtime.h"
#include "native_interruptible.h"
#include "sonic3_kos_native.h"
#include <stddef.h>
#include <string.h>

#ifdef SONIC3_STANDALONE
enum { KOS_ENTRY=0x1EA4 };
#else
enum { KOS_ENTRY=0x1C1A };
#endif
_Static_assert(sizeof(M68KState)==sizeof(M68kStepRegisters),"CPU register ABI");
_Static_assert(offsetof(M68KState,PC)==offsetof(M68kStepRegisters,PC),"CPU PC ABI");
_Static_assert(offsetof(M68KState,USP)==offsetof(M68kStepRegisters,USP),"CPU USP ABI");
static M68kStepBusResult read8(void *u,uint32_t a,uint8_t *v)
{ (void)u; *v=m68k_read8(a);return M68K_BUS_OK; }
static M68kStepBusResult read16(void *u,uint32_t a,uint16_t *v)
{ (void)u; *v=m68k_read16(a);return M68K_BUS_OK; }
static M68kStepBusResult fetch16(void *u,uint32_t a,uint16_t *v)
{ (void)u;if(a+1>=sizeof g_rom)return M68K_BUS_FAULT;*v=(uint16_t)((g_rom[a]<<8)|g_rom[a+1]);return M68K_BUS_OK; }
static M68kStepBusResult write8(void *u,uint32_t a,uint8_t v)
{ (void)u;m68k_write8(a,v);return M68K_BUS_OK; }
static M68kStepBusResult write16(void *u,uint32_t a,uint16_t v)
{ (void)u;m68k_write16(a,v);return M68K_BUS_OK; }
static int native_instruction(void *user,unsigned *cycles)
{
    M68kStepContext *ctx=user;
    const M68kStepModule *module=&sonic3_kos_native_module;
    size_t lo=0,hi=module->count;
    while(lo<hi){size_t m=lo+(hi-lo)/2;if(module->entries[m].pc<g_cpu.PC)lo=m+1;else hi=m;}
    if(lo==module->count || module->entries[lo].pc!=g_cpu.PC)return 0;
    memcpy(&ctx->state.regs,&g_cpu,sizeof g_cpu);
    if(g_cpu.SR&0x2000u)ctx->state.ssp=g_cpu.A[7];
    uint64_t before=ctx->state.cycles;
    if(m68k_step_execute(ctx,&module->entries[lo])!=M68K_STEP_OK)return 0;
    memcpy(&g_cpu,&ctx->state.regs,sizeof g_cpu);
    uint64_t elapsed=ctx->state.cycles-before;
    if(!elapsed || elapsed>65535u)return 0;
    *cycles=(unsigned)elapsed;
    return 1;
}
int s3_kos_owns(uint32_t pc) { return pc==KOS_ENTRY; }
int s3_kos_instruction(uint32_t pc)
{
    /* Limit the correction to the assessed AIZ gameplay host. Native boot,
     * other stages and empty queues keep their existing execution path. */
    if(pc!=KOS_ENTRY || g_ram[0xF600]!=12 || !g_ram[0xF711] ||
       g_ram[0xFE10] || g_ram[0xFE11]>1 ||
       !(((unsigned)g_ram[0xFF0E]<<8|g_ram[0xFF0F])&0x7FFFu))return 0;
    return s3_kos_execute(pc);
}
int s3_kos_execute(uint32_t pc)
{
    if(pc!=KOS_ENTRY || !(((unsigned)g_ram[0xFF0E]<<8|g_ram[0xFF0F])&0x7FFFu))return 0;
    const M68kStepBus bus={read8,read16,write8,write16,fetch16,NULL,NULL,NULL};
    M68kStepContext ctx;
    m68k_step_init(&ctx,&bus,NULL);
    return genesis_interruptible_native(pc,native_instruction,&ctx);
}
