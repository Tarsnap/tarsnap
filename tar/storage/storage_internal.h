#ifndef _STORAGE_INTERNAL_H_
#define _STORAGE_INTERNAL_H_

/**
 * storage_transaction_start_write(NPC, machinenum, lastseq, seqnum):
 * Start a write transaction, presuming that ${lastseq} is the sequence
 * number of the last committed transaction; and return the sequence number
 * of the new transaction in ${seqnum}.
 */
int storage_transaction_start_write(NETPACKET_CONNECTION *, uint64_t,
    const uint8_t[32], uint8_t[32]);

/**
 * storage_transaction_start_delete(NPC, machinenum, lastseq, seqnum):
 * As storage_transaction_start_delete, but s/write/delete/.
 */
int storage_transaction_start_delete(NETPACKET_CONNECTION *, uint64_t,
    const uint8_t[32], uint8_t[32]);

/**
 * storage_transaction_start_fsck(NPC, machinenum, seqnum, whichkey):
 * Start a fsck transaction, and return the sequence number of the new
 * transaction in ${seqnum}.  Use the key specified by whichkey.
 */
int storage_transaction_start_fsck(NETPACKET_CONNECTION *, uint64_t,
    uint8_t[32], int);

#endif /* !_STORAGE_INTERNAL_H_ */
