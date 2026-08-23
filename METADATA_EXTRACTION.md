# Metadata Extraction Feature

## Overview

The metadata extraction feature automatically populates song metadata (title, artist, album) when users select audio files. This works for:
- **Local files**: Via "Choose local music file" button
- **Remote SSH files**: Via "Extract SSH metadata" button

This eliminates the need for manual metadata entry for most files with embedded metadata.

## Architecture

### New Module: `metadata.h` / `metadata.c`

A dedicated module for metadata extraction with the following key components:

#### Public API

```c
/* Extracts metadata from a local audio file.
 * Returns 1 on success, 0 if file not found, -1 on error. */
int metadata_extract_from_file(const char *filepath, AudioMetadata *metadata, 
                               char *error, size_t error_size);

/* Extracts metadata from a remote audio file via SSH.
 * Returns 1 on success, 0 if file not found, -1 on error. */
int metadata_extract_from_ssh(const char *username, const char *ip, const char *filepath,
                              AudioMetadata *metadata, char *error, size_t error_size);

/* Free allocated fields in metadata struct */
void metadata_destroy(AudioMetadata *metadata);
```

#### Data Structure

```c
typedef struct {
    char *title;
    char *artist;
    char *album;
} AudioMetadata;
```

### Extraction Strategy

The module uses a **fallback chain** for maximum compatibility:

1. **Primary**: `mediainfo` command (preferred for comprehensive format support)
2. **Secondary**: `ffprobe` command (fallback if mediainfo unavailable)

Both tools parse audio file metadata without requiring in-memory decoding.

#### Supported Formats

Both extraction tools support:
- MP3 (ID3 tags)
- FLAC (Vorbis comments)
- OGG/Opus (Vorbis comments)
- WAV (ID3 tags, RIFF INFO chunks)
- M4A/AAC (iTunes tags)
- WMA
- Many other formats

#### Metadata Fields Extracted

- **Title**: Primary track name
- **Artist**: Performer/composer information
- **Album**: Album/collection name

## Integration with UI

### Local Files: `choose_local_file()`

When a user clicks "Choose local music file":

1. File picker dialog opens (using `zenity`)
2. User selects an audio file
3. **NEW**: Metadata extraction is attempted
4. **NEW**: If successful, title/artist/album fields are auto-populated
5. User can still manually edit fields before saving

Example status messages:
- Success: `"Selected: /path/to/song.mp3 (metadata extracted)"`
- Fallback: `"Selected local file: /path/to/song.mp3"` (if extraction unavailable)

### Remote SSH Files: `choose_ssh_file()` and `extract_ssh_metadata()`

When a user clicks "Choose remote music file" button (available when SOURCE_SSH is selected):

1. **File Browser**: Uses SSH to find music files on the remote system
   - Searches for `.mp3`, `.flac`, `.ogg`, `.wav`, `.m4a` files up to 5 directories deep
   - Presents list via `zenity` dialog
2. **Automatic Metadata Extraction**: After selection, extracts metadata from remote file
3. **Auto-Population**: Title/artist/album fields auto-populate with remote file metadata
4. User can still manually edit fields before saving

When a user clicks "Extract SSH metadata" button:

1. Extracts metadata from the path currently in the PATH field
2. Useful when users already know the remote file path or want to refresh metadata
3. Auto-populates title/artist/album fields

Example workflow (using file browser):
1. Select "SSH" as source method
2. Enter USERNAME (e.g., "user")
3. Enter IP (e.g., "192.168.1.100")
4. Click "Choose remote music file" button
5. File browser shows available music files
6. User selects a file (e.g., "song.mp3")
7. Fields auto-populate with remote file metadata:
   - Path: "./song.mp3"
   - Title: "My Favorite Song"
   - Artist: "The Artist"
   - Album: "Greatest Hits"
8. User reviews and clicks "Save source to library"

Alternative workflow (manual path + extract):
1. Select "SSH" as source method
2. Enter USERNAME, IP, and PATH
3. Click "Extract SSH metadata" button
4. Fields auto-populate with metadata
5. Click "Save source to library"

