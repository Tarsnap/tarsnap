#include "cpusupport.h"

#ifdef CPUSUPPORT_HWCAP_GETAUXVAL
#include <sys/auxv.h>

#if defined(__arm__)
/**
 * Workaround for a glibc bug: <bits/hwcap.h> contains a comment saying:
 *     The following must match the kernel's <asm/hwcap.h>.
 * However, it does not contain any of the HWCAP2_* entries from <asm/hwcap.h>.
 */
#ifndef HWCAP2_AES
#include <asm/hwcap.h>
#endif
#endif /* __arm__ */
#endif /* CPUSUPPORT_HWCAP_GETAUXVAL */

CPUSUPPORT_FEATURE_DECL(arm, aes)
{
	int supported = 0;

#if defined(CPUSUPPORT_ARM_AES)
#if defined(CPUSUPPORT_HWCAP_GETAUXVAL)
	unsigned long capabilities;

#if defined(__aarch64__)
	capabilities = getauxval(AT_HWCAP);
	supported = (capabilities & HWCAP_AES) ? 1 : 0;
#elif defined(__arm__)
	capabilities = getauxval(AT_HWCAP2);
	supported = (capabilities & HWCAP2_AES) ? 1 : 0;
#endif
#endif /* CPUSUPPORT_HWCAP_GETAUXVAL */
#endif /* CPUSUPPORT_ARM_AES */

	/* Return the supported status. */
	return (supported);
}
