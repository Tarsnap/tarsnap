#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

struct proto_conn {
    volatile int fd;
    volatile int state;
};

struct events {
    pthread_t threads[2];
    sig_atomic_t conndone;
};

void warnp(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    static char msg[128];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fputs(msg, stderr);
}

void proto_conn_drop(void *cookie) {
    struct proto_conn *C = (struct proto_conn *)cookie;
    if (C) {
        C->state = 0;
    }
}

int events_spin(struct events *ET) {
    /* Simulates the event loop */
    return 1; /* Return 1 to indicate a "fail/finish" state for the spin */
}

void *
pushbits(void *arg) {
    struct events *ET = (struct events *)arg;
    /* Simulate work done by pushbits reading/writing shared state */
    while (ET->conndone) {
        /* Do work */
        ET->conndone = 0; /* Trigger loop break */
    }
    return NULL;
}

int main(void) {
    struct proto_conn conn = { .fd = 3, .state = 1 };
    struct events ET = { .conndone = 1 };

    /* Spawn two threads simulating pushbits */
    /* Thread 1: Read from s[0] to stdout */
    pthread_create(&ET.threads[1], NULL, pushbits, &ET);
    /* Thread 0: Read from stdin to s[0] */
    pthread_create(&ET.threads[0], NULL, pushbits, &ET);

    /* Loop until we're done with the connection. */
    if (events_spin(&ET.conndone)) {
        warnp("Error running event loop");
        /* Jump to the start of the cleanup cascade */
        goto err6;
    }

    /* err6: Cancel/Join Thread 1 (The "Vulnerable" Thread 1 path) */
    err6:
        pthread_cancel(ET.threads[1]);
        if (pthread_join(ET.threads[1], NULL) != 0) {
            warnp("Join thread 1 failed");
        }
        /* Fall through to err7 */

        /* err7: Cancel/Join Thread 0 (The "Vulnerable" Thread 0 path) */
        err7:
        pthread_cancel(ET.threads[0]);
        if (pthread_join(ET.threads[0], NULL) != 0) {
            warnp("Join thread 0 failed");
        }
        
        /* Fall through to err5 */
        err5:
            proto_conn_drop(&conn); /* Drop connection state */
            
            /* Cleanup remaining socket if exists */
            if (conn.fd > 0) close(conn.fd);

            return 0;
    }

    return 0;
}