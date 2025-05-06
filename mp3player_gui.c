#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // Cần cho access()
#include <signal.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <taglib/tag_c.h>
#include <fcntl.h> // *** THÊM HEADER NÀY ***
#include <sys/types.h> // Thường cần thiết với fcntl.h và unistd.h

// --- Định nghĩa Hằng số ---
// (Giữ nguyên các hằng số khác)
#define WINDOW_WIDTH 1300
#define WINDOW_HEIGHT 600
#define MAX_SONGS 500
#define USB_MONITOR_DIR_FORMAT "/media/%s"
#define FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define FALLBACK_FONT_PATH "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
#define SEARCH_QUERY_MAX 255

// --- GUI Constants ---
#define PADDING 5
#define BUTTON_HEIGHT 45
#define BUTTON_WIDTH 45
#define PROGRESS_BAR_HEIGHT 15
#define LIST_ITEM_HEIGHT 20
#define VOLUME_SLIDER_WIDTH 150 // Chiều rộng thanh trượt âm lượng
#define VOLUME_SLIDER_HEIGHT 10 // Chiều cao thanh trượt âm lượng
#define VOLUME_HANDLE_WIDTH 10 // Chiều rộng núm âm lượng
#define VOLUME_HANDLE_HEIGHT 18 // Chiều cao núm âm lượng

// --- Icon Paths (*** Bỏ ICON_PATH_VOL_UP/DOWN ***) ---
#define ICON_DIR "icons/"
#define ICON_PATH_PLAY       ICON_DIR "play.png"
#define ICON_PATH_PAUSE      ICON_DIR "pause.png"
#define ICON_PATH_NEXT       ICON_DIR "next.png"
#define ICON_PATH_PREV       ICON_DIR "prev.png"
#define ICON_PATH_REWIND     ICON_DIR "seek_bwd.png"  // <<< THÊM DÒNG NÀY
#define ICON_PATH_FORWARD    ICON_DIR "seek_fwd.png" // <<< THÊM DÒNG NÀY
#define ICON_PATH_SEARCH     ICON_DIR "search.png"
#define ICON_PATH_SEARCH_ACTIVE ICON_DIR "search_active.png"
#define ICON_PATH_VOLUME     ICON_DIR "volume.png" // Icon mới cho volume slider

// --- Cấu trúc SongInfo (Không đổi) ---
typedef struct {
    char *filepath; 
    char *title; 
    char *artist; 
    char *album;
    int year; 
    char *genre; 
    char *display_name; 
    char *search_string;
    bool metadata_loaded;
} SongInfo;

// --- Biến Toàn cục (Bỏ tex_vol_up/down, thêm volume state) ---
SongInfo playlist[MAX_SONGS];
int num_songs = 0;
int current_index = -1;
int volume = 100; // Âm lượng hiện tại (0-100)
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

bool search_active = false;
char search_query[SEARCH_QUERY_MAX + 1] = {0};
int filtered_indices[MAX_SONGS];
int num_filtered_songs = 0;
int current_filtered_index = -1;

Uint32 last_playlist_click_time = 0; // Thời gian của lần click cuối cùng vào playlist
int last_playlist_click_index = -1; // Index của bài hát được click lần cuối
#define DOUBLE_CLICK_INTERVAL 500 // Ngưỡng thời gian double-click (ms)

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
TTF_Font* font = NULL;
TTF_Font* small_font = NULL;
SDL_Color white     = {255, 255, 255, 255};
SDL_Color yellow    = {255, 255, 0,   255};
SDL_Color green     = {0,   200, 0,   255};
SDL_Color gray      = {150, 150, 150, 255};
SDL_Color dark_gray = {100, 100, 100, 255};
SDL_Color darker_gray = {50, 50, 50, 255};
SDL_Color red       = {255, 0,   0,   255};
SDL_Color blue      = {100, 150, 255, 255};
SDL_Color search_bg = {50,  50,  80,  255};
SDL_Color cyan      = {0,   255, 255, 255};
SDL_Color button_bg_color = {60, 60, 70, 255};
SDL_Color slider_handle_color = {200, 200, 220, 255}; // Màu núm trượt
SDL_Color slider_handle_drag_color = {255, 255, 255, 255}; // Màu núm trượt khi kéo
// Ví dụ: Một màu xanh dương đậm
SDL_Color left_background_color  = {20, 30, 55, 255}; // Màu xanh dương đậm cho bên trái
SDL_Color right_background_color = {45, 55, 75, 255}; // Màu xám xanh nhạt hơn cho bên phải
// --- Icon Textures (Bỏ tex_vol_up/down, thêm tex_volume) ---
SDL_Texture* tex_play = NULL;
SDL_Texture* tex_pause = NULL;
SDL_Texture* tex_rewind = NULL;  
SDL_Texture* tex_forward = NULL;
SDL_Texture* tex_next = NULL;
SDL_Texture* tex_prev = NULL;
SDL_Texture* tex_volume = NULL; // Icon tĩnh cho volume
SDL_Texture* tex_search = NULL;
SDL_Texture* tex_search_active = NULL;
SDL_Texture* tex_default_art = NULL;
SDL_Texture* tex_left_background = NULL; // <<< THÊM BIẾN NÀY

// --- GUI Rects (Bỏ vol_up/down button, thêm volume slider) ---
SDL_Rect search_bar_rect;
SDL_Rect playlist_area_rect;
SDL_Rect info_area_rect;
SDL_Rect control_area_rect;
SDL_Rect progress_bar_bg_rect;
SDL_Rect progress_bar_fg_rect;
SDL_Rect time_text_rect;
SDL_Rect volume_text_rect; // Vẫn giữ để hiển thị %
SDL_Rect prev_button_rect;
SDL_Rect play_pause_button_rect;
SDL_Rect rewind_button_rect;   // <<< THÊM DÒNG NÀY
SDL_Rect forward_button_rect;  // <<< THÊM DÒNG NÀY
SDL_Rect next_button_rect;
SDL_Rect search_activate_button_rect;
SDL_Rect volume_slider_bg_rect;  // Nền thanh trượt volume
SDL_Rect volume_handle_rect;   // Núm kéo volume
SDL_Rect volume_icon_rect;     // Rect cho icon volume tĩnh
SDL_Rect album_art_rect; 

int list_scroll_offset = 0;
int visible_list_items = 0;
bool is_dragging_volume = false; // Trạng thái kéo volume

typedef enum { BTN_NORMAL, BTN_HOVER, BTN_PRESSED } ButtonState;
ButtonState prev_btn_state = BTN_NORMAL;
ButtonState play_pause_btn_state = BTN_NORMAL;
ButtonState next_btn_state = BTN_NORMAL;
ButtonState search_activate_btn_state = BTN_NORMAL;
ButtonState rewind_btn_state = BTN_NORMAL;   // <<< THÊM DÒNG NÀY
ButtonState forward_btn_state = BTN_NORMAL;  // <<< THÊM DÒNG NÀY

// --- Khai báo hàm (Bỏ vol +/- button handler, thêm volume slider logic) ---
void play_music(int playlist_idx);
void stop_music();
void cleanup();
void find_usb_and_create_playlist(bool initial_scan);
void load_song_metadata(const char *filepath, SongInfo *song);
void free_song_info(SongInfo *song);
void update_search_filter();
void draw_text_simple(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color);
void draw_text_clipped(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color, SDL_Rect* clip_rect);
void draw_icon_button(SDL_Renderer* renderer, SDL_Rect* rect, SDL_Texture* icon_tex, ButtonState state);
void* monitor_mpg123_output(void* arg);
void* watch_usb_changes(void* arg);
void toggle_pause();
void jump_seconds(int seconds);
void set_volume(int vol); // Hàm này vẫn giữ nguyên để set volume cuối cùng
bool init_sdl_and_gui();
bool load_icons(SDL_Renderer* renderer);
void calculate_layout();
void handle_mouse_click(int x, int y);
void handle_mouse_motion(int x, int y, bool mouse_button_down); // Thêm trạng thái nút chuột
void handle_mouse_up(int x, int y); // Xử lý nhả chuột
void handle_mouse_wheel(int y_delta);
void update_volume_from_mouse(int mouse_x); // Hàm tính volume từ vị trí chuột

// --- Các hàm cốt lõi (giữ nguyên từ phiên bản trước) ---
// ... (Dán code của các hàm này vào đây: has_mp3_extension, free_song_info, load_song_metadata, search_and_save_mp3_recursive, find_usb_and_create_playlist, update_search_filter, monitor_mpg123_output, stop_music, play_music, toggle_pause, jump_seconds, set_volume, watch_usb_changes) ...
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
    song->filepath = song->title = song->artist = song->album = song->genre = song->display_name = song->search_string = NULL;
    song->metadata_loaded = false; song->year = 0;
}

