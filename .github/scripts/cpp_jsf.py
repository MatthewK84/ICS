"""Gate the C++ held to the JSF AV C++ Coding Standards (ICS-037, docs/cpp-jsf-av.md).

  cpp_jsf.py check --root ROOT --build BUILD (--paths FILE | --files FILE ...)
      checks every C++ file under the paths listed in FILE, or the files given

The paths file lists one path from the repository root per line, a folder or
a file; # starts a comment. Three kinds of check run, and every finding names
the AV rule it enforces:

- text checks, on the source with its comments and literals blanked: the
  rules no tool here checks, such as the pre-processor rules, the banned
  library facilities, braces on their own lines, continue and break, and the
  universal types;
- clang-tidy with the JSF profile, cpp/policy/jsf-clang-tidy.yaml, passed
  with --config so the CI clang-tidy cache hashes its contents;
- clang-query matchers, for the rules about types and expressions that
  clang-tidy 17 has no check for, such as unsigned arithmetic, the layout
  of parameters and each function's cyclomatic complexity.

clang-tidy and clang-query read BUILD's compile_commands.json, so they see
only the .cpp files a build compiles; a header is checked through the .cpp
files that include it. Exit 0 when every file keeps the rules, 1 when one
breaks a rule, and 2 when the input cannot be used.
"""

from __future__ import annotations

import argparse
import itertools
import json
import re
import subprocess
import sys
import tempfile
from collections.abc import Iterable, Sequence
from dataclasses import dataclass, replace
from pathlib import Path, PurePosixPath

EXIT_CLEAN: int = 0
EXIT_FINDINGS: int = 1
EXIT_UNUSABLE: int = 2

CLANG_TIDY: str = "clang-tidy-17"
CLANG_QUERY: str = "clang-query-17"
PROFILE: str = "cpp/policy/jsf-clang-tidy.yaml"

HEADER_SUFFIXES: frozenset[str] = frozenset({".h", ".hpp", ".hh", ".hxx", ".h++"})
SOURCE_SUFFIXES: frozenset[str] = frozenset({".cpp", ".cc", ".cxx", ".c++"})
MAX_LINE: int = 120
MAX_IDENTIFIER: int = 64
MAX_NAMESPACE_DEPTH: int = 2
MAX_PARAMETERS_ON_ONE_LINE: int = 2
MAX_COMPLEXITY: int = 20
# AV 9: the basic source character set, as code points: space through tilde,
# and the horizontal tab, vertical tab, form feed and newline controls.
FIRST_PRINTABLE: int = 0x20
LAST_PRINTABLE: int = 0x7E
ALLOWED_CONTROLS: frozenset[str] = frozenset({"\t", "\v", "\f", "\n"})
UNIVERSAL_TYPES: str = "universal_types.h"

# AV 26: the only pre-processor directives.
DIRECTIVES: frozenset[str] = frozenset({"ifndef", "define", "endif", "include"})
# AV 17 to 25, and AV 108's <cstdarg>: headers JSF code may not include.
BANNED_HEADERS: dict[str, str] = {
    "cerrno": "AV 17",
    "errno.h": "AV 17",
    "clocale": "AV 19",
    "locale.h": "AV 19",
    "csetjmp": "AV 20",
    "setjmp.h": "AV 20",
    "csignal": "AV 21",
    "signal.h": "AV 21",
    "cstdio": "AV 22",
    "stdio.h": "AV 22",
    "ctime": "AV 25",
    "time.h": "AV 25",
    "cstdarg": "AV 108",
    "stdarg.h": "AV 108",
}
# AV 17 to 25: library facilities JSF code may not use, wherever they are declared.
BANNED_NAMES: dict[str, str] = {"errno": "AV 17", "offsetof": "AV 18"}
BANNED_CALLS: dict[str, str] = {
    "setlocale": "AV 19",
    "setjmp": "AV 20",
    "longjmp": "AV 20",
    "signal": "AV 21",
    "raise": "AV 21",
    "printf": "AV 22",
    "fprintf": "AV 22",
    "sprintf": "AV 22",
    "snprintf": "AV 22",
    "puts": "AV 22",
    "fputs": "AV 22",
    "fopen": "AV 22",
    "scanf": "AV 22",
    "atof": "AV 23",
    "atoi": "AV 23",
    "atol": "AV 23",
    "abort": "AV 24",
    "exit": "AV 24",
    "getenv": "AV 24",
    "system": "AV 24",
    "time": "AV 25",
    "clock": "AV 25",
    "mktime": "AV 25",
    "localtime": "AV 25",
    "gmtime": "AV 25",
    "strftime": "AV 25",
}
# Keywords for features JSF forbids.
BANNED_KEYWORDS: dict[str, str] = {
    "register": "AV 140",
    "union": "AV 153",
    "goto": "AV 189",
    "continue": "AV 190",
    "volatile": "AV 205",
    "throw": "AV 208",
    "try": "AV 208",
    "catch": "AV 208",
}
# AV 209: the basic types, which only universal_types.h names.
BASIC_TYPES: frozenset[str] = frozenset({"int", "short", "long", "float", "double", "signed", "unsigned"})
# AV 12: digraphs.
DIGRAPHS: frozenset[str] = frozenset({"<:", ":>", "<%", "%>", "%:"})
# AV 13: encoding prefixes of wide and multi-byte literals.
WIDE_PREFIXES: frozenset[str] = frozenset({"L", "u", "U", "u8"})
# The access sections of a class, in the order AV 57 requires.
ACCESS_ORDER: dict[str, int] = {"public": 0, "protected": 1, "private": 2}
# What may follow a block's closing brace on its line (AV 61).
AFTER_CLOSING_BRACE: frozenset[str] = frozenset({";", ",", ")"})
# The tokens after which an opening brace starts a block rather than an
# initializer (AV 60, 61).
BLOCK_AFTER: frozenset[str] = frozenset(
    {")", "]", "else", "do", "try", "const", "noexcept", "override", "final", "mutable"}
)
TYPE_HEADS: frozenset[str] = frozenset({"namespace", "class", "struct", "enum", "union", "extern"})
LABEL_HEADS: frozenset[str] = frozenset({"case", "default"})
LOOP_KEYWORDS: frozenset[str] = frozenset({"for", "while"})

