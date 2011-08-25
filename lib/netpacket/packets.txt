Tarsnap client-server protocol packets
======================================

Unless specified otherwise, all values are stored as big-endian integers.

HMACW(X) = HMAC(<write access key>, X)
HMACR(X) = HMAC(<read access key>, X)
HMACD(X) = HMAC(<delete access key>, X)

In any single connection, the following operation types cannot be mixed:
 * Registering a machine.
 * Reading files.
 * Listing files.
 * Transactions (start/commit/write file/delete file, etc.)

In addition, a NETPACKET_WRITE_FEXIST_REQUEST may not be sent while any
other requests are pending on the same connection.

NETPACKET_REGISTER_REQUEST (0x00)
---------------------------------

A new machine wants to register with the server.

Packet contents:
	char user[]

The string user[] must be no more than 255 bytes, and is not NUL terminated.

The server will respond with a NETPACKET_REGISTER_CHALLENGE packet or
a NETPACKET_REGISTER_RESPONSE packet specifying "no such user".

NETPACKET_REGISTER_CHALLENGE (0x80)
-----------------------------------

As part of the machine registration process, the server needs to verify that
the machine has the user password.

Packet contents:
	uint8_t salt[32]
	uint8_t serverpub[CRYPTO_DH_PUBLEN]

The client must compute DH parameters (priv, pub) from the salt and
password, and then compute K = serverpub^priv.  It must then send back a
NETPACKET_REGISTER_CHA_RESPONSE packet.

NOTE: The value serverpub[] expires when the network protocol connection is
closed, so the NETPACKET_REGISTER_CHA_RESPONSE packet MUST be sent as part
of the same connection.

NETPACKET_REGISTER_CHA_RESPONSE (0x01)
--------------------------------------

As part of the machine registration process, the new machine needs to prove
that it holds the user password, and send its access keys and user-friendly
name.

Packet contents:
	uint8_t key_put[32]
	uint8_t key_get[32]
	uint8_t key_delete[32]
	uint8_t namelen
	char name[namelen]
	uint8_t hmac[32]

The string name[namelen] is not NUL terminated.

The value hmac is HMAC(SHA256(K), 0x01 || [the packet minus the final hmac]),
where K is as described under NETPACKET_REGISTER_CHALLENGE.  The server will
respond with a NETPACKET_REGISTER_RESPONSE packet.

NETPACKET_REGISTER_RESPONSE (0x81)
----------------------------------

As part of the machine registration process, the server needs to confirm
that the machine is registered and provide the registration number; or
inform the client that the user or password is incorrect.

Packet contents:
	uint8_t status
	uint64_t machinenum
	uint8_t hmac[32]

The value status is
	0	Success; machinenum is the machine number
	1	No such user
	2	Incorrect hmac (i.e., password is wrong)
	3	Account balance is not positive.

The value machinenum is (uint64_t)(-1) if status is non-zero.

The value hmac is HMAC(SHA256(K), 0x81 || [the packet minus the final hmac]),
where K is as described under NETPACKET_REGISTER_CHALLENGE, if status is 0 or
3; or zero otherwise.

The server will close the connection after sending this packet.

NETPACKET_TRANSACTION_GETNONCE (0x10)
-------------------------------------

Packet contents:
	uint64_t machinenum

NETPACKET_TRANSACTION_GETNONCE_RESPONSE (0x90)
----------------------------------------------

Packet contents:
	uint8_t snonce[32]

NOTE: This server nonce becomes invalid as soon as (a) the network protocol
connection is closed, or (b) a transaction is started by the machine for
which this nonce is being provided.

NETPACKET_TRANSACTION_START (0x11)
----------------------------------

Packet contents:
	uint64_t machinenum
	uint8_t operation
	uint8_t snonce[32]
	uint8_t cnonce[32]
	uint8_t state[32]
	uint8_t hmac[32]

The value operation is
	0	Write transaction (key = write access key)
	1	Delete transaction (key = delete access key)
	2	Fsck transaction (key = delete access key, state = 0)
	3	Fsck transaction w/o pruning (key = write access key, state = 0).

