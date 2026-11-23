# rules-qml spikes (2026-11-24)

Throwaway prototypes behind `../2026-11-24-rules-qml-design.md`. They are evidence, not the implementation:
the implementation is C++23 modules under `rules/` and `src/`. Section numbers refer to the design document.

| file | what it proves | how it was run |
|---|---|---|
| `closure.py` | the runtime-module closure (design Q1, §8) equals `qmlimportscanner`'s on 4 samples, including all 40 top-level Qt modules (24/27/15/49 modules, both difference sets empty) | `python3 -I closure.py <dir of .qml>` |
| `binary.hpp`, `needed_main.cpp` | `needed(path)` for ELF/PE/Mach-O (design Q2, §7): 180/180 SDK ELF files identical to `readelf -d`; mingw-built PE and lld-built Mach-O (thin and fat) identical to `objdump -p` / `llvm-otool -L`; 1500 truncated or corrupted inputs, no crash under ASan+UBSan | `g++ -std=c++23 -fsanitize=address,undefined needed_main.cpp -o needed` |
| `module-pipeline.build.mcpp`, `module-pipeline.Main.qml` | the per-module action chain (design §6 steps 1-6) for one dotted uri with a C++ type, a singleton, a `.js`, a nested file and a resource: `cpp=84 singleton=red js=42 nested=7 resource=hello-resource`; with a C++ `QML_SINGLETON` added: `theme=9` | a package whose `build.mcpp` is this file, `src/backend.h` with `QML_ELEMENT`, `qml/ui/*`, `xim:qt` 6.11.1, `mcpp run` |
| `multi-module.build.mcpp`, `multi-module.Main.qml` | two modules (`app.core` with C++ types and `Card.qml`, `app.ui` importing it); `--foreign-types` across modules gives `prototype: "Base"` for a class derived across modules; `sum=19`; the qmldir needs no `depends` line | same as above, ASCII path without spaces |
| `llvm-check.sh` | the generated sources compile with clang (20.1.7, 22.1.8, 23.1.3, 23.1.6) against the real libstdc++, link with lld and run with the same output as gcc | see the pitfall in the script header |

Not covered by any spike: Windows, macOS, `qsb` (not runnable from `xim:qt-addons`), `qmllint`, `qt.conf` under a macOS bundle.
