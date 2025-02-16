#include <openssl/rsa.h>

int
main(void)
{
	RSA * rsa;

	/* Allocate and free a RSA key. */
	rsa = RSA_new();
	RSA_free(rsa);

	/* Success! */
	return (0);
}
