/*
 * @Website: https://ntutn.top
 * @Date: 2019-11-17 07:55:47
 * @LastEditors: zero
 * @LastEditTime: 2019-11-17 09:16:22
 * @Description: 迷你窗口工具头文件
 * @FilePath: /example19/src/miniwindow.h
 */
#include "stdntutn.h"

typedef struct
{
    int height;
    int width;
    int startX;
    int startY;
    char *title;
    WINDOW *handle;
} MiniWindow;

/**
 * @description: 用于在特定窗口中间显示字符
 * @param {type} 
 * @return: 
 */
void print_in_middle(WINDOW*win,int starty,int startx,int width,char*string,chtype color);

/**
 * @description: 创建一个窗口
 * @param {int} 窗口高度
 * @param {int} 窗口宽度
 * @param {int} 窗口创建时的位置Y
 * @param {int} 窗口创建时的位置X
 * @return: {MiniWindow*}
 */
MiniWindow *createMiniWindow(int, int, int, int, char *);

/**
 * @description: 在默认屏幕上绘制窗口
 * @param {MiniWindow*} 要绘制的窗口
 * @return: 
 */
void paintMiniWindow(MiniWindow *);

/**
 * @description: 销毁窗口
 * @param {MiniWindow*} 要销毁的窗口
 * @return: 
 */
void destoryMiniWindow(MiniWindow *);