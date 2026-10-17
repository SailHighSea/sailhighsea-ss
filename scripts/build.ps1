<#
  Local Windows dev build. Every build is numbered and copied to the test folder:
      test\SailHighSea-SimpleSync-v0.0.1-dev-1.exe, ...-dev-2.exe, ...

  .\scripts\build.ps1            build (Release, no console window)
  .\scripts\build.ps1 -Console   Debug build that opens a console with raylib's log
  .\scripts\build.ps1 -Test      build, then run the engine tests
  .\scripts\build.ps1 -Run       build, then start the app with the test sandbox (--dev)
  .\scripts\build.ps1 -Clean     wipe the build folder first (the dev counter is kept)
  .\scripts\build.ps1 -Keep 5    keep the newest 5 test exes (default 3)
  .\scripts\build.ps1 -Version 0.0.2   start a new numbering line (counter restarts at 1)

  Because each build has its own file name, a copy that is still running never blocks the next build.
  Needs CMake, Ninja and a C compiler. If they are not on PATH it falls back to
  MSYS2 UCRT64 (C:\msys64\ucrt64\bin, pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja}).
#>
param([switch]$Test, [switch]$Run, [switch]$Release, [switch]$Console, [switch]$Clean, [string]$Version = '0.0.1', [int]$Keep = 3)
$ErrorActionPreference = 'Continue'   # native tools print warnings to stderr; failures are caught via $LASTEXITCODE
$root = Split-Path -Parent $PSScriptRoot
$testDir = Join-Path $root 'test'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $msys = 'C:\msys64\ucrt64\bin'
    if (-not (Test-Path "$msys\cmake.exe")) { throw "cmake not found. Install MSYS2 UCRT64 gcc/cmake/ninja or add cmake to PATH." }
    $env:PATH = "$msys;C:\msys64\usr\bin;$env:PATH"
}

$build = Join-Path $testDir '_build'          # keeps the project root tidy; ignored by git
$counterFile = Join-Path $testDir '_dev-build.txt'

# dev counter: "<version>:<n>"; restarts at 1 when -Version changes
$n = 1
if (Test-Path $counterFile) {
    $prev = (Get-Content $counterFile -TotalCount 1) -split ':'
    if ($prev.Count -eq 2 -and $prev[0] -eq $Version) { $n = [int]$prev[1] + 1 }
}
$tag = "v$Version-dev-$n"

if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }
$type = if ($Console) { 'Debug' } else { 'Release' }   # Debug keeps a console window with raylib's log
$stage = Join-Path $build 'out'               # CMake writes the plain exe here, then we copy it with a version name

cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$type" "-DSHS_VERSION_STRING=$Version-dev-$n" "-DSHS_TEST_DIR=$($stage -replace '\\','/')"
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $build --parallel
if ($LASTEXITCODE) { throw "build failed" }

if ($Test) {
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw "tests failed" }
}

$exe = Join-Path $testDir "SailHighSea-SimpleSync-$tag.exe"
Copy-Item (Join-Path $stage 'SailHighSea-SimpleSync.exe') $exe -Force
Set-Content $counterFile "${Version}:$n"

# keep only the newest $Keep versioned exes (a copy that is still running is skipped)
Get-ChildItem $testDir -Filter 'SailHighSea-SimpleSync-v*.exe' | Sort-Object LastWriteTime -Descending |
    Select-Object -Skip $Keep | ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }

Write-Host "`nBuilt $tag : $exe" -ForegroundColor Green
if ($Run) { Start-Process $exe -ArgumentList '--dev' -WorkingDirectory $testDir }
