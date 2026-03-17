#pragma once

#include "chess.hpp"

struct SearchStats {
	int nodes = 0;
	int depthReached = 0;
	int bestScore = 0;
	int timeMs = 0;
	double nps = 0.0;
};

Move computeBestMove(GameState gs, int depth);
Move computeBestMove(GameState gs, int maxDepth, int timeLimitMs);
SearchStats getLastSearchStats();