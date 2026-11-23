#!/usr/bin/env bash
# Compile, link and run the generated QML sources with clang and the REAL libstdc++ (Linux).
# usage: llvm-check.sh <dir of generated .cpp> <dir of moc/other .cpp> <main.cpp> <include dir of src/>
# PITFALL: mcpp's `clang++.cfg` appends libc++ headers unconditionally, so `clang++ -stdlib=libstdc++`
# silently uses libc++. Use the C driver `clang -x c++ --no-default-config` and prove the runtime first.
set -euo pipefail
GEN=$1; GEN2=$2; MAIN=$3; SRC=$4
LLVM=${LLVM:-$HOME/.mcpp/registry/data/xpkgs/xim-x-llvm/23.1.3}; GCC=$HOME/.mcpp/registry/data/xpkgs/xim-x-gcc/16.1.0
GLD=$HOME/.mcpp/registry/data/xpkgs/xim-x-glibc/2.44.3; LH=$HOME/.mcpp/registry/data/xpkgs/xim-x-linux-headers/5.11.1/include
Q=${QT_ROOT_DIR:-$HOME/.xlings/data/xpkgs/xim-x-qt/6.11.1}
X="--no-default-config -x c++ -std=c++23 -nostdlibinc -nostdinc++ -isystem $GCC/include/c++/16.1.0 -isystem $GCC/include/c++/16.1.0/x86_64-linux-gnu -isystem $GLD/include -isystem $LH"
printf '#include <string>\n#if defined(_LIBCPP_VERSION)\n#error RUNTIME=libc++\n#elif defined(__GLIBCXX__)\n#error RUNTIME=libstdc++\n#endif\n' > /tmp/rt.cpp
$LLVM/bin/clang $X -fsyntax-only /tmp/rt.cpp 2>&1 | grep -q 'RUNTIME=libstdc++' || { echo "not libstdc++"; exit 1; }
INC="-I$Q/include -I$Q/include/QtCore -I$Q/include/QtGui -I$Q/include/QtQml -I$Q/include/QtQuick -I$SRC -fPIC"
D=$(mktemp -d); cd "$D"
for f in $GEN/*.cpp $GEN2/*.cpp $MAIN; do $LLVM/bin/clang $X $INC -O1 -c -Wno-everything "$f" -o "$(basename "$f").o"; done
$LLVM/bin/clang --no-default-config -fuse-ld=lld --gcc-install-dir=$GCC/lib/gcc/x86_64-linux-gnu/16.1.0 -B$GLD/lib64 -L$GLD/lib64 \
  -Wl,--dynamic-linker=$GLD/lib64/ld-linux-x86-64.so.2 -Wl,-rpath,$GLD/lib64 *.o -o prog -L$Q/lib -lQt6Quick -lQt6Qml -lQt6Gui -lQt6Core \
  -L$GCC/lib64 -lstdc++ -lm -Wl,-rpath,$GCC/lib64 -Wl,-rpath,$Q/lib -Wl,-rpath-link,$Q/lib -Wl,--allow-shlib-undefined
QT_QPA_PLATFORM=offscreen LC_ALL=C.UTF-8 ./prog
