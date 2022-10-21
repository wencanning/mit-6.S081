#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(2, "usage: %s <sleep-second>", argv[0]);
        exit(1);
    }
    int sec = atoi(argv[1]);
    printf("(nothing happens for a little while)\n");
    sleep(sec);
    exit(0);
}