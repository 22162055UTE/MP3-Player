#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <stdbool.h> // Sử dụng kiểu bool cho dễ đọc
#include <errno.h>   // Để kiểm tra lỗi
#include <math.h>    // Cho hàm abs
#include <ctype.h>   // For tolower in search
#include <taglib/tag_c.h> // *** Thêm thư viện TagLib C ***

// --- Định nghĩa Hằng số ---
#define WINDOW_WIDTH 1300
#define WINDOW_HEIGHT 600
#define MAX_SONGS 500 // Tăng giới hạn nếu cần
#define PLAYLIST_FILE "playlist.dat" // Có thể dùng file nhị phân hoặc text
#define USB_MONITOR_DIR_FORMAT "/media/%s"
#define FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define FALLBACK_FONT_PATH "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
#define SEARCH_QUERY_MAX 255

// --- Cấu trúc SongInfo ---
typedef struct {
    char *filepath;
    char *title;
    char *artist;
    char *album;
    int year;
    char *genre;
    char *display_name; // Tên dùng để hiển thị (ưu tiên title, fallback filename)
    char *search_string; // Chuỗi tổng hợp để tìm kiếm (lowercase)
    bool metadata_loaded;
} SongInfo;

// --- Biến Toàn cục ---
SongInfo playlist[MAX_SONGS]; // Thay đổi kiểu playlist
int num_songs = 0;
int current_index = -1; // Index trong playlist gốc
int volume = 100;
volatile int current_time = 0;
volatile int total_time = 0;
volatile bool is_playing = false;
volatile bool is_paused = false;
volatile bool song_finished_flag = false;

FILE* mpg123_stdin = NULL;
pid_t mpg123_pid = -1;
pthread_t monitor_thread;
pthread_t usb_thread;
volatile bool monitor_thread_should_run = false;
volatile bool usb_thread_running = false;
pthread_mutex_t playlist_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Biến cho Tìm kiếm ---
bool search_active = false;
char search_query[SEARCH_QUERY_MAX + 1] = {0};
int filtered_indices[MAX_SONGS];
int num_filtered_songs = 0;
int current_filtered_index = -1; // Index trong danh sách lọc

// --- Khai báo hàm ---
void play_music(int playlist_idx);
void stop_music();
void cleanup();
void find_usb_and_create_playlist(bool initial_scan);
// int load_playlist_from_file(const char* filename); // Hiện không dùng
// void save_playlist_to_file(const char* filename); // Hiện không dùng
void load_song_metadata(const char *filepath, SongInfo *song);
void free_song_info(SongInfo *song);
void update_search_filter();
void draw_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color);
void* monitor_mpg123_output(void* arg);
void* watch_usb_changes(void* arg);

// --- Hàm kiểm tra đuôi .mp3 ---
int has_mp3_extension(const char* filename) {
    if (!filename) return 0;
    const char* dot = strrchr(filename, '.');
    return dot && (strcasecmp(dot, ".mp3") == 0);
}

// --- Giải phóng tài nguyên của một SongInfo ---
void free_song_info(SongInfo *song) {
    if (!song) return;
    free(song->filepath);
    free(song->title);
    free(song->artist);
    free(song->album);
    free(song->genre);
    free(song->display_name);
    free(song->search_string);
    // Đặt lại con trỏ để tránh double free
    song->filepath = song->title = song->artist = song->album = song->genre = song->display_name = song->search_string = NULL;
    song->metadata_loaded = false;
    song->year = 0;
}

// --- Hàm đọc metadata từ file MP3 ---
void load_song_metadata(const char *filepath, SongInfo *song) {
    // Khởi tạo giá trị mặc định
    song->filepath = strdup(filepath);
    song->title = NULL;
    song->artist = NULL;
    song->album = NULL;
    song->year = 0;
    song->genre = NULL;
    song->display_name = NULL;
    song->search_string = NULL;
    song->metadata_loaded = false;

    if (!song->filepath) {
        perror("strdup filepath failed");
        return;
    }

    // Lấy tên file làm display_name mặc định
    const char* filename_only = strrchr(filepath, '/');
    filename_only = filename_only ? filename_only + 1 : filepath;
    song->display_name = strdup(filename_only);
    if (!song->display_name) {
        perror("strdup display_name failed");
        // Tiếp tục với các trường khác
    }

    // Đặt taglib không in lỗi ra stderr
    taglib_set_strings_unicode(true); // Quan trọng cho tên file/tag UTF-8
    taglib_set_string_management_enabled(false); // Tự quản lý bộ nhớ chuỗi trả về

    TagLib_File *file = taglib_file_new(filepath);
    if (file == NULL) {
        // fprintf(stderr, "Debug: taglib_file_new failed for %s\n", filepath);
        goto build_search_string; // Vẫn tạo search_string từ filename
    }

    TagLib_Tag *tag = taglib_file_tag(file);
    if (tag != NULL) {
        char *title = taglib_tag_title(tag);
        char *artist = taglib_tag_artist(tag);
        char *album = taglib_tag_album(tag);
        unsigned int year = taglib_tag_year(tag);
        char *genre = taglib_tag_genre(tag);

        if (title && strlen(title) > 0) song->title = strdup(title);
        if (artist && strlen(artist) > 0) song->artist = strdup(artist);
        if (album && strlen(album) > 0) song->album = strdup(album);
        song->year = year;
        if (genre && strlen(genre) > 0) song->genre = strdup(genre);

        // Ưu tiên title làm display_name
        if (song->title) {
            free(song->display_name); // Giải phóng tên file mặc định
            song->display_name = strdup(song->title);
        }

        // Giải phóng chuỗi trả về từ taglib C bindings nếu không dùng strdup
        taglib_free(title);
        taglib_free(artist);
        taglib_free(album);
        taglib_free(genre);

        song->metadata_loaded = true;
        // printf("Debug: Loaded metadata for %s (Title: %s)\n", filepath, song->title ? song->title : "N/A");
    } else {
        // fprintf(stderr, "Debug: taglib_file_tag failed for %s\n", filepath);
    }

    taglib_file_free(file); // Luôn giải phóng file handle

build_search_string:
    // --- Tạo chuỗi tìm kiếm tổng hợp (lowercase) ---
    size_t needed = 1; // for null terminator
    if (song->display_name) needed += strlen(song->display_name);
    if (song->artist) needed += strlen(song->artist);
    if (song->album) needed += strlen(song->album);
    if (song->genre) needed += strlen(song->genre);
    needed += 10; // Thêm khoảng trắng, năm và dự phòng

    song->search_string = malloc(needed);
    if (song->search_string) {
        char year_str[16] = ""; //Tang kich thuoc
        if (song->year > 0) snprintf(year_str, sizeof(year_str), "%d", song->year);

        snprintf(song->search_string, needed, "%s %s %s %s %s",
                 song->display_name ? song->display_name : "",
                 song->artist ? song->artist : "",
                 song->album ? song->album : "",
                 year_str,
                 song->genre ? song->genre : "");

        for (int i = 0; song->search_string[i]; i++) {
            song->search_string[i] = tolower(song->search_string[i]);
        }
    } else {
        perror("malloc for search_string failed");
    }
}


