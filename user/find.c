#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

const char *target;

char* fmtname(char *path) {
    static char buf[DIRSIZ+1];
    char *p;

    // Find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if(strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    buf[strlen(p)] = '\0';
    return buf;
}

// 1:idr  0:file
int check(char *path) {
    struct stat st;
    if(stat(path, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        return 0; 
    }
    char *name;
    switch(st.type) {
        case T_FILE:
            name = fmtname(path);
            if(strcmp(name, target) == 0) {
                printf("%s\n", path);
            }
            return 0;
        case T_DIR:
            name = fmtname(path);
            if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                return 0;
            }
            else {
                return 1;
            }
        default:
            return 0;
    }
}

void dfs(char *path) {

    int fd;
    struct stat st;
    struct dirent de;    

    char buf[512], *p;

    if((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return ;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    
    if(st.type != T_DIR) {
        fprintf(2, "fin: find need DIR, but %s is not.\n", path);
        close(fd);
        return ;
    }
    
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf("find: path too long\n");
        return ;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if(de.inum == 0) 
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0){
            printf("find: cannot stat %s\n", buf);
            continue;
        }
        if(check(buf)) {
            dfs(buf);
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
        fprintf(2, "usage: find <path> <target>\n");
        exit(1);
    }
    target = argv[2];
    dfs(argv[1]);
    exit(0);
}