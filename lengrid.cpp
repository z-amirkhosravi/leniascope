//#include <complex>

// 22-04-2023: added LeniaBase, LeniaGrid, and LeniaGrid3D

#include <fstream>
#include <cstring>
#include <iostream>

#include "fftw_lib/fftw3.h"
#include "lengrid.h"

#define LEN_FILE_SIGNATURE		"Leniav1"
#define WRITE(f, out)			(f).write(reinterpret_cast<char *>(&(out)), sizeof((out)))
#define READ(f, in)				(f).read(reinterpret_cast<char *>(&(in)), sizeof((in)))

inline int modulo(int a, int b) noexcept			// true mathematical remainder of a divided by b
{			
	const int result = a % b;
	return result >= 0 ? result : result + b;
}

static inline double clip(double x) noexcept 
{
	return (x > 1) ? 1 : ((x < 0) ? 0 : x);
}

static inline void multiply_complex_by(fftw_complex& z, const fftw_complex &w) 
{
	double temp = z[0];
	z[0] = z[0] * w[0] - z[1] * w[1];
	z[1] = z[1] * w[0] +  temp * w[1];
}

LeniaBase::LeniaBase(int h, int w, int d, int r, double m, double s, double e):
Grid(h,w,d), kernel_radius(r), mu(m), sigma(s), epsilon(e), 
K(nullptr), cx_data(nullptr), 
backward_plan(nullptr), forward_plan(nullptr), cx_total(0)			// these are properly initialized in the children
{

}

void LeniaBase::DestroyData() 
{
	fftw_free(K);
	fftw_free(cx_data);

	K = nullptr;
	cx_data = nullptr;

	fftw_destroy_plan(forward_plan);
	fftw_destroy_plan(backward_plan);
}

LeniaBase::~LeniaBase() 
{
	DestroyData();
}

double LeniaBase::growth(double x) noexcept 
{
	return 2 * gaussian(x, mu, sigma) - 1;
}

double LeniaBase::gaussian(double x, double m, double s) noexcept 
{
	return exp( - pow((x - m) / s, 2) / 2);
}




void LeniaBase::evolve() 
{
	int x = 0;
	
	fftw_execute(forward_plan);

	for (x = 0; x < cx_total; x++) 
		multiply_complex_by(cx_data[x], K[x]);

	fftw_execute(backward_plan);

	for (x = 0; x < total; x++) {
		value[x] = value[x] + epsilon * growth(aux_grid[x] / total);
		value[x] = clip(value[x]);
	}
}

void LeniaBase::save_to_stream(std::fstream& file) {
	file.write(reinterpret_cast<char*>(value), sizeof(double) * total);
}

// LeniaGrid class implements rank 2 Lenia 

void LeniaGrid::randomize() 
{
	FILETIME ft_now;

	GetSystemTimeAsFileTime(&ft_now);

	srand((int)ft_now.dwLowDateTime % INT_MAX);

	int border = 150;

	memset(value, 0, sizeof(double) * total);

	for (int t = 0; t < 10; t++) {

		int wi = 80 + (rand() % 50);
		int he = 80 + (rand() % 50);

		int x = border + (rand() % (width - 2 * border));
		int y = border + (rand() % (height - 2 * border));

		for (int j = 0; j < he; j++)
			for (int i = 0; i < wi; i++)
					set(x+i, y + j, (double)rand() / RAND_MAX);

	}

}

void LeniaGrid::randomize(float mi, float mj, float Mi, float Mj) 
{
	FILETIME ft_now;

	GetSystemTimeAsFileTime(&ft_now);

	srand((int)ft_now.dwLowDateTime % INT_MAX);

	int mini = (int) (width * mi);
	int maxi = (int) (width * Mi);
	int minj =  (int) (height * mj);
	int maxj = (int) (height * Mj);

	for (int j = 0; j < height ; j++)
		for (int i = 0; i < width; i++)
		{
			if ((i >= mini) && (i <= maxi) && (j >= minj) && (j <= maxj))
				set(i, j, (double)rand() / RAND_MAX);
			else
				set(i, j, 0);
		}

}