// --- Hàm đệ quy tìm, lưu file MP3 và đọc metadata ---
void search_and_save_mp3_recursive(const char* path, SongInfo* current_playlist, int* count, int max_songs) {
    DIR* dir = opendir(path);
    if (!dir) {
        // fprintf(stderr, "Warning: Cannot open directory %s: %s\n", path, strerror(errno));
        return;
    }
    struct dirent* entry;
    char fullPath[2048];

    while (*count < max_songs && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        int len = snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);

        // Kiểm tra lỗi từ snprintf trước
        if (len < 0) {
            fprintf(stderr, "Error during path formatting for %s/%s\n", path, entry->d_name);
            continue; // Bỏ qua nếu có lỗi tạo đường dẫn
        }

        // Bây giờ kiểm tra xem đường dẫn có bị cắt ngắn không (len không âm ở đây)
        if ((size_t)len >= sizeof(fullPath)) {
            fprintf(stderr, "Warning: Path too long, skipping: %s/%s\n", path, entry->d_name);
            continue;
        }


        struct stat st;
        if (lstat(fullPath, &st) == 0) {
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                 search_and_save_mp3_recursive(fullPath, current_playlist, count, max_songs);
            } else if (S_ISREG(st.st_mode) && has_mp3_extension(entry->d_name)) {
                // *** SỬA LỖI Ở ĐÂY ***
                load_song_metadata(fullPath, &current_playlist[*count]); // Dùng current_playlist
                (*count)++;
            }
        } else {
             // fprintf(stderr, "Warning: lstat failed for %s: %s\n", fullPath, strerror(errno));
        }
    }
    closedir(dir);
}


