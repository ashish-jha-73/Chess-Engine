#include "../include/engine.hpp"
#include "../include/chess.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <vector>

static constexpr int INF         =  1000000000;
static constexpr int MATE_SCORE  =  1000000;
static constexpr int DRAW_SCORE  =  0;

static constexpr int MAX_PLY     = 64;   
static constexpr int MAX_KILLERS = 2; 

static constexpr int pawnTable[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0 },
    { 50, 50, 50, 50, 50, 50, 50, 50 },
    { 10, 10, 20, 30, 30, 20, 10, 10 },
    {  5,  5, 10, 25, 25, 10,  5,  5 },
    {  0,  0,  0, 20, 20,  0,  0,  0 },
    {  5, -5,-10,  0,  0,-10, -5,  5 },
    {  5, 10, 10,-20,-20, 10, 10,  5 },
    {  0,  0,  0,  0,  0,  0,  0,  0 },
};

static constexpr int knightTable[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50 },
    {-40,-20,  0,  0,  0,  0,-20,-40 },
    {-30,  0, 10, 15, 15, 10,  0,-30 },
    {-30,  5, 15, 20, 20, 15,  5,-30 },
    {-30,  0, 15, 20, 20, 15,  0,-30 },
    {-30,  5, 10, 15, 15, 10,  5,-30 },
    {-40,-20,  0,  5,  5,  0,-20,-40 },
    {-50,-40,-30,-30,-30,-30,-40,-50 },
};

static constexpr int bishopTable[8][8] = {
    {-20,-10,-10,-10,-10,-10,-10,-20 },
    {-10,  0,  0,  0,  0,  0,  0,-10 },
    {-10,  0,  5, 10, 10,  5,  0,-10 },
    {-10,  5,  5, 10, 10,  5,  5,-10 },
    {-10,  0, 10, 10, 10, 10,  0,-10 },
    {-10, 10, 10, 10, 10, 10, 10,-10 },
    {-10,  5,  0,  0,  0,  0,  5,-10 },
    {-20,-10,-10,-10,-10,-10,-10,-20 },
};

static constexpr int rookTable[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0 },
    {  5, 10, 10, 10, 10, 10, 10,  5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    {  0,  0,  0,  5,  5,  0,  0,  0 },
};

static constexpr int queenTable[8][8] = {
    {-20,-10,-10, -5, -5,-10,-10,-20 },
    {-10,  0,  0,  0,  0,  0,  0,-10 },
    {-10,  0,  5,  5,  5,  5,  0,-10 },
    { -5,  0,  5,  5,  5,  5,  0, -5 },
    {  0,  0,  5,  5,  5,  5,  0, -5 },
    {-10,  5,  5,  5,  5,  5,  0,-10 },
    {-10,  0,  5,  0,  0,  0,  0,-10 },
    {-20,-10,-10, -5, -5,-10,-10,-20 },
};

static constexpr int kingMiddleTable[8][8] = {
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-20,-30,-30,-40,-40,-30,-30,-20 },
    {-10,-20,-20,-20,-20,-20,-20,-10 },
    { 20, 20,  0,  0,  0,  0, 20, 20 },
    { 20, 30, 10,  0,  0, 10, 30, 20 },
};

static constexpr int kingEndTable[8][8] = {
    {-50,-40,-30,-20,-20,-30,-40,-50 },
    {-30,-20,-10,  0,  0,-10,-20,-30 },
    {-30,-10, 20, 30, 30, 20,-10,-30 },
    {-30,-10, 30, 40, 40, 30,-10,-30 },
    {-30,-10, 30, 40, 40, 30,-10,-30 },
    {-30,-10, 20, 30, 30, 20,-10,-30 },
    {-30,-30,  0,  0,  0,  0,-30,-30 },
    {-50,-30,-30,-30,-30,-30,-30,-50 },
};

int pieceValue(int pt)
{
    switch (pt) {
        case P: return 100;
        case N: return 320;
        case B: return 330;
        case R: return 500;
        case Q: return 900;
        case K: return 20000;
        default: return 0;
    }
}

