# Hush generator release bundles

The Hush-owned workflow is `.github/workflows/hush-release.yml`. Leave upstream
LLVM workflows disabled through GitHub's workflow settings; their YAML files do
not need modification.

## Platforms and assets

| Bundle | Architecture | Build runner / supported baseline |
| --- | --- | --- |
| `hush-tools-linux-x64.tar.gz` | x86-64 | Ubuntu 22.04; glibc 2.35 or newer |
| `hush-tools-windows-x64.zip` | x86-64 | Windows Server 2022 runner; Windows 10/Server 2022 or newer intended |
| `hush-tools-macos-arm64.tar.gz` | Apple Silicon | macOS 15 runner; deployment target macOS 14 |
| `hush-tools-macos-x64.tar.gz` | Intel | macOS 15 Intel runner; deployment target macOS 14 |

Deployment targets are not a substitute for testing on the oldest OS. The smoke
tests run on the listed build runners. Linux/Windows ARM64, older Linux glibc,
musl-based distributions, and a universal macOS binary are not provided.

Each archive has a `hush-tools-<platform>/` root containing:

- `bin/hush-export` and `bin/hush-reflection` (`.exe` on Windows).
- `lib/clang/<version>/include/`, the matching Clang builtin headers.
- `licenses/`, LLVM/Clang notices and GNU runtime notices for the Linux bundle.
- `build-info.json`, recording the source commit, ref, platform, and native
  dependency inspection output.
- This README.

LLVM and Clang libraries are linked statically. Linux also statically links the
GNU C++ runtime; Windows uses the static MSVC runtime. macOS retains its system
libc++. Optional compression, XML, editing, and HTTP libraries are disabled to
avoid accidental dependencies on runner-installed libraries. The packager rejects
unexpected dynamic dependencies rather than silently publishing incomplete tools.

These are **code generators, not a complete compiler SDK**. Parsing project code
still requires a compatible C++ development environment and the project's include
paths/defines: GCC/libstdc++ development headers on Linux, Visual Studio C++ tools
and the Windows SDK on Windows, or Xcode Command Line Tools/SDK on macOS. For
macOS, callers may need to pass `-isysroot "$(xcrun --show-sdk-path)"` after `--`,
or supply it through their compilation database.

## Download and use

1. Download the platform archive and `SHA256SUMS` from the same release.
2. Verify the archive's SHA-256 against `SHA256SUMS`.
3. Extract the **whole archive**, preserving the `bin/` and `lib/` layout.
4. Point the engine's generator configuration at the executables in `bin/`.

Do not copy just the executables into another directory: Clang's builtin-header
lookup is relative to the executable location. `hush-export.exe` and
`hush-reflection.exe` are also published as legacy compatibility assets, but new
consumers should use the full ZIP instead. Engine versions that download only
those files should migrate to extracting the bundle and pinning its checksum.
The generator's version must match the engine's expected generated-code API.

macOS executables are not Developer ID signed or notarized; this workflow has no
Apple signing credentials. Organization-specific signing/distribution policy is
a separate release requirement. Checksums provide integrity, not code signing.

## Release operation

Publish a GitHub release whose tag includes this workflow and its helper files.
The build job explicitly checks out that tag, builds the two generators and their
resource headers, then packages and smoke-tests the extracted archives. The four
matrix builds run independently. Only after **all four pass** does a separate job
with `contents: write` upload the six assets and a combined `SHA256SUMS` file.
The build jobs themselves have only `contents: read`.

To test without publishing, manually run **Hush Release Binaries** from the
Actions UI on a ref containing this workflow. `workflow_dispatch` produces Actions
artifacts with 14-day retention and never uploads to a GitHub release. The workflow
must exist on the default branch for GitHub to expose the manual dispatch UI.
No push/PR events or upstream LLVM test workflows are enabled by this change.

Release uploads deliberately omit `--clobber`: reruns cannot silently replace
hash-pinned release assets. Prefer a new version for changed binaries. If the
upload itself fails halfway through, inspect the release and remove only the
incomplete attempt's assets before retrying. GitHub uploads are not transactional.
A failure before the publish job leaves the release without these new assets.

Releases published using another workflow's ordinary `GITHUB_TOKEN` do not trigger
this release workflow. Publish via the UI, an authenticated user, or an appropriately
scoped GitHub App/PAT if release creation is automated.

## Validation

The packaging helper:

- Checks the runner OS/CPU matches the asset label.
- Audits native dependencies with `ldd`, `otool -L`, or `dumpbin /dependents`.
- Installs matching Clang builtin headers with CMake's
  `clang-resource-headers` component.
- Extracts the finished archive into a different directory containing spaces.
- Executes both tools, parses builtin and C++ standard headers, verifies Hush
  attributes are recognized, and checks representative generated output.
- Checks that malformed input produces a failure status and diagnostic.

This is not the full LLVM test suite and does not establish engine compatibility
or oldest-OS support. The existing Hush engine test suite remains necessary.

Run the lightweight helper tests locally (Python 3.12+):

```sh
python -m unittest discover -s .github/hush-release -p 'test_*.py' -v
```

To package an existing native build configured like the workflow, for example
on Apple Silicon:

```sh
python .github/hush-release/package.py \
  --build-dir build --output-dir dist --platform macos-arm64 --ref local-test
```

For the static Linux build, also pass `--runtime-license PATH` for the GNU runtime
copyright/exception notice and referenced license texts; the workflow shows the
Ubuntu paths. `dist` must be empty. Full builds require substantial disk, RAM, and time; the
workflow caps compilation at two concurrent jobs and linking at one job.
