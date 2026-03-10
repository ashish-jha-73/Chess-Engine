#include "../include/engine.hpp"
#include "../include/chess.hpp"
#include <limits>
#include <algorithm>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

#define MAX_DEPTH 10

static const int INF = 1000000000;

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
{-30,  0, 5, 15, 15, 5,  0,-30},
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

bool hasMatingMaterial(const GameState& gs, bool white)
{
    int bishops = 0, knights = 0, rooks = 0, queens = 0;

    for(int r=0;r<8;r++)
        for(int c=0;c<8;c++){
            auto p = gs.board[r][c];

            if(p.type == EMPTY || p.white != white) continue;

            if(p.type == B) bishops++;
            if(p.type == N) knights++;
            if(p.type == R) rooks++;
            if(p.type == Q) queens++;
        }

    if(queens > 0) return true;
    if(rooks > 0) return true;
    if(bishops >= 2) return true;

    return false;
}

int matingNet(const GameState& gs)
{
    int wr=-1,wc=-1, br=-1,bc=-1;

    for(int r=0;r<8;r++)
        for(int c=0;c<8;c++){
            auto p = gs.board[r][c];

            if(p.type==K){
                if(p.white){ wr=r; wc=c; }
                else{ br=r; bc=c; }
            }
        }

    int score = 0;

    int edgeDist = std::min({br,7-br,bc,7-bc});
    score += 400 * (3 - edgeDist);

    int kingDist = abs(wr-br) + abs(wc-bc);
    score += 40 * (14 - kingDist);

    return score;
}

int oppositionBonus(const GameState& gs)
{
    int wr=-1,wc=-1, br=-1,bc=-1;

    for(int r=0;r<8;r++)
        for(int c=0;c<8;c++){
            auto p = gs.board[r][c];
            if(p.type == K){
                if(p.white){ wr=r; wc=c; }
                else{ br=r; bc=c; }
            }
        }

    int score = 0;
    if(wc == bc && abs(wr-br) == 2)
    {
        if(gs.whiteToMove)
            score -= 20;
        else
            score += 20;
    }
    if(wr == br && abs(wc-bc) == 2)
    {
        if(gs.whiteToMove)
            score -= 20;
        else
            score += 20;
    }

    return score;
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
                case R: pst = rookTable[row][c]; if (endgame) pst += 20; break;
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
    if(endgame)
    {
        if(hasMatingMaterial(gs,true))
            score += matingNet(gs);

        if(hasMatingMaterial(gs,false))
            score -= matingNet(gs);
        
        score += oppositionBonus(gs);
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
            return maximizingPlayer ? -1000000 + (MAX_DEPTH - depth): 1000000 - (MAX_DEPTH - depth);
        } else {
            return maximizingPlayer ? -400 : 400;
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


Move computeBestMove(GameState gs, int depth)
{
    auto moves = generateLegalMoves(gs);
    if (moves.empty()) {
        return Move { -1, -1, -1, -1 };
    }
    int bestVal = gs.whiteToMove ? -INF : INF;
    std::vector<Move> bestMoves;
    if (isEndgame(gs)) depth += 2;
    for (auto& m : moves) {
        bool temp = !gs.whiteToMove;
        makeMove(gs, m);
        int val = minimax(gs, depth - 1, -INF, INF, temp);
        undoMove(gs);
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
        std::cout << val << std::endl;
    }
    if (bestMoves.empty()) return moves.front();
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::uniform_int_distribution<size_t> dist(0, bestMoves.size()-1);
    std::mt19937 gen(seed);
    return bestMoves[dist(gen)];
}