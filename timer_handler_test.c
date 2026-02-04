#include "timer_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

// 타이머 콜백 함수 예제
void my_timer_handler(int tfd __attribute__((unused)), uint64_t expirations, void *user_data)
{
    printf("Timer expired %lu times\n", (unsigned long)expirations);
    
    // user_data 사용 예제
    if (user_data) {
        int *counter = (int *)user_data;
        *counter += expirations;
        printf("Total count: %d\n", *counter);
    }
}

// 전역 변수로 핸들러 저장 (시그널 핸들러에서 접근)
static timer_handler_t *g_handler = NULL;

// 시그널 핸들러 (Ctrl+C 처리)
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        printf("\nShutting down timer handler...\n");
        if (g_handler) {
            if (timer_handler_is_running(g_handler)) {
                timer_handler_stop(g_handler);
            }
            timer_handler_destroy(g_handler);
        }
        exit(0);
    }
}

int main(int argc, char* argv[]) 
{
    // 시그널 핸들러 등록
    signal(SIGINT, signal_handler);

    // 타이머 핸들러 생성
    timer_handler_t *handler = timer_handler_create();
    if (!handler) {
        fprintf(stderr, "Failed to create timer handler\n");
        return 1;
    }
    g_handler = handler;

    // 사용자 데이터 예제
    int counter = 0;

    // 타이머 추가: 1초 후 시작, 3초마다 반복
    int tfd1 = timer_handler_add_timer(handler,
                                       CLOCK_REALTIME,
                                       1, 0,  // 1초 후 시작
                                       3, 0,  // 3초마다 반복
                                       my_timer_handler,
                                       &counter);
    if (tfd1 == -1) {
        fprintf(stderr, "Failed to add timer 1\n");
        timer_handler_destroy(handler);
        return 1;
    }

    // 두 번째 타이머 추가: 2초 후 시작, 5초마다 반복
    int tfd2 = timer_handler_add_timer(handler,
                                       CLOCK_REALTIME,
                                       2, 0,  // 2초 후 시작
                                       5, 0,  // 5초마다 반복
                                       my_timer_handler,
                                       NULL);
    if (tfd2 == -1) {
        fprintf(stderr, "Failed to add timer 2\n");
        timer_handler_destroy(handler);
        return 1;
    }

    printf("Timer handler started. Press Ctrl+C to exit.\n");
    printf("Timer 1: starts after 1s, repeats every 3s\n");
    printf("Timer 2: starts after 2s, repeats every 5s\n");

    // 스레드에서 타이머 핸들러 시작 (논블로킹)
    if (timer_handler_start(handler) != 0) {
        fprintf(stderr, "Failed to start timer handler thread\n");
        timer_handler_destroy(handler);
        return 1;
    }

    printf("Timer handler running in background thread.\n");
    printf("Main thread can do other work...\n");

    // 메인 스레드에서 다른 작업 수행 가능
    while (timer_handler_is_running(handler)) {
        sleep(1);
        // 여기서 다른 작업을 수행할 수 있습니다
        // 예: 네트워크 처리, 파일 I/O 등
    }

    // 여기까지 도달하지 않음 (Ctrl+C로 종료)
    timer_handler_destroy(handler);
    return 0;
}
