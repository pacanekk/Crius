#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "shell.h"
#include "shell_internal.h"
#include "api.h"

extern int edit_active(void);

/* Terminal dimensions - cached at init from /dev/fbinfo */
int term_cols = 80;
int term_rows = 25;

/* Helper: write plain text to stdout */
void shell_write(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

/* Helper: write single char to stdout */
void shell_putc(char c) {
    write(1, &c, 1);
}

/* Write colored text using ANSI SGR */
void ansi_set_color(int fg) {
    char buf[8];
    int n = 0;
    buf[n++] = '\033'; buf[n++] = '[';
    if (fg >= 0) {
        buf[n++] = '0' + (fg / 10);
        buf[n++] = '0' + (fg % 10);
    } else {
        buf[n++] = '0';
    }
    buf[n++] = 'm';
    write(1, buf, n);
}
void ansi_reset_color(void) { shell_write("\033[0m"); }

void ansi_write_colored(const char *s, int fg) {
    ansi_set_color(fg);
    shell_write(s);
    ansi_reset_color();
}

/* Read terminal dimensions from /dev/fbinfo */
void load_term_size(void) {
    int fd = open("/dev/fbinfo", O_RDONLY);
    if (fd < 0) return;
    char buf[128];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    /* Parse "width: NNN\nheight: NNN\n" */
    int w = 0, h = 0;
    const char *p = buf;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); p++; }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; }
    if (w > 0) term_cols = w / 8;
    if (h > 0) term_rows = h / 8;
}

/* ===== Command buffer ===== */
char cmd_buf[MAX_CMD_LEN];
int cmd_len = 0;
int cmd_pos = 0;

/* Shell environment variables */
char shell_path[128] = "/bin:/sbin";
char shell_hostname[64] = "nexus";

#define MAX_DIR_ENTRIES 32
static char dir_names[MAX_DIR_ENTRIES][32];
static int dir_name_count = 0;

static void load_dir_names(const char *path) {
    dir_name_count = 0;
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && dir_name_count < MAX_DIR_ENTRIES) {
        int j;
        for (j = 0; j < 31 && de->d_name[j]; j++) dir_names[dir_name_count][j] = de->d_name[j];
        dir_names[dir_name_count][j] = '\0';
        dir_name_count++;
    }
    closedir(d);
}

void redraw_line_plain(void) {
    char pwd[128];
    getcwd(pwd, 128);
    shell_write("\r\033[K");
    ansi_write_colored("root@", 94);
    ansi_write_colored(shell_hostname, 94);
    ansi_write_colored(":", 36);
    ansi_write_colored(pwd, 92);
    ansi_write_colored("$ ", 36);
    write(1, cmd_buf, cmd_len);
}

void redraw_line(void) {
    char pwd[128];
    getcwd(pwd, 128);
    shell_write("\r\033[K");
    ansi_write_colored("root@", 94);
    ansi_write_colored(shell_hostname, 94);
    ansi_write_colored(":", 36);
    ansi_write_colored(pwd, 92);
    ansi_write_colored("$ ", 36);
    if (cmd_pos < cmd_len) {
        write(1, cmd_buf, cmd_pos);
        shell_write("\033[7m");
        shell_putc(cmd_buf[cmd_pos]);
        shell_write("\033[0m");
        write(1, cmd_buf + cmd_pos + 1, cmd_len - cmd_pos - 1);
    } else {
        write(1, cmd_buf, cmd_len);
        shell_write("\033[7m \033[0m");
        shell_putc('\b');
    }
}

void insert_char(char c) {
    if (cmd_len >= MAX_CMD_LEN - 1) return;
    for (int i = cmd_len; i > cmd_pos; i--)
        cmd_buf[i] = cmd_buf[i - 1];
    cmd_buf[cmd_pos] = c;
    cmd_len++;
    cmd_pos++;
    redraw_line();
}

void delete_char(void) {
    if (cmd_pos == 0) return;
    for (int i = cmd_pos - 1; i < cmd_len - 1; i++)
        cmd_buf[i] = cmd_buf[i + 1];
    cmd_len--;
    cmd_pos--;
    redraw_line();
}

/* ===== History ===== */
static char history[HISTORY_SIZE][MAX_CMD_LEN];
static int history_count = 0;
static int history_pos = 0;
static int history_active = 0;
static char history_saved[MAX_CMD_LEN];

void history_save_current(void) {
    cmd_buf[cmd_len] = '\0';
    int i = 0;
    while (cmd_buf[i] && i < MAX_CMD_LEN - 1) { history_saved[i] = cmd_buf[i]; i++; }
    history_saved[i] = '\0';
}

