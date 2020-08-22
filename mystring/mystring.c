#include <stdio.h>
#include <stdbool.h>
#define N 26

bool judge(const char *str)
{
    char *p = (char *)str;
    while (*p)
    {
        //多读入了回车
        if(*p==10){
            break;
        }
        //A~Z
        if (*p < 'A' || *p > 'Z')
        {
            return false;
        }
        //递增
        if (p > str && *p !=( *(p - 1) + 1))
        {
            return false;
        }
        p++;
    }
    return true;
}

int main()
{
    char B[N + 1];
    fgets(B, N + 1, stdin);
    if (judge(B))
    {
        puts("true\n");
    }
    else
    {
        puts("false\n");
    }
    return 0;
}