#!/bin/bash

# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

# Mock-based regression test for static-analyzer.sh.
#
# It exercises the fail-closed state matrix from issue #632 without a full OCUDU
# or CodeChecker build: a fake "CodeChecker" and "cppcheck_suppress_unspecified.py"
# placed first on PATH drive each path, and the real wrapper is run against a
# throw-away workspace. Run with: bash static-analyzer-test.sh

set -u

here="$(cd "$(dirname "$0")" && pwd)"
failures=0

# Run the real wrapper once under a mock toolchain and check its exit code, the
# presence of the published Code Quality report, and the HTML tree.
#   $1 expected exit code
#   $2 expected report:    y | n
#   $3 expected html tree: y | n | -   ('-' = do not check)
#   $4 label
# Behaviour is controlled through the CC_* environment variables.
run_case() {
    local exp_exit="$1" exp_report="$2" exp_html="$3" label="$4"
    local work bin folder
    work="$(mktemp -d)"
    bin="$work/bin"
    folder="$work/src"
    mkdir -p "$bin" "$folder/build"
    # A real run analyses a non-empty compilation database; CC_EMPTY_DATABASE models an empty one (the build produced
    # no compile commands). CC_ALL_SKIPPED models a non-empty database whose every command is filtered out. Both make
    # CodeChecker schedule zero actions, which the wrapper must reject rather than pass as a 0%-coverage green.
    if [[ "${CC_EMPTY_DATABASE:-0}" == 1 ]]; then
        echo '[]' >"$folder/build/compile_commands.json"
    else
        printf '[{"directory":"%s/build","command":"cc -c a.c","file":"a.c"}]' "$folder" >"$folder/build/compile_commands.json"
    fi

    cat >"$bin/CodeChecker" <<'MOCK'
#!/bin/bash
sub="$1"
shift
out=""
prev=""
for a in "$@"; do
    [[ "$prev" == "-o" ]] && out="$a"
    prev="$a"
done
case "$sub" in
analyzer-version) echo "mock CodeChecker ${CC_VERSION:-6.28.2}" ;;
analyze)
    mkdir -p "$out"
    # Model CodeChecker's action manifest: it is written only when at least one action is analysed. An empty database
    # (CC_EMPTY_DATABASE) or an all-skipped one (CC_ALL_SKIPPED) leaves it absent; the other flags shape its contents.
    if [[ "${CC_EMPTY_DATABASE:-0}" != 1 && "${CC_ALL_SKIPPED:-0}" != 1 ]]; then
        if [[ "${CC_EMPTY_MANIFEST:-0}" == 1 ]]; then
            echo '[]' >"$out/unique_compile_commands.json"
        elif [[ "${CC_BAD_MANIFEST:-0}" == 1 ]]; then
            printf '{ not a json array' >"$out/unique_compile_commands.json"
        else
            printf '[{"directory":"/b","command":"cc -c a.c","file":"a.c"}]' >"$out/unique_compile_commands.json"
        fi
    fi
    # Model CodeChecker's metadata.json (analyzer worker execution statistics). Written alongside the manifest on a run
    # that scheduled work; the flags shape whether an analyzer actually executed.
    if [[ "${CC_EMPTY_DATABASE:-0}" != 1 && "${CC_ALL_SKIPPED:-0}" != 1 && "${CC_NO_METADATA:-0}" != 1 ]]; then
        analyzer="${CC_ANALYZER:-clang-tidy}"
        if [[ "${CC_BAD_METADATA:-0}" == 1 ]]; then
            printf '{ not json' >"$out/metadata.json"
        elif [[ "${CC_EMPTY_ANALYZERS:-0}" == 1 ]]; then
            printf '{"tools":[{"action_num":1,"analyzers":{}}]}' >"$out/metadata.json"
        elif [[ "${CC_WRONG_ANALYZER:-0}" == 1 ]]; then
            # Metadata records an analyzer other than the one this job requested.
            printf '{"tools":[{"action_num":1,"analyzers":{"some-other-analyzer":{"analyzer_statistics":{"successful":1,"failed":0}}}}]}' >"$out/metadata.json"
        elif [[ "${CC_DUP_ANALYZER:-0}" == 1 ]]; then
            # Same analyzer across two tools[] entries, the first failed, the second clean: last-writer-wins flattening
            # would hide the failure.
            printf '{"tools":[{"action_num":1,"analyzers":{"%s":{"analyzer_statistics":{"successful":0,"failed":1}}}},{"action_num":1,"analyzers":{"%s":{"analyzer_statistics":{"successful":1,"failed":0}}}}]}' "$analyzer" "$analyzer" >"$out/metadata.json"
        elif [[ "${CC_NO_STATS:-0}" == 1 ]]; then
            printf '{"tools":[{"action_num":1,"analyzers":{"%s":{}}}]}' "$analyzer" >"$out/metadata.json"
        else
            # Real 6.28.2 shape: one tools[] record with action_num == successful + failed for the requested analyzer.
            printf '{"tools":[{"action_num":%s,"analyzers":{"%s":{"analyzer_statistics":{"successful":%s,"failed":%s}}}}]}' "${CC_ACTION_NUM:-1}" "$analyzer" "${CC_SUCCESSFUL:-1}" "${CC_FAILED:-0}" >"$out/metadata.json"
        fi
    fi
    if [[ "${CC_FAKE_FAILED:-0}" == 1 ]]; then
        mkdir -p "$out/failed"
        : >"$out/failed/tu1.plist.error"
    elif [[ "${CC_FAKE_FAILED_DIR:-0}" == 1 ]]; then
        # An empty failed/ dir: the wrapper still scans it, so this isolates a
        # find scan error (CC_FIND_RC) from a non-empty failed/ tree.
        mkdir -p "$out/failed"
    fi
    exit "${CC_ANALYZE_RC:-0}"
    ;;
