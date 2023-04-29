#ifndef LENGRID_H
#define LENGRID_H

//#include <complex>
#include <fstream>

#include "fftw_lib/fftw3.h"

#include "grid.h"
#include "fft.h"



// abstract class that implements most of the functionality of a Lenia grid, 
// but leaves a few methods that depend on the rank to the children to define

class LeniaBase : public Grid<double>
{
	friend class LeniaIOHandler;
public:
	LeniaBase(int h, int w, int d, int r, double m, double s, double e);
	~LeniaBase();

	// kernel depends on the rank of the grid, so it is up to the child classes to set it up
	virtual void fill_kernel(int r) = 0;

	void set_mu(double m) noexcept { mu = m; }
	void set_sigma(double s) noexcept { sigma = s; }
	void set_epsilon(double e) noexcept { epsilon = e; }

	inline int get_width() const noexcept { return width; }
	inline int get_height() const noexcept { return height; }
	inline int get_depth() const noexcept { return depth; }
	inline double get_mu() const noexcept { return mu; }
	inline double get_sigma() const noexcept { return sigma; }
	inline int get_R() const noexcept { return kernel_radius; }
	inline double get_epsilon() const noexcept { return epsilon; }
	inline int get_size() const noexcept { return total; }


	virtual void evolve() override;
	virtual void randomize() = 0;

protected:

	double mu, sigma;				// variables for normal curve of growth function
	double epsilon;					// infinitesimal step
	int kernel_radius;

	int cx_total;					// the size of cx_data, which is smaller than total because of symmetry

	fftw_complex* K;						// DFT of the convolution kernel
	fftw_complex* cx_data;					// DFT of the value grid
	fftw_plan forward_plan, backward_plan;

	// these two functions call exp(double x), which in general throws an overflow exception if the resulting value is too large
	// however, they only call exp with x negative, so the return value is bounded above by 1 and never overflows
	double gaussian(double x, double m, double s) noexcept;
	double growth(double x) noexcept;

	virtual void DestroyData() override;

private:
	double* get_value() { return value; };

	void save_to_stream(std::fstream&);		// writes the contents of "value" to stream
};

class LeniaGrid : public LeniaBase 
{
public:
	LeniaGrid(int h, int w, int r, double m, double s, double e);
	void fill_kernel(int r);
	void randomize();
	void randomize(float, float, float, float);

private:
	virtual void CreateData() override;
};

class LeniaGrid3D : public LeniaBase
{
public:
	LeniaGrid3D(int h, int w, int d, int r, double m, double s, double e);
	void fill_kernel(int r);
	void randomize();
	void randomize(float, float, float, float, float, float);

private:
	virtual void CreateData() override;
};
#endif

class LeniaIOHandler {
public:
	LeniaIOHandler() {};
	static int save(LeniaBase &,  std::fstream&);
	static LeniaBase* load(std::fstream &);
	static LeniaGrid* load_2d(std::fstream&);
};
