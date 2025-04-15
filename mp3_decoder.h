#ifndef MP3_DECODER_H
#define MP3_DECODER_H

#include <stddef.h> // Thêm để định nghĩa size_t

typedef struct {
    unsigned char* data;
    size_t size;
} PCMData;

PCMData* decode_mp3(const char* file_path);
void free_pcm(PCMData* pcm);

#endif