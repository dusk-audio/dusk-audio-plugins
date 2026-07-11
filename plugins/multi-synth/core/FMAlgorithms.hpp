// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FMAlgorithms.hpp — the single source of truth for the Prism 4-operator FM
// routing topology.
//
// `kPrismAlgos[8]` is a plain-data descriptor table shared by BOTH the DSP
// (FMEngine.hpp) and the UI (the algorithm-diagram widget in MultiSynthUI.cpp).
// The engine reads `edges`, `carrier` and `fbOp` to route modulation; the UI
// reads `ops[].gx/gy` to lay the operator blocks out on screen and `edges` to
// draw the routing lines. Neither side hardcodes topology separately — change a
// routing here and both the sound and the diagram follow.
//
// Zero framework includes; pure C++17 POD. Operator indices are ZERO-BASED
// throughout (0 = "Op 1" on screen … 3 = "Op 4"). An edge {from,to} means
// operator `from` phase-modulates operator `to`, so it always holds that
// `from > to` (every algorithm is a strict descending DAG — see FMEngine.hpp,
// which relies on this to evaluate ops 3→0 in one pass). `fbOp` is the operator
// carrying self-feedback; it is Op 4 (index 3) in every algorithm.

#pragma once

#include <cstdint>

namespace msynth
{

// Matches the struct the UI spec (09-multi-synth-ui-spec.md §8.6) expects, plus
// a short `name` used for tooltips / preset docs. `gx,gy` are diagram grid cells
// (gy = 0 is the top row, modulators sit above the carriers they feed).
struct PrismAlgo
{
    struct Op   { uint8_t gx, gy; bool carrier; };
    struct Edge { uint8_t from, to; };

    Op          ops[4];
    Edge        edges[6];
    uint8_t     nEdges;
    uint8_t     fbOp;      // operator with self-feedback (always 3 = Op 4)
    uint8_t     carrierMask; // bit i set => op i reaches the output bus
    const char* name;
};

// The classic 4-op algorithm set. ASCII diagrams use "1".."4" (screen labels =
// index+1); "a -> b" reads "a modulates b"; "===" is the output bus.
//
//  #1 serial            #2 (3+4)->2->1        #3 4->(2,3)->1        #4 4->3->(1,2)
//     [4]                 [4] [3]                 [4]                    [4]
//      |                    \ /                  /   \                    |
//     [3]                   [2]                [2]   [3]                 [3]
//      |                     |                   \   /                  /   \  .
//     [2]                   [1]                   [1]                 [1]   [2]
//      |                   =====                 =====               =========
//     [1]
//    =====
//
//  #5 (2->1)+(4->3)     #6 3->(1,2) + 4       #7 4->3 + 1,2         #8 additive
//     [2] [4]              [3]      [4]           [4]                [1][2][3][4]
//      |   |               / \       |             |                  | | | |
//     [1] [3]            [1] [2]    (car)          [3]                =========
//    =========          =============            [1][2][3]
//                                                =========
//
static const PrismAlgo kPrismAlgos[8] = {
    // ---- #1: 4 -> 3 -> 2 -> 1 serial (single carrier, brightest/metallic) ----
    {
        { {0,3,true}, {0,2,false}, {0,1,false}, {0,0,false} },   // op1..op4
        { {3,2}, {2,1}, {1,0} }, 3, 3, 0x01, "Serial"
    },
    // ---- #2: (3+4) -> 2 -> 1  (two modulators into one — rich/vocal) ----
    {
        { {0,2,true}, {0,1,false}, {1,0,false}, {0,0,false} },
        { {3,1}, {2,1}, {1,0} }, 3, 3, 0x01, "Stack-2M"
    },
    // ---- #3: 4 -> (2,3) -> 1 branch (one mod fans into two, both into carrier) ----
    {
        { {0,2,true}, {0,1,false}, {1,1,false}, {0,0,false} },
        { {3,1}, {3,2}, {1,0}, {2,0} }, 4, 3, 0x01, "Branch"
    },
    // ---- #4: 4 -> 3 -> (1,2) two carriers ----
    {
        { {0,2,true}, {1,2,true}, {0,1,false}, {0,0,false} },
        { {3,2}, {2,0}, {2,1} }, 3, 3, 0x03, "Y-Split"
    },
    // ---- #5: (2->1) + (4->3) dual stacks — classic tine e-piano ----
    {
        { {0,1,true}, {0,0,false}, {1,1,true}, {1,0,false} },
        { {1,0}, {3,2} }, 2, 3, 0x05, "Dual"
    },
    // ---- #6: 3 -> (1,2) + 4 standalone carrier ----
    {
        { {0,1,true}, {1,1,true}, {0,0,false}, {2,1,true} },
        { {2,0}, {2,1} }, 2, 3, 0x0B, "Twin+1"
    },
    // ---- #7: 4 -> 3 + 1,2 clean carriers ----
    {
        { {0,1,true}, {1,1,true}, {2,1,true}, {2,0,false} },
        { {3,2} }, 1, 3, 0x07, "Tri+FM"
    },
    // ---- #8: all parallel (additive / organ) ----
    {
        { {0,1,true}, {1,1,true}, {2,1,true}, {3,1,true} },
        { }, 0, 3, 0x0F, "Additive"
    },
};

} // namespace msynth
