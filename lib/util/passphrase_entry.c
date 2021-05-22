#include <stdlib.h>
#include <string.h>

#include "passphrase_entry.h"
#include "readpass.h"
#include "warnp.h"

/**
 * passphrase_entry_parse(arg, entry_method_p, entry_arg_p):
 * Parse "METHOD:ARG" from ${arg} into an ${*entry_method_p}:${*entry_arg_p}.
 */
int
passphrase_entry_parse(const char * arg,
    enum passphrase_entry * passphrase_entry_p, const char ** passphrase_arg_p)
{
	const char * p;

	/* Find the separator in "method:arg", or fail if there isn't one. */
	if ((p = strchr(arg, ':')) == NULL)
		goto err1;

	/* Extract the "arg" part. */
	*passphrase_arg_p = &p[1];

	/* Parse the "method". */
	if (strncmp(arg, "dev:", 4) == 0) {
		if (strcmp(*passphrase_arg_p, "tty-stdin") == 0) {
			*passphrase_entry_p = PASSPHRASE_TTY_STDIN;
			goto success;
		}
		else if (strcmp(*passphrase_arg_p, "stdin-once") == 0) {
			*passphrase_entry_p = PASSPHRASE_STDIN_ONCE;
			goto success;
		}
		else if (strcmp(*passphrase_arg_p, "tty-once") == 0) {
			*passphrase_entry_p = PASSPHRASE_TTY_ONCE;
			goto success;
		}
	}
	if (strncmp(arg, "env:", 4) == 0) {
		*passphrase_entry_p = PASSPHRASE_ENV;
		goto success;
	}
	if (strncmp(arg, "file:", 5) == 0) {
		*passphrase_entry_p = PASSPHRASE_FILE;
		goto success;
	}

err1:
	warn0("Invalid option: --passphrase %s", arg);

	/* Failure! */
	return (-1);

success:
	/* Success! */
	return (0);
}

/**
 * passphrase_entry_readpass(passwd, entry_method, entry_arg, prompt,
 *     confirmprompt, once):
 * Use ${entry_method} to read a passphrase and return it as a malloced
 * NUL-terminated string via ${passwd}.  If ${entry_method} is
 * PASSPHRASE_TTY_STDIN and ${once} is zero, ask for the passphrase twice;
 * otherwise ask for it once.  Use ${prompt} for the first prompt, and
 * ${confirmprompt} for the second prompt (if applicable).
 */
int
passphrase_entry_readpass(char ** passwd,
    enum passphrase_entry passphrase_entry, const char * passphrase_arg,
    const char * prompt, const char * confirmprompt, int once)
{
	const char * passwd_env;

	switch (passphrase_entry) {
	case PASSPHRASE_TTY_STDIN:
		/* Read passphrase, prompting only once if decrypting. */
		if (readpass(passwd, prompt, (once) ? NULL : confirmprompt, 1))
			goto err0;
		break;
	case PASSPHRASE_STDIN_ONCE:
		/* Read passphrase, prompting only once, from stdin only. */
		if (readpass(passwd, prompt, NULL, 0))
			goto err0;
		break;
	case PASSPHRASE_TTY_ONCE:
		/* Read passphrase, prompting only once, from tty only. */
		if (readpass(passwd, prompt, NULL, 2))
			goto err0;
		break;
	case PASSPHRASE_ENV:
		/* We're not allowed to modify the output of getenv(). */
		if ((passwd_env = getenv(passphrase_arg)) == NULL) {
			warn0("Failed to read from ${%s}", passphrase_arg);
			goto err0;
		}

		/* This allows us to use the same insecure_zero() logic. */
		if ((*passwd = strdup(passwd_env)) == NULL) {
			warnp("Out of memory");
			goto err0;
		}
		break;
	case PASSPHRASE_FILE:
		if (readpass_file(passwd, passphrase_arg))
			goto err0;
		break;
	case PASSPHRASE_UNSET:
		warn0("Programming error: passphrase_entry is not set");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
