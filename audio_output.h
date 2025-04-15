#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <stdbool.h> // Thêm để định nghĩa bool
#include "mp3_decoder.h"

bool audio_init(void);
void audio_play(PCMData* pcm);
void audio_pause(void);
void audio_resume(void);
void audio_cleanup(void);

#endif