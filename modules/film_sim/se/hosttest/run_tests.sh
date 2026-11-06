#!/bin/sh
# Every host test this module has, run in one place so CI and a human use the
# same command. Each test prints PASS or FAIL on its own final line; this
# script turns that into an exit code.
#
#   ./run_tests.sh            build and run everything
#   ./run_tests.sh --no-build run what is already built
set -u
cd "$(dirname "$0")" || exit 1

if [ "${1:-}" != "--no-build" ]; then
    make -s all >/dev/null 2>&1 || { echo "BUILD FAILED"; make -s all; exit 1; }
    make -s tableerr creatorwrite fujiwrite rampchk >/dev/null 2>&1 || {
        echo "BUILD FAILED (extra tests)"; make tableerr creatorwrite fujiwrite rampchk
        exit 1; }
fi

fail=0
check() {   # check <name> <command...>
    name=$1; shift
    out=$("$@" 2>&1)
    if echo "$out" | grep -q "^PASS"; then
        echo "ok   $name"
    else
        echo "FAIL $name"
        echo "$out" | tail -5
        fail=1
    fi
}

check creatorwrite ./creatorwrite
check fujiwrite    ./fujiwrite

# rampchk has no PASS line: it is a measurement, so assert its bound instead.
if [ -x ./rampchk ]; then
    n=$(./rampchk ../../films/*.fs 2>/dev/null | sed -n 's/.*TOTAL inversions across [0-9]* stocks: //p')
    if [ -z "$n" ]; then echo "FAIL rampchk (no output)"; fail=1
    elif [ "$n" -le 20 ]; then echo "ok   rampchk ($n inversions, bound 20)"
    else echo "FAIL rampchk ($n inversions over bound 20)"; fail=1
    fi
fi

if [ -x ./tableerr ]; then
    e=$(./tableerr ../../films/portra.fs 1 2>/dev/null | sed -n 's/^  all *n=[0-9]* *mean *\([0-9.]*\).*/\1/p')
    if [ -z "$e" ]; then echo "FAIL tableerr (no output)"; fail=1
    elif awk "BEGIN{exit !($e <= 4.0)}"; then echo "ok   tableerr (portra mean $e, bound 4.0)"
    else echo "FAIL tableerr (portra mean $e over bound 4.0)"; fail=1
    fi
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || echo "HOST TESTS FAILED"
exit $fail
