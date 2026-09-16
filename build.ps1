param([string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
cmake -S $PSScriptRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $BuildDirectory --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
ctest --test-dir $BuildDirectory -C Release --output-on-failure --timeout 30
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
& (Join-Path $PSScriptRoot 'package.ps1') -BuildDirectory $BuildDirectory
