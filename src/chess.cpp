#include "../include/chess.hpp"
#include <array>
#include <cctype>
#include <cmath>
#include <random>
#include <sstream>
#include <vector>

namespace {
constexpr int WHITE = 0;
constexpr int BLACK = 1;

inline bool inBounds(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }
inline int colorIndex(bool white) { return white ? WHITE : BLACK; }
inline int pieceIndex(int type) { return type - 1; }
inline int squareOf(int r, int c) { return r * 8 + c; }
inline int rowOf(int sq) { return sq / 8; }
inline int colOf(int sq) { return sq % 8; }
inline Bitboard bitAt(int sq) { return 1ULL << sq; }
inline std::uint8_t encodePiece(const Piece& p) { return p.type == EMPTY ? 0 : static_cast<std::uint8_t>(p.type + (p.white ? 0 : 8)); }
inline Piece decodePiece(std::uint8_t code)
{
    if (code == 0) {
        return Piece {};
    }
    return Piece { static_cast<std::uint8_t>(code & 0x7), (code & 0x8) == 0 };
}

struct ZobristKeys {
    std::array<std::array<std::array<Bitboard, 64>, 6>, 2> piece{};
    std::array<Bitboard, 16> castling{};
    std::array<Bitboard, 64> enPassant{};
    Bitboard sideToMove = 0;
};

ZobristKeys initZobristKeys()
{
    ZobristKeys z;
    std::mt19937_64 rng(0x9E3779B97F4A7C15ULL);
    auto rand64 = [&]() { return static_cast<Bitboard>(rng()); };

    for (int color = 0; color < 2; ++color) {
        for (int pt = 0; pt < 6; ++pt) {
            for (int sq = 0; sq < 64; ++sq) {
                z.piece[color][pt][sq] = rand64();
            }
        }
    }
    for (int i = 0; i < 16; ++i) {
        z.castling[i] = rand64();
    }
    for (int sq = 0; sq < 64; ++sq) {
        z.enPassant[sq] = rand64();
    }
    z.sideToMove = rand64();
    return z;
}

const ZobristKeys ZOBRIST = initZobristKeys();

inline int castlingRightsMask(const GameState& gs)
{
    int mask = 0;
    if (!gs.wkMoved && !gs.wrHHMoved) mask |= 1;
    if (!gs.wkMoved && !gs.wrAHMoved) mask |= 2;
    if (!gs.bkMoved && !gs.brHHMoved) mask |= 4;
    if (!gs.bkMoved && !gs.brAHMoved) mask |= 8;
    return mask;
}

inline int epSquare(const std::optional<std::pair<int, int>>& ep)
{
    if (!ep.has_value()) {
        return -1;
    }
    return squareOf(ep->first, ep->second);
}

inline Bitboard pieceZobrist(const Piece& p, int sq)
{
    if (p.type == EMPTY || sq < 0 || sq >= 64) {
        return 0;
    }
    return ZOBRIST.piece[colorIndex(p.white)][pieceIndex(p.type)][sq];
}

Bitboard computeZobristFromState(const GameState& gs)
{
    Bitboard h = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = decodePiece(gs.pieceOnSquare[sq]);
        if (p.type != EMPTY) {
            h ^= pieceZobrist(p, sq);
        }
    }

    h ^= ZOBRIST.castling[castlingRightsMask(gs)];

    const int epsq = epSquare(gs.enPassantTarget);
    if (epsq >= 0) {
        h ^= ZOBRIST.enPassant[epsq];
    }

    if (!gs.whiteToMove) {
        h ^= ZOBRIST.sideToMove;
    }

    return h;
}

