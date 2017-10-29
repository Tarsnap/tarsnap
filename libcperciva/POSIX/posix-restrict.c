static int
foo(char * restrict x, char * restrict y)
{

	return (x == y);
}

int
main() {
	char x[10];
	char y[10];

	return (foo(x, y));
}
