param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($DllPath)) {
    $DllPath = Join-Path $repoRoot "third_party\bc7e\bin\win64\ds2_jacket_bc7e.dll"
}

if (-not (Test-Path $DllPath)) {
    Write-Host "BC7E_DLL_MISSING $DllPath"
    exit 1
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Ds2Bc7eNative {
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryW(string fileName);

    [DllImport("kernel32", SetLastError = true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string procName);

    [DllImport("kernel32")]
    public static extern bool FreeLibrary(IntPtr module);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int Encode(IntPtr rgba, UInt32 width, UInt32 height,
        IntPtr bc7, UInt32 bc7Bytes);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate UInt32 Version();
}
"@

$module = [Ds2Bc7eNative]::LoadLibraryW((Resolve-Path $DllPath).Path)
if ($module -eq [IntPtr]::Zero) {
    Write-Host "BC7E_LOAD_FAILED $DllPath"
    exit 2
}

try {
    $encodePtr = [Ds2Bc7eNative]::GetProcAddress($module, "DS2_EncodeRgbaToBc7")
    $versionPtr = [Ds2Bc7eNative]::GetProcAddress($module, "DS2_Bc7eWrapperVersion")
    if ($encodePtr -eq [IntPtr]::Zero -or $versionPtr -eq [IntPtr]::Zero) {
        Write-Host "BC7E_EXPORT_MISSING $DllPath"
        exit 3
    }

    $encode = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($encodePtr, [Ds2Bc7eNative+Encode])
    $version = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($versionPtr, [Ds2Bc7eNative+Version])
    if ($version.Invoke() -eq 0) {
        Write-Host "BC7E_VERSION_INVALID $DllPath"
        exit 4
    }

    [byte[]]$rgba = New-Object byte[] (8 * 8 * 4)
    for ($y = 0; $y -lt 8; $y++) {
        for ($x = 0; $x -lt 8; $x++) {
            $i = ($y * 8 + $x) * 4
            $rgba[$i + 0] = [byte]($x * 31)
            $rgba[$i + 1] = [byte]($y * 29)
            $rgba[$i + 2] = [byte](($x + $y) * 13)
            $rgba[$i + 3] = 255
        }
    }

    [byte[]]$bc7 = New-Object byte[] 64
    $rgbaHandle = [Runtime.InteropServices.GCHandle]::Alloc($rgba, "Pinned")
    $bc7Handle = [Runtime.InteropServices.GCHandle]::Alloc($bc7, "Pinned")
    try {
        $ok = $encode.Invoke($rgbaHandle.AddrOfPinnedObject(), 8, 8,
            $bc7Handle.AddrOfPinnedObject(), $bc7.Length)
    } finally {
        $rgbaHandle.Free()
        $bc7Handle.Free()
    }
    if ($ok -eq 0 -or -not ($bc7 | Where-Object { $_ -ne 0 } | Select-Object -First 1)) {
        Write-Host "BC7E_ENCODE_FAILED $DllPath"
        exit 5
    }
} finally {
    [void][Ds2Bc7eNative]::FreeLibrary($module)
}

$hash = Get-FileHash -LiteralPath $DllPath -Algorithm SHA256
Write-Host "BC7E_DLL_OK $($hash.Hash) $DllPath"
