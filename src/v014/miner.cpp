// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "v014/miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "hash.h"
#include "main.h"
#include "net.h"
#include "policy/policy.h"
#include "v014/pos.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "masternode.h"
#include "masternode/payments.h"
#include "masternode/sync.h"
#include "validationinterface.h"

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// 3DCoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
bool WinnerIsMe = false;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

CBlockv2Template* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn)
{
    // Create new block
    std::unique_ptr<CBlockv2Template> pblockv2template(new CBlockv2Template());
    if(!pblockv2template.get())
        return NULL;
    CBlockv2 *pblockv2 = &pblockv2template->blockv2; // pointer for convenience

    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;

    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    // Limit to between 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Collect memory pool transactions into the block
    CTxMemPool::setEntries inBlock;
    CTxMemPool::setEntries waitSet;

    // This vector will be sorted into a priority queue:
    vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    std::priority_queue<CTxMemPool::txiter, std::vector<CTxMemPool::txiter>, ScoreCompare> clearedTxs;
    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    uint64_t nBlockSize = 1000;
    uint64_t nBlockTx = 0;
    unsigned int nBlockSigOps = 100;
    int lastFewTxs = 0;
    CAmount nFees = 0;

    {
        LOCK2(cs_main, mempool.cs);
        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        pblockv2->nTime = GetAdjustedTime();
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        // Add our coinbase tx as first transaction
        pblockv2->vtx.push_back(txNew);
        pblockv2template->vTxFees.push_back(-1); // updated at end
        pblockv2template->vTxSigOps.push_back(-1); // updated at end
        pblockv2->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (chainparams.MineBlocksOnDemand())
            pblockv2->nVersion = GetArg("-blockversion", pblockv2->nVersion);

        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                ? nMedianTimePast
                                : pblockv2->GetBlockTime();


        bool fPriorityBlock = nBlockPrioritySize > 0;
        if (fPriorityBlock) {
            vecPriority.reserve(mempool.mapTx.size());
            for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
                 mi != mempool.mapTx.end(); ++mi)
            {
                double dPriority = mi->GetPriority(nHeight);
                CAmount dummy;
                mempool.ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
                vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
            }
            std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        }

        CTxMemPool::indexed_transaction_set::nth_index<3>::type::iterator mi = mempool.mapTx.get<3>().begin();
        CTxMemPool::txiter iter;

        while (mi != mempool.mapTx.get<3>().end() || !clearedTxs.empty())
        {
            
            bool priorityTx = false;
            if (fPriorityBlock && !vecPriority.empty()) { // add a tx from priority queue to fill the blockprioritysize
                priorityTx = true;
                iter = vecPriority.front().second;
                actualPriority = vecPriority.front().first;
                std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                vecPriority.pop_back();
            }
            else if (clearedTxs.empty()) { // add tx with next highest score
                iter = mempool.mapTx.project<0>(mi);
                mi++;
            }
            else {  // try to add a previously postponed child tx
                iter = clearedTxs.top();
                clearedTxs.pop();
            }

            if (inBlock.count(iter))
                continue; // could have been added to the priorityBlock

            const CTransaction& tx = iter->GetTx();

            bool fOrphan = false;
            BOOST_FOREACH(CTxMemPool::txiter parent, mempool.GetMemPoolParents(iter))
            {
                if (!inBlock.count(parent)) {
                    fOrphan = true;
                    break;
                }
            }
            if (fOrphan) {
                if (priorityTx)
                    waitPriMap.insert(std::make_pair(iter,actualPriority));
                else
                    waitSet.insert(iter);
                continue;
            }

            unsigned int nTxSize = iter->GetTxSize();
            if (fPriorityBlock &&
                (nBlockSize + nTxSize >= nBlockPrioritySize || !AllowFree(actualPriority))) {
                fPriorityBlock = false;
                waitPriMap.clear();
            }
            if (!priorityTx &&
                (iter->GetModifiedFee() < ::minRelayTxFee.GetFee(nTxSize) && nBlockSize >= nBlockMinSize)) {
                break;
            }
            if (nBlockSize + nTxSize >= nBlockMaxSize) {
                if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
                    break;
                }
                // Once we're within 1000 bytes of a full block, only look at 50 more txs
                // to try to fill the remaining space.
                if (nBlockSize > nBlockMaxSize - 1000) {
                    lastFewTxs++;
                }
                continue;
            }

            if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
                continue;

            unsigned int nTxSigOps = iter->GetSigOpCount();
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS) {
                if (nBlockSigOps > MAX_BLOCK_SIGOPS - 2) {
                    break;
                }
                continue;
            }

            CAmount nTxFees = iter->GetFee();
            // Added
            pblockv2->vtx.push_back(tx);
            pblockv2template->vTxFees.push_back(nTxFees);
            pblockv2template->vTxSigOps.push_back(nTxSigOps);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fPrintPriority)
            {
                double dPriority = iter->GetPriority(nHeight);
                CAmount dummy;
                mempool.ApplyDeltas(tx.GetHash(), dPriority, dummy);
                LogPrintf("priority %.1f fee %s txid %s\n",
                          dPriority , CFeeRate(iter->GetModifiedFee(), nTxSize).ToString(), tx.GetHash().ToString());
            }

            inBlock.insert(iter);

            // Add transactions that depend on this one to the priority queue
            BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
            {
                if (fPriorityBlock) {
                    waitPriIter wpiter = waitPriMap.find(child);
                    if (wpiter != waitPriMap.end()) {
                        vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                        std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                        waitPriMap.erase(wpiter);
                    }
                }
                else {
                    if (waitSet.count(child)) {
                        clearedTxs.push(child);
                        waitSet.erase(child);
                    }
                }
            }
        }

        // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
        CAmount blockReward = nFees + GetBlockSubsidy(pindexPrev->nHeight, Params().GetConsensus());

        // Compute regular coinbase transaction.
        txNew.vout[0].nValue = blockReward;
        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;

        // Update coinbase transaction with additional info about masternode and governance payments,
        // get some info back to pass to getblocktemplate
        FillBlockPayments(txNew, nHeight, blockReward, pblockv2->txoutMasternode);
        // LogPrintf("CreateNewBlock -- nBlockHeight %d blockReward %lld txoutMasternode %s txNew %s",
        //             nHeight, blockReward, pblock->txoutMasternode.ToString(), txNew.ToString());
        
        if (WinnerIsmine(txNew, pindexPrev)){
            WinnerIsMe = true;
            //LogPrintf("CreateNewBlock(): Im the winner");
        }
        

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

        // Update block coinbase
        pblockv2->vtx[0] = txNew;
        pblockv2template->vTxFees[0] = -nFees;

        // Fill in header
        pblockv2->hashPrevBlock  = pindexPrev->GetBlockHash();
        UpdateTime(pblockv2, chainparams.GetConsensus(), pindexPrev);
        pblockv2->nBits          = GetNextWorkRequiredv2(pindexPrev, pblockv2, chainparams.GetConsensus());
        pblockv2->nNonce         = 0;
        pblockv2template->vTxSigOps[0] = GetLegacySigOpCount(pblockv2->vtx[0]);
        SignBlock(pindexPrev->nHeight, pblockv2);

        CValidationState state;
        if (!TestBlockv2Validity(state, chainparams, *pblockv2, pindexPrev, false, false)) {
            throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
        }
    }

    return pblockv2template.release();
}

