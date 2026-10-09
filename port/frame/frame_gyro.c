/*
FRAME_GYRO.C

Aiming with the right controller on the Steam Frame.

Steam gives the game the Frame's controllers as an Xbox pad, which has no
motion sensor, but SteamVR tracks them. A thread here connects to SteamVR as
a background application (OpenVR, lib/libopenvr_api.so; it draws nothing
and leaves the screen to the game) and follows the right controller's
orientation. Turning it left or right and tilting it up or down becomes
mouse motion, which the game adds to the right stick's aim
(port/linux/src/xinput_sdl.c, halo_linux_mouse_look): at speed 1 the view
turns as far as the controller did, scaled down when zoomed like the mouse.

SteamVR is first reached from a separate process (`halo --gyro-probe`); if
that crashes or hangs, the gyro stays off for the run and the game is
untouched.

On the gamepad:
	both sticks pressed in for half a second   gyro on or off
	view (back) held, Y                        faster
	view (back) held, A                        slower
each answered with a rumble. The setting is kept in gyro.txt beside
config.toml; HALO_GYRO=0 turns it off for one run. Messages go to the log
with a "gyro:" prefix.

Slow motion (hand tremor) is averaged over a few samples and fast motion
passes straight through (soft tiered smoothing).
*/

#include "frame_gyro.h"
#include "host.h"

#include <dlfcn.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "openvr/openvr_capi.h"

/* the game's radians of view per pixel of mouse motion (xinput_sdl.c;
input.mouse_sensitivity is 1 on this build) */
#define RADIANS_PER_PIXEL 0.0022f
#define SPEED_DEFAULT 1.5f
#define SPEED_STEP 0.25f
#define SPEED_MIN 0.25f
#define SPEED_MAX 5.0f
/* below the first rate (degrees a second) motion is smoothed, above the
second it passes straight through */
#define SMOOTH_BELOW 2.0f
#define DIRECT_ABOVE 8.0f
#define SMOOTH_SAMPLES 6
#define SAMPLE_MICROSECONDS 4000
/* aim the game has not taken for this long is dropped (menus, loading) */
#define STALE_NS (150 * SDL_NS_PER_MS)
#define COMBO_HOLD_MS 500
#define RETRY_SECONDS 5
#define PROBE_SECONDS 15

static pthread_mutex_t gyro_lock = PTHREAD_MUTEX_INITIALIZER;
static bool gyro_enabled = true;
static float gyro_speed = SPEED_DEFAULT;
/* radians turned since the game last took them */
static float pending_yaw, pending_pitch;
static Uint64 taken_ns;

/* ---------- settings */

static void settings_path(char *buffer, size_t size)
{
	const char *root = getenv("HALO_DATA_ROOT");
	const char *home = getenv("HOME");

	if (root && *root)
		snprintf(buffer, size, "%s/gyro.txt", root);
	else
		snprintf(buffer, size, "%s/.local/share/halo-ce/gyro.txt", home ? home : ".");
}

static void settings_load(void)
{
	char path[600], line[128];
	FILE *file;

	settings_path(path, sizeof(path));
	file = fopen(path, "r");
	if (file)
	{
		while (fgets(line, sizeof(line), file))
		{
			int enabled;
			float speed;

			if (sscanf(line, "enabled %d", &enabled) == 1)
				gyro_enabled = enabled != 0;
			else if (sscanf(line, "speed %f", &speed) == 1 && speed >= SPEED_MIN && speed <= SPEED_MAX)
				gyro_speed = speed;
		}
		fclose(file);
	}
	if (getenv("HALO_GYRO") && strcmp(getenv("HALO_GYRO"), "0") == 0)
		gyro_enabled = false;
}

static void settings_save(void)
{
	char path[600];
	FILE *file;

	settings_path(path, sizeof(path));
	file = fopen(path, "w");
	if (!file)
		return;
	fprintf(file, "# right controller aiming on the Steam Frame (both sticks in: on/off; view+Y/A: speed)\n");
	fprintf(file, "enabled %d\nspeed %.2f\n", gyro_enabled ? 1 : 0, gyro_speed);
	fclose(file);
}

