#include "../include/engine.hpp"
#include "../include/chess.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

static constexpr int INF         =  1000000000;
static constexpr int MATE_SCORE  =  1000000;
static constexpr int DRAW_SCORE  =  0;
static constexpr int MAX_DEPTH   = 100;
static constexpr int DEFAULT_HASH_MB = 256;
static constexpr int QSEARCH_MAX_DEPTH = 40;
static constexpr int QSEARCH_CHECK_DEPTH = 4;

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

inline bool isValidMove(const Move& m)
{
    return m.value != 0xFFFFFFFFu;
}

static constexpr int MAX_PLY     = 64;   
static constexpr int MAX_KILLERS = 2; 
static constexpr int PV_MAX_PLY  = 256;

static std::atomic<bool> g_stopRequested { false };
static bool g_searchInfoOutputEnabled = true;
static EngineTuningParams g_tuningParams {};
static std::string g_syzygyPath;
static int g_syzygyProbeLimit = 6;

#if defined(__has_include)
#if __has_include("fathom/tbprobe.h")
#include "fathom/tbprobe.h"
#define HAS_FATHOM_TB 1
#else
#define HAS_FATHOM_TB 0
#endif
#else
#define HAS_FATHOM_TB 0
#endif

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
        case P: return 120;
        case N: return 340;
        case B: return 350;
        case R: return 550;
        case Q: return 980;
        case K: return 21000;
        default: return 0;
    }
}

static int sideHangingDanger(const GameState& gs, bool white)
{
    int danger = 0;

    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = pieceAtSq(gs, sq);
        if (p.type == EMPTY || p.type == K || p.white != white) {
            continue;
        }

        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        const bool attackedByEnemy = isSquareAttacked(gs, r, c, !white);
        if (!attackedByEnemy) {
            continue;
        }

        const bool defendedByOwn = isSquareAttacked(gs, r, c, white);

        int unit = 0;
        switch (p.type) {
            case P: unit = 8; break;
            case N: unit = 24; break;
            case B: unit = 28; break;
            case R: unit = 42; break;
            case Q: unit = 64; break;
            default: break;
        }

        danger += unit;
        if (!defendedByOwn) {
            danger += unit;
            if (p.type == B || p.type == Q) {
                danger += 12;
            }
        }
    }

    return danger;
}

static int hangingPieceScore(const GameState& gs)
{
    const int whiteDanger = sideHangingDanger(gs, true);
    const int blackDanger = sideHangingDanger(gs, false);
    return blackDanger - whiteDanger;
}

static bool isPassedPawnAt(const GameState& gs, int sq, bool white)
{
    const uint64_t enemyPawns = gs.bitboards[white ? 1 : 0][P - 1];
    const int r = rowOfSq(sq);
    const int c = colOfSq(sq);

    if (white) {
        for (int rr = r - 1; rr >= 0; --rr) {
            for (int cc = std::max(0, c - 1); cc <= std::min(7, c + 1); ++cc) {
                if (enemyPawns & (1ULL << (rr * 8 + cc))) {
                    return false;
                }
            }
        }
    } else {
        for (int rr = r + 1; rr < 8; ++rr) {
            for (int cc = std::max(0, c - 1); cc <= std::min(7, c + 1); ++cc) {
                if (enemyPawns & (1ULL << (rr * 8 + cc))) {
                    return false;
                }
            }
        }
    }

    return true;
}

static int sidePasserEndgamePressure(const GameState& gs, bool white)
{
    int pressure = 0;
    uint64_t pawns = gs.bitboards[white ? 0 : 1][P - 1];

    const int ownKingSq = lsbSquare64(gs.bitboards[white ? 0 : 1][K - 1]);
    const int enemyKingSq = lsbSquare64(gs.bitboards[white ? 1 : 0][K - 1]);
    const int ownKr = rowOfSq(ownKingSq);
    const int ownKc = colOfSq(ownKingSq);
    const int enemyKr = rowOfSq(enemyKingSq);
    const int enemyKc = colOfSq(enemyKingSq);

    while (pawns) {
        const int sq = lsbSquare64(popLsb64(pawns));
        if (!isPassedPawnAt(gs, sq, white)) {
            continue;
        }

        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        const int advance = white ? (7 - r) : r;
        if (advance < 2) {
            continue;
        }

        const int stepSq = white ? (sq - 8) : (sq + 8);
        const bool blockaded = (stepSq >= 0 && stepSq < 64) && (pieceAtSq(gs, stepSq).type != EMPTY);
        const bool defended = isSquareAttacked(gs, r, c, white);
        const bool attacked = isSquareAttacked(gs, r, c, !white);

        const int promoR = white ? 0 : 7;
        const int ownDist = std::abs(ownKr - promoR) + std::abs(ownKc - c);
        const int enemyDist = std::abs(enemyKr - promoR) + std::abs(enemyKc - c);
        const int kingRace = enemyDist - ownDist;

        int p = advance * advance * 7;
        p += std::max(0, (advance - 3) * 8);
        p += std::max(0, kingRace * 5);
        if (advance >= 5) {
            p += 28;
        }

        if (blockaded) {
            p -= 50 + advance * 8;
        }
        if (attacked && !defended) {
            p -= 40 + advance * 6;
        } else if (attacked) {
            p -= 14;
        } else if (defended) {
            p += 12;
        }

        pressure += std::max(0, p);
    }

    return pressure;
}

static int sideEndgamePawnPlanScore(const GameState& gs, bool white)
{
    int score = 0;
    const int side = white ? 0 : 1;
    const int enemy = side ^ 1;

    const int ownKingSq = lsbSquare64(gs.bitboards[side][K - 1]);
    const int enemyKingSq = lsbSquare64(gs.bitboards[enemy][K - 1]);
    const int ownKr = rowOfSq(ownKingSq);
    const int ownKc = colOfSq(ownKingSq);
    const int enemyKr = rowOfSq(enemyKingSq);
    const int enemyKc = colOfSq(enemyKingSq);

    uint64_t ownPawns = gs.bitboards[side][P - 1];
    while (ownPawns) {
        const int sq = lsbSquare64(popLsb64(ownPawns));
        if (!isPassedPawnAt(gs, sq, white)) {
            continue;
        }

        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        const int advance = white ? (7 - r) : r;
        if (advance < 2) {
            continue;
        }

        const int stepSq = white ? (sq - 8) : (sq + 8);
        const bool blockaded = (stepSq >= 0 && stepSq < 64) && (pieceAtSq(gs, stepSq).type != EMPTY);
        const int promoR = white ? 0 : 7;

        const int ownDist = std::abs(ownKr - promoR) + std::abs(ownKc - c);
        const int enemyDist = std::abs(enemyKr - promoR) + std::abs(enemyKc - c);
        const int kingRace = enemyDist - ownDist;

        int p = 22 + advance * advance * 10;
        p += std::max(0, kingRace * 7);
        if (!blockaded) {
            p += 24;
        } else {
            p -= 48;
        }

        // King support behind/alongside passer helps conversion.
        if ((white && ownKr <= r) || (!white && ownKr >= r)) {
            p += 14;
        }

        score += std::max(0, p);
    }

    uint64_t enemyPawns = gs.bitboards[enemy][P - 1];
    while (enemyPawns) {
        const int sq = lsbSquare64(popLsb64(enemyPawns));
        if (!isPassedPawnAt(gs, sq, !white)) {
            continue;
        }

        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        const int enemyAdvance = white ? r : (7 - r);
        if (enemyAdvance < 2) {
            continue;
        }

        const int enemyStepSq = white ? (sq + 8) : (sq - 8);
        const bool enemyBlockaded = (enemyStepSq >= 0 && enemyStepSq < 64) && (pieceAtSq(gs, enemyStepSq).type != EMPTY);
        const int enemyPromoR = white ? 7 : 0;

        int threat = 16 + enemyAdvance * enemyAdvance * 9;
        if (enemyBlockaded) {
            threat -= 44;
        }

        // Reward having king close enough to stop the passer.
        const int ownDist = std::abs(ownKr - enemyPromoR) + std::abs(ownKc - c);
        const int oppDist = std::abs(enemyKr - enemyPromoR) + std::abs(enemyKc - c);
        if (ownDist + 1 <= oppDist) {
            threat -= 26;
        }

        score -= std::max(0, threat);
    }

    return score;
}

uint64_t computeHash(const GameState& gs)
{
    return gs.zobristKey;
}


enum TTFlag : uint8_t { TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    uint64_t hash    = 0;
    int      score   = 0;
    int      depth   = -1;
    TTFlag   flag    = TT_EXACT;
    Move     bestMove = invalidMove();
};

struct TranspositionTable {
    std::vector<TTEntry> table;
    size_t mask = 0;
    int hashMb = DEFAULT_HASH_MB;

    TranspositionTable() {
        resizeMb(DEFAULT_HASH_MB);
    }

    void resizeMb(int mb) {
        mb = std::clamp(mb, 1, 2048);
        const size_t bytes = static_cast<size_t>(mb) * 1024ULL * 1024ULL;
        const size_t entryBytes = sizeof(TTEntry);
        size_t targetEntries = bytes / std::max<size_t>(1, entryBytes);

        size_t pow2 = 1;
        while ((pow2 << 1) <= targetEntries) {
            pow2 <<= 1;
        }
        if (pow2 < 1024) {
            pow2 = 1024;
        }

        table.assign(pow2, TTEntry{});
        mask = pow2 - 1;
        hashMb = mb;
    }

    TTEntry* probe(uint64_t hash) {
        TTEntry* e = &table[hash & mask];
        return (e->hash == hash) ? e : nullptr;
    }

    void store(uint64_t hash, int score, int depth, TTFlag flag, const Move& best) {
        TTEntry& e = table[hash & mask];
        // Always replace same-position entries when depth is not worse.
        if (e.hash == hash) {
            if (depth > e.depth || flag == TT_EXACT || e.bestMove.value == 0xFFFFFFFFu) {
                e = { hash, score, depth, flag, best };
            }
            return;
        }

        // On collisions, avoid overwriting much deeper exact entries.
        if (e.hash != 0 && e.depth >= depth + 2 && e.flag == TT_EXACT) {
            return;
        }

        e = { hash, score, depth, flag, best };
    }

    void clear() { std::fill(table.begin(), table.end(), TTEntry{}); }
} static tt;



struct SearchState {
    Move killers[MAX_PLY][MAX_KILLERS];
    Move counterMoves[2][64][64];
    std::array<int16_t, 2 * 64 * 64 * 64> continuation{};
    int  history[2][8][8][8][8]; 
    std::array<Move, 1024> pathMoves{};
    std::array<std::array<Move, PV_MAX_PLY>, PV_MAX_PLY> pvTable{};
    std::array<int, PV_MAX_PLY> pvLength{};
    std::array<uint64_t, 1024> hashHistory{};
    int hashCount = 0;
    int  nodes = 0;
    std::array<int, 1024> evalCoreNoKingStack{};
    int evalPly = 0;
    bool evalActive = false;
    bool stopped = false;
    std::chrono::steady_clock::time_point startTime;
    int  timeLimitMs = 5000;

    void clear() {
        nodes = 0;
        hashCount = 0;
        evalPly = 0;
        evalActive = false;
        stopped = false;
        for (auto& ply : killers)
            for (auto& k : ply)
                k = invalidMove();
        for (auto& side : counterMoves)
            for (auto& from : side)
                for (auto& to : from)
                    to = invalidMove();
        for (auto& m : pathMoves)
            m = invalidMove();
        for (auto& row : pvTable)
            for (auto& m : row)
                m = invalidMove();
        pvLength.fill(0);
        continuation.fill(0);
        for (auto& a : history)
            for (auto& b : a)
                for (auto& c : b)
                    for (auto& d : c)
                        for (auto& e : d)
                            e = 0;
    }

    bool timeUp() {
        if (g_stopRequested.load(std::memory_order_relaxed)) return true;
        if ((nodes & 4095) != 0) return false;
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count()
               >= timeLimitMs;
    }
} static ss;

static SearchStats lastSearchStats;

