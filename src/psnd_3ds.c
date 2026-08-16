/*
 * YEET32A Nintendo 3DS SDL2 sound backend.
 *
 * Loads AvP's embedded PCM WAV samples without expanding them in memory,
 * then mixes software voices into SDL2's stereo S16 output callback.
 */

#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3dc.h"
#include "inline.h"
#include "psndplat.h"
#include "gamedef.h"
#include "avpview.h"
#include "ffstdio.h"

#define AVP3DS_AUDIO_RATE       44100
#define AVP3DS_AUDIO_CHANNELS   2
#define AVP3DS_AUDIO_SAMPLES    1024
#define AVP3DS_POSITION_ONE     UINT64_C(4294967296)

typedef struct AvP3DS_Sample
{
    unsigned char *data;
    uint32_t bytes;
    uint32_t frames;
    uint32_t sample_rate;
    uint16_t block_align;
    uint8_t channels;
    uint8_t bits_per_sample;
} AvP3DS_Sample;

typedef struct AvP3DS_Voice
{
    const AvP3DS_Sample *sample;
    uint64_t position;
    uint64_t step;
    int active;
    int loop;
    int volume;
} AvP3DS_Voice;

ACTIVESOUNDSAMPLE ActiveSounds[SOUND_MAXACTIVE];

ACTIVESOUNDSAMPLE BlankActiveSound =
{
    .soundIndex = SID_NOSOUND,
    .priority = ASP_Minimum
};

SOUNDSAMPLEDATA BlankGameSound = {0};
SOUNDSAMPLEDATA GameSounds[SID_MAXIMUM];

static SDL_AudioDeviceID avp3ds_audio_device = 0;
static SDL_AudioSpec avp3ds_audio_spec;
static AvP3DS_Voice avp3ds_voices[SOUND_MAXACTIVE];

static int avp3ds_sound_active = 0;
static int avp3ds_global_volume = VOLUME_DEFAULT;

/* YEET32B audio diagnostics */
static unsigned int avp3ds_diag_loaded = 0;
static unsigned int avp3ds_diag_load_failed = 0;
static unsigned int avp3ds_diag_play_requested = 0;
static unsigned int avp3ds_diag_play_started = 0;
static unsigned int avp3ds_diag_play_unloaded = 0;
static unsigned int avp3ds_diag_play_no_buffer = 0;
static unsigned int avp3ds_diag_peak_active = 0;
static unsigned int avp3ds_diag_failure_prints = 0;
static unsigned int avp3ds_diag_frames = 0;

extern int WantSound;

static uint16_t AvP3DS_ReadLE16(const unsigned char *data);
static uint32_t AvP3DS_ReadLE32(const unsigned char *data);
static int16_t AvP3DS_ClampSample(int value);

/*
 * YEET34A streamed CDDA replacement.
 *
 * Converted music tracks are ordinary 44100 Hz, stereo, signed
 * 16-bit PCM WAV files stored as Music/Track01.wav ... Track15.wav.
 *
 * File reads happen on the main thread. The SDL callback only consumes
 * a prefilled ring buffer, so no SD access occurs inside the audio thread.
 */
#define AVP3DS_CDDA_TRACK_COUNT       15
#define AVP3DS_CDDA_RING_FRAMES       32768U
#define AVP3DS_CDDA_PUMP_FRAMES       4096U
#define AVP3DS_CDDA_FRAME_BYTES       4U
#define AVP3DS_CDDA_VOLUME_DEFAULT    127

typedef struct AvP3DS_CDDAState
{
    FILE *file;
    long data_offset;
    uint32_t data_bytes;
    uint32_t bytes_remaining;

    uint32_t read_frame;
    uint32_t write_frame;
    uint32_t buffered_frames;

    int on;
    int playing;
    int looping;
    int eof;
    int current_track;
} AvP3DS_CDDAState;

static AvP3DS_CDDAState avp3ds_cdda =
{
    .current_track = -1
};

static int16_t avp3ds_cdda_ring[
    AVP3DS_CDDA_RING_FRAMES * AVP3DS_AUDIO_CHANNELS];

static unsigned char avp3ds_cdda_pump_buffer[
    AVP3DS_CDDA_PUMP_FRAMES * AVP3DS_CDDA_FRAME_BYTES];

static unsigned int avp3ds_cdda_error_prints = 0;

int CDPlayerVolume = AVP3DS_CDDA_VOLUME_DEFAULT;

static void AvP3DS_CDDALock(void)
{
    if (avp3ds_audio_device != 0)
        SDL_LockAudioDevice(avp3ds_audio_device);
}

static void AvP3DS_CDDAUnlock(void)
{
    if (avp3ds_audio_device != 0)
        SDL_UnlockAudioDevice(avp3ds_audio_device);
}

static void AvP3DS_CDDACloseFile(void)
{
    if (avp3ds_cdda.file != NULL)
    {
        fclose(avp3ds_cdda.file);
        avp3ds_cdda.file = NULL;
    }
}

