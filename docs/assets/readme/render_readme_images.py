"""Render the README's PNG images: header, flow charts and language bar.

Run from the repository root:

    python3 docs/assets/readme/render_readme_images.py

The script uses only the Python standard library to write SVG, then
rsvg-convert (Debian/Ubuntu package librsvg2-bin) to turn each SVG into a
PNG at twice its drawn size. Output goes to docs/img/. The Inter font gives
the intended look; any sans-serif font works.
"""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Final

ROOT: Final[pathlib.Path] = pathlib.Path(__file__).resolve().parents[3]
OUT_DIR: Final[pathlib.Path] = ROOT / "docs" / "img"
ZOOM: Final[str] = "2"
FONT: Final[str] = "Inter, 'Segoe UI', Roboto, 'DejaVu Sans', sans-serif"
MONO: Final[str] = "'DejaVu Sans Mono', Menlo, Consolas, monospace"

INK: Final[str] = "#1F2328"
MUTED: Final[str] = "#59636E"
LINE: Final[str] = "#D0D7DE"
CARD: Final[str] = "#FFFFFF"
ACCENT: Final[str] = "#0969DA"


class RenderError(Exception):
    """Raised when an image cannot be written or converted."""


@dataclass(frozen=True)
class Stage:
    """One column of the data-flow chart."""

    title: str
    phase: str
    items: tuple[str, ...]
    tint: str
    planned: bool


@dataclass(frozen=True)
class Lane:
    """One workflow row of the CI chart."""

    workflow: str
    checks: tuple[str, ...]


@dataclass(frozen=True)
class Language:
    """How a file extension is counted in the language bar."""

    name: str
    colour: str
    suffixes: tuple[str, ...]


LANGUAGES: Final[tuple[Language, ...]] = (
    Language("C++", "#F34B7D", (".cpp", ".hpp", ".h", ".cc")),
    Language("Python", "#3572A5", (".py",)),
    Language("TypeScript", "#3178C6", (".ts", ".tsx")),
    Language("Protobuf", "#7A5AF8", (".proto",)),
    Language("CMake", "#2DA44E", (".cmake", "CMakeLists.txt")),
    Language("Shell", "#89E051", (".sh",)),
    Language("CI YAML", "#CB171E", (".yml", ".yaml")),
)

EXCLUDED_PARTS: Final[tuple[str, ...]] = ("/gen/", "golden/", "/policy/seeded")


def esc(text: str) -> str:
    """Escape text for an SVG text node."""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def svg_open(width: int, height: int, label: str) -> str:
    """Return the opening tag and title of an SVG document."""
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="{esc(label)}"><title>{esc(label)}</title>'
    )


def text(x: float, y: float, body: str, size: int, fill: str, weight: int = 400,
         anchor: str = "start", family: str = FONT) -> str:
    """Return one SVG text element."""
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" font-size="{size}" '
        f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{esc(body)}</text>'
    )


def rect(x: float, y: float, w: float, h: float, fill: str, stroke: str,
         radius: int = 6, dashed: bool = False) -> str:
    """Return one rounded SVG rectangle."""
    dash = ' stroke-dasharray="4 3"' if dashed else ""
    return (
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{radius}" '
        f'fill="{fill}" stroke="{stroke}"{dash}/>'
    )


def arrow_defs() -> str:
    """Return the shared arrowhead marker."""
    return (
        '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
        f'<path d="M0,0 L10,5 L0,10 z" fill="{MUTED}"/></marker></defs>'
    )


def arrow(x1: float, y1: float, x2: float, y2: float) -> str:
    """Return a straight arrow between two points."""
    return (
        f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
        f'stroke="{MUTED}" stroke-width="1.4" marker-end="url(#arrow)"/>'
    )


def card(width: int, height: int) -> str:
    """Return the light card every chart sits on."""
    return rect(0.5, 0.5, width - 1, height - 1, CARD, LINE, radius=10)


def lcg_points(count: int, width: int, height: int) -> list[tuple[float, float, float]]:
    """Return deterministic star positions and sizes from a fixed-seed LCG."""
    state: int = 20261122
    points: list[tuple[float, float, float]] = []
    for _ in range(count):
        state = (1103515245 * state + 12345) % 2147483648
        x = (state % 10000) / 10000 * width
        state = (1103515245 * state + 12345) % 2147483648
        y = (state % 10000) / 10000 * height
        state = (1103515245 * state + 12345) % 2147483648
        r = 0.3 + (state % 100) / 100 * 0.8
        points.append((x, y, r))
    return points


