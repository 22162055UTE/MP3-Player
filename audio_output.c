#include "audio_output.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "error_handler.h"

static snd_pcm_t* pcm_handle = NULL;
static bool is_paused = false;

bool audio_init(void) {
    int err;
    snd_pcm_hw_params_t* hw_params;

    if ((err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        error_log(ERR_AUDIO, "Cannot open PCM device");
        return false;
    }

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    unsigned int rate = 44100;
    snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, 2);
    snd_pcm_hw_params(pcm_handle, hw_params);

    if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
        error_log(ERR_AUDIO, "Cannot prepare PCM");
        snd_pcm_close(pcm_handle);
        return false;
    }

    printf("Audio initialized\n");
    return true;
}

void audio_play(PCMData* pcm) {
    if (!pcm || !pcm->data || pcm->size == 0) {
        printf("No PCM data to play\n");
        return;
    }

    is_paused = false;
    snd_pcm_uframes_t frame_size = pcm->size / 4; // 2 kênh, 16-bit
    int err;

    if ((err = snd_pcm_writei(pcm_handle, pcm->data, frame_size)) != frame_size) {
        error_log(ERR_AUDIO, "PCM write error");
        snd_pcm_prepare(pcm_handle);
    } else {
        printf("Playing PCM data of size %zu\n", pcm->size);
    }
}

void audio_pause(void) {
    if (!is_paused) {
        snd_pcm_pause(pcm_handle, 1);
        is_paused = true;
        printf("Audio paused\n");
    }
}

void audio_resume(void) {
    if (is_paused) {
        snd_pcm_pause(pcm_handle, 0);
        is_paused = false;
        printf("Audio resumed\n");
    }
}

void audio_cleanup(void) {
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        printf("Audio cleaned up\n");
    }
}