static int AvP3DS_CDDAOpenTrack(int track)
{
    char filename[64];
    unsigned char riff_header[12];
    unsigned char chunk_header[8];
    unsigned char fmt_data[16];
    FILE *file;
    int fmt_valid = 0;

    snprintf(
        filename,
        sizeof(filename),
        "Music/Track%02d.wav",
        track);

    file = OpenGameFile(
        filename,
        FILEMODE_READONLY,
        FILETYPE_OPTIONAL);

    if (file == NULL)
    {
        if (avp3ds_cdda_error_prints < 16U)
        {
            printf("YEET34A music missing: %s\n", filename);
            ++avp3ds_cdda_error_prints;
        }

        return 0;
    }

    if (fread(riff_header, 1, sizeof(riff_header), file)
            != sizeof(riff_header) ||
        memcmp(riff_header, "RIFF", 4) != 0 ||
        memcmp(riff_header + 8, "WAVE", 4) != 0)
    {
        fclose(file);
        printf("YEET34A invalid WAV: %s\n", filename);
        return 0;
    }

    while (fread(chunk_header, 1, sizeof(chunk_header), file)
            == sizeof(chunk_header))
    {
        uint32_t chunk_bytes =
            AvP3DS_ReadLE32(chunk_header + 4);

        if (memcmp(chunk_header, "fmt ", 4) == 0)
        {
            uint16_t format;
            uint16_t channels;
            uint32_t rate;
            uint16_t block_align;
            uint16_t bits;

            if (chunk_bytes < sizeof(fmt_data) ||
                fread(fmt_data, 1, sizeof(fmt_data), file)
                    != sizeof(fmt_data))
            {
                fclose(file);
                return 0;
            }

            format = AvP3DS_ReadLE16(fmt_data + 0);
            channels = AvP3DS_ReadLE16(fmt_data + 2);
            rate = AvP3DS_ReadLE32(fmt_data + 4);
            block_align = AvP3DS_ReadLE16(fmt_data + 12);
            bits = AvP3DS_ReadLE16(fmt_data + 14);

            fmt_valid =
                format == 1 &&
                channels == AVP3DS_AUDIO_CHANNELS &&
                rate == AVP3DS_AUDIO_RATE &&
                block_align == AVP3DS_CDDA_FRAME_BYTES &&
                bits == 16;

            if (chunk_bytes > sizeof(fmt_data))
            {
                fseek(
                    file,
                    (long)(chunk_bytes - sizeof(fmt_data)),
                    SEEK_CUR);
            }

            if (chunk_bytes & 1U)
                fseek(file, 1L, SEEK_CUR);
        }
        else if (memcmp(chunk_header, "data", 4) == 0)
        {
            if (!fmt_valid)
            {
                fclose(file);
                printf(
                    "YEET34A unsupported WAV format: %s\n",
                    filename);
                return 0;
            }

            avp3ds_cdda.file = file;
            avp3ds_cdda.data_offset = ftell(file);
            avp3ds_cdda.data_bytes =
                chunk_bytes -
                (chunk_bytes % AVP3DS_CDDA_FRAME_BYTES);
            avp3ds_cdda.bytes_remaining =
                avp3ds_cdda.data_bytes;

            return 1;
        }
        else
        {
            fseek(
                file,
                (long)chunk_bytes + (long)(chunk_bytes & 1U),
                SEEK_CUR);
        }
    }

    fclose(file);
    printf("YEET34A WAV data missing: %s\n", filename);
    return 0;
}

static void AvP3DS_CDDAPumpInternal(void)
{
    while (avp3ds_cdda.file != NULL)
    {
        uint32_t free_frames;
        uint32_t frames_wanted;
        uint32_t bytes_wanted;
        size_t bytes_read;
        uint32_t frames_read;
        uint32_t first_frames;
        uint32_t second_frames;

        AvP3DS_CDDALock();

        free_frames =
            AVP3DS_CDDA_RING_FRAMES -
            avp3ds_cdda.buffered_frames;

        AvP3DS_CDDAUnlock();

        if (free_frames == 0)
            return;

        if (avp3ds_cdda.bytes_remaining == 0)
        {
            if (avp3ds_cdda.looping)
            {
                if (fseek(
                        avp3ds_cdda.file,
                        avp3ds_cdda.data_offset,
                        SEEK_SET) != 0)
                {
                    AvP3DS_CDDACloseFile();

                    AvP3DS_CDDALock();
                    avp3ds_cdda.eof = 1;
                    AvP3DS_CDDAUnlock();
                    return;
                }

                avp3ds_cdda.bytes_remaining =
                    avp3ds_cdda.data_bytes;
            }
            else
            {
                AvP3DS_CDDACloseFile();

                AvP3DS_CDDALock();
                avp3ds_cdda.eof = 1;
                AvP3DS_CDDAUnlock();
                return;
            }
        }

        frames_wanted = free_frames;

        if (frames_wanted > AVP3DS_CDDA_PUMP_FRAMES)
            frames_wanted = AVP3DS_CDDA_PUMP_FRAMES;

        bytes_wanted =
            frames_wanted * AVP3DS_CDDA_FRAME_BYTES;

        if (bytes_wanted > avp3ds_cdda.bytes_remaining)
            bytes_wanted = avp3ds_cdda.bytes_remaining;

        bytes_read = fread(
            avp3ds_cdda_pump_buffer,
            1,
            bytes_wanted,
            avp3ds_cdda.file);

        bytes_read -=
            bytes_read % AVP3DS_CDDA_FRAME_BYTES;

        if (bytes_read == 0)
        {
            avp3ds_cdda.bytes_remaining = 0;
            continue;
        }

        frames_read =
            (uint32_t)bytes_read /
            AVP3DS_CDDA_FRAME_BYTES;

        AvP3DS_CDDALock();

        first_frames = frames_read;

        if (first_frames >
            AVP3DS_CDDA_RING_FRAMES -
            avp3ds_cdda.write_frame)
        {
            first_frames =
                AVP3DS_CDDA_RING_FRAMES -
                avp3ds_cdda.write_frame;
        }

        memcpy(
            &avp3ds_cdda_ring[
                avp3ds_cdda.write_frame *
                AVP3DS_AUDIO_CHANNELS],
            avp3ds_cdda_pump_buffer,
            first_frames * AVP3DS_CDDA_FRAME_BYTES);

        second_frames = frames_read - first_frames;

        if (second_frames != 0)
        {
            memcpy(
                &avp3ds_cdda_ring[0],
                avp3ds_cdda_pump_buffer +
                    first_frames *
                    AVP3DS_CDDA_FRAME_BYTES,
                second_frames *
                    AVP3DS_CDDA_FRAME_BYTES);
        }

        avp3ds_cdda.write_frame =
            (avp3ds_cdda.write_frame + frames_read) %
            AVP3DS_CDDA_RING_FRAMES;

        avp3ds_cdda.buffered_frames += frames_read;

        AvP3DS_CDDAUnlock();

        avp3ds_cdda.bytes_remaining -=
            (uint32_t)bytes_read;

        if (bytes_read < bytes_wanted)
            avp3ds_cdda.bytes_remaining = 0;
    }
}

