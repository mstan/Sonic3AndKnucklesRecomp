#pragma once
struct S3StateIO;
void s3_restoration_load(void);
int s3_restoration_netplay_allowed(void);
void s3_restoration_post_reset(void);
int s3_restoration_instruction(unsigned pc);
void s3_restoration_state(struct S3StateIO *io);
