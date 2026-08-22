#include "assembler.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const unsigned char input[] = "abcdefghij";
    const char *expected[] = { "abcd", "efgh", "ij" };
    AssemblerConfig config = { .piece_size = 4 };
    Assembler *assembler = assembler_create(&config);
    AssemblerPiece piece = {0};
    size_t index;
    if (!assembler || assembler_push(assembler, input, sizeof(input) - 1) != 1 || assembler_queued_pieces(assembler) != 3) return 1;
    for (index = 0; index < 3; index++) {
        if (assembler_pop(assembler, &piece) != 1 || piece.size != strlen(expected[index]) || memcmp(piece.data, expected[index], piece.size)) { assembler_piece_destroy(&piece); assembler_destroy(assembler); return 1; }
        assembler_piece_destroy(&piece);
    }
    if (assembler_pop(assembler, &piece) != 0) { assembler_destroy(assembler); return 1; }
    assembler_destroy(assembler);
    return 0;
}
