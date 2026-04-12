#!/usr/bin/env python3
"""
Convert Unity test output to JUnit XML.

Usage:
    python3 unity_to_junit.py <input_file> <output_file>

Unity output format (one line per test):
    filename.c:line:test_name:PASS
    filename.c:line:test_name:FAIL:message

Groups tests into <testsuite> elements by source filename.
"""

import sys
import re
from collections import defaultdict
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent


def parse_unity_output(text):
    """Parse Unity stdout and return suites dict keyed by suite name."""
    suites = defaultdict(lambda: {"tests": [], "failures": 0})

    for line in text.splitlines():
        m = re.match(r"^([^:\s]+\.c):(\d+):(\w+):(PASS|FAIL)(.*)$", line)
        if not m:
            continue
        source, lineno, test_name, result, rest = m.groups()
        suite_name = source.rsplit("/", 1)[-1].removesuffix(".c")
        message = rest.lstrip(":") if rest else ""

        suites[suite_name]["tests"].append(
            {
                "name": test_name,
                "result": result,
                "message": message,
                "source": source,
                "line": lineno,
            }
        )
        if result == "FAIL":
            suites[suite_name]["failures"] += 1

    return suites


def build_junit_xml(suites):
    """Build a <testsuites> ElementTree from the parsed suites dict."""
    testsuites = Element("testsuites")

    for suite_name, data in suites.items():
        tests = data["tests"]
        ts = SubElement(
            testsuites,
            "testsuite",
            name=suite_name,
            tests=str(len(tests)),
            failures=str(data["failures"]),
            errors="0",
            time="0",
        )
        for t in tests:
            tc = SubElement(
                ts,
                "testcase",
                name=t["name"],
                classname=suite_name,
                time="0",
            )
            if t["result"] == "FAIL":
                failure = SubElement(
                    tc,
                    "failure",
                    message=t["message"],
                    type="AssertionError",
                )
                failure.text = f"{t['source']}:{t['line']}: {t['message']}"

    return testsuites


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_file> <output_file>", file=sys.stderr)
        sys.exit(1)

    input_file, output_file = sys.argv[1], sys.argv[2]

    with open(input_file) as f:
        text = f.read()

    suites = parse_unity_output(text)

    if not suites:
        print("Warning: no test results found in input", file=sys.stderr)

    root = build_junit_xml(suites)
    indent(root)

    tree = ElementTree(root)
    tree.write(output_file, encoding="unicode", xml_declaration=True)

    total = sum(len(s["tests"]) for s in suites.values())
    failures = sum(s["failures"] for s in suites.values())
    print(f"Wrote {total} test results ({failures} failures) to {output_file}")


if __name__ == "__main__":
    main()