// --- Hàm tìm USB và tạo playlist ---
void find_usb_and_create_playlist(bool initial_scan) {
    struct passwd* pw = getpwuid(getuid());
    if (!pw) { fprintf(stderr, "Error getting user info.\n"); return; }
    const char* username = pw->pw_name;
    char basePath[512];
    snprintf(basePath, sizeof(basePath), USB_MONITOR_DIR_FORMAT, username);

    printf("Scanning for MP3 files in %s and subdirectories...\n", basePath);

    SongInfo* temp_playlist = calloc(MAX_SONGS, sizeof(SongInfo));
    if (!temp_playlist) {
        perror("Failed to allocate memory for temporary playlist scan");
        return;
    }
    int temp_num_songs = 0;

    DIR* dir = opendir(basePath);
    if (dir) {
        struct dirent* entry;
        while (temp_num_songs < MAX_SONGS && (entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            char potential_usb_path[1024];
            snprintf(potential_usb_path, sizeof(potential_usb_path), "%s/%s", basePath, entry->d_name);
            struct stat st;
            if (stat(potential_usb_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("Scanning potential mount point: %s\n", potential_usb_path);
                search_and_save_mp3_recursive(potential_usb_path, temp_playlist, &temp_num_songs, MAX_SONGS);
            }
        }
        closedir(dir);
    } else {
        fprintf(stderr, "Warning: Could not open base directory %s. No USB drive mounted?\n", basePath);
    }

    printf("Scan complete. Found %d MP3 files.\n", temp_num_songs);

    pthread_mutex_lock(&playlist_mutex);

    for (int i = 0; i < num_songs; i++) {
        free_song_info(&playlist[i]);
    }
    num_songs = 0;

    for(int i = 0; i < temp_num_songs; i++) {
        playlist[i] = temp_playlist[i];
    }
    num_songs = temp_num_songs;

    current_index = (num_songs > 0) ? 0 : -1;
    if (!initial_scan) {
        stop_music();
        is_playing = false;
        is_paused = false;
        current_time = 0;
        total_time = 0;
        search_active = false;
        search_query[0] = '\0';
        num_filtered_songs = 0;
        current_filtered_index = -1;
        printf("Playlist updated due to USB change. Music stopped.\n");
    } else {
        printf("Initial playlist loaded with %d songs.\n", num_songs);
    }

    pthread_mutex_unlock(&playlist_mutex);
    free(temp_playlist);
}

// --- Hàm cập nhật danh sách lọc dựa trên search_query ---
void update_search_filter() {
    pthread_mutex_lock(&playlist_mutex);
    num_filtered_songs = 0;
    current_filtered_index = -1;

    if (!search_active || strlen(search_query) == 0) {
        num_filtered_songs = num_songs;
        for(int i=0; i<num_songs; ++i) filtered_indices[i] = i;
        // Cố gắng giữ lại chỉ số chọn trong view nếu nó còn tồn tại
        if (current_index != -1) {
             current_filtered_index = current_index;
        } else if (num_filtered_songs > 0){
            current_filtered_index = 0;
        }

    } else {
        char lower_query[SEARCH_QUERY_MAX + 1];
        strncpy(lower_query, search_query, SEARCH_QUERY_MAX);
        lower_query[SEARCH_QUERY_MAX] = '\0';
        for (int i = 0; lower_query[i]; i++) {
            lower_query[i] = tolower(lower_query[i]);
        }

        for (int i = 0; i < num_songs; ++i) {
            if (playlist[i].search_string && strstr(playlist[i].search_string, lower_query)) {
                if (num_filtered_songs < MAX_SONGS) {
                    filtered_indices[num_filtered_songs++] = i;
                } else {
                    fprintf(stderr, "Warning: Filtered results exceed MAX_SONGS limit.\n");
                    break;
                }
            }
        }
        if (num_filtered_songs > 0) {
            current_filtered_index = 0;
        }
         printf("Search for '%s': Found %d results.\n", search_query, num_filtered_songs);
    }
    pthread_mutex_unlock(&playlist_mutex);
}


// --- Luồng theo dõi output của mpg123 ---
void* monitor_mpg123_output(void* arg) {
    FILE* mpg123_stdout = (FILE*)arg;
    char line[512];
    bool local_song_finished = false;
    pid_t monitored_pid = mpg123_pid;

    printf("Monitor thread started for PID %d\n", monitored_pid);
    monitor_thread_should_run = true;

    while (monitor_thread_should_run && fgets(line, sizeof(line), mpg123_stdout)) {
        if (strncmp(line, "@F", 2) == 0) {
            int frame_curr, frame_left;
            double sec_curr, sec_left;
            int scan_result = sscanf(line, "@F %d %d %lf %lf", &frame_curr, &frame_left, &sec_curr, &sec_left);
            if (scan_result == 4) {
                current_time = (int)round(sec_curr);
                int new_total_time = (int)round(sec_curr + sec_left);
                if (new_total_time > 0 && (total_time <= 0 || abs(new_total_time - total_time) > 2)) {
                    total_time = new_total_time;
                }
            }
        } else if (strstr(line, "@P 0")) {
            printf("Monitor thread (PID %d) detected @P 0 (song finished naturally)\n", monitored_pid);
            local_song_finished = true;
            break;
        } else if (strstr(line, "@E ")) {
            fprintf(stderr, "mpg123 error (PID: %d): %s", monitored_pid, line + 3);
        }

        if (mpg123_pid != monitored_pid) {
             printf("Monitor thread (for PID %d) detects PID change to %d. Exiting.\n", monitored_pid, mpg123_pid);
             break;
        }
    }

    printf("Monitor thread (PID %d) loop finished. Should run flag: %d\n", monitored_pid, monitor_thread_should_run);
    fclose(mpg123_stdout);
    printf("Monitor thread (PID %d) closed stdout pipe.\n", monitored_pid);

    if (local_song_finished && monitor_thread_should_run) {
        song_finished_flag = true;
    }
    monitor_thread_should_run = false;
    printf("Monitor thread (PID %d) exiting.\n", monitored_pid);
    return NULL;
}

// --- Hàm dừng nhạc ---
void stop_music() {
    pid_t pid_to_stop = mpg123_pid;
    FILE* stdin_to_close = mpg123_stdin;

    printf("Stopping music (Target PID: %d)...\n", pid_to_stop);

    if (monitor_thread_should_run) {
        printf("Signaling monitor thread to stop...\n");
        monitor_thread_should_run = false;
    }

    is_playing = false;
    is_paused = false;
    song_finished_flag = false;
    mpg123_pid = -1;
    mpg123_stdin = NULL;

    if (stdin_to_close) {
        fprintf(stdin_to_close, "QUIT\n");
        fflush(stdin_to_close);
        fclose(stdin_to_close);
        printf("Sent QUIT and closed stdin pipe for PID %d.\n", pid_to_stop);
    }

    if (pid_to_stop > 0) {
        printf("Waiting for monitor thread (if any) to join...\n");
        // Cần kiểm tra xem monitor_thread có được tạo hợp lệ không trước khi join
        // Thay vì phức tạp, chỉ cần join nếu pid_to_stop > 0 là đủ tốt
        int join_result = pthread_join(monitor_thread, NULL);
        if(join_result != 0) {
             fprintf(stderr, "Warning: pthread_join failed with code %d\n", join_result);
        } else {
            printf("Monitor thread joined.\n");
        }

        printf("Waiting for PID %d to terminate...\n", pid_to_stop);
        int status;
        if (waitpid(pid_to_stop, &status, WNOHANG) == 0) {
            printf("PID %d still running, sending SIGTERM...\n", pid_to_stop);
            kill(pid_to_stop, SIGTERM);
            usleep(100000);
            if (waitpid(pid_to_stop, &status, WNOHANG) == 0) {
                printf("PID %d still running after SIGTERM, sending SIGKILL...\n", pid_to_stop);
                kill(pid_to_stop, SIGKILL);
            }
        }
        waitpid(pid_to_stop, NULL, 0);
        printf("PID %d stopped and reaped.\n", pid_to_stop);
    } else {
         printf("No active process was running.\n");
    }

    current_time = 0;
    total_time = 0;
    printf("Music stop sequence complete.\n");
}


// --- Hàm phát nhạc theo index trong playlist GỐC ---
void play_music(int playlist_idx) {
    pthread_mutex_lock(&playlist_mutex);
    if (playlist_idx < 0 || playlist_idx >= num_songs) {
        fprintf(stderr, "Error: Invalid song index %d (num_songs=%d)\n", playlist_idx, num_songs);
        pthread_mutex_unlock(&playlist_mutex);
        return;
    }
    SongInfo* song = &playlist[playlist_idx];
    if (!song || !song->filepath) {
         fprintf(stderr, "Error: Playlist entry or filepath at index %d is NULL\n", playlist_idx);
         pthread_mutex_unlock(&playlist_mutex);
         return;
    }
    char file_to_play[2048];
    strncpy(file_to_play, song->filepath, sizeof(file_to_play) - 1);
    file_to_play[sizeof(file_to_play) - 1] = '\0';
    pthread_mutex_unlock(&playlist_mutex);

    printf("Attempting to play index %d: %s\n", playlist_idx, file_to_play);

    stop_music();

    current_time = 0;
    total_time = 0;
    is_playing = false;
    is_paused = false;
    song_finished_flag = false;
    monitor_thread_should_run = false;

    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) { perror("pipe failed"); return; }

    pid_t child_pid = fork();
    if (child_pid < 0) {
        perror("fork failed");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return;
    }

    if (child_pid == 0) { // Tiến trình con (mpg123)
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        printf("Child process executing: mpg123 -R\n");
        execlp("mpg123", "mpg123", "-R", NULL);
        perror("execlp mpg123 failed"); exit(EXIT_FAILURE);
    } else { // Tiến trình cha
        mpg123_pid = child_pid;
        close(stdin_pipe[0]); close(stdout_pipe[1]);
        mpg123_stdin = fdopen(stdin_pipe[1], "w");
        FILE* mpg123_stdout_for_thread = fdopen(stdout_pipe[0], "r");

        if (!mpg123_stdin || !mpg123_stdout_for_thread) {
            perror("fdopen failed");
            if(mpg123_stdin) fclose(mpg123_stdin);
            if(mpg123_stdout_for_thread) fclose(mpg123_stdout_for_thread);
             // Đảm bảo đóng fd gốc nếu fdopen lỗi
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            kill(mpg123_pid, SIGKILL); waitpid(mpg123_pid, NULL, 0);
            mpg123_pid = -1; mpg123_stdin = NULL;
            return;
        }

        if (pthread_create(&monitor_thread, NULL, monitor_mpg123_output, (void*)mpg123_stdout_for_thread) != 0) {
             perror("pthread_create for monitor failed");
             fclose(mpg123_stdin); fclose(mpg123_stdout_for_thread);
             // Không cần đóng fd gốc vì fdopen đã thành công và chúng sẽ được đóng bởi fclose
             kill(mpg123_pid, SIGKILL); waitpid(mpg123_pid, NULL, 0);
             mpg123_pid = -1; mpg123_stdin = NULL;
             return;
        }

        fprintf(mpg123_stdin, "LOAD %s\n", file_to_play);
        fprintf(mpg123_stdin, "VOLUME %d\n", volume);
        fflush(mpg123_stdin);

        current_index = playlist_idx;
        is_playing = true;
        is_paused = false;
        printf("Sent initial commands to mpg123 (PID: %d). Playing index %d: %s\n", mpg123_pid, playlist_idx, file_to_play);
    }
}

// --- Hàm Pause/Resume ---
void toggle_pause() {
    if (mpg123_pid != -1 && mpg123_stdin && is_playing) {
        fprintf(mpg123_stdin, "PAUSE\n");
        fflush(mpg123_stdin);
        is_paused = !is_paused;
        printf("Toggle Pause: %s\n", is_paused ? "Paused" : "Resumed");
    } else if (!is_playing && current_index != -1) {
        play_music(current_index);
    }
}

// --- Hàm tua nhạc ---
void jump_seconds(int seconds) {
    if (mpg123_pid != -1 && mpg123_stdin && is_playing && !is_paused) {
        printf("Jumping %+d seconds\n", seconds);
        fprintf(mpg123_stdin, "JUMP %+ds\n", seconds);
        fflush(mpg123_stdin);
        int estimated_time = current_time + seconds;
        if (estimated_time < 0) estimated_time = 0;
        if (total_time > 0 && estimated_time > total_time) estimated_time = total_time;
        current_time = estimated_time;
    }
}

// --- Hàm đặt âm lượng ---
void set_volume(int vol) {
    volume = vol < 0 ? 0 : (vol > 100 ? 100 : vol);
    printf("Setting volume to %d\n", volume);
    if (mpg123_pid != -1 && mpg123_stdin) {
        fprintf(mpg123_stdin, "VOLUME %d\n", volume);
        fflush(mpg123_stdin);
    }
}

// --- Hàm vẽ chữ ---
void draw_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color) {
    if (!text || !font || !renderer || strlen(text) == 0) return;
    SDL_Surface* surface = TTF_RenderUTF8_Solid(font, text, color);
    if (!surface) {
        fprintf(stderr, "TTF_RenderUTF8_Solid Error: %s (text: %s)\n", TTF_GetError(), text);
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTextureFromSurface Error: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dst = {x, y, surface->w, surface->h};

    int max_width = WINDOW_WIDTH - x - 10;
    if (dst.w > max_width) {
        dst.w = max_width;
    }
    if (dst.y < 0) dst.y = 0;
    if (dst.y + dst.h > WINDOW_HEIGHT) dst.h = WINDOW_HEIGHT - dst.y;

    if (dst.w > 0 && dst.h > 0) {
       SDL_RenderCopy(renderer, texture, NULL, &dst);
    }

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

// --- Luồng theo dõi thay đổi USB ---
void* watch_usb_changes(void* arg) {
     char* path = (char*)arg;
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) { perror("inotify_init1 failed"); free(path); return NULL; }

    int wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_ISDIR);
    if (wd == -1) {
        fprintf(stderr, "inotify_add_watch failed for %s: %s\n", path, strerror(errno));
        close(fd); free(path); return NULL;
    }

    printf("USB Watchdog started for path: %s\n", path);
    usb_thread_running = true;
    char buffer[sizeof(struct inotify_event) + NAME_MAX + 1] __attribute__ ((aligned(__alignof__(struct inotify_event))));

    while (usb_thread_running) {
        int length = read(fd, buffer, sizeof(buffer));
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(500000);
                continue;
            } else {
                perror("read inotify failed"); break;
            }
        }

        int i = 0;
        bool change_detected = false;
        while (i < length && usb_thread_running) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->mask & IN_ISDIR) {
                 // printf("inotify event: mask=0x%X, name=%s\n", event->mask, event->len ? event->name : "[no name]"); // DEBUG
                if ((event->mask & IN_CREATE) || (event->mask & IN_DELETE) || (event->mask & IN_MOVED_TO) || (event->mask & IN_MOVED_FROM)) {
                    if (event->len > 0 && event->name[0] != '.') {
                        printf("Potential USB mount/unmount detected: %s\n", event->name);
                        change_detected = true;
                    }
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }

        if (change_detected) {
             printf("USB change confirmed. Triggering rescan in 2 seconds...\n");
             sleep(2);
             find_usb_and_create_playlist(false);
        }
    }

    printf("USB Watchdog stopping...\n");
    inotify_rm_watch(fd, wd);
    close(fd);
    free(path);
    printf("USB Watchdog stopped.\n");
    return NULL;
}

