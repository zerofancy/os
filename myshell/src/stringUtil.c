/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-18 20:41:05
 * @LastEditors  : zero
 * @LastEditTime : 2019-12-18 20:42:50
 * @Description: 字符串工具
 * @FilePath: /os/myshell/src/stringUtil.c
 */
#include "stringUtil.h"

bool isSpace(char c){
    if(c==' '){
        return true;
    }
    if(c=='\t'){
        return true;
    }
    if(c=='\n'){
        return true;
    }
    return false;
}

void stringUtil_trim(char*str){
    char*p1=str,*p2=str;
    while (isSpace(*p2))
    {
        p2++;
    }
    while (*p2)
    {
        *p1=*p2;
        p1++;
        p2++;
    }
    *p1=*p2;
    p1--;
    while (p1>=str&&isSpace(*p1))
    {
        *p1='\0';
        p1--;
    }
}