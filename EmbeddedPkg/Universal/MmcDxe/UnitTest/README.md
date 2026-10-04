# MMC host regression tests

Run from the edk2 root on Linux x64 with Python 3 and GCC or Clang:

```sh
python3 EmbeddedPkg/Universal/MmcDxe/UnitTest/RunHostTests.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  CC='cc -fsanitize=address,undefined -fno-omit-frame-pointer --param=asan-globals=0' \
  python3 EmbeddedPkg/Universal/MmcDxe/UnitTest/RunHostTests.py
```

The tests include the production identification and block I/O implementations
and substitute an MMC host. Block I/O coverage includes multi-request single-block
fallback, STOP ordering, transfer and response error propagation, readiness
timeouts, and overflow-safe LBA bounds. Identification coverage includes tagged
SD CMD6 versus legacy and eMMC commands,
wire-order switch status, unsupported/rejected functions, one-bit cards,
transport errors, and bounded eMMC programming/data-state waits. It needs no
third-party test framework or firmware build. Leak detection is disabled in the
sanitizer example because some sandbox runtimes use ptrace; allocation cleanup
on the tested eMMC failure path is checked explicitly.

The SD command discriminator and endian correction originate in
[StarFive commit 49b24a5](https://github.com/starfive-tech/edk2/commit/49b24a5f284a5a3df51a85aa935145b8ae4cd8be)
by minda.chen <minda.chen@starfivetech.com>. This port preserves command values
for revision 1.2 hosts and replaces the vendor byte-swap macro with byte-based
wire-format parsing. A host implementing the discriminator must explicitly set
its revision to `MMC_HOST_PROTOCOL_REVISION_SD_CMD`.

The vendor NOR addition is already represented by upstream `gd25lq128` for
JEDEC ID `c8:60:18`; a duplicate table entry is intentionally unnecessary.

These tests validate software behavior, not physical SD/eMMC signal timing or
on-board boot. They are standalone regression checks, not integrated into the
EmbeddedPkg host-unit-test DSC (the package currently has none).
