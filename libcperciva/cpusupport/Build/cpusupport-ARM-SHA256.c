#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

int
main(void)
{
	uint32x4_t w0 = {0};
	uint32x4_t w4 = {0};
	uint32x4_t output;

	output = vsha256su0q_u32(w0, w4);
	(void)output; /* UNUSED */

	return (0);
}
