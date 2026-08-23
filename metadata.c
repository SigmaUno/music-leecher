/*
 * metadata.c
 *
 * Extracts metadata from local audio files using external tools (mediainfo or ffprobe).
 * Falls back gracefully if tools are unavailable.
 */

#define _POSIX_C_SOURCE 200809L

#include "metadata.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_error(char *error, size_t error_size, const char *format, ...) {
    va_list args;
    if (!error || !error_size) return;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
}

static void trim_string(char *str) {
    if (!str || !*str) return;
    char *end;
    while (*str && isspace((unsigned char)*str)) str++;
    memmove(str, str, strlen(str) + 1);
    if (!*str) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
}

/* Validate SSH username/IP format (prevent command injection) */
static int valid_ssh_name(const char *value, int allow_colon) {
    const unsigned char *p = (const unsigned char *)value;
    if (!p || !*p) return 0;
    for (; *p; p++) {
        if (!isalnum(*p) && *p != '.' && *p != '-' && *p != '_' &&
            !(allow_colon && *p == ':')) return 0;
    }
    return 1;
}

/* Quote SSH path to prevent shell injection */
static char *remote_cat_command(const char *path) {
    const char *cursor;
    char *command, *out;
    size_t length = 8; /* "cat -- " plus the final NUL */
    for (cursor = path; *cursor; cursor++) length += *cursor == '\'' ? 4 : 1;
    command = malloc(length + 2); /* enclosing single quotes */
    if (!command) return NULL;
    out = command;
    memcpy(out, "cat -- ", 7); out += 7;
    *out++ = '\'';
    for (cursor = path; *cursor; cursor++) {
        if (*cursor == '\'') { memcpy(out, "'\\''", 4); out += 4; }
        else *out++ = *cursor;
    }
    *out++ = '\'';
    *out = '\0';
    return command;
}

/* Parse key=value format output from mediainfo */
static void parse_mediainfo_line(const char *line, AudioMetadata *metadata) {
    char key[256], value[256];
    if (sscanf(line, "%255[^=]=%255[^\n]", key, value) != 2) return;
    trim_string(key);
    trim_string(value);
    if (strcmp(key, "Title") == 0 && !metadata->title) {
        metadata->title = malloc(strlen(value) + 1);
        if (metadata->title) strcpy(metadata->title, value);
    } else if (strcmp(key, "Performer") == 0 && !metadata->artist) {
        metadata->artist = malloc(strlen(value) + 1);
        if (metadata->artist) strcpy(metadata->artist, value);
    } else if (strcmp(key, "Album") == 0 && !metadata->album) {
        metadata->album = malloc(strlen(value) + 1);
        if (metadata->album) strcpy(metadata->album, value);
    }
}

/* Extract metadata using mediainfo command */
static int extract_with_mediainfo(const char *filepath, AudioMetadata *metadata, char *error, size_t error_size) {
    FILE *pipe;
    char command[1024];
    char line[512];

    if (snprintf(command, sizeof(command), "mediainfo --Output='General;Title=%%Title%% \\nPerformer=%%Performer%% \\nAlbum=%%Album%%' -- '%s' 2>/dev/null", filepath) >= (int)sizeof(command)) {
        set_error(error, error_size, "Command path too long");
        return -1;
    }

    pipe = popen(command, "r");
    if (!pipe) {
        set_error(error, error_size, "Could not execute mediainfo");
        return -1;
    }

    while (fgets(line, sizeof(line), pipe)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (*line) parse_mediainfo_line(line, metadata);
    }
    pclose(pipe);
    return (metadata->title || metadata->artist || metadata->album) ? 1 : 0;
}

