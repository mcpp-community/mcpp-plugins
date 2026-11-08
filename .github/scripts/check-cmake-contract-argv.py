#!/usr/bin/env python3
"""Exercise the generated contract toolchain with actual compiler arguments."""
import argparse
import json
import sys
from pathlib import Path
import shutil
import subprocess
import tempfile


def checked(command, cwd, log):
    result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        log.flush()
        details = Path(log.name).read_text(errors="replace")[-8000:]
        details = details.replace(str(cwd), "<fixture>").replace(str(Path.home()), "<home>")
        raise AssertionError(f"{Path(str(command[0])).name} exited {result.returncode}\n{details}")


def check_clang_target(root, toolchain, manifest, cmake, clang):
    compiler = Path(clang).resolve()
    ar, rc = (compiler.parent / name for name in ("llvm-ar", "llvm-rc"))
    assert ar.is_file() and rc.is_file(), "missing LLVM archive/resource tools"
    source = root / "clang-target-source"
    source.mkdir()
    (source / "probe.c").write_text('''#if !defined(_WIN32) || !defined(_MSC_VER) || !defined(_M_X64)
#error wrong_target
#endif
typedef char pointer_width[(sizeof(void*) == 8) ? 1 : -1];
int answer(void) { return 42; }
''')
    (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.19)
