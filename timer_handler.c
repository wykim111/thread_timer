#include "timer_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define INITIAL_CAPACITY 8

// 파일 디스크립터를 논블로킹 모드로 설정
static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        return -1;
    }
    return 0;
}

// 타이머 핸들러 생성
timer_handler_t* timer_handler_create(void)
{
    timer_handler_t *handler = malloc(sizeof(timer_handler_t));
    if (!handler) {
        return NULL;
    }

    handler->epoll_fd = epoll_create1(0);
    if (handler->epoll_fd == -1) {
        free(handler);
        return NULL;
    }

    handler->timer_count = 0;
    handler->timer_capacity = INITIAL_CAPACITY;
    handler->timers = malloc(sizeof(timer_info_t) * handler->timer_capacity);
    if (!handler->timers) {
        close(handler->epoll_fd);
        free(handler);
        return NULL;
    }

    handler->running = 0;
    if (pthread_mutex_init(&handler->mutex, NULL) != 0) {
        close(handler->epoll_fd);
        free(handler->timers);
        free(handler);
        return NULL;
    }

    return handler;
}

// 타이머 추가
int timer_handler_add_timer(timer_handler_t *handler,
                            int clockid,
                            time_t initial_sec, long initial_nsec,
                            time_t interval_sec, long interval_nsec,
                            timer_callback_t callback,
                            void *user_data)
{
    if (!handler || !callback) {
        return -1;
    }

    pthread_mutex_lock(&handler->mutex);

    // 타이머 생성
    int tfd = timerfd_create(clockid, 0);
    if (tfd == -1) {
        return -1;
    }

    // 논블로킹 모드 설정
    if (set_nonblock(tfd) == -1) {
        close(tfd);
        return -1;
    }

    // 타이머 설정
    struct itimerspec ts;
    ts.it_value.tv_sec = initial_sec;
    ts.it_value.tv_nsec = initial_nsec;
    ts.it_interval.tv_sec = interval_sec;
    ts.it_interval.tv_nsec = interval_nsec;

    if (timerfd_settime(tfd, 0, &ts, NULL) == -1) {
        close(tfd);
        return -1;
    }

    // 배열 확장 필요 시
    if (handler->timer_count >= handler->timer_capacity) {
        int new_capacity = handler->timer_capacity * 2;
        timer_info_t *new_timers = realloc(handler->timers, 
                                           sizeof(timer_info_t) * new_capacity);
        if (!new_timers) {
            close(tfd);
            return -1;
        }
        handler->timers = new_timers;
        handler->timer_capacity = new_capacity;
    }

    // epoll에 등록
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = tfd;
    if (epoll_ctl(handler->epoll_fd, EPOLL_CTL_ADD, tfd, &ev) == -1) {
        close(tfd);
        return -1;
    }

    // 타이머 정보 저장
    handler->timers[handler->timer_count].tfd = tfd;
    handler->timers[handler->timer_count].callback = callback;
    handler->timers[handler->timer_count].user_data = user_data;
    handler->timer_count++;

    pthread_mutex_unlock(&handler->mutex);
    return tfd;
}

