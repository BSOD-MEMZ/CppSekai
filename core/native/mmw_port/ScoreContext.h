#pragma once

#include "Constants.h"
#include "Score.h"
#include "Tempo.h"

namespace MikuMikuWorld
{
	class ScoreContext
	{
	public:
		Score score;
		int currentTick{};

		double getTimeAtCurrentTick() const
		{
			return accumulateDuration(currentTick, TICKS_PER_BEAT, score.tempoChanges);
		}
	};
}
