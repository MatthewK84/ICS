---
name: operator-grade-ui
description: Use whenever building or restyling any frontend, dashboard, or web UI. Sets dense operator-grade defaults (tokens, Blueprint, domain stack, screenshot critique) and bans generic AI styling.
---

# Operator-Grade UI

BLUF: Build every interface like an operations console made by a dedicated design team. Make it dense, restrained, token-driven, and designed for a named operator. Never ship the web's average SaaS template.

## Scope and precedence

- Applies to every new frontend, dashboard, internal tool, or web app, and to every restyle request.
- Overrides stack, color, and motion guidance from other loaded skills, including any that prescribe shadcn, Recharts, Lucide, or Framer Motion defaults.
- Domain patterns from other skills (org charts, Gantt, KPI layouts) still apply. Render them under these rules.
- If the user explicitly asks for a marketing page or a named style, follow the user. State which rules you relaxed.

## Workflow

Run in order. Do not write component code before step 4.

1. Write the operator statement and pick the density tier (Section 1).
2. Detect the existing stack and pick the build path (Section 2).
3. Read the bans (Section 3).
4. Create the token file (Section 4).
5. Build the shell layout, then the views (Sections 5 and 6).
6. Add the domain stack where the domain calls for it (Section 7).
7. Seed realistic data (Section 8).
8. Run the banned-pattern scan (Section 9).
9. Close the visual loop for 2-3 rounds (Section 10).
10. Report (Section 11).

## 1. Operator statement and density tier

Write one sentence before any code. Put it at the top of your response and in a comment at the top of the app shell file.

Template: [Role] doing [task] for [duration] on [display] in [environment], handling [data volume], deciding within [time].

Examples:

- Watch officer monitoring 200 air tracks on a 27-inch 1440p monitor on a dim ops floor, 8-hour shift, deciding within 10 seconds.
- Test director reviewing 40 interceptor engagements per day on a 1080p laptop in a range trailer, comparing results against criteria.
- MBA student exploring a pricing decision for 30 minutes on a 14-inch laptop in a classroom.

If the user gave no operator, infer one from context, state it, and proceed. Ask only when nothing hints at the user.

Pick the tier from the operator statement:

| Tier | Operator | Theme | Base font | Table row | Character |
|---|---|---|---|---|---|
| 1 Ops | Monitoring live data, long shifts | Dark | 13px (12px tables) | 24px | Max density, keyboard first, map or table is the workspace |
| 2 Analyst | Investigating, comparing, filtering | Dark or light | 13px | 28px | Dense tables, linked charts, inspector panel |
| 3 Decision | Briefing, learning, deciding | Light | 14px | 32px | Fewer, larger charts, same bans and token discipline |

Density follows the operator, not taste. Tier 3 is still not a landing page.

## 2. Stack selection

Read package.json, existing components, and existing styles first.

- Existing UI library: keep it. Apply tokens and bans on top. Do not swap libraries without approval.
- New React app: use Blueprint (`@blueprintjs/core`, `@blueprintjs/icons`, plus `@blueprintjs/table` or TanStack Table for large tables).
- Blueprint dark theme: apply the `Classes.DARK` constant to the root container. Never hardcode the class string, because the prefix changes between major versions.
- Use Blueprint components as designed. Do not restyle their internals.
- Tailwind present: expose tokens as CSS variables and map them in the Tailwind theme. Use only token-backed utilities.
- Tailwind and Blueprint together: disable Tailwind preflight, because its reset breaks Blueprint components.
- Never mix component libraries (no shadcn with Blueprint, no MUI with Blueprint).
- No framework: plain CSS with the token file and CSS modules.
- Verify every library API against the installed version's type definitions or docs. Do not write library calls from memory.
- Self-host fonts. Never load fonts from a CDN in air-gapped builds.

Code standards apply even when the session carries no other instructions:

- TypeScript strict mode. Explicit `interface` or `type` for every data shape, prop set, parameter, and return value.
- No `any`, no non-null assertion (`!`), no `@ts-ignore`, no `eslint-disable`.
- Functions under 50 lines. No recursion. No global mutable state.

