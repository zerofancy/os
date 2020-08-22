/*
 * @Website: https://ntutn.top
 * @Date: 2019-11-17 07:55:38
 * @LastEditors: zero
 * @LastEditTime: 2019-12-09 23:41:09
 * @Description: 封装的窗口工具
 * @FilePath: /example19/src/mini_window.c
 */
#include "stdntutn.h"
#include "miniwindow.h"
 
void print_in_middle(WINDOW *win, int starty, int startx, int width, char *string, chtype color)
{
    int length, x, y;
    float temp;
    if (win == NULL)
    {
        win = stdscr;
    }
    getyx(win, y, x);
    if (startx != 0)
    {
        x = startx;
    }
    if (starty != 0)
    {
        y = starty;
    }
    if (width == 0)
    {
        width = 80;
    }

    length = strlen(string);
    temp = (width - length) / 2;
    x = startx + (int)temp;
    wattron(win, color);
    mvwprintw(win, y, x, "%s", string);
    wattroff(win, color);
    refresh();
}

MiniWindow *createMiniWindow(int height, int width, int startY, int startX, char *title)
{
    //存储窗口信息
    MiniWindow *resWindow = (MiniWindow *)calloc(1, sizeof(MiniWindow));
    resWindow->height = height;
    resWindow->width = width;
    resWindow->startY = startY;
    resWindow->startX = startX;
    resWindow->title = title;
    resWindow->handle = newwin(resWindow->height, resWindow->width, resWindow->startY, resWindow->startX);

    //允许控制字符
    keypad(resWindow->handle, TRUE);

    paintMiniWindow(resWindow);

    return resWindow;
}

void paintMiniWindow(MiniWindow *miniWindow)
{
    //绘制边框
    box(miniWindow->handle, 0, 0);
    print_in_middle(miniWindow->handle, 1, 0, miniWindow->width, miniWindow->title, COLOR_PAIR(1));
    mvwaddch(miniWindow->handle, 2, 0, ACS_LTEE);
    mvwhline(miniWindow->handle, 2, 1, ACS_HLINE, miniWindow->width - 2);
    mvwaddch(miniWindow->handle, 2, miniWindow->width - 1, ACS_RTEE);
    refresh();
    wrefresh(miniWindow->handle);
}

void destoryMiniWindow(MiniWindow *miniWindow)
{
    // free(miniWindow->title);
    delwin(miniWindow->handle);
    free(miniWindow);
}