#!/bin/sh
set -eu

fc_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fc_build=$(mktemp -d "$fc_root/.look-test.XXXXXX")
trap 'rm -rf "$fc_build"' EXIT HUP INT TERM

if [ -x /mingw64/bin/g++ ]; then
    fc_cxx=/mingw64/bin/g++
else
    fc_cxx=${CXX:-g++}
fi

"$fc_cxx" -std=c++17 -Wall -Wextra -Werror \
    -I"$fc_root/third_party/vkbasalt/src" \
    "$fc_root/tests/test_look_transform.cpp" -o "$fc_build/test_look_transform"
"$fc_build/test_look_transform"
