#include "../include/chess.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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

void rebuildOccupancies(GameState& gs)
{
    gs.occupancies[WHITE] = 0;
    gs.occupancies[BLACK] = 0;
    for (int pt = 0; pt < 6; ++pt) {
        gs.occupancies[WHITE] |= gs.bitboards[WHITE][pt];
        gs.occupancies[BLACK] |= gs.bitboards[BLACK][pt];
    }
    gs.occupancyBoth = gs.occupancies[WHITE] | gs.occupancies[BLACK];
}

void clearBitboards(GameState& gs)
{
    for (auto& color : gs.bitboards) {
        color.fill(0ULL);
    }
    gs.occupancies = { 0ULL, 0ULL };
    gs.occupancyBoth = 0ULL;
}

void placePiece(GameState& gs, int sq, const Piece& p)
{
    if (p.type == EMPTY) {
        return;
    }
    gs.bitboards[colorIndex(p.white)][pieceIndex(p.type)] |= bitAt(sq);
}

void removePiece(GameState& gs, int sq, const Piece& p)
{
    if (p.type == EMPTY) {
        return;
    }
    gs.bitboards[colorIndex(p.white)][pieceIndex(p.type)] &= ~bitAt(sq);
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

const std::array<Bitboard, 64> KNIGHT_ATTACKS = initKnightAttacks();
const std::array<Bitboard, 64> KING_ATTACKS = initKingAttacks();

void addMove(std::vector<Move>& moves, int fromSq, int toSq, int capturedType = EMPTY,
    bool isEnPassant = false, bool isCastle = false, bool promotion = false, int promotionType = Q)
{
    Move m;
    m.from = static_cast<std::uint8_t>(fromSq);
    m.to = static_cast<std::uint8_t>(toSq);
    m.promotionType = static_cast<std::uint8_t>(promotionType);
    m.capturedType = static_cast<std::uint8_t>(capturedType);
    if (capturedType != EMPTY) {
        m.flags |= Move::FLAG_CAPTURE;
    }
    if (isEnPassant) {
        m.flags |= Move::FLAG_EN_PASSANT;
    }
    if (isCastle) {
        m.flags |= Move::FLAG_CASTLE;
    }
    if (promotion) {
        m.flags |= Move::FLAG_PROMOTION;
    }
    moves.push_back(m);
}

void applyMoveNoHistory(GameState& gs, const Move& m)
{
    const int fromSq = m.from;
    const int toSq = m.to;
    const int fromR = rowOf(fromSq);
    const int fromC = colOf(fromSq);
    const int toR = rowOf(toSq);
    const int toC = colOf(toSq);

    Piece movingPiece = gs.board[fromR][fromC];
    Piece capturedPiece { EMPTY, false };

    if (m.isEnPassant()) {
        capturedPiece = gs.board[fromR][toC];
    } else {
        capturedPiece = gs.board[toR][toC];
    }

    removePiece(gs, fromSq, movingPiece);
    gs.board[fromR][fromC] = { EMPTY, false };

    if (m.isEnPassant()) {
        const int capSq = squareOf(fromR, toC);
        removePiece(gs, capSq, capturedPiece);
        gs.board[fromR][toC] = { EMPTY, false };
    } else if (capturedPiece.type != EMPTY) {
        removePiece(gs, toSq, capturedPiece);
    }

    if (m.isCastle()) {
        gs.board[toR][toC] = movingPiece;
        placePiece(gs, toSq, movingPiece);

        if (toC == 6) {
            const Piece rook = gs.board[toR][7];
            const int rookFrom = squareOf(toR, 7);
            const int rookTo = squareOf(toR, 5);
            removePiece(gs, rookFrom, rook);
            gs.board[toR][7] = { EMPTY, false };
            gs.board[toR][5] = rook;
            placePiece(gs, rookTo, rook);
        } else {
            const Piece rook = gs.board[toR][0];
            const int rookFrom = squareOf(toR, 0);
            const int rookTo = squareOf(toR, 3);
            removePiece(gs, rookFrom, rook);
            gs.board[toR][0] = { EMPTY, false };
            gs.board[toR][3] = rook;
            placePiece(gs, rookTo, rook);
        }
    } else {
        Piece placed = movingPiece;
        if (m.isPromotion()) {
            placed.type = m.promotionType;
        }
        gs.board[toR][toC] = placed;
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
    rebuildOccupancies(gs);
}
}

void GameState::loadFromFen(const std::string& fen)
{
    board.fill({});
    history.clear();
    undoStack.clear();
    positionCounts.clear();
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
        case 'p':
            p.type = P;
            break;
        case 'n':
            p.type = N;
            break;
        case 'b':
            p.type = B;
            break;
        case 'r':
            p.type = R;
            break;
        case 'q':
            p.type = Q;
            break;
        case 'k':
            p.type = K;
            break;
        default:
            p.type = EMPTY;
            break;
        }

        if (inBounds(r, c) && p.type != EMPTY) {
            board[r][c] = p;
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

    rebuildOccupancies(*this);
    positionCounts[boardToString(board)]++;
}

void GameState::initStandard()
{
    loadFromFen("2k3r/8/8/8/8/8/8/2K w - - 0 1");
}

bool isSquareAttacked(const Board& board, int r, int c, bool byWhite)
{
    const int dir = byWhite ? 1 : -1;
    const int pr = r + dir;
    if (inBounds(pr, c - 1) && board[pr][c - 1].type == P && board[pr][c - 1].white == byWhite)
        return true;
    if (inBounds(pr, c + 1) && board[pr][c + 1].type == P && board[pr][c + 1].white == byWhite)
        return true;

    static constexpr int kn[8][2] = { { -2, -1 }, { -2, 1 }, { -1, -2 }, { -1, 2 }, { 1, -2 }, { 1, 2 }, { 2, -1 }, { 2, 1 } };
    for (const auto& d : kn) {
        const int nr = r + d[0];
        const int nc = c + d[1];
        if (inBounds(nr, nc) && board[nr][nc].type == N && board[nr][nc].white == byWhite)
            return true;
    }

    static constexpr int diag[4][2] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    for (const auto& d : diag) {
        int nr = r + d[0];
        int nc = c + d[1];
        while (inBounds(nr, nc)) {
            if (board[nr][nc].type != EMPTY) {
                if ((board[nr][nc].type == B || board[nr][nc].type == Q) && board[nr][nc].white == byWhite)
                    return true;
                break;
            }
            nr += d[0];
            nc += d[1];
        }
    }

    static constexpr int ortho[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (const auto& d : ortho) {
        int nr = r + d[0];
        int nc = c + d[1];
        while (inBounds(nr, nc)) {
            if (board[nr][nc].type != EMPTY) {
                if ((board[nr][nc].type == R || board[nr][nc].type == Q) && board[nr][nc].white == byWhite)
                    return true;
                break;
            }
            nr += d[0];
            nc += d[1];
        }
    }

    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0)
                continue;
            const int nr = r + dr;
            const int nc = c + dc;
            if (inBounds(nr, nc) && board[nr][nc].type == K && board[nr][nc].white == byWhite)
                return true;
        }
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
    const int kr = rowOf(kingSq);
    const int kc = colOf(kingSq);
    return isSquareAttacked(gs.board, kr, kc, !white);
}

