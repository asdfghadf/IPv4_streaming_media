#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <proto.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include "client.h"
#define MAX_EVENTS 1
#define MAX_SONG_NAME 256
struct client_conf_st client_conf = {
    .server_ip = DEFAULT_SERVERIP,
    .server_port = DEFAULT_RCVPORT,
    .player_cmd = DEFAULT_PLAYERCMD};
struct msg_songlist_st *msg_songlist;
static void printhelp(void)
{
    printf("-I --ip      指定服务器IP\n");
    printf("-P --port    指定服务器端口\n");
    printf("-p --player  指定播放器\n");
    printf("-H --help    显示帮助\n");
}

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

void clear_pipe(int fd)
{
    char buf[4096]; // 设定较大的缓冲区
    int bytes_available;

    // 设置非阻塞模式，防止 `read()` 阻塞
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // 获取管道内剩余数据大小
    if (ioctl(fd, FIONREAD, &bytes_available) == -1)
    {
        perror("ioctl() failed");
        return;
    }

    // 读取并丢弃所有数据
    while (bytes_available > 0)
    {
        int bytes_to_read = bytes_available > sizeof(buf) ? sizeof(buf) : bytes_available;
        int bytes_read = read(fd, buf, bytes_to_read);
        if (bytes_read <= 0)
            break;
        bytes_available -= bytes_read;
    }

    // 恢复原来的模式
    fcntl(fd, F_SETFL, flags);
}

/* 封装写函数 */
static ssize_t writen(int fd, const char *buf, size_t len)
{
    int ret;
    int pos = 0;
    while (len > 0)
    {
        ret = write(fd, buf + pos, len);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            perror("write()");
            return -1;
        }
        len -= ret;
        pos += ret;
    }
    return pos;
}

/* 全局暂停标志，volatile 保证多线程间可见 */
volatile pid_t child_pid = -1;
int recv_len;
songid_t chosen_id = MAXSONGID;
int cur_id = -1;
static int sd;
static pthread_t tid;
static int total = 0;
static int pd[2];

void sigint_handler(int sig)
{
    printf("\nCtrl+C detected! Stopping playback and exiting...\n");
    clear_pipe(pd[0]);
    // 1. 发送 CTRL_STOP 指令给服务器
    struct msg_request_st req;
    memset(&req, 0, sizeof(req));
    req.type = CTRL_STOP;
    req.song_id = 0;
    req.len = htons(1);
    req.song_name[0] = '\0';

    if (send(sd, &req, sizeof(req), 0) < 0)
    {
        perror("send CTRL_STOP");
    }
    else
    {
        printf("Sent CTRL_STOP to server.\n");
    }

    // 3. 终止控制线程
    printf("Terminating control thread...\n");
    pthread_cancel(tid);
    pthread_join(tid, NULL);

    // 2. 终止子进程（音乐播放器）
    if (child_pid > 0)
    {
        printf("Killing player process (PID: %d)...\n", child_pid);
        kill(child_pid, SIGKILL);
    }
    wait(NULL);
    close(pd[0]);
    close(pd[1]);
    // 4. 关闭 socket，清理资源
    close(sd);
    free(msg_songlist);
    // 5. 退出程序
    exit(0);
}

