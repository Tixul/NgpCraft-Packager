param([string]$CompilerBin = 'C:\Qt\Tools\mingw1310_64\bin')
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath (Join-Path $CompilerBin 'g++.exe'))) {
    throw 'Indiquez le dossier bin de MinGW-w64 avec -CompilerBin.'
}
$env:PATH = $CompilerBin + ';' + $env:PATH
$cmakeCompilerBin = $CompilerBin.Replace('\', '/')
cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'MinGW Makefiles' "-DCMAKE_CXX_COMPILER=$cmakeCompilerBin/g++.exe" "-DCMAKE_RC_COMPILER=$cmakeCompilerBin/windres.exe" "-DCMAKE_MAKE_PROGRAM=$cmakeCompilerBin/mingw32-make.exe" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Configuration CMake echouee.' }
cmake --build (Join-Path $PSScriptRoot 'build') -j 4
if ($LASTEXITCODE -ne 0) { throw 'Compilation echouee.' }
$releaseDir = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build/NgpCraftPackager.exe') -Destination $releaseDir
foreach ($name in @('README.md', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $releaseDir
}
Write-Host "Packager disponible dans $releaseDir"
