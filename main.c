#include "usb_manager.h"
#include "file_scanner.h"
#include "ui.h"
#include "playback_controller.h"
#include "error_handler.h"
#include <stdlib.h>
#include <stdio.h>

int main() {
    error_init();

    char* mount_path = NULL;
    if (!usb_init(&mount_path)) {
        error_log(ERR_USB, "Failed to initialize USB");
        error_cleanup();
        return 1;
    }

    printf("Using USB mount path: %s\n", mount_path);
    List* playlist = scan_files(mount_path);
    if (!playlist) {
        error_log(ERR_FILE, "No MP3 files found");
        free(mount_path);
        usb_cleanup();
        error_cleanup();
        return 1;
    }

    ui_init(playlist);
    playback_init(playlist);

    ui_run(); // Chạy vòng lặp giao diện

    playback_cleanup();
    ui_cleanup();
    free_list(playlist);
    free(mount_path);
    usb_cleanup();
    error_cleanup();

    return 0;
}