// --- Hàm dọn dẹp tài nguyên ---
void cleanup() {
    printf("Cleaning up...\n");
    if (usb_thread_running) {
        usb_thread_running = false;
        printf("Waiting for USB watchdog thread to join...\n");
        int join_result = pthread_join(usb_thread, NULL);
        if(join_result != 0) fprintf(stderr, "Warning: pthread_join (USB) failed with code %d\n", join_result);
        else printf("USB watchdog thread joined.\n");
    }
    stop_music(); // Đã bao gồm join luồng monitor
    printf("Freeing playlist memory...\n");
    pthread_mutex_lock(&playlist_mutex);
    for (int i = 0; i < num_songs; i++) {
        free_song_info(&playlist[i]);
    }
    num_songs = 0;
    pthread_mutex_unlock(&playlist_mutex);
    pthread_mutex_destroy(&playlist_mutex);
    printf("Playlist memory freed and mutex destroyed.\n");
    // taglib_tag_free_strings(); // Không cần nếu management=false
    TTF_Quit();
    SDL_Quit();
    printf("SDL and TTF quit.\nCleanup complete.\n");
}

// --- Hàm main ---
// *** SỬA SIGNATURE Ở ĐÂY ***
int main(void) {
    // --- Khởi tạo ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init Error: %s\n", TTF_GetError());
        SDL_Quit(); return 1;
    }

    // --- Playlist ban đầu ---
    find_usb_and_create_playlist(true);
    update_search_filter();

    // --- Cửa sổ & Renderer ---
    SDL_Window* window = SDL_CreateWindow("Simple MP3 Player with Metadata", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        cleanup(); return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); cleanup(); return 1;
    }

    // --- Font ---
    TTF_Font* font = TTF_OpenFont(FONT_PATH, 16);
    TTF_Font* small_font = NULL;
    if (!font) {
        fprintf(stderr, "TTF_OpenFont Error (%s): %s\n", FONT_PATH, TTF_GetError());
        font = TTF_OpenFont(FALLBACK_FONT_PATH, 16);
         if(!font) {
            fprintf(stderr, "TTF_OpenFont Error (Fallback %s): %s\n", FALLBACK_FONT_PATH, TTF_GetError());
            SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); cleanup(); return 1;
         }
         printf("Using fallback font: %s\n", FALLBACK_FONT_PATH);
    } else { printf("Using font: %s\n", FONT_PATH); }

    small_font = TTF_OpenFont(FONT_PATH, 12);
    if (!small_font) small_font = TTF_OpenFont(FALLBACK_FONT_PATH, 12);
    if (!small_font) small_font = font; // Fallback

    // --- Luồng USB ---
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
        char* usb_watch_path_dynamic = malloc(512);
        if (usb_watch_path_dynamic) {
             snprintf(usb_watch_path_dynamic, 512, USB_MONITOR_DIR_FORMAT, pw->pw_name);
             // Kiểm tra xem thư mục có tồn tại không trước khi tạo luồng
             struct stat st_usb_dir;
             if (stat(usb_watch_path_dynamic, &st_usb_dir) == 0 && S_ISDIR(st_usb_dir.st_mode)) {
                 if (pthread_create(&usb_thread, NULL, watch_usb_changes, usb_watch_path_dynamic) != 0) {
                     perror("pthread_create for USB watcher failed"); free(usb_watch_path_dynamic);
                     // Không thoát chương trình, nhưng cảnh báo
                 } else {
                      // Thành công thì không free ở đây, luồng sẽ free
                 }
             } else {
                  fprintf(stderr, "Warning: USB monitor directory '%s' not found or not a directory. USB watching disabled.\n", usb_watch_path_dynamic);
                  free(usb_watch_path_dynamic); // Free nếu không dùng
             }
        } else { perror("Failed to allocate memory for USB watch path"); }
    } else { fprintf(stderr, "Could not get user info to start USB watcher.\n"); }


    // --- Vòng lặp chính ---
    bool running = true;
    SDL_Event e;
    // *** SỬA KHỞI TẠO MÀU ***
    SDL_Color white     = {255, 255, 255, 255};
    SDL_Color yellow    = {255, 255, 0,   255};
    SDL_Color green     = {0,   200, 0,   255};
    SDL_Color gray      = {100, 100, 100, 255};
    SDL_Color red       = {255, 0,   0,   255};
    SDL_Color blue      = {100, 150, 255, 255};
    SDL_Color search_bg = {50,  50,  80,  255};
    SDL_Color cyan      = {0,   255, 255, 255};

    Uint32 last_input_time = SDL_GetTicks();

    while (running) {
        // --- Xử lý sự kiện ---
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; }

            if (search_active) { // Ưu tiên input tìm kiếm
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            search_active = false;
                            SDL_StopTextInput();
                            search_query[0] = '\0';
                            update_search_filter();
                            break;
                        case SDLK_BACKSPACE:
                            if (strlen(search_query) > 0) {
                                // TODO: Xử lý UTF-8 backspace đúng cách nếu cần
                                search_query[strlen(search_query) - 1] = '\0';
                                update_search_filter();
                            }
                            break;
                        case SDLK_RETURN: case SDLK_KP_ENTER:
                            if (num_filtered_songs > 0 && current_filtered_index >= 0) {
                                play_music(filtered_indices[current_filtered_index]); // Chơi bài đang chọn trong kết quả
                                // Tùy chọn: tắt tìm kiếm sau khi Enter
                                // search_active = false;
                                // SDL_StopTextInput();
                                // update_search_filter();
                            }
                            break;
                        case SDLK_UP:
                             if (num_filtered_songs > 0) {
                                 current_filtered_index = (current_filtered_index - 1 + num_filtered_songs) % num_filtered_songs;
                             }
                            break;
                        case SDLK_DOWN:
                            if (num_filtered_songs > 0) {
                                current_filtered_index = (current_filtered_index + 1) % num_filtered_songs;
                            }
                            break;
                         case SDLK_SPACE: toggle_pause(); break; // Cho phép Play/Pause khi tìm
                         // Các phím điều khiển khác (A, D, S, Left, Right, +/-) bị bỏ qua khi đang tìm kiếm
                         default: break;
                    }
                } else if (e.type == SDL_TEXTINPUT) {
                    if (strlen(search_query) < SEARCH_QUERY_MAX) {
                        strcat(search_query, e.text.text);
                        update_search_filter();
                    }
                }
            }
            else { // Xử lý input thông thường
                 if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_SPACE: toggle_pause(); break;
                        case SDLK_d: // Next
                            pthread_mutex_lock(&playlist_mutex);
                            if (num_songs > 0) {
                                int next_idx = (current_index + 1) % num_songs;
                                pthread_mutex_unlock(&playlist_mutex); // Mở khóa trước khi gọi play_music
                                play_music(next_idx);
                            } else {
                                pthread_mutex_unlock(&playlist_mutex);
                            }
                            break;
                        case SDLK_a: // Previous
                            pthread_mutex_lock(&playlist_mutex);
                            if (num_songs > 0) {
                                int prev_idx = (current_index - 1 + num_songs) % num_songs;
                                pthread_mutex_unlock(&playlist_mutex); // Mở khóa trước khi gọi play_music
                                play_music(prev_idx);
                            } else {
                                pthread_mutex_unlock(&playlist_mutex);
                            }
                            break;
                        case SDLK_RIGHT: jump_seconds(5); break;
                        case SDLK_LEFT: jump_seconds(-5); break;
                        case SDLK_EQUALS: case SDLK_PLUS: set_volume(volume + 5); break;
                        case SDLK_MINUS: set_volume(volume - 5); break;
                        case SDLK_s: stop_music(); break;
                        case SDLK_UP: // Select previous
                             pthread_mutex_lock(&playlist_mutex);
                             if (num_songs > 0) {
                                current_index = (current_index - 1 + num_songs) % num_songs;
                                if (search_active) current_filtered_index = current_index; // Đồng bộ nếu đang search (dù không nên ở đây)
                             }
                             pthread_mutex_unlock(&playlist_mutex);
                             break;
                        case SDLK_DOWN: // Select next
                             pthread_mutex_lock(&playlist_mutex);
                             if (num_songs > 0) {
                                current_index = (current_index + 1) % num_songs;
                                 if (search_active) current_filtered_index = current_index; // Đồng bộ nếu đang search (dù không nên ở đây)
                             }
                             pthread_mutex_unlock(&playlist_mutex);
                             break;
                        case SDLK_RETURN: case SDLK_KP_ENTER: // Play selected
                            if(current_index != -1) play_music(current_index);
                            break;
                        case SDLK_f: // Activate search
                            search_active = true;
                            SDL_StartTextInput();
                            search_query[0] = '\0'; // Xóa query cũ
                            update_search_filter(); // Cập nhật bộ lọc
                            break;
                    }
                 }
            } // end if (!search_active)
        } // end while SDL_PollEvent

        // --- Xử lý chuyển bài tự động ---
        if (song_finished_flag) {
            song_finished_flag = false;
            int next_index_to_play = -1;
            pthread_mutex_lock(&playlist_mutex);
            if (num_songs > 0 && current_index != -1) {
                 // TODO: Quyết định logic chuyển bài khi đang tìm kiếm?
                 // Hiện tại luôn chuyển bài trong playlist gốc.
                 next_index_to_play = (current_index + 1) % num_songs;
            }
            int current_num_songs_check = num_songs; // Kiểm tra trong lock
            pthread_mutex_unlock(&playlist_mutex);

            if (next_index_to_play != -1 && current_num_songs_check > 0) {
                 printf("Playing next song automatically (index: %d).\n", next_index_to_play);
                 play_music(next_index_to_play);
            } else {
                 printf("Song finished, but no next song or playlist empty. Stopping.\n");
                 stop_music();
            }
        }

        // --- Vẽ ---
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        // --- Vẽ Ô Tìm kiếm ---
        int search_bar_y = 10;
        int search_bar_height = 25;
        if (search_active) {
            SDL_Rect search_rect = {20, search_bar_y, WINDOW_WIDTH - 40, search_bar_height};
            SDL_SetRenderDrawColor(renderer, search_bg.r, search_bg.g, search_bg.b, 255);
            SDL_RenderFillRect(renderer, &search_rect);
            char search_display[SEARCH_QUERY_MAX + 20];
            snprintf(search_display, sizeof(search_display), "Search: %s", search_query);
            draw_text(renderer, font, search_display, 25, search_bar_y + 5, white);

            Uint32 current_ticks = SDL_GetTicks();
            if ((current_ticks - last_input_time) % 1000 < 500) {
                 int text_w, text_h;
                 // Cần kiểm tra lỗi TTF_SizeUTF8
                 if (TTF_SizeUTF8(font, search_display, &text_w, &text_h) == 0) {
                     SDL_Rect cursor_rect = {25 + text_w + 1, search_bar_y + 5, 2, text_h};
                     SDL_SetRenderDrawColor(renderer, white.r, white.g, white.b, 255);
                     SDL_RenderFillRect(renderer, &cursor_rect);
                 } else {
                     fprintf(stderr,"TTF_SizeUTF8 error: %s\n", TTF_GetError());
                 }
            }
        } else {
             draw_text(renderer, font, "[F] to Search", 20, search_bar_y + 5, gray);
        }
        last_input_time = SDL_GetTicks();

        // --- Vẽ Playlist hoặc Kết quả Tìm kiếm ---
        pthread_mutex_lock(&playlist_mutex);
        int list_y_start = search_bar_y + search_bar_height + 10;
        int item_height = TTF_FontHeight(font) + 4; // Chiều cao dựa trên font + padding
        int metadata_y_offset = item_height; // Vị trí metadata dưới status
        int status_y = WINDOW_HEIGHT - 3*item_height - 10; // Tính toán y cho status
        int max_display_songs = (status_y - list_y_start) / item_height;
        if (max_display_songs < 1) max_display_songs = 1; // Đảm bảo ít nhất 1

        int count_to_display = search_active ? num_filtered_songs : num_songs;
        // Chỉ số đang được chọn trong view hiện tại (lọc hoặc đầy đủ)
        int current_selection_in_view = -1;
        if (search_active) {
            current_selection_in_view = current_filtered_index; // Chỉ số trong mảng lọc
        } else {
             current_selection_in_view = current_index; // Chỉ số trong playlist gốc
        }

        int start_display_index = 0; // Index bắt đầu vẽ trong danh sách (lọc hoặc đầy đủ)
        if (count_to_display > max_display_songs && current_selection_in_view >= 0) {
            start_display_index = current_selection_in_view - max_display_songs / 2;
            if (start_display_index < 0) start_display_index = 0;
            // Đảm bảo không cuộn quá cuối danh sách
            if (start_display_index > count_to_display - max_display_songs) {
                start_display_index = count_to_display - max_display_songs;
            }
             if (start_display_index < 0) start_display_index = 0; // Kiểm tra lại phòng trường hợp count_to_display < max_display_songs/2
        }

        for (int i = 0; i < max_display_songs; ++i) {
            int view_index = start_display_index + i;
            if (view_index >= count_to_display) break;

            int playlist_idx = search_active ? filtered_indices[view_index] : view_index;
            // Kiểm tra playlist_idx hợp lệ trước khi truy cập
            if (playlist_idx < 0 || playlist_idx >= num_songs) continue;

            SongInfo* song = &playlist[playlist_idx];

            if (song && song->display_name) {
                char buf[1024];
                char prefix = ' ';
                bool is_selected_in_view = (view_index == current_selection_in_view);
                bool is_currently_playing = (playlist_idx == current_index && (is_playing || is_paused));

                SDL_Color text_color = white;
                if (is_selected_in_view) {
                    prefix = '>';
                    text_color = yellow;
                }
                if (is_currently_playing) {
                     text_color = is_paused ? blue : green;
                     if(is_selected_in_view) prefix = '#';
                     else prefix = '*';
                }

                char metadata_info[200] = "";
                if (song->artist) {
                    snprintf(metadata_info, sizeof(metadata_info), " - %s", song->artist);
                }

                char display_limited[301];
                snprintf(display_limited, sizeof(display_limited), "%.100s%s", song->display_name, metadata_info);

                snprintf(buf, sizeof(buf), "%c %d: %s", prefix, playlist_idx + 1, display_limited);
                draw_text(renderer, font, buf, 40, list_y_start + i * item_height, text_color);
            }
        }

        // --- Vẽ Trạng thái và Metadata chi tiết ---
        // status_y đã được tính toán ở trên
        char status_buf[1100] = {0};
        char metadata_buf[1100] = {0};
        SDL_Color status_color = yellow;
        int current_playing_or_selected_idx = current_index; // Luôn hiển thị metadata của bài đang được chọn trong playlist gốc

        if (current_playing_or_selected_idx != -1 && current_playing_or_selected_idx < num_songs) {
            SongInfo* song = &playlist[current_playing_or_selected_idx];
            const char* status_str = "Selected";
             if (is_playing) {
                 status_str = is_paused ? "Paused" : "Playing";
                 status_color = is_paused ? blue : green;
             } else if (mpg123_pid != -1){
                 status_str = "Stopped"; status_color = red;
             } else {
                 status_str = "Selected"; status_color = yellow;
             }

            snprintf(status_buf, sizeof(status_buf), "Status: %s [%d/%d] | %s",
                     status_str, current_playing_or_selected_idx + 1, num_songs,
                     song->display_name ? song->display_name : "Unknown Title");
            draw_text(renderer, font, status_buf, 20, status_y, status_color);

            char year_str[16] = ""; 
            if (song->year > 0) snprintf(year_str, sizeof(year_str)," (%d)", song->year);
            snprintf(metadata_buf, sizeof(metadata_buf), "Artist: %s | Album: %s%s | Genre: %s",
                     song->artist ? song->artist : "N/A",
                     song->album ? song->album : "N/A",
                     year_str,
                     song->genre ? song->genre : "N/A");
            draw_text(renderer, small_font, metadata_buf, 20, status_y + metadata_y_offset, gray);

        } else if (num_songs == 0) {
             snprintf(status_buf, sizeof(status_buf), "No songs found. Connect USB or check scan path."); status_color = red;
             draw_text(renderer, font, status_buf, 20, status_y, status_color);
        } else {
             snprintf(status_buf, sizeof(status_buf), "Select a song (UP/DOWN) + ENTER/SPACE to play."); status_color = yellow;
             draw_text(renderer, font, status_buf, 20, status_y, status_color);
        }
        pthread_mutex_unlock(&playlist_mutex);

        // --- Vẽ Thanh Tiến trình & Thời gian ---
        int progress_bar_y = status_y + 2 * metadata_y_offset + 5; // Dưới metadata
        int time_text_y = progress_bar_y - item_height; // Trên thanh progress
        int bar_x = 20;
        int bar_width = WINDOW_WIDTH - 40 - 100;
        SDL_Rect bg_rect = {bar_x, progress_bar_y, bar_width, 15};

        SDL_SetRenderDrawColor(renderer, gray.r, gray.g, gray.b, 255);
        SDL_RenderFillRect(renderer, &bg_rect);

        if ((is_playing || is_paused) && total_time > 0) {
            float progress = (float)current_time / total_time;
            progress = fmaxf(0.0f, fminf(1.0f, progress)); // Clamp 0-1
            int filled_width = (int)(bar_width * progress);
            SDL_Rect fg_rect = {bar_x, progress_bar_y, filled_width, 15};
            SDL_SetRenderDrawColor(renderer, green.r, green.g, green.b, 255);
            SDL_RenderFillRect(renderer, &fg_rect);

            char time_info[64];
            snprintf(time_info, sizeof(time_info), "%02d:%02d / %02d:%02d",
                     current_time / 60, current_time % 60,
                     total_time / 60, total_time % 60);
            // Căn giữa text thời gian
            int time_w, time_h;
             if (TTF_SizeUTF8(font, time_info, &time_w, &time_h) == 0) {
                  draw_text(renderer, font, time_info, bar_x + (bar_width - time_w) / 2, time_text_y, white);
             } else { // Fallback vẽ lệch trái nếu lỗi size
                  draw_text(renderer, font, time_info, bar_x , time_text_y, white);
             }
        } else if (is_playing) {
             draw_text(renderer, font, "Loading...", bar_x + bar_width / 2 - 30, time_text_y, white);
        } else {
             draw_text(renderer, font, "00:00 / 00:00", bar_x + bar_width / 2 - 50, time_text_y, gray);
        }

        // Vẽ Âm lượng
        char vol_info[20];
        snprintf(vol_info, sizeof(vol_info), "Vol: %d%%", volume);
        draw_text(renderer, font, vol_info, bar_x + bar_width + 10, progress_bar_y, white);

        // Vẽ Hướng dẫn
        const char* help_text = "[SPACE]Play/Pause [A/D]Prev/Next [S]Stop [←/→]Seek [+/-]Vol [↑/↓]Select [ENTER]Play [F]Search [ESC]Exit";
        draw_text(renderer, font, help_text, 20, WINDOW_HEIGHT - item_height - 5, cyan);

        // --- Hiển thị ---
        SDL_RenderPresent(renderer);

        SDL_Delay(16); // ~60 FPS
    }

    // --- Dọn dẹp ---
    cleanup();
    if (font) TTF_CloseFont(font);
    if (small_font && small_font != font) TTF_CloseFont(small_font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    printf("Application finished normally.\n");
    return 0;
}