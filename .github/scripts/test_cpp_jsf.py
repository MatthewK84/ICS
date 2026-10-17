"""Unit tests for cpp_jsf.py (stdlib unittest; clang-tidy and clang-query are faked)."""

from __future__ import annotations

import io
import json
import subprocess
import tempfile
import unittest
from collections.abc import Sequence
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

import cpp_jsf as jsf

INTRO: str = "// sample.cpp: a sample.\n"


def rules(text: str, path: str = "cpp/cue/src/sample.cpp") -> list[str]:
    """The rules the text checks find in text, in order."""
    return [finding.rule for finding in jsf.check_text(jsf.lex(path, text))]


def body(code: str) -> str:
    """A well-formed implementation file holding code."""
    return INTRO + "namespace seed\n{\n\n" + code + "\n}  // namespace seed\n"


class LexerTest(unittest.TestCase):
    def test_blanks_comments_and_literal_contents_and_keeps_lines(self) -> None:
        source = jsf.lex("a.cpp", 'int x = 1; // note\nconst char* s = "a{b";\n/* c\nd */ y\n')
        self.assertEqual(source.code.count("\n"), source.text.count("\n"))
        self.assertNotIn("note", source.code)
        self.assertNotIn("a{b", source.code)
        self.assertIn("y", source.code)
        self.assertEqual([(c.line, c.block) for c in source.comments], [(1, False), (3, True)])

    def test_reads_raw_strings_prefixes_and_digit_separators(self) -> None:
        source = jsf.lex("a.cpp", 'auto r = R"x(a")\nb)x"; auto w = L\'c\'; auto n = 1\'000\'000; auto u = u8R"(z)";\n')
        self.assertEqual([literal.prefix for literal in source.literals], ["", "L", "u8"])
        self.assertIn("1'000'000", source.code)
        self.assertNotIn("b)x", source.code)

    def test_leaves_an_unterminated_literal_at_the_end_of_its_line(self) -> None:
        source = jsf.lex("a.cpp", 'auto s = "open\nint y;\n')
        self.assertIn("int y;", source.code)

    def test_tokenizes_with_lines_and_columns(self) -> None:
        tokens = jsf.tokenize("a::b <:\n  0x1F")
        self.assertEqual(
            [(t.text, t.line, t.column) for t in tokens],
            [("a", 1, 1), ("::", 1, 2), ("b", 1, 4), ("<:", 1, 6), ("0x1F", 2, 3)],
        )


