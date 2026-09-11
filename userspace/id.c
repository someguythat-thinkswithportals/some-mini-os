#include <some-libc.h>
#include <syscall.h>

int main(void) {
    int uid = getuid();
    int gid = getgid();
    printf("uid=%d gid=%d\n", uid, gid);
    return 0;
}
