#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

"""Fail-closed validators for the CodeChecker wrapper (static-analyzer.sh).

Subcommands, each reading a file the wrapper produced:

  manifest <unique_compile_commands.json>
      Exit 0 if the analysis scheduled at least one action (a non-empty JSON list), 1 otherwise. CodeChecker writes
      this file only when it has work to do, so its absence or emptiness marks a 0%-coverage run.

  metadata <metadata.json> <expected_analyzer>
      Exit 0 if the requested analyzer ran with no failures and at least one success, 1 otherwise (reason on stderr).
      Keyed on the analyzer this job requested, so a metadata that names a different analyzer, duplicates the analyzer
      across tools[] (hiding a failure via last-writer-wins), or records a failed/zero-execution run is rejected.

  report <code-quality-report.json>
      Validate the GitLab Code Quality report and look for major-or-higher findings. Disjoint exit codes that skip 1, so
      an unhandled exception (Python exit 1) can never be mistaken for a "valid, fatal" report and published:
        0  valid, no fatal findings
        10 valid, at least one major/critical/blocker finding
        20 report does not match the GitLab Code Quality schema
        30 validator internal error (fail closed)
"""

import json
import os
import sys

EXIT_CLEAN = 0
EXIT_FATAL = 10
EXIT_SCHEMA_INVALID = 20
EXIT_INTERNAL_ERROR = 30


def _load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _nonneg_int(value, what):
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError("non-negative-integer %s expected, got %r" % (what, value))
    return value


def cmd_manifest(path):
    try:
        data = _load(path)
    except (OSError, ValueError):
        return 1
    return 0 if isinstance(data, list) and data else 1


def cmd_metadata(path, expected):
    def die(msg):
        print("ERROR: incomplete CodeChecker analysis: " + msg, file=sys.stderr)
        return 1

    if not expected:
        return die("no expected analyzer given; cannot verify completeness")
    try:
        meta = _load(path)
    except (OSError, ValueError) as error:
        return die("metadata.json not readable as JSON (%s)" % error)

    tools = meta.get("tools")
    if not isinstance(tools, list) or not tools:
        return die("metadata.tools is not a non-empty list")

    records = []  # analyzer_statistics for each tools[] entry that ran the requested analyzer
    try:
        for tool in tools:
            if not isinstance(tool, dict):
                return die("metadata.tools entry is not an object")
            # action_num is validated for shape but deliberately NOT compared to successful + failed. Under --ctu,
            # CodeChecker keeps the skip-listed actions in action_num (CTU pre-analysis needs the full set) but skips
            # them in the analysis itself, so successful + failed < action_num on a healthy run. Incomplete runs are
            # caught by the failed/ directory and the analyze exit code, not here.
            _nonneg_int(tool.get("action_num"), "action_num")
            analyzers = tool.get("analyzers")
            if not isinstance(analyzers, dict):
                return die("metadata.tools entry has no analyzers object")
            if expected in analyzers:
                rec = analyzers[expected]
                stats = rec.get("analyzer_statistics") if isinstance(rec, dict) else None
                if not isinstance(stats, dict):
                    return die("%s record has no analyzer_statistics object" % expected)
                records.append(stats)

        if len(records) != 1:
            return die("expected exactly one %r analyzer record, found %d" % (expected, len(records)))

        stats = records[0]
        successful = _nonneg_int(stats.get("successful"), "successful")
        failed = _nonneg_int(stats.get("failed"), "failed")
    except ValueError as error:
        return die(str(error))

    if failed != 0:
        return die("%s reported %d failed action(s)" % (expected, failed))
    if successful == 0:
        return die("%s executed zero actions" % expected)
    return 0


def cmd_report(path):
    # Test-only fault injection: force the internal-error path to prove any unexpected exception fails closed as 30
    # (never a silent success or Python's exit 1). Unset in production; if ever set it only makes the gate fail closed.
    if os.environ.get("CC_TEST_FORCE_INTERNAL_ERROR"):
        raise RuntimeError("forced validator error (test hook)")

    def invalid(message):
        print("ERROR: invalid Code Quality report: " + message, file=sys.stderr)
        raise SystemExit(EXIT_SCHEMA_INVALID)

    def reject_constant(value):
        # json.load() accepts NaN/Infinity/-Infinity by default; the GitLab Code Quality parser and RFC 8259 do not.
        raise ValueError("non-standard JSON constant %r" % value)

    try:
        with open(path, encoding="utf-8") as report_file:
            findings = json.load(report_file, parse_constant=reject_constant)
    except (OSError, ValueError) as error:
        invalid("not readable as JSON (%s)" % error)

    if not isinstance(findings, list):
        invalid("root is not a JSON array")

    allowed = {"info", "minor", "major", "critical", "blocker"}
    fatal = {"major", "critical", "blocker"}
    has_fatal = False

    for index, finding in enumerate(findings):
        if not isinstance(finding, dict):
            invalid("finding %d is not an object" % index)
        for field in ("description", "check_name", "fingerprint"):
            if not isinstance(finding.get(field), str) or not finding[field]:
                invalid("finding %d has an invalid %s" % (index, field))
        severity = finding.get("severity")
        # isinstance guard first: a non-string severity (list, object, number, bool, null) is not a valid GitLab
        # severity, and "severity not in allowed" would raise TypeError on an unhashable list/dict.
        if not isinstance(severity, str) or severity not in allowed:
            invalid("finding %d has an invalid severity %r" % (index, severity))
        location = finding.get("location")
        if not isinstance(location, dict):
            invalid("finding %d has no location object" % index)
        path_value = location.get("path")
        if not isinstance(path_value, str) or not path_value:
            invalid("finding %d has an invalid location.path" % index)
        # GitLab requires a repo-relative path not prefixed with "./".
        if path_value.startswith("/") or path_value.startswith("./") or ".." in path_value.split("/"):
            invalid("finding %d has a non-repo-relative location.path %r" % (index, path_value))
        lines = location.get("lines")
        begin = lines.get("begin") if isinstance(lines, dict) else None
        if begin is None:
            positions = location.get("positions")
            begin_pos = positions.get("begin") if isinstance(positions, dict) else None
            begin = begin_pos.get("line") if isinstance(begin_pos, dict) else None
        if not isinstance(begin, int) or isinstance(begin, bool) or begin < 1:
            invalid("finding %d has an invalid begin line" % index)
        if severity in fatal:
            has_fatal = True

    raise SystemExit(EXIT_FATAL if has_fatal else EXIT_CLEAN)


def main(argv):
    if len(argv) < 2:
        print("usage: validate_analysis.py {manifest|metadata|report} <file> [analyzer]", file=sys.stderr)
        return EXIT_INTERNAL_ERROR
    sub = argv[1]
    try:
        if sub == "manifest":
            return cmd_manifest(argv[2])
        if sub == "metadata":
            return cmd_metadata(argv[2], argv[3] if len(argv) > 3 else os.environ.get("ANALYZER", ""))
        if sub == "report":
            cmd_report(argv[2])  # raises SystemExit with the tri-state code
            return EXIT_INTERNAL_ERROR  # unreachable
        print("ERROR: unknown subcommand %r" % sub, file=sys.stderr)
        return EXIT_INTERNAL_ERROR
    except SystemExit:
        raise
    except BaseException as error:  # fail closed: any unexpected error is an internal error, never a silent success
        print("ERROR: validate_analysis internal error: %r" % error, file=sys.stderr)
        return EXIT_INTERNAL_ERROR


if __name__ == "__main__":
    sys.exit(main(sys.argv))
