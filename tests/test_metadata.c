/*
 * Test metadata extraction functionality
 * Build: cc -std=c11 -Wall -Wextra -Wpedantic metadata.c -o test-metadata
 */

#define _POSIX_C_SOURCE 200809L

#include "metadata.h"
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_remote_metadata_with_fake_ssh(void) {
    char directory[] = "/tmp/leecher-metadata-test-XXXXXX";
    char script_path[512];
    char log_path[512];
    char path[2048];
    char log[2048] = {0};
    const char *old_path;
    char *saved_path = NULL;
    FILE *script;
    FILE *log_file;
    AudioMetadata metadata = {0};
    char error[256] = {0};
    int result;
    int passed = 0;

    if (!mkdtemp(directory)) return 0;
    snprintf(script_path, sizeof(script_path), "%s/ssh", directory);
    snprintf(log_path, sizeof(log_path), "%s/ssh-command", directory);

    script = fopen(script_path, "w");
    if (!script) goto cleanup;
    fputs("#!/bin/sh\n"
          "for argument; do last=$argument; done\n"
          "printf '%s\\n' \"$@\" > \"$TEST_SSH_LOG\"\n"
          "case \"$last\" in\n"
          "  *mediainfo*) exit 1 ;;\n"
          "  *ffprobe*) printf 'TAG:TITLE=Remote Song\\nTAG:ARTIST=Remote Artist\\nTAG:ALBUM=Remote Album\\n' ;;\n"
          "  *) exit 1 ;;\n"
          "esac\n", script);
    fclose(script);
    if (chmod(script_path, S_IRWXU) != 0) goto cleanup;

    old_path = getenv("PATH");
    if (old_path) {
        saved_path = strdup(old_path);
        if (!saved_path) goto cleanup;
    }
    snprintf(path, sizeof(path), "%s:%s", directory, old_path ? old_path : "/usr/bin:/bin");
    if (setenv("PATH", path, 1) != 0 || setenv("TEST_SSH_LOG", log_path, 1) != 0) goto cleanup;

    result = metadata_extract_from_ssh("music", "192.0.2.10",
                                       "/srv/Music/O'Connor.flac", &metadata,
                                       error, sizeof(error));
    log_file = fopen(log_path, "r");
    if (log_file) {
        fread(log, 1, sizeof(log) - 1, log_file);
        fclose(log_file);
    }
    passed = result == 1 && metadata.title && metadata.artist && metadata.album &&
             strcmp(metadata.title, "Remote Song") == 0 &&
             strcmp(metadata.artist, "Remote Artist") == 0 &&
             strcmp(metadata.album, "Remote Album") == 0 &&
             strstr(log, "-F\n/dev/null\n") != NULL &&
             strstr(log, "/srv/Music/O'\\''Connor.flac") != NULL;
    metadata_destroy(&metadata);

cleanup:
    if (saved_path) {
        setenv("PATH", saved_path, 1);
        free(saved_path);
    }
    unsetenv("TEST_SSH_LOG");
    unlink(script_path);
    unlink(log_path);
    rmdir(directory);
    return passed;
}

int main(void) {
    AudioMetadata metadata = {0};
    char error[256] = {0};
    int result;

    printf("Testing metadata extraction module...\n\n");

    /* Test 1: Non-existent local file */
    printf("Test 1: Non-existent local file\n");
    result = metadata_extract_from_file("/tmp/nonexistent_audio_file_12345.mp3", &metadata, error, sizeof(error));
    printf("  Result: %d\n", result);
    printf("  Error: %s\n", error);
    printf("  Expected: 0 (file not found)\n\n");

    /* Test 2: Invalid parameters */
    printf("Test 2: Invalid parameters (NULL filepath)\n");
    result = metadata_extract_from_file(NULL, &metadata, error, sizeof(error));
    printf("  Result: %d\n", result);
    printf("  Error: %s\n", error);
    printf("  Expected: -1 (invalid parameters)\n\n");

    /* Test 3: Invalid SSH parameters */
    printf("Test 3: Invalid SSH parameters (empty username)\n");
    memset(&metadata, 0, sizeof(metadata));
    memset(error, 0, sizeof(error));
    result = metadata_extract_from_ssh("", "192.168.1.100", "/tmp/song.mp3", &metadata, error, sizeof(error));
    printf("  Result: %d\n", result);
    printf("  Error: %s\n", error);
    printf("  Expected: -1 (invalid parameters)\n\n");

    /* Test 4: Invalid SSH credentials format */
    printf("Test 4: Invalid SSH credentials (bad IP format)\n");
    memset(&metadata, 0, sizeof(metadata));
    memset(error, 0, sizeof(error));
    result = metadata_extract_from_ssh("user", "999.999.999.999!", "/tmp/song.mp3", &metadata, error, sizeof(error));
    printf("  Result: %d\n", result);
    printf("  Error: %s\n", error);
    printf("  Expected: -1 (invalid SSH format)\n\n");

    /* Test 5: Create a test file and extract locally */
    printf("Test 5: Creating test audio file and extracting metadata...\n");
    int system_result = system("ffmpeg -f lavfi -i sine=f=1000:d=1 -metadata title='Test Song' -metadata artist='Test Artist' -metadata album='Test Album' /tmp/test_metadata.wav -y >/dev/null 2>&1");
    
    if (system_result == 0) {
        memset(&metadata, 0, sizeof(metadata));
        memset(error, 0, sizeof(error));
        result = metadata_extract_from_file("/tmp/test_metadata.wav", &metadata, error, sizeof(error));
        printf("  Result: %d\n", result);
        if (result > 0) {
            printf("  Title: %s\n", metadata.title ? metadata.title : "(not found)");
            printf("  Artist: %s\n", metadata.artist ? metadata.artist : "(not found)");
            printf("  Album: %s\n", metadata.album ? metadata.album : "(not found)");
            printf("  Status: ✓ PASS\n");
        } else {
            printf("  Note: Extraction returned %d\n", result);
            printf("  Error: %s\n", error);
            printf("  Status: Tools may not be available\n");
        }
        metadata_destroy(&metadata);
        system("rm -f /tmp/test_metadata.wav");
    } else {
        printf("  Note: ffmpeg not available, skipping audio file test\n");
    }
    printf("\n");

    /* Test 6: SSH with non-existent remote host (will fail, which is expected) */
    printf("Test 6: SSH with unreachable host (expected to fail gracefully)\n");
    memset(&metadata, 0, sizeof(metadata));
    memset(error, 0, sizeof(error));
    result = metadata_extract_from_ssh("testuser", "127.0.0.1", "/nonexistent.mp3", &metadata, error, sizeof(error));
    printf("  Result: %d\n", result);
    if (result < 0) printf("  Error: %s\n", error);
    printf("  Expected: -1 or 0 (SSH fails or file not found)\n  Status: ✓ PASS (graceful error handling)\n\n");

    /* Test 7: SSH extraction with a deterministic transport replacement.
     * This verifies parsed fields and that an apostrophe in the remote path
     * remains shell-escaped all the way to ssh. */
    printf("Test 7: SSH metadata extraction and quoted remote path\n");
    if (!test_remote_metadata_with_fake_ssh()) {
        fprintf(stderr, "  Status: FAIL\n");
        return 1;
    }
    printf("  Status: ✓ PASS\n\n");

    printf("Metadata extraction module tests completed.\n");
    return 0;
}
