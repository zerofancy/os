/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-07 15:47:07
 * @LastEditors: zero
 * @LastEditTime: 2019-12-07 16:50:33
 * @Description: 文件描述
 * @FilePath: /os/process/3_pipe/pipe.c
 */
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>

int main()
{
int fd[2];	//���������ļ�������
int fdin;	//�ļ�ָ��
char buf[PIPE_BUF];	//������
int pid,len;
if(pipe(fd)<0)	//����ܵ�
  {
  perror("pipe error");
  exit(1);
  }
pid=fork();
if(pid<0)
  {
  perror("fork error");
  exit(1);
  }
if(pid==0)
  {
  close(fd[1]);	//�����ӽ��̣��ر�д�ܵ�
  printf("Child process ");
  //��Ϣ��fd[0]�ж�����buf֮�У�����Ϊlen
  while((len=read(fd[0],buf,PIPE_BUF))>0)
    {
    buf[len]='\0';	//���һ���ַ���ʶ��ֹ
    printf("read %d bytes...\n",len);
    printf("%s\n",buf);	//��ʾ����������
    }
  close(fd[0]);	//�رն��ܵ�
  }
  if(pid>0)	//������
  {
  close(fd[0]);	//�رն��ܵ�
  printf("Parent start...\n");
  //ʹ��ֻ����ʽ���ļ�hello
  fdin=open("hello",O_RDONLY);
  if(fdin<0)
    {
    perror("open hello file error\n");
    exit(1);
    }
  //��Ϣ���ļ�fdin����buf������Ϊlen
  while((len=read(fdin,buf,PIPE_BUF))>0)
  write(fd[1],buf,len); //��Ϣ��bufд��fd[1]��
  close(fdin);	//�ر��ļ�ָ��
  printf("Parent over...\n");
  close(fd[1]);	//�ر�д�ܵ�
  }
  waitpid(pid,NULL,0);	//�ȴ��ӽ��̽���
  exit(0);
}
