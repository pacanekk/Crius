#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "api.h"

int ls_main(int argc, char **argv) {
    const char *path = ".";
    int show_size = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 's') show_size = 1;
            }
        } else {
            path = argv[i];
        }
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) { prog_print_color("No such directory\n", 0x00FF0000, 0); return 1; }

    struct dirent de;
    int any = 0;
    while (read(fd, &de, sizeof(de)) > 0) {
        any = 1;
        if (de.type == FILE_TYPE_DIR)
            prog_putc('/', 0x0000FFFF, 0);
        uint32_t color = de.type == FILE_TYPE_DIR ? 0x0000FFFF :
                         de.type == FILE_TYPE_DEV ? 0x00FF00FF : 0x00FFFFFF;
        prog_print_color(de.name, color, 0);
        if (show_size) {
            prog_print_color("  ", 0x00FFFFFF, 0);
            struct stat st;
            char fullpath[256];
            int pl = 0;
            while (path[pl] && pl < 250) { fullpath[pl] = path[pl]; pl++; }
            if (pl > 0 && fullpath[pl-1] != '/' && pl < 254) fullpath[pl++] = '/';
            int nl = 0;
            while (de.name[nl] && pl + nl < 255) { fullpath[pl + nl] = de.name[nl]; nl++; }
            fullpath[pl + nl] = '\0';
            stat(fullpath, &st);
            char sbuf[16]; int sn = 0;
            if (st.type == FILE_TYPE_DEV) { sbuf[sn++] = 'd'; sbuf[sn++] = 'e'; sbuf[sn++] = 'v'; }
            else if (st.type == FILE_TYPE_DIR) { sbuf[sn++] = '-'; }
            else {
                size_t sz = st.size;
                if (sz < 1024) {
                    if (sz == 0) sbuf[sn++] = '0';
                    else { char tmp[16]; int tm = 0; while (sz) { tmp[tm++] = '0' + (sz % 10); sz /= 10; } while (tm > 0) sbuf[sn++] = tmp[--tm]; }
                    sbuf[sn++] = 'B';
                } else {
                    const char *units[] = {"KiB", "MiB", "GiB", "TiB"};
                    size_t val = sz / 1024;
                    int div = 0;
                    while (val >= 1024 && div < 3) { val /= 1024; div++; }
                    const char *unit = units[div];
                    if (val == 0) sbuf[sn++] = '0';
                    else { char tmp[16]; int tm = 0; while (val) { tmp[tm++] = '0' + (val % 10); val /= 10; } while (tm > 0) sbuf[sn++] = tmp[--tm]; }
                    for (int u = 0; unit[u]; u++) sbuf[sn++] = unit[u];
                }
            }
            sbuf[sn] = '\0';
            prog_print_color(sbuf, 0x00FFFFFF, 0);
        }
        prog_newline();
    }
    close(fd);
    if (!any) { prog_print_color("(empty)\n", 0x00FFFFFF, 0); }
    return 0;
}

int cat_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: cat <name>\n", 0x00FF0000, 0); return 1; }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { prog_print_color("File not found\n", 0x00FF0000, 0); return 1; }
    char buf[1024];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) prog_putc(buf[i], 0x00FFFFFF, 0);
    }
    close(fd);
    return 0;
}

int write_main(int argc, char **argv) {
    if (argc < 3) { prog_print_color("Usage: write <name> <text...>\n", 0x00FF0000, 0); return 1; }
    char text[512]; int pos = 0;
    for (int i = 2; i < argc; i++) {
        for (int j = 0; argv[i][j] && pos < 511; j++) text[pos++] = argv[i][j];
        if (i < argc - 1 && pos < 511) text[pos++] = ' ';
    }
    text[pos++] = '\n';
    int fd = open(argv[1], O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) { prog_print_color("Failed to create file\n", 0x00FF0000, 0); return 1; }
    write(fd, text, pos);
    close(fd);
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}

int append_main(int argc, char **argv) {
    if (argc < 3) { prog_print_color("Usage: append <name> <text...>\n", 0x00FF0000, 0); return 1; }
    char text[512]; int pos = 0;
    for (int i = 2; i < argc; i++) {
        for (int j = 0; argv[i][j] && pos < 510; j++) text[pos++] = argv[i][j];
        if (i < argc - 1 && pos < 510) text[pos++] = ' ';
    }
    text[pos++] = '\n';
    int fd = open(argv[1], O_WRONLY | O_APPEND);
    if (fd < 0) { prog_print_color("File not found\n", 0x00FF0000, 0); return 1; }
    write(fd, text, pos);
    close(fd);
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}

int rm_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: rm <name>\n", 0x00FF0000, 0); return 1; }
    if (unlink(argv[1]) < 0) { prog_print_color("File not found\n", 0x00FF0000, 0); return 1; }
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}

int mkdir_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: mkdir <name>\n", 0x00FF0000, 0); return 1; }
    if (mkdir(argv[1]) < 0) { prog_print_color("Failed\n", 0x00FF0000, 0); return 1; }
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}

int cd_main(int argc, char **argv) {
    if (argc < 2) chdir("/");
    else if (chdir(argv[1]) < 0) { prog_print_color("No such directory\n", 0x00FF0000, 0); return 1; }
    return 0;
}

int pwd_main(int argc, char **argv) {
    (void)argc; (void)argv;
    char pwd[128];
    getcwd(pwd, 128);
    prog_print_color(pwd, 0x00FFFFFF, 0);
    prog_newline();
    return 0;
}
