/*
FRAME_GYRO.H

Aiming with the right controller on the Steam Frame (frame_gyro.c).
*/

#ifndef __HALO_FRAME_GYRO_H
#define __HALO_FRAME_GYRO_H

#include <SDL3/SDL.h>

/* `halo --gyro-probe`: connects to SteamVR and lets go again, in a process
of its own, so that a SteamVR that crashes or hangs cannot take the game
with it; exits 0 when connected, 2 when SteamVR is not available */
int frame_gyro_probe(void);

/* starts reading the controller (once; host_sdl_init) */
void frame_gyro_start(void);

/* called when the guest's event queue is empty (host_sdl_poll_event): fills
the event with the aim gathered since the last one and returns true, or
returns false; also watches the gamepad for the gyro's button combinations */
bool frame_gyro_event(SDL_Event *event);

/* whether the guest should see the button as up, while it is part of a
gyro combination (host_sdl_gamepad_button) */
bool frame_gyro_hides_button(int button);

#endif