struct Zobrist {
    uint64_t pieces[6][2][8][8];
    uint64_t sideToMove;          
    uint64_t castling[4];         
    uint64_t enPassant[8];        

    Zobrist() {
        std::mt19937_64 rng(0xDEADBEEFCAFEBABEull);
        auto rand64 = [&]{ return rng(); };

        for (auto& a : pieces)
            for (auto& b : a)
                for (auto& c : b)
                    for (auto& d : c)
                        d = rand64();

        sideToMove = rand64();
        for (auto& x : castling)  x = rand64();
        for (auto& x : enPassant) x = rand64();
    }
} static const zob;

static int pieceIndex(int type) {
    switch(type){ case P:return 0; case N:return 1; case B:return 2;
                  case R:return 3; case Q:return 4; case K:return 5; }
    return 0;
}

uint64_t computeHash(const GameState& gs)
{
    uint64_t h = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == EMPTY) continue;
            h ^= zob.pieces[pieceIndex(p.type)][p.white ? 0 : 1][r][c];
        }
    if (!gs.whiteToMove) h ^= zob.sideToMove;
    return h;
}


enum TTFlag : uint8_t { TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    uint64_t hash    = 0;
    int      score   = 0;
    int      depth   = -1;
    TTFlag   flag    = TT_EXACT;
    Move     bestMove{ -1,-1,-1,-1 };
};

static constexpr size_t TT_SIZE = 1 << 22;

struct TranspositionTable {
    std::vector<TTEntry> table;

    TranspositionTable() : table(TT_SIZE) {}

    TTEntry* probe(uint64_t hash) {
        TTEntry* e = &table[hash & (TT_SIZE - 1)];
        return (e->hash == hash) ? e : nullptr;
    }

    void store(uint64_t hash, int score, int depth, TTFlag flag, const Move& best) {
        TTEntry& e = table[hash & (TT_SIZE - 1)];
        e = { hash, score, depth, flag, best };
    }

    void clear() { std::fill(table.begin(), table.end(), TTEntry{}); }
} static tt;



struct SearchState {
    Move killers[MAX_PLY][MAX_KILLERS];
    int  history[2][8][8][8][8]; 
    int  nodes = 0;
    bool stopped = false;
    std::chrono::steady_clock::time_point startTime;
    int  timeLimitMs = 5000;

    void clear() {
        nodes = 0;
        stopped = false;
        for (auto& ply : killers)
            for (auto& k : ply)
                k = { -1,-1,-1,-1 };
        for (auto& a : history)
            for (auto& b : a)
                for (auto& c : b)
                    for (auto& d : c)
                        for (auto& e : d)
                            e = 0;
    }

    bool timeUp() {
        if ((nodes & 4095) != 0) return false;
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count()
               >= timeLimitMs;
    }
} static ss;


static bool isEndgame(const GameState& gs)
{
    int mat = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type != EMPTY && p.type != K)
                mat += pieceValue(p.type);
        }
    return mat < 1800;
}

static int matingNetBonus(const GameState& gs, bool whiteWins)
{
    int wkr=-1,wkc=-1, bkr=-1,bkc=-1;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == K) {
                if (p.white) { wkr=r; wkc=c; }
                else         { bkr=r; bkc=c; }
            }
        }

    int loserR = whiteWins ? bkr : wkr;
    int loserC = whiteWins ? bkc : wkc;
    int edgeDist = std::min({ loserR, 7-loserR, loserC, 7-loserC });
    int cornerBonus = 400 * (3 - edgeDist); 
    int kingDist = std::abs(wkr-bkr) + std::abs(wkc-bkc);
    int proximityBonus = 40 * (14 - kingDist);

    return cornerBonus + proximityBonus;
}

static bool hasMatingMaterial(const GameState& gs, bool white)
{
    int bishops=0, knights=0, rooks=0, queens=0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == EMPTY || p.white != white) continue;
            switch(p.type){
                case B: bishops++; break;
                case N: knights++; break;
                case R: rooks++;   break;
                case Q: queens++;  break;
            }
        }
    if (queens  > 0) return true;
    if (rooks   > 0) return true;
    if (bishops >= 2) return true;
    if (bishops >= 1 && knights >= 1) return true;
    return false;
}