/* ---------- SteamVR */

static struct
{
	void *library;
	intptr_t (*init)(EVRInitError *error, EVRApplicationType type, const char *startup_info);
	void (*shutdown)(void);
	intptr_t (*get_interface)(const char *version, EVRInitError *error);
	const char *(*error_text)(EVRInitError error);
	struct VR_IVRSystem_FnTable *system;
} vr;

static bool vr_load(void)
{
	char path[1024];
	const char *base = SDL_GetBasePath();

	snprintf(path, sizeof(path), "%slib/libopenvr_api.so", base ? base : "");
	vr.library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!vr.library)
		vr.library = dlopen("libopenvr_api.so", RTLD_NOW | RTLD_LOCAL);
	if (!vr.library)
	{
		host_logf(HOST_LOG_WARN, "gyro: no OpenVR library (%s)", dlerror());
		return false;
	}
	vr.init = (void *)dlsym(vr.library, "VR_InitInternal2");
	vr.shutdown = (void *)dlsym(vr.library, "VR_ShutdownInternal");
	vr.get_interface = (void *)dlsym(vr.library, "VR_GetGenericInterface");
	vr.error_text = (void *)dlsym(vr.library, "VR_GetVRInitErrorAsEnglishDescription");
	if (!vr.init || !vr.shutdown || !vr.get_interface || !vr.error_text)
	{
		host_logf(HOST_LOG_WARN, "gyro: the OpenVR library lacks its entry points");
		return false;
	}
	return true;
}

static bool vr_connect(EVRInitError *error)
{
	char version[64];

	*error = EVRInitError_VRInitError_None;
	vr.init(error, EVRApplicationType_VRApplication_Background, NULL);
	if (*error != EVRInitError_VRInitError_None)
		return false;
	snprintf(version, sizeof(version), "FnTable:%s", IVRSystem_Version);
	vr.system = (struct VR_IVRSystem_FnTable *)vr.get_interface(version, error);
	if (!vr.system || *error != EVRInitError_VRInitError_None)
	{
		vr.system = NULL;
		vr.shutdown();
		return false;
	}
	return true;
}

static void vr_disconnect(void)
{
	if (vr.system)
	{
		vr.system = NULL;
		vr.shutdown();
	}
}

int frame_gyro_probe(void)
{
	EVRInitError error;

	if (!vr_load())
		return 1;
	if (!vr_connect(&error))
	{
		host_logf(HOST_LOG_INFO, "gyro probe: SteamVR not available (%d: %s)", (int)error, vr.error_text(error));
		return 2;
	}
	host_logf(HOST_LOG_INFO, "gyro probe: connected (right controller: device %u)",
		(unsigned)vr.system->GetTrackedDeviceIndexForControllerRole(
			ETrackedControllerRole_TrackedControllerRole_RightHand));
	vr_disconnect();
	return 0;
}

extern char **environ;

/* runs `halo --gyro-probe`: 0 connected, 2 SteamVR not there (try again
later), -1 it crashed, hung or could not run (leave SteamVR alone) */
static int probe_in_child(void)
{
	char *arguments[] = { "halo", "--gyro-probe", NULL };
	pid_t child;
	int status, waited;

	if (posix_spawn(&child, "/proc/self/exe", NULL, NULL, arguments, environ) != 0)
		return -1;
	for (waited = 0; waited < PROBE_SECONDS * 10; waited++)
	{
		pid_t done = waitpid(child, &status, WNOHANG);

		if (done == child)
		{
			if (WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 2))
				return WEXITSTATUS(status);
			host_logf(HOST_LOG_WARN, "gyro: the SteamVR probe failed (status %#x)", status);
			return -1;
		}
		usleep(100000);
	}
	kill(child, SIGKILL);
	waitpid(child, &status, 0);
	host_logf(HOST_LOG_WARN, "gyro: the SteamVR probe hung");
	return -1;
}

/* ---------- following the controller */

struct rotation
{
	float m[3][3];
};