project(target_identity C)
add_library(probe STATIC probe.c)
file(WRITE "${CMAKE_BINARY_DIR}/identity.txt" "${CMAKE_C_COMPILER_ID}|${CMAKE_C_SIMULATE_ID}|${CMAKE_C_COMPILER_FRONTEND_VARIANT}|${CMAKE_C_PLATFORM_ID}|${CMAKE_SIZEOF_VOID_P}|${CMAKE_C_COMPILER_TARGET}")
''')
    contract = {
        "schema": 1, "dialect": "gnu", "target": "logical-platform-alias",
        "compile": {name: {"program": str(compiler), "args": [
            "--target=aarch64-linux-gnu", "-target", "x86_64-pc-windows-msvc"]}
            for name in ("c", "cxx", "asm")},
        "link": {"program": str(compiler.parent / "lld-link"), "archiver": str(ar), "args": []}}
    manifest.write_text(json.dumps(contract))
    for generator in ("Ninja", "Unix Makefiles", "Ninja Multi-Config"):
        build = root / ("clang-target-" + generator.replace(" ", "-"))
        with (root / "private.log").open("w") as log:
            checked([cmake, "-S", str(source), "-B", str(build), "-G", generator,
                     "-DCMAKE_SYSTEM_NAME=Windows", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                     "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DMCPP_CONTRACT=" + str(manifest),
                     "-DCMAKE_RC_COMPILER=" + str(rc)], root, log)
            checked([cmake, "--build", str(build), "--config", "Release"], root, log)
            identity = (build / "identity.txt").read_text()
            assert identity == "Clang|MSVC|GNU|Windows|8|x86_64-pc-windows-msvc", f"compiler target identity differs: {identity}"
            archive = build / "Release/probe.lib" if generator == "Ninja Multi-Config" else build / "probe.lib"
            assert archive.is_file() and not (build / "libprobe.a").exists(), "target archive naming differs"
            member = subprocess.check_output([str(ar), "p", str(archive)])
            assert member[:2] == b"\x64\x86", "archive member is not AMD64 COFF"
        print(f"PASS: {generator}: retargeted Clang identity, ABI, archive naming and actual COFF")


def check_empty_argument(root, source, toolchain, manifest, contract, cmake):
    (source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.19)\nproject(empty_argument NONE)\n")
    unit = root / "empty-argument.c"
    unit.write_text("int answer(void) { return 42; }\n")
    probe = json.loads(json.dumps(contract))
    # Some drivers ignore an empty input filename. Place the empty word where
    # -D must consume it: dropping it would instead consume the valid fallback.
    probe["compile"]["c"]["args"] = ["-DMCPP_EMPTY_ARGUMENT_CONTROL=1", "-D"]
    build = root / "empty-argument-build"
    with (root / "private.log").open("w") as log:
        configure = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja",
                     "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DMCPP_CONTRACT=" + str(manifest)]
        for empty in (False, True):
            if empty:
                probe["compile"]["c"]["args"].append("")
            manifest.write_text(json.dumps(probe))
            if empty:
                # LLVM's GNU response parser discards quoted empty words.
                # Reject the lossy representation instead of running it.
                result = subprocess.run(configure, cwd=root, capture_output=True, text=True)
                assert result.returncode > 0 and "empty GNU argument" in result.stdout + result.stderr
                command = [probe["compile"]["c"]["program"], *probe["compile"]["c"]["args"],
                           "MCPP_EMPTY_WORD_FALLBACK=1", "-x", "c", "-c", str(unit),
                           "-o", str(root / "empty-argument.o")]
                native = subprocess.run(command, cwd=root, capture_output=True, text=True)
                assert native.returncode > 0, "the empty native argument control must be invalid"
                continue
            before = set(toolchain.parent.glob("*.rsp"))
            checked(configure, root, log)
            added = set(toolchain.parent.glob("*.rsp")) - before
            response = [p for p in added if b"MCPP_EMPTY_ARGUMENT_CONTROL" in p.read_bytes()]
            assert len(response) == 1, "expected a newly addressed response file"
            command = [probe["compile"]["c"]["program"], "@" + str(response[0]),
                       "MCPP_EMPTY_WORD_FALLBACK=1", "-x", "c", "-c", str(unit),
                       "-o", str(root / "empty-argument.o")]
            checked(command, root, log)
    print("PASS: a lossy empty GNU argv word is rejected; the actual native invocation is invalid")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("toolchain", type=Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--msvc-preprocessor", help="Also compile a COFF probe with clang-cl or cl")
    parser.add_argument("--target-clang", help="Also verify retargeted Clang identity and a Windows static library without an SDK")
    args = parser.parse_args()
    helper = args.toolchain.resolve().read_bytes()
    values = ["hello world", 'literal "quotes"', "back\\slash\\", "$MCPP_ARG_WORD", "a;b", "tab\tvalue", "", "café Ω"]
    with tempfile.TemporaryDirectory(prefix="mcpp-cmake-argv-") as temp:
        root = Path(temp) / "space Ω $literal"
        root.mkdir()
        helper_dir = root / "contract 'quote"
        helper_dir.mkdir()
        toolchain = helper_dir / "contract.cmake"
        toolchain.write_bytes(helper)
        source = root / "source"
        (source / "child").mkdir(parents=True)
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.16)
project(contract_words C CXX)
include(CheckCSourceCompiles)
check_c_source_compiles("#ifndef ONLY_C\\n#error contract_missing\\n#endif\\nint main(void){return 0;}" CONTRACT_PROBE)
if(NOT CONTRACT_PROBE)
  message(FATAL_ERROR "contract arguments did not reach try_compile")
endif()
add_subdirectory(child)
''')
        (source / "child/CMakeLists.txt").write_text("add_executable(probe_c probe.c)\nadd_executable(probe_cxx probe.cpp)\n")
        code = '''#include <stdio.h>
#ifdef __cplusplus
#if !defined(ONLY_CXX) || defined(ONLY_C)
#error wrong_language_options
#endif
#else
#if !defined(ONLY_C) || defined(ONLY_CXX)
#error wrong_language_options
#endif
#endif
#if !defined(PROBE_ORDER) || PROBE_ORDER != 1
#error option_group_order_lost
#endif
int main(void) {
'''
        code += "".join(f"puts(PROBE_{i});\n" for i in range(len(values))) + "return 0;\n}\n"
        for name in ("probe.c", "probe.cpp"):
            (source / "child" / name).write_text(code)
        definitions = [f"-DPROBE_{i}={json.dumps(v, ensure_ascii=False).replace(chr(92) + 't', chr(9))}" for i, v in enumerate(values)]
        contract = {"schema": 1, "dialect": "gnu", "compile": {
            "c": {"program": shutil.which(args.cc), "args": [*definitions, "-DONLY_C", "-DPROBE_ORDER=1", "-UPROBE_ORDER", "-DPROBE_ORDER=1"]},
            "cxx": {"program": shutil.which(args.cxx), "args": [*definitions, "-DONLY_CXX", "-DPROBE_ORDER=1", "-UPROBE_ORDER", "-DPROBE_ORDER=1"]},
            "asm": {"program": shutil.which(args.cc), "args": []}},
            "link": {"program": shutil.which(args.cxx), "archiver": shutil.which("ar"), "args": []}}
        assert all(v["program"] for v in contract["compile"].values()), "missing compiler"
        if sys.platform.startswith("linux"):
            contract["link"]["args"] = ["-Wl,-rpath,$ORIGIN/probe space"]
        manifest = root / "contract.json"
        for generator in ("Ninja", "Unix Makefiles"):
            build = root / generator.replace(" ", "-")
            manifest.write_text(json.dumps(contract))
            configure = [args.cmake, "-S", str(source), "-B", str(build), "-G", generator,
                         "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DMCPP_CONTRACT=" + str(manifest)]
            with (root / "private.log").open("w") as log:
                checked(configure, root, log)
                checked([args.cmake, "--build", str(build), "--verbose"], root, log)
                for name in ("probe_c", "probe_cxx"):
                    output = subprocess.check_output([str(build / "child" / name)], text=True)
                    assert output.splitlines() == values, (generator, name)
                    if sys.platform.startswith("linux"):
                        dynamic = subprocess.check_output(["readelf", "-d", str(build / "child" / name)], text=True)
                        assert "$ORIGIN/probe space" in dynamic, "literal linker path changed"
                objects = {p: p.stat().st_mtime_ns for p in build.rglob("*.o")}
                assert objects, "no compilation objects"
                checked(configure, root, log)
                if generator == "Ninja":
                    checked([shutil.which("ninja"), "-C", str(build), "-d", "explain", "-n"], root, log)
                checked([args.cmake, "--build", str(build), "--verbose"], root, log)
                rebuilt = [str(p.relative_to(build)) for p, stamp in objects.items() if p.stat().st_mtime_ns != stamp]
                if rebuilt:
                    log.flush()
                    details = Path(log.name).read_text(errors="replace")[-8000:].replace(str(root), "<fixture>").replace(str(Path.home()), "<home>")
                    raise AssertionError(f"unchanged flags rebuilt objects: {rebuilt}\n{details}")
                changed = json.loads(json.dumps(contract))
                changed["compile"]["c"]["args"][0] = '-DPROBE_0="changed value"'
                manifest.write_text(json.dumps(changed))
                checked(configure, root, log)
                checked([args.cmake, "--build", str(build)], root, log)
                assert subprocess.check_output([str(build / "child/probe_c")], text=True).splitlines() == ["changed value", *values[1:]]
                assert subprocess.check_output([str(build / "child/probe_cxx")], text=True).splitlines() == values
            print(f"PASS: {generator}: literal arguments, language scope, try_compile and incremental identity")
        check_empty_argument(root, source, toolchain, manifest, contract, args.cmake)
        if args.msvc_preprocessor:
            contract["dialect"] = "msvc"
            for spec in contract["compile"].values():
                spec["program"] = str(Path(args.msvc_preprocessor).resolve())
                spec["args"] = ["/utf-8", *[f"/DPROBE_{i}={json.dumps(v, ensure_ascii=False).replace(chr(92) + 't', chr(9))}" for i, v in enumerate(values)]]
            manifest.write_text(json.dumps(contract))
            (source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.16)\nproject(response_only NONE)\n")
            with (root / "private.log").open("w") as log:
                checked([args.cmake, "-S", str(source), "-B", str(root / "msvc"), "-G", "Ninja",
                         "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DMCPP_CONTRACT=" + str(manifest)], root, log)
                generated = [p for p in helper_dir.glob("*.rsp") if p.read_bytes().startswith(b"\xef\xbb\xbf") and b"/DPROBE_0=" in p.read_bytes()]
                assert len(generated) == 1, "expected one content-addressed MSVC response file"
                probe = root / "coff.c"
                probe.write_text("".join(f"const char probe_{i}[] = PROBE_{i};\n" for i in range(len(values))))
                output = root / "coff.obj"
                checked([args.msvc_preprocessor, "/nologo", "/c", "/TC", "@" + str(generated[0]), str(probe), "/Fo" + str(output)], root, log)
                data = output.read_bytes()
                assert all(v.encode() + b"\0" in data for v in values), "COFF literal values differ"
            print("PASS: MSVC response quoting: actual compiler preserves all literal values in COFF")
        if args.target_clang:
            check_clang_target(root, toolchain, manifest, args.cmake, args.target_clang)


if __name__ == "__main__":
    main()