static uint8_t pawnFileMask(const GameState& gs, bool white)
{
    uint8_t mask = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == P && p.white == white)
                mask |= (1 << c);
        }
    return mask;
}

static int pawnStructureScore(const GameState& gs)
{
    int score = 0;

    uint8_t wFiles = pawnFileMask(gs, true);
    uint8_t bFiles = pawnFileMask(gs, false);

    for (int c = 0; c < 8; c++) {
        bool wHas = (wFiles >> c) & 1;
        bool bHas = (bFiles >> c) & 1;
        int wCount = 0, bCount = 0;
        for (int r = 0; r < 8; r++) {
            auto pw = gs.board[r][c]; 
            if (pw.type == P && pw.white)  wCount++;
            if (pw.type == P && !pw.white) bCount++;
        }
        if (wCount > 1) score -= 20 * (wCount - 1);
        if (bCount > 1) score += 20 * (bCount - 1);

        bool wLeft  = (c > 0) && ((wFiles >> (c-1)) & 1);
        bool wRight = (c < 7) && ((wFiles >> (c+1)) & 1);
        if (wHas && !wLeft && !wRight) score -= 15;

        bool bLeft  = (c > 0) && ((bFiles >> (c-1)) & 1);
        bool bRight = (c < 7) && ((bFiles >> (c+1)) & 1);
        if (bHas && !bLeft && !bRight) score += 15;
    }

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type != P) continue;

            if (p.white) {
                bool passed = true;
                for (int rr = r-1; rr >= 0 && passed; rr--)
                    for (int cc = std::max(0,c-1); cc <= std::min(7,c+1) && passed; cc++) {
                        auto q = gs.board[rr][cc];
                        if (q.type == P && !q.white) passed = false;
                    }
                if (passed) {
                    int rank = 7 - r;
                    score += 20 + rank * rank * 5;
                }
            } else {
                bool passed = true;
                for (int rr = r+1; rr < 8 && passed; rr++)
                    for (int cc = std::max(0,c-1); cc <= std::min(7,c+1) && passed; cc++) {
                        auto q = gs.board[rr][cc];
                        if (q.type == P && q.white) passed = false;
                    }
                if (passed) {
                    int rank = r;
                    score -= 20 + rank * rank * 5;
                }
            }
        }
    }

    return score;
}

static int rookOpenFileBonus(const GameState& gs)
{
    int score = 0;
    uint8_t wFiles = pawnFileMask(gs, true);
    uint8_t bFiles = pawnFileMask(gs, false);

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type != R) continue;

            bool noFriendly = !((p.white ? wFiles : bFiles) >> c & 1);
            bool noEnemy    = !((p.white ? bFiles : wFiles) >> c & 1);

            if (noFriendly && noEnemy) score += p.white ? 20 : -20;
            else if (noFriendly)       score += p.white ? 10 : -10;
        }
    return score;
}

static int bishopPairBonus(const GameState& gs)
{
    int wb = 0, bb = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == B) { if (p.white) wb++; else bb++; }
        }
    return (wb >= 2 ? 30 : 0) - (bb >= 2 ? 30 : 0);
}

static int evaluate(const GameState& gs)
{
    int score = 0;
    bool eg = isEndgame(gs);

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            auto p = gs.board[r][c];
            if (p.type == EMPTY) continue;

            int row = p.white ? r : 7 - r;
            int pst = 0;

            switch (p.type) {
                case P: pst = pawnTable[row][c];   break;
                case N: pst = knightTable[row][c]; break;
                case B: pst = bishopTable[row][c]; break;
                case R: pst = rookTable[row][c]; if (eg) pst += 10; break;
                case Q: pst = queenTable[row][c];  break;
                case K: pst = eg ? kingEndTable[row][c] : kingMiddleTable[row][c]; break;
            }

            int total = pieceValue(p.type) + pst;
            score += p.white ? total : -total;
        }
    }

    score += pawnStructureScore(gs);
    score += rookOpenFileBonus(gs);
    score += bishopPairBonus(gs);

    if (eg) {
        if (hasMatingMaterial(gs, true))  score += matingNetBonus(gs, true);
        if (hasMatingMaterial(gs, false)) score -= matingNetBonus(gs, false);
    }

    return gs.whiteToMove ? score : -score;
}