The value snonce is as provided by the server (see above); the value cnonce
is a random client nonce; the value state is the transaction nonce of the
last committed transaction, or 32 zero bytes if this is the first transaction
by this machine or if this is a fsck transaction.

The value hmac is HMAC(key, 0x11 || [packet minus final hmac]).

The transaction nonce for the new transaction is SHA256(snonce || cnonce).

NETPACKET_TRANSACTION_START_RESPONSE (0x91)
-------------------------------------------

Packet contents:
	uint8_t status
	uint8_t hmac[32]

The value status is
	0	Success
	1	Bad state (run --fsck)
	2	Account balance is not positive.

A status value of 2 will only ever be sent in response to an attempt to start
a write transaction.

The value hmac is HMAC(key, 0x91 || nonce || status) where key is the write
or delete key as in NETPACKET_TRANSACTION_START and nonce is the transaction
nonce as described above.

NETPACKET_TRANSACTION_COMMIT (0x12)
-----------------------------------

Packet contents:
	uint64_t machinenum
	uint8_t whichkey
	uint8_t nonce[32]
	uint8_t hmac[32]

The value whichkey is
	0	key = write access key
	1	key = delete access key.

The value nonce is the transaction nonce for the transaction which the
client wants to have committed.

The value hmac is HMAC(key, 0x12 || [packet minus final hmac]).

NETPACKET_TRANSACTION_COMMIT_RESPONSE (0x92)
--------------------------------------------

Packet contents:
	uint8_t hmac[32]

The value hmac is HMAC(key, 0x92 || nonce) where key is the write or delete
key as in NETPACKET_TRANSACTION_COMMIT.

Note that this packet does not indicate that the requested transaction was
committed; only that IF the requested transaction was the most recent
non-committed transaction, THEN it has been committed.

NETPACKET_TRANSACTION_CHECKPOINT (0x13)
---------------------------------------

Mark a "checkpoint" in a write transaction.  When committing a write
transaction, files are omitted from the transaction if (a) they were uploaded
after the last checkpoint, or (b) they were uploaded prior to the penultimate
checkpoint and have class 'i' or 'm'.

Packet contents:
	uint64_t machinenum
	uint8_t whichkey
	uint8_t ckptnonce[32]
	uint8_t nonce[32]
	uint8_t hmac[32]

The value whichkey is
	0	key = write access key
	1	key = delete access key.
Note that while checkpoints only exist in write transactions, a checkpoint
request can be signed with a delete access key in the "log checkpoint
creation; crash; run tarsnap -d" case.

The value ckptnonce is a random nonce; if a checkpoint request is replayed,
the ckptnonce value must be identical.

The value nonce is the transaction nonce of the current write transaction.

The value hmac is HMAC(0x13 || [packet minute final hmac]).

NETPACKET_TRANSACTION_CHECKPOINT_RESPONSE (0x93)
------------------------------------------------

Packet contents:
	uint8_t status
	uint8_t ckptnonce[32]
	uint8_t hmac[32]

The value status means:
	0	Success
	1	Transaction nonce is incorrect.

The value hmac is HMAC(key, 0x93 || nonce || [packet minus final hmac]),
where nonce is the nonce provided by the client (which may not be the nonce
of the current transaction, if status == 1), and key is the write or delete
key as in NETPACKET_TRANSACTION_CHECKPOINT.

NETPACKET_TRANSACTION_CANCEL (0x14)
-----------------------------------

The client wants to cancel any pending transaction as with TRANSACTION_START,
but not actually start a transaction (yet).

Packet contents:
	uint64_t machinenum
	uint8_t whichkey
	uint8_t snonce[32]
	uint8_t cnonce[32]
	uint8_t state[32]
	uint8_t hmac[32]

The value whichkey is
	0	key = write access key
	1	key = delete access key
	2	key = delete access key and state = 0
	3	key = write access key and state = 0.

The value snonce is as provided by the server in response to a
NETPACKET_TRANSACTION_GETNONCE request; the value cnonce is a random client
nonce; the value state is the transaction nonce of the last committed
transaction.

The value hmac is HMAC(key, 0x14 || [packet minus final hmac]).