/* the yaw (about the world's up) and pitch (about the controller's own
horizontal right) that turn the previous orientation into the current one */
static void rotation_turned(const struct rotation *previous, const struct rotation *current,
	float *yaw, float *pitch)
{
	float delta[3][3], axis[3], right[3];
	float sine, cosine, angle, scale, length;
	int i, j, k;

	/* delta = current * previous transposed, in the world's frame */
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
		{
			delta[i][j] = 0.0f;
			for (k = 0; k < 3; k++)
				delta[i][j] += current->m[i][k] * previous->m[j][k];
		}
	}
	axis[0] = 0.5f * (delta[2][1] - delta[1][2]);
	axis[1] = 0.5f * (delta[0][2] - delta[2][0]);
	axis[2] = 0.5f * (delta[1][0] - delta[0][1]);
	sine = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
	cosine = 0.5f * (delta[0][0] + delta[1][1] + delta[2][2] - 1.0f);
	angle = atan2f(sine, cosine);
	scale = sine > 1e-6f ? angle / sine : 1.0f;
	for (i = 0; i < 3; i++)
		axis[i] *= scale;

	/* the controller points down its -Z; its right, kept level, is
	forward x up */
	right[0] = current->m[2][2];
	right[1] = 0.0f;
	right[2] = -current->m[0][2];
	length = sqrtf(right[0] * right[0] + right[2] * right[2]);
	if (length < 0.2f)
	{
		/* pointing straight up or down: its own right axis, levelled */
		right[0] = current->m[0][0];
		right[2] = current->m[2][0];
		length = sqrtf(right[0] * right[0] + right[2] * right[2]);
	}
	*yaw = axis[1];
	*pitch = length > 1e-3f ? (axis[0] * right[0] + axis[2] * right[2]) / length : 0.0f;
}

static struct
{
	float yaw[SMOOTH_SAMPLES], pitch[SMOOTH_SAMPLES];
	int next;
} smoothing;

/* soft tiered smoothing: the part of each sample's turn below the
thresholds is averaged with the samples before it */
static void smooth(float seconds, float *yaw, float *pitch)
{
	float rate = sqrtf(*yaw * *yaw + *pitch * *pitch) / (seconds > 1e-4f ? seconds : 1e-4f);
	float direct = (rate * (180.0f / (float)M_PI) - SMOOTH_BELOW) / (DIRECT_ABOVE - SMOOTH_BELOW);
	float average_yaw = 0.0f, average_pitch = 0.0f;
	int i;

	direct = direct < 0.0f ? 0.0f : direct > 1.0f ? 1.0f : direct;
	smoothing.yaw[smoothing.next] = *yaw * (1.0f - direct);
	smoothing.pitch[smoothing.next] = *pitch * (1.0f - direct);
	smoothing.next = (smoothing.next + 1) % SMOOTH_SAMPLES;
	for (i = 0; i < SMOOTH_SAMPLES; i++)
	{
		average_yaw += smoothing.yaw[i];
		average_pitch += smoothing.pitch[i];
	}
	*yaw = *yaw * direct + average_yaw / SMOOTH_SAMPLES;
	*pitch = *pitch * direct + average_pitch / SMOOTH_SAMPLES;
}

static double now_seconds(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec + time.tv_nsec * 1e-9;
}

