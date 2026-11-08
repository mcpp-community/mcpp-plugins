#!/usr/bin/env python3
"""Check actual rule outputs; no device or installed GPU driver is required."""
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

expected = sys.argv[1].split(',')
cuda_target = next((arg.split('=', 1)[1] for arg in sys.argv[2:]
                    if arg.startswith('--cuda-target=')), None)
graph = max(Path('target').glob('*/*/build.ninja'), key=lambda p: p.stat().st_mtime)
text = graph.read_text()
assert 'src/cuda/unused.cu' not in text, 'a pure SYCL request compiled the retained CUDA group'
commands = [line.strip().removeprefix('command = ') for line in text.splitlines()
            if line.strip().startswith('command = ') and '-fsycl-link ' in line]
assert len(commands) == 1, 'expected one rule device-link command'
command = shlex.split(commands[0])
with tempfile.TemporaryDirectory(prefix='sycl-images-') as temporary:
    work = Path(temporary)
    # Keep the rule's exact compiler, flags and inputs; save intermediates from
    # the device link so the test verifies images rather than just flag spelling.
    command.insert(1, '-save-temps=obj')
    command[command.index('-o') + 1] = str(work / 'device-link.o')
    subprocess.run(command, cwd=work, check=True, timeout=180)
    images = []
    for table in work.glob('*.table'):
        lines = table.read_text().splitlines()
        if not lines or lines[0] != '[Code|Properties|Symbols]':
            continue
        for line in lines[1:]:
            path = Path(line.split('|')[0])
            assert path.is_file() and path.stat().st_size > 100, str(path)
            images.append((table.name, path.read_bytes()[:4]))
    for target in expected:
        if target == 'spir64':
            assert any(magic == bytes.fromhex('03022307') for _, magic in images), images
        else:
            assert any(target in name and magic == bytes.fromhex('50ed55ba')
                       for name, magic in images), (target, images)
    if cuda_target:
        cuda_commands = [line.strip().removeprefix('command = ') for line in text.splitlines()
                         if line.strip().startswith('command = ') and ' -x cuda ' in line]
        assert len(cuda_commands) == 1, 'expected a separate CUDA compilation action'
        cuda = shlex.split(cuda_commands[0])
        cuda.insert(1, '-save-temps=obj')
        cuda[cuda.index('-o') + 1] = str(work / 'cuda.o')
        subprocess.run(cuda, cwd=work, check=True, timeout=180)
        ptx = [file.read_text() for file in work.glob('*.s')]
        assert any('.target ' + cuda_target in file for file in ptx), cuda_target
        print('PASS: the independent CUDA action emits', cuda_target)
    print('PASS: actual device images:', ', '.join(expected))
