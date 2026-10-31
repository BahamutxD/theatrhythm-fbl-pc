#!/usr/bin/env bash
# Builds out/theatrhythm-fbl.exe and out/theatrhythm-setup.exe with MSYS2 CLANG64 (packages in docs/building.md).
# The GPU library is static and CMake links the probe as well. --test also runs the runtime tests.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out
case $(uname -s) in
MINGW*|MSYS*) ;;
*) echo 'Windows only: run this from Git Bash or an MSYS2 shell.' >&2; exit 1 ;;
esac
# shellcheck source=msys2-env.sh
source ./msys2-env.sh
for tool in clang cmake ninja pkg-config; do
    command -v "$tool" >/dev/null || { echo "Need $tool from MSYS2 CLANG64 (docs/building.md)." >&2; exit 1; }
done
if [[ ! -f gpu/third_party/imgui/imgui.h || ! -f gpu/third_party/xbyak/xbyak/xbyak.h ]]; then
    git submodule update --init --recursive
fi
cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ >/dev/null
if ! ninja -C out/gpu theatrhythm-fbl theatrhythm-setup > out/gpu-build.log 2>&1; then
    grep -v '^\[' out/gpu-build.log | tail -40 >&2
    echo 'Build failed (full log: out/gpu-build.log)' >&2; exit 1
fi
cp out/gpu/theatrhythm-fbl.exe out/gpu/theatrhythm-setup.exe out/
echo "Built $PWD/out/theatrhythm-fbl.exe and theatrhythm-setup.exe"
if [[ ${1:-} == --test ]]; then
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_services.c src/compat_win.c -lbcrypt -o out/services-test.exe
    out/services-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_savedata.c src/compat_win.c -lbcrypt -o out/savedata-test.exe
    out/savedata-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_eventflag.c -o out/eventflag-test.exe
    out/eventflag-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_threadcalls.c src/compat_win.c -lbcrypt -o out/threadcalls-test.exe
    out/threadcalls-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_posixsem.c -o out/posixsem-test.exe
    out/posixsem-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_net.c src/compat_win.c -lbcrypt -lws2_32 -o out/net-test.exe
    out/net-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_kernel_files.c src/compat_win.c -lbcrypt -o out/kernel-files-test.exe
    out/kernel-files-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_button_icons.c -o out/button-icons-test.exe
    out/button-icons-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_memory.c src/compat_win.c -lbcrypt -lonecore -o out/memory-test.exe
    out/memory-test.exe
    clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_memory_read_backing.c src/compat_win.c -lbcrypt -lonecore -o out/memory-read-backing-test.exe
    out/memory-read-backing-test.exe
    ninja -C out/gpu pad-test >/dev/null
    out/gpu/pad-test.exe
fi