static int squareFromCoord(const std::string& s, int start)
{
    if (start + 1 >= static_cast<int>(s.size())) return -1;
    const int file = s[start] - 'a';
    const int rank = s[start + 1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    const int row = 7 - rank;
    return row * 8 + file;
}

static std::string coordFromSquare(int sq)
{
    const int row = rowOfSq(sq);
    const int col = colOfSq(sq);
    std::string out;
    out.push_back(static_cast<char>('a' + col));
    out.push_back(static_cast<char>('8' - row));
    return out;
}

static std::string moveToUciString(const Move& m)
{
    if (!isValidMove(m)) {
        return "0000";
    }

    std::string out = coordFromSquare(m.from()) + coordFromSquare(m.to());
    if (m.isPromotion()) {
        char p = 'q';
        if (m.promotionType() == N) p = 'n';
        else if (m.promotionType() == B) p = 'b';
        else if (m.promotionType() == R) p = 'r';
        out.push_back(p);
    }
    return out;
}

static bool moveMatchesUci(const Move& m, const std::string& uci)
{
    if (uci.size() < 4) return false;
    const int from = squareFromCoord(uci, 0);
    const int to = squareFromCoord(uci, 2);
    if (from < 0 || to < 0) return false;
    if (m.from() != from || m.to() != to) return false;

    if (uci.size() >= 5) {
        if (!m.isPromotion()) return false;
        char p = static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4])));
        int pt = Q;
        if (p == 'n') pt = N;
        else if (p == 'b') pt = B;
        else if (p == 'r') pt = R;
        else if (p == 'q') pt = Q;
        return m.promotionType() == pt;
    }

    return !m.isPromotion() || m.promotionType() == Q;
}

static bool sameMoveIdentity(const Move& a, const Move& b)
{
    if (a.from() != b.from() || a.to() != b.to()) {
        return false;
    }
    if (a.isPromotion() != b.isPromotion()) {
        return false;
    }
    if (a.isPromotion() && a.promotionType() != b.promotionType()) {
        return false;
    }
    return true;
}

static bool findLegalMoveByUci(GameState& gs, const std::string& uci, Move& out)
{
    MoveList legal;
    generateLegalMoves(gs, legal);
    for (int i = 0; i < legal.count; ++i) {
        if (moveMatchesUci(legal.moves[i], uci)) {
            out = legal.moves[i];
            return true;
        }
    }
    return false;
}

static void addBookLine(std::unordered_map<std::uint64_t, std::vector<Move>>& book,
                        const std::vector<std::string>& line)
{
    GameState gs;
    gs.initStandard();

    for (const std::string& uci : line) {
        Move m;
        if (!findLegalMoveByUci(gs, uci, m)) {
            break;
        }

        const std::uint64_t h = positionHash(gs);
        auto& options = book[h];
        bool exists = false;
        for (const Move& o : options) {
            if (sameMoveIdentity(o, m)) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            options.push_back(m);
        }

        makeMove(gs, m, false);
    }
}

static void loadBookLinesFromFile(std::unordered_map<std::uint64_t, std::vector<Move>>& book,
                                  const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::vector<std::string> moves;
        std::string mv;
        while (iss >> mv) {
            if (mv.size() < 4 || mv.size() > 5) {
                moves.clear();
                break;
            }
            moves.push_back(mv);
        }

        if (!moves.empty()) {
            addBookLine(book, moves);
        }
    }
}

static const std::unordered_map<std::uint64_t, std::vector<Move>>& openingBook()
{
    static const std::unordered_map<std::uint64_t, std::vector<Move>> book = [] {
        std::unordered_map<std::uint64_t, std::vector<Move>> b;

        // Open games (1.e4 e5)
        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","f1c4","g8f6","d2d3","f8c5"});
        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","f1b5","a7a6","b5a4","g8f6"});
        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","f1b5","g8f6","e1g1","f8e7"});
        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","f1c4","f8c5","c2c3","g8f6"});
        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","f1c4","f8c5","b2b4","c5b4"});
        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","d2d4","e5d4","f3d4","g8f6"});
        addBookLine(b, {"e2e4","e7e5","b1c3","g8f6","f2f4","d7d5","f4e5","f6e4"});
        addBookLine(b, {"e2e4","e7e5","f2f4","e5f4","g1f3","g7g5","h2h4","g5g4"});
        addBookLine(b, {"e2e4","e7e5","g1f3","g8f6","f3e5","d7d6","e5f3","f6e4"});
        addBookLine(b, {"e2e4","e7e5","g1f3","d7d6","d2d4","e5d4","f3d4","g8f6"});

        // Sicilian Defence
        addBookLine(b, {"e2e4","c7c5","g1f3","d7d6","d2d4","c5d4","f3d4","g8f6"});
        addBookLine(b, {"e2e4","c7c5","g1f3","d7d6","d2d4","c5d4","f3d4","b8c6"});
        addBookLine(b, {"e2e4","c7c5","g1f3","d7d6","d2d4","c5d4","f3d4","g8f6","b1c3","a7a6"});
        addBookLine(b, {"e2e4","c7c5","g1f3","b8c6","d2d4","c5d4","f3d4","g7g6"});
        addBookLine(b, {"e2e4","c7c5","c2c3","d7d5","e4d5","d8d5","d2d4","b8c6"});
        addBookLine(b, {"e2e4","c7c5","d2d4","c5d4","c2c3","d4c3","b1c3","b8c6"});

        // French Defence
        addBookLine(b, {"e2e4","e7e6","d2d4","d7d5","b1c3","g8f6","c1g5","f8e7"});
        addBookLine(b, {"e2e4","e7e6","d2d4","d7d5","e4e5","c7c5","c2c3","b8c6"});
        addBookLine(b, {"e2e4","e7e6","d2d4","d7d5","b1d2","c7c5","e4d5","e6d5"});

        // Caro-Kann
        addBookLine(b, {"e2e4","c7c6","d2d4","d7d5","b1c3","d5e4","c3e4","c8f5"});
        addBookLine(b, {"e2e4","c7c6","d2d4","d7d5","e4e5","c8f5","g1f3","e7e6"});

        // Scandinavian / Alekhine / Pirc / Modern
        addBookLine(b, {"e2e4","d7d5","e4d5","d8d5","b1c3","d5a5","d2d4","g8f6"});
        addBookLine(b, {"e2e4","g8f6","e4e5","f6d5","d2d4","d7d6","g1f3","c8g4"});
        addBookLine(b, {"e2e4","d7d6","d2d4","g8f6","b1c3","g7g6","g1f3","f8g7"});
        addBookLine(b, {"e2e4","g7g6","d2d4","f8g7","b1c3","d7d6","g1f3","a7a6"});

        // Queen pawn openings
        addBookLine(b, {"d2d4","d7d5","c2c4","e7e6","b1c3","g8f6","c1g5","f8e7"});
        addBookLine(b, {"d2d4","d7d5","c2c4","d5c4","g1f3","g8f6","e2e3","e7e6"});
        addBookLine(b, {"d2d4","d7d5","c2c4","c7c6","b1c3","g8f6","g1f3","d5c4"});
        addBookLine(b, {"d2d4","d7d5","g1f3","g8f6","c1f4","e7e6","e2e3","c7c5"});
        addBookLine(b, {"d2d4","d7d5","g1f3","g8f6","e2e3","e7e6","f1d3","c7c5"});

        // Indian defences
        addBookLine(b, {"d2d4","g8f6","c2c4","e7e6","g1f3","d7d5","b1c3","f8e7"});
        addBookLine(b, {"d2d4","g8f6","c2c4","e7e6","b1c3","f8b4","e2e3","e8g8"});
        addBookLine(b, {"d2d4","g8f6","c2c4","e7e6","g1f3","b7b6","g2g3","c8b7"});
        addBookLine(b, {"d2d4","g8f6","c2c4","g7g6","b1c3","f8g7","e2e4","d7d6"});
        addBookLine(b, {"d2d4","g8f6","c2c4","g7g6","b1c3","d7d5","c4d5","f6d5"});
        addBookLine(b, {"d2d4","g8f6","c2c4","c7c5","d4d5","e7e6","b1c3","e6d5"});

        // English / Reti / flank systems
        addBookLine(b, {"c2c4","e7e5","b1c3","g8f6","g2g3","d7d5","c4d5","f6d5"});
        addBookLine(b, {"c2c4","c7c5","b1c3","b8c6","g2g3","g7g6","f1g2","f8g7"});
        addBookLine(b, {"g1f3","d7d5","d2d4","g8f6","c2c4","e7e6","b1c3","f8e7"});
        addBookLine(b, {"g1f3","d7d5","c2c4","e7e6","g2g3","g8f6","f1g2","f8e7"});
        addBookLine(b, {"f2f4","d7d5","g1f3","g8f6","e2e3","g7g6","b2b3","f8g7"});

        // Keep a few sharp/forcing sidelines for variety.
        addBookLine(b, {"g1f3","d7d5","d2d4","g8f6","c2c4","e7e6","b1c3","f8e7"});
        addBookLine(b, {"e2e4","b8c6","d2d4","d7d5","b1c3","d5e4","d4d5","c6b8"});

        // Optional external book file for large libraries.
        loadBookLinesFromFile(b, "assets/opening_book_lines.txt");
        loadBookLinesFromFile(b, "assets/book_lines.txt");
        loadBookLinesFromFile(b, "opening_book_lines.txt");

        return b;
    }();

    return book;
}

struct ExperienceMoveStat {
    Move move = invalidMove();
    int sumScore = 0;
    int visits = 0;
};

static std::unordered_map<uint64_t, std::vector<ExperienceMoveStat>> g_experienceBook;
static bool g_experienceLoaded = false;
static std::mutex g_experienceMutex;
static constexpr int EXPERIENCE_MAX_VISITS = 30000;
static std::unordered_map<uint64_t, std::vector<ExperienceMoveStat>> g_pendingExperience;
static bool g_learningGameActive = true;
static bool g_experienceLearningEnabled = true;

static std::string experiencePath()
{
    return "assets/experience_book.txt";
}

static void loadExperienceBookIfNeeded()
{
    if (!g_experienceLearningEnabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_experienceMutex);
    if (g_experienceLoaded) {
        return;
    }

    std::ifstream in(experiencePath());
    if (!in.is_open()) {
        g_experienceLoaded = true;
        return;
    }

    uint64_t hash = 0;
    uint32_t moveValue = 0;
    int sumScore = 0;
    int visits = 0;
    while (in >> hash >> moveValue >> sumScore >> visits) {
        if (visits <= 0) {
            continue;
        }
        Move m;
        m.value = moveValue;
        if (!isValidMove(m)) {
            continue;
        }

        auto& vec = g_experienceBook[hash];
        bool merged = false;
        for (auto& e : vec) {
            if (sameMoveIdentity(e.move, m)) {
                e.sumScore = std::clamp(e.sumScore + sumScore, -50000000, 50000000);
                e.visits = std::min(EXPERIENCE_MAX_VISITS, e.visits + visits);
                merged = true;
                break;
            }
        }
        if (!merged) {
            ExperienceMoveStat e;
            e.move = m;
            e.sumScore = std::clamp(sumScore, -50000000, 50000000);
            e.visits = std::min(EXPERIENCE_MAX_VISITS, visits);
            vec.push_back(e);
        }
    }

    g_experienceLoaded = true;
}

