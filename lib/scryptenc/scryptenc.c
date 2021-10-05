/*-
 * Copyright 2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_aes.h"
#include "crypto_aesctr.h"
#include "crypto_entropy.h"
#include "crypto_verify_bytes.h"
#include "humansize.h"
#include "insecure_memzero.h"
#include "sha256.h"
#include "sysendian.h"
#include "warnp.h"

#include "crypto_scrypt.h"
#include "memlimit.h"
#include "scryptenc_cpuperf.h"

#include "scryptenc.h"

#define ENCBLOCK 65536

static int pickparams(size_t, double, double,
    int *, uint32_t *, uint32_t *, int);
static int checkparams(size_t, double, double, int, uint32_t, uint32_t, int,
    int);
#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
static int scryptdec_file_load_header(FILE * infile, uint8_t header[static 96]);
#else
static int scryptdec_file_load_header(FILE *, uint8_t [static 96]);
#endif

struct scryptdec_file_cookie {
	FILE *	infile;		/* This is not owned by this cookie. */
	uint8_t	header[96];
	uint8_t	dk[64];
};

static void
display_params(int logN, uint32_t r, uint32_t p, size_t memlimit,
    double opps, double maxtime)
{
	uint64_t N = (uint64_t)(1) << logN;
	uint64_t mem_minimum = 128 * r * N;
	double expected_seconds = opps > 0 ? (double)(4 * N * r * p) / opps : 0;
	char * human_memlimit = humansize(memlimit);
	char * human_mem_minimum = humansize(mem_minimum);

	/* Parameters */
	fprintf(stderr, "Parameters used: N = %" PRIu64 "; r = %" PRIu32
	    "; p = %" PRIu32 ";\n", N, r, p);

	/* Memory */
	fprintf(stderr, "    Decrypting this file requires at least"
	    " %s of memory", human_mem_minimum);
	if (memlimit > 0)
		fprintf(stderr, " (%s available)", human_memlimit);

	/* CPU time */
	if (opps > 0)
		fprintf(stderr, ",\n    and will take approximately %.1f "
		    "seconds (limit: %.1f seconds)", expected_seconds, maxtime);
	fprintf(stderr, ".\n");

	/* Clean up */
	free(human_memlimit);
	free(human_mem_minimum);
}

static int
pickparams(size_t maxmem, double maxmemfrac, double maxtime,
    int * logN, uint32_t * r, uint32_t * p, int verbose)
{
	size_t memlimit;
	double opps;
	double opslimit;
	double maxN, maxrp;
	uint64_t checkN;
	int rc;

	/* Figure out how much memory to use. */
	if (memtouse(maxmem, maxmemfrac, &memlimit))
		return (SCRYPT_ELIMIT);

	/* Figure out how fast the CPU is. */
	if ((rc = scryptenc_cpuperf(&opps)) != 0)
		return (rc);
	opslimit = opps * maxtime;

	/* Allow a minimum of 2^15 salsa20/8 cores. */
	if (opslimit < 32768)
		opslimit = 32768;

	/* Fix r = 8 for now. */
	*r = 8;

	/*
	 * The memory limit requires that 128Nr <= memlimit, while the CPU
	 * limit requires that 4Nrp <= opslimit.  If opslimit < memlimit/32,
	 * opslimit imposes the stronger limit on N.
	 */
#ifdef DEBUG
	fprintf(stderr, "Requiring 128Nr <= %zu, 4Nrp <= %f\n",
	    memlimit, opslimit);
#endif
	if (opslimit < (double)memlimit / 32) {
		/* Set p = 1 and choose N based on the CPU limit. */
		*p = 1;
		maxN = opslimit / (*r * 4);
		for (*logN = 1; *logN < 63; *logN += 1) {
			checkN = (uint64_t)(1) << *logN;

			/*
			 * Find the largest power of two <= maxN, which is
			 * also the least power of two > maxN/2.
			 */
			if ((double)checkN > maxN / 2)
				break;
		}
	} else {
		/* Set N based on the memory limit. */
		maxN = (double)(memlimit / (*r * 128));
		for (*logN = 1; *logN < 63; *logN += 1) {
			checkN = (uint64_t)(1) << *logN;
			if ((double)checkN > maxN / 2)
				break;
		}

		/* Choose p based on the CPU limit. */
		checkN = (uint64_t)(1) << *logN;
		maxrp = (opslimit / 4) / (double)checkN;
		if (maxrp > 0x3fffffff)
			maxrp = 0x3fffffff;
		*p = (uint32_t)(maxrp) / *r;
	}

	if (verbose)
		display_params(*logN, *r, *p, memlimit, opps, maxtime);

	/* Success! */
	return (SCRYPT_OK);
}

