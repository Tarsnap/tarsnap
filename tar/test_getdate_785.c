/* Regression harness for tarsnap issue #785.
 *
 * Verifies that "MONTH DAY YEAR" spellings agree with comma/ISO forms,
 * and that legacy interpretations are preserved:
 *   - "Jun 15 20" must keep its pre-fix meaning (a clock token, not a
 *     year) -- i.e. it resolves within the current year.
 *   - "513" still means 05:13.
 *   - Month-day-only forms still resolve inside the current year.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

time_t get_date(time_t now, char *p);

static int failures = 0;

static void
expect_eq(const char *a, const char *b, const char *why)
{
	time_t now = time(NULL);
	time_t ta = get_date(now, (char *)a);
	time_t tb = get_date(now, (char *)b);

	if (ta != tb) {
		printf("FAIL: %s\n  \"%s\" -> %lld\n  \"%s\" -> %lld\n",
		    why, a, (long long)ta, b, (long long)tb);
		failures++;
	} else {
		printf("ok:   %-24s == %-22s (%s)\n", a, b, why);
	}
}

/* The parsed date must fall inside the calendar year of `now`. */
static void
expect_same_year(const char *a, const char *why)
{
	time_t now = time(NULL);
	time_t ta;
	struct tm tm_now, tm_a;

	localtime_r(&now, &tm_now);
	ta = get_date(now, (char *)a);
	localtime_r(&ta, &tm_a);

	if (tm_now.tm_year != tm_a.tm_year) {
		printf("FAIL: %s\n  \"%s\" -> year %d (expected %d)\n", why,
		    a, tm_a.tm_year + 1900, tm_now.tm_year + 1900);
		failures++;
	} else {
		printf("ok:   %-24s -> current year      (%s)\n", a, why);
	}
}

static void
expect_hm(const char *a, int hour, int min, const char *why)
{
	time_t now = time(NULL);
	time_t ta = get_date(now, (char *)a);
	struct tm tm_a;

	localtime_r(&ta, &tm_a);
	if (tm_a.tm_hour != hour || tm_a.tm_min != min) {
		printf("FAIL: %s\n  \"%s\" -> %02d:%02d (expected %02d:%02d)\n",
		    why, a, tm_a.tm_hour, tm_a.tm_min, hour, min);
		failures++;
	} else {
		printf("ok:   %-24s -> %02d:%02d            (%s)\n",
		    a, tm_a.tm_hour, tm_a.tm_min, why);
	}
}

int
main(void)
{
	/* Core acceptance from issue #785: all three threshold spellings
	 * for "15 June 2020" must agree. */
	expect_eq("Jun 15 2020", "Jun 15, 2020", "issue #785 core");
	expect_eq("Jun 15 2020", "2020-06-15", "issue #785 core");

	/* More month-day-year forms */
	expect_eq("Aug 24 2019", "2019-08-24", "mdy vs iso");
	expect_eq("December 31 1999", "Dec 31, 1999", "full month name");
	expect_eq("Jan 1 2000 UTC", "Jan 1, 2000 UTC", "with timezone");

	/* Legacy behaviors that must not change */
	expect_eq("Jun 15, 2020", "2020-06-15", "comma form unchanged");
	expect_eq("12 Sept 1997", "Sep 12, 1997", "dmy form unchanged");
	expect_eq("Jun 15 2020 513", "Jun 15 2020 05:13", "date+clock combo");
	expect_hm("Jun 15 20", 20, 0, "2-digit token stays an hour");
	expect_same_year("May 3", "month-day only unchanged");

	if (failures) {
		printf("\n%d FAILURE(S)\n", failures);
		return (1);
	}
	printf("\nALL PASS\n");
	return (0);
}
