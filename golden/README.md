# golden

Cross-language golden test vectors. Python references publish them; C++ ports must match them in parity tests before they merge.

**Language:** data files shared by C++ and Python. **Lead role:** Modeling engineer.

Planned contents:

- Frame and time conversion vectors generated with GeographicLib ([ICS-014](https://github.com/MatthewK84/ICS/issues/14)).
- Golden outputs from each Python reference model in `python/ics_models` (for example [ICS-044](https://github.com/MatthewK84/ICS/issues/44) to [ICS-049](https://github.com/MatthewK84/ICS/issues/49)).

Rules: golden files change only through the reference that produces them, and every change is reviewed with that reference.

First issue: [ICS-014](https://github.com/MatthewK84/ICS/issues/14).