static void saveExperienceBook()
{
    if (!g_experienceLearningEnabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_experienceMutex);
    std::ofstream out(experiencePath(), std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    for (const auto& [hash, vec] : g_experienceBook) {
        for (const auto& e : vec) {
            if (!isValidMove(e.move) || e.visits <= 0) {
                continue;
            }
            out << hash << ' ' << e.move.value << ' ' << e.sumScore << ' ' << e.visits << '\n';
        }
    }
}

static void updateExperienceBookEntry(uint64_t hash, const Move& move, int score)
{
    auto& vec = g_experienceBook[hash];
    for (auto& e : vec) {
        if (sameMoveIdentity(e.move, move)) {
            e.sumScore = std::clamp(e.sumScore + score, -50000000, 50000000);
            e.visits = std::min(EXPERIENCE_MAX_VISITS, e.visits + 1);
            return;
        }
    }

    ExperienceMoveStat e;
    e.move = move;
    e.sumScore = score;
    e.visits = 1;
    vec.push_back(e);
}

static void recordPendingExperience(uint64_t hash, const Move& move, int score)
{
    if (!g_experienceLearningEnabled || !g_learningGameActive || !isValidMove(move)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_experienceMutex);
    auto& vec = g_pendingExperience[hash];
    for (auto& e : vec) {
        if (sameMoveIdentity(e.move, move)) {
            e.sumScore = std::clamp(e.sumScore + score, -50000000, 50000000);
            e.visits = std::min(EXPERIENCE_MAX_VISITS, e.visits + 1);
            return;
        }
    }

    ExperienceMoveStat e;
    e.move = move;
    e.sumScore = score;
    e.visits = 1;
    vec.push_back(e);
}

static int experienceMoveBias(uint64_t hash, const Move& move)
{
    if (!g_experienceLearningEnabled) {
        return 0;
    }
    loadExperienceBookIfNeeded();
    std::lock_guard<std::mutex> lock(g_experienceMutex);

    auto it = g_experienceBook.find(hash);
    if (it == g_experienceBook.end()) {
        return 0;
    }

    for (const auto& e : it->second) {
        if (!sameMoveIdentity(e.move, move) || e.visits <= 0) {
            continue;
        }
        const int avg = e.sumScore / e.visits;
        const int conf = std::min(120, e.visits * 4);
        return std::clamp((avg * conf) / 160, -240, 240);
    }

    return 0;
}

static Move bookMoveForPosition(GameState& gs)
{
    const int ply = (gs.fullmoveNumber - 1) * 2 + (gs.whiteToMove ? 0 : 1);
    if (ply >= 20) {
        return invalidMove();
    }

    const auto& book = openingBook();
    const std::uint64_t h = positionHash(gs);
    auto it = book.find(h);
    if (it == book.end() || it->second.empty()) {
        return invalidMove();
    }

    MoveList legal;
    generateLegalMoves(gs, legal);
    std::vector<Move> candidates;
    candidates.reserve(it->second.size());
    for (const Move& bm : it->second) {
        for (int i = 0; i < legal.count; ++i) {
            if (sameMoveIdentity(legal.moves[i], bm)) {
                candidates.push_back(legal.moves[i]);
                break;
            }
        }
    }

    if (!candidates.empty()) {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        return candidates[dist(rng)];
    }

    return invalidMove();
}

struct PawnEvalEntry {
    uint64_t key = 0;
    int pawnStructure = 0;
    int rookOpenFile = 0;
};

static constexpr size_t PAWN_EVAL_SIZE = 1 << 19;

struct PawnEvalCache {
    std::vector<PawnEvalEntry> table;

    PawnEvalCache() : table(PAWN_EVAL_SIZE) {}

    static uint64_t makeKey(const GameState& gs)
    {
        uint64_t w = gs.bitboards[0][P - 1];
        uint64_t b = gs.bitboards[1][P - 1];

        // splitmix64-style mixing for a compact pawn signature.
        auto mix = [](uint64_t x) {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        };

        uint64_t key = mix(w) ^ (mix(b) << 1);
        if (key == 0) key = 1;
        return key;
    }

    PawnEvalEntry* probe(uint64_t key)
    {
        PawnEvalEntry* e = &table[key & (PAWN_EVAL_SIZE - 1)];
        return (e->key == key) ? e : nullptr;
    }

    void store(uint64_t key, int pawnStructure, int rookOpenFile)
    {
        PawnEvalEntry& e = table[key & (PAWN_EVAL_SIZE - 1)];
        e.key = key;
        e.pawnStructure = pawnStructure;
        e.rookOpenFile = rookOpenFile;
    }
} static pawnEvalCache;

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

static inline bool hasNonPawnMaterial(const GameState& gs, bool white)
{
    const int side = white ? 0 : 1;
    return (gs.bitboards[side][N - 1]
            | gs.bitboards[side][B - 1]
            | gs.bitboards[side][R - 1]
            | gs.bitboards[side][Q - 1]) != 0;
}

static int phaseDepthBonus(const GameState& gs)
{
    // Use non-pawn material as a cheap phase proxy.
    int nonPawnMaterial = 0;
    nonPawnMaterial += pieceValue(N) * (popcount64(gs.bitboards[0][N - 1]) + popcount64(gs.bitboards[1][N - 1]));
    nonPawnMaterial += pieceValue(B) * (popcount64(gs.bitboards[0][B - 1]) + popcount64(gs.bitboards[1][B - 1]));
    nonPawnMaterial += pieceValue(R) * (popcount64(gs.bitboards[0][R - 1]) + popcount64(gs.bitboards[1][R - 1]));
    nonPawnMaterial += pieceValue(Q) * (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]));

    // Opening -> middlegame -> middle-endgame -> endgame.
    if (nonPawnMaterial >= 5000) return 0;
    if (nonPawnMaterial >= 3000) return 1;
    if (nonPawnMaterial >= 2000) return 2;
    return 3;
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

static int pawnStructureScoreRaw(const GameState& gs)
{
    int score = 0;

    const uint64_t wPawns = gs.bitboards[0][P - 1];
    const uint64_t bPawns = gs.bitboards[1][P - 1];

    uint8_t wFiles = pawnFileMask(gs, true);
    uint8_t bFiles = pawnFileMask(gs, false);

    auto pawnIslands = [](uint8_t files) {
        int islands = 0;
        bool inIsland = false;
        for (int c = 0; c < 8; ++c) {
            const bool has = ((files >> c) & 1) != 0;
            if (has && !inIsland) {
                islands++;
                inIsland = true;
            } else if (!has) {
                inIsland = false;
            }
        }
        return islands;
    };

    const int whiteIslands = pawnIslands(wFiles);
    const int blackIslands = pawnIslands(bFiles);
    score -= std::max(0, whiteIslands - 1) * 8;
    score += std::max(0, blackIslands - 1) * 8;

    for (int c = 0; c < 8; c++) {
        bool wHas = (wFiles >> c) & 1;
        bool bHas = (bFiles >> c) & 1;
        uint64_t fileMask = 0x0101010101010101ULL << c;
        int wCount = popcount64(wPawns & fileMask);
        int bCount = popcount64(bPawns & fileMask);
        if (wCount > 1) score -= 24 * (wCount - 1);
        if (bCount > 1) score += 24 * (bCount - 1);

        bool wLeft  = (c > 0) && ((wFiles >> (c-1)) & 1);
        bool wRight = (c < 7) && ((wFiles >> (c+1)) & 1);
        if (wHas && !wLeft && !wRight) score -= 20;

        bool bLeft  = (c > 0) && ((bFiles >> (c-1)) & 1);
        bool bRight = (c < 7) && ((bFiles >> (c+1)) & 1);
        if (bHas && !bLeft && !bRight) score += 20;
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
            score += 8 + rank * rank * 2;
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
            score -= 8 + rank * rank * 2;
        }
    }

    return score;
}

static int loosePawnScore(const GameState& gs)
{
    auto sidePenalty = [&](bool white) {
        int penalty = 0;
        uint64_t pawns = gs.bitboards[white ? 0 : 1][P - 1];
        while (pawns) {
            const int sq = lsbSquare64(popLsb64(pawns));
            const int r = rowOfSq(sq);
            const int c = colOfSq(sq);
            const bool attacked = isSquareAttacked(gs, r, c, !white);
            if (!attacked) {
                continue;
            }
            const bool defended = isSquareAttacked(gs, r, c, white);
            if (!defended) {
                const int advance = white ? (7 - r) : r;
                penalty += 26 + advance * 3;
            }
        }
        return penalty;
    };

    const int whitePenalty = sidePenalty(true);
    const int blackPenalty = sidePenalty(false);
    return blackPenalty - whitePenalty;
}

static int rookOpenFileBonusRaw(const GameState& gs)
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

static void pawnEvalTerms(const GameState& gs, int& pawnStructure, int& rookOpenFile)
{
    const uint64_t key = PawnEvalCache::makeKey(gs);
    if (PawnEvalEntry* e = pawnEvalCache.probe(key)) {
        pawnStructure = e->pawnStructure;
        rookOpenFile = e->rookOpenFile;
        return;
    }

    pawnStructure = pawnStructureScoreRaw(gs);
    rookOpenFile = rookOpenFileBonusRaw(gs);
    pawnEvalCache.store(key, pawnStructure, rookOpenFile);
}

static int bishopPairBonus(const GameState& gs)
{
    const int wb = popcount64(gs.bitboards[0][B - 1]);
    const int bb = popcount64(gs.bitboards[1][B - 1]);
    return (wb >= 2 ? 30 : 0) - (bb >= 2 ? 30 : 0);
}

static int openingPrinciplesScore(const GameState& gs)
{
    // Opening guidance: faster development, center control, avoid premature queen moves.
    int score = 0;

    auto sideScore = [&](bool white) {
        int sideScore = 0;
        const int side = white ? 0 : 1;

        const int knightHome1 = white ? 57 : 1; // b1 / b8
        const int knightHome2 = white ? 62 : 6; // g1 / g8
        const int bishopHome1 = white ? 58 : 2; // c1 / c8
        const int bishopHome2 = white ? 61 : 5; // f1 / f8
        const int queenHome = white ? 59 : 3;   // d1 / d8

        int undevelopedMinor = 0;
        if (pieceAtSq(gs, knightHome1).type == N && pieceAtSq(gs, knightHome1).white == white) undevelopedMinor++;
        if (pieceAtSq(gs, knightHome2).type == N && pieceAtSq(gs, knightHome2).white == white) undevelopedMinor++;
        if (pieceAtSq(gs, bishopHome1).type == B && pieceAtSq(gs, bishopHome1).white == white) undevelopedMinor++;
        if (pieceAtSq(gs, bishopHome2).type == B && pieceAtSq(gs, bishopHome2).white == white) undevelopedMinor++;
        sideScore -= undevelopedMinor * 12;

        const Piece qHome = pieceAtSq(gs, queenHome);
        if (!(qHome.type == Q && qHome.white == white) && undevelopedMinor >= 2) {
            sideScore -= 20;
        }

        // Reward central pawn presence/advance (d/e files).
        const int dSq = white ? 51 : 11; // d2 / d7
        const int eSq = white ? 52 : 12; // e2 / e7
        const Piece dPawn = pieceAtSq(gs, dSq);
        const Piece ePawn = pieceAtSq(gs, eSq);
        if (!(dPawn.type == P && dPawn.white == white)) sideScore += 8;
        if (!(ePawn.type == P && ePawn.white == white)) sideScore += 8;

        // Early wing pawn pushes around own king often distort structure.
        const int fHome = white ? 53 : 13;
        const int gHome = white ? 54 : 14;
        const int hHome = white ? 55 : 15;
        const bool fMoved = !(pieceAtSq(gs, fHome).type == P && pieceAtSq(gs, fHome).white == white);
        const bool gMoved = !(pieceAtSq(gs, gHome).type == P && pieceAtSq(gs, gHome).white == white);
        const bool hMoved = !(pieceAtSq(gs, hHome).type == P && pieceAtSq(gs, hHome).white == white);
        if (undevelopedMinor >= 2) {
            if (fMoved) sideScore -= 8;
            if (gMoved) sideScore -= 10;
            if (hMoved) sideScore -= 8;
        }

        // Penalize king stuck in center once castling rights are gone.
        const int kingSq = lsbSquare64(gs.bitboards[side][K - 1]);
        const int kc = colOfSq(kingSq);
        const bool kingCentral = (kc >= 3 && kc <= 5);
        if (white) {
            if (kingCentral && gs.wkMoved && gs.wrAHMoved && gs.wrHHMoved) sideScore -= 15;
        } else {
            if (kingCentral && gs.bkMoved && gs.brAHMoved && gs.brHHMoved) sideScore -= 15;
        }

        return sideScore;
    };

    score += sideScore(true);
    score -= sideScore(false);
    return score;
}

static int lightweightKingSafetyScore(const GameState& gs, bool eg)
{
    if (eg) {
        return 0;
    }

    static constexpr uint64_t fileMasks[8] = {
        0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
        0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL
    };

    const uint64_t wPawns = gs.bitboards[0][P - 1];
    const uint64_t bPawns = gs.bitboards[1][P - 1];

    auto sidePenalty = [&](bool white) {
        const int side = white ? 0 : 1;
        const int kingSq = lsbSquare64(gs.bitboards[side][K - 1]);
        const int kr = rowOfSq(kingSq);
        const int kc = colOfSq(kingSq);
        const uint64_t ownPawns = white ? wPawns : bPawns;
        const uint64_t allPawns = wPawns | bPawns;

        int penalty = 0;

        for (int dc = -1; dc <= 1; ++dc) {
            const int c = kc + dc;
            if (c < 0 || c > 7) {
                continue;
            }

            const int front1r = white ? (kr - 1) : (kr + 1);
            const int front2r = white ? (kr - 2) : (kr + 2);

            bool shield1 = false;
            bool shield2 = false;

            if (front1r >= 0 && front1r < 8) {
                const int sq = front1r * 8 + c;
                shield1 = (ownPawns & (1ULL << sq)) != 0;
            }
            if (front2r >= 0 && front2r < 8) {
                const int sq = front2r * 8 + c;
                shield2 = (ownPawns & (1ULL << sq)) != 0;
            }

            if (!shield1) penalty += 14;
            if (!shield2) penalty += 8;

            const bool ownOnFile = (ownPawns & fileMasks[c]) != 0;
            const bool anyOnFile = (allPawns & fileMasks[c]) != 0;
            if (!ownOnFile) penalty += 10;
            if (!anyOnFile) penalty += 12;
        }

        // King exposed in center files is more vulnerable before simplification.
        if (kc >= 3 && kc <= 4) {
            penalty += 10;
        }

        return penalty;
    };

    const int whitePenalty = sidePenalty(true);
    const int blackPenalty = sidePenalty(false);
    return blackPenalty - whitePenalty;
}

