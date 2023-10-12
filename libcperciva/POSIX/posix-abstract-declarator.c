#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
static int func(int ARGNAME[static restrict 1]);
#else
static int func(int [static restrict 1]);
#endif

int
func(int arr[static restrict 1])
{

	(void)arr; /* UNUSED */

	/* Success! */
	return (0);
}

int
main(void)
{

	(void)func; /* UNUSED */

	/* Success! */
	return (0);
}
