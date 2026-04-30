param(
    [switch]$FullGame
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $env:PSL1GHT) {
    throw "PSL1GHT is not set. Set it to your PSL1GHT SDK path before building."
}

$make = Get-Command make -ErrorAction SilentlyContinue
if (-not $make) {
    throw "make was not found in PATH."
}

$ppuGcc = Get-Command ppu-gcc -ErrorAction SilentlyContinue
$ps3Gcc = Get-Command powerpc64-ps3-elf-gcc -ErrorAction SilentlyContinue
if (-not $ppuGcc -and -not $ps3Gcc) {
    throw "PS3 PPU compiler was not found in PATH. Expected ppu-gcc or powerpc64-ps3-elf-gcc."
}

if ($FullGame) {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if (-not $cmake) {
        throw "cmake was not found in PATH."
    }
    if (-not $ninja) {
        throw "ninja was not found in PATH."
    }

    $useMsysPaths = ($cmake.Source -match "\\msys64\\usr\\bin\\") -or
        ($cmake.Source -match "\\cygwin")
    $cygpath = $null
    if ($useMsysPaths) {
        $cygpath = Get-Command cygpath -ErrorAction SilentlyContinue
        if (-not $cygpath) {
            throw "MSYS/Cygwin cmake was found, but cygpath was not found in PATH."
        }
    }

    function Convert-BuildPath {
        param([string]$Path)
        if (-not $useMsysPaths) {
            return $Path
        }
        $converted = & $cygpath.Source -u $Path
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to convert path for MSYS/Cygwin tools: $Path"
        }
        return $converted.Trim()
    }

    $buildDir = Join-Path $root "ps3\build-full"
    $toolchain = Join-Path $root "ps3\cmake\Toolchain-psl1ght.cmake"
    $sourceDirForTool = Convert-BuildPath (Join-Path $root "ps3")
    $buildDirForTool = Convert-BuildPath $buildDir
    $toolchainForTool = Convert-BuildPath $toolchain

    & $cmake.Source -S $sourceDirForTool -B $buildDirForTool -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainForTool" `
        -DACGC_PS3_BOOTSTRAP_ONLY=OFF
    if ($LASTEXITCODE -ne 0) {
        throw "Full PS3 CMake configure failed."
    }
    & $ninja.Source -C $buildDirForTool
    if ($LASTEXITCODE -ne 0) {
        throw "Full PS3 build failed."
    }
    return
}

Push-Location (Join-Path $root "ps3")
try {
    & make pkg
    if ($LASTEXITCODE -ne 0) {
        Write-Host "pkg target failed; trying a plain ELF/SELF build."
        & make
        if ($LASTEXITCODE -ne 0) {
            throw "PS3 build failed."
        }
    }
}
finally {
    Pop-Location
}
