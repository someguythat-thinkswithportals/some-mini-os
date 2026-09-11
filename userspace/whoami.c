#include <some-libc.h>

int main(void) {
    int uid = getuid();
    passwd_entry_t* pw = getpwuid(uid);
    if (pw) {
        printf("%s\n", pw->name);
    } else {
        printf("%d\n", uid);
    }
    return 0;
}
