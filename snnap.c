#include "snnap.h"
#include <assert.h>

static const unsigned NBUFS = 2;  // Number of input & output buffers.
static const unsigned BUFSIZE = 4096;  // Size of each buffer in bytes.
static volatile void * const ibuf_begin = (void*) 0xFFFF0000;
static volatile void * const obuf_begin = (void*) 0xFFFF8000;
static unsigned ibn;  // Which buffer is current?
static unsigned obn;
static bool ibf[NBUFS];  // Full flag for each buffer.
static bool invoked[NBUFS];  // Whether the NPU has actually been invoked.

#define INCR_BUF(n) n = (n + 1) % NBUFS

// ARM data synchronization barrier.
static void dsb() {
    __asm__ __volatile__ ("dsb" : : : "memory");
}

// Send event.
static void sev() {
    __asm__ __volatile__ ("SEV\n");
}

// Wait for event (maybe).
static void wfe() {
    __asm__ __volatile__ ("WFE\n");
}

// Index calculations.
static volatile void *ibuf(unsigned n) {
    return ibuf_begin + BUFSIZE * n;
}
static volatile void *obuf(unsigned n) {
    return obuf_begin + BUFSIZE * n;
}
// Especially hacky: the flag for determining whether the output is ready is
// just the 4th byte of the output. This has the obvious drawback of totally
// breaking if the output has a zero in this position.
// N.B. This is a difference from npu.h, which uses the last byte of the
// output, so we don't need to know the output width.
static volatile bool *obf(unsigned n) {
    return obuf(n) + 3;
}

void snnap_init() {
    ibn = 0;
    obn = 0;
    for (unsigned i = 0; i < NBUFS; ++i) {
        ibf[i] = 0;
        *obf(i) = 0;
        invoked[i] = 0;
    }
}

bool snnap_canread() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    return *obf(obn);
}

const volatile void *snnap_readbuf() {
    assert(invoked[obn]);
    assert(*obf(obn));
    return obuf(obn);
}

void snnap_consumebuf() {
    assert(*obf(obn));
    assert(ibf[obn]);
    assert(invoked[obn]);
    *obf(obn) = false;
    ibf[obn] = false;
    invoked[obn] = false;
    INCR_BUF(obn);
}

void snnap_block() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    while (!*obf(obn)) {
        wfe();  // TODO Check this.
    }
}

bool snnap_canwrite() {
    // This is only false when we've come "around the loop" back to a buffer
    // that has been submitted but not consumed.
    return !ibf[ibn];
}

volatile void *snnap_writebuf() {
    assert(!ibf[ibn]);
    assert(*obf(ibn));
    assert(!invoked[ibn]);
    ibf[ibn] = true;
    return ibuf(ibn);
}

void snnap_sendbuf() {
    assert(ibf[ibn]);
    assert(*obf(ibn));
    assert(!invoked[ibn]);
    invoked[ibn] = true;
    INCR_BUF(ibn);

    // TODO Check this.
    dsb();
    sev();
}
