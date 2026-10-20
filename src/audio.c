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

/* QB64 sound handles (_SNDOPEN and the other _SND commands). Each handle
 * holds the loaded audio and its own track, so it can be played, looped,
 * paused and stopped; _SNDPLAYCOPY plays on a pool of extra tracks so copies
 * overlap. Without audio output the sounds still load (for _SNDLEN and to
 * report bad files) but have no track and never play. */
#define MAX_SOUND_HANDLES 256
#define MAX_SOUND_COPIES 32

typedef struct {
    MIX_Audio *audio;
    MIX_Track *track; /* NULL without audio output */
} SoundHandle;

static SoundHandle sound_handles[MAX_SOUND_HANDLES + 1]; /* handle 0 is never used */
static MIX_Track *copy_tracks[MAX_SOUND_COPIES];
static int next_copy_track = 0;

static void close_all_sounds(void) {
    for (int i = 1; i <= MAX_SOUND_HANDLES; i++) {
        if (sound_handles[i].track) MIX_DestroyTrack(sound_handles[i].track);
        if (sound_handles[i].audio) MIX_DestroyAudio(sound_handles[i].audio);
        sound_handles[i] = (SoundHandle){0};
    }
    for (int i = 0; i < MAX_SOUND_COPIES; i++) {
        if (copy_tracks[i]) MIX_DestroyTrack(copy_tracks[i]);
        copy_tracks[i] = NULL;
    }
}

