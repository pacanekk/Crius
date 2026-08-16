#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "shell_internal.h"
#include "api.h"

extern int edit_init(int argc, char **argv);

/* Built-in commands: cd, exit, export, echo, edit, which
 * Returns 1 if handled, 0 if not a builtin. */
int shell_try_builtin(int argc, char **argv) {
    if (argc == 0) return 1;

    if (strcmp(argv[0], "cd") == 0) {
        if (argc < 2) chdir("/");
        else if (chdir(argv[1]) < 0)
            shell_write("No such directory\n");
        return 1;
    }
    if (strcmp(argv[0], "exit") == 0) {
        shell_write("Goodbye.\n");
        exit(0);
        return 1;
    }

    /* export VAR=VALUE */
    if (strcmp(argv[0], "export") == 0) {
        if (argc < 2) {
            shell_write("PATH=");
            shell_write(shell_path);
            shell_putc('\n');
            return 1;
        }
        char *eq = argv[1];
        while (*eq && *eq != '=') eq++;
        if (*eq == '=') {
            *eq = '\0';
            const char *var = argv[1];
            const char *val = eq + 1;
            if (strcmp(var, "PATH") == 0) {
                int i = 0;
                while (val[i] && i < 127) { shell_path[i] = val[i]; i++; }
                shell_path[i] = '\0';
            } else if (strcmp(var, "HOSTNAME") == 0) {
                int i = 0;
                while (val[i] && i < 63) { shell_hostname[i] = val[i]; i++; }
                shell_hostname[i] = '\0';
            }
        }
        return 1;
    }

    /* echo with $VAR support */
    if (strcmp(argv[0], "echo") == 0 && argc >= 2) {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '$') {
                if (strcmp(argv[i], "$PATH") == 0)
                    shell_write(shell_path);
                else if (strcmp(argv[i], "$HOSTNAME") == 0)
                    shell_write(shell_hostname);
                else
                    shell_write(argv[i]);
            } else {
                shell_write(argv[i]);
            }
            if (i < argc - 1) shell_putc(' ');
        }
        shell_putc('\n');
        return 1;
    }

    /* edit - builtin editor (runs in-process for screen + input control) */
    if (strcmp(argv[0], "edit") == 0) {
        edit_init(argc, argv);
        return 1;
    }

    /* which: show full path of a command */
    if (strcmp(argv[0], "which") == 0) {
        if (argc < 2) return 1;
        if (argv[1][0] == '/' || argv[1][0] == '.') {
            shell_write(argv[1]);
            shell_putc('\n');
            return 1;
        }
        int pi = 0;
        int found = 0;
        while (shell_path[pi] && !found) {
            char dir[64];
            int di = 0;
            while (shell_path[pi] && shell_path[pi] != ':' && di < 63)
                dir[di++] = shell_path[pi++];
            dir[di] = '\0';
            if (shell_path[pi] == ':') pi++;

            char full[128];
            int ri = 0;
            for (int j = 0; dir[j] && ri < 127; j++) full[ri++] = dir[j];
            if (ri > 0 && full[ri-1] != '/' && ri < 127) full[ri++] = '/';
            for (int j = 0; argv[1][j] && ri < 127; j++) full[ri++] = argv[1][j];
            full[ri] = '\0';

            struct stat st;
            if (stat(full, &st) == 0) {
                shell_write(full);
                shell_putc('\n');
                found = 1;
            }
        }
        if (!found) {
            shell_write("not found\n");
        }
        return 1;
    }

    return 0;
}
