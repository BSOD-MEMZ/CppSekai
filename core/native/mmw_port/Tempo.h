#pragma once

#include <map>
#include <vector>

namespace MikuMikuWorld
{
	struct HiSpeedChange
	{
		int tick;
		float speed;
	};

	struct TimeSignature
	{
		int measure;
		int numerator;
		int denominator;
	};

	struct Tempo
	{
		int tick;
		float bpm;

		Tempo();
		Tempo(int tick, float bpm);
	};

	float ticksToSec(int ticks, int beatTicks, float bpm);
	int secsToTicks(float secs, int beatTicks, float bpm);
	float accumulateDuration(int tick, int beatTicks, const std::vector<Tempo>& tempos);
	int accumulateTicks(float sec, int beatTicks, const std::vector<Tempo>& tempos);
	double accumulateScaledDuration(int tick, int ticksPerBeat, const std::vector<Tempo>& bpms, const std::vector<HiSpeedChange>& hispeeds);
}