def header_sky(width: int, height: int) -> str:
    """Return the header's gradient sky, stars and range grid."""
    parts: list[str] = [
        '<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#0B1220"/><stop offset="0.62" stop-color="#16243A"/>'
        '<stop offset="1" stop-color="#2B3F5C"/></linearGradient>'
        '<linearGradient id="ground" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#101826"/><stop offset="1" stop-color="#0A0F18"/>'
        "</linearGradient></defs>",
        f'<rect width="{width}" height="{height}" fill="url(#sky)"/>',
    ]
    for x, y, r in lcg_points(90, width, int(height * 0.62)):
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.2f}" fill="#C9D6EA" opacity="0.55"/>')
    horizon = height * 0.78
    parts.append(f'<rect x="0" y="{horizon:.1f}" width="{width}" height="{height - horizon:.1f}" fill="url(#ground)"/>')
    for step in range(0, 13):
        x_far = width * 0.5 + (step - 6) * 40
        x_near = width * 0.5 + (step - 6) * 170
        parts.append(
            f'<line x1="{x_far:.1f}" y1="{horizon:.1f}" x2="{x_near:.1f}" y2="{height}" '
            'stroke="#2E4A6E" stroke-width="0.6" opacity="0.7"/>'
        )
    for row in (0.0, 0.25, 0.55, 1.0):
        y = horizon + (height - horizon) * row
        parts.append(f'<line x1="0" y1="{y:.1f}" x2="{width}" y2="{y:.1f}" stroke="#2E4A6E" stroke-width="0.6" opacity="0.7"/>')
    return "".join(parts)


def station(x: float, y: float) -> str:
    """Return a small camera station: tripod, mount and two cameras."""
    leg = "#8FA3BF"
    return (
        f'<line x1="{x:.1f}" y1="{y - 18:.1f}" x2="{x - 12:.1f}" y2="{y:.1f}" stroke="{leg}" stroke-width="1.6"/>'
        f'<line x1="{x:.1f}" y1="{y - 18:.1f}" x2="{x + 12:.1f}" y2="{y:.1f}" stroke="{leg}" stroke-width="1.6"/>'
        f'<line x1="{x:.1f}" y1="{y - 18:.1f}" x2="{x:.1f}" y2="{y:.1f}" stroke="{leg}" stroke-width="1.6"/>'
        f'<rect x="{x - 8:.1f}" y="{y - 30:.1f}" width="16" height="12" rx="2" fill="#5B7090"/>'
        f'<rect x="{x - 14:.1f}" y="{y - 40:.1f}" width="28" height="9" rx="2" fill="#B8C7DC"/>'
        f'<circle cx="{x + 14:.1f}" cy="{y - 35.5:.1f}" r="3" fill="#7FD1FF"/>'
    )