LeniaGrid::LeniaGrid(int h, int w, int r, double m, double s, double e) :
	LeniaBase(h, w, 0, r, m, s, e)
{
	cx_total = width * (height / 2 + 1);
	CreateData();
	fill_kernel(r);
}
void LeniaGrid::CreateData()
{
	cx_data = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * cx_total);
	K = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * cx_total);
	forward_plan = fftw_plan_dft_r2c_2d(width, height, value, cx_data, FFTW_MEASURE);
	backward_plan = fftw_plan_dft_c2r_2d(width, height, cx_data, aux_grid, FFTW_MEASURE);
}

void LeniaGrid::fill_kernel(int r)
{
	kernel_radius = r;

	double* K_aux = (double*)fftw_malloc(sizeof(double) * total);			// allocate temp space for the pre-transformed kernel

	const fftw_plan p = fftw_plan_dft_r2c_2d(width, height, K_aux, K, FFTW_ESTIMATE);

	// populate K_aux with the kernel data:

	memset(K_aux, 0, total * sizeof(double));

	double sum = 0;
	for (int y = -r + 1; y <= r; y++)
		for (int x = -r + 1; x <= r; x++) {
			double temp = sqrt(pow(x, 2) + pow(y, 2)) / r;					// radius of lattice points of the form (x/r, y/r)
			if (temp < 1) {
				int xx = modulo(x, width);
				int yy = modulo(y, height);
				temp = gaussian(temp, 0.5, 0.15);
				K_aux[yy * width + xx] = temp;
				sum += temp;
			}
		}

	// normalize K_aux

	for (int y = -r + 1; y <= r; y++)
		for (int x = -r + 1; x <= r; x++) {
			int xx = modulo(x, width);
			int yy = modulo(y, height);
			K_aux[yy * width + xx] /= sum;
		}

	// take the DFT and destroy pre-transformed kernel, which isn't needed anymore

	fftw_execute(p);
	fftw_destroy_plan(p);
	fftw_free(K_aux);
}

// LeniaGrid3D implements Lenia grid of rank 3

LeniaGrid3D::LeniaGrid3D(int h, int w, int d, int r, double m, double s, double e) :
	LeniaBase(h, w, d, r, m, s, e)
{
	cx_total = width * height * (depth / 2 + 1);
	CreateData();
	fill_kernel(r);
}

void LeniaGrid3D::CreateData()
{
	cx_data = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * cx_total);
	K = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * cx_total);
	forward_plan = fftw_plan_dft_r2c_3d(width, height, depth, value, cx_data, FFTW_MEASURE);
	backward_plan = fftw_plan_dft_c2r_3d(width, height, depth, cx_data, aux_grid, FFTW_MEASURE);
}

void LeniaGrid3D::randomize(float mi, float mj, float mk, float Mi, float Mj, float Mk)
{
	FILETIME ft_now;

	GetSystemTimeAsFileTime(&ft_now);

	srand((int)ft_now.dwLowDateTime % INT_MAX);

	int mini = (int)(width * mi);
	int maxi = (int)(width * Mi);
	int minj = (int)(height * mj);
	int maxj = (int)(height * Mj);
	int mink = (int)(depth * mk);
	int maxk = (int)(depth * Mk);

	for (int j = 0; j < height; j++)
		for (int i = 0; i < width; i++)
			for (int k = 0; k < depth; k++)
			{
				if ((i >= mini) && (i <= maxi) && (j >= minj) && (j <= maxj) && ((k >= mink) && (k <= maxk)))
					set(i, j, k, (double)rand() / RAND_MAX);
				else
					set(i, j, k, 0);
			}

}