static int pieceSquareTableScore(bool white, int type, int sq)
{
    const int r = rowOfSq(sq);
    const int c = colOfSq(sq);
    const int row = white ? r : 7 - r;

    switch (type) {
        case P: return pawnTable[row][c];
        case N: return knightTable[row][c];
        case B: return bishopTable[row][c];
        case R: return rookTable[row][c];
        case Q: return queenTable[row][c];
        default: return 0;
    }
}

static int gamePhase24(const GameState& gs)
{
    int phase = 0;
    phase += popcount64(gs.bitboards[0][N - 1]) + popcount64(gs.bitboards[1][N - 1]);
    phase += popcount64(gs.bitboards[0][B - 1]) + popcount64(gs.bitboards[1][B - 1]);
    phase += 2 * (popcount64(gs.bitboards[0][R - 1]) + popcount64(gs.bitboards[1][R - 1]));
    phase += 4 * (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]));
    return std::clamp(phase, 0, 24);
}

static int pieceContributionNoKing(bool white, int type, int sq)
{
    if (type == EMPTY || type == K) {
        return 0;
    }
    const int v = pieceValue(type) + pieceSquareTableScore(white, type, sq);
    return white ? v : -v;
}

static int computeCoreEvalNoKing(const GameState& gs)
{
    int score = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = pieceAtSq(gs, sq);
        if (p.type == EMPTY || p.type == K) {
            continue;
        }
        score += pieceContributionNoKing(p.white, p.type, sq);
    }
    return score;
}

static int kingPstMiddleScore(const GameState& gs)
{
    const int wKingSq = lsbSquare64(gs.bitboards[0][K - 1]);
    const int bKingSq = lsbSquare64(gs.bitboards[1][K - 1]);

    const int wr = rowOfSq(wKingSq), wc = colOfSq(wKingSq);
    const int br = rowOfSq(bKingSq), bc = colOfSq(bKingSq);
    const int wRow = wr;
    const int bRow = 7 - br;

    const int wPst = kingMiddleTable[wRow][wc];
    const int bPst = kingMiddleTable[bRow][bc];
    return wPst - bPst;
}

static int kingPstEndScore(const GameState& gs)
{
    const int wKingSq = lsbSquare64(gs.bitboards[0][K - 1]);
    const int bKingSq = lsbSquare64(gs.bitboards[1][K - 1]);

    const int wr = rowOfSq(wKingSq), wc = colOfSq(wKingSq);
    const int br = rowOfSq(bKingSq), bc = colOfSq(bKingSq);
    const int wRow = wr;
    const int bRow = 7 - br;

    const int wPst = kingEndTable[wRow][wc];
    const int bPst = kingEndTable[bRow][bc];
    return wPst - bPst;
}

static int mobilityScore(const GameState& gs)
{
    static constexpr int knightW = 4;
    static constexpr int bishopW = 5;
    static constexpr int rookW = 2;

    auto knightAttacks = [](int sq) {
        int attacks = 0;
        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        static constexpr int d[8][2] = {
            { -2, -1 }, { -2, 1 }, { -1, -2 }, { -1, 2 },
            { 1, -2 },  { 1, 2 },  { 2, -1 },  { 2, 1 },
        };
        for (const auto& o : d) {
            const int nr = r + o[0];
            const int nc = c + o[1];
            if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
                attacks++;
            }
        }
        return attacks;
    };

    auto sliderMobility = [&](int sq, int side, const int dirs[][2], int dirCount) {
        int count = 0;
        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        const uint64_t ownOcc = gs.occupancies[side];
        const uint64_t occ = gs.occupancyBoth;

        for (int i = 0; i < dirCount; ++i) {
            int nr = r + dirs[i][0];
            int nc = c + dirs[i][1];
            while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
                const int toSq = nr * 8 + nc;
                const uint64_t m = 1ULL << toSq;
                if (ownOcc & m) {
                    break;
                }
                count++;
                if (occ & m) {
                    break;
                }
                nr += dirs[i][0];
                nc += dirs[i][1];
            }
        }
        return count;
    };

    static constexpr int bishopDirs[4][2] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    static constexpr int rookDirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };

    int whiteMob = 0;
    int blackMob = 0;

    uint64_t wn = gs.bitboards[0][N - 1];
    while (wn) {
        const int sq = lsbSquare64(popLsb64(wn));
        whiteMob += knightAttacks(sq) * knightW;
    }
    uint64_t bn = gs.bitboards[1][N - 1];
    while (bn) {
        const int sq = lsbSquare64(popLsb64(bn));
        blackMob += knightAttacks(sq) * knightW;
    }

    uint64_t wb = gs.bitboards[0][B - 1];
    while (wb) {
        const int sq = lsbSquare64(popLsb64(wb));
        whiteMob += sliderMobility(sq, 0, bishopDirs, 4) * bishopW;
    }
    uint64_t bb = gs.bitboards[1][B - 1];
    while (bb) {
        const int sq = lsbSquare64(popLsb64(bb));
        blackMob += sliderMobility(sq, 1, bishopDirs, 4) * bishopW;
    }

    uint64_t wr = gs.bitboards[0][R - 1];
    while (wr) {
        const int sq = lsbSquare64(popLsb64(wr));
        whiteMob += sliderMobility(sq, 0, rookDirs, 4) * rookW;
    }
    uint64_t br = gs.bitboards[1][R - 1];
    while (br) {
        const int sq = lsbSquare64(popLsb64(br));
        blackMob += sliderMobility(sq, 1, rookDirs, 4) * rookW;
    }

    return whiteMob - blackMob;
}

static int rookEndgameOffset(const GameState& gs)
{
    return 10 * (popcount64(gs.bitboards[0][R - 1]) - popcount64(gs.bitboards[1][R - 1]));
}

static int computeMoveCoreDeltaNoKing(const GameState& gs, const Move& m)
{
    int delta = 0;
    const int fromSq = m.from();
    const int toSq = m.to();
    const int fromR = rowOfSq(fromSq);
    const int fromC = colOfSq(fromSq);
    const int toR = rowOfSq(toSq);
    const int toC = colOfSq(toSq);

    const Piece moving = pieceAtSq(gs, fromSq);
    if (moving.type != EMPTY && moving.type != K) {
        delta -= pieceContributionNoKing(moving.white, moving.type, fromSq);
        if (m.isPromotion()) {
            delta += pieceContributionNoKing(moving.white, m.promotionType(), toSq);
        } else {
            delta += pieceContributionNoKing(moving.white, moving.type, toSq);
        }
    }

    if (m.isEnPassant()) {
        const int capSq = fromR * 8 + toC;
        const Piece captured = pieceAtSq(gs, capSq);
        if (captured.type != EMPTY && captured.type != K) {
            delta -= pieceContributionNoKing(captured.white, captured.type, capSq);
        }
    } else {
        const Piece captured = pieceAtSq(gs, toSq);
        if (captured.type != EMPTY && captured.type != K) {
            delta -= pieceContributionNoKing(captured.white, captured.type, toSq);
        }
    }

    if (m.isCastle()) {
        if (toC == 6) {
            const int rookFrom = toR * 8 + 7;
            const int rookTo = toR * 8 + 5;
            delta -= pieceContributionNoKing(moving.white, R, rookFrom);
            delta += pieceContributionNoKing(moving.white, R, rookTo);
        } else {
            const int rookFrom = toR * 8 + 0;
            const int rookTo = toR * 8 + 3;
            delta -= pieceContributionNoKing(moving.white, R, rookFrom);
            delta += pieceContributionNoKing(moving.white, R, rookTo);
        }
    }

    (void)fromC;
    return delta;
}

static inline void searchMakeMove(GameState& gs, const Move& m)
{
    const bool hasRoom = (ss.evalPly + 1 < static_cast<int>(ss.evalCoreNoKingStack.size()));
    if (ss.evalActive && hasRoom) {
        const int delta = computeMoveCoreDeltaNoKing(gs, m);
        ss.evalCoreNoKingStack[ss.evalPly + 1] = ss.evalCoreNoKingStack[ss.evalPly] + delta;

        ++ss.evalPly;
    } else {
        ss.evalActive = false;
    }
    makeMove(gs, m, false);
}

static inline void searchUndoMove(GameState& gs)
{
    undoMove(gs, false);
    if (ss.evalPly > 0) {
        --ss.evalPly;
    }
}

struct NullMoveState {
    bool whiteToMove = true;
    std::optional<std::pair<int, int>> enPassantTarget;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
    std::uint64_t zobristKey = 0;
};

static inline NullMoveState doNullMove(GameState& gs)
{
    NullMoveState st;
    st.whiteToMove = gs.whiteToMove;
    st.enPassantTarget = gs.enPassantTarget;
    st.halfmoveClock = gs.halfmoveClock;
    st.fullmoveNumber = gs.fullmoveNumber;
    st.zobristKey = gs.zobristKey;

    gs.enPassantTarget = std::nullopt;
    gs.halfmoveClock++;
    if (!gs.whiteToMove) {
        gs.fullmoveNumber++;
    }
    gs.whiteToMove = !gs.whiteToMove;
    gs.zobristKey = recomputePositionHash(gs);
    return st;
}

static inline void undoNullMove(GameState& gs, const NullMoveState& st)
{
    gs.whiteToMove = st.whiteToMove;
    gs.enPassantTarget = st.enPassantTarget;
    gs.halfmoveClock = st.halfmoveClock;
    gs.fullmoveNumber = st.fullmoveNumber;
    gs.zobristKey = st.zobristKey;
}

