#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <proto.h>
#include "server_conf.h"
#include "medialib.h"
#include "song.h"
int song_list_send(struct mlib_songentry_st *listp, int num, int clientsd)
{
    int totalsize = 0;
    struct msg_songlist_st *entlistp; // 音乐总列表
    struct msg_songentry_st *entryp;  // 单首音乐
    int size;
    for (int i = 0; i < num; i++)
    {
        totalsize += sizeof(struct msg_songlist_st) + strlen(listp[i].name); // 加上每一条音乐名的大小
    }
    entlistp = malloc(totalsize);
    if (entlistp == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        return -1;
    }
    entryp = entlistp->song_entry;
    syslog(LOG_DEBUG, "nr_list_entn:%d\n", num);
    for (int i = 0; i < num; i++)
    {
        size = sizeof(struct msg_songentry_st) + strlen(listp[i].name); // 每个音乐名的大小

        entryp->song_id = htons(listp[i].songid);
        entryp->len = htons(size);
        strcpy(entryp->song_name, listp[i].name);
        entryp = (void *)(((char *)entryp) + size); // 指向下一个音乐的地址
        syslog(LOG_DEBUG, "entryp len:%d\n", entryp->len);
    }
    if (send(clientsd, entlistp, totalsize, 0) < 0)
    {
        syslog(LOG_ERR, "send():%s", strerror(errno));
        free(entlistp);
        return -1;
    }
    free(entlistp);
    return 0;
}

int song_recv(struct msg_request_st *request, int clientsd)
{
    size_t header_size = sizeof(request->type) + sizeof(request->song_id) + sizeof(request->len);
    size_t total = 0;

    // 清空 request 结构体，防止未初始化数据
    memset(request, 0, sizeof(struct msg_request_st));

    // 先接收 type、song_id 和 len 这三个字段
    while (total < header_size)
    {
        ssize_t ret = recv(clientsd, (char *)request + total, header_size - total, 0);
        if (ret < 0)
        {
            printf("recv header error: %s\n", strerror(errno));
            return -1;
        }
        else if (ret == 0)
        {
            printf("Connection closed by peer while receiving header\n");
            return -1;
        }
        total += ret;
    }

    // 解析歌曲名长度
    uint16_t name_len = ntohs(request->len);

    // 校验歌曲名长度
    if (name_len <= 0 || name_len >= MSG_REQUEST_MAX)
    {
        printf("Invalid song name length: %d\n", name_len);
        return -1;
    }

    // 计算整个消息包大小（头部 + 歌曲名）
    size_t expected = header_size + name_len;

    // 接收歌曲名部分（无论 type 是什么，都需要读取，否则下次读取数据会有问题）
    total = header_size;
    while (total < expected)
    {
        ssize_t ret = recv(clientsd, (char *)request + total, expected - total, 0);
        if (ret < 0)
        {
            printf("recv song_name error: %s\n", strerror(errno));
            return -1;
        }
        else if (ret == 0)
        {
            printf("Connection closed by peer while receiving song_name\n");
            return -1;
        }
        total += ret;
    }

    // 确保歌曲名称以 '\0' 结尾
    request->song_name[name_len - 1] = '\0';

    // 打印收到的包内容

    return 0;
}

int song_data_send(songid_t songid, int clientsd)
{
    struct msg_musicdata_st *sbufp;
    int len, packet_size, sent;
    int sent_total = 0;
    sbufp = malloc(MSG_MUSIC_MAX);
    if (sbufp == NULL)
    {
        syslog(LOG_ERR, "malloc(): %s", strerror(errno));
        return -1;
    }

    sbufp->song_id = htons(songid);

    // 读取一次音乐数据
    len = mlib_readsong(sbufp->music_data, MAX_MUSIC_DATA);
    if (len < 0)
    {
        syslog(LOG_ERR, "mlib_readsong() failed for song %d", songid);
        free(sbufp);
        return -1;
    }

    if (len == 0)
    {
        syslog(LOG_INFO, "Song %d playback complete", songid);
        free(sbufp);
        return 0; // 返回 0 表示歌曲已播放完
    }

    packet_size = sizeof(songid_t) + len;
    while (sent_total < packet_size)
    {
        int sent = send(clientsd, ((char *)sbufp) + sent_total, packet_size - sent_total, 0);
        if (sent <= 0)
        {
            if (sent == 0)
                syslog(LOG_INFO, "Client closed connection while sending song %d", songid);
            else
                syslog(LOG_ERR, "send error for song %d: %s", songid, strerror(errno));

            free(sbufp);
            return -1;
        }
        sent_total += sent;
    }

    syslog(LOG_INFO, "Sent %d bytes for song %d", sent_total, songid);
    free(sbufp);
    return sent_total; // 返回发送的字节数
}
