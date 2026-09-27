"""Tests for ics_lint.checks."""

from __future__ import annotations

import textwrap
from typing import Final

from hypothesis import given
from hypothesis import strategies as st

from ics_lint.checks import MAX_FUNCTION_LINES, Finding, check_source

PATH: Final = "sample.py"
MAX_GENERATED_FUNCTIONS: Final = 6

type Edges = frozenset[tuple[int, int]]


def findings(source: str) -> list[tuple[int, str]]:
    return [(finding.line, finding.code) for finding in check_source(textwrap.dedent(source), PATH)]


def recursive_names(source: str) -> set[str]:
    return {finding.message.split("'")[1] for finding in check_source(source, PATH) if finding.code == "ICS101"}


def module_with_calls(count: int, edges: Edges) -> str:
    lines: list[str] = []
    for caller in range(count):
        lines.append(f"def f{caller}() -> None:")
        lines.extend(f"    f{callee}()" for source, callee in sorted(edges) if source == caller)
        lines.append("    return None")
    return "\n".join(lines) + "\n"


def functions_on_a_cycle(count: int, edges: Edges) -> set[str]:
    """Independent answer: the transitive closure by Floyd-Warshall."""
    reach = [[(start, end) in edges for end in range(count)] for start in range(count)]
    for via in range(count):
        for start in range(count):
            for end in range(count):
                reach[start][end] = reach[start][end] or (reach[start][via] and reach[via][end])
    return {f"f{index}" for index in range(count) if reach[index][index]}


call_graphs = st.integers(1, MAX_GENERATED_FUNCTIONS).flatmap(
    lambda count: st.tuples(
        st.just(count),
        st.frozensets(st.tuples(st.integers(0, count - 1), st.integers(0, count - 1))),
    )
)


@given(call_graphs)
def test_recursion_matches_the_transitive_closure(graph: tuple[int, Edges]) -> None:
    count, edges = graph
    assert recursive_names(module_with_calls(count, edges)) == functions_on_a_cycle(count, edges)


def test_direct_and_mutual_recursion() -> None:
    source = """
        def countdown(n: int) -> int:
            return countdown(n - 1)

        def ping() -> None:
            pong()

        def pong() -> None:
            ping()
    """
    assert findings(source) == [(2, "ICS101"), (5, "ICS101"), (8, "ICS101")]


def test_methods_recurse_through_self_and_cls() -> None:
    source = """
        class Tree:
            def walk(self) -> None:
                self.walk()

            @classmethod
            def build(cls) -> None:
                cls.build()

            def leaf(self) -> None:
                self.other()
    """
    assert findings(source) == [(3, "ICS101"), (7, "ICS101")]


def test_nested_functions_lambdas_and_definition_time_calls() -> None:
    source = """
        def outer() -> None:
            def inner() -> None:
                inner()

        def by_lambda(n: int) -> list[int]:
            return list(map(lambda x: by_lambda(x), [n]))

        def by_default() -> None:
            def helper(x: object = by_default()) -> None:
                return None

        def by_class_body() -> None:
            class Local:
                value = by_class_body()

        def defines_but_never_calls() -> None:
            def later() -> None:
                defines_but_never_calls()
    """
    assert findings(source) == [(3, "ICS101"), (6, "ICS101"), (9, "ICS101"), (13, "ICS101")]


def test_names_resolve_as_python_resolves_them() -> None:
    source = """
        def run() -> None:
            return None

        class Job:
            def run(self) -> None:
                run()

            def start(self) -> None:
                other.start()
                len([])
    """
    assert findings(source) == []


@given(st.integers(1, 2 * MAX_FUNCTION_LINES))
def test_function_length_limit(statements: int) -> None:
    body = "".join("    x = 1\n" for _ in range(statements))
    source = f"def f() -> None:\n{body}\nasync def g() -> None:\n{body}"
    expected = statements + 1 > MAX_FUNCTION_LINES
    assert {code for _, code in findings(source)} == ({"ICS102"} if expected else set())


def test_mutable_module_state() -> None:
    source = """
        import collections
        from typing import Final

        NAMES = ["a"]
        TABLE: dict[str, int] = {}
        SEEN = {1}
        SQUARES = [n * n for n in range(3)]
        PAIRS = {n: n for n in range(3)}
        ODD = {n for n in range(3)}
        QUEUE = collections.deque()
        EMPTY = list()
        LIMITS = (1, 2)
        FROZEN = frozenset({1})
        COUNT: Final = 3
        HANDLER = HANDLERS[0]()
        pending: int

        def bump() -> None:
            global COUNT
    """
    expected = [(line, "ICS103") for line in (5, 6, 7, 8, 9, 10, 11, 12, 20)]
    assert findings(source) == expected


def test_unparseable_source() -> None:
    assert findings("def broken(:\n") == [(1, "ICS100")]


def test_finding_renders_like_other_linters() -> None:
    finding = Finding("a.py", 3, "ICS101", "function 'f' is recursive")
    assert finding.render() == "a.py:3: ICS101 function 'f' is recursive"
