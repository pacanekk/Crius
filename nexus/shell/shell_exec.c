#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "shell_internal.h"
#include "api.h"

extern int shell_try_builtin(int argc, char **argv);

void shell_exec(char *line) {
    static char *argv[PROG_MAX_ARGS];
    int argc = 0;

    char *p = line;
    while (*p && argc < PROG_MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }

    if (argc == 0) return;
    argv[argc] = NULL;

    /* Try builtins first */
    if (shell_try_builtin(argc, argv))
        return;

    /* Everything else: exec() */
    char resolved[128];
    if (argv[0][0] != '/' && argv[0][0] != '.') {
        int found = 0;
        int pi = 0;
        while (shell_path[pi] && !found) {
            char dir[64];
            int di = 0;
            while (shell_path[pi] && shell_path[pi] != ':' && di < 63)
                dir[di++] = shell_path[pi++];
            dir[di] = '\0';
            if (shell_path[pi] == ':') pi++;

            int ri = 0;
            for (int j = 0; dir[j] && ri < 127; j++) resolved[ri++] = dir[j];
            if (ri > 0 && resolved[ri-1] != '/' && ri < 127) resolved[ri++] = '/';
            for (int j = 0; argv[0][j] && ri < 127; j++) resolved[ri++] = argv[0][j];
            resolved[ri] = '\0';

            struct stat st;
            int sr = stat(resolved, &st);
            if (sr == 0) {
                found = 1;
            }
        }
        if (found) {
            argv[0] = resolved;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        shell_write("fork failed\n");
        return;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        shell_write("Unknown command: ");
        shell_write(argv[0]);
        shell_putc('\n');
        exit(1);
    }
    waitpid(pid, NULL, 0);
}
