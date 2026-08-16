#ifndef NEXUS_API_H
#define NEXUS_API_H

#include <stddef.h>
#include <stdint.h>

#define PROG_MAX_ARGS 16

typedef int (*prog_main_t)(int argc, char **argv);

void prog_print(const char *s);
void prog_print_color(const char *s, uint32_t fg, uint32_t bg);
void prog_putc(char c, uint32_t fg, uint32_t bg);
void prog_newline(void);

#endif
