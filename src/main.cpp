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

#include "../include/chess.hpp"
#include "../include/engine.hpp"

using namespace std;

bool inBounds(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }

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
    if (argc > 1 && std::string(argv[1]) == "--bench") {
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
    const int aiDepth = 12;
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