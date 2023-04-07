#pragma once
#ifndef GRID_H
#define GRID_H
#include <memory>

template<typename T>
class Grid {
private:

	int num_neighs(int x, int y);

protected:
	int height, width, total;
	int logheight, logwidth;
	T* value;
	T* aux_grid;

public:

	~Grid();
	Grid();

	Grid(int h, int w);

	T get(int x, int y);
	int get_height();
	int get_width();
	int size();
	void set(int x, int y, T v);
	void randomize(int, int);
	void setup();

	void evolve();
};

#include "grid.tpp"
#endif
