#include "error_handler.h"
#include <stdio.h>

void error_init(void) {
    printf("Error handler initialized\n");
}

void error_log(ErrorCode code, const char* message) {
    printf("Error %d: %s\n", code, message);
}

void error_cleanup(void) {
    printf("Error handler cleaned up\n");
}