#pragma once
#include <stdint.h>
/* Presentation defaults on Data Select; never changes initialized saves. */
int s3_knuckles_menu_owns(uint32_t pc);
void s3_knuckles_menu_hook(uint32_t pc, int enabled);
