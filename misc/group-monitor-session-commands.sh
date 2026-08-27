#!/usr/bin/env bash
#
# Record of every terminal command run during the group-monitor (FilesMonitor)
# verify-and-test session of 2026-08-26, in order, grouped by what they were doing.
#
# The single-file monitor session is recorded separately in misc/session-commands.sh;
# nothing here modifies that record.
#
# THIS IS A RECORD, NOT A SCRIPT TO RUN. Some commands are polling or diagnostic, some
# depend on scratch files that no longer exist, the mutation-testing section deliberately
# corrupts a copy of the source, and section 9 changes a kernel sysctl. Run individual
# commands deliberately; do not execute this file.
#
# Paths used during the session:
#   REPO=/Users/jamster/Projects/SparkySIEM
#   SP=/private/tmp/claude-501/-Users-jamster-Projects-SparkySIEM/<session-id>/scratchpad
#
# For the curated, runnable command list see misc/group-monitor-build-and-test-commands.md.

exit 0   # guard: prevents accidental execution

# =============================================================================
# 1. Explore the repository and the branch
# =============================================================================
ls -la
find . -type f -not -path './.git/*' | head -100
cat README.md

git log --oneline -20 --graph --all | head -40
git branch -a
git log --oneline --all -- FilesMonitor.cpp FilesMonitor.h
git show --stat 2795f6d | head -30      # what the merge brought in

# =============================================================================
# 2. Read the code under test
# =============================================================================
cat FilesMonitor.h; cat FilesMonitor.cpp          # the group monitor, this session's subject
cat FileMonitor.h;  cat FileMonitor.cpp           # the single-file monitor, used as the model
cat MessageSink.h MessageFormat.h MessageFormat.cpp main.cpp
cat KafkaSink.h KafkaSink.cpp
cat Makefile Dockerfile.test run_tests.sh
cat tests/FakeSink.h tests/TestSupport.h