void history_push(const char *line) {
    if (line[0] == '\0') return;
    int len = 0;
    while (line[len] && len < MAX_CMD_LEN - 1) len++;
    if (len == 0) return;

    int all_space = 1;
    for (int i = 0; i < len; i++) {
        if (line[i] != ' ') { all_space = 0; break; }
    }
    if (all_space) return;

    if (history_count > 0) {
        int prev = (history_count - 1) % HISTORY_SIZE;
        int same = 1;
        for (int i = 0; i <= len; i++) {
            if (history[prev][i] != line[i]) { same = 0; break; }
        }
        if (same) return;
    }

    int idx = history_count % HISTORY_SIZE;
    for (int i = 0; i < len; i++) history[idx][i] = line[i];
    history[idx][len] = '\0';
    history_count++;
}

void history_clear_line(void) {
    cmd_len = 0;
    cmd_pos = 0;
    cmd_buf[0] = '\0';
    redraw_line();
}

void history_show(int idx) {
    history_clear_line();
    if (idx < 0 || idx >= history_count) return;
    int real_idx = idx % HISTORY_SIZE;
    const char *line = history[real_idx];
    while (*line && cmd_len < MAX_CMD_LEN - 1) {
        cmd_buf[cmd_len++] = *line;
        line++;
    }
    cmd_pos = cmd_len;
    cmd_buf[cmd_len] = '\0';
    redraw_line();
}

/* ===== Tab completion ===== */
static int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static char tab_fmatches[16][128];
static int tab_ftypes[16];
static int tab_fmcount = 0;
static char tab_file_prefix[256];
static int tab_file_prefix_len = 0;

static void load_tab_files(const char *dir_path) {
    tab_fmcount = 0;
    DIR *d = opendir(dir_path);
    if (!d) return;

    int dlen = 0;
    while (dir_path[dlen]) dlen++;
    char fullpath[256];
    int i;
    for (i = 0; i < dlen && i < 250; i++) fullpath[i] = dir_path[i];
    if (i > 0 && fullpath[i - 1] != '/' && i < 254)
        fullpath[i++] = '/';

    struct dirent *de;
    while ((de = readdir(d)) != NULL && tab_fmcount < 16) {
        /* Check if name matches prefix */
        int nl = 0;
        while (de->d_name[nl] && nl < 127) {
            if (nl >= tab_file_prefix_len) break;
            if (de->d_name[nl] != tab_file_prefix[nl]) break;
            nl++;
        }
        if (nl >= tab_file_prefix_len) {
            int ml = 0;
            while (de->d_name[ml] && ml < 127) { tab_fmatches[tab_fmcount][ml] = de->d_name[ml]; ml++; }
            tab_fmatches[tab_fmcount][ml] = '\0';
            int j;
            for (j = 0; de->d_name[j] && i + j < 254; j++) fullpath[i + j] = de->d_name[j];
            fullpath[i + j] = '\0';
            struct stat st;
            if (stat(fullpath, &st) == 0)
                tab_ftypes[tab_fmcount] = (int)st.st_mode;
            else
                tab_ftypes[tab_fmcount] = S_IFREG;
            tab_fmcount++;
        }
    }
    closedir(d);
}

