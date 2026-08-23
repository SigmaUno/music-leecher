#define _DEFAULT_SOURCE
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION

#include "library_handler.h"
#include "music_ripper.h"
#include "assembler.h"
#include "decoder.h"
#include "metadata.h"
#include "third_party/nuklear/nuklear.h"
#include "third_party/nuklear/nuklear_sdl_renderer.h"

#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef enum { SOURCE_LOCAL, SOURCE_SSH, SOURCE_HTTPS, SOURCE_NETWORK } SourceMethod;

typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    char path[512];
    char username[128];
    char url[512];
    char ip[64];
    char status[256];
    SourceMethod method;
    size_t selected_track;
    SDL_AudioDeviceID audio_device;
    pid_t ssh_agent_pid;
    char ssh_agent_dir[64];
    char ssh_agent_socket[96];
} AppState;

static void launch_credential_agent(const char *program, AppState *state, const char *message) {
    pid_t pid = fork();
    if (pid == 0) { execlp(program, program, (char *)NULL); _exit(127); }
    snprintf(state->status, sizeof(state->status), "%s", pid < 0 ? "Could not start the system credential agent." : message);
}

static int start_ssh_agent(AppState *state) {
    char directory[] = "/tmp/leecher-ssh-agent-XXXXXX";
    pid_t pid;
    int attempt;

    if (!mkdtemp(directory)) {
        snprintf(state->status, sizeof(state->status), "Could not create a private directory for the SSH agent.");
        return 0;
    }
    snprintf(state->ssh_agent_dir, sizeof(state->ssh_agent_dir), "%s", directory);
    snprintf(state->ssh_agent_socket, sizeof(state->ssh_agent_socket), "%s/socket", directory);
    pid = fork();
    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) { dup2(null_fd, STDOUT_FILENO); dup2(null_fd, STDERR_FILENO); close(null_fd); }
        execlp("ssh-agent", "ssh-agent", "-D", "-a", state->ssh_agent_socket, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        rmdir(state->ssh_agent_dir);
        state->ssh_agent_dir[0] = '\0';
        snprintf(state->status, sizeof(state->status), "Could not start an SSH agent.");
        return 0;
    }
    state->ssh_agent_pid = pid;
    for (attempt = 0; attempt < 100; attempt++) {
        if (access(state->ssh_agent_socket, F_OK) == 0) {
            setenv("SSH_AUTH_SOCK", state->ssh_agent_socket, 1);
            return 1;
        }
        if (waitpid(pid, NULL, WNOHANG) == pid) break;
        SDL_Delay(10);
    }
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    unlink(state->ssh_agent_socket);
    rmdir(state->ssh_agent_dir);
    state->ssh_agent_pid = 0;
    state->ssh_agent_dir[0] = '\0';
    state->ssh_agent_socket[0] = '\0';
    snprintf(state->status, sizeof(state->status), "The SSH agent did not become ready.");
    return 0;
}

static void stop_ssh_agent(AppState *state) {
    const char *socket = getenv("SSH_AUTH_SOCK");
    if (state->ssh_agent_pid > 0) {
        kill(state->ssh_agent_pid, SIGTERM);
        waitpid(state->ssh_agent_pid, NULL, 0);
    }
    if (state->ssh_agent_socket[0]) unlink(state->ssh_agent_socket);
    if (state->ssh_agent_dir[0]) rmdir(state->ssh_agent_dir);
    if (socket && strcmp(socket, state->ssh_agent_socket) == 0) unsetenv("SSH_AUTH_SOCK");
}

static void unlock_ssh_agent(AppState *state) {
    const char *socket = getenv("SSH_AUTH_SOCK");

    if (!socket || !socket[0]) if (!start_ssh_agent(state)) return;
    launch_credential_agent("ssh-add", state,
                            "SSH agent is ready; add your key in the system prompt.");
}

static const char *method_name(SourceMethod method) {
    static const char *names[] = { "Local file", "SSH", "HTTPS", "Local network" };
    return names[method];
}

static LibrarySourceKind library_kind(SourceMethod method) {
    return (LibrarySourceKind)method;
}

static int assemble_audio(const unsigned char *data, size_t size, void *userdata) {
    return assembler_push(userdata, data, size) == 1;
}

