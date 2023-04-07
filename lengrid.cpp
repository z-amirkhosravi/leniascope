#include <complex>
#include <fstream>
#include <cstring>
#include <complex>

#include <iostream>

#include "fft.h"
#include "lengrid.h"

#define M_PI 3.14159265358979323846

#define ORBIUM {{0, 0, 0, 0, 0, 0, 0.1, 0.14, 0.1, 0, 0, 0.03, 0.03, 0, 0, 0.3, 0, 0, 0, 0} , \
{0, 0, 0, 0, 0, 0.08, 0.24, 0.3, 0.3, 0.18, 0.14, 0.15, 0.16, 0.15, 0.09, 0.2, 0, 0, 0, 0},\
{0, 0, 0, 0, 0, 0.15, 0.34, 0.44, 0.46, 0.38, 0.18, 0.14, 0.11, 0.13, 0.19, 0.18, 0.45, 0, 0, 0},\
{0, 0, 0, 0, 0.06, 0.13, 0.39, 0.5, 0.5, 0.37, 0.06, 0, 0, 0, 0.02, 0.16, 0.68, 0, 0, 0},\
{0, 0, 0, 0.11, 0.17, 0.17, 0.33, 0.4, 0.38, 0.28, 0.14, 0, 0, 0, 0, 0, 0.18, 0.42, 0, 0},\
{0, 0, 0.09, 0.18, 0.13, 0.06, 0.08, 0.26, 0.32, 0.32, 0.27, 0, 0, 0, 0, 0, 0, 0.82, 0, 0},\
{0.27, 0, 0.16, 0.12, 0, 0, 0, 0.25, 0.38, 0.44, 0.45, 0.34, 0, 0, 0, 0, 0, 0.22, 0.17, 0},\
{0, 0.07, 0.2, 0.02, 0, 0, 0, 0.31, 0.48, 0.57, 0.6, 0.57, 0, 0, 0, 0, 0, 0, 0.49, 0},\
{0, 0.59, 0.19, 0, 0, 0, 0, 0.2, 0.57, 0.69, 0.76, 0.76, 0.49, 0, 0, 0, 0, 0, 0.36, 0},\
{0, 0.58, 0.19, 0, 0, 0, 0, 0, 0.67, 0.83, 0.9, 0.92, 0.87, 0.12, 0, 0, 0, 0, 0.22, 0.07},\
{0, 0, 0.46, 0, 0, 0, 0, 0, 0.7, 0.93, 1, 1, 1, 0.61, 0, 0, 0, 0, 0.18, 0.11},\
{0, 0, 0.82, 0, 0, 0, 0, 0, 0.47, 1, 1, 0.98, 1, 0.96, 0.27, 0, 0, 0, 0.19, 0.1},\
{0, 0, 0.46, 0, 0, 0, 0, 0, 0.25, 1, 1, 0.84, 0.92, 0.97, 0.54, 0.14, 0.04, 0.1, 0.21, 0.05},\
{0, 0, 0, 0.4, 0, 0, 0, 0, 0.09, 0.8, 1, 0.82, 0.8, 0.85, 0.63, 0.31, 0.18, 0.19, 0.2, 0.01},\
{0, 0, 0, 0.36, 0.1, 0, 0, 0, 0.05, 0.54, 0.86, 0.79, 0.74, 0.72, 0.6, 0.39, 0.28, 0.24, 0.13, 0},\
{0, 0, 0, 0.01, 0.3, 0.07, 0, 0, 0.08, 0.36, 0.64, 0.7, 0.64, 0.6, 0.51, 0.39, 0.29, 0.19, 0.04, 0},\
{0, 0, 0, 0, 0.1, 0.24, 0.14, 0.1, 0.15, 0.29, 0.45, 0.53, 0.52, 0.46, 0.4, 0.31, 0.21, 0.08, 0, 0},\
{0, 0, 0, 0, 0, 0.08, 0.21, 0.21, 0.22, 0.29, 0.36, 0.39, 0.37, 0.33, 0.26, 0.18, 0.09, 0, 0, 0},\
{0, 0, 0, 0, 0, 0, 0.03, 0.13, 0.19, 0.22, 0.24, 0.24, 0.23, 0.18, 0.13, 0.05, 0, 0, 0, 0},\
{0, 0, 0, 0, 0, 0, 0, 0, 0.02, 0.06, 0.08, 0.09, 0.07, 0.05, 0.01, 0, 0, 0, 0, 0}}

#define LEN_FILE_SIGNATURE		"Leniav1"

#define WRITE(f, out)		f.write(reinterpret_cast<char *>(&out), sizeof(out))
#define READ(f, in)			f.read(reinterpret_cast<char *>(&in), sizeof(in))

