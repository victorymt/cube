#!/bin/sh

set -u

timeout_seconds=${TEST_TIMEOUT_SECONDS:-120}

case "$timeout_seconds" in
    ''|*[!0-9]*|0)
        echo "TEST_TIMEOUT_SECONDS must be a positive integer" >&2
        exit 2
        ;;
esac

if [ "$#" -eq 0 ]; then
    echo "no test binaries were provided" >&2
    exit 2
fi

if ! command -v timeout >/dev/null 2>&1; then
    echo "the timeout command is required to run tests" >&2
    exit 2
fi

passed=0
failed=0
failures=""

for test_binary in "$@"; do
    start_time=$(date +%s)
    printf '[TEST] %s\n' "$test_binary"

    if timeout "${timeout_seconds}s" "$test_binary"; then
        status=0
    else
        status=$?
    fi

    end_time=$(date +%s)
    elapsed=$((end_time - start_time))

    if [ "$status" -eq 0 ]; then
        passed=$((passed + 1))
        printf '[PASS] %s (%ss)\n' "$test_binary" "$elapsed"
    else
        failed=$((failed + 1))
        if [ "$status" -eq 124 ]; then
            reason="timeout after ${timeout_seconds}s"
        else
            reason="exit ${status}"
        fi
        failures="${failures}\n  - ${test_binary}: ${reason}"
        printf '[FAIL] %s (%s, %ss)\n' "$test_binary" "$reason" "$elapsed" >&2
    fi
done

printf '\nTest summary: %s passed, %s failed\n' "$passed" "$failed"

if [ "$failed" -ne 0 ]; then
    printf 'Failures:%b\n' "$failures" >&2
    exit 1
fi