static int valid_ssh_name(const char *value, int allow_colon) {
    const unsigned char *p = (const unsigned char *)value;
    if (!p || !*p) return 0;
    for (; *p; p++) {
        if (!isalnum(*p) && *p != '.' && *p != '-' && *p != '_' &&
            !(allow_colon && *p == ':')) return 0;
    }
    return 1;
}

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

static int stream_ssh(const LibrarySource *source, MusicRipperWriteFn write,
                      void *write_userdata, void *transport_userdata) {
    char target[256];
    char *command;
    unsigned char buffer[64 * 1024];
    int pipe_fds[2], status;
    pid_t pid;
    ssize_t bytes;
    int result = 0;
    (void)transport_userdata;
    if (!source || !source->path || !valid_ssh_name(source->username, 0) ||
        !valid_ssh_name(source->ip, 1)) return -1;
    if (snprintf(target, sizeof(target), "%s@%s", source->username, source->ip) >= (int)sizeof(target)) return -1;
    command = remote_cat_command(source->path);
    if (!command || pipe(pipe_fds) != 0) { free(command); return -1; }
    pid = fork();
    if (pid == 0) {
        char *const arguments[] = { "ssh", "-o", "BatchMode=yes", "-o", "RequestTTY=no",
            "-o", "ClearAllForwardings=yes", "-o", "LogLevel=ERROR", "--", target, command, NULL };
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipe_fds[1]);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    close(pipe_fds[1]);
    if (pid < 0) { close(pipe_fds[0]); free(command); return -1; }
    while ((bytes = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        if (!write(buffer, (size_t)bytes, write_userdata)) { result = -1; break; }
    }
    if (bytes < 0 && errno != EINTR) result = -1;
    close(pipe_fds[0]);
    if (result) kill(pid, SIGTERM);
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) result = -1;
    free(command);
    return result;
}

static int stream_https(const LibrarySource *source, MusicRipperWriteFn write,
                        void *write_userdata, void *transport_userdata) {
    unsigned char buffer[64 * 1024];
    int pipe_fds[2], status;
    pid_t pid;
    ssize_t bytes;
    int result = 0;
    (void)transport_userdata;
    if (!source || !source->url || strncasecmp(source->url, "https://", 8) != 0) return -1;
    if (pipe(pipe_fds) != 0) return -1;
    pid = fork();
    if (pid == 0) {
        char *const arguments[] = { "curl", "--fail", "--location", "--max-redirs", "5",
            "--proto", "=https", "--tlsv1.2", "--connect-timeout", "15", "--max-time", "300",
            "--silent", "--show-error", "--output", "-", "--", source->url, NULL };
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipe_fds[1]);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    close(pipe_fds[1]);
    if (pid < 0) { close(pipe_fds[0]); return -1; }
    while ((bytes = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        if (!write(buffer, (size_t)bytes, write_userdata)) { result = -1; break; }
    }
    if (bytes < 0 && errno != EINTR) result = -1;
    close(pipe_fds[0]);
    if (result) kill(pid, SIGTERM);
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) result = -1;
    return result;
}

static void choose_local_file(AppState *state) {
    char path[sizeof(state->path)] = {0};
    FILE *picker = popen("zenity --file-selection --title='Choose music file'", "r");
    if (!picker) { snprintf(state->status, sizeof(state->status), "Could not open the system file picker."); return; }
    if (fgets(path, sizeof(path), picker)) {
        AudioMetadata metadata = {0};
        char error[256] = {0};
        
        path[strcspn(path, "\r\n")] = '\0';
        snprintf(state->path, sizeof(state->path), "%s", path);
        
        /* Try to extract metadata from the file */
        if (metadata_extract_from_file(path, &metadata, error, sizeof(error)) == 1) {
            if (metadata.title) snprintf(state->title, sizeof(state->title), "%s", metadata.title);
            if (metadata.artist) snprintf(state->artist, sizeof(state->artist), "%s", metadata.artist);
            if (metadata.album) snprintf(state->album, sizeof(state->album), "%s", metadata.album);
            snprintf(state->status, sizeof(state->status), "Selected: %.150s (metadata extracted)", path);
            metadata_destroy(&metadata);
        } else {
            snprintf(state->status, sizeof(state->status), "Selected local file: %.220s", path);
        }
    }
    pclose(picker);
}