NETPACKET_TRANSACTION_CANCEL_RESPONSE (0x94)
-------------------------------------------

Packet contents:
	uint8_t status
	uint8_t hmac[32]

The value status is
	0	Success
	1	Try again later

A "success" here means that *if* the client's state value was correct *then*
there is currently no transaction in progress.  A "try again later" does not
guarantee anything except that the server is alive and responsive; in
particular, the server may cancel the currently in-progress transaction but
still respond "try again later".

The value hmac is HMAC(key, 0x94 || nonce || status) where key is the write
or delete key as in NETPACKET_TRANSACTION_CANCEL and nonce is SHA256(snonce
|| cnonce).

NETPACKET_TRANSACTION_TRYCOMMIT (0x15)
--------------------------------------

Packet contents:
	uint64_t machinenum
	uint8_t whichkey
	uint8_t nonce[32]
	uint8_t hmac[32]

The value whichkey is
	0	key = write access key
	1	key = delete access key.

The value nonce is the transaction nonce for the transaction which the
client wants to have committed.

The value hmac is HMAC(key, 0x15 || [packet minus final hmac]).

NETPACKET_TRANSACTION_TRYCOMMIT_RESPONSE (0x95)
-----------------------------------------------

Packet contents:
	uint8_t status
	uint8_t hmac[32]

The value status is
	0	Success
	1	Try again later

A "success" here means that *if* the specified transaction was the most
recent non-committed transaction, *then* it has been committed.  A "try again
later" guarantees nothing except that the server is alive and responsive;
in particular, the server may commit the transaction in question but still
respond "try again later".

The value hmac is HMAC(key, 0x95 || nonce || status) where key is the write
or delete key as in NETPACKET_TRANSACTION_TRYCOMMIT.

NETPACKET_TRANSACTION_ISCHECKPOINTED (0x16)
-------------------------------------------

Packet contents:
	uint64_t machinenum
	uint8_t whichkey
	uint8_t nonce[32]
	uint8_t hmac[32]

The value whichkey is
	0	key = write access key
	1	key = delete access key.

The value nonce is a random 256-bit operation nonce.

The value hmac is HMAC(key, 0x16 || [packet minus final hmac]).

NETPACKET_TRANSACTION_ISCHECKPOINTED_RESPONSE (0x96)
----------------------------------------------------

Packet contents:
	uint8_t status
	uint8_t tnonce[32]
	uint8_t hmac[32]

The value status is
	0	No transaction is in progress, the transaction in progress is
		a delete transaction, or the write transaction in progress
		does not have any checkpoints.
	1	A write transaction is in progress and has a checkpoint.
	2	"Reply hazy, try again" -- Magic 8-Ball

The value tnonce is the transaction nonce of the current transaction if
status is 1; and zero otherwise.

The value hmac is HMAC(key, 0x96 || nonce || [packet minus final hmac]) where
key is the write or delete key as in NETPACKET_TRANSACTION_ISCHECKPOINTED.

NETPACKET_WRITE_FEXIST (0x20)
-----------------------------

Packet contents:
	uint64_t machinenum
	uint8_t class
	uint8_t name[32]
	uint8_t nonce[32]
	uint8_t hmac[32]

The value nonce is the transaction nonce of the current transaction.

The value hmac is HMACW(0x20 || [packet minus final hmac]).

NETPACKET_WRITE_FEXIST_RESPONSE (0xa0)
--------------------------------------

Packet contents:
	uint8_t status
	uint8_t class
	uint8_t name[32]
	uint8_t hmac[32]

The value status means:
	0	File 'name' does not exist in class 'class'.
	1	File 'name' exists in class 'class'.
	2	Transaction nonce is incorrect.

The value hmac is HMACW(0xa0 || nonce || [packet minus final hmac]), where
nonce is the nonce provided by the client (which may not be the nonce of the
current transaction).

NETPACKET_WRITE_FILE (0x21)
---------------------------

Packet contents:
	uint64_t machinenum
	uint8_t class
	uint8_t name[32]
	uint8_t nonce[32]
	uint32_t filelen
	uint8_t data[filelen]
	uint8_t hmac[32]

The value nonce is the transaction nonce of the current transaction.

