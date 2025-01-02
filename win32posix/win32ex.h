#ifndef WIN32EX_H
#define WIN32EX_H

#include <time.h>

#define NORETURN __declspec(noreturn)
#define UTIME_NOW ((time_t)-1)
#define CLOCK_REALTIME 0
#define AT_FDCWD -100


int clock_gettime(int clk_id, struct timespec *tp);

int setenv(const char* name, const char* value, int n);

int win32_system_via_sh(const char *command);

extern int opterr;
extern int optopt;
extern int optind;
extern char * optarg;

int getopt(int argc, char *const *argv, const char *options);

char * stpcpy(char *dest, const char *src);

char *strndup(const char *s, size_t n);

int kill(int pid, int sig);

typedef unsigned int sigset_t;

struct sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    sigset_t sa_mask;
};

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

int sigemptyset(sigset_t *set);

char *realpath(const char *path, char *resolved_path);

int utimensat(int dirfd, const char *pathname, const struct timespec times[2],
              int flags);

#endif // WIN32EX_H