// --- Hàm đọc metadata từ file MP3 ---
void load_song_metadata(const char *filepath, SongInfo *song) {
    song->filepath = strdup(filepath); // Sao chép đường dẫn file vào cấu trúc SongInfo

    //Khởi tạo các trường metadata về giá trị mặc định (NULL hoặc 0)
    song->title = NULL; song->artist = NULL; song->album = NULL; song->year = 0;
    song->genre = NULL; song->display_name = NULL; song->search_string = NULL;

    //Đánh dấu metadata chưa được tải
    song->metadata_loaded = false;

    //Kiểm tra lỗi cấp phát bộ nhớ cho đường dẫn
    if (!song->filepath) { 
        perror("strdup filepath failed"); //In thông báo lỗi hệ thống nếu strdup thất bại
        //Hàm strdup() trong C được dùng để tạo một bản sao của một chuỗi (string) — tức là sao chép nội dung chuỗi gốc vào một vùng nhớ mới được cấp phát trên heap.
        /*
        Tham số: str là chuỗi gốc cần sao chép.
        Trả về: con trỏ đến chuỗi mới được sao chép (trên heap), hoặc NULL nếu cấp phát bộ nhớ thất bại.
        Lưu ý: strdup() ngầm dùng malloc() để cấp phát bộ nhớ.
        */
        return; 
        // Giải thích: Nếu `strdup` trả về NULL (do hết bộ nhớ), không thể tiếp tục xử lý, nên in lỗi và thoát.
    }

    //Tìm vị trí dấu '/' cuối cùng trong đường dẫn
    const char* filename_only = strrchr(filepath, '/'); 
    filename_only = filename_only ? filename_only + 1 : filepath;
     // Giải thích: 
    // - Toán tử tam phân `?:`: Kiểm tra `filename_only` có phải là NULL không.
    // - Nếu `filename_only` không NULL (tìm thấy '/'): `filename_only + 1` sẽ là con trỏ tới ký tự ngay sau dấu '/', tức là bắt đầu của tên file.
    // - Nếu `filename_only` là NULL (không có '/'): Tên file chính là toàn bộ `filepath`.
    // - Kết quả: `filename_only` bây giờ trỏ đến phần tên file của đường dẫn.


    song->display_name = strdup(filename_only); //Tạo một bản sao của tên file và gán cho `display_name`. Đây sẽ là tên hiển thị nếu không đọc được 'Title' từ tag.
    if (!song->display_name) { 
        perror("strdup display_name failed"); 
    }
    taglib_set_strings_unicode(true); //Cấu hình TagLib để xử lý chuỗi Unicode (UTF-8)
    taglib_set_string_management_enabled(false); //Tắt quản lý bộ nhớ chuỗi tự động của TagLib
                                                // Bảo TagLib không tự động giải phóng bộ nhớ của các chuỗi nó trả về. Chương trình sẽ tự chịu trách nhiệm giải phóng bằng `taglib_free` sau khi đã `strdup`.
    
    TagLib_File *file = taglib_file_new(filepath); // mở file tại `filepath` để đọc tag. Trả về con trỏ `TagLib_File*` nếu thành công, NULL nếu thất bại.
    if (file) {
        TagLib_Tag *tag = taglib_file_tag(file);
        if (tag) {
            //Đọc các trường metadata từ tag
            char *title = taglib_tag_title(tag); char *artist = taglib_tag_artist(tag); char *album = taglib_tag_album(tag);
            unsigned int year = taglib_tag_year(tag); char *genre = taglib_tag_genre(tag);
            if (title && strlen(title) > 0) song->title = strdup(title);
            if (artist && strlen(artist) > 0) song->artist = strdup(artist);
            if (album && strlen(album) > 0) {
                song->album = strdup(album); 
            }
            song->year = year;
            if (genre && strlen(genre) > 0) song->genre = strdup(genre);
            if (song->title) { 
                free(song->display_name); 
                song->display_name = strdup(song->title); }
            taglib_free(title); taglib_free(artist); taglib_free(album); taglib_free(genre);
            song->metadata_loaded = true;
        } taglib_file_free(file);
    }
    size_t needed = 1; if (song->display_name) needed += strlen(song->display_name); if (song->artist) needed += strlen(song->artist);
    if (song->album) {
        needed += strlen(song->album); 
    }
    if (song->genre) { 
        needed += strlen(song->genre); 
    }
    needed += 10;

    //Cấp phát bộ nhớ cho chuỗi tìm kiếm
    song->search_string = malloc(needed);
    if (song->search_string) {
        char year_str[16] = ""; if (song->year > 0) snprintf(year_str, sizeof(year_str), "%d", song->year);
        snprintf(song->search_string, needed, "%s %s %s %s %s", song->display_name ? song->display_name : "", song->artist ? song->artist : "",
                 song->album ? song->album : "", year_str, song->genre ? song->genre : "");
        for (int i = 0; song->search_string[i]; i++) song->search_string[i] = tolower(song->search_string[i]);
    } else { perror("malloc for search_string failed"); }
}

// --- Hàm đệ quy tìm, lưu file MP3 và đọc metadata ---
void search_and_save_mp3_recursive(const char* path, SongInfo* current_playlist, int* count, int max_songs) {
    DIR* dir = opendir(path); //`opendir` trả về một con trỏ DIR* nếu thành công, NULL nếu thất bại (vd: thư mục không tồn tại, không có quyền).
    if (!dir) return; 
    struct dirent* entry; //`struct dirent` chứa thông tin như tên của file/thư mục con (`d_name`).
    char fullPath[2048]; //Dùng để ghép `path` với tên mục con để tạo đường dẫn hoàn chỉnh. Kích thước 2048 là khá lớn để chứa các đường dẫn dài.
    while (*count < max_songs && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; // Bỏ qua các thư mục đặc biệt "." (hiện tại) và ".." (cha).
        int len = snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name); //`snprintf` định dạng chuỗi an toàn, ghép `path`, dấu '/', và tên mục con (`entry->d_name`) vào `fullPath`. Nó trả về số ký tự (không tính null) lẽ ra đã ghi.
        if (len < 0 || (size_t)len >= sizeof(fullPath)) {
            continue; 
        }
        struct stat st; // Khai báo cấu trúc để lưu thông tin trạng thái của mục
        if (lstat(fullPath, &st) == 0) { // `lstat` lấy thông tin của file/thư mục/symlink. Trả về 0 nếu thành công.
                                        // Khác với `stat`, `lstat` không đi theo symbolic link.
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) { // `S_ISDIR` kiểm tra cờ chế độ (mode) xem có phải thư mục. `!S_ISLNK` đảm bảo không phải symlink để tránh đệ quy vô hạn.
                search_and_save_mp3_recursive(fullPath, current_playlist, count, max_songs); // Nếu là thư mục hợp lệ, gọi đệ quy chính hàm này.
            }
            else if (S_ISREG(st.st_mode) && has_mp3_extension(entry->d_name)) { // `S_ISREG` kiểm tra xem có phải file thông thường. `has_mp3_extension` (hàm bạn tự định nghĩa) kiểm tra đuôi file.
                load_song_metadata(fullPath, &current_playlist[*count]); (*count)++; //Nếu là file MP3, đọc metadata và thêm vào playlist tạm thời.
            }
        }
    } closedir(dir);
}

