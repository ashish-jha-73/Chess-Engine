#include "../include/engine.hpp"
#include "../include/chess.hpp"
#include <limits>
#include <algorithm>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstdint>

#define MAX_DEPTH 50
#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA 2

std::chrono::steady_clock::time_point startTime;
int timeLimitMs = 20000;
bool stopSearch = false;
long long nodes = 0;

struct TTEntry
{
    uint64_t key;
    int depth;
    int score;
    int flag;
    Move bestMove;
};

Move killerMoves[2][MAX_DEPTH];
long long historyTable[64][64];
uint64_t zobristPiece[12][64];
uint64_t zobristSide;
const int TT_SIZE = 1 << 20;
TTEntry TT[TT_SIZE];

static const int INF = 1000000000;

static int pawnTable[8][8] = {
{0,0,0,0,0,0,0,0},
{50,50,50,50,50,50,50,50},
{10,10,20,30,30,20,10,10},
{5,5,10,25,25,10,5,5},
{0,0,0,20,20,0,0,0},
{5,-5,-10,0,0,-10,-5,5},
{5,10,10,-20,-20,10,10,5},
{0,0,0,0,0,0,0,0}
};

static int knightTable[8][8] = {
{-50,-40,-30,-30,-30,-30,-40,-50},
{-40,-20,0,0,0,0,-20,-40},
{-30,0,5,15,15,5,0,-30},
{-30,5,15,20,20,15,5,-30},
{-30,0,15,20,20,15,0,-30},
{-30,5,10,15,15,10,5,-30},
{-40,-20,0,5,5,0,-20,-40},
{-50,-40,-30,-30,-30,-30,-40,-50}
};

static int bishopTable[8][8] = {
{-20,-10,-10,-10,-10,-10,-10,-20},
{-10,0,0,0,0,0,0,-10},
{-10,0,5,10,10,5,0,-10},
{-10,5,5,10,10,5,5,-10},
{-10,0,10,10,10,10,0,-10},
{-10,10,10,10,10,10,10,-10},
{-10,5,0,0,0,0,5,-10},
{-20,-10,-10,-10,-10,-10,-10,-20}
};

static int rookTable[8][8] = {
{0,0,0,0,0,0,0,0},
{5,10,10,10,10,10,10,5},
{-5,0,0,0,0,0,0,-5},
{-5,0,0,0,0,0,0,-5},
{-5,0,0,0,0,0,0,-5},
{-5,0,0,0,0,0,0,-5},
{-5,0,0,0,0,0,0,-5},
{0,0,0,5,5,0,0,0}
};

static int queenTable[8][8] = {
{-20,-10,-10,-5,-5,-10,-10,-20},
{-10,0,0,0,0,0,0,-10},
{-10,0,5,5,5,5,0,-10},
{-5,0,5,5,5,5,0,-5},
{0,0,5,5,5,5,0,-5},
{-10,5,5,5,5,5,0,-10},
{-10,0,5,0,0,0,0,-10},
{-20,-10,-10,-5,-5,-10,-10,-20}
};

static int kingMiddleTable[8][8] = {
{-30,-40,-40,-50,-50,-40,-40,-30},
{-30,-40,-40,-50,-50,-40,-40,-30},
{-30,-40,-40,-50,-50,-40,-40,-30},
{-30,-40,-40,-50,-50,-40,-40,-30},
{-20,-30,-30,-40,-40,-30,-30,-20},
{-10,-20,-20,-20,-20,-20,-20,-10},
{20,20,0,0,0,0,20,20},
{20,30,10,0,0,10,30,20}
};

static int kingEndTable[8][8] = {
{-50,-40,-30,-20,-20,-30,-40,-50},
{-30,-20,-10,0,0,-10,-20,-30},
{-30,-10,20,30,30,20,-10,-30},
{-30,-10,30,40,40,30,-10,-30},
{-30,-10,30,40,40,30,-10,-30},
{-30,-10,20,30,30,20,-10,-30},
{-30,-30,0,0,0,0,-30,-30},
{-50,-30,-30,-30,-30,-30,-30,-50}
};

int pieceValue(int pt)
{
    switch(pt){
        case P: return 100;
        case N: return 320;
        case B: return 330;
        case R: return 500;
        case Q: return 900;
        case K: return 20000;
        default: return 0;
    }
}

void initZobrist()
{
    std::mt19937_64 rng(123456);
    for(int p=0;p<12;p++)
        for(int s=0;s<64;s++)
            zobristPiece[p][s] = rng();
    zobristSide = rng();
}

