CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread 

# 타겟
TARGET = timer_handler_test
OBJS = timer_handler.o timer_handler_test.o

# 기본 타겟
all: $(TARGET)

# 실행 파일 빌드
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# 오브젝트 파일 빌드
timer_handler.o: timer_handler.c timer_handler.h
	$(CC) $(CFLAGS) -c timer_handler.c

timer_handler_test.o: timer_handler_test.c timer_handler.h
	$(CC) $(CFLAGS) -c timer_handler_test.c

# 정리
clean:
	rm -f $(OBJS) $(TARGET)

# 재빌드
rebuild: clean all

.PHONY: all clean rebuild

