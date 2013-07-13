#include "bsdtar_platform.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "warnp.h"

#include "chunkify.h"

/*
 * Internal chunkifier data.
 */
struct chunkifier_internal {
	/* Chunkification parameters */
	uint32_t mu;		/* Desired mean chunk length */
	uint32_t p;		/* Modulus */
	uint32_t pp;		/* - p^(-1) mod 2^32 */
	uint32_t ar;		/* alpha * 2^32 mod p */
	uint32_t * cm;		/* Coefficient map modulo p */
	uint32_t htlen;		/* Size of hash table in 2-word entries */
	uint32_t blen;		/* Length of buf[] */
	uint32_t w;		/* Minimum substring length; size of b[] */

	/* Callback parameters */
	chunkify_callback * chunkdone;	/* Callback */
	void * cookie;		/* Cookie passed to callback */

	/* Current state */
	uint32_t k;		/* Number of bytes in chunk so far */
	uint32_t r;		/* floor(sqrt(4 * k - mu)) */
	uint32_t rs;		/* (r + 1)^2 - (4 * k - mu) */
	uint32_t akr;		/* a^k * 2^32 mod p */
	uint32_t yka;		/* Power series truncated before x^k term */
				/* evaluated at a mod p */
	uint32_t * b;		/* Circular buffer of values waiting to */
				/* be added to the hash table. */
	uint32_t * ht;		/* Hash table; pairs of the form (yka, k). */
	uint8_t * buf;		/* Buffer of bytes processed */
};

static int isprime(uint32_t n);
static uint32_t nextprime(uint32_t n);
static uint32_t mmul(uint32_t a, uint32_t b, uint32_t p, uint32_t pp);
static int minorder(uint32_t ar, uint32_t ord, uint32_t p, uint32_t pp);
static uint32_t isqrt(uint32_t x);
static void chunkify_start(CHUNKIFIER * c);

/* Returns nonzero iff n is prime. */
static int
isprime(uint32_t n)
{
	uint32_t x;

	for (x = 2; (x * x <= n) && (x < 65536); x++)
		if ((n % x) == 0)
			return (0);

	return (n > 1);
}

/*
 * Returns the smallest prime satisfying n <= p < 2^32, or 0 if none exist.
 */
static uint32_t
nextprime(uint32_t n)
{
	volatile uint32_t p;

	for (p = n; p != 0; p++)
		if (isprime(p))
			break;

	return (p);
}

/**
 * Compute $(a * b + (a * b * pp \bmod 2^{32}) * p) / 2^{32}$.
 * Note that for $b \leq p$ this is at most $p * (1 + a / 2^{32})$.
 */
static uint32_t
mmul(uint32_t a, uint32_t b, uint32_t p, uint32_t pp)
{
	uint64_t ab;
	uint32_t abpp;

	ab = (uint64_t)(a) * (uint64_t)(b);
	abpp = (uint32_t)(ab) * pp;
	ab += (uint64_t)(abpp) * (uint64_t)(p);
	return (ab >> 32);
}

/*
 * Returns nonzero if (ar / 2^32) has multiplicative order at least ord mod p.
 */
static int
minorder(uint32_t ar, uint32_t ord, uint32_t p, uint32_t pp)
{
	uint32_t akr;
	uint32_t akr0;
	uint32_t k;

	akr = akr0 = (- p) % p;

	for (k = 0; k < ord; k++) {
		akr = mmul(akr, ar, p, pp) % p;
		if (akr == akr0)
			return (0);
	}

	return (1);
}

/*
 * Return the greatest y such that y^2 <= x.
 */
static uint32_t
isqrt(uint32_t x)
{
	uint32_t y;

	for (y = 1; y < 65536; y++)
		if (y * y > x)
			break;

	return (y - 1);
}

/*
 * Prepare the CHUNKIFIER for input.
 */
static void
chunkify_start(CHUNKIFIER * c)
{
	uint32_t i;

	/* No entries in the hash table. */
	for (i = 0; i < c->htlen; i++)
		c->ht[i * 2] = - c->htlen;

	/* Nothing in the queue waiting to be added to the table, either. */
	for (i = 0; i < c->w; i++)
		c->b[i] = c->p;

	/* No bytes input yet. */
	c->akr = (- c->p) % c->p;
	c->yka = 0;
	c->k = 0;
	c->r = 0;
	c->rs = 1 + c->mu;
}