parse)
    if [[ " $* " == *" -e codeclimate "* ]]; then
        printf '%s' "${CC_REPORT_JSON:-[]}" >"$out"
        exit "${CC_CODECLIMATE_RC:-0}"
    fi
    if [[ " $* " == *" -e html "* ]]; then
        mkdir -p "$out"
        : >"$out/index.html"
        exit "${CC_HTML_RC:-0}"
    fi
    exit "${CC_SUMMARY_RC:-0}"
    ;;
esac
exit 0
MOCK
    chmod +x "$bin/CodeChecker"

    printf '#!/bin/bash\nexit %s\n' "${CC_SUPPRESS_RC:-0}" >"$bin/cppcheck_suppress_unspecified.py"
    chmod +x "$bin/cppcheck_suppress_unspecified.py"

    # The wrapper resolves its sibling helpers (validate_analysis.py, cppcheck_suppress_unspecified.py) from its own
    # directory before falling back to PATH, so run a copy of it from "$bin" where the real validator and the fake
    # suppression helper sit side by side. This keeps the validator real while letting the test drive the suppression
    # exit code.
    cp "$here/static-analyzer.sh" "$bin/static-analyzer.sh"
    cp "$here/validate_analysis.py" "$bin/validate_analysis.py"
    chmod +x "$bin/static-analyzer.sh" "$bin/validate_analysis.py"

    # Fake "find" that fails on demand (CC_FIND_RC) to exercise the failed-directory
    # scan-error path; with CC_FIND_RC unset it delegates to the real find.
    local real_find
    real_find="$(command -v find)"
    cat >"$bin/find" <<MOCK
#!/bin/bash
if [[ -n "\${CC_FIND_RC:-}" ]]; then
    exit "\$CC_FIND_RC"