## 3. Banned defaults

These patterns are the generic generative-AI look. Never produce them unless the user lifts a specific ban by name.

Layout:

- Hero sections, centered single-column pages, "Welcome back" headers.
- Grids of equal cards as the primary page structure.
- Giant KPI numbers (above 24px in Tier 1-2, above 32px in Tier 3).
- Page-level scroll in an app shell. Panels scroll; the shell does not.
- Modals for content that fits inline or in the inspector.

Surface:

- Gradients of any kind, including gradient text and gradient borders.
- Glassmorphism, backdrop blur, frosted panels.
- Border radius above 4px. Status dots and avatars are the only exception.
- Drop shadows on static panels. Shadows belong only on floating layers (menus, popovers, dialogs).
- Decorative illustrations, blobs, mesh backgrounds, noise textures.

Color:

- Indigo, violet, or purple as a default brand or accent color.
- Decorative color. Every hue maps to a meaning in the color map (Section 4).
- Pure #000 backgrounds or pure #FFF text.
- Library default chart palettes and boxed legends.

Content:

- Emoji anywhere in the UI.
- Marketing copy: "Unlock insights," "Supercharge," exclamation points, sparkle "AI" icons.
- Decorative icons beside every heading.
- Pill badges on everything.
- Lorem ipsum and round placeholder numbers (100, 1,000, $12,345).

Motion:

- Transitions above 150ms, bounce or spring easing, parallax, animated number counters, shimmer on everything.
- Animated layout properties. Animate opacity and transform only.
- Ignoring `prefers-reduced-motion`.

## 4. Tokens

Create `src/styles/tokens.css` before writing components. All colors, sizes, and spacing come from tokens. A hex value outside this file is a defect.

```css
:root {
  /* Spacing: 4px grid */
  --space-1: 4px;
  --space-2: 8px;
  --space-3: 12px;
  --space-4: 16px;
  --space-6: 24px;
  --space-8: 32px;

  /* Type */
  --font-ui: Inter, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  --font-mono: "JetBrains Mono", "IBM Plex Mono", ui-monospace, Menlo, Consolas, monospace;
  --text-xs: 11px;   /* axis labels, metadata, section labels */
  --text-sm: 12px;   /* dense tables, secondary text */
  --text-md: 13px;   /* base UI text */
  --text-lg: 14px;   /* panel titles, Tier 3 base */
  --text-xl: 16px;   /* view title */
  --text-2xl: 20px;  /* metric values; max size in Tier 1-2 */
  --leading-tight: 1.25;
  --leading-normal: 1.4;
  --weight-regular: 400;
  --weight-medium: 500;
  --weight-semibold: 600;

  /* Shape and sizing */
  --radius-sm: 2px;
  --radius-md: 4px;
  --border-width: 1px;
  --row-h-dense: 24px;
  --row-h: 28px;
  --row-h-relaxed: 32px;
  --control-h: 28px;
  --topbar-h: 36px;

  /* Motion */
  --duration-fast: 100ms;
  --duration-base: 150ms;
  --ease-out: cubic-bezier(0.2, 0, 0, 1);
}

/* Dark theme: default for Tier 1-2 */
:root,
[data-theme="dark"] {
  --bg-app: #0B0E12;
  --bg-panel: #11161C;
  --bg-raised: #171D25;     /* toolbars, table headers */
  --bg-hover: #1E2530;
  --bg-selected: #1B2A3D;
  --border-subtle: #222A35;
  --border: #2D3643;
  --border-strong: #3D4757;

  --text-primary: #E3E7ED;
  --text-secondary: #A1AAB8;
  --text-muted: #8992A0;    /* 4.5:1 or better on every dark surface */
  --text-disabled: #4A5260;

  --accent: #4C90F0;        /* the only interactive hue: links, primary action, selection */
  --accent-hover: #6AA4F5;
  --focus-ring: #8ABBFF;

  --status-ok: #32A467;
  --status-caution: #E3A008;
  --status-critical: #EA696D;
  --status-stale: #8992A0;  /* always paired with dashed border or STALE text */

  /* MIL-STD-2525 affiliation fills: track symbology and affiliation chips only */
  --aff-friend: #80E0FF;
  --aff-hostile: #FF8080;
  --aff-neutral: #AAFFAA;
  --aff-unknown: #FFFF80;

  /* Categorical series: charts only, max 6 per view */
  --cat-1: #6CA6E8;
  --cat-2: #8F7FD8;
  --cat-3: #4FB3A9;
  --cat-4: #C98A5A;
  --cat-5: #B57BA6;
  --cat-6: #8A939F;
}

/* Light theme: default for Tier 3, daylight work, print */
[data-theme="light"] {
  --bg-app: #F2F4F7;
  --bg-panel: #FFFFFF;
  --bg-raised: #EEF1F4;
  --bg-hover: #E6EAEF;
  --bg-selected: #DCE8F8;
  --border-subtle: #E1E5EA;
  --border: #D3D8DE;
  --border-strong: #B8C0CA;

  --text-primary: #1C2127;
  --text-secondary: #4A5361;
  --text-muted: #5B6775;
  --text-disabled: #A7B0BB;

  --accent: #2865BB;
  --accent-hover: #1F5299;
  --focus-ring: #4C90F0;

  --status-ok: #1B7547;
  --status-caution: #8B5D00;
  --status-critical: #BE2E34;
  --status-stale: #5B6775;
}
```

