#include <sys/auxv.h>

int
main(void)
{
	unsigned long val;

	val = getauxval(AT_HWCAP);

	return (val != 0);
}