static void extract_ssh_metadata(AppState *state) {
    AudioMetadata metadata = {0};
    char error[256] = {0};
    int result;

    if (!state->username[0] || !state->ip[0] || !state->path[0]) {
        snprintf(state->status, sizeof(state->status), "Enter USERNAME, IP, and PATH to extract metadata.");
        return;
    }

    result = metadata_extract_from_ssh(state->username, state->ip, state->path, &metadata, error, sizeof(error));
    
    if (result == 1) {
        if (metadata.title) snprintf(state->title, sizeof(state->title), "%s", metadata.title);
        if (metadata.artist) snprintf(state->artist, sizeof(state->artist), "%s", metadata.artist);
        if (metadata.album) snprintf(state->album, sizeof(state->album), "%s", metadata.album);
        snprintf(state->status, sizeof(state->status), "Remote metadata extracted from %.150s", state->path);
        metadata_destroy(&metadata);
    } else if (result == 0) {
        snprintf(state->status, sizeof(state->status), "Remote file not found: %.220s", state->path);
    } else {
        snprintf(state->status, sizeof(state->status), "Metadata error: %s", error);
    }
}

static void choose_ssh_file(AppState *state) {
    char path[sizeof(state->path)] = {0};
    char command[2048];
    char temp_file[256];
    FILE *temp, *picker;
    AudioMetadata metadata = {0};
    char error[256] = {0};
    char line[512];
    int file_count = 0;

    if (!state->username[0] || !state->ip[0]) {
        snprintf(state->status, sizeof(state->status), "Enter USERNAME and IP first.");
        return;
    }

    /* Create temp file for file list output */
    snprintf(temp_file, sizeof(temp_file), "/tmp/music_files_%ld.txt", (long)time(NULL));

    /* If PATH is already entered, search only in that directory */
    if (state->path[0]) {
        /* Search in the specified directory */
        if (snprintf(command, sizeof(command),
            "ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -- %s@%s "
            "'find \"%s\" -maxdepth 1 -type f \\( -iname \"*.mp3\" -o -iname \"*.flac\" -o -iname \"*.ogg\" -o -iname \"*.wav\" -o -iname \"*.m4a\" \\) 2>/dev/null | sort' > %s 2>&1",
            state->username, state->ip, state->path, temp_file) >= (int)sizeof(command)) {
            snprintf(state->status, sizeof(state->status), "SSH command too long.");
            return;
        }
    } else {
        /* No PATH specified - search from home with fallbacks */
        if (snprintf(command, sizeof(command),
            "(ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -- %s@%s "
            "'find ~/ -type f \\( -iname \"*.mp3\" -o -iname \"*.flac\" -o -iname \"*.ogg\" -o -iname \"*.wav\" -o -iname \"*.m4a\" \\) 2>/dev/null' || "
            "ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -- %s@%s "
            "'find ~/Music -type f 2>/dev/null' || "
            "ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -- %s@%s "
            "'ls -1 ~/Music/*.mp3 ~/Music/*.flac ~/Music/*.ogg 2>/dev/null') 2>&1 | sort > %s",
            state->username, state->ip,
            state->username, state->ip,
            state->username, state->ip,
            temp_file) >= (int)sizeof(command)) {
            snprintf(state->status, sizeof(state->status), "SSH command too long.");
            return;
        }
    }

    /* Execute the command and wait for completion */
    system(command);

    /* Read results from temp file */
    temp = fopen(temp_file, "r");
    if (!temp) {
        snprintf(state->status, sizeof(state->status), "Could not create file list (SSH may have failed).");
        return;
    }

    /* Count non-empty lines */
    file_count = 0;
    while (fgets(line, sizeof(line), temp)) {
        if (line[0] && line[0] != ' ' && line[0] != '\n') {
            file_count++;
        }
    }
    rewind(temp);

    if (file_count == 0) {
        fclose(temp);
        unlink(temp_file);
        if (state->path[0]) {
            snprintf(state->status, sizeof(state->status), "No music files in: %.180s", state->path);
        } else {
            snprintf(state->status, sizeof(state->status), 
                "No music files found. Check SSH access, music location, or file permissions.");
        }
        return;
    }

    /* Build zenity command - use file directly, not pipe */
    char zenity_cmd[2048];
    if (state->path[0]) {
        snprintf(zenity_cmd, sizeof(zenity_cmd), 
            "zenity --list --title='Music files in %s (%d files)' --column='File' --width=750 --height=550 < %s",
            state->path, file_count, temp_file);
    } else {
        snprintf(zenity_cmd, sizeof(zenity_cmd), 
            "zenity --list --title='Choose remote music file (%d files)' --column='File' --width=750 --height=550 < %s",
            file_count, temp_file);
    }

    picker = popen(zenity_cmd, "r");
    if (!picker) {
        fclose(temp);
        unlink(temp_file);
        snprintf(state->status, sizeof(state->status), "Could not open file picker dialog.");
        return;
    }

    if (fgets(path, sizeof(path), picker)) {
        int status = pclose(picker);
        
        /* Check if user actually selected something (zenity returns 0 on success) */
        if (status == 0 && path[0]) {
            path[strcspn(path, "\r\n")] = '\0';
            
            if (path[0]) {
                snprintf(state->path, sizeof(state->path), "%s", path);

                /* Try to extract metadata from the remote file */
                if (metadata_extract_from_ssh(state->username, state->ip, path, &metadata, error, sizeof(error)) == 1) {
                    if (metadata.title) snprintf(state->title, sizeof(state->title), "%s", metadata.title);
                    if (metadata.artist) snprintf(state->artist, sizeof(state->artist), "%s", metadata.artist);
                    if (metadata.album) snprintf(state->album, sizeof(state->album), "%s", metadata.album);
                    snprintf(state->status, sizeof(state->status), "Selected: %.100s (metadata extracted)", path);
                    metadata_destroy(&metadata);
                } else {
                    snprintf(state->status, sizeof(state->status), "Selected: %.150s (metadata unavailable on remote)", path);
                }
            } else {
                snprintf(state->status, sizeof(state->status), "No file selected.");
            }
        } else {
            snprintf(state->status, sizeof(state->status), "No file selected.");
        }
    } else {
        pclose(picker);
        snprintf(state->status, sizeof(state->status), "File picker was cancelled or failed.");
    }

    fclose(temp);
    unlink(temp_file);
}




