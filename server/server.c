#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <proto.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include "redis_songlist.h"
#include "server_conf.h"
#include "medialib.h"
#include "song.h"
#include "fsm.h"
#define MINSPARESERVER 5
#define MAXSPARESERVER 10
#define MAXCLIENTS 20
#define SIG_NOTIFY SIGUSR2
/*
 * -M    指定多播组
 * -P    指定接受端口
 * -F    前台运行
 * -D    指定媒体库位置
 * -H    显示帮助
 * -I    指定网络设备
 */
int serversd;
static struct server_st *serverpool;
static int idle_count = 0;
static int busy_count = 0;

enum
{
    STATE_IDEL = 0,
    STATE_BUSY
};

struct server_conf_st server_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .runmode = RUN_FOREGROUND,
};

struct server_st
{
    pid_t pid;
    int state;
};

static void printhelp(void)
{
    printf("-P    指定接受端口\n");
    printf("-F    前台运行\n");
    printf("-H    显示帮助\n");
}

static void daemon_exit(int s)
{

    syslog(LOG_WARNING, "signal-%d caught, exit now", s);
    munmap(serverpool, sizeof(struct server_st) * MAXCLIENTS);
    wait(NULL);
    closelog();
    exit(0);
}

static int daemonize(void)
{
    pid_t pid;
    int fd;
    pid = fork();
    if (pid < 0)
    {
        // perror("fork()");
        syslog(LOG_ERR, "fork():%s", strerror(errno)); // 不需要\n
        return -1;
    }
    if (pid > 0)
    {
        exit(0); // 父进程退出 子进程变成孤儿进程被1号进程接收
    }
    else
    {
        fd = open("/dev/null", O_RDWR);
        if (fd < 0)
        {
            // perror("open");
            syslog(LOG_WARNING, "open():%s", strerror(errno));
            return -2;
        }
        else
        {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
            {
                close(fd);
            }
        }

        setsid();   // 设置为守护进程
        chdir("/"); // 把守护进程的运行路径改到根目录下
        return 0;
    }
}

static void usr2_handler(int s)
{
    return;
}

static void server_job(int pos)
{
    pid_t ppid;
    struct sockaddr_in raddr;
    socklen_t raddr_len;
    int client_sd;
    raddr_len = sizeof(raddr);
    ppid = getppid();
    struct mlib_songentry_st *list = NULL;
    int list_size;
    while (1)
    {
        serverpool[pos].state = STATE_IDEL;
        kill(ppid, SIG_NOTIFY);
        client_sd = accept(serversd, (void *)&raddr, &raddr_len);
        if (client_sd < 0)
        {
            continue;
        }
        serverpool[pos].state = STATE_BUSY;
        kill(ppid, SIG_NOTIFY);

        redisContext *redis = redis_connect("127.0.0.1", 6379);
        if (!redis)
        {
            syslog(LOG_ERR, "Redis connection failed");
            close(client_sd);
            continue;
        }
        struct my_shared_list *retrieved_list = load_song_list_from_redis(redis);
        if (!retrieved_list)
        {
            syslog(LOG_ERR, "Failed to load song list from Redis");
            redis_disconnect(redis);
            close(client_sd);
            continue;
        }
        list_size = retrieved_list->list_size;
        list = retrieved_list->list;
        mlib_initsong();
        if (song_list_send(list, list_size, client_sd) < 0)
        {
            syslog(LOG_ERR, "song_list_send() failed");
            free(retrieved_list);
            redis_disconnect(redis);
            close(client_sd);
            continue;
        }

        fsm_t fsm;
        if (fsm_init(&fsm, client_sd, list, list_size) == -1)
        {
            syslog(LOG_ERR, "fsm_init() failed");
            free(retrieved_list);
            redis_disconnect(redis);
            close(client_sd);
            continue;
        }

        fsm_run(&fsm);
        fsm_destroy(&fsm);
        mlib_distroysong();
        free(retrieved_list);
        redis_disconnect(redis);
        close(client_sd);
    }
}

