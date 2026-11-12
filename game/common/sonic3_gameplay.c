#include "sonic3_gameplay.h"
#include <stdio.h>
#include <string.h>
#ifdef S3_SOUND_COMBINED
#include "stage_pack.h"
#endif
#ifdef _WIN32
#include <windows.h>
#endif
S3GameplayConfig s3_gameplay;
static char path[1024],error[256];
static int standalone_s3,standalone_sk,combined_mode;
void s3_gameplay_load(const char *settings_path,const char *mode)
{
    memset(&s3_gameplay,0,sizeof s3_gameplay);
    error[0]=0;standalone_s3=mode&&!strcmp(mode,"sonic3");
    standalone_sk=mode&&!strcmp(mode,"sandk");
    combined_mode=mode&&!strcmp(mode,"sonic3k");
    snprintf(path,sizeof path,"%s",settings_path?settings_path:"settings.ini");
    char *a=strrchr(path,'/'),*b=strrchr(path,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:path;
    snprintf(name,sizeof path-(size_t)(name-path),"%s-gameplay.ini",mode?mode:"sonic3k");
    FILE *f=fopen(path,"rb");if(!f)return;
    char line[128];
    while(fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"infinite_lives=1"))s3_gameplay.infinite_lives=1;
        if(!strcmp(line,"infinite_time=1"))s3_gameplay.infinite_time=1;
        if(!strcmp(line,"easy_level_select=1"))s3_gameplay.easy_level_select=1;
        if(!strcmp(line,"debug_mode=1"))s3_gameplay.debug_mode=1;
        if(combined_mode&&!strcmp(line,"checkpoint_bonus=1"))s3_gameplay.checkpoint_bonus=1;
        if(!strcmp(line,"shield_transform=1"))s3_gameplay.shield_transform=1;
        if(combined_mode&&!strcmp(line,"tails_ring_follow=1"))s3_gameplay.tails_ring_follow=1;
        if(!standalone_sk&&!strcmp(line,"competition_title_cards=1"))s3_gameplay.competition_title_cards=1;
        if(combined_mode&&!strcmp(line,"original_zone_order=1"))s3_gameplay.original_zone_order=1;
        if(!standalone_s3&&!strcmp(line,"early_zone_names=1"))s3_gameplay.early_zone_names=1;
    }
    fclose(f);
}
int s3_gameplay_save(void)
{
    error[0]=0;
    char temp[1040];snprintf(temp,sizeof temp,"%s.tmp",path);
    FILE *f=fopen(temp,"wb");if(!f)goto failed;
    fprintf(f,"infinite_lives=%d\ninfinite_time=%d\neasy_level_select=%d\ndebug_mode=%d\ncheckpoint_bonus=%d\nshield_transform=%d\ntails_ring_follow=%d\ncompetition_title_cards=%d\n",
        s3_gameplay.infinite_lives,s3_gameplay.infinite_time,
        s3_gameplay.easy_level_select,s3_gameplay.debug_mode,s3_gameplay.checkpoint_bonus,s3_gameplay.shield_transform,s3_gameplay.tails_ring_follow,s3_gameplay.competition_title_cards);
    fprintf(f,"original_zone_order=%d\n",s3_gameplay.original_zone_order);
    fprintf(f,"early_zone_names=%d\n",s3_gameplay.early_zone_names);
    int ok=!ferror(f);if(fclose(f))ok=0;
    if(!ok)goto failed;
#ifdef _WIN32
    if(!MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))goto failed;
#else
    if(rename(temp,path))goto failed;
#endif
    return 1;
failed:
    snprintf(error,sizeof error,"Unable to save gameplay options");return 0;
}
int s3_gameplay_netplay_allowed(void)
{ return !s3_gameplay.infinite_lives && !s3_gameplay.infinite_time &&
    !s3_gameplay.easy_level_select && !s3_gameplay.debug_mode &&
    !s3_gameplay.checkpoint_bonus && !s3_gameplay.shield_transform && !s3_gameplay.tails_ring_follow && !s3_gameplay.competition_title_cards && !s3_gameplay.original_zone_order && !s3_gameplay.early_zone_names; }
static unsigned word(const uint8_t *ram,unsigned p)
{return (unsigned)ram[p]<<8|ram[p+1];}
static int shield_site(uint32_t pc)
{return standalone_s3?(pc==0x12956u || pc==0x1299Au || pc==0x129C2u):
                     (pc==0x1193Cu || pc==0x11980u || pc==0x119A8u);}
static int timer_site(uint32_t pc)
{return standalone_s3?(pc==0xEAF2u || pc==0xEB14u || pc==0xEB26u):
                     (pc==0xDD76u || pc==0xDD98u || pc==0xDDAAu);}
