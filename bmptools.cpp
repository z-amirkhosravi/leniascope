#include "bmptools.h"
#include <fstream>

#define WRITE(f, out)			(f).write(reinterpret_cast<char *>(&(out)), sizeof((out)))

int BMPToolChest::LoadFromFile(HWND hwnd, const std::wstring& filename, HBITMAP& hTarget, int w, int h)
{
    HDC hSrcDC, hTargetDC, hdc;
    HBITMAP hbmp, hOldSrc, hOldTarget;

    hbmp = (HBITMAP)LoadImage(NULL, filename.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    
    hdc = GetDC(hwnd);
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

int BMPToolChest::WriteToStream(std::fstream& file, HBITMAP hBmp)
{
    BITMAP bmp;
    BITMAPINFOHEADER header;
    BITMAPFILEHEADER fheader;

    GetObject(hBmp, sizeof(BITMAP), (LPSTR)&bmp);

    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = bmp.bmWidth;
    header.biHeight = -bmp.bmHeight;
    header.biPlanes = 1;
    header.biBitCount = 24;
    header.biCompression = BI_RGB;
    header.biSizeImage = 0;
    header.biXPelsPerMeter = 0;
    header.biYPelsPerMeter = 0;
    header.biClrImportant = 0;
    header.biClrUsed = 0;

    // compute size of data, including padding so rows have byte size multiple of 4:
    int datasize = bmp.bmWidth * 3;
    datasize = (datasize + 4 - (datasize % 4)) * bmp.bmHeight;

    fheader.bfType = 0x4D42; // ascii for "BM" 
    fheader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + datasize;
    fheader.bfReserved1 = 0;
    fheader.bfReserved2 = 0;
    fheader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    
    file.write(reinterpret_cast<char*>(&fheader), sizeof(BITMAPFILEHEADER));
    file.write(reinterpret_cast<char*>(&header), sizeof(BITMAPINFOHEADER));

}

BitmapWrapper::BitmapWrapper(HBITMAP h):
    hBmp(h)
{
    GetObject(hBmp, sizeof(BITMAP), (LPSTR)&bmp);
}

void BitmapWrapper::FillBitmapInfoHeader(BITMAPINFOHEADER& bmpih)
{
    bmpih.biSize = sizeof(BITMAPINFOHEADER);
    bmpih.biWidth = bmp.bmWidth;
    bmpih.biHeight = -bmp.bmHeight;     // negative value indicates data is ordered top-down 
    bmpih.biPlanes = bmp.bmPlanes;
    bmpih.biBitCount = bmp.bmBitsPixel;
    bmpih.biCompression = BI_RGB;
    bmpih.biSizeImage = DataSize();
    bmpih.biClrImportant = 0;
}

void BitmapWrapper::FillBitmapFileHeader(BITMAPFILEHEADER& bmpfh)
{
    bmpfh.bfType = 0x4D42;      // "BM" ascii
    bmpfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + PaletteSize() + DataSize();
    bmpfh.bfReserved1 = 0;
    bmpfh.bfReserved2 = 0;
    bmpfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + PaletteSize();
}

int BitmapWrapper::ColorBits() {
    static const int choices[6] =  {1,4,8,16,24,32} ;       // possible return values
    int cb = bmp.bmPlanes * bmp.bmBitsPixel;
    int ret;

    if (cb == 1)
        ret = 1;
    else if (cb > 24)
        ret = 32;
    else {
        int idx = 1;
        while (idx < 6 && cb <= choices[idx])
            ++idx;
        ret = choices[idx - 1];
    }
    return ret;
}

int BitmapWrapper::DataSize() {             
    int stride = (((bmp.bmWidth * ColorBits()) + 31) & ~31 ) << 3;      // number of bytes in a padded row
    return stride * bmp.bmHeight;
}

void BitmapWrapper::WriteToStream(std::fstream& file)
{
    HDC hdc;
    char * bits;
    BITMAPFILEHEADER bmpfh;
    BITMAPINFO bmpi;
    FillBitmapFileHeader(bmpfh);
    FillBitmapInfoHeader(bmpi.bmiHeader);

    bits = new char[DataSize()];

    hdc = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);

    GetDIBits(hdc, hBmp, 0, bmpi.bmiHeader.biHeight, (LPVOID) bits, &bmpi, DIB_RGB_COLORS );
    DeleteDC(hdc);

    WRITE(file, bmpfh); 
    WRITE(file, bmpi.bmiHeader);

    file.write(bits, DataSize());
    delete[] bits;
}