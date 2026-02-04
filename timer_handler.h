#ifndef TIMER_HANDLER_H
#define TIMER_HANDLER_H

#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

// 타이머 콜백 함수 타입
// tfd: 타이머 파일 디스크립터
// expirations: 만료 횟수
// user_data: 사용자 정의 데이터
typedef void (*timer_callback_t)(int tfd, uint64_t expirations, void *user_data);

// 타이머 정보 구조체
typedef struct {
    int tfd;                    // 타이머 파일 디스크립터
    timer_callback_t callback;   // 콜백 함수
    void *user_data;            // 사용자 정의 데이터
} timer_info_t;

// 타이머 핸들러 구조체
typedef struct {
    int epoll_fd;               // epoll 파일 디스크립터
    timer_info_t *timers;       // 타이머 배열
    int timer_count;            // 타이머 개수
    int timer_capacity;         // 타이머 배열 용량
    pthread_t thread_id;        // 스레드 ID
    int running;                // 실행 중 플래그
    pthread_mutex_t mutex;      // 뮤텍스 (스레드 안전성)
} timer_handler_t;

// 타이머 핸들러 생성
// 반환값: 성공 시 timer_handler_t 포인터, 실패 시 NULL
timer_handler_t* timer_handler_create(void);

// 타이머 추가
// handler: 타이머 핸들러
// clockid: 클럭 ID (CLOCK_REALTIME, CLOCK_MONOTONIC 등)
// initial_sec: 초기 만료까지 시간 (초)
// initial_nsec: 초기 만료까지 시간 (나노초)
// interval_sec: 반복 주기 (초)
// interval_nsec: 반복 주기 (나노초)
// callback: 콜백 함수
// user_data: 사용자 정의 데이터
// 반환값: 성공 시 타이머 파일 디스크립터, 실패 시 -1
int timer_handler_add_timer(timer_handler_t *handler,
                            int clockid,
                            time_t initial_sec, long initial_nsec,
                            time_t interval_sec, long interval_nsec,
                            timer_callback_t callback,
                            void *user_data);

// 타이머 제거
// handler: 타이머 핸들러
// tfd: 제거할 타이머 파일 디스크립터
// 반환값: 성공 시 0, 실패 시 -1
int timer_handler_remove_timer(timer_handler_t *handler, int tfd);

// 이벤트 루프 실행 (블로킹)
// handler: 타이머 핸들러
// timeout_ms: 타임아웃 (밀리초), -1이면 무한 대기
// 반환값: 처리된 이벤트 개수, 에러 시 -1
int timer_handler_run_once(timer_handler_t *handler, int timeout_ms);

// 이벤트 루프 실행 (무한 루프, 블로킹)
// handler: 타이머 핸들러
void timer_handler_run(timer_handler_t *handler);

// 스레드에서 타이머 핸들러 시작 (논블로킹)
// handler: 타이머 핸들러
// 반환값: 성공 시 0, 실패 시 -1
int timer_handler_start(timer_handler_t *handler);

// 스레드에서 실행 중인 타이머 핸들러 중지
// handler: 타이머 핸들러
// 반환값: 성공 시 0, 실패 시 -1
int timer_handler_stop(timer_handler_t *handler);

// 타이머 핸들러가 실행 중인지 확인
// handler: 타이머 핸들러
// 반환값: 실행 중이면 1, 아니면 0
int timer_handler_is_running(timer_handler_t *handler);

// 타이머 핸들러 정리 및 메모리 해제
// handler: 타이머 핸들러
void timer_handler_destroy(timer_handler_t *handler);

#endif // TIMER_HANDLER_H

