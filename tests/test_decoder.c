#include "assembler.h"
#include "decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const unsigned char wav[] = {
        'R','I','F','F', 40,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0,
        1,0, 1,0, 0x40,0x1f,0,0, 0x80,0x3e,0,0, 2,0, 16,0,
        'd','a','t','a', 4,0,0,0, 1,0, 255,255
    };
    AssemblerConfig config = { .piece_size = 13 };
    Assembler *assembler = assembler_create(&config);
    DecoderSource *src = NULL;
    short buf[2];
    char error[128] = {0};
    long long got;
    if (!assembler || assembler_push(assembler, wav, sizeof(wav)) != 1) return 1;
    if (decoder_open(assembler, &src, error, sizeof(error)) != 1) { assembler_destroy(assembler); fprintf(stderr, "open: %s\n", error); return 1; }
    if (decoder_rate(src) != 8000 || decoder_channels(src) != 1 || decoder_total_frames(src) != 2) { decoder_close(src); assembler_destroy(assembler); return 1; }
    got = decoder_read_frames(src, buf, 1);
    if (got != 1 || buf[0] != 1) { decoder_close(src); assembler_destroy(assembler); return 1; }
    got = decoder_read_frames(src, buf, 4);
    if (got != 1 || buf[0] != -1) { decoder_close(src); assembler_destroy(assembler); return 1; }
    if (decoder_read_frames(src, buf, 2) != 0) { decoder_close(src); assembler_destroy(assembler); return 1; }
    if (decoder_seek(src, 0) != 0) { decoder_close(src); assembler_destroy(assembler); return 1; }
    got = decoder_read_frames(src, buf, 1);
    if (got != 1 || buf[0] != 1) { decoder_close(src); assembler_destroy(assembler); return 1; }
    decoder_close(src);
    assembler_destroy(assembler);
    return 0;
}
