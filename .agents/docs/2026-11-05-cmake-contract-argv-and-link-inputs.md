---
subject: cmake-contract
status: active
---

# CMake contract arguments and consumer link inputs

Audience: maintainers implementing and reviewing the cross-build adapter.

This record explains the failure boundaries, the selected representation and
the evidence needed before releasing the changes in PR #52. It does not claim
qualification of every CMake generator or every foreign executable platform.

## Confirmed failures and ownership

| Failure | Evidence | Owner |
|---|---|---|
| Joining JSON arguments loses literal quotes, backslashes and tabs | The previous reader changes values and argument boundaries under the native parser | Plugin adapter |
| LLVM's GNU response parser drops a quoted empty argument | A native `-D` followed by an empty word is invalid; the corresponding response-file invocation consumes the next valid definition and succeeds | Compiler-parser limitation; adapter rejects the lossy representation |
| A single raw flags string needs different dollar escaping in Make compilation and its direct link script | Actual compiler controls fail despite successful configuration | Plugin adapter |
| An independent subproject executable requires the library that the same subproject has yet to install | Fresh actual musl cross-build fails at that future archive | Core target-contract export |
| Reconfiguration ignores a newly introduced toolchain while retaining old flags | Actual previously configured CMake directory warns that the toolchain is unused | Plugin build-directory identity |
| Switching an existing cross-build from Ninja to Unix Makefiles reuses its incompatible cache | Actual mcpp build fails with the generator mismatch diagnostic | Plugin build-directory identity |
| Changing `options::system_name` retains the previous target system | Actual mcpp build requests Generic but the configured project still reports Linux | Plugin build-directory identity |
| A retargeted Clang driver is identified as a Linux compiler although it emits Windows COFF | A no-SDK static-library control reports `Clang\|\|GNU\|Linux` and names the archive `libprobe.a` | Plugin compiler-ID configuration |
| A source path containing an apostrophe is split in the dependency graph | Pure CMake 4.4.2/Ninja control reproduces without mcpp, a toolchain or response files | External CMake/Ninja path handling |

The consuming package's own libraries are not target configuration. Core PR
#889 excludes package link flags and libraries from the foreign-build contract
while retaining them on the consumer's actual link. No CMake primitive belongs
in that correction.

## Selected representation

The action supplies a generated CMake toolchain. It reads the engine contract
when configuration runs, after the build program has planned the action.
The generated toolchain is an explicit action input. Its contract path is
propagated through `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`, so ordinary compiler
ABI checks and source-file compile probes load the same settings.

JSON arrays are read by index and written to compiler response files. GNU
driver quoting and MSVC command-line quoting are separate encodings selected
by the contract's dialect. MSVC files include a UTF-8 BOM. CMake receives each
response-file reference as one native option and performs generator escaping;
literal argument contents do not pass through Ninja, Make or a shell.
One grouped reference also preserves repeated options and their order.
Literal empty GNU argument words are rejected with a configure diagnostic:
Clang's GNU response parser discards them even when quoted. Empty string
values within nonempty options remain supported. No process wrapper or
second argument grammar is introduced for this invalid-invocation boundary.

The response-file name hashes the dialect and encoded contents. Changed
arguments therefore change the generated compile command. Identical contents
are retained without rewriting the file. A new representation uses its own
build-directory key, since an old cache does not begin loading a toolchain
merely because a later configure command names one.

Immutable generator selections (`-G`, `-A`, `-T`, and
`CMAKE_GENERATOR*` cache definitions) and the effective cross system name
participate in the build-directory identity. Joined and separated short
options, and typed and untyped cache definitions, use the same identity.
Ordinary `-D` definitions reconfigure the existing directory. Returning to
a previous generator and system reuses that selection's directory.

