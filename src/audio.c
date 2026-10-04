#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

#define AUDIO_RATE 44100
#define AUDIO_AMPLITUDE 9000
#define BASIKA_TICKS_PER_SECOND 18.2

typedef struct {
    Sint16 *samples;
    size_t length;
    size_t capacity;
} AudioBuffer;

typedef enum {
    ARTICULATION_NORMAL,
    ARTICULATION_LEGATO,
    ARTICULATION_STACCATO
} Articulation;

static MIX_Mixer *mixer = NULL;
static int audio_attempted = 0;
static int audio_ready = 0;
static int mixer_initialized = 0;
static int shutdown_registered = 0;
static int audio_enabled = 1;
static int music_foreground = 1;

static void audio_shutdown(void) {
    if (mixer) {
        MIX_DestroyMixer(mixer);
        mixer = NULL;
    }
    if (mixer_initialized) {
        MIX_Quit();
        mixer_initialized = 0;
    }
}

static int audio_init(void) {
    if (audio_attempted) return audio_ready;
    audio_attempted = 1;

    if (!MIX_Init()) return 0;
    mixer_initialized = 1;
    if (!shutdown_registered) {
        atexit(audio_shutdown);
        shutdown_registered = 1;
    }

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = AUDIO_RATE;

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!mixer) return 0;

    audio_ready = 1;
    return 1;
}

void audio_set_enabled(int enabled) {
    audio_enabled = enabled != 0;
}

int audio_is_enabled(void) {
    return audio_enabled;
}

static int ticks_to_ms(double ticks) {
    if (ticks <= 0) return 0;
    return (int)((ticks * 1000.0 / BASIKA_TICKS_PER_SECOND) + 0.5);
}

static void delay_ms(int ms) {
    if (ms > 0) SDL_Delay((Uint32)ms);
}

static int buffer_append_silence(AudioBuffer *buffer, size_t samples) {
    if (samples > SIZE_MAX - buffer->length) return 0;
    size_t needed = buffer->length + samples;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : AUDIO_RATE;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(*buffer->samples)) return 0;
        Sint16 *resized = (Sint16 *)SDL_realloc(buffer->samples, capacity * sizeof(*buffer->samples));
        if (!resized) return 0;
        buffer->samples = resized;
        buffer->capacity = capacity;
    }
    memset(buffer->samples + buffer->length, 0, samples * sizeof(*buffer->samples));
    buffer->length = needed;
    return 1;
}

static int buffer_append_tone(AudioBuffer *buffer, double frequency, int ms) {
    if (ms <= 0) return 1;
    size_t samples = ((size_t)AUDIO_RATE * (size_t)ms) / 1000;
    if (samples == 0) samples = 1;
    size_t start = buffer->length;
    if (!buffer_append_silence(buffer, samples)) return 0;

    double phase = 0.0;
    double step = frequency / AUDIO_RATE;
    for (size_t i = 0; i < samples; i++) {
        buffer->samples[start + i] = (phase < 0.5) ? AUDIO_AMPLITUDE : -AUDIO_AMPLITUDE;
        phase += step;
        phase -= floor(phase);
    }
    return 1;
}

static MIX_Audio *make_buffer_audio(AudioBuffer *buffer) {
    if (!buffer->length || buffer->length > SIZE_MAX / sizeof(*buffer->samples)) return NULL;

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = AUDIO_RATE;
    MIX_Audio *audio = MIX_LoadRawAudioNoCopy(
        mixer,
        buffer->samples,
        buffer->length * sizeof(*buffer->samples),
        &spec,
        true
    );
    if (audio) buffer->samples = NULL;
    return audio;
}

void audio_sound(double frequency, double duration_ticks) {
    int ms = ticks_to_ms(duration_ticks);
    if (frequency <= 0 || ms <= 0 || !audio_enabled) return;

    if (audio_init()) {
        AudioBuffer buffer = {0};
        if (buffer_append_tone(&buffer, frequency, ms)) {
            MIX_Audio *audio = make_buffer_audio(&buffer);
            if (audio && MIX_PlayAudio(mixer, audio)) {
                MIX_DestroyAudio(audio);
                SDL_free(buffer.samples);
                delay_ms(ms);
                return;
            }
            if (audio) MIX_DestroyAudio(audio);
        }
        SDL_free(buffer.samples);
    }

    delay_ms(ms);
}

static int read_number(const char **p) {
    int value = 0;
    while (isdigit((unsigned char)**p)) {
        int digit = **p - '0';
        if (value > (INT_MAX - digit) / 10) value = INT_MAX;
        else value = value * 10 + digit;
        (*p)++;
    }
    return value;
}

static double note_frequency(int semitone, int octave) {
    int midi = (octave + 1) * 12 + semitone;
    return 440.0 * pow(2.0, (midi - 69) / 12.0);
}

