#ifndef PRINT_SEPARATOR_H_
#define PRINT_SEPARATOR_H_

#include <stdio.h>

/**
 * print_separator(stream, separator, print_nulls, num_nulls):
 * Print ${separator} to ${stream} if ${print_nulls} is zero; otherwise,
 * print ${num_nulls} '\0'.
 */
int print_separator(FILE *, char *, int, int);

#endif /* !PRINT_SEPARATOR_H_ */