static int
checkparams(size_t maxmem, double maxmemfrac, double maxtime,
    int logN, uint32_t r, uint32_t p, int verbose, int force)
{
	size_t memlimit;
	double opps;
	double opslimit;
	uint64_t N;
	int rc;

	/* Sanity-check values. */
	if ((logN < 1) || (logN > 63))
		return (SCRYPT_EINVAL);
	if ((uint64_t)(r) * (uint64_t)(p) >= 0x40000000)
		return (SCRYPT_EINVAL);
	if ((r == 0) || (p == 0))
		return (SCRYPT_EINVAL);

	/* Are we forcing decryption, regardless of resource limits? */
	if (!force) {
		/* Figure out the maximum amount of memory we can use. */
		if (memtouse(maxmem, maxmemfrac, &memlimit))
			return (SCRYPT_ELIMIT);

		/* Figure out how fast the CPU is. */
		if ((rc = scryptenc_cpuperf(&opps)) != 0)
			return (rc);
		opslimit = opps * maxtime;

		if (verbose)
			display_params(logN, r, p, memlimit, opps, maxtime);

		/* Check limits. */
		N = (uint64_t)(1) << logN;
		if (((memlimit / N) / r < 128) &&
		    (((opslimit / (double)N) / r) / p < 4))
			return (SCRYPT_EBIGSLOW);
		if ((memlimit / N) / r < 128)
			return (SCRYPT_ETOOBIG);
		if (((opslimit / (double)N) / r) / p < 4)
			return (SCRYPT_ETOOSLOW);
	} else {
		/* We have no limit. */
		memlimit = 0;
		opps = 0;

		if (verbose)
			display_params(logN, r, p, memlimit, opps, maxtime);
	}

	/* Success! */
	return (SCRYPT_OK);
}

/*
 * NOTE: The caller is responsible for sanitizing ${dk}, including if this
 * function fails.
 */
static int
scryptenc_setup(uint8_t header[96], uint8_t dk[64],
    const uint8_t * passwd, size_t passwdlen,
    struct scryptenc_params * P, int verbose, int force)
{
	uint8_t salt[32];
	uint8_t hbuf[32];
	uint64_t N;
	SHA256_CTX ctx;
	uint8_t * key_hmac = &dk[32];
	HMAC_SHA256_CTX hctx;
	int rc;

	/* Determine parameters. */
	if (P->logN != 0) {
		/* Check logN, r, p. */
		if ((rc = checkparams(P->maxmem, P->maxmemfrac, P->maxtime,
		    P->logN, P->r, P->p, verbose, force)) != 0) {
			/* Warn about resource limit, but suppress the error. */
			if ((rc == SCRYPT_ETOOBIG) || (rc == SCRYPT_EBIGSLOW))
				warn0("Warning: Explicit parameters"
				    " might exceed memory limit");
			if ((rc == SCRYPT_ETOOSLOW) || (rc == SCRYPT_EBIGSLOW))
				warn0("Warning: Explicit parameters"
				    " might exceed time limit");
			if ((rc == SCRYPT_ETOOBIG) || (rc == SCRYPT_ETOOSLOW) ||
			    (rc == SCRYPT_EBIGSLOW))
				rc = 0;

			/* Provide a more meaningful error message. */
			if (rc == SCRYPT_EINVAL)
				rc = SCRYPT_EPARAM;

			/* Bail if we haven't suppressed the error. */
			if (rc != 0)
				return (rc);
		}
	} else {
		/* Pick values for N, r, p. */
		if ((rc = pickparams(P->maxmem, P->maxmemfrac, P->maxtime,
		    &P->logN, &P->r, &P->p, verbose)) != 0)
			return (rc);
	}

	/* Sanity check. */
	assert((P->logN > 0) && (P->logN < 64));