static void AvP3DS_MixCDDA(
    int16_t *output,
    int output_frames)
{
    int output_frame;

    if (!avp3ds_cdda.on ||
        !avp3ds_cdda.playing)
    {
        return;
    }

    for (output_frame = 0;
         output_frame < output_frames;
         ++output_frame)
    {
        int left;
        int right;
        int mixed_left;
        int mixed_right;
        uint32_t ring_index;

        if (avp3ds_cdda.buffered_frames == 0)
        {
            if (avp3ds_cdda.eof)
                avp3ds_cdda.playing = 0;

            break;
        }

        ring_index =
            avp3ds_cdda.read_frame *
            AVP3DS_AUDIO_CHANNELS;

        left = avp3ds_cdda_ring[ring_index + 0];
        right = avp3ds_cdda_ring[ring_index + 1];

        left =
            (left * CDPlayerVolume) /
            AVP3DS_CDDA_VOLUME_DEFAULT;

        right =
            (right * CDPlayerVolume) /
            AVP3DS_CDDA_VOLUME_DEFAULT;

        mixed_left =
            output[output_frame * 2 + 0] + left;

        mixed_right =
            output[output_frame * 2 + 1] + right;

        output[output_frame * 2 + 0] =
            AvP3DS_ClampSample(mixed_left);

        output[output_frame * 2 + 1] =
            AvP3DS_ClampSample(mixed_right);

        avp3ds_cdda.read_frame =
            (avp3ds_cdda.read_frame + 1U) %
            AVP3DS_CDDA_RING_FRAMES;

        --avp3ds_cdda.buffered_frames;
    }
}

void CDDA_Stop(void)
{
    AvP3DS_CDDACloseFile();

    AvP3DS_CDDALock();

    avp3ds_cdda.playing = 0;
    avp3ds_cdda.looping = 0;
    avp3ds_cdda.eof = 0;
    avp3ds_cdda.current_track = -1;

    avp3ds_cdda.read_frame = 0;
    avp3ds_cdda.write_frame = 0;
    avp3ds_cdda.buffered_frames = 0;

    AvP3DS_CDDAUnlock();
}

static void AvP3DS_CDDAPlayInternal(
    int track,
    int looping)
{
    int started;

    if (!avp3ds_cdda.on ||
        avp3ds_audio_device == 0 ||
        !avp3ds_sound_active)
    {
        return;
    }

    if (track < 1 ||
        track > AVP3DS_CDDA_TRACK_COUNT)
    {
        return;
    }

    CDDA_Stop();

    if (!AvP3DS_CDDAOpenTrack(track))
        return;

    avp3ds_cdda.looping = looping ? 1 : 0;
    avp3ds_cdda.eof = 0;
    avp3ds_cdda.current_track = track;

    AvP3DS_CDDAPumpInternal();

    AvP3DS_CDDALock();

    started =
        avp3ds_cdda.buffered_frames != 0;

    avp3ds_cdda.playing = started;

    AvP3DS_CDDAUnlock();

    if (started)
    {
        printf(
            "YEET34A music started: Track%02d.wav\n",
            track);
    }
}

void CDDA_Start(void)
{
    avp3ds_cdda.on = 1;
    CDPlayerVolume = AVP3DS_CDDA_VOLUME_DEFAULT;

    printf("YEET34A CDDA ready\n");
}

void CDDA_End(void)
{
    CDDA_Stop();
    avp3ds_cdda.on = 0;
}

void CDDA_Management(void)
{
    if (avp3ds_cdda.on &&
        avp3ds_cdda.file != NULL)
    {
        AvP3DS_CDDAPumpInternal();
    }
}

void CDDA_Play(int track)
{
    AvP3DS_CDDAPlayInternal(track, 0);
}

void CDDA_PlayLoop(int track)
{
    AvP3DS_CDDAPlayInternal(track, 1);
}

void CDDA_ChangeVolume(int volume)
{
    if (volume < 0)
        volume = 0;

    if (volume > AVP3DS_CDDA_VOLUME_DEFAULT)
        volume = AVP3DS_CDDA_VOLUME_DEFAULT;

    AvP3DS_CDDALock();
    CDPlayerVolume = volume;
    AvP3DS_CDDAUnlock();
}

