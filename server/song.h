#ifndef __SONG_H__
#define __SONG_H__
#include "medialib.h"
int song_list_send(struct mlib_songentry_st *, int, int); // 发送音乐列表

int song_recv(struct msg_request_st *, int); // 接受请求

int song_data_send(songid_t, int); // 发送音乐数据

#endif