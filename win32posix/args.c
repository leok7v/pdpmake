#include "args.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

void args_set(int32_t argc, const char* argv[], const char** env) {
    args.c = argc;
    args.v = argv;
    args.env = env;
}

int32_t args_option_index(const char* option) {
    for (int32_t i = 1; i < args.c; i++) {
        if (strcmp(args.v[i], "--") == 0) { break; } // no options after '--'
        if (strcmp(args.v[i], option) == 0) { return i; }
    }
    return -1;
}

void args_remove_at(int32_t ix) {
    // returns new argc
    assert(0 < args.c);
    assert(0 < ix && ix < args.c); // cannot remove args.v[0]
    for (int32_t i = ix; i < args.c; i++) {
        args.v[i] = args.v[i + 1];
    }
    args.v[args.c - 1] = "";
    args.c--;
}

bool args_option_bool(const char* option) {
    int32_t ix = args_option_index(option);
    if (ix > 0) { args_remove_at(ix); }
    return ix > 0;
}

bool args_option_int(const char* option, int64_t *value) {
    int32_t ix = args_option_index(option);
    if (ix > 0 && ix < args.c - 1) {
        const char* s = args.v[ix + 1];
        int32_t base = (strstr(s, "0x") == s || strstr(s, "0X") == s) ? 16 : 10;
        const char* b = s + (base == 10 ? 0 : 2);
        char* e = (void*)0;
        errno = 0;
        int64_t v = strtoll(b, &e, base);
        if (errno == 0 && e > b && *e == 0) {
            *value = v;
        } else {
            ix = -1;
        }
    } else {
        ix = -1;
    }
    if (ix > 0) {
        args_remove_at(ix); // remove option
        args_remove_at(ix); // remove following number
    }
    return ix > 0;
}

const char* args_option_str(const char* option) {
    int32_t ix = args_option_index(option);
    const char* s = (void*)0;
    if (ix > 0 && ix < args.c - 1) {
        s = args.v[ix + 1];
    } else {
        ix = -1;
    }
    if (ix > 0) {
        args_remove_at(ix); // remove option
        args_remove_at(ix); // remove following string
    }
    return ix > 0 ? s : (void*)0;
}