// --- Hàm tìm USB và tạo playlist ---
void find_usb_and_create_playlist(bool initial_scan) {
    struct passwd* pw = getpwuid(getuid()); 
    // `getuid()`: Lấy User ID của người chạy chương trình.
    // `getpwuid()`: Tìm thông tin người dùng (tên, thư mục nhà...) dựa trên User ID.

    //Kiểm tra lỗi lấy thông tin người dùng.
    if (!pw) { 
        fprintf(stderr, "Error getting user info.\n"); 
        return; 
    }

    const char* username = pw->pw_name; //Lấy tên người dùng.
    char basePath[512]; //Khai báo bộ đệm cho đường dẫn cơ sở.
    snprintf(basePath, sizeof(basePath), USB_MONITOR_DIR_FORMAT, username); //// `USB_MONITOR_DIR_FORMAT` (thường là "/media/%s") được định dạng với `username`. Kết quả lưu vào `basePath`.
    printf("Scanning for MP3 files in %s and subdirectories...\n", basePath); 
    
    SongInfo* temp_playlist = calloc(MAX_SONGS, sizeof(SongInfo)); //Cấp phát bộ nhớ cho playlist tạm thời.
    // `calloc`: Cấp phát bộ nhớ cho mảng chứa `MAX_SONGS` phần tử `SongInfo` và khởi tạo tất cả các byte về 0 (quan trọng để con trỏ là NULL).

    if (!temp_playlist) { 
        perror("Failed to allocate memory for temporary playlist scan"); 
        return; 
    } 

    int temp_num_songs = 0; //Cập nhật số lượng bài hát toàn cục.
    DIR* dir = opendir(basePath);
    if (dir) { 
        struct dirent* entry; 
        while (temp_num_songs < MAX_SONGS && (entry = readdir(dir)) != NULL) { 
            if (entry->d_name[0] == '.') continue;
            char potential_usb_path[1024]; 
            snprintf(potential_usb_path, 
                sizeof(potential_usb_path), "%s/%s", basePath, entry->d_name); 
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

     // --- Bắt đầu phần cập nhật playlist toàn cục (Cần bảo vệ bằng Mutex) ---
    // Khóa Mutex để đảm bảo an toàn luồng.
    pthread_mutex_lock(&playlist_mutex); 
    for (int i = 0; i < num_songs; i++) free_song_info(&playlist[i]);     // `free_song_info` giải phóng các chuỗi (filepath, title...) bên trong mỗi SongInfo.
    num_songs = 0;  // Reset số lượng bài hát toàn cục về 0.
    
    //Sao chép từ playlist tạm thời sang playlist toàn cục.
    for(int i = 0; i < temp_num_songs; i++) {
        playlist[i] = temp_playlist[i]; // Sao chép cấu trúc (bao gồm cả con trỏ).
    }
    num_songs = temp_num_songs; //Cập nhật số lượng bài hát toàn cục
    current_index = (num_songs > 0) ? 0 : -1; 
    list_scroll_offset = 0; //Reset vị trí cuộn playlist.


    // Kiểm tra xem có phải là lần quét lại không.
    if (!initial_scan) { 
        //Dừng nhạc đang phát.
        stop_music(); 
        //Reset trạng thái phát nhạc.
        is_playing = false; 
        is_paused = false; 
        current_time = 0; 
        total_time = 0; 
        //Reset trạng thái tìm kiếm.
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
    update_search_filter();
}

// --- Hàm cập nhật danh sách lọc dựa trên search_query ---
void update_search_filter() {
    // Khóa Mutex playlist_mutex.
    pthread_mutex_lock(&playlist_mutex); 
    
    num_filtered_songs = 0; //Reset số lượng bài hát trong danh sách đã lọc về 0
    current_filtered_index = -1; //reset chỉ mục của bài hát đang được chọn TRONG DANH SÁCH LỌC
    list_scroll_offset = 0; // Reset vị trí cuộn của playlist về đầu.
    
    // Kiểm tra xem có nên thực hiện lọc hay không.
    if (!search_active || strlen(search_query) == 0) {
    // Điều kiện này đúng nếu:
    // - `!search_active`: Chế độ tìm kiếm đang TẮT.
    // - HOẶC `strlen(search_query) == 0`: Chế độ tìm kiếm đang BẬT nhưng người dùng chưa nhập gì (hoặc đã xóa hết).
    // -> Trường hợp này nghĩa là cần hiển thị TOÀN BỘ playlist.

        num_filtered_songs = num_songs; // Danh sách lọc sẽ chứa tất cả bài hát.
        for(int i=0; i<num_songs; ++i) filtered_indices[i] = i;
        if (current_index >= 0 && current_index < num_songs) { 
            for(int i=0; i<num_filtered_songs; ++i) { 
                if(filtered_indices[i] == current_index) { 
                    current_filtered_index = i; 
                    break; 
                } 
            } 
        }
        else if (num_filtered_songs > 0) { 
            current_filtered_index = 0; 
        }
    } else { // --- Khối lệnh thực thi khi CẦN lọc (search_active là true VÀ search_query không rỗng) ---
        char lower_query[SEARCH_QUERY_MAX + 1]; //Khai báo bộ đệm để lưu bản sao chữ thường của từ khóa tìm kiếm.
        strncpy(lower_query, search_query, SEARCH_QUERY_MAX); //Sao chép an toàn từ khóa tìm kiếm gốc vào bộ đệm.
        lower_query[SEARCH_QUERY_MAX] = '\0'; //Đảm bảo chuỗi `lower_query` luôn kết thúc bằng null.
        for (int i = 0; lower_query[i]; i++) 
        lower_query[i] = tolower(lower_query[i]);
        for (int i = 0; i < num_songs; ++i) { 
            if (playlist[i].search_string && strstr(playlist[i].search_string, lower_query)) { 
                if (num_filtered_songs < MAX_SONGS) filtered_indices[num_filtered_songs++] = i; 
                else break;} 
            }
        if (num_filtered_songs > 0) {
            current_filtered_index = 0; 
        }
        //printf("Search for '%s': Found %d results.\n", search_query, num_filtered_songs);
        printf("Search for '%s': Found %d results.\n", search_query, num_filtered_songs); // Luôn in ra, kể cả khi không tìm thấy
    } 
    pthread_mutex_unlock(&playlist_mutex);
}

// --- Luồng theo dõi output của mpg123 ---
// Hàm này được thiết kế để chạy trong một luồng riêng biệt (pthread).
// Nó nhận vào một đối số 'arg', là con trỏ tới FILE* đại diện cho stdout của mpg123.
void* monitor_mpg123_output(void* arg) {
    FILE* mpg123_stdout = (FILE*)arg; // Ép kiểu đối số 'arg' thành con trỏ FILE* và gán cho biến cục bộ.
    char line[512]; //Khai báo bộ đệm để đọc từng dòng output.
    bool local_song_finished = false; //Khai báo cờ cục bộ để ghi nhận nếu bài hát kết thúc tự nhiên.
    pid_t monitored_pid = mpg123_pid; //Lưu lại PID của tiến trình mpg123 mà luồng này đang theo dõi.
    printf("Monitor thread started for PID %d\n", monitored_pid);
    monitor_thread_should_run = true; //Đặt cờ báo hiệu luồng này nên bắt đầu chạy.
                                      //// Biến toàn cục `volatile` này được dùng bởi hàm `stop_music` để yêu cầu luồng này dừng lại.

    while (monitor_thread_should_run && fgets(line, sizeof(line), mpg123_stdout)) {
    // Vòng lặp tiếp tục khi:
    // - `monitor_thread_should_run` là true (luồng chưa bị yêu cầu dừng).
    // - `fgets(line, sizeof(line), mpg123_stdout)` đọc thành công một dòng từ `mpg123_stdout` vào `line`. Nếu `fgets` trả về NULL (do hết dữ liệu, pipe đóng, hoặc lỗi), vòng lặp sẽ dừng.
        
        // Kiểm tra xem dòng có bắt đầu bằng "@F" không (Thông tin Frame/Thời gian).
        if (strncmp(line, "@F", 2) == 0) {
            //Khai báo các biến để lưu trữ dữ liệu từ dòng @F.
            int fcurr, fleft; // Số frame hiện tại, số frame còn lại
            double scurr, sleft; // Thời gian hiện tại (giây), thời gian còn lại (giây)
            if (sscanf(line, "@F %d %d %lf %lf", &fcurr, &fleft, &scurr, &sleft) == 4) { // `sscanf` đọc dữ liệu từ chuỗi `line` theo định dạng "%d %d %lf %lf" sau "@F ". Trả về số lượng mục đã đọc thành công (cần là 4).
                current_time = (int)round(scurr); //Cập nhật thời gian hiện tại (biến toàn cục)
                int new_total = (int)round(scurr + sleft); //Tính toán tổng thời gian ước tính.
                if (new_total > 0 && (total_time <= 0 || abs(new_total - total_time) > 2))
                    total_time = new_total;
            }
        }
        else if (strstr(line, "@P 0")) {
        // `strstr` tìm chuỗi con "@P 0" trong `line`. "@P 0" là tín hiệu mpg123 gửi khi dừng ở trạng thái cuối cùng (thường là hết bài).
            printf("Monitor thread (PID %d) detected @P 0 (song finished naturally)\n", monitored_pid);
            local_song_finished = true; //Đặt cờ cục bộ báo hiệu bài hát đã kết thúc.
            break;
        }
        else if (strstr(line, "@E ")) { // "@E " là tiền tố cho các thông báo lỗi từ mpg123.
            fprintf(stderr, "mpg123 error (PID: %d): %s", monitored_pid, line + 3);
        }

        //Kiểm tra xem PID toàn cục có thay đổi không.
        if (mpg123_pid != monitored_pid) {
            // So sánh PID toàn cục hiện tại (`mpg123_pid` có thể đã bị hàm `play_music` khác thay đổi) với PID mà luồng này đang theo dõi (`monitored_pid`).
            printf("Monitor thread (for PID %d) detects PID change to %d. Exiting.\n", monitored_pid, mpg123_pid);
            break;
        }
    }
    printf("Monitor thread (PID %d) loop finished. Should run flag: %d\n", monitored_pid, monitor_thread_should_run);
    fclose(mpg123_stdout);
    printf("Monitor thread (PID %d) closed stdout pipe.\n", monitored_pid);

    //Đặt cờ toàn cục báo hiệu bài hát kết thúc (nếu đúng).
    if (local_song_finished && monitor_thread_should_run)
        song_finished_flag = true;
    monitor_thread_should_run = false;
    printf("Monitor thread (PID %d) exiting.\n", monitored_pid);
    return NULL;
}

// --- Hàm dừng nhạc ---
void stop_music() {
    pid_t pid_to_stop = mpg123_pid;
    FILE* stdin_to_close = mpg123_stdin;
    bool was_monitor_running = monitor_thread_should_run;
    printf("Stopping music (Target PID: %d)...\n", pid_to_stop);
    if (monitor_thread_should_run) {
        printf("Signaling monitor thread to stop...\n");
        monitor_thread_should_run = false;
    }
    is_playing = false;
    is_paused = false; // Không còn đang tạm dừng.
    song_finished_flag = false; // Reset cờ báo hiệu bài hát kết thúc tự nhiên.
    mpg123_pid = -1; // Đánh dấu không còn quản lý tiến trình mpg123 nào.
    mpg123_stdin = NULL;
    current_time = 0;
    total_time = 0;
    if (stdin_to_close) {
        fprintf(stdin_to_close, "QUIT\n");
        fflush(stdin_to_close);
        fclose(stdin_to_close);
        printf("Sent QUIT and closed stdin pipe for PID %d.\n", pid_to_stop);
    } else {
        printf("No stdin pipe to close for PID %d.\n", pid_to_stop);
    }
    if (was_monitor_running && pid_to_stop > 0) {
        printf("Waiting for monitor thread (for PID %d) to join...\n", pid_to_stop);
        int join_result = pthread_join(monitor_thread, NULL);
        if (join_result != 0)
            fprintf(stderr, "Warning: pthread_join for monitor thread failed with code %d (errno %d: %s)\n", join_result, errno, strerror(errno));
        else
            printf("Monitor thread joined.\n");
    } else {
        printf("Monitor thread was not running or no PID, skipping join.\n");
    }
    if (pid_to_stop > 0) {
        printf("Checking status of PID %d...\n", pid_to_stop);
        int status;
        pid_t result = waitpid(pid_to_stop, &status, WNOHANG);
        if (result == 0) {
            printf("PID %d still running, sending SIGTERM...\n", pid_to_stop);
            kill(pid_to_stop, SIGTERM);
            usleep(100000);
            result = waitpid(pid_to_stop, &status, WNOHANG);
            if (result == 0) {
                printf("PID %d still running after SIGTERM, sending SIGKILL...\n", pid_to_stop);
                kill(pid_to_stop, SIGKILL);
                waitpid(pid_to_stop, &status, 0);
            } else if (result == pid_to_stop)
                printf("PID %d terminated after SIGTERM.\n", pid_to_stop);
            else
                perror("waitpid after SIGTERM failed");
        } else if (result == pid_to_stop)
            printf("PID %d had already terminated.\n", pid_to_stop);
        else {
            if (errno == ECHILD)
                printf("PID %d does not exist or is not a child. Already reaped?\n", pid_to_stop);
            else
                perror("Initial waitpid failed");
        }
        if (result != pid_to_stop)
            waitpid(pid_to_stop, NULL, 0);
        printf("PID %d stopped and reaped.\n", pid_to_stop);
    } else {
        printf("No active process (PID <= 0) was running.\n");
    }
    printf("Music stop sequence complete.\n");
}

// --- Hàm phát nhạc theo index trong playlist GỐC ---
void play_music(int playlist_idx) {
    pthread_mutex_lock(&playlist_mutex);
    if (playlist_idx < 0 || playlist_idx >= num_songs) {
        pthread_mutex_unlock(&playlist_mutex);
        return;
    }
    SongInfo* song = &playlist[playlist_idx];
    if (!song || !song->filepath) {
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
    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
        perror("pipe failed");
        return;
    }
    pid_t child_pid = fork();
    if (child_pid < 0) {
        perror("fork failed");
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return;
    }
    if (child_pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp("mpg123", "mpg123", "-R", "--no-control", NULL);
        perror("execlp mpg123 failed");
        exit(EXIT_FAILURE);
    }
    else {
        mpg123_pid = child_pid;
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        mpg123_stdin = fdopen(stdin_pipe[1], "w");
        FILE* mpg123_stdout_for_thread = fdopen(stdout_pipe[0], "r");
        if (!mpg123_stdin || !mpg123_stdout_for_thread) {
            perror("fdopen failed");
            if (mpg123_stdin)
                fclose(mpg123_stdin);
            else
                close(stdin_pipe[1]);
            if (mpg123_stdout_for_thread)
                fclose(mpg123_stdout_for_thread);
            else
                close(stdout_pipe[0]);
            kill(mpg123_pid, SIGKILL);
            waitpid(mpg123_pid, NULL, 0);
            mpg123_pid = -1;
            mpg123_stdin = NULL;
            return;
        }
        setlinebuf(mpg123_stdin);
        monitor_thread_should_run = true;
        if (pthread_create(&monitor_thread, NULL, monitor_mpg123_output, (void*)mpg123_stdout_for_thread) != 0) {
            perror("pthread_create for monitor failed");
            fclose(mpg123_stdin);
            fclose(mpg123_stdout_for_thread);
            kill(mpg123_pid, SIGKILL);
            waitpid(mpg123_pid, NULL, 0);
            mpg123_pid = -1;
            mpg123_stdin = NULL;
            monitor_thread_should_run = false;
            return;
        }
        fprintf(mpg123_stdin, "LOAD %s\n", file_to_play);
        fprintf(mpg123_stdin, "VOLUME %d\n", volume);
        pthread_mutex_lock(&playlist_mutex);
        current_index = playlist_idx;
        pthread_mutex_unlock(&playlist_mutex);
        is_playing = true;
        is_paused = false;
        printf("Sent initial commands to mpg123 (PID: %d). Playing index %d: %s\n", mpg123_pid, playlist_idx, file_to_play);
    }
}

// --- Hàm Pause/Resume ---
void toggle_pause() {
    if (mpg123_pid != -1 && mpg123_stdin && is_playing) { 
        fprintf(mpg123_stdin, "PAUSE\n"); is_paused = !is_paused; 
        printf("Toggle Pause: %s\n", is_paused ? "Paused" : "Resumed"); 
    }
    else if (!is_playing && current_index != -1) { 
        pthread_mutex_lock(&playlist_mutex); 
        int idx = current_index; 
        pthread_mutex_unlock(&playlist_mutex); 
        if(idx != -1) play_music(idx); }
}
// --- Hàm tua nhạc ---
void jump_seconds(int seconds) {
    if (mpg123_pid != -1 && mpg123_stdin && is_playing && !is_paused) { 
        printf("Jumping %+d seconds\n", seconds); 
        fprintf(mpg123_stdin, "JUMP %+ds\n", seconds); 
        int est = current_time + seconds; 
        if (est < 0) est = 0; 
        if (total_time > 0 && est > total_time) est = total_time; 
        current_time = est; 
    }
}
// --- Hàm đặt âm lượng ---
void set_volume(int vol) {
    int new_volume = vol < 0 ? 0 : (vol > 100 ? 100 : vol);
    if (new_volume != volume) { // Chỉ gửi lệnh nếu volume thay đổi
        volume = new_volume;
        printf("Setting volume to %d\n", volume);
        if (mpg123_pid != -1 && mpg123_stdin) {
            fprintf(mpg123_stdin, "VOLUME %d\n", volume);
            // fflush(mpg123_stdin); // Không cần nếu dùng setlinebuf
        }
    }
}
// --- Luồng theo dõi thay đổi USB ---
void* watch_usb_changes(void* arg) {
    char* path = (char*)arg; int fd = inotify_init1(IN_NONBLOCK); if (fd < 0) { perror("inotify_init1 failed"); free(path); return NULL; }
    int wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_ISDIR); if (wd == -1) { fprintf(stderr, "inotify_add_watch failed for %s: %s\n", path, strerror(errno)); close(fd); free(path); return NULL; }
    printf("USB Watchdog started for path: %s\n", path); usb_thread_running = true; char buffer[sizeof(struct inotify_event) + NAME_MAX + 1] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    while (usb_thread_running) { int length = read(fd, buffer, sizeof(buffer)); if (length < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(500000); continue; } else { perror("read inotify failed"); break; } }
        int i = 0; bool change_detected = false; while (i < length && usb_thread_running) { struct inotify_event* event = (struct inotify_event*)&buffer[i]; if (event->mask & IN_ISDIR) { if ((event->mask & IN_CREATE) || (event->mask & IN_DELETE) || (event->mask & IN_MOVED_TO) || (event->mask & IN_MOVED_FROM)) { if (event->len > 0 && event->name[0] != '.') { printf("Potential USB mount/unmount detected: %s\n", event->name); change_detected = true; break; } } } i += sizeof(struct inotify_event) + event->len; }
        if (change_detected) { printf("USB change confirmed. Triggering rescan in 2 seconds...\n"); sleep(2); find_usb_and_create_playlist(false); }
    } printf("USB Watchdog stopping...\n"); inotify_rm_watch(fd, wd); close(fd); free(path); printf("USB Watchdog stopped.\n"); return NULL;
}


// --- Hàm vẽ chữ đơn giản ---
void draw_text_simple(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color) {
    if (!text || !font || !renderer || strlen(text) == 0) return;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color); if (!surface) { fprintf(stderr, "TTF_RenderUTF8_Blended Error: %s (text: %s)\n", TTF_GetError(), text); return; }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface); if (!texture) { fprintf(stderr, "SDL_CreateTextureFromSurface Error: %s\n", SDL_GetError()); SDL_FreeSurface(surface); return; }
    SDL_Rect dst = {x, y, surface->w, surface->h}; SDL_RenderCopy(renderer, texture, NULL, &dst); SDL_DestroyTexture(texture); SDL_FreeSurface(surface);
}