/* Extract metadata using ffprobe command */
static int extract_with_ffprobe(const char *filepath, AudioMetadata *metadata, char *error, size_t error_size) {
    FILE *pipe;
    char command[1024];
    char line[512];

    if (snprintf(command, sizeof(command), "ffprobe -v error -show_format -print_format compact=p=\\: '%s' 2>/dev/null | grep -E '(title|artist|album)='", filepath) >= (int)sizeof(command)) {
        set_error(error, error_size, "Command path too long");
        return -1;
    }

    pipe = popen(command, "r");
    if (!pipe) {
        set_error(error, error_size, "Could not execute ffprobe");
        return -1;
    }

    while (fgets(line, sizeof(line), pipe)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *sep = strchr(line, '=');
        if (sep) {
            *sep = '\0';
            const char *key = line;
            const char *value = sep + 1;
            
            if (strcmp(key, "TAG:title") == 0 && !metadata->title) {
                metadata->title = malloc(strlen(value) + 1);
                if (metadata->title) strcpy(metadata->title, value);
            } else if (strcmp(key, "TAG:artist") == 0 && !metadata->artist) {
                metadata->artist = malloc(strlen(value) + 1);
                if (metadata->artist) strcpy(metadata->artist, value);
            } else if (strcmp(key, "TAG:album") == 0 && !metadata->album) {
                metadata->album = malloc(strlen(value) + 1);
                if (metadata->album) strcpy(metadata->album, value);
            }
        }
    }
    pclose(pipe);
    return (metadata->title || metadata->artist || metadata->album) ? 1 : 0;
}

int metadata_extract_from_file(const char *filepath, AudioMetadata *metadata, char *error, size_t error_size) {
    int result;
    
    if (!filepath || !metadata) {
        set_error(error, error_size, "Invalid parameters");
        return -1;
    }

    if (access(filepath, F_OK) != 0) {
        set_error(error, error_size, "File not found");
        return 0;
    }

    memset(metadata, 0, sizeof(*metadata));

    /* Try mediainfo first (more reliable for various formats) */
    result = extract_with_mediainfo(filepath, metadata, error, error_size);
    if (result > 0) return 1;

    /* Fall back to ffprobe */
    memset(metadata, 0, sizeof(*metadata));
    result = extract_with_ffprobe(filepath, metadata, error, error_size);
    if (result > 0) return 1;

    set_error(error, error_size, "Could not extract metadata (mediainfo and ffprobe not available)");
    return -1;
}

