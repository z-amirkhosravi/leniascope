#include <Windows.h>
#include <CommCtrl.h>
#include <commctrl.h>
#include <wincodec.h>
#include <shobjidl.h>          // for file open/save dialogs
#include <fstream>              // for saving and loading data
#include <format>               // formatting strings with std::format

#pragma comment(lib, "comctl32.lib")


#include <string>
#include <map>

#include "cmap.h"
#include "mainwin.h"
#include "grid.h"

#define SKY_BLUE                RGB(0x87, 0xCE, 0xEB);

#define CODE_LABEL_MU           6
#define CODE_LABEL_E            18
#define CODE_LABEL_R            19
#define CODE_LABEL_SIGMA        20
#define CODE_EDIT_MU            5
#define CODE_EDIT_SIGMA         11
#define CODE_EDIT_R             12
#define CODE_EDIT_E             17
#define CODE_STYLE_MENU         7
#define CODE_RANDOM             1
#define CODE_QUIT               2
#define CODE_UPDOWN_MU          8
#define CODE_UPDOWN_SIGMA       9
#define CODE_UPDOWN_R           10
#define CODE_UPDOWN_E           16
#define CODE_PLAY               13
#define CODE_SAVE               14
#define CODE_LOAD               15

template<class Interface>
inline void SafeRelease(Interface** ppInterfaceToRelease)
{
    if (*ppInterfaceToRelease != NULL)
    {
        (*ppInterfaceToRelease)->Release();
        (*ppInterfaceToRelease) = NULL;
    }
}


PCWSTR  MainWindow::ClassName() const { return L"Main Window Class"; }


MainWindow::MainWindow()
{
    FILETIME ft_now;
    int seed;

    GetSystemTimeAsFileTime(&ft_now);
    seed = (int)ft_now.dwLowDateTime % 10000;
    srand(seed);


    m_pFactory = nullptr;
    m_pRenderTarget = nullptr;
    m_pBrush = nullptr;
    m_pBlackBrush = nullptr;

    /*backgroundColor = RGB(0x87, 0xCE, 0xEB);*/

    window_mode = LENIA_MODE;
    paused = false;

    ll_last_evolved = 0;

    //  window_mode = GOL_MODE;
    //  grid = new Grid<int>(50, 100);
    //  grid->randomize(10,6);
    ///*  grid->setup();*/
    //  grid->evolve();

    cmap = nullptr;
    
    SetStyle(cmap_keys[DEFAULT_STYLE]);

    leniagrid = new LeniaGrid(9,9, 104, 0.15, 0.017, 0.1);

    leniagrid->randomize(0.2f, 0.2f, 0.8f, 0.7f);
    /*leniagrid->insert_orb(50, 50);*/

    if (window_mode == LENIA_MODE) {
        cell_width = 1;
        cell_height = 1;

        grid_width = leniagrid->get_width();
        grid_height = leniagrid->get_height();
    }
    
    if (window_mode == GOL_MODE) {
        cell_width = 10;
        cell_height = 10;

        grid_width = grid->get_width() * cell_width;
        grid_height = grid->get_height() * cell_height;
    }

    LoadIcons();
}

