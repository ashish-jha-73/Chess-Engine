#pragma once

#include "chess.hpp"
#include <string>

struct SearchStats {
	int nodes = 0;
	int depthReached = 0;
	int bestScore = 0;
	int timeMs = 0;
	double nps = 0.0;
};

struct EngineTuningParams {
	int futilityBaseMargin = 200;
	int futilityDepthMargin = 100;
	int lmpBaseMargin = 180;
	int lmpDepthMargin = 120;
	int qsearchDeltaMargin = 260;
	int qsearchSeeThreshold = 140;
	int singularBaseMargin = 50;
	int singularDepthMargin = 8;
	int nnueMgWeight = 100;
	int nnueEgWeight = 67;
	int nnueWeightDiv = 100;
	int nullVerifyMinDepth = 6;
};

Move computeBestMove(GameState gs, int depth);
Move computeBestMove(GameState gs, int maxDepth, int timeLimitMs);
SearchStats getLastSearchStats();
void requestStopSearch();
void clearStopSearch();
void setHashSizeMb(int mb);
int getHashSizeMb();
void setTuningParams(const EngineTuningParams& p);
EngineTuningParams getTuningParams();
void setSyzygyPath(const std::string& path);
std::string getSyzygyPath();
void setSyzygyProbeLimit(int pieces);
int getSyzygyProbeLimit();
void setSearchInfoOutputEnabled(bool enabled);
bool isSearchInfoOutputEnabled();
void learningStartGame();
void learningAbortGame();
void learningFinalizeGame();
void setExperienceLearningEnabled(bool enabled);
bool isExperienceLearningEnabled();