static bool quietMoveMayGiveCheck(const GameState& gs, const Move& m, bool sideToMove)
{
    if (m.isCapture() || m.isPromotion()) {
        return false;
    }

    if (m.isCastle()) {
        return true;
    }

    const int enemy = sideToMove ? 1 : 0;
    const uint64_t enemyKingBB = gs.bitboards[enemy][K - 1];
    if (!enemyKingBB) {
        return true;
    }

    const int kingSq = lsbSquare64(enemyKingBB);
    const int kr = rowOfSq(kingSq);
    const int kc = colOfSq(kingSq);
    const int fromSq = m.from();
    const int toSq = m.to();
    const int fr = rowOfSq(fromSq);
    const int fc = colOfSq(fromSq);
    const int tr = rowOfSq(toSq);
    const int tc = colOfSq(toSq);

    const Piece mover = pieceAtSq(gs, fromSq);
    switch (mover.type) {
        case N: {
            const int dr = std::abs(tr - kr);
            const int dc = std::abs(tc - kc);
            if ((dr == 1 && dc == 2) || (dr == 2 && dc == 1)) {
                return true;
            }
            break;
        }
        case B:
            if (std::abs(tr - kr) == std::abs(tc - kc)) {
                return true;
            }
            break;
        case R:
            if (tr == kr || tc == kc) {
                return true;
            }
            break;
        case Q:
            if (tr == kr || tc == kc || std::abs(tr - kr) == std::abs(tc - kc)) {
                return true;
            }
            break;
        case K:
            if (std::max(std::abs(tr - kr), std::abs(tc - kc)) == 1) {
                return true;
            }
            break;
        default:
            break;
    }

    // Discovered checks are only possible if the moved piece vacates a line to the enemy king.
    if (fr == kr || fc == kc || std::abs(fr - kr) == std::abs(fc - kc)) {
        return true;
    }

    return false;
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

static int neuralAwarenessScore(const GameState& gs,
                                int baseScore,
                                int pawnStructure,
                                int rookOpenFile,
                                int bishopPair,
                                int mobility,
                                int kingSafety,
                                int openingScore,
                                int phase)
{
    // Tiny embedded MLP for global positional awareness.
    // Inputs are intentionally coarse to keep this very fast.
    std::array<int, 12> x{};
    x[0] = std::clamp(baseScore / 100, -64, 64);
    x[1] = std::clamp(pawnStructure / 30, -32, 32);
    x[2] = std::clamp(rookOpenFile / 20, -32, 32);
    x[3] = std::clamp(bishopPair / 30, -8, 8);
    x[4] = std::clamp(mobility / 25, -48, 48);
    x[5] = std::clamp(kingSafety / 20, -48, 48);
    x[6] = std::clamp(openingScore / 20, -32, 32);
    x[7] = std::clamp(phase - 12, -12, 12);
    x[8] = popcount64(gs.bitboards[0][Q - 1]) - popcount64(gs.bitboards[1][Q - 1]);
    x[9] = popcount64(gs.bitboards[0][R - 1]) - popcount64(gs.bitboards[1][R - 1]);
    x[10] = popcount64(gs.bitboards[0][B - 1]) - popcount64(gs.bitboards[1][B - 1]);
    x[11] = popcount64(gs.bitboards[0][N - 1]) - popcount64(gs.bitboards[1][N - 1]);

    static constexpr int H = 8;
    static constexpr std::array<std::array<int, 12>, H> W1 = {{
        {{ 7, 5, 2, 4, 3, 6, 2, -1, 4, 3, 2, 2 }},
        {{ 5, 2, 4, 1, 6, 3, 1, -2, 2, 4, 2, 3 }},
        {{ 3, 6, 1, 2, 2, 5, 4, 1, 1, 2, 3, 3 }},
        {{ 4, 1, 5, 2, 3, 2, 6, 2, 2, 3, 1, 1 }},
        {{ 6, 3, 2, 3, 5, 1, 1, -1, 3, 2, 2, 4 }},
        {{ 2, 4, 3, 2, 1, 6, 3, 1, 1, 2, 4, 2 }},
        {{ 5, 2, 2, 5, 2, 3, 2, -2, 4, 1, 2, 2 }},
        {{ 3, 5, 1, 2, 4, 2, 3, 0, 2, 3, 2, 1 }}
    }};
    static constexpr std::array<int, H> B1 = {{ 6, 4, 5, 3, 4, 5, 4, 3 }};
    static constexpr std::array<int, H> W2 = {{ 11, 9, 8, 8, 10, 7, 9, 8 }};
    static constexpr int B2 = 0;

    int out = B2;
    for (int i = 0; i < H; ++i) {
        int h = B1[i];
        for (int j = 0; j < static_cast<int>(x.size()); ++j) {
            h += W1[i][j] * x[j];
        }
        if (h < 0) {
            h = 0;
        }
        out += W2[i] * h;
    }

    // Keep this term bounded and well-behaved vs handcrafted eval.
    return std::clamp(out / 64, -120, 120);
}

struct NnueStyle {
    static constexpr int KING_BUCKETS = 8;
    static constexpr int PIECE_KIND = 10;
    static constexpr int FEATURES = KING_BUCKETS * PIECE_KIND * 64;
    static constexpr int HIDDEN = 48;

    std::array<std::array<int16_t, HIDDEN>, FEATURES> w1{};
    std::array<int16_t, HIDDEN> b1{};
    std::array<int16_t, HIDDEN> w2{};
    int16_t b2 = 0;

    NnueStyle()
    {
        std::mt19937 rng(0x4D4E4E55u);
        std::uniform_int_distribution<int> d1(-10, 10);
        std::uniform_int_distribution<int> d2(-24, 24);
        std::uniform_int_distribution<int> db(-16, 16);

        for (int f = 0; f < FEATURES; ++f) {
            for (int h = 0; h < HIDDEN; ++h) {
                w1[f][h] = static_cast<int16_t>(d1(rng));
            }
        }
        for (int h = 0; h < HIDDEN; ++h) {
            b1[h] = static_cast<int16_t>(db(rng));
            w2[h] = static_cast<int16_t>(d2(rng));
        }
        b2 = static_cast<int16_t>(db(rng));
    }

    static int kingBucket(int sq)
    {
        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        return (r / 2) * 2 + (c / 4);
    }

    static int orientSquare(int sq, bool perspectiveWhite)
    {
        if (perspectiveWhite) return sq;
        const int r = rowOfSq(sq);
        const int c = colOfSq(sq);
        return (7 - r) * 8 + c;
    }

    static int pieceKindIndex(const Piece& p, bool perspectiveWhite)
    {
        if (p.type == EMPTY || p.type == K) return -1;
        const bool own = (p.white == perspectiveWhite);
        const int base = own ? 0 : 5;
        switch (p.type) {
            case P: return base + 0;
            case N: return base + 1;
            case B: return base + 2;
            case R: return base + 3;
            case Q: return base + 4;
            default: return -1;
        }
    }

    int perspectiveScore(const GameState& gs, bool perspectiveWhite) const
    {
        const int side = perspectiveWhite ? 0 : 1;
        const uint64_t kingBb = gs.bitboards[side][K - 1];
        if (!kingBb) return 0;

        const int ksq = lsbSquare64(kingBb);
        const int bucket = kingBucket(orientSquare(ksq, perspectiveWhite));

        std::array<int, HIDDEN> acc{};
        for (int h = 0; h < HIDDEN; ++h) {
            acc[h] = b1[h];
        }

        for (int sq = 0; sq < 64; ++sq) {
            const Piece p = pieceAtSq(gs, sq);
            const int kind = pieceKindIndex(p, perspectiveWhite);
            if (kind < 0) continue;

            const int osq = orientSquare(sq, perspectiveWhite);
            const int feat = ((bucket * PIECE_KIND + kind) * 64) + osq;
            for (int h = 0; h < HIDDEN; ++h) {
                acc[h] += w1[feat][h];
            }
        }

        int out = b2;
        for (int h = 0; h < HIDDEN; ++h) {
            const int a = std::clamp(acc[h], 0, 127);
            out += (a * w2[h]);
        }

        return out / 32;
    }

    int evaluate(const GameState& gs) const
    {
        const int white = perspectiveScore(gs, true);
        const int black = perspectiveScore(gs, false);
        return std::clamp(white - black, -280, 280);
    }
};

static const NnueStyle g_nnueStyle;

static int evaluate(const GameState& gs)
{
    int baseScore = ss.evalActive ? ss.evalCoreNoKingStack[ss.evalPly] : computeCoreEvalNoKing(gs);
    bool eg = isEndgame(gs);
    const int phase = gamePhase24(gs);

    int mgScore = baseScore;
    int egScore = baseScore;

    mgScore += kingPstMiddleScore(gs);
    egScore += kingPstEndScore(gs);

    int pawnStructure = 0;
    int rookOpenFile = 0;
    pawnEvalTerms(gs, pawnStructure, rookOpenFile);
    mgScore += pawnStructure;
    egScore += pawnStructure;
    mgScore += rookOpenFile;
    egScore += rookOpenFile;

    const int bishopPair = bishopPairBonus(gs);
    mgScore += bishopPair;
    egScore += bishopPair;

    const int hanging = hangingPieceScore(gs);
    mgScore += hanging;
    egScore += hanging / 2;

    const int loosePawns = loosePawnScore(gs);
    mgScore += loosePawns;
    egScore += loosePawns / 2;

    const int whitePasserPressure = sidePasserEndgamePressure(gs, true);
    const int blackPasserPressure = sidePasserEndgamePressure(gs, false);
    const int passerPressure = whitePasserPressure - blackPasserPressure;
    mgScore += passerPressure / 6;
    egScore += (passerPressure * 3) / 4;

    if (eg) {
        const int whitePawnPlan = sideEndgamePawnPlanScore(gs, true);
        const int blackPawnPlan = sideEndgamePawnPlanScore(gs, false);
        const int pawnPlan = whitePawnPlan - blackPawnPlan;

        // Increase pawn-plan priority as we approach very low non-pawn material.
        const int veryEgWeight = std::max(0, 10 - phase);
        egScore += pawnPlan + (pawnPlan * veryEgWeight) / 6;
    }

    const int mob = mobilityScore(gs);
    mgScore += mob;
    egScore += mob / 2;

    const int kingSafety = lightweightKingSafetyScore(gs, false);
    mgScore += kingSafety;

    int openingScore = 0;
    if (!eg) {
        openingScore = openingPrinciplesScore(gs);
        mgScore += openingScore;
    }

     const int awareness = neuralAwarenessScore(gs,
                                                              baseScore,
                                                              pawnStructure,
                                                              rookOpenFile,
                                                              bishopPair,
                                                              mob,
                                                              kingSafety,
                                                              openingScore,
                                                              phase);
    mgScore += awareness;
    egScore += awareness / 2;

    const int nnue = g_nnueStyle.evaluate(gs);
    mgScore += (nnue * g_tuningParams.nnueMgWeight) / std::max(1, g_tuningParams.nnueWeightDiv);
    egScore += (nnue * g_tuningParams.nnueEgWeight) / std::max(1, g_tuningParams.nnueWeightDiv);

    egScore += rookEndgameOffset(gs);

    if (eg) {
        if (hasMatingMaterial(gs, true))  egScore += matingNetBonus(gs, true);
        if (hasMatingMaterial(gs, false)) egScore -= matingNetBonus(gs, false);
        if (isKRK(gs, true)) {
            egScore += krkBonus(gs, true);
        } else if (isKRK(gs, false)) {
            egScore += krkBonus(gs, false);
        } else if (isKBBK(gs, true)) {
            egScore += kbbkBonus(gs, true);
        } else if (isKBBK(gs, false)) {
            egScore += kbbkBonus(gs, false);
        } else if (isKBNK(gs, true)) {
            egScore += kbnkBonus(gs, true);
        } else if (isKBNK(gs, false)) {
            egScore += kbnkBonus(gs, false);
        } else if (isKQKFamily(gs, true)) {
            egScore += kqkFamilyBonus(gs, true);
        } else if (isKQKFamily(gs, false)) {
            egScore += kqkFamilyBonus(gs, false);
        }
    }

    int score = (mgScore * phase + egScore * (24 - phase)) / 24;

    return gs.whiteToMove ? score : -score;
}

static int staticExchangeEval(const GameState& gs, const Move& m);

static bool probeSyzygyWdl(const GameState& gs, int ply, int& score)
{
    const int pieceCount = popcount64(gs.occupancyBoth);
    if (pieceCount > g_syzygyProbeLimit || g_syzygyPath.empty()) {
        return false;
    }

#if HAS_FATHOM_TB
    // Adapter placeholder: this engine uses custom state and still needs
    // explicit mapping from GameState to Fathom's probe input format.
    (void)gs;
    (void)ply;
    (void)score;
    return false;
#else
    (void)gs;
    (void)ply;
    (void)score;
    return false;
#endif
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
        int see = 0;
        if (!m.isEnPassant() && ply <= 6) {
            see = std::clamp(staticExchangeEval(gs, m), -300, 300);
        }
        return 1'000'000 + (victim * 10) - attacker + (see * 3);
    }

    if (m.isPromotion()) return 900000;

    for (int i = 0; i < MAX_KILLERS; i++)
        if (ply < MAX_PLY &&
            m.value == ss.killers[ply][i].value)
            return 800000 - i * 100;

    if (ply > 0 && ply < static_cast<int>(ss.pathMoves.size())) {
        const Move& prev = ss.pathMoves[ply - 1];
        if (isValidMove(prev) && prev.from() < 64 && prev.to() < 64) {
            const int side = gs.whiteToMove ? 0 : 1;
            if (m.value == ss.counterMoves[side][prev.from()][prev.to()].value) {
                return 780000;
            }
        }
    }

    int side = gs.whiteToMove ? 0 : 1;
    int h = 0;
    if (m.from() < 64 && m.to() < 64)
        h = ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())];

    if (!m.isCapture() && !m.isPromotion() && ply > 0 && ply < static_cast<int>(ss.pathMoves.size())) {
        const Move& prev = ss.pathMoves[ply - 1];
        if (isValidMove(prev) && prev.from() < 64 && prev.to() < 64 && m.to() < 64) {
            const size_t idx = static_cast<size_t>(((side * 64 + prev.from()) * 64 + prev.to()) * 64 + m.to());
            h += static_cast<int>(ss.continuation[idx]) * 2;
        }
    }

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

static inline void clampHistory(int& v)
{
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
}

static void updateContinuationBonus(const GameState& gs, int ply, const Move& m, int depth)
{
    if (ply <= 0 || ply >= static_cast<int>(ss.pathMoves.size())) return;
    if (m.isCapture() || m.isPromotion() || m.from() >= 64 || m.to() >= 64) return;

    const Move& prev = ss.pathMoves[ply - 1];
    if (!isValidMove(prev) || prev.from() >= 64 || prev.to() >= 64) return;

    const int side = gs.whiteToMove ? 0 : 1;
    const size_t idx = static_cast<size_t>(((side * 64 + prev.from()) * 64 + prev.to()) * 64 + m.to());
    int hv = static_cast<int>(ss.continuation[idx]);
    const int bonus = std::min(1200, depth * depth * 5);
    hv += bonus - (hv * bonus) / 32768;
    hv = std::clamp(hv, -32767, 32767);
    ss.continuation[idx] = static_cast<int16_t>(hv);
}

