#include "library_handler.h"
#include "music_ripper.h"

#include <stdio.h>
#include <string.h>

static int write_audio(const unsigned char *data, size_t size, void *userdata) {
    char *output = userdata;
    if (size != 5) return 0;
    memcpy(output, data, size);
    output[size] = '\0';
    return 1;
}

static int fetch_https(const LibrarySource *source, MusicRipperWriteFn write, void *write_userdata, void *transport_userdata) {
    const unsigned char audio[] = "audio";
    (void)transport_userdata;
    if (!source->url || strcmp(source->url, "https://media.example.invalid/teardrop.flac")) return -1;
    return write(audio, sizeof(audio) - 1, write_userdata) ? 0 : -1;
}

int main(void) {
    char error[128] = {0};
    char output[6] = {0};
    LibraryHandler *library = library_handler_open("library.example.json", error, sizeof(error));
    LibrarySongQuery query = { .title = "Teardrop", .artist = "Massive Attack" };
    LibrarySourceKind https = LIBRARY_SOURCE_HTTPS;
    MusicRipper ripper;
    if (!library) { fprintf(stderr, "%s\n", error); return 1; }
    memset(&ripper, 0, sizeof(ripper));
    ripper.library = library;
    ripper.transports.https = fetch_https;
    if (music_ripper_play_next(&ripper, &query, &https, write_audio, output, error, sizeof(error)) != 1) { fprintf(stderr, "%s\n", error); library_handler_close(library); return 1; }
    library_handler_close(library);
    return strcmp(output, "audio") != 0;
}