def header() -> str:
    """Return the header banner SVG."""
    width, height = 960, 280
    point = (742.0, 74.0)
    left = (560.0, 214.0)
    right = (900.0, 222.0)
    parts: list[str] = [svg_open(width, height, "ICS: Intercept Characterization System"), header_sky(width, height)]
    for base in (left, right):
        parts.append(
            f'<line x1="{base[0] + 14:.1f}" y1="{base[1] - 36:.1f}" x2="{point[0]:.1f}" y2="{point[1]:.1f}" '
            'stroke="#7FD1FF" stroke-width="1.1" stroke-dasharray="5 4" opacity="0.85"/>'
        )
    parts.append(
        f'<path d="M 470 150 Q 620 70 {point[0]:.1f} {point[1]:.1f}" fill="none" stroke="#E8EEF6" '
        'stroke-width="1" stroke-dasharray="1.5 3" opacity="0.7"/>'
        f'<path d="M 950 40 Q 850 60 {point[0]:.1f} {point[1]:.1f}" fill="none" stroke="#E8EEF6" '
        'stroke-width="1" stroke-dasharray="1.5 3" opacity="0.7"/>'
        f'<ellipse cx="{point[0]:.1f}" cy="{point[1]:.1f}" rx="16" ry="7" transform="rotate(-18 {point[0]:.1f} {point[1]:.1f})" '
        'fill="#7FD1FF" fill-opacity="0.12" stroke="#7FD1FF" stroke-width="1"/>'
        f'<circle cx="{point[0]:.1f}" cy="{point[1]:.1f}" r="2.2" fill="#FFFFFF"/>'
    )
    parts.append(text(point[0] - 24, point[1] - 14, "closest approach ± σ", 10, "#BFD3EA", anchor="end", family=MONO))
    parts.append(station(*left))
    parts.append(station(*right))
    parts.append(text(left[0] - 4, left[1] + 18, "station A", 10, "#8FA3BF", anchor="middle", family=MONO))
    parts.append(text(right[0] - 4, right[1] + 18, "station B", 10, "#8FA3BF", anchor="middle", family=MONO))
    parts.append(text(48, 104, "ICS", 56, "#F2F5F9", weight=700))
    parts.append(text(50, 138, "Intercept Characterization System", 21, "#DCE4EE", weight=500))
    parts.append(text(50, 170, "Time-tagged stereo-optical measurement of counter-sUAS interceptors", 13, "#A9B8CC"))
    parts.append(text(50, 192, "UTC nanoseconds · WGS84 and range ENU · traceable to raw data", 13, "#A9B8CC"))
    parts.append(f'<rect x="50" y="210" width="64" height="3" rx="1.5" fill="{ACCENT}"/>')
    parts.append("</svg>")
    return "".join(parts)


STAGES: Final[tuple[Stage, ...]] = (
    Stage("Sources", "on the range", ("PLI: four feeds", "Radar tracks", "High-speed cameras", "Tracking camera", "Mount encoders"), "#F6F8FA", False),
    Stage("Ingest", "Phase 1", ("ics-capd: pcap", "MAVLink adapter", "CoT adapter", "Lattice adapter", "SAPIENT adapter"), "#DDF4FF", False),
    Stage("Time & cameras", "Phase 1", ("ics-timingd: PTP", "Strobe offsets", "Camera drivers", "ics-camd: arm, offload"), "#DDF4FF", False),
    Stage("Store & align", "Phase 1", ("ics-plid: Parquet", "Onboard log import", "Clock fit to UTC"), "#DDF4FF", False),
    Stage("Station control", "Phase 2", ("Encoder time-tagger", "Line of sight", "Cue extrapolator", "Closed-loop tracker", "Hand-off scheduler", "Trigger controller", "Mount client"), "#DAFBE1", False),
    Stage("Products", "Phase 4 · planned", ("LOS + refraction", "Detection", "Closest approach", "Debris estimation", "Signed run records"), "#FFFFFF", True),
)


def stage_column(x: float, top: float, width: float, stage: Stage) -> str:
    """Return one data-flow column: its heading and item boxes."""
    stroke = "#8C959F" if stage.planned else LINE
    parts: list[str] = [
        rect(x, top, width, 44, stage.tint, stroke, dashed=stage.planned),
        text(x + width / 2, top + 19, stage.title, 13, INK, weight=600, anchor="middle"),
        text(x + width / 2, top + 35, stage.phase, 10, MUTED, anchor="middle"),
    ]
    for index, item in enumerate(stage.items):
        y = top + 56 + index * 30
        parts.append(rect(x + 6, y, width - 12, 24, CARD, stroke, radius=4, dashed=stage.planned))
        parts.append(text(x + width / 2, y + 16, item, 10, INK, anchor="middle"))
    return "".join(parts)


def dataflow() -> str:
    """Return the station data-flow chart SVG."""
    width, height = 960, 400
    col_w, gap, left, top = 134.0, 25.0, 15.0, 52.0
    parts: list[str] = [svg_open(width, height, "ICS station data flow"), arrow_defs(), card(width, height)]
    parts.append(text(24, 32, "How data moves through a station", 15, INK, weight=600))
    for index, stage in enumerate(STAGES):
        x = left + index * (col_w + gap)
        parts.append(stage_column(x, top, col_w, stage))
        if index > 0:
            parts.append(arrow(x - gap + 3, top + 22, x - 3, top + 22))
    bands = (
        ("Reference models · Phase 3", "Python first (ics_models); every C++ port is held to golden vectors", "#FFF8C5"),
        ("Foundations · Phases 0 to 1", "proto contracts · common · logging · config · frames (WGS84, ENU, EGM96)", "#F6F8FA"),
    )
    for index, (title, body, tint) in enumerate(bands):
        y = 318 + index * 38
        parts.append(rect(15, y, width - 30, 30, tint, LINE))
        parts.append(text(28, y + 19.5, title, 11, INK, weight=600))
        parts.append(text(214, y + 19.5, body, 11, MUTED))
    parts.append("</svg>")
    return "".join(parts)


