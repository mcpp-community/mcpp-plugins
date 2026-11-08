#!/usr/bin/env python3
"""Exercise CMake cache selection through the real mcpp adapter."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def checked(command, project, log):
    result = subprocess.run(command, cwd=project, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        log.flush()
        details = Path(log.name).read_text(errors="replace")[-8000:]
        details = details.replace(str(project), "<fixture>").replace(str(Path.home()), "<home>")
        raise AssertionError(f"mcpp exited {result.returncode}\n{details}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mcpp")
    parser.add_argument("--target", default="x86_64-linux-musl")
    args = parser.parse_args()
    binary = str(Path(shutil.which(args.mcpp) or args.mcpp).resolve())
    repository = Path(__file__).resolve().parents[2]
    fixture = repository / "tests/cmake-cross"
    with tempfile.TemporaryDirectory(prefix="mcpp-cmake-cache-") as temporary:
        project = Path(temporary) / "project"
        shutil.copytree(fixture, project, ignore=shutil.ignore_patterns("target", ".mcpp", ".xlings"))
        manifest = project / "mcpp.toml"
        text = manifest.read_text().replace('path = "../.."', "path = " + json.dumps(str(repository)))
        assert text != manifest.read_text(), "fixture dependency path was not replaced"
        manifest.write_text(text)
        cmake = project / "answer/CMakeLists.txt"
        cmake.write_text(cmake.read_text() + r'''
file(WRITE "${CMAKE_BINARY_DIR}/mcpp-cache-probe.json"
  "{\"epoch\":${MCPP_CACHE_PROBE_EPOCH},\"generator\":\"${CMAKE_GENERATOR}\",\"system\":\"${CMAKE_SYSTEM_NAME}\"}")
''')
        selections = [
            ("Ninja", "Linux"), ("Unix Makefiles", "Linux"), ("Unix Makefiles", "Linux"),
            ("Ninja", "Linux"), ("Ninja", "Linux"),
            ("Ninja", "Generic"), ("Ninja", "Generic"), ("Ninja", "Linux"),
            ("Ninja Multi-Config", "Linux"), ("Ninja Multi-Config", "Linux")]
        seen = {}
        for epoch, (generator, system) in enumerate(selections, 1):
            objects = {p: p.stat().st_mtime_ns for p in project.glob("target/.build-mcpp/out/*/cmake-cross/deps-cmake/answer/build-*/**/*.o")}
            options = ["-G", generator, f"-DMCPP_CACHE_PROBE_EPOCH={epoch}"]
            words = ", ".join(json.dumps(value) for value in options)
            (project / "build.mcpp").write_text('''import std;
import mcpp;
import mcpp.deps.cmake;
int main() {
  mcpp::deps::cmake::options o;
  o.source = "answer";
  o.libraries = {"answer"};
''' + f'  o.cache_args = {{{words}}};\n  o.system_name = "{system}";\n' + '''  return mcpp::deps::cmake::use(o) ? 0 : 1;
}
''')
            with (project / "private.log").open("w") as log:
                checked([binary, "build", "--target", args.target, "--profile", "dev"], project, log)
            current = []
            for path in project.glob("target/.build-mcpp/out/*/cmake-cross/deps-cmake/answer/build-*/mcpp-cache-probe.json"):
                record = json.loads(path.read_text())
                if record["epoch"] == epoch:
                    current.append((path.parent, record))
            assert len(current) == 1, "the active CMake configure did not write its current epoch"
            build, record = current[0]
            assert record["generator"] == generator and record["system"] == system, record
            selection = (generator, system)
            if selection in seen:
                assert build == seen[selection], "returning to a selection lost its CMake cache"
                assert all(p.stat().st_mtime_ns == stamp for p, stamp in objects.items() if p.is_relative_to(build)), "unchanged compilation objects rebuilt"
            else:
                assert build not in seen.values(), "incompatible selections share a CMake cache"
                seen[selection] = build
            applications = list((project / "target" / args.target).glob("*/bin/cmake-cross"))
            assert len(applications) == 1
            output = subprocess.check_output([str(applications[0])], text=True).strip()
            assert output == "cmake-cross: the subproject was compiled against not glibc", output
            print(f"PASS: selection {epoch}: {generator}/{system}; actual target program and cache identity")


if __name__ == "__main__":
    main()
