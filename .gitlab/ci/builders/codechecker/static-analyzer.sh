#!/bin/bash

# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

# Print help and syntax
print_help() {
    echo "Script for running CodeChecker"
    echo
    echo "Syntax: static-analyzer.sh -args /folder"
    echo "args:"
    echo "  -h | --help                             - Print his message"
    echo "  --dryrun                                - Dry run. Parse previous analysis instead of running a new one"
    echo "  --jobs                                  - Number of parallel jobs for the analysis (default: number of CPU cores)"
    echo "  --cppcheck-max-template-recursion=<num> - Set the maximum template recursion for cppcheck (default: 100)"
    echo "  others                                  - Extra args will be sent to 'CodeChecker'"
    echo "folder:"
    echo "  Folder where source code is located. It should be the last parameter"
    echo ""
}

# Variables
FOLDER=""
DRYRUN=""
CPPCHECK_MAX_TEMPLATE=100
NUM_JOBS="$(nproc --all)"
CODECHECK_ARGS=""

# Begin the parsing
while (("$#")); do
    case "$1" in
    -h | --help)
        print_help
        exit 0
        ;;
    --dryrun)
        DRYRUN="true"
        shift 1
        ;;
    --jobs)
        NUM_JOBS="$2"
        shift 2
        ;;
    --cppcheck-max-template-recursion)
        CPPCHECK_MAX_TEMPLATE=$(echo "$2" | cut -d'=' -f 2)
        shift 2
        ;;
    *)
        CODECHECK_ARGS="$CODECHECK_ARGS $1"
        shift 1
        ;;

    esac
done

set -- ${CODECHECK_ARGS}
FOLDER="${*: -1}"
set -- "${@:1:$#-1}"