TOKEN: re.Pattern[str] = re.compile(
    r"(?P<identifier>[A-Za-z_]\w*)"
    r"|(?P<number>\.?\d(?:[\w']|[eEpP][+-]|\.)*)"
    r"|(?P<punct>::|->\*?|<<=?|>>=?|<=>|&&|\|\||[-+*/%&|^!=<>]=|\+\+|--|\.\.\.|<:|:>|<%|%>|%:|[^\s\w])"
)
TRIGRAPH: re.Pattern[str] = re.compile(r"\?\?[=/'()!<>\-]")
OCTAL: re.Pattern[str] = re.compile(r"0[0-7']+[uUlL]*")
HEX_LOWERCASE_DIGIT: re.Pattern[str] = re.compile(r"0[xX][0-9A-F']*[a-f]")
INCLUDE: re.Pattern[str] = re.compile(r'#\s*include\s*(?P<open>[<"])(?P<name>[^>"]*)[>"]')
DIRECTIVE: re.Pattern[str] = re.compile(r"\s*#\s*(?P<name>\w*)\s*(?P<argument>\S*)")
STANDARD_HEADER: re.Pattern[str] = re.compile(r"[a-z_]+")
ICS_MODULE_HEADER: re.Pattern[str] = re.compile(r"ics/[a-z_]+/[a-z0-9_/]+\.hpp")
MAIN_SIGNATURE: re.Pattern[str] = re.compile(r"\bint\s+main\s*\(\s*(?:const\s+)?int\b")
CODE_LIKE_COMMENT: re.Pattern[str] = re.compile(r"[;{}]\s*$")
QUERY_NOTE: re.Pattern[str] = re.compile(
    r'^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): note: "(?P<binding>[^"]+)" binds here'
)
MACRO_NOTE: re.Pattern[str] = re.compile(r"^.+?:\d+:\d+: note: expanded from macro ")
TIDY_ERROR: re.Pattern[str] = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):\d+: error: (?P<message>.*?) \[(?P<check>[^\],]+)"
)


class JsfError(Exception):
    """An input that cannot be used."""


@dataclass(frozen=True)
class Finding:
    """One broken rule, at a line of a file."""

    path: str
    line: int
    rule: str
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.rule}: {self.message}"


@dataclass(frozen=True)
class Comment:
    """A comment: the line it starts on, its text, and whether it is a /* */ comment."""

    line: int
    text: str
    block: bool


@dataclass(frozen=True)
class Literal:
    """A string or character literal: its line and its encoding prefix."""

    line: int
    prefix: str


@dataclass(frozen=True)
class Token:
    """One token of the code: its kind, text, line and column."""

    kind: str
    text: str
    line: int
    column: int


@dataclass(frozen=True)
class Source:
    """A file, its code with comments and literal contents blanked, and its comments and literals."""

    path: str
    text: str
    code: str
    comments: tuple[Comment, ...]
    literals: tuple[Literal, ...]

    @property
    def is_header(self) -> bool:
        return PurePosixPath(self.path).suffix in HEADER_SUFFIXES


@dataclass(frozen=True)
class Piece:
    """What the lexer read at one position: where it ends, the text the code view gets, and what it found."""

    end: int
    code: str
    comment: Comment | None = None
    literal: Literal | None = None


IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_]\w*")
NUMBER: re.Pattern[str] = re.compile(r"\.?\d(?:[\w']|[eEpP][+-]|\.)*")
LITERAL_PREFIXES: frozenset[str] = frozenset({"u8", "u", "U", "L", "R", "u8R", "uR", "UR", "LR"})


def blank(text: str) -> str:
    """text with every character but newlines turned to a space."""
    return "".join("\n" if character == "\n" else " " for character in text)


