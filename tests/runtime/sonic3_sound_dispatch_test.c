/* Compile the actual game hook chain.
 * This catches interception that a standalone driver or CD handler test misses. */
#include "../../game/sonic3k/sonic3k_instruction.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
M68KState g_cpu;
uint8_t g_ram[65536];
static int clone,cd,audio_calls,cd_calls,renderer_calls;
int s3_sound_active(void){return clone;}
int s3_sound_owns(uint32_t pc){return pc==0x12CE || pc==0x1358 || pc==0x13C2;}
int s3_sound_hook(uint32_t pc){(void)pc;++audio_calls;return clone;}
int trilogy_owns(uint32_t pc){return pc==0x1358 || pc==0x13C2;}
int trilogy_instruction(uint32_t pc)
{if(cd && trilogy_owns(pc)){++cd_calls;g_cpu.D[0]=0xCD;return 1;}return 0;}
int s3_state_menu_instruction(uint32_t pc){(void)pc;return 0;}
int s3_kos_owns(uint32_t pc){(void)pc;return 0;}
int s3_kos_instruction(uint32_t pc){(void)pc;return 0;}
int s3_gameplay_owns(uint32_t pc){(void)pc;return 0;}
void s3_gameplay_allocation_hook(uint32_t pc,const uint8_t *ram,uint32_t p,uint16_t *sr){(void)pc;(void)ram;(void)p;(void)sr;}
void s3_gameplay_nemesis_pop(uint32_t pc,uint8_t *ram,uint16_t *sr){(void)pc;(void)ram;(void)sr;}
void s3_gameplay_timer_hook(uint32_t pc,const uint8_t *ram,uint32_t *d,uint16_t *sr){(void)pc;(void)ram;(void)d;(void)sr;}
int s3_gameplay_skip_shield(uint32_t pc,const uint8_t *ram,uint32_t p,int native){(void)pc;(void)ram;(void)p;(void)native;return 0;}
int tri_character_active(void){return 0;}
int s3_army_owns(uint32_t pc){(void)pc;return 0;}
int s3_sk_instruction_hook(uint32_t pc){(void)pc;++renderer_calls;g_cpu.D[0]=0xBAD;return 0;}
int main(void)
{
    const uint32_t requests[]={0x1358,0x13C2};
    for(unsigned i=0;i<2;i++){
        clone=0;cd=1;audio_calls=cd_calls=renderer_calls=0;g_cpu.D[0]=0x2C;
        CHECK(s3k_instruction(requests[i])==1);CHECK(cd_calls==1 && !audio_calls && !renderer_calls && g_cpu.D[0]==0xCD);
        cd=0;audio_calls=cd_calls=renderer_calls=0;g_cpu.D[0]=0x2C;
        CHECK(!s3k_instruction(requests[i]));CHECK(!audio_calls && !cd_calls && !renderer_calls && g_cpu.D[0]==0x2C);
        clone=1;cd=1;audio_calls=cd_calls=renderer_calls=0;
        CHECK(s3k_instruction(requests[i])==1);CHECK(audio_calls==1 && !cd_calls && !renderer_calls);
    }
    clone=cd=0;renderer_calls=0;g_cpu.D[0]=0x1234;
    CHECK(!s3k_instruction(0x12CE));CHECK(!renderer_calls && g_cpu.D[0]==0x1234);
    puts("PASS actual hook composition: original/CD music and tempo, active Clone priority, renderer isolation");return 0;
}