static void updateContinuationMalus(const GameState& gs, int ply, const Move& m, int depth)
{
    if (ply <= 0 || ply >= static_cast<int>(ss.pathMoves.size())) return;
    if (m.isCapture() || m.isPromotion() || m.from() >= 64 || m.to() >= 64) return;

    const Move& prev = ss.pathMoves[ply - 1];
    if (!isValidMove(prev) || prev.from() >= 64 || prev.to() >= 64) return;

    const int side = gs.whiteToMove ? 0 : 1;
    const size_t idx = static_cast<size_t>(((side * 64 + prev.from()) * 64 + prev.to()) * 64 + m.to());
    int hv = static_cast<int>(ss.continuation[idx]);
    const int malus = std::min(1000, depth * depth * 4);
    hv -= malus + (hv * malus) / 32768;
    hv = std::clamp(hv, -32767, 32767);
    ss.continuation[idx] = static_cast<int16_t>(hv);
}

static void updateHistoryBonus(const GameState& gs, const Move& m, int depth)
{
    if (m.isCapture() || m.from() >= 64 || m.to() >= 64) return;
    const int side = gs.whiteToMove ? 0 : 1;
    int& h = ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())];
    const int bonus = std::min(1600, depth * depth * 8);
    h += bonus - (h * bonus) / 32768;
    clampHistory(h);
}

static void updateHistoryMalus(const GameState& gs, const Move& m, int depth)
{
    if (m.isCapture() || m.from() >= 64 || m.to() >= 64) return;
    const int side = gs.whiteToMove ? 0 : 1;
    int& h = ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())];
    const int malus = std::min(1400, depth * depth * 6);
    h -= malus + (h * malus) / 32768;
    clampHistory(h);
}

static void updateCounterMove(const GameState& gs, int ply, const Move& reply)
{
    if (ply <= 0 || ply >= static_cast<int>(ss.pathMoves.size())) {
        return;
    }
    const Move& prev = ss.pathMoves[ply - 1];
    if (!isValidMove(prev) || prev.from() >= 64 || prev.to() >= 64) {
        return;
    }
    const int side = gs.whiteToMove ? 0 : 1;
    ss.counterMoves[side][prev.from()][prev.to()] = reply;
}

static void generateTacticalLegalMoves(GameState& gs, MoveList& out)
{
    out.clear();

    MoveList pseudo;
    generatePseudoLegalMoves(gs, pseudo);
    const bool sideToCheck = gs.whiteToMove;

    for (int i = 0; i < pseudo.count; ++i) {
        const Move& m = pseudo.moves[i];
        if (!m.isCapture() && !m.isPromotion()) {
            continue;
        }

        makeMove(gs, m, false);
        const bool legal = !isInCheck(gs, sideToCheck);
        undoMove(gs, false);

        if (legal && out.count < static_cast<int>(out.moves.size())) {
            out.moves[out.count++] = m;
        }
    }
}

static void generateQuietCheckingMoves(GameState& gs, MoveList& out)
{
    out.clear();

    MoveList pseudo;
    generatePseudoLegalMoves(gs, pseudo);
    const bool sideToCheck = gs.whiteToMove;

    for (int i = 0; i < pseudo.count; ++i) {
        const Move& m = pseudo.moves[i];
        if (m.isCapture() || m.isPromotion()) {
            continue;
        }
        if (!quietMoveMayGiveCheck(gs, m, sideToCheck)) {
            continue;
        }

        makeMove(gs, m, false);
        const bool legal = !isInCheck(gs, sideToCheck);
        const bool givesCheck = legal && isInCheck(gs, gs.whiteToMove);
        undoMove(gs, false);

        if (givesCheck && out.count < static_cast<int>(out.moves.size())) {
            out.moves[out.count++] = m;
        }
    }
}

static inline uint64_t bitAtSq64(int sq)
{
    return 1ULL << sq;
}

static uint64_t attackersToSquare(const std::array<std::array<uint64_t, 6>, 2>& pieces,
                                  uint64_t occupancy,
                                  int targetSq,
                                  int side)
{
    uint64_t attackers = 0;
    const int tr = rowOfSq(targetSq);
    const int tc = colOfSq(targetSq);

    // Pawn attackers depend on side to move toward the target square.
    if (side == 0) {
        if (tr < 7 && tc > 0) {
            const int sq = targetSq + 7;
            if (pieces[0][P - 1] & bitAtSq64(sq)) attackers |= bitAtSq64(sq);
        }
        if (tr < 7 && tc < 7) {
            const int sq = targetSq + 9;
            if (pieces[0][P - 1] & bitAtSq64(sq)) attackers |= bitAtSq64(sq);
        }
    } else {
        if (tr > 0 && tc > 0) {
            const int sq = targetSq - 9;
            if (pieces[1][P - 1] & bitAtSq64(sq)) attackers |= bitAtSq64(sq);
        }
        if (tr > 0 && tc < 7) {
            const int sq = targetSq - 7;
            if (pieces[1][P - 1] & bitAtSq64(sq)) attackers |= bitAtSq64(sq);
        }
    }

    static constexpr int knightOffsets[8][2] = {
        { -2, -1 }, { -2, 1 }, { -1, -2 }, { -1, 2 },
        { 1, -2 },  { 1, 2 },  { 2, -1 },  { 2, 1 },
    };
    for (const auto& d : knightOffsets) {
        const int nr = tr + d[0];
        const int nc = tc + d[1];
        if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
        const int sq = nr * 8 + nc;
        const uint64_t m = bitAtSq64(sq);
        if (pieces[side][N - 1] & m) attackers |= m;
    }

    static constexpr int kingOffsets[8][2] = {
        { -1, -1 }, { -1, 0 }, { -1, 1 },
        { 0, -1 },              { 0, 1 },
        { 1, -1 },  { 1, 0 },   { 1, 1 },
    };
    for (const auto& d : kingOffsets) {
        const int nr = tr + d[0];
        const int nc = tc + d[1];
        if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
        const int sq = nr * 8 + nc;
        const uint64_t m = bitAtSq64(sq);
        if (pieces[side][K - 1] & m) attackers |= m;
    }

    static constexpr int diag[4][2] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    for (const auto& d : diag) {
        int nr = tr + d[0];
        int nc = tc + d[1];
        while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
            const int sq = nr * 8 + nc;
            const uint64_t m = bitAtSq64(sq);
            if (occupancy & m) {
                if ((pieces[side][B - 1] & m) || (pieces[side][Q - 1] & m)) {
                    attackers |= m;
                }
                break;
            }
            nr += d[0];
            nc += d[1];
        }
    }

    static constexpr int ortho[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (const auto& d : ortho) {
        int nr = tr + d[0];
        int nc = tc + d[1];
        while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
            const int sq = nr * 8 + nc;
            const uint64_t m = bitAtSq64(sq);
            if (occupancy & m) {
                if ((pieces[side][R - 1] & m) || (pieces[side][Q - 1] & m)) {
                    attackers |= m;
                }
                break;
            }
            nr += d[0];
            nc += d[1];
        }
    }

    return attackers;
}

static int leastValuableAttackerSquare(const std::array<std::array<uint64_t, 6>, 2>& pieces,
                                       int side,
                                       uint64_t attackers,
                                       int& pieceType)
{
    static constexpr int order[6] = { P, N, B, R, Q, K };
    for (int pt : order) {
        uint64_t bb = attackers & pieces[side][pt - 1];
        if (bb) {
            pieceType = pt;
            return lsbSquare64(bb);
        }
    }
    pieceType = EMPTY;
    return -1;
}

static int staticExchangeEval(const GameState& gs, const Move& m)
{
    if (!m.isCapture() || m.isEnPassant()) {
        return 0;
    }

    const int fromSq = m.from();
    const int toSq = m.to();
    const uint64_t fromMask = bitAtSq64(fromSq);
    const uint64_t toMask = bitAtSq64(toSq);

    const int us = gs.whiteToMove ? 0 : 1;
    const int them = us ^ 1;
    const Piece movingPiece = pieceAtSq(gs, fromSq);
    if (movingPiece.type == EMPTY) {
        return 0;
    }

    int capturedType = m.capturedType();
    if (capturedType == EMPTY) {
        capturedType = pieceAtSq(gs, toSq).type;
    }
    if (capturedType == EMPTY) {
        return 0;
    }

    std::array<std::array<uint64_t, 6>, 2> pieces = gs.bitboards;
    uint64_t occupancy = gs.occupancyBoth;

    pieces[us][movingPiece.type - 1] ^= fromMask;
    pieces[us][m.isPromotion() ? (m.promotionType() - 1) : (movingPiece.type - 1)] |= toMask;
    pieces[them][capturedType - 1] ^= toMask;
    occupancy ^= fromMask;

    int gain[32];
    int depth = 0;
    gain[0] = pieceValue(capturedType);

    int side = them;
    int capturedOnTarget = m.isPromotion() ? m.promotionType() : movingPiece.type;

    while (depth < 30) {
        const uint64_t attackers = attackersToSquare(pieces, occupancy, toSq, side);
        int attackerType = EMPTY;
        const int attackerSq = leastValuableAttackerSquare(pieces, side, attackers, attackerType);
        if (attackerSq < 0) {
            break;
        }

        const uint64_t attackerMask = bitAtSq64(attackerSq);
        const int other = side ^ 1;

        ++depth;
        gain[depth] = pieceValue(capturedOnTarget) - gain[depth - 1];

        pieces[side][attackerType - 1] ^= attackerMask;
        occupancy ^= attackerMask;
        pieces[other][capturedOnTarget - 1] ^= toMask;
        pieces[side][attackerType - 1] |= toMask;

        capturedOnTarget = attackerType;
        side = other;
    }

    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }

    return gain[0];
}

static int quiescence(GameState& gs, int alpha, int beta, int ply, int qDepth)
{
    ss.nodes++;

    int tbScore = 0;
    if (probeSyzygyWdl(gs, ply, tbScore)) {
        return tbScore;
    }

    const int alphaOrig = alpha;
    const uint64_t hash = computeHash(gs);
    TTEntry* entry = tt.probe(hash);
    Move ttBestMove = invalidMove();
    if (entry) {
        ttBestMove = entry->bestMove;
        if (entry->depth <= 0) {
            const int ttScore = scoreFromTT(entry->score, ply);
            if (entry->flag == TT_EXACT) {
                return ttScore;
            }
            if (entry->flag == TT_LOWER && ttScore >= beta) {
                return ttScore;
            }
            if (entry->flag == TT_UPPER && ttScore <= alpha) {
                return ttScore;
            }
        }
    }

    const bool inCheck = isInCheck(gs, gs.whiteToMove);

    if (qDepth >= QSEARCH_MAX_DEPTH || ply >= 254) {
        return evaluate(gs);
    }

    if (inCheck) {
        MoveList evasions;
        generateLegalMoves(gs, evasions);
        if (evasions.empty()) {
            return -(MATE_SCORE - ply);
        }

        sortMoves(evasions, gs, ply, ttBestMove);

        for (int i = 0; i < evasions.count; ++i) {
            Move& m = evasions.moves[i];
            searchMakeMove(gs, m);
            int score = -quiescence(gs, -beta, -alpha, ply + 1, qDepth + 1);
            searchUndoMove(gs);

            if (score >= beta) {
                tt.store(hash, scoreToTT(beta, ply), 0, TT_LOWER, m);
                return beta;
            }
            if (score > alpha) alpha = score;
        }

        const TTFlag flag = (alpha <= alphaOrig) ? TT_UPPER : TT_EXACT;
        tt.store(hash, scoreToTT(alpha, ply), 0, flag, invalidMove());
        return alpha;
    }

    int stand_pat = evaluate(gs);

    if (stand_pat >= beta) {
        tt.store(hash, scoreToTT(beta, ply), 0, TT_LOWER, invalidMove());
        return beta;
    }
    if (stand_pat > alpha)  alpha = stand_pat;

    MoveList moves;
    generateTacticalLegalMoves(gs, moves);

    sortMoves(moves, gs, ply, ttBestMove);

    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        if (m.isCapture()) {
            int captured = pieceValue(m.capturedType());
            if (captured == 0 && !m.isEnPassant()) {
                captured = pieceValue(pieceAtSq(gs, m.to()).type);
            }

            int promotionGain = 0;
            if (m.isPromotion()) {
                promotionGain = pieceValue(m.promotionType()) - pieceValue(P);
            }

            // Strength-first delta pruning margin to avoid dropping critical tactics.
            if (stand_pat + captured + promotionGain + g_tuningParams.qsearchDeltaMargin <= alpha) {
                continue;
            }
        }

        if (m.isCapture() && !m.isEnPassant() && staticExchangeEval(gs, m) < -g_tuningParams.qsearchSeeThreshold) {
            continue;
        }

        searchMakeMove(gs, m);
        int score = -quiescence(gs, -beta, -alpha, ply + 1, qDepth + 1);
        searchUndoMove(gs);

        if (score >= beta) {
            tt.store(hash, scoreToTT(beta, ply), 0, TT_LOWER, m);
            return beta;
        }
        if (score > alpha)  alpha = score;
    }

    if (qDepth < QSEARCH_CHECK_DEPTH) {
        MoveList checks;
        generateQuietCheckingMoves(gs, checks);
        sortMoves(checks, gs, ply, ttBestMove);

        for (int i = 0; i < checks.count; ++i) {
            Move& m = checks.moves[i];

            searchMakeMove(gs, m);
            int score = -quiescence(gs, -beta, -alpha, ply + 1, qDepth + 1);
            searchUndoMove(gs);

            if (score >= beta) {
                tt.store(hash, scoreToTT(beta, ply), 0, TT_LOWER, m);
                return beta;
            }
            if (score > alpha) {
                alpha = score;
            }
        }
    }

    const TTFlag flag = (alpha <= alphaOrig) ? TT_UPPER : TT_EXACT;
    tt.store(hash, scoreToTT(alpha, ply), 0, flag, invalidMove());
    return alpha;
}

