/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-07 02:29:02
 * @LastEditors: zero
 * @LastEditTime: 2019-12-07 02:50:50
 * @Description: 文件描述
 * @FilePath: /code/nodevfs.c
 */
//nodevfs.c

#ifndef __KERNEL__
#define __KERNEL__
#endif

#ifndef MODULE
#define MODULE
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/init.h>
#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>  
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>    /* O_ACCMODE */
#include <linux/poll.h>    /* COPY_TO_USER */
#include <asm/system.h>     /* cli(), *_flags */

#define DEVICE_NAME "evan"
#define evan_MAJOR 99
#define evan_MINOR 0
static int MAX_BUF_LEN=1024;
static char drv_buf[1024];
static int WRI_LENGTH=0;

#include <stdbool.h>

#define N 100
static char data[N][N];

#define RIGHT 0
#define BEHAND 1
#define LEFT 2
#define TOP 3

static char currentChar = 'Z';

static char getNextChar()
{
    currentChar++;
    if (currentChar > 'Z')
    {
        currentChar = 'A';
    }
    return currentChar;
}

static void func(int m, int n)
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

static void cpTobuf(int r,int c){
    int i,j;
    for (i = 0; i < r; i++)
    {
        for (j = 0; j < c; j++)
        {
            *(drv_buf+i*c+j)=data[i][j];
        }   
    }
}

static char* cast(int m,int n)
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

static void do_write()
{
int i;
int len=WRI_LENGTH;
char tmp;
for(i=0;i<(len>>1);i++,len--)
{
tmp=drv_buf[len-i];
drv_buf[len-i]=drv_buf[i];
drv_buf[i]=tmp;
}
}

static ssize_t evan_write(struct file *filp, char *buffer, size_t count)
{
if (count>MAX_BUF_LEN) count=MAX_BUF_LEN;
copy_from_user(drv_buf,buffer,count);
WRI_LENGTH=count;
printk("user write data to drivers!\n");
int row=(int)(drv_buf[0]);
int col=(int)(drv_buf[1]);
// do_write();
cast(row,col);
return count;
}

static ssize_t evan_read(struct file *filp, char *buffer, size_t count,loff_t *ppos)
{
if (count>MAX_BUF_LEN) count=MAX_BUF_LEN;
copy_to_user(buffer,drv_buf,count);
printk("user read data from drivers!\n");
do_write();
return count;
}

static int evan_ioctl(struct inode *inode,struct file *file,unsigned int cmd,unsigned long arg)
{
printk("ioctl running!\n");
switch(cmd)
{default:break;}
return 0;
}

static int evan_open(struct inode *inode,struct file *file)
{
MOD_INC_USE_COUNT;
sprintf(drv_buf,"device open success!\n");
printk("device open success!\n");
return 0;
}

static int evan_release(struct inode *inode,struct file *filp)
{
MOD_DEC_USE_COUNT;
printk("device open success!\n");
return 0;
}

static struct file_operations evan_fops=
{
owner:	THIS_MODULE,
write:	evan_write,
read:	evan_read,
ioctl:	evan_ioctl,
open:	evan_open,
release:	evan_release,
};

static int __init evan_init(void)
{
int result;
SET_MODULE_OWNER(&evan_fops);
result=register_chrdev(evan_MAJOR,"evan",&evan_fops);
if(result<0) return result;
printk(DEVICE_NAME " initialized\n");
return 0;
}

static void __exit evan_exit(void)
{
unregister_chrdev(evan_MAJOR,"evan");
printk(DEVICE_NAME " unloaded\n");
}

module_init(evan_init);
module_exit(evan_exit);
