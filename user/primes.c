#include "kernel/types.h"
#include "user/user.h"



int main() {
    int fd[2];
    pipe(fd);
    char *argv[] = {"prime", 0};

    if(fork() == 0) {
        close(0);
        dup(fd[0]);
        close(fd[1]);
        close(fd[0]);
        
        exec("prime", argv);
        fprintf(2, "exec error\n");
        exit(1);
    } 
    close(1);
    dup(fd[1]);
    close(fd[0]);
    close(fd[1]);
    for(int i = 2; i <= 35; i++) {
        write(1, &i, 4);
    }
    //very important
    close(1);
    close(0);

    while(wait(0) != -1) 
        ;
    
    exit(0);
}