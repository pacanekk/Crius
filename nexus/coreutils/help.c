#include <string.h>
#include "api.h"

int help_main(int argc, char **argv) {
    (void)argc; (void)argv;
    prog_print_color("Shell builtins:\n", 0x00FFFF00, 0);
    prog_print_color("  cd <dir>  - change directory\n", 0x00FFFFFF, 0);
    prog_print_color("  exit      - exit shell\n", 0x00FFFFFF, 0);
    prog_print_color("  export    - set env var: export PATH=/bin:/sbin\n", 0x00FFFFFF, 0);
    prog_print_color("  which     - show path of command\n", 0x00FFFFFF, 0);
    prog_print_color("  echo $VAR - print text or env var ($PATH, $HOSTNAME)\n", 0x00FFFFFF, 0);
    prog_print_color("\n/bin programs:\n", 0x0000FF00, 0);
    prog_print_color("  echo      - print text\n", 0x00FFFFFF, 0);
    prog_print_color("  clear     - clear screen\n", 0x00FFFFFF, 0);
    prog_print_color("  reboot    - reboot system\n", 0x00FFFFFF, 0);
    prog_print_color("  ls        - list directory (ls -s for sizes)\n", 0x00FFFFFF, 0);
    prog_print_color("  cat       - print file\n", 0x00FFFFFF, 0);
    prog_print_color("  write     - write file: write <name> <text>\n", 0x00FFFFFF, 0);
    prog_print_color("  append    - append to file\n", 0x00FFFFFF, 0);
    prog_print_color("  rm        - delete file/dir\n", 0x00FFFFFF, 0);
    prog_print_color("  mkdir     - make directory\n", 0x00FFFFFF, 0);
    prog_print_color("  pwd       - print working directory\n", 0x00FFFFFF, 0);
    prog_print_color("  edit      - line editor: edit <name>\n", 0x00FFFFFF, 0);
    prog_print_color("\n/sbin programs:\n", 0x0000FFFF, 0);
    prog_print_color("  ps        - list tasks\n", 0x00FFFFFF, 0);
    prog_print_color("  kill      - kill task: kill <id>\n", 0x00FFFFFF, 0);
    prog_print_color("  sleep     - sleep N ms: sleep <ms>\n", 0x00FFFFFF, 0);
    prog_print_color("  priority  - set priority: priority <id> <0-5>\n", 0x00FFFFFF, 0);
    prog_print_color("  disk      - show disk info\n", 0x00FFFFFF, 0);
    prog_print_color("  rdisk     - read sector: rdisk <lba>\n", 0x00FFFFFF, 0);
    prog_print_color("  wdisk     - write sector: wdisk <lba> <text>\n", 0x00FFFFFF, 0);
    prog_print_color("  mount     - mount device: mount <dev> <path>\n", 0x00FFFFFF, 0);
    prog_print_color("  umount    - unmount: umount <path>\n", 0x00FFFFFF, 0);
    prog_print_color("  svc       - service manager\n", 0x00FFFFFF, 0);
    return 0;
}
