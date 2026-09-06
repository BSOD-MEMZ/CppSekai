#include "Utilities.h"
#include "Math.h"
#include <ctime>

namespace MikuMikuWorld
{
	std::string Utilities::getCurrentDateTime()
	{
		std::time_t now = std::time(0);
		std::tm localTime = *std::localtime(&now);

		char buf[128];
		strftime(buf, 128, "%Y-%m-%d-%H-%M-%S", &localTime);

		return buf;
	}

	std::string Utilities::getSystemLocale()
	{
		return "en";
	}

	std::string Utilities::getDivisionString(int div)
	{
		return std::to_string(div);
	}

	Random globalRandom{};

	float Random::get(float min, float max)
	{
		float factor = dist(gen);
		return lerp(min, max, factor);
	}

	float Random::get()
	{
		return dist(gen);
	}

	void Random::setSeed(int seed)
	{
		gen.seed(seed);
	}

	uint32_t RandN::xorShift()
	{
		uint32_t t = x ^ (x << 11);
		x = y; y = z; z = w;
		return w = w ^ (w >> 19) ^ t ^ (t >> 8);
	}

	uint32_t RandN::nextUInt32()
	{
		return xorShift();
	}

	float RandN::nextFloat()
	{
		return 1.0f - nextFloatRange(0.0f, 1.0f);
	}

	float RandN::nextFloatRange(float min, float max)
	{
		return (min - max) * ((float)(xorShift() << 9) / 0xFFFFFFFF) + max;
	}

	void RandN::setSeed(uint32_t seed)
	{
		x = (uint32_t)seed;
		y = (uint32_t)(MT19937 * x + 1);
		z = (uint32_t)(MT19937 * y + 1);
		w = (uint32_t)(MT19937 * z + 1);
	}
}
