#ifndef __PROTO_H__
#define __PROTO_H__
#include <site_type.h>
#define DEFAULT_RCVPORT "8888"       // 端口
#define DEFAULT_SERVERIP "127.0.0.1" // 点播客户端ip

#define SONGR 100 // 歌曲数量

#define MINSONGID 0                   // 歌曲最小id
#define MAXSONGID (MINSONGID + SONGR) // 歌曲最大id

// 以下用于点播
typedef enum
{
    CTRL_STOP,   // 停止
    CTRL_PAUSE,  // 暂停
    CTRL_RESUME, // 恢复播放
    CTRL_PREV,   // 上一首
    CTRL_NEXT,   // 下一首
    CTRL_SELECT  // 选择歌曲
} ControlCommand;

// 歌曲列表结构体
#define MSG_SONGLIST_MAX (65536 - 20 - 8) // 歌曲列表结构体最大长度
#define MAX_SONGENTRY (MSG_SONGLIST_MAX)  // 歌曲列表entry最大长度

struct msg_songentry_st // 歌曲列表每条数据的结构体
{
    songid_t song_id;
    uint16_t len;
    uint8_t song_name[1];
} __attribute__((packed));

struct msg_songlist_st // 歌曲列表结构体
{
    struct msg_songentry_st song_entry[1];
} __attribute__((packed));

// 客户端请求播放的歌曲名字结构体
#define MSG_REQUEST_MAX (65536 - 20 - 8) // 请求消息的最大长度
struct msg_request_st                    // 客户端请求的结构体
{
    uint8_t type;
    songid_t song_id;
    uint16_t len;
    uint8_t song_name[1];
} __attribute__((packed));

// 音乐数据结构体
#define MSG_MUSIC_MAX (65536 - 20 - 8) // 音乐数据结构体最大长度
#define MAX_MUSIC_DATA (MSG_MUSIC_MAX - sizeof(songid_t))
struct msg_musicdata_st // 音乐数据结构体
{
    songid_t song_id;
    uint8_t music_data[1];
} __attribute__((packed));

#endif