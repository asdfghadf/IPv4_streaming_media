#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <proto.h>
#include <glob.h>
#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mysql/mysql.h>
#include <errno.h>
#include "medialib.h"
#include "mytbf.h"
#include "server_conf.h"
#include "mp3_parser.h"
#define PATHSIZE 1024
#define LINEBUFSIZE 1024
#define MP3_BITRATE 128 * 1024 // correct bps:128*1024
songid_t curr_id;
struct song_context_st
{
    int fd;       // 某首歌的fd
    off_t offset; // 当前播放到哪首歌的哪个位置
    mytbf_t *tbf; // 令牌桶
};
static struct song_context_st song; // 当前要播放的歌曲
int mlib_getsonglist(struct mlib_songentry_st **result, int *num)
{
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL)
    {
        fprintf(stderr, "mysql_init() failed\n");
        return -1;
    }

    if (mysql_real_connect(conn, "localhost", "root", "123456", "music", 0, NULL, 0) == NULL)
    {
        fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    const char *query = "SELECT id, name, singer, path FROM entry";
    if (mysql_query(conn, query))
    {
        fprintf(stderr, "QUERY FAILED: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL)
    {
        fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    int num_rows = mysql_num_rows(res); // 获取查询到的行数
    if (num_rows <= 0)
    {
        fprintf(stderr, "No data found\n");
        mysql_free_result(res);
        mysql_close(conn);
        return -1;
    }

    *num = num_rows; // 赋值行数

    // 动态分配结构体数组
    struct mlib_songentry_st *songs = (struct mlib_songentry_st *)malloc(num_rows * sizeof(struct mlib_songentry_st));
    if (songs == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        mysql_free_result(res);
        mysql_close(conn);
        return -1;
    }

    MYSQL_ROW row;
    int index = 0;
    while ((row = mysql_fetch_row(res)) && index < num_rows)
    {
        songs[index].songid = atoi(row[0]) - 1; // 转换 songid_t 类型
        strncpy(songs[index].name, row[1], sizeof(songs[index].name) - 1);
        strncpy(songs[index].singer, row[2], sizeof(songs[index].singer) - 1);
        strncpy(songs[index].path, row[3], sizeof(songs[index].path) - 1);

        // 确保字符串以 null 结尾
        songs[index].name[sizeof(songs[index].name) - 1] = '\0';
        songs[index].singer[sizeof(songs[index].singer) - 1] = '\0';
        songs[index].path[sizeof(songs[index].path) - 1] = '\0';

        index++;
    }

    mysql_free_result(res);
    mysql_close(conn);

    *result = songs; // 让 `result` 指向动态分配的数据

    return 0; // 成功返回 0
}
int mlib_freesonglist(struct mlib_songentry_st *ptr)
{
    free(ptr);
    return 0;
}

int mlib_initsong()
{
    song.fd = -1;
    song.offset = 0;
    song.tbf = mytbf_init(0, 0);
    if (song.tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init()");
        return -1;
    }
}
int mlib_distroysong()
{
    if (song.fd >= 0)
        close(song.fd);
    mytbf_destroy(song.tbf);
}
int mlib_setsong(const char *path)
{

    // path/desc.text  path/*.mp3
    syslog(LOG_INFO, "current path :%s\n", path);
    char pathstr[PATHSIZE] = {'\0'};
    char linebuf[LINEBUFSIZE];
    int bitrate;

    /*流量控制*/
    bitrate = get_mp3_bitrate(path); // 根据歌曲的比特率调整流速

    mytbf_setarg(song.tbf, bitrate * 1024 / 8, bitrate * 1024 / 8 * 10);

    song.offset = 0;
    if (song.fd >= 0)
        close(song.fd);
    song.fd = open(path, O_RDONLY);
    if (song.fd < 0)
    {
        syslog(LOG_INFO, "%s open failed.", path);
        return -1;
    }
    return 0;
}

ssize_t mlib_readsong(void *buf, size_t size)
{
    int tbfsize;
    int len;
    // get token number
    tbfsize = mytbf_fetchtoken(song.tbf, size);
    syslog(LOG_INFO, "current tbf():%d", mytbf_checktoken(song.tbf));

    len = pread(song.fd, buf, tbfsize, song.offset);
    /*current song open failed*/
    if (len < 0)
    {
        // 当前这首歌可能有问题
        syslog(LOG_DEBUG, "song is error");
    }
    else if (len == 0)
    {
        syslog(LOG_DEBUG, "song is over");
    }
    else /*len > 0*/ // 真正读取到了数据
    {
        song.offset += len;
        syslog(LOG_DEBUG, "epoch : %f", (song.offset) / (16 * 1000 * 1.024));
    }
    // remain some token
    if (len >= 0 && tbfsize - len > 0)
        mytbf_returntoken(song.tbf, tbfsize - len);
    return len; // 返回读取到的长度
}

void mlib_pause()
{
    mytbf_pause(song.tbf);
}
void mlib_resume()
{
    mytbf_resume(song.tbf);
}