// --- Hàm vẽ chữ với cắt xén ---
void draw_text_clipped(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color, SDL_Rect* clip_rect) {
     if (!text || !font || !renderer || strlen(text) == 0 || !clip_rect) return;
     SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color); if (!surface) return;
     SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface); if (!texture) { SDL_FreeSurface(surface); return; }
     SDL_Rect src_rect = {0, 0, surface->w, surface->h}; SDL_Rect dst_rect = {x, y, surface->w, surface->h}; SDL_Rect render_rect;
     if (SDL_IntersectRect(&dst_rect, clip_rect, &render_rect)) {
          if (dst_rect.x < render_rect.x) { src_rect.x = (int)(((float)(render_rect.x - dst_rect.x) / dst_rect.w) * src_rect.w); src_rect.w = (int)(((float)render_rect.w / dst_rect.w) * src_rect.w); } else { src_rect.x = 0; src_rect.w = (int)(((float)render_rect.w / dst_rect.w) * src_rect.w); }
          if (dst_rect.y < render_rect.y) { src_rect.y = (int)(((float)(render_rect.y - dst_rect.y) / dst_rect.h) * src_rect.h); src_rect.h = (int)(((float)render_rect.h / dst_rect.h) * src_rect.h); } else { src_rect.y = 0; src_rect.h = (int)(((float)render_rect.h / dst_rect.h) * src_rect.h); }
          SDL_RenderCopy(renderer, texture, &src_rect, &render_rect);
     } SDL_DestroyTexture(texture); SDL_FreeSurface(surface);
}

// --- Hàm vẽ nút bấm Icon ---
void draw_icon_button(SDL_Renderer* renderer, SDL_Rect* rect, SDL_Texture* icon_tex, ButtonState state) {
    SDL_SetRenderDrawColor(renderer, button_bg_color.r, button_bg_color.g, button_bg_color.b, button_bg_color.a); SDL_RenderFillRect(renderer, rect);
    if (icon_tex) { int icon_w, icon_h; SDL_QueryTexture(icon_tex, NULL, NULL, &icon_w, &icon_h); SDL_Rect icon_dest_rect; int padding = 5; int max_icon_w = rect->w - 2 * padding; int max_icon_h = rect->h - 2 * padding; float w_ratio = (float)max_icon_w / icon_w; float h_ratio = (float)max_icon_h / icon_h; float scale = fminf(1.0f, fminf(w_ratio, h_ratio)); icon_dest_rect.w = (int)(icon_w * scale); icon_dest_rect.h = (int)(icon_h * scale); icon_dest_rect.x = rect->x + (rect->w - icon_dest_rect.w) / 2; icon_dest_rect.y = rect->y + (rect->h - icon_dest_rect.h) / 2; SDL_RenderCopy(renderer, icon_tex, NULL, &icon_dest_rect); }
    else { draw_text_simple(renderer, font, "X", rect->x + rect->w / 2 - 5, rect->y + rect->h / 2 - 8, red); }
    if (state == BTN_HOVER) { SDL_SetRenderDrawColor(renderer, yellow.r, yellow.g, yellow.b, 180); SDL_RenderDrawRect(renderer, rect); }
    else if (state == BTN_PRESSED) { SDL_SetRenderDrawColor(renderer, white.r, white.g, white.b, 220); SDL_RenderDrawRect(renderer, rect); }
}

// --- Tải Icons (Bỏ vol_up/down, thêm volume) ---
bool load_icons(SDL_Renderer* renderer) {
    bool success = true;
    #define LOAD_ICON(tex_var, path) tex_var = IMG_LoadTexture(renderer, path); if (!tex_var) { fprintf(stderr, "Failed to load icon '%s': %s\n", path, IMG_GetError()); success = false; } else { printf("Loaded icon: %s\n", path); }
    LOAD_ICON(tex_play, ICON_PATH_PLAY); 
    LOAD_ICON(tex_pause, ICON_PATH_PAUSE); 
    LOAD_ICON(tex_next, ICON_PATH_NEXT); 
    LOAD_ICON(tex_prev, ICON_PATH_PREV);
    LOAD_ICON(tex_rewind, ICON_PATH_REWIND);   // <<< THÊM DÒNG NÀY
    LOAD_ICON(tex_forward, ICON_PATH_FORWARD); // <<< THÊM DÒNG NÀY
    LOAD_ICON(tex_volume, ICON_PATH_VOLUME); // Thêm icon volume
    LOAD_ICON(tex_search, ICON_PATH_SEARCH); 
    LOAD_ICON(tex_search_active, ICON_PATH_SEARCH_ACTIVE);
    #undef LOAD_ICON
    if (!success) { fprintf(stderr, "--- One or more icons failed to load. ---\n"); }
    return success;
}

