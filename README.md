libsnnap
========

This is a C library for communicating with the SNNAP neural accelerator.

Copious comments in `snnap.h` describe the interfaces. The `example` directory shows how to use the library. Just type `make run` to see it in action.

The example source uses `libsnnap`'s high-level stream-oriented API, but it also includes an alternative implementation using the library's lower-level API (`#ifdef`'d out by default). The latter is somewhat painful since it requires explicit buffer management, but you can run it instead with `make run STREAM=0`.
