#include <iostream>
#include <cuda.h>
#include <chrono>
#include <string>
#include <vector>
#include <math.h>
#include <stdexcept>
#include <iomanip>
#include <fstream>
#include <new> // std::bad_alloc //
#include <algorithm> // std::swap //

using namespace std;
using Clock = chrono::high_resolution_clock;
using Duration = chrono::duration<double>;

#define TILESIZE 32

#define checkCudaErrors(err)  __checkCudaErrors (err, __FILE__, __LINE__)

inline void __checkCudaErrors( CUresult err, const char *file, const int line ) {
	const char* msg;
	cuGetErrorName(err, &msg);
	if( CUDA_SUCCESS != err) {
		cout << "***CUDA Driver API error = " << err << "(" << msg << ")";
		cout << " from file <" << file << ">, line " << line << "." << endl;
		exit(-1);
	}
}

template<class IntType>
void commandLineGetInt(IntType* p, const std::string& key, int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == key) {
			*p = std::stoll(argv[i+1]);
			break;
		}
	}
}

void commandLineGetString(string* s, const std::string& key, int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == key) {
			s->assign(argv[i + 1]);
			break;
		}
	}
}

void commandLineGetBool(bool* p, const std::string& key, int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == key) {
			*p = true;
			break;
		}
	}
}

bool commandLineGetBool(const std::string& key, int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == key) {
			return true;
			break;
		}
	}
	return false;
}

void stencil_cpu(float* in, float* out, int N) {
	for (int i = 0; i < N*N; ++i) {
		int x = i % N;
		int y = i / N;
		int id;
		float res;
		if (x > 0 && y > 0 && x < N - 1 && y < N - 1) {
			id = x + y * N;
			// we specify the order of execution here, because
			// we want to ensure the same order of execution as it is
			// written in the opencl kernel. This is mandatory as opencl
			// does not define the *operator precedence* clearly.
			res = in[id - N];
			res += in[id + N];
			res += in[id - 1];
			res += in[id + 1];
			res += -4.0f * in[id];
			res *= 0.24f;
			res += in[id];

			res = res > 127 ? 127 : res;
			res = res < 0 ? 0 : res;
			out[id] = res;
		}
	}
}

// gives back the relative number of different elements,
// thus the result will be between 0 and 1.
double relNumDiffEl(float* a, float* b, int N) {
	double res = 0;
	for (int i = 0; i < N*N; ++i) {
		if (fabs(a[i] - b[i]) > 1e-6) {
			res += 1. / (float) (N * N);
		}
	}
	return res;
}

void writeGrid(float* a, int N, string fname) {
	if (fname.empty()) { return; }
	fstream fs;
	fs.open(fname.c_str(), fstream::trunc | fstream::out);
	fs << a[0];
	for (unsigned int i = 1; i < N*N; ++i) {
		if (i % N == 0) {fs << endl;}
		else {fs << ' ';}
		fs << a[i];
	}
	fs.close();
}

void printGrid(float* a, int N) {
	for (unsigned i = 0; i < N * N; ++i) {
		if (i % N == 0) { cout << endl; }
		cout << setw(6) << setprecision(1) << fixed << a[i];
	}
}

void printUsage(const char* fileName) {
	cout << "Usage:" << endl
	     << fileName << " [OPTION]"<< endl
	     << "Options:" << endl
	     << "  -N <size>" << endl
	     << "     denotes the number of grid points along one side of the" << endl
	     << "     squared grid, thus you have size*size points in total." << endl
	     << "  -T <steps>" << endl
	     << "     time steps to calculate the stencil" << endl
	     << "  -check" << endl
	     << "     progam verifies the results with the cpu calculations." << endl
	     << "     If an error occurs, the progam will abort and report" << endl
	     << "     the error rate." << endl
	     << "  -h" << endl
	     << "     show this help message." << endl
	     << "  -v" << endl
	     << "     print grid after calculation for gpu (and cpu)" << endl
		 << "  -b" << endl
		 << "     initialize also a heat source at the bottom of the stencil" << endl
	     << "  -fname <output file>" << endl
	     << "     e.g. ./results/out.txt the programm will create" << endl
	     << "     a file representing the grid at the last timestep." << endl << endl
	     << "The program expects a stencil-kernel.ptx with a kernel" << endl
	     << "named stencil5p_2D in the same directory." << endl;
}

