# firmware

Firmware for the encoder time-tagger: an MCU on the mount encoder tap, disciplined by the GNSS PPS edge and, through the station host's UTC labels, by PTP, streaming time-tagged mount angles to `ics-mountd`.

**Language:** freestanding C++20, approved on ICS-032 in place of C so that the existing checks apply unchanged. **Lead role:** Controls engineer.

The tagger's core is [`cpp/tagger`](../cpp/tagger), built and tested with the C++ code ([ICS-032](https://github.com/MatthewK84/ICS/issues/32)). The board port is [`encoder_tagger/`](encoder_tagger) ([#163](https://github.com/MatthewK84/ICS/issues/163)).

Target: time-tag error of 50 µs or less against injected PPS (validation criterion C18). This task is on the critical path; a slip here delays every absolute measurement.

First issue: [ICS-032](https://github.com/MatthewK84/ICS/issues/32).
