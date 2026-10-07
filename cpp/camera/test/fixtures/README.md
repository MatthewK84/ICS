# Camera fixtures

- `pims-sample.cine`: a cine that ICS's own `write_cine` wrote, of 7 frames of 16 × 8 pixels at 5,000 frames/s, from 3 frames before the trigger (sha256 `8ea6b8f7abdf801deb7a5305fe368e4e680ae0ee8a98706afd036821c2ef34f1`). [PIMS](https://github.com/soft-matter/pims) 0.7 (BSD-3-Clause), with numpy 2.4.6, read it as follows. `fixture_test.cpp` holds `read_cine` to the same values:

  | Field | PIMS reads |
  |---|---|
  | `image_count`, `first_image_no` | 7, −3 |
  | `bi_width`, `bi_height`, `bi_bit_count` | 16, 8, 16 |
  | `frame_rate`, `shutter_ns`, `real_bpp` | 5000, 150000, 12 |
  | `trigger_time` | 1791331200 s and a fraction of 530242871 / 2³² |
  | frame times (block 1002), UTC ns | 1791331200122856789, then each 200,013 ns later |
  | exposures (block 1003), ns | 150000, then each 1 ns longer |
  | frame 0 | an 8 × 16 array of 16-bit zeros |

  PIMS reads a time stamp's seconds in the local time zone, so it was run with `TZ=UTC`. ICS uses PIMS only as a reference for the layout, and no PIMS code.
