param([Parameter(Mandatory=$true)][string]$ToolchainRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$vc = Join-Path $toolsRoot 'msvc\VC\Tools\MSVC\14.44.35207'
$sdk = Join-Path $toolsRoot 'microsoft.windows.sdk.cpp\c'
$sdkLib = Join-Path $toolsRoot 'microsoft.windows.sdk.cpp.x64\c'
$compiler = Join-Path $vc 'bin\Hostx64\x64\cl.exe'
$output = Join-Path $projectRoot 'build\engine\Release'
$objects = Join-Path $projectRoot 'build\obj\engine\Release'
$oldInclude = $env:INCLUDE; $oldLib = $env:LIB; $oldPath = $env:PATH
Push-Location $projectRoot
try {
    $env:INCLUDE = "$vc\include;$sdk\Include\10.0.26100.0\ucrt;$sdk\Include\10.0.26100.0\shared;$sdk\Include\10.0.26100.0\um"
    $env:LIB = "$vc\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"
    $env:PATH = "$vc\bin\Hostx64\x64;$env:PATH"
    New-Item -ItemType Directory -Force $output,$objects | Out-Null
    & $compiler /nologo /std:c++17 /utf-8 /W4 /WX /EHsc /O2 /MT /LD /DUNICODE /D_UNICODE /DNOMINMAX `
        engine\ValueEngine.cpp ('/Fo' + (Join-Path $objects 'ValueEngine.obj')) `
        ('/Fe' + (Join-Path $output 'DarkEdenValueEngine.dll')) /link /NODEFAULTLIB:oldnames.lib advapi32.lib psapi.lib
    if ($LASTEXITCODE -ne 0) { throw 'C++ engine build failed.' }
    Write-Output "Engine: $output\DarkEdenValueEngine.dll"
} finally {
    $env:INCLUDE = $oldInclude; $env:LIB = $oldLib; $env:PATH = $oldPath
    Pop-Location
}
