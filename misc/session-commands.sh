#!/usr/bin/env bash
#
# Record of every terminal command run during the verify-and-test session
# (2026-08-18 / 2026-08-19), in order, grouped by what they were doing.
#
# THIS IS A RECORD, NOT A SCRIPT TO RUN. Some commands intentionally fail (they are
# what proved the defects), some depend on scratch files that no longer exist, and the
# mutation-testing section deliberately corrupts a copy of the source. Run individual
# commands deliberately; do not execute this file.
#
# Paths used during the session:
#   REPO=/Users/jamster/Repos/SparkySIEM
#   SP=/private/tmp/claude-501/-Users-jamster-Repos-SparkySIEM/<session-id>/scratchpad
#
# For the curated, runnable command list see misc/build-and-test-commands.md.

exit 0   # guard: prevents accidental execution

# =============================================================================
# 1. Explore the repository
# =============================================================================
find /Users/jamster/Repos/SparkySIEM -type f -not -path "*/.git/*" | head -100
ls -la /Users/jamster/Repos/SparkySIEM

for f in cpp_compiler_commands.txt test.txt .vscode/tasks.json .vscode/settings.json \
         ConfigReader.cpp docker_stuff/compose.yaml docker_stuff/docker_commands_for_cli \
         docker_stuff/commands; do
    echo "===== $f ====="; cat "$f"; echo
done

echo "===== rand_data_gen.c =====" && cat rand_data_gen/rand_data_gen.c && echo \
    && echo "===== file.txt (head) =====" && head -5 rand_data_gen/file.txt \
    && wc -l rand_data_gen/file.txt

# =============================================================================
# 2. Check the build/run environment
# =============================================================================
uname -a
g++ --version | head -3
which docker && docker --version
docker info >/dev/null 2>&1 && echo YES || echo NO
ls /opt/homebrew/include/librdkafka || ls /usr/local/include/librdkafka
ls /usr/include/sys/inotify.h            # absent on macOS, as expected

git log --oneline -15
git branch -a
file SparkySIEM                          # stale ELF x86-64 binary of the old code

# List files on the other branches, to check for existing test scaffolding (none found)
for b in main jam_grp_file_mon jam_ind_file_mon origin/jam_ind_file_mon_claude; do
    echo "=== $b ==="; git ls-tree -r --name-only $b | head -30
done

# =============================================================================
# 3. Start Kafka and build a Linux toolchain image
# =============================================================================
cd /Users/jamster/Repos/SparkySIEM/docker_stuff && docker compose up -d

# Toolchain image (written to $SP/Dockerfile.build, then built)
cat > "$SP/Dockerfile.build" <<'DOCKERFILE'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ make librdkafka-dev libgtest-dev libgmock-dev cmake ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
DOCKERFILE
docker build -f "$SP/Dockerfile.build" -t sparky-claude-build:latest "$SP"

docker images
docker context ls
docker image inspect sparky-siem-test:latest --format 'Created: {{.Created}}'
docker history sparky-siem-test:latest | head -8

# =============================================================================
# 4. Compile the ORIGINAL code in Linux — this is where defect #1 showed up
# =============================================================================
# main.cpp + FileMonitor.cpp: compiles clean
docker run --rm -v "$PWD":/src -w /src sparky-claude-build:latest \
    g++ -std=c++17 -Wall -Wextra -g main.cpp FileMonitor.cpp -o /tmp/SparkySIEM \
        -lrdkafka++ -lrdkafka

# FilesMonitor.cpp: FAILS — make_unique<FileMonitor>(filePath, topic), ctor needs 3 args
docker run --rm -v "$PWD":/src -w /src sparky-claude-build:latest \
    g++ -std=c++17 -Wall -Wextra -g -c FilesMonitor.cpp -o /tmp/FilesMonitor.o

docker network ls | grep -i -E "kafka|docker_stuff|ctest"
docker inspect ctest_broker --format '{{range $k,$v := .NetworkSettings.Networks}}{{$k}}{{end}}'

# =============================================================================
# 5. Runtime verification #1 against live Kafka
#    (harness: $SP/verify/verify_main.cpp takes <file> <broker> <topic> on argv;
#     $SP/verify/run_verify.sh builds it, appends lines, and kills the monitor)
# =============================================================================
docker run --rm --network docker_stuff_default \
    -v "$PWD":/src:ro -v "$SP/verify":/verify \
    sparky-claude-build:latest bash /verify/run_verify.sh

# Showed: whole file resent on every append; unescaped quotes/backslashes break JSON
docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-verify --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 8000

