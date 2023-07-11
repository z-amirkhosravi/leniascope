#pragma once
#include <windows.h>
#include <d2d1.h>

#include "mainwin.h"

namespace app {

	class MainApp {
	public:
		MainApp();
		~MainApp();

		HRESULT Initialize();
		void RunMessageLoop();

	private:

		MainWindow win;
	};

}

