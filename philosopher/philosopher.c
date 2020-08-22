/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-11 00:46:25
 * @LastEditors: zero
 * @LastEditTime: 2019-12-11 18:44:43
 * @Description: 哲学家进餐问题   
 * @FilePath: /os/philosopher/philosopher.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

//哲学家数量
#define N 6

pthread_mutex_t mutexChopsticks[N];
int noodles;
pthread_mutex_t mutexNoodles;

/**
 * @description:用于获取[a,b]之间随机数 
 * @param {int} 下界
 * @param {int} 上界
 * @return: 
 */
int getRndNum(int a, int b)
{
    return (rand() % (b - a + 1)) + a;
}

/**
 * @description: 用于初始化
 * @return: 是否成功
 */
bool init()
{
    //面条总数
    noodles = 100;
    for (int i = 0; i < N; i++)
    {
        if (pthread_mutex_init(mutexChopsticks + i, NULL) != 0)
        {
            // 互斥锁初始化失败
            return false;
        }
    }
    if (pthread_mutex_init(&mutexNoodles, NULL) != 0)
    {
        // 互斥锁初始化失败
        return false;
    }
    return true;
}

void *philosopher(void *id)
{
    int philosopherId = *((int *)id);
    int ated = 0;
    while (true)
    {
        bool flag = false;
        int lId = philosopherId;
        int rId = (philosopherId + 1) % N;
        if (lId > rId)
        {
            int tmp = lId;
            lId = rId;
            rId = tmp;
        }
        if (pthread_mutex_lock(&mutexChopsticks[lId]) != 0)
        {
            fprintf(stdout, "lock error!\n");
        }
        if (pthread_mutex_lock(&mutexChopsticks[rId]) != 0)
        {
            fprintf(stdout, "lock error!\n");
        }
        if (pthread_mutex_lock(&mutexNoodles) != 0)
        {
            fprintf(stdout, "lock error!\n");
        }
        if (noodles > 0)
        {
            usleep(getRndNum(10000, 50000));
            noodles--;
            printf("哲学家#%d吃了一根面条，剩余面条%d根。\n", philosopherId, noodles);
            ated++;
        }
        else
        {
            flag = true;
        }
        pthread_mutex_unlock(&mutexNoodles);
        pthread_mutex_unlock(&mutexChopsticks[rId]);
        pthread_mutex_unlock(&mutexChopsticks[lId]);
        if (flag)
        {
            break;
        }
        usleep(getRndNum(10000, 50000));
    }
    printf("哲学家#%d一共吃了%d根面条。\n", philosopherId, ated);
}

int main()
{
    if (!init())
    {
        return 1;
    }
    pthread_t *pt = (pthread_t *)malloc(sizeof(pthread_t) * N);
    int *id = (int *)malloc(sizeof(int) * N);
    for (int i = 0; i < N; i++)
    {
        id[i] = i;
        if (pthread_create(&pt[i], NULL, philosopher, &id[i]) != 0)
        {
            printf("thread create failed!\n");
            return 1;
        }
    }
    for (int i = 0; i < N; i++)
    {
        pthread_join(pt[i], NULL);
    }
    for (int i = 0; i < N; i++)
    {
        pthread_mutex_destroy(&mutexChopsticks[i]);
    }
    pthread_mutex_destroy(&mutexNoodles);
    return 0;
}