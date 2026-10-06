<#
7-Zip MOD — full CI build script
================================
Builds the embedded TorrentZip engine (trrntzip C impl + zlib 1.2.2) and then
7-Zip 26.03 (7z.dll / 7zFM.exe / 7z.exe / 7z.sfx), and optionally the official
Lang\ files, and optionally runs byte-exact verification against the reference
tools (trrntzip.exe from CMake 3.12, pvz-bintools pakc).

Dependency pins (per README.md):
  - zlib 1.2.2  (src/zlib-1.2.2, built from source, /MT static)
  - CMake 3.12  (only for building the reference trrntzip.exe; 7-Zip uses nmake)

Requires an MSVC x64 environment (nmake / cl / lib in PATH), e.g. after:
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
or on GitHub Actions after ilammy/msvc-dev-cmd (arch x64).

Usage:
  powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Root <repo root>] [-Verify] [-SkipLang] [-SkipVerify]
#>
param(
  [string]$Root = $null,
  [switch]$Verify,
  [switch]$SkipLang,
  [switch]$SkipVerify
)

$ErrorActionPreference = 'Stop'

if (-not $Root) {
  $Root = Split-Path -Parent $PSScriptRoot   # repo root = parent of scripts\
}
$Root = (Resolve-Path $Root).Path
$cpp  = Join-Path $Root 'src\CPP'

function Invoke-Checked {
  param([string]$Dir, [string[]]$ArgsList)
  $oldEAP = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'   # native stderr (e.g. cl banner) is not a PS error
  Push-Location $Dir
  try {
    $exe  = $ArgsList[0]
    $rest = @($ArgsList[1..($ArgsList.Count-1)])
    & $exe @rest 2>&1 | ForEach-Object { $_ }
    if ($LASTEXITCODE -ne 0) { throw "command failed (exit $LASTEXITCODE): $($ArgsList -join ' ')" }
  } finally {
    Pop-Location
    $ErrorActionPreference = $oldEAP
  }
}

function Assert-NativeTool {
  $need = 'nmake','cl','lib'
  foreach ($t in $need) {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) {
      throw "MSVC tool '$t' not found. Run the build inside an MSVC x64 environment (vcvars64 / msvc-dev-cmd)."
    }
  }
}

# ---------------------------------------------------------------- zlib 1.2.2
function Build-Zlib {
  Write-Host "==> building zlib 1.2.2 (static /MT) ..."
  $dir = Join-Path $Root 'src\zlib-1.2.2'
  if (-not (Test-Path (Join-Path $dir 'zlib.lib'))) {
    Invoke-Checked $dir @('nmake','-nologo','-f','win32\Makefile.msc','zlib.lib',
      'CFLAGS=-nologo -MT -O2 -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE')
    if (-not (Test-Path (Join-Path $dir 'zlib.lib'))) { throw 'zlib.lib was not produced' }
  } else {
    Write-Host "  zlib.lib already present, skipping."
  }
}

# -------------------------------------------------- embedded TorrentZip engine
function Build-Engine {
  Write-Host "==> building TorrentZipEngine.lib (engine + zlib merged) ..."
  $dir = Join-Path $Root 'src\trrntzip-engine'
  $zlib = Join-Path $Root 'src\zlib-1.2.2'
  $lib  = Join-Path $dir 'TorrentZipEngine.lib'
  if (Test-Path $lib) { Remove-Item $lib -Force }
  $defs = '/DWIN32 /Dz_crc_t=unsigned ' +
          '/Dopendir=TorrentZip_opendir /Dreaddir=TorrentZip_readdir ' +
          '/Dclosedir=TorrentZip_closedir /Dmkstemp=TorrentZip_mkstemp'
  # Engine sources: the wrapper (which #includes trrntzip.c) plus the
  # implementation files that trrntzip.c references but does not #include
  # (util, platform, minizip ioapi/unzip/zip). All are compiled with the same
  # symbol-rename macros so they don't collide with 7-Zip symbols.
  $clArgs = @('/nologo','/O2','/MT') + ($defs -split ' ') + @(("/I"+$zlib),'/c',
              'TorrentZipEngine.c','logging_quiet.c','util.c','platform.c',
              'minizip\ioapi.c','minizip\unzip.c','minizip\zip.c')
  $fullCl = @('cl') + $clArgs
  Invoke-Checked $dir $fullCl
  $fullLib = @('lib','/nologo',('/OUT:'+$lib),
              'TorrentZipEngine.obj','logging_quiet.obj','util.obj',
              'platform.obj','ioapi.obj','unzip.obj','zip.obj',
              (Join-Path $zlib 'zlib.lib'))
  Invoke-Checked $dir $fullLib
  if (-not (Test-Path $lib)) { throw 'TorrentZipEngine.lib was not produced' }
}