static int negamax(GameState& gs, int depth, int alpha, int beta, int ply,
                   bool nullMoveAllowed, const Move* excludedMove = nullptr)
{
    if (ss.timeUp()) { ss.stopped = true; return 0; }

    ss.nodes++;

    int tbScore = 0;
    if (probeSyzygyWdl(gs, ply, tbScore)) {
        return tbScore;
    }

    if (ply < PV_MAX_PLY) {
        ss.pvLength[ply] = ply;
    }

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
    const bool pvNode = (beta - alpha) > 1;

    // Mate distance pruning keeps mate scores consistent and narrows windows.
    alpha = std::max(alpha, -MATE_SCORE + ply);
    beta = std::min(beta, MATE_SCORE - ply - 1);
    if (alpha >= beta) {
        return alpha;
    }

    if (entry && entry->depth >= depth && ply > 0) {
        int ttScore = scoreFromTT(entry->score, ply);
        if (entry->flag == TT_EXACT)              return ttScore;
        if (entry->flag == TT_LOWER && ttScore >= beta) return ttScore;
        if (entry->flag == TT_UPPER && ttScore <= alpha) return ttScore;
        ttBestMove = entry->bestMove;
    } else if (entry) {
        ttBestMove = entry->bestMove;
    }

    const bool inCheck = isInCheck(gs, gs.whiteToMove);

    // IID: if no TT move at a deeper node, do a small pre-search to seed TT ordering.
    if (!isValidMove(ttBestMove) && depth >= 6 && !inCheck && !pvNode) {
        (void)negamax(gs, depth - 2, alpha, beta, ply, false, nullptr);
        if (!ss.stopped) {
            if (TTEntry* iidEntry = tt.probe(hash)) {
                ttBestMove = iidEntry->bestMove;
            }
        }
    }

    // Check extension: improve tactical accuracy in forced lines.
    if (inCheck && depth > 0 && depth < 8) {
        depth += 1;
    }

    if (depth == 0) return quiescence(gs, alpha, beta, ply, 0);

    int staticEval = 0;
    int tacticalDanger = 0;
    int passerDanger = 0;
    bool highStrategicDanger = false;
    if (!inCheck) {
        staticEval = evaluate(gs);
        tacticalDanger = sideHangingDanger(gs, gs.whiteToMove);
        passerDanger = sidePasserEndgamePressure(gs, gs.whiteToMove);
        const bool highDanger = (tacticalDanger >= 56) || (passerDanger >= 95);
        highStrategicDanger = highDanger;

        if (!pvNode && !highDanger && depth == 1) {
            if (staticEval + 120 <= alpha) return staticEval;
        }

        if (!pvNode && depth <= 2) {
            const int margin = 260 + depth * 140;
            if (!highDanger && staticEval - margin >= beta) {
                return staticEval;
            }
        }
    }

    MoveList moves;
    generatePseudoLegalMoves(gs, moves);

    if (nullMoveAllowed && depth >= 3 && !inCheck && !highStrategicDanger && hasNonPawnMaterial(gs, gs.whiteToMove))
    {
        const NullMoveState nullState = doNullMove(gs);
        int R = 2;
        if (depth >= 9 && staticEval >= beta + 120) {
            R = 3;
        }
        int nullScore = -negamax(gs, depth - 1 - R, -beta, -beta + 1, ply + 1, false, nullptr);
        undoNullMove(gs, nullState);

        if (!ss.stopped && nullScore >= beta) {
            // Verification search reduces false null-move cutoffs (zugzwang-ish cases).
            if (depth >= g_tuningParams.nullVerifyMinDepth) {
                const int verify = negamax(gs, depth - 1 - R, beta - 1, beta, ply + 1, false, nullptr);
                if (ss.stopped) return 0;
                if (verify >= beta) {
                    return beta;
                }
            } else {
                return beta;
            }
        }
    }

    sortMoves(moves, gs, ply, ttBestMove);

    TTFlag flag = TT_UPPER;
    Move bestMove = invalidMove();
    int  bestScore = -INF;
    int  moveCount = 0;
    std::array<Move, 64> quietTried{};
    int quietTriedCount = 0;
    const bool useFutility = !inCheck && depth == 1 && !highStrategicDanger;
    const int futilityBase = staticEval;

    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        if (excludedMove && sameMoveIdentity(m, *excludedMove)) {
            continue;
        }
        const bool quietMove = !m.isCapture() && !m.isPromotion();

        int quietHistory = 0;
        if (quietMove && m.from() < 64 && m.to() < 64) {
            const int side = gs.whiteToMove ? 0 : 1;
            quietHistory = ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())];
        }

        if (ply < static_cast<int>(ss.pathMoves.size())) {
            ss.pathMoves[ply] = m;
        }

        searchMakeMove(gs, m);
        const bool legal = !isInCheck(gs, !gs.whiteToMove);
        if (!legal) {
            searchUndoMove(gs);
            continue;
        }

        moveCount++;

        const bool givesCheck = isInCheck(gs, gs.whiteToMove);

        if (!pvNode && !inCheck && depth <= 4 && m.isCapture() && !m.isPromotion() && !givesCheck) {
            const int see = staticExchangeEval(gs, m);
            if (see < -120 * depth) {
                searchUndoMove(gs);
                continue;
            }
        }

        if (useFutility && moveCount > 3 && quietMove && !givesCheck && quietHistory < 7000) {
            const int futilityMargin = g_tuningParams.futilityBaseMargin + depth * g_tuningParams.futilityDepthMargin;
            if (futilityBase + futilityMargin <= alpha) {
                searchUndoMove(gs);
                continue;
            }
        }

        if (!pvNode && !inCheck && !highStrategicDanger && quietMove && !givesCheck && quietHistory < 5000
            && depth <= 3 && moveCount > (6 + depth * 5)) {
            const int lmpMargin = g_tuningParams.lmpBaseMargin + depth * g_tuningParams.lmpDepthMargin;
            if (futilityBase + lmpMargin <= alpha) {
                searchUndoMove(gs);
                continue;
            }
        }

        if (quietMove && quietTriedCount < static_cast<int>(quietTried.size())) {
            quietTried[quietTriedCount++] = m;
        }

        int extension = 0;
        if (!m.isCapture() && givesCheck && depth >= 2 && depth <= 6 && moveCount <= 6) {
            extension = 1;
        }

        if (!pvNode && !inCheck && !excludedMove && depth >= 7 && moveCount <= 2
            && isValidMove(ttBestMove) && sameMoveIdentity(m, ttBestMove)
            && entry && entry->depth >= depth - 2 && entry->flag != TT_UPPER) {
            const int ttScore = scoreFromTT(entry->score, ply);
            const int margin = g_tuningParams.singularBaseMargin + depth * g_tuningParams.singularDepthMargin;
            const int singularBeta = ttScore - margin;
            const int singularDepth = std::max(1, depth / 2);

            const int singular = negamax(gs,
                                         singularDepth,
                                         singularBeta - 1,
                                         singularBeta,
                                         ply + 1,
                                         false,
                                         &m);
            if (!ss.stopped && singular < singularBeta) {
                extension += (depth >= 10) ? 2 : 1;
            }
        }

        if (m.isCapture() && ply > 0 && ply < static_cast<int>(ss.pathMoves.size())) {
            const Move& prev = ss.pathMoves[ply - 1];
            if (isValidMove(prev) && m.to() == prev.to() && depth >= 3 && depth <= 8) {
                extension = 1;
            }
        }
        extension = std::min(extension, 2);
        const int searchDepth = depth - 1 + extension;

        int score;
        if (moveCount == 1) {
            score = -negamax(gs, searchDepth, -beta, -alpha, ply + 1, true, nullptr);
        } else if (!pvNode && quietMove && depth >= 3 && moveCount > 3 && !givesCheck) {
            int reduction = static_cast<int>(std::log(static_cast<double>(depth))
                                                * std::log(static_cast<double>(moveCount)));
            if (reduction > 0) {
                reduction -= 1;
            }
            if (quietHistory > 8000 && reduction > 1) reduction--;
            if (quietHistory < -4000 && reduction < depth - 2) reduction++;
            reduction = std::max(1, std::min(reduction, std::max(1, searchDepth - 1)));
            if (highStrategicDanger && reduction > 1) reduction--;

            if (ply > 0 && ply < static_cast<int>(ss.pathMoves.size())) {
                const Move& prev = ss.pathMoves[ply - 1];
                if (isValidMove(prev) && prev.from() < 64 && prev.to() < 64) {
                    const int side = gs.whiteToMove ? 0 : 1;
                    if (m.value == ss.counterMoves[side][prev.from()][prev.to()].value && reduction > 1) {
                        reduction--;
                    }
                }
            }

            score = -negamax(gs, std::max(0, searchDepth - reduction), -alpha - 1, -alpha, ply + 1, true, nullptr);
            if (score > alpha)
                score = -negamax(gs, searchDepth, -beta, -alpha, ply + 1, true, nullptr);
        } else {
            score = -negamax(gs, searchDepth, -alpha - 1, -alpha, ply + 1, true, nullptr);
            if (score > alpha)
                score = -negamax(gs, searchDepth, -beta, -alpha, ply + 1, true, nullptr);
        }

        searchUndoMove(gs);

        if (ss.stopped) return 0;

        if (score > bestScore) {
            bestScore = score;
            bestMove  = m;
        }

        if (score > alpha) {
            alpha = score;
            flag  = TT_EXACT;

            if (ply < PV_MAX_PLY) {
                ss.pvTable[ply][ply] = m;
                int nextLen = (ply + 1 < PV_MAX_PLY) ? ss.pvLength[ply + 1] : (ply + 1);
                if (nextLen < ply + 1) {
                    nextLen = ply + 1;
                }
                const int copyEnd = std::min(nextLen, PV_MAX_PLY);
                for (int j = ply + 1; j < copyEnd; ++j) {
                    ss.pvTable[ply][j] = ss.pvTable[ply + 1][j];
                }
                ss.pvLength[ply] = copyEnd;
            }

            if (quietMove) {
                updateHistoryBonus(gs, m, std::max(1, depth / 2));
                updateContinuationBonus(gs, ply, m, std::max(1, depth / 2));
            }
        }

        if (alpha >= beta) {
            storeKiller(ply, m);
            if (quietMove) {
                updateHistoryBonus(gs, m, depth + 1);
                updateContinuationBonus(gs, ply, m, depth + 1);
                updateCounterMove(gs, ply, m);
                for (int q = 0; q < quietTriedCount; ++q) {
                    if (quietTried[q].value != m.value) {
                        updateHistoryMalus(gs, quietTried[q], depth);
                        updateContinuationMalus(gs, ply, quietTried[q], depth);
                    }
                }
            }
            tt.store(hash, scoreToTT(beta, ply), depth, TT_LOWER, m);
            return beta;
        }
    }

    if (moveCount == 0) {
        if (inCheck)
            return -(MATE_SCORE - ply);
        return DRAW_SCORE;
    }

    tt.store(hash, scoreToTT(bestScore, ply), depth, flag, bestMove);
    return bestScore;
}