// --- Dọn dẹp tài nguyên (Bỏ vol_up/down, thêm volume) ---
void cleanup() {
    printf("Cleaning up...\n");
    if (usb_thread_running) { 
        usb_thread_running = false; 
        pthread_join(usb_thread, NULL); 
        printf("USB watchdog thread joined.\n"); 
    }
    stop_music();
    printf("Freeing playlist memory...\n"); 
    pthread_mutex_lock(&playlist_mutex); 
    for (int i = 0; i < num_songs; i++) free_song_info(&playlist[i]); 
    num_songs = 0; 
    pthread_mutex_unlock(&playlist_mutex); 
    pthread_mutex_destroy(&playlist_mutex); 
    printf("Playlist memory freed and mutex destroyed.\n");
    
    printf("Destroying icon textures...\n"); 
    SDL_DestroyTexture(tex_play); 
    SDL_DestroyTexture(tex_pause); 
    SDL_DestroyTexture(tex_next); 
    SDL_DestroyTexture(tex_prev);
    SDL_DestroyTexture(tex_rewind);   // <<< THÊM DÒNG NÀY
    SDL_DestroyTexture(tex_forward);  // <<< THÊM DÒNG NÀ
    SDL_DestroyTexture(tex_volume); // Thêm volume
    SDL_DestroyTexture(tex_search); SDL_DestroyTexture(tex_search_active);
    tex_play = tex_pause = tex_next = tex_prev = tex_rewind = tex_forward = tex_volume = tex_search = tex_search_active = NULL; // <<< SỬA LẠI DÒNG NÀY
    // <<< THÊM: Giải phóng ảnh bìa mặc định >>>
    SDL_DestroyTexture(tex_default_art); tex_default_art = NULL;
    // <<< KẾT THÚC THÊM >>>
    // <<< THÊM: Giải phóng ảnh nền cố định >>>
    SDL_DestroyTexture(tex_left_background);
    tex_left_background = NULL;
    // <<< KẾT THÚC THÊM >>>
    if (font) {
        TTF_CloseFont(font); 
    }
    if (small_font && small_font != font) TTF_CloseFont(small_font);
    if (renderer) {
        SDL_DestroyRenderer(renderer); 
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    IMG_Quit(); 
    TTF_Quit(); 
    SDL_Quit(); 
    printf("SDL, TTF, and IMG quit.\nCleanup complete.\n");
}

// --- Khởi tạo SDL, TTF, SDL_image và GUI ---
bool init_sdl_and_gui() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) { //Khởi tạo SDL để dùng video và timer. Nếu thất bại, in lỗi rồi return false.
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError()); 
        return false; 
    }
    if (TTF_Init() < 0) { //Khởi tạo thư viện vẽ chữ SDL_ttf.
        fprintf(stderr, "TTF_Init Error: %s\n", TTF_GetError()); 
        SDL_Quit(); 
        return false; 
    }

    int imgFlags = IMG_INIT_PNG; 
    if (!(IMG_Init(imgFlags) & imgFlags)) { // Khởi tạo thư viện SDL_image để có thể load ảnh .png.
        fprintf(stderr, "IMG_Init Error: %s\n", IMG_GetError()); 
        TTF_Quit(); 
        SDL_Quit(); 
        return false; 
    }

    //Tạo cửa sổ chính có tiêu đề "Slider MP3 Player" với kích thước WINDOW_WIDTH x WINDOW_HEIGHT
    window = SDL_CreateWindow("Slider MP3 Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) { 
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError()); 
        IMG_Quit(); TTF_Quit(); SDL_Quit(); 
        return false; 
    }

    // Tạo renderer để vẽ đồ họa lên cửa sổ, có hỗ trợ tăng tốc phần cứng và đồng bộ với VSYNC.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { 
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError()); 
        SDL_DestroyWindow(window); 
        IMG_Quit(); 
        TTF_Quit(); 
        SDL_Quit(); 
        return false; 
    }
    font = TTF_OpenFont(FONT_PATH, 20); // Tải font chính kích cỡ 20 (font chữ to)
    if (!font) { 
        font = TTF_OpenFont(FALLBACK_FONT_PATH, 20); //Thử font dự phòng
        if(!font) { 
            fprintf(stderr, "TTF_OpenFont Error: %s\n", TTF_GetError()); 
            cleanup(); return false; } 
            printf("Using fallback font.\n"); 
    } else { 
        printf("Using font: %s\n", FONT_PATH); 
    }
    small_font = TTF_OpenFont(FONT_PATH, 15); // Tải font chữ nhỏ hơn (kích cỡ 15), dùng cho các thông tin phụ.
    if (!small_font) small_font = TTF_OpenFont(FALLBACK_FONT_PATH, 15); //font dự phòng
    if (!small_font) { 
        printf("Warning: Could not load small font, using default.\n"); 
        small_font = font; 
    }
    if (!load_icons(renderer)) { //Gọi hàm load_icons() để tải các icon dùng trong giao diện. 
                                // Nếu không tải được, chương trình vẫn tiếp tục chạy.
        fprintf(stderr, "Proceeding without some icons...\n"); /* return false; */ 
    }

    // <<< THÊM: Tải ảnh bìa mặc định >>>
    tex_default_art = IMG_LoadTexture(renderer, "icons/no_art.png"); // Đặt đúng đường dẫn 
    if (!tex_default_art) {
        fprintf(stderr, "Warning: Failed to load default album art 'icons/no_art.png': %s\n", IMG_GetError());
        // Bạn có thể tạo một texture màu đơn giản làm fallback ở đây nếu muốn
    } else {
        printf("Loaded default album art.\n");
    }
    // <<< KẾT THÚC THÊM >>>

    // <<< THÊM: Tải ảnh nền cố định cho bên trái >>>
    tex_left_background = IMG_LoadTexture(renderer, "icons/background_left.png"); // <<< ĐẶT ĐÚNG ĐƯỜNG DẪN FILE ẢNH NỀN CỦA BẠN
    if (!tex_left_background) {
        fprintf(stderr, "Warning: Failed to load left background image 'icons/background_left.png': %s\n", IMG_GetError());
        // Chương trình vẫn chạy, nhưng nền trái sẽ chỉ là màu đơn sắc
    } else {
        printf("Loaded fixed left background image.\n");
    }
    // <<< KẾT THÚC THÊM >>>

    calculate_layout(); //  hàm để tính toán vị trí và kích thước 
                        // các thành phần giao diện (playlist, nút điều khiển, ảnh bìa...).
    return true;
}

// --- Tính toán Layout (Thay thế nút Vol bằng Slider) ---
// --- Tính toán Layout (Chia 2/3 Trái - 1/3 Phải) ---
void calculate_layout() {
    // --- Xác định điểm chia và chiều rộng các khu vực ---
    int divider_x = (WINDOW_WIDTH * 2) / 3;
    int left_width = divider_x;
    int right_width = WINDOW_WIDTH - divider_x;

    // --- Khu vực bên Phải (1/3): Playlist và Search ---
    // Search (Nằm trên cùng bên phải)
    search_activate_button_rect = (SDL_Rect){divider_x + PADDING, PADDING, BUTTON_WIDTH, BUTTON_HEIGHT};
    search_bar_rect = (SDL_Rect){
        search_activate_button_rect.x + search_activate_button_rect.w + PADDING,
        PADDING,
        right_width - (search_activate_button_rect.w + PADDING * 3), // Trừ padding 2 bên và nút search
        BUTTON_HEIGHT
    };
    // Playlist (Nằm dưới search bên phải, chiếm hết chiều cao còn lại)
    playlist_area_rect = (SDL_Rect){
        divider_x + PADDING,
        search_bar_rect.y + search_bar_rect.h + PADDING,
        right_width - PADDING * 2,
        WINDOW_HEIGHT - (search_bar_rect.y + search_bar_rect.h + PADDING) - PADDING
    };

    // --- Khu vực bên Trái (2/3): Controls, Info, Progress ---
    // Controls (Nằm dưới cùng bên trái)
    control_area_rect = (SDL_Rect){
        PADDING,
        WINDOW_HEIGHT - BUTTON_HEIGHT - PADDING,
        left_width - PADDING * 2,
        BUTTON_HEIGHT
    };
    // Progress Bar (Nằm trên Controls bên trái)
    progress_bar_bg_rect = (SDL_Rect){
        PADDING,
        control_area_rect.y - PROGRESS_BAR_HEIGHT - PADDING / 2,
        left_width - PADDING * 2,
        PROGRESS_BAR_HEIGHT
    };
    // Time Text (Nằm trên Progress Bar bên trái)
    time_text_rect = (SDL_Rect){
        PADDING,
        progress_bar_bg_rect.y - LIST_ITEM_HEIGHT - 5,
        left_width - PADDING * 2,
        LIST_ITEM_HEIGHT
    };
     // Info Area (Nằm trên Time Text bên trái)
    info_area_rect = (SDL_Rect){
        PADDING,
        time_text_rect.y - LIST_ITEM_HEIGHT * 2 - PADDING,
        left_width - PADDING * 2,
        LIST_ITEM_HEIGHT * 2
    };

    // --- Sắp xếp lại các nút điều khiển và Volume trong Control Area (bên trái) ---
    int button_group_width = BUTTON_WIDTH * 5 + PADDING * 4; // 5 nút
    int vol_area_width = BUTTON_WIDTH + PADDING + VOLUME_SLIDER_WIDTH + PADDING + 50; // Vol Icon, Slider, Text %
    int total_control_width = button_group_width + PADDING * 2 + vol_area_width; // Tổng chiều rộng cần thiết
    int control_start_x;

    // Canh giữa cụm control trong khu vực control_area_rect bên trái
    if (total_control_width < control_area_rect.w) {
         control_start_x = control_area_rect.x + (control_area_rect.w - total_control_width) / 2;
    } else {
        // Nếu không đủ chỗ, bắt đầu từ bên trái
        control_start_x = control_area_rect.x;
        // Cân nhắc giảm PADDING hoặc kích thước nút/slider nếu bị tràn quá nhiều
    }

    // Các nút điều khiển chính
    prev_button_rect = (SDL_Rect){control_start_x, control_area_rect.y, BUTTON_WIDTH, BUTTON_HEIGHT};
    control_start_x += BUTTON_WIDTH + PADDING;
    rewind_button_rect = (SDL_Rect){control_start_x, control_area_rect.y, BUTTON_WIDTH, BUTTON_HEIGHT};
    control_start_x += BUTTON_WIDTH + PADDING;
    play_pause_button_rect = (SDL_Rect){control_start_x, control_area_rect.y, BUTTON_WIDTH, BUTTON_HEIGHT};
    control_start_x += BUTTON_WIDTH + PADDING;
    forward_button_rect = (SDL_Rect){control_start_x, control_area_rect.y, BUTTON_WIDTH, BUTTON_HEIGHT};
    control_start_x += BUTTON_WIDTH + PADDING;
    next_button_rect = (SDL_Rect){control_start_x, control_area_rect.y, BUTTON_WIDTH, BUTTON_HEIGHT};
    control_start_x += BUTTON_WIDTH + PADDING * 2; // Khoảng cách trước volume

    // Khu vực Volume
    volume_icon_rect = (SDL_Rect){control_start_x, control_area_rect.y + (BUTTON_HEIGHT - BUTTON_WIDTH)/2, BUTTON_WIDTH, BUTTON_WIDTH};
    control_start_x += BUTTON_WIDTH + PADDING;
    volume_slider_bg_rect = (SDL_Rect){control_start_x, control_area_rect.y + (BUTTON_HEIGHT - VOLUME_SLIDER_HEIGHT) / 2, VOLUME_SLIDER_WIDTH, VOLUME_SLIDER_HEIGHT};
    control_start_x += VOLUME_SLIDER_WIDTH + PADDING;
    volume_text_rect = (SDL_Rect){control_start_x, control_area_rect.y + (BUTTON_HEIGHT - LIST_ITEM_HEIGHT) / 2, 45, LIST_ITEM_HEIGHT};

    // Volume handle (chỉ cần tính lại Y nếu control_area_rect.y thay đổi)
    volume_handle_rect.w = VOLUME_HANDLE_WIDTH;
    volume_handle_rect.h = VOLUME_HANDLE_HEIGHT;
    volume_handle_rect.y = volume_slider_bg_rect.y + (volume_slider_bg_rect.h - VOLUME_HANDLE_HEIGHT) / 2;
    // Vị trí X của volume handle sẽ được tính toán lại trong vòng lặp chính dựa trên giá trị volume hiện tại

    // Tính lại số item hiển thị trong playlist dựa trên chiều cao mới
    if (font) {
        int fh = TTF_FontHeight(font);
        visible_list_items = (fh > 0) ? playlist_area_rect.h / fh : playlist_area_rect.h / LIST_ITEM_HEIGHT;
    } else {
        visible_list_items = playlist_area_rect.h / LIST_ITEM_HEIGHT;
    }
    if (visible_list_items < 1) visible_list_items = 1;
}

