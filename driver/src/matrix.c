/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-07 01:54:22
 * @LastEditors: zero
 * @LastEditTime: 2019-12-07 02:27:15
 * @Description: 文件描述
 * @FilePath: /os/driver/matrix.c
 */
#include <stdbool.h>

#define N 100
char data[N][N];
char drv_buf[1024];

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

void func(int m, int n)
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

void cpTobuf(int r,int c){
    int i,j;
    for (i = 0; i < r; i++)
    {
        for (j = 0; j < c; j++)
        {
            *(drv_buf+i*c+j)=data[i][j];
        }   
    }
}

char* cast(int m,int n)
{
    int i,j;
    for(i=0;i<m;i++){
        for(j=0;j<n;j++){
            data[i][j]=-1;
        }
    }

    currentChar='Z';
    func(m, n);
    cpTobuf(m,n);
    return drv_buf;
}