	/* Set N. */
	N = (uint64_t)(1) << P->logN;

	/* Get some salt. */
	if (crypto_entropy_read(salt, 32))
		return (SCRYPT_ESALT);

	/* Generate the derived keys. */
	if (crypto_scrypt(passwd, passwdlen, salt, 32, N, P->r, P->p, dk, 64))
		return (SCRYPT_EKEY);

	/* Construct the file header. */
	memcpy(header, "scrypt", 6);
	header[6] = 0;
	header[7] = P->logN & 0xff;
	be32enc(&header[8], P->r);
	be32enc(&header[12], P->p);
	memcpy(&header[16], salt, 32);

	/* Add header checksum. */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, header, 48);
	SHA256_Final(hbuf, &ctx);
	memcpy(&header[48], hbuf, 16);

	/* Add header signature (used for verifying password). */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, header, 64);
	HMAC_SHA256_Final(hbuf, &hctx);
	memcpy(&header[64], hbuf, 32);

	/* Success! */
	return (SCRYPT_OK);
}

/**
 * scryptdec_file_printparams(infile):
 * Print the encryption parameters (N, r, p) used for the encrypted ${infile}.
 */
int
scryptdec_file_printparams(FILE * infile)
{
	uint8_t header[96];
	int logN;
	uint32_t r;
	uint32_t p;
	int rc;

	/* Load the header. */
	if ((rc = scryptdec_file_load_header(infile, header)) != 0)
		goto err0;

	/* Parse N, r, p. */
	logN = header[7];
	r = be32dec(&header[8]);
	p = be32dec(&header[12]);

	/* Print parameters. */
	display_params(logN, r, p, 0, 0, 0);

	/* Success! */
	return (SCRYPT_OK);

err0:
	/* Failure! */
	return (rc);
}

/*
 * NOTE: The caller is responsible for sanitizing ${dk}, including if this
 * function fails.
 */
static int
scryptdec_setup(const uint8_t header[96], uint8_t dk[64],
    const uint8_t * passwd, size_t passwdlen,
    struct scryptenc_params * P, int verbose,
    int force)
{
	uint8_t salt[32];
	uint8_t hbuf[32];
	uint64_t N;
	SHA256_CTX ctx;
	uint8_t * key_hmac = &dk[32];
	HMAC_SHA256_CTX hctx;
	int rc;

	/* Parse N, r, p, salt. */
	P->logN = header[7];
	P->r = be32dec(&header[8]);
	P->p = be32dec(&header[12]);
	memcpy(salt, &header[16], 32);

	/* Verify header checksum. */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, header, 48);
	SHA256_Final(hbuf, &ctx);
	if (crypto_verify_bytes(&header[48], hbuf, 16))
		return (SCRYPT_EINVAL);

	/*
	 * Check whether the provided parameters are valid and whether the
	 * key derivation function can be computed within the allowed memory
	 * and CPU time, unless the user chose to disable this test.
	 */
	if ((rc = checkparams(P->maxmem, P->maxmemfrac, P->maxtime, P->logN,
	    P->r, P->p, verbose, force)) != 0)
		return (rc);

	/* Compute the derived keys. */
	N = (uint64_t)(1) << P->logN;
	if (crypto_scrypt(passwd, passwdlen, salt, 32, N, P->r, P->p, dk, 64))
		return (SCRYPT_EKEY);

	/* Check header signature (i.e., verify password). */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, header, 64);
	HMAC_SHA256_Final(hbuf, &hctx);
	if (crypto_verify_bytes(hbuf, &header[64], 32))
		return (SCRYPT_EPASS);

	/* Success! */
	return (SCRYPT_OK);
}

/**
 * scryptenc_buf(inbuf, inbuflen, outbuf, passwd, passwdlen,
 *     params, verbose, force):
 * Encrypt ${inbuflen} bytes from ${inbuf}, writing the resulting
 * ${inbuflen} + 128 bytes to ${outbuf}.  If ${force} is 1, do not check
 * whether decryption will exceed the estimated available memory or time.
 * The explicit parameters within ${params} must be zero or must all be
 * non-zero.  If explicit parameters are used and the computation is estimated
 * to exceed resource limits, print a warning instead of returning an error.
 * Return the explicit parameters used via ${params}.
 */
