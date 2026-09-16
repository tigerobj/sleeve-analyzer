param(
    [string]$QtSdk = 'H:\librecad\QtOss\5.12.5\mingw73_32',
    [string]$CompilerRoot = 'H:\librecad\Qt\Tools\mingw730_32\bin',
    [switch]$Deploy
)

$ErrorActionPreference = 'Stop'
$pluginDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = (Resolve-Path (Join-Path $pluginDirectory '..\..')).Path
$libreCadHeaders = Join-Path $repositoryRoot 'librecad\src\plugins'
$compiler = Join-Path $CompilerRoot 'g++.exe'
$moc = Join-Path $QtSdk 'bin\moc.exe'
$buildDirectory = Join-Path $pluginDirectory 'build-plugin'
$output = Join-Path $repositoryRoot 'windows\resources\plugins\objectsize1.dll'

foreach ($required in @(
        $compiler,
        $moc,
        (Join-Path $QtSdk 'include\QtCore\QtCore'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required build tool/header not found: $required"
    }
}

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
$env:PATH = "$(Join-Path $QtSdk 'bin');$CompilerRoot;$env:PATH"

$includeArguments = @(
    "-I$(Join-Path $QtSdk 'include')",
    "-I$(Join-Path $QtSdk 'include\QtCore')",
    "-I$(Join-Path $QtSdk 'include\QtGui')",
    "-I$(Join-Path $QtSdk 'include\QtWidgets')",
    "-I$libreCadHeaders",
    "-I$pluginDirectory"
)
$compileArguments = @(
    '-std=c++11', '-O2', '-mthreads', '-Wall', '-Wextra',
    '-DUNICODE', '-D_UNICODE', '-DQT_NO_DEBUG', '-DQT_PLUGIN',
    '-DQT_WIDGETS_LIB', '-DQT_GUI_LIB', '-DQT_CORE_LIB'
) + $includeArguments

$mocSource = Join-Path $buildDirectory 'moc_objectsize.cpp'
& $moc @includeArguments (Join-Path $pluginDirectory 'objectsize.h') -o $mocSource
if ($LASTEXITCODE -ne 0) { throw "moc failed with exit code $LASTEXITCODE" }

$sources = @('objectsize.cpp')
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $buildDirectory ($source -replace '\.cpp$', '.o')
    & $compiler @compileArguments -c (Join-Path $pluginDirectory $source) -o $object
    if ($LASTEXITCODE -ne 0) { throw "compile failed for $source" }
    $objects += $object
}

$mocObject = Join-Path $buildDirectory 'moc_objectsize.o'
& $compiler @compileArguments -c $mocSource -o $mocObject
if ($LASTEXITCODE -ne 0) { throw 'compile failed for generated plugin metadata' }
$objects += $mocObject

$linkArguments = @('-shared', '-mthreads', '-o', $output) + $objects + @(
    "-L$(Join-Path $QtSdk 'lib')",
    '-lQt5Widgets', '-lQt5Gui', '-lQt5Core', '-lmingw32'
)
& $compiler @linkArguments
if ($LASTEXITCODE -ne 0) { throw "link failed with exit code $LASTEXITCODE" }

if ($Deploy) {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    $destinationDirectory = Join-Path $documents 'LibreCAD\plugins'
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
    Copy-Item -LiteralPath $output -Destination (Join-Path $destinationDirectory 'objectsize1.dll') -Force
}

Get-Item -LiteralPath $output