int main(int argc, char* argv[]) {

	// Default Values //
	int N = 1024;
	int T = 10;
	bool checkResult = false;
	bool verbose = false;
	bool bottomSource = false;
	string fname;

	if (commandLineGetBool("-h", argc, argv) || commandLineGetBool("--help", argc, argv)) {
		printUsage(argv[0]);
		return EXIT_SUCCESS;
	}

	checkResult = commandLineGetBool("-check", argc, argv);
	verbose = commandLineGetBool("-v", argc, argv);
	bottomSource = commandLineGetBool("-b", argc, argv);
	commandLineGetInt(&N, "-N", argc, argv);
	commandLineGetInt(&T, "-T", argc, argv);
	commandLineGetString(&fname, "-fname", argc, argv);

	size_t size = sizeof(float) * N * N;

	cout << "***" <<endl
	     << "*** Starting Stencil Computation..." << endl
	     << "***" << endl << endl
	     << "** Input Arguments:" << endl
	     << "*  N    = " << setw(28) << left << N << "(N*N points in total)" << endl
	     << "*  T    = " << setw(28) << T << "(number of iterations)" << endl
	     << "*  size = " << setw(28) << size * 1e-6 << "(MBytes for one stencil array)" << endl
	     << "*  checkResult = " << boolalpha << setw(18) << checkResult << " (verify with cpu calculation)" << endl
		 << "*  bottomSource = " << bottomSource << endl
	     << "*  fname = " << setw(24) << (fname.empty() ? "<no output specified>" : fname) << " (output file)" << endl;
	
	// ALLOCATE HOST MEMORY //
	float* gridA_h;
	float* gridB_h;
	float* gpu_result;
	try {
		gridA_h = new float[N * N];
		gridB_h = new float[N * N];
		gpu_result = new float[N * N];
	} catch (bad_alloc& ba) {
		cout << "*** ERROR: can't allocate grids in host memory" << endl << "*** MSG: " << ba.what() << endl;
		return EXIT_FAILURE;
	}

	// SET INPUT DATA //
	for (unsigned int i = 0; i < N * N; ++i) {
		gridA_h[i] = 0;
		gridB_h[i] = 0;
		gpu_result[i] = 0;
		if (i >= 0.25 * N && i <= 0.75 * N) {
			gridA_h[i] = 127;
			gridB_h[i] = 127;
			gpu_result[i] = 127;
		}
	}
	if (bottomSource) {
		for (unsigned int i = 0; i < N * N; ++i) {
			if (i >= 0.25 * N && i <= 0.75 * N) {
				gridA_h[N * N - i] = 127;
				gridB_h[N * N - i] = 127;
				gpu_result[N * N - i] = 127;
			}
		}
	}

	// CUDA INIT, CONTEXT, MODULE, FUNCTION //
	CUdevice device;
	CUcontext context;
	CUmodule module;
	CUfunction function;
	CUresult err;

	cout << endl << "** Initializing CUDA + platform..." << endl;
	checkCudaErrors(cuInit(0));

	cout << "** Get CUDA Device..." << endl;
	checkCudaErrors(cuDeviceGet(&device, 0));

	char name[256];
	cuDeviceGetName(name, 256, device);
	cout << "*  Device name: " << name << endl;

	cout << "** Get Device Compute Capability..." << endl;
	int major = 0, minor = 0;
	checkCudaErrors(cuDeviceComputeCapability(&major, &minor, device));
	cout << "*  Architecture: sm_" << major << minor << endl;

	const char* ptx_file = "stencil-kernel.ptx";
	const char* kernel_name = "stencil5p_2D";

	cout << "** Initializing CUDA Context" << endl;
	if (cuCtxCreate(&context, 0, device) != CUDA_SUCCESS) {
		cout << "***ERROR: while initializing CUDA context" << endl;
		cuCtxDetach(context);
		return EXIT_FAILURE;
	}

	cout << "** Loading module " << ptx_file << endl;
	auto err0 = cuModuleLoad(&module, ptx_file);
	if (err0 != CUDA_SUCCESS) {
		cout << "***ERROR: while loading module from " << ptx_file << endl;
		const char* p;
		cuGetErrorName(err0, &p);
		cout << p << endl;
		
		cuCtxDetach(context);
		return EXIT_FAILURE;
	}

	cout << "** Acquiring kernel function " << kernel_name << endl;
	if (cuModuleGetFunction(&function, module, kernel_name) != CUDA_SUCCESS) {
		cout << "***ERROR: while loading function " << kernel_name << " from file " << ptx_file << endl;
		cuCtxDetach(context);
		return EXIT_FAILURE;
	} 

	// ALLOCATE DEVICE MEMORY //
	cout << "** Allocating device memory" << endl;
	CUdeviceptr gridA_d;
	CUdeviceptr gridB_d;
	checkCudaErrors(cuMemAlloc(&gridA_d, size));
	checkCudaErrors(cuMemAlloc(&gridB_d, size));

	// COPY HOST TO DEVICE //
	cout << "** Copy from host to device" << endl;
	auto htod_begin = Clock::now();
	checkCudaErrors(cuMemcpyHtoD(gridA_d, gridA_h, size));
	checkCudaErrors(cuMemcpyHtoD(gridB_d, gridB_h, size));
	Duration t_htod = Clock::now() - htod_begin;

	// VERIFY RESULTS WITH CPU STENCIL //
	if (checkResult) {
		cout << "** Execute CPU Stencil Calculation" << endl;
		for (unsigned int i = 0; i < T; ++i) {
			stencil_cpu(gridA_h, gridB_h, N);
			swap(gridA_h, gridB_h);
		}
	}

	// PREPARE KERNEL ARGUMENTS //
	int threads = TILESIZE;
	int blocks = N / TILESIZE;

	void* args[] = { &gridA_d, &gridB_d, &N };

	if ( N % TILESIZE != 0) {
		cout << "***ERROR: case N % TILESIZE != 0 is not supported" << endl
		     << "***            now TILESIZE  = " << TILESIZE << endl;
		cuCtxDetach(context);
		return EXIT_FAILURE;
	}

	// KERNEL LAUNCH //
	cout << "*** Launching Kernel with grid {" << blocks << ", " << blocks
	     << ", 1}, block {" << threads << ", " << threads << ", 1}" << endl;

	auto kernel_begin = Clock::now();
	for (unsigned i = 0; i < T; ++i) {
		// second kernel arg will be written
		checkCudaErrors(cuLaunchKernel(function,
									   blocks, blocks, 1, // Grid Size //
									   threads, threads, 1, // Block Size //
									   0, 0, args, 0));
		checkCudaErrors(cuCtxSynchronize());
		swap(gridB_d, gridA_d);
	}
	Duration t_kernel = Clock::now() - kernel_begin;

	// COPY DEVICE TO HOST //
	cout << "** Copy from device to host" << endl;
	auto dtoh_begin = Clock::now();
	checkCudaErrors(cuMemcpyDtoH(gpu_result, gridA_d, size));
	Duration t_dtoh = Clock::now() - dtoh_begin;

	// CHECK IF RESULTS ARE EQUAL //
	if (checkResult) {
		cout << "** Verifying GPU results with CPU results: error = "
			 << relNumDiffEl(gridA_h, gpu_result, N) * 100 << " %" << endl;
	}

	// IF VERBOSE PRINT GRIDS //
	if (verbose) {
		if (checkResult) {
			cout << "** CPU GRID:" << endl;
			printGrid(gridA_h, N);
		}
		cout << endl << "** GPU GRID:" << endl;
		printGrid(gpu_result, N);
		cout << endl;
	}

	// WRITE RESULT //
	writeGrid(gridA_h, N, fname); // write the result created by the gpu //

	// FREE MEMORY //
	delete[] gridA_h;
	delete[] gridB_h;
	delete[] gpu_result;

	checkCudaErrors(cuMemFree(gridA_d));
	checkCudaErrors(cuMemFree(gridB_d));
	checkCudaErrors(cuCtxDestroy(context));

	// REPORT //
	cout << "*** Report:" << endl
	     << "**  host to device copy time = " << t_htod.count() << " s (" << 2*size << " Bytes)" << endl
	     << "**  device to host copy time = " << t_dtoh.count() << " s (" <<   size << " Bytes)" << endl
	     << "**  kernel time = " << t_kernel.count() << " s" << endl;

	return EXIT_SUCCESS;
}