int
scryptenc_buf(const uint8_t * inbuf, size_t inbuflen, uint8_t * outbuf,
    const uint8_t * passwd, size_t passwdlen,
    struct scryptenc_params * P, int verbose, int force)
{
	uint8_t dk[64];
	uint8_t hbuf[32];
	uint8_t header[96];
	uint8_t * key_enc = dk;
	uint8_t * key_hmac = &dk[32];
	int rc;
	HMAC_SHA256_CTX hctx;
	struct crypto_aes_key * key_enc_exp;
	struct crypto_aesctr * AES;

	/* The explicit parameters must be zero, or all non-zero. */
	assert(((P->logN == 0) && (P->r == 0) && (P->p == 0)) ||
	    ((P->logN != 0) && (P->r != 0) && (P->p != 0)));

	/* Generate the header and derived key. */
	if ((rc = scryptenc_setup(header, dk, passwd, passwdlen,
	    P, verbose, force)) != 0)
		goto err1;

	/* Copy header into output buffer. */
	memcpy(outbuf, header, 96);

	/* Encrypt data. */
	if ((key_enc_exp = crypto_aes_key_expand(key_enc, 32)) == NULL) {
		rc = SCRYPT_EOPENSSL;
		goto err1;
	}
	if ((AES = crypto_aesctr_init(key_enc_exp, 0)) == NULL) {
		crypto_aes_key_free(key_enc_exp);
		rc = SCRYPT_ENOMEM;
		goto err1;
	}
	crypto_aesctr_stream(AES, inbuf, &outbuf[96], inbuflen);
	crypto_aesctr_free(AES);
	crypto_aes_key_free(key_enc_exp);

	/* Add signature. */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, outbuf, 96 + inbuflen);
	HMAC_SHA256_Final(hbuf, &hctx);
	memcpy(&outbuf[96 + inbuflen], hbuf, 32);

	/* Zero sensitive data. */
	insecure_memzero(dk, 64);

	/* Success! */
	return (SCRYPT_OK);

err1:
	insecure_memzero(dk, 64);

	/* Failure! */
	return (rc);
}

/**
 * scryptdec_buf(inbuf, inbuflen, outbuf, outlen, passwd, passwdlen,
 *     params, verbose, force):
 * Decrypt ${inbuflen} bytes from ${inbuf}, writing the result into ${outbuf}
 * and the decrypted data length to ${outlen}.  The allocated length of
 * ${outbuf} must be at least ${inbuflen}.  If ${force} is 1, do not check
 * whether decryption will exceed the estimated available memory or time.
 * The explicit parameters within ${params} must be zero.  Return the explicit
 * parameters used via ${params}.
 */
int
scryptdec_buf(const uint8_t * inbuf, size_t inbuflen, uint8_t * outbuf,
    size_t * outlen, const uint8_t * passwd, size_t passwdlen,
    struct scryptenc_params * P, int verbose,
    int force)
{
	uint8_t hbuf[32];
	uint8_t dk[64];
	uint8_t * key_enc = dk;
	uint8_t * key_hmac = &dk[32];
	int rc;
	HMAC_SHA256_CTX hctx;
	struct crypto_aes_key * key_enc_exp;
	struct crypto_aesctr * AES;

	/* The explicit parameters must be zero. */
	assert((P->logN == 0) && (P->r == 0) && (P->p == 0));

	/*
	 * All versions of the scrypt format will start with "scrypt" and
	 * have at least 7 bytes of header.
	 */
	if ((inbuflen < 7) || (memcmp(inbuf, "scrypt", 6) != 0)) {
		rc = SCRYPT_EINVAL;
		goto err0;
	}

	/* Check the format. */
	if (inbuf[6] != 0) {
		rc = SCRYPT_EVERSION;
		goto err0;
	}

	/* We must have at least 128 bytes. */
	if (inbuflen < 128) {
		rc = SCRYPT_EINVAL;
		goto err0;
	}

	/* Parse the header and generate derived keys. */
	if ((rc = scryptdec_setup(inbuf, dk, passwd, passwdlen,
	    P, verbose, force)) != 0)
		goto err1;

	/* Decrypt data. */
	if ((key_enc_exp = crypto_aes_key_expand(key_enc, 32)) == NULL) {
		rc = SCRYPT_EOPENSSL;
		goto err1;
	}
	if ((AES = crypto_aesctr_init(key_enc_exp, 0)) == NULL) {
		crypto_aes_key_free(key_enc_exp);
		rc = SCRYPT_ENOMEM;
		goto err1;
	}
	crypto_aesctr_stream(AES, &inbuf[96], outbuf, inbuflen - 128);
	crypto_aesctr_free(AES);
	crypto_aes_key_free(key_enc_exp);
	*outlen = inbuflen - 128;

	/* Verify signature. */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, inbuf, inbuflen - 32);
	HMAC_SHA256_Final(hbuf, &hctx);
	if (crypto_verify_bytes(hbuf, &inbuf[inbuflen - 32], 32)) {
		rc = SCRYPT_EINVAL;
		goto err1;
	}

	/* Zero sensitive data. */
	insecure_memzero(dk, 64);

	/* Success! */
	return (SCRYPT_OK);

