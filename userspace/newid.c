#include <some-libc.h>

int main(void) {
    int uid = getuid();
    int gid = getgid();
    passwd_entry_t* upw = getpwuid(uid);
    passwd_entry_t* gpw = getpwuid(gid);

    if (upw && gpw) {
        printf("uid=%d(%s) gid=%d(%s)\n", uid, upw->name, gid, gpw->name);
    } else if (upw) {
        printf("uid=%d(%s) gid=%d\n", uid, upw->name, gid);
    } else if (gpw) {
        printf("uid=%d gid=%d(%s)\n", uid, gid, gpw->name);
    } else {
        printf("uid=%d gid=%d\n", uid, gid);
    }
    return 0;
}
