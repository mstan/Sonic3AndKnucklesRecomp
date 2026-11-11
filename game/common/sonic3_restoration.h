#pragma once
struct S3StateIO;
void s3_restoration_load(void);
void s3_restoration_settings(const char *path,const char *mode);
int s3_restoration_select(unsigned component,int on);
struct RecompLauncherCModProvider;
const struct RecompLauncherCModProvider *s3_restoration_mods(const struct RecompLauncherCModProvider *base);
int s3_restoration_netplay_allowed(void);
void s3_restoration_post_reset(void);
int s3_restoration_instruction(unsigned pc);
void s3_restoration_state(struct S3StateIO *io);