std::vector<Move> generatePseudoLegalMoves(const GameState& gs)
{
    std::vector<Move> moves;
    moves.reserve(128);

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

        for (int dc : { -1, 1 }) {
            const int nc = c + dc;
            if (nc < 0 || nc > 7)
                continue;
            const int toSq = wtm ? sq - 8 + dc : sq + 8 + dc;
            if (toSq < 0 || toSq >= 64)
                continue;

            if (enemyOcc & bitAt(toSq)) {
                const int toR = rowOf(toSq);
                const int toC = colOf(toSq);
                const Piece captured = gs.board[toR][toC];
                const bool isPromotion = (wtm && toR == 0) || (!wtm && toR == 7);
                if (isPromotion) {
                    for (int pt : { Q, R, B, N }) {
                        addMove(moves, sq, toSq, captured.type, false, false, true, pt);
                    }
                } else {
                    addMove(moves, sq, toSq, captured.type);
                }
            }
        }

        if (gs.enPassantTarget) {
            const auto [er, ec] = *gs.enPassantTarget;
            if ((wtm && r == 3) || (!wtm && r == 4)) {
                if (std::abs(c - ec) == 1 && ((wtm && er == r - 1) || (!wtm && er == r + 1))) {
                    addMove(moves, sq, squareOf(er, ec), gs.board[r][ec].type, true, false, false, Q);
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
            const Piece p = gs.board[rowOf(toSq)][colOf(toSq)];
            addMove(moves, sq, toSq, p.type);
        }
    }

    auto genSliding = [&](int type, const std::vector<std::pair<int, int>>& dirs) {
        Bitboard bb = gs.bitboards[us][pieceIndex(type)];
        while (bb) {
            const int sq = lsbSquare(popLsb(bb));
            const int r = rowOf(sq);
            const int c = colOf(sq);
            for (const auto& [dr, dc] : dirs) {
                int nr = r + dr;
                int nc = c + dc;
                while (inBounds(nr, nc)) {
                    const int toSq = squareOf(nr, nc);
                    const Bitboard mask = bitAt(toSq);
                    if (!(gs.occupancyBoth & mask)) {
                        addMove(moves, sq, toSq);
                    } else {
                        if (enemyOcc & mask) {
                            addMove(moves, sq, toSq, gs.board[nr][nc].type);
                        }
                        break;
                    }
                    nr += dr;
                    nc += dc;
                }
            }
        }
    };

    genSliding(B, { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } });
    genSliding(R, { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } });
    genSliding(Q, { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } });

    Bitboard king = gs.bitboards[us][pieceIndex(K)];
    if (king) {
        const int sq = lsbSquare(king);
        Bitboard attacks = KING_ATTACKS[sq] & ~ownOcc;
        while (attacks) {
            const int toSq = lsbSquare(popLsb(attacks));
            const Piece p = gs.board[rowOf(toSq)][colOf(toSq)];
            addMove(moves, sq, toSq, p.type);
        }

        if (!isInCheck(gs, wtm)) {
            if (wtm) {
                if (!gs.wkMoved && !gs.wrHHMoved && gs.board[7][5].type == EMPTY && gs.board[7][6].type == EMPTY) {
                    if (!isSquareAttacked(gs.board, 7, 5, false) && !isSquareAttacked(gs.board, 7, 6, false)) {
                        addMove(moves, squareOf(7, 4), squareOf(7, 6), EMPTY, false, true);
                    }
                }
                if (!gs.wkMoved && !gs.wrAHMoved && gs.board[7][1].type == EMPTY && gs.board[7][2].type == EMPTY && gs.board[7][3].type == EMPTY) {
                    if (!isSquareAttacked(gs.board, 7, 3, false) && !isSquareAttacked(gs.board, 7, 2, false)) {
                        addMove(moves, squareOf(7, 4), squareOf(7, 2), EMPTY, false, true);
                    }
                }
            } else {
                if (!gs.bkMoved && !gs.brHHMoved && gs.board[0][5].type == EMPTY && gs.board[0][6].type == EMPTY) {
                    if (!isSquareAttacked(gs.board, 0, 5, true) && !isSquareAttacked(gs.board, 0, 6, true)) {
                        addMove(moves, squareOf(0, 4), squareOf(0, 6), EMPTY, false, true);
                    }
                }
                if (!gs.bkMoved && !gs.brAHMoved && gs.board[0][1].type == EMPTY && gs.board[0][2].type == EMPTY && gs.board[0][3].type == EMPTY) {
                    if (!isSquareAttacked(gs.board, 0, 3, true) && !isSquareAttacked(gs.board, 0, 2, true)) {
                        addMove(moves, squareOf(0, 4), squareOf(0, 2), EMPTY, false, true);
                    }
                }
            }
        }
    }

    return moves;
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

    const int fromSq = m.from;
    const int toSq = m.to;
    const int fromR = rowOf(fromSq);
    const int fromC = colOf(fromSq);
    const int toR = rowOf(toSq);
    const int toC = colOf(toSq);

    undo.movedPiece = gs.board[fromR][fromC];
    const Piece captured = m.isEnPassant() ? gs.board[fromR][toC] : gs.board[toR][toC];
    undo.capturedPiece = captured;
    undo.hadCapture = (captured.type != EMPTY);

    applyMoveNoHistory(gs, m);

    gs.undoStack.push_back(undo);
    if (trackHistory) {
        gs.history.push_back(m);
        gs.positionCounts[boardToString(gs.board)]++;
    }
}

