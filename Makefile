CC = gcc
CFLAGS = -Wall
LIBS = -lmad -lasound -ludev # Thêm -ludev
TARGET = mp3_player

SRCS = main.c usb_manager.c file_scanner.c mp3_decoder.c audio_output.c ui.c playback_controller.c error_handler.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean