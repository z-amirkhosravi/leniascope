if "%1"=="x64" (
echo x64 argument set
set arg=x64
) else (
echo defaulting to x86
set arg=x86
)
lib /def: libfftw3-3.dll /machine:%arg%
lib /def: libfftw3l-3 /machine:%arg%
lib /def: libfftw3f-3 /machine:%arg%