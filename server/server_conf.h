#ifndef __SERVERCONF_H__
#define __SERVERCONF_H__

#include "medialib.h"
enum
{
    RUN_DAEMON = 1, // 后台运行
    RUN_FOREGROUND  // 前台运行
};

struct server_conf_st
{
    char *rcvport;
    char runmode;
};

struct my_shared_list
{
    int list_size;
    struct mlib_songentry_st list[0];
}; // redis缓存的类型

extern struct server_conf_st server_conf;
extern int serversd;
#endif