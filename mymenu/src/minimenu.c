/*
 * @Website: https://ntutn.top
 * @Date: 2019-11-17 09:17:24
 * @LastEditors: zero
 * @LastEditTime: 2019-12-11 00:42:52
 * @Description: 封装的迷你菜单工具
 * @FilePath: /example19/src/minimenu.c
 */
#include "minimenu.h"

MiniMenu *initMenu(int maxCount)
{
    MiniMenu *menu = (MiniMenu *)calloc(sizeof(MiniMenu), 1);
    menu->maxCount = maxCount + 1;
    menu->choiceCount = 0;
    menu->choices = (Choice **)calloc(sizeof(Choice), maxCount);
}

void addChoice(MiniMenu *menu, char *caption, void (*callback)())
{
    Choice *choice = (Choice *)calloc(1, sizeof(Choice));
    choice->caption = caption;
    choice->callback = callback;
    menu->choices[menu->choiceCount] = choice;
    menu->choiceCount++;
}

void paintMenu(MiniMenu *menu, int height, int width, int startY, int startX)
{
    menu->window = createMiniWindow(height, width, startY, startX, "MENU");

    ITEM **my_items = (ITEM **)calloc(menu->choiceCount, sizeof(ITEM *));
    for (int i = 0; i < menu->choiceCount; i++)
    {
        my_items[i] = new_item(menu->choices[i]->caption, "");
    }
    my_items[menu->choiceCount] = new_item((char *)NULL, (char *)NULL);
    // getchar();
    menu->handle = new_menu((ITEM **)my_items);
    // getchar();

    set_menu_win(menu->handle, menu->window->handle);
    set_menu_sub(menu->handle, derwin(menu->window->handle, height - 4, width - 2, 3, 1));
    set_menu_format(menu->handle, height - 4, 1);

    set_menu_mark(menu->handle, " * ");

    paintMiniWindow(menu->window);

    post_menu(menu->handle);
    wrefresh(menu->window->handle);
}

bool menuLoop(MiniMenu *menu)
{
    int c = wgetch(menu->window->handle);
    ITEM *cur_item;
    if (c == 113)
    {
        return false;
    }
    switch (c)
    {
    case KEY_DOWN:
        menu_driver(menu->handle, REQ_DOWN_ITEM);
        break;
    case KEY_UP:
        menu_driver(menu->handle, REQ_UP_ITEM);
        break;
    case KEY_LEFT:
        menu_driver(menu->handle, REQ_SCR_UPAGE);
        break;
    case KEY_RIGHT:
        menu_driver(menu->handle, REQ_SCR_DPAGE);
        break;
    case 10: //回车
        cur_item = current_item(menu->handle);
        int index = item_index(cur_item);
        menu->choices[index]->callback();
        break;
    default:
        break;
    }
    return true;
}

void destoryMenu(MiniMenu *menu)
{
    for (int i = 0; i < menu->choiceCount; i++)
    {
        free(menu->choices[i]);
    }
    free(menu->choices);
    for (int i = 0; i < menu->choiceCount+1; i++)
    {
        free_item(menu->handle->items[i]);
    }
    free_menu(menu->handle);
    destoryMiniWindow(menu->window);
    free(menu);
}