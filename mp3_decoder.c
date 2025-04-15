#include "mp3_decoder.h"
#include <mad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "error_handler.h"

#define INPUT_BUFFER_SIZE (10 * 8192) // Bộ đệm đầu vào
#define PCM_BUFFER_SIZE (1152 * 8)    // Bộ đệm PCM

static inline signed int scale(mad_fixed_t sample) {
    sample += (1L << (MAD_F_FRACBITS - 16));
    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;
    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

PCMData* decode_mp3(const char* file_path) {
    printf("Attempting to decode: %s\n", file_path);

    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        error_log(ERR_FILE, "Cannot open MP3 file");
        printf("Error: Cannot open %s\n", file_path);
        return NULL;
    }

    // Kiểm tra kích thước tệp
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0) {
        error_log(ERR_FILE, "MP3 file is empty or invalid");
        printf("Error: File %s is empty or invalid\n", file_path);
        fclose(fp);
        return NULL;
    }
    printf("File size: %ld bytes\n", file_size);

    // Khởi tạo libmad
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    mad_stream_init(&stream);
    mad_frame_init(&frame);
    mad_synth_init(&synth);

    // Bộ đệm đầu vào
    unsigned char input_buffer[INPUT_BUFFER_SIZE];
    size_t input_size = 0;
    size_t remaining = 0;

    // Bộ đệm PCM
    PCMData* pcm = malloc(sizeof(PCMData));
    if (!pcm) {
        error_log(ERR_MEMORY, "Failed to allocate PCM structure");
        printf("Error: Failed to allocate PCM structure\n");
        fclose(fp);
        return NULL;
    }
    pcm->data = NULL;
    pcm->size = 0;
    size_t pcm_capacity = 0;

    while (1) {
        if (remaining < INPUT_BUFFER_SIZE / 2) {
            memmove(input_buffer, stream.next_frame ? stream.next_frame : input_buffer, remaining);
            input_size = fread(input_buffer + remaining, 1, INPUT_BUFFER_SIZE - remaining, fp);
            if (input_size == 0 && feof(fp)) {
                break; // Hết tệp
            }
            if (input_size == 0) {
                error_log(ERR_FILE, "Error reading MP3 file");
                printf("Error: Failed to read %s\n", file_path);
                break;
            }
            remaining += input_size;
            mad_stream_buffer(&stream, input_buffer, remaining);
        }

        int ret = mad_frame_decode(&frame, &stream);
        if (ret == -1) {
            if (stream.error == MAD_ERROR_BUFLEN) {
                remaining = stream.bufend - stream.buffer;
                continue; // Cần thêm dữ liệu
            } else if (MAD_RECOVERABLE(stream.error)) {
                printf("Recoverable error: %s\n", mad_stream_errorstr(&stream));
                remaining = stream.bufend - stream.buffer;
                continue;
            } else {
                error_log(ERR_FILE, "Unrecoverable decode error");
                printf("Error: Unrecoverable decode error %d\n", stream.error);
                break;
            }
        }

        mad_synth_frame(&synth, &frame);
        for (unsigned int i = 0; i < synth.pcm.length; i++) {
            signed int sample;
            // Kênh trái
            sample = scale(synth.pcm.samples[0][i]);
            if (pcm->size + 2 > pcm_capacity) {
                pcm_capacity += PCM_BUFFER_SIZE;
                pcm->data = realloc(pcm->data, pcm_capacity);
                if (!pcm->data) {
                    error_log(ERR_MEMORY, "Failed to allocate PCM buffer");
                    printf("Error: Failed to allocate PCM buffer\n");
                    break;
                }
            }
            pcm->data[pcm->size++] = (sample >> 0) & 0xff;
            pcm->data[pcm->size++] = (sample >> 8) & 0xff;

            // Kênh phải (nếu có)
            if (MAD_NCHANNELS(&frame.header) == 2) {
                sample = scale(synth.pcm.samples[1][i]);
                if (pcm->size + 2 > pcm_capacity) {
                    pcm_capacity += PCM_BUFFER_SIZE;
                    pcm->data = realloc(pcm->data, pcm_capacity);
                    if (!pcm->data) {
                        error_log(ERR_MEMORY, "Failed to allocate PCM buffer");
                        printf("Error: Failed to allocate PCM buffer\n");
                        break;
                    }
                }
                pcm->data[pcm->size++] = (sample >> 0) & 0xff;
                pcm->data[pcm->size++] = (sample >> 8) & 0xff;
            }
        }
        remaining = stream.bufend - stream.next_frame;
    }

    fclose(fp);
    mad_synth_finish(&synth);
    mad_frame_finish(&frame);
    mad_stream_finish(&stream);

    if (pcm->size == 0) {
        error_log(ERR_FILE, "Failed to decode MP3");
        printf("Error: No PCM data decoded from %s\n", file_path);
        free(pcm->data);
        free(pcm);
        return NULL;
    }

    printf("Decoded PCM size: %zu bytes\n", pcm->size);
    return pcm;
}

void free_pcm(PCMData* pcm) {
    if (pcm) {
        free(pcm->data);
        free(pcm);
    }
}