def line_at(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def scan_comment(text: str, index: int) -> Piece:
    """A // or /* */ comment starting at index."""
    line = line_at(text, index)
    if text.startswith("//", index):
        newline = text.find("\n", index)
        end = len(text) if newline < 0 else newline
        return Piece(end, blank(text[index:end]), Comment(line, text[index + 2 : end], block=False))
    close = text.find("*/", index + 2)
    end = len(text) if close < 0 else close + 2
    return Piece(end, blank(text[index:end]), Comment(line, text[index + 2 : max(end - 2, index + 2)], block=True))


def quoted_end(text: str, index: int) -> int:
    """The index just past the quoted literal whose opening quote is at index; it stops at a newline."""
    quote = text[index]
    position = index + 1
    while position < len(text) and text[position] not in (quote, "\n"):
        position += 2 if text[position] == "\\" else 1
    return min(position + 1, len(text))


def scan_literal(text: str, index: int, prefix: str) -> Piece:
    """A string or character literal whose opening quote is at index, after its encoding prefix."""
    line = line_at(text, index)
    if prefix.endswith("R") and text[index] == '"':
        opening = text.find("(", index)
        delimiter = text[index + 1 : opening] if opening >= 0 else ""
        closing = text.find(")" + delimiter + '"', max(opening, index))
        end = len(text) if opening < 0 or closing < 0 else closing + len(delimiter) + 2
    else:
        end = quoted_end(text, index)
    literal_prefix = prefix[:-1] if prefix.endswith("R") else prefix
    return Piece(end, blank(text[index:end]), literal=Literal(line, literal_prefix))


def scan_word(text: str, index: int) -> Piece:
    """An identifier, or the encoding prefix of the literal that follows it."""
    match = IDENTIFIER.match(text, index)
    if match is None:
        return Piece(index + 1, text[index])
    word = match.group(0)
    after = match.end()
    if word in LITERAL_PREFIXES and after < len(text) and text[after] in "\"'":
        piece = scan_literal(text, after, word)
        return Piece(piece.end, blank(word) + piece.code, literal=piece.literal)
    return Piece(after, word)


def scan(text: str, index: int) -> Piece:
    """Whatever starts at index: a comment, a literal, an identifier, a number or one character."""
    character = text[index]
    if text.startswith(("//", "/*"), index):
        return scan_comment(text, index)
    if character in "\"'":
        return scan_literal(text, index, "")
    if character.isalpha() or character == "_":
        return scan_word(text, index)
    number = NUMBER.match(text, index)
    if number is not None:
        return Piece(number.end(), number.group(0))
    return Piece(index + 1, character)


def lex(path: str, text: str) -> Source:
    """The file's code with comments and literal contents blanked, its comments and its literals."""
    code: list[str] = []
    comments: list[Comment] = []
    literals: list[Literal] = []
    index = 0
    while index < len(text):
        piece = scan(text, index)
        code.append(piece.code)
        if piece.comment is not None:
            comments.append(piece.comment)
        if piece.literal is not None:
            literals.append(piece.literal)
        index = piece.end
    return Source(path, text, "".join(code), tuple(comments), tuple(literals))


def tokenize(code: str) -> list[Token]:
    """The code's tokens, each with its line and column."""
    tokens: list[Token] = []
    for number, line in enumerate(code.split("\n"), start=1):
        for match in TOKEN.finditer(line):
            kind = match.lastgroup or "punct"
            tokens.append(Token(kind, match.group(0), number, match.start() + 1))
    return tokens


def check_extension(source: Source) -> list[Finding]:
    """AV 53 and 54: headers end in .h, implementation files in .cpp."""
    suffix = PurePosixPath(source.path).suffix
    if suffix in HEADER_SUFFIXES and suffix != ".h":
        return [Finding(source.path, 1, "AV 53", f"a header's name ends in .h, not {suffix}")]
    if suffix in SOURCE_SUFFIXES and suffix != ".cpp":
        return [Finding(source.path, 1, "AV 54", f"an implementation file's name ends in .cpp, not {suffix}")]
    return []


def outside_source_set(line: str) -> bool:
    """Whether a line holds a character outside the basic source character set (AV 9)."""
    return any(
        not FIRST_PRINTABLE <= ord(character) <= LAST_PRINTABLE and character not in ALLOWED_CONTROLS
        for character in line
    )


def check_characters(source: Source) -> list[Finding]:
    """AV 9, 41 and 43: the basic source character set, lines of 120 characters or less, no tabs."""
    findings: list[Finding] = []
    for number, line in enumerate(source.text.split("\n"), start=1):
        if outside_source_set(line):
            findings.append(Finding(source.path, number, "AV 9", "a character outside the basic source character set"))
        if "\t" in line:
            findings.append(Finding(source.path, number, "AV 43", "a tab"))
        if len(line) > MAX_LINE:
            findings.append(Finding(source.path, number, "AV 41", f"a line of {len(line)} characters"))
    return findings


def check_sequences(source: Source, tokens: Sequence[Token]) -> list[Finding]:
    """AV 11, 12 and 13: no trigraphs, digraphs, or wide or multi-byte literals."""
    findings = [
        Finding(source.path, number, "AV 11", "a trigraph")
        for number, line in enumerate(source.text.split("\n"), start=1)
        if TRIGRAPH.search(line)
    ]
    findings.extend(
        Finding(source.path, token.line, "AV 12", f"the digraph {token.text}")
        for token in tokens
        if token.text in DIGRAPHS
    )
    findings.extend(
        Finding(source.path, literal.line, "AV 13", f"a literal with the {literal.prefix} prefix")
        for literal in source.literals
        if literal.prefix in WIDE_PREFIXES
    )
    return findings


def check_comments(source: Source) -> list[Finding]:
    """AV 126 and 127: // comments only, and no code commented out."""
    findings: list[Finding] = []
    for comment in source.comments:
        if comment.block:
            findings.append(Finding(source.path, comment.line, "AV 126", "a /* */ comment; use //"))
        elif CODE_LIKE_COMMENT.search(comment.text):
            findings.append(Finding(source.path, comment.line, "AV 127", "a comment that reads as code; delete it"))
    return findings


def check_intro(source: Source) -> list[Finding]:
    """AV 133: the file opens with a comment that names it and says what it holds."""
    name = PurePosixPath(source.path).name
    opening = source.text.split("\n")[:2]
    if any(line.startswith(f"// {name}: ") for line in opening):
        return []
    return [Finding(source.path, 1, "AV 133", f"the file must open with '// {name}: ' and what it holds")]


def include_findings(path: str, number: int, line: str) -> list[Finding]:
    """AV 32, 33, 53.1 and 17 to 25 for one #include line."""
    match = INCLUDE.match(line.strip())
    if match is None:
        return [Finding(path, number, "AV 33", "an #include the gate cannot read")]
    name = match.group("name")
    findings: list[Finding] = []
    if match.group("open") == '"':
        findings.append(Finding(path, number, "AV 33", f'#include "{name}"; use <{name}>'))
    if any(sequence in name for sequence in ("'", "\\", "/*", "//")):
        findings.append(Finding(path, number, "AV 53.1", f"the header name {name} holds a forbidden sequence"))
    if name in BANNED_HEADERS:
        findings.append(Finding(path, number, BANNED_HEADERS[name], f"<{name}> may not be included"))
    elif not (name.endswith(".h") or STANDARD_HEADER.fullmatch(name) or ICS_MODULE_HEADER.fullmatch(name)):
        findings.append(Finding(path, number, "AV 32", f"<{name}> is not a .h header"))
    return findings


@dataclass(frozen=True)
class Directive:
    """A pre-processor directive: its line, name and first argument."""

    line: int
    name: str
    argument: str


def directives(source: Source) -> list[Directive]:
    """The file's pre-processor directives, found in the code view so commented ones are left out."""
    found: list[Directive] = []
    for number, line in enumerate(source.code.split("\n"), start=1):
        match = DIRECTIVE.match(line)
        if match is not None and line.lstrip().startswith("#"):
            found.append(Directive(number, match.group("name"), match.group("argument")))
    return found


def guard_findings(source: Source, found: Sequence[Directive]) -> list[Finding]:
    """AV 27, 28, 31 and 35: a header's include guard, and no other #ifndef, #define or #endif."""
    guards = [directive for directive in found if directive.name in ("ifndef", "define", "endif")]
    if not source.is_header:
        return [
            Finding(source.path, directive.line, "AV 31", f"#{directive.name} outside a header's guard")
            for directive in guards
        ]
    names = [directive.name for directive in guards]
    first = found[0] if found else None
    last = found[-1] if found else None
    well_formed = (
        names == ["ifndef", "define", "endif"]
        and guards[0].argument == guards[1].argument
        and first is guards[0]
        and last is guards[2]
    )
    if well_formed:
        return []
    return [
        Finding(
            source.path,
            1,
            "AV 35",
            "a header holds exactly #ifndef G, #define G ... #endif, and nothing else of the kind",
        )
    ]


def check_preprocessor(source: Source) -> list[Finding]:
    """AV 26 to 35: the directives allowed, includes in <file.h> form, and include guards."""
    found = directives(source)
    raw_lines = source.text.split("\n")
    findings: list[Finding] = []
    for directive in found:
        if directive.name not in DIRECTIVES:
            findings.append(Finding(source.path, directive.line, "AV 26", f"the directive #{directive.name}"))
        elif directive.name == "include":
            findings.extend(include_findings(source.path, directive.line, raw_lines[directive.line - 1]))
    findings.extend(guard_findings(source, found))
    return findings


def text_at(tokens: Sequence[Token], index: int) -> str:
    """The text of the token at index, or nothing past either end."""
    return tokens[index].text if 0 <= index < len(tokens) else ""


def in_main_signature(tokens: Sequence[Token], index: int) -> bool:
    """Whether the int at index is main's return type or argc's type: the C++ standard fixes both."""
    if text_at(tokens, index + 1) == "main":
        return True
    before = [text_at(tokens, index - offset) for offset in (1, 2, 3)]
    return before[:2] == ["(", "main"] or before == ["const", "(", "main"]


def is_banned_call(tokens: Sequence[Token], index: int, path: str) -> bool:
    """Whether the identifier at index calls a banned library function, as name( or std::name(."""
    name = tokens[index].text
    if name not in BANNED_CALLS or text_at(tokens, index + 1) != "(":
        return False
    before = text_at(tokens, index - 1)
    qualified_outside_std = before == "::" and text_at(tokens, index - 2) not in ("std", "")
    member = before in (".", "->")
    # Deviation D-5: a fuzz target reports a finding to libFuzzer with std::abort.
    fuzz_abort = name == "abort" and "fuzz" in PurePosixPath(path).parts
    return not (qualified_outside_std or member or fuzz_abort)


def identifier_findings(source: Source, tokens: Sequence[Token], index: int) -> list[Finding]:
    """AV 17 to 25, 46, 47, 140, 153, 189, 190, 205, 208 and 209 for the identifier at index."""
    token = tokens[index]
    word = token.text
    findings: list[Finding] = []
    if word in BANNED_NAMES:
        findings.append(Finding(source.path, token.line, BANNED_NAMES[word], f"{word} may not be used"))
    if is_banned_call(tokens, index, source.path):
        findings.append(Finding(source.path, token.line, BANNED_CALLS[word], f"{word}() may not be called"))
    if word in BANNED_KEYWORDS:
        findings.append(Finding(source.path, token.line, BANNED_KEYWORDS[word], f"the keyword {word}"))
    basic_allowed = PurePosixPath(source.path).name == UNIVERSAL_TYPES or (
        word == "int" and in_main_signature(tokens, index)
    )
    if word in BASIC_TYPES and not basic_allowed:
        findings.append(Finding(source.path, token.line, "AV 209", f"the basic type {word}; use universal_types.h"))
    if len(word) > MAX_IDENTIFIER:
        findings.append(Finding(source.path, token.line, "AV 46", f"an identifier of {len(word)} characters"))
    if word.startswith("_"):
        findings.append(Finding(source.path, token.line, "AV 47", f"the identifier {word} begins with _"))
    return findings


def literal_findings(source: Source, token: Token) -> list[Finding]:
    """AV 149 and 150: no octal constants but zero, and hexadecimal digits in uppercase."""
    digits = token.text.replace("'", "").rstrip("uUlL")
    if OCTAL.fullmatch(token.text) and digits.strip("0"):
        return [Finding(source.path, token.line, "AV 149", f"the octal constant {token.text}")]
    if HEX_LOWERCASE_DIGIT.match(token.text):
        return [
            Finding(source.path, token.line, "AV 150", f"the hexadecimal constant {token.text} has lowercase digits")
        ]
    return []


def check_tokens(source: Source, tokens: Sequence[Token]) -> list[Finding]:
    """The rules about single tokens: banned names, calls and keywords, basic types, identifiers and literals."""
    findings: list[Finding] = []
    for index, token in enumerate(tokens):
        if token.kind == "identifier":
            findings.extend(identifier_findings(source, tokens, index))
        elif token.kind == "number":
            findings.extend(literal_findings(source, token))
    return findings


def check_statements(source: Source, tokens: Sequence[Token]) -> list[Finding]:
    """AV 42: one statement a line; the semicolons inside a for loop's parentheses are not counted."""
    depth = 0
    per_line: dict[int, int] = {}
    for token in tokens:
        if token.text == "(":
            depth += 1
        elif token.text == ")":
            depth = max(depth - 1, 0)
        elif token.text == ";" and depth == 0:
            per_line[token.line] = per_line.get(token.line, 0) + 1
    return [
        Finding(source.path, line, "AV 42", f"{count} statements on one line")
        for line, count in sorted(per_line.items())
        if count > 1
    ]


@dataclass
class Frame:
    """An open brace: what it opens, and what the walk has seen inside it."""

    kind: str
    names: int = 0
    access: int = -1


@dataclass
class Walk:
    """The state of a walk over a file's tokens, matching braces."""

    source: Source
    tokens: Sequence[Token]
    lines: dict[int, list[int]]
    stack: list[Frame]
    findings: list[Finding]
    statement_start: int = 0
    pending: str = ""
    awaiting: str = ""
    paren_depth: int = 0
    condition_depth: int = -1


def classify(walk: Walk, index: int) -> str:
    """What the brace at index opens: an initializer, or a block of some kind."""
    previous = text_at(walk.tokens, index - 1)
    head_tokens = walk.tokens[walk.statement_start : index]
    heads = {token.text for token in head_tokens}
    head = head_tokens[0].text if head_tokens else ""
    enclosing = walk.stack[-1].kind if walk.stack else "block"
    kind = "init"
    if walk.pending:
        kind = walk.pending
    elif "enum" in heads:
        kind = "block"
    elif "namespace" in heads:
        kind = "namespace"
    elif heads & {"class", "struct", "union"}:
        kind = "class"
    elif "extern" in heads or previous in BLOCK_AFTER or (previous == ":" and head in LABEL_HEADS):
        kind = "block"
    elif previous in ("{", ";", "}"):
        kind = "init" if enclosing == "init" else "block"
    return kind


def alone_on_line(walk: Walk, index: int, allowed_after: frozenset[str]) -> bool:
    """Whether the brace at index has nothing before it on its line, and only allowed tokens after it."""
    on_line = walk.lines[walk.tokens[index].line]
    position = on_line.index(index)
    after = [walk.tokens[other].text for other in on_line[position + 1 :]]
    return position == 0 and all(text in allowed_after for text in after)


def open_brace(walk: Walk, index: int) -> None:
    """AV 60, 61 and 99 at an opening brace."""
    kind = classify(walk, index)
    walk.pending = ""
    line = walk.tokens[index].line
    if kind != "init" and not alone_on_line(walk, index, frozenset()):
        walk.findings.append(
            Finding(walk.source.path, line, "AV 61", "a block's opening brace stands alone on its line")
        )
    names = 0
    if kind == "namespace":
        names = 1 + sum(1 for token in walk.tokens[walk.statement_start : index] if token.text == "::")
    walk.stack.append(Frame(kind, names))
    depth = sum(frame.names for frame in walk.stack)
    if names and depth > MAX_NAMESPACE_DEPTH:
        walk.findings.append(Finding(walk.source.path, line, "AV 99", f"namespaces nested {depth} deep"))


def close_brace(walk: Walk, index: int) -> None:
    """AV 60 and 61 at a closing brace."""
    frame = walk.stack.pop() if walk.stack else Frame("block")
    if frame.kind != "init" and not alone_on_line(walk, index, AFTER_CLOSING_BRACE):
        line = walk.tokens[index].line
        walk.findings.append(
            Finding(walk.source.path, line, "AV 61", "a block's closing brace stands alone on its line")
        )


def check_break(walk: Walk, index: int) -> None:
    """AV 191: break only ends a switch's case, never a loop."""
    innermost = next((frame.kind for frame in reversed(walk.stack) if frame.kind in ("switch", "loop")), "")
    if innermost == "loop":
        line = walk.tokens[index].line
        walk.findings.append(Finding(walk.source.path, line, "AV 191", "break out of a loop"))


def check_access(walk: Walk, index: int) -> None:
    """AV 57: a class's public, protected and private sections come in that order."""
    if text_at(walk.tokens, index + 1) != ":" or not walk.stack or walk.stack[-1].kind != "class":
        return
    frame = walk.stack[-1]
    order = ACCESS_ORDER[walk.tokens[index].text]
    if order < frame.access:
        line = walk.tokens[index].line
        walk.findings.append(
            Finding(walk.source.path, line, "AV 57", "public, protected and private sections out of order")
        )
    frame.access = max(frame.access, order)


def track_conditions(walk: Walk, text: str) -> None:
    """Note which brace a switch, for, while or do opens, from the parentheses of its condition."""
    if text == "switch":
        walk.awaiting = "switch"
    elif text in LOOP_KEYWORDS:
        walk.awaiting = "loop"
    elif text == "do":
        walk.pending = "loop"
    elif text == "(":
        walk.paren_depth += 1
        if walk.awaiting and walk.condition_depth < 0:
            walk.condition_depth = walk.paren_depth
    elif text == ")":
        if walk.condition_depth == walk.paren_depth:
            walk.pending = walk.awaiting
            walk.awaiting = ""
            walk.condition_depth = -1
        walk.paren_depth = max(walk.paren_depth - 1, 0)


def step(walk: Walk, index: int) -> None:
    """One token of the walk."""
    text = walk.tokens[index].text
    if walk.pending and text != "{":
        walk.pending = ""
    track_conditions(walk, text)
    if text == "{":
        open_brace(walk, index)
    elif text == "}":
        close_brace(walk, index)
    elif text == "break":
        check_break(walk, index)
    elif text in ACCESS_ORDER:
        check_access(walk, index)
    if text in (";", "{", "}"):
        walk.statement_start = index + 1


def check_structure(source: Source, tokens: Sequence[Token]) -> list[Finding]:
    """AV 57, 60, 61, 99 and 191, from one walk that matches the file's braces."""
    lines: dict[int, list[int]] = {}
    for index, token in enumerate(tokens):
        lines.setdefault(token.line, []).append(index)
    walk = Walk(source, tokens, lines, [], [])
    for index in range(len(tokens)):
        step(walk, index)
    return walk.findings


def check_text(source: Source) -> list[Finding]:
    """Every text check on one file."""
    tokens = tokenize(source.code)
    findings = check_extension(source)
    findings.extend(check_characters(source))
    findings.extend(check_sequences(source, tokens))
    findings.extend(check_comments(source))
    findings.extend(check_intro(source))
    findings.extend(check_preprocessor(source))
    findings.extend(check_tokens(source, tokens))
    findings.extend(check_statements(source, tokens))
    findings.extend(check_structure(source, tokens))
    return findings


# The AV rule each clang-tidy check in the JSF profile enforces.
TIDY_RULES: dict[str, str] = {
    "bugprone-assignment-in-if-condition": "AV 160",
    "bugprone-signed-char-misuse": "AV 180",
    "bugprone-sizeof-expression": "AV 166",
    "bugprone-switch-missing-default-case": "AV 194",
    "bugprone-unhandled-self-assignment": "AV 81",
    "bugprone-unused-return-value": "AV 115",
    "cert-err52-cpp": "AV 20",
    "cert-flp30-c": "AV 197",
    "clang-analyzer-core.DivideZero": "AV 203",
    "clang-analyzer-core.NullDereference": "AV 174",
    "clang-analyzer-deadcode.DeadStores": "AV 187",
    "clang-analyzer-optin.cplusplus.VirtualCall": "AV 71.1",
    "cppcoreguidelines-avoid-c-arrays": "AV 97",
    "cppcoreguidelines-avoid-goto": "AV 189",
    "cppcoreguidelines-avoid-non-const-global-variables": "AV 207",
    "cppcoreguidelines-init-variables": "AV 142",
    "cppcoreguidelines-macro-usage": "AV 29",
    "cppcoreguidelines-narrowing-conversions": "AV 180",
    "cppcoreguidelines-no-malloc": "AV 206",
    "cppcoreguidelines-owning-memory": "AV 206",
    "cppcoreguidelines-prefer-member-initializer": "AV 74",
    "cppcoreguidelines-pro-bounds-array-to-pointer-decay": "AV 97",
    "cppcoreguidelines-pro-bounds-pointer-arithmetic": "AV 215",
    "cppcoreguidelines-pro-type-const-cast": "AV 183",
    "cppcoreguidelines-pro-type-cstyle-cast": "AV 185",
    "cppcoreguidelines-pro-type-reinterpret-cast": "AV 182",
    "cppcoreguidelines-pro-type-union-access": "AV 153",
    "cppcoreguidelines-pro-type-vararg": "AV 108",
    "cppcoreguidelines-special-member-functions": "AV 68",
    "cppcoreguidelines-virtual-class-destructor": "AV 78",
    "google-default-arguments": "AV 95",
    "google-explicit-constructor": "AV 177",
    "hicpp-multiway-paths-covered": "AV 192",
    "hicpp-signed-bitwise": "AV 164",
    "misc-confusable-identifiers": "AV 48",
    "misc-definitions-in-headers": "AV 39",
    "misc-include-cleaner": "AV 40",
    "misc-no-recursion": "AV 119",
    "misc-unconventional-assign-operator": "AV 82",
    "misc-use-anonymous-namespace": "AV 137",
    "modernize-use-nullptr": "AV 175",
    "readability-braces-around-statements": "AV 59",
    "readability-function-size": "AV 1",
    "readability-identifier-naming": "AV 51",
    "readability-isolate-declaration": "AV 152",
    "readability-magic-numbers": "AV 151",
    "readability-make-member-function-const": "AV 69",
    "readability-misleading-indentation": "AV 59",
    "readability-uppercase-literal-suffix": "AV 14",
}

# AV 50's form for the names of types: a first word with a capital, then
# lowercase words, each joined by _. A word may be an acronym in capitals (AV 49).
TYPE_NAME: str = "::([A-Z][a-z0-9]*|[A-Z][A-Z0-9]+)(_([a-z0-9]+|[A-Z][A-Z0-9]+))*$"
# AV 50's exception: the universal types are named as the fundamental types they replace.
UNIVERSAL_NAME: str = "::ics::(u?int(8|16|32|64)|float(32|64))$"
NOT_FROM_TEST_MACROS: str = 'unless(isExpandedFromMacro("TEST")), unless(isExpandedFromMacro("TEST_F"))'

IN_JSF: str = 'isExpansionInFileMatching("{r}")'
INCREMENTS: str = 'hasAnyOperatorName("++", "--")'
ARITHMETIC: str = 'hasAnyOperatorName("+", "-", "*", "/", "%", "+=", "-=", "*=", "/=", "%=")'
LOGICAL: str = 'hasAnyOperatorName("&&", "||")'
COUNTER_CHANGED: str = (
    "anyOf("
    'binaryOperator(isAssignmentOperator(), hasLHS(declRefExpr(to(varDecl(equalsBoundNode("counter")))))), '
    f'unaryOperator({INCREMENTS}, hasUnaryOperand(declRefExpr(to(varDecl(equalsBoundNode("counter")))))))'
)
IN_FOR_BODY: str = (
    'hasAncestor(compoundStmt(hasParent(forStmt(hasLoopInit(declStmt(hasSingleDecl(varDecl().bind("counter"))))))))'
)
TYPE_OK: str = f'unless(matchesName("{TYPE_NAME}"))'
TYPE_MESSAGE: str = "a type name is a capitalized first word and lowercase words joined by _"

# Each clang-query matcher: the binding it reports under, the AV rule, what is
# wrong, and the matcher, with {r} for the JSF files' path pattern.
QUERIES: tuple[tuple[str, str, str, str], ...] = (
    ("av163", "AV 163", "unsigned arithmetic", f"binaryOperator({IN_JSF}, {ARITHMETIC}, hasType(isUnsignedInteger()))"),
    (
        "av163",
        "AV 163",
        "unsigned arithmetic",
        f"unaryOperator({IN_JSF}, {INCREMENTS}, hasUnaryOperand(hasType(isUnsignedInteger())))",
    ),
    (
        "av165",
        "AV 165",
        "unary minus on an unsigned value",
        f'unaryOperator({IN_JSF}, hasOperatorName("-"), hasUnaryOperand(hasType(isUnsignedInteger())))',
    ),
    (
        "av158",
        "AV 158",
        "an operand of && or || that holds a binary operator is not parenthesized",
        f"binaryOperator({IN_JSF}, {LOGICAL}, hasEitherOperand(ignoringImpCasts(binaryOperator(unless({LOGICAL})))))",
    ),
    (
        "av157",
        "AV 157",
        "a side effect in the right-hand operand of && or ||",
        f"binaryOperator({IN_JSF}, {LOGICAL}, hasRHS(anyOf(unaryOperator({INCREMENTS}), "
        f"hasDescendant(unaryOperator({INCREMENTS})), binaryOperator(isAssignmentOperator()), "
        "hasDescendant(binaryOperator(isAssignmentOperator())))))",
    ),
    (
        "av160",
        "AV 160",
        "an assignment used inside an expression",
        f"binaryOperator({IN_JSF}, isAssignmentOperator(), unless(hasParent(compoundStmt())), "
        "unless(hasParent(forStmt())), unless(hasParent(caseStmt())), unless(hasParent(defaultStmt())))",
    ),
    (
        "av201",
        "AV 201",
        "a for loop's counter changed in its body",
        f"expr({IN_JSF}, {IN_FOR_BODY}, {COUNTER_CHANGED})",
    ),
    (
        "av200",
        "AV 200",
        "a for loop without an initialization or an increment; use a while loop",
        f"forStmt({IN_JSF}, anyOf(unless(hasLoopInit(anything())), unless(hasIncrement(anything()))))",
    ),
    ("av107", "AV 107", "a function declared at block scope", f"functionDecl({IN_JSF}, hasParent(declStmt()))"),
    ("av154", "AV 154", "a bit-field", f"fieldDecl({IN_JSF}, isBitField())"),
    (
        "av88",
        "AV 88.1",
        "a virtual base class",
        f"cxxRecordDecl({IN_JSF}, hasAnyBase(cxxBaseSpecifier(isVirtual())))",
    ),
    ("av178", "AV 178", "dynamic_cast", f"cxxDynamicCastExpr({IN_JSF})"),
    ("av188", "AV 188", "a label outside a switch", f"labelStmt({IN_JSF})"),
    ("av108", "AV 108", "a function with a variable number of arguments", f"functionDecl({IN_JSF}, isVariadic())"),
    (
        "av159",
        "AV 159",
        "an overloaded &&, || or &",
        f'functionDecl({IN_JSF}, hasAnyOverloadedOperatorName("&&", "||", "&"))',
    ),
    (
        "av170",
        "AV 170",
        "more than two levels of pointer indirection",
        f"declaratorDecl({IN_JSF}, hasType(pointerType(pointee(pointerType(pointee(pointerType()))))))",
    ),
    (
        "av171",
        "AV 171",
        "a relational operator on pointers",
        f'binaryOperator({IN_JSF}, hasAnyOperatorName("<", ">", "<=", ">="), '
        "hasEitherOperand(ignoringImpCasts(hasType(pointerType()))))",
    ),
    (
        "av50",
        "AV 50",
        TYPE_MESSAGE,
        f"cxxRecordDecl({IN_JSF}, isDefinition(), unless(isImplicit()), unless(isLambda()), "
        f"{NOT_FROM_TEST_MACROS}, {TYPE_OK})",
    ),
    ("av50", "AV 50", TYPE_MESSAGE, f"enumDecl({IN_JSF}, {TYPE_OK})"),
    (
        "av50",
        "AV 50",
        TYPE_MESSAGE,
        f'typedefNameDecl({IN_JSF}, {TYPE_OK}, unless(matchesName("{UNIVERSAL_NAME}")))',
    ),
)
# AV 58's matcher: each function with each of its parameters.
PARAMETER_QUERY: str = (
    f"functionDecl({IN_JSF}, unless(isImplicit()), {NOT_FROM_TEST_MACROS}, "
    'forEachDescendant(parmVarDecl().bind("av58p"))).bind("av58f")'
)
# AV 3's matcher: each decision, with the function it belongs to. A function's
# cyclomatic complexity is one more than its decisions.
DECISION_QUERY: str = (
    f"stmt({IN_JSF}, anyOf(ifStmt(), forStmt(), cxxForRangeStmt(), whileStmt(), doStmt(), caseStmt(), "
    "conditionalOperator(), binaryConditionalOperator(), cxxCatchStmt(), "
    'binaryOperator(hasAnyOperatorName("&&", "||"))), '
    'forFunction(functionDecl().bind("av3f"))).bind("av3s")'
)
REPORTED_BINDINGS: dict[str, tuple[str, str]] = {binding: (rule, message) for binding, rule, message, _ in QUERIES}


def path_pattern(paths: Iterable[str]) -> str:
    """A regular expression that matches the absolute paths of the given files and folders."""
    parts: list[str] = []
    for path in sorted(paths):
        escaped = re.sub(r"([.+()|^$?*\[\]{}])", r"[\1]", path)
        parts.append(f"/{escaped}$" if PurePosixPath(path).suffix else f"/{escaped}/")
    return "|".join(parts) if parts else "^$"


def query_script(pattern: str) -> str:
    """The clang-query commands for every matcher."""
    lines = ["set output diag", "set bind-root false"]
    lines.extend(f'match {matcher.replace("{r}", pattern)}.bind("{binding}")' for binding, _, _, matcher in QUERIES)
    lines.append(f"match {PARAMETER_QUERY.replace('{r}', pattern)}")
    lines.append(f"match {DECISION_QUERY.replace('{r}', pattern)}")
    return "\n".join(lines) + "\n"


@dataclass(frozen=True)
class Location:
    """A place in a file, as a tool reports it."""

    path: str
    line: int
    column: int


def relative(root: Path, path: str) -> str:
    """path from the repository root, if it is under it."""
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError:
        return path


@dataclass(frozen=True)
class Bound:
    """A node clang-query bound: its binding, where it is, and whether a macro expanded it."""

    binding: str
    where: Location
    from_macro: bool


def bound_nodes(output: str, root: Path) -> list[Bound]:
    """Each node in clang-query's output, in order; a macro note follows the node it is about."""
    nodes: list[Bound] = []
    for line in output.splitlines():
        match = QUERY_NOTE.match(line)
        if match is not None:
            where = Location(relative(root, match.group("path")), int(match.group("line")), int(match.group("column")))
            nodes.append(Bound(match.group("binding"), where, from_macro=False))
        elif nodes and MACRO_NOTE.match(line):
            nodes[-1] = replace(nodes[-1], from_macro=True)
    return nodes


def parse_query(output: str, root: Path) -> list[Finding]:
    """The findings in clang-query's output: AV 58's from the parameters of each
    function, and AV 3's from the decisions in each.

    A decision a macro expands to, such as those inside GoogleTest's EXPECT_EQ,
    is the framework's (deviation D-5), so it does not count.
    """
    findings: list[Finding] = []
    function = Location("", 0, 0)
    parameters: dict[Location, set[Location]] = {}
    decisions: dict[Location, set[Location]] = {}
    for node in bound_nodes(output, root):
        if node.binding in REPORTED_BINDINGS:
            rule, message = REPORTED_BINDINGS[node.binding]
            findings.append(Finding(node.where.path, node.where.line, rule, message))
        elif node.binding in ("av58f", "av3f"):
            function = node.where
        elif node.binding == "av58p":
            parameters.setdefault(node.where, set()).add(function)
        elif node.binding == "av3s" and not node.from_macro:
            decisions.setdefault(function, set()).add(node.where)
    findings.extend(parameter_findings(parameters))
    findings.extend(complexity_findings(decisions))
    return findings


def complexity_findings(decisions: dict[Location, set[Location]]) -> list[Finding]:
    """AV 3: each function's cyclomatic complexity, one more than its decisions, is 20 or less.

    Each decision is counted once, however many template instantiations or
    translation units it appears in.
    """
    findings: list[Finding] = []
    for function, inside in decisions.items():
        complexity = len(inside) + 1
        if complexity > MAX_COMPLEXITY:
            findings.append(
                Finding(
                    function.path,
                    function.line,
                    "AV 3",
                    f"a cyclomatic complexity of {complexity}, over {MAX_COMPLEXITY}",
                )
            )
    return findings


def parameter_findings(parameters: dict[Location, set[Location]]) -> list[Finding]:
    """AV 58: past two parameters, each after the first is on a line of its own.

    A lambda's parameters are also descendants of the function around it, so
    each parameter belongs to the function that holds the fewest parameters.
    """
    counts: dict[Location, int] = {}
    for functions in parameters.values():
        for function in functions:
            counts[function] = counts.get(function, 0) + 1
    owned: dict[Location, list[Location]] = {}
    for parameter, functions in parameters.items():
        owner = min(functions, key=lambda function: (counts[function], function.line, function.column))
        owned.setdefault(owner, []).append(parameter)
    findings: list[Finding] = []
    for function, own in owned.items():
        lines = [parameter.line for parameter in sorted(own, key=lambda found: (found.line, found.column))]
        shared = any(earlier == later for earlier, later in itertools.pairwise(lines))
        if len(lines) > MAX_PARAMETERS_ON_ONE_LINE and shared:
            findings.append(
                Finding(
                    function.path,
                    function.line,
                    "AV 58",
                    "past two parameters, each after the first takes a line of its own",
                )
            )
    return findings


def parse_tidy(output: str, root: Path) -> list[Finding]:
    """The findings in clang-tidy's output, each under the AV rule of its check."""
    findings: list[Finding] = []
    for line in output.splitlines():
        match = TIDY_ERROR.match(line)
        if match is not None:
            check = match.group("check")
            rule = TIDY_RULES.get(check, "AV ?")
            if check == "readability-identifier-naming" and re.search(r"constant|constexpr", match.group("message")):
                rule = "AV 52"
            findings.append(
                Finding(
                    relative(root, match.group("path")),
                    int(match.group("line")),
                    rule,
                    f"{match.group('message')} [{check}]",
                )
            )
    return findings


def run_tool(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError as error:
        raise JsfError(f"cannot run {command[0]}: {error}") from error


def run_tidy(root: Path, build: Path, sources: Sequence[Path], pattern: str) -> list[Finding]:
    """clang-tidy with the JSF profile on each source, one at a time, as the CI cache expects."""
    profile = read_text(root / PROFILE)
    findings: list[Finding] = []
    for source in sources:
        command = [
            CLANG_TIDY,
            "--quiet",
            f"-p={build}",
            f"--config={profile}",
            f"--header-filter={pattern}",
            str(source),
        ]
        result = run_tool(command)
        found = parse_tidy(result.stdout, root)
        if result.returncode != 0 and not found:
            raise JsfError(f"clang-tidy failed on {source}: {result.stdout[-2000:]}{result.stderr[-2000:]}")
        findings.extend(found)
    return findings


def run_query(root: Path, build: Path, sources: Sequence[Path], pattern: str) -> list[Finding]:
    """clang-query with every matcher on the sources."""
    with tempfile.TemporaryDirectory() as folder:
        script = Path(folder) / "jsf.query"
        script.write_text(query_script(pattern), encoding="utf-8")
        result = run_tool([CLANG_QUERY, f"-p={build}", "-f", str(script), *map(str, sources)])
    errors = [
        line for line in result.stdout.splitlines() + result.stderr.splitlines() if re.match(r"\d+:\d+: Error", line)
    ]
    if result.returncode != 0 or errors:
        raise JsfError(f"clang-query failed: {' '.join(errors)[:2000]}{result.stderr[-2000:]}")
    return parse_query(result.stdout, root)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise JsfError(f"cannot read {path}: {error}") from error


def listed_paths(text: str) -> list[str]:
    """The paths in a paths file: one a line, with # comments and blank lines left out."""
    paths: list[str] = []
    for line in text.splitlines():
        entry = line.split("#", 1)[0].strip()
        if entry:
            paths.append(entry)
    return paths


def cpp_files(root: Path, path: str) -> list[Path]:
    """The C++ files at a listed path: the file itself, or every C++ file under the folder."""
    full = root / path
    if full.is_file():
        return [full]
    if not full.is_dir():
        raise JsfError(f"{path} is listed but does not exist")
    suffixes = HEADER_SUFFIXES | SOURCE_SUFFIXES
    return sorted(
        found
        for found in full.rglob("*")
        if found.is_file() and found.suffix in suffixes and "build" not in found.parts
    )


def compiled_files(build: Path) -> set[Path]:
    """The files the build's compile_commands.json compiles."""
    database = build / "compile_commands.json"
    try:
        entries = json.loads(read_text(database))
    except json.JSONDecodeError as error:
        raise JsfError(f"{database} is not JSON: {error}") from error
    if not isinstance(entries, list):
        raise JsfError(f"{database} must hold a list")
    files: set[Path] = set()
    for entry in entries:
        if isinstance(entry, dict) and isinstance(entry.get("file"), str) and isinstance(entry.get("directory"), str):
            files.add((Path(entry["directory"]) / entry["file"]).resolve())
    return files


def translation_units(files: Sequence[Path], build: Path) -> list[Path]:
    """The implementation files among files, each of which the build must compile."""
    compiled = compiled_files(build)
    units: list[Path] = []
    for file in files:
        if file.suffix not in SOURCE_SUFFIXES:
            continue
        if file.resolve() not in compiled:
            raise JsfError(f"{file} is JSF code that {build}/compile_commands.json does not compile")
        units.append(file)
    return units


def check_files(root: Path, build: Path, files: Sequence[Path]) -> list[Finding]:
    """Every JSF check on the files."""
    findings: list[Finding] = []
    for file in files:
        findings.extend(check_text(lex(relative(root, str(file)), read_text(file))))
    units = translation_units(files, build)
    if units:
        pattern = path_pattern(relative(root, str(file)) for file in files)
        findings.extend(run_tidy(root, build, units, pattern))
        findings.extend(run_query(root, build, units, pattern))
    return sorted(set(findings), key=lambda found: (found.path, found.line, found.rule, found.message))


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="cpp_jsf", description="Gate C++ held to the JSF AV rules.")
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="check JSF C++ files")
    check.add_argument("--root", type=Path, required=True, help="the repository root")
    check.add_argument("--build", type=Path, required=True, help="a build folder with compile_commands.json")
    source = check.add_mutually_exclusive_group(required=True)
    source.add_argument("--paths", type=Path, metavar="FILE", help="a file listing the JSF paths")
    source.add_argument("--files", type=Path, nargs="+", metavar="FILE", help="the files to check")
    return parser.parse_args(argv)


def selected_files(args: argparse.Namespace) -> list[Path]:
    root: Path = args.root
    if args.files:
        return [file if file.is_absolute() else root / file for file in args.files]
    files: list[Path] = []
    for path in listed_paths(read_text(args.paths)):
        files.extend(cpp_files(root, path))
    return files


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        files = selected_files(args)
        findings = check_files(args.root, args.build, files)
    except JsfError as error:
        sys.stdout.write(f"::error::{error}\n")
        return EXIT_UNUSABLE
    for finding in findings:
        sys.stdout.write(finding.render() + "\n")
    if findings:
        sys.stdout.write(f"::error::{len(findings)} JSF AV finding(s); see docs/cpp-jsf-av.md\n")
        return EXIT_FINDINGS
    sys.stdout.write(f"JSF AV: {len(files)} file(s) keep the rules\n")
    return EXIT_CLEAN


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
