#ifndef BMPTOOLS_H
#define BMPTOOLS_H

#include <fstream>
#include <Windows.h>

class BMPToolChest {
public:
	static int LoadFromFile(HWND hwnd, const std::wstring& filename, HBITMAP& hTarget, int w, int h);

	static int WriteToStream(std::fstream& file, HBITMAP hBmp);
};

class BitmapWrapper {
public:
	BitmapWrapper(HBITMAP h);
	void FillBitmapInfoHeader(BITMAPINFOHEADER&);
	void FillBitmapFileHeader(BITMAPFILEHEADER&);
	void WriteToStream(std::fstream&);

	int PaletteSize() { return 0; };			// palettes not implemented
	int ColorBits();
	int DataSize();
private:
	BITMAP bmp;
	HBITMAP hBmp;
};
#endif