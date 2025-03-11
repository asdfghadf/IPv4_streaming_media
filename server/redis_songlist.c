#include "redis_songlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include <cjson/cJSON.h>

// 连接 Redis
redisContext *redis_connect(const char *host, int port)
{
    redisContext *redis = redisConnect(host, port);
    if (redis == NULL || redis->err)
    {
        fprintf(stderr, "Redis connection error: %s\n", redis->err ? redis->errstr : "Unknown");
        return NULL;
    }
    return redis;
}

// 断开 Redis
void redis_disconnect(redisContext *redis)
{
    if (redis)
        redisFree(redis);
}

// 序列化歌曲列表为 JSON
char *serialize_song_list(struct my_shared_list *list)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "list_size", list->list_size);

    cJSON *songs = cJSON_CreateArray();
    for (int i = 0; i < list->list_size; i++)
    {
        cJSON *song = cJSON_CreateObject();
        cJSON_AddNumberToObject(song, "songid", list->list[i].songid);
        cJSON_AddStringToObject(song, "name", list->list[i].name);
        cJSON_AddStringToObject(song, "singer", list->list[i].singer);
        cJSON_AddStringToObject(song, "path", list->list[i].path);
        cJSON_AddItemToArray(songs, song);
    }
    cJSON_AddItemToObject(root, "songs", songs);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// 反序列化 JSON 为歌曲列表
struct my_shared_list *deserialize_song_list(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root)
    {
        fprintf(stderr, "JSON parsing error\n");
        return NULL;
    }

    int list_size = cJSON_GetObjectItem(root, "list_size")->valueint;
    struct my_shared_list *list = malloc(sizeof(struct my_shared_list) + list_size * sizeof(struct mlib_songentry_st));
    list->list_size = list_size;

    cJSON *songs = cJSON_GetObjectItem(root, "songs");
    for (int i = 0; i < list_size; i++)
    {
        cJSON *song = cJSON_GetArrayItem(songs, i);
        list->list[i].songid = cJSON_GetObjectItem(song, "songid")->valueint;
        strcpy(list->list[i].name, cJSON_GetObjectItem(song, "name")->valuestring);
        strcpy(list->list[i].singer, cJSON_GetObjectItem(song, "singer")->valuestring);
        strcpy(list->list[i].path, cJSON_GetObjectItem(song, "path")->valuestring);
    }

    cJSON_Delete(root);
    return list;
}

// 存储歌曲列表到 Redis
int save_song_list_to_redis(redisContext *redis, struct my_shared_list *list)
{
    if (!redis)
        return -1;

    char *json_data = serialize_song_list(list);
    redisReply *reply = redisCommand(redis, "SET songlist %s", json_data);
    free(json_data);

    if (!reply)
    {
        fprintf(stderr, "Redis SET error: %s\n", redis->errstr);
        return -1;
    }

    freeReplyObject(reply);
    return 0;
}

// 从 Redis 读取歌曲列表
struct my_shared_list *load_song_list_from_redis(redisContext *redis)
{
    if (!redis)
        return NULL;

    redisReply *reply = redisCommand(redis, "GET songlist");
    if (!reply || reply->type != REDIS_REPLY_STRING)
    {
        fprintf(stderr, "Redis GET error or no data found\n");
        return NULL;
    }

    struct my_shared_list *list = deserialize_song_list(reply->str);
    freeReplyObject(reply);
    return list;
}
