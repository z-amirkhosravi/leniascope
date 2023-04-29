#include "lengrid3d.h"

LeniaGrid3D::LeniaGrid3D(int h, int w, int d, int r, double m, double s, double e) :
	LeniaGrid(h, w, r, m, s, e),logdepth(d), depth(1 << d)
{
	total = height * width * depth;
}

void LeniaGrid3D::CreateData() {

}

