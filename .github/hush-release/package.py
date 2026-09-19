#!/usr/bin/env python3
"""Stage, relocate, smoke-test, and archive the native Hush generators."""

import argparse
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

SOURCE_DIR = Path(__file__).resolve().parents[2]
TOOLS = ("hush-export", "hush-reflection")
PLATFORMS = {
    "linux-x64": ("Linux", "x86_64"),
    "windows-x64": ("Windows", "x86_64"),
    "macos-arm64": ("Darwin", "arm64"),
    "macos-x64": ("Darwin", "x86_64"),
}


def run(*args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def check_host(target):
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    actual = (platform.system(), machine)
    if actual != PLATFORMS[target]:
        raise RuntimeError(f"Cannot label {actual} binaries as {target}")


def validate_dependencies(system, output):
    """Reject dependencies on build-machine libraries not shipped in the bundle."""
    if system == "Linux":
        allowed = {
            "linux-vdso.so.1", "libc.so.6", "libm.so.6", "libdl.so.2",
            "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2",
        }
        if "not found" in output:
            raise RuntimeError(f"Missing shared library:\n{output}")
        dependencies = [Path(line.split()[0]).name for line in output.splitlines()
                        if line.strip()]
        unexpected = set(dependencies) - allowed
    elif system == "Darwin":
        dependencies = [line.strip().split(" (", 1)[0]
                        for line in output.splitlines()[1:] if line.strip()]
        unexpected = {dep for dep in dependencies
                      if not dep.startswith(("/usr/lib/", "/System/Library/"))}
    else:
        # /MT avoids redistributing VC runtime DLLs. LLVM/Clang are static, too.
        allowed = {
            "kernel32.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
            "oleaut32.dll", "user32.dll", "ws2_32.dll", "psapi.dll",
            "version.dll", "ntdll.dll", "bcrypt.dll", "dbghelp.dll",
            "secur32.dll", "crypt32.dll", "rpcrt4.dll", "shlwapi.dll",
            "msvcrt.dll", "ucrtbase.dll",
        }
        dependencies = re.findall(r"^\s*([\w.-]+\.dll)\s*$", output, re.M | re.I)
        unexpected = {dep.lower() for dep in dependencies
                      if dep.lower() not in allowed
                      and not dep.lower().startswith(("api-ms-win-", "ext-ms-win-"))}
    if not dependencies:
        raise RuntimeError(f"No dependencies found in {system} audit:\n{output}")
    if unexpected:
        raise RuntimeError(f"Unbundled dependencies: {sorted(unexpected)}")


def audit_dependencies(binary):
    system = platform.system()
    command = {"Linux": ["ldd"], "Darwin": ["otool", "-L"],
               "Windows": ["dumpbin", "/dependents"]}[system]
    output = run(*command, binary, capture_output=True, text=True).stdout
    print(output, flush=True)
    validate_dependencies(system, output)
    return output


def require_text(path, *needles):
    text = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            raise RuntimeError(f"{path} does not contain {needle!r}")


def smoke_test(bundle):
    """Exercise the extracted package, not executables in the LLVM build tree."""
    suffix = ".exe" if platform.system() == "Windows" else ""
    binaries = {tool: bundle / "bin" / (tool + suffix) for tool in TOOLS}
    for binary in binaries.values():
        run(binary, "--help", stdout=subprocess.DEVNULL)

    with tempfile.TemporaryDirectory(prefix="hush source with spaces ") as tmp:
        work = Path(tmp)
        header = work / "probe.hpp"
        header.write_text('''#include <stddef.h>
#include <stdarg.h>
#include <vector>
static_assert(__has_cpp_attribute(hush::reflect));
static_assert(__has_cpp_attribute(hush::export));
static_assert(sizeof(size_t) == sizeof(void *));
using StandardLibraryProbe = std::vector<int>;
struct [[hush::reflect]] ReleaseProbe {
    [[hush::property]] int value;
};
struct [[hush::export]] ExportProbe { int value; };
[[hush::export]] int release_probe_add(int value);
''', encoding="utf-8")
        source = work / "probe.cpp"
        source.write_text('#include "probe.hpp"\n', encoding="utf-8")
        compiler_args = ["-std=c++20"]
        if platform.system() == "Darwin":
            sdk = run("xcrun", "--show-sdk-path", capture_output=True, text=True).stdout.strip()
            compiler_args += ["-isysroot", sdk, "-stdlib=libc++"]
        run(binaries["hush-reflection"], "--output-stamp=ReleaseSmoke.hushgen.cpp",
            source, "--", *compiler_args, cwd=work)
        require_text(work / "probe.hushgen.hpp", "ReleaseProbe", "RegisterReflection", "value")
        require_text(work / "ReleaseSmoke.hushgen.cpp", "ReleaseProbe")
        run(binaries["hush-export"], source, "--", *compiler_args, cwd=work)
        require_text(work / "HushBindings.h", "ExportProbe", "release_probe_add")
        require_text(work / "HushBindings.cpp", "release_probe_add")

        # Both tools must propagate parsing failures rather than reporting success.
        source.write_text("#error intentional_release_smoke_failure\n", encoding="utf-8")
        for binary in binaries.values():
            result = subprocess.run([str(binary), str(source), "--", *compiler_args],
                                    cwd=work, capture_output=True, text=True)
            if result.returncode == 0 or "intentional_release_smoke_failure" not in result.stderr:
                raise RuntimeError(f"{binary.name} failed the error-reporting smoke test")


def create_archive(bundle, output_dir, windows):
    archive_format = "zip" if windows else "gztar"
    return Path(shutil.make_archive(str(output_dir / bundle.name), archive_format,
                                    root_dir=bundle.parent, base_dir=bundle.name))


def extract_archive(archive, destination):
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package:
            package.extractall(destination)
    else:
        with tarfile.open(archive) as package:
            package.extractall(destination, filter="data")


def package(build_dir, output_dir, target, ref, runtime_licenses):
    check_host(target)
    if target.startswith("linux-") and not runtime_licenses:
        raise RuntimeError("Supply GNU runtime notices with --runtime-license for the static Linux build")
    build_dir = build_dir.resolve()
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    if any(output_dir.iterdir()):
        raise RuntimeError(f"Output directory must be empty: {output_dir}")
    revision = run("git", "rev-parse", "HEAD", cwd=SOURCE_DIR,
                   capture_output=True, text=True).stdout.strip()
    windows = target.startswith("windows-")
    suffix = ".exe" if windows else ""
    with tempfile.TemporaryDirectory(prefix="hush stage ") as tmp:
        bundle = Path(tmp) / f"hush-tools-{target}"
        (bundle / "bin").mkdir(parents=True)
        dependencies = {}
        for tool in TOOLS:
            filename = tool + suffix
            binary = bundle / "bin" / filename
            shutil.copy2(build_dir / "bin" / filename, binary)
            dependencies[filename] = audit_dependencies(binary)

        # ClangTool locates builtin headers relative to its executable:
        # <prefix>/bin/tool -> <prefix>/lib/clang/<version>/include.
        run("cmake", "--install", build_dir, "--config", "Release",
            "--component", "clang-resource-headers", "--prefix", bundle)
        if not list(bundle.glob("lib/clang/*/include/stddef.h")):
            raise RuntimeError("Clang builtin headers were not installed")
        licenses = bundle / "licenses"
        licenses.mkdir()
        for project in ("llvm", "clang"):
            shutil.copy2(SOURCE_DIR / project / "LICENSE.TXT", licenses / f"{project}.txt")
        shutil.copy2(SOURCE_DIR / "LICENSE.TXT", licenses / "LICENSE.TXT")
        if runtime_licenses:
            runtime_dir = licenses / "runtime"
            runtime_dir.mkdir()
            for license_file in runtime_licenses:
                shutil.copy2(license_file, runtime_dir)
        shutil.copy2(Path(__file__).with_name("README.md"), bundle / "README.md")
        (bundle / "build-info.json").write_text(json.dumps({
            "ref": ref, "revision": revision, "platform": target,
            "runner_os": platform.platform(), "dependencies": dependencies,
        }, indent=2) + "\n", encoding="utf-8")
        archive = create_archive(bundle, output_dir, windows)
        with tempfile.TemporaryDirectory(prefix="hush relocated package ") as extracted:
            extract_archive(archive, extracted)
            smoke_test(Path(extracted) / bundle.name)
        if windows:
            # Legacy engine releases download these names directly. New consumers
            # should use the ZIP, which also contains the required builtin headers.
            for tool in TOOLS:
                shutil.copy2(bundle / "bin" / (tool + suffix), output_dir)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--platform", choices=PLATFORMS, required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--runtime-license", type=Path, action="append", default=[],
                        help="Runtime licensing notice to include (repeatable; required on Linux)")
    args = parser.parse_args()
    package(args.build_dir, args.output_dir, args.platform, args.ref, args.runtime_license)


if __name__ == "__main__":
    main()
