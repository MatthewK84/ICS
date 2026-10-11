# encoder_tagger

The board port of the encoder time-tagger (FW-01). The tagger's core, the part that runs the same on any board, is [`ics::tagger`](../../cpp/tagger) ([ICS-032](https://github.com/MatthewK84/ICS/issues/32)):
- the PPS-disciplined clock;
- the sampler;
- the datagram.

The core is freestanding C++20: it allocates nothing after start-up and throws nothing, so it builds for the MCU with arm-none-eabi-g++ (`-fno-exceptions -fno-rtti`). The port is [#163](https://github.com/MatthewK84/ICS/issues/163), which waits for the board, a cross toolchain in the toolchain image, and a PPS bench.

## Target

An STM32H7-class MCU:
- Ethernet, for the datagrams to `ics-mountd`;
- a 32-bit timer at 100 MHz or more off a crystal within ±50 ppm, with input capture for the PPS edge;
- hardware quadrature decoding of each axis's incremental A/B/Z encoder tap.

An absolute encoder (BiSS-C or SSI) needs a different reader in the port; the core is the same.

## What the port provides

| Duty | How |
|---|---|
| The counter | Free-running 32-bit timer, read through one `CounterExtender`. Readings must be less than 2^31 ticks apart, and may come slightly out of order. |
| PPS | The timer's capture of each rising edge, passed to `PpsClock::on_pps` after extension. |
| UTC labels | Each label from the host, passed to `PpsClock::label` with the counter read when it arrived. |
| Samples | A 1 kHz tick that latches the counter and both encoder counters together, passed to `Sampler::add`. |
| Datagrams | Each full batch, written by `encode` into a 256-byte buffer and sent over UDP to `ics-mountd`. |
| Failed checks | `ics::check` reports through the C library's `write` to stderr; the port routes it to a serial port. |

The host reads each datagram with `ics::tagger::decode`. Times follow [Encoder clocks](../../docs/frames-and-time.md#encoder-clocks).

## Done when

Time-tag error of 50 µs or less (C18) against injected PPS, on the board. The emulated bench in [`cpp/tagger/test/bench_test.cpp`](../../cpp/tagger/test/bench_test.cpp) already proves it against an emulated station.