int CDDA_GetCurrentVolumeSetting(void)
{
    return CDPlayerVolume;
}

int CDDA_CheckNumberOfTracks(void)
{
    int track;
    int count = 0;

    for (track = 1;
         track <= AVP3DS_CDDA_TRACK_COUNT;
         ++track)
    {
        char filename[64];
        FILE *file;

        snprintf(
            filename,
            sizeof(filename),
            "Music/Track%02d.wav",
            track);

        file = OpenGameFile(
            filename,
            FILEMODE_READONLY,
            FILETYPE_OPTIONAL);

        if (file != NULL)
        {
            ++count;
            fclose(file);
        }
    }

    printf(
        "YEET34A CDDA tracks found: %d/%d\n",
        count,
        AVP3DS_CDDA_TRACK_COUNT);

    return count;
}

void CDDA_SwitchOn(void)
{
    avp3ds_cdda.on = 1;
}

void CDDA_SwitchOff(void)
{
    CDDA_Stop();
    avp3ds_cdda.on = 0;
}

int CDDA_IsOn(void)
{
    return avp3ds_cdda.on;
}

int CDDA_IsPlaying(void)
{
    int playing;

    AvP3DS_CDDALock();
    playing = avp3ds_cdda.playing;
    AvP3DS_CDDAUnlock();

    return playing;
}


static uint16_t AvP3DS_ReadLE16(const unsigned char *data)
{
    return (uint16_t)(
        ((uint16_t)data[0]) |
        ((uint16_t)data[1] << 8));
}

static uint32_t AvP3DS_ReadLE32(const unsigned char *data)
{
    return
        ((uint32_t)data[0]) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);
}

static int AvP3DS_ClampVolume(int volume)
{
    if (volume < VOLUME_MIN)
        return VOLUME_MIN;

    if (volume > VOLUME_MAX)
        return VOLUME_MAX;

    return volume;
}

static int16_t AvP3DS_ClampSample(int value)
{
    if (value < -32768)
        return -32768;

    if (value > 32767)
        return 32767;

    return (int16_t)value;
}

static void AvP3DS_FreeSample(AvP3DS_Sample *sample)
{
    if (sample == NULL)
        return;

    free(sample->data);
    free(sample);
}

static int AvP3DS_ParseWav(
    const unsigned char *wav,
    size_t wav_bytes,
    AvP3DS_Sample **sample_out)
{
    const unsigned char *format_data = NULL;
    const unsigned char *pcm_data = NULL;

    uint32_t riff_bytes;
    uint32_t pcm_bytes = 0;
    uint16_t format_tag = 0;
    uint16_t channels = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;

    size_t position;
    size_t riff_end;

    AvP3DS_Sample *sample;

    if (wav == NULL || sample_out == NULL || wav_bytes < 12)
        return 0;

    if (memcmp(wav, "RIFF", 4) != 0 ||
        memcmp(wav + 8, "WAVE", 4) != 0)
    {
        return 0;
    }

    riff_bytes = AvP3DS_ReadLE32(wav + 4);

    if ((uint64_t)riff_bytes + 8U > wav_bytes)
        return 0;

    riff_end = (size_t)riff_bytes + 8U;
    position = 12;

    while (position + 8 <= riff_end)
    {
        const unsigned char *chunk = wav + position;
        uint32_t chunk_bytes = AvP3DS_ReadLE32(chunk + 4);
        size_t chunk_data = position + 8;
        size_t chunk_end = chunk_data + chunk_bytes;

        if (chunk_end > riff_end)
            return 0;

        if (memcmp(chunk, "fmt ", 4) == 0 &&
            chunk_bytes >= 16)
        {
            format_data = wav + chunk_data;
        }
        else if (memcmp(chunk, "data", 4) == 0)
        {
            pcm_data = wav + chunk_data;
            pcm_bytes = chunk_bytes;
        }

        position = chunk_end + (chunk_bytes & 1U);
    }

    if (format_data == NULL || pcm_data == NULL)
        return 0;

    format_tag = AvP3DS_ReadLE16(format_data + 0);
    channels = AvP3DS_ReadLE16(format_data + 2);
    sample_rate = AvP3DS_ReadLE32(format_data + 4);
    block_align = AvP3DS_ReadLE16(format_data + 12);
    bits_per_sample = AvP3DS_ReadLE16(format_data + 14);

    if (format_tag != 1 ||
        (channels != 1 && channels != 2) ||
        (bits_per_sample != 8 && bits_per_sample != 16) ||
        sample_rate == 0)
    {
        return 0;
    }

    if (block_align == 0)
    {
        block_align =
            (uint16_t)(channels * (bits_per_sample / 8));
    }

    if (block_align == 0)
        return 0;

    pcm_bytes -= pcm_bytes % block_align;

    if (pcm_bytes == 0)
        return 0;

    sample = (AvP3DS_Sample *)calloc(1, sizeof(*sample));

    if (sample == NULL)
        return 0;

    sample->data = (unsigned char *)malloc(pcm_bytes);

    if (sample->data == NULL)
    {
        free(sample);
        return 0;
    }

    memcpy(sample->data, pcm_data, pcm_bytes);

    sample->bytes = pcm_bytes;
    sample->frames = pcm_bytes / block_align;
    sample->sample_rate = sample_rate;
    sample->block_align = block_align;
    sample->channels = (uint8_t)channels;
    sample->bits_per_sample = (uint8_t)bits_per_sample;

    *sample_out = sample;
    return 1;
}

