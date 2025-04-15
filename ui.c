#include "ui.h"
#include "playback_controller.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static List* playlist;

void ui_init(List* pl) {
    playlist = pl;
    printf("Playlist:\n");
    Node* current = playlist->head;
    int i = 1;
    if (!current) {
        printf("No MP3 files found.\n");
    }
    while (current) {
        printf("%d. %s\n", i++, current->file_path);
        current = current->next;
    }
}

void ui_run(void) {
    char input[256];
    while (1) {
        printf("Enter command (play <n>, pause, resume, next, stop): ");
        if (!fgets(input, sizeof(input), stdin)) {
            continue;
        }
        input[strcspn(input, "\n")] = 0; // Xóa ký tự xuống dòng

        if (strcmp(input, "stop") == 0) {
            playback_stop();
            break;
        } else if (strncmp(input, "play ", 5) == 0) {
            int index;
            if (sscanf(input + 5, "%d", &index) == 1 && index > 0) {
                playback_play(index - 1);
            } else {
                printf("Invalid play command. Use: play <number>\n");
            }
        } else if (strcmp(input, "play") == 0) {
            playback_play(0); // Mặc định phát bài đầu tiên
        } else if (strcmp(input, "pause") == 0) {
            playback_pause();
        } else if (strcmp(input, "resume") == 0) {
            playback_resume();
        } else if (strcmp(input, "next") == 0) {
            playback_next();
        } else {
            printf("Unknown command\n");
        }
    }
}

void ui_cleanup(void) {
    printf("UI cleaned up\n");
}