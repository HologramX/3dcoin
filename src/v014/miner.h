// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "v014/blocks.h"
#include "util.h"

#include <stdint.h>

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;
namespace Consensus { struct Params; };

static const bool DEFAULT_GENERATE = true;
static const int DEFAULT_GENERATE_THREADS = GetNumCores();

static const bool DEFAULT_PRINTPRIORITY = false;


//v14 blockv2

struct CBlockv2Template
{
    CBlockv2 blockv2;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};



/** Run the miner threads */
void GenerateBitcoins(bool fGenerate, bool fMasterNode, int nThreads, const CChainParams& chainparams);
/** Generate a new block, without valid proof-of-work */
CBlockv2Template* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn);
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlockv2* pblockv2, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
int64_t UpdateTime(CBlockv2Header* pblockv2, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);

#endif // BITCOIN_MINER_H
