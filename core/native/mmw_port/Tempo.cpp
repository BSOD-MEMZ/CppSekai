#include "Tempo.h"

#include <algorithm>
#include <cstdint>

namespace MikuMikuWorld
{
	Tempo::Tempo() : tick{ 0 }, bpm{ 160.0f } {}
	Tempo::Tempo(int _tick, float _bpm) : tick{ _tick }, bpm{ _bpm } {}

	float ticksToSec(int ticks, int beatTicks, float bpm)
	{
		return ticks * (60.0f / bpm / static_cast<float>(beatTicks));
	}

	int secsToTicks(float secs, int beatTicks, float bpm)
	{
		return static_cast<int>(secs / (60.0f / bpm / static_cast<float>(beatTicks)));
	}

	float accumulateDuration(int tick, int beatTicks, const std::vector<Tempo>& bpms)
	{
		float total = 0.0f;
		int accTicks = 0;
		int lastBpm = 0;

		for (int i = 0; i < static_cast<int>(bpms.size()) - 1; ++i)
		{
			lastBpm = i;
			const int ticks = bpms[i + 1].tick - bpms[i].tick;
			if (accTicks + ticks >= tick)
				break;

			accTicks += ticks;
			total += ticksToSec(bpms[i + 1].tick - bpms[i].tick, beatTicks, bpms[i].bpm);
			lastBpm = i + 1;
		}

		total += ticksToSec(tick - bpms[lastBpm].tick, beatTicks, bpms[lastBpm].bpm);
		return total;
	}

	double accumulateScaledDuration(int tick, int ticksPerBeat, const std::vector<Tempo>& bpms, const std::vector<HiSpeedChange>& hispeeds)
	{
		int previousBpm = 0;
		int previousSpeed = -1;
		int accTicks = 0;
		double totalDuration = 0.0;

		while (accTicks < tick)
		{
			const int nextBpmTick = previousBpm + 1 < static_cast<int>(bpms.size()) ? bpms[previousBpm + 1].tick : INT32_MAX;
			const int nextSpeedTick = previousSpeed + 1 < static_cast<int>(hispeeds.size()) ? hispeeds[previousSpeed + 1].tick : INT32_MAX;
			const int nextTick = std::min({ nextBpmTick, nextSpeedTick, tick });

			const float currentBpm = bpms.at(previousBpm).bpm;
			const float currentSpeed = previousSpeed >= 0 ? hispeeds[previousSpeed].speed : 1.0f;
			totalDuration += ticksToSec(nextTick - accTicks, ticksPerBeat, currentBpm) * currentSpeed;

			if (nextTick == nextBpmTick)
				previousBpm++;
			if (nextTick == nextSpeedTick)
				previousSpeed++;
			accTicks = nextTick;
		}

		return totalDuration;
	}

	int accumulateTicks(float sec, int beatTicks, const std::vector<Tempo>& bpms)
	{
		int total = 0;
		float accSecs = 0.0f;
		int lastBpm = 0;

		for (int i = 0; i < static_cast<int>(bpms.size()) - 1; ++i)
		{
			lastBpm = i;
			const float seconds = ticksToSec(bpms[i + 1].tick - bpms[i].tick, beatTicks, bpms[i].bpm);
			if (accSecs + seconds >= sec)
				break;

			total += secsToTicks(seconds, beatTicks, bpms[i].bpm);
			accSecs += seconds;
			lastBpm = i + 1;
		}

		total += secsToTicks(sec - accSecs, beatTicks, bpms[lastBpm].bpm);
		return total;
	}
}
