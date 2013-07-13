#include "bsdtar_platform.h"

#include <sys/select.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_verify_bytes.h"
#include "sysendian.h"
#include "warnp.h"

#include "crypto.h"
#include "netproto_internal.h"
#include "tsnetwork.h"

#include "netproto.h"

/**
 * Packet format:
 * position length
 * 0        1      packet type (encrypted)
 * 1        4      data length, big-endian (encrypted)
 * 5        32     SHA256(data) (encrypted)
 * 37       32     HMAC(ciphertext bytes 0--36) (not encrypted)
 * 69       N      packet data (encrypted)
 */

struct writepacket_internal {
	network_callback * callback;
	void * cookie;
	NETPROTO_CONNECTION * C;
	size_t len;
	uint8_t * buf;
};

struct readpacket_internal {
	int (* callback_getbuf)(void *, uint8_t, uint8_t **, size_t);
	network_callback * callback_done;
	void * cookie;
	NETPROTO_CONNECTION * C;
	uint8_t header[69];
	size_t len;
	uint8_t * buf;
};

static network_callback packet_sent;
static network_callback header_received;
static network_callback data_received;

static int
packet_sent(void * cookie, int status)
{
	struct writepacket_internal * WC = cookie;
	int rc;

	/* Adjust traffic statistics. */
	WC->C->bytesqueued -= WC->len;
	if (status == NETWORK_STATUS_OK)
		WC->C->bytesout += WC->len;

	/* Call upstream callback. */
	rc = (WC->callback)(WC->cookie, status);

	/* Free buffer and writepacket cookie. */
	free(WC->buf);
	free(WC);

	/* Return value from callback. */
	return (rc);
}

static int
header_received(void * cookie, int status)
{
	struct readpacket_internal * RC = cookie;
	struct timeval to0, to1;
	uint32_t len;
	int rc = 0;	/* No error unless specified otherwise. */

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	RC->C->bytesin += 69;

	/* Verify that the header is authentic. */
	if (crypto_session_verify(RC->C->keys, RC->header, 37,
	    RC->header + 37)) {
		status = NETPROTO_STATUS_PROTERR;
		goto err1;
	}

	/* Decrypt header in-place. */
	crypto_session_decrypt(RC->C->keys, RC->header, RC->header, 37);

	/* Decode packet length. */
	len = be32dec(&RC->header[1]);
#if SIZE_MAX < UINT32_MAX
	if (len > SIZE_MAX) {
		errno = ENOMEM;
		goto err2;
	}
#endif
	RC->len = len;

	/* Ask callback to provide a buffer. */
	status = (RC->callback_getbuf)(RC->cookie, RC->header[0], &RC->buf,
	    RC->len);
	if (status != NETWORK_STATUS_OK)
		goto err1;

	/*
	 * If the packet data is zero bytes long, we have finished reading
	 * the packet; invoke the upstream callback.  We don't bother to
	 * verify that the included SHA256 hash is equal to SHA256(nothing),
	 * since the authenticity of the packet data length is "proved" by
	 * the HMAC on the header and there is only one possible data block
	 * of length zero bytes.
	 */
	if (RC->len == 0) {
		rc = (RC->callback_done)(RC->cookie, NETWORK_STATUS_OK);
		free(RC);
		goto done;
	}

	/*
	 * Read data into the provided buffer.  Allow up to 5 minutes for
	 * the entire packet to arrive -- this works out to a 256kB packet
	 * arriving at a rate of 7kbps, which is a reasonable lower bound
	 * for the bandwidth of systems running tarsnap.
	 */
	to0.tv_sec = 60;
	to1.tv_sec = 300;
	to0.tv_usec = to1.tv_usec = 0;
	if (tsnetwork_read(RC->C->fd, RC->buf, RC->len, &to0, &to1,
	    data_received, RC))
		goto err2;

done:
	/* Success! */
	return (rc);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	rc = (RC->callback_done)(RC->cookie, status);
	free(RC);

	/* Failure! */
	return (rc);
}

static int
data_received(void * cookie, int status)
{
	struct readpacket_internal * RC = cookie;
	uint8_t hash[32];
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	RC->C->bytesin += RC->len;

	/* Decrypt the data in-place. */
	crypto_session_decrypt(RC->C->keys, RC->buf, RC->buf, RC->len);

	/*
	 * Verify that the data is not corrupt.  Protecting against a timing
	 * side channel which would allow an attacker to determine in which
	 * byte of its SHA256 hash a buffer which he mangled had changed is
	 * probably a little bit of overkill, but you can never have too much
	 * overkill where security is concerned. :-)
	 */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, RC->buf, RC->len,
	    hash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err1;
	}

	if (crypto_verify_bytes(&RC->header[5], hash, 32)) {
		status = NETPROTO_STATUS_PROTERR;
		goto err1;
	}

