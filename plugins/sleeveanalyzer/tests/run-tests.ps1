param(
    [string]$QtSdk = 'H:\librecad\QtOss\5.12.5\mingw73_32',
    [string]$CompilerRoot = 'H:\librecad\Qt\Tools\mingw730_32\bin',
    [string]$Fixture = ''
)

$ErrorActionPreference = 'Stop'
$testDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$pluginDirectory = (Resolve-Path (Join-Path $testDirectory '..')).Path
$repositoryRoot = (Resolve-Path (Join-Path $pluginDirectory '..\..')).Path
$qtTarget = $QtSdk
$compiler = Join-Path $CompilerRoot 'g++.exe'
$libreCadHeaders = Join-Path $repositoryRoot 'librecad\src\plugins'
$installedRuntime = 'C:\Program Files (x86)\LibreCAD'
$env:PATH = "$installedRuntime;$CompilerRoot;$env:PATH"
if ([string]::IsNullOrEmpty($Fixture)) {
    $Fixture = (Get-ChildItem -LiteralPath 'H:\librecad' -Filter 'M-Q0419A-*.dxf' |
        Select-Object -First 1 -ExpandProperty FullName)
}
if (-not (Test-Path -LiteralPath $Fixture)) {
    throw "DXF fixture not found: $Fixture"
}

$common = @(
    '-std=c++11', '-O2', '-mthreads', '-DUNICODE', '-D_UNICODE', '-DQT_NO_DEBUG',
    '-DQT_GUI_LIB', '-DQT_CORE_LIB',
    "-I$(Join-Path $qtTarget 'include')",
    "-I$(Join-Path $qtTarget 'include\QtCore')",
    "-I$(Join-Path $qtTarget 'include\QtGui')",
    "-I$pluginDirectory",
    "-I$libreCadHeaders",
    "-L$(Join-Path $qtTarget 'lib')"
)

Push-Location $testDirectory
try {
    $analysisBuild = $common + @(
        'SleeveAnalyzerTests.cpp',
        (Join-Path $pluginDirectory 'LongSleeveGenerator.cpp'),
        (Join-Path $pluginDirectory 'SleeveAnalyzer.cpp'),
        (Join-Path $pluginDirectory 'SleeveGeometry.cpp'),
        (Join-Path $pluginDirectory 'SleevePatternFeatures.cpp'),
        (Join-Path $pluginDirectory 'SleeveSideGeometry.cpp'),
        (Join-Path $pluginDirectory 'SleeveTargetGeometry.cpp'),
        (Join-Path $pluginDirectory 'SleeveSizeTable.cpp'),
        '-lQt5Gui', '-lQt5Core', '-o', 'SleeveAnalyzerTests.exe'
    )
    & $compiler @analysisBuild
    if ($LASTEXITCODE -ne 0) { throw 'SleeveAnalyzerTests compilation failed' }
    & .\SleeveAnalyzerTests.exe $Fixture
    if ($LASTEXITCODE -ne 0) { throw 'SleeveAnalyzerTests failed' }

    $loadBuild = $common + @(
        "-I$libreCadHeaders", 'PluginLoadTest.cpp',
        '-lQt5Core', '-o', 'PluginLoadTest.exe'
    )
    & $compiler @loadBuild
    if ($LASTEXITCODE -ne 0) { throw 'PluginLoadTest compilation failed' }
    $pluginDll = Join-Path $repositoryRoot 'windows\resources\plugins\sleeveanalyzer1.dll'
    & .\PluginLoadTest.exe $pluginDll
    if ($LASTEXITCODE -ne 0) { throw 'PluginLoadTest failed' }
} finally {
    Pop-Location
}
