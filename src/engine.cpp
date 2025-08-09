#include "../include/engine.hpp"
#include <limits>
#include <algorithm>
#include <vector>

namespace {
    int pieceValue(int pt){
        switch(pt){
            case P: return 100; case N: return 320; case B: return 330;
            case R: return 500; case Q: return 900; case K: return 20000;
            default: return 0;
        }
    }

    int evaluateMaterial(const GameState &gs){
        int score=0;
        for(int r=0;r<8;r++) for(int c=0;c<8;c++){
            auto p = gs.board[r][c];
            if(p.type!=EMPTY) {
                score += (p.white ? 1 : -1) * pieceValue(p.type);
            }
        }
        return score;
    }

    int minimax(GameState gs, int depth, int alpha, int beta, bool maximizingPlayer){
        if(depth==0) return evaluateMaterial(gs);

        auto moves = generateLegalMoves(gs);
        if(moves.empty()){
            if(isInCheck(gs, gs.whiteToMove)) {
                return maximizingPlayer ? -1000000 : 1000000;
            } else {
                return 0; 
            }
        }

        if(maximizingPlayer){
            int value = -std::numeric_limits<int>::max();
            for(auto &m: moves){
                GameState copy = gs;
                makeMove(copy,m);
                int v = minimax(copy, depth-1, alpha, beta, false);
                value = std::max(value,v);
                alpha = std::max(alpha,value);
                if(alpha>=beta) break;
            }
            return value;
        } else {
            int value = std::numeric_limits<int>::max();
            for(auto &m: moves){
                GameState copy = gs;
                makeMove(copy,m);
                int v = minimax(copy, depth-1, alpha, beta, true);
                value = std::min(value,v);
                beta = std::min(beta,value);
                if(alpha>=beta) break;
            }
            return value;
        }
    }
}

Move computeBestMove(GameState gs, int depth){
    auto moves = generateLegalMoves(gs);
    Move bestMove;
    if (moves.empty()) {
        return Move{-1,-1,-1,-1};
    }
    
    bestMove = moves.front();
    int bestVal = gs.whiteToMove ? -std::numeric_limits<int>::max() : std::numeric_limits<int>::max();

    for(auto &m: moves){
        GameState copy = gs;
        makeMove(copy,m);
        int val = minimax(copy, depth-1, -std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), !gs.whiteToMove);

        if(gs.whiteToMove){
            if(val > bestVal){
                bestVal = val;
                bestMove = m;
            }
        } else {
            if(val < bestVal){
                bestVal = val;
                bestMove = m;
            }
        }
    }
    return bestMove;
}