#include "assembler.h"
#include "decoder.h"

int main(void) {
    const unsigned char wav[] = {
        'R','I','F','F', 40,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0,
        1,0, 1,0, 0x40,0x1f,0,0, 0x80,0x3e,0,0, 2,0, 16,0,
        'd','a','t','a', 4,0,0,0, 1,0, 255,255
    };
    AssemblerConfig config = { .piece_size = 13 };
    Assembler *assembler = assembler_create(&config);
    DecoderPcm pcm = {0};
    char error[128] = {0};
    int result;
    if (!assembler || assembler_push(assembler, wav, sizeof(wav)) != 1) return 1;
    result = decoder_decode_queue(assembler, &pcm, error, sizeof(error));
    if (result != 1 || pcm.sample_rate != 8000 || pcm.channels != 1 || pcm.sample_count != 2 || pcm.samples[0] != 1 || pcm.samples[1] != -1) { decoder_pcm_destroy(&pcm); assembler_destroy(assembler); return 1; }
    decoder_pcm_destroy(&pcm); assembler_destroy(assembler);
    return 0;
}
