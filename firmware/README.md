# firmware

Firmware for the encoder time-tagger: an MCU or FPGA on the mount encoder tap, disciplined by PPS and PTP, streaming time-tagged mount angles to `ics-mountd`.

**Language:** C (or HDL if an FPGA is chosen). **Lead role:** Controls engineer.

Planned layout: `encoder_tagger/`.

Target: time-tag error of 50 µs or less against injected PPS (validation criterion C18). This task is on the critical path; a slip here delays every absolute measurement.

First issue: [ICS-032](https://github.com/MatthewK84/ICS/issues/32).