class TextChecksTest(unittest.TestCase):
    def test_a_well_formed_file_keeps_the_rules(self) -> None:
        code = (
            "ics::int32 sign(ics::int32 value)\n{\n  ics::int32 sign = 0;\n  if (value > 0)\n  {\n    sign = 1;\n  }\n"
            "  else\n  {\n    sign = -1;\n  }\n  const auto f = [](ics::int32 x)\n  {\n    return x;\n  };\n"
            "  const Pair pair{1, 2};\n  return sign + f(pair.first);\n}\n"
        )
        self.assertEqual(
            rules(INTRO + "#include <ics/common/universal_types.h>\n" + body(code).removeprefix(INTRO)), []
        )

    def test_names_the_characters_tabs_and_long_lines(self) -> None:
        self.assertEqual(rules(body("// 5 µrad\n")), ["AV 9"])
        self.assertEqual(rules(body("\tbool tabbed = true;\n")), ["AV 43"])
        self.assertEqual(rules(body("// " + "x" * 130 + "\n")), ["AV 41"])
        self.assertEqual(rules(body("bool carriage = true;\r\n")), ["AV 9"])

    def test_names_trigraphs_digraphs_and_wide_literals(self) -> None:
        self.assertEqual(rules(body("// ??= no\n")), ["AV 11"])
        self.assertEqual(rules(body("bool b = a <: 0 :>;\n")), ["AV 12", "AV 12"])
        self.assertEqual(rules(body('auto w = L"wide";\n')), ["AV 13"])
        self.assertEqual(rules(body('auto u = u"utf16";\n')), ["AV 13"])

    def test_names_block_comments_commented_code_and_a_missing_intro(self) -> None:
        self.assertEqual(rules(body("/* no */\n")), ["AV 126"])
        self.assertEqual(rules(body("// count += 1;\n")), ["AV 127"])
        self.assertEqual(rules(body("bool ok = true;\n").replace(INTRO, "")), ["AV 133"])
        self.assertEqual(rules("// expect: seed\n" + body("bool ok = true;\n")), [])

    def test_names_the_file_extensions(self) -> None:
        self.assertEqual(rules(body(""), "cpp/cue/src/sample.cc").count("AV 54"), 1)
        header = "// sample.hpp: a sample.\n#ifndef G\n#define G\n#endif  // G\n"
        self.assertEqual(rules(header, "cpp/cue/include/sample.hpp"), ["AV 53"])

    def test_names_the_preprocessor_directives_includes_and_guards(self) -> None:
        self.assertEqual(rules(body("#pragma once\n")), ["AV 26"])
        self.assertEqual(rules(body('#include "local.h"\n')), ["AV 33"])
        self.assertEqual(rules(body("#include <cstdio>\n")), ["AV 22"])
        self.assertEqual(rules(body("#include <tl/expected.hpp>\n")), ["AV 32"])
        self.assertEqual(rules(body("#include <odd'name.h>\n")), ["AV 53.1"])
        self.assertEqual(rules(body("#include <ics/common/error.hpp>\n")), [])
        self.assertEqual(rules(body("#define LIMIT 3\n")), ["AV 31"])
        self.assertEqual(rules(body("#include\n")), ["AV 33"])

    def test_checks_a_headers_guard(self) -> None:
        path = "cpp/cue/include/sample.h"
        good = "// sample.h: a sample.\n#ifndef G\n#define G\n#include <vector>\n#endif  // G\n"
        self.assertEqual(rules(good, path), [])
        for bad in (
            "// sample.h: a sample.\n#include <vector>\n#ifndef G\n#define G\n#endif\n",
            "// sample.h: a sample.\n#ifndef G\n#define H\n#endif\n",
            "// sample.h: a sample.\n#ifndef G\n#define G\n#define X\n#endif\n",
            "// sample.h: a sample.\nint32 x;\n",
        ):
            self.assertIn("AV 35", rules(bad, path), bad)

    def test_names_banned_library_facilities(self) -> None:
        self.assertEqual(rules(body("void f()\n{\n  std::abort();\n}\n")), ["AV 24"])
        self.assertEqual(rules(body("void f()\n{\n  abort();\n}\n"), "cpp/cue/fuzz/sample.cpp"), [])
        self.assertEqual(rules(body("void f()\n{\n  exit(1);\n}\n")), ["AV 24"])
        self.assertEqual(rules(body("void f()\n{\n  thing.exit(1);\n  ics::exit(1);\n}\n")), [])
        self.assertEqual(rules(body("auto e = errno;\n")), ["AV 17"])
        self.assertEqual(rules(body("void f()\n{\n  printf(x);\n}\n")), ["AV 22"])

    def test_names_banned_keywords_and_basic_types(self) -> None:
        self.assertEqual(rules(body("void f()\n{\n  throw 1;\n}\n")), ["AV 208"])
        self.assertEqual(rules(body("volatile bool v = true;\n")), ["AV 205"])
        self.assertEqual(rules(body("double half = 0.5;\n")), ["AV 209"])
        self.assertEqual(rules(INTRO + "int main(int argc, char* argv[])\n{\n  return 0;\n}\n"), [])
        self.assertEqual(rules(INTRO + "int main(const int argc, char* argv[])\n{\n  return 0;\n}\n"), [])
        self.assertEqual(
            rules(INTRO + "using float64 = double;\n", "cpp/common/include/ics/common/universal_types.h").count(
                "AV 209"
            ),
            0,
        )

    def test_names_long_and_underscored_identifiers_and_literals(self) -> None:
        self.assertEqual(rules(body("bool " + "a" * 65 + " = true;\n")), ["AV 46"])
        self.assertEqual(rules(body("bool _hidden = true;\n")), ["AV 47"])
        self.assertEqual(rules(body("auto o = 0755;\n")), ["AV 149"])
        self.assertEqual(rules(body("auto z = 00;\nauto h = 0x1F;\nauto f = 0.5;\n")), [])
        self.assertEqual(rules(body("auto h = 0x1f;\n")), ["AV 150"])

    def test_names_two_statements_on_a_line(self) -> None:
        self.assertEqual(rules(body("void f()\n{\n  a(); b();\n}\n")), ["AV 42"])
        self.assertEqual(rules(body("void f()\n{\n  for (int32 i = 0; i < 3; ++i)\n  {\n  }\n}\n")), [])


