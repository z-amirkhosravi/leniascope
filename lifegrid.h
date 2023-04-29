#pragma once
#ifndef LIFEGRID_H
#define LIFEGRID_H
#include <memory>

class LifeGrid: public Grid<int> {

protected:
	int height, width, total;
	T* value;							// pointer to array holding the main data
	T* aux_grid;						// auxiliary array of the same size, for computations

	virtual void CreateData();
	virtual void DestroyData();

public:

	LifeGrid(int h, int w);
	virtual ~LifeGrid();

	int get_height() const noexcept { return height; }
	int get_width() const noexcept { return width; }
	int size() const noexcept { return total; }

	virtual T get(int x, int y);
	virtual void set(int x, int y, T v);
	virtual void randomize(int, int);
	virtual void evolve();

	void setup();

private:

	int num_neighs(int x, int y);
};

#include "grid.tpp"
#endif
#pragma once
