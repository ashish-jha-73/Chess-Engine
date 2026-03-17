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
static constexpr int MAX_DEPTH   = 100;

inline int rowOfSq(int sq) { return sq / 8; }
inline int colOfSq(int sq) { return sq % 8; }
inline int popcount64(uint64_t bb) { return __builtin_popcountll(bb); }
inline int lsbSquare64(uint64_t bb) { return __builtin_ctzll(bb); }
inline uint64_t popLsb64(uint64_t& bb)
{
    const uint64_t lsb = bb & -bb;
    bb ^= lsb;
    return lsb;
}
inline Move invalidMove()
{
    Move m;
    m.value = 0xFFFFFFFFu;
    return m;
}

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

uint64_t computeHash(const GameState& gs)
{
    uint64_t h = 0;
    for (int color = 0; color < 2; ++color) {
        for (int pt = 0; pt < 6; ++pt) {
            uint64_t bb = gs.bitboards[color][pt];
            while (bb) {
                const uint64_t lsb = bb & -bb;
                const int sq = __builtin_ctzll(bb);
                bb ^= lsb;
                const int r = sq / 8;
                const int c = sq % 8;
                h ^= zob.pieces[pt][color][r][c];
            }
        }
    }

    if (!gs.wkMoved && !gs.wrHHMoved) h ^= zob.castling[0];
    if (!gs.wkMoved && !gs.wrAHMoved) h ^= zob.castling[1];
    if (!gs.bkMoved && !gs.brHHMoved) h ^= zob.castling[2];
    if (!gs.bkMoved && !gs.brAHMoved) h ^= zob.castling[3];

    if (gs.enPassantTarget.has_value()) {
        h ^= zob.enPassant[gs.enPassantTarget->second];
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
    Move     bestMove = invalidMove();
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
    std::array<uint64_t, 1024> hashHistory{};
    int hashCount = 0;
    int  nodes = 0;
    bool stopped = false;
    std::chrono::steady_clock::time_point startTime;
    int  timeLimitMs = 5000;

    void clear() {
        nodes = 0;
        hashCount = 0;
        stopped = false;
        for (auto& ply : killers)
            for (auto& k : ply)
                k = invalidMove();
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

static SearchStats lastSearchStats;

static inline int scoreToTT(int score, int ply)
{
    if (score > MATE_SCORE - MAX_PLY) return score + ply;
    if (score < -MATE_SCORE + MAX_PLY) return score - ply;
    return score;
}

static inline int scoreFromTT(int score, int ply)
{
    if (score > MATE_SCORE - MAX_PLY) return score - ply;
    if (score < -MATE_SCORE + MAX_PLY) return score + ply;
    return score;
}

static bool isThreefoldInSearch(uint64_t hash)
{
    int seen = 0;
    for (int i = 0; i < ss.hashCount; ++i) {
        if (ss.hashHistory[i] == hash) {
            seen++;
            if (seen >= 3) {
                return true;
            }
        }
    }
    return false;
}

static int repetitionScore(const GameState& gs)
{
    // Dynamic contempt: avoid repetition when better, embrace it when clearly worse.
    int material = 0;
    for (int pt = P; pt <= Q; ++pt) {
        material += pieceValue(pt) * popcount64(gs.bitboards[0][pt - 1]);
        material -= pieceValue(pt) * popcount64(gs.bitboards[1][pt - 1]);
    }
    const int perspectiveMat = gs.whiteToMove ? material : -material;

    if (perspectiveMat > 300) return -900;
    if (perspectiveMat > 120) return -400;
    if (perspectiveMat < -300) return 120;
    return -80;
}

struct HashHistoryGuard {
    bool active = false;

    explicit HashHistoryGuard(uint64_t hash)
    {
        if (ss.hashCount < static_cast<int>(ss.hashHistory.size())) {
            ss.hashHistory[ss.hashCount++] = hash;
            active = true;
        }
    }

    ~HashHistoryGuard()
    {
        if (active && ss.hashCount > 0) {
            --ss.hashCount;
        }
    }
};


static bool isEndgame(const GameState& gs)
{
    int mat = 0;
    mat += pieceValue(P) * (popcount64(gs.bitboards[0][P - 1]) + popcount64(gs.bitboards[1][P - 1]));
    mat += pieceValue(N) * (popcount64(gs.bitboards[0][N - 1]) + popcount64(gs.bitboards[1][N - 1]));
    mat += pieceValue(B) * (popcount64(gs.bitboards[0][B - 1]) + popcount64(gs.bitboards[1][B - 1]));
    mat += pieceValue(R) * (popcount64(gs.bitboards[0][R - 1]) + popcount64(gs.bitboards[1][R - 1]));
    mat += pieceValue(Q) * (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]));
    return mat < 1800;
}

static int matingNetBonus(const GameState& gs, bool whiteWins)
{
    const int wKingSq = lsbSquare64(gs.bitboards[0][K - 1]);
    const int bKingSq = lsbSquare64(gs.bitboards[1][K - 1]);
    const int wkr = rowOfSq(wKingSq);
    const int wkc = colOfSq(wKingSq);
    const int bkr = rowOfSq(bKingSq);
    const int bkc = colOfSq(bKingSq);

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
    const int side = white ? 0 : 1;
    const int bishops = popcount64(gs.bitboards[side][B - 1]);
    const int knights = popcount64(gs.bitboards[side][N - 1]);
    const int rooks = popcount64(gs.bitboards[side][R - 1]);
    const int queens = popcount64(gs.bitboards[side][Q - 1]);
    if (queens  > 0) return true;
    if (rooks   > 0) return true;
    if (bishops >= 2) return true;
    if (bishops >= 1 && knights >= 1) return true;
    return false;
}

static uint8_t pawnFileMask(const GameState& gs, bool white)
{
    uint8_t mask = 0;
    uint64_t pawns = gs.bitboards[white ? 0 : 1][P - 1];
    while (pawns) {
        int sq = lsbSquare64(popLsb64(pawns));
        mask |= static_cast<uint8_t>(1u << colOfSq(sq));
    }
    return mask;
}

static int pawnStructureScore(const GameState& gs)
{
    int score = 0;

    const uint64_t wPawns = gs.bitboards[0][P - 1];
    const uint64_t bPawns = gs.bitboards[1][P - 1];

    uint8_t wFiles = pawnFileMask(gs, true);
    uint8_t bFiles = pawnFileMask(gs, false);

    for (int c = 0; c < 8; c++) {
        bool wHas = (wFiles >> c) & 1;
        bool bHas = (bFiles >> c) & 1;
        uint64_t fileMask = 0x0101010101010101ULL << c;
        int wCount = popcount64(wPawns & fileMask);
        int bCount = popcount64(bPawns & fileMask);
        if (wCount > 1) score -= 20 * (wCount - 1);
        if (bCount > 1) score += 20 * (bCount - 1);

        bool wLeft  = (c > 0) && ((wFiles >> (c-1)) & 1);
        bool wRight = (c < 7) && ((wFiles >> (c+1)) & 1);
        if (wHas && !wLeft && !wRight) score -= 15;

        bool bLeft  = (c > 0) && ((bFiles >> (c-1)) & 1);
        bool bRight = (c < 7) && ((bFiles >> (c+1)) & 1);
        if (bHas && !bLeft && !bRight) score += 15;
    }

    uint64_t wp = wPawns;
    while (wp) {
        const int sq = lsbSquare64(popLsb64(wp));
        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        uint64_t frontMask = 0;
        for (int rr = r - 1; rr >= 0; --rr) {
            for (int cc = std::max(0, c - 1); cc <= std::min(7, c + 1); ++cc) {
                frontMask |= (1ULL << (rr * 8 + cc));
            }
        }
        if ((bPawns & frontMask) == 0) {
            int rank = 7 - r;
            score += 20 + rank * rank * 5;
        }
    }

    uint64_t bp = bPawns;
    while (bp) {
        const int sq = lsbSquare64(popLsb64(bp));
        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        uint64_t frontMask = 0;
        for (int rr = r + 1; rr < 8; ++rr) {
            for (int cc = std::max(0, c - 1); cc <= std::min(7, c + 1); ++cc) {
                frontMask |= (1ULL << (rr * 8 + cc));
            }
        }
        if ((wPawns & frontMask) == 0) {
            int rank = r;
            score -= 20 + rank * rank * 5;
        }
    }

    return score;
}

static int rookOpenFileBonus(const GameState& gs)
{
    int score = 0;
    uint8_t wFiles = pawnFileMask(gs, true);
    uint8_t bFiles = pawnFileMask(gs, false);

    uint64_t wr = gs.bitboards[0][R - 1];
    while (wr) {
        const int c = colOfSq(lsbSquare64(popLsb64(wr)));
        bool noFriendly = ((wFiles >> c) & 1) == 0;
        bool noEnemy = ((bFiles >> c) & 1) == 0;
        if (noFriendly && noEnemy)
            score += 20;
        else if (noFriendly)
            score += 10;
    }

    uint64_t br = gs.bitboards[1][R - 1];
    while (br) {
        const int c = colOfSq(lsbSquare64(popLsb64(br)));
        bool noFriendly = ((bFiles >> c) & 1) == 0;
        bool noEnemy = ((wFiles >> c) & 1) == 0;
        if (noFriendly && noEnemy)
            score -= 20;
        else if (noFriendly)
            score -= 10;
    }
    return score;
}

static int bishopPairBonus(const GameState& gs)
{
    const int wb = popcount64(gs.bitboards[0][B - 1]);
    const int bb = popcount64(gs.bitboards[1][B - 1]);
    return (wb >= 2 ? 30 : 0) - (bb >= 2 ? 30 : 0);
}

static bool isKRK(const GameState& gs, bool whiteHasRook)
{
    const int strong = whiteHasRook ? 0 : 1;
    const int weak = whiteHasRook ? 1 : 0;

    if (popcount64(gs.bitboards[strong][R - 1]) != 1) return false;
    if (popcount64(gs.bitboards[weak][R - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][P - 1]) + popcount64(gs.bitboards[1][P - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][N - 1]) + popcount64(gs.bitboards[1][N - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][B - 1]) + popcount64(gs.bitboards[1][B - 1]) != 0) return false;
    return true;
}

static int krkBonus(const GameState& gs, bool whiteHasRook)
{
    const int strong = whiteHasRook ? 0 : 1;
    const int weak = whiteHasRook ? 1 : 0;

    const int strongKingSq = lsbSquare64(gs.bitboards[strong][K - 1]);
    const int weakKingSq = lsbSquare64(gs.bitboards[weak][K - 1]);
    const int rookSq = lsbSquare64(gs.bitboards[strong][R - 1]);

    const int skr = rowOfSq(strongKingSq);
    const int skc = colOfSq(strongKingSq);
    const int wkr = rowOfSq(weakKingSq);
    const int wkc = colOfSq(weakKingSq);
    const int rr = rowOfSq(rookSq);
    const int rc = colOfSq(rookSq);

    const int edgeDist = std::min({ wkr, 7 - wkr, wkc, 7 - wkc });
    const int kingDist = std::abs(skr - wkr) + std::abs(skc - wkc);
    const int rookCuts = (rr == wkr || rc == wkc) ? 1 : 0;
    const int weakInCheck = isInCheck(gs, !whiteHasRook) ? 1 : 0;

    int bonus = 0;
    bonus += 700;
    bonus += (3 - edgeDist) * 220;
    bonus += (14 - kingDist) * 45;
    bonus += rookCuts * 140;
    bonus += weakInCheck * 120;
    return whiteHasRook ? bonus : -bonus;
}

static bool isKBBK(const GameState& gs, bool whiteHasBishops)
{
    const int strong = whiteHasBishops ? 0 : 1;
    const int weak = whiteHasBishops ? 1 : 0;

    if (popcount64(gs.bitboards[strong][B - 1]) != 2) return false;
    if (popcount64(gs.bitboards[weak][B - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][R - 1]) + popcount64(gs.bitboards[1][R - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][N - 1]) + popcount64(gs.bitboards[1][N - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][P - 1]) + popcount64(gs.bitboards[1][P - 1]) != 0) return false;
    return true;
}

static int kbbkBonus(const GameState& gs, bool whiteHasBishops)
{
    const int strong = whiteHasBishops ? 0 : 1;
    const int weak = whiteHasBishops ? 1 : 0;

    const int strongKingSq = lsbSquare64(gs.bitboards[strong][K - 1]);
    const int weakKingSq = lsbSquare64(gs.bitboards[weak][K - 1]);

    const int skr = rowOfSq(strongKingSq);
    const int skc = colOfSq(strongKingSq);
    const int wkr = rowOfSq(weakKingSq);
    const int wkc = colOfSq(weakKingSq);

    const int edgeDist = std::min({ wkr, 7 - wkr, wkc, 7 - wkc });
    const int kingDist = std::abs(skr - wkr) + std::abs(skc - wkc);
    const int weakInCheck = isInCheck(gs, !whiteHasBishops) ? 1 : 0;

    int bonus = 0;
    bonus += 820;
    bonus += (3 - edgeDist) * 260;
    bonus += (14 - kingDist) * 55;
    bonus += weakInCheck * 120;
    return whiteHasBishops ? bonus : -bonus;
}

static bool isKBNK(const GameState& gs, bool whiteHasBN)
{
    const int strong = whiteHasBN ? 0 : 1;
    const int weak = whiteHasBN ? 1 : 0;

    if (popcount64(gs.bitboards[strong][B - 1]) != 1) return false;
    if (popcount64(gs.bitboards[strong][N - 1]) != 1) return false;
    if (popcount64(gs.bitboards[weak][B - 1]) != 0) return false;
    if (popcount64(gs.bitboards[weak][N - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][R - 1]) + popcount64(gs.bitboards[1][R - 1]) != 0) return false;
    if (popcount64(gs.bitboards[0][P - 1]) + popcount64(gs.bitboards[1][P - 1]) != 0) return false;
    return true;
}

static int kbnkBonus(const GameState& gs, bool whiteHasBN)
{
    const int strong = whiteHasBN ? 0 : 1;
    const int weak = whiteHasBN ? 1 : 0;

    const int strongKingSq = lsbSquare64(gs.bitboards[strong][K - 1]);
    const int weakKingSq = lsbSquare64(gs.bitboards[weak][K - 1]);
    const int bishopSq = lsbSquare64(gs.bitboards[strong][B - 1]);
    const int knightSq = lsbSquare64(gs.bitboards[strong][N - 1]);

    const int skr = rowOfSq(strongKingSq);
    const int skc = colOfSq(strongKingSq);
    const int wkr = rowOfSq(weakKingSq);
    const int wkc = colOfSq(weakKingSq);
    const int br = rowOfSq(bishopSq);
    const int bc = colOfSq(bishopSq);
    const int nr = rowOfSq(knightSq);
    const int nc = colOfSq(knightSq);

    const int edgeDist = std::min({ wkr, 7 - wkr, wkc, 7 - wkc });
    const int kingDist = std::abs(skr - wkr) + std::abs(skc - wkc);
    const int bishopDist = std::abs(br - wkr) + std::abs(bc - wkc);
    const int knightDist = std::abs(nr - wkr) + std::abs(nc - wkc);
    const int weakInCheck = isInCheck(gs, !whiteHasBN) ? 1 : 0;

    // In KBNK, mate is only possible in corners matching bishop color.
    const int bishopCornerColor = (br + bc) & 1;
    const int targetCornerDist = std::min({
        std::abs(wkr - 0) + std::abs(wkc - bishopCornerColor * 7),
        std::abs(wkr - 7) + std::abs(wkc - ((1 - bishopCornerColor) * 7))
    });

    int bonus = 0;
    bonus += 720;
    bonus += (3 - edgeDist) * 230;
    bonus += (14 - kingDist) * 55;
    bonus += (14 - bishopDist) * 18;
    bonus += (14 - knightDist) * 20;
    bonus += (14 - targetCornerDist) * 35;
    bonus += weakInCheck * 120;
    return whiteHasBN ? bonus : -bonus;
}

static bool isKQKFamily(const GameState& gs, bool whiteHasQueen)
{
    const int strong = whiteHasQueen ? 0 : 1;
    const int weak = whiteHasQueen ? 1 : 0;

    if (popcount64(gs.bitboards[strong][Q - 1]) != 1) return false;
    if (popcount64(gs.bitboards[strong][R - 1]) != 0) return false;
    if (popcount64(gs.bitboards[strong][B - 1]) != 0) return false;
    if (popcount64(gs.bitboards[strong][N - 1]) != 0) return false;
    if (popcount64(gs.bitboards[strong][P - 1]) != 0) return false;

    if (popcount64(gs.bitboards[weak][Q - 1]) != 0) return false;
    if (popcount64(gs.bitboards[weak][R - 1]) != 0) return false;
    if (popcount64(gs.bitboards[weak][B - 1]) != 0) return false;
    if (popcount64(gs.bitboards[weak][N - 1]) != 0) return false;
    if (popcount64(gs.bitboards[weak][P - 1]) > 1) return false;
    return true;
}

static int kqkFamilyBonus(const GameState& gs, bool whiteHasQueen)
{
    const int strong = whiteHasQueen ? 0 : 1;
    const int weak = whiteHasQueen ? 1 : 0;

    const int strongKingSq = lsbSquare64(gs.bitboards[strong][K - 1]);
    const int weakKingSq = lsbSquare64(gs.bitboards[weak][K - 1]);
    const int queenSq = lsbSquare64(gs.bitboards[strong][Q - 1]);

    const int skr = rowOfSq(strongKingSq);
    const int skc = colOfSq(strongKingSq);
    const int wkr = rowOfSq(weakKingSq);
    const int wkc = colOfSq(weakKingSq);
    const int qr = rowOfSq(queenSq);
    const int qc = colOfSq(queenSq);

    const int edgeDist = std::min({ wkr, 7 - wkr, wkc, 7 - wkc });
    const int kingDist = std::abs(skr - wkr) + std::abs(skc - wkc);
    const int queenDist = std::abs(qr - wkr) + std::abs(qc - wkc);
    const int queenCuts = (qr == wkr || qc == wkc) ? 1 : 0;
    const int weakInCheck = isInCheck(gs, !whiteHasQueen) ? 1 : 0;

    int bonus = 0;
    bonus += 900;
    bonus += (3 - edgeDist) * 260;
    bonus += (14 - kingDist) * 55;
    bonus += (14 - queenDist) * 30;
    bonus += queenCuts * 160;
    bonus += weakInCheck * 140;

    uint64_t weakPawns = gs.bitboards[weak][P - 1];
    if (weakPawns) {
        const int pawnSq = lsbSquare64(weakPawns);
        const int pr = rowOfSq(pawnSq);
        const int pc = colOfSq(pawnSq);
        const int promotionDist = whiteHasQueen ? (7 - pr) : pr;
        // Opponent passed pawn near promotion reduces practical mating margin.
        bonus -= (7 - promotionDist) * 150;

        const int queenPawnDist = std::abs(qr - pr) + std::abs(qc - pc);
        bonus += (14 - queenPawnDist) * 25;
    }

    return whiteHasQueen ? bonus : -bonus;
}

static int evaluate(const GameState& gs)
{
    int score = 0;
    bool eg = isEndgame(gs);

    auto addPieces = [&](bool white, int type, const int table[8][8]) {
        int side = white ? 0 : 1;
        uint64_t bb = gs.bitboards[side][type - 1];
        while (bb) {
            const int sq = lsbSquare64(popLsb64(bb));
            const int r = rowOfSq(sq);
            const int c = colOfSq(sq);
            const int row = white ? r : 7 - r;
            int pst = table[row][c];
            if (type == R && eg) {
                pst += 10;
            }
            const int total = pieceValue(type) + pst;
            score += white ? total : -total;
        }
    };

    addPieces(true, P, pawnTable);
    addPieces(true, N, knightTable);
    addPieces(true, B, bishopTable);
    addPieces(true, R, rookTable);
    addPieces(true, Q, queenTable);
    addPieces(true, K, eg ? kingEndTable : kingMiddleTable);

    addPieces(false, P, pawnTable);
    addPieces(false, N, knightTable);
    addPieces(false, B, bishopTable);
    addPieces(false, R, rookTable);
    addPieces(false, Q, queenTable);
    addPieces(false, K, eg ? kingEndTable : kingMiddleTable);

    score += pawnStructureScore(gs);
    score += rookOpenFileBonus(gs);
    score += bishopPairBonus(gs);

    if (eg) {
        if (hasMatingMaterial(gs, true))  score += matingNetBonus(gs, true);
        if (hasMatingMaterial(gs, false)) score -= matingNetBonus(gs, false);

        if (isKRK(gs, true)) {
            score += krkBonus(gs, true);
        } else if (isKRK(gs, false)) {
            score += krkBonus(gs, false);
        } else if (isKBBK(gs, true)) {
            score += kbbkBonus(gs, true);
        } else if (isKBBK(gs, false)) {
            score += kbbkBonus(gs, false);
        } else if (isKBNK(gs, true)) {
            score += kbnkBonus(gs, true);
        } else if (isKBNK(gs, false)) {
            score += kbnkBonus(gs, false);
        } else if (isKQKFamily(gs, true)) {
            score += kqkFamilyBonus(gs, true);
        } else if (isKQKFamily(gs, false)) {
            score += kqkFamilyBonus(gs, false);
        }
    }

    return gs.whiteToMove ? score : -score;
}


static int scoreMoveForOrdering(const GameState& gs, const Move& m, int ply, const Move& ttMove)
{
    if (m.value == ttMove.value)
        return 2000000;

    if (m.isCapture()) {
        const int fromR = rowOfSq(m.from());
        const int fromC = colOfSq(m.from());
        int victim   = pieceValue(m.capturedType());
        int attacker = pieceValue(pieceAt(gs, fromR, fromC).type);
        return 1'000'000 + (victim * 10) - attacker;
    }

    if (m.isPromotion()) return 900000;

    for (int i = 0; i < MAX_KILLERS; i++)
        if (ply < MAX_PLY &&
            m.value == ss.killers[ply][i].value)
            return 800000 - i * 100;

    int side = gs.whiteToMove ? 0 : 1;
    int h = 0;
    if (m.from() < 64 && m.to() < 64)
        h = ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())];

    return h;
}

