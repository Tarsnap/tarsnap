#include <stdint.h>

#include <openssl/aes.h>

int
main(void)
{
	AES_KEY kexp_actual;
	const uint8_t key_unexpanded[16] = { 0 };

	AES_set_encrypt_key(key_unexpanded, 128, &kexp_actual);

	/* Success! */
	return (0);
}
