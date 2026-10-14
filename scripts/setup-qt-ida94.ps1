# Build QtBase 6.8.2 for IDA 9.4 without touching the IDA installation.
param(
    [string]$DevDir = 'C:\dev',
    [string]$QtPrefix = '',
    [string]$BuildDir = '',
    [string]$ArchivePath = '',
    [int]$Jobs = 8,
    [switch]$NoDownload
)

$ErrorActionPreference = 'Stop'
$version = '6.8.2'
$archiveName = "qtbase-everywhere-src-$version.zip"
$archiveHash = '44087aec0caa4aa81437e787917d29d97536484a682a5d51ec035878e57c0b5c'
# Use the Qt-published source archive path for QtBase 6.8.2.
$archiveUrls = @(
    "https://download.qt.io/archive/qt/6.8/$version/submodules/$archiveName",
    "https://ftp.fau.de/qtproject/archive/qt/6.8/$version/submodules/$archiveName"
)

function Invoke-Checked( [string]$Executable, [string[]]$Arguments, [string]$Description ) {
    Write-Host "--- $Description"
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    if ( $exitCode -ne 0 ) {
        throw "$Description failed (exit $exitCode)."
    }
}


function Download-QtBaseArchive( [string]$TargetPath ) {
    $partialPath = "$TargetPath.part"
    $downloaded = $false

    foreach ( $url in $archiveUrls ) {
        Write-Host "Downloading QtBase $version from: $url"
        Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue

        $oldPreference = $ErrorActionPreference
        try {
            # -sS avoids curl progress output being interpreted as an error
            # by Windows PowerShell 5.1 when piping to Tee-Object.
            $ErrorActionPreference = 'Continue'
            & curl.exe '--fail' '--location' '--silent' '--show-error' `
                '--retry' '3' '--retry-delay' '2' `
                '--output' $partialPath $url
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $oldPreference
        }

        if ( $exitCode -ne 0 ) {
            Write-Warning "Download failed (curl exit $exitCode): $url"
            continue
        }

        $hash = ( Get-FileHash -LiteralPath $partialPath -Algorithm SHA256 ).Hash
        if ( $hash -ine $archiveHash ) {
            Write-Warning "Download checksum mismatch from $url (SHA-256: $hash)."
            continue
        }

        Move-Item -LiteralPath $partialPath -Destination $TargetPath -Force
        $downloaded = $true
        break
    }

    Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
    if ( -not $downloaded ) {
        throw "Unable to download a verified QtBase archive. Download $archiveName manually from https://download.qt.io/archive/qt/6.8/$version/submodules/ and rerun with -ArchivePath '<path-to-zip>' -NoDownload."
    }
}

function Find-Ninja {
    $ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ( $null -ne $ninja ) { return $ninja.Source }

    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ( $null -ne $cmake ) {
        $nextToCmake = Join-Path ( Split-Path $cmake.Source -Parent ) 'ninja.exe'
        if ( Test-Path -LiteralPath $nextToCmake -PathType Leaf ) {
            return $nextToCmake
        }
    }

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if ( Test-Path -LiteralPath $vswherePath ) {
        $installations = @( & $vswherePath -products '*' -property installationPath )
        foreach ( $installation in $installations ) {
            if ( [string]::IsNullOrWhiteSpace( $installation ) ) { continue }
            $candidate = Join-Path $installation `
                'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
            if ( Test-Path -LiteralPath $candidate -PathType Leaf ) {
                return $candidate
            }
        }
    }
    throw 'Ninja missing. Install it: winget install --id Ninja-build.Ninja --exact'
}

function Initialize-Msvc {
    if ( $null -ne ( Get-Command cl.exe -ErrorAction SilentlyContinue ) ) {
        Write-Host 'MSVC developer environment already active.'
        return
    }

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if ( -not ( Test-Path -LiteralPath $vswherePath -PathType Leaf ) ) {
        throw 'vswhere.exe not found. Run from an x64 VS Developer PowerShell.'
    }
    $vsRoot = @( & $vswherePath -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath ) | Select-Object -First 1
    if ( [string]::IsNullOrWhiteSpace( $vsRoot ) ) {
        throw 'MSVC x64 tools not found. Install the Visual Studio C++ workload.'
    }
    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
    if ( -not ( Test-Path -LiteralPath $vcvars -PathType Leaf ) ) {
        throw "MSVC setup batch file missing: $vcvars"
    }

    $environmentBatch = Join-Path $env:TEMP 'cantordust-msvc-env.cmd'
    try {
        @(
            '@echo off',
            "call `"$vcvars`" >nul || exit /b 1",
            'set'
        ) | Set-Content -LiteralPath $environmentBatch -Encoding Ascii

        $lines = @( & $env:ComSpec /d /c $environmentBatch )
        if ( $LASTEXITCODE -ne 0 ) { throw 'MSVC environment initialization failed.' }
        foreach ( $line in $lines ) {
            if ( $line -match '^([^=]+)=(.*)$' ) {
                [Environment]::SetEnvironmentVariable(
                    $matches[ 1 ], $matches[ 2 ], 'Process' )
            }
        }
    } finally {
        Remove-Item -LiteralPath $environmentBatch -ErrorAction SilentlyContinue
    }
    if ( $null -eq ( Get-Command cl.exe -ErrorAction SilentlyContinue ) ) {
        throw 'MSVC cl.exe is still not on PATH after vcvars64.bat.'
    }
    Write-Host "MSVC environment: $vsRoot"
}