static void play_assembled_audio(Assembler *assembler, AppState *state) {
    DecoderPcm pcm = {0};
    SDL_AudioSpec desired = {0}, obtained = {0};
    char error[256] = {0};
    if (decoder_decode_queue(assembler, &pcm, error, sizeof(error)) != 1) { snprintf(state->status, sizeof(state->status), "%s", error); return; }
    desired.freq = pcm.sample_rate; desired.format = AUDIO_S16SYS; desired.channels = (Uint8)pcm.channels; desired.samples = 4096;
    if (state->audio_device) SDL_CloseAudioDevice(state->audio_device);
    state->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (!state->audio_device || SDL_QueueAudio(state->audio_device, pcm.samples, (Uint32)(pcm.sample_count * sizeof(*pcm.samples))) != 0) {
        snprintf(state->status, sizeof(state->status), "SDL audio: %s", SDL_GetError());
        if (state->audio_device) { SDL_CloseAudioDevice(state->audio_device); state->audio_device = 0; }
    } else {
        SDL_PauseAudioDevice(state->audio_device, 0);
        snprintf(state->status, sizeof(state->status), "Playing decoded audio through SDL.");
    }
    decoder_pcm_destroy(&pcm);
}

static void fetch_and_play_track(const MusicRipper *ripper, Assembler *assembler,
                                 const LibraryTrack *track, AppState *state) {
    LibrarySongQuery song = { .id = track->id, .title = track->title, .artist = track->artist, .album = track->album };
    char error[256] = {0};
    int result = music_ripper_play_next(ripper, &song, NULL, assemble_audio, assembler, error, sizeof(error));
    if (result == 1) play_assembled_audio(assembler, state);
    else snprintf(state->status, sizeof(state->status), "%s", result == 0 ? "Track was not found." : error);
}