Cross-build contract options follow CMake's language and configuration flags.
`options::config` continues to select the CMake configuration; a conflicting
contract profile option takes precedence in the compiler argument order.
The subproject still owns its language standard and target-specific options.
Compiler-ID probing does not consume directory compile options. The adapter
therefore also supplies `CMAKE_<LANG>_COMPILER_TARGET` from the last actual
`--target` or `-target` compiler option, retaining the original argument words.
It does not substitute `contract.target`, which may be a logical platform
alias. This follows CMake's documented [Clang cross-toolchain configuration](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-using-clang).
CMake 3.19 is the minimum for reading the JSON contract.

## Actual controls

The adapter passes all 52 current plugin-logic cases and the existing actual
musl cross-build. Compiler controls use its generated toolchain, rather than
a second implementation of the serializer. GCC and Clang execute C and C++
programs under Ninja and Unix Makefiles with literal quotes, backslashes,
dollar signs, semicolons, tabs, empty strings and Unicode. They also check
language isolation, repeated-option order, actual compile probes, unchanged
objects after reconfiguration and invalidation when one contract word changes.
Linux controls inspect the actual ELF runpath for literal `$ORIGIN` content.
A separate empty-word control places an empty argument after `-D`. GCC and
Clang reject that native invocation; removing the word consumes a valid
fallback definition and succeeds. The generated helper rejects that empty
word during configuration, preventing LLVM's response parser from silently
changing the invalid invocation into a valid one.

An actual clang-cl invocation consumes the MSVC response file and produces
COFF; its initialized data retains the expected literal values. This is a
compiler-parser control, not native MSVC or Visual Studio generator coverage.

An actual GNU-style Clang 22 driver is retargeted to Windows without an SDK.
Ninja, Unix Makefiles and Ninja Multi-Config build static libraries whose
members are AMD64 COFF. The resulting identity is
`Clang|MSVC|GNU|Windows`, pointer width is eight, and the archive is named
`probe.lib`. A deliberately different logical contract target remains unused
for compiler identity. The same control fails with the old reader at its
identity assertion, although that reader can emit COFF.

The real adapter also passes ten successive selections: Ninja/Linux,
Make/Linux, Make no-op, Ninja return, Ninja no-op, Ninja/Generic, Generic
no-op, Linux return, Ninja Multi-Config/Linux and Multi-Config no-op.
Each build executes the actual musl program. Incompatible selections have
distinct CMake directories; return and no-op cases retain object timestamps.
This matrix passes with the diagnostic corrected core and independently with
the published mcpp 2026.11.5.1 binary. It does not depend on the consumer-link
correction. Before the system-name key correction, the first five selections
pass and the Generic selection still reports Linux.

The actual sidecar failure remains with the response-file adapter and the old
core. Replacing only two core modules fixes it: the library installs, both the
independent sidecar and the consuming musl program execute, and the consumer's
Ninja graph retains its archive. Extended core E2E 982 fails with the old module
and passes with the corrected contract while retaining the consumer link flag.

The apostrophe negative remains separate. The positive argument test keeps an
apostrophe in the response-file directory; source paths contain spaces,
Unicode and dollar signs. It does not claim to fix native dependency parsing
of source paths containing apostrophes.

## Remaining qualification

The response-file and cache-selection regressions can run against the
currently published mcpp 2026.11.5.1. They do not require waiting for the core
release. Native MSVC, Visual Studio and Xcode generators, custom link drivers
and standalone executables over graph-supplied runtimes are not established
by the Linux compiler controls. The original platform CI remains required.

Core 2026.11.5.2 must complete its final CI, independent ecosystem review,
publication and ordinary installation. Then the plugin CI pin and real
sidecar fixture can follow the public version. The package's protocol floor
remains distinct from a feature's required bug-fix version. All final plugin
CI and review gates remain required before merge and release.

The final sequence is local adapter verification and documentation, a
reviewable PR #52 update, public core installation and sidecar integration,
then the full plugin CI and ecosystem review. The sidecar uses ordinary
prebuilt musl; it does not establish graph-runtime executable support.

Public reports contain fixed assertions and synthetic fixtures. Raw user logs,
usernames, private paths and private project names are not published.
