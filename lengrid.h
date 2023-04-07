#ifndef LENGRID_H
#define LENGRID_H

#include <complex>
#include <fstream>

#include "grid.h"
#include "fft.h"

class LeniaGrid : public Grid<double>
{
public:
	void setKernel(int h, int w, double* inputK);
	void set_growth(double m, double s);

	LeniaGrid(int h, int w, int r, double m, double s, double e);
	~LeniaGrid();
	void evolve();
	void randomize(float, float, float, float);
	void randomize();
	void insert_orb(int x, int y);

	void set_mu(double m);
	void set_sigma(double s);
	double get_mu();
	double get_sigma();
	int get_R();
	double get_epsilon();
	void set_epsilon(double e);

	void fill_kernel(int r);

	void toggle_pause();
	bool is_paused();

	int save(std::fstream &);
	int load(std::fstream &);

private:
	double* K;				// pointer to array holding the kernel

	std::complex<double>* K_aux;			// pointer to auxiliary array holding the padded kernel, for computinng convolutions
	std::complex<double>* cx_grid;			// pointer to auxiliary complex array holding the transform of the grid
	std::complex<double>* cx_grid2;

	double Ksum;

	int kernel_radius;

	FFT2D* fft;

	double mu, sigma;				// variables for normal curve of growth function
	double epsilon;					// infinitesimal step

	double apply_kernel(int x, int y);
	double gaussian(double x, double m, double s);
	void fill_kernel_uniform(int r);
	double growth(double x);

	bool paused;
};

#endif
