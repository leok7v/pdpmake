#ifdef _WIN32
#if !HAVE_DECL_GLOB

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdbool.h>

#include "glob.h"

#define EXPAND_COUNT 8
#define UNSUPPORTED_FLAGS(flags) (~(GLOB_APPEND | GLOB_BRACE) & flags)

/**
 * Append path to pglob. The fields pglob->gl_pathc and pglob->pathv must be
 * initialized to 0 and NULL respectively, before calling this function for the
 * first time. The field pglob->gl_pathv contains an array of strings allocated
 * on the heap. The field pglob->gl_pathc contains the number of allocated
 * elements.
 */
static bool GlobAppend(glob_t *const pglob, const char *const path)
{
    assert(pglob != NULL);
    assert(path != NULL);
    assert(
        (pglob->gl_pathv == NULL && pglob->gl_pathc == 0)
        || (pglob->gl_pathv != NULL && pglob->gl_pathc != 0));

    char *const match = _strdup(path);
    if (match == NULL)
    {
        return false;
    }

    /* Ensure there is enough capacity to hold the new element and the
     * terminating NULL-pointer.
     */
    if (((pglob->gl_pathc + 2) % EXPAND_COUNT == 0)
        || (pglob->gl_pathv == NULL))
    {
        char **const pathv = (char **) realloc(
            pglob->gl_pathv,
            (pglob->gl_pathc + 2 + EXPAND_COUNT) * sizeof(char *));
        if (pathv == NULL)
        {
            free(match);
            return false;
        }
        pglob->gl_pathv = pathv;
    }

    pglob->gl_pathv[pglob->gl_pathc++] = match;
    pglob->gl_pathv[pglob->gl_pathc] = NULL;

    return true;
}

/**
 * Copies at most n bytes from str into a duplicate. If str is longer than n,
 * only n bytes are copied, and the terminating null byte is added.
 */
static char *StringNDuplicate(const char *const str, size_t n)
{
    assert(str != NULL);

    char *dup = (char *) malloc(n + 1);
    if (dup == NULL)
    {
        return NULL;
    }

    _memccpy(dup, str, '\0', n);
    dup[n] = '\0';

    return dup;
}

/**
 * Returns a new string formatted according the fmt.
 */
static char *StringFormat(const char *fmt, ...)
{
    assert(fmt != NULL);

    /* Calculate required length */
    va_list ap;
    va_start(ap, fmt);
    const int len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    assert(len >= 0);
    if (len < 0)
    {
        return NULL;
    }

    // Allocate required memory
    char *str = (char *) malloc(len + 1 /* null-byte */);
    if (str == NULL)
    {
        return NULL;
    }

    // Produce output string according to format
    va_start(ap, fmt);
    const int ret = vsnprintf(str, (size_t) len + 1 /* null-byte */, fmt, ap);
    va_end(ap);

    assert(ret == len);
    if (ret != len)
    {
        free(str);
        return NULL;
    }

    return str;
}

/**
 * Destroys an array of strings by first freeing count elements from arr,
 * followed by freeing arr.
 */
static inline void StringArrayDestroy(char **const arr, const size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        free(arr[i]);
    }
    free(arr);
}

/**
 * Create an array of strings by splitting str on delimitors. The number of
 * elements are stored in count.
 */
static char **StringSplit(
    const char *const str, const char *const delimitors, size_t *const count)
{
    assert(str != NULL);
    assert(count != NULL);

    size_t used = 0, capacity = EXPAND_COUNT;

    char **components = (char **) malloc(capacity * sizeof(char *));
    if (components == NULL)
    {
        return NULL;
    }

    const char *start = str;
    const char *end = strpbrk(str, delimitors);

    while (end != NULL)
    {
        if (used + 1 >= capacity)
        {
            capacity *= 2;
            char **temp =
                (char **) realloc(components, capacity * sizeof(char *));
            if (temp == NULL)
            {
                StringArrayDestroy(components, used);
                return NULL;
            }
            components = temp;
        }

        char *component = StringNDuplicate(start, end - start);
        if (component == NULL)
        {
            StringArrayDestroy(components, used);
            return NULL;
        }
        components[used++] = component;

        start = end + 1;
        end = strpbrk(start, delimitors);
    }

    char *component = _strdup(start);
    if (component == NULL)
    {
        StringArrayDestroy(components, used);
        return NULL;
    }
    components[used++] = component;

    *count = used;
    return components;
}

/**
 * Expand wild cards in the first element of components, by concatinating it
 * with root (unless root is NULL). The components variable should hold an
 * array of strings, each representing a component in a path separated by
 * back-/forward slashes. The count parameter should hold the number of
 * elements contained in components. This function recusively calls itself, by
 * popping and concatenating first element of components with root. Upon
 * finding a file that matches the glob, the match is appened to pglob. The
 * initial call to this function should set the root parameter to NULL;
 */
