/* Scalar regression of the actual production Padme helper: no chunk allocation. */
#include "platform.h"
#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef CHUNKS_SOURCE
#define CHUNKS_SOURCE "../../../tar/chunks/chunks_write.c"
#endif
#include CHUNKS_SOURCE

int
main(int argc, char ** argv)
{
	uintmax_t length, maximum;
	size_t maxchunk = SIZE_MAX / 2;
	size_t bound = maxchunk + maxchunk / 1000 + 13;
	int converted;

	if (argc == 2 && strcmp(argv[1], "--info") == 0) {
		printf("{\"size_bits\":%zu,\"int_bits\":%zu,\"bound\":%" PRIuMAX "}\n",
		    sizeof(size_t) * CHAR_BIT, sizeof(int) * CHAR_BIT,
		    (uintmax_t)bound);
		return (0);
	}
	if (argc != 1)
		return (2);
	while ((converted = scanf("%" SCNuMAX " %" SCNuMAX,
	    &length, &maximum)) == 2) {
		if (length == 0 || length > maximum || maximum > bound)
			return (2);
		printf("%zu\n", padme((size_t)length, (size_t)maximum));
	}
	return (converted == EOF ? 0 : 2);
}