// --- Hàm tính toán và cập nhật volume từ vị trí chuột X ---
void update_volume_from_mouse(int mouse_x) {
    // Tính toán vị trí tương đối của chuột trên thanh slider
    int relative_x = mouse_x - volume_slider_bg_rect.x;
    // Tính tỉ lệ (0.0 to 1.0)
    float volume_ratio = (float)relative_x / volume_slider_bg_rect.w;
    // Giới hạn tỉ lệ trong khoảng 0.0 - 1.0
    volume_ratio = fmaxf(0.0f, fminf(1.0f, volume_ratio));
    // Chuyển đổi sang giá trị volume (0-100)
    int new_volume = (int)roundf(volume_ratio * 100.0f);
    // Đặt âm lượng mới
    set_volume(new_volume);
}

// --- Xử lý sự kiện nhấp chuột ---
void handle_mouse_click(int x, int y) {
    SDL_Point click_point = {x, y};

    // --- Kiểm tra Volume Slider trước ---
    // Tạo rect lớn hơn chút để dễ click
    SDL_Rect volume_click_area = volume_slider_bg_rect;
    volume_click_area.x -= 5;
    volume_click_area.w += 10;
    volume_click_area.y -= 5; // Mở rộng theo chiều dọc
    volume_click_area.h += 10;

    if (SDL_PointInRect(&click_point, &volume_click_area)) {
        is_dragging_volume = true;
        update_volume_from_mouse(x); // Cập nhật volume ngay khi click
        return; // Đã xử lý volume click
    }

    // --- Kiểm tra các nút khác ---
    if (SDL_PointInRect(&click_point, &prev_button_rect)) { pthread_mutex_lock(&playlist_mutex); if (num_songs > 0) { int idx = (current_index - 1 + num_songs) % num_songs; pthread_mutex_unlock(&playlist_mutex); play_music(idx); } else pthread_mutex_unlock(&playlist_mutex); prev_btn_state = BTN_PRESSED; return; }
    if (SDL_PointInRect(&click_point, &rewind_button_rect)) { jump_seconds(-5); rewind_btn_state = BTN_PRESSED; return; } // <<< THÊM DÒNG NÀY
    if (SDL_PointInRect(&click_point, &play_pause_button_rect)) { toggle_pause(); play_pause_btn_state = BTN_PRESSED; return; }
    if (SDL_PointInRect(&click_point, &forward_button_rect)) { jump_seconds(5); forward_btn_state = BTN_PRESSED; return; } // <<< THÊM DÒNG NÀY
    if (SDL_PointInRect(&click_point, &next_button_rect)) { pthread_mutex_lock(&playlist_mutex); if (num_songs > 0) { int idx = (current_index + 1) % num_songs; pthread_mutex_unlock(&playlist_mutex); play_music(idx); } else pthread_mutex_unlock(&playlist_mutex); next_btn_state = BTN_PRESSED; return; }
    if (SDL_PointInRect(&click_point, &search_activate_button_rect)) { search_active = !search_active; if (search_active) { SDL_StartTextInput(); search_query[0] = '\0'; } else { SDL_StopTextInput(); search_query[0] = '\0'; } update_search_filter(); search_activate_btn_state = search_active ? BTN_PRESSED : BTN_NORMAL; return; }

    // --- Kiểm tra thanh tiến trình bài hát ---
    if (SDL_PointInRect(&click_point, &progress_bar_bg_rect)) { if ((is_playing || is_paused) && total_time > 0) { float ratio = fmaxf(0.0f, fminf(1.0f, (float)(x - progress_bar_bg_rect.x) / progress_bar_bg_rect.w)); int target = (int)(ratio * total_time); jump_seconds(target - current_time); } return; }

    // --- Kiểm tra vùng playlist ---
    if (SDL_PointInRect(&click_point, &playlist_area_rect)) { 
        Uint32 current_click_time = SDL_GetTicks(); // Lấy thời gian click hiện tại
        int p_idx = -1;      // Index thực sự trong playlist[]
        int visual_idx = -1; // Index nhìn thấy trên màn hình

        pthread_mutex_lock(&playlist_mutex); 
        int count = search_active ? num_filtered_songs : num_songs; 
        int fh = TTF_FontHeight(font) > 0 ? TTF_FontHeight(font) : LIST_ITEM_HEIGHT; 
        visual_idx = (y - playlist_area_rect.y + list_scroll_offset) / fh; 
        /* 
        if (visual_idx >= 0 && visual_idx < count) { 
            if (search_active) { 
                current_filtered_index = visual_idx; 
                current_index = filtered_indices[current_filtered_index]; 
            } else { 
                current_index = visual_idx; 
            } 
            printf("Selected index %d via click.\n", current_index); // play_music(current_index);  
        } 
        pthread_mutex_unlock(&playlist_mutex); 
        */

        if (visual_idx >= 0 && visual_idx < count) {
            // Xác định index thực sự (p_idx) dựa trên visual_idx
            if (search_active) {
                p_idx = filtered_indices[visual_idx];
            } else {
                p_idx = visual_idx;
            }
    
            // *** BẮT ĐẦU LOGIC DOUBLE CLICK ***
            if (p_idx != -1 && p_idx == last_playlist_click_index && (current_click_time - last_playlist_click_time < DOUBLE_CLICK_INTERVAL)) {
                // --- Đã phát hiện Double Click ---
                printf("Double-click on index %d. Playing.\n", p_idx);
                int index_to_play = p_idx; // Lưu lại index trước khi unlock mutex
                pthread_mutex_unlock(&playlist_mutex); // Unlock mutex trước khi gọi play_music
    
                play_music(index_to_play); // Gọi hàm phát nhạc
    
                // Reset trạng thái double-click để tránh click tiếp theo bị nhầm
                last_playlist_click_time = 0;
                last_playlist_click_index = -1;
    
            } else {
                // --- Single Click hoặc Click đầu tiên ---
                printf("Single click on index %d. Selecting.\n", p_idx);
                // Hành động chọn bài (như code cũ của bạn)
                if (search_active) {
                    current_filtered_index = visual_idx;
                    current_index = p_idx; // Cũng cập nhật current_index chính
                } else {
                    current_index = p_idx;
                }
    
                // Lưu lại thông tin cho lần click tiếp theo (để kiểm tra double-click)
                last_playlist_click_time = current_click_time;
                last_playlist_click_index = p_idx;
    
                pthread_mutex_unlock(&playlist_mutex); // Unlock mutex sau khi xử lý single click
            }
            // *** KẾT THÚC LOGIC DOUBLE CLICK ***
    
        } else {
            // Click vào vùng trống trong playlist, reset trạng thái double-click
            last_playlist_click_time = 0;
            last_playlist_click_index = -1;
            pthread_mutex_unlock(&playlist_mutex);
        }
        
        
        return; 
    }

    // --- Click ngoài vùng search khi đang active ---
    if (search_active && !SDL_PointInRect(&click_point, &search_bar_rect) && !SDL_PointInRect(&click_point, &search_activate_button_rect)) { search_active = false; SDL_StopTextInput(); search_query[0] = '\0'; update_search_filter(); search_activate_btn_state = BTN_NORMAL; }
}

// --- Xử lý di chuyển chuột (cập nhật volume nếu đang kéo) ---
void handle_mouse_motion(int x, int y, bool mouse_button_down) {
    SDL_Point mouse_point = {x, y};

    // Cập nhật volume nếu đang kéo
    if (is_dragging_volume && mouse_button_down) {
        update_volume_from_mouse(x);
    }

    // Cập nhật trạng thái hover cho các nút (chỉ khi không kéo volume)
    if (!is_dragging_volume) {
         if (prev_btn_state != BTN_PRESSED) prev_btn_state = SDL_PointInRect(&mouse_point, &prev_button_rect) ? BTN_HOVER : BTN_NORMAL;
         if (rewind_btn_state != BTN_PRESSED) rewind_btn_state = SDL_PointInRect(&mouse_point, &rewind_button_rect) ? BTN_HOVER : BTN_NORMAL; // <<< THÊM DÒNG NÀY
         if (play_pause_btn_state != BTN_PRESSED) play_pause_btn_state = SDL_PointInRect(&mouse_point, &play_pause_button_rect) ? BTN_HOVER : BTN_NORMAL;
         if (forward_btn_state != BTN_PRESSED) forward_btn_state = SDL_PointInRect(&mouse_point, &forward_button_rect) ? BTN_HOVER : BTN_NORMAL; // <<< THÊM DÒNG NÀY
         if (next_btn_state != BTN_PRESSED) next_btn_state = SDL_PointInRect(&mouse_point, &next_button_rect) ? BTN_HOVER : BTN_NORMAL;
         search_activate_btn_state = SDL_PointInRect(&mouse_point, &search_activate_button_rect) ? BTN_HOVER : BTN_NORMAL;
    }
}

// --- Xử lý nhả chuột (dừng kéo volume) ---
void handle_mouse_up(int x, int y) {
    SDL_Point click_point = {x, y}; // Đặt tên rõ ràng hơ
    if (is_dragging_volume) {
        is_dragging_volume = false;
        // Có thể cập nhật trạng thái hover của các nút khác ở đây nếu cần
        handle_mouse_motion(x, y, false); // Gọi lại để cập nhật hover
    }
    // Reset trạng thái PRESSED của các nút về HOVER hoặc NORMAL
    if (prev_btn_state == BTN_PRESSED) prev_btn_state = SDL_PointInRect(&(SDL_Point){x,y}, &prev_button_rect) ? BTN_HOVER : BTN_NORMAL;
    if (rewind_btn_state == BTN_PRESSED) rewind_btn_state = SDL_PointInRect(&click_point, &rewind_button_rect) ? BTN_HOVER : BTN_NORMAL; // <<< THÊM DÒNG NÀY
    if (play_pause_btn_state == BTN_PRESSED) play_pause_btn_state = SDL_PointInRect(&(SDL_Point){x,y}, &play_pause_button_rect) ? BTN_HOVER : BTN_NORMAL;
    if (forward_btn_state == BTN_PRESSED) forward_btn_state = SDL_PointInRect(&click_point, &forward_button_rect) ? BTN_HOVER : BTN_NORMAL; // <<< THÊM DÒNG NÀY
    if (next_btn_state == BTN_PRESSED) next_btn_state = SDL_PointInRect(&(SDL_Point){x,y}, &next_button_rect) ? BTN_HOVER : BTN_NORMAL;
    // Nút search không giữ pressed
    search_activate_btn_state = SDL_PointInRect(&(SDL_Point){x, y}, &search_activate_button_rect) ? BTN_HOVER : BTN_NORMAL;
}


