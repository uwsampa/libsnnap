#include "snnap.h"
#include <assert.h>

static const unsigned NBUFS = 2;  // Number of input & output buffers.
static const unsigned BUFSIZE = 64;  // Size of each buffer in bytes.
static volatile char (* const ibuf)[BUFSIZE] = (void*) 0xFFFF8000;
static volatile char (* const obuf)[BUFSIZE] = (void*) 0xFFFFF000;
static unsigned ibn;  // Which buffer is current?
static unsigned obn;
static bool ibf[NBUFS];  // Full flag for each buffer.
volatile static bool obf[NBUFS];  // TODO the NPU needs to set these ones.
static bool invoked[NBUFS];  // Whether the NPU has actually been invoked.

#define INCR_BUF(n) n = (n + 1) % NBUFS

static void dsb() {
    __asm__ __volatile__ ("dsb" : : : "memory");
}

static void sev() {
    __asm__ __volatile__ ("SEV\n");
}

static void wfe() {
    __asm__ __volatile__ ("WFE\n");
}

void snnap_init() {
    ibn = 0;
    obn = 0;
    for (unsigned i = 0; i < NBUFS; ++i) {
        ibf[i] = 0;
        obf[i] = 0;
        invoked[i] = 0;
    }
}


bool snnap_canread() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    return obf[obn];
}

const volatile char *snnap_readbuf() {
    assert(invoked[obn]);
    assert(obf[obn]);
    return obuf[obn];
}

void snnap_consumebuf() {
    assert(obf[obn]);
    assert(ibf[obn]);
    assert(invoked[obn]);
    obf[obn] = false;
    ibf[obn] = false;
    invoked[obn] = false;
    INCR_BUF(obn);
}

void snnap_block() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    while (!obf[obn]) {
        wfe();  // TODO Check this.
    }
}

bool snnap_canwrite() {
    // This is only false when we've come "around the loop" back to a buffer
    // that has been submitted but not consumed.
    return !ibf[ibn];
}

volatile char *snnap_writebuf() {
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
    INCR_BUF(ibn);

    // TODO Check this.
    dsb();
    sev();
}
