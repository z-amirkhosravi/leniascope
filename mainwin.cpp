#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <commctrl.h>
#include <wincodec.h>
#include <shobjidl.h>          // for file open/save dialogs

#pragma comment(lib, "comctl32")

#include <fstream>              
#include <format>             
#include <string>
#include <memory>
#include <map>

#include "cmap.h"
#include "mainwin.h"
#include "grid.h"

#define SKY_BLUE                RGB(0x87, 0xCE, 0xEB);


namespace app 
{

    enum {
        idc_label_mu, idc_label_e, idc_label_R, idc_label_sigma,
        idc_edit_mu, idc_edit_sigma, idc_edit_R, idc_edit_e, idc_style_menu, idc_random, idc_quit,
        idc_updown_mu, idc_updown_sigma, idc_updown_R, idc_updown_e,
        idc_pause, idc_save, idc_load, idc_export, idc_expand,
        idc_radio_mode, idc_radio_lenia, idc_radio_lenia3d
    };


template<class Interface>
inline void SafeRelease(Interface** ppInterfaceToRelease)
{
    if (*ppInterfaceToRelease != NULL) {
        (*ppInterfaceToRelease)->Release();
        (*ppInterfaceToRelease) = NULL;
    }
}


PCWSTR  MainWindow::ClassName() const { return L"Main Window Class"; }


MainWindow::MainWindow():
    m_pFactory(nullptr),
    m_pRenderTarget(nullptr),
    m_pBrush(nullptr),
    m_pBlackBrush(nullptr),
    cmap(nullptr),
    paused(false),
    ll_last_evolved(0)
{
    //hMenu = LoadMenu(NULL, MAKEINTRESOURCE(IDR_MENU1));

    FILETIME ft_now;
    GetSystemTimeAsFileTime(&ft_now);
    srand ((int)ft_now.dwLowDateTime % 10000);

    SetStyle(cmap::cmap_keys[DEFAULT_STYLE]);
    SetMode(LENIA3D_MODE);           
    leniagrid->randomize();

    LoadIcons();
    LoadBMP(L"expand.bmp", hExpand, 50, 50);
}

//  SetMode() changes the type of grid the window is currently displaying
//  if renew is true, it resets the grid object as well

void MainWindow::SetMode(int mode, bool renew)
{
    if (mode == window_mode)        // nothing to do
        return;

    switch (mode) {
    case LENIA_MODE:
        window_mode = LENIA_MODE;
        if (renew) {
            leniagrid = std::make_shared<LeniaGrid>(10, 10, 104, 0.15, 0.017, 0.1);
            leniagrid->randomize();
        }
        /*leniagrid->randomize(0.2f, 0.2f, 0.8f, 0.7f);*/

        cell_width = 1;
        cell_height = 1;

        grid_width = leniagrid->get_width();
        grid_height = leniagrid->get_height();
        wait_time = 10;
        break;
    case LENIA3D_MODE:
        window_mode = LENIA3D_MODE;
        if (renew) {
            leniagrid = std::make_shared<LeniaGrid3D>(7, 7, 7, 26, 0.15, 0.017, 0.1);
            leniagrid->randomize();
        }
        /*leniagrid->randomize(0.2f, 0.2f, 0.2f, 0.7f, 0.5f, 0.5f);*/

        cell_width = 4;
        cell_height = 4;

        grid_width = leniagrid->get_width();
        grid_height = leniagrid->get_height();
        wait_time = 10;
        break;
    case GOL_MODE:
        cell_width = 10;
        cell_height = 10;

        grid_width = grid->get_width() * cell_width;
        grid_height = grid->get_height() * cell_height;
        break;
    }
}

int MainWindow::LoadBMP(const std::wstring &filename, HBITMAP &hTarget, int w, int h) 
{
    HDC hSrcDC, hTargetDC, hdc;
    HBITMAP hbmp, hOldSrc, hOldTarget;

    hbmp = (HBITMAP)LoadImage(NULL, filename.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

    hdc = GetDC(m_hwnd);
    hSrcDC = CreateCompatibleDC(hdc);
    hTargetDC = CreateCompatibleDC(hdc);

    hTarget = CreateCompatibleBitmap(hTargetDC, w, h);

    hOldTarget = (HBITMAP)SelectObject(hTargetDC, hTarget);
    hOldSrc = (HBITMAP)SelectObject(hSrcDC, hbmp);

    BitBlt(hTargetDC, 0, 0, 60, 60, hSrcDC, 0, 0, SRCCOPY);

    SelectObject(hTargetDC, hOldTarget);
    SelectObject(hSrcDC, hOldSrc);
    DeleteDC(hSrcDC);
    DeleteDC(hTargetDC);
    return 0;
}

int MainWindow::LoadIcons() 
{
    HDC hSrcDC, hTargetDC, hdc;
    HBITMAP hOldSrc, hOldTarget;

    hBMP = (HBITMAP)LoadImage(NULL, L"icons.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

 /*   hPlayIcon = (HBITMAP)CopyImage(hBMP, IMAGE_BITMAP, 60, 25, LR_CREATEDIBSECTION);*/

    hdc = GetDC(m_hwnd);
    hSrcDC = CreateCompatibleDC(hdc);
    hTargetDC = CreateCompatibleDC(hdc);

    hPlayIcon = CreateCompatibleBitmap(hTargetDC, 60, 60);
    hPauseIcon = CreateCompatibleBitmap(hTargetDC, 60, 60);

    hOldTarget = (HBITMAP)SelectObject(hTargetDC, hPlayIcon);
    hOldSrc = (HBITMAP)SelectObject(hSrcDC, hBMP);

    BitBlt(hTargetDC, 0,0, 60, 60, hSrcDC, 195, 7, SRCCOPY);

    SelectObject(hTargetDC, hPauseIcon);

    BitBlt(hTargetDC, 0, 0, 60, 60, hSrcDC, 370, 7, SRCCOPY);

    SelectObject(hTargetDC, hOldTarget);
    SelectObject(hSrcDC, hOldSrc);
    DeleteDC(hSrcDC);
    DeleteDC(hTargetDC);

    return 0;
}


HRESULT MainWindow::CreateGraphicsResources()
{
    HRESULT hr = S_OK;
    if (m_pRenderTarget == NULL)
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        hr = m_pFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, size),
            &m_pRenderTarget);

        if (SUCCEEDED(hr))
        {
            const D2D1_COLOR_F color = D2D1::ColorF(1.0f, 1.0f, 0);
            hr = m_pRenderTarget->CreateSolidColorBrush(color, &m_pBrush);

            if (SUCCEEDED(hr)) {
                hr = m_pRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(D2D1::ColorF::Yellow),
                    &m_pBlackBrush
                );
            }
            if (SUCCEEDED(hr))
            {
                CalculateLayout();
            }
        }
    }
    return hr;
}