err1:
	insecure_memzero(dk, 64);
err0:
	/* Failure! */
	return (rc);
}

/**
 * scryptenc_file(infile, outfile, passwd, passwdlen, params, verbose, force):
 * Read a stream from ${infile} and encrypt it, writing the resulting stream
 * to ${outfile}.  If ${force} is 1, do not check whether decryption will
 * exceed the estimated available memory or time.  The explicit parameters
 * within ${params} must be zero or must all be non-zero.  If explicit
 * parameters are used and the computation is estimated to exceed resource
 * limits, print a warning instead of returning an error.  Return the explicit
 * parameters used via ${params}.
 */
int
scryptenc_file(FILE * infile, FILE * outfile,
    const uint8_t * passwd, size_t passwdlen,
    struct scryptenc_params * P, int verbose, int force)
{
	uint8_t buf[ENCBLOCK];
	uint8_t dk[64];
	uint8_t hbuf[32];
	uint8_t header[96];
	uint8_t * key_enc = dk;
	uint8_t * key_hmac = &dk[32];
	size_t readlen;
	HMAC_SHA256_CTX hctx;
	struct crypto_aes_key * key_enc_exp;
	struct crypto_aesctr * AES;
	int rc;

	/* The explicit parameters must be zero, or all non-zero. */
	assert(((P->logN == 0) && (P->r == 0) && (P->p == 0)) ||
	    ((P->logN != 0) && (P->r != 0) && (P->p != 0)));

	/* Generate the header and derived key. */
	if ((rc = scryptenc_setup(header, dk, passwd, passwdlen,
	    P, verbose, force)) != 0)
		goto err1;

	/* Hash and write the header. */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, header, 96);
	if (fwrite(header, 96, 1, outfile) != 1) {
		rc = SCRYPT_EWRFILE;
		goto err1;
	}

	/*
	 * Read blocks of data, encrypt them, and write them out; hash the
	 * data as it is produced.
	 */
	if ((key_enc_exp = crypto_aes_key_expand(key_enc, 32)) == NULL) {
		rc = SCRYPT_EOPENSSL;
		goto err1;
	}
	if ((AES = crypto_aesctr_init(key_enc_exp, 0)) == NULL) {
		crypto_aes_key_free(key_enc_exp);
		rc = SCRYPT_ENOMEM;
		goto err1;
	}
	do {
		if ((readlen = fread(buf, 1, ENCBLOCK, infile)) == 0)
			break;
		crypto_aesctr_stream(AES, buf, buf, readlen);
		HMAC_SHA256_Update(&hctx, buf, readlen);
		if (fwrite(buf, 1, readlen, outfile) < readlen) {
			crypto_aesctr_free(AES);
			rc = SCRYPT_EWRFILE;
			goto err1;
		}
	} while (1);
	crypto_aesctr_free(AES);
	crypto_aes_key_free(key_enc_exp);

	/* Did we exit the loop due to a read error? */
	if (ferror(infile)) {
		rc = SCRYPT_ERDFILE;
		goto err1;
	}

	/* Compute the final HMAC and output it. */
	HMAC_SHA256_Final(hbuf, &hctx);
	if (fwrite(hbuf, 32, 1, outfile) != 1) {
		rc = SCRYPT_EWRFILE;
		goto err1;
	}

	/* Zero sensitive data. */
	insecure_memzero(dk, 64);

	/* Success! */
	return (SCRYPT_OK);

