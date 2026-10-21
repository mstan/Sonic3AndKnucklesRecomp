#ifndef S3K_CD_IMPORT_H
#define S3K_CD_IMPORT_H
#include <stddef.h>
/* Runs the shipped owned-input converter without a shell. Does not change
 * settings or saves. The caller validates both returned resources before
 * adopting either path. All paths and log output remain in the settings tree. */
int s3_cd_import(const char *disc,const char *rom,const char *settings,
                 char content[1024],char music[1024],char *error,size_t capacity,
                 void (*progress)(void *,float,const char *),void *user);
#endif
