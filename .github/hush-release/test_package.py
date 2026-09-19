import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from package import (
    check_host,
    create_archive,
    extract_archive,
    require_text,
    validate_dependencies,
)


class DependencyTests(unittest.TestCase):
    def test_linux_system_libraries(self):
        validate_dependencies("Linux", """
linux-vdso.so.1 (0x00007ffc)
libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x00007fff)
libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007fff)
/lib64/ld-linux-x86-64.so.2 (0x00007fff)
""")

    def test_linux_rejects_missing_and_unbundled_libraries(self):
        for output in (
            "libclang.so => not found",
            "libstdc++.so.6 => /lib/libstdc++.so.6 (0x00007fff)",
            "libLLVM.so => /build/libLLVM.so (0x00007fff)",
        ):
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                validate_dependencies("Linux", output)

    def test_macos_system_libraries(self):
        validate_dependencies("Darwin", """/tmp/package/bin/hush-reflection:
    /usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1900.0.0)
    /usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)
    /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation (compatibility version 150.0.0)
""")

    def test_macos_rejects_homebrew_and_rpath_libraries(self):
        for library in ("/opt/homebrew/lib/libzstd.1.dylib", "@rpath/libclang.dylib"):
            with self.subTest(library=library), self.assertRaises(RuntimeError):
                validate_dependencies("Darwin", f"tool:\n    {library} (compatibility version 1.0.0)\n")

    def test_windows_system_libraries(self):
        validate_dependencies("Windows", """Microsoft (R) COFF/PE Dumper Version 14
Dump of file C:\\temp\\hush-export.exe
  Image has the following dependencies:
    KERNEL32.dll
    ADVAPI32.dll
    api-ms-win-core-synch-l1-2-0.dll
  Summary
""")

    def test_windows_rejects_dynamic_vc_and_llvm(self):
        for library in ("VCRUNTIME140.dll", "MSVCP140.dll", "LLVM.dll", "clang.dll"):
            with self.subTest(library=library), self.assertRaises(RuntimeError):
                validate_dependencies("Windows", f"    {library}\n")

    def test_empty_audit_is_not_success(self):
        for system in ("Linux", "Darwin", "Windows"):
            with self.subTest(system=system), self.assertRaises(RuntimeError):
                validate_dependencies(system, "")


class PackageTests(unittest.TestCase):
    def test_archive_layout_and_permissions(self):
        for windows in (False, True):
            with self.subTest(windows=windows), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                bundle = root / "stage with spaces" / "hush-tools-test"
                binary = bundle / "bin" / ("hush-export.exe" if windows else "hush-export")
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b"test binary")
                binary.chmod(0o755)
                header = bundle / "lib" / "clang" / "23" / "include" / "stddef.h"
                header.parent.mkdir(parents=True)
                header.write_text("// builtin header\n", encoding="utf-8")
                output = root / "dist"
                output.mkdir()
                archive = create_archive(bundle, output, windows)
                self.assertEqual(archive.name, "hush-tools-test.zip" if windows else "hush-tools-test.tar.gz")
                extracted = root / "extracted with spaces"
                extract_archive(archive, extracted)
                extracted_binary = extracted / bundle.name / "bin" / binary.name
                self.assertEqual(extracted_binary.read_bytes(), b"test binary")
                self.assertTrue((extracted / bundle.name / header.relative_to(bundle)).is_file())
                if not windows and os.name != "nt":
                    self.assertTrue(os.access(extracted_binary, os.X_OK))

    def test_generated_output_is_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "generated.hpp"
            output.write_text("ReleaseProbe RegisterReflection", encoding="utf-8")
            require_text(output, "ReleaseProbe", "RegisterReflection")
            with self.assertRaises(RuntimeError):
                require_text(output, "missing")

    @patch("package.platform.system", return_value="Windows")
    @patch("package.platform.machine", return_value="AMD64")
    def test_windows_architecture_alias(self, *_):
        check_host("windows-x64")
        with self.assertRaises(RuntimeError):
            check_host("macos-x64")

    @patch("package.platform.system", return_value="Darwin")
    @patch("package.platform.machine", return_value="arm64")
    def test_macos_architecture_must_match(self, *_):
        check_host("macos-arm64")
        with self.assertRaises(RuntimeError):
            check_host("macos-x64")


if __name__ == "__main__":
    unittest.main()
