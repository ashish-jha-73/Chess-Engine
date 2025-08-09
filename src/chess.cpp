#include "../include/chess.hpp"
#include <sstream>
#include <vector>
#include <map>

// --- Helper Functions (internal to this file) ---
namespace {
    bool inBounds(int r,int c){return r>=0&&r<8&&c>=0&&c<8;}
    
    // Splits a string by a delimiter
    std::vector<std::string> split(const std::string& s, char delimiter) {
       std::vector<std::string> tokens;
       std::string token;
       std::istringstream tokenStream(s);
       while (std::getline(tokenStream, token, delimiter)) {
          tokens.push_back(token);
       }
       return tokens;
    }
}

void GameState::loadFromFen(const std::string& fen) {
    // Clear all previous state
    board.fill({ });
    history.clear();
    positionCounts.clear();
    initialFen = fen;

    auto parts = split(fen, ' ');
    if (parts.size() < 4) return; // Basic validation

    // 1. Piece Placement
    int r = 0, c = 0;
    for (char ch : parts[0]) {
        if (ch == '/') {
            r++; c = 0;
        } else if (isdigit(ch)) {
            c += ch - '0';
        } else {
            Piece p;
            p.white = isupper(ch);
            switch (tolower(ch)) {
                case 'p': p.type = P; break; case 'n': p.type = N; break;
                case 'b': p.type = B; break; case 'r': p.type = R; break;
                case 'q': p.type = Q; break; case 'k': p.type = K; break;
                default: p.type = EMPTY; break;
            }
            if (inBounds(r,c)) { board[r][c] = p; }
            c++;
        }
    }

    // 2. Active Color
    whiteToMove = (parts[1] == "w");

    // 3. Castling Availability
    std::string castling = parts[2];
    wkMoved = (castling.find('K') == std::string::npos && castling.find('Q') == std::string::npos);
    bkMoved = (castling.find('k') == std::string::npos && castling.find('q') == std::string::npos);
    wrHHMoved = (castling.find('K') == std::string::npos);
    wrAHMoved = (castling.find('Q') == std::string::npos);
    brHHMoved = (castling.find('k') == std::string::npos);
    brAHMoved = (castling.find('q') == std::string::npos);

    // 4. En Passant Target
    enPassantTarget = std::nullopt;
    if (parts[3] != "-") {
        int ep_c = parts[3][0] - 'a';
        int ep_r = 8 - (parts[3][1] - '0');
        if (inBounds(ep_r, ep_c)) {
            enPassantTarget = std::make_pair(ep_r, ep_c);
        }
    }

    // 5. Halfmove Clock (optional)
    halfmoveClock = (parts.size() > 4) ? std::stoi(parts[4]) : 0;
    
    // 6. Fullmove Number (optional)
    fullmoveNumber = (parts.size() > 5) ? std::stoi(parts[5]) : 1;
    
    // Finally, record the starting position for threefold repetition check
    positionCounts[boardToString(this->board)]++;
}

