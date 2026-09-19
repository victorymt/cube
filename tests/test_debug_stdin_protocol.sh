#!/usr/bin/env bash

set -euo pipefail

test_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-stdin-protocol.XXXXXX")
command_timeout=5
debug_protocol_log="$test_dir/replies.log"

# A protocol peer with no graphics, timers, or application dependencies. The
# first completion is deliberately left unread by the caller, as in the E2E.
coproc GAME_PROCESS {
    IFS= read -r line
    [[ "$line" == first ]]
    printf '%s\n' 'DEBUG_CONTROL first ok' \
        'DEBUG_SCRIPT complete source=stdin'

    IFS= read -r line
    [[ "$line" == 'assert false' ]]
    printf '%s\n' 'DEBUG_CONTROL second started' \
        'DEBUG_SCRIPT error source=stdin line=1 column=1 code=assertion message=assertion failed'

    IFS= read -r line
    [[ "$line" == 'block gallery invalid' ]]
    printf '%s\n' 'DEBUG_CONTROL block gallery error reason=region_unloaded' \
        'DEBUG_SCRIPT error source=stdin line=1 column=1 code=callback message=gallery_region_unloaded'

    IFS= read -r line
    [[ "$line" == 'assert true' ]]
    printf '%s\n' 'DEBUG_SCRIPT complete source=stdin'

    IFS= read -r line
    [[ "$line" == 'repeat 2 {' ]]
    IFS= read -r line
    [[ "$line" == 'water debug on' ]]
    IFS= read -r line
    [[ "$line" == 'water debug off' ]]
    IFS= read -r line
    [[ "$line" == '}' ]]
    printf '%s\n' 'DEBUG_CONTROL water debug enabled=1' \
        'DEBUG_CONTROL water debug enabled=0' \
        'DEBUG_CONTROL water debug enabled=1' \
        'DEBUG_CONTROL water debug enabled=0' \
        'DEBUG_SCRIPT complete source=stdin'

    IFS= read -r line
    [[ "$line" == screenshot ]]
    printf '%s\n' 'DEBUG_CONTROL screenshot scheduled' \
        'DEBUG_SCRIPT complete source=stdin' \
        'DEBUG_CONTROL capture ok png=fake.png report=fake.txt'

    IFS= read -r line
    [[ "$line" == 'wait until stalled timeout 700' ]]
    printf '%s\n' 'DEBUG_CONTROL stalled'
    # The test releases this pending block separately after checking its
    # timeout diagnostic, so send_command must not bypass synchronization.
    IFS= read -r line
    [[ "$line" == resume ]]
    printf '%s\n' 'DEBUG_SCRIPT complete source=stdin'

    IFS= read -r line
    [[ "$line" == 'exit 7' ]]
    printf '%s\n' 'DEBUG_SCRIPT exit code=7'
    exit 7
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"

cleanup() {
    if kill -0 "$game_pid" >/dev/null 2>&1; then
        kill "$game_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT
source tests/debug_stdin_protocol.sh

send_command first
wait_for_reply '^DEBUG_CONTROL first ok$'
[[ "$debug_command_pending" == 1 ]]
send_command 'assert false'
# send_command must consume first's terminal reply before transmitting second.
[[ "$(tail -n 2 "$debug_protocol_log" | head -n 1)" == \
    'DEBUG_SCRIPT complete source=stdin' ]]
wait_for_reply '^DEBUG_CONTROL second started$'
if wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' \
        >"$test_dir/unexpected-error.log" 2>&1; then
    echo 'a stale completion incorrectly completed assert false' >&2
    exit 1
fi
grep -Fq 'unexpected debug script error' "$test_dir/unexpected-error.log"
grep -Fq 'command=assert false' "$test_dir/unexpected-error.log"
[[ "$debug_command_pending" == 0 ]]

# An explicitly expected error is terminal too; the next send must not wait
# for a completion that will never arrive after a rejected block.
send_command 'block gallery invalid'
wait_for_reply '^DEBUG_CONTROL block gallery error '
wait_for_reply '^DEBUG_SCRIPT error source=stdin .*code=callback '
[[ "$debug_command_pending" == 0 ]]
send_command 'assert true'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command $'repeat 2 {\nwater debug on\nwater debug off\n}'
wait_for_reply '^DEBUG_CONTROL water debug enabled=1$'
wait_for_reply '^DEBUG_CONTROL water debug enabled=0$'
wait_for_reply '^DEBUG_CONTROL water debug enabled=1$'
wait_for_reply '^DEBUG_CONTROL water debug enabled=0$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Deferred capture replies can follow the block completion. Consuming it while
# waiting for the capture must also clear the pending-block state.
send_command screenshot
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok '
[[ "$debug_command_pending" == 0 ]]

send_command 'wait until stalled timeout 700'
wait_for_reply '^DEBUG_CONTROL stalled$'
if wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' 1 \
        >"$test_dir/timeout.log" 2>&1; then
    echo 'an unfinished command unexpectedly completed' >&2
    exit 1
fi
grep -Fq 'timed out waiting for:' "$test_dir/timeout.log"
grep -Fq 'command_pending=1 command=wait until stalled timeout 700' \
    "$test_dir/timeout.log"
printf '%s\n' resume >&"$game_input"
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command 'exit 7'
wait_for_reply '^DEBUG_SCRIPT exit code=7$'
[[ "$debug_command_pending" == 0 ]]
exec {game_input}>&-
if wait "$game_pid"; then
    echo 'fake process lost its requested exit code' >&2
    exit 1
else
    [[ "$?" == 7 ]]
fi
printf '%s\n' 'debug stdin protocol test passed: completion=error=multiline=capture=timeout=exit'
