#ifndef _WIN32_GLOB_COMPAT
#define _WIN32_GLOB_COMPAT
#ifdef _WIN32
#if !HAVE_DECL_GLOB

// On successful completion, glob() returns zero. Other possible returns are:
enum
{
    GLOB_NOSPACE = 1, /* Out of memory */
    GLOB_ABORTED,     /* Read error */
    GLOB_NOMATCH,     /* No matches */
};

/**
 * The argument flags are made up of the bitwise OR of zero or more of the
 * following symbolic constants, which modify the behavior if glob():
 */
#define GLOB_APPEND (1 << 5) // Append results to previous call
#define GLOB_BRACE (1 << 9)  // Expand brace expressions (GNU EXTENSION)

#define	GLOB_DOOFFS	    0x0002	/* Use gl_offs. */
#define	GLOB_ERR	    0x0004	/* Return on error. */
#define	GLOB_MARK	    0x0008	/* Append / to matching directories. */
#define	GLOB_NOCHECK	0x0010	/* Return pattern itself if nothing matches. */
#define	GLOB_NOSORT	0x0020	/* Don't sort. */
#define	GLOB_NOESCAPE	0x1000	/* Disable backslash escaping. */

#define	GLOB_NOSPACE	(-1)	/* Malloc call failed. */
#define	GLOB_ABORTED	(-2)	/* Unignored error. */
#define	GLOB_NOMATCH	(-3)	/* No match and GLOB_NOCHECK not set. */
#define	GLOB_NOSYS	    (-4)	/* Function not supported. */

/**
 * The results of a glob() call are stored in this structure.
 */
typedef struct
{
    size_t gl_pathc; /* Count of paths matched so far  */
    char **gl_pathv; /* List of matched pathnames.  */
    size_t gl_offs;  /* Slots to reserve in gl_pathv (not used).  */
} glob_t;

/**
 * The glob() function searches for all the pathnames matching pattern
 * according to the rules used by the shell. The errfunc parameter is not
 * supported and must be set to NULL.
 */
int glob(
    const char *pattern,
    int flags,
    int (*errfunc)(const char *epath, int eerrno),
    glob_t *pglob);

/**
 * Destroy memory allocated in pglob by the glob() function.
 */
void globfree(glob_t *pglob);

#endif // !HAVE_DECL_GLOB
#endif // _WIN32
#endif // _WIN32_GLOB_COMPAT