Example status messages:
- Success: `"Selected remote: ./song.mp3 (metadata extracted)"`
- Success (extract): `"Remote metadata extracted from ./song.mp3"`
- No selection: `"Enter USERNAME and IP first."`
- File not found: `"Remote file not found: ./song.mp3"`
- Error: `"Metadata error: Could not extract remote metadata (...)"`

## Dependencies

### Runtime Dependencies (Optional)

Install one of these for metadata extraction to work:

```bash
# Option 1: mediainfo (recommended)
sudo apt install mediainfo

# Option 2: ffmpeg (includes ffprobe)
sudo apt install ffmpeg

# Option 3: Both (for maximum compatibility)
sudo apt install mediainfo ffmpeg
```

**Important**: The app still functions without these tools. Fields simply won't auto-populate.

### SSH Requirements for Remote Extraction

For SSH metadata extraction to work, ensure:
- SSH access configured with key-based auth (no password prompts)
- `mediainfo` or `ffprobe` installed on **both local and remote** systems
- Remote path is readable by the SSH user
- SSH access works in batch mode (no TTY required)

### Build Dependencies

No new build dependencies. The module only uses standard C library functions and system tools via `popen()`.

## Building and Testing

### Build

```bash
make app          # Build main application
make test         # Run all tests including metadata tests
```

### Test

The metadata extraction is tested via `test-metadata`:

```bash
make test-metadata
./test-metadata
```

Test coverage:
- ✓ Non-existent file handling (local)
- ✓ Invalid parameter handling
- ✓ Graceful degradation when tools unavailable
- ✓ Actual metadata extraction (if tools available)

## Error Handling

The module implements robust error handling:

```
Return Value | Meaning
-------------|--------
      1      | Success - metadata extracted
      0      | File not found
     -1      | Error (invalid parameters, no tools available, SSH failure, etc)
```

Error messages are provided via `error` parameter for diagnostics.

## Code Organization

```
metadata.h          - Public API declaration
metadata.c          - Implementation with local and SSH extraction
tests/test_metadata.c - Unit tests
Makefile            - Build configuration (updated)
app.c               - UI integration
                    - choose_local_file (local file picker + extraction)
                    - choose_ssh_file (SSH file browser + extraction)
                    - extract_ssh_metadata (manual path extraction)
```

## Limitations and Future Improvements

### Current Limitations

1. **Tool Dependency**: Requires `mediainfo` or `ffprobe` for full functionality
2. **SSH Batch Mode**: SSH key-based auth required (no password prompts)
3. **Tag Format Support**: Limited to what external tools support
4. **Performance**: External process invocation adds ~100-500ms per file
5. **File Browser Depth**: SSH file browser searches up to 5 levels deep (configurable in code)

### Future Enhancements

1. Add SFTP file browser for true remote filesystem browsing
2. Add libmetaflac or similar for native tag reading
3. Cache metadata for recently used files
4. Support for more metadata fields (year, genre, comment, etc.)
5. Batch metadata extraction when adding multiple files
6. Progress indication for slow remote operations
7. Support for more authentication methods (passwords, agent forwarding)
8. Configurable file search depth and patterns for SSH browser

## Example Usage

### User Workflow - Local File

1. User clicks "Choose local music file" button
2. File picker opens
3. User selects "My Favorite Song.mp3" with embedded metadata
4. Fields auto-populate:
   - Title: "My Favorite Song"
   - Artist: "The Artist"
   - Album: "Greatest Hits"
5. User reviews and clicks "Save source to library"

### User Workflow - Remote SSH File (File Picker)

1. User selects "SSH" as source method
2. Enters SSH credentials:
   - USERNAME: "user"
   - IP: "192.168.1.100"
3. Clicks "Choose remote music file" button
4. File browser opens, showing available music files
5. User selects a file (e.g., "song.mp3")
6. Fields auto-populate with remote file metadata:
   - Path: "./song.mp3"
   - Title: "My Favorite Song"
   - Artist: "The Artist"
   - Album: "Greatest Hits"