void GameState::initStandard() {
    loadFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// --- Game Logic Function Implementations ---

bool isSquareAttacked(const Board &board, int r, int c, bool byWhite) {
    int dir = byWhite ? 1 : -1;
    int pr = r + dir;
    if(inBounds(pr,c-1) && board[pr][c-1].type==P && board[pr][c-1].white==byWhite) return true;
    if(inBounds(pr,c+1) && board[pr][c+1].type==P && board[pr][c+1].white==byWhite) return true;
    static int kn[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for(auto &d:kn){int nr=r+d[0], nc=c+d[1]; if(inBounds(nr,nc) && board[nr][nc].type==N && board[nr][nc].white==byWhite) return true;}
    static int diag[4][2]={{-1,-1},{-1,1},{1,-1},{1,1}};
    for(auto &d:diag){int nr=r+d[0], nc=c+d[1]; while(inBounds(nr,nc)){
        if(board[nr][nc].type!=EMPTY){ if((board[nr][nc].type==B||board[nr][nc].type==Q) && board[nr][nc].white==byWhite) return true; break; }
        nr+=d[0]; nc+=d[1];
    }}
    static int ortho[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
    for(auto &d:ortho){int nr=r+d[0], nc=c+d[1]; while(inBounds(nr,nc)){
        if(board[nr][nc].type!=EMPTY){ if((board[nr][nc].type==R||board[nr][nc].type==Q) && board[nr][nc].white==byWhite) return true; break; }
        nr+=d[0]; nc+=d[1];
    }}
    for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++) if(!(dr==0&&dc==0)){
        int nr=r+dr,nc=c+dc; if(inBounds(nr,nc) && board[nr][nc].type==K && board[nr][nc].white==byWhite) return true;
    }
    return false;
}

bool isInCheck(const GameState &gs, bool white) {
    int kr=-1,kc=-1;
    for(int r=0;r<8;r++) for(int c=0;c<8;c++) if(gs.board[r][c].type==K && gs.board[r][c].white==white){ kr=r; kc=c; break; }
    if(kr==-1) return true; // King is missing (captured), which is a form of checkmate.
    return isSquareAttacked(gs.board, kr, kc, !white);
}

std::vector<Move> generatePseudoLegalMoves(const GameState &gs) {
    std::vector<Move> moves;
    const Board &board = gs.board;
    bool wtm = gs.whiteToMove;
    for(int r=0;r<8;r++){
        for(int c=0;c<8;c++){
            const Piece &p = board[r][c]; if(p.type==EMPTY || p.white!=wtm) continue;
            switch(p.type){
                case P: {
                    int dir = p.white ? -1 : 1;
                    int nr = r + dir;
                    bool isPromotion = (p.white && nr == 0) || (!p.white && nr == 7);

                    if(inBounds(nr,c) && board[nr][c].type==EMPTY){
                        if (isPromotion) {
                            for (int pt : {Q, R, B, N}) {
                                moves.push_back({r,c,nr,c,{},false,false,true,pt});
                            }
                        } else {
                            moves.push_back({r, c, nr, c});
                        }
                        if((p.white && r==6) || (!p.white && r==1)){
                            int sr = r + 2*dir;
                            if(inBounds(sr,c) && board[sr][c].type==EMPTY){ moves.push_back({r,c,sr,c}); }
                        }
                    }
                    for(int dc:{-1,1}){
                        int nc=c+dc;
                        if(inBounds(nr,nc) && board[nr][nc].type!=EMPTY && board[nr][nc].white!=p.white){
                             if (isPromotion) {
                                for (int pt : {Q, R, B, N}) {
                                    moves.push_back({r,c,nr,nc,board[nr][nc],false,false,true,pt});
                                }
                            } else {
                                moves.push_back({r,c,nr,nc,board[nr][nc]});
                            }
                        }
                    }
                    if(gs.enPassantTarget){
                        auto [er,ec]=*gs.enPassantTarget;
                        if(nr==er && std::abs(c-ec)==1){ moves.push_back({r,c,er,ec,gs.board[r][ec],true,false,false,Q}); }
                    }
                } break;
                case N: {
                    static int kn[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
                    for(auto &d:kn){int nr=r+d[0], nc=c+d[1]; if(inBounds(nr,nc) && (board[nr][nc].type==EMPTY || board[nr][nc].white!=p.white)){
                        moves.push_back({r,c,nr,nc,(board[nr][nc].type!=EMPTY ? std::optional(board[nr][nc]) : std::nullopt)});
                    }}
                } break;
                case B: case R: case Q: {
                    std::vector<std::pair<int,int>> dirs;
                    if(p.type==B||p.type==Q) { dirs.insert(dirs.end(), {{-1,-1},{-1,1},{1,-1},{1,1}}); }
                    if(p.type==R||p.type==Q) { dirs.insert(dirs.end(), {{-1,0},{1,0},{0,-1},{0,1}}); }
                    for(auto &d:dirs){ int nr=r+d.first, nc=c+d.second; while(inBounds(nr,nc)){
                        if(board[nr][nc].type==EMPTY){ moves.push_back({r,c,nr,nc}); }
                        else { if(board[nr][nc].white!=p.white){ moves.push_back({r,c,nr,nc,board[nr][nc]});} break; }
                        nr+=d.first; nc+=d.second;
                    }}
                } break;
                case K: {
                    for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++) if(!(dr==0&&dc==0)){
                        int nr=r+dr, nc=c+dc; if(inBounds(nr,nc) && (board[nr][nc].type==EMPTY || board[nr][nc].white!=p.white)){
                           moves.push_back({r,c,nr,nc,(board[nr][nc].type!=EMPTY ? std::optional(board[nr][nc]) : std::nullopt)});
                        }}
                    if (!isInCheck(gs, wtm)) {
                        if(p.white){
                            if(!gs.wkMoved && !gs.wrHHMoved && board[7][5].type==EMPTY && board[7][6].type==EMPTY){ if(!isSquareAttacked(board,7,5,!wtm) && !isSquareAttacked(board,7,6,!wtm)) { moves.push_back({7,4,7,6,{},false,true});}}
                            if(!gs.wkMoved && !gs.wrAHMoved && board[7][1].type==EMPTY && board[7][2].type==EMPTY && board[7][3].type==EMPTY){ if(!isSquareAttacked(board,7,2,!wtm) && !isSquareAttacked(board,7,3,!wtm)) { moves.push_back({7,4,7,2,{},false,true});}}
                        } else {
                            if(!gs.bkMoved && !gs.brHHMoved && board[0][5].type==EMPTY && board[0][6].type==EMPTY){ if(!isSquareAttacked(board,0,5,!wtm) && !isSquareAttacked(board,0,6,!wtm)) { moves.push_back({0,4,0,6,{},false,true});}}
                            if(!gs.bkMoved && !gs.brAHMoved && board[0][1].type==EMPTY && board[0][2].type==EMPTY && board[0][3].type==EMPTY){ if(!isSquareAttacked(board,0,2,!wtm) && !isSquareAttacked(board,0,3,!wtm)) { moves.push_back({0,4,0,2,{},false,true});}}
                        }
                    }
                } break;
                default: break;
            }
        }
    }
    return moves;
}

void makeMove(GameState &gs, const Move &m){
    Piece movingPiece = gs.board[m.sx][m.sy];
    if(movingPiece.type == P || m.captured.has_value()) gs.halfmoveClock = 0; else gs.halfmoveClock++;
    if (!gs.whiteToMove) gs.fullmoveNumber++;

    if (m.isCastle) {
        gs.board[m.dx][m.dy] = movingPiece; gs.board[m.sx][m.sy] = {EMPTY,false};
        if (m.dy == 6) { gs.board[m.dx][5] = gs.board[m.dx][7]; gs.board[m.dx][7] = {EMPTY,false}; }
        else { gs.board[m.dx][3] = gs.board[m.dx][0]; gs.board[m.dx][0] = {EMPTY,false}; }
    } else {
        if (m.isEnPassant) gs.board[m.sx][m.dy] = {EMPTY,false};
        gs.board[m.dx][m.dy] = movingPiece;
        gs.board[m.sx][m.sy] = {EMPTY,false};
        if (m.promotion) gs.board[m.dx][m.dy].type = m.promotionType;
    }
    if (movingPiece.type == K) { if (movingPiece.white) gs.wkMoved = true; else gs.bkMoved = true; }
    if (movingPiece.type == R) {
        if (movingPiece.white) { if (m.sx == 7 && m.sy == 0) gs.wrAHMoved = true; if (m.sx == 7 && m.sy == 7) gs.wrHHMoved = true; }
        else { if (m.sx == 0 && m.sy == 0) gs.brAHMoved = true; if (m.sx == 0 && m.sy == 7) gs.brHHMoved = true; }
    }
    if(m.captured.has_value() && m.captured->type == R){
        if (m.dx == 0 && m.dy == 0) gs.brAHMoved = true;
        if (m.dx == 0 && m.dy == 7) gs.brHHMoved = true;
        if (m.dx == 7 && m.dy == 0) gs.wrAHMoved = true;
        if (m.dx == 7 && m.dy == 7) gs.wrHHMoved = true;
    }
    if (movingPiece.type == P && std::abs(m.sx - m.dx) == 2) gs.enPassantTarget = std::make_pair((m.sx + m.dx) / 2, m.sy); else gs.enPassantTarget = std::nullopt;
    gs.history.push_back(m);
    gs.whiteToMove = !gs.whiteToMove;

    std::string currentPos = boardToString(gs.board);
    gs.positionCounts[currentPos]++;
}

void undoMove(GameState &gs){ 
    if(gs.history.empty()) return;
    
    std::vector<Move> hist = gs.history;
    hist.pop_back();
    gs.loadFromFen(gs.initialFen); 
    
    for(const auto &mv : hist) {
        makeMove(gs, mv);
    }
}

std::vector<Move> generateLegalMoves(const GameState &gs){ std::vector<Move> out; auto pseudo = generatePseudoLegalMoves(gs); for(auto &m: pseudo){ GameState copy = gs; makeMove(copy,m); if(!isInCheck(copy, gs.whiteToMove)) out.push_back(m); } return out; }

bool hasSufficientMaterial(const GameState &gs) {
    int whiteKnights = 0, whiteBishops = 0;
    int blackKnights = 0, blackBishops = 0;
    int whiteBishopColor = -1, blackBishopColor = -1;

    for(int r=0; r<8; ++r) {
        for (int c=0; c<8; ++c) {
            Piece p = gs.board[r][c];
            if (p.type == Q || p.type == R || p.type == P) return true;
            if (p.type == N) { if (p.white) whiteKnights++; else blackKnights++; }
            if (p.type == B) {
                if (p.white) {
                    whiteBishops++;
                    whiteBishopColor = (r+c)%2;
                } else {
                    blackBishops++;
                    blackBishopColor = (r+c)%2;
                }
            }
        }
    }
    if (whiteKnights == 0 && whiteBishops == 0 && blackKnights == 0 && blackBishops == 0) return false;
    if (whiteKnights == 1 && whiteBishops == 0 && blackKnights == 0 && blackBishops == 0) return false;
    if (whiteKnights == 0 && whiteBishops == 1 && blackKnights == 0 && blackBishops == 0) return false;
    if (blackKnights == 1 && blackBishops == 0 && whiteKnights == 0 && whiteBishops == 0) return false;
    if (blackKnights == 0 && blackBishops == 1 && whiteKnights == 0 && whiteBishops == 0) return false;
    if (whiteKnights == 0 && blackKnights == 0 && whiteBishops == 1 && blackBishops == 1) {
        if (whiteBishopColor == blackBishopColor) return false;
    }

    return true;
}

std::string boardToString(const Board& board) {
    std::stringstream ss;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece p = board[r][c];
            if (p.type == EMPTY) ss << '.';
            else {
                char ch = '?';
                switch(p.type){case P:ch='p';break;case N:ch='n';break;case B:ch='b';break;case R:ch='r';break;case Q:ch='q';break;case K:ch='k';break;}
                ss << (p.white ? (char)toupper(ch) : ch);
            }
        }
    }
    return ss.str();
}

std::optional<std::string> checkGameOver(GameState &gs){
    auto legalMoves = generateLegalMoves(gs);
    if(legalMoves.empty()){
        if(isInCheck(gs, gs.whiteToMove)) return std::string(gs.whiteToMove ? "Checkmate! Black wins." : "Checkmate! White wins.");
        else return std::string("Stalemate! It's a draw.");
    }
    if (gs.halfmoveClock >= 100) return std::string("Draw by 50-move rule.");
    if (!hasSufficientMaterial(gs)) return std::string("Draw by insufficient material.");
    
    std::string currentPos = boardToString(gs.board);
    if (gs.positionCounts[currentPos] >= 3) return std::string("Draw by threefold repetition.");

    return std::nullopt;
}