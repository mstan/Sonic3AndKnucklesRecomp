#include "scd_import.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static double seconds(void){return GetTickCount64()/1000.0;}
#else
#include <time.h>
static double seconds(void){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return now.tv_sec+now.tv_nsec/1e9;}
#endif

static int completed;
static void progress(void *user,float fraction,const char *message)
{(void)user;if(fraction==1.0f&&!strcmp(message,"fixture complete"))completed=1;}

int main(int argc,char **argv)
{
    char content[1024],music[1024],error[2048];
    if(argc!=2)return 2;
    double began=seconds();
    int ok=s3_cd_import("success.cue","fixture-rom.bin",argv[1],content,music,
                        error,sizeof error,progress,NULL);
    if(!ok||!completed||strcmp(content,"fixture-content.pak")||strcmp(music,"fixture-music.pak")) {
        fprintf(stderr,"Success receipt/progress failed: %s\n",ok?"unexpected result":error);return 1;
    }
    if(seconds()-began>=4){fputs("Waited for inherited stdout instead of the importer process.\n",stderr);return 1;}
    began=seconds();
    ok=s3_cd_import("failure.cue","fixture-rom.bin",argv[1],content,music,
                     error,sizeof error,NULL,NULL);
    if(ok||*content||*music||!strstr(error,"fixture failure")||seconds()-began>=4) {
        fprintf(stderr,"Child failure/output handling failed: %s\n",error);return 1;
    }
    puts("Importer success/failure returns without waiting for inherited pipe handles.");
    return 0;
}
