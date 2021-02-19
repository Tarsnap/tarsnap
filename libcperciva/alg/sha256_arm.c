#include "cpusupport.h"
#ifdef CPUSUPPORT_ARM_SHA256
/**
 * CPUSUPPORT CFLAGS: ARM_SHA256
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "sha256_arm.h"

/* SHA256 round constants. */
static const uint32_t Krnd[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Round computation. */
#define RND4(S0, S1, M, Kp) do {				\
		uint32x4_t S0_step;				\
		uint32x4_t Wk;					\
		S0_step = S0;					\
		Wk = vaddq_u32(M, vld1q_u32(Kp));		\
		S0 = vsha256hq_u32(S0, S1, Wk);			\
		S1 = vsha256h2q_u32(S1, S0_step, Wk);		\
	} while (0)

/* Message schedule computation */
#define MSG4(X0, X1, X2, X3)					\
	X0 = vsha256su1q_u32(vsha256su0q_u32(X0, X1), X2, X3)

/**
 * SHA256_Transform_arm(state, block, W, S):
 * Compute the SHA256 block compression function, transforming ${state} using
 * the data in ${block}.  This implementation uses ARM SHA256 instructions,
 * and should only be used if _SHA256 is defined and cpusupport_arm_sha256()
 * returns nonzero.  The arrays W and S may be filled with sensitive data, and
 * should be cleared by the callee.
 */
#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
void
SHA256_Transform_arm(uint32_t state[8], const uint8_t block[64],
    uint32_t W[64], uint32_t S[8])
#else
void
SHA256_Transform_arm(uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8])
#endif
{
	uint32x4_t Y[4];
	uint32x4_t S0, S1;
	uint32x4_t _state[2];
	int i;

	(void)W; /* UNUSED */
	(void)S; /* UNUSED */

	/* 1. Prepare the first part of the message schedule W. */
	Y[0] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[0])));
	Y[1] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[16])));
	Y[2] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[32])));
	Y[3] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[48])));

	/* 2. Initialize working variables. */
	S0 = _state[0] = vld1q_u32(&state[0]);
	S1 = _state[1] = vld1q_u32(&state[4]);

	/* 3. Mix. */
	for (i = 0; i < 64; i += 16) {
		RND4(S0, S1, Y[0], &Krnd[i + 0]);
		RND4(S0, S1, Y[1], &Krnd[i + 4]);
		RND4(S0, S1, Y[2], &Krnd[i + 8]);
		RND4(S0, S1, Y[3], &Krnd[i + 12]);

		if (i == 48)
			break;
		MSG4(Y[0], Y[1], Y[2], Y[3]);
		MSG4(Y[1], Y[2], Y[3], Y[0]);
		MSG4(Y[2], Y[3], Y[0], Y[1]);
		MSG4(Y[3], Y[0], Y[1], Y[2]);
	}

	/* 4. Mix local working variables into global state. */
	vst1q_u32(&state[0], vaddq_u32(_state[0], S0));
	vst1q_u32(&state[4], vaddq_u32(_state[1], S1));
}
#endif /* CPUSUPPORT_ARM_SHA256 */