The value filelen is at most 262144 (2^18).

The value hmac is HMACW(0x20 || [packet minus final hmac]).

NETPACKET_WRITE_FILE_RESPONSE (0xa1)
------------------------------------

Packet contents:
	uint8_t status
	uint8_t class
	uint8_t name[32]
	uint8_t hmac[32]

The value status is
	0	Success
	1	The specified file already exists
	2	Transaction nonce is incorrect.

The value hmac is HMACW(0xa1 || nonce || [packet minus final hmac]), where
nonce is the nonce provided by the client (which may not be the nonce of the
current transaction).

NETPACKET_DELETE_FILE (0x30)
----------------------------

Packet contents:
	uint64_t machinenum
	uint8_t class
	uint8_t name[32]
	uint8_t nonce[32]
	uint8_t hmac[32]

The value nonce is the transaction nonce of the current transaction.

The value hmac is HMACD(0x30 || [packet minus final hmac]).

NETPACKET_DELETE_FILE_RESPONSE (0xb0)
-------------------------------------

Packet contents:
	uint8_t status
	uint8_t class
	uint8_t name[32]
	uint8_t hmac[32]

The value status is
	0	Success
	1	The specified file does not exist
	2	Transaction nonce is incorrect.

The value hmac is HMACD(0xb0 || nonce || [packet minus final hmac]), where
nonce is the nonce provided by the client (which may not be the nonce of the
current transaction).

NETPACKET_READ_FILE (0x40)
--------------------------

Packet contents:
	uint64_t machinenum
	uint8_t class
	uint8_t name[32]
	uint32_t size
	uint8_t hmac[32]

The value size is the expected file size (at most 262144), or (uint32_t)(-1)
if the file size is unknown.

The value hmac is HMACR(0x40 || [packet minus final hmac]).

NETPACKET_READ_FILE_RESPONSE (0xc0)
-----------------------------------

Packet contents:
	uint8_t status
	uint8_t class
	uint8_t name[32]
	uint32_t filelen
	uint8_t data[filelen or 0]
	uint8_t hmac[32]

The value status is
	0	Success
	1	File does not exist
	2	File size is incorrect
	3	Account balance is not positive.

The value filelen is the actual size of the file (possibly not equal to the
size field of the NETPACKET_READ_FILE packet); or 0 if status is 1 or 3.

The field data[] contains the file if status is 0; otherwise, the field is
omitted.

The value hmac is HMACR(0xc0 || [packet minus final hmac]).

NETPACKET_DIRECTORY (0x50)
--------------------------

Packet contents:
	uint64_t machinenum
	uint8_t class
	uint8_t start[32]
	uint8_t snonce[32]
	uint8_t cnonce[32]
	uint8_t hmac[32]

The value class is the class for which a directory listing is desired.

The value start is the least value which should be returned by the server.

The value snonce is as provided by the server (as in transaction starting);
the value cnonce is a random client nonce.

The value hmac is HMACR(0x50 || [packet minus final hmac]).

NOTE: The server will respond with one or more NETPACKET_DIRECTORY_RESPONSE
packets.

NETPACKET_DIRECTORY_D (0x51)
----------------------------

This is identical to a NETPACKET_DIRECTORY packet, except that hmac is
HMACD(0x51 || [packet minus final hmac]).

NETPACKET_DIRECTORY_RESPONSE (0xd0)
-----------------------------------

Packet contents:
	uint8_t status
	uint8_t class
	uint8_t start[32]
	uint32_t nfiles
	uint8_t flist[nfiles * 32]
	uint8_t hmac[32]

The value status is
	0	There are no more files after this
	1	More packets to come
	2	There might be more files after this; send another
		NETPACKET_DIRECTORY packet to request them
	3	Account balance is not positive.

A status value of 3 will only ever be sent in response to a DIRECTORY
request, not to a DIRECTORY_D request.

The files returned are the first nfiles files after start in lexicographical
order.

The value hmac is HMACR(0xd0 || nonce || [packet minus final hmac]), where
nonce is SHA256(snonce || cnonce); or HMACD(...) if the packet is being sent
in response to a NETPACKET_DIRECTORY_D packet.