void IncrementExtraNonce(CBlockv2* pblockv2, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblockv2->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblockv2->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(pblockv2->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblockv2->vtx[0] = txCoinbase;
    pblockv2->hashTxRoot = BlockTxRoot(*pblockv2);
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

// ***TODO*** ScanHash is not yet used in 3DCoin
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
//bool static ScanHash(const CBlockHeader *pblock, uint32_t& nNonce, uint256 *phash)
//{
//    // Write the first 76 bytes of the block header to a double-SHA256 state.
//    CHash256 hasher;
//    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//    ss << *pblock;
//    assert(ss.size() == 80);
//    hasher.Write((unsigned char*)&ss[0], 76);

//    while (true) {
//        nNonce++;

//        // Write the last 4 bytes of the block header (the nonce) to a copy of
//        // the double-SHA256 state, and compute the result.
//        CHash256(hasher).Write((unsigned char*)&nNonce, 4).Finalize((unsigned char*)phash);

//        // Return the nonce if the hash has at least some zero bits,
//        // caller will check if it has enough to reach the target
//        if (((uint16_t*)phash)[15] == 0)
//            return true;

//        // If nothing found after trying for a while, return -1
//        if ((nNonce & 0xfff) == 0)
//            return false;
//    }
//}

static bool ProcessBlockFound(const CBlockv2* pblockv2, const CChainParams& chainparams)
{
    LogPrintf("%s\n", pblockv2->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblockv2->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblockv2->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("ProcessBlockFound -- generated block is stale");
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblockv2->GetHash());

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlockv2(state, chainparams, NULL, pblockv2, true, NULL))
        return error("ProcessBlockFound -- ProcessNewBlock() failed, block not accepted");

    return true;
}

// ***TODO*** that part changed in bitcoin, we are using a mix with old one here for now
void static BitcoinMiner(const CChainParams& chainparams)
{
    LogPrintf("3DCoinMiner -- started\n");
    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    RenameThread("3dcoin-miner");

    unsigned int nExtraNonce = 0;

    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);

    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

        while (true) {
            if (chainparams.MiningRequiresPeers()) {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do {
                    bool fvNodesEmpty;
                    {
                        LOCK(cs_vNodes);
                        fvNodesEmpty = vNodes.empty();
                    }
                    if (!fvNodesEmpty && !IsInitialBlockDownload() && masternodeSync.IsSynced())
                        break;
                    MilliSleep(1000);
                } while (true);
            }


            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();
            if(!pindexPrev) break;

            std::unique_ptr<CBlockv2Template> pblockv2template(CreateNewBlock(chainparams, coinbaseScript->reserveScript));
            if (!pblockv2template.get())
            {
                LogPrintf("3DCoinMiner -- Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                return;
            }
            CBlockv2 *pblockv2 = &pblockv2template->blockv2;
            IncrementExtraNonce(pblockv2, pindexPrev, nExtraNonce);

            LogPrintf("3DCoinMiner -- Running miner with %u transactions in block (%u bytes)\n", pblockv2->vtx.size(),
                ::GetSerializeSize(*pblockv2, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            if (WinnerIsMe) {
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblockv2->nBits);
            while (true)
            {
                unsigned int nHashesDone = 0;

                uint256 hash;
                while (true)
                {
                    hash = pblockv2->GetHash();
                    
                        if (UintToArith256(hash) <= hashTarget)
                        {
                        // Found a solution
                        LogPrintf("3DCoinMiner:\n  proof-of-work found\n  hash: %s\n  target: %s\n done: %s\n", hash.GetHex(), hashTarget.GetHex(), nHashesDone);
                        ProcessBlockFound(pblockv2, chainparams);
                        coinbaseScript->KeepScript();
                        WinnerIsMe = false;

                        // In regression test mode, stop mining after a block is found. This
                        // allows developers to controllably generate a block on demand.
                        if (chainparams.MineBlocksOnDemand())
                            throw boost::thread_interrupted();

                        break;
                        }
                    
                    pblockv2->nNonce += 1;
                    nHashesDone += 1;
                    if ((pblockv2->nNonce & 0xFF) == 0)
                        break;
                }

                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                if (vNodes.empty() && chainparams.MiningRequiresPeers())
                    break;
                if (pblockv2->nNonce >= 0xffff0000)
                    break;
                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 20)
                    break;
                if (pindexPrev != chainActive.Tip())
                    break;

                // Update nTime every few seconds
                if (UpdateTime(pblockv2, chainparams.GetConsensus(), pindexPrev) < 0)
                    break; // Recreate the block if the clock has run backwards,
                           // so that we can use the correct time.
                if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
                {
                    // Changing pblock->nTime can change work required on testnet:
                    hashTarget.SetCompact(pblockv2->nBits);
                }
            }

            }
            MilliSleep(20000);
            //LogPrintf("3DCoinMiner:\n  Time: %s\n", GetTime());
        }
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("3DCoinMiner -- terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("3DCoinMiner -- runtime error: %s\n", e.what());
        return;
    }
}

void GenerateBitcoins(bool fGenerate, bool fMasterNode, int nThreads, const CChainParams& chainparams)
{
    static boost::thread_group* minerThreads = NULL;

    if (nThreads > 1){
        nThreads = 1;
    }

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (!fMasterNode || nThreads < 1 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&BitcoinMiner, boost::cref(chainparams)));
}
