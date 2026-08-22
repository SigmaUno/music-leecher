#include "decoder.h"

#include <sndfile.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const unsigned char *data; sf_count_t size; sf_count_t offset; } MemoryAudio;

static void set_error(char *error, size_t error_size, const char *format, ...) {
    va_list arguments;
    if (!error || !error_size) return;
    va_start(arguments, format); vsnprintf(error, error_size, format, arguments); va_end(arguments);
}
static sf_count_t memory_length(void *userdata) { return ((MemoryAudio *)userdata)->size; }
static sf_count_t memory_seek(sf_count_t offset, int whence, void *userdata) {
    MemoryAudio *audio = userdata;
    sf_count_t position = whence == SEEK_SET ? offset : whence == SEEK_CUR ? audio->offset + offset : whence == SEEK_END ? audio->size + offset : -1;
    if (position < 0 || position > audio->size) return -1;
    audio->offset = position; return position;
}
static sf_count_t memory_read(void *destination, sf_count_t count, void *userdata) {
    MemoryAudio *audio = userdata;
    if (count > audio->size - audio->offset) count = audio->size - audio->offset;
    memcpy(destination, audio->data + audio->offset, (size_t)count);
    audio->offset += count; return count;
}
static sf_count_t memory_tell(void *userdata) { return ((MemoryAudio *)userdata)->offset; }

void decoder_pcm_destroy(DecoderPcm *pcm) {
    if (!pcm) return;
    free(pcm->samples); memset(pcm, 0, sizeof(*pcm));
}

int decoder_decode_queue(Assembler *assembler, DecoderPcm *pcm, char *error, size_t error_size) {
    AssemblerPiece piece = {0};
    unsigned char *bytes = NULL;
    size_t size = 0;
    MemoryAudio audio;
    SF_VIRTUAL_IO io = { memory_length, memory_seek, memory_read, NULL, memory_tell };
    SF_INFO info = {0};
    SNDFILE *file;
    sf_count_t frames;
    if (!assembler || !pcm) { set_error(error, error_size, "assembler and PCM output are required"); return -1; }
    memset(pcm, 0, sizeof(*pcm));
    while (assembler_pop(assembler, &piece) == 1) {
        unsigned char *grown;
        if (piece.size > SIZE_MAX - size) { assembler_piece_destroy(&piece); free(bytes); set_error(error, error_size, "assembled audio is too large"); return -1; }
        grown = realloc(bytes, size + piece.size);
        if (!grown) { assembler_piece_destroy(&piece); free(bytes); set_error(error, error_size, "out of memory"); return -1; }
        bytes = grown; memcpy(bytes + size, piece.data, piece.size); size += piece.size;
        assembler_piece_destroy(&piece);
    }
    if (!size) { set_error(error, error_size, "assembler queue is empty"); return 0; }
    audio.data = bytes; audio.size = (sf_count_t)size; audio.offset = 0;
    file = sf_open_virtual(&io, SFM_READ, &info, &audio);
    if (!file) { set_error(error, error_size, "decoder: %s", sf_strerror(NULL)); free(bytes); return -1; }
    if (info.frames <= 0 || info.channels <= 0 || (size_t)info.frames > SIZE_MAX / ((size_t)info.channels * sizeof(short))) { sf_close(file); free(bytes); set_error(error, error_size, "unsupported or oversized audio stream"); return -1; }
    pcm->sample_count = (size_t)info.frames * (size_t)info.channels;
    pcm->samples = malloc(pcm->sample_count * sizeof(*pcm->samples));
    if (!pcm->samples) { sf_close(file); free(bytes); set_error(error, error_size, "out of memory"); return -1; }
    frames = sf_readf_short(file, pcm->samples, info.frames);
    sf_close(file); free(bytes);
    if (frames <= 0) { decoder_pcm_destroy(pcm); set_error(error, error_size, "decoder produced no PCM samples"); return -1; }
    pcm->sample_count = (size_t)frames * (size_t)info.channels;
    pcm->sample_rate = info.samplerate; pcm->channels = info.channels;
    return 1;
}