class StructureTest(unittest.TestCase):
    def test_wants_block_braces_on_their_own_lines(self) -> None:
        self.assertEqual(rules(body("void f() {\n}\n")), ["AV 61"])
        self.assertEqual(rules(body("void f()\n{\n  if (x)\n  {\n  } else\n  {\n  }\n}\n")), ["AV 61"])
        self.assertEqual(rules(body("void f()\n{\n}\nstruct S\n{\n  int32 x{0};\n};\n")), [])
        self.assertEqual(rules(body("void f()\n{\n  g([](int32 x)\n  {\n    return x;\n  });\n}\n")), [])
        self.assertEqual(rules(body("void f()\n{\n  do\n  {\n  }\n  while (x);\n}\n")), [])
        self.assertEqual(rules(body("void f()\n{\n  const auto v = {{1, 2}, {3, 4}};\n}\n")), [])
        self.assertEqual(
            rules(body("void f()\n{\n  switch (x)\n  {\n    case 1:\n    {\n      break;\n    }\n  }\n}\n")), []
        )

    def test_allows_break_only_in_a_switch(self) -> None:
        loop = body("void f()\n{\n  while (x)\n  {\n    break;\n  }\n}\n")
        self.assertEqual(rules(loop), ["AV 191"])
        nested = body(
            "void f()\n{\n  for (;;)\n  {\n    switch (x)\n    {\n      default:\n        break;\n    }\n  }\n}\n"
        )
        self.assertEqual(rules(nested), [])
        self.assertEqual(rules(body("void f()\n{\n  while (x)\n  {\n    continue;\n  }\n}\n")), ["AV 190"])
        self.assertEqual(rules(body("void f()\n{\n  if (x)\n    y();\n  break;\n}\n")), [])

    def test_wants_access_sections_in_order(self) -> None:
        good = body("class C\n{\npublic:\n  void f();\n\nprotected:\n  void g();\n\nprivate:\n  int32 x_;\n};\n")
        self.assertEqual(rules(good), [])
        bad = body("class C\n{\nprivate:\n  int32 x_;\n\npublic:\n  void f();\n};\n")
        self.assertEqual(rules(bad), ["AV 57"])
        self.assertEqual(rules(body("struct D : public B\n{\n};\n")), [])

    def test_wants_namespaces_two_deep_at_most(self) -> None:
        self.assertEqual(rules(INTRO + "namespace a::b\n{\n}\nnamespace\n{\n}\n"), [])
        self.assertEqual(rules(INTRO + "namespace a::b\n{\nnamespace\n{\n}\n}\n"), ["AV 99"])
        self.assertEqual(rules(INTRO + 'enum class Mode : uint8\n{\n  slow\n};\nextern "C" void f()\n{\n}\n'), [])

    def test_survives_unbalanced_braces(self) -> None:
        self.assertEqual(rules(body("}\n")), [])


