#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <iostream>
#include <map>
#include <functional>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

#include "../include/chess.hpp"
#include "../include/engine.hpp"

using namespace std;

bool inBounds(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }

static int parseSquare(const std::string& s)
{
    if (s.size() != 2) return -1;
    const int file = s[0] - 'a';
    const int rank = s[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    const int row = 7 - rank;
    return row * 8 + file;
}

static std::string squareToUci(int sq)
{
    const int row = sq / 8;
    const int col = sq % 8;
    std::string out;
    out.push_back(static_cast<char>('a' + col));
    out.push_back(static_cast<char>('8' - row));
    return out;
}

static std::string moveToUci(const Move& m)
{
    if (m.value == 0xFFFFFFFFu) {
        return "0000";
    }
    std::string out = squareToUci(m.from()) + squareToUci(m.to());
    if (m.isPromotion()) {
        char p = 'q';
        if (m.promotionType() == N) p = 'n';
        else if (m.promotionType() == B) p = 'b';
        else if (m.promotionType() == R) p = 'r';
        out.push_back(p);
    }
    return out;
}

static bool parseUciMove(GameState& gs, const std::string& uci, Move& out)
{
    if (uci.size() < 4) return false;
    const int from = parseSquare(uci.substr(0, 2));
    const int to = parseSquare(uci.substr(2, 2));
    if (from < 0 || to < 0) return false;

    int promo = EMPTY;
    if (uci.size() >= 5) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4])));
        if (c == 'q') promo = Q;
        else if (c == 'r') promo = R;
        else if (c == 'b') promo = B;
        else if (c == 'n') promo = N;
    }

    MoveList legal;
    generateLegalMoves(gs, legal);
    for (int i = 0; i < legal.count; ++i) {
        const Move& m = legal.moves[i];
        if (m.from() != from || m.to() != to) continue;
        if (!m.isPromotion() && promo == EMPTY) {
            out = m;
            return true;
        }
        if (m.isPromotion() && promo != EMPTY && m.promotionType() == promo) {
            out = m;
            return true;
        }
    }
    return false;
}

static int computeTimeForGo(const GameState& gs,
                            int movetime,
                            int wtime,
                            int btime,
                            int winc,
                            int binc,
                            int movestogo)
{
    if (movetime > 0) {
        return std::max(50, movetime);
    }

    const bool white = gs.whiteToMove;
    const int remain = white ? wtime : btime;
    const int inc = white ? winc : binc;
    if (remain <= 0) {
        return 1000;
    }

    const int mtg = (movestogo > 0) ? movestogo : 25;
    int budget = remain / std::max(8, mtg) + inc / 2;
    budget = std::max(50, budget);
    budget = std::min(budget, std::max(50, remain - 50));
    return budget;
}

