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
#include <string>
#include <unordered_map>
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

inline bool isValidMove(const Move& m)
{
    return m.value != 0xFFFFFFFFu;
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

static int squareFromCoord(const std::string& s, int start)
{
    if (start + 1 >= static_cast<int>(s.size())) return -1;
    const int file = s[start] - 'a';
    const int rank = s[start + 1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    const int row = 7 - rank;
    return row * 8 + file;
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

static const std::unordered_map<std::uint64_t, std::vector<Move>>& openingBook()
{
    static const std::unordered_map<std::uint64_t, std::vector<Move>> book = [] {
        std::unordered_map<std::uint64_t, std::vector<Move>> b;

        addBookLine(b, {"e2e4","e7e5","g1f3","b8c6","f1c4","g8f6","d2d3","f8c5"});
        addBookLine(b, {"e2e4","c7c5","g1f3","d7d6","d2d4","c5d4","f3d4","g8f6"});
        addBookLine(b, {"e2e4","e7e6","d2d4","d7d5","b1c3","g8f6","c1g5","f8e7"});
        addBookLine(b, {"d2d4","d7d5","c2c4","e7e6","b1c3","g8f6","c1g5","f8e7"});
        addBookLine(b, {"d2d4","g8f6","c2c4","e7e6","g1f3","d7d5","b1c3","f8e7"});
        addBookLine(b, {"c2c4","e7e5","b1c3","g8f6","g2g3","d7d5","c4d5","f6d5"});
        addBookLine(b, {"g1f3","d7d5","d2d4","g8f6","c2c4","e7e6","b1c3","f8e7"});
        addBookLine(b, {"e2e4","e7e5","g1f3","g8f6","f3e5","d7d6","e5f3","f6e4"});

        return b;
    }();

    return book;
}

static Move bookMoveForPosition(GameState& gs)
{
    const int ply = (gs.fullmoveNumber - 1) * 2 + (gs.whiteToMove ? 0 : 1);
    if (ply >= 12) {
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
    for (const Move& bm : it->second) {
        for (int i = 0; i < legal.count; ++i) {
            if (sameMoveIdentity(legal.moves[i], bm)) {
                return legal.moves[i];
            }
        }
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

static int phaseDepthBonus(const GameState& gs)
{
    // Use non-pawn material as a cheap phase proxy.
    int npm = 0;
    npm += pieceValue(P) * (popcount64(gs.bitboards[0][P - 1]) + popcount64(gs.bitboards[1][P - 1]));
    npm += pieceValue(N) * (popcount64(gs.bitboards[0][N - 1]) + popcount64(gs.bitboards[1][N - 1]));
    npm += pieceValue(B) * (popcount64(gs.bitboards[0][B - 1]) + popcount64(gs.bitboards[1][B - 1]));
    npm += pieceValue(R) * (popcount64(gs.bitboards[0][R - 1]) + popcount64(gs.bitboards[1][R - 1]));
    npm += pieceValue(Q) * (popcount64(gs.bitboards[0][Q - 1]) + popcount64(gs.bitboards[1][Q - 1]));

    // Opening -> middlegame -> middle-endgame -> endgame.
    if (npm >= 5000) return 0;
    if (npm >= 3000) return 1;
    if (npm >= 2000) return 2;
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
    if (ss.evalActive && ss.evalPly + 1 < static_cast<int>(ss.evalCoreNoKingStack.size())) {
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

    const int mob = mobilityScore(gs);
    mgScore += mob;
    egScore += mob / 2;

    mgScore += lightweightKingSafetyScore(gs, false);

    if (!eg) {
        mgScore += openingPrinciplesScore(gs);
    }

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
            searchMakeMove(gs, m);
            int score = -quiescence(gs, -beta, -alpha, ply + 1);
            searchUndoMove(gs);

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    int stand_pat = evaluate(gs);

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha)  alpha = stand_pat;

    MoveList moves;
    generateTacticalLegalMoves(gs, moves);

    Move dummy = invalidMove();
    sortMoves(moves, gs, 0, dummy);

    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        if (m.isCapture() && !m.isEnPassant() && staticExchangeEval(gs, m) < 0) {
            continue;
        }

        searchMakeMove(gs, m);
        int score = -quiescence(gs, -beta, -alpha, ply + 1);
        searchUndoMove(gs);

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

    const bool inCheck = isInCheck(gs, gs.whiteToMove);

    // Check extension: improve tactical accuracy in forced lines.
    if (inCheck && depth > 0 && depth < 8) {
        depth += 1;
    }

    if (depth == 0) return quiescence(gs, alpha, beta, ply);

    int staticEval = 0;
    if (!inCheck) {
        staticEval = evaluate(gs);
        if (depth == 1) {
            const int rfpMargin = 80;
            if (staticEval - rfpMargin >= beta) {
                return staticEval;
            }
        }
    }

    MoveList moves;
    generateLegalMoves(gs, moves);

    if (moves.empty()) {
        if (inCheck)
            return -(MATE_SCORE - ply);   
        return DRAW_SCORE;
    }

    if (nullMoveAllowed && depth >= 3 && !inCheck && !isEndgame(gs))
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
    const bool pvNode = (beta - alpha) > 1;
    const bool useFutility = !inCheck && depth == 1;
    const int futilityBase = staticEval;

    for (int i = 0; i < moves.count; ++i) {
        Move& m = moves.moves[i];
        moveCount++;
        const bool quietMove = !m.isCapture() && !m.isPromotion();

        if (useFutility && moveCount > 3 && quietMove) {
            const int futilityMargin = 120;
            if (futilityBase + futilityMargin <= alpha) {
                continue;
            }
        }

        if (!pvNode && !inCheck && quietMove && depth <= 4 && moveCount > (2 + depth * 3)) {
            const int lmpMargin = 140 + depth * 100;
            if (futilityBase + lmpMargin <= alpha) {
                continue;
            }
        }

        int quietHistory = 0;
        if (quietMove && m.from() < 64 && m.to() < 64) {
            const int side = gs.whiteToMove ? 0 : 1;
            quietHistory = ss.history[side][rowOfSq(m.from())][colOfSq(m.from())][rowOfSq(m.to())][colOfSq(m.to())];
        }

        searchMakeMove(gs, m);
        const bool givesCheck = isInCheck(gs, gs.whiteToMove);

        int score;
        if (moveCount == 1) {
            score = -negamax(gs, depth - 1, -beta, -alpha, ply + 1, true);
        } else if (!pvNode && quietMove && depth >= 3 && moveCount > 3 && !givesCheck) {
            int reduction = 1;
            if (moveCount > 8) reduction++;
            if (depth >= 6 && moveCount > 14) reduction++;
            if (quietHistory > 8000 && reduction > 1) reduction--;

            score = -negamax(gs, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha)
                score = -negamax(gs, depth - 1, -beta, -alpha, ply + 1, true);
        } else {
            score = -negamax(gs, depth - 1, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha)
                score = -negamax(gs, depth - 1, -beta, -alpha, ply + 1, true);
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
    if (timeLimitMs >= 10000) {
        Move bookMove = bookMoveForPosition(gs);
        if (isValidMove(bookMove)) {
            lastSearchStats = SearchStats {};
            return bookMove;
        }
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

    Move bestMove = moves.moves[0];
    int  bestScore = -INF;
    int  depthReached = 0;
    int  prevIterScore = 0;
    bool hasPrevIterScore = false;
    auto searchRootWindow = [&](int depth, int alpha, int beta, Move& outBestMove) -> int {
        int localBestScore = -INF;
        Move localBestMove = moves.moves[0];

        uint64_t hash = computeHash(gs);
        TTEntry* e = tt.probe(hash);
        Move ttMove = (e && isValidMove(e->bestMove))
            ? e->bestMove
            : invalidMove();
        sortMoves(moves, gs, 0, ttMove);

        int localAlpha = alpha;

        for (int i = 0; i < moves.count; ++i) {
            Move& m = moves.moves[i];
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

            searchUndoMove(gs);

            if (ss.stopped) {
                outBestMove = localBestMove;
                return localBestScore;
            }

            if (score > localBestScore) {
                localBestScore = score;
                localBestMove = m;
            }
            if (score > localAlpha) {
                localAlpha = score;
            }
            if (localAlpha >= beta) {
                break;
            }
        }

        outBestMove = localBestMove;
        return localBestScore;
    };

    for (int depth = 1; depth <= maxDepth; depth++) {
        Move currentBest = moves.moves[0];
        int currentBestScore = -INF;

        int alpha = -INF;
        int beta = INF;
        if (hasPrevIterScore && depth >= 3) {
            const int delta = 22 + depth * 6;
            alpha = std::max(-INF, prevIterScore - delta);
            beta = std::min(INF, prevIterScore + delta);
        }

        currentBestScore = searchRootWindow(depth, alpha, beta, currentBest);
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

                currentBestScore = searchRootWindow(depth, alpha, beta, currentBest);
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