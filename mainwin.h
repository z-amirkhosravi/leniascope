#pragma once
#include <d2d1.h>
#include <commctrl.h>
#include <memory>

#include "basewindow.h"
#include "grid.h"
#include "lengrid.h"
#include "cmap.h"

#define NULL_MODE		0
#define GOL_MODE		1
#define LENIA_MODE		2
#define LENIA3D_MODE	3

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
namespace app {

	enum {
		min_edit_R = 1, max_edit_R = 200,
		min_edit_e = 1, max_edit_e = 100,
		min_edit_mu = 1, max_edit_mu = 100,
		min_edit_sigma = 1, max_edit_sigma = 100,

		radio_vertical_space = 30
	};

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

		HWND button_rand, button_quit, button_pause, button_save, button_load, button_export;					// handles to buttons
		HWND button_expand;

		HWND radio_lenia, radio_lenia3d;

		HWND label_mu, label_sigma, label_R, label_e;
		HWND style_menu;
		HWND edit_mu, edit_sigma, edit_R, edit_e;
		HWND hUpdownMu, hUpdownSigma, hUpdownR, hUpdowne;

		HBITMAP hPlayIcon, hPauseIcon, hBMP;
		HBITMAP hExpand;

		HMENU hMenu;

		HRESULT CreateGraphicsResources();
		void DiscardGraphicsResources();

		int LoadIcons();
		int LoadBMP(const std::wstring& filename, HBITMAP& hTarget, int w, int h);

		void CalculateLayout();
		void PositionButtons();
		void UpdateDials();
		void UpdateRadio();

		void ValidateControlData(HWND);

		void Resize();
		void OnPaint();
		int OnCreate();

		void SetMode(int mode, bool renew = true);
		void UpdatePauseButton();
		void ProcessMouseWheel(int);
		inline void TogglePause(); 

		void PaintGoL();
		void PaintLenia();
		void PaintLenia3D();

		void SetCMap(int sel);
		void SetStyle(const std::wstring cmap_label);

		HRESULT SaveDialog();
		HRESULT OpenDialog();
		HRESULT ExportDialog();

		int ExportBMP(std::fstream& file);

		LONGLONG ll_last_evolved;

		Grid<int>* grid;
		std::shared_ptr<LeniaBase> leniagrid;

		int window_mode = NULL_MODE;
		bool paused;
		int wait_time;

		int z_slice = 64;		// only used in 3D mode;

		ID2D1Factory* m_pFactory;
		ID2D1HwndRenderTarget* m_pRenderTarget;
		ID2D1SolidColorBrush* m_pBrush;
		ID2D1SolidColorBrush* m_pBlackBrush;

		cmap::CMap* cmap;
		//ID2D1SolidColorBrush* m_pCornflowerBlueBrush;

	};

}