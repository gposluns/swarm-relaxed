/** $lic$
 * Copyright (C) 2012-2021 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2012 by The Board of Trustees of Stanford University
 *
 * This file is part of the Swarm simulator.
 *
 * This simulator is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 * This simulator was developed as part of the Swarm architecture project. If
 * you use this software in your research, we request that you reference the
 * Swarm MICRO 2018 paper ("Harmonizing Speculative and Non-Speculative
 * Execution in Architectures for Ordered Parallelism", Jeffrey et al.,
 * MICRO-51, 2018) as the source of this simulator in any publications that use
 * this software, and that you send us a citation of your work.
 *
 * This simulator is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* This file was adapted from zsim. */

#include "sim/memory/hash.h"
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include "sim/assert.h"

H3HashFamily::H3HashFamily(uint32_t numFunctions, uint32_t outputBits, uint64_t randSeed) : numFuncs(numFunctions) {
    std::mt19937 gen(randSeed);
    std::bernoulli_distribution flipFairCoin(0.5);

    if (outputBits <= 8) {
        resShift = 3;
    } else if (outputBits <= 16) {
        resShift = 2;
    } else if (outputBits <= 32) {
        resShift = 1;
    } else if (outputBits <= 64) {
        resShift = 0;
    } else {
        panic("Hash function can't produce more than 64 bits of output!!");
    }

    uint32_t words = 64 >> resShift;
    hMatrix = new uint64_t[words*numFuncs] ();
    for (uint32_t ii = 0; ii < numFuncs; ii++) {
        for (uint32_t jj = 0; jj < words; jj++) {
            uint64_t val = 0;
            for (int kk = 0; kk < 64; kk++) {
                val = val << 1;
                if (flipFairCoin(gen)) val++;
            }
            //Indeed, they are distributed around 32, but we might get better mileage by forcing 32b...
            //info("H3: Function %d Matrix 64-bit word %d has %d 1s", ii, jj, __builtin_popcountll(val));
            //if (__builtin_popcountll(val) != 32) {jj--; continue;} // no difference
            hMatrix[ii*words + jj] = val;
        }
    }
}

H3HashFamily::~H3HashFamily() {
    delete [] hMatrix;
}

/* NOTE: This is fairly well hand-optimized. Go to the commit logs to see the speedup of this function. Main things:
 * 1. resShift indicates how many bits of output are computed (64, 32, 16, or 8). With less than 64 bits, several rounds are folded at the end.
 * 2. The output folding does not mask, the output is expected to be masked by caller.
 * 3. The main loop is hand-unrolled and optimized for ILP.
 * 4. Pre-computing shifted versions of the input does not help, as it increases register pressure.
 *
 * For reference, here is the original, simpler code (computes a 64-bit hash):
 * for (uint32_t x = 0; x < 64; x++) {
 *     res ^= val & hMatrix[id*64 + x];
 *     res = (res << 1) | (res >> 63);
 * }
 */
uint64_t H3HashFamily::hash(uint32_t id, uint64_t val) const {
    uint64_t res = 0;
    assert(id >= 0 && id < numFuncs);

    // 8-way unrolled loop
    uint32_t maxBits = 64 >> resShift;
    for (uint32_t x = 0; x < maxBits; x+=8) {
        uint32_t base = (id << (6 - resShift)) + x;
        uint64_t res0 = val & hMatrix[base];
        uint64_t res1 = val & hMatrix[base+1];
        uint64_t res2 = val & hMatrix[base+2];
        uint64_t res3 = val & hMatrix[base+3];

        uint64_t res4 = val & hMatrix[base+4];
        uint64_t res5 = val & hMatrix[base+5];
        uint64_t res6 = val & hMatrix[base+6];
        uint64_t res7 = val & hMatrix[base+7];

        res ^= res0 ^ ((res1 << 1) | (res1 >> 63)) ^ ((res2 << 2) | (res2 >> 62)) ^ ((res3 << 3) | (res3 >> 61));
        res ^= ((res4 << 4) | (res4 >> 60)) ^ ((res5 << 5) | (res5 >> 59)) ^ ((res6 << 6) | (res6 >> 58)) ^ ((res7 << 7) | (res7 >> 57));
        res = (res << 8) | (res >> 56);
    }

    // Fold bits to match output
    switch (resShift) {
        case 0: //64-bit output
            break;
        case 1: //32-bit output
            res = (res >> 32) ^ res;
            break;
        case 2: //16-bit output
            res = (res >> 32) ^ res;
            res = (res >> 16) ^ res;
            break;
        case 3: //8-bit output
            res = (res >> 32) ^ res;
            res = (res >> 16) ^ res;
            res = (res >> 8) ^ res;
            break;
    }

    //info("0x%lx", res);

    return res;
}