std::vector<std::string> split(const std::string& s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

Bitboard popLsb(Bitboard& bb)
{
    const Bitboard lsb = bb & -bb;
    bb ^= lsb;
    return lsb;
}

int lsbSquare(Bitboard bb)
{
    return __builtin_ctzll(bb);
}

int msbSquare(Bitboard bb)
{
    return 63 - __builtin_clzll(bb);
}

void clearBitboards(GameState& gs)
{
    for (auto& color : gs.bitboards) {
        color.fill(0ULL);
    }
    gs.occupancies = { 0ULL, 0ULL };
    gs.occupancyBoth = 0ULL;
    gs.pieceOnSquare.fill(0);
}

void placePiece(GameState& gs, int sq, const Piece& p)
{
    if (p.type == EMPTY) {
        return;
    }
    const int color = colorIndex(p.white);
    const Bitboard mask = bitAt(sq);
    gs.bitboards[color][pieceIndex(p.type)] |= mask;
    gs.occupancies[color] |= mask;
    gs.occupancyBoth |= mask;
    gs.pieceOnSquare[sq] = encodePiece(p);
}

void removePiece(GameState& gs, int sq, const Piece& p)
{
    if (p.type == EMPTY) {
        return;
    }
    const int color = colorIndex(p.white);
    const Bitboard mask = bitAt(sq);
    gs.bitboards[color][pieceIndex(p.type)] &= ~mask;
    gs.occupancies[color] &= ~mask;
    gs.occupancyBoth &= ~mask;
    gs.pieceOnSquare[sq] = 0;
}

Piece pieceAtSqImpl(const GameState& gs, int sq)
{
    return decodePiece(gs.pieceOnSquare[sq]);
}

std::array<Bitboard, 64> initKnightAttacks()
{
    std::array<Bitboard, 64> attacks{};
    static constexpr int offsets[8][2] = {
        { -2, -1 }, { -2, 1 }, { -1, -2 }, { -1, 2 },
        { 1, -2 },  { 1, 2 },  { 2, -1 },  { 2, 1 },
    };

    for (int sq = 0; sq < 64; ++sq) {
        const int r = rowOf(sq);
        const int c = colOf(sq);
        Bitboard bb = 0;
        for (const auto& d : offsets) {
            const int nr = r + d[0];
            const int nc = c + d[1];
            if (inBounds(nr, nc)) {
                bb |= bitAt(squareOf(nr, nc));
            }
        }
        attacks[sq] = bb;
    }
    return attacks;
}

std::array<Bitboard, 64> initKingAttacks()
{
    std::array<Bitboard, 64> attacks{};
    for (int sq = 0; sq < 64; ++sq) {
        const int r = rowOf(sq);
        const int c = colOf(sq);
        Bitboard bb = 0;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) {
                    continue;
                }
                const int nr = r + dr;
                const int nc = c + dc;
                if (inBounds(nr, nc)) {
                    bb |= bitAt(squareOf(nr, nc));
                }
            }
        }
        attacks[sq] = bb;
    }
    return attacks;
}

std::array<std::array<Bitboard, 64>, 2> initPawnAttacks()
{
    std::array<std::array<Bitboard, 64>, 2> attacks{};

    for (int sq = 0; sq < 64; ++sq) {
        const int r = rowOf(sq);
        const int c = colOf(sq);

        Bitboard whiteAttacks = 0;
        if (r > 0 && c > 0) {
            whiteAttacks |= bitAt(squareOf(r - 1, c - 1));
        }
        if (r > 0 && c < 7) {
            whiteAttacks |= bitAt(squareOf(r - 1, c + 1));
        }
        attacks[WHITE][sq] = whiteAttacks;

        Bitboard blackAttacks = 0;
        if (r < 7 && c > 0) {
            blackAttacks |= bitAt(squareOf(r + 1, c - 1));
        }
        if (r < 7 && c < 7) {
            blackAttacks |= bitAt(squareOf(r + 1, c + 1));
        }
        attacks[BLACK][sq] = blackAttacks;
    }

    return attacks;
}

std::array<std::array<Bitboard, 64>, 2> initPawnAttackers(const std::array<std::array<Bitboard, 64>, 2>& pawnAttacks)
{
    std::array<std::array<Bitboard, 64>, 2> attackers{};

    for (int color = 0; color < 2; ++color) {
        for (int fromSq = 0; fromSq < 64; ++fromSq) {
            Bitboard targets = pawnAttacks[color][fromSq];
            while (targets) {
                const int toSq = lsbSquare(popLsb(targets));
                attackers[color][toSq] |= bitAt(fromSq);
            }
        }
    }

    return attackers;
}

enum Direction : int {
    NORTH = 0,
    SOUTH = 1,
    WEST = 2,
    EAST = 3,
    NORTH_WEST = 4,
    NORTH_EAST = 5,
    SOUTH_WEST = 6,
    SOUTH_EAST = 7
};

