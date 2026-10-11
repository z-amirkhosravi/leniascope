param([int]$Steps=100)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$cli=Join-Path $repo 'x64\Modern\Release\LeniascopeCli.exe'
$out=Join-Path $repo 'x64\Benchmarks'
New-Item -ItemType Directory -Force $out | Out-Null
foreach($dimension in @(2,3)){
    $size=if($dimension -eq 2){1024}else{128}
    $radius=if($dimension -eq 2){104}else{26}
    foreach($backend in @('cpu','gpu')){
        & $cli run --dimensions $dimension --size $size --radius $radius --seed 1 --steps $Steps --backend $backend --sample-every 10 --csv (Join-Path $out "$backend-$dimension.csv")
        if($LASTEXITCODE -ne 0){throw 'Benchmark failed'}
    }
}