# Resolve this script's directory so its sibling helpers can be found whether it is invoked by path (bash
# /path/static-analyzer.sh, the repo checkout) or by bare name from PATH (the image's /usr/local/bin, where $0 has no
# directory). Prefer a sibling helper, fall back to PATH for tools shipped elsewhere (e.g. cppcheck_suppress).
self="$0"
[[ "$self" != */* ]] && self="$(command -v "$self")"
script_dir="$(cd "$(dirname "$self")" && pwd)"
resolve_helper() { if [[ -x "${script_dir}/$1" ]]; then echo "${script_dir}/$1"; else command -v "$1"; fi; }
validator="$(resolve_helper validate_analysis.py)"

# Setup
cd "$FOLDER" || exit
mkdir -p build
cd build || exit

# cppcheck workaround for codechecer 6.20.0
echo "<?xml version='1.0' encoding='UTF-8'?><project version='1'><max-template-recursion>${CPPCHECK_MAX_TEMPLATE}</max-template-recursion></project>" >/tmp/config.cppcheck
if ! grep -q "/tmp/config.cppcheck" /usr/local/lib/python3.10/dist-packages/codechecker_analyzer/analyzers/cppcheck/analyzer.py; then
    sed -i "s/# TODO implement/analyzer_cmd.append('--project=\/tmp\/config.cppcheck') # TODO implement/" /usr/local/lib/python3.10/dist-packages/codechecker_analyzer/analyzers/cppcheck/analyzer.py
fi

# Publish the Code Quality report from this main script (not an after_script step, whose failure or timeout GitLab
# ignores) via the atomic rename below, so publication succeeds or fails with the job.
report="../code-quality-report.json"
report_tmp="${report}.tmp"
rm -f -- "$report" "$report_tmp" || {
    echo "ERROR: failed to clear Code Quality output" >&2
    exit 1
}

# Build and run codechecker
if [[ -z $DRYRUN ]]; then
    if ! rm -Rf codechecker_output || [[ -e codechecker_output ]]; then
        echo "ERROR: failed to remove stale CodeChecker output" >&2
        exit 1
    fi
    CodeChecker analyzer-version
    CodeChecker analyze -j "$NUM_JOBS" compile_commands.json -o codechecker_output/ "$@"
    analyze_rc=$?
    "$(resolve_helper cppcheck_suppress_unspecified.py)" codechecker_output
    suppress_rc=$?
    # Reject a zero-action analysis: CodeChecker writes unique_compile_commands.json only once it has at least one
    # action to analyse, so its absence (empty or all-skipped compilation database) marks a 0%-coverage run.
    actions_rc=0
    python3 "$validator" manifest codechecker_output/unique_compile_commands.json || actions_rc=$?
    # Reject an incomplete analysis: a non-empty manifest only proves actions were scheduled, not that the requested
    # analyzer ran them. Verify metadata.json against $ANALYZER (exactly one record, no failures, all scheduled actions
    # executed). See validate_analysis.py.
    execution_rc=0
    python3 "$validator" metadata codechecker_output/metadata.json "$ANALYZER" || execution_rc=$?
else
    analyze_rc=0
    suppress_rc=0
    actions_rc=0
    execution_rc=0
    echo "----======== Parsing Previous Analysis ========----"
fi

# Fail closed on an incomplete CodeChecker run *before* any Code Quality report is generated. A crashed, killed, or
# timed-out translation unit is written to codechecker_output/failed/; a non-zero analyze exit or a failed
# cppcheck-suppression step means the run did not finish cleanly. Exiting here means no report exists for GitLab to
# ingest from an incomplete run.
failed_output=""
failed_scan_rc=0
if [[ -d codechecker_output/failed ]]; then
    failed_output=$(find codechecker_output/failed -mindepth 1 -print -quit) || failed_scan_rc=$?
fi
if [[ "${analyze_rc:-0}" -ne 0 || "${suppress_rc:-0}" -ne 0 || "${failed_scan_rc:-0}" -ne 0 ||
    "${actions_rc:-0}" -ne 0 || "${execution_rc:-0}" -ne 0 || -n "$failed_output" ]]; then
    echo "ERROR: CodeChecker run incomplete:" >&2
    echo "  analyze exit=${analyze_rc:-0}" >&2
    echo "  cppcheck-suppress exit=${suppress_rc:-0}" >&2
    echo "  failed-dir scan exit=${failed_scan_rc:-0}" >&2
    echo "  zero-action check exit=${actions_rc:-0}" >&2
    echo "  incomplete-execution check exit=${execution_rc:-0}" >&2
    echo "  failed output present=$([[ -n "$failed_output" ]] && echo yes || echo no)" >&2
    exit 1
fi

# Generate the Code Quality report with a fail-closed write. "CodeChecker parse -e codeclimate" has a documented
# tri-state exit code (0 no findings, 1 error, 2 findings); the report is written before that exit is chosen, so 0 and 2
# both imply a complete write and any other value is an interrupted/killed/disk-full write. The exporter is not atomic,
# so export to a temporary file, require a 0/2 exit, validate it against the GitLab Code Quality schema, and only then
# publish it atomically.
codeclimate_rc=0
CodeChecker parse --trim-path-prefix "$FOLDER" -e codeclimate ./codechecker_output -o "$report_tmp" &>/dev/null || codeclimate_rc=$?
case "$codeclimate_rc" in
0 | 2) ;;
*)
    echo "ERROR: CodeClimate export failed (exit=${codeclimate_rc})" >&2
    rm -f "$report_tmp"
    exit 1
    ;;
esac

# Validate the report and detect major-or-higher findings. validate_analysis.py exits 0 (valid, no fatal), 10 (valid,
# fatal finding), 20 (schema-invalid) or 30 (internal error); the codes skip 1 so an unhandled exception cannot be read
# as "valid, fatal" and published.
validate_rc=0
python3 "$validator" report "$report_tmp" || validate_rc=$?
case "$validate_rc" in
0 | 10) ;;
20)
    echo "ERROR: Code Quality report failed GitLab schema validation" >&2
    rm -f "$report_tmp"
    exit 1
    ;;
*)
    echo "ERROR: Code Quality validator failed internally (exit=${validate_rc})" >&2
    rm -f "$report_tmp"
    exit 1
    ;;
esac

# The report is complete and schema-valid: publish it atomically to the final artifact path. The findings failure is
# raised only after this rename succeeds, so the merge request shows exactly the findings that failed the gate and a
# publication failure is a hard script failure rather than a silently dropped artifact.
mv -f -- "$report_tmp" "$report" || {
    echo "ERROR: failed to publish Code Quality report" >&2
    exit 1
}

# Print summary (best-effort diagnostics, after the report is safely published). "parse" shares the tri-state exit
# code, so 2 just means findings are present; only surface a genuine error.
summary_rc=0
CodeChecker parse ./codechecker_output/ || summary_rc=$?
case "$summary_rc" in
0 | 2) ;;
*) echo "WARNING: CodeChecker summary parse failed (exit=${summary_rc})" >&2 ;;
esac

# HTML report, so a user can browse the findings. Best-effort and generated after the Code Quality report is published,
# so a slow HTML export can never delay or block the core report.
mkdir -p ../codechecker_html
CodeChecker parse --trim-path-prefix "$FOLDER" -e html ./codechecker_output -o ../codechecker_html &>/dev/null || true

if [[ "$validate_rc" -eq 10 ]]; then
    echo "ERROR: CodeChecker reported major-or-higher findings" >&2
    exit 1
fi
