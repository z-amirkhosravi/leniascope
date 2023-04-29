#include "cmap.h"
#include <vector>
#include <iostream>
#include <windows.h>
#include <d2d1.h>

//class LinearSegmentedFunction {
//
//public:
//	LinearSegmentedFunction(vector<double *>);
//
//	double operator()(double x);
//};

namespace cmap {

	LinearSegmentedFunction::LinearSegmentedFunction(std::vector<std::vector<double>> dd, int multiplier) :
		data(dd) {
		for (auto it = data.begin(); it < data.end(); it++) {
			(*it)[1] *= multiplier;
			(*it)[2] *= multiplier;
		}
	}

	unsigned char LinearSegmentedFunction::operator()(double x) {
		/* LinearSegmentedFunction is how matplotlib color maps implement the RGB values of an input x.

		The input data for a cmap is an vector of triples {x_i, y1_i, y2_i} i=1,2,...  with x1 = 0 < x2 < ... < xn = 1.

		If x lies in the interval [x_i, x_(i+1)), the value y of the camp function is linearly interpolated between y2_i and y1_(i+1),

		so the point (x,y) lies on the line segment from (x_i, y2_i) to (x_(i+1), y1_(i+1) )

		*/
		int i = 0;
		while ((i + 1 < data.size()) && (data[i + 1][0] <= x))
			i++;

		if (i == data.size() - 1)
			return data[i][2];

		return data[i][2] + (data[i + 1][1] - data[i][2]) * (x - data[i][0]) / (data[i + 1][0] - data[i][0]);

		//auto it = data.begin();
		//while (it < data.end() && (it + 1) < data.end() && (*(it + 1))[0] <= x)
		//	it++;

		//if (it == data.end())
		//	return 0;
		//if ((it + 1) == data.end())
		//	return (*it)[2];

		//return ((*it)[2] + ((*(it + 1))[1] - (*it)[2]) * (x - (*it)[0]) / ((*(it + 1))[0] - (*it)[0]));
	}

	CMap::CMap(std::vector < std::vector<double>> r, std::vector < std::vector<double>> g, std::vector < std::vector<double>> b, int multiplier) :
		red(r, multiplier), green(g, multiplier), blue(b, multiplier) {}

	//D2D1_COLOR_F CMap::operator()(double x) {			// evaluating the cmap outputs a D2D1 color data structure
	//	return D2D1::ColorF((FLOAT) red(x), (FLOAT) green(x), (FLOAT) blue(x));
	//}

	uint32_t CMap::operator()(double x) {			// evaluating the cmap outputs a D2D1 color data structure
		return (red(x) << 16) + (green(x) << 8) + blue(x);
	}
}