class ToolOutputTest(unittest.TestCase):
    def test_reads_clang_query_findings_and_parameter_layout(self) -> None:
        root = Path("/repo")
        output = "\n".join(
            [
                '/repo/cpp/cue/a.cpp:4:10: note: "av163" binds here',
                '/repo/cpp/cue/a.cpp:9:1: note: "counter" binds here',
                '/repo/cpp/cue/a.cpp:7:1: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:7:10: note: "av58p" binds here',
                '/repo/cpp/cue/a.cpp:7:1: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:7:20: note: "av58p" binds here',
                '/repo/cpp/cue/a.cpp:7:1: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:8:10: note: "av58p" binds here',
                '/repo/cpp/cue/a.cpp:7:1: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:9:10: note: "av58p" binds here',
                '/repo/cpp/cue/a.cpp:7:1: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:9:20: note: "av58p" binds here',
                '/repo/cpp/cue/a.cpp:9:30: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:9:10: note: "av58p" binds here',
                '/repo/cpp/cue/a.cpp:9:30: note: "av58f" binds here',
                '/repo/cpp/cue/a.cpp:9:20: note: "av58p" binds here',
                "2 matches.",
            ]
        )
        findings = jsf.parse_query(output, root)
        self.assertEqual([(f.line, f.rule) for f in findings], [(4, "AV 163"), (7, "AV 58")])

    def test_passes_parameters_each_on_their_own_line(self) -> None:
        output = "\n".join(
            [
                '/repo/a.cpp:1:1: note: "av58f" binds here',
                '/repo/a.cpp:1:10: note: "av58p" binds here',
                '/repo/a.cpp:1:1: note: "av58f" binds here',
                '/repo/a.cpp:2:10: note: "av58p" binds here',
                '/repo/a.cpp:1:1: note: "av58f" binds here',
                '/repo/a.cpp:3:10: note: "av58p" binds here',
            ]
        )
        self.assertEqual(jsf.parse_query(output, Path("/repo")), [])

    def test_counts_each_functions_decisions_outside_macros(self) -> None:
        def decisions(function: str, count: int, first_line: int) -> list[str]:
            notes: list[str] = []
            for line in range(first_line, first_line + count):
                notes.append(f'/repo/a.cpp:{function}: note: "av3f" binds here')
                notes.append(f'/repo/a.cpp:{line}:5: note: "av3s" binds here')
            return notes

        macro = [
            '/repo/a.cpp:70:1: note: "av3f" binds here',
            "/gtest/gtest.h:9:1: note: expanded from macro 'TEST'",
            '/repo/a.cpp:71:3: note: "av3s" binds here',
            "   71 |   EXPECT_EQ(a, b);",
            "/gtest/gtest.h:20:3: note: expanded from macro 'EXPECT_EQ'",
        ]
        output = "\n".join(
            [
                *decisions("1:1", 20, 2),
                *decisions("1:1", 1, 2),
                *decisions("30:1", 19, 31),
                *macro,
                *decisions("70:1", 19, 72),
                '/repo/a.cpp:70:1: note: "av3f" binds here',
                '/repo/a.cpp:72:9: note: "av3s" binds here',
                "/gtest/gtest.h:20:3: note: expanded from macro 'EXPECT_EQ'",
            ]
        )
        findings = jsf.parse_query(output, Path("/repo"))
        self.assertEqual(
            [(f.line, f.rule, f.message) for f in findings],
            [(1, "AV 3", "a cyclomatic complexity of 21, over 20")],
        )

    def test_reads_clang_tidy_findings(self) -> None:
        suffix = ",-warnings-as-errors]"
        output = "\n".join(
            [
                "/repo/cpp/cue/a.cpp:3:5: error: statement should be inside braces "
                "[readability-braces-around-statements" + suffix,
                "/repo/cpp/cue/a.cpp:4:5: error: invalid case style for constexpr variable 'kX' "
                "[readability-identifier-naming" + suffix,
                "/repo/cpp/cue/a.cpp:5:5: error: invalid case style for function 'Foo' [readability-identifier-naming"
                + suffix,
                "/repo/cpp/cue/a.cpp:6:5: error: something new [new-check" + suffix,
                "3 warnings generated.",
            ]
        )
        found = [(f.path, f.line, f.rule) for f in jsf.parse_tidy(output, Path("/repo"))]
        self.assertEqual(
            found,
            [
                ("cpp/cue/a.cpp", 3, "AV 59"),
                ("cpp/cue/a.cpp", 4, "AV 52"),
                ("cpp/cue/a.cpp", 5, "AV 51"),
                ("cpp/cue/a.cpp", 6, "AV ?"),
            ],
        )

    def test_matches_listed_paths_and_builds_the_queries(self) -> None:
        pattern = jsf.path_pattern(["cpp/cue", "cpp/common/include/ics/common/universal_types.h"])
        self.assertEqual(pattern, "/cpp/common/include/ics/common/universal_types[.]h$|/cpp/cue/")
        self.assertEqual(jsf.path_pattern([]), "^$")
        script = jsf.query_script(pattern)
        self.assertEqual(script.count("\nmatch "), len(jsf.QUERIES) + 2)
        self.assertNotIn("{r}", script)

    def test_reads_a_paths_file(self) -> None:
        self.assertEqual(jsf.listed_paths("# heading\ncpp/cue\n\ncpp/a.h  # why\n"), ["cpp/cue", "cpp/a.h"])


def completed(code: int, stdout: str = "", stderr: str = "") -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess([], code, stdout, stderr)


