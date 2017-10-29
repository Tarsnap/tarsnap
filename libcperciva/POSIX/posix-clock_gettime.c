#include <time.h>

int main() {
	struct timespec ts;

	return (clock_gettime(CLOCK_REALTIME, &ts));
}
