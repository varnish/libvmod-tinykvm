#pragma once
#ifndef UTIL_XORSHIFT_HPP	// Include guard
#define UTIL_XORSHIFT_HPP

#include <climits>
#include <cstdint>

namespace kvm
{
	class XorShift128Plus {
	public:
		using result_type = uint64_t;

		XorShift128Plus(uint64_t seed0, uint64_t seed1)
			: state{seed0, seed1}
		{}

		result_type operator()()
		{
			auto x = state[0];
			auto y = state[1];
			state[0] = y;
			x ^= x << 23;
			state[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
			return state[1] + y;
		}

	private:
		result_type state[2];
	};

	class XorPRNG {
	public:
		using GeneratorType = XorShift128Plus;

		uint32_t rand(uint32_t min, uint32_t max)
		{
			return generator() % (max - min) + min;
		}
		uint32_t randInt(uint32_t min, uint32_t max) // Alias
		{
			return rand(min, max);
		}
		int randRange(int max)
		{
			return generator() % max;
		}
		int32_t randI32()
		{
			return generator() & INT_MAX;
		}
		float randFloat()
		{
			constexpr int FMAX = (1 << 24);
			return float(randI32() & (FMAX-1)) / FMAX;
		}
		float randNorm(float scale = 1.0f)
		{
			return (randFloat() - 0.5f) * scale * 2.0f;
		}
		uint64_t randU64()
		{
			return generator();
		}

		bool randBool()
		{
			if (counter == 0) {
				counter = sizeof(GeneratorType::result_type) * CHAR_BIT;
				random_integer = generator();
			}
			return (random_integer >> --counter) & 1;
		}

		XorPRNG(GeneratorType::result_type seed0, GeneratorType::result_type seed1)
			: generator(seed0, seed1) {}
		XorPRNG(std::pair<GeneratorType::result_type, GeneratorType::result_type> seeds)
			: generator(seeds.first, seeds.second) {}
	private:

		GeneratorType generator;
		GeneratorType::result_type random_integer;
		int counter = 0;
	};
}

#endif // UTIL_XORSHIFT_HPP
