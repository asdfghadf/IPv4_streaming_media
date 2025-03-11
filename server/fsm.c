#include "fsm.h"
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <proto.h>
#include <arpa/inet.h>
#include "song.h"
#include "medialib.h"
struct msg_request_st *request;
struct mlib_songentry_st *list;
int list_size;
int curlist_id = -1;
// 初始化 FSM
int fsm_init(fsm_t *fsm, int client_sd, struct mlib_songentry_st *songentry, int num)
{
    list = songentry;
    list_size = num;
    request = malloc(MSG_REQUEST_MAX);
    if (request == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        return -1;
    }
    fsm->client_sd = client_sd;
    fsm->state = STATE_PAUSED;
    // 创建 epoll 实例
    fsm->epoll_fd = epoll_create1(0);
    if (fsm->epoll_fd == -1)
    {
        perror("epoll_create1()");
        return -1;
    }

    // 监听客户端 socket
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_sd;

    if (epoll_ctl(fsm->epoll_fd, EPOLL_CTL_ADD, client_sd, &ev) == -1)
    {
        perror("epoll_ctl()");
        close(fsm->epoll_fd);
        return -1;
    }

    return 0;
}

// 处理客户端控制指令
static void handle_control_command(fsm_t *fsm, int command)
{
    switch (command)
    {
    case CTRL_SELECT:
        printf("FSM: Received SELECT command\n");
        for (int i = 0; i < list_size; i++)
        {
            if (ntohs(request->song_id) == list[i].songid)
            {
                curlist_id = i;
                mlib_setsong(list[i].path);
                break;
            }
        }
        fsm->state = STATE_PLAYING;
        break;
    case CTRL_PAUSE:
        printf("FSM: Received PAUSE command\n");
        mlib_pause();
        fsm->state = STATE_PAUSED;
        break;
    case CTRL_RESUME:
        printf("FSM: Received RESUME command\n");
        mlib_resume();
        fsm->state = STATE_PLAYING;
        break;
    case CTRL_PREV:
        printf("FSM: Received PREVIOUS SONG command\n");
        curlist_id = curlist_id - 1 >= 0 ? curlist_id - 1 : list_size - 1;
        mlib_setsong(list[curlist_id].path);
        fsm->state = STATE_PREV;
        break;
    case CTRL_NEXT:
        printf("FSM: Received NEXT SONG command\n");
        curlist_id = (curlist_id + 1) % list_size;
        mlib_setsong(list[curlist_id].path);
        fsm->state = STATE_NEXT;
        break;
    case CTRL_STOP:
        printf("FSM: Received STOP command\n");
        fsm->state = STATE_STOPPED;
        break;
    default:
        printf("FSM: Unknown command: %d\n", command);
        break;
    }
}

// FSM 事件循环
void fsm_run(fsm_t *fsm)
{
    struct epoll_event events[10];

    while (fsm->state != STATE_STOPPED)
    {
        int nfds = epoll_wait(fsm->epoll_fd, events, 10, 0);
        if (nfds == -1)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait()");
            break;
        }

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == fsm->client_sd)
            {
                ssize_t bytes_read = song_recv(request, fsm->client_sd);
                if (bytes_read < 0)
                {
                    fsm->state = STATE_STOPPED;
                    printf("song_recv error\n");
                    break;
                }
                int command = request->type;
                printf("command %d\n", command);
                handle_control_command(fsm, command);
            }
        }

        // 根据状态执行播放控制
        if (fsm->state == STATE_PLAYING)
        {
            printf("FSM: Playing audio...\n");
            int len = song_data_send(list[curlist_id].songid, fsm->client_sd);
            if (len < 0)
            {
                fsm->state = STATE_STOPPED;
            }
            if (len == 0)
            {
                fsm->state = STATE_PAUSED;
            }
        }
        else if (fsm->state == STATE_PAUSED)
        {
            printf("FSM: Paused...\n");
            sleep(1);
        }
        else if (fsm->state == STATE_NEXT)
        {
            printf("FSM: Switching to next song...\n");
            fsm->state = STATE_PLAYING;
        }
        else if (fsm->state == STATE_PREV)
        {
            printf("FSM: Switching to previous song...\n");
            fsm->state = STATE_PLAYING;
        }
    }
}

// 释放 FSM 资源
void fsm_destroy(fsm_t *fsm)
{
    free(request);
    close(fsm->epoll_fd);
}