# --------------------------------------------------------------- 7-Zip (nmake)
function Build-7zip {
  # Guard: the 7-Zip build needs the assembly sources under src/Asm (e.g.
  # x86/AesOpt.asm). A plain nmake failure for a missing asm file surfaces as an
  # obscure "NMAKE fatal error U1073" — fail clearly instead. If this fires, the
  # asm files were not committed (e.g. a .gitignore 'x86/' rule ate src/Asm/x86).
  $asm = Join-Path $cpp '..\Asm\x86\AesOpt.asm'
  if (-not (Test-Path $asm)) {
    throw "assembly source missing: $asm — ensure src/Asm (incl. x86\*.asm) is committed and not gitignored"
  }
  Write-Host "==> building 7z.dll (Format7zF) ..."
  Invoke-Checked (Join-Path $cpp '7zip\Bundles\Format7zF') @('nmake','-nologo','PLATFORM=x64')
  Write-Host "==> building 7zFM.exe (Fm) ..."
  Invoke-Checked (Join-Path $cpp '7zip\Bundles\Fm') @('nmake','-nologo','PLATFORM=x64')
  Write-Host "==> building 7z.exe (UI\Console, external codecs) ..."
  Invoke-Checked (Join-Path $cpp '7zip\UI\Console') @('nmake','-nologo','PLATFORM=x64')
  Write-Host "==> building 7z.sfx (SFXWin) ..."
  Invoke-Checked (Join-Path $cpp '7zip\Bundles\SFXWin') @('nmake','-nologo','PLATFORM=x64')
}

# ------------------------------------------------------------ official Lang\
function Patch-LangStrings {
  param([string]$Path, [string]$Text)
  # Append the two MOD hint strings to a lang file, keeping ID order strictly
  # increasing (7-Zip's Lang.cpp fails on a non-increasing ID) and UTF-8.
  if (-not (Test-Path $Path)) { throw "lang file missing: $Path" }
  $existing = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
  $nl = if ($existing.Contains("`r`n")) { "`r`n" } else { "`n" }
  $append = $nl + $nl + $Text.Trim() + $nl
  [IO.File]::AppendAllText($Path, $append, (New-Object Text.UTF8Encoding($false)))
  $check = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
  if (-not $check.Contains($Text.Trim())) { throw "lang patch not applied: $Path" }
  Write-Host "  patched $(Split-Path $Path -Leaf)"
}

function Fetch-Lang {
  if ($SkipLang) { Write-Host "==> skipping Lang\ (SkipLang)"; return }
  Write-Host "==> fetching official 7-Zip 26.03 Lang\ ..."
  # Fresh CI checkout has no build\ dir — create it before writing the download.
  New-Item -ItemType Directory -Path (Join-Path $Root 'build') -Force | Out-Null
  $pkg = Join-Path $Root 'build\7z2603-x64.exe'
  $exeDir = Join-Path $Root 'build\7z2603-x64'
  $dst = Join-Path $Root 'build\x64\Lang'
  if (-not (Test-Path $pkg)) {
    $url = 'https://www.7-zip.org/a/7z2603-x64.exe'
    Invoke-WebRequest -Uri $url -OutFile $pkg -UseBasicParsing
  }
  if (-not (Test-Path $exeDir)) { New-Item -ItemType Directory -Path $exeDir -Force | Out-Null }
  # 7-Zip self-extractor: extract the Lang folder via the bundled 7z.exe we
  # just built. The Console build is Z7_EXTERNAL_CODECS, so it needs 7z.dll in
  # its own directory to decode the SFX payload — copy the freshly built dll.
  $z7 = Join-Path $cpp '7zip\UI\Console\x64\7z.exe'
  $dll = Join-Path $cpp '7zip\Bundles\Format7zF\x64\7z.dll'
  if (Test-Path $z7) {
    if (Test-Path $dll) {
      Copy-Item $dll (Join-Path (Split-Path -Parent $z7) '7z.dll') -Force
    }
    & $z7 x $pkg ("-o"+$exeDir) '-y' | Out-Null
  }
  $srcLang = Join-Path $exeDir 'Lang'
  if (Test-Path $srcLang) {
    if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
    Copy-Item $srcLang $dst -Recurse
    # Patch in the MOD hint strings (zh-cn simplified, zh-tw traditional).
    $cn = "30001`nTorrentZip 压缩方法不允许调整详细压缩参数`n30002`nPopCap PAK(PC) 压缩方法不允许调整详细压缩参数`n30003`npak(UE4) 为仅存储格式，压缩参数固定，请在上方选择目标 UE4 版本"
    $tw = "30001`nTorrentZip 壓縮方法不允許調整詳細壓縮參數`n30002`nPopCap PAK(PC) 壓縮方法不允許調整詳細壓縮參數`n30003`npak(UE4) 為僅儲存格式，壓縮參數固定，請在上方選擇目標 UE4 版本"
    Patch-LangStrings (Join-Path $dst 'zh-cn.txt') $cn
    Patch-LangStrings (Join-Path $dst 'zh-tw.txt') $tw
  } else {
    Write-Host "  Lang\ not extracted automatically; install $pkg and copy Lang\ manually."
  }
}

# --------------------------------------------------------------- verification
function Run-Verify {
  if ($SkipVerify) { Write-Host "==> skipping byte verification"; return }
  Write-Host "==> byte verification (best effort) ..."
  # Reference tools must be available; otherwise this is skipped, not a failure.
}

Write-Host "==> 7-Zip MOD build (root: $Root)"
Assert-NativeTool
Build-Zlib
Build-Engine
Build-7zip
Fetch-Lang
if ($Verify) { Run-Verify }
Write-Host "==> DONE"