static int nemesis_pop_site(uint32_t pc)
{return pc==(standalone_s3?0x1B0Au:0x1880u);}
static int palette_reset_site(uint32_t pc)
{return pc==(standalone_s3?0x3188u:0x3812u);}
void s3_gameplay_palette_reset(uint32_t pc,uint8_t ram[65536])
{
    /* Revert underflows to FFFA. Retail clears only the high byte, leaving
     * 00FA for the next fade-in, which then reads code past the color table.
     * Clear the low byte too; the original instruction retains its own CCR. */
    if(ram && palette_reset_site(pc))ram[0xF65D]=0;
}
void s3_gameplay_zone_order(uint32_t pc,const uint8_t ram[65536],uint32_t *d0)
{
    if(!combined_mode || !s3_gameplay.original_zone_order || !ram || !d0 ||
       word(ram,0xFFD0) || word(ram,0xFFE8))return;
    unsigned zone=word(ram,0xFE10),next=*d0&65535u;
    if(pc==0x85E3Au) {
        /* StartNewLevel: retain each native exit and incoming initialization. */
        if(zone==0x301 && next==0x500)next=0x400;
        else if(zone==0x401 && next==0x800)next=0x500;
        else if(zone==0x701 && next==0x400)next=0x800;
        *d0=(*d0&0xFFFF0000u)|next;
    } else if(pc==0xC452u) {
        /* SaveGame's already-decoded next-zone byte. These are the retail
         * save codes for FBZ, ICZ and SOZ, so disabling the mod or importing
         * the SRAM still resumes the actual saved zone. */
        if(zone==0x300 || zone==0x301)next=7;
        else if(zone==0x400 || zone==0x401)next=4;
        else if(zone==0x700 || zone==0x701)next=8;
        else return;
        *d0=(*d0&0xFFFFFF00u)|next;
    }
}
uint32_t s3_gameplay_zone_departure(uint32_t pc,uint8_t ram[65536],uint32_t actor)
{
    actor&=0xFFFFFFu;
    if(!combined_mode || !s3_gameplay.original_zone_order || pc!=0x76270 || !ram ||
       word(ram,0xFE10)!=0x701 || word(ram,0xFFD0) || word(ram,0xFFE8) || ram[0xFAA8] ||
       actor<0xFFB0DEu || actor>=0xFFCAE2u || (actor-0xFFB000u)%0x4Au)return 0;
    /* The native controller reaches this point only after the capsule and
     * results release it. Sandopolis follows on foot: omit the airship
     * boarding sequence and retain the original timed fade/next-level path. */
    unsigned o=actor&65535u;
    ram[o]=0;ram[o+1]=7;ram[o+2]=0x64;ram[o+3]=0x56;
    ram[o+0x2E]=0;ram[o+0x2F]=59;
    return 0x76456;
}
static int allocation_site(uint32_t pc)
{return standalone_s3?(pc==0x1A0E0u || pc==0x1A0BCu):
        combined_mode && (pc==0x1BB10u || pc==0x1BAECu);}
static int title_card_site(uint32_t pc)
{return (standalone_s3 || combined_mode) &&
    pc==(standalone_s3?0x2CA7Cu:0x2D6A6u);}
uint32_t s3_gameplay_title_card(uint32_t pc,uint8_t ram[65536],uint32_t actor)
{
    actor&=0xFFFFFFu;
    if(!s3_gameplay.competition_title_cards || !title_card_site(pc) ||
       pc!=(standalone_s3?0x2CA7Cu:0x2D6A6u) || !ram ||
       (ram[0xF600]&0x7F)!=12 || ram[0xFE10]<0xE || ram[0xFE10]>0x12 ||
       actor<0xFFB000u || actor>=0xFFCAE2u)return 0;
    /* The unused controller/layout is complete. Competition's planes and
     * HUD occupy the story-card pattern banks, so keep its controller timing
     * but render ROM-derived course art without queuing the story PLCs. */
    ram[(actor&65535u)+0x44]=255;
    return standalone_s3?0x2CADCu:0x2D756u;
}
int s3_gameplay_title_card_sprite(uint32_t pc,const uint8_t ram[65536],uint32_t actor)
{
    actor&=0xFFFFFFu;
    if(!s3_gameplay.competition_title_cards || !(standalone_s3 || combined_mode) ||
       pc!=(standalone_s3?0x19240u:0x1ABC6u) || !ram ||
       (ram[0xF600]&0x7F)!=12 || ram[0xFE10]<0xE || ram[0xFE10]>0x12 ||
       actor<0xFFB000u || actor>=0xFFCAE2u)return 0;
    unsigned o=actor&65535u;
    uint32_t map=(uint32_t)word(ram,o+12)<<16|word(ram,o+14);
    return map==(standalone_s3?0x2D90Cu:0x2EE10u) && ram[o+0x22]==0x12;
}
int s3_gameplay_owns(uint32_t pc)
{return shield_site(pc) || timer_site(pc) || nemesis_pop_site(pc) || palette_reset_site(pc) ||
        (combined_mode && (pc==0x85E3Au || pc==0xC452u || pc==0x76270u)) ||
        allocation_site(pc) || title_card_site(pc) || (standalone_s3 && pc==0x19240u);}
