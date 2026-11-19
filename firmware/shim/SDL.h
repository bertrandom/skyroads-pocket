/* The shared game code includes SDL only for SDL_Delay (pocket.c). */
#ifndef POCKET_SDL_SHIM_H
#define POCKET_SDL_SHIM_H
#include <stdint.h>
void SDL_Delay(uint32_t ms);
#endif
