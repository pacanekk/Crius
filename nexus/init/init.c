/*
 * Nexus Init - PID 1 process
 *
 * The Crius kernel launches this after boot. Init is responsible for:
 *   - mounting filesystems
 *   - creating directory hierarchy
 *   - installing embedded programs
 *   - launching the shell
 *   - monitoring and restarting shell if it crashes
 *
 * The kernel does not know about services, shells, or any userspace policy.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mount.h>
#include <crius/abi.h>

/* Auto-generated embedded program data */
struct embedded_prog {
    const char *path;
    const unsigned char *data;
    size_t size;
};
extern const struct embedded_prog embedded_programs[];
extern const int embedded_program_count;

/* Helper: write a string to stdout */
static void out(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

static void install_programs(void) {
    mkdir("/bin");
    mkdir("/sbin");
    for (int i = 0; i < embedded_program_count; i++) {
        const struct embedded_prog *p = &embedded_programs[i];
        create(p->path);
        write_file(p->path, (const char *)p->data, p->size);
    }
    klog("init: installed programs\n");
}

static void setup_filesystem(void) {
    /* Auto-mount ext2 partitions FIRST, so directory creation
     * goes to the right filesystem (ext2 if mounted, ramfs otherwise). */
    int mounted = 0;

    int hda_fd = open("/dev/hda", O_RDONLY);
    if (hda_fd >= 0) {
        struct block_dev_info dev;
        long ret = ioctl(hda_fd, BLK_GET_INFO, &dev);
        close(hda_fd);

        if (ret == 0 && dev.present) {
            /* Try partitions first */
            for (int p = 0; p < MAX_PARTITIONS; p++) {
                if (!dev.partitions[p].used) continue;
                char devpath[16];
                /* build /dev/<name><part> */
                devpath[0] = '/'; devpath[1] = 'd'; devpath[2] = 'e';
                devpath[3] = 'v'; devpath[4] = '/';
                int dn = 0;
                while (dev.name[dn] && dn < 7) { devpath[5 + dn] = dev.name[dn]; dn++; }
                devpath[5 + dn] = '0' + p + 1;
                devpath[5 + dn + 1] = '\0';

                const char *mntpath = (mounted == 0) ? "/" : "/mnt";
                if (mounted > 0) mkdir("/mnt");
                if (mount(devpath, mntpath) == 0) {
                    mounted++;
                    break;
                }
            }

            /* Try whole disk if no partition mounted */
            if (mounted == 0) {
                char devpath[16];
                devpath[0] = '/'; devpath[1] = 'd'; devpath[2] = 'e';
                devpath[3] = 'v'; devpath[4] = '/';
                int dn = 0;
                while (dev.name[dn] && dn < 7) { devpath[5 + dn] = dev.name[dn]; dn++; }
                devpath[5 + dn] = '\0';

                if (mount(devpath, "/") == 0) mounted++;
            }
        }
    }

    /* Create standard directory hierarchy (userspace policy). */
    mkdir("/mnt");
    mkdir("/boot");
    mkdir("/etc");
    mkdir("/var");
    mkdir("/var/log");
    mkdir("/var/cache");
    mkdir("/opt");
    mkdir("/opt/apps");
    mkdir("/opt/games");
    mkdir("/home");
    mkdir("/home/root");
    mkdir("/home/root/projects");
    mkdir("/home/root/config");
    mkdir("/home/root/downloads");
    mkdir("/home/root/notes");
    mkdir("/sys");
    mkdir("/tmp");

    /* System identity - userspace policy */
    create("/etc/os-release");
    write_file("/etc/os-release", "NAME=\"Crius OS\"\nVERSION=\"0.1\"\n", 30);
    create("/etc/hostname");
    write_file("/etc/hostname", "crius\n", 6);

    /* Set default working directory */
    chdir("/home/root");
}

static void print_banner(void) {
    /* Clear screen via ANSI */
    out("\033[2J");
    /* Move cursor to top-left */
    out("\033[H");

    /* Purple (bright magenta) */
    out("\033[95m");
    out("\n\n\n\n");
    out("   :####:  ######:    ######   ##    ##   :####:  \n");
    out("   ######  #######    ######   ##    ##  :######  \n");
    out(" :##:  .#  ##   :##     ##     ##    ##  ##:  :#  \n");
    out(" ##        ##    ##     ##     ##    ##  ##       \n");
    out(" ##.       ##   :##     ##     ##    ##  ###:     \n");
    out(" ##        #######:     ##     ##    ##  :#####:  \n");

    /* Violet (magenta) */
    out("\033[35m");
    out(" ##        ######       ##     ##    ##   .#####: \n");
    out(" ##.       ##   ##.     ##     ##    ##      :### \n");
    out(" ##        ##   ##      ##     ##    ##        ## \n");
    out(" :##:  .#  ##   :##     ##     ##    ##  #:.  :## \n");
    out("   ######  ##    ##:  ######   :######:  #######: \n");
    out("   :####:  ##    ###  ######    :####:   .#####:  \n\n");

    /* Reset to default color */
    out("\033[0m");
    out("          Crius Kernel + Nexus Userspace\n\n\n");
}

void userspace_init(void) {
    klog("init: userspace started (PID 1)\n");

    /* Check if framebuffer is available via VFS */
    int fbinfo_fd = open("/dev/fbinfo", O_RDONLY);
    if (fbinfo_fd >= 0) {
        close(fbinfo_fd);
        print_banner();
    }

    /* Mount filesystems and create directory hierarchy */
    klog("init: mounting filesystems\n");
    setup_filesystem();

    /* Install embedded programs to ramfs */
    install_programs();

    /* Start shell and monitor it */
    klog("init: launching shell\n");

    for (;;) {
        pid_t pid = fork();
        if (pid < 0) {
            klog("init: fork failed\n");
            yield();
            continue;
        }
        if (pid == 0) {
            char *sargv[] = { "shell", NULL };
            execv("/bin/shell", sargv);
            klog("init: failed to exec shell\n");
            exit(1);
        }

        /* Wait specifically for the shell to exit */
        waitpid(pid, NULL, 0);
        klog("init: shell exited, reaping orphans\n");

        /* Reap any orphaned children that were reparented to init.
         * waitpid(-1) returns -1 when no more children exist. */
        while (waitpid(-1, NULL, 0) > 0)
            ;

        klog("init: restarting shell\n");
    }
}