void undoMove(GameState& gs, bool trackHistory)
{
    if (gs.undoStack.empty())
        return;

    if (trackHistory) {
        const std::string currentPos = boardToString(gs.board);
        auto it = gs.positionCounts.find(currentPos);
        if (it != gs.positionCounts.end()) {
            it->second--;
            if (it->second <= 0) {
                gs.positionCounts.erase(it);
            }
        }
    }

    const UndoState undo = gs.undoStack.back();
    gs.undoStack.pop_back();
    if (trackHistory && !gs.history.empty()) {
        gs.history.pop_back();
    }

    const Move& m = undo.move;
    const int fromSq = m.from;
    const int toSq = m.to;
    const int fromR = rowOf(fromSq);
    const int fromC = colOf(fromSq);
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

    const Piece movedNow = gs.board[toR][toC];
    removePiece(gs, toSq, movedNow);
    gs.board[toR][toC] = { EMPTY, false };

    if (m.isCastle()) {
        if (toC == 6) {
            const Piece rook = gs.board[toR][5];
            removePiece(gs, squareOf(toR, 5), rook);
            gs.board[toR][5] = { EMPTY, false };
            gs.board[toR][7] = rook;
            placePiece(gs, squareOf(toR, 7), rook);
        } else {
            const Piece rook = gs.board[toR][3];
            removePiece(gs, squareOf(toR, 3), rook);
            gs.board[toR][3] = { EMPTY, false };
            gs.board[toR][0] = rook;
            placePiece(gs, squareOf(toR, 0), rook);
        }
    }

    gs.board[fromR][fromC] = undo.movedPiece;
    placePiece(gs, fromSq, undo.movedPiece);

    if (undo.hadCapture) {
        if (m.isEnPassant()) {
            gs.board[fromR][toC] = undo.capturedPiece;
            placePiece(gs, squareOf(fromR, toC), undo.capturedPiece);
        } else {
            gs.board[toR][toC] = undo.capturedPiece;
            placePiece(gs, toSq, undo.capturedPiece);
        }
    }

    rebuildOccupancies(gs);
}

