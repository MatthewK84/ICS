# C++ JSF AV rules

From ICS-037 ([#37](https://github.com/MatthewK84/ICS/issues/37)) on, new C++ in ICS follows the *Joint Strike Fighter Air Vehicle C++ Coding Standards* (Lockheed Martin, document 2RDU00001 Rev C, December 2005), [published by Bjarne Stroustrup](https://www.stroustrup.com/JSF-AV-rules.pdf). It applies on top of the Power-of-Ten profile in [cpp/README.md](../cpp/README.md#power-of-ten-checks); where the two differ, the stricter one holds (functions of 50 lines, not 200).

This page says how ICS checks each of the standard's 229 rules, records every deviation from them, documents integer division (AV 167), and lists what review checks.

## Scope

The JSF code is every file under the paths in [`cpp/policy/jsf-paths.txt`](../cpp/policy/jsf-paths.txt): each module written from ICS-037 on, and [`ics/common/universal_types.h`](../cpp/common/include/ics/common/universal_types.h). The modules written before ICS-037 keep the Power-of-Ten profile until [#170](https://github.com/MatthewK84/ICS/issues/170) brings each one to these rules.

To add a module:

1. List its folder in `cpp/policy/jsf-paths.txt`.
2. Call `ics_jsf_target(<target>)`, from [`cpp/cmake/Jsf.cmake`](../cpp/cmake/Jsf.cmake), for each of its targets, tests and fuzz targets included. For JSF files in a target shared with older code, call `ics_jsf_sources(<files>)` instead.
3. Write each file to the rules below. Use the fixed-size types from `universal_types.h` (`int32`, `float64` and the rest), never `int` or `double`.

## Rule types

The standard has three kinds of rule (its section 4.2.1):

- a **shall** rule is mandatory and must be verified, by a tool or by review;
- a **will** rule is mandatory but need not be verified;
- a **should** rule is advice.

ICS checks every rule a tool can check, whatever its kind, and CI fails on any finding. Breaking a rule the tools check, of any kind, takes an entry in the [deviations register](#deviations). The rules only review can check are in the [review checklist](#review-checklist). The standard has no rules 161 or 172.

## The checks

[`cpp/policy/check-policy.sh`](../cpp/policy/check-policy.sh) runs them in its `clang` half, which CI runs on every change to `cpp/`:

| Code | Check | What it is |
|---|---|---|
| G | the gate's text checks | [`.github/scripts/cpp_jsf.py`](../.github/scripts/cpp_jsf.py) reads each JSF file's characters, comments, preprocessor lines and tokens: banned headers, names and keywords, the basic types, literals, braces, layout and the intro comment |
| Q | the gate's clang-query matchers | the same script runs clang-query over each JSF translation unit, for the rules no clang-tidy check covers (cyclomatic complexity, unsigned arithmetic, type names, side effects in `&&` and `\|\|`, `for` loops, labels and more) |
| T | clang-tidy's JSF profile | the same script runs clang-tidy with [`cpp/policy/jsf-clang-tidy.yaml`](../cpp/policy/jsf-clang-tidy.yaml) over each JSF translation unit and the JSF headers it includes |
| C | the compiler | [`cpp/cmake/Warnings.cmake`](../cpp/cmake/Warnings.cmake)'s warnings and `-Werror` everywhere, and the extra warnings in [`cpp/cmake/Jsf.cmake`](../cpp/cmake/Jsf.cmake) on the JSF targets, with both GCC and Clang |
| R | review | the reviewer checks the rule against the [review checklist](#review-checklist) |
| D | a deviation | the rule is broken on purpose; see [Deviations](#deviations) |
| P | process | the rule is about how ICS works, not about code |

Every gate finding names its file, line and rule, as in `cpp/cue/src/filter.cpp:42: AV 163: unsigned arithmetic`. The gate stops with an error when a JSF `.cpp` file is missing from `compile_commands.json`, so a file cannot dodge the T and Q checks by not being built.

The script also proves that each check still fires. Each file in [`cpp/policy/seeded/jsf/`](../cpp/policy/seeded/jsf) breaks one rule and must be rejected with the message on its first line: 38 seeds for the gate, and 2 for the compiler warnings.

To run the gate alone, build the `clang-debug` preset, then:

```sh
python3 .github/scripts/cpp_jsf.py check --root . --build cpp/build/clang-debug --paths cpp/policy/jsf-paths.txt
python3 .github/scripts/cpp_jsf.py check --root . --build cpp/build/clang-debug --files cpp/cue/src/filter.cpp
```

It exits 0 when the files keep the rules, 1 on findings, and 2 when it cannot run, such as a missing file or tool.

## Rules

Each rule's text is shortened; the standard has the full text, its rationale and its examples. ICS's own limits are stricter in places, such as 50-line functions.

| AV | Kind | Rule | How ICS checks it |
|---|---|---|---|
| 1 | will | Any one function (or method) will contain no more than 200 logical source lines of code. | T: `readability-function-size`, 50 lines (ICS's limit, stricter than 200) |
| 2 | shall | There shall not be any self-modifying code. | R: no ICS code writes or patches code at run time |
| 3 | shall | All functions shall have a cyclomatic complexity number of 20 or less. | Q: one more than the `if`, loop, `case`, `catch`, `?:`, `&&` and `\|\|` in each function, leaving out those a macro expands to (D-5) |
| 4 | process | To break a "should" rule, the developer needs the software engineering lead's approval. | P: approved by the code owner as software engineering lead (see Deviations) |
| 5 | process | To break a "will" or a "shall" rule, the developer needs the approval of the software engineering lead and the software product manager. | P: approved by the code owner as software engineering lead and software product manager (see Deviations) |
| 6 | shall | Each deviation from a "shall" rule shall be documented in the file that contains it; this rule allows no deviation. | P: a file that deviates cites the register entry beside the deviation; a project-wide deviation is cited where it is configured (see Deviations) |
| 7 | will | Approval will not be required for a deviation from a "shall" or "will" rule that complies with an exception specified by that rule. | P: a deviation a rule's own exception allows needs no approval |
| 8 | shall | All code shall conform to ISO/IEC 14882:2002(E) standard C++. | D-1: C++20 |
| 9 | will | Only those characters specified in the C++ basic source character set will be used. | G: characters outside the basic source character set |
| 10 | will | Values of character types will be restricted to a defined and documented subset of ISO 10646-1. | G: the source is ASCII (AV 9's check); R: character values in code are ASCII |
| 11 | will | Trigraphs will not be used. | G: trigraphs |
| 12 | will | The digraphs `<%`, `%>`, `<:`, `:>`, `%:` and `%:%:` will not be used. | G: digraphs |
| 13 | will | Multi-byte characters and wide string literals will not be used. | G: `L`, `u`, `U` and `u8` literals |
| 14 | shall | Literal suffixes shall use uppercase rather than lowercase letters. | T: `readability-uppercase-literal-suffix` |
| 15 | shall | Provision shall be made for run-time checking (defensive programming). | The coverage gate's assertion-density floors (`ics::check`, cpp/policy/coverage-gates.txt) |
| 16 | shall | Only DO-178B level A certifiable or SEAL 1 C/C++ libraries shall be used with safety-critical (SEAL 1) code. | D-3: no certified libraries |
| 17 | shall | The error indicator errno shall not be used. | G: `errno` and `<cerrno>` |
| 18 | shall | The macro offsetof, in library `<stddef.h>`, shall not be used. | G: `offsetof` |
| 19 | shall | `<locale.h>` and the setlocale function shall not be used. | G: `setlocale` and `<clocale>` |
| 20 | shall | The setjmp macro and the longjmp function shall not be used. | G: `setjmp`, `longjmp` and `<csetjmp>`; T: `cert-err52-cpp` |
| 21 | shall | The signal handling facilities of `<signal.h>` shall not be used. | G: `signal`, `raise` and `<csignal>` |
| 22 | shall | The input/output library `<stdio.h>` shall not be used. | G: `<cstdio>` and its functions |
| 23 | shall | The library functions atof, atoi and atol from library `<stdlib.h>` shall not be used. | G: `atof`, `atoi` and `atol` |
| 24 | shall | The library functions abort, exit, getenv and system from library `<stdlib.h>` shall not be used. | G: `abort`, `exit`, `getenv` and `system` (D-5 for fuzz targets) |
| 25 | shall | The time handling functions of library `<time.h>` shall not be used. | G: `<ctime>` and its functions; `<chrono>` is used instead |
| 26 | shall | Only the `#ifndef`, `#define`, `#endif` and `#include` directives shall be used. | G: any other directive |
| 27 | will | #ifndef, #define and #endif will be used to prevent multiple inclusions of the same header file. | G: a header's guard is `#ifndef G`, `#define G` ... `#endif`, and nothing else |
| 28 | will | The #ifndef and #endif pre-processor directives will only be used as defined in AV Rule 27 to prevent multiple inclusions of the same header file. | G: no other `#ifndef` or `#endif` |
| 29 | shall | The #define pre-processor directive shall not be used to create inline macros. | T: `cppcoreguidelines-macro-usage`; G: `#define` only in a guard |
| 30 | shall | The #define pre-processor directive shall not be used to define constant values. | T: `cppcoreguidelines-macro-usage`; G: `#define` only in a guard |
| 31 | will | The #define pre-processor directive will only be used as part of the technique to prevent multiple inclusions of the same header file. | G: `#define` only in a guard |
| 32 | will | The #include pre-processor directive will only be used to include header (*.h) files. | G: an `#include` names a `.h` header, a standard header, or an existing module's `.hpp` (D-6) |
| 33 | shall | The #include directive shall use the `<filename.h>` notation to include header files. | G: `<file.h>` form only |
| 34 | should | Header files should contain logically related declarations only. | R |
| 35 | will | A header file will contain a mechanism that prevents multiple inclusions of itself. | G: the include guard |
| 36 | should | Compilation dependencies should be minimized when possible. | R |
| 37 | should | Header (include) files should include only those header files that are required for them to successfully compile. | T: `misc-include-cleaner` (headers not used) |
| 38 | should | Declarations of classes that are only accessed via pointers (*) or references (&) should be supplied by forward headers that contain only forward declarations. | R |
| 39 | will | Header files (*.h) will not contain non-const variable definitions or function definitions. | T: `misc-definitions-in-headers` |
| 40 | shall | Every implementation file shall include the header files that uniquely define the inline functions, types, and templates used. | T: `misc-include-cleaner` (headers missing) |
| 41 | will | Source lines will be kept to a length of 120 characters or less. | G: lines of 120 characters or less |
| 42 | will | Each expression-statement will be on a separate line. | G: one statement a line |
| 43 | should | Tabs should be avoided. | G: tabs |
| 44 | will | All indentations will be at least two spaces and be consistent within the same source file. | R: two-space indentation |
| 45 | will | All words in an identifier will be separated by the '_' character. | T: `readability-identifier-naming` (`lower_case`); Q: type names (AV 50) |
| 46 | will | User-specified identifiers (internal and external) will not rely on significance of more than 64 characters. | G: identifiers of 64 characters or fewer |
| 47 | will | Identifiers will not begin with the underscore character '_'. | G: a leading `_` |
| 48 | will | Identifiers will not differ only by case, an underscore, or the interchange of O, 0 and D; I, 1 and l; S and 5; Z and 2; or n and h. | T: `misc-confusable-identifiers`; R for the rest |
| 49 | will | All acronyms in an identifier will be composed of uppercase letters. | Q: AV 50's pattern allows an acronym in capitals as a word; R |
| 50 | will | The first word of the name of a class, structure, namespace, enumeration, or type created with typedef will begin with an uppercase letter. | Q: type names; D-4 for namespaces |
| 51 | will | All letters contained in function and variable names will be composed entirely of lowercase letters. | T: `readability-identifier-naming` (functions, variables, parameters, members) |
| 52 | shall | Identifiers for constant and enumerator values shall be lowercase. | T: `readability-identifier-naming` (constants, constexpr variables, enumerators) |
| 53 | will | Header files will always have a file name extension of ".h". | G: headers end in `.h` |
| 53.1 | shall | The sequences `'`, `\`, `/*`, `//` and `"` shall not appear in header file names. | G: the sequences in `#include` names |
| 54 | will | Implementation files will always have a file name extension of ".cpp". | G: implementation files end in `.cpp` |
| 55 | should | The name of a header file should reflect the logical entity for which it provides declarations. | R |
| 56 | should | An implementation file's name should reflect the logical entity it defines, and end in ".cpp". | R |
| 57 | will | A class's public, protected and private sections will be declared in that order. | G: public, protected, private in order |
| 58 | will | When declaring and defining functions with more than two parameters, the leading parenthesis and the first argument will be written on the same line as the function name. | Q: each parameter after the first on a line of its own, past two |
| 59 | shall | The statements forming the body of an if, else if, else, while, do...while or for statement shall always be enclosed in braces, even if the braces form an empty block. | T: `readability-braces-around-statements` |
| 60 | will | Braces ("{}") which enclose a block will be placed in the same column, on separate lines directly before and after the block. | G: a block's braces on their own lines |
| 61 | will | Braces ("{}") which enclose a block will have nothing else on the line except comments (if necessary). | G: nothing else on a block brace's line |
| 62 | will | The dereference operator '*' and the address-of operator '&' will be directly connected with the type-specifier. | R |
| 63 | will | Spaces will not be used around '.' or '->', nor between unary operators and operands. | R |
| 64 | should | A class interface should be complete and minimal. | R |
| 65 | should | A structure should be used to model an entity that does not require an invariant. | R |
| 66 | should | A class should be used to model an entity that maintains an invariant. | R |
| 67 | should | Public and protected data should only be used in structs, not classes. | R |
| 68 | shall | Unneeded implicitly generated member functions shall be explicitly disallowed. | T: `cppcoreguidelines-special-member-functions` |
| 69 | will | A member function that does not affect the state of an object (its instance variables) will be declared const. | T: `readability-make-member-function-const` |
| 70 | will | A class will have friends only when a function or object needs its private elements but cannot be a member. | R |
| 70.1 | shall | An object shall not be improperly used before its lifetime begins or after its lifetime ends. | R; ASan in CI |
| 71 | shall | Calls to an externally visible operation of an object, other than its constructors, shall not be allowed until the object has been fully initialized. | R |
| 71.1 | shall | A class's virtual functions shall not be invoked from its destructor or any of its constructors. | T: `clang-analyzer-optin.cplusplus.VirtualCall` |
| 72 | should | A class's invariant should be part of each constructor's postcondition, the destructor's precondition, and each other public operation's pre- and postconditions. | R |
| 73 | shall | Unnecessary default constructors shall not be defined. | R |
| 74 | will | Initialization of nonstatic class members will be performed through the member initialization list rather than through assignment in the body of a constructor. | T: `cppcoreguidelines-prefer-member-initializer` |
| 75 | shall | Members of the initialization list shall be listed in the order in which they are declared in the class. | C: `-Wreorder` (in `-Wall`) |
| 76 | shall | A copy constructor and an assignment operator shall be declared for classes that contain pointers to data items or nontrivial destructors. | T: `cppcoreguidelines-special-member-functions` |
| 77 | shall | A copy constructor shall copy all data members and bases that affect the class invariant (a data element representing a cache, for example, would not need to be copied). | R |
| 77.1 | shall | A member function shall not have default arguments that give it the signature of the implicit copy constructor. | R |
| 78 | shall | All base classes with a virtual function shall define a virtual destructor. | T: `cppcoreguidelines-virtual-class-destructor`; C: `-Wnon-virtual-dtor` |
| 79 | shall | All resources acquired by a class shall be released by the class's destructor. | R; ASan's leak check in CI |
| 80 | will | The default copy and assignment operators will be used for classes when those operators offer reasonable semantics. | R |
| 81 | shall | The assignment operator shall handle self-assignment correctly. | T: `bugprone-unhandled-self-assignment` |
| 82 | shall | An assignment operator shall return a reference to *this. | T: `misc-unconventional-assign-operator` |
| 83 | shall | An assignment operator shall assign all data members and bases that affect the class invariant. | R |
| 84 | will | Operator overloading will be used sparingly and in a conventional manner. | R |
| 85 | will | When two operators are opposites (such as == and !=), both will be defined and one will be defined in terms of the other. | R |
| 86 | should | Concrete types should be used to represent simple independent concepts. | R |
| 87 | should | Hierarchies should be based on abstract classes. | R |
| 88 | shall | Multiple inheritance shall only be allowed in the following restricted form: n interfaces plus m private implementations, plus at most one protected implementation. | R |
| 88.1 | shall | A stateful virtual base shall be explicitly declared in each derived class that accesses it. | Q: no virtual bases at all |
| 89 | shall | A base class shall not be both virtual and non-virtual in the same hierarchy. | Q: no virtual bases at all |
| 90 | should | Heavily used interfaces should be minimal, general and abstract. | R |
| 91 | will | Public inheritance will be used to implement "is-a" relationships. | R |
| 92 | will | A subtype will have preconditions no stronger, and postconditions no weaker, than those of the methods it overrides. | R |
| 93 | will | "has-a" or "is-implemented-in-terms-of" relationships will be modeled through membership or non-public inheritance. | R |
| 94 | shall | An inherited nonvirtual function shall not be redefined in a derived class. | R |
| 95 | shall | An inherited default parameter shall never be redefined. | T: `google-default-arguments` |
| 96 | shall | Arrays shall not be treated polymorphically. | R |
| 97 | shall | Arrays shall not be used in interfaces. | T: `cppcoreguidelines-avoid-c-arrays`, `-pro-bounds-array-to-pointer-decay` (D-5, D-7) |
| 97.1 | shall | Neither operand of an equality operator (== or !=) shall be a pointer to a virtual member function. | R |
| 98 | should | Every nonlocal name, except main(), should be placed in some namespace. | R |
| 99 | will | Namespaces will not be nested more than two levels deep. | G: namespaces two deep at most, the unnamed namespace included |
| 100 | should | Namespace elements should be selected by a using declaration or explicit qualification for a few names, and a using directive for many. | R |
| 101 | shall | Templates shall be reviewed alone, for what they require of their arguments, and for each function their actual arguments instantiate. | R: each template reviewed alone and for each instantiation |
| 102 | shall | Template tests shall be created to cover all actual template instantiations. | R: the tests instantiate each template with every argument the code uses |
| 103 | should | Constraint checks should be applied to template arguments. | R |
| 104 | shall | A template specialization shall be declared before its use. | C: an explicit specialization after its use is ill-formed |
| 105 | should | A template definition's dependence on its instantiation contexts should be minimized. | R |
| 106 | should | Specializations for pointer types should be made where appropriate. | R |
| 107 | shall | Functions shall always be declared at file scope. | Q: functions declared at block scope |
| 108 | shall | Functions with variable numbers of arguments shall not be used. | T: `cppcoreguidelines-pro-type-vararg`; Q: variadic functions; G: `<cstdarg>` |
| 109 | should | A function definition should not be placed in a class specification unless the function is intended to be inlined. | R |
| 110 | will | Functions with more than 7 arguments will not be used. | T: `readability-function-size`, 7 parameters |
| 111 | shall | A function shall not return a pointer or reference to a non-static local object. | C: `-Wreturn-stack-address` (Clang) and `-Wreturn-local-addr` (GCC), on by default |
| 112 | should | Function return values should not obscure resource ownership. | R |
| 113 | will | Functions will have a single exit point. | R, with the rule's exception for guard clauses |
| 114 | shall | All exit points of value-returning functions shall be through return statements. | C: `-Wreturn-type` (in `-Wall`), an error |
| 115 | will | If a function returns error information, then that error information will be tested. | C: `[[nodiscard]]` results with `-Werror`; T: `bugprone-unused-return-value` |
| 116 | should | Small concrete-type arguments (two or three words) should be passed by value when changes to them should not reach the caller. | R |
| 117 | should | Arguments should be passed by reference if NULL values are not possible. | R |
| 118 | should | Arguments should be passed via pointers if NULL values are possible. | R |
| 119 | shall | Functions shall not call themselves, either directly or indirectly (i.e. recursion shall not be allowed). | T: `misc-no-recursion` |
| 120 | should | Overloaded operations or methods should form families with the same semantics, name and purpose, differing only in their parameters. | R |
| 121 | should | Only functions with 1 or 2 statements should be considered candidates for inline functions. | R |
| 122 | should | Trivial accessor and mutator functions should be inlined. | R |
| 123 | should | The number of accessor and mutator functions should be minimized. | R |
| 124 | should | Trivial forwarding functions should be inlined. | R |
| 125 | should | Unnecessary temporary objects should be avoided. | R |
| 126 | shall | Only valid C++ style comments (//) shall be used. | G: `/* */` comments |
| 127 | shall | Code that is not used (commented out) shall be deleted. | G: comments that end in `;`, `{` or `}`; R for the rest |
| 128 | will | Comments that document actions or sources (e.g. tables, figures, paragraphs, etc.) outside of the file being documented will not be allowed. | R |
| 129 | should | Comments in header files should describe the externally visible behavior of the functions or classes being documented. | R |
| 130 | should | The purpose of every line of executable code should be explained by a comment, although one comment may describe more than one line of code. | R |
| 131 | should | One should avoid stating in comments what is better stated in code (i.e. do not simply repeat what is in the code). | R |
| 132 | will | Each variable declaration, typedef, enumeration value, and structure member will be commented. | R |
| 133 | will | Every source file will open with a comment giving its name, its contents and any information the program requires. | G: the file opens with `// <name>: ` and what it holds |
| 134 | should | Assumptions (limitations) made by functions should be documented in the function's preamble. | R |
| 135 | shall | Identifiers in an inner scope shall not use the same name as an identifier in an outer scope, and therefore hide that identifier. | C: `-Wshadow` |
| 136 | should | Declarations should be at the smallest feasible scope. | R |
| 137 | should | All declarations at file scope should be static where possible. | T: `misc-use-anonymous-namespace` |
| 138 | shall | Identifiers shall not simultaneously have both internal and external linkage in the same translation unit. | C: conflicting linkage is ill-formed; R |
| 139 | will | External objects will not be declared in more than one file. | R; the linker for conflicting definitions |
| 140 | shall | The register storage class specifier shall not be used. | G: `register`; C: C++17 removed it |
| 141 | will | A class, structure, or enumeration will not be declared in the definition of its type. | R |
| 142 | shall | All variables shall be initialized before use. | T: `cppcoreguidelines-init-variables`; C: `-Wuninitialized` (in `-Wall`) |
| 143 | will | Variables will not be introduced until they can be initialized with meaningful values. | R |
| 144 | shall | Braces shall be used to indicate and match the structure in the non-zero initialization of arrays and structures. | C: `-Wmissing-braces` (Clang) |
| 145 | shall | In an enumerator list, the '=' construct shall not be used to explicitly initialize members other than the first, unless all items are explicitly initialized. | R |
| 146 | shall | Floating point implementations shall comply with a defined floating point standard. | `universal_types.h` asserts IEEE 754 at compile time |
| 147 | shall | The underlying bit representations of floating point numbers shall not be used in any way by the programmer. | R; no unions, and no pointer casts (T) |
| 148 | shall | Enumeration types shall be used instead of integer types (and constants) to select from a limited series of choices. | R |
| 149 | shall | Octal constants (other than zero) shall not be used. | G: octal constants |
| 150 | will | Hexadecimal constants will be represented using all uppercase letters. | G: lowercase hexadecimal digits |
| 151 | will | Numeric values in code will not be used; symbolic values will be used instead. | T: `readability-magic-numbers` (0, 1, 2, 0.5, 1.0 and 2.0 allowed) |
| 151.1 | shall | A string literal shall not be modified. | C: string literals are `const` arrays in C++ |
| 152 | shall | Multiple variable declarations shall not be allowed on the same line. | T: `readability-isolate-declaration` |
| 153 | shall | Unions shall not be used. | G: `union`; T: `cppcoreguidelines-pro-type-union-access` |
| 154 | shall | Bit-fields shall have explicitly unsigned integral or enumeration types only. | Q: no bit-fields at all |
| 155 | will | Bit-fields will not be used to pack data into a word for the sole purpose of saving space. | Q: no bit-fields at all |
| 156 | shall | All the members of a structure (or class) shall be named and shall only be accessed via their names. | C: `-Wpedantic` (anonymous structs are an extension) |
| 157 | shall | The right hand operand of a && or \|\| operator shall not contain side effects. | Q: increments and assignments on the right of `&&` and `\|\|`; R for calls |
| 158 | shall | The operands of a logical && or \|\| shall be parenthesized if the operands contain binary operators. | Q: each operand of `&&` and `\|\|` that holds a binary operator is parenthesized |
| 159 | shall | Operators \|\|, &&, and unary & shall not be overloaded. | Q: no overloaded `&&`, `\|\|` or `&` |
| 160 | shall | An assignment expression shall be used only as the expression in an expression statement. | Q: assignments outside expression statements; T: `bugprone-assignment-in-if-condition` |
| 162 | shall | Signed and unsigned values shall not be mixed in arithmetic or comparison operations. | C: `-Wsign-conversion`, `-Wsign-compare` |
| 163 | shall | Unsigned arithmetic shall not be used. | Q: unsigned arithmetic |
| 164 | shall | The right hand operand of a shift operator shall lie between zero and one less than the width in bits of the left-hand operand (inclusive). | T: `hicpp-signed-bitwise` (shifts of unsigned values only); R for the shift count |
| 164.1 | shall | The left-hand operand of a right-shift operator shall not have a negative value. | T: `hicpp-signed-bitwise` |
| 165 | shall | The unary minus operator shall not be applied to an unsigned expression. | Q: unary minus on unsigned values |
| 166 | will | The sizeof operator will not be used on expressions that contain side effects. | T: `bugprone-sizeof-expression`; R |
| 167 | shall | The implementation of integer division in the chosen compiler shall be determined, documented and taken into account. | Documented below |
| 168 | shall | The comma operator shall not be used. | C: `-Wcomma` (Clang) |
| 169 | should | Pointers to pointers should be avoided when possible. | R (D-8 for Aravis and GLib) |
| 170 | shall | More than 2 levels of pointer indirection shall not be used. | Q: three levels of pointer indirection |
| 171 | shall | Relational operators shall not be applied to pointers, except to pointers of the same type into the same object, function or array. | Q: no relational operators on pointers at all |
| 173 | shall | The address of an object with automatic storage shall not be assigned to an object which persists after the object has ceased to exist. | R; ASan's stack-use-after-return in CI |
| 174 | shall | The null pointer shall not be de-referenced. | T: `clang-analyzer-core.NullDereference`; ASan in CI |
| 175 | shall | A pointer shall not be compared to NULL or be assigned NULL; use plain 0 instead. | D-2: `nullptr`; T: `modernize-use-nullptr` |
| 176 | will | A typedef will be used to simplify program syntax when declaring function pointers. | R |
| 177 | should | User-defined conversion functions should be avoided. | T: `google-explicit-constructor` |
| 178 | shall | Down casting shall only be done through virtual functions that act like dynamic casts, or the visitor pattern. | Q: no `dynamic_cast`; R for `static_cast` |
| 179 | shall | A pointer to a virtual base class shall not be converted to a pointer to a derived class. | Q: no virtual bases at all |
| 180 | shall | Implicit conversions that may result in a loss of information shall not be used. | C: `-Wconversion`; T: `cppcoreguidelines-narrowing-conversions` |
| 181 | will | Redundant explicit casts will not be used. | C: `-Wuseless-cast` (GCC) |
| 182 | shall | Type casting from any type to or from pointers shall not be used. | T: `cppcoreguidelines-pro-type-reinterpret-cast` |
| 183 | should | Every possible measure should be taken to avoid type casting. | T: `cppcoreguidelines-pro-type-const-cast`; C: `-Wcast-qual` |
| 184 | shall | Floating point numbers shall not be converted to integers unless such a conversion is a specified algorithmic requirement or is necessary for a hardware interface. | T: `cppcoreguidelines-narrowing-conversions`; R |
| 185 | shall | C++ style casts (const_cast, reinterpret_cast, and static_cast) shall be used instead of the traditional C-style casts. | C: `-Wold-style-cast`; T: `cppcoreguidelines-pro-type-cstyle-cast` |
| 186 | shall | There shall be no unreachable code. | C: `-Wunreachable-code-aggressive` (Clang) |
| 187 | shall | All non-null statements shall potentially have a side-effect. | C: `-Wunused-value` (in `-Wall`); T: `clang-analyzer-deadcode.DeadStores` |
| 188 | will | Labels will not be used, except in switch statements. | Q: labels |
| 189 | shall | The goto statement shall not be used. | G: `goto`; T: `cppcoreguidelines-avoid-goto` |
| 190 | shall | The continue statement shall not be used. | G: `continue` |
| 191 | shall | The break statement shall not be used (except to terminate the cases of a switch statement). | G: `break` out of a loop |
| 192 | will | All if, else if constructs will contain either a final else clause or a comment indicating why a final else clause is not necessary. | T: `hicpp-multiway-paths-covered`, which wants a final `else` every time |
| 193 | shall | Every non-empty case clause in a switch statement shall be terminated with a break statement. | C: `-Wimplicit-fallthrough` |
| 194 | shall | All switch statements that do not intend to test for every enumeration value shall contain a final default clause. | T: `bugprone-switch-missing-default-case`; C: `-Wswitch` (in `-Wall`) |
| 195 | will | A switch expression will not represent a Boolean value. | C: `-Wswitch-bool`, on by default |
| 196 | will | Every switch statement will have at least two cases and a potential default. | T: `hicpp-multiway-paths-covered` |
| 197 | shall | Floating point variables shall not be used as loop counters. | T: `cert-flp30-c` |
| 198 | will | The initialization expression in a for loop will perform no actions other than to initialize the value of a single for loop parameter. | R |
| 199 | will | The increment expression in a for loop will perform no action other than to change a single loop parameter to the next value for the loop. | R |
| 200 | will | Null initialize or increment expressions in for loops will not be used; a while loop will be used instead. | Q: `for` loops without an initialization or an increment |
| 201 | shall | Numeric variables being used within a for loop for iteration counting shall not be modified in the body of the loop. | Q: a `for` loop's counter changed in its body |
| 202 | shall | Floating point variables shall not be tested for exact equality or inequality. | C: `-Wfloat-equal` |
| 203 | shall | Evaluation of expressions shall not lead to overflow/underflow (unless required algorithmically and then should be heavily documented). | UBSan in CI (signed overflow); T: `clang-analyzer-core.DivideZero`; R |
| 204 | shall | A single operation with side effects shall be used only by itself, as an assignment's right side, as a condition, as the one such argument of a call, or as one part of a chained operation. | R; C: `-Wunsequenced` (Clang) and `-Wsequence-point` (GCC) |
| 204.1 | shall | The value of an expression shall be the same under any order of evaluation that the standard permits. | C: `-Wunsequenced` (Clang) and `-Wsequence-point` (GCC); R |
| 205 | shall | The volatile keyword shall not be used unless directly interfacing with hardware. | G: `volatile` |
| 206 | shall | Allocation/deallocation from/to the free store (heap) shall not occur after initialization. | The allocation guard (ICS-015) in each module's tests; T: `cppcoreguidelines-no-malloc`, `-owning-memory`; C: `-Wvla` |
| 207 | will | Unencapsulated global data will be avoided. | T: `cppcoreguidelines-avoid-non-const-global-variables` |
| 208 | shall | C++ exceptions shall not be used (i.e. throw, catch and try shall not be used.) | G: `throw`, `try` and `catch` |
| 209 | shall | The basic types int, short, long, float and double shall not be used; fixed-length typedefs are used in their place. | G: `int`, `short`, `long`, `float`, `double`, `signed` and `unsigned` outside `universal_types.h` (D-7 for `main`) |
| 210 | shall | Algorithms shall not assume how data is represented in memory (endianness, or the order of subobjects or members). | R; no unions and no pointer casts (T) |
| 210.1 | shall | Algorithms shall not make assumptions concerning the order of allocation of nonstatic data members separated by an access specifier. | R |
| 211 | shall | Algorithms shall not assume that shorts, ints, longs, floats, doubles or long doubles begin at particular addresses. | R; no pointer casts (T) |
| 212 | shall | Underflow or overflow functioning shall not be depended on in any special way. | R; with no unsigned arithmetic (AV 163), nothing relies on wrapping |
| 213 | shall | No dependence shall be placed on C++'s operator precedence rules, below arithmetic operators, in expressions. | C: `-Wparentheses` (in `-Wall`); Q for `&&` and `\|\|`; R |
| 214 | shall | Assuming that non-local static objects, in separate translation units, are initialized in a special order shall not be done. | R |
| 215 | will | Pointer arithmetic will not be used. | T: `cppcoreguidelines-pro-bounds-pointer-arithmetic` |
| 216 | should | Programmers should not attempt to prematurely optimize code. | R |
| 217 | should | Compile-time and link-time errors should be preferred over run-time errors. | R |
| 218 | will | Compiler warning levels will be set in compliance with project policies. | `cpp/cmake/Warnings.cmake` and `cpp/cmake/Jsf.cmake` |
| 219 | shall | All tests applied to a base class interface shall be applied to all derived class interfaces as well. | R; the coverage gate |
| 220 | shall | Structural coverage algorithms shall be applied against flattened classes. | R; the coverage gate |
| 221 | shall | Structural coverage of a class in a hierarchy with virtual functions shall test every resolution of each polymorphic reference. | R; the coverage gate |

## Deviations

AV 4 and AV 5 ask for approval to break a rule. The repository's code owner holds both roles they name, software engineering lead and software product manager. The owner approves a deviation in the task card of the issue that needs it, or by merging the pull request that adds it to this register.

AV 6 asks for each deviation to be documented in the file that contains it:

- A deviation in a single file is cited there, beside the deviation, as `// JSF deviation D-<n>: <why>`.
- A project-wide deviation applies to every JSF file. It is cited once, where it is configured.

AV 7 needs no approval for what a rule's own exception allows. Two such cases come up often:

- AV 50's exception lets the universal types be named in lowercase, like the fundamental types they replace.
- AV 113's exception allows more than one exit when a single exit would obscure or complicate a function's logic, for example by adding variables. A guard clause that returns early is the usual case.

| ID | Rules | Deviation | Why | Cited | Approved |
|---|---|---|---|---|---|
| D-1 | AV 8 | C++20 (ISO/IEC 14882:2020), not ISO/IEC 14882:2002 | The toolchain, the dependencies and every ICS module are C++20. The C++ since 2002 brings `std::span`, `<chrono>`, `[[nodiscard]]` and `static_assert`, which ICS uses to keep AV 97, 25, 115 and 146 | `cpp/CMakeLists.txt`, at `CMAKE_CXX_STANDARD` | ICS-037 task card, 2026-10-17 |
| D-2 | AV 175 | `nullptr`, not `0`, for null pointers | `nullptr` does not convert to an integer, so it avoids the confusion AV 175 guards against better than `0` does | `cpp/policy/jsf-clang-tidy.yaml` (`modernize-use-nullptr`) | ICS-037 task card, 2026-10-17 |
| D-3 | AV 16 | Libraries without DO-178B level A or SEAL 1 certification: the C++ standard library and the libraries in the dependency register | No certifiable builds of these libraries are available to ICS. Each is pinned and reviewed in the register, and runs under ICS's tests, sanitizers and fuzzing | [`docs/dependency-register.toml`](dependency-register.toml) | ICS-037 task card, 2026-10-17 |
| D-4 | AV 50 | Namespace names in `lower_case` (`ics`, `ics::cue`) | They match the C++ standard library and every module written before ICS-037, which JSF code calls | `cpp/policy/jsf-clang-tidy.yaml` (`NamespaceCase`); the gate's AV 50 matchers check only types | ICS-037 task card, 2026-10-17 |
| D-5 | AV 24, 29, 50, 51, 97, 182, 209 | In test and fuzz code: what GoogleTest's `TEST` and `TEST_F` macros and libFuzzer's `LLVMFuzzerTestOneInput` expand to or require, and `std::abort` to report a fuzz finding | The frameworks fix these names, signatures and expansions | Once in each test or fuzz file, before its first test or its entry point | ICS-037 task card, 2026-10-17 |
| D-6 | AV 32, 53 | `#include` of the `.hpp` headers of modules written before ICS-037 | JSF code calls those modules (`ics::Result`, logging, config) until [#170](https://github.com/MatthewK84/ICS/issues/170) renames their headers. The gate allows only `ics/<module>/<name>.hpp` | Once in each file, beside its includes | By merging the JSF gate pull request |
| D-7 | AV 97, 209 | `main`'s signature, `int main(int argc, char* argv[])` | The C++ standard fixes it | Beside `main` | By merging the JSF gate pull request |
| D-8 | AV 169 | Aravis and GLib's C API, whose calls report errors through a `GError**` out-parameter | Aravis is the GigE Vision library the tracker streams its camera through ([register](dependency-register.toml)), and its API is C. Each object it returns is owned by a `std::unique_ptr` that calls `g_object_unref`; each error is read and cleared where it is returned; no Aravis or GLib macro is used; and frames stream into buffers ICS allocates, so no pointer is cast | `cpp/tracker/src/aravis_camera.cpp` only, beside its includes | ICS-038 task card, 2026-10-19 |

## Integer division (AV 167)

ICS builds with GCC and Clang 17 in the `ics-cpp` image. In C++11 and later, which includes C++20 (D-1), integer division is defined by the standard, not by the compiler:

- `a / b` truncates toward zero: `7 / 2 == 3` and `-7 / 2 == -3`.
- `a % b` takes the sign of the dividend `a`: `-7 % 2 == -1` and `7 % -2 == 1`.
- `(a / b) * b + a % b == a` whenever `a / b` is representable.
- Division by zero is undefined behaviour, and so is `INT32_MIN / -1` (and `INT64_MIN / -1`), whose result does not fit. UBSan in CI stops on both. The code must rule out both cases before dividing.

Code that needs floor division, or a modulus that is never negative (for example, to wrap an index or an angle), writes it out and says so, rather than relying on `/` or `%`.

## Review checklist

The reviewer of a pull request that touches JSF code checks these rules. Where a check appears in the table above too, the reviewer checks only the part the tools cannot, such as AV 48's look-alike names beyond those `misc-confusable-identifiers` finds.

Mandatory (shall and will):

- **AV 2** (shall). There shall not be any self-modifying code.
- **AV 10** (will). Values of character types will be restricted to a defined and documented subset of ISO 10646-1.
- **AV 44** (will). All indentations will be at least two spaces and be consistent within the same source file.
- **AV 48** (will). Identifiers will not differ only by case, an underscore, or the interchange of O, 0 and D; I, 1 and l; S and 5; Z and 2; or n and h.
- **AV 49** (will). All acronyms in an identifier will be composed of uppercase letters.
- **AV 62** (will). The dereference operator '*' and the address-of operator '&' will be directly connected with the type-specifier.
- **AV 63** (will). Spaces will not be used around '.' or '->', nor between unary operators and operands.
- **AV 70** (will). A class will have friends only when a function or object needs its private elements but cannot be a member.
- **AV 70.1** (shall). An object shall not be improperly used before its lifetime begins or after its lifetime ends.
- **AV 71** (shall). Calls to an externally visible operation of an object, other than its constructors, shall not be allowed until the object has been fully initialized.
- **AV 73** (shall). Unnecessary default constructors shall not be defined.
- **AV 77** (shall). A copy constructor shall copy all data members and bases that affect the class invariant (a data element representing a cache, for example, would not need to be copied).
- **AV 77.1** (shall). A member function shall not have default arguments that give it the signature of the implicit copy constructor.
- **AV 79** (shall). All resources acquired by a class shall be released by the class's destructor.
- **AV 80** (will). The default copy and assignment operators will be used for classes when those operators offer reasonable semantics.
- **AV 83** (shall). An assignment operator shall assign all data members and bases that affect the class invariant.
- **AV 84** (will). Operator overloading will be used sparingly and in a conventional manner.
- **AV 85** (will). When two operators are opposites (such as == and !=), both will be defined and one will be defined in terms of the other.
- **AV 88** (shall). Multiple inheritance shall only be allowed in the following restricted form: n interfaces plus m private implementations, plus at most one protected implementation.
- **AV 91** (will). Public inheritance will be used to implement "is-a" relationships.
- **AV 92** (will). A subtype will have preconditions no stronger, and postconditions no weaker, than those of the methods it overrides.
- **AV 93** (will). "has-a" or "is-implemented-in-terms-of" relationships will be modeled through membership or non-public inheritance.
- **AV 94** (shall). An inherited nonvirtual function shall not be redefined in a derived class.
- **AV 96** (shall). Arrays shall not be treated polymorphically.
- **AV 97.1** (shall). Neither operand of an equality operator (== or !=) shall be a pointer to a virtual member function.
- **AV 101** (shall). Templates shall be reviewed alone, for what they require of their arguments, and for each function their actual arguments instantiate.
- **AV 102** (shall). Template tests shall be created to cover all actual template instantiations.
- **AV 113** (will). Functions will have a single exit point.
- **AV 127** (shall). Code that is not used (commented out) shall be deleted.
- **AV 128** (will). Comments that document actions or sources (e.g. tables, figures, paragraphs, etc.) outside of the file being documented will not be allowed.
- **AV 132** (will). Each variable declaration, typedef, enumeration value, and structure member will be commented.
- **AV 138** (shall). Identifiers shall not simultaneously have both internal and external linkage in the same translation unit.
- **AV 139** (will). External objects will not be declared in more than one file.
- **AV 141** (will). A class, structure, or enumeration will not be declared in the definition of its type.
- **AV 143** (will). Variables will not be introduced until they can be initialized with meaningful values.
- **AV 145** (shall). In an enumerator list, the '=' construct shall not be used to explicitly initialize members other than the first, unless all items are explicitly initialized.
- **AV 147** (shall). The underlying bit representations of floating point numbers shall not be used in any way by the programmer.
- **AV 148** (shall). Enumeration types shall be used instead of integer types (and constants) to select from a limited series of choices.
- **AV 157** (shall). The right hand operand of a && or || operator shall not contain side effects.
- **AV 164** (shall). The right hand operand of a shift operator shall lie between zero and one less than the width in bits of the left-hand operand (inclusive).
- **AV 166** (will). The sizeof operator will not be used on expressions that contain side effects.
- **AV 173** (shall). The address of an object with automatic storage shall not be assigned to an object which persists after the object has ceased to exist.
- **AV 176** (will). A typedef will be used to simplify program syntax when declaring function pointers.
- **AV 178** (shall). Down casting shall only be done through virtual functions that act like dynamic casts, or the visitor pattern.
- **AV 184** (shall). Floating point numbers shall not be converted to integers unless such a conversion is a specified algorithmic requirement or is necessary for a hardware interface.
- **AV 198** (will). The initialization expression in a for loop will perform no actions other than to initialize the value of a single for loop parameter.
- **AV 199** (will). The increment expression in a for loop will perform no action other than to change a single loop parameter to the next value for the loop.
- **AV 203** (shall). Evaluation of expressions shall not lead to overflow/underflow (unless required algorithmically and then should be heavily documented).
- **AV 204** (shall). A single operation with side effects shall be used only by itself, as an assignment's right side, as a condition, as the one such argument of a call, or as one part of a chained operation.
- **AV 204.1** (shall). The value of an expression shall be the same under any order of evaluation that the standard permits.
- **AV 210** (shall). Algorithms shall not assume how data is represented in memory (endianness, or the order of subobjects or members).
- **AV 210.1** (shall). Algorithms shall not make assumptions concerning the order of allocation of nonstatic data members separated by an access specifier.
- **AV 211** (shall). Algorithms shall not assume that shorts, ints, longs, floats, doubles or long doubles begin at particular addresses.
- **AV 212** (shall). Underflow or overflow functioning shall not be depended on in any special way.
- **AV 213** (shall). No dependence shall be placed on C++'s operator precedence rules, below arithmetic operators, in expressions.
- **AV 214** (shall). Assuming that non-local static objects, in separate translation units, are initialized in a special order shall not be done.
- **AV 219** (shall). All tests applied to a base class interface shall be applied to all derived class interfaces as well.
- **AV 220** (shall). Structural coverage algorithms shall be applied against flattened classes.
- **AV 221** (shall). Structural coverage of a class in a hierarchy with virtual functions shall test every resolution of each polymorphic reference.

Advisory (should):

- **AV 34** (should). Header files should contain logically related declarations only.
- **AV 36** (should). Compilation dependencies should be minimized when possible.
- **AV 38** (should). Declarations of classes that are only accessed via pointers (*) or references (&) should be supplied by forward headers that contain only forward declarations.
- **AV 55** (should). The name of a header file should reflect the logical entity for which it provides declarations.
- **AV 56** (should). An implementation file's name should reflect the logical entity it defines, and end in ".cpp".
- **AV 64** (should). A class interface should be complete and minimal.
- **AV 65** (should). A structure should be used to model an entity that does not require an invariant.
- **AV 66** (should). A class should be used to model an entity that maintains an invariant.
- **AV 67** (should). Public and protected data should only be used in structs, not classes.
- **AV 72** (should). A class's invariant should be part of each constructor's postcondition, the destructor's precondition, and each other public operation's pre- and postconditions.
- **AV 86** (should). Concrete types should be used to represent simple independent concepts.
- **AV 87** (should). Hierarchies should be based on abstract classes.
- **AV 90** (should). Heavily used interfaces should be minimal, general and abstract.
- **AV 98** (should). Every nonlocal name, except main(), should be placed in some namespace.
- **AV 100** (should). Namespace elements should be selected by a using declaration or explicit qualification for a few names, and a using directive for many.
- **AV 103** (should). Constraint checks should be applied to template arguments.
- **AV 105** (should). A template definition's dependence on its instantiation contexts should be minimized.
- **AV 106** (should). Specializations for pointer types should be made where appropriate.
- **AV 109** (should). A function definition should not be placed in a class specification unless the function is intended to be inlined.
- **AV 112** (should). Function return values should not obscure resource ownership.
- **AV 116** (should). Small concrete-type arguments (two or three words) should be passed by value when changes to them should not reach the caller.
- **AV 117** (should). Arguments should be passed by reference if NULL values are not possible.
- **AV 118** (should). Arguments should be passed via pointers if NULL values are possible.
- **AV 120** (should). Overloaded operations or methods should form families with the same semantics, name and purpose, differing only in their parameters.
- **AV 121** (should). Only functions with 1 or 2 statements should be considered candidates for inline functions.
- **AV 122** (should). Trivial accessor and mutator functions should be inlined.
- **AV 123** (should). The number of accessor and mutator functions should be minimized.
- **AV 124** (should). Trivial forwarding functions should be inlined.
- **AV 125** (should). Unnecessary temporary objects should be avoided.
- **AV 129** (should). Comments in header files should describe the externally visible behavior of the functions or classes being documented.
- **AV 130** (should). The purpose of every line of executable code should be explained by a comment, although one comment may describe more than one line of code.
- **AV 131** (should). One should avoid stating in comments what is better stated in code (i.e. do not simply repeat what is in the code).
- **AV 134** (should). Assumptions (limitations) made by functions should be documented in the function's preamble.
- **AV 136** (should). Declarations should be at the smallest feasible scope.
- **AV 169** (should). Pointers to pointers should be avoided when possible.
- **AV 216** (should). Programmers should not attempt to prematurely optimize code.
- **AV 217** (should). Compile-time and link-time errors should be preferred over run-time errors.
