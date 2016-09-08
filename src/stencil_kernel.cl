__kernel void stencil5p_2D(__global float* in, __global float* out, int N) {
	float res;
	int x = get_global_id(0);
	int y = get_global_id(1);
	int id;
	if (x > 0 && y > 0 && x < N - 1 && y < N - 1) {
		id = x + y * N;

		// the order of execution affects the result, as the operator
		// precedence is not clearly defined in opencl
		// res = in[id] + 0.24f * (-4.f * in[id] + in[id + 1] + in[id - 1] + in[id + N] + in[id - N]);
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
