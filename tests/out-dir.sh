#!/bin/sh
# Where `mcpp::out_dir()` pointed in the newest build of the package in the
# current directory. mcpp 2026.10.10.1 and later give each configuration its own
# output directory and record it as the root package's `outDir` in
# resolution.json; earlier engines use target/.build-mcpp/out.
r=$(ls -t target/*/*/resolution.json 2>/dev/null | head -1)
if [ -n "$r" ]; then
    py=$(command -v python3 || command -v python)
    d=$("$py" -c 'import json, sys
for p in json.load(open(sys.argv[1])).get("graph", {}).get("packages", []):
    if p.get("root"):
        print(p.get("outDir", ""))
        break' "$r" 2>/dev/null)
    if [ -n "$d" ]; then
        echo "$d"
        exit 0
    fi
fi
echo "target/.build-mcpp/out"