static void *gyro_thread(void *unused)
{
	TrackedDevicePose_t poses[k_unMaxTrackedDeviceCount];
	struct rotation previous;
	bool have_previous = false;
	TrackedDeviceIndex_t followed = k_unTrackedDeviceIndexInvalid;
	EVRInitError last_error = EVRInitError_VRInitError_None;
	double retry_at = 0.0, previous_at = 0.0;
	bool probed = false;

	(void)unused;
	if (!vr_load())
		return NULL;
	for (;;)
	{
		bool enabled;
		double now = now_seconds();

		pthread_mutex_lock(&gyro_lock);
		enabled = gyro_enabled;
		pthread_mutex_unlock(&gyro_lock);
		if (!enabled)
		{
			/* off: leave SteamVR alone entirely */
			if (vr.system)
			{
				vr_disconnect();
				host_logf(HOST_LOG_INFO, "gyro: off, disconnected from SteamVR");
			}
			have_previous = false;
			followed = k_unTrackedDeviceIndexInvalid;
			usleep(100000);
			continue;
		}
		if (!vr.system)
		{
			EVRInitError error;

			if (now < retry_at)
			{
				usleep(100000);
				continue;
			}
			if (!probed)
			{
				int probe = probe_in_child();

				if (probe < 0)
				{
					host_logf(HOST_LOG_WARN, "gyro: off for this run (the game is unaffected)");
					return NULL;
				}
				if (probe != 0)
				{
					retry_at = now + RETRY_SECONDS;
					continue;
				}
				probed = true;
			}
			if (!vr_connect(&error))
			{
				if (error != last_error)
					host_logf(HOST_LOG_WARN, "gyro: SteamVR not available (%d: %s), retrying every %d s",
						(int)error, vr.error_text(error), RETRY_SECONDS);
				last_error = error;
				retry_at = now + RETRY_SECONDS;
				continue;
			}
			last_error = EVRInitError_VRInitError_None;
			host_logf(HOST_LOG_INFO, "gyro: connected to SteamVR (%s)", IVRSystem_Version);
		}
		{
			struct VREvent_t event;

			while (vr.system->PollNextEvent(&event, sizeof(event)))
			{
				if (event.eventType == EVREventType_VREvent_Quit)
				{
					host_logf(HOST_LOG_INFO, "gyro: SteamVR is quitting, disconnecting");
					vr_disconnect();
					retry_at = now + RETRY_SECONDS;
					break;
				}
			}
			if (!vr.system)
			{
				have_previous = false;
				continue;
			}
		}
		{
			TrackedDeviceIndex_t right = vr.system->GetTrackedDeviceIndexForControllerRole(
				ETrackedControllerRole_TrackedControllerRole_RightHand);

			if (right != followed)
			{
				host_logf(HOST_LOG_INFO, right < k_unMaxTrackedDeviceCount ?
					"gyro: following the right controller (device %u)" : "gyro: no right controller (%u)",
					(unsigned)right);
				followed = right;
				have_previous = false;
			}
			if (right >= k_unMaxTrackedDeviceCount)
			{
				usleep(250000);
				continue;
			}
			vr.system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding, 0.0f,
				poses, k_unMaxTrackedDeviceCount);
			if (!poses[right].bPoseIsValid || !poses[right].bDeviceIsConnected)
			{
				have_previous = false;
			}
			else
			{
				struct rotation current;
				int i, j;

				for (i = 0; i < 3; i++)
					for (j = 0; j < 3; j++)
						current.m[i][j] = poses[right].mDeviceToAbsoluteTracking.m[i][j];
				if (have_previous)
				{
					float yaw, pitch;

					rotation_turned(&previous, &current, &yaw, &pitch);
					smooth((float)(now - previous_at), &yaw, &pitch);
					pthread_mutex_lock(&gyro_lock);
					pending_yaw += yaw;
					pending_pitch += pitch;
					pthread_mutex_unlock(&gyro_lock);
				}
				previous = current;
				previous_at = now;
				have_previous = true;
			}
		}
		usleep(SAMPLE_MICROSECONDS);
	}
	return NULL;
}

void frame_gyro_start(void)
{
	static bool started;
	pthread_t thread;

	if (started)
		return;
	started = true;
	settings_load();
	host_logf(HOST_LOG_INFO, "gyro: %s, speed %.2f (both sticks in: on/off; view+Y faster, view+A slower)",
		gyro_enabled ? "on" : "off", gyro_speed);
	if (pthread_create(&thread, NULL, gyro_thread, NULL) == 0)
		pthread_detach(thread);
}

/* ---------- the gamepad's combinations */

enum
{
	_hide_sticks = 1,
	_hide_faces = 2,
};
static int hidden;

static SDL_Gamepad *first_gamepad(void)
{
	SDL_Gamepad *gamepad = NULL;
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);

	if (ids && count > 0)
		gamepad = SDL_GetGamepadFromID(ids[0]);
	SDL_free(ids);
	return gamepad;
}

static void rumble(SDL_Gamepad *gamepad, Uint16 low, Uint16 high, Uint32 milliseconds)
{
	SDL_RumbleGamepad(gamepad, low, high, milliseconds);
}

