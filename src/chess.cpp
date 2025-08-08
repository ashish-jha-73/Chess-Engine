#include "../include/chess.hpp"
#include <sstream>
#include <map>

// --- Helper Functions (internal to this file) ---
namespace {
    bool inBounds(int r,int c){return r>=0&&r<8&&c>=0&&c<8;}
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
                            if(!gs.wkMoved){
                                if(!gs.wrHHMoved && board[7][5].type==EMPTY && board[7][6].type==EMPTY){ if(!isSquareAttacked(board,7,5,!wtm) && !isSquareAttacked(board,7,6,!wtm)) { moves.push_back({7,4,7,6,{},false,true});}}
                                if(!gs.wrAHMoved && board[7][1].type==EMPTY && board[7][2].type==EMPTY && board[7][3].type==EMPTY){ if(!isSquareAttacked(board,7,2,!wtm) && !isSquareAttacked(board,7,3,!wtm)) { moves.push_back({7,4,7,2,{},false,true});}}
                            }
                        } else {
                            if(!gs.bkMoved){
                                if(!gs.brHHMoved && board[0][5].type==EMPTY && board[0][6].type==EMPTY){ if(!isSquareAttacked(board,0,5,!wtm) && !isSquareAttacked(board,0,6,!wtm)) { moves.push_back({0,4,0,6,{},false,true});}}
                                if(!gs.brAHMoved && board[0][1].type==EMPTY && board[0][2].type==EMPTY && board[0][3].type==EMPTY){ if(!isSquareAttacked(board,0,2,!wtm) && !isSquareAttacked(board,0,3,!wtm)) { moves.push_back({0,4,0,2,{},false,true});}}
                            }
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

    // Update repetition count
    std::string currentPos = boardToString(gs.board);
    gs.positionCounts[currentPos]++;
}

void undoMove(GameState &gs){ if(gs.history.empty()) return; GameState base; base.initStandard(); std::vector<Move> hist = gs.history; hist.pop_back(); gs = base; for(const auto &mv : hist) makeMove(gs,mv); }
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