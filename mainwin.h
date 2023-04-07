#pragma once
#include <d2d1.h>

#include "basewindow.h"
#include "grid.h"
#include "lengrid.h"
#include "cmap.h"

#define GOL_MODE	1
#define LENIA_MODE	2

#define BUTTON_WIDTH	80
#define BUTTON_HEIGHT	25

#define DIAL_WIDTH		100
#define EDIT_WIDTH		50
#define LABEL_WIDTH		52
#define LABEL_HEIGHT	25

#define BUTTON_X_OFFSET 100
#define DIAL_Y_OFFSET	40
#define UP_DOWN_OFFSET	16

#define BUTTON_VERTICAL_SPACE   10
#define DIAL_HORIZONTAL_SPACE   10

#define STYLE_MENU_HEIGHT		210	
#define STYLE_MENU_WIDTH		120

#define DEFAULT_STYLE			14			// index of "jet" inside cmap_keys defined in cmap.h


#define MIN_EDIT_R				1
#define MAX_EDIT_R				200

#define MIN_EDIT_E				1
#define MAX_EDIT_E				100

#define MIN_EDIT_MU				1
#define MAX_EDIT_MU				100

#define MIN_EDIT_SIGMA			1
#define MAX_EDIT_SIGMA			100




class MainWindow : public BaseWindow<MainWindow>
{
public:
	MainWindow();
	~MainWindow();
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);	// need to be public because using template
	LRESULT CALLBACK EditControlProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR dwRefData);		// WndProc for edit control subclass

	static LRESULT CALLBACK EditControlProcWrapper(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR dwRefData);		// WndProc for edit control subclass
	PCWSTR  ClassName() const;

	void update();

private:
	int grid_origin_x, grid_origin_y;				// DIP coordinates of the grid
	int button_list_x, button_list_y;				// DIP coordinates of the button list
	int dial_list_x, dial_list_y;					// DIP coordinates of edit and up/down controls

	int cell_width, cell_height, grid_width, grid_height;		// grid and individual cell dimensions

	HWND button_rand, button_quit, button_play, button_save, button_load;					// handles to buttons
	HWND label_mu, label_sigma, label_R, label_e;
	HWND style_menu;
	HWND edit_mu, edit_sigma, edit_R, edit_e;
	HWND hUpdownMu, hUpdownSigma, hUpdownR, hUpdowne;

	HBITMAP hPlayIcon, hPauseIcon, hBMP;

	HRESULT CreateGraphicsResources();
	void DiscardGraphicsResources();
	int LoadIcons();

	void CalculateLayout();
	void PositionButtons();
	void UpdateDials();
	void ValidateControlData(HWND);

	void Resize();
	void OnPaint();
	int OnCreate();

	void PaintGoL();
	void PaintLenia();
	void PaintLenia2();

	void SetCMap(int sel);
	void SetStyle(const std::wstring cmap_label);

	HRESULT SaveDialog();
	HRESULT OpenDialog();

	LONGLONG ll_last_evolved;

	Grid<int> *grid;
	LeniaGrid* leniagrid;

	int window_mode;
	bool paused;

	ID2D1Factory* m_pFactory;
	ID2D1HwndRenderTarget* m_pRenderTarget;
	ID2D1SolidColorBrush* m_pBrush;
	ID2D1SolidColorBrush* m_pBlackBrush;

	CMap* cmap;
	//ID2D1SolidColorBrush* m_pCornflowerBlueBrush;

};
