#ifndef FSM_H
#define FSM_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "medialib.h"
// FSM 状态定义
typedef enum
{
    STATE_PLAYING, // 发送音频数据
    STATE_PAUSED,  // 暂停播放
    STATE_NEXT,    // 切换到下一首
    STATE_PREV,    // 切换到上一首
    STATE_STOPPED  // 停止播放
} server_state_t;

// FSM 结构体
typedef struct
{
    int client_sd;        // 客户端 socket
    int epoll_fd;         // epoll 实例
    server_state_t state; // 当前状态
} fsm_t;

// 初始化 FSM
int fsm_init(fsm_t *fsm, int client_sd, struct mlib_songentry_st *songentry, int num);

// 运行 FSM 事件循环
void fsm_run(fsm_t *fsm);

// 释放 FSM 资源
void fsm_destroy(fsm_t *fsm);

#endif // FSM_H