void s3_gameplay_allocation_hook(uint32_t pc,const uint8_t ram[65536],
                                 uint32_t candidate,uint16_t *sr)
{
    /* HCZ1BGE_DoTransition installs its splash at fixed slot B172. An
     * ordinary allocation there can leave the end-sign controller awaiting
     * results in that slot; the splash then destroys it before its native
     * Change_Act2Sizes call. Make the original DBEQ allocator continue past
     * this reserved slot. Its own counter, bounds and occupied-slot behavior
     * remain intact; no result/camera flag or object contents are changed.
     * Cover CreateNewSprite4 (level placements) as well as the loop shared
     * by AllocateObject and AllocateObjectAfterCurrent. */
    if(allocation_site(pc) && ram && sr && ram[0xF600]==12 &&
       word(ram,0xFE10)==0x100 && !word(ram,standalone_s3?0xFFD8:0xFFE8) &&
       (candidate&0xFFFFFFu)==0xFFB172u)
        *sr&=(uint16_t)~4u;
}
/* Original skdisasm FixBugs repair for Process_Nem_Queue_ShiftUp. Retail
 * shifts only88 of90 bytes and never retires its16th source. Once occupied,
 * that source repeats forever and later append requests overwrite variables
 * beyond the queue, including Tails' AI routine. Complete the FIFO pop at
 * its original RTS; no native registers or decompressor state are replaced. */
