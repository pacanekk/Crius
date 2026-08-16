#include <string.h>
#include <unistd.h>
#include "api.h"

int echo_main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        int len = 0;
        while (argv[i][len]) len++;
        write(1, argv[i], len);
        if (i < argc - 1) {
            char sp = ' ';
            write(1, &sp, 1);
        }
    }
    char nl = '\n';
    write(1, &nl, 1);
    return 0;
}

int clear_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* ANSI clear screen */
    write(1, "\033[2J\033[H", 7);
    return 0;
}

int reboot_main(int argc, char **argv) {
    (void)argc; (void)argv;
    reboot();
    for (;;) __asm__ volatile ("hlt");
    return 0;
}
