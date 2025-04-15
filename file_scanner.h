#ifndef FILE_SCANNER_H
#define FILE_SCANNER_H

typedef struct Node {
    char* file_path;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
} List;

List* scan_files(const char* path);
void free_list(List* list);

#endif