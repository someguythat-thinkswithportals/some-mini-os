#include <some-libc.h>

#define F_SYSNAME   (1u << 0)
#define F_NODENAME  (1u << 1)
#define F_RELEASE   (1u << 2)
#define F_VERSION   (1u << 3)
#define F_MACHINE   (1u << 4)
#define F_OS        (1u << 5)
#define F_ALL       (F_SYSNAME | F_NODENAME | F_RELEASE | F_VERSION | F_MACHINE | F_OS)

static const char* kernel_name = "some-mini-os";
static const char* node_name = "some-mini-os";
static const char* kernel_release = "0.7";
static const char* kernel_version = "0.7";
static const char* operating_system = "some-mini-os";

static unsigned flags;
static int printed;

static const char* machine_name(void) {
#ifdef __x86_64__
    return "x86_64";
#else
    return "i386";
#endif
}

static void print_field(const char* s) {
    if (printed) printf(" ");
    printf("%s", s);
    printed = 1;
}

static void print_uname(void) {
    printed = 0;
    if (flags & F_SYSNAME)  print_field(kernel_name);
    if (flags & F_NODENAME) print_field(node_name);
    if (flags & F_RELEASE)  print_field(kernel_release);
    if (flags & F_VERSION)  print_field(kernel_version);
    if (flags & F_MACHINE)  print_field(machine_name());
    if (flags & F_OS)       print_field(operating_system);
    printf("\n");
}

static void usage(void) {
    printf("Usage: uname [OPTION]...\n");
    printf("Print certain system information.  With no OPTION, same as -s.\n");
    printf("\n");
    printf("  -a, --all                print all information, in the following order,\n");
    printf("                             except omit -p and -i if unknown:\n");
    printf("  -s, --kernel-name        print the kernel name\n");
    printf("  -n, --nodename           print the network node hostname\n");
    printf("  -r, --kernel-release     print the kernel release\n");
    printf("  -v, --kernel-version     print the kernel version\n");
    printf("  -m, --machine            print the machine hardware name\n");
    printf("  -o, --operating-system   print the operating system\n");
    printf("      --help               display this help and exit\n");
}

static void invalid_option(char c) {
    printf("uname: invalid option -- '%c'\n", c);
    printf("Try 'uname --help' for more information.\n");
    exit(1);
}

static void invalid_long_option(const char* opt) {
    printf("uname: unrecognized option '--%s'\n", opt);
    printf("Try 'uname --help' for more information.\n");
    exit(1);
}

static void set_short_flag(char c) {
    switch (c) {
        case 'a': flags |= F_ALL; break;
        case 's': flags |= F_SYSNAME; break;
        case 'n': flags |= F_NODENAME; break;
        case 'r': flags |= F_RELEASE; break;
        case 'v': flags |= F_VERSION; break;
        case 'm': flags |= F_MACHINE; break;
        case 'o': flags |= F_OS; break;
        default: invalid_option(c);
    }
}

static void set_long_flag(const char* opt) {
    if (strcmp(opt, "help") == 0) {
        usage();
        exit(0);
    }
    if (strcmp(opt, "all") == 0) flags |= F_ALL;
    else if (strcmp(opt, "kernel-name") == 0) flags |= F_SYSNAME;
    else if (strcmp(opt, "nodename") == 0) flags |= F_NODENAME;
    else if (strcmp(opt, "kernel-release") == 0) flags |= F_RELEASE;
    else if (strcmp(opt, "kernel-version") == 0) flags |= F_VERSION;
    else if (strcmp(opt, "machine") == 0) flags |= F_MACHINE;
    else if (strcmp(opt, "operating-system") == 0) flags |= F_OS;
    else invalid_long_option(opt);
}

int main(void) {
    volatile char* args = (volatile char*)0x50F000;
    flags = 0;

    char* p = (char*)args;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) break;
        char* tok = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        char saved = *p;
        if (*p) *p = 0;

        if (tok[0] == '-' && tok[1] == '-') {
            set_long_flag(tok + 2);
        } else if (tok[0] == '-' && tok[1] != 0) {
            for (int i = 1; tok[i]; i++) set_short_flag(tok[i]);
        }

        if (saved) *p = saved;
    }

    if (flags == 0) flags = F_SYSNAME;
    print_uname();
    return 0;
}