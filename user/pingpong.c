#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char **argv) {
    int fd1[1], fd2[2];
    pipe(fd1);
    pipe(fd2);

    char buf[10] = "12";
    if(fork() == 0) {
        read(fd2[0], buf, 1);
        printf("%d: received ping\n", getpid());
        write(fd1[1], buf, 1);
        exit(0);
    }
    else {
        write(fd2[1], buf, 1);
        read(fd1[0], buf, 1);
        printf("%d: received pong\n", getpid());
        exit(0);
    }
}