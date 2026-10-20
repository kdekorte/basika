#ifndef PLATFORM_H
#define PLATFORM_H

/* Deferred SDL loading and SDL-free timing.
 *
 * macOS links the SDL libraries as lazy-load dylibs and glibc Linux leaves
 * their symbols unresolved, so a console-only program neither loads nor
 * needs SDL. graphics.c and audio.c call platform_load_sdl() before their
 * first SDL use; on Linux it dlopen()s the libraries into the global scope,
 * where the dynamic linker binds the still-open calls, elsewhere the
 * libraries are already available.
 *
 * The timing helpers wrap the system monotonic clock and nanosleep, so
 * SLEEP, _DELAY, _LIMIT and the sound waits work without SDL as well. */

int platform_load_sdl(void);
void platform_delay_ms(int ms);
void platform_delay_ns(unsigned long long ns);
unsigned long long platform_ticks_ns(void);

#endif