static int scoreMoveForOrdering(const GameState& gs, const Move& m, int ply, const Move& ttMove)
{
    if (m.sx == ttMove.sx && m.sy == ttMove.sy &&
        m.dx == ttMove.dx && m.dy == ttMove.dy)
        return 2000000;

    if (m.captured.has_value()) {
        int victim   = pieceValue(m.captured->type);
        int attacker = pieceValue(gs.board[m.sx][m.sy].type);
        return 1'000'000 + (victim * 10) - attacker;
    }

    if (m.promotion) return 900000;

    for (int i = 0; i < MAX_KILLERS; i++)
        if (ply < MAX_PLY &&
            m.sx == ss.killers[ply][i].sx && m.sy == ss.killers[ply][i].sy &&
            m.dx == ss.killers[ply][i].dx && m.dy == ss.killers[ply][i].dy)
            return 800000 - i * 100;

    int side = gs.whiteToMove ? 0 : 1;
    int h = 0;
    if (m.sx>=0 && m.sx<8 && m.sy>=0 && m.sy<8 &&
        m.dx>=0 && m.dx<8 && m.dy>=0 && m.dy<8)
        h = ss.history[side][m.sx][m.sy][m.dx][m.dy];

    return h;
}

static void sortMoves(std::vector<Move>& moves, const GameState& gs, int ply,
                      const Move& ttMove)
{
    std::sort(moves.begin(), moves.end(),
        [&](const Move& a, const Move& b) {
            return scoreMoveForOrdering(gs, a, ply, ttMove)
                 > scoreMoveForOrdering(gs, b, ply, ttMove);
        });
}

static void storeKiller(int ply, const Move& m)
{
    if (ply >= MAX_PLY) return;
    if (m.captured.has_value()) return;
    ss.killers[ply][1] = ss.killers[ply][0];
    ss.killers[ply][0] = m;
}

static void updateHistory(const GameState& gs, const Move& m, int depth)
{
    if (!m.captured.has_value() &&
        m.sx>=0 && m.sx<8 && m.sy>=0 && m.sy<8 &&
        m.dx>=0 && m.dx<8 && m.dy>=0 && m.dy<8)
    {
        int side = gs.whiteToMove ? 0 : 1;
        ss.history[side][m.sx][m.sy][m.dx][m.dy] += depth * depth;
    }
}

static int quiescence(GameState& gs, int alpha, int beta)
{
    ss.nodes++;

    int stand_pat = evaluate(gs);

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha)  alpha = stand_pat;

    auto moves = generateLegalMoves(gs);

    Move dummy{-1,-1,-1,-1};
    sortMoves(moves, gs, 0, dummy);

    for (auto& m : moves) {
        if (!m.captured.has_value() && !m.promotion)
            continue;

        makeMove(gs, m);
        int score = -quiescence(gs, -beta, -alpha);
        undoMove(gs);

        if (score >= beta) return beta;
        if (score > alpha)  alpha = score;
    }
    return alpha;
}