static const char *AvP3DS_BaseName(const char *path)
{
    const char *slash;
    const char *backslash;

    if (path == NULL)
        return "";

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');

    if (slash == NULL)
        return backslash != NULL ? backslash + 1 : path;

    if (backslash == NULL)
        return slash + 1;

    return slash > backslash ? slash + 1 : backslash + 1;
}

static int AvP3DS_StoreSample(
    int sound_index,
    const char *wav_name,
    const unsigned char *wav,
    size_t wav_bytes)
{
    AvP3DS_Sample *sample = NULL;
    const char *base_name;
    size_t name_bytes;
    int fixed_length;

    if (sound_index < 0 || sound_index >= SID_MAXIMUM)
        return 0;

    if (!AvP3DS_ParseWav(wav, wav_bytes, &sample))
    {
        ++avp3ds_diag_load_failed;

        if (avp3ds_diag_failure_prints < 24U)
        {
            printf(
                "AUD LOAD FAIL id=%d name=%s bytes=%lu\n",
                sound_index,
                wav_name != NULL ? wav_name : "(null)",
                (unsigned long)wav_bytes);

            ++avp3ds_diag_failure_prints;
        }

        return 0;
    }

    if (GameSounds[sound_index].buffer != NULL)
    {
        AvP3DS_FreeSample(
            (AvP3DS_Sample *)GameSounds[sound_index].buffer);
    }

    if (GameSounds[sound_index].wavName != NULL)
    {
        DeallocateMem(GameSounds[sound_index].wavName);
    }

    base_name = AvP3DS_BaseName(wav_name);
    name_bytes = strlen(base_name) + 1;

    GameSounds[sound_index].wavName =
        (char *)AllocateMem(name_bytes);

    if (GameSounds[sound_index].wavName == NULL)
    {
        AvP3DS_FreeSample(sample);
        return 0;
    }

    memcpy(
        GameSounds[sound_index].wavName,
        base_name,
        name_bytes);

    fixed_length = (int)(
        ((uint64_t)sample->frames * ONE_FIXED) /
        sample->sample_rate);

    if (fixed_length <= 0)
        fixed_length = 1;

    GameSounds[sound_index].buffer = sample;
    GameSounds[sound_index].dsBufferP = sound_index + 1;
    GameSounds[sound_index].flags = SAMPLE_IN_SW;
    GameSounds[sound_index].dsFrequency =
        (int)sample->sample_rate;
    GameSounds[sound_index].length = fixed_length;

    ++avp3ds_diag_loaded;
    return 1;
}

static int16_t AvP3DS_ReadChannel(
    const AvP3DS_Sample *sample,
    uint32_t frame,
    int channel)
{
    const unsigned char *source =
        sample->data +
        (size_t)frame * sample->block_align;

    if (sample->channels == 1)
        channel = 0;

    if (sample->bits_per_sample == 8)
    {
        int value = source[channel];
        return (int16_t)((value - 128) << 8);
    }
    else
    {
        const unsigned char *value =
            source + channel * 2;

        return (int16_t)AvP3DS_ReadLE16(value);
    }
}

static void AvP3DS_UpdateVoiceStep(int active_index)
{
    AvP3DS_Voice *voice = &avp3ds_voices[active_index];
    double pitch_ratio;
    double step;

    if (voice->sample == NULL)
    {
        voice->step = AVP3DS_POSITION_ONE;
        return;
    }

    pitch_ratio = pow(
        2.0,
        (double)ActiveSounds[active_index].pitch / 1536.0);

    step =
        ((double)voice->sample->sample_rate /
         (double)AVP3DS_AUDIO_RATE) *
        pitch_ratio *
        (double)AVP3DS_POSITION_ONE;

    if (step < 1.0)
        step = 1.0;

    voice->step = (uint64_t)step;
}

static void SDLCALL AvP3DS_AudioCallback(
    void *userdata,
    Uint8 *stream,
    int stream_bytes)
{
    int16_t *output = (int16_t *)stream;
    int output_frames =
        stream_bytes /
        (int)(sizeof(int16_t) * AVP3DS_AUDIO_CHANNELS);

    int active_index;

    (void)userdata;

    memset(stream, 0, (size_t)stream_bytes);

    for (active_index = 0;
         active_index < SOUND_MAXACTIVE;
         ++active_index)
    {
        AvP3DS_Voice *voice =
            &avp3ds_voices[active_index];

        const AvP3DS_Sample *sample;
        int combined_volume;
        int output_frame;

        if (!voice->active ||
            voice->sample == NULL ||
            ActiveSounds[active_index].paused)
        {
            continue;
        }

        sample = voice->sample;

        combined_volume =
            AvP3DS_ClampVolume(voice->volume) *
            AvP3DS_ClampVolume(avp3ds_global_volume);

        for (output_frame = 0;
             output_frame < output_frames;
             ++output_frame)
        {
            uint32_t source_frame =
                (uint32_t)(voice->position >> 32);

            int left;
            int right;
            int output_left;
            int output_right;

            if (source_frame >= sample->frames)
            {
                if (voice->loop)
                {
                    uint64_t sample_length =
                        (uint64_t)sample->frames << 32;

                    if (sample_length == 0)
                    {
                        voice->active = 0;
                        break;
                    }

                    voice->position %= sample_length;
                    source_frame =
                        (uint32_t)(voice->position >> 32);
                }
                else
                {
                    voice->active = 0;
                    break;
                }
            }

            left = AvP3DS_ReadChannel(
                sample,
                source_frame,
                0);

            right = AvP3DS_ReadChannel(
                sample,
                source_frame,
                1);

            left =
                (left * combined_volume) /
                (127 * 127);

            right =
                (right * combined_volume) /
                (127 * 127);

            output_left =
                output[output_frame * 2 + 0] + left;

            output_right =
                output[output_frame * 2 + 1] + right;

            output[output_frame * 2 + 0] =
                AvP3DS_ClampSample(output_left);

            output[output_frame * 2 + 1] =
                AvP3DS_ClampSample(output_right);

            voice->position += voice->step;
        }
    }

    AvP3DS_MixCDDA(output, output_frames);
}