int MainWindow::OnCreate()
{
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pFactory)))
        return -1;

    CreateGraphicsResources();

    // button are placed at (0,0), then repositioned with the same method that Resize() calls

    button_rand = CreateWindow(TEXT("button"), TEXT("Randomize"),
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)idc_random, NULL, NULL);

    button_save = CreateWindow(TEXT("button"), TEXT("Save"),
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)idc_save, NULL, NULL);

    button_load = CreateWindow(TEXT("button"), TEXT("Load"),
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)idc_load, NULL, NULL);

    button_export = CreateWindow(TEXT("button"), TEXT("Export"),
        WS_VISIBLE | WS_CHILD | BS_BITMAP | WS_BORDER,
        0, 0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)idc_export, NULL, NULL);

    button_quit = CreateWindow(TEXT("button"), TEXT("Quit"),
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)idc_quit, NULL, NULL);

    button_pause = CreateWindow(TEXT("button"), TEXT("Pause"),
        WS_VISIBLE | WS_CHILD | BS_BITMAP | WS_BORDER,
        0, 0, 38, 40,
        m_hwnd, (HMENU)idc_pause, NULL, NULL);

    UpdatePauseButton();        // set button to correct state

    /*button_expand = CreateWindow(TEXT("button"), TEXT("Expand"),
        WS_VISIBLE | WS_CHILD | BS_BITMAP | WS_BORDER,
        0, 0, 50, 50,
        m_hwnd, (HMENU)CODE_EXPAND, NULL, NULL);

    SendMessage(button_expand, BM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hExpand);*/

    style_menu = CreateWindow(TEXT("combobox"),
        NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL| WS_BORDER,
        0, 0, STYLE_MENU_WIDTH, STYLE_MENU_HEIGHT,
        m_hwnd, (HMENU)idc_style_menu, NULL, NULL);

    for (auto it = cmap::cmap_list.begin(); it != cmap::cmap_list.end(); it++)
        SendMessage(style_menu, CB_ADDSTRING, 0, (LPARAM)(const wchar_t*)(it->first).c_str());

    SendMessage(style_menu, CB_SETCURSEL, (WPARAM)DEFAULT_STYLE, 0);

    label_mu = CreateWindow(TEXT("static"), TEXT("10\u00b2\u03BC:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_label_mu, NULL, NULL);

    edit_mu = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_edit_mu, NULL, NULL);

    SetWindowSubclass(edit_mu, EditControlProcWrapper, idc_edit_mu, (DWORD_PTR) this);

    hUpdownMu = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 0, 0,
        m_hwnd, (HMENU)idc_updown_mu, NULL, NULL);

    SendMessage(hUpdownMu, UDM_SETRANGE, 0, MAKELPARAM(100, 1));

    label_sigma = CreateWindow(TEXT("static"), TEXT("10\u00b3\u03C3:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_label_sigma, NULL, NULL);

    edit_sigma = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_edit_sigma, NULL, NULL);

    SetWindowSubclass(edit_sigma, EditControlProcWrapper, idc_edit_sigma, (DWORD_PTR) this);

    hUpdownSigma = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0,0, 30, 30,
        m_hwnd, (HMENU)idc_updown_sigma, NULL, NULL);

    SendMessage(hUpdownSigma, UDM_SETRANGE, 0, MAKELPARAM(100, 1));

    label_R = CreateWindow(TEXT("static"), TEXT("R:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0,0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_label_R, NULL, NULL);

    edit_R = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_edit_R, NULL, NULL);

    SetWindowSubclass(edit_R, EditControlProcWrapper, idc_edit_R, (DWORD_PTR) this);

    hUpdownR = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 30, 30,
        m_hwnd, (HMENU)idc_updown_R, NULL, NULL);

    SendMessage(hUpdownR, UDM_SETRANGE, 0, MAKELPARAM(200, 1));

    label_e = CreateWindow(TEXT("static"), TEXT("10\u00b2\u03b5:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_label_e, NULL, NULL);

    edit_e = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)idc_edit_e, NULL, NULL);

    SetWindowSubclass(edit_e, EditControlProcWrapper, idc_edit_e, (DWORD_PTR) this);

    hUpdowne = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 30, 30,
        m_hwnd, (HMENU)idc_updown_e, NULL, NULL);

    SendMessage(hUpdowne, UDM_SETRANGE, 0, MAKELPARAM(100, 1));

    // Radio button for mode:

    radio_lenia = CreateWindow(TEXT("button"), TEXT("&Lenia"),
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        0,0, 100, 30, m_hwnd, (HMENU)idc_radio_lenia, NULL, NULL);

    radio_lenia3d = CreateWindow(TEXT("button"), TEXT("&Lenia 3D"),
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        0, 0, 100, 30, m_hwnd, (HMENU)idc_radio_lenia3d, NULL, NULL);

    UpdateRadio();
    PositionButtons(); // sets the actual positions 
    UpdateDials();
        
    return 0;
}

