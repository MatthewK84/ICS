# python

Reference models, calibration fitting, synthetic data, statistics, validation scoring and ML training. Python never runs in the live capture or control path.

**Language:** Python 3.12. **Lead role:** Modeling engineer.

Planned layout:

```text
python/
  ics_models/   pointing, calibration, trajectory, drag, footprint, killclass, uncertainty
  ics_synth/    synthetic engagement generator
  ics_stats/    Pk, timeline analysis, validation scorer
```

Rules (enforced in CI by [ICS-006](https://github.com/MatthewK84/ICS/issues/6)):

- Full type annotations, `mypy --strict`, `ruff`; no `noqa` or `type: ignore`.
- No recursion, functions of 50 lines or fewer, no module-level mutable state.
- Every algorithm is written here first as a reviewed reference with golden outputs in `golden/`; the C++ port must match it.
- Parameter files are versioned and schema-validated; C++ loads them at startup.

First issue: [ICS-006](https://github.com/MatthewK84/ICS/issues/6) (toolchain).
