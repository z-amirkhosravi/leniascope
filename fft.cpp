#include "fft.h"
#include <complex>

#define M_PI 3.14159265358979323846
#define M_I std::complex<double>(0,1)

FFT::FFT(int n): size(n){
	arrsize = 1 << n;

	bit_reverse_dict = new int[arrsize];

	roots = new std::complex<double>[arrsize];
	invroots = new std::complex<double>[arrsize];
	buff = new std::complex<double>[arrsize];

	fill_bit_reverse_dict();
	fill_roots();
}

FFT::~FFT() {
delete[] roots;
delete[] bit_reverse_dict;
delete[] buff;
}

void FFT::fill_roots() {/*
	const std::complex<double> M_I(0.0, 1.0);*/

	std::complex<double> w = exp( ((double) 2 / arrsize) * M_PI * M_I );

	roots[0] = 1;
	invroots[0] = 1;
	for (int i = 1; i < arrsize / 2; i++) {
		roots[i] = roots[i - 1] * w;
		invroots[i] = invroots[i - 1] / w;
	}
}

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
2D transforms by applying 1D transforms to "rows" and "columns" in serialized data */

void FFT::bit_reverse(std::complex<double>* data, int step = 1) {		// assumes data is pointer to arrsize many complex numbers
	std::complex<double> temp;
	int temp_int;

	for (int n = 1; n < arrsize; n++) {
		if (bit_reverse_dict[n] == n)
			bit_reverse_dict[n] = -n;
		else if (bit_reverse_dict[n] > 0) {
			temp = data[n * step];
			data[n * step] = data[bit_reverse_dict[n] * step];
			data[bit_reverse_dict[n] * step] = temp;

			temp_int = bit_reverse_dict[n];			
			bit_reverse_dict[n] = -temp_int;	// change sign to indicate already swapped data at tshose indices
			bit_reverse_dict[temp_int] = -bit_reverse_dict[temp_int];
		}
	}

	for (int n = 1; n < arrsize; n++)
		bit_reverse_dict[n] = -bit_reverse_dict[n];				// change signs back
}

/* This method computes the DFT of the input data vector by repeatedly applying matrices B_1, B_2, ..., B_(n-1), 
  where B_n = diag(A_n, A_n, ..., A_n),

			|  I_n   I_n  |
 where A_n = |			  |,
			|  S_n  -S_n  |

 I_n is the identity matrix, and S_n = diag(1, z_n, z_n^2, ..., z_n^(n-1)), z_n = exp(2 * pi * i / 2n).

*/



void FFT::transform(std::complex<double>* data, int step = 1) {
	int block_size = 1;											// will go up to 2 ** (size - 1) in powers of two
	int cur_block_idx;
	int co_block_size = arrsize / 2;							// will be 2 ** (size - block_size - 1) in the loop
	int root_idx = 0;											// index tracking roots of unity

	std::complex<double> temp, temp2;

	bit_reverse(data, step);

	for (int k = 0; k < size; k++) {
		for (int x = 0, cur_block_idx = 0; x < co_block_size; x++, cur_block_idx += 2 * block_size) {
			for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += co_block_size) {
				int idx = step * (cur_block_idx + y);
				int idx2 = step * (cur_block_idx + block_size + y);

				temp2 = data[idx2] * roots[root_idx];		// avoid calculating this twice
				temp = data[idx] - temp2;
				data[idx] = data[idx] + temp2;
				data[idx2] = temp;
			}
		}
		co_block_size = co_block_size /2;
		block_size = block_size * 2;
	}

	return;

	// this code is for a different normalization of the transform
	// 
	//for (int x = 0; x < arrsize * step; x += step)                
	//	data[x] = data[x] * ((double)(1 / sqrt(arrsize)));
}

//void FFT::inverse_transform(std::complex<double>* data, int step = 1) {
//
//	int block_size = 1;											// will go up to 2 ** (size - 1) in powers of two
//	int cur_block_idx;									
//	int co_block_size;											// will be 2 ** (size - block_size - 1) in the loop
//	int root_idx = 0;											// index tracking roots of unity
//
//	std::complex<double> temp, temp2;
//
//	co_block_size = arrsize / 2;
//
//	bit_reverse(data, step);
//
//	for (int k = 0; k < size; k++) {
//		for (int x = 0, cur_block_idx = 0; x < co_block_size; x++, cur_block_idx += block_size*2) {
//			for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += co_block_size) {
//
//				int idx2 = step * (cur_block_idx + y + block_size);
//				int idx = step * (cur_block_idx + y);
//
//				temp2 = data[idx2] * invroots[root_idx];		// avoid calculating this twice
//				temp = data[idx] - temp2;
//
//				data[idx] = data[idx] + temp2;
//				data[idx2] = temp;
//			}
//		}
//		co_block_size = co_block_size * 2;
//		block_size = block_size / 2;
//	}
//
//	for (int x = 0; x < arrsize * step; x += step)
//		data[x] = ((double)1 / arrsize) * data[x];
//}