int MainWindow::LoadIcons() {
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
        WS_VISIBLE | WS_CHILD,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)CODE_RANDOM, NULL, NULL);

    button_save = CreateWindow(TEXT("button"), TEXT("Save"),
        WS_VISIBLE | WS_CHILD,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)CODE_SAVE, NULL, NULL);

    button_load = CreateWindow(TEXT("button"), TEXT("Load"),
        WS_VISIBLE | WS_CHILD,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)CODE_LOAD, NULL, NULL);

    button_quit = CreateWindow(TEXT("button"), TEXT("Quit"),
        WS_VISIBLE | WS_CHILD,
        0,0, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, (HMENU)CODE_QUIT, NULL, NULL);

    button_play = CreateWindow(TEXT("button"), TEXT("Play"),
        WS_VISIBLE | WS_CHILD | BS_BITMAP,
        0, 0, 38, 40,
        m_hwnd, (HMENU)CODE_PLAY, NULL, NULL);

    SendMessage(button_play, BM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hPlayIcon);

    style_menu = CreateWindow(TEXT("combobox"),
        NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
        0, 0, STYLE_MENU_WIDTH, STYLE_MENU_HEIGHT,
        m_hwnd, (HMENU)CODE_STYLE_MENU, NULL, NULL);

    for (auto it = cmap_list.begin(); it != cmap_list.end(); it++)
        SendMessage(style_menu, CB_ADDSTRING, 0, (LPARAM)(const wchar_t*)(it->first).c_str());

    SendMessage(style_menu, CB_SETCURSEL, (WPARAM)DEFAULT_STYLE, 0);

    label_mu = CreateWindow(TEXT("static"), TEXT("10\u00b2\u03BC:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_LABEL_MU, NULL, NULL);

    edit_mu = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_EDIT_MU, NULL, NULL);

    SetWindowSubclass(edit_mu, EditControlProcWrapper, CODE_EDIT_MU, (DWORD_PTR) this);

    hUpdownMu = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 0, 0,
        m_hwnd, (HMENU)CODE_UPDOWN_MU, NULL, NULL);

    SendMessage(hUpdownMu, UDM_SETRANGE, 0, MAKELPARAM(100, 1));

    label_sigma = CreateWindow(TEXT("static"), TEXT("10\u00b3\u03C3:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_LABEL_SIGMA, NULL, NULL);

    edit_sigma = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_EDIT_SIGMA, NULL, NULL);

    SetWindowSubclass(edit_sigma, EditControlProcWrapper, CODE_EDIT_SIGMA, (DWORD_PTR) this);

    hUpdownSigma = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0,0, 30, 30,
        m_hwnd, (HMENU)CODE_UPDOWN_SIGMA, NULL, NULL);

    SendMessage(hUpdownSigma, UDM_SETRANGE, 0, MAKELPARAM(100, 1));

    label_R = CreateWindow(TEXT("static"), TEXT("R:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0,0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_LABEL_R, NULL, NULL);

    edit_R = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_EDIT_R, NULL, NULL);

    SetWindowSubclass(edit_R, EditControlProcWrapper, CODE_EDIT_R, (DWORD_PTR) this);

    hUpdownR = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 30, 30,
        m_hwnd, (HMENU)CODE_UPDOWN_R, NULL, NULL);


    /*SendMessage(hUpdownR, UDM_SETBUDDY, 0, MAKELPARAM(edit_R, 0));*/
    SendMessage(hUpdownR, UDM_SETRANGE, 0, MAKELPARAM(200, 1));

    label_e = CreateWindow(TEXT("static"), TEXT("10\u00b2\u03b5:"),
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, LABEL_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_LABEL_E, NULL, NULL);

    edit_e = CreateWindow(TEXT("edit"), NULL,
        WS_BORDER | WS_VISIBLE | WS_CHILD | ES_LEFT | SS_CENTER,
        0, 0, EDIT_WIDTH, LABEL_HEIGHT,
        m_hwnd, (HMENU)CODE_EDIT_E, NULL, NULL);

    SetWindowSubclass(edit_e, EditControlProcWrapper, CODE_EDIT_E, (DWORD_PTR) this);

    hUpdowne = CreateWindow(UPDOWN_CLASS, NULL, 
        WS_CHILDWINDOW | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 30, 30,
        m_hwnd, (HMENU)CODE_UPDOWN_E, NULL, NULL);

    SendMessage(hUpdowne, UDM_SETRANGE, 0, MAKELPARAM(100, 1));

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

    hdwp = DeferWindowPos(hdwp, button_quit, HWND_TOPMOST, 
        button_list_x, button_list_y + 3 * (BUTTON_VERTICAL_SPACE + BUTTON_HEIGHT),
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // horizontal dials:

    hdwp = DeferWindowPos(hdwp, label_mu, HWND_TOPMOST, 
        dial_list_x, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_mu, HWND_TOPMOST, 
        dial_list_x + LABEL_WIDTH, dial_list_y ,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdownMu, HWND_TOPMOST, 
        dial_list_x + LABEL_WIDTH + EDIT_WIDTH -  UP_DOWN_OFFSET, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, label_sigma, HWND_TOPMOST, 
        dial_list_x + (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE), dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_sigma, HWND_TOPMOST, 
        dial_list_x + (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE) + LABEL_WIDTH, dial_list_y ,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdownSigma, HWND_TOPMOST, 
        dial_list_x + (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE) + LABEL_WIDTH + EDIT_WIDTH - UP_DOWN_OFFSET, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, label_R, HWND_TOPMOST, 
        dial_list_x + 2 * (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE), dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_R, HWND_TOPMOST,
        dial_list_x + 2 * (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE) + LABEL_WIDTH, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdownR, HWND_TOPMOST, 
        dial_list_x + 2 * (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE) + LABEL_WIDTH + EDIT_WIDTH - UP_DOWN_OFFSET, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, label_e, HWND_TOPMOST,
        dial_list_x + 3 * (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE), dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, edit_e, HWND_TOPMOST,
        dial_list_x + 3 * (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE) + LABEL_WIDTH, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, hUpdowne, HWND_TOPMOST,
        dial_list_x + 3 * (DIAL_WIDTH + DIAL_HORIZONTAL_SPACE) + LABEL_WIDTH + EDIT_WIDTH - UP_DOWN_OFFSET, dial_list_y,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // bottom gadgets

    hdwp = DeferWindowPos(hdwp, button_play, HWND_TOPMOST,
        grid_origin_x + (grid_width / 2), grid_origin_y + grid_height * cell_height + 10, 
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    hdwp = DeferWindowPos(hdwp, style_menu, HWND_TOPMOST,
        grid_origin_x, grid_origin_y + grid_height * cell_height + 10,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EndDeferWindowPos(hdwp);

}

void MainWindow::SetStyle(const std::wstring cmap_label) 
{
    std::map < std::string, std::vector<std::vector<double>>> cmap_data;

    cmap_data = cmap_list.find(cmap_label)->second;
    if (cmap != nullptr)
        delete cmap;
    cmap = new CMap(cmap_data["red"], cmap_data["green"], cmap_data["blue"]);
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

        case CODE_PLAY:
            leniagrid->toggle_pause();

            if (leniagrid->is_paused()) 
                SendMessage(button_play, BM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hPauseIcon);
            else 
                SendMessage(button_play, BM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hPlayIcon);
            break;

        case CODE_RANDOM:
            leniagrid->randomize();
            /*InvalidateRect(m_hwnd, NULL, FALSE);*/
            OnPaint();                                  // manually repaint, because we want it to happen even if paused
            break;

        case CODE_SAVE:
            SaveDialog();
            break;

        case CODE_LOAD:
            OpenDialog();
            break;

        case CODE_QUIT:
            DiscardGraphicsResources();
            SafeRelease(&m_pFactory);
            PostQuitMessage(0);

        case CODE_STYLE_MENU:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                wchar_t cmap_text[20];
                LRESULT sel = SendMessage(style_menu, CB_GETCURSEL, 0, 0);
                SendMessage(style_menu, CB_GETLBTEXT, sel, (LPARAM)&cmap_text);
                cmap_text[19] = 0;              // compiler complains if we don't make sure it's null-terminated
                SetStyle((std::wstring) cmap_text);
            }
            break;
            
        case CODE_UPDOWN_R:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                LRESULT lr = SendMessage(hUpdownR, UDM_GETPOS, 0, 0);
                leniagrid->fill_kernel((int)LOWORD(lr));
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


    case WM_NOTIFY:
        LRESULT lr;
        switch (((LPNMHDR)lParam)->idFrom) {
        case CODE_UPDOWN_MU:
            lr = SendMessage(hUpdownMu, UDM_GETPOS, 0, 0);
            leniagrid->set_mu((double)LOWORD(lr) / 100);
            break;
        
        case CODE_UPDOWN_SIGMA:
            lr = SendMessage(hUpdownSigma, UDM_GETPOS, 0, 0);
            leniagrid->set_sigma((double)LOWORD(lr) / 1000);
            break;

        case CODE_UPDOWN_R:
            lr = SendMessage(hUpdownR, UDM_GETPOS, 0, 0);
            leniagrid->fill_kernel((int)LOWORD(lr));
            break;

        case CODE_UPDOWN_E:
            lr = SendMessage(hUpdowne, UDM_GETPOS, 0, 0);
            leniagrid->set_epsilon(((double)LOWORD(lr)/100));
            break;
        }
        /*break;*/

    /* this code is not needed because system handles background repaint automatically based on flag set at create time*/
    //case WM_ERASEBKGND:                               
    //    m_pRenderTarget->BeginDraw();
    //    m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::SkyBlue));
    //    m_pRenderTarget->EndDraw();
    //    return -1;

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
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void MainWindow::ValidateControlData(HWND hwnd)
{
    int min, max, cur;              // minimum, maximum, and current values of the parameter
    bool failed_validation = false;

    if (hwnd == edit_e) {
        max = MAX_EDIT_E;
        min = MIN_EDIT_E;
        cur = (int)leniagrid->get_epsilon() * 1000;
    }
    else if (hwnd == edit_mu) {
        max = MAX_EDIT_MU;
        min = MIN_EDIT_MU;
        cur = (int)leniagrid->get_mu() * 100;
    }
    else if (hwnd == edit_sigma) {
        max = MAX_EDIT_SIGMA;
        min = MIN_EDIT_SIGMA;
        cur = (int)leniagrid->get_sigma() * 100;
    }
    else if (hwnd == edit_R) {
        max = MAX_EDIT_R;
        min = MIN_EDIT_R;
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
        PaintLenia2();
        break;
    }
}

void MainWindow::PaintLenia2()
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
            char* m_pBits;

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

            holdbmp = (HBITMAP)SelectObject(memhdc, hbmp);
            GdiFlush();
         
            /* here do the painting */

            for (int y = 0; y < grid_height; y++) {
                for (int t = 0; t < cell_height; t++) {
                    for (int x = 0; x < grid_width; x++)                            
                    {   
                        double pixel_value = leniagrid->get(x, y);                  // get each pixel's color and draw
                        for (int u = 0; u < cell_width; u++) {
                            *m_pBits = (char)(cmap->blue(pixel_value) * 255);
                            m_pBits++;
                            *m_pBits = (char)(cmap->green(pixel_value) * 255);
                            m_pBits++;
                            *m_pBits = (char)(cmap->red(pixel_value) * 255);
                            m_pBits += 2;
                        }
                    }
                }
            }

            BitBlt(hdc, grid_origin_x, grid_origin_y, grid_width * cell_width, grid_height * cell_height, memhdc, 0, 0, SRCCOPY);

            SelectObject(memhdc, holdbmp);
            DeleteDC(memhdc);
            DeleteObject(hbmp);
        }
        EndPaint(m_hwnd, &ps);
    }
}

