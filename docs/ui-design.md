# Front-end design standard

Every ICS view follows the operator-grade UI standard in [`.claude/skills/operator-grade-ui/SKILL.md`](../.claude/skills/operator-grade-ui/SKILL.md):
- dense and restrained;
- built from design tokens;
- designed for a named operator, never a generic SaaS template.

This page records how ICS applies it: who each view is for, the stack, the offline rules, the one deviation, and how CI enforces it. Where this page and the standard differ, this page says why. Otherwise the standard governs.

## Operators and density tiers

Each Phase 5 view ([build plan](build-plan.md)) has an operator statement, which sets its density tier. The statement goes in a comment at the top of the view's shell file.

| View | Operator statement | Tier |
|---|---|---|
| UI-03 Range status | Test conductor monitoring 3 to 6 stations' time quality, GNSS and holdover, mount pointing, sun cone and trigger state on a 27-inch 1440p display in range control, over a 6 to 10 hour test day, deciding within 10 seconds | 1 Ops |
| UI-04 Engagement timeline, UI-05 Run quick-look | Test director reviewing about 40 interceptor engagements a day on a 1080p laptop in a range trailer, comparing each against its C4 time gates and criteria | 2 Analyst |
| UI-06 3D engagement viewer, UI-07 Footprint map, UI-08 Imagery review | Test analyst investigating one engagement's trajectories, debris and frames on a 1080p laptop or 1440p display, cross-checking views against each other | 2 Analyst |
| UI-09 Evaluator workflow | Evaluator signing off runs, adjudicating no-tests and overriding kill classes with a reason, on a 1080p laptop, every decision audited | 2 Analyst |
| UI-10 Validation dashboard | Program reviewer briefing C1 to C19 status and its evidence, on a 1080p display or in print | 3 Decision |

Tier 1 and 2 views use the dark theme; Tier 3 and print use the light theme. The theme is set with `data-theme` on `<html>`.

## Stack

| Need | Choice | Notes |
|---|---|---|
| App | Vite, React 18, strict TypeScript ([FND-05](build-plan.md)) | Already in [`web/`](../web/README.md) |
| Components and icons | Blueprint: `@blueprintjs/core`, `@blueprintjs/icons` | Added by UI-01 ([ICS-082](https://github.com/MatthewK84/ICS/issues/82)), with dependency-register entries. Dark theme through the `Classes.DARK` constant, never the class string |
| Tables at volume | `@blueprintjs/table`, or TanStack Table with TanStack Virtual | Virtualize above 100 rows |
| Footprint map (UI-07) | MapLibre GL JS, with deck.gl layers through `MapboxOverlay` | Offline PMTiles; dark, low-saturation basemap |
| 3D engagement viewer (UI-06) | three.js | The deviation below |
| Track symbology | milsymbol, pre-rendered into an icon atlas | Never built per frame |
| Charts | uPlot for streaming series; Observable Plot or visx for analysis | Themed from tokens; no library default palettes |
| Coordinates | `mgrs` | Wherever an operator thinks in grids |
| Fonts | Inter and JetBrains Mono, self-hosted | Added by UI-01; until then the tokens fall back to system fonts |

Use no other component library alongside Blueprint, and verify every library call against the installed version's types.

## Offline

The range network allows no external calls ([build plan](build-plan.md), Phase 5):
- serve every font, tile, icon and asset from the app itself;
- load nothing from a CDN;
- embed no API key or token.

The standard's air-gapped rules apply to every ICS build.

## Deviation

**UI-06 uses three.js, not CesiumJS.** The standard names CesiumJS for 3D globes, terrain and airspace volumes. The engagement viewer is none of those. It is a local scene in the range ENU frame, a few kilometres across, that must draw 5,000 tracks at 30 frames a second or more. three.js draws that scene directly, without a globe, terrain or Cesium ion. It is themed from the tokens like every other view. If ICS ever needs a globe or terrain view, that view uses CesiumJS with Resium.

## No classification banner

ICS is developed from public information only ([SECURITY.md](../SECURITY.md)). As the standard requires, no view shows a classification banner unless a marking is specified.

## Enforcement

- **Tokens.** [`web/apps/ics-web/src/styles/tokens.css`](../web/apps/ics-web/src/styles/tokens.css) holds every colour, size and spacing value, exactly as the standard's section 4 defines them. A colour anywhere else under an app's `src/` fails CI.
- **Banned patterns.** [`web/policy/check-policy.sh`](../web/policy/check-policy.sh) runs the standard's section 9 scan over every app's `src/` in the TypeScript toolchain workflow. It uses GNU grep, because the CI image has no ripgrep. It checks for:
  - gradient, blur, large-radius and shadow utilities;
  - indigo, violet and purple;
  - hard-coded colours outside `tokens.css`;
  - emoji;
  - escape hatches.

  Each pattern group has a seeded violation in [`web/policy/seeded`](../web/policy/seeded) that the scan must reject.
- **Views.** Every UI task's pull request carries:
  - the operator statement and tier;
  - the five states (loading, empty, error, stale, partial);
  - realistic seeded mock data from `src/mock/`;
  - 2 or 3 rounds of the standard's screenshot loop at 1366×768, 1920×1080 and 2560×1440;
  - the standard's section 11 report, as part of its close-out.
