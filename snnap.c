#include <stdbool.h>
#include <assert.h>

static const unsigned NBUFS = 2;  // Number of input & output buffers.
static const unsigned BUFSIZE = 64;  // Size of each buffer in bytes.
static char (* const ibuf)[BUFSIZE] = (void*) 0xDEF;
static char (* const obuf)[BUFSIZE] = (void*) 0xABC;
static unsigned ibn;  // Which buffer is current?
static unsigned obn;
static bool ibf[NBUFS];  // Full flag for each buffer.
static bool obf[NBUFS];  // TODO the NPU needs to set these ones.
static bool invoked[NBUFS];  // Whether the NPU has actually been invoked.

#define INCR_BUF(n) n = (n + 1) % NBUFS

void snnap_init() {
    ibn = 0;
    obn = 0;
    for (unsigned i = 0; i < NBUFS; ++i) {
        ibf[i] = 0;
        obf[i] = 0;
        invoked[i] = 0;
    }
}

/*
Two possible reading strategies. First is a "try-read":

    if (snnap_canread()) {
        char *buf = snnap_readbuf();  // Must succeed.
        do_something(buf);
        snnap_consumebuf();
    }

Second is a "blocking read":

    snnap_block();
    char *buf = snnap_readbuf();  // Must succeed.
    do_something(buf);
    snnap_consumebuf();

*/

/* Check whether the current output buffer is full, i.e., whether it is safe
 * to call `snnap_readbuf()`.
 */
bool snnap_canread() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    return obf[obn];
}

/* Get the most recent output data from the NPU. Precondition: the current
 * output buffer must be full.
 */
const char *snnap_readbuf() {
    assert(invoked[obn]);
    assert(obf[obn]);
    return obuf[obn];
}

/* "Free" the current output buffer to indicate that it can be filled by
 * another invocation. Precondition: the buffer must be full (i.e., don't call
 * this when the buffer is already free).
 */
void snnap_consumebuf() {
    assert(obf[obn]);
    assert(ibf[obn]);
    assert(invoked[obn]);
    obf[obn] = false;
    ibf[obn] = false;
    invoked[obn] = false;
    INCR_BUF(obn);
}

/* Wait until the current output buffer has data ready. Precondition: the
 * corresponding input buffer is full, but the output buffer itself may or may
 * not be full (i.e., the work has been submitted but may or may not have
 * completed). Postcondition: the output buffer is full.
 */
void snnap_block() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    while (!obf[obn]) {
        // TODO sleep for NPU signal
    }
}

/* Correspondingly, two ways to write:

A "try-write":

    if (snnap_canwrite()) {
        *(snnap_writebuf()) = data;
        snnap_sendbuf();
    }

A "blocking write" may need to read before it can write:

    if (!snnap_canwrite()) {
        snnap_block();
        char *buf = snnap_readbuf();
        do_something(buf);
        snnap_consumebuf();
    }
    assert(snnap_canwrite());  // Must succeed now.
    *(snnap_writebuf()) = data;
    snnap_sendbuf();

*/

bool snnap_canwrite() {
    // This is only false when we've come "around the loop" back to a buffer
    // that has been submitted but not consumed.
    return !ibf[ibn];
}

char *snnap_writebuf() {
    assert(!ibf[ibn]);
    assert(!obf[ibn]);
    assert(!invoked[ibn]);
    ibf[ibn] = true;
    return ibuf[ibn];
}

void snnap_sendbuf() {
    assert(ibf[ibn]);
    assert(!obf[ibn]);
    assert(!invoked[ibn]);
    invoked[ibn] = true;
    // TODO notify the NPU
}
