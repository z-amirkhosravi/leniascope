#include <vector>
#include <windows.h>
#include "grid.h"

#ifndef GRID_TPP
#define GRID_TPP

template <typename T>
Grid<T>::Grid(int h, int w): Grid(h,w,1) {}

template <typename T>
Grid<T>::Grid(int h, int w, int d) : 
	logheight(h), logwidth(w), logdepth(d) {
	height = 1 << h;
	width = 1 << w;
	depth = 1 << d;
	total = height * width * depth;

	CreateData();
}

template <typename T>
void Grid<T>::CreateData() {
	value = new T[total];
	aux_grid = new T[total];
}

template <typename T>
void Grid<T>::DestroyData() {
	delete[] value;
	delete[] aux_grid;
}


template <typename T>
Grid<T>::~Grid() {
	DestroyData();
}

template <typename T>
T Grid<T>::get(int x, int y) {			// this assumes value is not null, and doesn't check
	return value[x + (y * width)];	
}

template <typename T>
T Grid<T>::get(int x, int y, int z) {			
	return value[x + (y + z *height)*width];
}

template <typename T>
void Grid<T>::set(int x, int y, T v) {
	value[x + (y * width)] = v;
}

template <typename T>
void Grid<T>::set(int x, int y, int z, T v) {
	value[(z * height + y) * width + x] = v;
}

template <typename T>
void Grid<T>::randomize(int n, int m) {
	for (int i=0;i<total;i++) 
		value[i] = (T) ((rand() % n) > m? 1 : 0);
}

template <typename T>
void Grid<T>::setup() {

	for (int i = 0; i < total; i++)
		value[i] = 0;

	set(3, 1, 1);
	set(1, 2, 1);
	set(3, 2, 1);
	set(2, 3, 1);
	set(3, 3, 1);
}

template <typename T>
T Grid<T>::num_neighs(int x, int y) {
	T num = 0;

	int xplus, xmin, yplus, ymin;

	xplus = (x == width - 1) ? 0 : x + 1;
	xmin = (x == 0 )? width - 1 : x - 1;
	yplus = (y == height - 1)? 0 : y + 1;
	ymin = (y == 0) ? height - 1 : y - 1;

	num += get(xmin, ymin);
	num += get(x, ymin);
	num += get(xplus, ymin);
	num += get(xmin, y);
	num += get(xplus, y);
	num += get(xmin, yplus);
	num += get(x, yplus);
	num += get(xplus, yplus);


	/* the following is old code that doesn't "wrap around" at the edges */

	//if (x > 0) {		// the three neighbours to the left 
	//	if (y > 0) 
	//		num += get(x - 1, y - 1);
	//	num += get(x - 1, y);
	//	if (y < height - 1) 
	//		num += get(x - 1, y + 1);
	//}

	//if (x < width - 1) { // the three neighbourss to the right
	//	if (y > 0) 
	//		num += get(x + 1, y - 1);
	//	num += get(x + 1, y);
	//	if (y < height - 1)
	//		num += get(x + 1, y + 1);
	//}

	//if (y > 0)			// top neighbour
	//	num += get(x, y - 1);

	//if (y < height - 1)	// bottom neighbour
	//	num += get(x, y + 1);

	return num;
}


template <typename T>
void Grid<T>::evolve() {
	int x = 0;

	for (int j = 0; j < height; j++)
		for (int i = 0; i < width; i++) {
			aux_grid[x] = num_neighs(i, j);
			x++;
		}

	for (x = 0; x < total; x++) {
		if (value[x] > 0) {
			if ((aux_grid[x] < 2) || (aux_grid[x] > 3))
				value[x] = 0;
		}
		else {
			if (aux_grid[x] == 3)
				value[x] = 1;
		}
	}

}


#endif