int PlatStartSoundSys(void)
{
    SDL_AudioSpec wanted;

    if (!WantSound)
        return 0;

    if (avp3ds_sound_active)
        return 1;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        printf(
            "YEET32A SDL audio init failed:\n%s\n",
            SDL_GetError());
        return 0;
    }

    SDL_zero(wanted);
    SDL_zero(avp3ds_audio_spec);

    wanted.freq = AVP3DS_AUDIO_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = AVP3DS_AUDIO_CHANNELS;
    wanted.samples = AVP3DS_AUDIO_SAMPLES;
    wanted.callback = AvP3DS_AudioCallback;

    avp3ds_audio_device = SDL_OpenAudioDevice(
        NULL,
        0,
        &wanted,
        &avp3ds_audio_spec,
        0);

    if (avp3ds_audio_device == 0)
    {
        printf(
            "YEET32A audio open failed:\n%s\n",
            SDL_GetError());

        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }

    if (avp3ds_audio_spec.freq != AVP3DS_AUDIO_RATE ||
        avp3ds_audio_spec.format != AUDIO_S16SYS ||
        avp3ds_audio_spec.channels != AVP3DS_AUDIO_CHANNELS)
    {
        printf(
            "YEET32A unexpected audio format:\n"
            "%d Hz fmt=%04X ch=%u\n",
            avp3ds_audio_spec.freq,
            avp3ds_audio_spec.format,
            avp3ds_audio_spec.channels);

        SDL_CloseAudioDevice(avp3ds_audio_device);
        avp3ds_audio_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }

    memset(avp3ds_voices, 0, sizeof(avp3ds_voices));

    avp3ds_global_volume = VOLUME_DEFAULT;
    avp3ds_sound_active = 1;

    SDL_PauseAudioDevice(avp3ds_audio_device, 0);

    printf(
        "YEET32A audio ready: %d Hz stereo\n",
        avp3ds_audio_spec.freq);

    return 1;
}

