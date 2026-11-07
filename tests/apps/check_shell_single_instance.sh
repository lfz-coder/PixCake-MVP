#!/bin/bash
# 单实例端到端检查（边界清单 B-67，测试清单 T-21，安全清单 S-29）。
# 参数：$1 = pixcake_app_shell 可执行文件。
set -u
shell_bin="$1"
work=$(mktemp -d /tmp/pixcake-si-XXXXXX)
instance="$work/inst"
export QT_QPA_PLATFORM=offscreen
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  rm -rf "$work"
}
trap cleanup EXIT
fail() { echo "FAIL: $*"; for f in "$work"/*.err "$work"/*.out; do [ -f "$f" ] && { echo "--- $f"; cat "$f"; }; done; exit 1; }

wait_for() {  # wait_for <tries> <command...>
  local tries=$1; shift
  for _ in $(seq "$tries"); do "$@" && return 0; sleep 0.1; done
  return 1
}

# Called directly, never as $(...): a command substitution runs in a subshell,
# and the pid would not reach `pids`, so cleanup would leave the instance running.
start_primary() {  # start_primary <name>; the pid is in $primary
  "$shell_bin" --instance-dir "$instance" >"$work/$1.out" 2>"$work/$1.err" &
  primary=$!
  pids+=($primary)
}

second_launch() {  # second_launch <name>; prints exit status
  "$shell_bin" --instance-dir "$instance" >"$work/$1.out" 2>"$work/$1.err" &
  local pid=$!
  pids+=($pid)
  wait_for 30 bash -c "! kill -0 $pid 2>/dev/null" || fail "$1 did not exit within 3 s"
  wait "$pid"
  echo $?
}

start_primary first
first=$primary
wait_for 100 test -S "$instance/instance.sock" || fail "first instance never listened"
sleep 0.3
status=$(second_launch second)
[ "$status" = 0 ] || fail "second launch exited $status"
grep -q "already running" "$work/second.out" || fail "second launch did not report the running instance"
wait_for 30 grep -q "activated by another launch" "$work/first.err" || fail "first instance was not activated"
kill -0 "$first" 2>/dev/null || fail "first instance exited"

# 主实例被强杀：锁随进程释放，残留的套接字文件不挡住下一次启动。
kill -9 "$first"; wait "$first" 2>/dev/null
start_primary third
third=$primary
sleep 1
kill -0 "$third" 2>/dev/null || fail "third launch did not become the primary instance"
grep -q "already running" "$work/third.out" && fail "third launch thought another instance was running"
status=$(second_launch fourth)
[ "$status" = 0 ] || fail "fourth launch exited $status"
wait_for 30 grep -q "activated by another launch" "$work/third.err" || fail "third instance was not activated"
echo "single instance ok"