static int socket_init(void)
{
    struct sockaddr_in recvaddr;

    serversd = socket(AF_INET, SOCK_STREAM, 0);
    if (serversd < 0)
    {
        syslog(LOG_ERR, "socket():%s", strerror(errno));
        exit(1);
    }
    memset(&recvaddr, 0, sizeof(recvaddr));
    recvaddr.sin_family = AF_INET;
    recvaddr.sin_port = htons(atoi(server_conf.rcvport));
    recvaddr.sin_addr.s_addr = INADDR_ANY;

    int val = 1;
    if (setsockopt(serversd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        syslog(LOG_ERR, "setsockopt():%s", strerror(errno));

        exit(1);
    }

    if (bind(serversd, (void *)&recvaddr, sizeof(recvaddr)) < 0)
    {
        syslog(LOG_ERR, "bind():%s", strerror(errno));
        close(serversd);
        exit(1);
    }

    if (listen(serversd, SOMAXCONN) < 0)
    {
        syslog(LOG_ERR, "listen():%s", strerror(errno));
        close(serversd);
        exit(1);
    }

    return 0;
}

static int add_1_server(void)
{
    int slot;
    pid_t pid;
    if (idle_count + busy_count >= MAXCLIENTS)
    {
        return -1;
    }
    for (slot = 0; slot < MAXCLIENTS; slot++)
    {
        if (serverpool[slot].pid == -1)
        {
            break;
        }
    }
    serverpool[slot].state = STATE_IDEL;
    pid = fork();
    if (pid < 0)
    {
        syslog(LOG_ERR, "fork():%s", strerror(errno));
        exit(1);
    }
    else if (pid == 0)
    {
        server_job(slot);
        exit(0);
    }
    else
    {
        serverpool[slot].pid = pid;
        idle_count++;
    }
    return 0;
}

static int del_1_server(void)
{
    if (idle_count == 0)
        return -1;
    for (int i = 0; i < MAXCLIENTS; i++)
    {
        if (serverpool[i].pid != -1 && serverpool[i].state == STATE_IDEL)
        {
            kill(serverpool[i].pid, SIGTERM); // 终结进程
            serverpool[i].pid = -1;
            idle_count--;
            break;
        }
    }
    return 0;
}

static int scan_pool()
{
    int idle = 0, busy = 0;
    for (int i = 0; i < MAXCLIENTS; i++)
    {
        if (serverpool[i].pid == -1)
            continue;
        if (kill(serverpool[i].pid, 0)) // 检测进程是否存在
        {
            serverpool[i].pid = -1;
            continue;
        }
        if (serverpool[i].state == STATE_IDEL)
            idle++;
        else if (serverpool[i].state == STATE_BUSY)
            busy++;
        else
        {
            syslog(LOG_ERR, "serverpool.state is error");
            abort();
            //_exit(1);
        }
    }
    idle_count = idle;
    busy_count = busy;
    return 0;
}

int main(int argc, char *argv[])
{
    int c;
    int i;
    struct sigaction sa;
    int err;
    sigset_t set, oset;
    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTERM); // 当sigaction执行信号函数后 不希望被这些信号打断 这里在将这些信号设置为阻塞
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGQUIT);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    sa.sa_handler = SIG_IGN; // 把SIGCHLD信号忽略掉
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDWAIT;    // 阻止子进程变成僵尸状态 免去收尸过程
    sigaction(SIGCHLD, &sa, NULL); // 当子进程结束时 父进程会接受到SIGCHLD这个信号
    sa.sa_handler = usr2_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIG_NOTIFY, &sa, NULL);

    openlog("netradio", LOG_PID | LOG_PERROR, LOG_DAEMON); // 写日志 第一个参数是自己取得名字

    /*命令行分析*/
    while (1)
    {
        c = getopt(argc, argv, "P:FH");
        if (c < 0)
            break;
        switch (c)
        {
        case 'P':
            server_conf.rcvport = optarg;
            break;
        case 'F':
            server_conf.runmode = RUN_FOREGROUND;
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

    /*守护进程*/
    if (server_conf.runmode == RUN_DAEMON)
    {
        if (daemonize() != 0)
        {
            exit(1);
        }
    }

    else if (server_conf.runmode == RUN_FOREGROUND)
    {
        /*do nothing*/
    }
    else
    {
        // fprintf(stderr, "EINVAL\n");
        syslog(LOG_ERR, "EINVAL server_conf.runmode");
        exit(1);
    }

    sigemptyset(&set);
    sigaddset(&set, SIG_NOTIFY);
    sigprocmask(SIG_BLOCK, &set, &oset);

    serverpool = mmap(NULL, sizeof(struct server_st) * MAXCLIENTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (serverpool == MAP_FAILED)
    {
        perror("mmap()");
        exit(1);
    }
    for (i = 0; i < MAXCLIENTS; i++)
    {
        serverpool[i].pid = -1;
    }

    struct mlib_songentry_st *list = NULL;
    int list_size;
    err = mlib_getsonglist(&list, &list_size);
    if (err)
    {
        exit(1);
    }
    redisContext *redis = redis_connect("127.0.0.1", 6379); // 连接redis
    struct my_shared_list *mylist = malloc(sizeof(struct my_shared_list) + list_size * sizeof(struct mlib_songentry_st));
    mylist->list_size = list_size;
    memcpy(mylist->list, list, list_size * sizeof(struct mlib_songentry_st));

    save_song_list_to_redis(redis, mylist); // 把歌曲列表从mysql存入缓存中

    free(mylist);

    redis_disconnect(redis);

    mlib_freesonglist(list);

    socket_init();

    for (i = 0; i < MINSPARESERVER; i++)
    {
        add_1_server();
    }
    while (1)
    {
        sigsuspend(&oset); // 前面notify信号被阻塞住了 不会被打断 在这里把信号打开 可以被打断 但接收到信号后 又会阻塞

        scan_pool();

        if (idle_count > MAXSPARESERVER)
        {
            for (i = 0; i < (idle_count - MAXSPARESERVER); i++)
            {
                del_1_server();
            }
        }
        else if (idle_count < MINSPARESERVER)
        {
            for (i = 0; i < (MINSPARESERVER - idle_count); i++)
            {
                add_1_server();
            }
        }
        printf("busy %d, idle %d\n", busy_count, idle_count);
        // for (i = 0; i < MAXCLIENTS; i++)
        // {
        //     if (serverpool[i].pid == -1)
        //         putchar(' ');
        //     else if (serverpool[i].state == STATE_IDEL)
        //         putchar('.');
        //     else
        //         putchar('x');
        // }
        // putchar('\n');
    }

    exit(0);
}
