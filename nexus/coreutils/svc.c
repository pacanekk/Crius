#include <stdint.h>
#include <string.h>
#include "api.h"

int svc_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    prog_print_color("svc: kernel service management has been removed.\n"
                     "Services are now managed by userspace (init).\n",
                     0x00FFFF00, 0);
    return 0;
}
