#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
#include <CommCtrl.h>
#include <d2d1.h>
#include "mainapp.h"

//LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
    MainApp app;

    INITCOMMONCONTROLSEX icex;

    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    if (SUCCEEDED(CoInitialize(NULL)))
    {

        app.Initialize();
        app.RunMessageLoop();
        CoUninitialize();
    }


    return 0;
}

//int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
//{
//    // Register the window class.
//    const wchar_t CLASS_NAME[] = L"Sample Window Class";
//
//    WNDCLASS wc = { };
//
//    wc.lpfnWndProc = WindowProc;
//    wc.hInstance = hInstance;
//    wc.lpszClassName = CLASS_NAME;
//
//    RegisterClass(&wc);
//
//    // Create the window.
//
//    HWND hwnd = CreateWindowEx(
//        0,                              // Optional window styles.
//        CLASS_NAME,                     // Window class
//        L"Learn to Program Windows",    // Window text
//        WS_OVERLAPPEDWINDOW,            // Window style
//
//        // Size and position
//        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
//
//        NULL,       // Parent window    
//        NULL,       // Menu
//        hInstance,  // Instance handle
//        NULL        // Additional application data
//    );
//
//    if (hwnd == NULL)
//    {
//        return 0;
//    }
//
//    ShowWindow(hwnd, nCmdShow);
//
//    // Run the message loop.
//
//    MSG msg = { };
//    while (GetMessage(&msg, NULL, 0, 0) > 0)
//    {
//        TranslateMessage(&msg);
//        DispatchMessage(&msg);
//    }
//
//    return 0;
//}
//
//LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
//{
//    switch (uMsg)
//    {
//    case WM_DESTROY:
//        PostQuitMessage(0);
//        return 0;
//
//    case WM_PAINT:
//    {
//        PAINTSTRUCT ps;
//        HDC hdc = BeginPaint(hwnd, &ps);
//
//        // All painting occurs here, between BeginPaint and EndPaint.
//
//        // FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW +1));
//
//        RECT rc;
//        GetClientRect(hwnd, &rc);
//        HGDIOBJ original = NULL;
//        original = SelectObject(ps.hdc, GetStockObject(DC_PEN));
//
//        HPEN blackpen = CreatePen(PS_DASH, 1, 0);
//
//        SelectObject(ps.hdc, blackpen);
//
//        Rectangle(ps.hdc, rc.left + 100, rc.top + 100, rc.right - 100, rc.bottom - 100);
//
//        DeleteObject(blackpen);
//
//        SelectObject(ps.hdc, original);
//
//        EndPaint(hwnd, &ps);
//    }
//    return 0;
//
//    }
//    return DefWindowProc(hwnd, uMsg, wParam, lParam);
//}