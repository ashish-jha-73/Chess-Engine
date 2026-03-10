#pragma once
#include <array>
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
    int sx, sy, dx, dy;
    std::optional<Piece> captured = std::nullopt;
    bool isEnPassant = false;
    bool isCastle = false;
    bool promotion = false;
    int promotionType = Q;
};

typedef std::array<std::array<Piece, 8>, 8> Board;
std::string boardToString(const Board& board);

struct GameState {
    Board board;
    bool whiteToMove = true;
    bool wkMoved = false, wrAHMoved = false, wrHHMoved = false;
    bool bkMoved = false, brAHMoved = false, brHHMoved = false;
    std::optional<std::pair<int, int>> enPassantTarget;
    std::vector<Move> history;
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
void makeMove(GameState& gs, const Move& m);
void undoMove(GameState& gs);
std::vector<Move> generateLegalMoves(const GameState& gs);
bool hasSufficientMaterial(const GameState& gs);
std::optional<std::string> checkGameOver(GameState& gs);