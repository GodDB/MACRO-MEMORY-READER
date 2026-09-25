param([Parameter(Mandatory=$true)][string]$ToolchainRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$vc = Join-Path $toolsRoot 'msvc\VC\Tools\MSVC\14.44.35207'
$wdk = Join-Path $toolsRoot 'microsoft.windows.wdk.x64\c'
$sdk = Join-Path $toolsRoot 'microsoft.windows.sdk.cpp\c'
$version = '10.0.26100.0'
$compiler = Join-Path $vc 'bin\Hostx64\x64\cl.exe'
$linker = Join-Path $vc 'bin\Hostx64\x64\link.exe'
foreach ($required in @($compiler,$linker,(Join-Path $wdk "Include\$version\km\ntifs.h"))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing build dependency: $required" }
}
$output = Join-Path $projectRoot 'build\driver\Release'
$objects = Join-Path $projectRoot 'build\obj\driver\Release'
New-Item -ItemType Directory -Force $output,$objects | Out-Null
$includes = @(
    (Join-Path $wdk "Include\$version\km"),
    (Join-Path $wdk "Include\$version\km\crt"),
    (Join-Path $sdk "Include\$version\shared"),
    (Join-Path $sdk "Include\$version\ucrt"),
    (Join-Path $vc 'include'),
    $projectRoot
)
$arguments = @('/nologo','/c','/kernel','/guard:cf','/std:c++17','/utf-8','/W4','/WX','/O2','/Zi','/GS','/GR-','/Zl',
    '/D_AMD64_','/D_WIN64','/D_WIN32','/D_WIN32_WINNT=0x0A00','/DNTDDI_VERSION=0x0A000000',
    '/D_HAS_EXCEPTIONS=0',('/Fd' + (Join-Path $objects 'compiler.pdb')))
foreach ($include in $includes) { $arguments += '/I' + $include }
$sources = @('driver\Driver.cpp','Example.cpp','KernelProcessMemoryReader.cpp')
$objectFiles = @()
foreach ($source in $sources) {
    $object = Join-Path $objects (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
    & $compiler @arguments ('/Fo' + $object) (Join-Path $projectRoot $source)
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
    $objectFiles += $object
}
$binary = Join-Path $output 'DarkEdenReader.sys'
$linkArguments = @('/NOLOGO','/DRIVER','/KERNEL','/GUARD:CF','/FILEALIGN:4096','/SUBSYSTEM:NATIVE,10.00','/MACHINE:X64','/NODEFAULTLIB',
    '/ENTRY:GsDriverEntry','/MANIFEST:NO','/INTEGRITYCHECK','/DYNAMICBASE','/NXCOMPAT',
    '/OSVERSION:10.0','/VERSION:10.0','/RELEASE','/SECTION:INIT,D','/MERGE:_TEXT=.text','/MERGE:_PAGE=PAGE','/OPT:REF','/OPT:ICF','/DEBUG',('/PDB:' + (Join-Path $output 'DarkEdenReader.pdb')),
    ('/OUT:' + $binary),('/LIBPATH:' + (Join-Path $wdk "Lib\$version\km\x64")))
& $linker @linkArguments @objectFiles ntoskrnl.lib hal.lib wdmsec.lib BufferOverflowFastFailK.lib libcntpr.lib
if ($LASTEXITCODE -ne 0) { throw 'Driver link failed.' }
Write-Output "Unsigned driver built: $binary"