static int negamax(GameState& gs, int depth, int alpha, int beta, int ply,
                   bool nullMoveAllowed)
{
    if (ss.timeUp()) { ss.stopped = true; return 0; }

    ss.nodes++;

    uint64_t hash = computeHash(gs);
    TTEntry* entry = tt.probe(hash);
    Move ttBestMove{-1,-1,-1,-1};

    if (entry && entry->depth >= depth && ply > 0) {
        int ttScore = entry->score;
        if (entry->flag == TT_EXACT)              return ttScore;
        if (entry->flag == TT_LOWER && ttScore >= beta) return ttScore;
        if (entry->flag == TT_UPPER && ttScore <= alpha) return ttScore;
        ttBestMove = entry->bestMove;
    } else if (entry) {
        ttBestMove = entry->bestMove;
    }

    if (depth == 0) return quiescence(gs, alpha, beta);

    auto moves = generateLegalMoves(gs);

    if (moves.empty()) {
        if (isInCheck(gs, gs.whiteToMove))
            return -(MATE_SCORE - ply);   
        return DRAW_SCORE;
    }

    if (nullMoveAllowed && depth >= 3 && !isInCheck(gs, gs.whiteToMove) && !isEndgame(gs))
    {
        gs.whiteToMove = !gs.whiteToMove;
        int R = (depth >= 6) ? 3 : 2;
        int nullScore = -negamax(gs, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
        gs.whiteToMove = !gs.whiteToMove;

        if (!ss.stopped && nullScore >= beta)
            return beta;
    }

    sortMoves(moves, gs, ply, ttBestMove);

    TTFlag flag = TT_UPPER;
    Move bestMove = moves.front();
    int  bestScore = -INF;
    int  moveCount = 0;

    for (auto& m : moves) {
        moveCount++;
        makeMove(gs, m);

        int score;
        if (moveCount > 4 && depth >= 3 && !m.captured.has_value() && !m.promotion && !isInCheck(gs, gs.whiteToMove)) {
            int reduction = 1 + (moveCount > 12 ? 1 : 0);
            score = -negamax(gs, depth - 1 - reduction, -alpha-1, -alpha, ply+1, true);
            if (score > alpha)
                score = -negamax(gs, depth - 1, -beta, -alpha, ply + 1, true);
        }
        else {
            if (moveCount == 1) {
                score = -negamax(gs, depth - 1, -beta, -alpha, ply + 1, true);
            } else {
                score = -negamax(gs, depth - 1, -alpha - 1, -alpha, ply + 1, true);
                if (score > alpha && score < beta)
                    score = -negamax(gs, depth - 1, -beta, -alpha, ply + 1, true);
            }
        }

        undoMove(gs);

        if (ss.stopped) return 0;

        if (score > bestScore) {
            bestScore = score;
            bestMove  = m;
        }

        if (score > alpha) {
            alpha = score;
            flag  = TT_EXACT;

            if (!m.captured.has_value())
                updateHistory(gs, m, depth);
        }

        if (alpha >= beta) {
            storeKiller(ply, m);
            tt.store(hash, beta, depth, TT_LOWER, m);
            return beta;
        }
    }

    tt.store(hash, bestScore, depth, flag, bestMove);
    return bestScore;
}

Move computeBestMove(GameState gs, int maxDepth, int timeLimitMs)
{
    auto moves = generateLegalMoves(gs);
    if (moves.empty()) return Move{ -1,-1,-1,-1 };

    ss.clear();
    ss.startTime   = std::chrono::steady_clock::now();
    ss.timeLimitMs = timeLimitMs;

    if (isEndgame(gs)) maxDepth += 3;

    Move bestMove = moves.front();
    int  bestScore = -INF;

    for (int depth = 1; depth <= maxDepth; depth++) {
        int alpha = -INF, beta = INF;
        Move currentBest = moves.front();
        int  currentBestScore = -INF;

        uint64_t hash = computeHash(gs);
        TTEntry* e = tt.probe(hash);
        Move ttMove = (e && e->bestMove.sx != -1) ? e->bestMove : Move{-1,-1,-1,-1};
        sortMoves(moves, gs, 0, ttMove);

        for (auto& m : moves) {
            makeMove(gs, m);
            int score = -negamax(gs, depth - 1, -beta, -alpha, 1, true);
            undoMove(gs);

            if (ss.stopped) goto done;

            if (score > currentBestScore) {
                currentBestScore = score;
                currentBest = m;
            }
            if (score > alpha) {
                alpha = score;
            }
        }

        bestMove  = currentBest;
        bestScore = currentBestScore;

        std::cout << "depth " << depth << "  score " << bestScore << "  nodes " << ss.nodes << "\n";

        if (std::abs(bestScore) >= MATE_SCORE - MAX_PLY) break;
    }

done:
    return bestMove;
}

Move computeBestMove(GameState gs, int depth)
{
    return computeBestMove(gs, depth, 600000);
}