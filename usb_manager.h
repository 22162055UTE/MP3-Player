#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <stdbool.h>

bool usb_init(char** mount_path); // Trả về đường dẫn gắn
void usb_cleanup(void);

#endif