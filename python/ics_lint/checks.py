"""The checks behind ics_lint. Each takes a parsed module and returns its findings."""

from __future__ import annotations

import ast
from dataclasses import dataclass
from typing import Final

MAX_FUNCTION_LINES: Final = 50
MUTABLE_CONSTRUCTORS: Final = frozenset(
    {"bytearray", "ChainMap", "Counter", "defaultdict", "deque", "dict", "list", "OrderedDict", "set"}
)
MUTABLE_DISPLAYS: Final = (ast.Dict, ast.DictComp, ast.List, ast.ListComp, ast.Set, ast.SetComp)
SELF_NAMES: Final = frozenset({"cls", "self"})

type FunctionNode = ast.FunctionDef | ast.AsyncFunctionDef
type CallGraph = dict[str, set[str]]


@dataclass(frozen=True, order=True)
class Finding:
    """One rule violation, reported as ``path:line: CODE message``."""

    path: str
    line: int
    code: str
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.code} {self.message}"


@dataclass(frozen=True)
class Function:
    """A function or method, named by its dotted path inside the module."""

    qualname: str
    scope: str
    node: FunctionNode


@dataclass(frozen=True)
class Definitions:
    functions: dict[str, Function]
    classes: frozenset[str]


def check_source(source: str, path: str) -> list[Finding]:
    """Run every check on one module's source text."""
    try:
        tree = ast.parse(source, filename=path)
    except SyntaxError as error:
        return [Finding(path, error.lineno or 1, "ICS100", f"cannot parse: {error.msg}")]
    findings = [
        *recursion_findings(tree, path),
        *function_length_findings(tree, path),
        *module_state_findings(tree, path),
    ]
    return sorted(findings)


def function_length_findings(tree: ast.Module, path: str, limit: int = MAX_FUNCTION_LINES) -> list[Finding]:
    """ICS102: functions longer than ``limit`` lines, counted from ``def`` to the last line."""
    findings: list[Finding] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.FunctionDef | ast.AsyncFunctionDef):
            continue
        length = (node.end_lineno or node.lineno) - node.lineno + 1
        if length > limit:
            message = f"function '{node.name}' is {length} lines long; the limit is {limit}"
            findings.append(Finding(path, node.lineno, "ICS102", message))
    return findings


def module_state_findings(tree: ast.Module, path: str) -> list[Finding]:
    """ICS103: mutable values bound at module level, and any global statement."""
    findings: list[Finding] = []
    for statement in tree.body:
        value = assigned_value(statement)
        if value is not None and is_mutable(value):
            message = "mutable value at module level; use a tuple, frozenset or immutable type"
            findings.append(Finding(path, statement.lineno, "ICS103", message))
    for node in ast.walk(tree):
        if isinstance(node, ast.Global):
            findings.append(Finding(path, node.lineno, "ICS103", "global statement"))
    return findings


def recursion_findings(tree: ast.Module, path: str) -> list[Finding]:
    """ICS101: functions that can reach themselves through calls resolved inside the module."""
    definitions = collect_definitions(tree)
    graph = call_graph(definitions)
    findings: list[Finding] = []
    for function in definitions.functions.values():
        if reaches_itself(graph, function.qualname):
            message = f"function '{function.qualname}' is recursive"
            findings.append(Finding(path, function.node.lineno, "ICS101", message))
    return findings


def assigned_value(statement: ast.stmt) -> ast.expr | None:
    if isinstance(statement, ast.Assign):
        return statement.value
    if isinstance(statement, ast.AnnAssign):
        return statement.value
    return None


def is_mutable(value: ast.expr) -> bool:
    if isinstance(value, MUTABLE_DISPLAYS):
        return True
    if not isinstance(value, ast.Call):
        return False
    return callee_name(value.func) in MUTABLE_CONSTRUCTORS


def callee_name(func: ast.expr) -> str:
    if isinstance(func, ast.Name):
        return func.id
    if isinstance(func, ast.Attribute):
        return func.attr
    return ""


def collect_definitions(tree: ast.Module) -> Definitions:
    """Every function and class in the module, walked with an explicit stack."""
    functions: dict[str, Function] = {}
    classes: set[str] = set()
    stack: list[tuple[ast.AST, str]] = [(tree, "")]
    while stack:
        node, scope = stack.pop()
        for child in ast.iter_child_nodes(node):
            if isinstance(child, ast.FunctionDef | ast.AsyncFunctionDef | ast.ClassDef):
                qualname = f"{scope}.{child.name}" if scope else child.name
                if isinstance(child, ast.ClassDef):
                    classes.add(qualname)
                else:
                    functions[qualname] = Function(qualname, scope, child)
                stack.append((child, qualname))
            else:
                stack.append((child, scope))
    return Definitions(functions, frozenset(classes))


def call_graph(definitions: Definitions) -> CallGraph:
    graph: CallGraph = {}
    for function in definitions.functions.values():
        targets = {resolve_call(call, function, definitions) for call in own_calls(function.node)}
        graph[function.qualname] = {target for target in targets if target}
    return graph


def own_calls(function: FunctionNode) -> list[ast.Call]:
    """Calls that run when the function runs.

    That includes the decorators and defaults of functions it defines, the
    bodies of classes it defines, and lambdas (which have no name of their own
    to call), but not the bodies of the functions it defines.
    """
    calls: list[ast.Call] = []
    stack: list[ast.AST] = [*function.body]
    while stack:
        node = stack.pop()
        if isinstance(node, ast.FunctionDef | ast.AsyncFunctionDef):
            stack.extend(definition_time_nodes(node))
            continue
        if isinstance(node, ast.Call):
            calls.append(node)
        stack.extend(ast.iter_child_nodes(node))
    return calls


def definition_time_nodes(node: FunctionNode) -> list[ast.AST]:
    """The parts of a function definition that run where it is defined."""
    kw_defaults = [default for default in node.args.kw_defaults if default is not None]
    return [*node.decorator_list, *node.args.defaults, *kw_defaults]


def resolve_call(call: ast.Call, caller: Function, definitions: Definitions) -> str:
    """The qualified name a call refers to, or "" when it is not a function of this module."""
    func = call.func
    if isinstance(func, ast.Name):
        return resolve_name(func.id, caller, definitions)
    if isinstance(func, ast.Attribute) and isinstance(func.value, ast.Name) and func.value.id in SELF_NAMES:
        target = f"{caller.scope}.{func.attr}"
        if caller.scope in definitions.classes and target in definitions.functions:
            return target
    return ""


def resolve_name(name: str, caller: Function, definitions: Definitions) -> str:
    """Look a bare name up as Python does: enclosing functions, then the module; class bodies are skipped."""
    parts = caller.qualname.split(".")
    for depth in range(len(parts), -1, -1):
        scope = ".".join(parts[:depth])
        target = f"{scope}.{name}" if scope else name
        if scope not in definitions.classes and target in definitions.functions:
            return target
    return ""


def reaches_itself(graph: CallGraph, start: str) -> bool:
    seen: set[str] = set()
    stack = list(graph.get(start, ()))
    while stack:
        current = stack.pop()
        if current == start:
            return True
        if current in seen:
            continue
        seen.add(current)
        stack.extend(graph.get(current, ()))
    return False
