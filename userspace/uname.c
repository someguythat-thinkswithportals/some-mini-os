#include <some-libc.h>

int main(void) {
#ifdef __x86_64__
    printf("some-mini-os some-mini-os 0.5.1 x86_64 some-mini-os\n");
#else
    printf("some-mini-os some-mini-os 0.5.1 i386 some-mini-os\n");
#endif
    return 0;
}
