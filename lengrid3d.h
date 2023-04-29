#ifndef LENGRID3D_H
#include "lengrid.h"

class LeniaGrid3D : public LeniaGrid {
public:
	inline int get_depth() const noexcept { return depth; }

	LeniaGrid3D(int h, int w, int d, int r, double m, double s, double e);
private:
	int depth, logdepth;

	virtual void DestroyData() override;
	virtual void CreateData() override;
};
#define LENGRID3D_H
#endif

