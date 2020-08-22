/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-18 20:33:10
 * @LastEditors  : zero
 * @LastEditTime : 2019-12-20 14:35:41
 * @Description: 执行内部命令
 * @FilePath: /os/myshell/src/internalCommand.c
 */
#include "internalCommand.h"

#define INTERNALCOUNT 3

void bye(){
    printf("Bye!\n");
    exit(0);
}

void showHelp(){
    printf("By Liu Haixin.\n");
}

void doNothing(){
    ;
}

bool doInternalCommand(char*command){
    char* internalCommands[]={
        "bye",
        "help",
        ""
    };

    void (*commandCallback[])()={
        bye,
        showHelp,
        doNothing
    };

    char tmpcommand[2048];

    if(strncmp(command,"run ",3)==0){
        sprintf(tmpcommand,"sh %s",command+4);
        system(tmpcommand);
        return true;
    }

    for(int i=0;i<INTERNALCOUNT;i++){
        if(strcmp(command,internalCommands[i])==0){
            commandCallback[i]();
            return true;
        }
    }
    return false;
}