inline int modulo(int a, int b) {			// true mathematical remainder of a divided by b
	const int result = a % b;
	return result >= 0 ? result : result + b;
}

static inline double clip(double x) {
	if (x > 1)
		return 1;
	else if (x < 0)
		return 0;

	return x;
}


LeniaGrid::LeniaGrid(int h, int w, int r, double m, double s, double e):
Grid(h,w), kernel_radius(r), mu(m), sigma(s), epsilon(e), K(nullptr), K_aux(nullptr)
{
	fft = new FFT2D(h,w);
	cx_grid = new std::complex<double>[total];

	fill_kernel(r);
	paused = false;

	/*fill_kernel_uniform(r);*/
}

LeniaGrid::~LeniaGrid() {
	delete[] K;
	delete[] cx_grid;
	delete[] K_aux;
	delete fft;
}

double LeniaGrid::apply_kernel(int x, int y) {

	double result = 0;

	// various index variables
	int initial_x, initial_y;
	int K_x, cur_x, cur_base;
	
	initial_x = modulo(x - kernel_radius + 1, width);
	initial_y = modulo(y - kernel_radius + 1, height);

	K_x = 0;												// index in kernel array
	cur_base = initial_y * width;							// index in value array corresponding to x=0 of current row

	for (int j = 0; j < 2 * kernel_radius; j++) {
		cur_x = initial_x;									// each row starts at this x position in value grid
		for (int i = 0; i < 2 * kernel_radius; i++) {
			result += value[cur_base + cur_x] * K[K_x];
			K_x++;
			cur_x = (cur_x + 1 == width) ? 0 : cur_x + 1;		// increment cur_x and wrap around if necessary
		}

		cur_base = (cur_base + width >= total) ? 0 : cur_base + width;
	}

	return result;
}

void LeniaGrid::set_growth(double m, double s) {
	mu = m;
	sigma = s;
}

void LeniaGrid::set_epsilon(double e) {
	epsilon = e;
}

double LeniaGrid::growth(double x) {
	return 2 * gaussian(x, mu, sigma) - 1;
}

double LeniaGrid::gaussian(double x, double m, double s) {
	return exp( - pow((x - m) / s, 2) / 2);
}

void LeniaGrid::fill_kernel(int r) {
	double sum = 0;
	double temp;
	int t = 0;
	int xx, yy;

	kernel_radius = r;

	if (K != nullptr) 
		delete[] K;

	if (K_aux != nullptr) 
		delete[] K_aux;

	K = new double[4 * r * r];
	K_aux = new std::complex<double>[total];

	for (int t = 0; t < total; t++) 
		K_aux[t] = 0;


	for (int y = -r + 1; y <= r; y++)
		for (int x = -r + 1; x <= r; x++) {
			temp = sqrt(pow(x,2) + pow (y,2)) / r;					// radius of lattice points of the form (x/r, y/r)
			if (temp >= 1)										// exclude points outside the unit circle
				K[t] = 0;
			else
				K[t] = gaussian(temp, 0.5, 0.15);
			sum += K[t];
			t++;
		}

	Ksum = sum;
	for (t = 0; t < 4 * r * r; t++) {
		K[t] = K[t] / sum;							// normalize the values so they sum to 1
	}

	t = 0;
	for (int s = 0; s < 2 * r; s++)
		for (int u = 0; u < 2 * r; u++)
		{
			int xx = modulo(u - r + 1, width);
			int yy = modulo(s - r + 1, height);
			K_aux[yy * width + xx] = (std::complex<double>) K[t];				// copy to auxiliary kernel grid
			t++;
		}
	fft->transform(K_aux);
}

void LeniaGrid::fill_kernel_uniform(int r) {
	double sum = 0;
	int t = 0;

	if (K != nullptr) {			// if K is not nullptr, the kernel was already filled once
		delete[] K;
		K = nullptr;
	}

	kernel_radius = r;
	K = new double[4 * r * r];

	for (t = 0; t < 4 * r * r; t++)			// normalize the values so they sum to 1
		K[t] =  1 / ( (double) 4 * r * r);
}

void LeniaGrid::evolve() {
	int x = 0;

	for (x = 0; x < total; x++)
		cx_grid[x] = (std::complex<double>) value[x]; 
	
	/*for (x = 0; x < total; x++)
		cx_grid[x] = K[x];*/

	fft->transform(cx_grid);
	/*fft->transform(value, cx_grid);*/

	for (x = 0; x < total; x++)
		cx_grid[x] = cx_grid[x] * K_aux[x];

	fft->inverse_transform(cx_grid);

	for (x = 0; x < total; x++) {
		value[x] = clip(value[x] + epsilon * growth(cx_grid[x].real()));
	}

	/*fft->inverse_transform(K_aux);

	for (x = 0; x < total; x++) {
		value[x] = K_aux[x].real() * 300 ;
	}
	fft->transform(K_aux);*/
}

