#!/bin/sh

set -e

me=$(basename "$0")


echo "$me: run shellcheck on shellscripts"
(
    git ls-files | grep -v "^testcases" | grep -E ".sh$" | xargs shellcheck
)

echo "$me: run shellcheck on testcases"
(
    # use this when all issues are fixed
    cd testcases && git ls-files | grep -E ".sh$" | xargs shellcheck -x
    # cd testcases && shellcheck -x ./checksum_buffersize*.sh
)

