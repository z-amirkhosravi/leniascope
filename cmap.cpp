#include "cmap.h"
#include <vector>
#include <windows.h>
#include <d2d1.h>

//class LinearSegmentedFunction {
//
//public:
//	LinearSegmentedFunction(vector<double *>);
//
//	double operator()(double x);
//};



LinearSegmentedFunction::LinearSegmentedFunction(std::vector<std::vector<double>> dd):
	data(dd){}

double LinearSegmentedFunction::operator()(double x) {
/* LinearSegmentedFunction is how matplotlib color maps implement the RGB values of an input x.

The input data for a cmap is an array of triples {x_i, y1_i, y2_i} i=1,2,...  with x1 = 0 < x2 < ... < xn = 1.

If x lies in the interval [x_i, x_(i+1)), the value y of the camp function is linearly interpolated between y2_i and y1_(i+1),

so the point (x,y) lies on the line segment from (x_i, y2_i) to (x_(i+1), y1_(i+1) )

*/
	auto it = data.begin();
	while (it < data.end() && (it + 1) < data.end() && (*(it + 1))[0] <= x)
		it++;

	if (it == data.end())
		return 0;
	if ((it + 1) == data.end())
		return (*it)[2];

	return (*it)[2] + ((*(it + 1))[1] - (*it)[2]) * (x - (*it)[0]) / ((*(it + 1))[0] - (*it)[0]);  
}

CMap::CMap(std::vector < std::vector<double>> r, std::vector < std::vector<double>> g, std::vector < std::vector<double>> b):
red(r), green(g), blue(b) {}

D2D1_COLOR_F CMap::operator()(double x) {			// evaluating the cmap outputs a D2D1 color data structure
	return D2D1::ColorF(red(x), green(x), blue(x));	
}