class RunTest(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        (self.root / "cpp/policy").mkdir(parents=True)
        (self.root / jsf.PROFILE).write_text("Checks: '-*'\n", encoding="utf-8")
        (self.root / "cpp/cue").mkdir(parents=True)
        self.source = self.root / "cpp/cue/sample.cpp"
        self.source.write_text(body("bool ok = true;\n"), encoding="utf-8")
        self.build = self.root / "build"
        self.build.mkdir()
        entry = {"directory": str(self.root), "file": "cpp/cue/sample.cpp", "command": "c++ -c cpp/cue/sample.cpp"}
        (self.build / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
        (self.root / "cpp/policy/jsf-paths.txt").write_text("cpp/cue\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.folder.cleanup()

    def run_main(self, outputs: Sequence[subprocess.CompletedProcess[str]], *extra: str) -> tuple[int, str]:
        args = ["check", "--root", str(self.root), "--build", str(self.build), *extra]
        stream = io.StringIO()
        with patch("cpp_jsf.subprocess.run", side_effect=list(outputs)), redirect_stdout(stream):
            status = jsf.main(args)
        return status, stream.getvalue()

    def test_passes_a_clean_tree(self) -> None:
        status, output = self.run_main(
            [completed(0), completed(0)], "--paths", str(self.root / "cpp/policy/jsf-paths.txt")
        )
        self.assertEqual(status, jsf.EXIT_CLEAN, output)
        self.assertIn("1 file(s) keep the rules", output)

    def test_reports_the_tools_findings(self) -> None:
        tidy = completed(1, f"{self.source}:6:1: error: magic [readability-magic-numbers,-warnings-as-errors]\n")
        query = completed(0, f'{self.source}:6:3: note: "av163" binds here\n')
        status, output = self.run_main([tidy, query], "--files", "cpp/cue/sample.cpp")
        self.assertEqual(status, jsf.EXIT_FINDINGS)
        self.assertIn("cpp/cue/sample.cpp:6: AV 151: magic", output)
        self.assertIn("cpp/cue/sample.cpp:6: AV 163: unsigned arithmetic", output)

    def test_stops_when_a_tool_fails(self) -> None:
        status, output = self.run_main([completed(1, "", "crashed")], "--files", "cpp/cue/sample.cpp")
        self.assertEqual(status, jsf.EXIT_UNUSABLE)
        self.assertIn("clang-tidy failed", output)
        status, output = self.run_main(
            [completed(0), completed(0, "1:2: Error parsing argument")], "--files", "cpp/cue/sample.cpp"
        )
        self.assertEqual(status, jsf.EXIT_UNUSABLE)
        self.assertIn("clang-query failed", output)

    def test_stops_on_files_it_cannot_use(self) -> None:
        other = self.root / "cpp/cue/other.cpp"
        other.write_text(body(""), encoding="utf-8")
        status, output = self.run_main([], "--files", str(other))
        self.assertEqual(status, jsf.EXIT_UNUSABLE)
        self.assertIn("does not compile", output)
        (self.root / "cpp/policy/jsf-paths.txt").write_text("cpp/missing\n", encoding="utf-8")
        status, output = self.run_main([], "--paths", str(self.root / "cpp/policy/jsf-paths.txt"))
        self.assertEqual(status, jsf.EXIT_UNUSABLE)
        self.assertIn("does not exist", output)
        (self.build / "compile_commands.json").write_text("{", encoding="utf-8")
        status, output = self.run_main([], "--files", "cpp/cue/sample.cpp")
        self.assertEqual(status, jsf.EXIT_UNUSABLE)
        self.assertIn("is not JSON", output)

    def test_checks_a_header_by_text_alone(self) -> None:
        header = self.root / "cpp/cue/sample.h"
        header.write_text("// sample.h: a sample.\n#pragma once\n", encoding="utf-8")
        status, output = self.run_main([], "--files", str(header))
        self.assertEqual(status, jsf.EXIT_FINDINGS)
        self.assertIn("AV 26", output)

    def test_says_when_a_tool_is_missing(self) -> None:
        args = ["check", "--root", str(self.root), "--build", str(self.build), "--files", "cpp/cue/sample.cpp"]
        stream = io.StringIO()
        with patch("cpp_jsf.subprocess.run", side_effect=FileNotFoundError("clang-tidy-17")), redirect_stdout(stream):
            status = jsf.main(args)
        self.assertEqual(status, jsf.EXIT_UNUSABLE)
        self.assertIn("cannot run clang-tidy-17", stream.getvalue())


if __name__ == "__main__":
    unittest.main()