static int runUciLoop()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    GameState gs;
    gs.initStandard();

    int optionMoveOverheadMs = 30;
    int optionHashMb = 16;
    setHashSizeMb(optionHashMb);
    optionHashMb = getHashSizeMb();
    int optionThreads = 1;
    bool optionUseNnue = isUseNnue();
    std::string optionEvalFile = getNnueEvalFile();

    std::thread searchThread;
    std::atomic<bool> searchRunning { false };
    std::mutex ioMutex;

    auto stopAndJoinSearch = [&]() {
        if (searchRunning.load(std::memory_order_relaxed)) {
            requestStopSearch();
        }
        if (searchThread.joinable()) {
            searchThread.join();
        }
        searchRunning.store(false, std::memory_order_relaxed);
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "uci") {
            std::cout << "id name Ikshvaku\n";
            std::cout << "id author Ashish\n";
            std::cout << "option name Hash type spin default 16 min 1 max 2048\n";
            std::cout << "option name Threads type spin default 1 min 1 max 1\n";
            std::cout << "option name Move Overhead type spin default 30 min 0 max 5000\n";
            std::cout << "option name UseNNUE type check default " << (optionUseNnue ? "true" : "false") << "\n";
            std::cout << "option name EvalFile type string default " << optionEvalFile << "\n";
            std::cout << "uciok\n";
        } else if (cmd == "isready") {
            if (optionUseNnue) {
                std::cout << "info string nnue " << (isNnueReady() ? "loaded" : "fallback-classical") << "\n";
            }
            std::cout << "readyok\n";
        } else if (cmd == "ucinewgame") {
            stopAndJoinSearch();
            gs.initStandard();
        } else if (cmd == "position") {
            stopAndJoinSearch();
            std::string token;
            iss >> token;
            if (token == "startpos") {
                gs.initStandard();
                if (iss >> token && token == "moves") {
                    std::string mv;
                    while (iss >> mv) {
                        Move m;
                        if (parseUciMove(gs, mv, m)) {
                            makeMove(gs, m);
                        }
                    }
                }
            } else if (token == "fen") {
                std::vector<std::string> parts;
                std::string t;
                for (int i = 0; i < 6 && iss >> t; ++i) {
                    parts.push_back(t);
                }
                if (parts.size() == 6) {
                    std::string fen;
                    for (int i = 0; i < 6; ++i) {
                        if (i) fen += ' ';
                        fen += parts[i];
                    }
                    gs.loadFromFen(fen);
                }

                if (iss >> token && token == "moves") {
                    std::string mv;
                    while (iss >> mv) {
                        Move m;
                        if (parseUciMove(gs, mv, m)) {
                            makeMove(gs, m);
                        }
                    }
                }
            }
        } else if (cmd == "go") {
            stopAndJoinSearch();

            int movetime = -1;
            int wtime = -1, btime = -1, winc = 0, binc = 0;
            int movestogo = -1;
            int depth = -1;
            bool infinite = false;
            bool ponder = false;

            std::string t;
            while (iss >> t) {
                if (t == "movetime") iss >> movetime;
                else if (t == "wtime") iss >> wtime;
                else if (t == "btime") iss >> btime;
                else if (t == "winc") iss >> winc;
                else if (t == "binc") iss >> binc;
                else if (t == "movestogo") iss >> movestogo;
                else if (t == "depth") iss >> depth;
                else if (t == "infinite") infinite = true;
                else if (t == "ponder") ponder = true;
                else if (t == "nodes" || t == "mate") {
                    int ignore = 0;
                    iss >> ignore;
                }
            }

            GameState gsCopy = gs;
            searchRunning.store(true, std::memory_order_relaxed);
            clearStopSearch();

            searchThread = std::thread([&, gsCopy, depth, movetime, wtime, btime, winc, binc, movestogo, infinite, ponder, optionMoveOverheadMs]() mutable {
                int timeMs = 600000;
                int maxDepth = 64;

                if (depth > 0) {
                    maxDepth = depth;
                    if (!infinite && !ponder) {
                        timeMs = 600000;
                    }
                } else if (infinite || ponder) {
                    timeMs = 600000;
                } else {
                    timeMs = computeTimeForGo(gsCopy, movetime, wtime, btime, winc, binc, movestogo);
                    timeMs = std::max(50, timeMs - optionMoveOverheadMs);
                }

                Move best = computeBestMove(gsCopy, maxDepth, timeMs);

                {
                    std::lock_guard<std::mutex> lock(ioMutex);
                    std::cout << "bestmove " << moveToUci(best) << "\n";
                    std::cout.flush();
                }
                searchRunning.store(false, std::memory_order_relaxed);
            });
        } else if (cmd == "stop") {
            stopAndJoinSearch();
        } else if (cmd == "setoption") {
            // Format: setoption name <name> [value <value>]
            std::vector<std::string> tokens;
            std::string tok;
            while (iss >> tok) tokens.push_back(tok);

            size_t namePos = std::find(tokens.begin(), tokens.end(), "name") - tokens.begin();
            if (namePos >= tokens.size()) {
                continue;
            }
            size_t valuePos = std::find(tokens.begin(), tokens.end(), "value") - tokens.begin();

            std::string name;
            std::string value;
            if (valuePos < tokens.size()) {
                for (size_t i = namePos + 1; i < valuePos; ++i) {
                    if (!name.empty()) name += ' ';
                    name += tokens[i];
                }
                for (size_t i = valuePos + 1; i < tokens.size(); ++i) {
                    if (!value.empty()) value += ' ';
                    value += tokens[i];
                }
            } else {
                for (size_t i = namePos + 1; i < tokens.size(); ++i) {
                    if (!name.empty()) name += ' ';
                    name += tokens[i];
                }
            }

            auto lower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return s;
            };

            const std::string lname = lower(name);
            if (lname == "move overhead" && !value.empty()) {
                optionMoveOverheadMs = std::clamp(std::stoi(value), 0, 5000);
            } else if (lname == "hash" && !value.empty()) {
                optionHashMb = std::clamp(std::stoi(value), 1, 2048);
                setHashSizeMb(optionHashMb);
                optionHashMb = getHashSizeMb();
                std::cout << "info string hash " << optionHashMb << " MB\n";
            } else if (lname == "threads" && !value.empty()) {
                optionThreads = std::clamp(std::stoi(value), 1, 1);
                (void)optionThreads;
            } else if (lname == "usennue") {
                const std::string lv = lower(value);
                const bool enabled = (lv == "true" || lv == "1" || lv == "on");
                optionUseNnue = enabled;
                setUseNnue(enabled);
            } else if (lname == "evalfile" && !value.empty()) {
                optionEvalFile = value;
                const bool ok = setNnueEvalFile(optionEvalFile);
                std::cout << "info string nnue-load " << (ok ? "ok" : "failed") << " file " << optionEvalFile << "\n";
            }
        } else if (cmd == "ponderhit") {
            // Ponder is treated as infinite search in this engine; stop/next go controls it.
        } else if (cmd == "quit") {
            stopAndJoinSearch();
            break;
        }

        std::cout.flush();
    }

    stopAndJoinSearch();
    return 0;
}

