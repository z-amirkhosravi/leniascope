param([ValidateSet('Release','Debug')][string]$Configuration='Release',[switch]$Test,[switch]$CpuOnly)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $install){throw 'Install Visual Studio C++ tools, the v142 toolset, and a Windows SDK.'}
$msbuild=Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $repo 'Leniascope.sln') /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64
if($LASTEXITCODE -ne 0){throw 'Build failed'}
if($Test){
    Push-Location (Join-Path $repo 'x64\CoreTests')
    try {
        $testArgs=@();if($CpuOnly){$testArgs+='--cpu-only'}
        & '.\CoreTests.exe' @testArgs
        if($LASTEXITCODE -ne 0){throw 'Tests failed'}
    } finally {Pop-Location}
}
