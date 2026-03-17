#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <map>

enum PieceType {
    EMPTY = 0,
    P = 1,
    N = 2,
    B = 3,
    R = 4,
    Q = 5,
    K = 6
};

struct Piece {
    int type = EMPTY;
    bool white = false;
};

struct Move {
    std::uint8_t from = 0;
    std::uint8_t to = 0;
    std::uint8_t flags = 0;
    std::uint8_t promotionType = Q;
    std::uint8_t capturedType = EMPTY;

    static constexpr std::uint8_t FLAG_CAPTURE = 1 << 0;
    static constexpr std::uint8_t FLAG_EN_PASSANT = 1 << 1;
    static constexpr std::uint8_t FLAG_CASTLE = 1 << 2;
    static constexpr std::uint8_t FLAG_PROMOTION = 1 << 3;

    bool isCapture() const { return (flags & FLAG_CAPTURE) != 0; }
    bool isEnPassant() const { return (flags & FLAG_EN_PASSANT) != 0; }
    bool isCastle() const { return (flags & FLAG_CASTLE) != 0; }
    bool isPromotion() const { return (flags & FLAG_PROMOTION) != 0; }
};

using Bitboard = std::uint64_t;

struct UndoState {
    Move move;
    Piece movedPiece;
    Piece capturedPiece;
    bool hadCapture = false;
    bool whiteToMove = true;
    bool wkMoved = false, wrAHMoved = false, wrHHMoved = false;
    bool bkMoved = false, brAHMoved = false, brHHMoved = false;
    std::optional<std::pair<int, int>> enPassantTarget;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
};

typedef std::array<std::array<Piece, 8>, 8> Board;
std::string boardToString(const Board& board);

struct GameState {
    Board board;
    std::array<std::array<Bitboard, 6>, 2> bitboards{};
    std::array<Bitboard, 2> occupancies{};
    Bitboard occupancyBoth = 0;
    bool whiteToMove = true;
    bool wkMoved = false, wrAHMoved = false, wrHHMoved = false;
    bool bkMoved = false, brAHMoved = false, brHHMoved = false;
    std::optional<std::pair<int, int>> enPassantTarget;
    std::vector<Move> history;
    std::vector<UndoState> undoStack;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
    std::map<std::string, int> positionCounts;
    std::string initialFen;

    void initStandard();
    void loadFromFen(const std::string& fen);
};

bool isSquareAttacked(const Board& board, int r, int c, bool byWhite);
bool isInCheck(const GameState& gs, bool white);
std::vector<Move> generatePseudoLegalMoves(const GameState& gs);
void makeMove(GameState& gs, const Move& m, bool trackHistory = true);
void undoMove(GameState& gs, bool trackHistory = true);
std::vector<Move> generateLegalMoves(GameState& gs);
bool hasSufficientMaterial(const GameState& gs);
std::optional<std::string> checkGameOver(GameState& gs);