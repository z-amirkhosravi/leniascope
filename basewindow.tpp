    
#ifndef BASEWINDOW_TPP
#define BASEWINDOW_TPP

#include <windows.h>
#include "headers/basewindow.h"

template<class DerivedWindowType>
BaseWindow<DerivedWindowType>::BaseWindow() {
    m_hwnd = nullptr;
}

template<class DerivedWindowType>
LRESULT CALLBACK BaseWindow<DerivedWindowType>::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
    DerivedWindowType* pThis = nullptr;

    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (DerivedWindowType*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->m_hwnd = hwnd;
    }
    else
        pThis = (DerivedWindowType*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (pThis)
        return pThis->HandleMessage(uMsg, wParam, lParam);
    else
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

template <class DerivedWindowType>
BOOL BaseWindow<DerivedWindowType>::Create(PCWSTR lpWindowName, DWORD dwStyle, int x , int y , int nWidth, int nHeight, HWND hWndParent, HMENU hMenu)
{
    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = DerivedWindowType::WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = ClassName();
    wc.hbrBackground = CreateSolidBrush(RGB(0x87, 0xce, 0xeb));

    RegisterClass(&wc);

    m_hwnd = CreateWindow( ClassName(), lpWindowName, dwStyle| WS_CLIPCHILDREN, x, y,
        nWidth, nHeight, hWndParent, hMenu, GetModuleHandle(NULL), this
    );

    return (m_hwnd ? TRUE : FALSE);
}

template <class DerivedWindowtype>
HWND BaseWindow<DerivedWindowtype>::Window() const {
    return m_hwnd;
}
#endif