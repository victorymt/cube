#!/usr/bin/env bash

# Source after assigning game_input, game_output, game_pid, and command_timeout.
# Each send_command argument must be one complete DSL block. Command callbacks
# may reply before the block's terminal reply, so drain that reply before a new
# block is sent; otherwise an old completion can satisfy a later wait/assert.
matched_line=""
debug_command_pending=0
debug_pending_command="<startup>"

debug_protocol_diagnostic() {
    printf '%s; command_pending=%s command=%s\n' \
        "$1" "$debug_command_pending" "$debug_pending_command" >&2
}

wait_for_reply() {
    local pattern=$1
    local timeout_seconds=${2:-$command_timeout}
    local deadline=$((SECONDS + timeout_seconds))
    local line

    matched_line=""
    while ((SECONDS < deadline)); do
        if IFS= read -r -t 1 line <&"$game_output"; then
            printf '%s\n' "$line"
            if [[ -n "${debug_protocol_log:-}" ]]; then
                printf '%s\n' "$line" >>"$debug_protocol_log"
            fi
            if [[ "$line" == 'DEBUG_SCRIPT complete source=stdin' ||
                  "$line" == DEBUG_SCRIPT\ error\ source=stdin\ * ||
                  "$line" == DEBUG_SCRIPT\ exit\ code=* ]]; then
                debug_command_pending=0
            fi
            if [[ "$line" =~ ^DEBUG_SCRIPT[[:space:]]error[[:space:]] ]] &&
               [[ ! "$line" =~ $pattern ]]; then
                debug_protocol_diagnostic \
                    "unexpected debug script error while waiting for: $pattern"
                return 1
            fi
            if [[ "$line" =~ $pattern ]]; then
                matched_line=$line
                return 0
            fi
        elif ! kill -0 "$game_pid" >/dev/null 2>&1; then
            debug_protocol_diagnostic "game exited while waiting for: $pattern"
            return 1
        fi
    done

    debug_protocol_diagnostic "timed out waiting for: $pattern"
    return 1
}

send_command() {
    if ((debug_command_pending)); then
        wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' || return 1
    fi
    debug_pending_command=$1
    debug_command_pending=1
    if [[ -n "${debug_protocol_log:-}" ]]; then
        printf 'DEBUG_TEST send %q\n' "$1" >>"$debug_protocol_log"
    fi
    printf '%s\n' "$1" >&"$game_input"
}
