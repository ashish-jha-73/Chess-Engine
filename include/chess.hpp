#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

enum PieceType : std::uint8_t {
    EMPTY = 0,
    P = 1,
    N = 2,
    B = 3,
    R = 4,
    Q = 5,
    K = 6
};

struct GameState;

struct Piece {
    std::uint8_t type = EMPTY;
    bool white = false;
};

struct Move {
    std::uint32_t value = 0;

    static constexpr std::uint8_t FLAG_CAPTURE = 1 << 0;
    static constexpr std::uint8_t FLAG_EN_PASSANT = 1 << 1;
    static constexpr std::uint8_t FLAG_CASTLE = 1 << 2;
    static constexpr std::uint8_t FLAG_PROMOTION = 1 << 3;

    static constexpr std::uint32_t FROM_SHIFT = 0;
    static constexpr std::uint32_t TO_SHIFT = 6;
    static constexpr std::uint32_t FLAGS_SHIFT = 12;
    static constexpr std::uint32_t PROMO_SHIFT = 16;
    static constexpr std::uint32_t CAPTURE_SHIFT = 20;

    static constexpr std::uint32_t FROM_MASK = 0x3Fu;
    static constexpr std::uint32_t TO_MASK = 0x3Fu;
    static constexpr std::uint32_t FLAGS_MASK = 0x0Fu;
    static constexpr std::uint32_t PROMO_MASK = 0x07u;
    static constexpr std::uint32_t CAPTURE_MASK = 0x07u;

    Move() = default;
    Move(std::uint8_t fromSq, std::uint8_t toSq, std::uint8_t moveFlags = 0,
        std::uint8_t promoType = Q, std::uint8_t capType = EMPTY)
    {
        setFrom(fromSq);
        setTo(toSq);
        setFlags(moveFlags);
        setPromotionType(promoType);
        setCapturedType(capType);
    }

    std::uint8_t from() const { return (value >> FROM_SHIFT) & FROM_MASK; }
    std::uint8_t to() const { return (value >> TO_SHIFT) & TO_MASK; }
    std::uint8_t flags() const { return (value >> FLAGS_SHIFT) & FLAGS_MASK; }
    std::uint8_t promotionType() const { return (value >> PROMO_SHIFT) & PROMO_MASK; }
    std::uint8_t capturedType() const { return (value >> CAPTURE_SHIFT) & CAPTURE_MASK; }

    void setFrom(std::uint8_t fromSq)
    {
        value = (value & ~(FROM_MASK << FROM_SHIFT)) | ((static_cast<std::uint32_t>(fromSq) & FROM_MASK) << FROM_SHIFT);
    }

    void setTo(std::uint8_t toSq)
    {
        value = (value & ~(TO_MASK << TO_SHIFT)) | ((static_cast<std::uint32_t>(toSq) & TO_MASK) << TO_SHIFT);
    }

    void setFlags(std::uint8_t moveFlags)
    {
        value = (value & ~(FLAGS_MASK << FLAGS_SHIFT)) | ((static_cast<std::uint32_t>(moveFlags) & FLAGS_MASK) << FLAGS_SHIFT);
    }

    void addFlags(std::uint8_t moveFlags)
    {
        setFlags(flags() | moveFlags);
    }

    void setPromotionType(std::uint8_t promoType)
    {
        value = (value & ~(PROMO_MASK << PROMO_SHIFT)) | ((static_cast<std::uint32_t>(promoType) & PROMO_MASK) << PROMO_SHIFT);
    }

    void setCapturedType(std::uint8_t capType)
    {
        value = (value & ~(CAPTURE_MASK << CAPTURE_SHIFT)) | ((static_cast<std::uint32_t>(capType) & CAPTURE_MASK) << CAPTURE_SHIFT);
    }

    bool isCapture() const { return (flags() & FLAG_CAPTURE) != 0; }
    bool isEnPassant() const { return (flags() & FLAG_EN_PASSANT) != 0; }
    bool isCastle() const { return (flags() & FLAG_CASTLE) != 0; }
    bool isPromotion() const { return (flags() & FLAG_PROMOTION) != 0; }
};

struct MoveList {
    std::array<Move, 256> moves{};
    int count = 0;

    void clear() { count = 0; }
    bool empty() const { return count == 0; }
    Move& operator[](int i) { return moves[i]; }
    const Move& operator[](int i) const { return moves[i]; }
    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + count; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }
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

Piece pieceAt(const GameState& gs, int r, int c);
Piece pieceAtSq(const GameState& gs, int sq);
std::string boardToString(const GameState& gs);
std::uint64_t positionHash(const GameState& gs);

struct GameState {
    std::array<std::array<Bitboard, 6>, 2> bitboards{};
    std::array<std::uint8_t, 64> pieceOnSquare{};
    std::array<Bitboard, 2> occupancies{};
    Bitboard occupancyBoth = 0;
    bool whiteToMove = true;
    bool wkMoved = false, wrAHMoved = false, wrHHMoved = false;
    bool bkMoved = false, brAHMoved = false, brHHMoved = false;
    std::optional<std::pair<int, int>> enPassantTarget;
    std::array<UndoState, 512> undoStack{};
    int undoTop = 0;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
    std::unordered_map<std::uint64_t, int> positionHashCounts;
    std::string initialFen;

    void initStandard();
    void loadFromFen(const std::string& fen);
};

bool isSquareAttacked(const GameState& gs, int r, int c, bool byWhite);
bool isInCheck(const GameState& gs, bool white);
void generatePseudoLegalMoves(const GameState& gs, MoveList& out);
void makeMove(GameState& gs, const Move& m, bool trackHistory = true);
void undoMove(GameState& gs, bool trackHistory = true);
void generateLegalMoves(GameState& gs, MoveList& out);
std::vector<Move> generatePseudoLegalMoves(const GameState& gs);
std::vector<Move> generateLegalMoves(GameState& gs);
bool hasSufficientMaterial(const GameState& gs);
std::optional<std::string> checkGameOver(GameState& gs);