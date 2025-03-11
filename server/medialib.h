#ifndef __MEDIALIB_H__
#define __MEDIALIB_H__
#include <site_type.h>
struct mlib_songentry_st
{
    songid_t songid;
    char name[20];
    char singer[20];
    char path[50];
};

int mlib_getsonglist(struct mlib_songentry_st **, int *); // 从mysql中得到音乐列表 第二个参数是数量

int mlib_freesonglist(struct mlib_songentry_st *); // 释放音乐列表内存
int mlib_setsong(const char *path);                // 设置当前歌曲的fd
ssize_t mlib_readsong(void *, size_t);             // 读取歌曲

int mlib_initsong();    // 初始化song结构体
int mlib_distroysong(); // 主要释放令牌桶 关闭fd

void mlib_pause();  // 使令牌桶停止增加token
void mlib_resume(); // 恢复增加token

#endif