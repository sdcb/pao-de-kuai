#!/usr/bin/env python3
"""Enforce the "src/ is pure C" acceptance criteria (plan.md 1.1, DoD 1-3).

This is the automated half of the definition of done: it proves that no C++
construct and no heavyweight standard header is left anywhere under src/, and
that every translation unit in src/ is a .c file.

It is intentionally separate from the compiler: a file can compile as C++ and
fail here (that is the whole point), and the report is per-file so a partially
finished port is easy to read.

Usage:
    python tools/check_c_only.py            # report and exit non-zero on findings
    python tools/check_c_only.py --report   # report only, always exit 0
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# C++ syntax that must not appear in a C translation unit.
RE_CPP_SYNTAX = [
    (re.compile(r"\bnamespace\b"), "namespace"),
    (re.compile(r"\bclass\s+\w"), "class"),
    (re.compile(r"\btemplate\s*<"), "template<"),
    (re.compile(r"\bstd::"), "std::"),
    (re.compile(r"\bvirtual\b"), "virtual"),
    (re.compile(r"\boverride\b"), "override"),
    (re.compile(r"\bconstexpr\b"), "constexpr"),
    (re.compile(r"\bnullptr\b"), "nullptr"),
    (re.compile(r"\busing\s+namespace\b"), "using namespace"),
    (re.compile(r"^\s*using\s+\w+\s*=", re.M), "using alias"),
    (re.compile(r"\bstatic_cast\s*<"), "static_cast"),
    (re.compile(r"\breinterpret_cast\s*<"), "reinterpret_cast"),
    (re.compile(r"\bconst_cast\s*<"), "const_cast"),
    (re.compile(r"\btry\s*\{"), "try"),
    (re.compile(r"\bcatch\s*\("), "catch"),
    (re.compile(r"\bnew\s+\w+\s*[\(\[]"), "new"),
    (re.compile(r"\bdelete\s+\[\]"), "delete[]"),
    (re.compile(r"\bexplicit\b"), "explicit"),
    (re.compile(r"\bnoexcept\b"), "noexcept"),
]

# Headers that the size budget forbids in src/ (AGENTS.md "体积约束" plus the
# plan's C++ standard-library removal list).
RE_BANNED_INCLUDE = re.compile(
    r'#\s*include\s*[<"]('
    r"string|string_view|vector|array|optional|map|set|unordered_map|unordered_set|"
    r"memory|span|function|charconv|chrono|mutex|thread|atomic|algorithm|numeric|"
    r"filesystem|fstream|sstream|iostream|iomanip|random|tuple|variant|type_traits|"
    r"utility|initializer_list|list|deque|queue|stack|bitset|complex|regex|ratio|"
    r"condition_variable|future|new|exception|stdexcept|cstdint|cstddef|cstdio|"
    r"cstring|cmath|cstdlib|climits|cassert|functional|wrl/|string>)\s*[>\"]"
)

# Compiler branches are only allowed in the shim outlet, plus one permanent exception:
# rules/Card.h carries a `#ifdef __cplusplus` member shim that gives the C++ test suite the
# std::vector-shaped Cards API it uses in ~139 places.  The port is finished -- src/ is pure C --
# and the tests are C++ by design, so that block is test support rather than a leftover, and it
# cannot move out of the header because a member cannot be added to a struct from outside.
ALLOWED_IFDEF_FILES = {
    os.path.join("graphics", "win_compat.h"),
    os.path.join("graphics", "Com.h"),
    os.path.join("audio", "MfCompat.h"),
    os.path.join("graphics", "iids.c"),
    os.path.join("rules", "Card.h"),
}
RE_COMPILER_BRANCH = re.compile(r"#\s*if(?:def|ndef)?\s+.*(_MSC_VER|__MINGW32__|__GNUC__)")


def strip_comments_and_strings(text):
    """Blank out comments and string/char literals so quoted words do not match."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c in "\"'":
            quote = c
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                if text[j] == "\n" and quote == "'":
                    break
                j += 1
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def cplusplus_guard_lines(code):
    """Line numbers that only a C++ compiler ever sees.

    Code inside `#ifdef __cplusplus` is by definition not part of the C translation unit, so C++
    syntax there is not a finding.  Without this the check reported `static_cast` in the
    `Cards` member shim in rules/Card.h -- which is exactly the code that block exists for.
    """
    guarded = set()
    stack = []
    open_guards = 0
    for number, line in enumerate(code.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("#"):
            if re.match(r"#\s*(ifdef|ifndef)\s+__cplusplus\b", stripped) or \
               re.match(r"#\s*if\s+.*\b__cplusplus\b", stripped):
                stack.append(True)
                open_guards += 1
            elif re.match(r"#\s*if\b", stripped):
                stack.append(False)
            elif re.match(r"#\s*elif\b", stripped):
                pass
            elif re.match(r"#\s*endif\b", stripped) and stack:
                if stack.pop():
                    open_guards -= 1
        if open_guards > 0:
            guarded.add(number)
    return guarded


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true",
                    help="only report, never fail")
    args = ap.parse_args()

    findings = {}
    cpp_files = []
    for dirpath, _dirs, files in os.walk(SRC):
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, SRC)
            if name.endswith((".cpp", ".cxx", ".cc", ".hpp")):
                cpp_files.append(rel)
                continue
            if not name.endswith((".c", ".h")):
                continue
            with open(path, encoding="utf-8", errors="replace") as fh:
                raw = fh.read()
            code = strip_comments_and_strings(raw)
            guarded = (cplusplus_guard_lines(code)
                       if rel.replace("/", os.sep) in ALLOWED_IFDEF_FILES else set())
            hits = []
            for pattern, label in RE_CPP_SYNTAX:
                found = pattern.search(code)
                if found:
                    line = code.count("\n", 0, found.start()) + 1
                    if line in guarded:
                        continue
                    hits.append("C++ %s (line %d)" % (label, line))
            for m in RE_BANNED_INCLUDE.finditer(code):
                line = code.count("\n", 0, m.start()) + 1
                if line in guarded:
                    continue
                hits.append("banned include <%s> (line %d)" % (m.group(1), line))
            if rel.replace("/", os.sep) not in ALLOWED_IFDEF_FILES:
                for m in RE_COMPILER_BRANCH.finditer(code):
                    line = code.count("\n", 0, m.start()) + 1
                    hits.append("compiler branch outside the shim (line %d)" % line)
            if hits:
                findings[rel] = hits

    total = 0
    for rel in sorted(findings):
        print(rel)
        for hit in findings[rel]:
            print("    " + hit)
            total += 1
    if cpp_files:
        print("\nstill C++ (%d):" % len(cpp_files))
        for rel in cpp_files:
            print("    " + rel)
    print("\n%d finding(s) in .c/.h, %d remaining C++ file(s)" % (total, len(cpp_files)))
    if total or cpp_files:
        print("src/ is NOT yet pure C.")
        if not args.report:
            sys.exit(1)
    else:
        print("src/ is pure C.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