void LeniaGrid::randomize() {
	FILETIME ft_now;

	GetSystemTimeAsFileTime(&ft_now);

	srand((int)ft_now.dwLowDateTime % INT_MAX);

	int border = 100;

	memset(value, 0, sizeof(double) * total);

	for (int t = 0; t < 10; t++) {

		int wi = 30 + (rand() % 50);
		int he = 30 + (rand() % 50);

		int x = border + (rand() % (width - 2 * border));
		int y = border + (rand() % (height - 2 * border));

		for (int j = 0; j < he; j++)
			for (int i = 0; i < wi; i++)
					set(x+i, y + j, (double)rand() / RAND_MAX);

	}


	for (int x = 0; x < total; x++)
		cx_grid[x] = (std::complex<double>) value[x];

	/*fft->transform(cx_grid);*/
}

void LeniaGrid::randomize(float mi, float mj, float Mi, float Mj) {
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


	for (int x = 0; x < total; x++)
		cx_grid[x] = (std::complex<double>) value[x];

	/*fft->transform(cx_grid);*/
}

void LeniaGrid::insert_orb(int x, int y) {
	const double orb[20][20] = ORBIUM;

	for (int t = 0; t < this->size(); t++)
		value[t] = 0;

	for (int j = 0; j < 20; j++)
		for (int i = 0; i < 20; i++)
			set(i + 20, j + 20, orb[i][j]);

	for (int x = 0; x < total; x++)
		cx_grid[x] = (std::complex<double>) value[x];

}

void LeniaGrid::set_mu(double m) {
	mu = m;
}

void LeniaGrid::set_sigma(double s) {
	sigma = s;
}

double LeniaGrid::get_mu() {
	return mu;
}

double LeniaGrid::get_sigma() {
	return sigma;
}

int LeniaGrid::get_R() {
	return kernel_radius;
}

double LeniaGrid::get_epsilon() {
	return epsilon;
}

int LeniaGrid::save(std::fstream &file) {
	
	file << LEN_FILE_SIGNATURE;
	file << (const char)0;

	WRITE(file, logheight);
	WRITE(file, logwidth);

	WRITE(file, kernel_radius);
	WRITE(file, mu);
	WRITE(file, sigma);
	WRITE(file, epsilon);

	file.write(reinterpret_cast<char *>(value), total * sizeof(double));

	if (!file)
		return -1;
	else
		return 0;
}

int LeniaGrid::load(std::fstream& file) {
	int new_logwidth, new_logheight, new_kernel_radius;
	double new_mu, new_sigma, new_epsilon;

	bool old_paused;

	old_paused = this->paused;
	this->paused = true;

	char sig[8];

	file.read(sig, 8);

	if (strcmp(sig, LEN_FILE_SIGNATURE) != 0) {
		std::cout << "goodbye";
		std::cout << "hello";
		return -1;
	}

	std::cout << "hello";

	READ(file, new_logheight);
	READ(file, new_logwidth);
	READ(file, new_kernel_radius);
	READ(file, new_mu);
	READ(file, new_sigma);
	READ(file, new_epsilon);

	logheight = new_logheight;
	logwidth = new_logwidth;
	height = 1 << new_logheight;
	width = 1 << new_logwidth;
	total = height * width;
	kernel_radius = new_kernel_radius;
	mu = new_mu;
	sigma = new_sigma;
	epsilon = new_epsilon;

	delete fft;
	delete[] cx_grid;
	delete[] value;

	this->fft = new FFT2D(logheight, logwidth);
	cx_grid = new std::complex<double>[total];
	value = new double[total];


	fill_kernel(kernel_radius);

	file.read(reinterpret_cast<char*>(value), total * sizeof(double));

	//fft = new FFT2D(h, w);

	//cx_grid = new std::complex<double>[total];
	//cx_grid2 = new std::complex<double>[total];

	//K_aux = new std::complex<double>[total];
	//memset(K_aux, 0, sizeof(std::complex<double>) * (total));

	//K = new double[4 * r * r];

	/*this->paused = old_paused;*/


	//HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	//if (stdOut != NULL && stdOut != INVALID_HANDLE_VALUE)
	//{
	//	DWORD written = 0;
	//	WriteConsoleA(stdOut, sig, strlen(sig), &written, NULL);
	//}

	if (!file)
		return -1;
	else
		return 0;
}


void LeniaGrid::toggle_pause() {
	if (this->paused)
		this->paused = false;
	else
		this->paused = true;
}

bool LeniaGrid::is_paused() {
	return this->paused;
}