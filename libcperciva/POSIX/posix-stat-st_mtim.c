#include <sys/stat.h>

int
main(void)
{
	struct stat sb;

	/* Can we reference st_mtim? */
	(void)sb.st_mtim.tv_sec;

	/* Success! */
	return (0);
}