uint64_t computeHash(const GameState& gs)
{
    uint64_t h = 0;
    for(int r=0;r<8;r++)
    for(int c=0;c<8;c++)
    {
        auto p = gs.board[r][c];
        if(p.type == EMPTY) continue;
        int pieceIndex = (p.white ? 0 : 6) + (p.type - 1);
        int sq = r*8 + c;
        h ^= zobristPiece[pieceIndex][sq];
    }
    if(gs.whiteToMove) h ^= zobristSide;
    return h;
}

bool sameMove(const Move& a,const Move& b)
{
    return a.sx==b.sx && a.sy==b.sy && a.dx==b.dx && a.dy==b.dy && a.promotion==b.promotion;
}

bool isEndgame(const GameState& gs)
{
    int material=0;
    for(int r=0;r<8;r++)
        for(int c=0;c<8;c++){
            auto p=gs.board[r][c];
            if(p.type!=EMPTY && p.type!=K)
                material+=pieceValue(p.type);
        }
    return material<2000;
}

int moveScore(const GameState& gs,const Move& m,int depth)
{
    int score=0;
    if(m.captured.has_value()){
        int victim=pieceValue(m.captured->type);
        int attacker=pieceValue(gs.board[m.sx][m.sy].type);
        score+=10000+(victim*10)-attacker;
    }else{
        if(sameMove(m,killerMoves[0][depth])) score+=9000;
        else if(sameMove(m,killerMoves[1][depth])) score+=8000;
        score += historyTable[m.sx*8 + m.sy][m.dx*8 + m.dy];
    }
    if(m.promotion) score+=8000;
    if(m.isCastle) score+=100;
    return score;
}

int evaluateMaterial(const GameState& gs)
{
    int score=0;
    bool endgame=isEndgame(gs);
    for(int r=0;r<8;r++){
        for(int c=0;c<8;c++){
            auto p=gs.board[r][c];
            if(p.type==EMPTY) continue;
            int value=pieceValue(p.type);
            int row=p.white?r:7-r;
            int pst=0;
            switch(p.type){
                case P: pst=pawnTable[row][c]; break;
                case N: pst=knightTable[row][c]; break;
                case B: pst=bishopTable[row][c]; break;
                case R: pst=rookTable[row][c]; if(endgame) pst+=20; break;
                case Q: pst=queenTable[row][c]; break;
                case K: pst=endgame?kingEndTable[row][c]:kingMiddleTable[row][c]; break;
            }
            int total=value+pst;
            score+=p.white?total:-total;
        }
    }
    if(endgame){
        int wr=-1,wc=-1,br=-1,bc=-1;
        for(int r=0;r<8;r++)
            for(int c=0;c<8;c++){
                auto p=gs.board[r][c];
                if(p.type==K){
                    if(p.white){wr=r;wc=c;}
                    else{br=r;bc=c;}
                }
            }
        int edge=std::min({br,7-br,bc,7-bc});
        score+=(4-edge)*80;
        int kingDist=abs(wr-br)+abs(wc-bc);
        score+=50*(14-kingDist);
    }
    return score;
}

int evaluateBishopPair(const GameState& gs)
{
    int white=0, black=0;

    for(int r=0;r<8;r++)
    for(int c=0;c<8;c++)
    {
        auto p=gs.board[r][c];

        if(p.type==B)
        {
            if(p.white) white++;
            else black++;
        }
    }

    int score=0;

    if(white>=2) score+=40;
    if(black>=2) score-=40;

    return score;
}

int evaluateMobility(GameState& gs)
{
    bool side=gs.whiteToMove;

    auto moves=generateLegalMoves(gs);
    int score=moves.size();

    gs.whiteToMove=!gs.whiteToMove;

    auto oppMoves=generateLegalMoves(gs);
    score-=oppMoves.size();

    gs.whiteToMove=side;

    return score*2;
}

int evaluatePawnStructure(const GameState& gs)
{
    int whiteFile[8]={0};
    int blackFile[8]={0};

    for(int r=0;r<8;r++)
    for(int c=0;c<8;c++)
    {
        auto p=gs.board[r][c];

        if(p.type==P)
        {
            if(p.white) whiteFile[c]++;
            else blackFile[c]++;
        }
    }

    int score=0;

    for(int f=0;f<8;f++)
    {
        if(whiteFile[f]>1) score-=20*(whiteFile[f]-1);
        if(blackFile[f]>1) score+=20*(blackFile[f]-1);
    }

    return score;
}