CHUNKIFIER *
chunkify_init(uint32_t meanlen, uint32_t maxlen,
    chunkify_callback * chunkdone, void * cookie)
{
	CHUNKIFIER * c;
	uint8_t hbuf[32];	/* HMAC of something */
	uint8_t pbuf[2];	/* 'p\0', 'a\0', or 'x' . i */
	uint32_t pmin;
	uint32_t i;

	/*
	 * Parameter verification.
	 */
	if ((meanlen > 1262226) || (maxlen <= meanlen)) {
		warn0("Incorrect API usage");
		return (NULL);
	}

	/*
	 * Allocate memory for CHUNKIFIER structure.
	 */
	c = malloc(sizeof(*c));
	if (c == NULL)
		return (NULL);

	/*
	 * Initialize fixed chunkifier parameters.
	 */
	c->mu = meanlen;
	c->blen = maxlen;
	c->w = 32;
	c->chunkdone = chunkdone;
	c->cookie = cookie;

	/*-
	 * Compute the necessary hash table size.  At any given time, there
	 * are sqrt(4 k - mu) entries and up to sqrt(4 k - mu) tombstones in
	 * the hash table, and we want table inserts and lookups to be fast,
	 * so we want these to use up no more than 50% of the table.  We also
	 * want the table size to be a power of 2.
	 *
	 * Consequently, the table size should be the least power of 2 in
	 * excess of 4 * sqrt(4 maxlen - mu) = 8 * sqrt(maxlen - mu / 4).
	 */
	c->htlen = 8;
	for (i = c->blen - c->mu / 4; i > 0; i >>= 2)
		c->htlen <<= 1;

	/*
	 * Allocate memory for buffers.
	 */
	c->cm = c->b = c->ht = NULL;
	c->buf = NULL;

	c->cm = malloc(256 * sizeof(c->cm[0]));
	c->b = malloc(c->w * sizeof(c->b[0]));
	c->ht = malloc(c->htlen * 2 * sizeof(c->ht[0]));
	c->buf = malloc(c->blen * sizeof(c->buf[0]));

	if ((c->cm == NULL) || (c->b == NULL) ||
	    (c->ht == NULL) || (c->buf == NULL))
		goto err;

	/* Generate parameter values by computing HMACs. */

	/* p is generated from HMAC('p\0'). */
	pbuf[0] = 'p';
	pbuf[1] = 0;
	if (crypto_hash_data(CRYPTO_KEY_HMAC_CPARAMS, pbuf, 2, hbuf))
		goto err;
	memcpy(&c->p, hbuf, sizeof(c->p));

	/* alpha is generated from HMAC('a\0'). */
	pbuf[0] = 'a';
	pbuf[1] = 0;
	if (crypto_hash_data(CRYPTO_KEY_HMAC_CPARAMS, pbuf, 2, hbuf))
		goto err;
	memcpy(&c->ar, hbuf, sizeof(c->ar));

	/* cm[i] is generated from HMAC('x' . i). */
	for (i = 0; i < 256; i++) {
		pbuf[0] = 'x';
		pbuf[1] = i;
		if (crypto_hash_data(CRYPTO_KEY_HMAC_CPARAMS, pbuf, 2, hbuf))
			goto err;
		memcpy(&c->cm[i], hbuf, sizeof(c->cm[i]));
	}

	/*
	 * Using the generated pseudorandom values, actually generate
	 * the parameters we want.
	 */

	/*
	 * We want p to be approximately mu^(3/2) * 1.009677744.  Compute p
	 * to be at least floor(mu*floor(sqrt(mu))*1.01) and no more than
	 * floor(sqrt(mu)) - 1 more than that.
	 */
	pmin = c->mu * isqrt(c->mu);
	pmin += pmin / 100;
	c->p = nextprime(pmin + (c->p % isqrt(c->mu)));
	/* c->p <= 1431655739 < 1431655765 = floor(2^32 / 3) */

	/* Compute pp = - p^(-1) mod 2^32. */
	c->pp = ((2 * c->p + 4) & 8) - c->p;	/* pp = - 1/p mod 2^4 */
	c->pp *= 2 + c->p * c->pp;		/* pp = - 1/p mod 2^8 */
	c->pp *= 2 + c->p * c->pp;		/* pp = - 1/p mod 2^16 */
	c->pp *= 2 + c->p * c->pp;		/* pp = - 1/p mod 2^32 */

	/*
	 * We want to have 1 < ar < p - 1 and the multiplicative order of
	 * alpha mod p greater than mu.
	 */
	c->ar = 2 + (c->ar % (c->p - 3));
	while (! minorder(c->ar, c->mu, c->p, c->pp)) {
		c->ar = c->ar + 1;
		if (c->ar == c->p)
			c->ar = 2;
	}

	/*
	 * Prepare for incoming data.
	 */
	chunkify_start(c);

	/*
	 * Return initialized CHUNKIFIER.
	 */
	return (c);

err:
	/* free(NULL) is safe, so it doesn't matter where we jumped from. */
	free(c->buf);
	free(c->ht);
	free(c->b);
	free(c->cm);
	free(c);
	return (NULL);
}

