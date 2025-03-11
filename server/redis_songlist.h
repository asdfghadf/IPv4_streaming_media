#ifndef REDIS_SONGLIST_H
#define REDIS_SONGLIST_H

#include <hiredis/hiredis.h>
#include "server_conf.h"
#include "medialib.h"

// 连接 Redis
redisContext *redis_connect(const char *host, int port);
void redis_disconnect(redisContext *redis);

// 存储 / 读取歌曲列表
int save_song_list_to_redis(redisContext *redis, struct my_shared_list *list);
struct my_shared_list *load_song_list_from_redis(redisContext *redis);

// JSON 序列化 / 反序列化
char *serialize_song_list(struct my_shared_list *list);
struct my_shared_list *deserialize_song_list(const char *json_str);

#endif