static void sortMoves(MoveList& moves, const GameState& gs, int ply,
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
    if (m.isCapture()) return;
    ss.killers[ply][1] = ss.killers[ply][0];
    ss.killers[ply][0] = m;
}

static void updateHistory(const GameState& gs, const Move& m, int depth)
{
    if (!m.isCapture() && m.from() < 64 && m.to() < 64)
    {
        int side = gs.whiteToMove ? 0 : 1;
        ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())] += depth * depth;
    }
}

static int quiescence(GameState& gs, int alpha, int beta, int ply)
{
    ss.nodes++;

    const bool inCheck = isInCheck(gs, gs.whiteToMove);

    if (inCheck) {
        MoveList evasions;
        generateLegalMoves(gs, evasions);
        if (evasions.empty()) {
            return -(MATE_SCORE - ply);
        }

        Move dummy = invalidMove();
        sortMoves(evasions, gs, ply, dummy);

        for (int i = 0; i < evasions.count; ++i) {
            Move& m = evasions.moves[i];
            makeMove(gs, m, false);
            int score = -quiescence(gs, -beta, -alpha, ply + 1);
            undoMove(gs, false);

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    int stand_pat = evaluate(gs);

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha)  alpha = stand_pat;

    MoveList moves;
    generateLegalMoves(gs, moves);

    Move dummy = invalidMove();
    sortMoves(moves, gs, 0, dummy);

    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        if (!m.isCapture() && !m.isPromotion())
            continue;

        makeMove(gs, m, false);
        int score = -quiescence(gs, -beta, -alpha, ply + 1);
        undoMove(gs, false);

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
    if (ply > 0 && isThreefoldInSearch(hash)) {
        return repetitionScore(gs);
    }
    if (gs.halfmoveClock >= 100 || !hasSufficientMaterial(gs)) {
        return DRAW_SCORE;
    }

    HashHistoryGuard historyGuard(hash);

    TTEntry* entry = tt.probe(hash);
    Move ttBestMove = invalidMove();

    if (entry && entry->depth >= depth && ply > 0) {
        int ttScore = scoreFromTT(entry->score, ply);
        if (entry->flag == TT_EXACT)              return ttScore;
        if (entry->flag == TT_LOWER && ttScore >= beta) return ttScore;
        if (entry->flag == TT_UPPER && ttScore <= alpha) return ttScore;
        ttBestMove = entry->bestMove;
    } else if (entry) {
        ttBestMove = entry->bestMove;
    }

    if (depth == 0) return quiescence(gs, alpha, beta, ply);

    MoveList moves;
    generateLegalMoves(gs, moves);

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
    Move bestMove = moves.moves[0];
    int  bestScore = -INF;
    int  moveCount = 0;

    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        moveCount++;
        makeMove(gs, m, false);

        int score;
        if (moveCount > 4 && depth >= 3 && !m.isCapture() && !m.isPromotion() && !isInCheck(gs, gs.whiteToMove)) {
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

        undoMove(gs, false);

        if (ss.stopped) return 0;

        if (score > bestScore) {
            bestScore = score;
            bestMove  = m;
        }

        if (score > alpha) {
            alpha = score;
            flag  = TT_EXACT;

            if (!m.isCapture())
                updateHistory(gs, m, depth);
        }

        if (alpha >= beta) {
            storeKiller(ply, m);
            tt.store(hash, scoreToTT(beta, ply), depth, TT_LOWER, m);
            return beta;
        }
    }

    tt.store(hash, scoreToTT(bestScore, ply), depth, flag, bestMove);
    return bestScore;
}

Move computeBestMove(GameState gs, int maxDepth, int timeLimitMs)
{
    MoveList moves;
    generateLegalMoves(gs, moves);
    if (moves.empty()) {
        lastSearchStats = SearchStats {};
        return invalidMove();
    }

    ss.clear();
    ss.startTime   = std::chrono::steady_clock::now();
    ss.timeLimitMs = timeLimitMs;
    ss.hashHistory[ss.hashCount++] = computeHash(gs);

    if (isEndgame(gs)) maxDepth += 6;
    if (isKQKFamily(gs, true) || isKQKFamily(gs, false)) maxDepth += 2;

    const int rootEval = evaluate(gs);

    Move bestMove = moves.moves[0];
    int  bestScore = -INF;
    int  depthReached = 0;

    for (int depth = 1; depth <= maxDepth; depth++) {
        int alpha = -INF, beta = INF;
        Move currentBest = moves.moves[0];
        int  currentBestScore = -INF;

        uint64_t hash = computeHash(gs);
        TTEntry* e = tt.probe(hash);
        Move ttMove = (e && e->bestMove.from() < 64)
            ? e->bestMove
            : invalidMove();
        sortMoves(moves, gs, 0, ttMove);

        for (int i = 0; i < moves.count; ++i) {
            Move& m = moves.moves[i];
            makeMove(gs, m, false);

            bool createsImmediateThreefold = false;
            const std::string childPos = boardToString(gs);
            auto posIt = gs.positionCounts.find(childPos);
            if (posIt != gs.positionCounts.end() && posIt->second >= 2) {
                createsImmediateThreefold = true;
            }

            int score = -negamax(gs, depth - 1, -beta, -alpha, 1, true);

            if (createsImmediateThreefold) {
                if (rootEval > 150) {
                    score -= 1200;
                } else if (rootEval > 40) {
                    score -= 500;
                } else if (rootEval < -150) {
                    score += 200;
                } else {
                    score -= 120;
                }
            }

            undoMove(gs, false);

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
        depthReached = depth;

        std::cout << "depth " << depth << "  score " << bestScore << "  nodes " << ss.nodes << "\n";
    }

done:
    {
        auto endTime = std::chrono::steady_clock::now();
        int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - ss.startTime).count());
        if (elapsedMs <= 0) {
            elapsedMs = 1;
        }
        lastSearchStats.nodes = ss.nodes;
        lastSearchStats.depthReached = depthReached;
        lastSearchStats.bestScore = bestScore;
        lastSearchStats.timeMs = elapsedMs;
        lastSearchStats.nps = static_cast<double>(ss.nodes) * 1000.0 / static_cast<double>(elapsedMs);
    }
    return bestMove;
}

Move computeBestMove(GameState gs, int depth)
{
    return computeBestMove(gs, depth, 600000);
}

SearchStats getLastSearchStats()
{
    return lastSearchStats;
}