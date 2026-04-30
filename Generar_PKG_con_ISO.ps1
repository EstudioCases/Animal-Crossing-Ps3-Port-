param()

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "ps3\build-full"
$LogPath = Join-Path $BuildDir "generate-pkg-with-iso.log"
$PkgPath = Join-Path $BuildDir "ACGCPS3-full.gnpdrm.pkg"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$transcriptStarted = $false

function Find-Bash {
    $defaultBash = "C:\msys64\usr\bin\bash.exe"
    if (Test-Path -LiteralPath $defaultBash) {
        return $defaultBash
    }

    $bashCommand = Get-Command bash -ErrorAction SilentlyContinue
    if ($bashCommand) {
        return $bashCommand.Source
    }

    throw "bash.exe was not found. Install MSYS2/PSL1GHT or update this script with the correct bash path."
}

function Require-Path {
    param(
        [string]$Path,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Name was not found: $Path"
    }
}

function Quote-BashLiteral {
    param([string]$Value)
    return "'" + $Value.Replace("'", "'\''") + "'"
}

try {
    Start-Transcript -Path $LogPath -Force | Out-Null
    $transcriptStarted = $true

    Write-Host "ACGC PS3 package generator"
    Write-Host "Root: $Root"

    $TestDir = Join-Path $Root "test"
    Require-Path $TestDir "ISO test folder"

    $DiscImage = Get-ChildItem -LiteralPath $TestDir -File |
        Where-Object { $_.Extension -match '^\.(iso|gcm|ciso)$' } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if (-not $DiscImage) {
        throw "No .iso, .gcm or .ciso file was found in: $TestDir"
    }

    Write-Host "Disc image: $($DiscImage.FullName)"
    Write-Host ("Disc size: {0:N2} GB" -f ($DiscImage.Length / 1GB))

    $bash = Find-Bash
    $ps3devWin = "C:\msys64\usr\local\ps3dev"
    $ps3devBin = Join-Path $ps3devWin "bin"
    $ppuBin = Join-Path $ps3devWin "ppu\bin"
    $msysBin = "C:\msys64\usr\bin"

    Require-Path $ps3devBin "PS3DEV bin folder"
    Require-Path $ppuBin "PS3DEV PPU bin folder"
    Require-Path $msysBin "MSYS2 usr bin folder"

    $env:PS3DEV = "/usr/local/ps3dev"
    $env:PSL1GHT = "/usr/local/ps3dev/psl1ght"
    $env:PATH = "$ps3devBin;$ppuBin;$msysBin;$env:PATH"

    Write-Host "[1/4] Building full PS3 executable..."
    Push-Location $Root
    try {
        & (Join-Path $Root "build_ps3.ps1") -FullGame
        if ($LASTEXITCODE -ne 0) {
            throw "FullGame build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }

    $PkgStaging = Join-Path $BuildDir "pkg"
    $UsrDir = Join-Path $PkgStaging "USRDIR"
    $RomDir = Join-Path $UsrDir "rom"
    New-Item -ItemType Directory -Force -Path $RomDir | Out-Null

    $DestDisc = Join-Path $RomDir ("game" + $DiscImage.Extension.ToLowerInvariant())
    Write-Host "[2/4] Copying disc image into package staging..."
    Copy-Item -LiteralPath $DiscImage.FullName -Destination $DestDisc -Force

    $rootU = (& $bash -lc ("cygpath -u " + (Quote-BashLiteral $Root))).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($rootU)) {
        throw "Failed to convert root path for MSYS2."
    }

    $env:ACGC_ROOT_U = $rootU

    $packageScript = @'
set -euo pipefail
export PATH=/usr/local/ps3dev/bin:/usr/local/ps3dev/ppu/bin:/usr/bin:$PATH
cd "$ACGC_ROOT_U"

CONTENTID=UP0001-ACGC00001_00-ACGCPS3FULLGAME1

mkdir -p ps3/build-full/pkg/USRDIR/rom
cp /usr/local/ps3dev/bin/ICON0.PNG ps3/build-full/pkg/ICON0.PNG
ppu-strip ps3/build-full/ACGCPS3 -o ps3/build-full/ACGCPS3.pkg.elf
sprxlinker ps3/build-full/ACGCPS3.pkg.elf
make_self_npdrm ps3/build-full/ACGCPS3.pkg.elf ps3/build-full/pkg/USRDIR/EBOOT.BIN "$CONTENTID"
python /usr/local/ps3dev/bin/sfo.py -f --title ACGCPS3FullGame --appid ACGC00001 /usr/local/ps3dev/bin/sfo.xml ps3/build-full/pkg/PARAM.SFO
rm -f ps3/build-full/ACGCPS3-full.pkg ps3/build-full/ACGCPS3-full.gnpdrm.pkg
python /usr/local/ps3dev/bin/pkg.py --contentid "$CONTENTID" ps3/build-full/pkg/ ps3/build-full/ACGCPS3-full.pkg
cp ps3/build-full/ACGCPS3-full.pkg ps3/build-full/ACGCPS3-full.gnpdrm.pkg
package_finalize ps3/build-full/ACGCPS3-full.gnpdrm.pkg
'@

    Write-Host "[3/4] Creating and finalizing PKG..."
    & $bash -lc $packageScript
    if ($LASTEXITCODE -ne 0) {
        throw "Package generation failed with exit code $LASTEXITCODE."
    }

    Require-Path $PkgPath "Final PKG"
    $pkgInfo = Get-Item -LiteralPath $PkgPath

    Write-Host "[4/4] Done."
    Write-Host "PKG: $($pkgInfo.FullName)"
    Write-Host ("PKG size: {0:N2} GB" -f ($pkgInfo.Length / 1GB))
    Write-Host "Build log: $LogPath"
}
finally {
    if ($transcriptStarted) {
        Stop-Transcript | Out-Null
    }
}