static bool ExpandWildcards(
    const char *const root,
    const char *const *const components,
    const size_t count,
    glob_t *const pglob)
{
    if (count == 0)
    {
        // Base case reached; root should hold the match
        if (!GlobAppend(pglob, root))
        {
            return false;
        }
        return true;
    }

    char *const pattern = (root == NULL)
                              ? _strdup(components[0])
                              : StringFormat("%s\\%s", root, components[0]);

    if (strpbrk(pattern, "*?") == NULL && count > 1)
    {
        /* There are no wildcards to expand, and there is no need to check for
         * the existence of a child node */
        if (!ExpandWildcards(pattern, components + 1, count - 1, pglob))
        {
            free(pattern);
            return false;
        }
        free(pattern);
        return true;
    }

    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return true;
    }

    do
    {
        if (strcmp(data.cFileName, ".") == 0
            || strcmp(data.cFileName, "..") == 0)
        {
            /* Traversing the . and .. directories would cause infinite
             * recursion */
            continue;
        }

        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count != 1)
        {
            continue;
        }

        char *const next = (root == NULL)
                               ? StringFormat("%s", data.cFileName)
                               : StringFormat("%s\\%s", root, data.cFileName);
        if (next == NULL)
        {
            FindClose(handle);
            return false;
        }

        if (!ExpandWildcards(next, components + 1, count - 1, pglob))
        {
            free(next);
            FindClose(handle);
            return false;
        }
        free(next);
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
    return true;
}

/**
 * This function expands braces in pattern if the GLOB_BRACE flag is set in
 * flags. The the resulting patterns are stored as a string array in result,
 * and the number of elements are stored in count;
 */
static bool ExpandBraces(
    const char *const pattern,
    char ***result,
    size_t *const count,
    const int flags)
{
    assert(pattern != NULL);

    char *const start = _strdup(pattern);
    if (start == NULL)
    {
        return false;
    }

    /* We don't expand braces unless the brace flag is set. However, we do
     * return an array of 1 element containing the pattern itself.
     */
    if (!(flags & GLOB_BRACE))
    {
        *result = (char **) malloc(sizeof(char *));
        if (*result == NULL)
        {
            return false;
        }
        *result[0] = start;
        *count = 1;
        return true;
    }

    /* First we will split the string into three parts based on the qurly brace
     * delimitors. We look for the shortest possible match. E.g., the string
     * "foo{{bar,baz}qux,}" becomes "foo{", "bar,baz", "qux,}".
     */
    char *left = NULL, *right = NULL;
    for (char *ch = start; *ch != '\0'; ch++)
    {
        if (*ch == '{')
        {
            left = ch;
        }
        else if (left != NULL && *ch == '}')
        {
            right = ch;
            break;
        }
    }

    /* Check if base case is reached. I.e., if there is no curly brace pair to
     * expand then return the pattern itself as the result.
     */
    if (right == NULL)
    {
        char **const temp =
            (char **) realloc(*result, sizeof(char *) * (*count + 1));
        if (temp == NULL)
        {
            free(start);
            return false;
        }
        temp[*count] = start;
        *count += 1;
        *result = temp;
        return true;
    }
    *left = '\0';
    *right = '\0';

    /* Next we split the middle part (between the braces) on the comma
     * delimitor. E.g., the string "bar,baz" becomes "bar", "baz".
     */
    char *middle = left + 1, *end = right + 1;
    size_t split_count;
    char **const split = StringSplit(middle, ",", &split_count);
    if (split == NULL)
    {
        free(start);
        return false;
    }

    /* Lastly we combine the three parts for each split element, and
     * recursively expand them.
     */
    for (size_t i = 0; i < split_count; i++)
    {
        char *const next = StringFormat("%s%s%s", start, split[i], end);
        if (next == NULL)
        {
            free(start);
            return false;
        }

        if (!ExpandBraces(next, result, count, flags))
        {
            free(next);
            free(start);
            return false;
        }
        free(next);
    }
    return true;
}

int glob(
    const char *pattern,
    const int flags,
    int (*const errfunc)(const char *epath, int eerrno),
    glob_t *const pglob)
{
    // Return error if unsupported flags are used
    if (UNSUPPORTED_FLAGS(flags))
    {
        return GLOB_ABORTED;
    }

    // Return error if unsupportted errfunc is used
    if (errfunc != NULL)
    {
        return GLOB_ABORTED;
    }

    // Initialize pglob unless append flag is set
    if (!(flags & GLOB_APPEND))
    {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = NULL;
    }

    char **subpatterns = NULL;
    size_t n_subpatterns = 0;
    if (!ExpandBraces(pattern, &subpatterns, &n_subpatterns, flags))
    {
        StringArrayDestroy(subpatterns, n_subpatterns);
        return GLOB_NOSPACE;
    }

    for (size_t i = 0; i < n_subpatterns; i++)
    {
        const char *const subpattern = subpatterns[i];

        /* Split path into components. E.g., the path "D:\Projects\\Glo*"
         * becomes "D:", "Projects", "", "Glo*".
         */
        size_t n_components;
        char **const components =
            StringSplit(subpattern, "\\/", &n_components);
        if (components == NULL)
        {
            StringArrayDestroy(subpatterns, n_subpatterns);
            globfree(pglob);
            return GLOB_NOSPACE;
        }

        // Remove trailing empty components.
        while (n_components > 0 && *components[n_components - 1] == '\0')
        {
            free(components[n_components - 1]);
            n_components -= 1;
        }

        if (!ExpandWildcards(NULL, components, n_components, pglob))
        {
            StringArrayDestroy(components, n_components);
            StringArrayDestroy(subpatterns, n_subpatterns);
            globfree(pglob);
            return false;
        }
        StringArrayDestroy(components, n_components);
    }

    StringArrayDestroy(subpatterns, n_subpatterns);
    return (pglob->gl_pathc == 0) ? GLOB_NOMATCH : 0;
}

void globfree(glob_t *pglob)
{
    assert(pglob != NULL);

    for (size_t i = 0; i < pglob->gl_pathc; i++)
    {
        char *path = pglob->gl_pathv[i];
        free(path);
    }
    free(pglob->gl_pathv);
    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
}

#endif // !HAVE_DECL_GLOB
#endif // _WIN32
