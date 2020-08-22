/*
 * @Website: https://ntutn.top
 * @Date: 2019-11-17 09:17:16
 * @LastEditors: zero
 * @LastEditTime: 2019-11-17 22:39:38
 * @Description: 封装的迷你菜单工具头文件
 * @FilePath: /example19/src/minimenu.h
 */
#include "stdntutn.h"
#include "miniwindow.h"

typedef struct{
    char*caption;
    void (*callback)();
}Choice;

typedef struct
{
    int choiceCount;
    int maxCount;
    Choice**choices;
    MiniWindow*window;
    MENU*handle;
}MiniMenu;

/**
 * @description: 创建菜单结构体，并对一些量赋初值
 * @param {int} maxCount
 * @return: MiniMenu*
 */
MiniMenu* initMenu(int);

/**
 * @description: 用于向菜单添加选项
 * @param {MiniMenu*} 
 * @param {char*} 文本
 * @param {void(*)()} 回调函数 
 * @return: void
 * 在执行这个方法前应该先指定maxCount
 */
void addChoice(MiniMenu*,char*,void(*)());

/**
 * @description: 创建并显示菜单（及窗口）
 * @param {int} height
 * @param {int} width
 * @param {int} startY
 * @param {int} startX
 * @param {MiniMenu*} 
 * @return: 
 */
void paintMenu(MiniMenu*,int,int,int,int);

/**
 * @description: 需要循环执行，直到菜单退出
 * @param {MiniMenu*} 
 * @return: 是否需要继续执行
 */
bool menuLoop(MiniMenu*);

/**
 * @description: 销毁对应窗口 
 * @param {MiniMenu*} 
 * @return: 
 */
void destoryMenu(MiniMenu*);