#pragma once
#ifndef GRID_H
#define GRID_H
#include <memory>

template<typename T>
class Grid {

protected:
	int height, width, depth, total;
	int logheight, logwidth, logdepth;
	T* value;							// pointer to array holding the main data
	T* aux_grid;						// auxiliary array of the same size, for computations

	virtual void CreateData();
	virtual void DestroyData();

public:

	Grid(int h, int w);
	Grid(int h, int w, int d);
	virtual ~Grid();

	int get_height() const noexcept { return height; }
	int get_width() const noexcept { return width; }
	int get_depth() const noexcept { return depth; }

	int size() const noexcept { return total; }

	virtual T get(int x, int y);
	virtual T get(int x, int y, int z);
	virtual void set(int x, int y, T v);
	virtual void set(int x, int y, int z, T v);
	virtual void randomize(int, int);
	virtual void evolve();

	void setup();

private:

	T num_neighs(int x, int y);
};

#include "grid.tpp"
#endif
