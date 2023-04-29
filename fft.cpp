#include "fft.h"
#include <complex>
#include <iostream>

namespace fft {

	const double pi = 3.14159265358979323846;

	// these constants are there to make the formulas in the code more readable

	const std::complex<double> complex_i = std::complex<double>(0, 1);
	const std::complex<double> one_half = std::complex<double>(0.5, 0);
	const std::complex<double> half_i = std::complex<double>(0, 0.5);

	// FFT::FFT(int n)

	// initializes the (one-dimensional) discrete fourier transformer for input data of size 2^n

	FFT::FFT(int n) : size(n) {

		arrsize = 1 << n;

		try {
			bit_reverse_dict = new int[arrsize];

			roots = new std::complex<double>[arrsize];
			invroots = new std::complex<double>[arrsize];
			buff = new std::complex<double>[arrsize];
		} catch (std::bad_alloc) {
			std::cerr << "Unable to allocate memory for FFT.\n";
			std::abort();
		}

		fill_bit_reverse_dict();
		fill_roots();
	}

	FFT::~FFT() {
		delete[] roots;
		delete[] invroots;
		delete[] bit_reverse_dict;
		delete[] buff;
	}

	// fill_roots() 
	//
	// fills the array "roots" (and "invroots") with arrsize_th roots of unity (and their inverses) 
	// needed to perform discrete fourier transforms

	void FFT::fill_roots() {

		std::complex<double> w = exp(((double)2 / arrsize) * pi * complex_i);		// a primitive arrsize_th root of unity

		roots[0] = 1;
		invroots[0] = 1;
		for (int i = 1; i < arrsize / 2; i++) {
			roots[i] = roots[i - 1] * w;
			invroots[i] = invroots[i - 1] / w;
		}
	}

	// fill_bit_reverse_dict() 
	//
	// fills the array "bit_reverse_dict", entering the appropriate bit reversal of n at the nth index

	void FFT::fill_bit_reverse_dict() {

		for (int n = 0; n < arrsize; n++) {

			int r = 0, temp = n;
			for (int i = 0; i < size; i++) {
				r *= 2;
				r += (temp % 2);
				temp /= 2;
			}
			bit_reverse_dict[n] = r;
		}
	}

	/* the bit_reverse and transform methods can traverse the data by a given step size, to facilitate computing
	2D transforms by applying 1D transforms to "rows" and "columns" in data that's been serialized into 1D array */

	void FFT::bit_reverse(std::complex<double>* data, int step = 1) {		// assumes data is pointer to arrsize-many complex numbers
		std::complex<double> temp;
		int temp_int;

		// this algorithm changes the sign of the indices in the bit_reversed_dict array to indicate a swap has already happened
		// so we can avoid swapping twice

		for (int n = 1; n < arrsize-1; n++) {		
			if (bit_reverse_dict[n] == n)			
				bit_reverse_dict[n] = -n;
			else if (bit_reverse_dict[n] > 0) {
				std::swap(data[n * step], data[bit_reverse_dict[n] * step]);

				temp_int = bit_reverse_dict[n];
				bit_reverse_dict[n] = -temp_int;	// change sign to indicate already swapped data at tshose indices
				bit_reverse_dict[temp_int] = -bit_reverse_dict[temp_int];
			}
		}

		for (int n = 1; n < arrsize-1; n++)
			bit_reverse_dict[n] = -bit_reverse_dict[n];				// change all signs back
	}

	// transform(std::complex<double> * data, int step = 1)

	// computes in place the discrete fourier transform of "data"
	// 
	// if step is set to n, it does the same to the array consisting of {data[n], data[2*n], data[3*n], ... data[arrsize*n]}

	/* the method first permutes the input data vector by applying bit reversal 
	   then considering the input data as a row vector, it multiplies it on the right 
	   by matrices B_1, B_2, ..., B_(n - 1), in that order,
	   where B_n = diag(A_n, A_n, ..., A_n),

				 |  I_n   I_n  |
	 where A_n = |			   |,
				 |  S_n  -S_n  |

	 I_n  = n x n identity matrix, 
	 
	 S_n = diag(1, w, w^2, ..., w^(n-1)),			 w = exp(2 * pi * i / 2n).

	*/

	void FFT::transform(std::complex<double>* data, int step = 1) {
		int block_size = 1;											// will go up to 2 ** (size - 1) in powers of two
		int cur_block_idx;
		int co_block_size = arrsize / 2;							// will be 2 ** (size - block_size - 1) in the loop
		int root_idx = 0;											// index tracking roots of unity

		std::complex<double> temp, temp2;

		bit_reverse(data, step);

		for (int k = 0; k < size; k++) {
			for (cur_block_idx = 0; cur_block_idx < arrsize; cur_block_idx += 2 * block_size) {
				for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += co_block_size) {
					int idx = step * (cur_block_idx + y);
					int idx2 = step * (cur_block_idx + block_size + y);

					temp2 = data[idx2] * roots[root_idx];		// avoid calculating this twice
					temp = data[idx] - temp2;
					data[idx] = data[idx] + temp2;
					data[idx2] = temp;
				}
			}
			co_block_size /= 2;
			block_size *= 2;
		}