static void draw_library(struct nk_context *ctx, const LibraryHandler *library, AppState *state,
                         const MusicRipper *ripper, Assembler *assembler) {
    size_t count = library_handler_track_count(library), i;
    if (!nk_group_begin(ctx, "library-scroll", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) return;
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_label(ctx, "LIBRARY", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Pick a song to play next", NK_TEXT_LEFT);
    for (i = 0; i < count; i++) {
        LibraryTrack track = {0};
        char label[384];
        if (library_handler_track_at(library, i, &track, NULL, 0) != 1) continue;
        snprintf(label, sizeof(label), "%s  |  %s%s%s", track.title ? track.title : "Untitled", track.artist ? track.artist : "Unknown artist", track.album ? "  -  " : "", track.album ? track.album : "");
        nk_layout_row_dynamic(ctx, 34, 1);
        if (nk_button_label(ctx, label)) {
            state->selected_track = i;
            fetch_and_play_track(ripper, assembler, &track, state);
        }
        library_handler_track_destroy(&track);
    }
    if (!count) { nk_layout_row_dynamic(ctx, 28, 1); nk_label(ctx, "No tracks in this library.", NK_TEXT_LEFT); }
    nk_group_end(ctx);
}

static void draw_scraper(struct nk_context *ctx, AppState *state, LibraryHandler **library,
                         const char *library_path, MusicRipper *ripper) {
    static const char *methods[] = { "Local file", "SSH", "HTTPS", "Local network" };
    if (!nk_group_begin(ctx, "scraper-scroll", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) return;
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_label(ctx, "SOURCE SCRAPER", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_label(ctx, "Build a source entry for the selected song", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28, 1);
    if (nk_combo_begin_label(ctx, method_name(state->method), nk_vec2(nk_widget_width(ctx), 140))) {
        int i;
        /* Combo items need their own row layout inside Nuklear's popup. */
        nk_layout_row_dynamic(ctx, 28, 1);
        for (i = 0; i < 4; i++) if (nk_combo_item_label(ctx, methods[i], NK_TEXT_LEFT)) state->method = (SourceMethod)i;
        nk_combo_end(ctx);
    }
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label(ctx, "Title", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->title, sizeof(state->title), nk_filter_default);
    nk_label(ctx, "Artist", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->artist, sizeof(state->artist), nk_filter_default);
    nk_label(ctx, "Album", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->album, sizeof(state->album), nk_filter_default);
    if (state->method == SOURCE_LOCAL || state->method == SOURCE_SSH || state->method == SOURCE_NETWORK) {
        nk_label(ctx, "PATH", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->path, sizeof(state->path), nk_filter_default);
        if (state->method == SOURCE_LOCAL) { nk_layout_row_dynamic(ctx, 28, 1); if (nk_button_label(ctx, "Choose local music file")) choose_local_file(state); nk_layout_row_dynamic(ctx, 24, 1); }
    }
    if (state->method == SOURCE_SSH || state->method == SOURCE_NETWORK) {
        nk_label(ctx, "USERNAME", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->username, sizeof(state->username), nk_filter_default);
        nk_label(ctx, "IP", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->ip, sizeof(state->ip), nk_filter_default);
        if (state->method == SOURCE_SSH) { 
            nk_layout_row_dynamic(ctx, 28, 1); 
            if (nk_button_label(ctx, "Choose remote music file")) choose_ssh_file(state); 
            if (nk_button_label(ctx, "Extract SSH metadata")) extract_ssh_metadata(state); 
            nk_layout_row_dynamic(ctx, 24, 1); 
        }
    }
    if (state->method == SOURCE_HTTPS) {
        nk_label(ctx, "URL", NK_TEXT_LEFT); nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->url, sizeof(state->url), nk_filter_default);
    }
    nk_layout_row_dynamic(ctx, 30, 1);
    if (nk_button_label(ctx, "Save source to library")) {
        LibrarySongQuery song = { .title = state->title, .artist = state->artist, .album = state->album };
        LibrarySource source = { .kind = library_kind(state->method), .path = state->path[0] ? state->path : NULL, .username = state->username[0] ? state->username : NULL, .url = state->url[0] ? state->url : NULL, .ip = state->ip[0] ? state->ip : NULL };
        char error[256] = {0};
        if (library_handler_add_source(library_path, &song, &source, error, sizeof(error)) == 1) {
            LibraryHandler *reloaded = library_handler_open(library_path, error, sizeof(error));
            if (reloaded) { library_handler_close(*library); *library = reloaded; ripper->library = reloaded; snprintf(state->status, sizeof(state->status), "%s source saved to the library.", method_name(state->method)); }
            else snprintf(state->status, sizeof(state->status), "Saved source, but reload failed: %s", error);
        } else snprintf(state->status, sizeof(state->status), "%s", error);
    }
    nk_layout_row_dynamic(ctx, 1, 1); nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Credentials stay with your system agents.", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30, 2);
    if (nk_button_label(ctx, "Unlock SSH agent")) unlock_ssh_agent(state);
    if (nk_button_label(ctx, "Network settings")) launch_credential_agent("nm-connection-editor", state, "System network settings opened. This app never receives network passwords.");
    nk_layout_row_dynamic(ctx, 44, 1);
    nk_label_wrap(ctx, "A kernel cannot present remote-login prompts. SSH agents and the desktop network/keyring service own authentication; the app only supplies PATH, USERNAME, URL, and IP.");
    nk_group_end(ctx);
}

int main(int argc, char **argv) {
    const char *library_path = argc > 1 ? argv[1] : "library.example.json";
    char error[256] = {0};
    LibraryHandler *library = library_handler_open(library_path, error, sizeof(error));
    MusicRipper ripper = {0};
    Assembler *assembler;
    SDL_Window *window;
    SDL_Renderer *renderer;
    struct nk_context *ctx;
    SDL_Event event;
    AppState state = { .method = SOURCE_LOCAL };
    int running = 1;
    struct nk_color colors[NK_COLOR_COUNT];
    struct nk_font_atlas *font_atlas;
    if (!library) { fprintf(stderr, "Cannot load %s: %s\n", library_path, error); return 1; }
    ripper.library = library;
    ripper.transports.ssh = stream_ssh;
    ripper.transports.https = stream_https;
    assembler = assembler_create(NULL);
    if (!assembler) { fprintf(stderr, "Cannot create assembler queue\n"); library_handler_close(library); return 1; }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); assembler_destroy(assembler); library_handler_close(library); return 1; }
    window = SDL_CreateWindow("Leecher Music Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1160, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!window || !renderer) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); if (renderer) SDL_DestroyRenderer(renderer); if (window) SDL_DestroyWindow(window); SDL_Quit(); assembler_destroy(assembler); library_handler_close(library); return 1; }
    ctx = nk_sdl_init(window, renderer);
    nk_sdl_font_stash_begin(&font_atlas); nk_sdl_font_stash_end();
    memset(colors, 0, sizeof(colors));
    colors[NK_COLOR_TEXT] = nk_rgb(29, 42, 48); colors[NK_COLOR_WINDOW] = nk_rgb(244, 239, 225); colors[NK_COLOR_HEADER] = nk_rgb(22, 92, 94); colors[NK_COLOR_BORDER] = nk_rgb(201, 142, 82); colors[NK_COLOR_BUTTON] = nk_rgb(232, 202, 152); colors[NK_COLOR_BUTTON_HOVER] = nk_rgb(242, 177, 93); colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(204, 119, 55); colors[NK_COLOR_EDIT] = nk_rgb(255, 252, 244); colors[NK_COLOR_EDIT_CURSOR] = nk_rgb(22, 92, 94); colors[NK_COLOR_PROPERTY] = nk_rgb(255, 252, 244); colors[NK_COLOR_COMBO] = nk_rgb(232, 202, 152); colors[NK_COLOR_SCROLLBAR] = nk_rgb(215, 181, 125); colors[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(22, 92, 94); nk_style_from_table(ctx, colors);
    while (running) {
        nk_input_begin(ctx);
        while (SDL_PollEvent(&event)) { if (event.type == SDL_QUIT) running = 0; nk_sdl_handle_event(&event); }
        nk_input_end(ctx);
        if (nk_begin(ctx, "Leecher", nk_rect(0, 0, 1160, 720), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(ctx, 32, 1); nk_label(ctx, "LEEcher  /  library to stream", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 24, 1); nk_label(ctx, state.status[0] ? state.status : "Choose a library track or add a source route.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 620, 2); draw_library(ctx, library, &state, &ripper, assembler); draw_scraper(ctx, &state, &library, library_path, &ripper);
        }
        nk_end(ctx);
        SDL_SetRenderDrawColor(renderer, 244, 239, 225, 255); SDL_RenderClear(renderer); nk_sdl_render(NK_ANTI_ALIASING_ON); SDL_RenderPresent(renderer);
    }
    if (state.audio_device) SDL_CloseAudioDevice(state.audio_device);
    stop_ssh_agent(&state);
    nk_sdl_shutdown(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); assembler_destroy(assembler); library_handler_close(library);
    return 0;
}
