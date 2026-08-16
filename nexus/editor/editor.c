#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "api.h"

#define MAX_NAME     32
#define MAX_FILESIZE 4096

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85

/* Terminal dimensions - cached at init */
static int term_cols = 80;
static int term_rows = 25;


static void load_term_size(void) {
    int fd = open("/dev/fbinfo", O_RDONLY);
    if (fd < 0) return;
    char buf[128];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
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

static void cursor_home(void) { write(1, "\033[H", 3); }
static void clear_screen(void) { write(1, "\033[2J", 4); }
static void ansi_clear_to_eol(void) { write(1, "\033[K", 3); }
static void ansi_set_color(int fg) {
    char buf[8]; int n = 0;
    buf[n++] = '\033'; buf[n++] = '[';
    if (fg >= 0) { buf[n++] = '0' + (fg / 10); buf[n++] = '0' + (fg % 10); }
    else buf[n++] = '0';
    buf[n++] = 'm';
    write(1, buf, n);
}
static void ansi_reset(void) { write(1, "\033[0m", 4); }

static void out_colored(const char *s, int fg) {
    ansi_set_color(fg);
    int len = 0; while (s[len]) len++;
    write(1, s, len);
    ansi_reset();
}

/* Inverse video for cursor */
static void ansi_inverse(void) { write(1, "\033[7m", 4); }

static int edit_mode = 0;
static int edit_dirty = 0;
static int edit_scroll = 0;  /* first visible line (0-based) */
static char edit_name[MAX_NAME];
static char edit_buf[MAX_FILESIZE];
static int edit_len = 0;
static int edit_cursor = 0;

static int cursor_line(void) {
    int line = 0;
    for (int i = 0; i < edit_cursor; i++)
        if (edit_buf[i] == '\n') line++;
    return line;
}

static void edit_redraw(void) {
    cursor_home();

    /* Header: filename in bright cyan, dirty '*' in bright yellow, help in gray */
    int hdr_w = 0;
    int max_w = term_cols - 1;
    ansi_set_color(96);
    if (hdr_w < max_w) { write(1, "[ ", 2); hdr_w += 2; }
    for (int k = 0; edit_name[k] && hdr_w < max_w - 4; k++) { write(1, &edit_name[k], 1); hdr_w++; }
    ansi_reset();
    if (edit_dirty) {
        ansi_set_color(93);
        if (hdr_w < max_w) { write(1, " *", 2); hdr_w += 2; }
        ansi_reset();
    }
    ansi_set_color(96);
    if (hdr_w < max_w) { write(1, "]", 1); hdr_w++; }
    ansi_reset();
    ansi_set_color(90);
    const char *hlp = " Esc=save Ctrl+X=quit";
    for (int k = 0; hlp[k] && hdr_w < max_w; k++) { write(1, &hlp[k], 1); hdr_w++; }
    ansi_reset();
    ansi_clear_to_eol();
    write(1, "\n", 1);

    /* Calculate visible window - 1 line for header */
    int max_lines = term_rows - 1;
    if (max_lines < 1) max_lines = 1;

    int cur_line = cursor_line();
    if (cur_line < edit_scroll) edit_scroll = cur_line;
    if (cur_line >= edit_scroll + max_lines) edit_scroll = cur_line - max_lines + 1;

    int line = 0;
    int line_start = 0;
    int displayed = 0;
    int need_nl = 0;

    for (int i = 0; i <= edit_len; i++) {
        int is_line_end = (i == edit_len || edit_buf[i] == '\n');
        if (!is_line_end) continue;

        if (line >= edit_scroll && displayed < max_lines) {
            if (need_nl) write(1, "\n", 1);
            need_nl = 1;

            int line_num = line + 1;
            char ln[8]; int lnp = 0;
            if (line_num < 10) { ln[lnp++] = '0' + line_num; }
            else if (line_num < 100) { ln[lnp++] = '0' + (line_num / 10); ln[lnp++] = '0' + (line_num % 10); }
            else if (line_num < 1000) { ln[lnp++] = '0' + (line_num / 100); ln[lnp++] = '0' + ((line_num / 10) % 10); ln[lnp++] = '0' + (line_num % 10); }
            else { ln[lnp++] = '0' + (line_num / 1000); ln[lnp++] = '0' + ((line_num / 100) % 10); ln[lnp++] = '0' + ((line_num / 10) % 10); ln[lnp++] = '0' + (line_num % 10); }
            ln[lnp++] = '>'; ln[lnp++] = ' ';
            ansi_set_color(90);  /* bright black = gray for line numbers */
            write(1, ln, lnp);
            ansi_reset();

            int line_len = i - line_start;
            int prefix_len = lnp;
            /* Reserve 2 chars: 1 for cursor space, 1 to prevent auto-wrap */
            int max_chars = term_cols - prefix_len - 2;
            if (max_chars < 1) max_chars = 1;
            int truncated = (line_len > max_chars);
            int display_len = truncated ? max_chars - 1 : line_len;
            if (display_len < 0) display_len = 0;

            for (int j = 0; j < display_len; j++) {
                int pos = line_start + j;
                if (pos == edit_cursor) {
                    ansi_inverse();
                    write(1, &edit_buf[pos], 1);
                    ansi_reset();
                } else {
                    write(1, &edit_buf[pos], 1);
                }
            }
            if (edit_cursor == i && !truncated && display_len < max_chars) {
                ansi_inverse();
                write(1, " ", 1);
                ansi_reset();
            }
            if (truncated) {
                ansi_set_color(91);  /* bright red for truncation marker */
                write(1, "$", 1);
                ansi_reset();
            }
            ansi_clear_to_eol();
            displayed++;
        }

        line++;
        line_start = i + 1;
    }

    /* Pad remaining lines with empty cleared lines to overwrite stale content */
    while (displayed < max_lines) {
        if (need_nl) write(1, "\n", 1);
        need_nl = 1;
        ansi_clear_to_eol();
        displayed++;
    }
}

void edit_handle_char(unsigned char c);

int edit_init(int argc, char **argv) {
    if (argc < 2) { out_colored("Usage: edit <name>\n", 31); return 1; }
    load_term_size();
    int j;
    for (j = 0; argv[1][j] && j < MAX_NAME - 1; j++) edit_name[j] = argv[1][j];
    edit_name[j] = '\0';
    edit_len = 0;
    int fd = open(edit_name, O_RDONLY);
    if (fd >= 0) {
        edit_len = read(fd, edit_buf, MAX_FILESIZE);
        if (edit_len < 0) edit_len = 0;
        close(fd);
    }
    if (edit_len > 0 && edit_buf[edit_len - 1] == '\n') edit_len--;
    edit_cursor = edit_len;
    edit_dirty = 0;
    edit_scroll = 0;
    edit_mode = 1;
    clear_screen();
    edit_redraw();
    return 0;
}

int edit_main(int argc, char **argv) {
    if (edit_init(argc, argv) != 0) return 1;
    while (edit_mode) {
        unsigned char c;
        int n = read(0, &c, 1);
        if (n > 0) edit_handle_char(c);
    }
    return 0;
}

static void edit_maybe_redraw(void) {
    edit_redraw();
}

int edit_active(void) { return edit_mode; }

void edit_handle_char(unsigned char c) {
    if (!edit_mode) return;

    if (c == 0x1B || c == 0x13) {
        if (edit_len < MAX_FILESIZE && (edit_len == 0 || edit_buf[edit_len - 1] != '\n'))
            edit_buf[edit_len++] = '\n';
        int fd = open(edit_name, O_WRONLY | O_CREAT);
        if (fd >= 0) {
            write(fd, edit_buf, edit_len);
            close(fd);
        }
        clear_screen();
        out_colored("Saved.\n", 32);
        edit_mode = 0;
        edit_dirty = 0;
        return;
    }
    if (c == 0x18) {
        clear_screen();
        out_colored("Cancelled.\n", 31);
        edit_mode = 0;
        return;
    }
    if (c == KEY_LEFT) { if (edit_cursor > 0) edit_cursor--; edit_maybe_redraw(); return; }
    if (c == KEY_RIGHT) { if (edit_cursor < edit_len) edit_cursor++; edit_maybe_redraw(); return; }
    if (c == KEY_HOME) {
        while (edit_cursor > 0 && edit_buf[edit_cursor - 1] != '\n') edit_cursor--;
        edit_maybe_redraw(); return;
    }
    if (c == KEY_END) {
        while (edit_cursor < edit_len && edit_buf[edit_cursor] != '\n') edit_cursor++;
        edit_maybe_redraw(); return;
    }
    if (c == KEY_UP) {
        int cur_start = edit_cursor;
        while (cur_start > 0 && edit_buf[cur_start - 1] != '\n') cur_start--;
        int col = edit_cursor - cur_start;
        if (cur_start > 0) {
            int prev_nl = cur_start - 1;
            int prev_start = prev_nl;
            while (prev_start > 0 && edit_buf[prev_start - 1] != '\n') prev_start--;
            int prev_len = prev_nl - prev_start;
            edit_cursor = prev_start + col;
            if (edit_cursor > prev_start + prev_len) edit_cursor = prev_start + prev_len;
        } else {
            edit_cursor = 0;
        }
        edit_maybe_redraw(); return;
    }
    if (c == KEY_DOWN) {
        int cur_start = edit_cursor;
        while (cur_start > 0 && edit_buf[cur_start - 1] != '\n') cur_start--;
        int col = edit_cursor - cur_start;
        int cur_end = edit_cursor;
        while (cur_end < edit_len && edit_buf[cur_end] != '\n') cur_end++;
        if (cur_end < edit_len) {
            int next_start = cur_end + 1;
            int next_end = next_start;
            while (next_end < edit_len && edit_buf[next_end] != '\n') next_end++;
            edit_cursor = next_start + col;
            if (edit_cursor > next_end) edit_cursor = next_end;
        }
        edit_maybe_redraw(); return;
    }
    if (c == '\b') {
        if (edit_cursor > 0) {
            for (int i = edit_cursor; i < edit_len; i++) edit_buf[i - 1] = edit_buf[i];
            edit_len--; edit_cursor--; edit_dirty = 1; edit_maybe_redraw();
        }
        return;
    }
    if (c == '\n') {
        if (edit_len < MAX_FILESIZE) {
            for (int i = edit_len; i > edit_cursor; i--) edit_buf[i] = edit_buf[i - 1];
            edit_buf[edit_cursor] = '\n'; edit_len++; edit_cursor++; edit_dirty = 1; edit_maybe_redraw();
        }
        return;
    }
    if (edit_len < MAX_FILESIZE && c >= 0x20 && c < 0x80) {
        for (int i = edit_len; i > edit_cursor; i--) edit_buf[i] = edit_buf[i - 1];
        edit_buf[edit_cursor] = c; edit_len++; edit_cursor++; edit_dirty = 1; edit_maybe_redraw();
    }
}