Set `data-theme` on the `html` element. With Blueprint, pick token values that sit inside Blueprint's own gray range rather than overriding Blueprint.

Color rules:

- One accent hue for interaction. Never use the accent to mean status.
- Show status as text, an icon, a small dot, a 2-3px left border, or a 12-16% tint (`color-mix(in srgb, var(--status-critical) 14%, transparent)`). Never as a large solid fill.
- Make every color signal redundant with text, an icon, or a shape. Operators may be color-deficient, on NVGs, or printing in grayscale.
- Reserve affiliation colors for track symbology and affiliation chips. Never reuse them for status or chart series.
- Mark map selection with a neutral high-contrast halo (`--text-primary`), not a hue.
- Hold text to 4.5:1 contrast and meaningful graphics or borders to 3:1.

Typography rules:

- Apply `font-variant-numeric: tabular-nums` to every number that changes or aligns in a column. Set it globally on tables, metric strips, and readouts.
- Use the mono font for IDs, coordinates, frequencies, hashes, and date-time groups.
- Section labels: 11px, uppercase, letter-spacing 0.04em, `--text-muted`, weight 600. Use sentence case everywhere else.
- Use three sizes or fewer per panel. Build hierarchy with weight and color before size.

## 5. Layout

App shell, desktop first, minimum supported width 1280px:

```
+----------------------------------------------------------------------+
| Top bar 36px: product | view switcher | status | UTC clock           |
+------+--------------------------------------------+------------------+
| Nav  | Toolbar 32px: filters, search, layers      | Inspector        |
| rail +--------------------------------------------+ 320-400px        |
| 40px | Primary workspace: map, table, or          | selected item    |
|      | timeline; fills remaining space            | details and      |
|      +--------------------------------------------+ actions          |
|      | Bottom drawer: event log or timeline       |                  |
+------+--------------------------------------------+------------------+
```

Layout rules:

- Fix the shell at 100vh. Panels scroll internally. Allow no horizontal page scroll at 1280px or wider.
- Separate panels with 1px `--border` lines, not gaps and shadows. Keep gutters at 0-8px.
- Pad panels 8-12px. Panel headers run 32px with an uppercase label left and actions right.
- Make splits resizable. Persist sizes in localStorage wrapped in try/catch.
- Give each view one primary focal region. The map or main table takes at least 60% of the area.
- Left-align on shared grid lines. Center only empty-state messages and icon buttons.
- Put metrics in a compact horizontal strip (48-56px tall) above the workspace, never in a card grid.
- Render a classification banner only when the user specifies a marking. Never invent a classification level.

## 6. Components

Tables:

