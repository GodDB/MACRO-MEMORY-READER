param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [switch]$RunTests,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$vc = Join-Path $toolsRoot 'msvc\VC\Tools\MSVC\14.44.35207'
$sdk = Join-Path $toolsRoot 'microsoft.windows.sdk.cpp\c'
$sdkLib = Join-Path $toolsRoot 'microsoft.windows.sdk.cpp.x64\c'
$compiler = Join-Path $vc 'bin\Hostx64\x64\cl.exe'
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'build\gui\Release' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$objects = Join-Path $projectRoot 'build\obj\gui\Release'
$tests = Join-Path $projectRoot 'build\tests'
$oldInclude = $env:INCLUDE; $oldLib = $env:LIB; $oldPath = $env:PATH
Push-Location $projectRoot
try {
    $env:INCLUDE = "$vc\include;$sdk\Include\10.0.26100.0\ucrt;$sdk\Include\10.0.26100.0\shared;$sdk\Include\10.0.26100.0\um"
    $env:LIB = "$vc\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"
    $env:PATH = "$vc\bin\Hostx64\x64;$env:PATH"
    New-Item -ItemType Directory -Force $output,$objects,$tests | Out-Null
    $common = @('/nologo','/std:c++17','/utf-8','/W4','/WX','/EHsc','/O2','/MT','/DUNICODE','/D_UNICODE')
    $binary = Join-Path $output 'DarkEdenReaderGui.exe'
    & $compiler @common gui\Main.cpp ('/Fo' + (Join-Path $objects 'Main.obj')) ('/Fe' + $binary) /link /NODEFAULTLIB:oldnames.lib /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'GUI build failed.' }
    if ($RunTests) {
        $cases = @(
            @{Name='DriverClientTests'; Sources=@('tests\DriverClientTests.cpp'); Includes=@()},
            @{Name='CoordinateReaderTests'; Sources=@('tests\CoordinateReaderTests.cpp'); Includes=@('/Itests/stubs','/I.')},
            @{Name='KernelProcessAccessTests'; Sources=@('tests\KernelProcessAccessTests.cpp','KernelProcessMemoryReader.cpp'); Includes=@('/Itests/kernel_stubs','/I.')}
        )
        foreach ($case in $cases) {
            $caseObjects = Join-Path $objects $case.Name
            New-Item -ItemType Directory -Force $caseObjects | Out-Null
            $sourceFiles = $case.Sources
            $includeFlags = $case.Includes
            $testBinary = Join-Path $tests ($case.Name + '.exe')
            & $compiler @common @includeFlags @sourceFiles ('/Fo' + $caseObjects + '/') ('/Fe' + $testBinary) /link /NODEFAULTLIB:oldnames.lib advapi32.lib
            if ($LASTEXITCODE -ne 0) { throw "Test build failed: $($case.Name)" }
            & $testBinary
            if ($LASTEXITCODE -ne 0) { throw "Test failed: $($case.Name)" }
        }
        $fixture = Join-Path $tests 'fixture'
        New-Item -ItemType Directory -Force $fixture | Out-Null
        & $compiler @common tests\DriverIntegrationTests.cpp ('/Fo' + (Join-Path $objects 'DriverIntegrationTests.obj')) ('/Fe' + (Join-Path $fixture 'darkeden.exe')) /link /NODEFAULTLIB:oldnames.lib advapi32.lib
        if ($LASTEXITCODE -ne 0) { throw 'Integration fixture build failed.' }
        # Running the fixture needs the new driver loaded; building does not load it.
    }
    $imports = & (Join-Path $vc 'bin\Hostx64\x64\dumpbin.exe') /imports $binary
    if ($LASTEXITCODE -ne 0) { throw 'GUI import inspection failed.' }
    if ($imports | Select-String '\b(OpenProcess|NtOpenProcess|ZwOpenProcess|NtQueryObject|QueryFullProcessImageNameW)\b') {
        throw 'GUI still imports a target process access API.'
    }
    Write-Output "GUI: $binary"
    Write-Output 'Verified: no target process access API imports in GUI.'
} finally {
    $env:INCLUDE = $oldInclude; $env:LIB = $oldLib; $env:PATH = $oldPath
    Pop-Location
}
