/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-07 02:51:23
 * @LastEditors: zero
 * @LastEditTime: 2019-12-07 02:55:11
 * @Description: 文件描述
 * @FilePath: /os/driver/mytest.c
 */
//test_demo.c

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

// void showbuf(char *buf);
void showbuf(char *buf,int r,int c);
int MAX_LEN=32;

int main()
{
int fd;
int i;
char buf[255];
for(i=0;i<MAX_LEN;i++)
{buf[i]=i;}

fd=open("/dev/evan",O_RDWR);
if(fd<0)
{
printf("######nodevfs drivers open failed!!######\n");
return(-1);
}
printf("######nodevfs drivers open success!!######\n");

// printf("write %d bytes data to /dev/evan \n",MAX_LEN);
// showbuf(buf);
// write(fd,buf,MAX_LEN);

// printf("Read %d bytes data from /dev/evan \n",MAX_LEN);
// read(fd,buf,MAX_LEN);
// showbuf(buf);

int row,col;

printf("please input two integer:\n");
//showbuf(buf);
scanf("%d%d",&row,&col);
buf[0]=(char)(row);
buf[1]=(char)(col);
write(fd,buf,2);

//printf("Read %d bytes data from /dev/evan \n",MAX_LEN);
read(fd,buf,row*col);
showbuf(buf,row,col);

ioctl(fd,1,NULL);
close(fd);
return 0;
}

// void showbuf(char *buf)
// {
// int i,j=0;
// for(i=0;i<MAX_LEN;i++)
// {
// if(i%4==0) printf("\n%4d:",j++);
// printf("%4d",buf[i]);
// }
// printf("\n*********************************\n");
// }

void showbuf(char*buf,int r,int c){
    int i,j=0;
    for(i=0;i<r;i++){
        for(j=0;j<c;j++){
            printf("%c ",*(buf+i*c+j));
        }
        printf("\n");
    }
}