std::array<std::array<Bitboard, 64>, 8> initRays()
{
    std::array<std::array<Bitboard, 64>, 8> rays{};
    static constexpr int dr[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    static constexpr int dc[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };

    for (int sq = 0; sq < 64; ++sq) {
        const int r = rowOf(sq);
        const int c = colOf(sq);
        for (int dir = 0; dir < 8; ++dir) {
            Bitboard ray = 0;
            int nr = r + dr[dir];
            int nc = c + dc[dir];
            while (inBounds(nr, nc)) {
                ray |= bitAt(squareOf(nr, nc));
                nr += dr[dir];
                nc += dc[dir];
            }
            rays[dir][sq] = ray;
        }
    }

    return rays;
}

inline bool directionIncreasesSquare(int dir)
{
    return dir == SOUTH || dir == EAST || dir == SOUTH_WEST || dir == SOUTH_EAST;
}

inline Bitboard rayAttacks(int sq, Bitboard occupancy, const int* dirs, int dirCount, const std::array<std::array<Bitboard, 64>, 8>& rays)
{
    Bitboard attacks = 0;
    for (int i = 0; i < dirCount; ++i) {
        const int dir = dirs[i];
        const Bitboard ray = rays[dir][sq];
        attacks |= ray;

        const Bitboard blockers = ray & occupancy;
        if (!blockers) {
            continue;
        }

        const int blockerSq = directionIncreasesSquare(dir) ? lsbSquare(blockers) : msbSquare(blockers);
        attacks &= ~rays[dir][blockerSq];
    }
    return attacks;
}

inline Bitboard bishopAttacks(int sq, Bitboard occupancy, const std::array<std::array<Bitboard, 64>, 8>& rays)
{
    static constexpr int dirs[4] = { NORTH_WEST, NORTH_EAST, SOUTH_WEST, SOUTH_EAST };
    return rayAttacks(sq, occupancy, dirs, 4, rays);
}

inline Bitboard rookAttacks(int sq, Bitboard occupancy, const std::array<std::array<Bitboard, 64>, 8>& rays)
{
    static constexpr int dirs[4] = { NORTH, SOUTH, WEST, EAST };
    return rayAttacks(sq, occupancy, dirs, 4, rays);
}

const std::array<std::array<Bitboard, 64>, 2> PAWN_ATTACKS = initPawnAttacks();
const std::array<std::array<Bitboard, 64>, 2> PAWN_ATTACKERS = initPawnAttackers(PAWN_ATTACKS);
const std::array<Bitboard, 64> KNIGHT_ATTACKS = initKnightAttacks();
const std::array<Bitboard, 64> KING_ATTACKS = initKingAttacks();
const std::array<std::array<Bitboard, 64>, 8> RAYS = initRays();

void addMove(MoveList& moves, int fromSq, int toSq, int capturedType = EMPTY,
    bool isEnPassant = false, bool isCastle = false, bool promotion = false, int promotionType = Q)
{
    if (moves.count >= static_cast<int>(moves.moves.size())) {
        return;
    }
    Move m;
    m.setFrom(static_cast<std::uint8_t>(fromSq));
    m.setTo(static_cast<std::uint8_t>(toSq));
    m.setPromotionType(static_cast<std::uint8_t>(promotionType));
    m.setCapturedType(static_cast<std::uint8_t>(capturedType));
    if (capturedType != EMPTY) {
        m.addFlags(Move::FLAG_CAPTURE);
    }
    if (isEnPassant) {
        m.addFlags(Move::FLAG_EN_PASSANT);
    }
    if (isCastle) {
        m.addFlags(Move::FLAG_CASTLE);
    }
    if (promotion) {
        m.addFlags(Move::FLAG_PROMOTION);
    }
    moves.moves[moves.count++] = m;
}

void applyMoveNoHistory(GameState& gs, const Move& m)
{
    const int fromSq = m.from();
    const int toSq = m.to();
    const int fromR = rowOf(fromSq);
    const int fromC = colOf(fromSq);
    const int toR = rowOf(toSq);
    const int toC = colOf(toSq);

    const Piece movingPiece = pieceAtSqImpl(gs, fromSq);
    Piece capturedPiece { EMPTY, false };

    if (m.isEnPassant()) {
        capturedPiece = pieceAtSqImpl(gs, squareOf(fromR, toC));
    } else {
        capturedPiece = pieceAtSqImpl(gs, toSq);
    }

    removePiece(gs, fromSq, movingPiece);

    if (m.isEnPassant()) {
        const int capSq = squareOf(fromR, toC);
        removePiece(gs, capSq, capturedPiece);
    } else if (capturedPiece.type != EMPTY) {
        removePiece(gs, toSq, capturedPiece);
    }

    if (m.isCastle()) {
        placePiece(gs, toSq, movingPiece);

        if (toC == 6) {
            const int rookFrom = squareOf(toR, 7);
            const int rookTo = squareOf(toR, 5);
            const Piece rook = pieceAtSqImpl(gs, rookFrom);
            removePiece(gs, rookFrom, rook);
            placePiece(gs, rookTo, rook);
        } else {
            const int rookFrom = squareOf(toR, 0);
            const int rookTo = squareOf(toR, 3);
            const Piece rook = pieceAtSqImpl(gs, rookFrom);
            removePiece(gs, rookFrom, rook);
            placePiece(gs, rookTo, rook);
        }
    } else {
        Piece placed = movingPiece;
        if (m.isPromotion()) {
            placed.type = m.promotionType();
        }
        placePiece(gs, toSq, placed);
    }

    if (movingPiece.type == K) {
        if (movingPiece.white) {
            gs.wkMoved = true;
        } else {
            gs.bkMoved = true;
        }
    }

    if (movingPiece.type == R) {
        if (movingPiece.white) {
            if (fromR == 7 && fromC == 0)
                gs.wrAHMoved = true;
            if (fromR == 7 && fromC == 7)
                gs.wrHHMoved = true;
        } else {
            if (fromR == 0 && fromC == 0)
                gs.brAHMoved = true;
            if (fromR == 0 && fromC == 7)
                gs.brHHMoved = true;
        }
    }

    if (capturedPiece.type == R) {
        if (toR == 0 && toC == 0)
            gs.brAHMoved = true;
        if (toR == 0 && toC == 7)
            gs.brHHMoved = true;
        if (toR == 7 && toC == 0)
            gs.wrAHMoved = true;
        if (toR == 7 && toC == 7)
            gs.wrHHMoved = true;
    }

    if (movingPiece.type == P && std::abs(fromR - toR) == 2) {
        gs.enPassantTarget = std::make_pair((fromR + toR) / 2, fromC);
    } else {
        gs.enPassantTarget = std::nullopt;
    }

    if (movingPiece.type == P || capturedPiece.type != EMPTY) {
        gs.halfmoveClock = 0;
    } else {
        gs.halfmoveClock++;
    }
    if (!gs.whiteToMove) {
        gs.fullmoveNumber++;
    }

    gs.whiteToMove = !gs.whiteToMove;
}
}

