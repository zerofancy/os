/*
 * @Website: https://ntutn.top
 * @Date: 2019-11-17 23:21:59
 * @LastEditors  : zero
 * @LastEditTime : 2019-12-20 15:10:02
 * @Description: 测试
 * @FilePath: /example19/src/menu.c
 */
#include "stdntutn.h"
#include "minimenu.h"

void mystringCallback()
{
    // system("gnome-terminal -- bash -c \"mystring/mystring;exec bash;\"");
    system("deepin-terminal -x mystring/mystring >/dev/null 2>&1");
}

void matrixCallback()
{
    // system("gnome-terminal -- bash -c \"matrix/matrix;exec bash;\"");
    system("deepin-terminal -x matrix/matrix >/dev/null 2>&1");
}

void addUserCallback()
{
    // system("gnome-terminal -- bash -c \"useroperation/adduser.sh;exec bash;\"");
    system("deepin-terminal -x useroperation/adduser.sh >/dev/null 2>&1");
}

void delUserCallback()
{
    // system("gnome-terminal -- bash -c \"useroperation/deluser.sh;exec bash;\"");
    system("deepin-terminal -x useroperation/deluser.sh >/dev/null 2>&1");
}

void processCallback1()
{
    // system("gnome-terminal -- bash -c \"process/fork;exec bash;\"");
    system("deepin-terminal -x process/fork >/dev/null 2>&1");
}

void processCallback2()
{
    // system("gnome-terminal -- bash -c \"process/signal;exec bash;\"");
    system("deepin-terminal -x process/signal >/dev/null 2>&1");
}

void processCallback3()
{
    // system("gnome-terminal -- bash -c \"cd process; ./pipe;exec bash;\"");
    system("deepin-terminal -x process/pipe >/dev/null 2>&1");
}

void processCallback4()
{
    // system("gnome-terminal -- bash -c \"process/popen;exec bash;\"");
    system("deepin-terminal -x process/popen >/dev/null 2>&1");
}

void processCallback5_1()
{
    // system("gnome-terminal -- bash -c \"cd process;./fifo1;exec bash;\"");
    system("deepin-terminal -x process/fifo1 >/dev/null 2>&1");
}

void processCallback5_2()
{
    // system("gnome-terminal -- bash -c \"cd process; ./fifo2;exec bash;\"");
    system("deepin-terminal -x process/fifo2 >/dev/null 2>&1");
}

void policeAndThiefCallback()
{
    // system("gnome-terminal -- bash -c \"cd policeandthief ; ./policeandthief;exec bash;\"");
    system("deepin-terminal -x policeandthief/policeandthief >/dev/null 2>&1");
}

void philosopherCallback()
{
    // system("gnome-terminal -- bash -c \"cd philosopher ; ./philosopher;exec bash;\"");
    system("deepin-terminal -x philosopher/philosopher >/dev/null 2>&1");
}

void myshellCallback()
{
    // system("gnome-terminal -- bash -c \"cd myshell ; ./myshell;exec bash;\"");
    system("deepin-terminal -x myshell/myshell >/dev/null 2>&1");
}

void memoryManageCallback()
{
    // system("gnome-terminal -- bash -c \"cd memorymanage; ./memorymanage;exec bash;\"");
    system("deepin-terminal -x memorymanage/memorymanage >/dev/null 2>&1");
}

int main()
{
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    init_pair(1, COLOR_YELLOW, COLOR_BLACK);

    MiniMenu *menu = initMenu(20);
    addChoice(menu, "1 string", mystringCallback);
    addChoice(menu, "2 matrix", matrixCallback);
    addChoice(menu, "3 adduser", addUserCallback);
    addChoice(menu, "4 deluser", delUserCallback);
    addChoice(menu, "5 fork", processCallback1);
    addChoice(menu, "6 signal", processCallback2);
    addChoice(menu, "7 pipe", processCallback3);
    addChoice(menu, "8 popen", processCallback4);
    addChoice(menu, "9 fifo1", processCallback5_1);
    addChoice(menu, "10 fifo2", processCallback5_2);
    addChoice(menu, "11 Police and Thief", policeAndThiefCallback);
    addChoice(menu, "12 Philosopher", philosopherCallback);
    addChoice(menu, "13 My Shell", myshellCallback);
    addChoice(menu, "14 Memory Management", memoryManageCallback);

    paintMenu(menu, LINES / 2, COLS / 2, LINES / 4, COLS / 4);

    mvprintw(LINES - 2, 0, "Arrow keys to switch, [ENTER] to select, and [Q] to quit.");
    refresh();

    while (menuLoop(menu))
    {
        ;
    }
    destoryMenu(menu);

    endwin();
    return 0;
}