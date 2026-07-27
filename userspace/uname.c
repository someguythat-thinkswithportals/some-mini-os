#include <some-libc.h>

int main(void) {
#ifdef __x86_64__
    printf("some-tiny-os some-tiny-os 0.5.1 x86_64 some-tiny-os\n");
#else
    printf("some-tiny-os some-tiny-os 0.5.1 i386 some-tiny-os\n");
#endif
    return 0;
}
