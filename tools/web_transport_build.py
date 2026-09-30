#!/usr/bin/env python3
"""Build the local native/browser transport experiment without Halo assets."""
import argparse
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = (
    ('libdatachannel', 'https://github.com/paullouisageneau/libdatachannel.git',
     '6b1e2e620f1e37f0eafeee702eaea0043cb305fd'),  # v0.24.6
    ('mbedtls', 'https://github.com/Mbed-TLS/mbedtls.git',
     '068ff080b369adfac81509f9b57b2afabaf82dc5'),  # mbedtls-3.6.7
)


def run(*args, cwd=ROOT):
    print('+', ' '.join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def output(*args, cwd):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True).strip()


def prepare():
    cache = ROOT / 'build/third_party'
    cache.mkdir(parents=True, exist_ok=True)
    for name, url, revision in DEPENDENCIES:
        source = cache / name
        if not source.exists():
            run('git', 'clone', '--filter=blob:none', '--no-checkout', url, source)
            run('git', 'checkout', '--detach', revision, cwd=source)
        if output('git', 'rev-parse', 'HEAD', cwd=source) != revision:
            raise SystemExit(f'{source} has a different revision; preserve it and move it aside before retrying')
        if output('git', 'status', '--porcelain', '--untracked-files=normal', '--ignore-submodules=all', cwd=source):
            raise SystemExit(f'{source} has local edits; preserve them before retrying')
        # Submodule commits come from the pinned parent's gitlinks. Never reset
        # an already-populated checkout that might contain someone else's work.
        status = output('git', 'submodule', 'status', '--recursive', cwd=source)
        if any(line.startswith(('+', 'U')) for line in status.splitlines()):
            raise SystemExit(f'{source} has a modified submodule revision; preserve it before retrying')
        run('git', 'submodule', 'update', '--init', '--recursive', cwd=source)
        if output('git', 'status', '--porcelain', '--untracked-files=normal', cwd=source):
            raise SystemExit(f'{source} or a submodule has local edits; preserve them before retrying')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--platform', choices=('native', 'simulator', 'device'), default='native')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 4))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.platform != 'native' and (platform.system() != 'Darwin' or platform.machine() != 'arm64'):
        parser.error('Apple Silicon and full Xcode are required for iOS builds')
    prepare()
    if args.prepare_only:
        return
    build = ROOT / f'build/web/{args.platform}'
    command = ['cmake', '-S', ROOT/'port/web', '-B', build,
               '-DCMAKE_BUILD_TYPE=Release', '-DHALO_WEB_USE_MBEDTLS=ON', '-DHALO_WEB_BUILD_PROBES=ON']
    if args.platform == 'native':
        command += ['-G', 'Ninja']
    else:
        sdk = 'iphonesimulator' if args.platform == 'simulator' else 'iphoneos'
        command += ['-G', 'Xcode', '-DCMAKE_SYSTEM_NAME=iOS', f'-DCMAKE_OSX_SYSROOT={sdk}',
                    '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=16.0']
    run(*command)
    command = ['cmake', '--build', build, '--config', 'Release', '--parallel', args.jobs]
    if args.platform != 'native':
        command += ['--target', 'HaloWebProbe', '--', 'CODE_SIGNING_ALLOWED=NO', '-quiet']
    run(*command)
    if args.platform == 'native':
        run(build/'halo-web-socket-probe')
        if platform.system() == 'Darwin':
            run(build/'halo-web-posix-probe')
    else:
        print(f'Probe app: {build}/Release-{sdk}/HaloWebProbe.app')


if __name__ == '__main__':
    main()