void MainWindow::UpdateDials()
{
    SendMessage(hUpdownR, UDM_SETPOS, 0, MAKELPARAM(leniagrid->get_R(), 0));

    SendMessage(hUpdownSigma, UDM_SETPOS, 0, MAKELPARAM((1000 * leniagrid->get_sigma()), 0));

    SendMessage(hUpdownMu, UDM_SETPOS, 0, MAKELPARAM((100 * leniagrid->get_mu()), 0));

    SendMessage(hUpdowne, UDM_SETPOS, 0, MAKELPARAM((100 * leniagrid->get_epsilon()), 0));
}

void MainWindow::PositionButtons()
{
    HDWP hdwp = BeginDeferWindowPos(0);         // we use DeferWindowPos to avoid smearing issues when resizing

    // vertical buttons:

    hdwp = DeferWindowPos(hdwp, button_rand, HWND_TOPMOST,
        button_list_x, button_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, button_save, HWND_TOPMOST, 
        button_list_x, button_list_y + (BUTTON_VERTICAL_SPACE + BUTTON_HEIGHT),
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, button_load, HWND_TOPMOST, 
        button_list_x, button_list_y + 2 * (BUTTON_VERTICAL_SPACE + BUTTON_HEIGHT),
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, button_export, HWND_TOPMOST,
        button_list_x, button_list_y + 3 * (BUTTON_VERTICAL_SPACE + BUTTON_HEIGHT),
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, button_quit, HWND_TOPMOST, 
        button_list_x, button_list_y + 4 * (BUTTON_VERTICAL_SPACE + BUTTON_HEIGHT),
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // dials:

    hdwp = DeferWindowPos(hdwp, label_mu, HWND_TOPMOST, 
        dial_list_x , dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_mu, HWND_TOPMOST, 
        dial_list_x + LABEL_WIDTH, dial_list_y ,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdownMu, HWND_TOPMOST, 
        dial_list_x + LABEL_WIDTH + EDIT_WIDTH - UP_DOWN_OFFSET, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, label_sigma, HWND_TOPMOST, 
        dial_list_x + DIAL_HORIZONTAL_SPACE + LABEL_WIDTH + EDIT_WIDTH, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_sigma, HWND_TOPMOST, 
        dial_list_x + DIAL_HORIZONTAL_SPACE +2*LABEL_WIDTH + EDIT_WIDTH, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdownSigma, HWND_TOPMOST, 
        dial_list_x + DIAL_HORIZONTAL_SPACE + 2*(LABEL_WIDTH + EDIT_WIDTH) - UP_DOWN_OFFSET, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, label_R, HWND_TOPMOST, 
        dial_list_x , dial_list_y + 40,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_R, HWND_TOPMOST,
        dial_list_x + LABEL_WIDTH, dial_list_y + 40,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdownR, HWND_TOPMOST, 
        dial_list_x + LABEL_WIDTH + EDIT_WIDTH - UP_DOWN_OFFSET, dial_list_y + 40,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, label_e, HWND_TOPMOST,
        dial_list_x + DIAL_HORIZONTAL_SPACE + LABEL_WIDTH + EDIT_WIDTH, dial_list_y + 40,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_e, HWND_TOPMOST,
        dial_list_x + DIAL_HORIZONTAL_SPACE + 2*LABEL_WIDTH + EDIT_WIDTH, dial_list_y +  40,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdowne, HWND_TOPMOST,
        dial_list_x + DIAL_HORIZONTAL_SPACE + 2*(LABEL_WIDTH + EDIT_WIDTH) - UP_DOWN_OFFSET, dial_list_y + 40,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // mode radio:

    hdwp = DeferWindowPos(hdwp, radio_lenia, HWND_TOPMOST,
        dial_list_x, dial_list_y + 80,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, radio_lenia3d, HWND_TOPMOST,
        dial_list_x, dial_list_y + 80 + radio_vertical_space,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // bottom gadgets

    hdwp = DeferWindowPos(hdwp, button_pause, HWND_TOPMOST,
        grid_origin_x + grid_width*cell_width + 50, grid_origin_y + grid_height * cell_height/2, 
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    /*hdwp = DeferWindowPos(hdwp, button_expand, HWND_TOPMOST,
        grid_origin_x*cell_width + grid_width + 20, grid_origin_y + 20,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);*/

    hdwp = DeferWindowPos(hdwp, style_menu, HWND_TOPMOST,
        grid_origin_x + grid_width*cell_width + 50, grid_origin_y + grid_height * cell_height - 30,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EndDeferWindowPos(hdwp);
        
}

void MainWindow::SetStyle(const std::wstring cmap_label) 
{
    std::map < std::string, std::vector<std::vector<double>>> cmap_data = cmap::cmap_list.find(cmap_label)->second;

    if (cmap != nullptr)
        delete cmap;
    cmap = new cmap::CMap(cmap_data["red"], cmap_data["green"], cmap_data["blue"]);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LPNMHDR p_nmh = NULL;

    switch (uMsg)
    {
    case WM_CREATE:
        return OnCreate();

    case WM_DESTROY:
        DiscardGraphicsResources();
        SafeRelease(&m_pFactory);
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
        OnPaint();
        break;

    case WM_COMMAND:

        switch (LOWORD(wParam)) {

        case idc_pause:
            TogglePause();
            UpdatePauseButton();
            break;

        case idc_random:
            leniagrid->randomize();
            /*InvalidateRect(m_hwnd, NULL, FALSE);*/
            OnPaint();                                  // manually repaint, because we want it to happen even if paused
            break;

        case idc_save:
            SaveDialog();
            break;

        case idc_export:
            ExportDialog();
            break;

        case idc_load:
            OpenDialog();
            break;

        case idc_quit:
            DiscardGraphicsResources();
            SafeRelease(&m_pFactory);
            PostQuitMessage(0);
            break;

        case idc_style_menu:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                wchar_t cmap_text[20];
                LRESULT sel = SendMessage(style_menu, CB_GETCURSEL, 0, 0);
                SendMessage(style_menu, CB_GETLBTEXT, sel, (LPARAM)&cmap_text);
                cmap_text[19] = 0;              // compiler complains if we don't ensure it's null-terminated
                SetStyle((std::wstring) cmap_text);
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            break;
            
        case idc_updown_R:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                LRESULT lr = SendMessage(hUpdownR, UDM_GETPOS, 0, 0);
                leniagrid->fill_kernel((int)LOWORD(lr));
            }
            break;

        case idc_radio_lenia:
            if (window_mode != LENIA_MODE) {
                SetMode(LENIA_MODE, true);
                Resize();
            }
            break;
        
        case idc_radio_lenia3d:
            if (window_mode != LENIA3D_MODE) {
                SetMode(LENIA3D_MODE, true);
                Resize();
            }
            break;

        }
        break;


    case WM_SIZE:
        Resize();
        return 0;

    //case WM_SETFOCUS:
    //    ValidateControlData((HWND)wParam);
    //    return 0;
    case WM_LBUTTONDOWN:            // this is needed to make edit controls lose focus when clicked outside
        SetFocus(m_hwnd);
        break;

    case WM_MOUSEWHEEL:
        if (window_mode == LENIA3D_MODE)
            ProcessMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        OnPaint();
        break;

    case WM_NOTIFY:

        LRESULT lr;
        LPNMHDR pnmh;
        LPNMUPDOWN nmupdown;
        nmupdown = (LPNMUPDOWN)lParam;
        pnmh = & (nmupdown->hdr);

        if (pnmh->code == UDN_DELTAPOS) { // possible compiler warning here because UDN_DELTAPOS has a fancy definition as negative number  
            int cur = nmupdown->iPos + nmupdown->iDelta;

            switch (pnmh->idFrom) {
            case idc_updown_mu:
                if ((cur >= min_edit_mu) && (cur <= max_edit_mu)) {
                    lr = SendMessage(hUpdownMu, UDM_GETPOS, 0, 0);
                    leniagrid->set_mu((double)LOWORD(lr) / 100);
                }
                break;

            case idc_updown_sigma:
                if ((cur >= min_edit_sigma) && (cur <= max_edit_sigma)) {
                    lr = SendMessage(hUpdownSigma, UDM_GETPOS, 0, 0);
                    leniagrid->set_sigma((double)LOWORD(lr) / 1000);
                }
                break;

            case idc_updown_R:
                if ((cur >= min_edit_R) && (cur <= max_edit_R)) {
                    lr = SendMessage(hUpdownR, UDM_GETPOS, 0, 0);
                    leniagrid->fill_kernel(cur);
                }
                break;

            case idc_updown_e:
                if ((cur >= min_edit_e) && (cur <= max_edit_e)) {
                    lr = SendMessage(hUpdowne, UDM_GETPOS, 0, 0);
                    leniagrid->set_epsilon(((double)LOWORD(lr) / 100));
                }
                break;
            }
        }
        // still need the default proc to take care of max/min limits on edit controls:
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam); 
    default:
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
    return TRUE;
}

LRESULT CALLBACK MainWindow::EditControlProcWrapper(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    MainWindow* mainw = (MainWindow*)dwRefData;
    if (mainw)
        return mainw->EditControlProc(hwnd, uMsg, wParam, lParam, uIdSubclass, dwRefData);
    else
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::EditControlProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{

    switch (uMsg) {
    case WM_GETDLGCODE:
        return (DLGC_WANTALLKEYS | DefSubclassProc( hwnd, uMsg, wParam, lParam));
    case WM_CHAR: 
        if ((wParam == VK_RETURN) || (wParam == VK_TAB) || (wParam == VK_ESCAPE))
            return 0;
        break;
    case WM_KEYDOWN:
        if ((wParam == VK_RETURN) || (wParam == VK_TAB)) {
            this->ValidateControlData(hwnd);
            SetFocus(GetParent(hwnd));
            return FALSE;
        }
        else if (wParam == VK_ESCAPE) {
            this->UpdateDials();
            SetFocus(GetParent(hwnd));
            return FALSE;
        }
        break;

    case WM_KILLFOCUS:
        this->ValidateControlData(hwnd);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}


void MainWindow::ValidateControlData(HWND hwnd)
{
    int min, max, cur;              // minimum, maximum, and current values of the parameter
    bool failed_validation = false;

    if (hwnd == edit_e) {
        max = max_edit_e;
        min = min_edit_e;
        cur = (int)leniagrid->get_epsilon() * 1000;
    }
    else if (hwnd == edit_mu) {
        max = max_edit_mu;
        min = min_edit_mu;
        cur = (int)leniagrid->get_mu() * 100;
    }
    else if (hwnd == edit_sigma) {
        max = max_edit_sigma;
        min = min_edit_sigma;
        cur = (int)leniagrid->get_sigma() * 100;
    }
    else if (hwnd == edit_R) {
        max = max_edit_R;
        min = min_edit_R;
        cur = leniagrid->get_R();
    }
    else
        return;

    wchar_t edit_text[20];
    int len = GetWindowTextLengthW(hwnd);
    int new_value = cur;          // value to be placed in text box

    if (!len || (GetWindowTextW(hwnd, (LPWSTR)edit_text, len + 1) == 0))
        failed_validation = true;

    if (!failed_validation) {
        new_value = wcstoul(edit_text, nullptr, 0);
        if (!new_value) {
            failed_validation = true;
            new_value = cur;
        }
    }

    if (!failed_validation) {
        if (new_value > max) {
            failed_validation = true;
            new_value = max;
        }
        else if (new_value < min) {
            failed_validation = true;
            new_value = min;
        }
    }

    if (failed_validation) 
        SetWindowTextW(hwnd, (LPCWSTR)std::to_wstring(new_value).c_str());
    
    if (new_value != cur) {
        if (hwnd == edit_e)
            leniagrid->set_epsilon((double)new_value / 100);
        else if (hwnd == edit_mu)
            leniagrid->set_mu((double)new_value / 100);
        else if (hwnd == edit_sigma)
            leniagrid->set_sigma((double)new_value / 1000);
        else if (hwnd == edit_R)
            leniagrid->fill_kernel(new_value);
    }
}

void MainWindow::DiscardGraphicsResources()
{
    SafeRelease(&m_pRenderTarget);
    SafeRelease(&m_pBrush);
    SafeRelease(&m_pBlackBrush);
}

void MainWindow :: OnPaint() 
{
    switch (window_mode) {
    case GOL_MODE:
        PaintGoL();
        break;
    case LENIA_MODE:
        PaintLenia();
        break;
    case LENIA3D_MODE:
        PaintLenia3D();
        break;
    }
}

void MainWindow::PaintLenia()
{
    HRESULT hr = CreateGraphicsResources();
    if (SUCCEEDED(hr))
    {
        RECT rc;
        HDC hdc;
        PAINTSTRUCT ps;

        GetClientRect(m_hwnd, &rc);
        hdc = GetDC(m_hwnd);         //get device context 

        BeginPaint(m_hwnd, &ps);

        if (hdc != NULL) {
            HDC memhdc;
            HBITMAP hbmp, holdbmp;
            BITMAPINFO bmi;
            uint32_t* m_pBits;

            memset(&bmi, 0, sizeof(BITMAPINFO));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = grid_width * cell_width;
            bmi.bmiHeader.biHeight = -grid_height * cell_height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&m_pBits, NULL, NULL);
            memhdc = CreateCompatibleDC(hdc);
            /*hbmp = CreateCompatibleBitmap(hdc, grid_width * cell_width, grid_height * cell_height);*/

            if (hbmp && memhdc) {     // if failed to create structures, do nothing this cycle

                holdbmp = (HBITMAP)SelectObject(memhdc, hbmp);
                GdiFlush();

                // The following code seems to do redunant work: it repeats each row's calculations several (cell_height) times.
                // But this is not that big a deal. It only matters when cell_height > 1, and in that case the grid must be smaller, meaning
                // the evolve() function elsewhere runs much faster.

                for (int y = 0; y < grid_height; y++) {
                    for (int t = 0; t < cell_height; t++) {
                        for (int x = 0; x < grid_width; x++) {
                            for (int u = 0; u < cell_width; u++) {
                                *m_pBits = (*cmap)(leniagrid->get(x, y));
                                m_pBits++;
                            }
                        }
                    }
                }

                BitBlt(hdc, grid_origin_x, grid_origin_y, grid_width * cell_width, grid_height * cell_height, memhdc, 0, 0, SRCCOPY);

                SelectObject(memhdc, holdbmp);
            }

            if (memhdc)
                DeleteDC(memhdc);
            if (hbmp)
                DeleteObject(hbmp);
        }
        EndPaint(m_hwnd, &ps);
    }

}

void MainWindow::PaintLenia3D()
{
     HRESULT hr = CreateGraphicsResources();
    if (SUCCEEDED(hr))
    {
        RECT rc;
        HDC hdc;
        PAINTSTRUCT ps;

        GetClientRect(m_hwnd, &rc);
        hdc = GetDC(m_hwnd);         //get device context 

        BeginPaint(m_hwnd, &ps);

        if (hdc != NULL) {
            HDC memhdc;
            HBITMAP hbmp, oldhbmp;
            BITMAPINFO bmi;
            uint32_t* m_pBits;

            memset(&bmi, 0, sizeof(BITMAPINFO));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = grid_width * cell_width;
            bmi.bmiHeader.biHeight = -grid_height * cell_height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&m_pBits, NULL, NULL);
            memhdc = CreateCompatibleDC(hdc);
            /*hbmp = CreateCompatibleBitmap(hdc, grid_width * cell_width, grid_height * cell_height);*/

            if (hbmp && memhdc) {     // if failed to create structures, do nothing this cycle

                oldhbmp = (HBITMAP)SelectObject(memhdc, hbmp);
                GdiFlush();

                // The following code seems to do redunant work: it repeats each row's calculations several (cell_height) times.
                // But this is not that big a deal. It only matters when cell_height > 1, and in that case the grid must be smaller, meaning
                // the evolve() function elsewhere runs much faster.

                for (int y = 0; y < grid_height; y++) {
                    for (int t = 0; t < cell_height; t++) {
                        for (int x = 0; x < grid_width; x++) {
                            for (int u = 0; u < cell_width; u++) {
                                *m_pBits = (*cmap)(leniagrid->get(x, y, z_slice));
                                m_pBits++;
                            }
                        }
                    }
                }

               
                BitBlt(hdc, grid_origin_x, grid_origin_y, grid_width * cell_width, grid_height * cell_height, memhdc, 0, 0, SRCCOPY);

                SelectObject(memhdc, oldhbmp);
            }
            if (memhdc)
                DeleteDC(memhdc);
            if (hbmp)
                DeleteObject(hbmp);

            std::wstring msg_str = std::format(L"z_slice: {:3}", z_slice);
            TextOut(hdc, grid_origin_x, grid_origin_y - 20, msg_str.c_str(), msg_str.length());
        }
            
        EndPaint(m_hwnd, &ps);
    }
}

void MainWindow::PaintGoL()
{

    HRESULT hr = CreateGraphicsResources();
    if (SUCCEEDED(hr))
    {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);

        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::SkyBlue));


        for (int x = (int) grid_origin_x; x <= grid_width + grid_origin_x; x += cell_width) {
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(static_cast<FLOAT>(x), static_cast<FLOAT>(grid_origin_y)),
                D2D1::Point2F(static_cast<FLOAT>(x), static_cast<FLOAT>(grid_origin_y + grid_height)),
                m_pBlackBrush, 0.5f);
        }

        for (int y = (int) grid_origin_y; y <= grid_height + grid_origin_y; y += cell_height) {
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(static_cast<FLOAT>(grid_origin_x), static_cast<FLOAT>(y)),
                D2D1::Point2F(static_cast<FLOAT>(grid_origin_x + grid_width), static_cast<FLOAT>(y)),
                m_pBlackBrush, 0.5f);
        }

        FLOAT cell_buffer = 1;
        for (int x = 0; x < grid_width; x++)             // get each pixel's color from the grid and draw it
            for (int y = 0; y < grid_height; y++) 
                if (grid->get(x, y) > 0) {
                    D2D1_RECT_F rect = D2D1::RectF(static_cast<FLOAT>(x * cell_width) + grid_origin_x + cell_buffer,
                        static_cast<FLOAT> (y * cell_height)+grid_origin_y + cell_buffer, 
                        static_cast<FLOAT>((x+1) * cell_width) + grid_origin_x - cell_buffer,
                        static_cast<FLOAT> ((y+1) * cell_height) + grid_origin_y - cell_buffer);
                    m_pRenderTarget->FillRectangle(&rect, m_pBlackBrush);
                 }

        hr = m_pRenderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET)
        {
            DiscardGraphicsResources();
        }
        EndPaint(m_hwnd, &ps);
    }
}