int
chunkify_write(CHUNKIFIER * c, const uint8_t * buf, size_t buflen)
{
	uint32_t htpos;
	uint32_t yka_tmp;
	size_t i;
	int rc;

	for (i = 0; i < buflen; i++) {
		/* Add byte to buffer. */
		c->buf[c->k] = buf[i];

		/* k := k + 1 */
		c->k++;
		while (c->rs <= 4) {
			c->rs += 2 * c->r + 1;
			c->r += 1;
		}
		c->rs -= 4;

		/*
		 * If k = blen, then we've filled the buffer and we
		 * automatically have the end of the chunk.
		 */
		if (c->k == c->blen)
			goto endofchunk;

		/*
		 * Don't waste time on arithmetic if we don't have enough
		 * data yet for a permitted loop to ever occur.
		 */
		if (c->r == 0)
			continue;

		/*
		 * Update state to add new character.
		 */

		/* y_k(a) := y_k(a) + a^k * x_k mod p */
		/* yka <= p * (2 + p / (2^32 - p)) <= p * 2.5 < 2^31 + p */
		c->yka += mmul(c->akr, c->cm[buf[i]], c->p, c->pp);

		/* Each step reduces yka by p iff yka >= p. */
		c->yka -= c->p & (((c->yka - c->p) >> 31) - 1);
		c->yka -= c->p & (((c->yka - c->p) >> 31) - 1);

		/* a^k := a^k * alpha mod p */
		/* akr <= p * 2^32 / (2^32 - p) */
		c->akr = mmul(c->akr, c->ar, c->p, c->pp);

		/*
		 * Check if yka is in the hash table.
		 */
		htpos = c->yka & (c->htlen - 1);
		do {
			/* Have we found yka? */
			if (c->ht[2 * htpos + 1] == c->yka) {
				/* Recent enough to be a valid entry? */
				if (c->k - c->ht[2 * htpos] - 1 < c->r)
					goto endofchunk;
			}

			/* Have we found an empty space? */
			if (c->k - c->ht[2 * htpos] - 1 >= 2 * c->r)
				break;

			/* Move to the next position in the table. */
			htpos = (htpos + 1) & (c->htlen - 1);
		} while (1);

		/*
		 * Insert queued value into table.
		 */
		yka_tmp = c->b[c->k & (c->w - 1)];
		htpos = yka_tmp & (c->htlen - 1);
		do {
			/* Have we found an empty space or tombstone? */
			if (c->k - c->ht[2 * htpos] - 1 >= c->r) {
				c->ht[2 * htpos] = c->k;
				c->ht[2 * htpos + 1] = yka_tmp;
				break;
			}

			/* Move to the next position in the table. */
			htpos = (htpos + 1) & (c->htlen - 1);
		} while (1);

		/*
		 * Add current value into queue.
		 */
		c->b[c->k & (c->w - 1)] = c->yka;

		/*
		 * Move on to next byte.
		 */
		continue;

endofchunk:
		/*
		 * We've reached the end of a chunk.
		 */
		rc = chunkify_end(c);
		if (rc)
			return (rc);
	}

	return (0);
}

int
chunkify_end(CHUNKIFIER * c)
{
	int rc;

	/* If we haven't started the chunk yet, don't end it either. */
	if (c->k == 0)
		return (0);

	/* Process the chunk. */
	rc = (c->chunkdone)(c->cookie, c->buf, c->k);
	if (rc)
		return (rc);

	/* Prepare for more input. */
	chunkify_start(c);

	return (0);
}

void
chunkify_free(CHUNKIFIER * c)
{

	/* Behave consistently with free(NULL). */
	if (c == NULL)
		return;

	/* Free everything. */
	free(c->buf);
	free(c->ht);
	free(c->b);
	free(c->cm);
	free(c);
}