fi
exec "$real_find" "\$@"
MOCK
    chmod +x "$bin/find"

    # Optionally pre-seed a stale Code Quality report to prove the wrapper clears it up front. CC_PRESEED_REPORT_DIR
    # seeds it as a *directory* so the up-front "rm -f" fails, exercising the cleanup-failure path.
    if [[ "${CC_PRESEED_REPORT_DIR:-0}" == 1 ]]; then
        mkdir -p "$folder/code-quality-report.json"
    elif [[ "${CC_PRESEED_REPORT:-0}" == 1 ]]; then
        printf 'STALE' >"$folder/code-quality-report.json"
    fi

    # ANALYZER is the analyzer the CI job requests; the metadata completeness gate keys on it. Default to clang-tidy,
    # matching the mock metadata's analyzer name.
    local analyzer="${CC_ANALYZER:-clang-tidy}"
    ANALYZER="$analyzer" PATH="$bin:$PATH" bash "$bin/static-analyzer.sh" "$folder" >/dev/null 2>&1
    local rc=$?

    local report="$folder/code-quality-report.json"
    local got_report=n html=n
    [[ -f "$report" ]] && got_report=y
    [[ -d "$folder/codechecker_html" ]] && html=y

    local ok=1 reason=""
    [[ "$rc" == "$exp_exit" ]] || {
        ok=0
        reason+=" exit=$rc(exp $exp_exit)"
    }
    [[ "$got_report" == "$exp_report" ]] || {
        ok=0
        reason+=" report=$got_report(exp $exp_report)"
    }
    [[ "$exp_html" == "-" || "$html" == "$exp_html" ]] || {
        ok=0
        reason+=" html=$html(exp $exp_html)"
    }
    # A leftover temporary report is always a failure: the report is published atomically and its .tmp must never be
    # left behind for the "when: always" upload.
    [[ -f "${report}.tmp" ]] && {
        ok=0
        reason+=" tmp-leftover"
    }
    # A published report must contain exactly what the exporter wrote (no empty,
    # stale, or wrong-case content).
    if [[ "$got_report" == y && -n "${CC_EXPECT_REPORT_JSON:-}" && "$(cat "$report")" != "$CC_EXPECT_REPORT_JSON" ]]; then
        ok=0
        reason+=" report-content-mismatch"
    fi

    if [[ "$ok" == 1 ]]; then
        echo "  PASS  $label"
    else
        echo "  FAIL  $label ::$reason"
        failures=1
    fi
    rm -rf "$work"
}

info_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"lib/a.cpp","lines":{"begin":1}}}]'
major_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"major","location":{"path":"lib/a.cpp","lines":{"begin":1}}}]'
abs_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"/abs/a.cpp","lines":{"begin":1}}}]'
dot_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"./a.cpp","lines":{"begin":1}}}]'
dotdot_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"../a.cpp","lines":{"begin":1}}}]'
unknown_sev='[{"description":"d","check_name":"c","fingerprint":"f","severity":"unknown","location":{"path":"a.cpp","lines":{"begin":1}}}]'
nonarray='{"description":"d"}'
truncated='[{"description":"d"'
# Findings that are valid except for a non-standard JSON constant in an extra field. json.load() accepts
# NaN/Infinity/-Infinity by default, but the GitLab Code Quality parser and RFC 8259 do not, so the wrapper must
# reject a report carrying any of them rather than publish something GitLab will refuse.
nan_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"lib/a.cpp","lines":{"begin":1}},"extra":NaN}]'
inf_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"lib/a.cpp","lines":{"begin":1}},"extra":Infinity}]'
ninf_report='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"lib/a.cpp","lines":{"begin":1}},"extra":-Infinity}]'

echo "== static-analyzer.sh fail-closed matrix =="

# Complete runs. Real CodeChecker exits 2 from the export when findings are
# present, so the finding cases assert the wrapper accepts exit 2, not only 0.
CC_REPORT_JSON='[]' CC_EXPECT_REPORT_JSON='[]' \
    run_case 0 y - "complete, no findings (exporter exit 0) -> pass, empty report published"