LANES: Final[tuple[Lane, ...]] = (
    Lane("C++ toolchain", ("GCC 13 reproducible", "ASan", "UBSan", "TSan", "Coverage", "Power of Ten")),
    Lane("Python toolchain", ("ruff", "mypy", "pytest", "Golden files")),
    Lane("TypeScript toolchain", ("Prettier", "ESLint", "tsc", "Vitest")),
    Lane("Protobuf", ("Lint", "Breaking changes", "Generated code")),
    Lane("CUDA toolchain", ("ics-cuda image", "Kernel build")),
    Lane("CodeQL", ("c-cpp", "python", "javascript")),
    Lane("Dependency register", ("Pins", "License review")),
    Lane("AI assist", ("Declaration",)),
)


def lane_row(x: float, y: float, lane: Lane) -> str:
    """Return one CI lane: the workflow name and its check chips."""
    parts: list[str] = [
        rect(x, y, 150, 26, "#DDF4FF", LINE, radius=4),
        text(x + 10, y + 17, lane.workflow, 11, INK, weight=600),
    ]
    chip_x = x + 160
    for check in lane.checks:
        chip_w = 14 + len(check) * 6.1
        parts.append(rect(chip_x, y + 2, chip_w, 22, "#F6F8FA", LINE, radius=11))
        parts.append(text(chip_x + chip_w / 2, y + 17, check, 10, INK, anchor="middle"))
        chip_x += chip_w + 6
    return "".join(parts)


def flow_box(x: float, y: float, w: float, h: float, title: str, body: tuple[str, ...], tint: str) -> str:
    """Return a titled box with up to three lines of body text."""
    parts: list[str] = [rect(x, y, w, h, tint, LINE), text(x + w / 2, y + 22, title, 12, INK, weight=600, anchor="middle")]
    for index, line in enumerate(body):
        parts.append(text(x + w / 2, y + 40 + index * 15, line, 10, MUTED, anchor="middle"))
    return "".join(parts)


def ci_pipeline() -> str:
    """Return the CI pipeline chart SVG."""
    width, height = 960, 380
    lane_top, lane_step = 58.0, 32.0
    mid = lane_top + lane_step * (len(LANES) - 1) / 2 + 13
    parts: list[str] = [svg_open(width, height, "ICS CI pipeline"), arrow_defs(), card(width, height)]
    parts.append(text(24, 32, "From pull request to signed evidence", 15, INK, weight=600))
    parts.append(flow_box(18, mid - 36, 112, 72, "Pull request", ("path filters pick", "the workflows"), "#F6F8FA"))
    parts.append(arrow(132, mid, 150, mid))
    for index, lane in enumerate(LANES):
        parts.append(lane_row(154, lane_top + index * lane_step, lane))
    parts.append(arrow(752, mid, 772, mid))
    parts.append(flow_box(776, mid - 52, 166, 104, "Merge to main", ("required checks green", "review approved", "squash merge"), "#DAFBE1"))
    parts.append(arrow(859, mid + 54, 859, 318))
    parts.append(flow_box(776, 320, 166, 50, "Evidence on push", ("signed SBOM + provenance",), "#FFF8C5"))
    parts.append(rect(18, 332, 740, 38, CARD, "#8C959F", dashed=True))
    parts.append(text(30, 355, "On demand or on matching paths:", 11, INK, weight=600))
    parts.append(text(236, 355, "SITL rig · Capture bench · Timing bench · Lattice sandbox · Sync milestones", 11, MUTED))
    parts.append("</svg>")
    return "".join(parts)