Piece pieceAt(const GameState& gs, int r, int c)
{
    if (!inBounds(r, c)) {
        return Piece {};
    }
    return pieceAtSqImpl(gs, squareOf(r, c));
}

Piece pieceAtSq(const GameState& gs, int sq)
{
    if (sq < 0 || sq >= 64) {
        return Piece {};
    }
    return pieceAtSqImpl(gs, sq);
}

void GameState::loadFromFen(const std::string& fen)
{
    undoTop = 0;
    positionHashCounts.clear();
    positionHashCounts.reserve(512);
    initialFen = fen;
    clearBitboards(*this);

    auto parts = split(fen, ' ');
    if (parts.size() < 4)
        return;

    int r = 0;
    int c = 0;
    for (char ch : parts[0]) {
        if (ch == '/') {
            ++r;
            c = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            c += ch - '0';
            continue;
        }

        Piece p;
        p.white = std::isupper(static_cast<unsigned char>(ch));
        switch (std::tolower(static_cast<unsigned char>(ch))) {
        case 'p': p.type = P; break;
        case 'n': p.type = N; break;
        case 'b': p.type = B; break;
        case 'r': p.type = R; break;
        case 'q': p.type = Q; break;
        case 'k': p.type = K; break;
        default: p.type = EMPTY; break;
        }

        if (inBounds(r, c) && p.type != EMPTY) {
            placePiece(*this, squareOf(r, c), p);
        }
        ++c;
    }

    whiteToMove = (parts[1] == "w");

    std::string castling = parts[2];
    wkMoved = (castling.find('K') == std::string::npos && castling.find('Q') == std::string::npos);
    bkMoved = (castling.find('k') == std::string::npos && castling.find('q') == std::string::npos);
    wrHHMoved = (castling.find('K') == std::string::npos);
    wrAHMoved = (castling.find('Q') == std::string::npos);
    brHHMoved = (castling.find('k') == std::string::npos);
    brAHMoved = (castling.find('q') == std::string::npos);

    enPassantTarget = std::nullopt;
    if (parts[3] != "-") {
        const int ep_c = parts[3][0] - 'a';
        const int ep_r = 8 - (parts[3][1] - '0');
        if (inBounds(ep_r, ep_c)) {
            enPassantTarget = std::make_pair(ep_r, ep_c);
        }
    }

    halfmoveClock = (parts.size() > 4) ? std::stoi(parts[4]) : 0;
    fullmoveNumber = (parts.size() > 5) ? std::stoi(parts[5]) : 1;

    zobristKey = computeZobristFromState(*this);

    positionHashCounts[positionHash(*this)]++;
}

