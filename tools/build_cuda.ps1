param([switch]$Test,[switch]$BenchmarkOnly)
$ErrorActionPreference='Stop'
if($env:PATHEXT -notmatch '(?i)\.EXE'){$env:PATHEXT='.COM;.EXE;.BAT;.CMD'}
$repo=Split-Path $PSScriptRoot -Parent
$cuda=Join-Path $repo 'x64\CudaToolkit'
$build=Join-Path $repo 'x64\CudaBuild'
if(-not (Test-Path "$cuda\bin\nvcc.exe")){throw 'First run: python tools/setup_cuda.py'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install=& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $install){
    $candidate=Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvarsall.bat" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if($candidate){$install=$candidate.FullName -replace '\\VC\\Auxiliary\\Build\\vcvarsall.bat$',''}
}
if(-not $install){throw 'Visual Studio C++ tools are required.'}
$vcvars=Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
# Import only the compiler environment for this process. CUDA 12.9 supports v142.
& cmd.exe /d /c "`"$vcvars`" x64 -vcvars_ver=14.29 >nul && set" | ForEach-Object {
    if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}
}
if($LASTEXITCODE -ne 0){throw 'The MSVC v142 compiler and Windows SDK are required.'}
New-Item -ItemType Directory -Force $build | Out-Null
$obj=Join-Path $build 'cuda_solver.obj'
& "$cuda\bin\nvcc.exe" -c "$repo\src\cuda_solver.cu" -o $obj -I $repo -std=c++17 -O3 `
    --generate-code=arch=compute_120,code=sm_120 --generate-code=arch=compute_75,code=compute_75 `
    -Xcompiler '/MD,/EHsc,/DNOMINMAX,/DUNICODE,/D_UNICODE' --display-error-number
if($LASTEXITCODE -ne 0){throw 'CUDA compilation failed'}
$msbuild=Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild "$repo\tests\CudaBench.vcxproj" /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 `
    "/p:LeniaCudaRoot=$cuda" "/p:LeniaCudaObject=$obj"
if($LASTEXITCODE -ne 0){throw 'CUDA benchmark build failed'}
if(-not $BenchmarkOnly){
    foreach($project in @('Leniascope','LeniascopeCli')){
        & $msbuild "$repo\$project.vcxproj" /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 `
            "/p:OutDir=$repo\x64\Cuda\Release\" "/p:LeniaCudaRoot=$cuda" "/p:LeniaCudaObject=$obj"
        if($LASTEXITCODE -ne 0){throw "$project CUDA build failed"}
    }
}
if($Test){
    Push-Location "$repo\x64\CudaBench"
    try {
        & '.\CudaBench.exe' --tests-only
        if($LASTEXITCODE -ne 0){throw 'CUDA validation failed'}
    } finally {Pop-Location}
}
