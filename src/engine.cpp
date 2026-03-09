#include "../include/engine.hpp"
#include "../include/chess.hpp"
#include <limits>
#include <algorithm>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

static const int INF = 1000000000;

inline int manhattan(int r1,int c1,int r2,int c2){ return abs(r1-r2)+abs(c1-c2); }
inline int edgeDistance(int r,int c){ return std::min({r,7-r,c,7-c}); }

namespace chess{

static int pawnTable[8][8] = {
{ 0,  0,  0,  0,  0,  0,  0,  0},
{50, 50, 50, 50, 50, 50, 50, 50},
{10, 10, 20, 30, 30, 20, 10, 10},
{ 5,  5, 10, 25, 25, 10,  5,  5},
{ 0,  0,  0, 20, 20,  0,  0,  0},
{ 5, -5,-10,  0,  0,-10, -5,  5},
{ 5, 10, 10,-20,-20, 10, 10,  5},
{ 0,  0,  0,  0,  0,  0,  0,  0}
};

static int knightTable[8][8] = {
{-50,-40,-30,-30,-30,-30,-40,-50},
{-40,-20,  0,  0,  0,  0,-20,-40},
{-30,  0, 10, 15, 15, 10,  0,-30},
{-30,  5, 15, 20, 20, 15,  5,-30},
{-30,  0, 15, 20, 20, 15,  0,-30},
{-30,  5, 10, 15, 15, 10,  5,-30},
{-40,-20,  0,  5,  5,  0,-20,-40},
{-50,-40,-30,-30,-30,-30,-40,-50}
};

static int bishopTable[8][8] = {
{-20,-10,-10,-10,-10,-10,-10,-20},
{-10,  0,  0,  0,  0,  0,  0,-10},
{-10,  0,  5, 10, 10,  5,  0,-10},
{-10,  5,  5, 10, 10,  5,  5,-10},
{-10,  0, 10, 10, 10, 10,  0,-10},
{-10, 10, 10, 10, 10, 10, 10,-10},
{-10,  5,  0,  0,  0,  0,  5,-10},
{-20,-10,-10,-10,-10,-10,-10,-20}
};

static int rookTable[8][8] = {
{ 0,  0,  0,  0,  0,  0,  0,  0},
{ 5, 10, 10, 10, 10, 10, 10,  5},
{-5,  0,  0,  0,  0,  0,  0, -5},
{-5,  0,  0,  0,  0,  0,  0, -5},
{-5,  0,  0,  0,  0,  0,  0, -5},
{-5,  0,  0,  0,  0,  0,  0, -5},
{-5,  0,  0,  0,  0,  0,  0, -5},
{ 0,  0,  0,  5,  5,  0,  0,  0}
};

static int queenTable[8][8] = {
{-20,-10,-10, -5, -5,-10,-10,-20},
{-10,  0,  0,  0,  0,  0,  0,-10},
{-10,  0,  5,  5,  5,  5,  0,-10},
{ -5,  0,  5,  5,  5,  5,  0, -5},
{  0,  0,  5,  5,  5,  5,  0, -5},
{-10,  5,  5,  5,  5,  5,  0,-10},
{-10,  0,  5,  0,  0,  0,  0,-10},
{-20,-10,-10, -5, -5,-10,-10,-20}
};

static int kingMiddleTable[8][8] = {
{-30,-40,-40,-50,-50,-40,-40,-30},
{-30,-40,-40,-50,-50,-40,-40,-30},
{-30,-40,-40,-50,-50,-40,-40,-30},
{-30,-40,-40,-50,-50,-40,-40,-30},
{-20,-30,-30,-40,-40,-30,-30,-20},
{-10,-20,-20,-20,-20,-20,-20,-10},
{ 20, 20,  0,  0,  0,  0, 20, 20},
{ 20, 30, 10,  0,  0, 10, 30, 20}
};

static int kingEndTable[8][8] = {
{-50,-40,-30,-20,-20,-30,-40,-50},
{-30,-20,-10,  0,  0,-10,-20,-30},
{-30,-10, 20, 30, 30, 20,-10,-30},
{-30,-10, 30, 40, 40, 30,-10,-30},
{-30,-10, 30, 40, 40, 30,-10,-30},
{-30,-10, 20, 30, 30, 20,-10,-30},
{-30,-30,  0,  0,  0,  0,-30,-30},
{-50,-30,-30,-30,-30,-30,-30,-50}
};

int pieceValue(int pt)
{
    switch (pt) {
    case P:
        return 100;
    case N:
        return 320;
    case B:
        return 330;
    case R:
        return 500;
    case Q:
        return 900;
    case K:
        return 20000;
    default:
        return 0;
    }
}

bool isEndgame(const GameState& gs)
{
    int material = 0;

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == Q)
                return false;

            if (p.type != EMPTY && p.type != K)
                material += pieceValue(p.type);
        }

    return material < 2000;
}

