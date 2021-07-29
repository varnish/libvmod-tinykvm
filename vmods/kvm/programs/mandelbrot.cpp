#include "../tests/kvm_api.h"
#include "lodepng.h"
#include <cmath>
#include <array>

inline constexpr uint32_t bgr24(uint32_t r, uint32_t g, uint32_t b) {
	return r | (g << 8) | (b << 16) | (255 << 24);
}

static constexpr std::array<uint32_t, 16> color_mapping {
	bgr24(66, 30, 15),
	bgr24(25, 7, 26),
	bgr24(9, 1, 47),
	bgr24(4, 4, 73),
	bgr24(0, 7, 100),
	bgr24(12, 44, 138),
	bgr24(24, 82, 177),
	bgr24(57, 125, 209),
	bgr24(134, 181, 229),
	bgr24(211, 236, 248),
	bgr24(241, 233, 191),
	bgr24(248, 201, 95),
	bgr24(255, 170, 0),
	bgr24(204, 128, 0),
	bgr24(153, 87, 0),
	bgr24(106, 52, 3),
};

inline void encode_color(uint32_t& px, int count, int max_count)
{
	px = color_mapping[count & 15];
}

// Function to draw mandelbrot set
template <int DimX, int DimY, int MaxCount>
//__attribute__((optimize("unroll-loops")))
std::array<uint32_t, DimX * DimY>
fractal(double left, double top, double xside, double yside)
{
	std::array<uint32_t, DimX * DimY> bitmap {};

	// setting up the xscale and yscale
	const double xscale = xside / DimX;
	const double yscale = yside / DimY;

	// scanning every point in that rectangular area.
	// Each point represents a Complex number (x + yi).
	// Iterate that complex number
	for (int y = 0; y < DimY / 2; y++)
	for (int x = 0; x < DimX; x++)
	{
		double c_real = x * xscale + left;
		double c_imag = y * yscale + top;
		double z_real = 0;
		double z_imag = 0;
		int count = 0;

		// Calculate whether c(c_real + c_imag) belongs
		// to the Mandelbrot set or not and draw a pixel
		// at coordinates (x, y) accordingly
		// If you reach the Maximum number of iterations
		// and If the distance from the origin is
		// greater than 2 exit the loop
		#pragma GCC unroll 4
		while ((z_real * z_real + z_imag * z_imag < 4)
			&& (count < MaxCount))
		{
			// Calculate Mandelbrot function
			// z = z*z + c where z is a complex number
			double tempx =
				z_real * z_real - z_imag * z_imag + c_real;
			z_imag = 2 * z_real * z_imag + c_imag;
			z_real = tempx;
			count++;
		}

		encode_color(bitmap[x + y * DimX], count, MaxCount);
	}
	for (int y = 0; y < DimY / 2; y++) {
		memcpy(&bitmap[(DimY-1 - y) * DimX], &bitmap[y * DimX], 4 * DimX);
	}
	return bitmap;
}

extern "C"
void my_backend(const char*, int, int)
{
	constexpr int counter = 0;
	constexpr size_t width  = 512;
	constexpr size_t height = 512;

	const double factor = powf(2.0, counter * -0.1);
	const double x1 = -1.5;
	const double x2 =  2.0 * factor;
	const double y1 = -1.0 * factor;
	const double y2 =  2.0 * factor;

	auto bitmap = fractal<width, height, 1200> (x1, y1, x2, y2);
	auto* data = (const uint8_t *)bitmap.data();

	std::vector<uint8_t> png;
	lodepng::encode(png, data, width, height);

	const char* ctype = "image/png";
	backend_response(ctype, strlen(ctype), png.data(), png.size());
}

int main()
{
	printf("-== Mandelbrot program ready ==-\n");
	//my_backend(nullptr, 0, 0);
}
