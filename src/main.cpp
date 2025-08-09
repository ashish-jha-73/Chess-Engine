#include <SFML/Graphics.hpp>
#include <iostream>
#include <map>
#include <functional>
#include <chrono>
#include <cmath>
#include <thread>   
#include <mutex>    
#include <atomic>   

#include "../include/chess.hpp"
#include "../include/engine.hpp"

using namespace std;

bool inBounds(int r,int c){return r>=0&&r<8&&c>=0&&c<8;}

struct UIButton {
    sf::RectangleShape rect;
    sf::Text label;
    function<void()> onClick;
    sf::Color baseColor = sf::Color(200, 200, 200);
    sf::Color hoverColor = sf::Color(170, 170, 170);
};

void clearSelection(optional<pair<int, int>>& selectedSquare, vector<Move>& legalMovesForSelected, bool& isDragging) {
    selectedSquare = nullopt;
    legalMovesForSelected.clear();
    isDragging = false;
}

int main(){
    GameState gs;
    gs.initStandard();

    sf::RenderWindow window(sf::VideoMode(640,720), "SFML Chess");
    window.setFramerateLimit(60);
    sf::Font font; if(!font.loadFromFile("assets/DejaVuSans.ttf")){ cerr<<"Error: Could not load font 'assets/DejaVuSans.ttf'\n"; return 1; }

    map<string,sf::Texture> tex;
    vector<pair<string,string>> pieces = {{"wp","assets/wp.png"},{"wn","assets/wn.png"},{"wb","assets/wb.png"},{"wr","assets/wr.png"},{"wq","assets/wq.png"},{"wk","assets/wk.png"},
                                          {"bp","assets/bp.png"},{"bn","assets/bn.png"},{"bb","assets/bb.png"},{"br","assets/br.png"},{"bq","assets/bq.png"},{"bk","assets/bk.png"}};
    for(auto &p:pieces){ sf::Texture t; if(!t.loadFromFile(p.second)){ cerr<<"Failed to load "<<p.second<<"\n"; return 1; } tex[p.first]=t; }

    vector<UIButton> buttons;
    float currentMenuX = 8.f;
    float menuPadding = 8.f;
    auto addButton = [&](string s, float width, function<void()> action) {
        UIButton b;
        b.rect.setPosition(currentMenuX, 4); b.rect.setSize({width, 32});
        b.rect.setFillColor(b.baseColor); b.rect.setOutlineColor(sf::Color::Black);
        b.rect.setOutlineThickness(1); b.label.setFont(font);
        b.label.setString(s); b.label.setCharacterSize(14);
        b.label.setFillColor(sf::Color::Black); sf::FloatRect textBounds = b.label.getLocalBounds();
        b.label.setOrigin(textBounds.left + textBounds.width / 2.0f, textBounds.top + textBounds.height / 2.0f);
        b.label.setPosition(currentMenuX + width/2.0f, 4 + 32/2.0f);
        b.onClick = action; buttons.push_back(b);
        currentMenuX += width + menuPadding;
    };
    
    atomic<bool> isAIThinking{false};
    optional<Move> aiMoveResult;
    mutex aiMoveMutex;
    
    bool aiEnabled = false, aiPlaysWhite = false; int aiDepth = 2;
    optional<string> gameOverMsg = nullopt;
    optional<pair<int, int>> selectedSquare;
    vector<Move> legalMovesForSelected;
    bool isDragging = false;
    sf::Sprite draggedSprite;
    sf::Vector2i dragStartPos;
    const int DRAG_THRESHOLD = 5;
    
    bool choosingPromotion = false;
    optional<Move> pendingPromotionMove;

    auto maybeDoAIMove = [&](){
        if(!gameOverMsg && aiEnabled && gs.whiteToMove == aiPlaysWhite && !isAIThinking) {
             isAIThinking = true;
             
             std::thread([&, gs_copy = gs, aiDepth](){
                Move bestMove = computeBestMove(gs_copy, aiDepth);
                
                {
                    std::lock_guard<std::mutex> lock(aiMoveMutex);
                    aiMoveResult = bestMove;
                }
                isAIThinking = false;
             }).detach(); 
        }
    };
    
    addButton("New Game", 100, [&](){
        gs.initStandard();
        gameOverMsg = nullopt;
        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
        choosingPromotion = false;
        if (aiEnabled && aiPlaysWhite) { maybeDoAIMove(); }
    });
    addButton("Undo", 60, [&](){
        if(isAIThinking) return;
        if (gs.history.empty()) return;
        if (aiEnabled) { undoMove(gs); if (!gs.history.empty()) { undoMove(gs); } } 
        else { undoMove(gs); }
        gameOverMsg = nullopt;
        clearSelection(selectedSquare, legalMovesForSelected, isDragging);
        choosingPromotion = false;
    });
    addButton("Save", 60, [&](){ /* ... */ });
    addButton("Play vs AI: OFF", 140, [&](){
        if(isAIThinking) return; 
        auto& aiButtonLabel = buttons[3].label;
        if (!aiEnabled) {
            aiEnabled = true; aiPlaysWhite = false; aiButtonLabel.setString("AI plays Black");
        } else if (!aiPlaysWhite) {
            aiPlaysWhite = true; aiButtonLabel.setString("AI plays White");
        } else {
            aiEnabled = false; aiPlaysWhite = false; aiButtonLabel.setString("Play vs AI: OFF");
        }
        sf::FloatRect tb = aiButtonLabel.getLocalBounds();
        aiButtonLabel.setOrigin(tb.left + tb.width/2.0f, tb.top + tb.height/2.0f);
        aiButtonLabel.setPosition(buttons[3].rect.getPosition().x + buttons[3].rect.getSize().x/2.0f, 4 + 32/2.0f);
        maybeDoAIMove();
    });
    addButton("Exit", 60, [&](){ window.close(); });

    sf::RectangleShape promotionBg(sf::Vector2f(80 * 4 + 10 * 3 + 10, 80 + 10));
    promotionBg.setFillColor(sf::Color(100, 100, 100, 220));
    promotionBg.setOutlineColor(sf::Color::White);
    promotionBg.setOutlineThickness(2.f);
    promotionBg.setPosition( (window.getSize().x - promotionBg.getSize().x) / 2.f, (window.getSize().y - promotionBg.getSize().y) / 2.f );
    map<int, sf::Sprite> promotionSprites;
while(window.isOpen()){
        if (!isAIThinking) {
            optional<Move> completedMove;
            {
                std::lock_guard<std::mutex> lock(aiMoveMutex);
                if(aiMoveResult.has_value()){
                    completedMove = aiMoveResult;
                    aiMoveResult = std::nullopt; 
                }
            }

            if(completedMove.has_value()){
                if(aiEnabled && gs.whiteToMove == aiPlaysWhite){
                    makeMove(gs, *completedMove);
                    gameOverMsg = checkGameOver(gs);
                }
            }
        }

        sf::Event ev;
        while(window.pollEvent(ev)){
            if(ev.type == sf::Event::Closed) window.close();

            sf::Vector2i mp = sf::Mouse::getPosition(window);
            
            if(ev.type == sf::Event::MouseButtonPressed && ev.mouseButton.button == sf::Mouse::Left) {
                bool buttonClicked = false;
                for(auto &b : buttons) {
                    if(b.rect.getGlobalBounds().contains((float)mp.x, (float)mp.y)) {
                        b.onClick();
                        buttonClicked = true;
                    }
                }
                if (buttonClicked) continue;
            }

            if (choosingPromotion) {
                if (ev.type == sf::Event::MouseButtonPressed && ev.mouseButton.button == sf::Mouse::Left) {
                    for(auto const& [type, sprite] : promotionSprites) {
                        if (sprite.getGlobalBounds().contains((float)mp.x, (float)mp.y)) {
                            Move finalMove = *pendingPromotionMove;
                            finalMove.promotionType = type;
                            makeMove(gs, finalMove);
                            gameOverMsg = checkGameOver(gs);
                            choosingPromotion = false;
                            maybeDoAIMove();
                            break;
                        }
                    }
                }
            } 
            else if (!gameOverMsg && !isAIThinking) {
                int c = mp.x / 80;
                int r = (mp.y - 40) / 80;

                if (ev.type == sf::Event::MouseButtonPressed && ev.mouseButton.button == sf::Mouse::Left) {
                    if (inBounds(r,c)) {
                        if (selectedSquare.has_value() && selectedSquare->first == r && selectedSquare->second == c) {
                            clearSelection(selectedSquare, legalMovesForSelected, isDragging);
                        }
                        else if (selectedSquare.has_value()) {
                            optional<Move> theMove;
                            for (const auto& legalMove : legalMovesForSelected) {
                                if (legalMove.dx == r && legalMove.dy == c) {
                                    theMove = legalMove;
                                    break; 
                                }
                            }
                            if (theMove.has_value()) {
                                if (theMove->promotion) {
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
                                if (gs.board[r][c].type != EMPTY && gs.board[r][c].white == gs.whiteToMove) { // Fall-through
                                } else { continue; }
                            }
                        }
                        if (!selectedSquare.has_value() && gs.board[r][c].type != EMPTY && gs.board[r][c].white == gs.whiteToMove && (!aiEnabled || gs.whiteToMove != aiPlaysWhite)) {
                            selectedSquare = make_pair(r, c);
                            auto allLegalMoves = generateLegalMoves(gs);
                            for(const auto& m : allLegalMoves) { if (m.sx == r && m.sy == c) legalMovesForSelected.push_back(m); }
                            dragStartPos = mp;
                            Piece p = gs.board[r][c]; string key = p.white?"w":"b";
                            switch(p.type){case P:key+="p";break;case N:key+="n";break;case B:key+="b";break;case R:key+="r";break;case Q:key+="q";break;case K:key+="k";break;}
                            draggedSprite.setTexture(tex[key]);
                            draggedSprite.setScale(80.0f/tex[key].getSize().x, 80.0f/tex[key].getSize().y);
                        }
                    }
                }
                if (ev.type == sf::Event::MouseMoved) {
                    if (selectedSquare.has_value() && sf::Mouse::isButtonPressed(sf::Mouse::Left) && !isDragging) {
                        if (abs(mp.x - dragStartPos.x) > DRAG_THRESHOLD || abs(mp.y - dragStartPos.y) > DRAG_THRESHOLD) {
                            isDragging = true;
                        }
                    }
                }
                if (ev.type == sf::Event::MouseButtonReleased && ev.mouseButton.button == sf::Mouse::Left) {
                    if (isDragging) {
                        if (inBounds(r,c)) {
                            optional<Move> theMove;
                            for (const auto& legalMove : legalMovesForSelected) {
                                if (legalMove.dx == r && legalMove.dy == c) { theMove = legalMove; break; }
                            }
                            if (theMove.has_value()) {
                                if (theMove->promotion) {
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

        window.clear(sf::Color(180,180,180));
        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
        for (auto& button : buttons) {
            button.rect.setFillColor(button.rect.getGlobalBounds().contains(mousePos.x, mousePos.y) ? button.hoverColor : button.baseColor);
            window.draw(button.rect); window.draw(button.label);
        }

        int boardYOffset = 40;
        for(int r=0;r<8;r++) {
            for(int c=0;c<8;c++){
                sf::RectangleShape sq(sf::Vector2f(80,80));
                sq.setPosition(c*80, boardYOffset + r*80);
                sq.setFillColor((r+c)%2==0? sf::Color(240,217,181) : sf::Color(181,136,99));
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
            if (move.promotion) {
                for(const auto& m2 : legalMovesForSelected) {
                    if (&move != &m2 && move.dx == m2.dx && move.dy == m2.dy) {
                        if (move.promotionType > m2.promotionType) { isDuplicatePromotion = true; break; }
                    }
                }
            }
            if(!isDuplicatePromotion) {
                moveHint.setPosition(move.dy * 80 + 40 - 15, boardYOffset + move.dx * 80 + 40 - 15);
                window.draw(moveHint);
            }
        }
        for(int r=0;r<8;r++) {
            for(int c=0;c<8;c++){
                if(isDragging && selectedSquare.has_value() && selectedSquare->first == r && selectedSquare->second == c) continue;
                Piece p = gs.board[r][c];
                if(p.type!=EMPTY){
                    string key = p.white?"w":"b";
                    switch(p.type){case P:key+="p";break;case N:key+="n";break;case B:key+="b";break;case R:key+="r";break;case Q:key+="q";break;case K:key+="k";break;}
                    sf::Sprite s(tex[key]);
                    s.setScale(80.0f/tex[key].getSize().x,80.0f/tex[key].getSize().y);
                    s.setPosition(c*80, boardYOffset + r*80);
                    window.draw(s);
                }
            }
        }
        if (isDragging) {
            draggedSprite.setPosition(mousePos.x - 40, mousePos.y - 40);
            window.draw(draggedSprite);
        }
        if(gameOverMsg){
            sf::Text t; t.setFont(font); t.setString(*gameOverMsg); t.setCharacterSize(40); t.setFillColor(sf::Color::Red); t.setStyle(sf::Text::Bold); sf::FloatRect textRect=t.getLocalBounds(); t.setOrigin(textRect.left+textRect.width/2.0f, textRect.top+textRect.height/2.0f); t.setPosition(window.getSize().x/2.0f, window.getSize().y/2.0f); window.draw(t);
        }

        if (choosingPromotion) {
            window.draw(promotionBg);
            promotionSprites.clear();
            string color = gs.whiteToMove ? "w" : "b";
            vector<pair<int, string>> choices = {{Q, "q"}, {R, "r"}, {B, "b"}, {N, "n"}};
            float startX = promotionBg.getPosition().x + 5;
            for(size_t i = 0; i < choices.size(); ++i) {
                sf::Sprite& s = promotionSprites[choices[i].first];
                s.setTexture(tex[color + choices[i].second]);
                s.setScale(80.f / s.getLocalBounds().width, 80.f / s.getLocalBounds().height);
                s.setPosition(startX + i * (80 + 10), promotionBg.getPosition().y + 5);
                window.draw(s);
            }
        }

        if (isAIThinking) {
            sf::Text aiStatusText;
            aiStatusText.setFont(font);
            aiStatusText.setString("AI is thinking...");
            aiStatusText.setCharacterSize(20);
            aiStatusText.setFillColor(sf::Color::Blue);
            aiStatusText.setPosition(window.getSize().x - aiStatusText.getLocalBounds().width - 150, window.getSize().y - 30);
            window.draw(aiStatusText);
        }
        
        window.display();
    }
    return 0;
}