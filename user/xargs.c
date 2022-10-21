#include "kernel/types.h"
#include "user/user.h"

char *arg[100];
char buf[100];

int main(int argc, char *argv[]) {
    for(int i = 0; i < argc - 1; i++) {
        arg[i] = argv[i+1];
    }   
    arg[argc] = 0; 
    memset(buf, '\0', sizeof buf);
    while(read(0, buf + strlen(buf), sizeof buf)) ; 
    int len = strlen(buf);

    for(int i = 0; i < len; i++) {
        if(buf[i] == ' ' || buf[i] == '\n') buf[i] = '\0';
    }
    char *p = buf;

    while(p-buf < len && *p == '\0') p++;
    while(p-buf < len) {
        if(fork() == 0) {
            arg[argc-1] = p;
            exec(arg[0], arg);

            fprintf(2, "exec error\n");
            exit(1);
        }
        while(p-buf < len && *p != '\0') p++;
        while(p-buf < len && *p == '\0') p++;
    }
    memset(buf, '\0', sizeof buf);
    exit(0);
}