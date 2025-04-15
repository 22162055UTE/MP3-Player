#include "file_scanner.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Thêm để dùng sprintf
#include "error_handler.h"

List* scan_files(const char* path) {
    List* list = malloc(sizeof(List));
    list->head = NULL;

    DIR* dir = opendir(path);
    if (!dir) {
        error_log(ERR_FILE, "Cannot open directory");
        free(list);
        return NULL;
    }

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".mp3")) {
            Node* node = malloc(sizeof(Node));
            node->file_path = malloc(strlen(path) + strlen(entry->d_name) + 2);
            sprintf(node->file_path, "%s/%s", path, entry->d_name);
            node->next = list->head;
            list->head = node;
        }
    }

    closedir(dir);
    return list;
}

void free_list(List* list) {
    Node* current = list->head;
    while (current) {
        Node* next = current->next;
        free(current->file_path);
        free(current);
        current = next;
    }
    free(list);
}