CC_REPORT_JSON="$info_report" CC_CODECLIMATE_RC=2 CC_EXPECT_REPORT_JSON="$info_report" \
    run_case 0 y - "complete, info finding (exporter exit 2) -> pass, report published"
CC_REPORT_JSON="$major_report" CC_CODECLIMATE_RC=2 CC_EXPECT_REPORT_JSON="$major_report" \
    run_case 1 y - "complete, major finding (exporter exit 2) -> fail, valid report still published"

# Incomplete or failed runs must fail closed with no report.
CC_ANALYZE_RC=3 run_case 1 n - "analyze non-zero -> fail, no report"
CC_FAKE_FAILED=1 run_case 1 n - "non-empty failed/ -> fail, no report"
CC_FAKE_FAILED_DIR=1 CC_FIND_RC=5 run_case 1 n - "failed-directory scan error -> fail, no report"
CC_SUPPRESS_RC=2 run_case 1 n - "suppression helper non-zero -> fail, no report"
CC_CODECLIMATE_RC=1 run_case 1 n - "exporter exit 1 -> fail, no report"
CC_PRESEED_REPORT=1 CC_ANALYZE_RC=3 run_case 1 n - "stale report cleared, then analyze fails -> no report"
CC_EMPTY_DATABASE=1 run_case 1 n - "empty compilation database (no action manifest) -> fail, no report"
CC_ALL_SKIPPED=1 run_case 1 n - "non-empty database but all actions skipped (no manifest) -> fail, no report"
CC_EMPTY_MANIFEST=1 run_case 1 n - "action manifest present but an empty list -> fail, no report"
CC_BAD_MANIFEST=1 run_case 1 n - "action manifest malformed JSON -> fail, no report"
CC_NO_METADATA=1 run_case 1 n - "manifest present but metadata.json missing -> fail, no report"
CC_BAD_METADATA=1 run_case 1 n - "metadata.json malformed -> fail, no report"
CC_EMPTY_ANALYZERS=1 run_case 1 n - "metadata analyzers empty (no enabled checker) -> fail, no report"
CC_SUCCESSFUL=0 run_case 1 n - "metadata successful=0 (zero-execution, e.g. --makefile) -> fail, no report"
CC_FAILED=1 run_case 1 n - "metadata records a failed analyzer job -> fail, no report"
CC_SUCCESSFUL=true run_case 1 n - "metadata successful is a bool (not coerced by int()) -> fail closed, no report"
CC_SUCCESSFUL='"1"' run_case 1 n - "metadata successful is a string (not coerced by int()) -> fail closed, no report"

# Blocker 2: the metadata completeness gate keys on the requested analyzer and accounts for every scheduled action, so
# it rejects metadata that names a different analyzer, duplicates the analyzer across tools[] (hiding a failure via
# last-writer-wins), records missing analyzer_statistics, or executes only a fraction of the scheduled actions.
CC_WRONG_ANALYZER=1 run_case 1 n - "metadata records a different analyzer than requested -> fail, no report"
CC_ANALYZER=clang-tidy CC_DUP_ANALYZER=1 run_case 1 n - "analyzer duplicated across tools[] (earlier failure hidden) -> fail, no report"
CC_NO_STATS=1 run_case 1 n - "analyzer record missing analyzer_statistics -> fail, no report"
CC_ACTION_NUM=100 CC_SUCCESSFUL=1 run_case 1 n - "only 1 of 100 scheduled actions executed -> fail, no report"
CC_ACTION_NUM=3 CC_SUCCESSFUL=2 run_case 1 n - "2 of 3 scheduled actions executed (partial) -> fail, no report"

# Real 6.28.2 shape passes: one tools[] record for the requested analyzer with action_num == successful + failed and no
# failures. Modelled for each analyzer the CI runs (clang-tidy / cppcheck / clangsa), including a multi-action run.
CC_REPORT_JSON='[]' CC_EXPECT_REPORT_JSON='[]' \
    run_case 0 y - "real-shaped clang-tidy metadata (1 action) -> pass"