struct UIButton {
    sf::RectangleShape rect;
    sf::Text label;
    function<void()> onClick;
    sf::Color baseColor = sf::Color(200, 200, 200);
    sf::Color hoverColor = sf::Color(170, 170, 170);
    UIButton(const sf::Font& font) : label(font) {}
};

void clearSelection(optional<pair<int, int>>& selectedSquare, vector<Move>& legalMovesForSelected, bool& isDragging)
{
    selectedSquare = nullopt;
    legalMovesForSelected.clear();
    isDragging = false;
}

bool sameMoveIdentity(const Move& a, const Move& b)
{
    if (a.from() != b.from() || a.to() != b.to()) return false;
    if (a.isPromotion() != b.isPromotion()) return false;
    if (a.isPromotion() && a.promotionType() != b.promotionType()) return false;
    return true;
}

bool sameMoveFromTo(const Move& a, const Move& b)
{
    return a.from() == b.from() && a.to() == b.to();
}

int main(int argc, char** argv)
{
    const std::string defaultNnue = "assets/nn-c288c895ea92.nnue";
    const bool nnueLoaded = setNnueEvalFile(defaultNnue);
    setUseNnue(nnueLoaded);

    const std::string modeArg = (argc > 1) ? std::string(argv[1]) : std::string();
    const bool graphicsMode = (modeArg == "--graphics" || modeArg == "--gui");

    if (!graphicsMode && modeArg == "--bench") {
        int depth = 7;
        int timeLimitMs = 2000;
        if (argc > 2) {
            depth = std::max(1, std::atoi(argv[2]));
        }
        if (argc > 3) {
            timeLimitMs = std::max(100, std::atoi(argv[3]));
        }

        const std::vector<std::string> fens = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r1bq1rk1/ppp2ppp/2np1n2/3Np3/2B1P3/5N2/PPP2PPP/R1BQ1RK1 w - - 0 8",
            "8/2p5/3p4/1p1P4/1P3k2/2P2P2/6K1/8 w - - 0 40",
            "2r2rk1/1bq1bppp/p2ppn2/1pn5/3NP3/1BN1BP2/PPQ2P1P/2RR2K1 w - - 0 14",
            "r4rk1/1pp1qppp/p1np1n2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQR1K1 w - - 2 11"
        };

        long long totalNodes = 0;
        long long totalTimeMs = 0;

        std::cout << "bench depth=" << depth << " timeLimitMs=" << timeLimitMs << "\n";
        for (size_t i = 0; i < fens.size(); ++i) {
            GameState benchState;
            benchState.loadFromFen(fens[i]);
            (void)computeBestMove(benchState, depth, timeLimitMs);
            const SearchStats stats = getLastSearchStats();
            totalNodes += stats.nodes;
            totalTimeMs += stats.timeMs;

            std::cout << "pos " << (i + 1)
                      << "  nodes " << stats.nodes
                      << "  timeMs " << stats.timeMs
                      << "  nps " << static_cast<long long>(stats.nps)
                      << "  depth " << stats.depthReached
                      << "  score " << stats.bestScore << "\n";
        }

        const double totalNps = (totalTimeMs > 0)
            ? (static_cast<double>(totalNodes) * 1000.0 / static_cast<double>(totalTimeMs))
            : 0.0;
        std::cout << "total nodes " << totalNodes
                  << "  totalTimeMs " << totalTimeMs
                  << "  totalNps " << static_cast<long long>(totalNps) << "\n";
        return 0;
    }

    // Default mode is UCI (for compatibility with GUIs that don't pass args).
    if (!graphicsMode) {
        return runUciLoop();
    }

    GameState gs;
    gs.initStandard();

    sf::RenderWindow window(sf::VideoMode({640, 720}), "SFML Chess");
    window.setFramerateLimit(60);
    sf::Font font;
    if (!font.openFromFile("assets/DejaVuSans.ttf")) {
        cerr << "Error: Could not load font 'assets/DejaVuSans.ttf'\n";
        return 1;
    }

    map<string, sf::Texture> tex;
    vector<pair<string, string>> pieces = { { "wp", "assets/wp.png" }, { "wn", "assets/wn.png" }, { "wb", "assets/wb.png" }, { "wr", "assets/wr.png" }, { "wq", "assets/wq.png" }, { "wk", "assets/wk.png" },
        { "bp", "assets/bp.png" }, { "bn", "assets/bn.png" }, { "bb", "assets/bb.png" }, { "br", "assets/br.png" }, { "bq", "assets/bq.png" }, { "bk", "assets/bk.png" } };
    for (auto& p : pieces) {
        sf::Texture t;
        if (!t.loadFromFile(p.second)) {
            cerr << "Failed to load " << p.second << "\n";
            return 1;
        }
        tex[p.first] = t;
    }

    vector<UIButton> buttons;
    float currentMenuX = 8.f;
    float menuPadding = 8.f;
    auto addButton = [&](string s, float width, function<void()> action) {
        UIButton b(font);
        b.rect.setPosition({currentMenuX, 4.f});
        b.rect.setSize({ width, 32.f });
        b.rect.setFillColor(b.baseColor);
        b.rect.setOutlineColor(sf::Color::Black);
        b.rect.setOutlineThickness(1.f);
        b.label.setString(s);
        b.label.setCharacterSize(14);
        b.label.setFillColor(sf::Color::Black);
        sf::FloatRect textBounds = b.label.getLocalBounds();
        b.label.setOrigin({textBounds.position.x + textBounds.size.x / 2.0f, textBounds.position.y + textBounds.size.y / 2.0f});
        b.label.setPosition({currentMenuX + width / 2.0f, 4.f + 32.f / 2.0f});
        b.onClick = action;
        buttons.push_back(b);
        currentMenuX += width + menuPadding;
    };

    atomic<bool> isAIThinking { false };
    optional<Move> aiMoveResult;
    mutex aiMoveMutex;

    bool aiEnabled = false, aiPlaysWhite = false;
    const int aiDepth = 14;
    optional<string> gameOverMsg = nullopt;
    optional<pair<int, int>> selectedSquare;
    vector<Move> legalMovesForSelected;
    bool isDragging = false;

    std::optional<sf::Sprite> draggedSprite;
    sf::Vector2i dragStartPos;
    const int DRAG_THRESHOLD = 5;

    bool choosingPromotion = false;
    optional<Move> pendingPromotionMove;

    auto maybeDoAIMove = [&]() {
        if (!gameOverMsg && aiEnabled && gs.whiteToMove == aiPlaysWhite && !isAIThinking) {
            isAIThinking = true;

            std::thread([&, gs_copy = gs, aiDepth]() {
                Move bestMove = computeBestMove(gs_copy, aiDepth);

                {
                    std::lock_guard<std::mutex> lock(aiMoveMutex);
                    aiMoveResult = bestMove;
                }
                isAIThinking = false;
            }).detach();
        }
    };

    addButton("New Game", 100, [&]() {
        gs.initStandard();
        gameOverMsg = nullopt;
        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
        choosingPromotion = false;
        if (aiEnabled && aiPlaysWhite) {
            maybeDoAIMove();
        }
    });
    addButton("Undo", 60, [&]() {
        if (isAIThinking) return;
        if (gs.undoTop <= 0) return;
        if (aiEnabled) {
            undoMove(gs);
            if (gs.undoTop > 0) {
                undoMove(gs);
            }
        } else {
            undoMove(gs);
        }
        gameOverMsg = nullopt;
        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
        choosingPromotion = false;
    });
    addButton("Save", 60, [&]() {  });
    addButton("Play vs AI: OFF", 140, [&]() {
        if (isAIThinking) return;
        auto& aiButtonLabel = buttons[3].label;
        if (!aiEnabled) {
            aiEnabled = true;
            aiPlaysWhite = false;
            aiButtonLabel.setString("AI plays Black");
        } else if (!aiPlaysWhite) {
            aiPlaysWhite = true;
            aiButtonLabel.setString("AI plays White");
        } else {
            aiEnabled = false;
            aiPlaysWhite = false;
            aiButtonLabel.setString("Play vs AI: OFF");
        }
        sf::FloatRect tb = aiButtonLabel.getLocalBounds();
        aiButtonLabel.setOrigin({tb.position.x + tb.size.x / 2.0f, tb.position.y + tb.size.y / 2.0f});
        aiButtonLabel.setPosition({buttons[3].rect.getPosition().x + buttons[3].rect.getSize().x / 2.0f, 4.f + 32.f / 2.0f});
        maybeDoAIMove();
    });
    addButton("Exit", 60, [&]() { window.close(); });

    sf::RectangleShape promotionBg({80.f * 4.f + 10.f * 3.f + 10.f, 80.f + 10.f});
    promotionBg.setFillColor(sf::Color(100, 100, 100, 220));
    promotionBg.setOutlineColor(sf::Color::White);
    promotionBg.setOutlineThickness(2.f);
    promotionBg.setPosition({(window.getSize().x - promotionBg.getSize().x) / 2.f, (window.getSize().y - promotionBg.getSize().y) / 2.f});
    
    vector<pair<int, sf::Sprite>> promotionSprites;
    
    while (window.isOpen()) {
        if (!isAIThinking) {
            optional<Move> completedMove;
            {
                std::lock_guard<std::mutex> lock(aiMoveMutex);
                if (aiMoveResult.has_value()) {
                    completedMove = aiMoveResult;
                    aiMoveResult = std::nullopt;
                }
            }

            if (completedMove.has_value()) {
                if (aiEnabled && gs.whiteToMove == aiPlaysWhite) {
                    bool legalNow = false;
                    MoveList legal;
                    generateLegalMoves(gs, legal);

                    for (int i = 0; i < legal.count; ++i) {
                        if (sameMoveIdentity(legal.moves[i], *completedMove)) {
                            *completedMove = legal.moves[i];
                            legalNow = true;
                            break;
                        }
                    }

                    if (!legalNow) {
                        for (int i = 0; i < legal.count; ++i) {
                            if (sameMoveFromTo(legal.moves[i], *completedMove)) {
                                *completedMove = legal.moves[i];
                                legalNow = true;
                                break;
                            }
                        }
                    }

                    if (legalNow) {
                        makeMove(gs, *completedMove);
                        gameOverMsg = checkGameOver(gs);
                    } else {
                        // If a stale/invalid result appears, request a fresh search.
                        maybeDoAIMove();
                    }
                }
            }
        }

        while (std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>())
                window.close();

            sf::Vector2i mp = sf::Mouse::getPosition(window);

            if (const auto* mouseBtnPressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (mouseBtnPressed->button == sf::Mouse::Button::Left) {
                    bool buttonClicked = false;
                    for (auto& b : buttons) {
                        if (b.rect.getGlobalBounds().contains(sf::Vector2f((float)mp.x, (float)mp.y))) {
                            b.onClick();
                            buttonClicked = true;
                        }
                    }
                    if (buttonClicked)
                        continue;

                    if (choosingPromotion) {
                        for (auto const& [type, sprite] : promotionSprites) {
                            if (sprite.getGlobalBounds().contains(sf::Vector2f((float)mp.x, (float)mp.y))) {
                                Move finalMove = *pendingPromotionMove;
                                finalMove.setPromotionType(static_cast<std::uint8_t>(type));
                                makeMove(gs, finalMove);
                                gameOverMsg = checkGameOver(gs);
                                choosingPromotion = false;
                                maybeDoAIMove();
                                break;
                            }
                        }
                    } else if (!gameOverMsg && !isAIThinking) {
                        int c = mp.x / 80;
                        int r = (mp.y - 40) / 80;

                        if (inBounds(r, c)) {
                            if (selectedSquare.has_value() && selectedSquare->first == r && selectedSquare->second == c) {
                                clearSelection(selectedSquare, legalMovesForSelected, isDragging);
                            } else if (selectedSquare.has_value()) {
                                optional<Move> theMove;
                                for (const auto& legalMove : legalMovesForSelected) {
                                    if (legalMove.to() == r * 8 + c) {
                                        theMove = legalMove;
                                        break;
                                    }
                                }
                                if (theMove.has_value()) {
                                    if (theMove->isPromotion()) {
                                        choosingPromotion = true;
                                        pendingPromotionMove = theMove;
                                        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
                                    } else {
                                        makeMove(gs, *theMove);
                                        gameOverMsg = checkGameOver(gs);
                                        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
                                        maybeDoAIMove();
                                    }
                                } else {
                                    clearSelection(selectedSquare, legalMovesForSelected, isDragging);
                                    const Piece p = pieceAt(gs, r, c);
                                    if (p.type != EMPTY && p.white == gs.whiteToMove) { // Fall-through
                                    } else {
                                        continue;
                                    }
                                }
                            }
                            const Piece selectedPiece = pieceAt(gs, r, c);
                            if (!selectedSquare.has_value() && selectedPiece.type != EMPTY && selectedPiece.white == gs.whiteToMove && (!aiEnabled || gs.whiteToMove != aiPlaysWhite)) {
                                selectedSquare = make_pair(r, c);
                                auto allLegalMoves = generateLegalMoves(gs);
                                for (const auto& m : allLegalMoves) {
                                    if (m.from() == r * 8 + c)
                                        legalMovesForSelected.push_back(m);
                                }
                                dragStartPos = mp;
                                Piece p = selectedPiece;
                                string key = p.white ? "w" : "b";
                                switch (p.type) {
                                case P: key += "p"; break;
                                case N: key += "n"; break;
                                case B: key += "b"; break;
                                case R: key += "r"; break;
                                case Q: key += "q"; break;
                                case K: key += "k"; break;
                                }
                                draggedSprite.emplace(tex[key]);
                                draggedSprite->setScale({80.0f / tex[key].getSize().x, 80.0f / tex[key].getSize().y});
                            }
                        }
                    }
                }
            }

            if (event->is<sf::Event::MouseMoved>()) {
                if (selectedSquare.has_value() && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) && !isDragging) {
                    if (abs(mp.x - dragStartPos.x) > DRAG_THRESHOLD || abs(mp.y - dragStartPos.y) > DRAG_THRESHOLD) {
                        isDragging = true;
                    }
                }
            }

            if (const auto* mouseBtnReleased = event->getIf<sf::Event::MouseButtonReleased>()) {
                if (mouseBtnReleased->button == sf::Mouse::Button::Left) {
                    if (isDragging) {
                        int c = mp.x / 80;
                        int r = (mp.y - 40) / 80;
                        if (inBounds(r, c)) {
                            optional<Move> theMove;
                            for (const auto& legalMove : legalMovesForSelected) {
                                if (legalMove.to() == r * 8 + c) {
                                    theMove = legalMove;
                                    break;
                                }
                            }
                            if (theMove.has_value()) {
                                if (theMove->isPromotion()) {
                                    choosingPromotion = true;
                                    pendingPromotionMove = theMove;
                                } else {
                                    makeMove(gs, *theMove);
                                    gameOverMsg = checkGameOver(gs);
                                    maybeDoAIMove();
                                }
                            }
                        }
                        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
                    }
                }
            }
        }

        window.clear(sf::Color(180, 180, 180));
        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
        for (auto& button : buttons) {
            button.rect.setFillColor(button.rect.getGlobalBounds().contains(sf::Vector2f((float)mousePos.x, (float)mousePos.y)) ? button.hoverColor : button.baseColor);
            window.draw(button.rect);
            window.draw(button.label);
        }

        int boardYOffset = 40;
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                sf::RectangleShape sq({80.f, 80.f});
                sq.setPosition({(float)c * 80.f, (float)boardYOffset + r * 80.f});
                sq.setFillColor((r + c) % 2 == 0 ? sf::Color(240, 217, 181) : sf::Color(181, 136, 99));
                if (selectedSquare.has_value() && selectedSquare->first == r && selectedSquare->second == c) {
                    sq.setFillColor(sf::Color(130, 151, 105));
                }
                window.draw(sq);
            }
        }
        sf::CircleShape moveHint(15.f);
        moveHint.setFillColor(sf::Color(130, 151, 105, 150));
        for (const auto& move : legalMovesForSelected) {
            bool isDuplicatePromotion = false;
            if (move.isPromotion()) {
                for (const auto& m2 : legalMovesForSelected) {
                    if (&move != &m2 && move.to() == m2.to()) {
                        if (move.promotionType() > m2.promotionType()) {
                            isDuplicatePromotion = true;
                            break;
                        }
                    }
                }
            }
            if (!isDuplicatePromotion) {
                const int moveRow = move.to() / 8;
                const int moveCol = move.to() % 8;
                moveHint.setPosition({(float)moveCol * 80.f + 40.f - 15.f, (float)boardYOffset + moveRow * 80.f + 40.f - 15.f});
                window.draw(moveHint);
            }
        }
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                if (isDragging && selectedSquare.has_value() && selectedSquare->first == r && selectedSquare->second == c)
                    continue;
                Piece p = pieceAt(gs, r, c);
                if (p.type != EMPTY) {
                    string key = p.white ? "w" : "b";
                    switch (p.type) {
                    case P: key += "p"; break;
                    case N: key += "n"; break;
                    case B: key += "b"; break;
                    case R: key += "r"; break;
                    case Q: key += "q"; break;
                    case K: key += "k"; break;
                    }
                    sf::Sprite s(tex[key]);
                    s.setScale({80.0f / tex[key].getSize().x, 80.0f / tex[key].getSize().y});
                    s.setPosition({(float)c * 80.f, (float)boardYOffset + r * 80.f});
                    window.draw(s);
                }
            }
        }
        if (isDragging && draggedSprite) {
            draggedSprite->setPosition({(float)mousePos.x - 40.f, (float)mousePos.y - 40.f});
            window.draw(*draggedSprite);
        }
        if (gameOverMsg) {
            sf::Text t(font); 
            t.setString(*gameOverMsg);
            t.setCharacterSize(40);
            t.setFillColor(sf::Color::Red);
            t.setStyle(sf::Text::Bold);
            sf::FloatRect textRect = t.getLocalBounds();
            t.setOrigin({textRect.position.x + textRect.size.x / 2.0f, textRect.position.y + textRect.size.y / 2.0f});
            t.setPosition({window.getSize().x / 2.0f, window.getSize().y / 2.0f});
            window.draw(t);
        }

        if (choosingPromotion) {
            window.draw(promotionBg);
            promotionSprites.clear();
            string color = gs.whiteToMove ? "w" : "b";
            vector<pair<int, string>> choices = { { Q, "q" }, { R, "r" }, { B, "b" }, { N, "n" } };
            float startX = promotionBg.getPosition().x + 5.f;
            for (size_t i = 0; i < choices.size(); ++i) {
                sf::Sprite s(tex[color + choices[i].second]);
                s.setScale({80.f / s.getLocalBounds().size.x, 80.f / s.getLocalBounds().size.y});
                s.setPosition({startX + i * (80.f + 10.f), promotionBg.getPosition().y + 5.f});
                window.draw(s);
                promotionSprites.push_back({choices[i].first, s});
            }
        }

        if (isAIThinking) {
            sf::Text aiStatusText(font);
            aiStatusText.setString("AI is thinking...");
            aiStatusText.setCharacterSize(20);
            aiStatusText.setFillColor(sf::Color::Blue);
            aiStatusText.setPosition({window.getSize().x - aiStatusText.getLocalBounds().size.x - 150.f, window.getSize().y - 30.f});
            window.draw(aiStatusText);
        }

        window.display();
    }
    return 0;
}