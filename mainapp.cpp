#include <iostream>
#include <d2d1.h>
#include <windows.h>
#include <CommCtrl.h>

#include "headers/mainwin.h"
#include "headers/mainapp.h"

#pragma comment(lib, "d2d1")


namespace app {
    MainApp::MainApp()
    {}

    MainApp::~MainApp()
    {
    }


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
        HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icex);

        RECT rc;
        SystemParametersInfoW(SPI_GETWORKAREA, NULL, &rc, NULL);        // get screen dimensions


        HRESULT hr = S_OK;

        if (SUCCEEDED(hr))
        {
            if (win.Create(L"Leniascope",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, // x, y, width, height
                0, 0) == 0)
                return 0;
            ShowWindow(win.Window(), SW_SHOWNORMAL);
            UpdateWindow(win.Window());
        }

        return hr;
    }

}