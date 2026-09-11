#include <some-libc.h>
#include <syscall.h>

int main(void) {
    int ruid = getuid();
    passwd_entry_t* pw = getpwuid(ruid);
    char username[32];
    if (pw) {
        strcpy(username, pw->name);
    } else {
        int_to_str(ruid, username);
    }

    shadow_entry_t* sp = getspnam(username);
    if (!sp) {
        printf("passwd: no shadow entry for %s\n", username);
        return 1;
    }

    if (ruid != 0) {
        char old_pass[32];
        read_password("(current) password: ", old_pass, sizeof(old_pass));
        if (!verify_password(sp->salt, sp->hash, old_pass)) {
            printf("passwd: incorrect password\n");
            return 1;
        }
    }

    char new_pass[32];
    char confirm[32];

    while (1) {
        read_password("New password: ", new_pass, sizeof(new_pass));
        if (new_pass[0] == 0) {
            printf("passwd: password not changed\n");
            return 1;
        }
        read_password("Retype new password: ", confirm, sizeof(confirm));
        if (strcmp(new_pass, confirm) == 0) break;
        printf("Sorry, passwords do not match.\n");
    }

    char salt[16];
    char hash_buf[32];
    generate_salt(salt);
    hash_only(salt, new_pass, hash_buf);

    remove_shadow_entry(username);
    add_shadow_entry(username, salt, hash_buf);

    printf("passwd: password updated successfully\n");
    return 0;
}