int evaluatePassedPawns(const GameState& gs)
{
    int score=0;

    for(int r=0;r<8;r++)
    for(int c=0;c<8;c++)
    {
        auto p=gs.board[r][c];

        if(p.type!=P) continue;

        bool passed=true;

        if(p.white)
        {
            for(int rr=r+1;rr<8;rr++)
            for(int cc=c-1;cc<=c+1;cc++)
            {
                if(cc>=0 && cc<8)
                {
                    auto t=gs.board[rr][cc];
                    if(t.type==P && !t.white) passed=false;
                }
            }

            if(passed) score+=r*10;
        }
        else
        {
            for(int rr=r-1;rr>=0;rr--)
            for(int cc=c-1;cc<=c+1;cc++)
            {
                if(cc>=0 && cc<8)
                {
                    auto t=gs.board[rr][cc];
                    if(t.type==P && t.white) passed=false;
                }
            }

            if(passed) score-=(7-r)*10;
        }
    }

    return score;
}

int evaluateKingSafety(const GameState& gs)
{
    int score=0;

    for(int r=0;r<8;r++)
    for(int c=0;c<8;c++)
    {
        auto p=gs.board[r][c];

        if(p.type!=K) continue;

        int dir=p.white?-1:1;

        for(int dc=-1;dc<=1;dc++)
        {
            int rr=r+dir;
            int cc=c+dc;

            if(rr>=0 && rr<8 && cc>=0 && cc<8)
            {
                auto pawn=gs.board[rr][cc];

                if(pawn.type==P && pawn.white==p.white)
                {
                    if(p.white) score+=10;
                    else score-=10;
                }
            }
        }
    }

    return score;
}

int evaluatePieceActivity(const GameState& gs)
{
    int score=0;

    for(int r=2;r<=5;r++)
    for(int c=2;c<=5;c++)
    {
        auto p=gs.board[r][c];

        if(p.type==EMPTY) continue;

        if(p.white) score+=5;
        else score-=5;
    }

    return score;
}

int evaluate(GameState& gs)
{
    int score = 0;

    score += evaluateMaterial(gs);
    score += evaluateMobility(gs);
    score += evaluatePawnStructure(gs);
    score += evaluatePassedPawns(gs);
    score += evaluateKingSafety(gs);
    score += evaluatePieceActivity(gs);
    score += evaluateBishopPair(gs);

    return score;
}

int quiescence(GameState& gs,int alpha,int beta,bool maximizingPlayer)
{
    int stand_pat=evaluate(gs);
    if(maximizingPlayer){
        if(stand_pat>=beta) return stand_pat;
        if(alpha<stand_pat) alpha=stand_pat;
    }else{
        if(stand_pat<=alpha) return stand_pat;
        if(beta>stand_pat) beta=stand_pat;
    }
    auto moves=generateLegalMoves(gs);
    std::sort(moves.begin(),moves.end(),
    [&](const Move& a,const Move& b){
        return moveScore(gs,a,0)>moveScore(gs,b,0);
    });
    for(auto& m:moves){
        if(!m.captured.has_value() && !m.promotion) continue;
        makeMove(gs,m);
        int score=quiescence(gs,alpha,beta,!maximizingPlayer);
        undoMove(gs);
        if(maximizingPlayer){
            if(score>alpha) alpha=score;
            if(alpha>=beta) break;
        }else{
            if(score<beta) beta=score;
            if(alpha>=beta) break;
        }
    }
    return maximizingPlayer?alpha:beta;
}

void initEngine()
{
    initZobrist();
    for(int i=0;i<TT_SIZE;i++){
        TT[i].key = 0;
        TT[i].depth = -1;
        TT[i].score = 0;
        TT[i].flag = TT_EXACT;
        TT[i].bestMove = Move{-1,-1,-1,-1};
    }
    for(int i=0;i<2;i++)
        for(int d=0;d<MAX_DEPTH;d++)
            killerMoves[i][d] = Move{-1,-1,-1,-1};
    for(int i=0;i<64;i++)
        for(int j=0;j<64;j++)
            historyTable[i][j] = 0;
}

