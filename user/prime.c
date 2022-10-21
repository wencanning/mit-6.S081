#include "kernel/types.h"
#include "user/user.h"


int main() {
    int prime;
    read(0, &prime, 4);
    printf("prime %d\n", prime);

    char *argv[] = {"prime", 0};
    int flag = 0;
    int fd[2], num;
    while(read(0, &num, 4)) {
        if(num % prime != 0) {
            if(!flag) {
                flag = 1;
                pipe(fd);
                if(fork() == 0) {
                    close(0);
                    dup(fd[0]);
                    close(fd[0]);
                    close(fd[1]);
                    exec("prime", argv);
                }

                close(1);
                dup(fd[1]);
                close(fd[0]);
                close(fd[1]);
            }
            write(1, &num, 4);
        }
    }
    if(flag) {
        close(1); 
    }
    close(0); 
    while(wait(0) != -1) 
        ;

    exit(0);
}
