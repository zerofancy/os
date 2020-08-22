/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-09 23:24:48
 * @LastEditors: zero
 * @LastEditTime: 2019-12-13 14:38:27
 * @Description: 文件描述
 * @FilePath: /os/policeandthife/policeandthife.c
 */
#include "miniwindow.h"
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#define MAX_DISTANCE 200

int prePolicePos = 0;
int preThiefPos = 0;
int policePos = 0;
int thiefPos = 0;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;

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
 * @description:计算警察位置的线程 
 * @return: 
 */
void policeMove()
{
    while (policePos < MAX_DISTANCE)
    {
        // lock
        if (pthread_mutex_lock(&mutex1) != 0)
        {
            perror("pthread_mutex_lock");
            exit(EXIT_FAILURE);
        }
        if (policePos - thiefPos < 3)
        {
            policePos++;
        }
        // unlock
        if (pthread_mutex_unlock(&mutex1) != 0)
        {
            perror("pthread_mutex_unlock");
            exit(EXIT_FAILURE);
        }
        usleep(getRndNum(100000, 500000));
    }
}

/**
 * @description: 用于记录小偷位置的线程
 * @param {type} 
 * @return: 
 */
void thiefMove()
{
    while (thiefPos < MAX_DISTANCE)
    {
        // lock
        if (pthread_mutex_lock(&mutex1) != 0)
        {
            perror("pthread_mutex_lock");
            exit(EXIT_FAILURE);
        }
        if (thiefPos - policePos < 3)
        {
            thiefPos++;
        }
        // unlock
        if (pthread_mutex_unlock(&mutex1) != 0)
        {
            perror("pthread_mutex_unlock");
            exit(EXIT_FAILURE);
        }
        usleep(getRndNum(100000, 500000));
    }
}

/**
 * @description: 将位置换算成应该显示的行号
 * @param {int} 位置
 * @return: 行号
 */
int getLINE(int pos)
{
    return pos / (COLS / 2 - 2) + LINES / 4 + 3;
}

/**
 * @description: 将位置换算成应该显示的列号
 * @param {int} 位置
 * @return: 列号
 */
int getCOL(int pos)
{
    return pos % (COLS / 2 - 2) + COLS / 4 + 1;
}

/**
 * @description: 用于重绘警察和小偷
 * @return: 
 */
void repaint()
{
    mvprintw(getLINE(0), getCOL(0), "S");
    mvprintw(getLINE(MAX_DISTANCE), getCOL(MAX_DISTANCE), "D");
    //清除原来绘制的位置
    mvprintw(getLINE(prePolicePos), getCOL(prePolicePos), " ");
    //小偷显示在警察的下一行
    mvprintw(getLINE(preThiefPos) + 1, getCOL(preThiefPos), " ");
    if (pthread_mutex_lock(&mutex3) != 0)
    {
        perror("pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }
    prePolicePos = policePos;
    preThiefPos = thiefPos;
    // unlock
    if (pthread_mutex_unlock(&mutex3) != 0)
    {
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
    //将当前位置绘制到屏幕上
    mvprintw(getLINE(prePolicePos), getCOL(prePolicePos), "P");
    mvprintw(getLINE(preThiefPos) + 1, getCOL(preThiefPos), "T");
    mvprintw(LINES-3,0,"DISTENCE:%2d",prePolicePos-preThiefPos);
    refresh();
}

int main()
{
    srand((unsigned)time(NULL));
    int ret;
    pthread_t threadPolice, threadThief;
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    init_pair(1, COLOR_YELLOW, COLOR_BLACK);
    curs_set(0);

    MiniWindow *window = createMiniWindow(LINES / 2, COLS / 2, LINES / 4, COLS / 4, "Policeman And Thief");
    paintMiniWindow(window);
    mvprintw(LINES - 2, 0, "[S] to start, and [Q] to quit.");
    refresh();

    while (true)
    {
        int c = wgetch(window->handle);
        bool flag = false;
        switch (c)
        {
        case 113:
            //q退出
            goto CLEAN;
            break;
        case 115:
            //s开始
            policePos = 0;
            thiefPos = 0;
            ret = pthread_create(&threadPolice, NULL, (void *)&policeMove, NULL);
            ret = pthread_create(&threadThief, NULL, (void *)&thiefMove, NULL);

            while (true)
            {
                repaint();
                usleep(10000);
                if (policePos == thiefPos && policePos == MAX_DISTANCE)
                {
                    //最后再调用一次，防止已经到达终点但屏幕显示没有到达的情况
                    repaint();
                    break;
                }
            }
            break;
        default:
            break;
        }
    }

CLEAN:
    destoryMiniWindow(window);
    endwin();
    return 0;
}