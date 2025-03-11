#ifndef __MYTBF_H__
#define __MYTBF_H__

#define MYTBF_MAX 1024
typedef void mytbf_t;

mytbf_t *mytbf_init(int, int); // 速率和上限

int mytbf_fetchtoken(mytbf_t *, int); // 获得token

int mytbf_checktoken(mytbf_t *); // 查询token

int mytbf_returntoken(mytbf_t *, int); // 归还token

int mytbf_pause(mytbf_t *);  // 暂停token增长
int mytbf_resume(mytbf_t *); // 恢复token增长

int mytbf_destroy(mytbf_t *); // 销毁

void mytbf_setarg(mytbf_t *, int, int); // 设置速率和上限

#endif