void s3_gameplay_nemesis_pop(uint32_t pc,uint8_t ram[65536],uint16_t *sr)
{
    if(!nemesis_pop_site(pc) || !ram || !sr)return;
    ram[0xF6D8]=ram[0xF6DE];ram[0xF6D9]=ram[0xF6DF];
    memset(ram+0xF6DA,0,4);
    *sr=(uint16_t)((*sr&~15u)|4u); /* native CLR.L: preserve X and upperSR */
}
static int timed_story(const uint8_t ram[65536])
{
    if(!ram || ram[0xF600]!=12 || (ram[0xFE10]>=0x13 && ram[0xFE10]<=0x15))return 0;
    unsigned demo=standalone_s3?0xFFF0:0xFFD0,competition=standalone_s3?0xFFD8:0xFFE8;
    return !word(ram,demo) && !word(ram,competition);
}
void s3_gameplay_timer_hook(uint32_t pc,const uint8_t ram[65536],
                           uint32_t *d1,uint16_t *sr)
{
    if(!timer_site(pc) || !timed_story(ram) || !d1 || !sr)return;
    unsigned minute=ram[0xFE23];
    if(pc==(standalone_s3?0xEAF2u:0xDD76u)) {
        /* Original BEQ after the exact 9:59:59 comparison. Let the native
         * counter roll normally. Re-enabling the limit after ten minutes
         * takes the original Time Over path on its next active update. */
        if(s3_gameplay.infinite_time)*sr&=(uint16_t)~4u;
        else if(minute>=10)*sr|=4u;
    } else if(pc==(standalone_s3?0xEB14u:0xDD98u)) {
        /* Original BLO before clamping the minute back to nine. */
        if(s3_gameplay.infinite_time && minute<=99)*sr|=1u;
    } else if(minute>=10 && minute<=99) {
        /* The renderer adds the original tens glyph in the spare column.
         * Preserve the earned time; pass only its ones digit to native art. */
        *d1=(*d1&~255u)|((*d1&255u)%10u);
    }
}
int s3_gameplay_skip_shield(uint32_t pc,const uint8_t ram[65536],
                            uint32_t actor,int native_sonic)
{
    if(!s3_gameplay.shield_transform || !native_sonic || !shield_site(pc) ||
       !ram || (actor&0xFFFFFFu)!=0xFFB000u || ram[0xF600]!=12)return 0;
    unsigned demo=standalone_s3?0xFFF0:0xFFD0,competition=standalone_s3?0xFFD8:0xFFE8;
    /* The native site follows the fresh-airborne-jump/double-jump/Super
     * checks. Repeat their guards so another caller cannot steal a shield
     * action. Up is the explicit modifier; ordinary jumps remain native. */
    if(word(ram,demo) || word(ram,competition) || word(ram,0xFE08) ||
       !(ram[0xB02A]&2) || ram[0xB02F] || ram[0xFE19] ||
       !(ram[0xF602]&1) || !(ram[0xF603]&0x70) ||
       !(ram[0xB02B]&0x70) || (ram[0xB02B]&2) ||
       word(ram,0xFE20)<50 || !ram[0xFE1E])return 0;
    /* Leave native shield moves intact when transformation is unavailable.
     * These are the original CheckTransform predicates, which the target
     * repeats before owning all palette/animation/ring-drain changes. */
    if(standalone_s3) {
        if(word(ram,0xFFB0)<7)return 0;
    } else if(ram[0xFFB1]<7 && (ram[0xFFB0]<7 || ram[0xFFBA]))return 0;
    return 1;
}
void s3_gameplay_apply(uint8_t ram[65536])
{
    if(!ram)return;
    unsigned demo=standalone_s3?0xFFF0:0xFFD0;
    unsigned competition=standalone_s3?0xFFD8:0xFFE8;
    if(ram[demo] || ram[demo+1] || ram[competition] || ram[competition+1])return;
    /* Unlock the existing third title choice / A+Start path. The native menu
     * still owns selection, stage initialization and save/no-save handling. */
    if(s3_gameplay.easy_level_select && ram[0xF600]==4)
        ram[standalone_s3?0xFFD0:0xFFE0]=1;
    /* All three cartridges share these variables. Exclude attract demos,
     * menus, bonus stages and Competition. The normal game owns death/retry,
     * score awards, timer progression, checkpoints and act transitions. */
    if(ram[0xF600]!=12 || (ram[0xFE10]>=0x13 && ram[0xFE10]<=0x15))return;
    /* Native B/A/C placement controls, never placement/result flags. Imported
     * rounds have different object tables; do not expose native debug there. */
    if(s3_gameplay.debug_mode)
        ram[standalone_s3?0xFFFA:0xFFDA]=ram[0xFE10]<0x20;
    if(s3_gameplay.infinite_lives && ram[0xFE12]<3) {
        ram[0xFE12]=3;ram[0xFE1C]=1;
    }
    /* Keep the native minute/second clock, extending its limit to 99:59.
     * Only the terminal subsecond is held; all unrelated timers still run.
     * Results use the true minutes, so the original 9:59 jackpot lasts only
     * its genuine second; ten minutes and later use the normal late bonus. */
    if(s3_gameplay.infinite_time && !ram[0xFE22] && ram[0xFE23]==99 &&
       ram[0xFE24]==59 && ram[0xFE25]==59)ram[0xFE25]=58;
}
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.gameplay";
static const char *ids[]={"infinite-lives","infinite-time","easy-level-select","debug-mode","shield-transform","competition-title-cards","s1-checkpoint-bonus","tails-ring-follow","original-zone-order","early-zone-names"};
static const char *names[]={"Infinite lives","Infinite time","Easier level select","Debug mode","Shielded transformation","Competition title cards","Sonic 1 checkpoint bonus stages","Tails giant-ring follow","Original zone order","Mushroom Valley name"};
static const char *descriptions[]={
    "Keep at least three lives, including after death and retry. Local play only.",
    "Disable the stage time limit. The clock counts to 99:59, then stays there. The 100,000-point time bonus applies only at 9:59. Local play only.",
    "Unlock Sound Test on the title menu. Press Up to select it, then Start; use the native stage list. Local play only.",
    "Enable native debug in original stages. B enters/exits placement, A cycles objects, C places one. Imported rounds are excluded. Local play only.",
    "Native Sonic: hold Up and press jump again in the air to transform with an elemental shield. Requires native emeralds and 50 rings. Without Up, shield actions stay normal. Shadow/Mighty keep their own controls. Local play only.",
    "Restore the five unused Competition course name cards using the cartridge's original art, animation and timing. Local play only.",
    "Sonic 1 rounds only: collect at least 20 rings and enter the stars above an activated checkpoint to visit a native bonus stage. Local play only.",
    "Sonic 1 end-of-act giant rings: a nearby Tails companion runs and jumps after the leader. Tails enters only if he reaches the ring before the tally; distant or missing companions keep normal behavior. Local play only.",
    "Sonic 3 & Knuckles: visit Flying Battery after Carnival Night, then IceCap, Launch Base, Mushroom Hill and Sandopolis. Uses the complete retail stages and their normal exits. Native saves retain their original zone codes. Default off; local play only.",
    "Use Mushroom Valley, the November 1993 level name, on native title cards and level select. The title lettering is reconstructed from your retail cartridge's font; the prototype's missing card is not claimed as recovered artwork. Default off; restart to apply. Local play only."};
