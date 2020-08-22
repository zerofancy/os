/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-18 20:21:20
 * @LastEditors  : zero
 * @LastEditTime : 2019-12-18 20:25:46
 * @Description: 主程序
 * @FilePath: /os/myshell/src/myshell.c
 */
#include "stdntutn.h"
#include "internalCommand.h"

#define MAXLENGTH 2048

char commandBuf[MAXLENGTH];

int main()
{
    printf("MINI SHELL 0.0.1 by 1707020219\n");
    while (true)
    {
        printf("->");
        fgets(commandBuf, MAXLENGTH - 1, stdin);
        stringUtil_trim(commandBuf);
        if (doInternalCommand(commandBuf))
        {
            ;
        }
        else
        {
            system(commandBuf);
        }
    }
    return 0;
}