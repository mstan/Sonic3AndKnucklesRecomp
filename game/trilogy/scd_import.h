#ifndef S3K_CD_IMPORT_H
#define S3K_CD_IMPORT_H
#include <stddef.h>
int s3_sound_donor_import(const char *source,const char *expected,const char *settings,char content[1024],
                         char *error,size_t capacity,
                         void (*progress)(void *,float,const char *),void *user);
/* Runs the shipped owned-input converter without a shell. Does not change
 * settings or saves. The caller validates both returned resources before
 * adopting either path. All paths and log output remain in the settings tree. */
int s3_cd_import(const char *disc,const char *rom,const char *settings,
                 char content[1024],char music[1024],char *error,size_t capacity,
                 void (*progress)(void *,float,const char *),void *user);
/* Campaign IDs 1/2/3 select Sonic 1/Sonic 2/Megamix. The latter also returns
 * its normalized owned ROM for the existing runtime donor verification. */
int s3_rom_import(unsigned kind,const char *source,const char *rom,const char *settings,
                  char content[1024],char donor[1024],char *error,size_t capacity,
                  void (*progress)(void *,float,const char *),void *user);
int s3_custom_music_import(const char *source,const char *track,const char *settings,char content[1024],
                         char *error,size_t capacity,void (*progress)(void *,float,const char *),void *user);
#endif
