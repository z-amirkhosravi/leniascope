#ifndef FFT_H
#define FFT_H

#include <complex>

namespace fft {

	class FFT {

	public:
		FFT(int);			
		FFT() = delete;		// disallow initializing without parameter
		~FFT();

		void transform(std::complex<double>* data, int step);
		void htransform(std::complex<double>* data, int step);

		void transform(double* data, std::complex<double>* output, int step);		// when input data is real
		void inverse_transform(std::complex<double>* data, int step);

	private:		

		int size;
		int arrsize;		// will be set to pow(2,size);


		std::complex<double>* roots;
		std::complex<double>* invroots;
		std::complex<double>* buff;

		double* cc, * ss;
		int* bit_reverse_dict;

		void bit_reverse(std::complex<double>* data, int step);
		void fill_bit_reverse_dict();
		void fill_roots();
	};


	class FFT2D {

	public:
		FFT2D(int s1, int s2);
		~FFT2D();

		void transform(double* data, std::complex<double>* output);
		void transform(std::complex<double>* data);
		void inverse_transform(std::complex<double>* data);

	private:
		FFT2D();		

		int size1, size2;
		int arrsize1, arrsize2;
		FFT* f1, * f2;
	};

}
#endif