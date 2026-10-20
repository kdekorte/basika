#include "platform.h"

#include <errno.h>
#include <time.h>

#if defined(__linux__)
#include <dlfcn.h>
#include <stdio.h>

/* Linux keeps the SDL symbols of graphics.c and audio.c unresolved at link
 * time; this loads the libraries into the global scope on first use, where
 * glibc binds their lazy PLT entries at the first call. */
static int sdl_state = 0; /* 0 = not tried yet, 1 = loaded, -1 = failed */

int platform_load_sdl(void) {
    if (sdl_state) return sdl_state == 1;
    /* The versioned names come from the distribution packages; the plain
     * .so names cover setups that only have the development symlinks. */
    static const char *const names[][2] = {
        {"libSDL3.so.0", "libSDL3.so"},
        {"libSDL3_ttf.so.0", "libSDL3_ttf.so"},
        {"libSDL3_mixer.so.0", "libSDL3_mixer.so"},
        {"libSDL3_image.so.0", "libSDL3_image.so"},
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        void *handle = dlopen(names[i][0], RTLD_LAZY | RTLD_GLOBAL);
        if (!handle) handle = dlopen(names[i][1], RTLD_LAZY | RTLD_GLOBAL);
        if (!handle) {
            fprintf(stderr, "basika: cannot load %s: %s (graphics and sound need SDL3 installed)\n",
                    names[i][0], dlerror());
            ok = 0;
        }
    }
    sdl_state = ok ? 1 : -1;
    return ok;
}

#else /* macOS links the SDL libraries as lazy-load dylibs, other systems
         link them normally; either way they are there when needed. */

int platform_load_sdl(void) { return 1; }

#endif

/* The timing helpers use the system monotonic clock instead of SDL's, so
 * SLEEP, _DELAY, _LIMIT and the sound waits never load SDL by themselves. */

unsigned long long platform_ticks_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
}

void platform_delay_ns(unsigned long long ns) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

void platform_delay_ms(int ms) {
    if (ms > 0) platform_delay_ns((unsigned long long)ms * 1000000ULL);
}
