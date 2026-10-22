/* Runtime access uses converted, integrity-checked resources. Original CUE
 * and BIN files are read only by the bounded converter, never by gameplay. */
#include "scd.h"
#include "scd_internal.h"
#include "scd_pack.h"
#include "stage_pack.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
extern const ScdPack *tri_scd_pack(void);
extern int tri_scd_available(void);
static ScdResource music;
static ScdTrack tracks[34];
static int tried;
int scd_disc_open(void){return tri_scd_available();}
const uint8_t *scd_disc_file(const char *name,uint32_t *size)
{return tri_scd_available()?scd_resource_item(&tri_scd_pack()->resource,name,size):NULL;}
const ScdTrack *scd_track_info(int number)
{
    if(!tried) {
        tried=1;char error[256];
        /* Integrity checking can outlast the device queue on a busy drive.
         * Retire its old waveform before this explicit loading boundary. */
        audio_discard_playback();
        if(scd_resource_open(&music,s3_cd_audio_path,1,error,sizeof error)) {
            for(unsigned i=0;i<34;i++) {
                tracks[i].number=(int)i+2;tracks[i].audio=1;
                snprintf(tracks[i].path,sizeof tracks[i].path,"%s",music.path);
                tracks[i].start=(long)music.items[i].offset;tracks[i].size=(long)music.items[i].size;
            }
            fprintf(stderr,"[scd] verified CD music: 34 bounded tracks\n");
        } else fprintf(stderr,"[scd] music unavailable; geometry and PCM remain available: %s\n",error);
    }
    return music.count && number>=2 && number<=35?tracks+number-2:NULL;
}
void scd_disc_close(void)
{scd_resource_close(&music);memset(tracks,0,sizeof tracks);tried=0;}