err1:
	insecure_memzero(dk, 64);

	/* Failure! */
	return (rc);
}

/**
 * scryptdec_file_cookie_free(cookie):
 * Free the ${cookie}.
 */
void
scryptdec_file_cookie_free(struct scryptdec_file_cookie * C)
{

	/* Behave consistently with free(NULL). */
	if (C == NULL)
		return;

	/* Zero sensitive data. */
	insecure_memzero(C->dk, 64);

	/* We do not free C->infile because it is not owned by this cookie. */

	/* Free the cookie. */
	free(C);
}

/* Load the header and check the magic. */
static int
scryptdec_file_load_header(FILE * infile, uint8_t header[static 96])
{
	int rc;

	/*
	 * Read the first 7 bytes of the file; all future versions of scrypt
	 * are guaranteed to have at least 7 bytes of header.
	 */
	if (fread(header, 7, 1, infile) < 1) {
		if (ferror(infile)) {
			rc = SCRYPT_ERDFILE;
			goto err0;
		} else {
			rc = SCRYPT_EINVAL;
			goto err0;
		}
	}

	/* Do we have the right magic? */
	if (memcmp(header, "scrypt", 6)) {
		rc = SCRYPT_EINVAL;
		goto err0;
	}
	if (header[6] != 0) {
		rc = SCRYPT_EVERSION;
		goto err0;
	}

	/*
	 * Read another 89 bytes of the file; version 0 of the scrypt file
	 * format has a 96-byte header.
	 */
	if (fread(&header[7], 89, 1, infile) < 1) {
		if (ferror(infile)) {
			rc = SCRYPT_ERDFILE;
			goto err0;
		} else {
			rc = SCRYPT_EINVAL;
			goto err0;
		}
	}

	/* Success! */
	return (SCRYPT_OK);

err0:
	/* Failure! */
	return (rc);
}

/**
 * scryptdec_file_prep(infile, passwd, passwdlen, params, force, cookie):
 * Prepare to decrypt ${infile}, including checking the passphrase.  Allocate
 * a cookie at ${cookie}.  After calling this function, ${infile} should not
 * be modified until the decryption is completed by scryptdec_file_copy().
 * If ${force} is 1, do not check whether decryption will exceed the estimated
 * available memory or time.  The explicit parameters within ${params} must be
 * zero.  Return the explicit parameters to be used via ${params}.
 */
int
scryptdec_file_prep(FILE * infile, const uint8_t * passwd,
    size_t passwdlen, struct scryptenc_params * P,
    int verbose, int force, struct scryptdec_file_cookie ** cookie)
{
	struct scryptdec_file_cookie * C;
	int rc;

	/* The explicit parameters must be zero. */
	assert((P->logN == 0) && (P->r == 0) && (P->p == 0));

	/* Allocate the cookie. */
	if ((C = malloc(sizeof(struct scryptdec_file_cookie))) == NULL)
		return (SCRYPT_ENOMEM);
	C->infile = infile;

	/* Load the header. */
	if ((rc = scryptdec_file_load_header(infile, C->header)) != 0)
		goto err1;

	/* Parse the header and generate derived keys. */
	if ((rc = scryptdec_setup(C->header, C->dk, passwd, passwdlen,
	    P, verbose, force)) != 0)
		goto err1;

	/* Set cookie for calling function. */
	*cookie = C;

	/* Success! */
	return (SCRYPT_OK);

err1:
	scryptdec_file_cookie_free(C);

	/* Failure! */
	return (rc);
}

/**
 * scryptdec_file_copy(cookie, outfile):
 * Read a stream from the file that was passed into the ${cookie} by
 * scryptdec_file_prep(), decrypt it, and write the resulting stream to
 * ${outfile}.  After this function completes, it is safe to modify/close
 * ${outfile} and the ${infile} which was given to scryptdec_file_prep().
 */
