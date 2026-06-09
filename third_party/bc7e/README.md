# BC7E external encoder

This directory is reserved for the optional external BC7 encoder used by custom
album jackets.

Expected runtime artifact:

```text
third_party/bc7e/bin/win64/ds2_jacket_bc7e.dll
```

The DLL must export:

```cpp
extern "C" __declspec(dllexport)
int __cdecl DS2_EncodeRgbaToBc7(
    const unsigned char* rgba,
    unsigned int width,
    unsigned int height,
    unsigned char* bc7,
    unsigned int bc7Bytes);
```

Return non-zero on success. The output is packed BC7 block data with
`((width + 3) / 4) * ((height + 3) / 4) * 16` bytes.

Recommended upstream: `richgel999/bc7enc_rdo`, built from a fixed commit with
BC7E support. Record the upstream commit, ISPC version, license, and DLL SHA256
when the binary is added.

Current verified build path:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-bc7e.ps1 -SkipFetch
```

Verified artifact:

```text
third_party/bc7e/bin/win64/ds2_jacket_bc7e.dll
```

The build script:

- checks out `richgel999/bc7enc_rdo` at
  `dbe416d28a5530b4e8cc45b14bf034dc6b96bbde`;
- runs the upstream `bc7e.ispc` through `ispc.exe` from that checkout when
  available;
- builds the DS2 wrapper DLL with MSBuild;
- calls `tools/verify-bc7e.ps1` to load the DLL and encode an 8x8 RGBA test
  image through `DS2_EncodeRgbaToBc7`;
- writes `ds2_jacket_bc7e.txt` with upstream commit, ISPC path, and SHA256.

To install directly into a game `scripts` directory, pass `-GameScriptsDir`.
The script then copies `ds2_jacket_bc7e.dll` there and verifies that copy.

`package-release.ps1` requires this DLL by default and verifies it before
placing it in the release package. Passing `-AllowMissingBc7e` is the explicit
fallback path for packages that intentionally omit the external encoder.