void FFT::inverse_transform(std::complex<double>* data, int step = 1) {	

	int co_block_size = arrsize / 2;
	int block_size = 1;											
	int root_idx = 0;			

	std::complex<double> temp, temp2;

	bit_reverse(data, step);
				
	for (int k = 0; k < size; k++) {
		for (int cur_block_idx = 0; cur_block_idx < arrsize;  cur_block_idx += 2 * block_size) {
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

	// this code is for a different normalization of the transform
	// 
	//for (int x = 0; x < arrsize * step; x += step)                
	//	data[x] = data[x] * ((double)(1 / sqrt(arrsize)));
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
			buff[x] = data[bit_reverse_dict[x]] + data[bit_reverse_dict[x + (arrsize / 2)]] * M_I;	// fold the real input data into a complex vector


	co_block_size = arrsize / 4;													// run the calculation for the size - 1 case
	for (int k = 0; k < size - 1; k++) {																		    
		for (int cur_block_idx = 0; cur_block_idx < arrsize / 2; cur_block_idx += block_size * 2) {
			for (int y = 0, root_idx = 0; y < block_size; y++, root_idx += 2*co_block_size) {
				int idx = step * (cur_block_idx + y);
				int idx2 = step * (cur_block_idx + block_size + y);

				temp2 = buff[idx2] * roots[root_idx];		// most expensive step, avoid calculating twice
				temp = buff[idx] - temp2;
				buff[idx] = buff[idx] + temp2;
				buff[idx2] = temp;
			}
		}
		co_block_size = co_block_size / 2;
		block_size = block_size * 2;
	}

	output[0] = std::complex<double>(0.5, -0.5) * buff[0] + std::complex<double>(0.5, +0.5) * std::conj(buff[0]);
	output[arrsize / 2] = std::complex<double>(0.5, +0.5) * buff[0] + std::complex<double>(0.5, -0.5) * std::conj(buff[0]);

	for (int x = 1; x < arrsize / 2; x++) {		
		//output[x] = (std::complex<double>(0.5, 0) + std::complex<double>(0, -0.5) * roots[x]) * buff[x]
		//	+ (std::complex<double>(0.5, 0) + std::complex<double>(0, 0.5) * roots[x]) * std::conj(buff[arrsize / 2 - x]);
		output[x] = (std::complex<double>(0.5, 0) + std::complex<double>(0, -0.5) * roots[x]) * buff[x]
			+ (std::complex<double>(0.5, 0) + std::complex<double>(0, +0.5) * roots[x]) * std::conj(buff[arrsize / 2 - x]);

		output[arrsize - x] = std::conj(output[x]);				// fill second half of output by symmetry
	}

	// this code is for a different normalization of the transform
	// 
	//for (int x = 0; x < arrsize * step; x += step)                
	//	data[x] = data[x] * ((double)(1 / sqrt(arrsize)));
}



FFT2D::FFT2D(int s1, int s2) : size1(s1), size2(s2) {
	arrsize1 = (int)pow(2, size1);
	arrsize2 = (int)pow(2, size2);

	f1 = new FFT(size1);
	f2 = new FFT(size2);
}

FFT2D::~FFT2D() {
	delete f1, f2;
}

void FFT2D::transform(double *data, std::complex<double>*output) {  // 2d transform when the input data is real

	for (int x = 0; x < arrsize1 * arrsize2; x += arrsize1)
		f1->transform(data + x, output + x);

	for (int y = 0; y < arrsize1; y++)
		f2->transform(output + y, arrsize1);
}

void FFT2D::transform(std::complex<double>* data) { // this assumes data points to an array of size1*size2 complex doubles

	for (int x = 0; x < arrsize1 * arrsize2; x += arrsize1)
		f1->transform(data + x);

	for (int y = 0; y < arrsize1; y++)
		f2->transform(data + y, arrsize1);
}

void FFT2D::inverse_transform(std::complex<double>* data) { // this assumes data points to an array of size1*size2 complex doubles
	for (int y = 0; y < arrsize1; y++)
		f2->inverse_transform(data + y, arrsize1);

	for (int x = 0; x < arrsize1 * arrsize2; x += arrsize1)
		f1->inverse_transform(data + x);

}