int minimax(GameState& gs,int depth,int alpha,int beta,bool maximizingPlayer)
{
    if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count() > timeLimitMs)
    {
        stopSearch = true;
        return evaluate(gs);
    }
    nodes++;
    uint64_t key = computeHash(gs);
    TTEntry &entry = TT[key % TT_SIZE];
    Move hashMove = Move{-1,-1,-1,-1};
    bool hasHashMove = false;
    if(entry.key == key){
        hasHashMove = true;
        hashMove = entry.bestMove;
    }
    int originalAlpha = alpha;
    if(entry.key == key && entry.depth >= depth){
        if(entry.flag == TT_EXACT) return entry.score;
        if(entry.flag == TT_ALPHA && entry.score <= alpha) return entry.score;
        if(entry.flag == TT_BETA && entry.score >= beta) return entry.score;
    }
    if(depth >= 3 && !isInCheck(gs, gs.whiteToMove))
    {
        GameState copy = gs;
        copy.whiteToMove = !copy.whiteToMove;

        int score = -minimax(copy, depth - 1 - 2, -beta, -beta + 1, !maximizingPlayer);

        if(score >= beta)
            return beta;
    }
    if(depth==0) return quiescence(gs,alpha,beta,maximizingPlayer);
    auto moves=generateLegalMoves(gs);
    if(moves.empty()){
        if(isInCheck(gs,gs.whiteToMove))
            return maximizingPlayer?-1000000+(MAX_DEPTH-depth):1000000-(MAX_DEPTH-depth);
        return 0;
    }
    std::sort(moves.begin(),moves.end(),
    [&](const Move& a,const Move& b){
        if(hasHashMove){
            if(sameMove(a,hashMove) && !sameMove(b,hashMove)) return true;
            if(sameMove(b,hashMove) && !sameMove(a,hashMove)) return false;
        }
        int sa = moveScore(gs,a,depth);
        int sb = moveScore(gs,b,depth);
        return sa > sb;
    });
    if(maximizingPlayer){
        int value=-INF;
        Move bestLocal = Move{-1,-1,-1,-1};
        for(auto& m:moves){
            makeMove(gs,m);
            int v=minimax(gs,depth-1,alpha,beta,false);
            undoMove(gs);
            if(v > value){
                value = v;
                bestLocal = m;
            }
            alpha = std::max(alpha,value);
            if(alpha>=beta){
                if(!m.captured.has_value()){
                    killerMoves[1][depth]=killerMoves[0][depth];
                    killerMoves[0][depth]=m;
                    historyTable[m.sx*8 + m.sy][m.dx*8 + m.dy] += depth * depth;
                }
                break;
            }
        }
        entry.key = key;
        entry.depth = depth;
        entry.score = value;
        entry.bestMove = bestLocal;
        if(value <= originalAlpha) entry.flag = TT_ALPHA;
        else if(value >= beta) entry.flag = TT_BETA;
        else entry.flag = TT_EXACT;
        return value;
    }else{
        int value=INF;
        Move bestLocal = Move{-1,-1,-1,-1};
        for(auto& m:moves){
            makeMove(gs,m);
            int v=minimax(gs,depth-1,alpha,beta,true);
            undoMove(gs);
            if(v < value){
                value = v;
                bestLocal = m;
            }
            beta = std::min(beta,value);
            if(alpha>=beta){
                if(!m.captured.has_value()){
                    killerMoves[1][depth]=killerMoves[0][depth];
                    killerMoves[0][depth]=m;
                    historyTable[m.sx*8 + m.sy][m.dx*8 + m.dy] += depth * depth;
                }
                break;
            }
        }
        entry.key = key;
        entry.depth = depth;
        entry.score = value;
        entry.bestMove = bestLocal;
        if(value <= originalAlpha) entry.flag = TT_ALPHA;
        else if(value >= beta) entry.flag = TT_BETA;
        else entry.flag = TT_EXACT;
        return value;
    }
}

Move computeBestMoveDepth(GameState gs,int depth)
{
    nodes = 0;
    auto moves=generateLegalMoves(gs);
    if(moves.empty()) return Move{-1,-1,-1,-1};
    int bestVal=gs.whiteToMove?-INF:INF;
    std::vector<Move> bestMoves;
    bool temp=!gs.whiteToMove;
    for(auto& m:moves){
        makeMove(gs,m);
        int val=minimax(gs,depth-1,-INF,INF,temp);
        undoMove(gs);
        if(gs.whiteToMove){
            if(val>bestVal){
                bestVal=val;
                bestMoves.clear();
                bestMoves.push_back(m);
            }else if(val==bestVal){
                bestMoves.push_back(m);
            }
        }else{
            if(val<bestVal){
                bestVal=val;
                bestMoves.clear();
                bestMoves.push_back(m);
            }else if(val==bestVal){
                bestMoves.push_back(m);
            }
        }
    }
    if(bestMoves.empty()) return moves.front();
    unsigned seed=std::chrono::system_clock::now().time_since_epoch().count();
    std::uniform_int_distribution<size_t> dist(0,bestMoves.size()-1);
    std::mt19937 gen(seed);
    return bestMoves[dist(gen)];
}

Move computeBestMove(GameState gs,int depth)
{
    startTime = std::chrono::steady_clock::now();
    stopSearch = false;

    Move bestMove{-1,-1,-1,-1};

    for(int depth=1; depth<MAX_DEPTH; depth++)
    {
        Move move = computeBestMoveDepth(gs,depth);
        if(stopSearch) break;
        bestMove = move;
        std::cout<<"Depth "<<depth<<" finished\n";
    }

    return bestMove;
}