/* Extract metadata from remote file via SSH */
static int extract_ssh_metadata_with_tool(const char *username, const char *ip, const char *filepath,
                                          AudioMetadata *metadata, const char *tool, char *error, size_t error_size) {
    FILE *pipe;
    char command[2048];
    char line[512];
    int result;
    char *remote_cmd = NULL;

    /* Verify SSH credentials format */
    if (!username || !*username || !ip || !*ip || !filepath || !*filepath) {
        set_error(error, error_size, "Invalid SSH parameters");
        return -1;
    }

    /* Check if username/ip are valid for shell (prevent injection) */
    if (!valid_ssh_name(username, 0) || !valid_ssh_name(ip, 1)) {
        set_error(error, error_size, "Invalid SSH username or IP format");
        return -1;
    }

    /* For mediainfo: use remote file directly (doesn't support stdin).
     * For ffprobe: pipe through stdin (does support stdin) */
    if (strcmp(tool, "mediainfo") == 0) {
        /* mediainfo needs direct file access, not piped input.
         * Quote the filepath safely for shell. */
        result = snprintf(command, sizeof(command),
            "ssh -o BatchMode=yes -o RequestTTY=no -o ClearAllForwardings=yes -o LogLevel=ERROR -- %s@%s "
            "'mediainfo --Output=\"General;Title=%%Title%% \\nPerformer=%%Performer%% \\nAlbum=%%Album%%\" -- '\"'\"'%s'\"'\"'' 2>/dev/null",
            username, ip, filepath);
    } else if (strcmp(tool, "ffprobe") == 0) {
        /* ffprobe supports piped input via /dev/stdin */
        remote_cmd = remote_cat_command(filepath);
        if (!remote_cmd) {
            set_error(error, error_size, "Path too long");
            return -1;
        }
        result = snprintf(command, sizeof(command),
            "ssh -o BatchMode=yes -o RequestTTY=no -o ClearAllForwardings=yes -o LogLevel=ERROR -- %s@%s %s 2>/dev/null | ffprobe -v error -show_format -print_format compact=p=\\: - 2>/dev/null | grep -E '(title|artist|album)='",
            username, ip, remote_cmd);
    } else {
        if (remote_cmd) free(remote_cmd);
        set_error(error, error_size, "Unknown metadata tool");
        return -1;
    }

    if (result >= (int)sizeof(command)) {
        if (remote_cmd) free(remote_cmd);
        set_error(error, error_size, "Command too long");
        return -1;
    }

    pipe = popen(command, "r");
    if (remote_cmd) free(remote_cmd);
    if (!pipe) {
        set_error(error, error_size, "Could not execute SSH command");
        return -1;
    }

    if (strcmp(tool, "mediainfo") == 0) {
        while (fgets(line, sizeof(line), pipe)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (*line) parse_mediainfo_line(line, metadata);
        }
    } else {
        while (fgets(line, sizeof(line), pipe)) {
            line[strcspn(line, "\r\n")] = '\0';
            char *sep = strchr(line, '=');
            if (sep) {
                *sep = '\0';
                const char *key = line;
                const char *value = sep + 1;
                
                if (strcmp(key, "TAG:title") == 0 && !metadata->title) {
                    metadata->title = malloc(strlen(value) + 1);
                    if (metadata->title) strcpy(metadata->title, value);
                } else if (strcmp(key, "TAG:artist") == 0 && !metadata->artist) {
                    metadata->artist = malloc(strlen(value) + 1);
                    if (metadata->artist) strcpy(metadata->artist, value);
                } else if (strcmp(key, "TAG:album") == 0 && !metadata->album) {
                    metadata->album = malloc(strlen(value) + 1);
                    if (metadata->album) strcpy(metadata->album, value);
                }
            }
        }
    }

    pclose(pipe);
    return (metadata->title || metadata->artist || metadata->album) ? 1 : 0;
}

int metadata_extract_from_ssh(const char *username, const char *ip, const char *filepath,
                              AudioMetadata *metadata, char *error, size_t error_size) {
    int result;
    char tool_error[256] = {0};
    
    if (!username || !ip || !filepath || !metadata) {
        set_error(error, error_size, "Invalid parameters");
        return -1;
    }

    memset(metadata, 0, sizeof(*metadata));

    /* Try mediainfo first */
    result = extract_ssh_metadata_with_tool(username, ip, filepath, metadata, "mediainfo", tool_error, sizeof(tool_error));
    if (result > 0) return 1;

    /* Fall back to ffprobe */
    memset(metadata, 0, sizeof(*metadata));
    result = extract_ssh_metadata_with_tool(username, ip, filepath, metadata, "ffprobe", tool_error, sizeof(tool_error));
    if (result > 0) return 1;

    /* Provide detailed error based on last tool attempt */
    if (strstr(tool_error, "Invalid SSH") || strstr(tool_error, "Invalid parameters")) {
        set_error(error, error_size, "%s", tool_error);
    } else if (strstr(tool_error, "Command too long") || strstr(tool_error, "Path too long")) {
        set_error(error, error_size, "%s", tool_error);
    } else if (strstr(tool_error, "Could not execute")) {
        set_error(error, error_size, "SSH connection failed. Check SSH key, username, IP, and firewall.");
    } else {
        set_error(error, error_size, "Remote metadata extraction failed. Ensure mediainfo/ffprobe installed on remote system and file is readable.");
    }
    return -1;
}

void metadata_destroy(AudioMetadata *metadata) {
    if (!metadata) return;
    free(metadata->title);
    free(metadata->artist);
    free(metadata->album);
    memset(metadata, 0, sizeof(*metadata));
}
