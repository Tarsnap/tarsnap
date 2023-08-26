static int
foo(const char * restrict x, const char * restrict y)
{

	return (x == y);
}

int
main(void)
{
	char x[10];
	char y[10];

	return (foo(x, y));
}
