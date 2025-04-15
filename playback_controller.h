#ifndef PLAYBACK_CONTROLLER_H
#define PLAYBACK_CONTROLLER_H

#include "file_scanner.h"

void playback_init(List* playlist);
void playback_play(int index);
void playback_pause(void);
void playback_resume(void);
void playback_next(void);
void playback_stop(void); // Khai báo hàm stop
void playback_cleanup(void);

#endif