std::vector<Move> generateLegalMoves(GameState& gs)
{
    std::vector<Move> out;
    const bool sideToCheck = gs.whiteToMove;
    auto pseudo = generatePseudoLegalMoves(gs);
    out.reserve(pseudo.size());

    for (const auto& m : pseudo) {
        makeMove(gs, m, false);
        if (!isInCheck(gs, sideToCheck)) {
            out.push_back(m);
        }
        undoMove(gs, false);
    }

    return out;
}

bool hasSufficientMaterial(const GameState& gs)
{
    int whiteKnights = 0;
    int whiteBishops = 0;
    int blackKnights = 0;
    int blackBishops = 0;
    int whiteBishopColor = -1;
    int blackBishopColor = -1;

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece p = gs.board[r][c];
            if (p.type == Q || p.type == R || p.type == P)
                return true;
            if (p.type == N) {
                if (p.white)
                    whiteKnights++;
                else
                    blackKnights++;
            }
            if (p.type == B) {
                if (p.white) {
                    whiteBishops++;
                    whiteBishopColor = (r + c) % 2;
                } else {
                    blackBishops++;
                    blackBishopColor = (r + c) % 2;
                }
            }
        }
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

std::string boardToString(const Board& board)
{
    std::stringstream ss;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece p = board[r][c];
            if (p.type == EMPTY) {
                ss << '.';
            } else {
                char ch = '?';
                switch (p.type) {
                case P:
                    ch = 'p';
                    break;
                case N:
                    ch = 'n';
                    break;
                case B:
                    ch = 'b';
                    break;
                case R:
                    ch = 'r';
                    break;
                case Q:
                    ch = 'q';
                    break;
                case K:
                    ch = 'k';
                    break;
                }
                ss << (p.white ? static_cast<char>(std::toupper(ch)) : ch);
            }
        }
    }
    return ss.str();
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

    std::string currentPos = boardToString(gs.board);
    if (gs.positionCounts[currentPos] >= 3)
        return std::string("Draw by threefold repetition.");

    return std::nullopt;
}
