#!/bin/sh
# run_all.sh -- run every STCS Phase 2 test suite from the project root.
#
#   make check            (builds first, then runs this script)
#   sh tests/run_all.sh   (if everything is already built)
#
# Output is one PASS/FAIL line per test plus a summary per suite; the exit
# status is non-zero if any suite failed. The saved copy of a real run is
# docs/test_results.txt.

cd "$(dirname "$0")/.." || exit 1

for program in build/test_framing build/test_units build/test_integration bin/server bin/client; do
    if [ ! -x "$program" ]; then
        echo "Missing $program -- run 'make all tests' first." >&2
        exit 1
    fi
done

echo "STCS Phase 2 test run"
echo "Date:     $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "Compiler: $(${CXX:-g++} --version | head -n 1)"
echo "System:   $(uname -sr)"
echo

status=0

echo "=== Suite 1: framing unit tests (no sockets) ==="
./build/test_framing || status=1
echo

echo "=== Suite 2: unit and in-process protocol tests ==="
./build/test_units || status=1
echo

echo "=== Suite 3: integration tests (real server and client over TCP) ==="
./build/test_integration bin || status=1
echo

if [ "$status" -eq 0 ]; then
    echo "ALL SUITES PASSED"
else
    echo "SOME TESTS FAILED"
fi
exit $status