void tab_complete(void) {
    if (cmd_len == 0) return;

    int last_space = -1;
    for (int i = 0; i < cmd_len; i++)
        if (cmd_buf[i] == ' ') last_space = i;

    const char *partial = cmd_buf + last_space + 1;
    int plen = cmd_len - last_space - 1;

    if (last_space < 0) {
        char match_name[32];
        match_name[0] = '\0';
        int match_count = 0;

        dir_name_count = 0;
        load_dir_names("/bin");
        load_dir_names("/sbin");

        for (int i = 0; i < dir_name_count; i++) {
            if (starts_with(dir_names[i], partial)) {
                strcpy(match_name, dir_names[i]);
                match_count++;
            }
        }
        if (match_count == 1) {
            int namelen = strlen(match_name);
            for (int i = plen; i < namelen; i++)
                insert_char(match_name[i]);
            if (cmd_pos > 0 && cmd_buf[cmd_pos - 1] != ' ')
                insert_char(' ');
        } else if (match_count > 1) {
            shell_putc('\n');
            for (int i = 0; i < dir_name_count; i++) {
                if (starts_with(dir_names[i], partial)) {
                    ansi_write_colored(dir_names[i], 94); /* bright blue */
                    shell_write("  ");
                }
            }
            shell_putc('\n');
            redraw_line();
        }
    } else {
        char dir_path[256];
        char file_prefix[256];
        int flen = 0;

        int last_slash = -1;
        for (int i = 0; i < plen; i++)
            if (partial[i] == '/') last_slash = i;

        if (last_slash >= 0) {
            int dlen = last_slash + 1;
            if (dlen > 255) dlen = 255;
            for (int i = 0; i < dlen; i++) dir_path[i] = partial[i];
            dir_path[dlen] = '\0';

            int rest = plen - last_slash - 1;
            for (int i = 0; i < rest && i < 255; i++) file_prefix[i] = partial[last_slash + 1 + i];
            file_prefix[rest] = '\0';
            flen = rest;
        } else {
            dir_path[0] = '.';
            dir_path[1] = '\0';
            for (int i = 0; i < plen && i < 255; i++) file_prefix[i] = partial[i];
            file_prefix[plen] = '\0';
            flen = plen;
        }

        tab_fmcount = 0;
        for (int i = 0; i < 256 && i < flen; i++) tab_file_prefix[i] = file_prefix[i];
        tab_file_prefix[flen] = '\0';
        tab_file_prefix_len = flen;

        load_tab_files(dir_path);

        if (tab_fmcount == 1) {
            int namelen = 0;
            while (tab_fmatches[0][namelen]) namelen++;
            for (int i = flen; i < namelen; i++)
                insert_char(tab_fmatches[0][i]);
            if (S_ISDIR(tab_ftypes[0]))
                insert_char('/');
        } else if (tab_fmcount > 1) {
            shell_putc('\n');
            for (int i = 0; i < tab_fmcount; i++) {
                if (S_ISDIR(tab_ftypes[i]))
                    ansi_write_colored(tab_fmatches[i], 96); /* bright cyan */
                else
                    ansi_write_colored(tab_fmatches[i], 92); /* bright green */
                shell_write("  ");
            }
            shell_putc('\n');
            redraw_line();
        }
    }
}

/* ===== Shell init + main loop ===== */

void shell_init(void) {
    cmd_len = 0;
    cmd_pos = 0;
    history_count = 0;
    history_pos = 0;
    load_term_size();
    chdir("/home/root");
    redraw_line();
}

void shell_handle_char(unsigned char c) {
    if (edit_active()) {
        extern void edit_handle_char(unsigned char c);
        edit_handle_char(c);
        if (!edit_active()) {
            redraw_line();
        }
        return;
    }

    if (c == '\t') {
        cmd_buf[cmd_len] = '\0';
        tab_complete();
        return;
    }

    if (c == 0x80) {
        if (history_count > 0) {
            if (!history_active) {
                history_save_current();
                history_active = 1;
            }
            if (history_pos > 0) history_pos--;
            if (history_pos >= 0 && history_pos < history_count)
                history_show(history_pos);
        }
        return;
    }
    if (c == 0x81) {
        if (history_count > 0) {
            history_pos++;
            if (history_pos >= history_count) {
                history_clear_line();
                const char *s = history_saved;
                while (*s && cmd_len < MAX_CMD_LEN - 1) {
                    cmd_buf[cmd_len++] = *s;
                    s++;
                }
                cmd_pos = cmd_len;
                cmd_buf[cmd_len] = '\0';
                redraw_line();
                history_pos = history_count;
                history_active = 0;
            } else {
                history_show(history_pos);
            }
        }
        return;
    }
    if (c == 0x82) {
        if (cmd_pos > 0) {
            cmd_pos--;
            redraw_line();
        }
        return;
    }
    if (c == 0x83) {
        if (cmd_pos < cmd_len) {
            cmd_pos++;
            redraw_line();
        }
        return;
    }

    if (c == '\n') {
        redraw_line_plain();
        shell_putc('\n');
        cmd_buf[cmd_len] = '\0';
        history_push(cmd_buf);
        history_pos = history_count;
        history_active = 0;
        shell_exec(cmd_buf);
        cmd_len = 0;
        cmd_pos = 0;
        if (!edit_active()) {
            redraw_line();
        }
    } else if (c == '\b') {
        delete_char();
    } else {
        insert_char(c);
    }
}

int shell_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    shell_init();

    char c;
    for (;;) {
        int n = read(0, &c, 1);
        if (n > 0) {
            shell_handle_char((unsigned char)c);
        } else {
            yield();
        }
    }
    return 0;
}
