/*
FRAME_SHIM.H

Included before each file of the Android host when it is built for the Steam
Frame (port/frame/build.sh): the Android-only SDL calls it makes.
*/

#ifndef __HALO_FRAME_SHIM_H
#define __HALO_FRAME_SHIM_H

#include <stdbool.h>
#include <stdio.h>

/* internet play's invite notice (host_sdl.c): there are no toasts here, so
the notice goes to the log and the game falls back as when a toast fails */
#define SDL_ShowAndroidToast(message, duration, gravity, x, y) \
	(fprintf(stderr, "I/halo: notice: %s\n", (message)), false)

#endif
