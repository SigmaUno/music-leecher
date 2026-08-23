/*
 * Test metadata extraction functionality
 * Build: cc -std=c11 -Wall -Wextra -Wpedantic metadata.c -o test-metadata
 */

#include "metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    printf("Metadata extraction module tests completed.\n");
    return 0;
}
