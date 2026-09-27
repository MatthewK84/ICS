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

## Toolchain

Python 3.12 comes from the operating system (Ubuntu 24.04 ships 3.12.3), and every dependency is pinned with hashes in [`uv.lock`](uv.lock). uv itself is pinned in [`requirements-uv.txt`](requirements-uv.txt), and `pyproject.toml` refuses any other uv version. All tool settings live in [`pyproject.toml`](pyproject.toml).

```sh
cd python
python3.12 -m venv /tmp/uv && /tmp/uv/bin/pip install --require-hashes --no-deps -r requirements-uv.txt
export PATH="/tmp/uv/bin:${PATH}"
uv sync --locked                                     # install the locked dependencies into .venv
uv run --locked ruff check . && uv run --locked ruff format --check .
uv run --locked mypy .                               # strict mode
uv run --locked python -m ics_lint . --exclude policy/seeded
uv run --locked pytest                               # hypothesis, coverage of at least 90%
policy/check-policy.sh
```

To add or upgrade a dependency, change `pyproject.toml`, run `uv lock`, and record the dependency in the [dependency register](../docs/dependency-register.md).

## Power-of-Ten checks

The Python rules from the coding-standards table in [docs/build-plan.md](../docs/build-plan.md) are enforced by tools, not review (ICS-006):

| Rule | Enforced by |
|---|---|
| Full type annotations | `mypy --strict` |
| Early returns, no bare `except`, bandit's security checks, complexity and style | ruff, with the rule set in `pyproject.toml` |
| No recursion (ICS101); functions of 50 lines or fewer (ICS102); no module-level mutable state (ICS103) | [`ics_lint`](ics_lint), the AST checks |
| No suppressions | [`policy/check-policy.sh`](policy/check-policy.sh) rejects `noqa`, `type: ignore`, `pragma: no cover`, inline mypy or pyright settings, and any tool configuration outside `pyproject.toml` |
| Tests | pytest with hypothesis, deterministic examples, warnings as errors, and a coverage floor of 90% |

The policy script also proves each check still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the message named on its first line.

`ics_lint` finds recursion through calls it can resolve inside one module: plain names, looked up as Python does, and `self.`/`cls.` method calls. Recursion across modules or through other objects is not detected. A module-level constant should be a tuple, a frozenset or another immutable type; lists, dicts, sets and `global` statements are rejected.

One exception is deliberate: tests may use plain `assert`, which pytest rewrites, so bandit's `S101` is off for `tests/` only.

Next issue: [ICS-007](https://github.com/MatthewK84/ICS/issues/7) (TypeScript toolchain). The first Python model is [ICS-035](https://github.com/MatthewK84/ICS/issues/35) (mount pointing model, milestone M3).
