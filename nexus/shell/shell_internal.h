#ifndef SHELL_INTERNAL_H
#define SHELL_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#define MAX_CMD_LEN     256
#define HISTORY_SIZE    32

extern char cmd_buf[MAX_CMD_LEN];
extern int cmd_len;
extern int cmd_pos;

extern char shell_path[128];
extern char shell_hostname[64];

extern int term_cols;
extern int term_rows;

void shell_write(const char *s);
void shell_putc(char c);
void ansi_set_color(int fg);
void ansi_reset_color(void);
void ansi_write_colored(const char *s, int fg);

void redraw_line(void);
void redraw_line_plain(void);
void insert_char(char c);
void delete_char(void);

void history_push(const char *line);
void history_save_current(void);
void history_clear_line(void);
void history_show(int idx);

void tab_complete(void);

void shell_exec(char *line);

void load_term_size(void);

#endif