def golden_flow() -> str:
    """Return the Python-first, golden-vector parity chart SVG."""
    width, height = 960, 150
    boxes = (
        ("Python reference", ("python/ics_models", "typed, 90% coverage")),
        ("Golden writer", ("python/ics_golden", "pinned BLAS kernels")),
        ("Golden vectors", ("golden/", "inputs and expected outputs")),
        ("C++ port", ("cpp/<module>", "JSF AV, Power of Ten")),
        ("Parity gate", ("ctest in CI", "numeric tolerance")),
    )
    box_w, gap, top = 166.0, 22.0, 52.0
    parts: list[str] = [svg_open(width, height, "Python-first reference models and golden parity"), arrow_defs(), card(width, height)]
    parts.append(text(24, 32, "Every algorithm is modelled in Python first", 15, INK, weight=600))
    for index, (title, body) in enumerate(boxes):
        x = 18 + index * (box_w + gap)
        tint = "#FFF8C5" if index < 3 else "#DDF4FF"
        parts.append(flow_box(x, top, box_w, 78, title, body, tint))
        if index > 0:
            parts.append(arrow(x - gap + 2, top + 39, x - 2, top + 39))
    parts.append("</svg>")
    return "".join(parts)


def tracked_files() -> list[str]:
    """Return the repository's tracked file paths."""
    try:
        result = subprocess.run(["git", "ls-files"], cwd=ROOT, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RenderError(f"git ls-files failed: {error}") from error
    return result.stdout.splitlines()


def language_of(path: str) -> str | None:
    """Return the counted language of a path, or None if it is not counted."""
    if any(part in path for part in EXCLUDED_PARTS):
        return None
    for language in LANGUAGES:
        if any(path.endswith(suffix) for suffix in language.suffixes):
            return language.name
    return None


def count_lines() -> dict[str, int]:
    """Return lines of hand-written source per language."""
    counts: dict[str, int] = {language.name: 0 for language in LANGUAGES}
    for path in tracked_files():
        name = language_of(path)
        if name is None:
            continue
        try:
            body = (ROOT / path).read_bytes()
        except OSError as error:
            raise RenderError(f"cannot read {path}: {error}") from error
        counts[name] += body.count(b"\n")
    return counts


def language_bar(counts: dict[str, int]) -> str:
    """Return the stacked language bar SVG."""
    width, height = 960, 118
    total = sum(counts.values())
    if total == 0:
        raise RenderError("no source lines counted")
    parts: list[str] = [svg_open(width, height, "Lines of source by language"), card(width, height)]
    parts.append(text(24, 30, f"Hand-written source by language · {total:,} lines", 13, INK, weight=600))
    parts.append('<clipPath id="bar"><rect x="24" y="44" width="912" height="14" rx="7"/></clipPath><g clip-path="url(#bar)">')
    x = 24.0
    for language in LANGUAGES:
        share = counts[language.name] / total * 912
        parts.append(f'<rect x="{x:.2f}" y="44" width="{share:.2f}" height="14" fill="{language.colour}"/>')
        x += share
    parts.append("</g>")
    legend_x = 24.0
    for language in LANGUAGES:
        label = f"{language.name} {counts[language.name] / total * 100:.1f}%"
        parts.append(f'<circle cx="{legend_x + 5:.1f}" cy="86" r="5" fill="{language.colour}"/>')
        parts.append(text(legend_x + 15, 90, label, 11, INK))
        legend_x += 30 + len(label) * 6.4
    parts.append(text(24, 108, "Excludes generated code, lockfiles, golden data and images.", 9, MUTED))
    parts.append("</svg>")
    return "".join(parts)


def to_png(name: str, svg: str) -> None:
    """Write one SVG to a temporary file and convert it to docs/img/<name>.png."""
    converter = shutil.which("rsvg-convert")
    if converter is None:
        raise RenderError("rsvg-convert not found; install librsvg2-bin")
    source = OUT_DIR / f".{name}.svg"
    target = OUT_DIR / f"{name}.png"
    try:
        source.write_text(svg, encoding="utf-8")
        subprocess.run([converter, "-z", ZOOM, "-o", str(target), str(source)], check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RenderError(f"cannot render {name}: {error}") from error
    finally:
        source.unlink(missing_ok=True)


def main() -> int:
    """Render every README image; return a process exit code."""
    try:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        to_png("header", header())
        to_png("dataflow", dataflow())
        to_png("ci-pipeline", ci_pipeline())
        to_png("golden-flow", golden_flow())
        to_png("languages", language_bar(count_lines()))
    except RenderError as error:
        print(f"render_readme_images: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