CC_ACTION_NUM=3 CC_SUCCESSFUL=3 CC_REPORT_JSON='[]' CC_EXPECT_REPORT_JSON='[]' \
    run_case 0 y - "real-shaped clang-tidy metadata (3 actions, all successful) -> pass"
CC_ANALYZER=cppcheck CC_REPORT_JSON='[]' CC_EXPECT_REPORT_JSON='[]' \
    run_case 0 y - "real-shaped cppcheck metadata for the requested analyzer -> pass"
CC_ANALYZER=clangsa CC_REPORT_JSON='[]' CC_EXPECT_REPORT_JSON='[]' \
    run_case 0 y - "real-shaped clangsa metadata for the requested analyzer -> pass"

# Schema validation rejects malformed or non-repo-relative reports.
CC_REPORT_JSON="$truncated" run_case 1 n - "truncated JSON -> schema fail, no report"
CC_REPORT_JSON="$nonarray" run_case 1 n - "root not a JSON array -> schema fail, no report"
CC_REPORT_JSON="$unknown_sev" run_case 1 n - "unknown severity -> schema fail, no report"
CC_REPORT_JSON="$abs_report" run_case 1 n - "absolute location.path -> schema fail, no report"
CC_REPORT_JSON="$dot_report" run_case 1 n - "./-prefixed location.path -> schema fail, no report"
CC_REPORT_JSON="$dotdot_report" run_case 1 n - "..-component location.path -> schema fail, no report"
CC_REPORT_JSON="$nan_report" run_case 1 n - "non-standard JSON constant NaN -> schema fail, no report"
CC_REPORT_JSON="$inf_report" run_case 1 n - "non-standard JSON constant Infinity -> schema fail, no report"
CC_REPORT_JSON="$ninf_report" run_case 1 n - "non-standard JSON constant -Infinity -> schema fail, no report"

# A non-string severity must be rejected as schema-invalid, never crash the validator. A JSON list or object is
# unhashable, so the pre-fix "severity not in allowed" raised TypeError -> Python exit 1, which the shell misread as
# "valid, fatal" and published the malformed report. Every non-string severity must fail closed with no report.
sev_list='[{"description":"d","check_name":"c","fingerprint":"f","severity":[],"location":{"path":"a.cpp","lines":{"begin":1}}}]'
sev_obj='[{"description":"d","check_name":"c","fingerprint":"f","severity":{},"location":{"path":"a.cpp","lines":{"begin":1}}}]'
sev_null='[{"description":"d","check_name":"c","fingerprint":"f","severity":null,"location":{"path":"a.cpp","lines":{"begin":1}}}]'
sev_num='[{"description":"d","check_name":"c","fingerprint":"f","severity":1,"location":{"path":"a.cpp","lines":{"begin":1}}}]'
sev_bool='[{"description":"d","check_name":"c","fingerprint":"f","severity":true,"location":{"path":"a.cpp","lines":{"begin":1}}}]'
CC_REPORT_JSON="$sev_list" run_case 1 n - "severity is a JSON list -> schema fail, no report (was TypeError->exit1 collision)"
CC_REPORT_JSON="$sev_obj" run_case 1 n - "severity is a JSON object -> schema fail, no report"
CC_REPORT_JSON="$sev_null" run_case 1 n - "severity is null -> schema fail, no report"
CC_REPORT_JSON="$sev_num" run_case 1 n - "severity is a number -> schema fail, no report"
CC_REPORT_JSON="$sev_bool" run_case 1 n - "severity is a bool -> schema fail, no report"

