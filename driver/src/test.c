/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-07 01:55:24
 * @LastEditors: zero
 * @LastEditTime: 2019-12-07 02:21:34
 * @Description: 文件描述
 * @FilePath: /os/driver/test.c
 */
#include <stdio.h>

char* cast(int m,int n);

void output2(char*buf,int m,int n){
    for (int i = 0; i < m; i++)
    {
        for (int j = 0; j < n; j++)
        {
            printf("%c ",*(buf+i*n+j));
        }
        printf("\n");
    }
}

int main(){
    int m=5,n=5;
    output2(cast(m,n),m,n);
}