void PlatEndSoundSys(void)
{
    CDDA_Stop();

    if (avp3ds_audio_device != 0)
    {
        SDL_LockAudioDevice(avp3ds_audio_device);
        memset(avp3ds_voices, 0, sizeof(avp3ds_voices));
        SDL_UnlockAudioDevice(avp3ds_audio_device);

        SDL_CloseAudioDevice(avp3ds_audio_device);
        avp3ds_audio_device = 0;
    }

    avp3ds_sound_active = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

int PlatPlaySound(int active_index)
{
    int sound_index;
    AvP3DS_Sample *sample;
    int volume;

    ++avp3ds_diag_play_requested;

    if (!avp3ds_sound_active ||
        active_index < 0 ||
        active_index >= SOUND_MAXACTIVE)
    {
        return 0;
    }

    sound_index = ActiveSounds[active_index].soundIndex;

    if (sound_index < 0 ||
        sound_index >= SID_MAXIMUM)
    {
        return 0;
    }

    if (!GameSounds[sound_index].loaded)
    {
        ++avp3ds_diag_play_unloaded;

        if (avp3ds_diag_failure_prints < 24U)
        {
            printf(
                "AUD PLAY UNLOADED id=%d name=%s\n",
                sound_index,
                GameSounds[sound_index].wavName != NULL
                    ? GameSounds[sound_index].wavName
                    : "(unnamed)");

            ++avp3ds_diag_failure_prints;
        }

        return 0;
    }

    sample =
        (AvP3DS_Sample *)GameSounds[sound_index].buffer;

    if (sample == NULL)
    {
        ++avp3ds_diag_play_no_buffer;

        if (avp3ds_diag_failure_prints < 24U)
        {
            printf(
                "AUD PLAY NOBUF id=%d name=%s\n",
                sound_index,
                GameSounds[sound_index].wavName != NULL
                    ? GameSounds[sound_index].wavName
                    : "(unnamed)");

            ++avp3ds_diag_failure_prints;
        }

        return 0;
    }

    volume = ActiveSounds[active_index].volume;

    if (!ActiveSounds[active_index].threedee)
    {
        volume =
            (volume * VOLUME_PLAT2DSCALE) >> 7;
    }

    SDL_LockAudioDevice(avp3ds_audio_device);

    avp3ds_voices[active_index].sample = sample;
    avp3ds_voices[active_index].position = 0;
    avp3ds_voices[active_index].active = 1;
    avp3ds_voices[active_index].loop =
        ActiveSounds[active_index].loop ? 1 : 0;
    avp3ds_voices[active_index].volume =
        AvP3DS_ClampVolume(volume);

    AvP3DS_UpdateVoiceStep(active_index);

    SDL_UnlockAudioDevice(avp3ds_audio_device);

    ++avp3ds_diag_play_started;
    return 1;
}

void PlatStopSound(int active_index)
{
    if (active_index < 0 ||
        active_index >= SOUND_MAXACTIVE ||
        avp3ds_audio_device == 0)
    {
        return;
    }

    SDL_LockAudioDevice(avp3ds_audio_device);

    avp3ds_voices[active_index].active = 0;
    avp3ds_voices[active_index].sample = NULL;
    avp3ds_voices[active_index].position = 0;

    SDL_UnlockAudioDevice(avp3ds_audio_device);
}


/* AVP-HEADROOM3-AUDIOPOLL1: collapse per-voice status polling to one lock per management pass. */
static unsigned char avp3ds_sound_status_snapshot[SOUND_MAXACTIVE];
static int avp3ds_sound_status_snapshot_valid = 0;

void AvP3DS_BeginSoundStatusSnapshot(void)
{
    int active_index;

    avp3ds_sound_status_snapshot_valid = 0;

    if (avp3ds_audio_device == 0)
    {
        memset(
            avp3ds_sound_status_snapshot,
            0,
            sizeof(avp3ds_sound_status_snapshot));
        avp3ds_sound_status_snapshot_valid = 1;
        return;
    }

    SDL_LockAudioDevice(avp3ds_audio_device);

    for (active_index = 0;
         active_index < SOUND_MAXACTIVE;
         ++active_index)
    {
        avp3ds_sound_status_snapshot[active_index] =
            avp3ds_voices[active_index].active ? 1U : 0U;
    }

    SDL_UnlockAudioDevice(avp3ds_audio_device);

    avp3ds_sound_status_snapshot_valid = 1;
}

void AvP3DS_EndSoundStatusSnapshot(void)
{
    avp3ds_sound_status_snapshot_valid = 0;
}

int PlatSoundHasStopped(int active_index)
{
    int stopped;

    if (active_index < 0 ||
        active_index >= SOUND_MAXACTIVE)
    {
        return 1;
    }

    if (avp3ds_audio_device == 0)
        return 1;

    

    if (avp3ds_sound_status_snapshot_valid)
    {
        return
            !avp3ds_sound_status_snapshot[active_index];
    }SDL_LockAudioDevice(avp3ds_audio_device);

    stopped =
        !avp3ds_voices[active_index].active;

    SDL_UnlockAudioDevice(avp3ds_audio_device);

    return stopped;
}

int PlatChangeGlobalVolume(int volume)
{
    if (!avp3ds_sound_active)
        return 0;

    SDL_LockAudioDevice(avp3ds_audio_device);

    avp3ds_global_volume =
        AvP3DS_ClampVolume(volume);

    SDL_UnlockAudioDevice(avp3ds_audio_device);

    return 1;
}

int PlatChangeSoundVolume(int active_index, int volume)
{
    if (active_index < 0 ||
        active_index >= SOUND_MAXACTIVE ||
        avp3ds_audio_device == 0)
    {
        return SOUND_PLATFORMERROR;
    }

    SDL_LockAudioDevice(avp3ds_audio_device);

    avp3ds_voices[active_index].volume =
        AvP3DS_ClampVolume(volume);

    SDL_UnlockAudioDevice(avp3ds_audio_device);

    return 1;
}

int PlatChangeSoundPitch(int active_index, int pitch)
{
    if (active_index < 0 ||
        active_index >= SOUND_MAXACTIVE ||
        avp3ds_audio_device == 0)
    {
        return SOUND_PLATFORMERROR;
    }

    if (pitch > PITCH_MAXPLAT)
        pitch = PITCH_MAXPLAT;

    if (pitch < PITCH_MINPLAT)
        pitch = PITCH_MINPLAT;

    SDL_LockAudioDevice(avp3ds_audio_device);

    ActiveSounds[active_index].pitch = pitch;
    AvP3DS_UpdateVoiceStep(active_index);

    SDL_UnlockAudioDevice(avp3ds_audio_device);

    return 1;
}

int PlatDo3dSound(int active_index)
{
    VECTORCH relative_position;
    int distance;
    int new_volume;
    int inner_range;
    int outer_range;

    if (active_index < 0 ||
        active_index >= SOUND_MAXACTIVE)
    {
        return SOUND_PLATFORMERROR;
    }

    if (Global_VDB_Ptr == NULL)
    {
        return PlatChangeSoundVolume(
            active_index,
            ActiveSounds[active_index].volume);
    }

    relative_position.vx =
        ActiveSounds[active_index].threedeedata.position.vx -
        Global_VDB_Ptr->VDB_World.vx;

    relative_position.vy =
        ActiveSounds[active_index].threedeedata.position.vy -
        Global_VDB_Ptr->VDB_World.vy;

    relative_position.vz =
        ActiveSounds[active_index].threedeedata.position.vz -
        Global_VDB_Ptr->VDB_World.vz;

    distance = Magnitude(&relative_position);

    inner_range =
        ActiveSounds[active_index].threedeedata.inner_range;

    outer_range =
        ActiveSounds[active_index].threedeedata.outer_range;

    if (distance <= inner_range)
    {
        new_volume =
            ActiveSounds[active_index].volume;
    }
    else if (distance < outer_range &&
             outer_range > inner_range)
    {
        new_volume =
            (ActiveSounds[active_index].volume *
             (outer_range - distance)) /
            (outer_range - inner_range);
    }
    else
    {
        new_volume = VOLUME_MIN;
    }

    return PlatChangeSoundVolume(
        active_index,
        new_volume);
}

void PlatEndGameSound(SOUNDINDEX sound_index)
{
    AvP3DS_Sample *sample;
    int active_index;

    if (sound_index < 0 ||
        sound_index >= SID_MAXIMUM)
    {
        return;
    }

    sample =
        (AvP3DS_Sample *)GameSounds[sound_index].buffer;

    if (avp3ds_audio_device != 0)
    {
        SDL_LockAudioDevice(avp3ds_audio_device);

        for (active_index = 0;
             active_index < SOUND_MAXACTIVE;
             ++active_index)
        {
            if (avp3ds_voices[active_index].sample == sample)
            {
                avp3ds_voices[active_index].active = 0;
                avp3ds_voices[active_index].sample = NULL;
            }
        }

        SDL_UnlockAudioDevice(avp3ds_audio_device);
    }

    AvP3DS_FreeSample(sample);

    if (GameSounds[sound_index].wavName != NULL)
    {
        DeallocateMem(GameSounds[sound_index].wavName);
    }

    GameSounds[sound_index] = BlankGameSound;
}

unsigned int PlatMaxHWSounds(void)
{
    return 0;
}

void PlatUpdatePlayer(void)
{
    /* AVP-SHIPFINAL1-VOICEHALF-MENUSAFE: retain CDDA pumping every call; ship the YEET32B diagnostic. */
    CDDA_Management();
}

void InitialiseBaseFrequency(SOUNDINDEX sound_index)
{
    if (sound_index < 0 ||
        sound_index >= SID_MAXIMUM)
    {
        return;
    }

    if (GameSounds[sound_index].pitch > PITCH_MAXPLAT)
        GameSounds[sound_index].pitch = PITCH_MAXPLAT;

    if (GameSounds[sound_index].pitch < PITCH_MINPLAT)
        GameSounds[sound_index].pitch = PITCH_MINPLAT;
}

void UpdateSoundFrequencies(void)
{
    int active_index;

    if (avp3ds_audio_device == 0)
        return;

    SDL_LockAudioDevice(avp3ds_audio_device);

    for (active_index = 0;
         active_index < SOUND_MAXACTIVE;
         ++active_index)
    {
        if (avp3ds_voices[active_index].active)
            AvP3DS_UpdateVoiceStep(active_index);
    }

    SDL_UnlockAudioDevice(avp3ds_audio_device);
}

int LoadWavFile(int sound_index, char *wav_file_name)
{
    FILE *file;
    long file_bytes;
    unsigned char *data;
    size_t bytes_read;
    int result;

    file = OpenGameFile(
        wav_file_name,
        FILEMODE_READONLY,
        FILETYPE_PERM);

    if (file == NULL)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    file_bytes = ftell(file);

    if (file_bytes <= 0)
    {
        fclose(file);
        return 0;
    }

    rewind(file);

    data = (unsigned char *)malloc((size_t)file_bytes);

    if (data == NULL)
    {
        fclose(file);
        return 0;
    }

    bytes_read =
        fread(data, 1, (size_t)file_bytes, file);

    fclose(file);

    if (bytes_read != (size_t)file_bytes)
    {
        free(data);
        return 0;
    }

    result = AvP3DS_StoreSample(
        sound_index,
        wav_file_name,
        data,
        bytes_read);

    free(data);
    return result;
}

unsigned char *ExtractWavFile(
    int sound_index,
    unsigned char *buffer)
{
    size_t name_bytes;
    unsigned char *wav;
    uint32_t riff_bytes;
    size_t total_bytes;

    if (buffer == NULL)
        return NULL;

    name_bytes = strlen((const char *)buffer) + 1;
    wav = buffer + name_bytes;

    if (memcmp(wav, "RIFF", 4) != 0)
        return NULL;

    riff_bytes = AvP3DS_ReadLE32(wav + 4);
    total_bytes = (size_t)riff_bytes + 8U;

    AvP3DS_StoreSample(
        sound_index,
        (const char *)buffer,
        wav,
        total_bytes);

    return wav + total_bytes;
}

int LoadWavFromFastFile(
    int sound_index,
    char *wav_file_name)
{
    FFILE *file;
    unsigned char *buffer;
    size_t file_bytes;
    size_t name_bytes;
    int result = 0;

    file = ffopen(wav_file_name, "rb");

    if (file == NULL)
        return 0;

    ffseek(file, 0, SEEK_END);
    file_bytes = fftell(file);
    ffseek(file, 0, SEEK_SET);

    name_bytes = strlen(wav_file_name) + 1;

    buffer = (unsigned char *)malloc(
        name_bytes + file_bytes);

    if (buffer != NULL)
    {
        memcpy(buffer, wav_file_name, name_bytes);

        /*
         * YEET33D dynamic fastfile sound fix:
         * ffread() maps to ffreadb() in this codebase and returns
         * the number of bytes read, not the number of items.
         */
        if (ffread(
                buffer + name_bytes,
                1,
                file_bytes,
                file) == file_bytes)
        {
            result =
                ExtractWavFile(sound_index, buffer) != NULL;
        }

        free(buffer);
    }

    ffclose(file);
    return result;
}
