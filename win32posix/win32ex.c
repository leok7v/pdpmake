#include "win32ex.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#include <Windows.h>

char *basename(char *path) {
    char *base = strrchr(path, '\\');
    if (base) {
        return base + 1;
    }
    base = strrchr(path, '/');
    return base ? base + 1 : path;
}

void timespec_to_filetime(const struct timespec *ts, FILETIME *ft) {
    // Convert timespec to 100-nanosecond intervals since January 1, 1601 (UTC)
    LONGLONG ll = Int32x32To64(ts->tv_sec, 10000000) + ts->tv_nsec / 100;
    // Copy the result to FILETIME structure
    ft->dwLowDateTime = (DWORD)ll;
    ft->dwHighDateTime = (DWORD)(ll >> 32);
}

int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags) {
    (void)flags; // unused only value is AT_SYMLINK_NOFOLLOW
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    if (dirfd != AT_FDCWD) {
        assert(FALSE); // not implemented
        return -1;
    }
    HANDLE hFile;
    FILETIME atime, mtime;
    hFile = CreateFile(pathname, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (times) {
        if (times[0].tv_nsec == UTIME_NOW) {
            atime = now;
        } else {
            timespec_to_filetime(&times[0], &atime);
        }
        if (times[1].tv_nsec == UTIME_NOW) {
            mtime = now;
        } else {
            timespec_to_filetime(&times[1], &mtime);
        }
    } else {
        atime = now;
        mtime = atime;
    }
    if (!SetFileTime(hFile, NULL, &atime, &mtime)) {
        CloseHandle(hFile);
        return -1;
    }
    CloseHandle(hFile);
    return 0;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    if (clk_id == CLOCK_REALTIME) {
        return timespec_get(tp, TIME_UTC);
    }
    // Handle other clock IDs if necessary
    return -1;
}

int setenv(const char* name, const char* value, int overwrite) {
    int r = 0;
    if (!overwrite) {
        DWORD bytes = GetEnvironmentVariable(name, NULL, 0);
        bool not_found = bytes == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND;
        // https://pubs.opengroup.org/onlinepubs/009604499/functions/setenv.html
        // "If the environment variable named by envname already exists
        //  and the value of overwrite is zero, the function shall return
        //  success and the environment shall remain unchanged."
        if (not_found) {
            r = SetEnvironmentVariable(name, value) ? 0 : -1;
        }
    } else {
        r = SetEnvironmentVariable(name, value) ? 0 : -1;
    }
    if (r != 0) { errno = ENOMEM; }
    return r;
}

static bool file_exist(const char* path) {
    DWORD attr = GetFileAttributes(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static char* escape_quotes(const char* command) {
    size_t len = strlen(command);
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        char prev = i > 0 ? command[i - 1] : '\0';
        if (command[i] == '"' && prev != '\\') {
            count++;
        }
    }
    if (count == 0) {
        return strdup(command);
    }
    char* escaped = calloc(len + count + 1, 1);
    if (!escaped) {
        errno = ENOMEM;
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char prev = i > 0 ? command[i - 1] : '\0';
        if (command[i] == '"' && prev != '\\') {
            escaped[j++] = '\\';
        }
        escaped[j++] = command[i];
    }
    return escaped;
}

int win32_system_via_sh(const char *commands) {
    char* command = escape_quotes(commands);
    if (!command) {
        errno = ENOMEM;
        return -1;
    }
    const char* sh = NULL;
    const char* shell = getenv("SHELL");
    if (shell && file_exist(shell)) {
        sh = shell;
    } else {
        static const char* shells[] = {
            "/bin/sh.exe",
            "/usr/bin/sh.exe",
            "/usr/local/bin/sh.exe",
            "/mingw/bin/sh.exe",
            "/Program Files/Git/bin/sh.exe"
        };
        for (size_t i = 0; i < sizeof(shells) / sizeof(shells[0]); i++) {
            if (file_exist(shells[i])) {
                sh = shells[i];
                break;
            }
        }
    }
    if (!sh) {
        fprintf(stderr, "sh.exe not found\n");
        fprintf(stderr, "Download from https://frippery.org/busybox/\n");
        fprintf(stderr, "Copy and run:\n");
        fprintf(stderr, "c:\\bin\\busybox64u.exe --install\n");
        fprintf(stderr, "for Intel x64; or\n");
        fprintf(stderr, "c:\\bin\\busybox64a.exe --install\n");
        fprintf(stderr, "for ARM64\n");
        free(command);
        return -1;
    }
    size_t len = strlen(command) + strlen(sh) + 16;
    char* cmdline = calloc(len, 1);
    if (!cmdline) {
        free(command);
        errno = ENOMEM;
        return -1;
    } else {
        snprintf(cmdline, len, "\"%s\" -c \"%s\"", sh, command);
        PROCESS_INFORMATION pi = {0};
        STARTUPINFO si = {0};
        si.cb = sizeof(si); // Required by CreateProcess
        if (!CreateProcessA(
                NULL,
                cmdline,
                NULL,            // Process security attributes
                NULL,            // Thread security attributes
                FALSE,           // Inherit handles
                0,               // Creation flags
                NULL,            // Environment block (NULL uses the current environment)
                NULL,            // Current directory (NULL uses the current directory)
                &si,             // STARTUPINFO
                &pi)) {          // PROCESS_INFORMATION
            fprintf(stderr, "Error: CreateProcess failed with error %d\n", GetLastError());
            free(cmdline);
            free(command);
            errno = ENOENT;
            return -1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
            fprintf(stderr, "Error: Unable to get exit code\n");
            exit_code = (DWORD)-1;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        free(cmdline);
        free(command);
        return (int)exit_code;
    }
}

int opterr;
int optopt;
int optind;
char * optarg;

int getopt(int argc, char *const *argv, const char *options) {
    (void)argc; // unused
    (void)argv; // unused
    (void)options; // unused
    if (1) { abort(); } // NOT IMPLEMENTED and shouldn't be called
    return 0;
}

char * stpcpy(char *dest, const char *src) {
    size_t len = strlen (src);
    return (char*)memcpy(dest, src, len + 1) + len;
}

char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *new_str = (char *)malloc(len + 1);
    if (new_str) {
        memcpy(new_str, s, len);
        new_str[len] = '\0';
    }
    return new_str;
}

int kill(int pid, int sig) {
    (void)sig; // unused
    HANDLE h = OpenProcess(PROCESS_TERMINATE, 0, pid);
    if (h) {
        int r = 0;
        if (!TerminateProcess(h, 1)) {
            errno = EPERM;
            r = -1;
        }
        CloseHandle(h);
        return r;
    } else {
        errno = ENOENT;
        return -1;
    }
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    if (signum == SIGINT) {
        if (oldact) {
            oldact->sa_handler = signal(SIGINT, act->sa_handler);
        } else {
            signal(SIGINT, act->sa_handler);
        }
        return 0;
    }
    // Add more signal handling as needed
    return -1;
}

int sigemptyset(sigset_t *set) {
    *set = 0;
    return 0;
}