err1:
	/* We're done.  Tell the callback. */
	rc = (RC->callback_done)(RC->cookie, status);

	/* Free readpacket cookie. */
	free(RC);

	/* Return the value from the callback. */
	return (rc);
}

/**
 * netproto_writepacket(C, type, buf, buflen, callback, cookie):
 * Write the provided packet to the connection.  When complete, call
 * callback(cookie, status), where status is a NETPROTO_STATUS_* value.
 */
int
netproto_writepacket(NETPROTO_CONNECTION * C, uint8_t type,
    const uint8_t * buf, size_t buflen, network_callback * callback,
    void * cookie)
{
	struct writepacket_internal * WC;
	uint8_t header[37];
	struct timeval timeout;

	/*
	 * Print a warning if the connection is broken.  This should never
	 * happen, so we could make this a fatal error; but it's not actually
	 * going to cause any harm (the remote host will detect a protocol
	 * error), so in the interest of resilience we might as well just
	 * print the warning and keep on going.
	 */
	if (C->broken) {
		warn0("Programmer error: "
		    "attempt to write to connection marked as broken");
	}

	/* Sanity check buffer length. */
	if (buflen > UINT32_MAX) {
		warn0("Programmer error: "
		    "buffer too large in netproto_writepacket");
		goto err0;
	}

	/* Allocate storage for writepacket cookie. */
	if ((WC = malloc(sizeof(struct writepacket_internal))) == NULL)
		goto err0;

	/* Store callback and cookie. */
	WC->callback = callback;
	WC->cookie = cookie;

	/* Record parameters needed for statistics purposes. */
	WC->C = C;
	WC->len = buflen + 69;

	/* Allocate storage for buffered packet. */
	if (buflen > SIZE_MAX - 69) {
		errno = ENOMEM;
		goto err1;
	}
	if ((WC->buf = malloc(buflen + 69)) == NULL)
		goto err1;

	/* Construct header. */
	header[0] = type;
	be32enc(&header[1], buflen);
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, buf, buflen,
	    &header[5])) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err2;
	}

	/* Encrypt packet header. */
	crypto_session_encrypt(C->keys, header, WC->buf, 37);

	/* Compute HMAC of packet header. */
	crypto_session_sign(C->keys, WC->buf, 37, WC->buf + 37);

	/* Encrypt packet data. */
	crypto_session_encrypt(C->keys, buf, WC->buf + 69, buflen);

	/*
	 * Add packet to connection write queue.  See comments in
	 * header_received concerning the timeout.
	 */
	timeout.tv_sec = 300;
	timeout.tv_usec = 0;
	if (network_writeq_add(C->Q, WC->buf, buflen + 69, &timeout,
	    packet_sent, WC))
		goto err2;
	C->bytesqueued += WC->len;

	/* Success! */
	return (0);

err2:
	free(WC->buf);
err1:
	free(WC);
err0:
	/* Failure! */
	return (-1);
}

/**
 * netproto_readpacket(C, callback_getbuf, callback_done, cookie):
 * Read a packet from the connection.  Once the type and length of the
 * packet is known, call callback_getbuf(cookie, type, buf, buflen); once
 * the packet is read or fails, call callback_done(cookie, status), where
 * status is a NETPROTO_STATUS_* value.
 */
int netproto_readpacket(NETPROTO_CONNECTION * C,
    int (* callback_getbuf)(void *, uint8_t, uint8_t **, size_t),
    network_callback * callback_done, void * cookie)
{
	struct readpacket_internal * RC;
	struct timeval to0, to1;

	/* Allocate space for readpacket cookie. */
	if ((RC = malloc(sizeof(struct readpacket_internal))) == NULL)
		goto err0;

	/* Store callbacks, cookie, and connection. */
	RC->callback_getbuf = callback_getbuf;
	RC->callback_done = callback_done;
	RC->cookie = cookie;
	RC->C = C;

	/*
	 * Read packet header.  Timeout if no data is received for 60s or
	 * if the complete header is not received for 120s; this allows an
	 * idle connection to be safely distinguished from a dead connection.
	 */
	to0.tv_sec = 60;
	to1.tv_sec = 120;
	to0.tv_usec = to1.tv_usec = 0;
	if (tsnetwork_read(C->fd, RC->header, 69, &to0, &to1,
	    header_received, RC))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(RC);
err0:
	/* Failure! */
	return (-1);
}