void MainWindow::Resize()
{
    HRESULT hr = CreateGraphicsResources();

    if (SUCCEEDED(hr))
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
        m_pRenderTarget->Resize(size);

        CalculateLayout();
        PositionButtons();

        InvalidateRect(m_hwnd, NULL, TRUE);     // third parameter must be TRUE to repaint the background
    }
}

void MainWindow::CalculateLayout()
{
    grid_width = leniagrid->get_width();
    grid_height = leniagrid->get_height();

    if (m_pRenderTarget != NULL) {
        D2D1_SIZE_F rtSize = m_pRenderTarget->GetSize();

        grid_origin_x = (int) (rtSize.width - grid_width * cell_width) / 2;
        grid_origin_y = (int) (rtSize.height - grid_height * cell_height) / 2;

        button_list_x = grid_origin_x - BUTTON_X_OFFSET;
        button_list_y = grid_origin_y;

        dial_list_x = grid_origin_x + grid_width * cell_width + DIAL_Y_OFFSET;
        dial_list_y = grid_origin_y;
    }
}

void MainWindow::update() 
{
    FILETIME ft_now;
    LONGLONG ll_now;

    GetSystemTimeAsFileTime(&ft_now);

    ll_now = ((LONGLONG)ft_now.dwLowDateTime + ((LONGLONG)(ft_now.dwHighDateTime) << 32LL)) / 10000;

    if (!paused && (ll_now - ll_last_evolved > wait_time)) {
        if (window_mode == GOL_MODE)
            grid->evolve();
        else if (window_mode == LENIA_MODE)
            leniagrid->evolve();
        else if (window_mode == LENIA3D_MODE)
            leniagrid->evolve();
        ll_last_evolved = ll_now;
        
        //rc.top = grid_origin_y;
        //rc.left = grid_origin_x;
        //rc.bottom = grid_origin_y + grid_height * cell_height;
        //rc.right = grid_origin_x + grid_width * cell_width;

        InvalidateRect(m_hwnd, NULL, FALSE);
    }

   
}

