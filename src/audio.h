#ifndef AUDIO_H
#define AUDIO_H

void audio_sound(double frequency, double duration_ticks);
void audio_play(const char *mml);
void audio_prepare(void);
void audio_set_enabled(int enabled);
int audio_is_enabled(void);

#endif
