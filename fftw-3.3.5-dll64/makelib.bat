if "%1"=="x64" (
echo x64 argument set
set arg=x64
) else (
echo defaulting to x86
set arg=x86
)
lib /def:libfftw3-3.def /machine:%arg%
lib /def:libfftw3l-3.def /machine:%arg%
lib /def:libfftw3f-3.def /machine:%arg%

copy libfftw3*.dll ..\fftw_lib
copy libfftw3*.lib ..\fftw_lib