- Header 28px on `--bg-raised`. Sticky, sortable, resizable columns.
- Rows 24px in Tier 1, 28px in Tier 2, 32px in Tier 3. Divide rows with 1px `--border-subtle`. No zebra striping.
- Right-align numbers with tabular numerals and fixed precision per column. Left-align text.
- Put units in the header, not in every cell.
- Truncate with an ellipsis and show the full value in a tooltip. Never wrap rows.
- Selected row: `--bg-selected` plus a 2px `--accent` left border. Hover: `--bg-hover`.
- Virtualize above 100 rows.

Metric strip:

- 11px uppercase muted label above an 18-20px tabular value. Add an optional delta with a direction glyph.
- Color a value with a status token only when a threshold defines the meaning. State the threshold in the tooltip.

Charts:

- Gridlines in `--border-subtle`. Axes and ticks at 11px in `--text-muted`. No chart borders or chart backgrounds.
- Label series directly instead of using a legend when there are 4 or fewer series.
- Start bar charts at zero. Show time axes in UTC.
- Replace pie charts above 4 slices with bars. Never use 3D charts.
- Draw single-series charts in `--text-secondary` or `--accent`. Use `--cat-*` for multi-series and `--status-*` only for status meaning.
- Animate data updates for 150ms or less, or not at all.

Controls and icons:

- Controls 28px tall (24px in Tier 1 toolbars). Radius 2-4px.
- Use one icon set (Blueprint icons with Blueprint) at 16px, or 14px in dense rows.
- Place icons only where they carry meaning or mark a known control.
- Allow one primary button per view region. Make everything else minimal or outlined.

Copy:

- Write terse, specific labels. "12 tracks stale over 30s" beats "Some data may be out of date."
- Make empty states name what is missing and the next action: "No tracks in AOI. Widen the AOI or clear filters."

States: every data view ships all five.

- Loading: a skeleton that matches the final layout. Never a centered spinner on a blank panel.
- Empty: as above.
- Error: inline, with the error detail and a retry control.
- Stale: last-update timestamp, muted or dashed treatment, and the text STALE.
- Partial: show what loaded and flag what failed.

Time, units, and coordinates:

- Display UTC. Use `HH:mm:ssZ` for clocks and date-time groups (`271412Z OCT 26`) where the domain expects them.
- Show relative ages ("12s ago") for freshness and the absolute time on hover.
- Show lat/lon to 5 decimals (about 1 m). Offer MGRS where the operator thinks in grids.
- Label altitude reference and units (ft MSL, m AGL). Never show a bare number.

## 7. Domain stack

Use these when the domain calls for them.

Geospatial and C2:

- 2D map: MapLibre GL JS with a dark, low-saturation basemap and muted labels so overlays dominate.
- High-volume overlays (tracks, trails, heatmaps, range rings): deck.gl layers on MapLibre through `MapboxOverlay` from `@deck.gl/mapbox`.
- 3D globe, terrain, and airspace volumes: CesiumJS, with Resium for React.
- Symbology: milsymbol from SIDC codes. Pre-render symbols into an icon atlas for deck.gl `IconLayer`. Never build SVGs per frame.
- Coordinates: the `mgrs` package for MGRS conversion.
- Always show a scale bar, a cursor coordinate readout (lat/lon and MGRS), the track count, and data freshness.
- Outline AOIs and zones with 1-2px strokes. Fill at 10% or less.
- Air-gapped or classified deployments: self-host tiles (PMTiles), terrain, imagery, and fonts. Cesium ion and hosted basemaps need network access and tokens. Never hardcode API keys.

Tables at volume: Blueprint Table, or TanStack Table with TanStack Virtual.

Time series and charts:

- uPlot for high-rate streaming data.
- Observable Plot or visx for analytic charts.
- Theme every chart from tokens. Recharts, Chart.js, and ECharts are acceptable only when fully restyled from tokens, never with defaults.

## 8. Realistic data

Put mock data in `src/mock/` behind an explicit flag. Generate it with a seeded PRNG (for example mulberry32) so screenshots are reproducible. Type every record.

Volume minimums:

- Tables: 200+ rows.
- Tracks: 30-150, with trails.
- Time series: 1 hour at 1 Hz (3,600 points) per series, or a domain-appropriate equivalent.
- Event logs: 500+ entries.

