// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POS_H
#define BITCOIN_POS_H

#include "consensus/params.h"

#include <stdint.h>

class CBlockv2Header;
class CBlockIndex;
class uint256;
class arith_uint256;

// Define difficulty retarget algorithms
enum DiffModev2 {
    DIFFV2_DEFAULT = 0, // Default to invalid 0
    DIFFV2_BTC     = 1, // Retarget every x blocks (Bitcoin style)
    DIFFV2_KGW     = 2, // Retarget using Kimoto Gravity Well
    DIFFV2_DGW     = 3, // Retarget using Dark Gravity Wave v3
};
unsigned int GetNextWorkRequiredv2(const CBlockIndex* pindexLast, const CBlockv2Header *pblockv2, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);


/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
arith_uint256 GetBlockProof(const CBlockIndex& block);

/** Return the time it would take to redo the work difference between from and to, assuming the current hashrate corresponds to the difficulty at tip, in seconds. */
int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params&);

#endif // BITCOIN_POS_H