if ( $Jobs -lt 1 ) { throw 'Jobs must be >= 1.' }
if ( -not ( Test-Path -LiteralPath $DevDir -PathType Container ) ) {
    New-Item -ItemType Directory -Path $DevDir -Force | Out-Null
}
$DevDir = ( Resolve-Path -LiteralPath $DevDir ).Path
if ( [string]::IsNullOrWhiteSpace( $QtPrefix ) ) {
    $QtPrefix = Join-Path $DevDir 'qt-ida-6.8.2'
}
if ( [string]::IsNullOrWhiteSpace( $BuildDir ) ) {
    $BuildDir = Join-Path $DevDir 'qt-ida-build-6.8.2'
}
$sourceDir = Join-Path $DevDir "qtbase-everywhere-src-$version"
if ( [string]::IsNullOrWhiteSpace( $ArchivePath ) ) {
    $ArchivePath = Join-Path $DevDir $archiveName
}

foreach ( $path in @( $sourceDir, $BuildDir ) ) {
    if ( $path -match '\s' ) {
        throw "Qt source/build paths cannot contain spaces: $path"
    }
}

$qtConfig = Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'
if ( Test-Path -LiteralPath $qtConfig -PathType Leaf ) {
    # Do not inspect qconfig.h for QT_NAMESPACE. Qt 6.8 exports the
    # definition on the Qt6::Core CMake target instead.
    Write-Host "Existing Qt development installation found: $QtPrefix"
    Write-Host 'The IDA plugin CMake configure will verify QT_NAMESPACE=QT.'
    exit 0
}

foreach ( $tool in @( 'cmake.exe', 'python.exe', 'curl.exe' ) ) {
    if ( $null -eq ( Get-Command $tool -ErrorAction SilentlyContinue ) ) {
        throw "Required tool missing: $tool"
    }
}

$ninjaPath = Find-Ninja
$env:PATH = ( Split-Path $ninjaPath -Parent ) + ';' + $env:PATH
Initialize-Msvc
Write-Host "Qt version: $version (QtBase only: Core/Gui/Widgets)"
Write-Host "Qt namespace: QT"
Write-Host "Qt install prefix: $QtPrefix"

if ( -not ( Test-Path -LiteralPath $sourceDir -PathType Container ) ) {
    if ( -not ( Test-Path -LiteralPath $ArchivePath -PathType Leaf ) ) {
        if ( $NoDownload ) {
            throw "Qt archive missing and -NoDownload was set: $ArchivePath"
        }
        Download-QtBaseArchive $ArchivePath
    }

    $actualHash = ( Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256 ).Hash
    if ( $actualHash -ine $archiveHash ) {
        throw "Qt source SHA-256 mismatch: $actualHash (expected $archiveHash)."
    }
    Write-Host 'QtBase archive SHA-256 verified.'
    Write-Host "Extracting to: $DevDir"
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $DevDir -Force
}

$configure = Join-Path $sourceDir 'configure.bat'
if ( -not ( Test-Path -LiteralPath $configure -PathType Leaf ) ) {
    throw "QtBase configure.bat not found: $configure"
}
if ( -not ( Test-Path -LiteralPath $BuildDir -PathType Container ) ) {
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
}
$BuildDir = ( Resolve-Path -LiteralPath $BuildDir ).Path

Push-Location $BuildDir
try {
    if ( -not ( Test-Path -LiteralPath 'CMakeCache.txt' -PathType Leaf ) ) {
        Invoke-Checked $configure @(
            '-prefix', $QtPrefix,
            '-release', '-shared', '-opensource', '-confirm-license',
            '-nomake', 'tests', '-nomake', 'examples', '-qtnamespace', 'QT',
            '--', '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
        ) 'QtBase configure'
    } else {
        Write-Host "Reusing existing Qt build directory: $BuildDir"
    }
    Invoke-Checked 'cmake.exe' @(
        '--build', $BuildDir, '--parallel', [string]$Jobs
    ) 'QtBase build'
    Invoke-Checked 'cmake.exe' @( '--install', $BuildDir ) 'QtBase installation'
} finally {
    Pop-Location
}

if ( -not ( Test-Path -LiteralPath $qtConfig -PathType Leaf ) ) {
    throw "Qt installation completed but Qt6Config.cmake is missing: $qtConfig"
}
Write-Host "QtBase installed successfully: $QtPrefix"
Write-Host "Next: .\scripts\diagnose-qt.ps1 -QtPrefix '$QtPrefix'"
Write-Host "Then: .\scripts\build-ida.ps1 -Sdk 'C:\dev\ida-sdk-9.4' -QtPrefix '$QtPrefix' -BuildDir '.\build-ida-with-qt'"
