#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define RIGHT 0
#define BEHAND 1
#define LEFT 2
#define TOP 3

char currentChar = 'Z';

char getNextChar()
{
    currentChar++;
    if (currentChar > 'Z')
    {
        currentChar = 'A';
    }
    return currentChar;
}

void func(char **data, int m, int n)
{
    int prex = 0, prey = 0; //备忘
    int x = 0, y = 0;       //从第一个格开始
    int direction = RIGHT;  //初始向右扫描
    bool flag = false;
    while (true)
    {
        //该行/列是否填满
        while (x < 0 || y < 0 || x >= m || y >= n || data[x][y] != -1)
        {
            if (!flag)
            {
                //没有格子了
                return;
            }
            flag = false;
            //退回上一步
            x = prex;
            y = prey;
            //换个方向
            direction++;
            direction %= 4;
            //移动到下一步
            switch (direction)
            {
            case RIGHT:
                y++;
                break;
            case BEHAND:
                x++;
                break;
            case LEFT:
                y--;
                break;
            default:
                x--;
                break;
            }
        }
        flag = true;
        //填充
        data[x][y] = getNextChar();
        prex = x;
        prey = y;
        //移动到下一步
        switch (direction)
        {
        case RIGHT:
            y++;
            break;
        case BEHAND:
            x++;
            break;
        case LEFT:
            y--;
            break;
        default:
            x--;
            break;
        }
    }
}

void output(char **data, int m, int n)
{
    for (int i = 0; i < m; i++)
    {
        bool flag = true;
        for (int j = 0; j < n; j++)
        {
            if (flag)
            {
                flag = false;
            }
            else
            {
                printf(" ");
            }
            printf("%c",data[i][j]);
        }
        printf("\n");
    }
}

int main()
{
    char **data;
    int m, n; //m行n列
    scanf("%d %d", &m, &n);
    //分配内存，初始化为-1
    data = (char **)malloc(sizeof(char *) * m);
    for (int i = 0; i < m; i++)
    {
        data[i] = (char *)malloc(sizeof(char) * n);
        memset(data[i], -1, n);
    }

    func(data, m, n);
    output(data, m, n);

    //释放内存
    for (int i = 0; i < m; i++)
    {
        free(data[i]);
    }
    free(data);
    return 0;
}