int
scryptdec_file_copy(struct scryptdec_file_cookie * C, FILE * outfile)
{
	uint8_t buf[ENCBLOCK + 32];
	uint8_t hbuf[32];
	uint8_t * key_enc;
	uint8_t * key_hmac;
	size_t buflen = 0;
	size_t readlen;
	HMAC_SHA256_CTX hctx;
	struct crypto_aes_key * key_enc_exp;
	struct crypto_aesctr * AES;
	int rc;

	/* Sanity check. */
	assert(C != NULL);

	/* Use existing array for these pointers. */
	key_enc = C->dk;
	key_hmac = &C->dk[32];

	/* Start hashing with the header. */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, C->header, 96);

	/*
	 * We don't know how long the encrypted data block is (we can't know,
	 * since data can be streamed into 'scrypt enc') so we need to read
	 * data and decrypt all of it except the final 32 bytes, then check
	 * if that final 32 bytes is the correct signature.
	 */
	if ((key_enc_exp = crypto_aes_key_expand(key_enc, 32)) == NULL) {
		rc = SCRYPT_EOPENSSL;
		goto err0;
	}
	if ((AES = crypto_aesctr_init(key_enc_exp, 0)) == NULL) {
		crypto_aes_key_free(key_enc_exp);
		rc = SCRYPT_ENOMEM;
		goto err0;
	}
	do {
		/* Read data until we have more than 32 bytes of it. */
		if ((readlen = fread(&buf[buflen], 1,
		    ENCBLOCK + 32 - buflen, C->infile)) == 0)
			break;
		buflen += readlen;
		if (buflen <= 32)
			continue;

		/*
		 * Decrypt, hash, and output everything except the last 32
		 * bytes out of what we have in our buffer.
		 */
		HMAC_SHA256_Update(&hctx, buf, buflen - 32);
		crypto_aesctr_stream(AES, buf, buf, buflen - 32);
		if (fwrite(buf, 1, buflen - 32, outfile) < buflen - 32) {
			crypto_aesctr_free(AES);
			rc = SCRYPT_EWRFILE;
			goto err0;
		}

		/* Move the last 32 bytes to the start of the buffer. */
		memmove(buf, &buf[buflen - 32], 32);
		buflen = 32;
	} while (1);
	crypto_aesctr_free(AES);
	crypto_aes_key_free(key_enc_exp);

	/* Did we exit the loop due to a read error? */
	if (ferror(C->infile)) {
		rc = SCRYPT_ERDFILE;
		goto err0;
	}

	/* Did we read enough data that we *might* have a valid signature? */
	if (buflen < 32) {
		rc = SCRYPT_EINVAL;
		goto err0;
	}

	/* Verify signature. */
	HMAC_SHA256_Final(hbuf, &hctx);
	if (crypto_verify_bytes(hbuf, buf, 32)) {
		rc = SCRYPT_EINVAL;
		goto err0;
	}

	/* Success! */
	return (SCRYPT_OK);

err0:
	/* Failure! */
	return (rc);
}

/**
 * scryptdec_file(infile, outfile, passwd, passwdlen, params, verbose, force):
 * Read a stream from ${infile} and decrypt it, writing the resulting stream
 * to ${outfile}.  If ${force} is 1, do not check whether decryption
 * will exceed the estimated available memory or time.  The explicit
 * parameters within ${params} must be zero.  Return the explicit parameters
 * used via ${params}.
 */
int
scryptdec_file(FILE * infile, FILE * outfile, const uint8_t * passwd,
    size_t passwdlen, struct scryptenc_params * P,
    int verbose, int force)
{
	struct scryptdec_file_cookie * C;
	int rc;

	/* The explicit parameters must be zero. */
	assert((P->logN == 0) && (P->r == 0) && (P->p == 0));

	/* Check header, including passphrase. */
	if ((rc = scryptdec_file_prep(infile, passwd, passwdlen, P,
	    verbose, force, &C)) != 0)
		goto err0;

	/* Copy unencrypted data to outfile. */
	if ((rc = scryptdec_file_copy(C, outfile)) != 0)
		goto err1;

	/* Clean up cookie, attempting to zero sensitive data. */
	scryptdec_file_cookie_free(C);

	/* Success! */
	return (SCRYPT_OK);

err1:
	scryptdec_file_cookie_free(C);
err0:
	/* Failure! */
	return (rc);
}
