# web

The read-only operator and evaluator interface on `ics-api`: range status, engagement timeline, run quick-look, 3D viewer, footprint map, imagery review, evaluator workflow and validation dashboard.

**Language:** TypeScript with React. **Lead role:** Front-end engineer.

Rules (enforced in CI by [ICS-007](https://github.com/MatthewK84/ICS/issues/7)):

- The UI never commands mounts, cameras or triggers; operators command stations from the station console.
- Strict TypeScript: no `any`, no non-null assertion, no `var`, no floating promises; functions of 50 lines or fewer.
- Responses are validated with zod at the API boundary; no untyped data reaches components.
- The range network allows no external calls: serve every asset and map tile locally.

## Toolchain

`web/` is a pnpm workspace: `apps/ics-web` is the application (a minimal Vite and React 18 page until [ICS-082](https://github.com/MatthewK84/ICS/issues/82) scaffolds the real one), and `packages/eslint-plugin-ics` holds ICS's own lint rules. Every version is pinned exactly and locked in [`pnpm-lock.yaml`](pnpm-lock.yaml); pnpm itself is pinned, with its checksum, in `package.json`, and pnpm refuses any Node.js version but 24.20.0.

CI runs everything in Microsoft's Playwright image, pinned by digest, which carries Node.js 24.20.0 and the matching browsers. To work the same way locally:

```sh
docker run --rm -it -v "$PWD":/work -w /work/web \
  mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27 bash
corepack enable && pnpm install --frozen-lockfile
pnpm run format:check && pnpm run lint && pnpm run typecheck
pnpm run test                        # Vitest, coverage of at least 80%
pnpm run build && pnpm run e2e       # Playwright against the production build
policy/check-policy.sh
```

## Power-of-Ten checks

The TypeScript rules from the coding-standards table in [docs/build-plan.md](../docs/build-plan.md) are enforced by tools, not review (ICS-007):

| Rule | Enforced by |
|---|---|
| Strict types: no `any`, no non-null assertion, no unsafe values, explicit return types | [`tsconfig.base.json`](tsconfig.base.json) (`strict` and more) and typescript-eslint's strict type-checked rules in [`eslint.config.js`](eslint.config.js) |
| No floating promises | `@typescript-eslint/no-floating-promises` |
| No `var`; `const` by default; no parameter mutation; no state on `window` or `globalThis` | ESLint core rules |
| No recursion | `ics/no-recursion` in [`packages/eslint-plugin-ics`](packages/eslint-plugin-ics), for direct and same-file mutual recursion, including `this.method()` calls |
| Functions of 50 lines or fewer | `max-lines-per-function`, counting every line |
| No suppressions | Inline `eslint` comments have no effect and fail the lint, `@ts-` directives are banned, and [`policy/check-policy.sh`](policy/check-policy.sh) also rejects `prettier-ignore` and coverage-ignore comments and any extra lint, format or TypeScript settings |
| Tests | Vitest with at least 80% coverage, and Playwright end-to-end tests |

The policy script also proves each rule still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the message named on its first line.

Next issue: [ICS-008](https://github.com/MatthewK84/ICS/issues/8) (CI test stages). The application work starts at [ICS-082](https://github.com/MatthewK84/ICS/issues/82).
