#ifndef _ARGS_H
#define _ARGS_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct {
    int32_t c;        // argc
    const char** v;   // argv[argc]
    const char** env; // args.env[] is null-terminated
} args;

void args_set(int32_t argc, const char* argv[], const char** env);

int32_t args_option_index(const char* option);
/* e.g. option: "--verbosity" or "-v" */

void args_remove_at(int32_t ix);

/* If option is present in args_option_* call it and possibly following
   argument is removed from args.v and arg.c is decremented */

/* args.c=3 args.v={"foo", "--verbose"} ->
   returns true; argc=1 argv={"foo"} */
bool args_option_bool(const char* option);

/* args.c=3 args.v={"foo", "--n", "153"} ->
   returns true; value==153, true; argc=1 argv={"foo"}
   also handles negative values (e.g. "-153") and hex (e.g. 0xBADF00D) */
bool args_option_int(const char* option, int64_t *value);

/* for args.c=3 args.v={"foo", "--path", "bar"}
       args_option_str("--path", option)
   returns option: "bar" and args.c=1 args.v={"foo"} */
const char* args_option_str(const char* option);

#ifdef __cplusplus
}
#endif

#endif