wc -l tests/*.cpp
cat tests/test_files_monitor.cpp                  # 11 existing tests for the group monitor
sed -n 1,80p tests/test_file_monitor.cpp          # house style for the new tests
sed -n 250,402p tests/test_file_monitor.cpp
grep -n '^TEST' tests/test_file_monitor.cpp tests/test_message_format.cpp

cat rand_data_gen/rand_data_gen.c
ls docker_stuff; cat docker_stuff/compose.yaml docker_stuff/commands
cat docker_stuff/docker_commands_for_cli

# Previous session's notes, for format and for what was already flagged
head -60 misc/session-transcript.md
grep -n '^#' misc/session-transcript.md
sed -n 197,240p misc/session-transcript.md
sed -n 1,120p   misc/build-and-test-commands.md
sed -n 120,260p misc/build-and-test-commands.md
tail -15 misc/session-commands.sh

# =============================================================================
# 3. Baseline: does the merged branch pass as it stands?
# =============================================================================
docker version --format '{{.Server.Version}}'
./run_tests.sh 2>&1 | tail -60                    # first run: builds the image, ~20 minutes

# While that ran, diagnosing whether the build was stuck or just slow:
docker images; docker ps -a
docker system df -v | sed -n '/Build cache/,+8p'
ps aux | grep "[d]ocker build"
time docker run --rm ubuntu:24.04 echo ok                       # base image already local
docker run --rm ubuntu:24.04 bash -c 'time apt-get update -qq'  # 35 s: slow mirror
docker run --rm ubuntu:24.04 bash -c \
    'cat /proc/sys/fs/inotify/max_user_instances /proc/sys/fs/inotify/max_user_watches'

# Same build with visible progress, which showed the apt layer taking 1159 s
docker build --progress=plain -f Dockerfile.test -t sparkysiem-tests .

# Baseline result: 45 tests from 15 test suites, all passing
docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build

# =============================================================================
# 4. End-to-end: the group monitor against a live broker
# =============================================================================
cd docker_stuff && docker compose up -d && cd ..
docker network ls | grep -i docker_stuff
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --list --bootstrap-server ctest_broker:29092

# $SP/e2e_group.sh: builds the forwarder and rand_data_gen inside the image, then runs
# the forwarder on a directory PLUS an explicit file and drives the whole scenario --
# appends to two monitored files, content containing " and \, a file named we"ird name.log,
# a file created after start, 1000 generated lines, a deletion, a rotation, then SIGTERM.
docker run --rm --network docker_stuff_default \
    -v "$PWD:/work:ro" -v "$SP:/scratch:ro" sparkysiem-tests \
    bash /scratch/e2e_group.sh sparky-group-1

docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-group-1 --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 10000 > "$SP/topic1.jsonl"
wc -l "$SP/topic1.jsonl"

# Analysis pass 1: every message parses, count per filePath, lifecycle types per file,
# duplicate detection over the MODIFY bodies
python3 - "$SP/topic1.jsonl" <<'PY'
# json.loads every line; Counter by filePath; Counter by type;
# per file: duplicated distinct lines among its MODIFY bodies
PY

# Analysis pass 2: ordering, byte-for-byte integrity, cross-talk between files,
# timestamp format, topic field
python3 - "$SP/topic1.jsonl" <<'PY'
# md5 of the 1000 published bodies vs md5 of the generated file.txt;
# each file's bodies listed; regex check on every timestamp
PY

# =============================================================================
# 5. Fix FilesMonitor (see section 4 of the session transcript for the why)
# =============================================================================
grep -n "cleanupDeletedFiles\|stopAll\|waitCondition\|stopMonitoring" FilesMonitor.cpp FilesMonitor.h

# Applied with python3 in-place edits:
#   FilesMonitor.h   -- takeDeletedFiles/pruneReportedFailures/joinAll declarations,
#                       corrected stop() and class documentation
#   FilesMonitor.cpp -- <stdexcept>; flag set under waitMutex in stop(); stop flag
#                       re-checked under monitorMutex in monitorLoop(); handleFile()
#                       returns early once stopping; join moved outside the lock;
#                       reportedFailures pruned
python3 - <<'PY'
# ... in-place edits to FilesMonitor.h
PY
python3 - <<'PY'
# ... in-place edits to FilesMonitor.cpp
PY

# Existing tests must still pass unchanged: 45, all passing
docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build GTEST_ARGS="--gtest_brief=1"

# =============================================================================
# 6. Write the new tests
# =============================================================================
# messagesFor() helper added to tests/TestSupport.h
python3 - <<'PY'
# ... insert messagesFor() ahead of bodiesOfType()
PY

# tests/test_files_monitor.cpp rewritten: 11 tests -> 28 at this point
cat > tests/test_files_monitor.cpp <<'CPP'
# ... full file, sectioned like tests/test_file_monitor.cpp
CPP

docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build GTEST_ARGS="--gtest_brief=1"   # 62 passing

# SlowFlushSink added to tests/FakeSink.h, plus the two tests that need it and the
# pruning behaviour: ADeletedFilesShutdownDoesNotBlockTheGroup and
# ReportsAnUnwatchableFileAgainAfterItComesBack
python3 - <<'PY'
# ... SlowFlushSink, then the two extra tests
PY

docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build GTEST_ARGS="--gtest_brief=1"   # 64 passing

# =============================================================================
# 7. Mutation testing: do the new tests actually have teeth?
# =============================================================================
# $SP/mutate.sh rsyncs the repo to a scratch copy, applies one mutation, runs a filtered
# subset in the container, and reports whether the suite noticed. Every run must FAIL.
export REPO=/Users/jamster/Projects/SparkySIEM
"$SP/mutate.sh" "join inside the lock"   "$SP/mut_join_inside_lock.py"  'FilesMonitorCleanup.*'
"$SP/mutate.sh" "no prune"               "$SP/mut_no_prune.py"          'FilesMonitorFailures.*'
"$SP/mutate.sh" "no notify_all"          "$SP/mut_no_notify.py"         'FilesMonitorShutdown.*'
"$SP/mutate.sh" "monitor() never called" "$SP/mut_never_run_monitor.py" 'FilesMonitor*.*'
# Caught by, respectively: ADeletedFilesShutdownDoesNotBlockTheGroup;
# ReportsAnUnwatchableFileAgainAfterItComesBack; StopCutsALongScanIntervalShort (30 s);
# and 11 FilesMonitor* tests.

# =============================================================================
# 8. Stability
# =============================================================================
docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build \
    GTEST_ARGS="--gtest_repeat=3 --gtest_shuffle --gtest_brief=1"

docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build \
    GTEST_ARGS="--gtest_repeat=6 --gtest_shuffle --gtest_brief=1"
# 9 shuffled runs in total, 64 passing each time.

# =============================================================================
# 9. How many files can one forwarder actually follow?
# =============================================================================
# $SP/probe_limit.cpp drives FilesMonitor against a directory with a FakeSink and prints
# how many files ended up monitored. $SP/probe_limit.sh lowers fs.inotify.max_user_instances
# to 20 inside the Docker VM, creates 40 files, runs the probe as the non-root user, and
# restores the original limit. Result: monitored=20, 20 distinct stderr reports.
docker run --rm --privileged --user root \
    -v "$PWD:/work:ro" -v "$SP:/scratch:ro" sparkysiem-tests bash /scratch/probe_limit.sh

# =============================================================================
# 10. Documentation
# =============================================================================
grep -n "Known limitations" -A 14 README.md
python3 - <<'PY'
# ... README: inotify instance/thread ceiling with the measured numbers, non-regular
#     files skipped, and one shared inotify instance added to the TO-DO list
PY

# =============================================================================
# 11. Re-verify end-to-end with the fixed code
# =============================================================================
docker run --rm --network docker_stuff_default \
    -v "$PWD:/work:ro" -v "$SP:/scratch:ro" sparkysiem-tests \
    bash /scratch/e2e_group.sh sparky-group-2

docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-group-2 --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 10000 > "$SP/topic2.jsonl"

python3 - "$SP/topic2.jsonl" <<'PY'
# same analysis as section 4: 2034 messages, same per-file counts, same md5
PY

# =============================================================================
# 12. Final checks
# =============================================================================
# Clean build of both binaries with -Wall -Wextra: no warnings, exit 0
docker run --rm -v "$PWD:/work:ro" -w /work sparkysiem-tests bash -c \
    'cp -r /work /tmp/s && cd /tmp/s && make BUILD_DIR=/tmp/b 2>&1 | grep -iE "warn|error"; \
     echo "APP BUILD EXIT: ${PIPESTATUS[0]}"; ls -l /tmp/b/SparkySIEM; \
     make test BUILD_DIR=/tmp/b 2>&1 | grep -iE "warning|error:"; echo "TEST BUILD clean"'

./run_tests.sh                     # final run: 64 tests, all passing

git status --short
git diff --stat
git diff FilesMonitor.h FilesMonitor.cpp