static void audio_shutdown(void) {
    close_all_sounds();
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

/* Opening the mixer and output device takes a noticeable moment (about a
 * quarter second on macOS); doing it before a program runs keeps the first
 * PLAY or SOUND from stalling it. */
void audio_prepare(void) {
    if (audio_enabled) audio_init();
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

static int buffer_append_tone(AudioBuffer *buffer, double frequency, int ms, int volume) {
    if (ms <= 0) return 1;
    size_t samples = ((size_t)AUDIO_RATE * (size_t)ms) / 1000;
    if (samples == 0) samples = 1;
    size_t start = buffer->length;
    if (!buffer_append_silence(buffer, samples)) return 0;

    double phase = 0.0;
    double step = frequency / AUDIO_RATE;
    size_t fade_samples = (size_t)AUDIO_RATE * 3 / 1000;
    if (fade_samples > samples / 2) fade_samples = samples / 2;
    int amplitude = AUDIO_AMPLITUDE * volume / 15;
    for (size_t i = 0; i < samples; i++) {
        double envelope = 1.0;
        if (fade_samples > 1 && i < fade_samples) {
            envelope = (double)i / (fade_samples - 1);
        } else if (fade_samples > 1 && samples - i <= fade_samples) {
            envelope = (double)(samples - 1 - i) / (fade_samples - 1);
        }
        int sample = (int)(amplitude * envelope);
        buffer->samples[start + i] = (Sint16)((phase < 0.5) ? sample : -sample);
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
        if (buffer_append_tone(&buffer, frequency, ms, 15)) {
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

static int append_note(AudioBuffer *buffer, double frequency, int duration_ms, Articulation articulation, int volume) {
    int sound_ms = duration_ms;
    if (articulation == ARTICULATION_NORMAL) sound_ms = (duration_ms * 7 + 4) / 8;
    else if (articulation == ARTICULATION_STACCATO) sound_ms = (duration_ms * 3 + 2) / 4;

    if (!buffer_append_tone(buffer, frequency, sound_ms, volume)) return 0;
    return append_rest(buffer, duration_ms - sound_ms);
}

void audio_play(const char *mml) {
    int tempo = 120;
    int octave = 4;
    int default_length = 4;
    int volume = 15;
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
        } else if (cmd == 'V') {
            int value = read_number(&p);
            if (value >= 0 && value <= 15) volume = value;
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
                    buffer_ok = append_note(&buffer, note_frequency(number - 1, 0), ms, articulation, volume);
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
                buffer_ok = append_note(&buffer, note_frequency(semitone, octave), ms, articulation, volume);
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

/* Whether sounds can be heard: _SND commands that play do nothing otherwise. */
static int sound_output(void) {
    return audio_enabled && audio_init();
}

static SoundHandle *sound_handle(int handle) {
    if (handle < 1 || handle > MAX_SOUND_HANDLES || !sound_handles[handle].audio) return NULL;
    return &sound_handles[handle];
}

int audio_sound_open(const char *path) {
    int handle = 1;
    while (handle <= MAX_SOUND_HANDLES && sound_handles[handle].audio) handle++;
    if (handle > MAX_SOUND_HANDLES || !path || !path[0]) return 0;
    if (!mixer_initialized) {
        if (!MIX_Init()) return 0;
        mixer_initialized = 1;
        if (!shutdown_registered) {
            atexit(audio_shutdown);
            shutdown_registered = 1;
        }
    }
    MIX_Mixer *output = sound_output() ? mixer : NULL;
    MIX_Audio *audio = MIX_LoadAudio(output, path, true);
    if (!audio) return 0;
    MIX_Track *track = NULL;
    if (output) {
        track = MIX_CreateTrack(output);
        if (!track || !MIX_SetTrackAudio(track, audio)) {
            if (track) MIX_DestroyTrack(track);
            MIX_DestroyAudio(audio);
            return 0;
        }
    }
    sound_handles[handle] = (SoundHandle){audio, track};
    return handle;
}

int audio_sound_valid(int handle) {
    return sound_handle(handle) != NULL;
}

int audio_sound_close(int handle) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    // Copies still playing keep the audio until they finish.
    if (sound->track) MIX_DestroyTrack(sound->track);
    MIX_DestroyAudio(sound->audio);
    *sound = (SoundHandle){0};
    return 1;
}

/* _SNDPLAY plays a stopped sound from the start and resumes a paused one; a
 * sound already playing carries on. _SNDLOOP does the same but repeats it. */
int audio_sound_play(int handle, int loop) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    if (!sound->track) return 1;
    if (MIX_TrackPaused(sound->track)) {
        MIX_ResumeTrack(sound->track);
        if (loop) MIX_SetTrackLoops(sound->track, -1);
    } else if (MIX_TrackPlaying(sound->track)) {
        if (loop) MIX_SetTrackLoops(sound->track, -1);
    } else {
        SDL_PropertiesID options = SDL_CreateProperties();
        if (loop) SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        MIX_PlayTrack(sound->track, options);
        SDL_DestroyProperties(options);
    }
    return 1;
}

int audio_sound_stop(int handle) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    if (sound->track) MIX_StopTrack(sound->track, 0);
    return 1;
}

int audio_sound_pause(int handle) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    if (sound->track && MIX_TrackPlaying(sound->track)) MIX_PauseTrack(sound->track);
    return 1;
}

static float clamp_volume(double volume) {
    if (!(volume > 0)) return 0;
    return volume > 1 ? 1 : (float)volume;
}

/* volume runs from 0 (silent) to 1 (full), as in QB64. */
int audio_sound_volume(int handle, double volume) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    if (sound->track) MIX_SetTrackGain(sound->track, clamp_volume(volume));
    return 1;
}

/* Plays a copy of the sound on a free copy track, so the same sound can
 * overlap itself (footsteps, shots); with every copy track busy the oldest
 * copy is cut off. */
int audio_sound_play_copy(int handle, double volume) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    if (!sound->track) return 1;
    int slot = -1;
    for (int i = 0; i < MAX_SOUND_COPIES && slot < 0; i++) {
        int candidate = (next_copy_track + i) % MAX_SOUND_COPIES;
        if (!copy_tracks[candidate] || !MIX_TrackPlaying(copy_tracks[candidate])) slot = candidate;
    }
    if (slot < 0) slot = next_copy_track;
    next_copy_track = (slot + 1) % MAX_SOUND_COPIES;
    if (!copy_tracks[slot]) {
        copy_tracks[slot] = MIX_CreateTrack(mixer);
        if (!copy_tracks[slot]) return 1;
    }
    MIX_StopTrack(copy_tracks[slot], 0);
    if (MIX_SetTrackAudio(copy_tracks[slot], sound->audio)) {
        MIX_SetTrackGain(copy_tracks[slot], clamp_volume(volume));
        MIX_PlayTrack(copy_tracks[slot], 0);
    }
    return 1;
}

int audio_sound_playing(int handle) {
    SoundHandle *sound = sound_handle(handle);
    return sound && sound->track && MIX_TrackPlaying(sound->track);
}

int audio_sound_paused(int handle) {
    SoundHandle *sound = sound_handle(handle);
    return sound && sound->track && MIX_TrackPaused(sound->track);
}

/* Length in seconds; -1 if it cannot be known (some streamed formats). */
double audio_sound_length(int handle) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound) return 0;
    Sint64 frames = MIX_GetAudioDuration(sound->audio);
    if (frames < 0) return -1;
    return MIX_AudioFramesToMS(sound->audio, frames) / 1000.0;
}

/* Playback position in seconds. */
double audio_sound_position(int handle) {
    SoundHandle *sound = sound_handle(handle);
    if (!sound || !sound->track) return 0;
    Sint64 frames = MIX_GetTrackPlaybackPosition(sound->track);
    if (frames <= 0) return 0;
    return MIX_TrackFramesToMS(sound->track, frames) / 1000.0;
}
