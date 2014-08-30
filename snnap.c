#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "xil_types.h"
#include "xil_mmu.h"

#include "snnap.h"

#define NBUFS 1
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
static volatile int *obf(unsigned n) {
    return obuf(n) + 3;
}

void snnap_init() {
    // TLB page settings.
    Xil_SetTlbAttributes((u32) ibuf_begin, 0x15C06);
    Xil_SetTlbAttributes((u32) obuf_begin, 0x15C06);

    ibn = 0;
    obn = 0;
    for (unsigned i = 0; i < NBUFS; ++i) {
        ibf[i] = 0;
        *obf(i) = 0;
        invoked[i] = 0;
    }
}

/** reading by buffer **/

bool snnap_canread() {
    assert(ibf[obn]);
    assert(invoked[obn]);
    return !!(*obf(obn));
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
    *obf(obn) = 0;
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

/** writing by buffer **/

bool snnap_canwrite() {
    // This is only false when we've come "around the loop" back to a buffer
    // that has been submitted but not consumed.
    return !ibf[ibn];
}

volatile void *snnap_writebuf() {
    assert(!ibf[ibn]);
    assert(!*obf(ibn));
    assert(!invoked[ibn]);
    ibf[ibn] = true;
    return ibuf(ibn);
}

void snnap_sendbuf() {
    assert(ibf[ibn]);
    assert(!*obf(ibn));
    assert(!invoked[ibn]);
    invoked[ibn] = true;
    INCR_BUF(ibn);

    // TODO Check this.
    dsb();
    sev();
}

/** stream interface **/

struct snnap_stream {
    unsigned inputSize;
    unsigned outputSize;
    unsigned numInvocations[NBUFS];
    void (*callback)(const volatile void *);
    volatile void *openIbuf;
};

struct snnap_stream *snnap_stream_new(
        unsigned iSize, unsigned oSize, void (*callback)(const volatile void *)
) {
    struct snnap_stream *stream = malloc(sizeof(struct snnap_stream));
    stream->inputSize = iSize;
    stream->outputSize = oSize;
    for (unsigned i = 0; i < NBUFS; ++i) {
        stream->numInvocations[i] = 0;
    }
    stream->callback = callback;
    stream->openIbuf = 0;
    return stream;
}

static unsigned stream_pos(struct snnap_stream *stream) {
    return stream->numInvocations[ibn] * stream->inputSize;
}

static void stream_consume(struct snnap_stream *stream) {
    snnap_block();
    const volatile void *buf = snnap_readbuf();
    const volatile void *bufEnd = buf +
        stream->numInvocations[obn] * stream->outputSize;
    for (const volatile void *pos = buf; pos < bufEnd;
            pos += stream->outputSize) {
        stream->callback(pos);
    }
    snnap_consumebuf();
    stream->numInvocations[obn] = 0;
}

volatile void *snnap_stream_write(struct snnap_stream *stream) {
    if (!stream->openIbuf) {
        // No buffer currently open. Get the next one.
        // Make sure we have room to write by consuming prior invocations.
        if (!snnap_canwrite()) {
            stream_consume(stream);
        }
        stream->openIbuf = snnap_writebuf();
    }

    unsigned ipos = stream_pos(stream);
    assert(ipos + stream->inputSize <= BUFSIZE);
    return stream->openIbuf + ipos;
}

void snnap_stream_send(struct snnap_stream *stream) {
    ++(stream->numInvocations[ibn]);
    unsigned ipos = stream_pos(stream);
    assert(ipos <= BUFSIZE);
    if (ipos + stream->inputSize > BUFSIZE) {
        // Next invocation would fill the buffer. Send.
        snnap_sendbuf();
        stream->openIbuf = 0;
    }
}

void snnap_stream_put(struct snnap_stream *stream, const void *data) {
    volatile void *buf = snnap_stream_write(stream);
    memcpy((void *)buf, data, stream->inputSize);
    snnap_stream_send(stream);
}

void snnap_stream_barrier(struct snnap_stream *stream) {
    // Finish the last invocation, if anything has been enqueued.
    if (stream->openIbuf) {
        snnap_sendbuf();
    }

    // Cycle through all buffers and check for in-flight invocations.
    unsigned obnOrig = obn;
    for (obn = 0; obn < NBUFS; ++obn) {
        if (stream->numInvocations[obn]) {
            assert(invoked[obn]);
            stream_consume(stream);
        }
    }
    obn = obnOrig;
}