		return;

	}

	// Hartley transform
	// 

	void FFT::htransform(std::complex<double>* data, int step = 1) {
		int block_size = 1;											// will go up to 2 ** (size - 1) in powers of two
		int cur_block_idx;
		int co_block_size = arrsize / 2;							// will be 2 ** (size - block_size - 1) in the loop
		int root_idx = 0;											// index tracking roots of unity

		std::complex<double> temp, temp2, temp3;

		bit_reverse(data, step);

		for (int k = 0; k < size; k++) {
			for (cur_block_idx = 0; cur_block_idx < arrsize; cur_block_idx += 2 * block_size) {
				for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += co_block_size) {
					int idx = step * (cur_block_idx + y);
					int idx2 = step * (cur_block_idx + block_size + y);
					int idx3 = step * (cur_block_idx + 2 * block_size - y);

					temp2 = data[idx2] * roots[root_idx].real();		// avoid calculating this twice
					temp3 = data[idx3] * roots[root_idx].imag();
					temp = data[idx] + temp2 - temp3;
					data[idx] = data[idx] + temp2 + temp3;
					data[idx2] = temp;
				}
			}
			co_block_size /= 2;
			block_size *= 2;
		}

		return;

	}

	// inverse_transform(std::complex<double>* data, int step = 1)

	// performs the inverse operation of transform()
	// the algorithm is the same, except the roots of unity are replaced by their inverses,
	// and there is a normalization step at end

	void FFT::inverse_transform(std::complex<double>* data, int step = 1) {

		int co_block_size = arrsize / 2;
		int block_size = 1;
		int root_idx = 0;

		std::complex<double> temp, temp2;

		bit_reverse(data, step);

		for (int k = 0; k < size; k++) {
			for (int cur_block_idx = 0; cur_block_idx < arrsize; cur_block_idx += 2 * block_size) {
				for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += co_block_size) {
					temp2 = data[step * (cur_block_idx + y + block_size)] * invroots[root_idx];		// avoid calculating this twice
					temp = data[step * (cur_block_idx + y)] - temp2;
					data[step * (cur_block_idx + y)] = data[step * (cur_block_idx + y)] + temp2;
					data[step * (cur_block_idx + block_size + y)] = temp;
				}
			}
			co_block_size = co_block_size / 2;
			block_size = block_size * 2;
		}

		for (int x = 0; x < arrsize * step; x += step)
			data[x] = ((double)1 / arrsize) * data[x];

		return;
	}

	/* If the data is real, the computations can be halved by exploiting symmetry */

	void FFT::transform(double* data, std::complex<double>* output, int step = 1) {
		int block_size = 1;											// will go up to 2 ** (size - 1) in powers of two
		int cur_block_idx;
		int co_block_size = arrsize / 2;
		int root_idx = 0;

		std::complex<double> temp, temp2;

		/*assert(size > 2);	*/										// this algorithm doesn't make sense over Z/2Z


		for (int x = 0; x < arrsize / 2; x++)
			// fold the real input data into a complex vector:
			buff[x] = data[bit_reverse_dict[x]] + data[bit_reverse_dict[x + (arrsize / 2)]] * complex_i;	

		co_block_size = arrsize / 4;													// run the calculation for the size - 1 case
		for (int k = 0; k < size - 1; k++) {
			for (int cur_block_idx = 0; cur_block_idx < arrsize / 2; cur_block_idx += block_size * 2) {
				for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += 2 * co_block_size) {
					int idx = step * (cur_block_idx + y);
					int idx2 = step * (cur_block_idx + block_size + y);

					temp2 = buff[idx2] * roots[root_idx];								// most expensive step, avoid calculating twice
					temp = buff[idx] - temp2;
					buff[idx] = buff[idx] + temp2;
					buff[idx2] = temp;
				}
			}
			co_block_size /= 2;
			block_size *= 2;
		}

		output[0] = (one_half - half_i) * buff[0] + (one_half + half_i) * std::conj(buff[0]);
		output[arrsize / 2] = (one_half + half_i) * buff[0] + (one_half - half_i) * std::conj(buff[0]);

		for (int x = 1; x < arrsize / 2; x++) {
			output[x] = (one_half - half_i * roots[x]) * buff[x]
				+ (one_half + half_i * roots[x]) * std::conj(buff[arrsize / 2 - x]);

			output[arrsize - x] = std::conj(output[x]);				// fill second half of output by symmetry
		}
	}


	// FFT2D:FF2D(int s1, int s2)

	// initializes a 2D fourier transformer for input data of size 2^s1 x 2^s2

	FFT2D::FFT2D(int s1, int s2) : size1(s1), size2(s2) {
		arrsize1 = 1 << size1;
		arrsize2 = 1 << size2;

		f1 = new FFT(size1);
		f2 = new FFT(size2);
	}

	FFT2D::~FFT2D() {
		delete f1, f2;
	}

	// FFT2D::transform(double* data, std::complex<double>* output)

	// performs the 2D discrete fourier transform of _real_ input data

	void FFT2D::transform(double* data, std::complex<double>* output) { 

		for (int x = 0; x < arrsize1 * arrsize2; x += arrsize1)		
			f1->transform(data + x, output + x);					// can use slightly faster transform in the first step because data is real

		for (int y = 0; y < arrsize1; y++)
			f2->transform(output + y, arrsize1);
	}

	void FFT2D::transform(std::complex<double>* data) {				// assumes data points to an array of arrsize1 * arrsize2 complex doubles

		for (int x = 0; x < arrsize1 * arrsize2; x += arrsize1)
			f1->transform(data + x);

		for (int y = 0; y < arrsize1; y++)
			f2->transform(data + y, arrsize1);
	}

	void FFT2D::inverse_transform(std::complex<double>* data) {		// assumes data points to an array of arrsize1*arrsize2 complex doubles
		for (int y = 0; y < arrsize1; y++)
			f2->inverse_transform(data + y, arrsize1);

		for (int x = 0; x < arrsize1 * arrsize2; x += arrsize1)
			f1->inverse_transform(data + x);

	}

}
