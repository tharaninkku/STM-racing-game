param([string]$Compiler = 'gcc')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$outputExe = Join-Path $outputDir 'test_game.exe'
$sources = @(
    (Join-Path $projectRoot 'Application/game.c'),
    (Join-Path $projectRoot 'Application/player.c'),
    (Join-Path $projectRoot 'Application/obstacle.c'),
    (Join-Path $projectRoot 'Application/collision.c'),
    (Join-Path $PSScriptRoot 'test_game.c')
)
& $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -I (Join-Path $projectRoot 'Application') @sources -o $outputExe
if ($LASTEXITCODE -ne 0) { throw 'Game test compilation failed.' }
& $outputExe
if ($LASTEXITCODE -ne 0) { throw 'Game tests failed.' }
