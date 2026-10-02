# golden

Cross-language golden test vectors. Python references and reference tools such as GeographicLib publish them; C++ ports must match them in parity tests before they merge.

**Language:** data files shared by C++, Python and TypeScript. **Lead role:** Modeling engineer.

Contents:

- [`proto/`](proto): one instance of each ICS protobuf message with every field set, written by [`python/ics_golden/proto.py`](../python/ics_golden/proto.py). The C++ and TypeScript tests must read each file and write back the same bytes ([ICS-012](https://github.com/MatthewK84/ICS/issues/12); see [`proto/README.md`](../proto/README.md#golden-files)).
- [`frames/`](frames): geodetic to ECEF, geodetic to range ENU, and EGM96 MSL to ellipsoid height, generated with GeographicLib 2.3 by [`frames/generate.sh`](frames/generate.sh) from the case lists in [`frames/inputs/`](frames/inputs). The C++ frames module, [`cpp/frames`](../cpp/frames), matches every vector within 1 mm in [`cpp/frames/test/golden_test.cpp`](../cpp/frames/test/golden_test.cpp) ([ICS-014](https://github.com/MatthewK84/ICS/issues/14), [ICS-017](https://github.com/MatthewK84/ICS/issues/17)); see [`docs/frames-and-time.md`](../docs/frames-and-time.md#golden-vectors).

Planned contents:

- Golden outputs from each Python reference model in `python/ics_models` (for example [ICS-044](https://github.com/MatthewK84/ICS/issues/44) to [ICS-049](https://github.com/MatthewK84/ICS/issues/49)).

Rules: golden files change only through the reference that produces them, and every change is reviewed with that reference.

Next issue: [ICS-044](https://github.com/MatthewK84/ICS/issues/44), the first Python reference model to publish golden outputs.
