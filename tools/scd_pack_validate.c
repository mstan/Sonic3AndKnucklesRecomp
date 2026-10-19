/* Optional standalone validator used before an atomic content publication. */
#include "scd_pack.h"
#include <stdio.h>
int main(int argc,char **argv)
{
    char error[256];
    if(argc!=2){fprintf(stderr,"usage: scd_pack_validate content.pak\n");return 2;}
    if(!scd_pack_validate(argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    return 0;
}
