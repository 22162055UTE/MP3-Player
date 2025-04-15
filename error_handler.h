#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

typedef enum {
    ERR_NONE = 0,
    ERR_FILE,
    ERR_USB,
    ERR_AUDIO,
    ERR_MEMORY // Thêm lỗi bộ nhớ
} ErrorCode;

void error_init(void);
void error_log(ErrorCode code, const char* message);
void error_cleanup(void);

#endif