# web

The read-only operator and evaluator interface on `ics-api`: range status, engagement timeline, run quick-look, 3D viewer, footprint map, imagery review, evaluator workflow and validation dashboard.

**Language:** TypeScript with React. **Lead role:** Front-end engineer.

Rules (enforced in CI by [ICS-007](https://github.com/MatthewK84/ICS/issues/7)):

- The UI never commands mounts, cameras or triggers; operators command stations from the station console.
- Strict TypeScript: no `any`, no non-null assertion, no `var`, no floating promises; functions of 50 lines or fewer.
- Responses are validated with zod at the API boundary; no untyped data reaches components.
- The range network allows no external calls: serve every asset and map tile locally.

First issues: [ICS-007](https://github.com/MatthewK84/ICS/issues/7) (toolchain), [ICS-082](https://github.com/MatthewK84/ICS/issues/82) (scaffold).
