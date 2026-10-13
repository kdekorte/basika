#ifndef AUDIO_H
#define AUDIO_H

void audio_sound(double frequency, double duration_ticks);
void audio_play(const char *mml);
void audio_prepare(void);
void audio_set_enabled(int enabled);
int audio_is_enabled(void);

/* QB64 sound handles; the int functions return 0 for a handle that is not open. */
int audio_sound_open(const char *path);
int audio_sound_valid(int handle);
int audio_sound_close(int handle);
int audio_sound_play(int handle, int loop);
int audio_sound_stop(int handle);
int audio_sound_pause(int handle);
int audio_sound_volume(int handle, double volume);
int audio_sound_play_copy(int handle, double volume);
int audio_sound_playing(int handle);
int audio_sound_paused(int handle);
double audio_sound_length(int handle);
double audio_sound_position(int handle);

#endif
