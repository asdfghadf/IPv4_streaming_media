#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "mytbf.h"

struct mytbf_st
{
    int cps;
    int cps_old;
    int burst;
    int token;
    int pos;
    pthread_mutex_t mut; // token的锁
    pthread_cond_t cond;
};

struct mytbf_st *job[MYTBF_MAX];
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER; // job数组的锁
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
pthread_t tid;

static int get_free_pos_unlocked(void)
{
    for (int i = 0; i < MYTBF_MAX; i++)
    {
        if (job[i] == NULL)
            return i;
    }
    return -1;
}

static void module_unload(void)
{
    pthread_cancel(tid);
    pthread_join(tid, NULL);
    for (int i = 0; i < MYTBF_MAX; i++)
    {
        free(job[i]);
        job[i] = NULL;
    }
    return;
}

static void *thr_alrm(void *p)
{
    while (1)
    {
        pthread_mutex_lock(&mut_job);
        for (int i = 0; i < MYTBF_MAX; i++)
        {
            if (job[i] != NULL)
            {
                pthread_mutex_lock(&job[i]->mut);
                job[i]->token += job[i]->cps;
                if (job[i]->token > job[i]->burst)
                {
                    job[i]->token = job[i]->burst;
                }
                pthread_cond_broadcast(&job[i]->cond);
                pthread_mutex_unlock(&job[i]->mut);
            }
        }
        pthread_mutex_unlock(&mut_job);

        sleep(1);
    }
}

static void module_load(void)
{
    int err;
    atexit(module_unload);
    err = pthread_create(&tid, NULL, thr_alrm, NULL);
    if (err)
    {
        fprintf(stderr, "pthread_create():%s", strerror(errno));
        exit(1);
    }
}

mytbf_t *mytbf_init(int cps, int burst)
{
    struct mytbf_st *me;
    int pos;

    pthread_once(&init_once, module_load);

    me = malloc(sizeof(*me));
    if (me == NULL)
    {
        return NULL;
    }

    me->cps = cps;
    me->cps_old = cps;
    me->burst = burst;
    me->token = 0;
    pthread_mutex_init(&me->mut, NULL);
    pthread_cond_init(&me->cond, NULL);

    pthread_mutex_lock(&mut_job);
    pos = get_free_pos_unlocked();
    if (pos < 0)
    {
        pthread_mutex_unlock(&mut_job);
        free(me);
        return NULL;
    }

    me->pos = pos;
    job[pos] = me;
    pthread_mutex_unlock(&mut_job);

    return me;
}

int min(int a, int b)
{
    return a > b ? b : a;
}

int mytbf_fetchtoken(mytbf_t *ptr, int size)
{
    struct mytbf_st *me = ptr;
    int n;
    pthread_mutex_lock(&me->mut);
    while (me->token <= 0)
    {
        pthread_cond_wait(&me->cond, &me->mut);
    }
    n = min(me->token, size);
    me->token -= n;
    pthread_mutex_unlock(&me->mut);
    return n;
}

int mytbf_returntoken(mytbf_t *ptr, int size)
{
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    me->token += size;
    if (me->token > me->burst)
        me->token = me->burst;
    pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);
    return 0;
}

int mytbf_destroy(mytbf_t *ptr)
{
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&mut_job);
    job[me->pos] = NULL;
    pthread_mutex_unlock(&mut_job);

    pthread_mutex_destroy(&me->mut);
    pthread_cond_destroy(&me->cond);
    free(me);
    return 0;
}

int mytbf_checktoken(mytbf_t *ptr)
{
    int token_left = 0;
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    token_left = me->token;
    pthread_mutex_unlock(&me->mut);
    return token_left;
}

int mytbf_pause(mytbf_t *ptr)
{
    struct mytbf_st *me = ptr;
    me->cps = 0;
}
int mytbf_resume(mytbf_t *ptr)
{
    struct mytbf_st *me = ptr;
    me->cps = me->cps_old;
}

void mytbf_setarg(mytbf_t *ptr, int cps, int burst)
{
    struct mytbf_st *me = ptr;
    me->token = 0;
    me->cps = cps;
    me->cps_old = cps;
    me->burst = burst;
}