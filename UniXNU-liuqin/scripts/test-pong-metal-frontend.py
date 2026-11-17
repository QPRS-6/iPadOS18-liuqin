#!/usr/bin/env python3
"""Host regression tests of the real frontend with a recording C ABI; no GPU."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/PongMetal'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--skip-fatal-fixtures', action='store_true',
               help='Run frontend/gate checks only; record fatal child-process fixtures as skipped')
p.add_argument('--test-public-backing-regression', action='store_true',
               help='Also prove the former public-buffer lookup fails the IOSurface synchronization test')
a = p.parse_args()
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
test = DRIVER / 'tests/FrontendTests.mm'
header = (DRIVER / 'bridge/NativeMetalObjects.h').read_text()
header = re.sub(r'/\*.*?\*/|//[^\n]*', '', header, flags=re.S)
declarations = re.findall(r'^([A-Za-z_][\w\s*]*?\b(nmb_\w+)\s*\(.*?\))\s*;', header, flags=re.M | re.S)
implemented = set(re.findall(r'^extern "C" [^\n]*?\b(nmb_\w+)\(', test.read_text(), flags=re.M))
stubs = ['#include "bridge/NativeMetalObjects.h"', '#include <cstdio>', '#include <cstdlib>']
seen = set()
for declaration, name in declarations:
    if name not in implemented and name not in seen:
        stubs.append(f'extern "C" {declaration} {{std::fputs("Unexpected C ABI call: {name}\\n",stderr);std::abort();}}')
    seen.add(name)
if len(declarations) < 100 or not implemented.issubset({name for _, name in declarations}):
    raise SystemExit('C ABI declaration discovery failed')
commands = []
def run(command, log):
    commands.append([str(x) for x in command])
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / log).write_text(result.stdout)
    if result.returncode:
        print(result.stdout)
        raise SystemExit(f'FAIL ({result.returncode}): {log}')
    return result.stdout

with tempfile.TemporaryDirectory(prefix='pong-metal-test-', dir=os.environ.get('TMPDIR')) as tmp:
    tmp = Path(tmp)
    stub = tmp / 'unexpected.cpp'
    stub.write_text('\n'.join(stubs) + '\n')
    common = ['xcrun', '--sdk', 'macosx', 'clang++', '-std=c++17', '-O1', '-g', '-fno-omit-frame-pointer',
              '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Werror',
              '-Wno-deprecated-declarations', '-I' + str(DRIVER)]
    # Unused named arguments are deliberate in generated, abort-only stubs.
    run(common + ['-Wno-unused-parameter', '-c', stub, '-o', tmp / 'unexpected.o'], 'stubs-build.log')
    run(common + ['-fobjc-arc', '-fblocks', test, tmp / 'unexpected.o', '-framework', 'Foundation',
                  '-framework', 'Metal', '-framework', 'IOSurface', '-framework', 'QuartzCore',
                  '-framework', 'CoreGraphics', '-o', tmp / 'frontend-tests'], 'frontend-build.log')
    run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-dynamiclib',
         DRIVER / 'tests/HostSyncMock.cpp','-o',tmp / 'host-sync.dylib'], 'sync-mock-build.log')
    os.environ['PONG_TEST_SYNC_LIB'] = str(tmp / 'host-sync.dylib')
    print(run([tmp / 'frontend-tests'], 'frontend-tests.log'), end='')
    if a.test_public_backing_regression:
        frontend = (DRIVER / 'NativeBridgeMetalFrontend.mm').read_text()
        before = 'addBuffer([texture hostImportBacking])'
        if frontend.count(before) != 1:
            raise SystemExit('Unexpected backing-lookup source for regression fixture')
        old = tmp / 'old-frontend.mm'
        old.write_text(frontend.replace(before, 'addBuffer(texture.buffer)', 1))
        fixture = tmp / 'old-test.mm'
        fixture.write_text(test.read_text().replace('#include "../NativeBridgeMetalFrontend.mm"', '#include ' + json.dumps(str(old)), 1))
        run(common + ['-fobjc-arc', '-fblocks', fixture, tmp / 'unexpected.o', '-framework', 'Foundation',
                      '-framework', 'Metal', '-framework', 'IOSurface', '-framework', 'QuartzCore',
                      '-framework', 'CoreGraphics', '-o', tmp / 'old-tests'], 'old-backing-build.log')
        result = subprocess.run([tmp / 'old-tests'], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
        (out / 'old-backing-tests.log').write_text(result.stdout)
        if result.returncode != 1 or 'FAIL assertion: *calls==1' not in result.stdout:
            raise SystemExit('Former IOSurface backing lookup did not fail the expected synchronization assertion')
        print('PASS regression: former public buffer getter never synchronizes IOSurfaces; private backing fix passes')
    run(['xcrun', '--sdk', 'macosx', 'clang++', '-std=c++17', '-O1', '-g', '-fno-omit-frame-pointer',
         '-fobjc-arc', '-fblocks', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
         DRIVER / 'tests/AppDiagnosticsTests.mm', '-framework', 'Foundation', '-o', tmp / 'Freeform'], 'diagnostics-build.log')
    print(run([tmp / 'Freeform', 'gates'], 'diagnostics-gates.log'), end='')
    fixtures = [] if a.skip_fatal_fixtures else [('fatal', -signal.SIGABRT, 'fatal signal=6'),
                               ('exception', -signal.SIGABRT, 'uncaught name=PongTestException'),
                               ('existing', 42, 'existing handler preserved')]
    for mode, status, text in fixtures:
        command = [str(tmp / 'Freeform'), mode]
        commands.append(command)
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
        (out / f'diagnostics-{mode}.log').write_text(result.stdout)
        if result.returncode != status or text not in result.stdout:
            print(result.stdout)
            raise SystemExit(f'FAIL diagnostics {mode}: exit={result.returncode}, expected={status}')
        print(f'PASS diagnostics {mode}: evidence captured, original termination/handler preserved')
(out / 'result.json').write_text(json.dumps(dict(result='PASS', scope='real Objective-C frontend, recording C ABI, UBSan; no GPU',
    fatal_fixtures='SKIP by explicit flag' if a.skip_fatal_fixtures else 'PASS',
    public_backing_regression='PASS' if a.test_public_backing_regression else 'not run',commands=commands), indent=2) + '\n')
