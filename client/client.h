#ifndef __CLIENT_H__
#define __CLIENT_H__

#define DEFAULT_PLAYERCMD "/usr/bin/mpg321 - > /dev/null"

struct client_conf_st // 点播客户端的配置
{
    char *server_port; // 端口
    char *server_ip;   // ip
    char *player_cmd;  // 播放器选择
};

extern struct client_conf_st client_conf;

#endif