//void MainWindow::SetCMap(const char * cmap_text) {
//    if (cmap != nullptr)
//        delete cmap;
//    }
//}

MainWindow::~MainWindow() 
{
    DiscardGraphicsResources();
    delete cmap;
}

HRESULT MainWindow::ExportDialog() 
{
    IFileDialog* pfd = nullptr;
    /*IFileDialogEvents* pfde = nullptr;*/
    IShellItem* shell_item = nullptr;
    PWSTR filepath;

    COMDLG_FILTERSPEC file_filter[] = { {L"bmp image files", L"*.bmp"} };

    HRESULT hr;

    hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

    if (FAILED(hr))
        return hr;

    if (SUCCEEDED(hr)) {
        pfd->SetFileTypes(ARRAYSIZE(file_filter), file_filter);
        pfd->SetDefaultExtension(L"len");


        hr = pfd->Show(NULL);

        if (SUCCEEDED(hr)) {
            hr = pfd->GetResult(&shell_item);

            if (SUCCEEDED(hr)) {
                shell_item->GetDisplayName(SIGDN_FILESYSPATH, &filepath);

                if (SUCCEEDED(hr)) {

                    std::fstream file;
                    file.open(filepath, std::ios::out | std::ios::trunc | std::ios::binary);

                    if (!file) {
                        TaskDialog(NULL, NULL, L"Export Failed",
                            std::format(L"Could not open {} for writing.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                    }
                    else {
                        if (ExportBMP(file)!=0)
                            TaskDialog(NULL, NULL, L"Export Failed", std::format(L"Failed to export image to {}.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                        file.close();
                    }
                    CoTaskMemFree(filepath);
                }
                shell_item->Release();
            }

        }
    }
    pfd->Release();

    return hr;
}

HRESULT MainWindow::SaveDialog() 
{
    IFileDialog* pfd = nullptr;
    /*IFileDialogEvents* pfde = nullptr;*/
    IShellItem* shell_item = nullptr;
    PWSTR filepath;

    COMDLG_FILTERSPEC file_filter[] = { {L"Lenia files", L"*.len"} };

    bool old_paused = paused;

    HRESULT hr;
  
    if (window_mode == LENIA3D_MODE) {
        TaskDialog(NULL, NULL, L"Save Failed",
            std::format(L"Saving Lenia3D not yet implemented.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
        return E_NOTIMPL;
    }

    hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

    if (FAILED(hr))
        return hr;

    paused = true;

    if (SUCCEEDED(hr)) {
        pfd->SetFileTypes(ARRAYSIZE(file_filter), file_filter);
        pfd->SetDefaultExtension(L"len");


        hr = pfd->Show(NULL);

        if (SUCCEEDED(hr)) {
            hr = pfd->GetResult(&shell_item);

            if (SUCCEEDED(hr)) {
                shell_item->GetDisplayName(SIGDN_FILESYSPATH, &filepath);

                if (SUCCEEDED(hr)) {

                    std::fstream file;
                    file.open(filepath, std::ios::out | std::ios::trunc | std::ios::binary);

                    if (!file) {
                        TaskDialog(NULL, NULL, L"Save Failed", 
                            std::format(L"Could not open {} for writing.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                    }
                    else {
                        if ( LeniaIOHandler::save(*leniagrid,file) != 0) 
                            TaskDialog(NULL, NULL, L"Save Failed", std::format(L"Failed to save data to {}.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                        file.close();
                    }
                    CoTaskMemFree(filepath);
                }
                shell_item->Release();
            }

        }
    }
    pfd->Release();

    paused = old_paused;

    return hr;
}

HRESULT MainWindow::OpenDialog() 
{
    IFileDialog* pfd = nullptr;
    /*IFileDialogEvents* pfde = nullptr;*/
    IShellItem* shell_item = nullptr;
    IFileDialogCustomize* pfdc;
    PWSTR filepath;

    COMDLG_FILTERSPEC file_filter[] = { {L"Lenia files", L"*.len"} };

    HRESULT hr;

    bool old_paused = paused;

    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

    if (FAILED(hr))
        return hr;

    paused = true;

    hr = pfd->QueryInterface(IID_PPV_ARGS(&pfdc));

    if (SUCCEEDED(hr)) {
        pfd->SetFileTypes(ARRAYSIZE(file_filter), file_filter);
        pfd->SetDefaultExtension(L"len");

        hr = pfd->Show(m_hwnd);     // if you pass NULL instead the parent window won't be disabled while dialog box is open

        if (SUCCEEDED(hr)) {
            hr = pfd->GetResult(&shell_item);

            if (SUCCEEDED(hr)) {
                shell_item->GetDisplayName(SIGDN_FILESYSPATH, &filepath);

                if (SUCCEEDED(hr)) {

                    std::fstream file;
                    file.open(filepath, std::ios::in | std::ios::binary);

                    if (!file) {
                        TaskDialog(NULL, NULL, L"Load Failed",
                            std::format(L"Could not open {} for reading.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                    }
                    else {
                        LeniaBase* newlenia = LeniaIOHandler::load(file); 
                        if (newlenia==nullptr)
                            TaskDialog(NULL, NULL, L"Error", std::format(L"Failed to load data from {}.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                        else {                       
                            // smart pointer should automatically call previous object's dtor
                            leniagrid.reset(newlenia);   

                            UpdateDials();

                            if ((window_mode == LENIA_MODE) && (leniagrid->get_depth() > 1)) {
                                SetMode(LENIA3D_MODE);
                                UpdateRadio();
                            }
                            else if ((window_mode == LENIA3D_MODE) && (leniagrid->get_depth() == 1)) {
                                SetMode(LENIA_MODE);
                                UpdateRadio();
                            }

                            Resize();
                            // need to change mode if necessary
                            //if ((window_mode == LENIA_MODE) && (leniagrid->get_depth() > 1))
                            //    SetMode(LENIA3D_MODE);
                            
                        }
                        file.close();
                    }
                    CoTaskMemFree(filepath);
                }
                shell_item->Release();
            }

        }
        pfdc->Release();
    }
    pfd->Release();

    paused = old_paused;
    return hr;
}
void MainWindow::UpdatePauseButton() {
    if (paused)
        SendMessage(button_pause, BM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hPlayIcon);
    else
        SendMessage(button_pause, BM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hPauseIcon);       
}

void MainWindow::ProcessMouseWheel(int delta) {

    if (delta > 0)
        ++z_slice;
    else
        --z_slice;

    int depth = leniagrid->get_depth();

    if (z_slice < 0)
        z_slice += depth;

    if (z_slice >= depth)
        z_slice -= depth;

    return;
}

void MainWindow::TogglePause() {
    paused = paused ? false: true;
    UpdatePauseButton();
}

void MainWindow::UpdateRadio() {
    if (window_mode == LENIA3D_MODE) {
        Button_SetCheck(radio_lenia3d, BST_CHECKED);
        Button_SetCheck(radio_lenia, BST_UNCHECKED);
    }
    else if (window_mode == LENIA_MODE) {
        Button_SetCheck(radio_lenia, BST_CHECKED);
        Button_SetCheck(radio_lenia3d, BST_UNCHECKED);
    }
}

int MainWindow::ExportBMP(std::fstream& file)
{
    BITMAPFILEHEADER bmpfh;
    BITMAPINFOHEADER bmpih;

    // file header
    bmpfh.bfType = 0x4D42;      // "BM"
    bmpfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
        + grid_width * cell_width * grid_height * cell_height * 4;
    bmpfh.bfReserved1 = 0;
    bmpfh.bfReserved2 = 0;
    bmpfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // DIB header
    bmpih.biSize = sizeof(BITMAPINFOHEADER);
    bmpih.biWidth = grid_width * cell_width;
    bmpih.biHeight = -grid_height * cell_height;
    bmpih.biPlanes = 1;
    bmpih.biBitCount = 32;
    bmpih.biCompression = BI_RGB;
    bmpih.biSizeImage = 0;
    bmpih.biXPelsPerMeter = 0;
    bmpih.biYPelsPerMeter = 0;
    bmpih.biClrImportant = 0;
    bmpih.biClrUsed = 0;

    file.write(reinterpret_cast<char*>(&bmpfh), sizeof(BITMAPFILEHEADER));
    file.write(reinterpret_cast<char*>(&bmpih), sizeof(BITMAPINFOHEADER));

    // the pixel array
    for (int y = 0; y < grid_height; y++) 
        for (int t = 0; t < cell_height; t++) 
            for (int x = 0; x < grid_width; x++) 
                for (int u = 0; u < cell_width; u++) {
                    uint32_t rgba;
                    if (window_mode == LENIA_MODE)
                        rgba = (*cmap)(leniagrid->get(x, y));
                    else if (window_mode == LENIA3D_MODE)
                        rgba = (*cmap)(leniagrid->get(x, y, z_slice));
                    file.write(reinterpret_cast<char*>(&rgba), sizeof(uint32_t));
                }

    if (!file)
        return -1;
    else
        return 0;
}
}