7. User reviews and clicks "Save source to library"

### User Workflow - Remote SSH File (Manual Path)

1. User selects "SSH" as source method
2. Enters SSH credentials:
   - USERNAME: "user"
   - IP: "192.168.1.100"
3. Enters remote path: "/home/user/Music/song.mp3"
4. Clicks "Extract SSH metadata" button
5. Fields auto-populate with remote file metadata
6. User reviews and clicks "Save source to library"

### Programmatic Usage

```c
#include "metadata.h"

// Local file extraction
AudioMetadata metadata = {0};
char error[256] = {0};

if (metadata_extract_from_file("/path/to/song.mp3", &metadata, error, sizeof(error)) == 1) {
    printf("Title: %s\n", metadata.title);
    printf("Artist: %s\n", metadata.artist);
    printf("Album: %s\n", metadata.album);
    metadata_destroy(&metadata);
} else {
    printf("Error: %s\n", error);
}

// Remote SSH file extraction
if (metadata_extract_from_ssh("user", "192.168.1.100", "/home/user/Music/song.mp3",
                              &metadata, error, sizeof(error)) == 1) {
    printf("Remote Title: %s\n", metadata.title);
    metadata_destroy(&metadata);
}
```

## Performance Considerations

- Metadata extraction is performed **once per user action** (not continuously)
- External tool invocation takes ~100-500ms depending on file size and tool
- SSH extraction adds network latency (typically 50-200ms for LAN)
- No blocking of UI (if async implementation added in future)
- No in-memory buffering of audio data

## Security Considerations

- File paths are properly quoted in shell commands to prevent injection
- SSH validation prevents malicious username/IP input
- Input validation ensures files exist before processing
- Error messages don't expose sensitive system paths
- SSH uses secure options (BatchMode=yes, no TTY required)
- External tools execute with current user privileges (expected)

## Troubleshooting

### "Could not extract remote metadata" Error

**Possible causes and solutions:**

1. **mediainfo/ffprobe not installed on remote system**
   - Solution: Install on remote: `sudo apt install mediainfo` or `sudo apt install ffmpeg` (for ffprobe)
   - The feature requires at least one of these tools on the remote system

2. **SSH connection failed**
   - Check SSH key is properly configured for passwordless auth
   - Verify username, IP address, and SSH port are correct
   - Test manually: `ssh -o BatchMode=yes username@ip "echo OK"`
   - Check firewall allows SSH access

3. **File not found or not readable on remote**
   - Verify the file path is correct and accessible
   - Check file permissions with: `ssh user@ip "ls -l /path/to/file"`
   - Use the directory-specific file picker if the file location is known

4. **Network timeout or latency**
   - The SSH command has a 5-second timeout
   - Check network connectivity and latency
   - Large files may take longer to extract metadata

### Important: mediainfo stdin limitation

**Note**: mediainfo does NOT support reading from piped input (`/dev/stdin`). The SSH implementation uses direct file access for mediainfo:
```bash
ssh user@ip 'mediainfo -- /path/to/file'  # Works
ssh user@ip 'cat /path/to/file' | mediainfo -- /dev/stdin  # Doesn't work
```

Only ffprobe supports piped input. The implementation automatically handles this difference.

### Debug Script

To diagnose SSH metadata extraction issues, use the debug script:
```bash
/tmp/debug_ssh_metadata.sh <username> <ip> <filepath>
```

This will test:
- SSH connectivity
- File existence on remote
- mediainfo/ffprobe availability
- Actual metadata extraction

## Maintenance Notes

- Update tool command strings if `mediainfo`/`ffprobe` output format changes
- Test with various audio formats when updating tools
- mediainfo and ffprobe have different quoting requirements (handle separately)
- Review error handling with actual tool failures and network issues
- SSH commands in metadata.c should be kept in sync with those in app.c
