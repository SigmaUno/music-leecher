#include "library_handler.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char error[256] = {0};
    LibrarySongQuery song = { .title = "Teardrop", .artist = "Massive Attack", .album = "Mezzanine" };
    LibrarySource source = { .kind = LIBRARY_SOURCE_NETWORK, .path = "/new/share/teardrop.flac", .username = "listener", .ip = "198.51.100.8" };
    LibraryHandler *library;
    LibraryTrack track = {0};
    if (argc != 2) return 2;
    if (library_handler_add_source(argv[1], &song, &source, error, sizeof(error)) != 1) { fprintf(stderr, "%s\n", error); return 1; }
    library = library_handler_open(argv[1], error, sizeof(error));
    if (!library || library_handler_resolve(library, &song, &track, error, sizeof(error)) != 1) { fprintf(stderr, "%s\n", error); library_handler_close(library); return 1; }
    if (track.source_count != 5 || strcmp(track.sources[4].path, source.path) || strcmp(track.sources[4].username, source.username) || strcmp(track.sources[4].ip, source.ip)) { library_handler_track_destroy(&track); library_handler_close(library); return 1; }
    library_handler_track_destroy(&track);
    library_handler_close(library);
    song.album = "Live version";
    if (library_handler_add_source(argv[1], &song, &source, error, sizeof(error)) != 1) return 1;
    library = library_handler_open(argv[1], error, sizeof(error));
    if (!library || library_handler_track_count(library) != 2 || library_handler_resolve(library, &song, &track, error, sizeof(error)) != 1 || track.source_count != 1) { library_handler_track_destroy(&track); library_handler_close(library); return 1; }
    library_handler_track_destroy(&track);
    library_handler_close(library);
    return 0;
}
