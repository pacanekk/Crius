/*
 * Nexus libc - program entry point.
 *
 * Compiled with -DPROG_MAIN=xxx_main for each program.
 * Linked with libc src files + program .o files.
 *
 * The kernel passes a pointer to struct exec_ctx in RDI
 * when scheduling a new task (or after exec).
 */

#include <unistd.h>
#include <crius/abi.h>

extern int PROG_MAIN(int argc, char **argv);

void _start(void *arg) {
    struct exec_ctx *ctx = (struct exec_ctx *)arg;
    int ret;
    if (ctx) {
        ret = PROG_MAIN(ctx->argc, ctx->argv);
    } else {
        ret = PROG_MAIN(0, (char **)0);
    }
    exit(ret);
    for (;;) __asm__ volatile ("hlt");
}
