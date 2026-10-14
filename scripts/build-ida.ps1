param(
    [string]$Sdk = $env:IDASDK,
    [string]$QtPrefix = $env:CANTORDUST_QT_PREFIX,
    [string]$BuildDir = "",
    [string]$StageDir = "",
    [string]$NinjaPath = "",
    [string]$QtVersion = "6.8.2",
    [string[]]$ExtraCMakeArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($Sdk)) {
    throw 'Pass -Sdk C:\path\to\ida-sdk or set IDASDK.'
}

$sdkInitInCheckout = Join-Path $Sdk 'src/cmake/idasdk_init.cmake'
$sdkInitInSource = Join-Path $Sdk 'cmake/idasdk_init.cmake'
if (-not (Test-Path -LiteralPath $sdkInitInCheckout -PathType Leaf) -and
    -not (Test-Path -LiteralPath $sdkInitInSource -PathType Leaf)) {
    throw "Invalid IDA SDK directory: $Sdk (expected an SDK checkout or its src directory)"
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $root 'build-ida'
}

if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $StageDir = if ($env:IDABIN) { $env:IDABIN } else {
        Join-Path $BuildDir 'stage'
    }
}

# The IDA SDK recommends Ninja instead of the multi-config VS generator.
if ([string]::IsNullOrWhiteSpace($NinjaPath)) {
    $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $NinjaPath = $command.Source
    }
}

if ([string]::IsNullOrWhiteSpace($NinjaPath)) {
    $candidates = @()

    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        $candidates += Join-Path (Split-Path $cmakeCommand.Source -Parent) 'ninja.exe'
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installations = @(& $vswhere -products '*' -property installationPath)
        foreach ($installation in $installations) {
            if ([string]::IsNullOrWhiteSpace($installation)) { continue }
            $candidates += Join-Path $installation `
                'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        }
    }

    $candidates += Join-Path $env:ProgramFiles 'CMake\bin\ninja.exe'
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $NinjaPath = $candidate
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($NinjaPath) -or
    -not (Test-Path -LiteralPath $NinjaPath -PathType Leaf)) {
    throw @'
Ninja is required to build against the IDA SDK, but ninja.exe was not found.
Install it: winget install --id Ninja-build.Ninja --exact
Open a new PowerShell window and retry, or pass -NinjaPath C:\path\to\ninja.exe.
'@
}

$NinjaPath = (Resolve-Path -LiteralPath $NinjaPath).Path
$env:PATH = (Split-Path $NinjaPath -Parent) + ';' + $env:PATH
Write-Host "Using Ninja: $NinjaPath"

$cmakeArgs = @(
    '-S', $root, '-B', $BuildDir, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
    '-DCMAKE_BUILD_TYPE=Release',
    '-DCANTORDUST_BUILD_IDA_PLUGIN=ON',
    "-DIDASDK=$Sdk", "-DIDABIN=$StageDir",
    "-DCANTORDUST_EXPECTED_QT_VERSION=$QtVersion"
)

if (-not [string]::IsNullOrWhiteSpace($QtPrefix)) {
    $QtPrefix = (Resolve-Path -LiteralPath $QtPrefix -ErrorAction Stop).Path
    $qtConfig = Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'
    if (-not (Test-Path -LiteralPath $qtConfig -PathType Leaf)) {
        throw "Qt prefix has no lib/cmake/Qt6/Qt6Config.cmake: $QtPrefix"
    }
    # Qt 6.8 exports QT_NAMESPACE through Qt6::Core's CMake target.
    # CMake checks the actual imported target during configuration.
    Write-Host "Using Qt prefix: $QtPrefix (namespace checked by CMake)"
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPrefix"
}

if ($ExtraCMakeArgs.Count -gt 0) {
    $cmakeArgs += $ExtraCMakeArgs
}

# Windows PowerShell 5.1 treats stderr from native processes as error records.
# Keep diagnostics flowing; check the native exit code explicitly afterward.
$previousErrorAction = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & cmake @cmakeArgs
    $configureExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
}
if ($configureExitCode -ne 0) {
    throw "CMake configure failed (exit $configureExitCode)."
}

try {
    $ErrorActionPreference = 'Continue'
    & cmake --build $BuildDir --target cantor_dust
    $buildExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
}
if ($buildExitCode -ne 0) {
    throw "IDA plugin compilation failed (exit $buildExitCode)."
}

Write-Host "Native IDA plugin build artifacts under: $StageDir/plugins"