Mess to include:

- Nulls and missing fields in 5-10% of records.
- Strings from 3 to 60 characters, including names that must truncate.
- Out-of-range values, negatives, and zeroes where the domain allows them.
- Mixed sensor precision (2-7 decimals), normalized at display time.
- Duplicates, out-of-order timestamps, and records stale by minutes or hours.
- At least one error record and one record in every status state.
- Domain-real identifiers: track numbers (TN 40213), SIDCs, tail numbers, unit designators, serials.

Never use round numbers, "John Doe," "Acme," "Item 1," or lorem ipsum.

## 9. Banned-pattern scan

Run the scan before every screenshot round. Add it to package.json as `lint:ui`. Fix every hit or justify it in the report. Zero matches is the pass condition; `rg` exits 1 on no matches, so invert the exit code in CI (`! rg ...`). Adjust `src` to the project's source root.

```bash
# Banned utility classes and styles
rg -n --glob '!**/node_modules/**' \
  -e 'gradient' -e 'backdrop-blur' \
  -e 'rounded-(md|lg|xl|2xl|3xl)' -e 'shadow-(md|lg|xl|2xl)' \
  -e '(indigo|violet|purple|fuchsia)-[0-9]{2,3}' \
  -e 'text-([3-9]xl)' -e 'animate-(bounce|pulse)' src

# Hard-coded colors outside the token file
rg -n --glob '!**/tokens.css' --glob '!**/node_modules/**' \
  -e '#[0-9a-fA-F]{3,8}\b' -e 'rgba?\(' src

# Emoji
rg -n -P '[\x{1F300}-\x{1FAFF}\x{2600}-\x{27BF}]' src

# Suppression markers and escape hatches
rg -n -e '@ts-ignore' -e '@ts-expect-error' -e 'eslint-disable' \
  -e ': any\b' -e 'as any\b' src
```

## 10. Visual loop

Run 2-3 rounds of render, screenshot, critique, fix.

1. Start the dev server in the background. Install Chromium once with `npx playwright install chromium`.
2. Capture three sizes, replacing the URL with the actual dev server address:

```bash
npx playwright screenshot --viewport-size "1920,1080" --wait-for-timeout 2000 http://localhost:5173 shot-1080.png
npx playwright screenshot --viewport-size "1366,768" --wait-for-timeout 2000 http://localhost:5173 shot-768.png
npx playwright screenshot --viewport-size "2560,1440" --wait-for-timeout 2000 http://localhost:5173 shot-1440.png
```

3. Capture each state (empty, loading, error, stale) through a URL flag or mock switch.
4. Open every image. Critique it against the checklist below. Write numbered findings with location and fix.
5. Fix the highest-impact findings first. Re-run the scan, then re-capture.
6. Stop after round 3 or when no checklist item fails.

If no browser can run in the environment, say so. Then do a code-level audit against the checklist plus the scan. Never claim visual verification you did not perform.

Checklist:

1. Operator fit: the layout serves the operator statement at the chosen tier.
2. Focal region: one clear primary workspace holds 60% or more of the area.
3. Alignment: edges and baselines share grid lines; all spacing is a 4px multiple.
4. Density: Tier 1 shows 30+ table rows at 1080p; no dead whitespace regions.
5. Type: three sizes or fewer per panel; tabular numerals on every number; mono for IDs and coordinates.
6. Contrast: text at 4.5:1, meaningful graphics at 3:1; muted text stays readable.
7. Color semantics: every hue maps to the color map; nothing decorative; color is redundant with text or shape.
8. Overflow: no wrapped table rows, no clipped text without a tooltip, no horizontal page scroll.
9. States: empty, loading, error, stale, and partial all exist and look designed.
10. Bans: nothing from Section 3 is visible.
11. Scale: the 1366x768 capture stays usable; the 2560x1440 capture leaves no empty bands.

## 11. Report

End with a short report:

- Operator statement and tier used.
- Stack chosen, with the reason in one clause.
- Screenshot rounds completed and checklist items still failing.
- Scan hits left in place, with justification.
- The one thing most likely to look wrong to the user.