static int own_features(void){return combined_mode?10:6;}
static int feature_index(int i){return standalone_sk && i==5?9:i;}
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static int own(const char *p){return p&&!strcmp(p,package);}
static int index_of(const char *f){for(int i=0;i<own_features();++i)if(f&&!strcmp(f,ids[feature_index(i)]))return feature_index(i);return -1;}
static int *value(int i){switch(i) {
    case 0:return &s3_gameplay.infinite_lives;
    case 1:return &s3_gameplay.infinite_time;
    case 2:return &s3_gameplay.easy_level_select;
    case 3:return &s3_gameplay.debug_mode;
    case 4:return &s3_gameplay.shield_transform;
    case 5:return &s3_gameplay.competition_title_cards;
    case 6:return &s3_gameplay.checkpoint_bonus;
    case 8:return &s3_gameplay.original_zone_order;
    case 9:return &s3_gameplay.early_zone_names;
    default:return &s3_gameplay.tails_ring_follow;
}}
static int packages(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int get_package(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;
    memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"Gameplay options");COPY(o->version,"0.1.0");
    COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Optional lives, time limit, native level select, debug controls, shielded transformation and checkpoint bonuses.");
    COPY(o->license,"Project license");o->enabled=!s3_gameplay_netplay_allowed();return 1;
}
static int features(void *c){(void)c;return own_features()+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int get_feature(void *c,int i,RecompLauncherCModFeature *o)
{
    (void)c;if(i>=own_features())return base&&base->feature_get&&base->feature_get(base->ctx,i-own_features(),o);
    if(i<0||!o)return 0;
    i=feature_index(i);
    memset(o,0,sizeof *o);
    COPY(o->id,ids[i]);COPY(o->package_id,package);COPY(o->package_name,"Gameplay options");
    COPY(o->package_version,"0.1.0");COPY(o->name,names[i]);COPY(o->group,"Gameplay");
    if(i==5){
        COPY(o->group,"Restoration");COPY(o->source_name,"Sonic Retro disassembly");
        COPY(o->source_url,"https://github.com/sonicretro/skdisasm");
    }
    if(i==8 || i==9){
        COPY(o->group,"Restoration");COPY(o->source_name,"Hidden Palace prototype documentation");
        COPY(o->source_url,"https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype)");
    }
    COPY(o->description,descriptions[i]);COPY(o->status,*value(i)?"Enabled â€” local play":"Disabled");
    if(i==2 && standalone_sk)
        COPY(o->description,"Hold A and press Start on the title screen to open native level select. Keep A held through the fade. Local play only.");
    o->enabled=*value(i);return 1;
}
static int enable(void *c,const char *p,const char *f,int on)
{
    (void)c;error[0]=0;
    if(!own(p))return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);
    int i=index_of(f);if(i<0)return 0;
#ifdef S3_SOUND_COMBINED
    if(i==9 && on && s3_stage_pack_enabled){COPY(error,"Disable imported campaigns and additional characters before enabling the Mushroom Valley name.");return 0;}
#endif
    *value(i)=on!=0;return 1;
}
static int enable_package(void *c,const char *p,int on)
{
    if(!own(p))return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    for(int i=0;i<own_features();++i)if(!enable(c,p,ids[feature_index(i)],on))return 0;
    return 1;
}
static int commit(void *c,const char *image)
{
    (void)c;error[0]=0;
#ifdef S3_SOUND_COMBINED
    if(s3_gameplay.early_zone_names && s3_stage_pack_enabled){COPY(error,"Disable imported campaigns and additional characters before enabling the Mushroom Valley name.");return 0;}
#endif
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;
    return s3_gameplay_save();
}
static int commit_netplay(void *c,const char *image)
{
    (void)c;error[0]=0;
    if(!s3_gameplay_netplay_allowed()) {
        COPY(error,"Disable gameplay options before netplay");return 0;
    }
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *c)
{(void)c;return *error?error:base&&base->last_error?base->last_error(base->ctx):"";}
const RecompLauncherCModProvider *s3_gameplay_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;
    if(base)p=*base;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=get_package;p.set_enabled=enable_package;
    p.feature_count=features;p.feature_get=get_feature;p.feature_enable=enable;
    p.commit=commit;p.commit_netplay=commit_netplay;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_gameplay_mods(const struct RecompLauncherCModProvider *common)
{return common;}
#endif
