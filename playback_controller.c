#include "playback_controller.h"
#include "mp3_decoder.h"
#include "audio_output.h"
#include <stdio.h>
#include <stdlib.h>

static List* playlist;
static Node* current_track;
static int current_index = -1;

void playback_init(List* pl) {
    playlist = pl;
    current_track = NULL;
}

void playback_play(int index) {
    if (!playlist || index < 0) {
        printf("Invalid track index\n");
        return;
    }

    Node* track = playlist->head;
    int i = 0;
    while (track && i < index) {
        track = track->next;
        i++;
    }

    if (!track) {
        printf("Track not found\n");
        return;
    }

    current_track = track;
    current_index = index;

    PCMData* pcm = decode_mp3(track->file_path);
    if (pcm) {
        audio_play(pcm);
        free_pcm(pcm);
    } else {
        printf("Failed to decode MP3\n");
    }
}

void playback_pause(void) {
    audio_pause();
}

void playback_resume(void) {
    audio_resume();
}

void playback_next(void) {
    if (!current_track || !current_track->next) {
        printf("No next track\n");
        return;
    }
    playback_play(current_index + 1);
}

void playback_stop(void) {
    audio_pause(); // Dừng âm thanh
    current_track = NULL;
    current_index = -1;
    printf("Playback stopped\n");
}

void playback_cleanup(void) {
    printf("Playback cleaned up\n");
}