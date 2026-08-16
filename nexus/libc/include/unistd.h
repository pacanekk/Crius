#ifndef NEXUS_UNISTD_H
#define NEXUS_UNISTD_H

#include <stddef.h>
#include <errno.h>
#include <sys/types.h>

struct mem_stats;

/* Standard file descriptor numbers */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ===== Process ===== */
void    exit(int code);
pid_t   fork(void);
int     exec(const char *path, int argc, char **argv);
pid_t   wait(pid_t pid);
int     kill(pid_t pid);
void    sleep(unsigned ticks);
void    yield(void);
pid_t   getpid(void);
int     setpriority(int pid, int prio);

/* ===== I/O (fd-based) ===== */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
int     ioctl(int fd, unsigned long request, void *arg);

/* ===== Filesystem (path-based) ===== */
int     mkdir(const char *path);
int     unlink(const char *path);
int     chdir(const char *path);
int     getcwd(char *buf, size_t size);

/* ===== Filesystem helpers (non-standard, used by init) ===== */
int     create(const char *path);
int     write_file(const char *path, const char *data, size_t len);

/* ===== System ===== */
void    reboot(void);
void    klog(const char *msg);
int     getmemstats(struct mem_stats *ms);

#endif