// 타이머 제거
int timer_handler_remove_timer(timer_handler_t *handler, int tfd)
{
    if (!handler) {
        return -1;
    }

    pthread_mutex_lock(&handler->mutex);

    // epoll에서 제거
    if (epoll_ctl(handler->epoll_fd, EPOLL_CTL_DEL, tfd, NULL) == -1) {
        pthread_mutex_unlock(&handler->mutex);
        return -1;
    }

    // 배열에서 찾아서 제거
    int found = 0;
    for (int i = 0; i < handler->timer_count; i++) {
        if (handler->timers[i].tfd == tfd) {
            close(tfd);
            // 마지막 요소로 이동
            if (i < handler->timer_count - 1) {
                handler->timers[i] = handler->timers[handler->timer_count - 1];
            }
            handler->timer_count--;
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&handler->mutex);
    return found ? 0 : -1;
}

// 이벤트 루프 실행 (한 번)
int timer_handler_run_once(timer_handler_t *handler, int timeout_ms)
{
    if (!handler) {
        return -1;
    }

    struct epoll_event events[handler->timer_count];
    int nfds = epoll_wait(handler->epoll_fd, events, handler->timer_count, timeout_ms);
    
    if (nfds == -1) {
        return -1;
    }

    // 각 이벤트 처리
    for (int i = 0; i < nfds; i++) {
        int tfd = events[i].data.fd;
        
        // 타이머 정보 찾기 (뮤텍스로 보호)
        timer_info_t *timer_info = NULL;
        pthread_mutex_lock(&handler->mutex);
        for (int j = 0; j < handler->timer_count; j++) {
            if (handler->timers[j].tfd == tfd) {
                timer_info = &handler->timers[j];
                break;
            }
        }
        pthread_mutex_unlock(&handler->mutex);

        if (timer_info && timer_info->callback) {
            // 타이머 만료 횟수 읽기
            uint64_t expirations;
            ssize_t s = read(tfd, &expirations, sizeof(uint64_t));
            if (s > 0) {
                timer_info->callback(tfd, expirations, timer_info->user_data);
            }
        }
    }

    return nfds;
}

// 이벤트 루프 실행 (무한 루프)
void timer_handler_run(timer_handler_t *handler)
{
    if (!handler) {
        return;
    }

    pthread_mutex_lock(&handler->mutex);
    handler->running = 1;
    pthread_mutex_unlock(&handler->mutex);

    while (1) {
        pthread_mutex_lock(&handler->mutex);
        int running = handler->running;
        pthread_mutex_unlock(&handler->mutex);

        if (!running) {
            break;
        }

        timer_handler_run_once(handler, 100); // 100ms 타임아웃으로 running 체크 가능하게
    }
}

// 스레드 함수
static void* timer_handler_thread_func(void *arg)
{
    timer_handler_t *handler = (timer_handler_t *)arg;
    timer_handler_run(handler);
    return NULL;
}

// 스레드에서 타이머 핸들러 시작
int timer_handler_start(timer_handler_t *handler)
{
    if (!handler) {
        return -1;
    }

    pthread_mutex_lock(&handler->mutex);
    if (handler->running) {
        pthread_mutex_unlock(&handler->mutex);
        return -1; // 이미 실행 중
    }
    handler->running = 1;
    pthread_mutex_unlock(&handler->mutex);

    if (pthread_create(&handler->thread_id, NULL, timer_handler_thread_func, handler) != 0) {
        pthread_mutex_lock(&handler->mutex);
        handler->running = 0;
        pthread_mutex_unlock(&handler->mutex);
        return -1;
    }

    return 0;
}

// 스레드에서 실행 중인 타이머 핸들러 중지
int timer_handler_stop(timer_handler_t *handler)
{
    if (!handler) {
        return -1;
    }

    pthread_mutex_lock(&handler->mutex);
    if (!handler->running) {
        pthread_mutex_unlock(&handler->mutex);
        return -1; // 실행 중이 아님
    }
    handler->running = 0;
    pthread_mutex_unlock(&handler->mutex);

    // 스레드 종료 대기
    pthread_join(handler->thread_id, NULL);

    return 0;
}

// 타이머 핸들러가 실행 중인지 확인
int timer_handler_is_running(timer_handler_t *handler)
{
    if (!handler) {
        return 0;
    }

    pthread_mutex_lock(&handler->mutex);
    int running = handler->running;
    pthread_mutex_unlock(&handler->mutex);

    return running;
}

// 타이머 핸들러 정리 및 메모리 해제
void timer_handler_destroy(timer_handler_t *handler)
{
    if (!handler) {
        return;
    }

    // 실행 중이면 중지
    if (timer_handler_is_running(handler)) {
        timer_handler_stop(handler);
    }

    // 모든 타이머 제거
    for (int i = 0; i < handler->timer_count; i++) {
        epoll_ctl(handler->epoll_fd, EPOLL_CTL_DEL, handler->timers[i].tfd, NULL);
        close(handler->timers[i].tfd);
    }

    // 리소스 해제
    close(handler->epoll_fd);
    pthread_mutex_destroy(&handler->mutex);
    free(handler->timers);
    free(handler);
}

