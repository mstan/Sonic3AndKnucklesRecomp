#include "scd_resource.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    if(argc!=4)return 2;
    ScdResource r={0};char error[256];int audio=atoi(argv[2]),expected=atoi(argv[3]);
    int ok=scd_resource_open(&r,argv[1],audio,error,sizeof error);
    if(ok!=expected){fprintf(stderr,"expected %d, got %d: %s\n",expected,ok,error);return 1;}
    if(ok) {
        const ScdResourceItem *entry=scd_resource_find(&r,audio?"TRACK02":"LEVELS");
        if(!entry || !entry->size || (!audio && !scd_resource_item(&r,"LEVELS",NULL)))return 1;
        if(audio && r.data)return 1;
    }
    scd_resource_close(&r);return 0;
}
