/*
 * Do the actual making for make
 */
#include "make.h"

struct name *target;

void
remove_target(void)
{
	if (!dryrun && !print && !precious &&
			target && !(target->n_flag & N_PRECIOUS) &&
			unlink(target->n_name) == 0) {
		warning("'%s' removed", target->n_name);
	}
}

/*
 * Do commands to make a target
 */
static int
docmds1(struct name *np, struct rule *rp)
{
	int estat = 0;	// 0 exit status is success
	char *q, *command;
	struct cmd *cp;

	for (cp = rp->r_cmd; cp; cp = cp->c_next) {
		uint8_t ssilent, signore, sdomake;

		q = command = expand_macros(cp->c_cmd);
		ssilent = silent || (np->n_flag & N_SILENT) || dotouch;
		signore = ignore || (np->n_flag & N_IGNORE);
		sdomake = !dryrun && !dotouch;
		for (;;) {
			if (*q == '@')	// Specific silent
				ssilent = TRUE + 1;
			else if (*q == '-')	// Specific ignore
				signore = TRUE;
			else if (*q == '+')	// Specific domake
				sdomake = TRUE + 1;
			else
				break;
			q++;
		}

		if (sdomake > TRUE) {
			// '+' must not override '@' or .SILENT
			if (ssilent != TRUE + 1 && !(np->n_flag & N_SILENT))
				ssilent = FALSE;
		} else if (!sdomake)
			ssilent = dotouch;

		if (!ssilent)
			puts(q);

		if (sdomake) {
			// Get the shell to execute it
			int status;
			char *cmd = !signore ? xconcat3("set -e;", q, "") : q;

			target = np;
			status = system(cmd);
			target = NULL;
			if (status == -1) {
				error("couldn't execute '%s'", q);
			} else if (status != 0 && !signore) {
				warning("failed to build '%s'", np->n_name);
				if (status == SIGINT || status == SIGQUIT)
					remove_target();
				if (errcont)
					estat = 1;	// 1 exit status is failure
				else
					exit(status);
			}
			if (!signore)
				free(cmd);
		}
		free(command);
	}
	return estat;
}

static int
docmds(struct name *np)
{
	struct rule *rp;
	int estat = 0;	// 0 exit status is success

	for (rp = np->n_rule; rp; rp = rp->r_next)
		estat |= docmds1(np, rp);
	return estat;
}

/*
 * Update the modification time of a file to now.
 */
static void
touch(struct name *np)
{
	if (dryrun || !silent)
		printf("touch %s\n", np->n_name);

	if (!dryrun) {
		const struct timespec timebuf[2] = {{0, UTIME_NOW}, {0, UTIME_NOW}};

		if (utimensat(AT_FDCWD, np->n_name, timebuf, 0) < 0) {
			if (errno == ENOENT) {
				int fd = open(np->n_name, O_RDWR | O_CREAT, 0666);
				if (fd > 0) {
					close(fd);
					return;
				}
			}
			warning("touch %s failed: %s\n", np->n_name, strerror(errno));
		}
	}
}

static int
make1(struct name *np, struct rule *rp, char *newer, struct name *implicit)
{
	int estat = 0;	// 0 exit status is success
	char *name, *member = NULL, *base;

	name = splitlib(np->n_name, &member);
	setmacro("?", newer, 0);
	setmacro("%", member, 0);
	setmacro("@", name, 0);
	if (implicit) {
		setmacro("<", implicit->n_name, 0);
		base = member ? member : name;
		*suffix(base) = '\0';
		setmacro("*", base, 0);
	}
	free(name);

	if (rp)		// rp set if doing a :: rule
		estat = docmds1(np, rp);
	else
		estat = docmds(np);

	if (dotouch)
		touch(np);

	return estat;
}

/*
 * Recursive routine to make a target.
 */
int
make(struct name *np, int level)
{
	struct depend *dp;
	struct rule *rp;
	struct name *impdep = NULL;	// implicit prerequisite
	char *newer = NULL;
	time_t dtime = 1;
	bool didsomething = 0;
	bool estat = 0;	// 0 exit status is success

	if (np->n_flag & N_DONE)
		return 0;

	if (!np->n_time)
		modtime(np);		// Get modtime of this file

	if (!(np->n_flag & N_DOUBLE)) {
		// Check if target has explicit build commands
		for (rp = np->n_rule; rp; rp = rp->r_next)
			if (rp->r_cmd)
				break;

		// If not look for an implicit rule
		if (!rp)
			impdep = dyndep(np, NULL);

		// As a last resort check for a default rule
		if (!(np->n_flag & N_TARGET) && np->n_time == 0L) {
			struct name *dflt = findname(".DEFAULT");
			if (!dflt)
				error("don't know how to make %s", np->n_name);
			addrule(np, NULL, dflt->n_rule->r_cmd, FALSE);
			impdep = np;
		}
	}

	for (rp = np->n_rule; rp; rp = rp->r_next) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// Each double colon rule is handled separately.
		if ((np->n_flag & N_DOUBLE)) {
			// If the rule has no commands look for an implicit rule.
			impdep = NULL;
			if (!rp->r_cmd) {
				impdep = dyndep(np, rp);
				if (!impdep)
					error("don't know how to make %s", np->n_name);
			}
			// A rule with no prerequisities is executed unconditionally.
			if (!rp->r_dep)
				dtime = np->n_time;
		}
#endif
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Make prerequisite
			estat |= make(dp->d_name, level+1);

			// Make a string listing prerequisites newer than target
			// (but not if we were invoked with -q).
			if (!quest && np->n_time <= dp->d_name->n_time)
				newer = xappendword(newer, dp->d_name->n_name);
			dtime = MAX(dtime, dp->d_name->n_time);
		}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if ((np->n_flag & N_DOUBLE)) {
			if (!quest && np->n_time <= dtime) {
				if (estat == 0) {
					estat = make1(np, rp, newer, impdep);
					dtime = 1;
					didsomething = 1;
				}
				free(newer);
				newer = NULL;
			}
			if (impdep) {
				dp = rp->r_dep->d_next;
				free(rp->r_dep);
				rp->r_dep = dp;
				rp->r_cmd = NULL;
			}
		}
#endif
	}

	np->n_flag |= N_DONE;

	if (quest) {
		if (np->n_time <= dtime) {
			time(&np->n_time);
			return 1;	// 1 means rebuild is needed
		}
	} else if (np->n_time <= dtime && !(np->n_flag & N_DOUBLE)) {
		if (estat == 0) {
			estat = make1(np, NULL, newer, impdep);
			time(&np->n_time);
		} else {
			warning("'%s' not built due to errors", np->n_name);
		}
		free(newer);
	} else if (level == 0 && !didsomething) {
		printf("%s: '%s' is up to date\n", myname, np->n_name);
	}
	return estat;
}