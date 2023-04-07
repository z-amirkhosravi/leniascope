#ifndef FFT_H
#define FFT_H

#include <complex>

class FFT {
private:
	int size;
	int arrsize;		// will be set to pow(2,size);
	std::complex<double>* roots;
	std::complex<double>* invroots;
	int* bit_reverse_dict;
	std::complex<double>* buff;

	void bit_reverse(std::complex<double>* data, int step);

	void fill_bit_reverse_dict();
	void fill_roots();

public:
	FFT(int);
	~FFT();
	void transform(std::complex<double>* data, int step);
	void transform(double* data, std::complex<double>* output, int step);		// when input data is real
	void inverse_transform(std::complex<double>* data, int step);
}; 


class FFT2D {
private:
	int size1, size2;
	int arrsize1, arrsize2;
	FFT* f1, * f2;

public:
	FFT2D(int s1, int s2);
	~FFT2D();

	void transform(double* data, std::complex<double>* output);
	void transform(std::complex<double>* data);
	void inverse_transform(std::complex<double>* data);
};

#endif