# =============================================================================
# 6. Runtime verification #2 — rotation and the busy loop
#    ($SP/verify/run_verify2.sh rotates a file, and makes another unreadable to a
#     non-root monitor process while root keeps modifying it)
# =============================================================================
docker run --rm --network docker_stuff_default \
    -v "$PWD":/src:ro -v "$SP/verify":/verify \
    sparky-claude-build:latest bash /verify/run_verify2.sh

# Rotation: content written after `mv` never appears
docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-rot --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 6000

# Busy loop: 1,799,740 error messages produced in ~4 seconds
docker exec ctest_broker /opt/kafka/bin/kafka-get-offsets.sh \
    --bootstrap-server ctest_broker:29092 --topic sparky-perm

# =============================================================================
# 7. Write the fixed implementation and the test suite
#    (Most files were created with the editor. These were written from the shell:)
# =============================================================================
cat > KafkaSink.cpp <<'EOF'
... (see KafkaSink.cpp in the repository root)
EOF

cat > run_tests.sh <<'EOF'
... (see run_tests.sh in the repository root)
EOF
chmod +x run_tests.sh

cat > .gitignore <<'EOF'
build/
*.o
rand_data_gen/rand_data_gen
EOF

cat > cpp_compiler_commands.txt <<'EOF'
... (see cpp_compiler_commands.txt in the repository root)
EOF

# =============================================================================
# 8. Run the new test suite
# =============================================================================
./run_tests.sh                                    # 45 tests, all passing

# =============================================================================
# 9. Mutation testing — prove the suite actually catches the original bugs
#    ($SP/mutate/apply_mutation.py reintroduces one defect into a copy of the source;
#     $SP/mutate/run_mutation.sh builds that copy and reports SURVIVED or KILLED)
# =============================================================================
rm -rf "$SP/mutate" && mkdir -p "$SP/mutate"
rsync -a --exclude .git --exclude build /Users/jamster/Repos/SparkySIEM/ "$SP/mutate/"

docker build -q -f Dockerfile.test -t sparkysiem-tests .

# Apply each mutation on the host (the test image has no python3), then run it
for m in whole_file_resend no_json_escape no_rotation error_flood; do
    rm -rf "$SP/mut_$m" && cp -r "$SP/mutate" "$SP/mut_$m"
    ( cd "$SP/mut_$m" && sed -i '' "s|/mut/|$SP/mut_$m/|g" apply_mutation.py \
      && python3 apply_mutation.py $m )
done

for m in "whole_file_resend:FileMonitorIncremental.*" \
         "no_json_escape:*Json*" \
         "no_rotation:FileMonitorRotation.*" \
         "error_flood:FileMonitorFailures.*"; do
    name="${m%%:*}"; filter="${m#*:}"
    docker run --rm -v "$SP/mut_$name":/work:ro sparkysiem-tests \
        bash /work/run_mutation.sh "$name" "$filter"
done
# -> 3 KILLED, error_flood SURVIVED; the flood assertion was then tightened and rerun:
docker run --rm -v "$SP/mut_error_flood":/work:ro sparkysiem-tests \
    bash /work/run_mutation.sh error_flood "FileMonitorFailures.*"     # -> KILLED

# =============================================================================
# 10. Flakiness check — 3 shuffled repeat runs
# =============================================================================
docker build -q -f Dockerfile.test -t sparkysiem-tests .
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build \
    GTEST_ARGS="--gtest_repeat=3 --gtest_shuffle --gtest_brief=1"

# =============================================================================
# 11. End-to-end run of the FIXED binary against live Kafka
#     ($SP/verify/e2e.sh builds the app, monitors a directory, appends, rotates,
#      adds a new file, then sends SIGTERM)
# =============================================================================
docker ps -a --filter name=ctest_broker --format '{{.Names}} {{.Status}}'
docker network ls
cd /Users/jamster/Repos/SparkySIEM/docker_stuff && docker compose up -d   # broker had been pruned
docker ps --filter name=ctest_broker --format '{{.Names}} {{.Status}}'

cd /Users/jamster/Repos/SparkySIEM
docker run --rm --network docker_stuff_default \
    -v "$PWD":/work:ro -v "$SP/verify":/verify \
    sparkysiem-tests bash /verify/e2e.sh

docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-fixed --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 8000 | sort -t'"' -k4

# =============================================================================
# 12. Final checks
# =============================================================================
# Clean build, warnings enabled (-Wall -Wextra): no warnings, exit 0
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests bash -c \
    'cp -r /work /tmp/s && cd /tmp/s && make BUILD_DIR=/tmp/b 2>&1 | grep -iE "warn|error"; \
     echo "BUILD EXIT: ${PIPESTATUS[0]}"; ls -l /tmp/b/SparkySIEM'

./run_tests.sh                     # final run: 45 tests, all passing

git status --short
git diff --stat
