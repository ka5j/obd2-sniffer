#!/usr/bin/env sh
# Check (or with --fix, apply) clang-format on project C sources.
# Vendor code under firmware/third_party is excluded.
set -eu

cd "$(dirname "$0")/.."

# --others: include new files not yet committed, so local runs match CI.
files=$(git ls-files --cached --others --exclude-standard '*.c' '*.h' | grep -v '^firmware/third_party/' || true)
[ -z "$files" ] && exit 0

if [ "${1:-}" = "--fix" ]; then
    echo "$files" | xargs clang-format -i
else
    echo "$files" | xargs clang-format --dry-run --Werror
fi