void LeniaGrid3D::fill_kernel(int r) {
	kernel_radius = r;

	double* K_aux = (double*)fftw_malloc(sizeof(double) * total);			// allocate temp space for the pre-transformed kernel

	const fftw_plan p = fftw_plan_dft_r2c_3d(width, height, depth, K_aux, K, FFTW_ESTIMATE);

	// populate K_aux with the kernel data:

	memset(K_aux, 0, total * sizeof(double));

	double sum = 0;
	for (int y = -r + 1; y <= r; y++)
		for (int x = -r + 1; x <= r; x++)
			for (int z = -r + 1; z <= r; z++) {
				double temp = sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2)) / r;					// radius of lattice points of the form (x/r, y/r)
				if (temp < 1) {
					int xx = modulo(x, width);
					int yy = modulo(y, height);
					int zz = modulo(z, depth);
					temp = gaussian(temp, 0.5, 0.15);
					K_aux[(zz * height + yy) * width + xx] = temp;
					sum += temp;
				}
			}

	// normalize K_aux

	for (int y = -r + 1; y <= r; y++)
		for (int x = -r + 1; x <= r; x++)
			for (int z = -r + 1; z <= r; z++) {
				int xx = modulo(x, width);
				int yy = modulo(y, height);
				int zz = modulo(z, depth);
				K_aux[(zz * height + yy) * width + xx] /= sum;
			}

	// take the DFT and destroy pre-transformed kernel, which isn't needed anymore

	fftw_execute(p);
	fftw_destroy_plan(p);
	fftw_free(K_aux);
}

void LeniaGrid3D::randomize()
{
	FILETIME ft_now;

	GetSystemTimeAsFileTime(&ft_now);

	srand((int)ft_now.dwLowDateTime % INT_MAX);

	int border = 10;

	memset(value, 0, sizeof(double) * total);

	for (int t = 0; t < 10; t++) {

		// wi, he, de are the dimensions of the box to be filled with random numbers
		int wi = 15 + (rand() % 20);
		int he = 15 + (rand() % 20);
		int de = 15 + (rand() % 20);

		// x,y,z is the corner coordinates of the box
		int x = border + (rand() % (width - 2 * border));
		int y = border + (rand() % (height - 2 * border));
		int z = border + (rand() % (depth - 2 * border));

		if (x + wi >= width)
			x = width - wi - border - 1;
		if (y + he >= height)
			y = height - he - border - 1;
		if (z + de >= depth)
			z = depth - de - border - 1;

		for (int j = 0; j < he; j++)
			for (int i = 0; i < wi; i++)
				for (int k = 0; k < de; k++)
					set(x + i, y + j, z + k, (double)rand() / RAND_MAX);

	}

}

// in the future, this method should read the signature of the file and run the appropriate loader
LeniaBase* LeniaIOHandler::load(std::fstream& file) {
	LeniaGrid* p = load_2d(file);
	return static_cast<LeniaBase*>(p);
}

LeniaGrid* LeniaIOHandler::load_2d(std::fstream& file)
{
	int w, h, r;		// log_2 of width, height, and radius
	double m, s, e;		// mu, sigma, epsilon
	LeniaGrid* p;

	char sig[8];
	file.read(sig, 8);

	if (strcmp(sig, LEN_FILE_SIGNATURE) != 0)
		return nullptr;

	READ(file, h);
	READ(file, w);
	READ(file, r);
	READ(file, m);
	READ(file, s);
	READ(file, e);

	p = new LeniaGrid(h, w, r, m, s, e);

	if (p != nullptr)
		file.read(reinterpret_cast<char*>(p->get_value()), sizeof(double) * p->get_size());

	if (!file) {
		delete p;
		return nullptr;
	}
	else
		return p;
}

int LeniaIOHandler::save(LeniaBase &lb, std::fstream& file)
{
	file << LEN_FILE_SIGNATURE;
	file << (const char)0;

	WRITE(file, lb.logheight);
	WRITE(file, lb.logwidth);

	WRITE(file, lb.kernel_radius);
	WRITE(file, lb.mu);
	WRITE(file, lb.sigma);
	WRITE(file, lb.epsilon);

	lb.save_to_stream(file);

	if (!file)
		return -1;
	else
		return 0;
}
