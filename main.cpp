#ifndef UNICODE
#define UNICODE
#endif 

#include "headers/mainapp.h"

int WINAPI wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
    app::MainApp app;

    if (SUCCEEDED(CoInitialize(NULL)))
    {

        app.Initialize();
        app.RunMessageLoop();
        CoUninitialize();
    }

    return 0;
}