static int note_ms(int tempo, int length, int dots) {
    double ms = (60000.0 / tempo) * (4.0 / length);
    double add = ms / 2.0;
    for (int i = 0; i < dots; i++) {
        ms += add;
        add /= 2.0;
    }
    return (int)(ms + 0.5);
}

static int append_rest(AudioBuffer *buffer, int ms) {
    size_t samples = ((size_t)AUDIO_RATE * (size_t)ms) / 1000;
    return buffer_append_silence(buffer, samples);
}

static int append_note(AudioBuffer *buffer, double frequency, int duration_ms, Articulation articulation) {
    int sound_ms = duration_ms;
    if (articulation == ARTICULATION_NORMAL) sound_ms = (duration_ms * 7 + 4) / 8;
    else if (articulation == ARTICULATION_STACCATO) sound_ms = (duration_ms * 3 + 2) / 4;

    if (!buffer_append_tone(buffer, frequency, sound_ms)) return 0;
    return append_rest(buffer, duration_ms - sound_ms);
}

void audio_play(const char *mml) {
    int tempo = 120;
    int octave = 4;
    int default_length = 4;
    Articulation articulation = ARTICULATION_NORMAL;
    int total_ms = 0;
    int buffer_ok = 1;
    AudioBuffer buffer = {0};
    const char *p = mml ? mml : "";

    while (*p) {
        char cmd = (char)toupper((unsigned char)*p++);

        if (isspace((unsigned char)cmd)) continue;

        if (cmd == 'T') {
            int value = read_number(&p);
            if (value >= 32 && value <= 255) tempo = value;
        } else if (cmd == 'O') {
            int value = read_number(&p);
            if (value >= 0 && value <= 6) octave = value;
        } else if (cmd == 'L') {
            int value = read_number(&p);
            if (value >= 1 && value <= 64) default_length = value;
        } else if (cmd == 'M') {
            char mode = (char)toupper((unsigned char)*p);
            if (mode == 'B' || mode == 'F' || mode == 'N' || mode == 'L' || mode == 'S') {
                p++;
                if (mode == 'B') music_foreground = 0;
                else if (mode == 'F') music_foreground = 1;
                else if (mode == 'N') articulation = ARTICULATION_NORMAL;
                else if (mode == 'L') articulation = ARTICULATION_LEGATO;
                else articulation = ARTICULATION_STACCATO;
            }
        } else if (cmd == '>') {
            if (octave < 6) octave++;
        } else if (cmd == '<') {
            if (octave > 0) octave--;
        } else if (cmd == 'P' || cmd == 'R') {
            int length = read_number(&p);
            if (length <= 0) length = default_length;
            int dots = 0;
            while (*p == '.') {
                dots++;
                p++;
            }
            int ms = note_ms(tempo, length, dots);
            if (ms <= INT_MAX - total_ms) total_ms += ms;
            else total_ms = INT_MAX;
            if (buffer_ok) buffer_ok = append_rest(&buffer, ms);
        } else if (cmd == 'N') {
            int number = read_number(&p);
            int ms = note_ms(tempo, default_length, 0);
            if (ms <= INT_MAX - total_ms) total_ms += ms;
            else total_ms = INT_MAX;
            if (buffer_ok) {
                if (number == 0) buffer_ok = append_rest(&buffer, ms);
                else if (number >= 1 && number <= 84) {
                    buffer_ok = append_note(&buffer, note_frequency(number - 1, 0), ms, articulation);
                }
            }
        } else if (cmd >= 'A' && cmd <= 'G') {
            static const int notes[] = {9, 11, 0, 2, 4, 5, 7};
            int semitone = notes[cmd - 'A'];
            if (*p == '#' || *p == '+') {
                semitone++;
                p++;
            } else if (*p == '-') {
                semitone--;
                p++;
            }

            int length = read_number(&p);
            if (length <= 0) length = default_length;
            int dots = 0;
            while (*p == '.') {
                dots++;
                p++;
            }
            int ms = note_ms(tempo, length, dots);
            if (ms <= INT_MAX - total_ms) total_ms += ms;
            else total_ms = INT_MAX;
            if (buffer_ok) {
                buffer_ok = append_note(&buffer, note_frequency(semitone, octave), ms, articulation);
            }
        }
    }

    if (audio_enabled && buffer_ok && buffer.length && audio_init()) {
        MIX_Audio *audio = make_buffer_audio(&buffer);
        if (audio) {
            if (MIX_PlayAudio(mixer, audio)) {
                MIX_DestroyAudio(audio);
                SDL_free(buffer.samples);
                buffer.samples = NULL;
            } else {
                MIX_DestroyAudio(audio);
            }
        }
    }
    SDL_free(buffer.samples);

    if (audio_enabled && music_foreground) delay_ms(total_ms);
}