// --- Xử lý cuộn chuột ---
void handle_mouse_wheel(int y_delta) {
    pthread_mutex_lock(&playlist_mutex);
    int count = search_active ? num_filtered_songs : num_songs; int fh = TTF_FontHeight(font) > 0 ? TTF_FontHeight(font) : LIST_ITEM_HEIGHT;
    int total_h = count * fh; int max_scroll = total_h > playlist_area_rect.h ? total_h - playlist_area_rect.h : 0;
    list_scroll_offset -= y_delta * fh * 3; // Scroll 3 lines
    if (list_scroll_offset < 0) {
        list_scroll_offset = 0; 
    }
    if (list_scroll_offset > max_scroll) {
        list_scroll_offset = max_scroll;
    }
    pthread_mutex_unlock(&playlist_mutex);
}

// --- Hàm main ---
int main(void) {
    // 1. Khởi tạo SDL, GUI và tải tài nguyên ban đầu
    if (!init_sdl_and_gui()) return 1;

    // 2. Quét USB lần đầu và tạo playlist ban đầu
    find_usb_and_create_playlist(true); 
    update_search_filter(); // Cập nhật bộ lọc ban đầu

    // 3. Khởi tạo luồng theo dõi thay đổi USB
    struct passwd* pw = getpwuid(getuid()); 
    if (pw) { 
        char* usb_path = malloc(512); 
        if (usb_path) { 
            snprintf(usb_path, 512, USB_MONITOR_DIR_FORMAT, pw->pw_name); 
            struct stat st; 
            if (stat(usb_path, &st) == 0 && S_ISDIR(st.st_mode)) { 
                if (pthread_create(&usb_thread, NULL, watch_usb_changes, usb_path) != 0) { 
                    perror("pthread_create USB failed"); 
                    free(usb_path); // Giải phóng nếu tạo luồng thất bại
                }
                // Nếu tạo luồng thành công, usb_path sẽ được free bên trong luồng 
            } else { 
                fprintf(stderr, "Warning: USB dir '%s' not found.\n", usb_path); 
                free(usb_path); // Giải phóng nếu không tìm thấy thư mục
            } 
        } else { 
            perror("malloc USB path failed"); 
        } 
    } else { 
        fprintf(stderr, "Could not get user info for USB watcher.\n"); 
    }

     // 4. Khai báo biến cho vòng lặp chính
    bool running = true; SDL_Event e; 
    Uint32 last_input_time = SDL_GetTicks(); // Cho con trỏ nhấp nháy trong search bar
    bool mouse_down = false; // Theo dõi trạng thái nút chuột trái

    // 5. Vòng lặp chính của ứng dụng
    while (running) {
        // --- 5.1 Xử lý sự kiện ---
        while (SDL_PollEvent(&e)) {
            // 5.1.1 Sự kiện đóng cửa sổ
            if (e.type == SDL_QUIT) { running = false; }

            // 5.1.2 Sự kiện chuột
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouse_down = true;
                handle_mouse_click(e.button.x, e.button.y);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_down = false;
                handle_mouse_up(e.button.x, e.button.y); // Xử lý nhả chuột
            } else if (e.type == SDL_MOUSEMOTION) {
                 handle_mouse_motion(e.motion.x, e.motion.y, mouse_down); // Truyền trạng thái nút chuột
            } else if (e.type == SDL_MOUSEWHEEL) {
                 handle_mouse_wheel(e.wheel.y);
            }

            // 5.1.4 Sự kiện bàn phím
            // Phân biệt chế độ tìm kiếm và chế độ bình thường
            if (search_active) { // --- Chế độ Tìm kiếm ---
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: // Thoát tìm kiếm
                            search_active = false; 
                            SDL_StopTextInput(); 
                            search_query[0] = '\0'; 
                            update_search_filter(); 
                            search_activate_btn_state = BTN_NORMAL; 
                            break;
                        case SDLK_BACKSPACE: // Xóa ký tự
                            if (strlen(search_query) > 0) { 
                                search_query[strlen(search_query) - 1] = '\0';
                                update_search_filter(); 
                            } 
                            break;
                        case SDLK_RETURN: // Phát bài hát đang chọn trong kết quả lọc
                        case SDLK_KP_ENTER: 
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_filtered_songs > 0 && current_filtered_index >= 0) { 
                                int idx = filtered_indices[current_filtered_index]; 
                                pthread_mutex_unlock(&playlist_mutex); 
                                play_music(idx); 
                            } else pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_UP: // Di chuyển lên trong kết quả lọc
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_filtered_songs > 0) 
                                current_filtered_index = (current_filtered_index - 1 + num_filtered_songs) % num_filtered_songs; 
                            pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_DOWN: // Di chuyển xuống trong kết quả lọc
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_filtered_songs > 0) current_filtered_index = (current_filtered_index + 1) % num_filtered_songs; 
                            pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_SPACE: 
                            toggle_pause(); 
                            break;
                        default: break;
                    }
                } else if (e.type == SDL_TEXTINPUT) { // Nhận ký tự nhập
                    if (strlen(search_query) < SEARCH_QUERY_MAX) { 
                        strcat(search_query, e.text.text); 
                        update_search_filter(); 
                    } 
                }
            } else { // --- Chế độ Bình thường ---
                 if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: // Thoát ứng dụng 
                            running = false; 
                            break;
                        case SDLK_SPACE: // Pause/Resume
                            toggle_pause(); 
                            break;
                        case SDLK_d:     // Bài kế tiếp
                        case SDLK_RIGHT: 
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_songs > 0) { 
                                int idx = (current_index + 1) % num_songs; 
                                pthread_mutex_unlock(&playlist_mutex); 
                                play_music(idx); 
                            } else pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_a:    // Bài trước đó
                        case SDLK_LEFT: 
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_songs > 0) { 
                                int idx = (current_index - 1 + num_songs) % num_songs; 
                                pthread_mutex_unlock(&playlist_mutex); 
                                play_music(idx); 
                            } else pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_EQUALS:   // Tăng âm lượng
                        case SDLK_PLUS:     // Tăng âm lượng
                        case SDLK_KP_PLUS: 
                            set_volume(volume + 5); 
                            break;
                        case SDLK_MINUS:    // Giảm âm lượng
                        case SDLK_KP_MINUS: // Giảm âm lượng
                            set_volume(volume - 5); 
                            break;
                        case SDLK_COMMA: 
                            jump_seconds(-5); 
                            break; // <<< THÊM: Tua lùi 5s
                        case SDLK_PERIOD: 
                            jump_seconds(5); 
                            break; // <<< THÊM: Tua tới 5s
                        case SDLK_UP: // Chọn bài phía trên trong playlist chính
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_songs > 0) 
                                current_index = (current_index - 1 + num_songs) % num_songs; 
                            pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_DOWN: // Chọn bài phía dưới trong playlist chính
                            pthread_mutex_lock(&playlist_mutex); 
                            if (num_songs > 0) 
                                current_index = (current_index + 1) % num_songs; 
                            pthread_mutex_unlock(&playlist_mutex); 
                            break;
                        case SDLK_RETURN: // Phát bài đang chọn
                        case SDLK_KP_ENTER: 
                            pthread_mutex_lock(&playlist_mutex);
                            int idx = current_index; 
                            pthread_mutex_unlock(&playlist_mutex); 
                            if(idx != -1) play_music(idx); 
                            break;
                        case SDLK_f:    // Kích hoạt tìm kiếm
                            search_active = true; 
                            SDL_StartTextInput(); 
                            search_query[0] = '\0'; update_search_filter(); 
                            search_activate_btn_state = BTN_HOVER; 
                            break;
                    }
                 }
            } // end if (!search_active)
        } // end while SDL_PollEvent

        // --- 5.2 Xử lý chuyển bài tự động khi bài hát kết thúc ---
        if (song_finished_flag) { 
            song_finished_flag = false;  // Reset cờ
            int next_idx = -1; 
            pthread_mutex_lock(&playlist_mutex); 

            if (num_songs > 0 && current_index != -1) next_idx = (current_index + 1) % num_songs; 
            int count = num_songs; 
            pthread_mutex_unlock(&playlist_mutex);

            if (next_idx != -1 && count > 0) { 
                printf("Playing next song automatically (index: %d).\n", next_idx); 
                play_music(next_idx); 
            } else { 
                printf("Song finished, playlist end/empty. Stopping.\n"); 
                stop_music(); 
            } 
        }

        // --- 5.3 Vẽ giao diện ---
        // 5.3.1 Vẽ nền chia đôi
        // (Sử dụng biến window_width, window_height nếu đã chuyển đổi)
        // 1. Xác định ranh giới và kích thước các vùng nền
        int current_window_width = WINDOW_WIDTH; // Hoặc biến nếu đã đổi
        int current_window_height = WINDOW_HEIGHT; // Hoặc biến nếu đã đổi
        int divider_x = (current_window_width * 2) / 3;
        SDL_Rect right_bg_fill_rect = { divider_x, 0, current_window_width - divider_x, current_window_height };
        
        // 2. Xóa toàn bộ màn hình bằng màu nền BÊN TRÁI trước
        SDL_SetRenderDrawColor(renderer,
                            left_background_color.r,
                            left_background_color.g,
                            left_background_color.b,
                            left_background_color.a); // Màu cho phần 2/3 trái
        SDL_RenderClear(renderer);

        // 3. Vẽ đè hình chữ nhật màu nền BÊN PHẢI lên khu vực 1/3
        SDL_SetRenderDrawColor(renderer,
                            right_background_color.r,
                            right_background_color.g,
                            right_background_color.b,
                            right_background_color.a); // Màu cho phần 1/3 phải
        SDL_RenderFillRect(renderer, &right_bg_fill_rect); // Vẽ hình chữ nhật nền phải

        // --- THÊM: Vẽ ảnh nền cố định cho bên trái ---
        if (tex_left_background) {
            // Xác định vùng đích là toàn bộ khu vực 2/3 bên trái
            SDL_Rect left_background_dest_rect = { 0, 0, divider_x, current_window_height };
            // Vẽ ảnh nền vào vùng đích. Ảnh sẽ tự động co giãn hoặc bị cắt nếu kích thước không khớp.
            // NULL thứ 3 nghĩa là vẽ toàn bộ texture nguồn.
            SDL_RenderCopy(renderer, tex_left_background, NULL, &left_background_dest_rect);
        }
        // --- KẾT THÚC Vẽ ảnh nền cố định ---

        // --- Kết thúc vẽ nền chia đôi ---


        // --- Vẽ Search ---
        SDL_Texture* search_icon = search_active ? tex_search_active : tex_search;
        draw_icon_button(renderer, &search_activate_button_rect, search_icon, search_activate_btn_state);
        SDL_SetRenderDrawColor(renderer, search_bg.r, search_bg.g, search_bg.b, 255); SDL_RenderFillRect(renderer, &search_bar_rect);
        char search_display[SEARCH_QUERY_MAX + 20]; if (search_active) { snprintf(search_display, sizeof(search_display), "%s", search_query); draw_text_simple(renderer, font, search_display, search_bar_rect.x + 5, search_bar_rect.y + (search_bar_rect.h - TTF_FontHeight(font))/2 , white); Uint32 ticks = SDL_GetTicks(); if ((ticks - last_input_time) % 1000 < 500) { int tw, th; TTF_SizeUTF8(font, search_display, &tw, &th); SDL_Rect cr = {search_bar_rect.x + 5 + tw + 1, search_bar_rect.y + (search_bar_rect.h - th)/2, 2, th}; SDL_SetRenderDrawColor(renderer, white.r, white.g, white.b, 255); SDL_RenderFillRect(renderer, &cr); } }
        else { draw_text_simple(renderer, font, "Search...", search_bar_rect.x + 5, search_bar_rect.y + (search_bar_rect.h - TTF_FontHeight(font))/2, gray); }
        last_input_time = SDL_GetTicks();

        // --- Vẽ Playlist ---
        pthread_mutex_lock(&playlist_mutex); int count = search_active ? num_filtered_songs : num_songs; int fh = TTF_FontHeight(font) > 0 ? TTF_FontHeight(font) : LIST_ITEM_HEIGHT; int total_h = count * fh; int max_scroll = total_h > playlist_area_rect.h ? total_h - playlist_area_rect.h : 0; if (list_scroll_offset > max_scroll) list_scroll_offset = max_scroll; if (list_scroll_offset < 0) list_scroll_offset = 0;
        SDL_RenderSetClipRect(renderer, &playlist_area_rect); int start_y = playlist_area_rect.y - list_scroll_offset;
        for (int i = 0; i < count; ++i) { int cy = start_y + i * fh; if (cy + fh < playlist_area_rect.y || cy > playlist_area_rect.y + playlist_area_rect.h) continue; int p_idx = search_active ? filtered_indices[i] : i; if (p_idx < 0 || p_idx >= num_songs) continue; SongInfo* song = &playlist[p_idx]; if (song && song->display_name) { char buf[1024]; SDL_Color tc = white; bool sel = (search_active && i == current_filtered_index) || (!search_active && p_idx == current_index); bool play = (p_idx == current_index && (is_playing || is_paused)); if (sel) { SDL_Rect sr = {playlist_area_rect.x, cy, playlist_area_rect.w, fh}; SDL_SetRenderDrawColor(renderer, yellow.r, yellow.g, yellow.b, 50); SDL_RenderFillRect(renderer, &sr); tc = yellow; } if (play) { tc = is_paused ? blue : green; SDL_Rect pr = {playlist_area_rect.x + 2, cy + 2, 5, fh-4}; SDL_SetRenderDrawColor(renderer, tc.r, tc.g, tc.b, 255); SDL_RenderFillRect(renderer, &pr); } snprintf(buf, sizeof(buf), "%d: %s %s", p_idx + 1, song->display_name, (song->artist && strlen(song->artist) > 0) ? "- ..." : ""); draw_text_clipped(renderer, font, buf, playlist_area_rect.x + PADDING + (play ? 8 : 0), cy + (fh - TTF_FontHeight(font))/2, tc, &playlist_area_rect); } }
        SDL_RenderSetClipRect(renderer, NULL);

        // --- Vẽ Info Area ---
        char status_buf[1100] = {0}; 
        char meta_buf[1100] = {0}; 
        SDL_Color st_c = yellow; int sel_idx = current_index;
        if (sel_idx != -1 && sel_idx < num_songs) { 
            SongInfo* s = &playlist[sel_idx]; 
            const char* st_str = "Selected"; 
            if (is_playing) st_str = is_paused ? "Paused" : "Playing"; else if (mpg123_pid != -1) st_str = "Stopped"; st_c = is_playing ? (is_paused ? blue : green) : (mpg123_pid != -1 ? red : yellow); 
            snprintf(status_buf, sizeof(status_buf), "[%s] %d/%d: %s", st_str, sel_idx + 1, num_songs, s->display_name ? s->display_name : "..."); 
            draw_text_simple(renderer, font, status_buf, info_area_rect.x, info_area_rect.y, st_c); 
            char yr_str[16]=""; 
            if(s->year>0) snprintf(yr_str,sizeof(yr_str)," (%d)",s->year); 
            snprintf(meta_buf, sizeof(meta_buf), "Artist: %s | Album: %s%s | Genre: %s", s->artist?s->artist:"N/A", s->album?s->album:"N/A", yr_str, s->genre?s->genre:"N/A"); 
            draw_text_simple(renderer, small_font, meta_buf, info_area_rect.x, info_area_rect.y + LIST_ITEM_HEIGHT, gray); }
        else if (num_songs == 0) { 
            draw_text_simple(renderer, font, "No songs found. Connect USB or check path.", info_area_rect.x, info_area_rect.y, red); }
        else { 
            draw_text_simple(renderer, font, "Select a song (UP/DOWN/Click) + ENTER/Play Button", info_area_rect.x, info_area_rect.y, yellow); 
        }
        pthread_mutex_unlock(&playlist_mutex);

        // --- Vẽ Progress Bar ---
        SDL_SetRenderDrawColor(renderer, dark_gray.r, dark_gray.g, dark_gray.b, 255); SDL_RenderFillRect(renderer, &progress_bar_bg_rect);
        if ((is_playing || is_paused) && total_time > 0) { float prog = fmaxf(0.0f, fminf(1.0f, (float)current_time / total_time)); progress_bar_fg_rect = progress_bar_bg_rect; progress_bar_fg_rect.w = (int)(progress_bar_bg_rect.w * prog); SDL_SetRenderDrawColor(renderer, green.r, green.g, green.b, 255); SDL_RenderFillRect(renderer, &progress_bar_fg_rect); char tinf[64]; snprintf(tinf, sizeof(tinf), "%02d:%02d / %02d:%02d", current_time / 60, current_time % 60, total_time / 60, total_time % 60); int tw, th; TTF_SizeUTF8(font, tinf, &tw, &th); draw_text_simple(renderer, font, tinf, time_text_rect.x + (time_text_rect.w - tw) / 2, time_text_rect.y, white); }
        else { int tw, th; TTF_SizeUTF8(font, "00:00 / 00:00", &tw, &th); draw_text_simple(renderer, font, "00:00 / 00:00", time_text_rect.x + (time_text_rect.w - tw) / 2, time_text_rect.y, gray); }

        // --- Vẽ Control Buttons & Volume Slider ---
        draw_icon_button(renderer, &prev_button_rect, tex_prev, prev_btn_state);
        draw_icon_button(renderer, &rewind_button_rect, tex_rewind, rewind_btn_state); // <<< THÊM VẼ REWIND
        draw_icon_button(renderer, &play_pause_button_rect, (is_playing && !is_paused) ? tex_pause : tex_play, play_pause_btn_state);
        draw_icon_button(renderer, &forward_button_rect, tex_forward, forward_btn_state); // <<< THÊM VẼ FORWARD
        draw_icon_button(renderer, &next_button_rect, tex_next, next_btn_state);

        // Vẽ Volume Slider
        SDL_SetRenderDrawColor(renderer, dark_gray.r, dark_gray.g, dark_gray.b, 255); // Nền slider
        SDL_RenderFillRect(renderer, &volume_slider_bg_rect);
        // Vẽ phần đã fill của slider (tùy chọn)
        SDL_Rect vol_fill_rect = volume_slider_bg_rect;
        vol_fill_rect.w = (int)(((float)volume / 100.0f) * volume_slider_bg_rect.w);
        SDL_SetRenderDrawColor(renderer, gray.r, gray.g, gray.b, 255);
        SDL_RenderFillRect(renderer, &vol_fill_rect);

        // Tính toán vị trí X của núm kéo
        float handle_pos_ratio = (float)volume / 100.0f;
        volume_handle_rect.x = volume_slider_bg_rect.x + (int)(handle_pos_ratio * (volume_slider_bg_rect.w - volume_handle_rect.w));
        // Giới hạn núm kéo trong phạm vi nền
        if (volume_handle_rect.x < volume_slider_bg_rect.x) volume_handle_rect.x = volume_slider_bg_rect.x;
        if (volume_handle_rect.x > volume_slider_bg_rect.x + volume_slider_bg_rect.w - volume_handle_rect.w) {
            volume_handle_rect.x = volume_slider_bg_rect.x + volume_slider_bg_rect.w - volume_handle_rect.w;
        }
        // Vẽ núm kéo
        SDL_Color current_handle_color = is_dragging_volume ? slider_handle_drag_color : slider_handle_color;
        SDL_SetRenderDrawColor(renderer, current_handle_color.r, current_handle_color.g, current_handle_color.b, 255);
        SDL_RenderFillRect(renderer, &volume_handle_rect);

        // Vẽ icon Volume tĩnh bên cạnh slider
        // SDL_RenderCopy(renderer, tex_volume, NULL, &volume_icon_rect); // Vẽ icon tĩnh nếu có
         draw_icon_button(renderer, &volume_icon_rect, tex_volume, BTN_NORMAL); // Hoặc dùng draw_icon_button

        // Vẽ text % volume
        char vol_info[10]; snprintf(vol_info, sizeof(vol_info), "%d%%", volume);
        draw_text_simple(renderer, font, vol_info, volume_text_rect.x, volume_text_rect.y, white);

        pthread_mutex_unlock(&playlist_mutex);

        // --- 5.4 Hiển thị kết quả lên màn hình ---
        SDL_RenderPresent(renderer);

        // --- 5.5 Delay nhỏ ---
        SDL_Delay(16); // ~60 FPS
    }

    cleanup();
    printf("Application finished normally.\n");
    return 0;
}