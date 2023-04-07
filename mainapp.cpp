#include <windows.h>
#include <iostream>
#include <d2d1.h>
#include "mainwin.h"
#include "mainapp.h"

#pragma comment(lib, "d2d1")

MainApp::MainApp() 
{}

//MainApp::~MainApp()
//{
//}


void MainApp::RunMessageLoop()
{
    MSG msg;
    bool cont = true, bGotMsg;
    
    while (cont) 
    {
        // Use PeekMessage() so we can use idle time to render the scene. 
        bGotMsg = (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE) != 0);

        if (bGotMsg)
        {

            TranslateMessage(&msg);
            DispatchMessage(&msg);
            cont = (WM_QUIT != msg.message);
        }
        else
        {
            win.update();
        }
    }
}


HRESULT MainApp::Initialize()
{
    HRESULT hr = S_OK;

    if (SUCCEEDED(hr))
    {
        if (win.Create(L"Lenia", 
                        WS_OVERLAPPEDWINDOW, 
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, // x, y, width, height
                        0, 0) == 0)
            return 0;
        ShowWindow(win.Window(), SW_SHOWNORMAL);
        UpdateWindow(win.Window());
    }

    return hr;
}
