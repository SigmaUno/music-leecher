#ifndef DECODER_H
#define DECODER_H

#include <stddef.h>

#include "assembler.h"

typedef struct {
    short *samples;
    size_t sample_count;
    int sample_rate;
    int channels;
} DecoderPcm;

/* Drains assembled source bytes and decodes them to signed 16-bit PCM. */
int decoder_decode_queue(Assembler *assembler, DecoderPcm *pcm, char *error, size_t error_size);
void decoder_pcm_destroy(DecoderPcm *pcm);

#endif