# Schema branches the validator rejects but the matrix did not yet exercise: a non-object finding, missing mandatory
# fields, a non-object location, a missing path, an invalid begin line (< 1 and a bool), and the location.positions
# alternate begin path (both a valid one that must publish and an invalid one that must fail closed).
finding_not_obj='[1]'
missing_desc='[{"check_name":"c","fingerprint":"f","severity":"info","location":{"path":"a.cpp","lines":{"begin":1}}}]'
missing_check='[{"description":"d","fingerprint":"f","severity":"info","location":{"path":"a.cpp","lines":{"begin":1}}}]'
missing_fp='[{"description":"d","check_name":"c","severity":"info","location":{"path":"a.cpp","lines":{"begin":1}}}]'
loc_not_obj='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":"nope"}]'
loc_path_missing='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"lines":{"begin":1}}}]'
begin_zero='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"a.cpp","lines":{"begin":0}}}]'
begin_bool='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"a.cpp","lines":{"begin":true}}}]'
positions_valid='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"a.cpp","positions":{"begin":{"line":5}}}}]'
positions_invalid='[{"description":"d","check_name":"c","fingerprint":"f","severity":"info","location":{"path":"a.cpp","positions":{"begin":{"line":0}}}}]'
CC_REPORT_JSON="$finding_not_obj" run_case 1 n - "finding is not an object -> schema fail, no report"
CC_REPORT_JSON="$missing_desc" run_case 1 n - "missing description -> schema fail, no report"
CC_REPORT_JSON="$missing_check" run_case 1 n - "missing check_name -> schema fail, no report"
CC_REPORT_JSON="$missing_fp" run_case 1 n - "missing fingerprint -> schema fail, no report"
CC_REPORT_JSON="$loc_not_obj" run_case 1 n - "location is not an object -> schema fail, no report"
CC_REPORT_JSON="$loc_path_missing" run_case 1 n - "location.path missing -> schema fail, no report"
CC_REPORT_JSON="$begin_zero" run_case 1 n - "begin line < 1 -> schema fail, no report"
CC_REPORT_JSON="$begin_bool" run_case 1 n - "begin line is a bool -> schema fail, no report"
CC_REPORT_JSON="$positions_valid" CC_EXPECT_REPORT_JSON="$positions_valid" run_case 0 y - "valid begin via location.positions.begin.line -> pass, report published"
CC_REPORT_JSON="$positions_invalid" run_case 1 n - "invalid begin via location.positions.begin.line -> schema fail, no report"

# Any unexpected validator exception must map to the internal-error exit code and fail closed -- never Python exit 1,
# never a published report. CC_TEST_FORCE_INTERNAL_ERROR forces one; the report is otherwise valid with no fatal finding.
CC_REPORT_JSON="$info_report" CC_TEST_FORCE_INTERNAL_ERROR=1 \
    run_case 1 n - "forced validator internal error -> fail closed, no report"

# ---- Blocker 2: report freshness. A preseeded report models a reused workspace's stale leftover; every early-exit
# path must clear it up front, so nothing stale is left behind for the "when: always" upload. ----
CC_PRESEED_REPORT=1 CC_CODECLIMATE_RC=1 \
    run_case 1 n - "preseed + interrupted/failed CodeClimate export -> cleared, no report"
CC_PRESEED_REPORT=1 CC_TEST_FORCE_INTERNAL_ERROR=1 CC_REPORT_JSON="$info_report" \
    run_case 1 n - "preseed + validator internal failure -> cleared, no report"
CC_PRESEED_REPORT_DIR=1 \
    run_case 1 n - "cleanup fails (un-removable stale report) -> fail closed"

# HTML debug tree: best-effort, produced after the report on a complete run so a user can browse the findings; an
# incomplete run fails the gate before HTML, so none is produced.
CC_REPORT_JSON="$major_report" CC_CODECLIMATE_RC=2 \
    run_case 1 y y "complete run with findings -> report published and HTML tree produced"
CC_ANALYZE_RC=3 \
    run_case 1 n n "incomplete run -> fails before HTML, no HTML tree"

echo
if [[ "$failures" == 0 ]]; then
    echo "ALL CASES PASSED"
else
    echo "SOME CASES FAILED"
    exit 1
fi
