#ifndef SNNAP_H
#define SNNAP_H

#include <stdbool.h>

/* Initialize the library's state.
 */
void snnap_init();

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
bool snnap_canread();

/* Get the most recent output data from the NPU. Precondition: the current
 * output buffer must be full.
 */
const volatile void *snnap_readbuf();

/* "Free" the current output buffer to indicate that it can be filled by
 * another invocation. Precondition: the buffer must be full (i.e., don't call
 * this when the buffer is already free).
 */
void snnap_consumebuf();

/* Wait until the current output buffer has data ready. Precondition: the
 * corresponding input buffer is full, but the output buffer itself may or may
 * not be full (i.e., the work has been submitted but may or may not have
 * completed). Postcondition: the output buffer is full.
 */
void snnap_block();

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

/* Check whether it is safe to call `snnap_writebuf()`, i.e., there is an open
 * slot for new inputs to the NPU (the current input buffer is not full).
 */
bool snnap_canwrite();

/* Get a pointer to the buffer where inputs should be written. Precondition:
 * the current input buffer is not full.
 */
volatile void *snnap_writebuf();

/* Invoke the NPU on the most recently written NPU. Move to the next input
 * buffer. Precondition: the current write buffer is full but has not been
 * invoked yet.
 */
void snnap_sendbuf();

/* The stream-based interface provides an alternative.

You need to define a callback:

    void handle(const volatile void *data) {
        // Read N bytes of data.
    }

Then invoke like this:

    struct snnap_stream *stream = snnap_stream_new(N, M, handle);
    for (...) {
        *snnap_stream_write(steam) = data;
        snnap_stream_send(stream);
    }
    snnap_stream_barrier(stream);
    free(stream);

*/

/* The snnap_stream encapsulates the state for a "streaming" invocation of the
 * accelerator. Initialize it by passing the size of your inputs, the size of
 * the neural network's outputs (both in bytes), and a callback function. The
 * callback will be invoked with each output element in the same order the
 * inputs are sent.
*/
struct snnap_stream;
typedef struct snnap_stream *snnap_stream_t;
snnap_stream_t snnap_stream_new(unsigned iSize, unsigned oSize,
        void (*callback)(const volatile void *));

/* Write a single input to the stream. Use the returned pointer to write
 * exactly the number of bytes declared as the input size for this stream.
 */
volatile void *snnap_stream_write(snnap_stream_t stream);

/* Mark the end of writing an input. Call this after every invocation of
 * snnap_stream_write() before moving on to the next input.
 */
void snnap_stream_send(snnap_stream_t stream);

/* Consume all outputs from the stream.
 */
void snnap_stream_barrier(snnap_stream_t stream);

/* Deallocate a stream object. First executes a barrier.
 */
void snnap_stream_free(snnap_stream_t stream);

#endif