void *pause_thread(void *arg)
{
    int epoll_fd, nfds;
    struct epoll_event ev, events[MAX_EVENTS];
    char buf[MAX_SONG_NAME];
    // 创建 epoll 实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        perror("epoll_create1()");
        return NULL;
    }

    // 监听标准输入
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) == -1)
    {
        perror("epoll_ctl()");
        close(epoll_fd);
        return NULL;
    }

    printf("输入命令: play [歌曲名], pause, resume, next, prev\n");

    while (1)
    {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait()");
            break;
        }

        if (events[0].data.fd == STDIN_FILENO)
        {
            // 读取输入命令
            if (fgets(buf, sizeof(buf), stdin) == NULL)
                continue;
            buf[strcspn(buf, "\n")] = 0; // 去掉换行符

            struct msg_request_st *req;
            size_t req_size;

            // 处理不同的输入
            if (strcmp(buf, "pause") == 0 || strcmp(buf, "resume") == 0 ||
                strcmp(buf, "next") == 0 || strcmp(buf, "prev") == 0)
            {
                req_size = sizeof(struct msg_request_st);
                req = malloc(req_size);
                if (!req)
                {
                    perror("malloc()");
                    continue;
                }

                // 初始化结构体
                memset(req, 0, req_size);

                if (strcmp(buf, "pause") == 0)
                {
                    req->type = CTRL_PAUSE;
                    kill(-child_pid, SIGSTOP);
                }

                else if (strcmp(buf, "resume") == 0)
                {
                    req->type = CTRL_RESUME;
                    kill(-child_pid, SIGCONT);
                }

                else if (strcmp(buf, "next") == 0)
                {
                    clear_pipe(pd[0]);
                    req->type = CTRL_NEXT;
                    chosen_id = (cur_id + 1) % total;
                    cur_id = chosen_id;
                }
                else
                {
                    clear_pipe(pd[0]);
                    req->type = CTRL_PREV;
                    chosen_id = cur_id - 1 >= 0 ? cur_id - 1 : total - 1;
                    cur_id = chosen_id;
                }

                req->song_id = ntohs(0);
                req->len = ntohs(1);
                req->song_name[0] = '\0';
            }
            else if (strncmp(buf, "play ", 5) == 0) // 选择歌曲
            {
                clear_pipe(pd[0]);
                char *song_name = buf + 5;
                size_t name_len = strlen(song_name) + 1;
                req_size = sizeof(struct msg_request_st) - 1 + name_len;

                req = malloc(req_size);
                if (!req)
                {
                    perror("malloc()");
                    continue;
                }

                // 初始化结构体
                memset(req, 0, req_size);

                req->type = CTRL_SELECT;
                req->len = htons(name_len);
                strcpy((char *)req->song_name, song_name);

                // 假设 `msg_songlist` 里有歌曲列表，在这里进行匹配
                struct msg_songentry_st *pos = msg_songlist->song_entry;
                chosen_id = MAXSONGID;
                for (; (char *)pos < ((char *)msg_songlist + recv_len);
                     pos = (void *)(((char *)pos) + ntohs(pos->len)))
                {
                    if (strcmp((char *)req->song_name, (char *)pos->song_name) == 0)
                    {
                        chosen_id = ntohs(pos->song_id);
                        req->song_id = htons(chosen_id);
                        cur_id = chosen_id;
                        break;
                    }
                }

                if (chosen_id == MAXSONGID)
                {
                    printf("歌曲未找到: %s\n", song_name);
                    free(req);
                    continue;
                }

                printf("已选择歌曲: %s (ID: %d)\n", song_name, chosen_id);
            }
            else
            {
                printf("未知命令: %s\n", buf);
                continue;
            }

            // 发送请求到服务器
            if (send(sd, req, req_size, 0) < 0)
            {
                perror("send()");
                free(req);
                continue;
            }

            printf("已发送请求: %d\n", req->type);
            free(req);
        }
    }

    close(epoll_fd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    int c;
    pid_t pid;
    struct sockaddr_in serveraddr;
    struct msg_request_st *req;
    struct msg_musicdata_st *msg_musicdata;
    int len;
    struct option argarr[] = {
        {"ip", 1, NULL, 'I'},
        {"port", 1, NULL, 'P'},
        {"player", 1, NULL, 'p'},
        {"help", 1, NULL, 'H'},
        {NULL, 0, NULL, 0}};

    while (1)
    {
        c = getopt_long(argc, argv, "I:P:p:H", argarr, NULL);
        if (c < 0)
            break;
        switch (c)
        {
        case 'I':
            client_conf.server_ip = optarg;
            break;
        case 'P':
            client_conf.server_port = optarg;
            break;
        case 'p':
            client_conf.player_cmd = optarg;
            break;
        case 'H':
            printhelp();
            exit(0);
            break;
        default:
            abort();
            break;
        }
    }
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0)
    {
        perror("socket()");
        exit(1);
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(atoi(client_conf.server_port));
    inet_pton(AF_INET, client_conf.server_ip, &serveraddr.sin_addr);

    if (connect(sd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        perror("connect()");
        close(sd);
        exit(1);
    }
    signal(SIGINT, sigint_handler);

    /* 获取歌曲列表 */
    msg_songlist = malloc(MSG_SONGLIST_MAX);
    if (msg_songlist == NULL)
    {
        perror("malloc()");
        close(sd);
        exit(1);
    }
    recv_len = recv(sd, msg_songlist, MSG_SONGLIST_MAX, 0);
    if (recv_len <= 0)
    {
        fprintf(stderr, "Failed to receive song list\n");
        free(msg_songlist);
        close(sd);
        exit(1);
    }

    /* 打印歌曲列表 */
    struct msg_songentry_st *pos = msg_songlist->song_entry;
    for (; (char *)pos < ((char *)msg_songlist + recv_len);
         pos = (void *)(((char *)pos) + ntohs(pos->len)))
    {
        printf("song %d: %s\n", ntohs(pos->song_id), pos->song_name);
        total++;
    }

    /* 创建管道用于子进程通信 */
    if (pipe(pd) < 0)
    {
        perror("pipe()");
        close(sd);
        exit(1);
    }

    pid = fork();
    if (pid < 0)
    {
        perror("fork()");
        close(sd);
        close(pd[0]);
        close(pd[1]);
        exit(1);
    }
    else if (pid == 0) // 子进程：调用解码器
    {
        setpgid(0, 0);
        close(sd);
        close(pd[1]);
        dup2(pd[0], 0);
        if (pd[0] > 0)
            close(pd[0]);
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
        perror("execl()");
        exit(1);
    }
    else // 父进程：接收音乐数据并发送给子进程，同时支持暂停
    {

        child_pid = pid;
        msg_musicdata = malloc(MSG_MUSIC_MAX);
        if (msg_musicdata == NULL)
        {
            perror("malloc()");
            close(sd);
            close(pd[1]);
            exit(1);
        }
        /* 创建控制线程，用于监听用户的指令 */
        if (pthread_create(&tid, NULL, pause_thread, NULL) != 0)
        {
            perror("pthread_create");
            free(msg_musicdata);
            close(sd);
            close(pd[1]);
            exit(1);
        }
        while (1)
        {

            len = recv(sd, msg_musicdata, MSG_MUSIC_MAX, 0);
            if (len <= 0)
            {
                perror("recv");
                break;
            }

            /* 假设 msg_musicdata->song_id 是网络字节序 */
            if (ntohs(msg_musicdata->song_id) == cur_id)
            {
                /* 注意：len 中包含 song_id 部分，减去 sizeof(uint16_t) */
                if (writen(pd[1], msg_musicdata->music_data, len - sizeof(uint16_t)) < 0)
                {
                    break;
                }
            }
        }
        free(msg_musicdata);
        close(sd);
        close(pd[0]);

        close(pd[1]);
        wait(NULL);
        exit(0);
    }
}