Move computeBestMove(GameState gs, int maxDepth, int timeLimitMs)
{
    if (g_experienceLearningEnabled) {
        loadExperienceBookIfNeeded();
    }

    Move bookMove = bookMoveForPosition(gs);
    if (isValidMove(bookMove)) {
        recordPendingExperience(computeHash(gs), bookMove, 0);
        lastSearchStats = SearchStats {};
        return bookMove;
    }

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
    ss.evalActive = true;
    ss.evalPly = 0;
    ss.evalCoreNoKingStack[0] = computeCoreEvalNoKing(gs);

    maxDepth += phaseDepthBonus(gs);
    const int rootEval = evaluate(gs);

    Move fallbackBestMove = moves.moves[0];
    int fallbackBestScore = -INF;
    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        ss.pathMoves[0] = m;
        searchMakeMove(gs, m);
        const int score = -evaluate(gs);
        searchUndoMove(gs);

        if (score > fallbackBestScore) {
            fallbackBestScore = score;
            fallbackBestMove = m;
        }
    }

    Move bestMove = fallbackBestMove;
    int  bestScore = fallbackBestScore;
    int  depthReached = 0;
    int  prevIterScore = 0;
    bool hasPrevIterScore = false;
    auto searchRootWindow = [&](int depth,
                                int alpha,
                                int beta,
                                Move& outBestMove,
                                std::array<Move, PV_MAX_PLY>& outPv,
                                int& outPvLen) -> int {
        int localBestScore = -INF;
        Move localBestMove = moves.moves[0];
        std::array<Move, PV_MAX_PLY> localPv{};
        for (auto& pm : localPv) {
            pm = invalidMove();
        }
        int localPvLen = 1;

        uint64_t hash = computeHash(gs);
        TTEntry* e = tt.probe(hash);
        Move ttMove = invalidMove();
        if (e && isValidMove(e->bestMove)) {
            ttMove = e->bestMove;
        } else if (hasPrevIterScore && isValidMove(bestMove)) {
            // Reuse previous iteration's PV head to stabilize root ordering.
            ttMove = bestMove;
        }
        sortMoves(moves, gs, 0, ttMove);
        if (g_experienceLearningEnabled) {
            std::vector<std::pair<Move, int>> decorated;
            decorated.reserve(static_cast<size_t>(moves.count));
            for (int i = 0; i < moves.count; ++i) {
                decorated.emplace_back(moves.moves[i], experienceMoveBias(hash, moves.moves[i]));
            }

            std::stable_sort(decorated.begin(), decorated.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });

            for (int i = 0; i < moves.count; ++i) {
                moves.moves[i] = decorated[static_cast<size_t>(i)].first;
            }
        }

        int localAlpha = alpha;

        for (int i = 0; i < moves.count; ++i) {
            Move& m = moves.moves[i];
            const Piece rootMover = pieceAtSq(gs, m.from());
            const bool rootHeavyMover = (rootMover.type == Q || rootMover.type == R);
            int rootSafetyMalus = 0;

            if (rootEval >= 220 && rootHeavyMover && m.isCapture() && !m.isEnPassant()) {
                const int see = staticExchangeEval(gs, m);
                if (see < -100) {
                    rootSafetyMalus += std::min(1200, (-see * 3) / 2);
                }
            }

            ss.pathMoves[0] = m;
            searchMakeMove(gs, m);

            bool createsImmediateThreefold = false;
            const std::uint64_t childHash = positionHash(gs);
            auto posIt = gs.positionHashCounts.find(childHash);
            if (posIt != gs.positionHashCounts.end() && posIt->second >= 2) {
                createsImmediateThreefold = true;
            }

            int score;
            if (i == 0) {
                score = -negamax(gs, depth - 1, -beta, -localAlpha, 1, true);
            } else {
                score = -negamax(gs, depth - 1, -localAlpha - 1, -localAlpha, 1, true);
                if (score > localAlpha) {
                    score = -negamax(gs, depth - 1, -beta, -localAlpha, 1, true);
                }
            }

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

            if (rootEval >= 220 && rootHeavyMover) {
                const int tr = rowOfSq(m.to());
                const int tc = colOfSq(m.to());
                const bool attacked = isSquareAttacked(gs, tr, tc, gs.whiteToMove);
                const bool defended = isSquareAttacked(gs, tr, tc, !gs.whiteToMove);
                if (attacked && !defended) {
                    rootSafetyMalus += (rootMover.type == Q) ? 780 : 420;
                }
            }

            if (rootSafetyMalus > 0 && std::abs(score) < (MATE_SCORE - 1024)) {
                // When clearly ahead, prefer safer conversion lines over material blunders.
                if (score < rootEval + 280) {
                    score -= rootSafetyMalus;
                } else {
                    score -= rootSafetyMalus / 3;
                }
            }

            searchUndoMove(gs);

            if (ss.stopped) {
                outBestMove = localBestMove;
                outPv = localPv;
                outPvLen = localPvLen;
                return localBestScore;
            }

            if (score > localBestScore) {
                localBestScore = score;
                localBestMove = m;

                localPvLen = std::clamp(ss.pvLength[1], 1, PV_MAX_PLY);
                for (int j = 1; j < localPvLen; ++j) {
                    localPv[j] = ss.pvTable[1][j];
                }
            }
            if (score > localAlpha) {
                localAlpha = score;
            }
            if (localAlpha >= beta) {
                break;
            }
        }

        outBestMove = localBestMove;
        outPv = localPv;
        outPvLen = localPvLen;
        return localBestScore;
    };

    for (int depth = 1; depth <= maxDepth; depth++) {
        Move currentBest = moves.moves[0];
        std::array<Move, PV_MAX_PLY> currentPv{};
        for (auto& pm : currentPv) {
            pm = invalidMove();
        }
        int currentPvLen = 1;
        int currentBestScore = -INF;

        int alpha = -INF;
        int beta = INF;
        if (hasPrevIterScore && depth >= 3) {
            const int delta = 22 + depth * 6;
            alpha = std::max(-INF, prevIterScore - delta);
            beta = std::min(INF, prevIterScore + delta);
        }

        currentBestScore = searchRootWindow(depth, alpha, beta, currentBest, currentPv, currentPvLen);
        if (ss.stopped) goto done;

        if (hasPrevIterScore && depth >= 3 && (currentBestScore <= alpha || currentBestScore >= beta)) {
            int widen = 64;
            for (int tries = 0; tries < 4; ++tries) {
                if (currentBestScore <= alpha) {
                    alpha = std::max(-INF, alpha - widen);
                }
                if (currentBestScore >= beta) {
                    beta = std::min(INF, beta + widen);
                }

                currentBestScore = searchRootWindow(depth, alpha, beta, currentBest, currentPv, currentPvLen);
                if (ss.stopped) goto done;

                if (currentBestScore > alpha && currentBestScore < beta) {
                    break;
                }

                widen *= 2;
            }
        }

        bestMove  = currentBest;
        bestScore = currentBestScore;
        depthReached = depth;
        prevIterScore = bestScore;
        hasPrevIterScore = true;

        if (g_searchInfoOutputEnabled) {
            std::string pvLine = moveToUciString(currentBest);
            {
                GameState pvState = gs;
                makeMove(pvState, currentBest, false);

                const int pvEnd = std::min(currentPvLen, PV_MAX_PLY);
                for (int j = 1; j < pvEnd; ++j) {
                    const Move& pm = currentPv[j];
                    if (!isValidMove(pm)) {
                        break;
                    }

                    MoveList legal;
                    generateLegalMoves(pvState, legal);
                    bool found = false;
                    Move legalMove = invalidMove();
                    for (int k = 0; k < legal.count; ++k) {
                        if (sameMoveIdentity(legal.moves[k], pm)) {
                            found = true;
                            legalMove = legal.moves[k];
                            break;
                        }
                    }

                    if (!found) {
                        break;
                    }

                    pvLine += " ";
                    pvLine += moveToUciString(legalMove);
                    makeMove(pvState, legalMove, false);
                }
            }

            auto now = std::chrono::steady_clock::now();
            int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - ss.startTime).count());
            if (elapsedMs <= 0) elapsedMs = 1;
            const int nps = static_cast<int>((static_cast<long long>(ss.nodes) * 1000LL) / elapsedMs);

            auto isMateScore = [](int s) {
                return std::abs(s) >= (MATE_SCORE - 512);
            };

            std::cout << "info depth " << depth;
            if (isMateScore(bestScore)) {
                int matePlies = std::max(1, MATE_SCORE - std::abs(bestScore));
                int mateMoves = (matePlies + 1) / 2;
                if (bestScore < 0) mateMoves = -mateMoves;
                std::cout << " score mate " << mateMoves;
            } else {
                std::cout << " score cp " << bestScore;
            }
            std::cout << " nodes " << ss.nodes
                      << " time " << elapsedMs
                      << " nps " << nps
                      << " pv " << pvLine << "\n";
        }
    }

done:
    if (isValidMove(bestMove)) {
        const uint64_t rootHash = computeHash(gs);
        recordPendingExperience(rootHash, bestMove, bestScore);
    }

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

void requestStopSearch()
{
    g_stopRequested.store(true, std::memory_order_relaxed);
}

void clearStopSearch()
{
    g_stopRequested.store(false, std::memory_order_relaxed);
}

void setHashSizeMb(int mb)
{
    tt.resizeMb(mb);
}

int getHashSizeMb()
{
    return tt.hashMb;
}

void setTuningParams(const EngineTuningParams& p)
{
    EngineTuningParams c = p;
    c.futilityBaseMargin = std::clamp(c.futilityBaseMargin, 0, 1200);
    c.futilityDepthMargin = std::clamp(c.futilityDepthMargin, 0, 600);
    c.lmpBaseMargin = std::clamp(c.lmpBaseMargin, 0, 1200);
    c.lmpDepthMargin = std::clamp(c.lmpDepthMargin, 0, 600);
    c.qsearchDeltaMargin = std::clamp(c.qsearchDeltaMargin, 0, 1200);
    c.qsearchSeeThreshold = std::clamp(c.qsearchSeeThreshold, 0, 1200);
    c.singularBaseMargin = std::clamp(c.singularBaseMargin, 0, 800);
    c.singularDepthMargin = std::clamp(c.singularDepthMargin, 0, 120);
    c.nnueMgWeight = std::clamp(c.nnueMgWeight, 0, 300);
    c.nnueEgWeight = std::clamp(c.nnueEgWeight, 0, 300);
    c.nnueWeightDiv = std::clamp(c.nnueWeightDiv, 1, 300);
    c.nullVerifyMinDepth = std::clamp(c.nullVerifyMinDepth, 2, 24);
    g_tuningParams = c;
}

EngineTuningParams getTuningParams()
{
    return g_tuningParams;
}

void setSyzygyPath(const std::string& path)
{
    g_syzygyPath = path;
}

std::string getSyzygyPath()
{
    return g_syzygyPath;
}

void setSyzygyProbeLimit(int pieces)
{
    g_syzygyProbeLimit = std::clamp(pieces, 3, 7);
}

int getSyzygyProbeLimit()
{
    return g_syzygyProbeLimit;
}

void setSearchInfoOutputEnabled(bool enabled)
{
    g_searchInfoOutputEnabled = enabled;
}

bool isSearchInfoOutputEnabled()
{
    return g_searchInfoOutputEnabled;
}

void learningStartGame()
{
    if (!g_experienceLearningEnabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_experienceMutex);
    g_pendingExperience.clear();
    g_learningGameActive = true;
}

void learningAbortGame()
{
    if (!g_experienceLearningEnabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_experienceMutex);
    g_pendingExperience.clear();
    g_learningGameActive = false;
}

void learningFinalizeGame()
{
    if (!g_experienceLearningEnabled) {
        return;
    }
    loadExperienceBookIfNeeded();
    bool hadPending = false;

    {
        std::lock_guard<std::mutex> lock(g_experienceMutex);
        for (const auto& [_, vec] : g_pendingExperience) {
            if (!vec.empty()) {
                hadPending = true;
                break;
            }
        }

        if (!hadPending) {
            g_learningGameActive = false;
            return;
        }

        for (const auto& [hash, vec] : g_pendingExperience) {
            for (const auto& e : vec) {
                if (!isValidMove(e.move) || e.visits <= 0) {
                    continue;
                }
                // Replay each visit as one update to preserve averaging behavior.
                const int avg = e.sumScore / std::max(1, e.visits);
                for (int i = 0; i < e.visits; ++i) {
                    updateExperienceBookEntry(hash, e.move, avg);
                }
            }
        }
        g_pendingExperience.clear();
        g_learningGameActive = false;
    }

    saveExperienceBook();
}

void setExperienceLearningEnabled(bool enabled)
{
    g_experienceLearningEnabled = enabled;
    if (!enabled) {
        learningAbortGame();
    } else {
        learningStartGame();
    }
}

bool isExperienceLearningEnabled()
{
    return g_experienceLearningEnabled;
}