int moveScore(const GameState& gs, const Move& m)
{
    int score = 0;

    if (m.captured.has_value()) {
        int victim = pieceValue(m.captured->type);
        int attacker = pieceValue(gs.board[m.sx][m.sy].type);
        // higher victim, lower attacker -> better
        score += 10000 + (victim * 10) - attacker;
    }

    if (m.promotion)
        score += 8000;
    if (m.isCastle)
        score += 100;

    return score;
}

int evaluateMaterial(const GameState& gs)
{
    int score = 0;
    bool endgame = isEndgame(gs);

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {

            auto p = gs.board[r][c];
            if (p.type == EMPTY) continue;

            int value = pieceValue(p.type);
            int row = p.white ? r : 7 - r;

            int pst = 0;
            
            switch (p.type){
                case P: pst = pawnTable[row][c]; break;
                case N: pst = knightTable[row][c]; break;
                case B: pst = bishopTable[row][c]; break;
                case R: pst = rookTable[row][c]; break;
                case Q: pst = queenTable[row][c]; break;

                case K:
                    if (endgame)
                        pst = kingEndTable[row][c];
                    else
                        pst = kingMiddleTable[row][c];
                    break;
            }

            int total = value + pst;

            score += p.white ? total : -total;
        }
    }

    return score;
}

int quiescence(GameState gs, int alpha, int beta, bool maximizingPlayer)
{
    int stand_pat = evaluateMaterial(gs);

    if (maximizingPlayer) {

        if (stand_pat >= beta)
            return beta;

        if (alpha < stand_pat)
            alpha = stand_pat;

    } else {

        if (stand_pat <= alpha)
            return alpha;

        if (beta > stand_pat)
            beta = stand_pat;
    }

    auto moves = generateLegalMoves(gs);

    std::sort(moves.begin(), moves.end(),
    [&](const Move& a, const Move& b) {
        return moveScore(gs, a) > moveScore(gs, b);
    });

    for (auto& m : moves)
    {
        if (!m.captured.has_value())
            continue;

        GameState copy = gs;
        makeMove(copy, m);

        int score = quiescence(copy, alpha, beta, !maximizingPlayer);

        if (maximizingPlayer)
        {
            if (score > alpha)
                alpha = score;

            if (alpha >= beta)
                break;
        }
        else
        {
            if (score < beta)
                beta = score;

            if (alpha >= beta)
                break;
        }
    }

    return maximizingPlayer ? alpha : beta;
}

int minimax(GameState gs, int depth, int alpha, int beta, bool maximizingPlayer)
{
    if (depth == 0)
        return quiescence(gs, alpha, beta, maximizingPlayer);

    auto moves = generateLegalMoves(gs);
    if (moves.empty()) {
        if (isInCheck(gs, gs.whiteToMove)) {
            return maximizingPlayer ? -1000000 + depth : 1000000 - depth;
        } else {
            return maximizingPlayer ? -300 : 300;
        }
    }
    
    std::sort(moves.begin(), moves.end(),
    [&](const Move& a, const Move& b) {
        return moveScore(gs, a) > moveScore(gs, b);
    });

    if (maximizingPlayer) {
        int value = -INF;
        for (auto& m : moves) {
            GameState copy = gs;
            makeMove(copy, m);
            int v = minimax(copy, depth - 1, alpha, beta, false);
            value = std::max(value, v);
            alpha = std::max(alpha, value);
            if (alpha >= beta)
                break;
        }
        return value;
    } else {
        int value = INF;
        for (auto& m : moves) {
            GameState copy = gs;
            makeMove(copy, m);
            int v = minimax(copy, depth - 1, alpha, beta, true);
            value = std::min(value, v);
            beta = std::min(beta, value);
            if (alpha >= beta)
                break;
        }
        return value;
    }
}
}

Move computeBestMove(GameState gs, int depth)
{
    auto moves = generateLegalMoves(gs);
    if (moves.empty()) {
        return Move { -1, -1, -1, -1 };
    }
    int bestVal = gs.whiteToMove ? -INF : INF;
    std::vector<Move> bestMoves;
    if (chess::isEndgame(gs)) depth += 2;
    for (auto& m : moves) {
        GameState copy = gs;
        makeMove(copy, m);
        
        int val = chess::minimax(copy, depth - 1, -INF, INF, !gs.whiteToMove);

        if (gs.whiteToMove) {
            if (val > bestVal) {
                bestVal = val;
                bestMoves.clear();
                bestMoves.push_back(m);
            }
            else if (val == bestVal) {
                bestMoves.push_back(m);
            }
        } else {
            if (val < bestVal) {
                bestVal = val;
                bestMoves.clear();
                bestMoves.push_back(m);
            }
            else if (val == bestVal) {
                bestMoves.push_back(m);
            }
        }
    }
    if (bestMoves.empty()) return moves.front();
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::uniform_int_distribution<size_t> dist(0, bestMoves.size()-1);
    std::mt19937 gen(seed);
    return bestMoves[dist(gen)];
}