void GameState::initStandard()
{
    loadFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool isSquareAttacked(const GameState& gs, int r, int c, bool byWhite)
{
    const int sq = squareOf(r, c);
    const int attackerColor = colorIndex(byWhite);

    if (PAWN_ATTACKERS[attackerColor][sq] & gs.bitboards[attackerColor][pieceIndex(P)]) {
        return true;
    }

    if (KNIGHT_ATTACKS[sq] & gs.bitboards[attackerColor][pieceIndex(N)])
        return true;

    if (KING_ATTACKS[sq] & gs.bitboards[attackerColor][pieceIndex(K)])
        return true;

    const Bitboard bishopLike = bishopAttacks(sq, gs.occupancyBoth, RAYS);
    if (bishopLike & (gs.bitboards[attackerColor][pieceIndex(B)] | gs.bitboards[attackerColor][pieceIndex(Q)])) {
        return true;
    }

    const Bitboard rookLike = rookAttacks(sq, gs.occupancyBoth, RAYS);
    if (rookLike & (gs.bitboards[attackerColor][pieceIndex(R)] | gs.bitboards[attackerColor][pieceIndex(Q)])) {
        return true;
    }

    return false;
}

bool isInCheck(const GameState& gs, bool white)
{
    const Bitboard kingBB = gs.bitboards[colorIndex(white)][pieceIndex(K)];
    if (!kingBB) {
        return true;
    }
    const int kingSq = lsbSquare(kingBB);
    return isSquareAttacked(gs, rowOf(kingSq), colOf(kingSq), !white);
}

void generatePseudoLegalMoves(const GameState& gs, MoveList& moves)
{
    moves.clear();

    const bool wtm = gs.whiteToMove;
    const int us = colorIndex(wtm);
    const int them = colorIndex(!wtm);
    const Bitboard ownOcc = gs.occupancies[us];
    const Bitboard enemyOcc = gs.occupancies[them];

    Bitboard pawns = gs.bitboards[us][pieceIndex(P)];
    while (pawns) {
        const int sq = lsbSquare(popLsb(pawns));
        const int r = rowOf(sq);
        const int c = colOf(sq);

        const int oneStep = wtm ? sq - 8 : sq + 8;
        if (oneStep >= 0 && oneStep < 64 && !(gs.occupancyBoth & bitAt(oneStep))) {
            const int toR = rowOf(oneStep);
            const bool isPromotion = (wtm && toR == 0) || (!wtm && toR == 7);
            if (isPromotion) {
                for (int pt : { Q, R, B, N }) {
                    addMove(moves, sq, oneStep, EMPTY, false, false, true, pt);
                }
            } else {
                addMove(moves, sq, oneStep);
            }

            if ((wtm && r == 6) || (!wtm && r == 1)) {
                const int twoStep = wtm ? sq - 16 : sq + 16;
                if (!(gs.occupancyBoth & bitAt(twoStep))) {
                    addMove(moves, sq, twoStep);
                }
            }
        }

        Bitboard captures = PAWN_ATTACKS[us][sq] & enemyOcc;
        while (captures) {
            const int toSq = lsbSquare(popLsb(captures));
            const int capturedType = pieceAtSqImpl(gs, toSq).type;
            const int toR = rowOf(toSq);
            const bool isPromotion = (wtm && toR == 0) || (!wtm && toR == 7);
            if (isPromotion) {
                for (int pt : { Q, R, B, N }) {
                    addMove(moves, sq, toSq, capturedType, false, false, true, pt);
                }
            } else {
                addMove(moves, sq, toSq, capturedType);
            }
        }

        if (gs.enPassantTarget) {
            const auto [er, ec] = *gs.enPassantTarget;
            if ((wtm && r == 3) || (!wtm && r == 4)) {
                if (std::abs(c - ec) == 1 && ((wtm && er == r - 1) || (!wtm && er == r + 1))) {
                    addMove(moves, sq, squareOf(er, ec), P, true, false, false, Q);
                }
            }
        }
    }

    Bitboard knights = gs.bitboards[us][pieceIndex(N)];
    while (knights) {
        const int sq = lsbSquare(popLsb(knights));
        Bitboard attacks = KNIGHT_ATTACKS[sq] & ~ownOcc;
        while (attacks) {
            const int toSq = lsbSquare(popLsb(attacks));
            const int capturedType = (enemyOcc & bitAt(toSq))
                ? static_cast<int>(pieceAtSqImpl(gs, toSq).type)
                : static_cast<int>(EMPTY);
            addMove(moves, sq, toSq, capturedType);
        }
    }

    Bitboard bishops = gs.bitboards[us][pieceIndex(B)];
    while (bishops) {
        const int sq = lsbSquare(popLsb(bishops));
        Bitboard attacks = bishopAttacks(sq, gs.occupancyBoth, RAYS) & ~ownOcc;
        while (attacks) {
            const int toSq = lsbSquare(popLsb(attacks));
            const int capturedType = (enemyOcc & bitAt(toSq))
                ? static_cast<int>(pieceAtSqImpl(gs, toSq).type)
                : static_cast<int>(EMPTY);
            addMove(moves, sq, toSq, capturedType);
        }
    }

    Bitboard rooks = gs.bitboards[us][pieceIndex(R)];
    while (rooks) {
        const int sq = lsbSquare(popLsb(rooks));
        Bitboard attacks = rookAttacks(sq, gs.occupancyBoth, RAYS) & ~ownOcc;
        while (attacks) {
            const int toSq = lsbSquare(popLsb(attacks));
            const int capturedType = (enemyOcc & bitAt(toSq))
                ? static_cast<int>(pieceAtSqImpl(gs, toSq).type)
                : static_cast<int>(EMPTY);
            addMove(moves, sq, toSq, capturedType);
        }
    }

    Bitboard queens = gs.bitboards[us][pieceIndex(Q)];
    while (queens) {
        const int sq = lsbSquare(popLsb(queens));
        Bitboard attacks = (bishopAttacks(sq, gs.occupancyBoth, RAYS) | rookAttacks(sq, gs.occupancyBoth, RAYS)) & ~ownOcc;
        while (attacks) {
            const int toSq = lsbSquare(popLsb(attacks));
            const int capturedType = (enemyOcc & bitAt(toSq))
                ? static_cast<int>(pieceAtSqImpl(gs, toSq).type)
                : static_cast<int>(EMPTY);
            addMove(moves, sq, toSq, capturedType);
        }
    }

    Bitboard king = gs.bitboards[us][pieceIndex(K)];
    if (king) {
        const int sq = lsbSquare(king);
        Bitboard attacks = KING_ATTACKS[sq] & ~ownOcc;
        while (attacks) {
            const int toSq = lsbSquare(popLsb(attacks));
            const int capturedType = (enemyOcc & bitAt(toSq))
                ? static_cast<int>(pieceAtSqImpl(gs, toSq).type)
                : static_cast<int>(EMPTY);
            addMove(moves, sq, toSq, capturedType);
        }

        if (!isInCheck(gs, wtm)) {
            const Bitboard fMask = bitAt(squareOf(wtm ? 7 : 0, 5));
            const Bitboard gMask = bitAt(squareOf(wtm ? 7 : 0, 6));
            const Bitboard bMask = bitAt(squareOf(wtm ? 7 : 0, 1));
            const Bitboard cMask = bitAt(squareOf(wtm ? 7 : 0, 2));
            const Bitboard dMask = bitAt(squareOf(wtm ? 7 : 0, 3));

            if (wtm) {
                if (!gs.wkMoved && !gs.wrHHMoved && !(gs.occupancyBoth & (fMask | gMask))) {
                    if (!isSquareAttacked(gs, 7, 5, false) && !isSquareAttacked(gs, 7, 6, false)) {
                        addMove(moves, squareOf(7, 4), squareOf(7, 6), EMPTY, false, true);
                    }
                }
                if (!gs.wkMoved && !gs.wrAHMoved && !(gs.occupancyBoth & (bMask | cMask | dMask))) {
                    if (!isSquareAttacked(gs, 7, 3, false) && !isSquareAttacked(gs, 7, 2, false)) {
                        addMove(moves, squareOf(7, 4), squareOf(7, 2), EMPTY, false, true);
                    }
                }
            } else {
                if (!gs.bkMoved && !gs.brHHMoved && !(gs.occupancyBoth & (fMask | gMask))) {
                    if (!isSquareAttacked(gs, 0, 5, true) && !isSquareAttacked(gs, 0, 6, true)) {
                        addMove(moves, squareOf(0, 4), squareOf(0, 6), EMPTY, false, true);
                    }
                }
                if (!gs.bkMoved && !gs.brAHMoved && !(gs.occupancyBoth & (bMask | cMask | dMask))) {
                    if (!isSquareAttacked(gs, 0, 3, true) && !isSquareAttacked(gs, 0, 2, true)) {
                        addMove(moves, squareOf(0, 4), squareOf(0, 2), EMPTY, false, true);
                    }
                }
            }
        }
    }

}

std::vector<Move> generatePseudoLegalMoves(const GameState& gs)
{
    MoveList list;
    generatePseudoLegalMoves(gs, list);
    return std::vector<Move>(list.begin(), list.end());
}

void makeMove(GameState& gs, const Move& m, bool trackHistory)
{
    UndoState undo;
    undo.move = m;
    undo.whiteToMove = gs.whiteToMove;
    undo.wkMoved = gs.wkMoved;
    undo.wrAHMoved = gs.wrAHMoved;
    undo.wrHHMoved = gs.wrHHMoved;
    undo.bkMoved = gs.bkMoved;
    undo.brAHMoved = gs.brAHMoved;
    undo.brHHMoved = gs.brHHMoved;
    undo.enPassantTarget = gs.enPassantTarget;
    undo.halfmoveClock = gs.halfmoveClock;
    undo.fullmoveNumber = gs.fullmoveNumber;
    undo.zobristKey = gs.zobristKey;

    const int fromSq = m.from();
    const int toSq = m.to();
    const int fromR = rowOf(fromSq);
    const int toR = rowOf(toSq);
    const int toC = colOf(toSq);

    undo.movedPiece = pieceAtSqImpl(gs, fromSq);
    const Piece captured = m.isEnPassant() ? pieceAtSqImpl(gs, squareOf(fromR, toC)) : pieceAtSqImpl(gs, toSq);
    undo.capturedPiece = captured;
    undo.hadCapture = (captured.type != EMPTY);

    Bitboard key = gs.zobristKey;
    const int oldCastleMask = castlingRightsMask(gs);
    const int oldEpSq = epSquare(gs.enPassantTarget);

    key ^= ZOBRIST.sideToMove;
    key ^= ZOBRIST.castling[oldCastleMask];
    if (oldEpSq >= 0) {
        key ^= ZOBRIST.enPassant[oldEpSq];
    }

    key ^= pieceZobrist(undo.movedPiece, fromSq);

    if (m.isEnPassant()) {
        const int capSq = squareOf(fromR, toC);
        key ^= pieceZobrist(captured, capSq);
    } else if (captured.type != EMPTY) {
        key ^= pieceZobrist(captured, toSq);
    }

    Piece placedPiece = undo.movedPiece;
    if (m.isPromotion()) {
        placedPiece.type = m.promotionType();
    }
    key ^= pieceZobrist(placedPiece, toSq);

    if (m.isCastle()) {
        const int rookFrom = (toC == 6) ? squareOf(toR, 7) : squareOf(toR, 0);
        const int rookTo = (toC == 6) ? squareOf(toR, 5) : squareOf(toR, 3);
        const Piece rook { R, undo.movedPiece.white };
        key ^= pieceZobrist(rook, rookFrom);
        key ^= pieceZobrist(rook, rookTo);
    }

    applyMoveNoHistory(gs, m);

    const int newCastleMask = castlingRightsMask(gs);
    key ^= ZOBRIST.castling[newCastleMask];
    const int newEpSq = epSquare(gs.enPassantTarget);
    if (newEpSq >= 0) {
        key ^= ZOBRIST.enPassant[newEpSq];
    }
    gs.zobristKey = key;

    if (gs.undoTop < static_cast<int>(gs.undoStack.size())) {
        gs.undoStack[gs.undoTop++] = undo;
    }
    if (trackHistory) {
        gs.positionHashCounts[positionHash(gs)]++;
    }
}

void undoMove(GameState& gs, bool trackHistory)
{
    if (gs.undoTop <= 0)
        return;

    if (trackHistory) {
        const uint64_t key = positionHash(gs);
        auto it = gs.positionHashCounts.find(key);
        if (it != gs.positionHashCounts.end()) {
            it->second--;
            if (it->second <= 0) {
                gs.positionHashCounts.erase(it);
            }
        }
    }

    const UndoState undo = gs.undoStack[--gs.undoTop];

    const Move& m = undo.move;
    const int fromSq = m.from();
    const int toSq = m.to();
    const int fromR = rowOf(fromSq);
    const int toR = rowOf(toSq);
    const int toC = colOf(toSq);

    gs.whiteToMove = undo.whiteToMove;
    gs.wkMoved = undo.wkMoved;
    gs.wrAHMoved = undo.wrAHMoved;
    gs.wrHHMoved = undo.wrHHMoved;
    gs.bkMoved = undo.bkMoved;
    gs.brAHMoved = undo.brAHMoved;
    gs.brHHMoved = undo.brHHMoved;
    gs.enPassantTarget = undo.enPassantTarget;
    gs.halfmoveClock = undo.halfmoveClock;
    gs.fullmoveNumber = undo.fullmoveNumber;
    gs.zobristKey = undo.zobristKey;

    const Piece movedNow = pieceAtSqImpl(gs, toSq);
    removePiece(gs, toSq, movedNow);

    if (m.isCastle()) {
        if (toC == 6) {
            const int rookFrom = squareOf(toR, 5);
            const int rookTo = squareOf(toR, 7);
            const Piece rook = pieceAtSqImpl(gs, rookFrom);
            removePiece(gs, rookFrom, rook);
            placePiece(gs, rookTo, rook);
        } else {
            const int rookFrom = squareOf(toR, 3);
            const int rookTo = squareOf(toR, 0);
            const Piece rook = pieceAtSqImpl(gs, rookFrom);
            removePiece(gs, rookFrom, rook);
            placePiece(gs, rookTo, rook);
        }
    }

    placePiece(gs, fromSq, undo.movedPiece);

    if (undo.hadCapture) {
        if (m.isEnPassant()) {
            placePiece(gs, squareOf(fromR, toC), undo.capturedPiece);
        } else {
            placePiece(gs, toSq, undo.capturedPiece);
        }
    }

}

void generateLegalMoves(GameState& gs, MoveList& out)
{
    out.clear();
    const bool sideToCheck = gs.whiteToMove;
    MoveList pseudo;
    generatePseudoLegalMoves(gs, pseudo);

    for (int i = 0; i < pseudo.count; ++i) {
        const Move& m = pseudo.moves[i];
        makeMove(gs, m, false);
        if (!isInCheck(gs, sideToCheck)) {
            if (out.count < static_cast<int>(out.moves.size())) {
                out.moves[out.count++] = m;
            }
        }
        undoMove(gs, false);
    }
}

std::vector<Move> generateLegalMoves(GameState& gs)
{
    MoveList list;
    generateLegalMoves(gs, list);
    return std::vector<Move>(list.begin(), list.end());
}

bool hasSufficientMaterial(const GameState& gs)
{
    const Bitboard wP = gs.bitboards[WHITE][pieceIndex(P)];
    const Bitboard bP = gs.bitboards[BLACK][pieceIndex(P)];
    const Bitboard wR = gs.bitboards[WHITE][pieceIndex(R)];
    const Bitboard bR = gs.bitboards[BLACK][pieceIndex(R)];
    const Bitboard wQ = gs.bitboards[WHITE][pieceIndex(Q)];
    const Bitboard bQ = gs.bitboards[BLACK][pieceIndex(Q)];
    if (wP || bP || wR || bR || wQ || bQ) {
        return true;
    }

    const int whiteKnights = __builtin_popcountll(gs.bitboards[WHITE][pieceIndex(N)]);
    const int blackKnights = __builtin_popcountll(gs.bitboards[BLACK][pieceIndex(N)]);
    const int whiteBishops = __builtin_popcountll(gs.bitboards[WHITE][pieceIndex(B)]);
    const int blackBishops = __builtin_popcountll(gs.bitboards[BLACK][pieceIndex(B)]);

    int whiteBishopColor = -1;
    int blackBishopColor = -1;

    if (whiteBishops == 1) {
        const int sq = lsbSquare(gs.bitboards[WHITE][pieceIndex(B)]);
        whiteBishopColor = (rowOf(sq) + colOf(sq)) % 2;
    }
    if (blackBishops == 1) {
        const int sq = lsbSquare(gs.bitboards[BLACK][pieceIndex(B)]);
        blackBishopColor = (rowOf(sq) + colOf(sq)) % 2;
    }

    if (whiteKnights == 0 && whiteBishops == 0 && blackKnights == 0 && blackBishops == 0)
        return false;
    if (whiteKnights == 1 && whiteBishops == 0 && blackKnights == 0 && blackBishops == 0)
        return false;
    if (whiteKnights == 0 && whiteBishops == 1 && blackKnights == 0 && blackBishops == 0)
        return false;
    if (blackKnights == 1 && blackBishops == 0 && whiteKnights == 0 && whiteBishops == 0)
        return false;
    if (blackKnights == 0 && blackBishops == 1 && whiteKnights == 0 && whiteBishops == 0)
        return false;
    if (whiteKnights == 0 && blackKnights == 0 && whiteBishops == 1 && blackBishops == 1) {
        if (whiteBishopColor == blackBishopColor)
            return false;
    }

    return true;
}

std::string boardToString(const GameState& gs)
{
    std::stringstream ss;
    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = pieceAtSqImpl(gs, sq);
        if (p.type == EMPTY) {
            ss << '.';
        } else {
            char ch = '?';
            switch (p.type) {
            case P: ch = 'p'; break;
            case N: ch = 'n'; break;
            case B: ch = 'b'; break;
            case R: ch = 'r'; break;
            case Q: ch = 'q'; break;
            case K: ch = 'k'; break;
            }
            ss << (p.white ? static_cast<char>(std::toupper(ch)) : ch);
        }
    }

    // Repetition identity must include side-to-move, castling rights, and en-passant target.
    ss << ' ' << (gs.whiteToMove ? 'w' : 'b');

    std::string castling;
    if (!gs.wkMoved && !gs.wrHHMoved) castling.push_back('K');
    if (!gs.wkMoved && !gs.wrAHMoved) castling.push_back('Q');
    if (!gs.bkMoved && !gs.brHHMoved) castling.push_back('k');
    if (!gs.bkMoved && !gs.brAHMoved) castling.push_back('q');
    if (castling.empty()) castling = "-";
    ss << ' ' << castling;

    if (gs.enPassantTarget.has_value()) {
        ss << ' ' << static_cast<char>('a' + gs.enPassantTarget->second);
    } else {
        ss << " -";
    }

    return ss.str();
}

std::uint64_t positionHash(const GameState& gs)
{
    return gs.zobristKey;
}

std::uint64_t recomputePositionHash(const GameState& gs)
{
    return computeZobristFromState(gs);
}

std::optional<std::string> checkGameOver(GameState& gs)
{
    auto legalMoves = generateLegalMoves(gs);
    if (legalMoves.empty()) {
        if (isInCheck(gs, gs.whiteToMove))
            return std::string(gs.whiteToMove ? "Checkmate! Black wins." : "Checkmate! White wins.");
        return std::string("Stalemate! It's a draw.");
    }

    if (gs.halfmoveClock >= 100)
        return std::string("Draw by 50-move rule.");
    if (!hasSufficientMaterial(gs))
        return std::string("Draw by insufficient material.");

    std::uint64_t key = positionHash(gs);
    auto it = gs.positionHashCounts.find(key);
    if (it != gs.positionHashCounts.end() && it->second >= 3)
        return std::string("Draw by threefold repetition.");

    return std::nullopt;
}
