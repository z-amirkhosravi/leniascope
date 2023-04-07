#pragma once
#include <windows.h>
#include <d2d1.h>

#include "mainapp.h"
#include "mainwin.h"

class MainApp {
public:
	MainApp();
	//~MainApp();

	HRESULT Initialize();
	void RunMessageLoop();

private:

	MainWindow win;
}; 