static void change_speed(SDL_Gamepad *gamepad, float step)
{
	float speed;

	pthread_mutex_lock(&gyro_lock);
	speed = gyro_speed + step;
	speed = speed < SPEED_MIN ? SPEED_MIN : speed > SPEED_MAX ? SPEED_MAX : speed;
	gyro_speed = speed;
	pthread_mutex_unlock(&gyro_lock);
	settings_save();
	host_logf(HOST_LOG_INFO, "gyro: speed %.2f", speed);
	if (step > 0.0f)
		rumble(gamepad, 0, 0xffff, speed >= SPEED_MAX ? 400 : 80);
	else
		rumble(gamepad, 0xffff, 0, speed <= SPEED_MIN ? 400 : 80);
}

static void watch_combinations(void)
{
	static Uint64 sticks_since;
	static bool sticks_used, y_was, a_was;
	SDL_Gamepad *gamepad = first_gamepad();
	bool sticks, back, y, a;

	if (!gamepad)
		return;
	sticks = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
		SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
	back = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
	y = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
	a = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);

	/* both sticks: hidden from the game until both are let go of */
	if (sticks)
	{
		hidden |= _hide_sticks;
		if (!sticks_since)
			sticks_since = SDL_GetTicks();
		if (!sticks_used && SDL_GetTicks() - sticks_since >= COMBO_HOLD_MS)
		{
			bool enabled;

			sticks_used = true;
			pthread_mutex_lock(&gyro_lock);
			gyro_enabled = !gyro_enabled;
			enabled = gyro_enabled;
			pending_yaw = pending_pitch = 0.0f;
			pthread_mutex_unlock(&gyro_lock);
			settings_save();
			host_logf(HOST_LOG_INFO, "gyro: %s", enabled ? "on" : "off");
			if (enabled)
				rumble(gamepad, 0xffff, 0xffff, 350);
			else
				rumble(gamepad, 0x5000, 0, 120);
		}
	}
	else
	{
		sticks_since = 0;
		sticks_used = false;
		if (!SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
			!SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK))
		{
			hidden &= ~_hide_sticks;
		}
	}

	/* view held: Y and A change the speed instead of reaching the game */
	if (back)
	{
		hidden |= _hide_faces;
		if (y && !y_was)
			change_speed(gamepad, SPEED_STEP);
		if (a && !a_was)
			change_speed(gamepad, -SPEED_STEP);
	}
	else if (!y && !a)
	{
		hidden &= ~_hide_faces;
	}
	y_was = y;
	a_was = a;
}

bool frame_gyro_hides_button(int button)
{
	if ((hidden & _hide_sticks) &&
		(button == SDL_GAMEPAD_BUTTON_LEFT_STICK || button == SDL_GAMEPAD_BUTTON_RIGHT_STICK))
	{
		return true;
	}
	if ((hidden & _hide_faces) && (button == SDL_GAMEPAD_BUTTON_NORTH || button == SDL_GAMEPAD_BUTTON_SOUTH))
		return true;
	return false;
}

/* ---------- the aim, as mouse motion */

bool frame_gyro_event(SDL_Event *event)
{
	/* one motion event per emptying of the queue, so the guest's loop ends */
	static bool sent;
	Uint64 now = SDL_GetTicksNS();
	float yaw, pitch, speed;
	bool stale;

	if (sent)
	{
		sent = false;
		return false;
	}
	watch_combinations();
	pthread_mutex_lock(&gyro_lock);
	yaw = pending_yaw;
	pitch = pending_pitch;
	speed = gyro_speed;
	pending_yaw = pending_pitch = 0.0f;
	stale = now - taken_ns > STALE_NS;
	taken_ns = now;
	pthread_mutex_unlock(&gyro_lock);
	if (stale || (yaw == 0.0f && pitch == 0.0f))
		return false;
	SDL_zerop(event);
	event->type = SDL_EVENT_MOUSE_MOTION;
	event->motion.timestamp = now;
	event->motion.xrel = -yaw * speed / RADIANS_PER_PIXEL;
	event->motion.yrel = -pitch * speed / RADIANS_PER_PIXEL;
	sent = true;
	return true;
}