void MainWindow::PaintLenia()
{
    double pixel_value;

    HRESULT hr = CreateGraphicsResources();
    if (SUCCEEDED(hr))
    {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);

        m_pRenderTarget->BeginDraw();

        /*       for (int x = grid_origin_x; x <= grid_width*cell_width + grid_origin_x; x += cell_width) {
                   m_pRenderTarget->DrawLine(
                       D2D1::Point2F(static_cast<FLOAT>(x), grid_origin_y),
                       D2D1::Point2F(static_cast<FLOAT>(x), grid_origin_y + grid_height * cell_height),
                       m_pBlackBrush, 0.5f);
               }

               for (int y = grid_origin_y; y <= grid_height*cell_width + grid_origin_y; y += cell_height) {
                   m_pRenderTarget->DrawLine(
                       D2D1::Point2F(grid_origin_x, static_cast<FLOAT>(y)),
                       D2D1::Point2F(grid_origin_x + grid_width * cell_width, static_cast<FLOAT>(y)),
                       m_pBlackBrush, 0.5f);
               }*/

        for (int x = 0; x < grid_width; x++)                            // get each pixel's color and draw
            for (int y = 0; y < grid_height; y++) {
                pixel_value = leniagrid->get(x, y);
                m_pBlackBrush->SetColor((*cmap)(pixel_value));          // set color according to specified cmap

                D2D1_RECT_F rect = D2D1::RectF(static_cast<FLOAT>(x * cell_width) + grid_origin_x,
                    static_cast<FLOAT> (y * cell_height) + grid_origin_y,
                    static_cast<FLOAT>((x + 1) * cell_width) + grid_origin_x,
                    static_cast<FLOAT> ((y + 1) * cell_height) + grid_origin_y);
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
    if (m_pRenderTarget != NULL) {
        D2D1_SIZE_F rtSize = m_pRenderTarget->GetSize();

        grid_origin_x = (int) (rtSize.width - grid_width * cell_width) / 2;
        grid_origin_y = (int) (rtSize.height - grid_height * cell_height) / 2;

        button_list_x = grid_origin_x - BUTTON_X_OFFSET;
        button_list_y = grid_origin_y;

        dial_list_x = grid_origin_x;
        dial_list_y = grid_origin_y - DIAL_Y_OFFSET;
    }
}

void MainWindow::update() {
    FILETIME ft_now;
    LONGLONG ll_now;

    GetSystemTimeAsFileTime(&ft_now);

    ll_now = ((LONGLONG)ft_now.dwLowDateTime + ((LONGLONG)(ft_now.dwHighDateTime) << 32LL)) / 10000;

    if (leniagrid->is_paused() && (ll_now - ll_last_evolved > 20)) {
        if (window_mode == GOL_MODE)
            grid->evolve();
        else if (window_mode == LENIA_MODE)
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

MainWindow::~MainWindow() {
    delete grid;
    delete leniagrid;
    delete cmap;
}

HRESULT MainWindow::SaveDialog() {
    IFileDialog* pfd = nullptr;
    /*IFileDialogEvents* pfde = nullptr;*/
    IShellItem* shell_item = nullptr;
    PWSTR filepath;

    COMDLG_FILTERSPEC file_filter[] = { {L"Lenia files", L"*.len"} };

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
                        TaskDialog(NULL, NULL, L"Save Failed", 
                            std::format(L"Could not open {} for writing.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                    }
                    else {
                        if ( leniagrid->save(file) != 0) 
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

    return hr;
}

HRESULT MainWindow::OpenDialog() {
    IFileDialog* pfd = nullptr;
    /*IFileDialogEvents* pfde = nullptr;*/
    IShellItem* shell_item = nullptr;
    IFileDialogCustomize* pfdc;
    PWSTR filepath;

    COMDLG_FILTERSPEC file_filter[] = { {L"Lenia files", L"*.len"} };

    HRESULT hr;

    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

    if (FAILED(hr))
        return hr;

    hr = pfd->QueryInterface(IID_PPV_ARGS(&pfdc));

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
                    file.open(filepath, std::ios::in | std::ios::binary);

                    if (!file) {
                        TaskDialog(NULL, NULL, L"Load Failed",
                            std::format(L"Could not open {} for reading.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                    }
                    else {
                        if (leniagrid->load(file))
                            TaskDialog(NULL, NULL, L"Error", std::format(L"Failed to load data from {}.", filepath).c_str(), NULL, TDCBF_OK_BUTTON | TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
                        else
                            UpdateDials();
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

    return hr;
}
   


