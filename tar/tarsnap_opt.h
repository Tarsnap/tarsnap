#ifndef _TARSNAP_OPT_H
#define _TARSNAP_OPT_H

/* Use multiple TCP connections when writing an archive. */
extern int tarsnap_opt_aggressive_networking;

/* Keep trying forever if connection lost. */
extern int tarsnap_opt_retry_forever;

/* Print statistics using "human-readable" SI prefixes. */
extern int tarsnap_opt_humanize_numbers;

/* Be verbose when warning about network glitches. */
extern int tarsnap_opt_noisy_warnings;

/* Maximum number of bytes to send over the network before QUITing. */
extern uint64_t tarsnap_opt_maxbytesout;

/* Number of bytes to send between checkpoints. */
extern uint64_t tarsnap_opt_checkpointbytes;

/* Number of bytes to process before printing progress. */
extern uint64_t tarsnap_opt_progressbytes;

#endif /* !_TARSNAP_OPT_H_ */
