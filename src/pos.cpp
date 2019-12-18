// Copyright (c) 2018-2019 The 3DCoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode/active.h"
#include "key.h"
#include "main.h"
#include "masternode/sync.h"
#include "masternode/man.h"
#include "messagesigner.h"
#include "net.h"
#include "protocol.h"
#include "pos.h"
#include "spork.h"
#include "sync.h"
#include "txmempool.h"
#include "util.h"
#include "consensus/validation.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>

extern CWallet* pwalletMain;
extern CTxMemPool mempool;


CPos pos;

bool CPos::ProcessPosTx(const CPosTx& posTx)
{
    LOCK2(cs_main, cs_pos);

    uint256 txHash = posTx.GetHash();

    BOOST_FOREACH(const CTxIn& txin, posTx.vin) {
        std::map<COutPoint, uint256>::iterator it = mapLockedOutpoints.find(txin.prevout);
        if(it != mapLockedOutpoints.end()) {
            LogPrintf("CPos::ProcessPosTx -- WARNING: Found conflicting completed Transaction, skipping current one, txid=%s, completed lock txid=%s\n",
                    posTx.GetHash().ToString(), it->second.ToString());
            return false;
        }
    }


    BOOST_FOREACH(const CTxIn& txin, posTx.vin) {
        std::map<COutPoint, std::set<uint256> >::iterator it = mapVotedOutpoints.find(txin.prevout);
        if(it != mapVotedOutpoints.end()) {
            BOOST_FOREACH(const uint256& hash, it->second) {
                if(hash != posTx.GetHash()) {
                    LogPrint("pos", "CPos::ProcessPosTx -- Double spend attempt! %s\n", txin.prevout.ToStringShort());
                }
            }
        }
    }

    LockPosTxIn(PosTxIn);

    return true;
}

bool CPos::CreateTxPMasternode(const CPosTx& posTx)
{
    uint256 txHash = posTx.GetHash();
    if(!posTx.IsValid(!IsEnoughOrphanVotesForTx(posTx))) return false;

    LOCK(cs_pos);

    std::map<uint256, CTxPMasternode>::iterator itPMasternode = mapTxPMasternodes.find(txHash);
    if(itPMasternode == mapTxPMasternodes.end()) {
        LogPrintf("CPos::CreateTxPMasternode -- new, txid=%s\n", txHash.ToString());

        CTxPMasternode PosTxIn(posTx);
        BOOST_REVERSE_FOREACH(const CTxIn& txin, posTx.vin) {
            PosTxIn.AddOutPointLock(txin.prevout);
        }
        mapTxPMasternodes.insert(std::make_pair(txHash, PosTxIn));
    } else {
        LogPrint("pos", "CPos::CreateTxPMasternode -- seen, txid=%s\n", txHash.ToString());
    }

    return true;
}

void CPos::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(!sporkManager.IsSporkActive(SPORK_15_RTOL_SYNC_REQUIRED)) return;
    if(!masternodeSync.IsMasternodeListSynced()) return;
    if (strCommand == NetMsgType::POSVOTE)
    {

        CPosMn vote;
        vRecv >> vote;

        LOCK2(cs_main, cs_pos);

        uint256 nVoteHash = vote.GetHash();

        pfrom->setAskFor.erase(nVoteHash);

        if(mPosMnVotes.count(nVoteHash)) return;
        mPosMnVotes.insert(std::make_pair(nVoteHash, vote));

        ProcessTxLockVote(pfrom, vote);

        return;
    }
}

void CPos::Vote(CTxPMasternode& PosTxIn)
{
    if(!fMasterNode) return;

    LOCK2(cs_main, cs_pos);

    uint256 txHash = PosTxIn.GetHash();
    std::map<COutPoint, COutPointLock>::iterator itOutpointLock = PosTxIn.mapOutPointLocks.begin();
    while(itOutpointLock != PosTxIn.mapOutPointLocks.end()) {

        int nPrevoutHeight = GetUTXOHeight(itOutpointLock->first);
        if(nPrevoutHeight == -1) {
            LogPrint("pos", "CPos::Vote -- Failed to find UTXO %s\n", itOutpointLock->first.ToStringShort());
            return;
        }

        int nLockInputHeight = nPrevoutHeight + 4;

        int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nLockInputHeight, MIN_POS_PROTO_VERSION);

        if(n == -1) {
            LogPrint("pos", "CPos::Vote -- Can't calculate rank for masternode %s\n", activeMasternode.vin.prevout.ToStringShort());
            ++itOutpointLock;
            continue;
        }

        int nSignaturesTotal = COutPointLock::SIGNATURES_TOTAL;
        if(n > nSignaturesTotal) {
            LogPrint("pos", "CPos::Vote -- Masternode not in the top %d (%d)\n", nSignaturesTotal, n);
            ++itOutpointLock;
            continue;
        }

        LogPrint("pos", "CPos::Vote -- In the top %d (%d)\n", nSignaturesTotal, n);

        std::map<COutPoint, std::set<uint256> >::iterator itVoted = mapVotedOutpoints.find(itOutpointLock->first);

        bool fAlreadyVoted = false;
        if(itVoted != mapVotedOutpoints.end()) {
            BOOST_FOREACH(const uint256& hash, itVoted->second) {
                std::map<uint256, CTxPMasternode>::iterator it2 = mapTxPMasternodes.find(hash);
                if(it2->second.HasMasternodeVoted(itOutpointLock->first, activeMasternode.vin.prevout)) {
                    // skip it anyway
                    fAlreadyVoted = true;
                    LogPrintf("CPos::Vote -- WARNING: We already voted for this outpoint, skipping: txHash=%s, outpoint=%s\n",
                            txHash.ToString(), itOutpointLock->first.ToStringShort());
                    break;
                }
            }
        }
        if(fAlreadyVoted) {
            ++itOutpointLock;
            continue; // skip to the next outpoint
        }

        // we haven't voted for this outpoint yet, let's try to do this now
        CPosMn vote(txHash, itOutpointLock->first, activeMasternode.vin.prevout);

        if(!vote.Sign()) {
            LogPrintf("CPos::Vote -- Failed to sign consensus vote\n");
            return;
        }
        if(!vote.CheckSignature()) {
            LogPrintf("CPos::Vote -- Signature invalid\n");
            return;
        }

        // vote constructed sucessfully, let's store and relay it
        uint256 nVoteHash = vote.GetHash();
        mPosMnVotes.insert(std::make_pair(nVoteHash, vote));
        if(itOutpointLock->second.AddVote(vote)) {
            LogPrintf("CPos::Vote -- Vote created successfully, relaying: txHash=%s, outpoint=%s, vote=%s\n",
                    txHash.ToString(), itOutpointLock->first.ToStringShort(), nVoteHash.ToString());

            if(itVoted == mapVotedOutpoints.end()) {
                std::set<uint256> setHashes;
                setHashes.insert(txHash);
                mapVotedOutpoints.insert(std::make_pair(itOutpointLock->first, setHashes));
            } else {
                mapVotedOutpoints[itOutpointLock->first].insert(txHash);
                if(mapVotedOutpoints[itOutpointLock->first].size() > 1) {
                    // it's ok to continue, just warn user
                    LogPrintf("CPos::Vote -- WARNING: Vote conflicts with some existing votes: txHash=%s, outpoint=%s, vote=%s\n",
                            txHash.ToString(), itOutpointLock->first.ToStringShort(), nVoteHash.ToString());
                }
            }

            vote.Relay();
        }

        ++itOutpointLock;
    }
}

bool CPos::ProcessTxLockVote(CNode* pfrom, CPosMn& vote)
{
    LOCK2(cs_main, cs_pos);

    uint256 txHash = vote.GetTxHash();

    if(!vote.IsValid(pfrom)) {
        LogPrint("pos", "CPos::ProcessTxLockVote -- Vote is invalid, txid=%s\n", txHash.ToString());
        return false;
    }

    std::map<uint256, CTxPMasternode>::iterator it = mapTxPMasternodes.find(txHash);
    if(it == mapTxPMasternodes.end()) {
        if(!mPosMnVotesOrphan.count(vote.GetHash())) {
            mPosMnVotesOrphan[vote.GetHash()] = vote;
            LogPrint("pos", "CPos::ProcessTxLockVote -- Orphan vote: txid=%s  masternode=%s new\n",
                    txHash.ToString(), vote.GetMasternodeOutpoint().ToStringShort());
            bool fReprocess = true;
            std::map<uint256, CPosTx>::iterator itLockRequest = mapLockRequestAccepted.find(txHash);
            if(itLockRequest == mapLockRequestAccepted.end()) {
                itLockRequest = mapLockRequestRejected.find(txHash);
                if(itLockRequest == mapLockRequestRejected.end()) {
                    fReprocess = false;
                }
            }
            if(fReprocess && IsEnoughOrphanVotesForTx(itLockRequest->second)) {
                LogPrint("pos", "CPos::ProcessTxLockVote -- Found enough orphan votes, reprocessing Transaction Lock Request: txid=%s\n", txHash.ToString());
                ProcessPosTx(itLockRequest->second);
                return true;
            }
        } else {
            LogPrint("pos", "CPos::ProcessTxLockVote -- Orphan vote: txid=%s  masternode=%s seen\n",
                    txHash.ToString(), vote.GetMasternodeOutpoint().ToStringShort());
        }

        int nMasternodeOrphanExpireTime = GetTime() + 60*10; // keep time data for 10 minutes
        if(!mapMasternodeOrphanVotes.count(vote.GetMasternodeOutpoint())) {
            mapMasternodeOrphanVotes[vote.GetMasternodeOutpoint()] = nMasternodeOrphanExpireTime;
        } else {
            int64_t nPrevOrphanVote = mapMasternodeOrphanVotes[vote.GetMasternodeOutpoint()];
            if(nPrevOrphanVote > GetTime() && nPrevOrphanVote > GetAverageMasternodeOrphanVoteTime()) {
                LogPrint("pos", "CPos::ProcessTxLockVote -- masternode is spamming orphan Transaction Lock Votes: txid=%s  masternode=%s\n",
                        txHash.ToString(), vote.GetMasternodeOutpoint().ToStringShort());
                // Misbehaving(pfrom->id, 1);
                return false;
            }
            // not spamming, refresh
            mapMasternodeOrphanVotes[vote.GetMasternodeOutpoint()] = nMasternodeOrphanExpireTime;
        }

        return true;
    }

    LogPrint("pos", "CPos::ProcessTxLockVote -- Transaction Lock Vote, txid=%s\n", txHash.ToString());

    std::map<COutPoint, std::set<uint256> >::iterator it1 = mapVotedOutpoints.find(vote.GetOutpoint());
    if(it1 != mapVotedOutpoints.end()) {
        BOOST_FOREACH(const uint256& hash, it1->second) {
            if(hash != txHash) {
                std::map<uint256, CTxPMasternode>::iterator it2 = mapTxPMasternodes.find(hash);
                if(it2->second.HasMasternodeVoted(vote.GetOutpoint(), vote.GetMasternodeOutpoint())) {
                    LogPrintf("CPos::ProcessTxLockVote -- masternode sent conflicting votes! %s\n", vote.GetMasternodeOutpoint().ToStringShort());
                    return false;
                }
            }
        }
        // we have votes by other masternodes only (so far), let's continue and see who will win
        it1->second.insert(txHash);
    } else {
        std::set<uint256> setHashes;
        setHashes.insert(txHash);
        mapVotedOutpoints.insert(std::make_pair(vote.GetOutpoint(), setHashes));
    }

    CTxPMasternode& PosTxIn = it->second;

    if(!PosTxIn.AddVote(vote)) {
        // this should never happen
        return false;
    }

    int nSignatures = PosTxIn.CountVotes();
    int nSignaturesMax = PosTxIn.posTx.GetMaxSignatures();
    LogPrint("pos", "CPos::ProcessTxLockVote -- Transaction Lock signatures count: %d/%d, vote hash=%s\n",
            nSignatures, nSignaturesMax, vote.GetHash().ToString());

    LockPosTxIn(PosTxIn);

    vote.Relay();

    return true;
}

void CPos::ProcessOrphanTxLockVotes()
{
    LOCK2(cs_main, cs_pos);
    std::map<uint256, CPosMn>::iterator it = mPosMnVotesOrphan.begin();
    while(it != mPosMnVotesOrphan.end()) {
        if(ProcessTxLockVote(NULL, it->second)) {
            mPosMnVotesOrphan.erase(it++);
        } else {
            ++it;
        }
    }
}

bool CPos::IsEnoughOrphanVotesForTx(const CPosTx& posTx)
{
    BOOST_FOREACH(const CTxIn& txin, posTx.vin) {
        if(!IsEnoughOrphanVotesForTxAndOutPoint(posTx.GetHash(), txin.prevout)) {
            return false;
        }
    }
    return true;
}

bool CPos::IsEnoughOrphanVotesForTxAndOutPoint(const uint256& txHash, const COutPoint& outpoint)
{
    LOCK2(cs_main, cs_pos);
    int nCountVotes = 0;
    std::map<uint256, CPosMn>::iterator it = mPosMnVotesOrphan.begin();
    while(it != mPosMnVotesOrphan.end()) {
        if(it->second.GetTxHash() == txHash && it->second.GetOutpoint() == outpoint) {
            nCountVotes++;
            if(nCountVotes >= COutPointLock::SIGNATURES_REQUIRED) {
                return true;
            }
        }
        ++it;
    }
    return false;
}

void CPos::LockPosTxIn(const CTxPMasternode& PosTxIn)
{
    LOCK2(cs_main, cs_pos);

    uint256 txHash = PosTxIn.posTx.GetHash();
    if(PosTxIn.IsAllOutPointsReady() && !IsLockedPoSTransaction(txHash)) {
        // we have enough votes now
        LogPrint("pos", "CPos::LockPosTxIn -- Transaction Lock is ready to complete, txid=%s\n", txHash.ToString());
        if(ResolveConflicts(PosTxIn, Params().GetConsensus().nPoSKeepLock)) {
            LockTransactionInputs(PosTxIn);
            UpdateLockedTransaction(PosTxIn);
        }
    }
}

void CPos::UpdateLockedTransaction(const CTxPMasternode& PosTxIn)
{
    LOCK(cs_pos);

    uint256 txHash = PosTxIn.GetHash();

    if(!IsLockedPoSTransaction(txHash)) return; // 

#ifdef ENABLE_WALLET
    if(pwalletMain && pwalletMain->UpdatedTransaction(txHash)) {
        nCompleteTXLocks++;
        std::string strCmd = GetArg("-posnotify", "");
        if(!strCmd.empty()) {
            boost::replace_all(strCmd, "%s", txHash.GetHex());
            boost::thread t(runCommand, strCmd); 
        }
    }
#endif

    GetMainSignals().NotifyTransactionLock(PosTxIn.posTx);

    LogPrint("pos", "CPos::UpdateLockedTransaction -- done, txid=%s\n", txHash.ToString());
}

void CPos::LockTransactionInputs(const CTxPMasternode& PosTxIn)
{
    LOCK(cs_pos);

    uint256 txHash = PosTxIn.GetHash();

    if(!PosTxIn.IsAllOutPointsReady()) return;

    std::map<COutPoint, COutPointLock>::const_iterator it = PosTxIn.mapOutPointLocks.begin();

    while(it != PosTxIn.mapOutPointLocks.end()) {
        mapLockedOutpoints.insert(std::make_pair(it->first, txHash));
        ++it;
    }
    LogPrint("pos", "CPos::LockTransactionInputs -- done, txid=%s\n", txHash.ToString());
}

bool CPos::GetLockedOutPointTxHash(const COutPoint& outpoint, uint256& hashRet)
{
    LOCK(cs_pos);
    std::map<COutPoint, uint256>::iterator it = mapLockedOutpoints.find(outpoint);
    if(it == mapLockedOutpoints.end()) return false;
    hashRet = it->second;
    return true;
}

bool CPos::ResolveConflicts(const CTxPMasternode& PosTxIn, int nMaxBlocks)
{
    if(nMaxBlocks < 1) return false;

    LOCK2(cs_main, cs_pos);

    uint256 txHash = PosTxIn.GetHash();

    if(!PosTxIn.IsAllOutPointsReady()) return true; 

    LOCK(mempool.cs); 

    bool fMempoolConflict = false;

    BOOST_FOREACH(const CTxIn& txin, PosTxIn.posTx.vin) {
        uint256 hashConflicting;
        if(GetLockedOutPointTxHash(txin.prevout, hashConflicting) && txHash != hashConflicting) {
            LogPrintf("CPos::ResolveConflicts -- WARNING: Found conflicting completed Transaction Lock, skipping current one, txid=%s, conflicting txid=%s\n",
                    txHash.ToString(), hashConflicting.ToString());
            return false; 
        } else if (mempool.mapNextTx.count(txin.prevout)) {
            hashConflicting = mempool.mapNextTx[txin.prevout].ptx->GetHash();
            if(txHash == hashConflicting) continue; 
            fMempoolConflict = true;
            if(HasPosTx(hashConflicting)) {
                LogPrintf("CPos::ResolveConflicts -- WARNING: Found conflicting Transaction Lock Request, replacing by completed Transaction Lock, txid=%s, conflicting txid=%s\n",
                        txHash.ToString(), hashConflicting.ToString());
            } else {
                LogPrintf("CPos::ResolveConflicts -- WARNING: Found conflicting transaction, replacing by completed Transaction Lock, txid=%s, conflicting txid=%s\n",
                        txHash.ToString(), hashConflicting.ToString());
            }
        }
    } // FOREACH
    if(fMempoolConflict) {
        std::list<CTransaction> removed;
        mempool.removeConflicts(PosTxIn.posTx, removed);
        CValidationState state;
        bool fMissingInputs = false;
        if(!AcceptToMemoryPool(mempool, state, PosTxIn.posTx, true, &fMissingInputs)) {
            LogPrintf("CPos::ResolveConflicts -- ERROR: Failed to accept completed Transaction Lock to mempool, txid=%s\n", txHash.ToString());
            return false;
        }
        LogPrintf("CPos::ResolveConflicts -- Accepted completed Transaction Lock, txid=%s\n", txHash.ToString());
        return true;
    }
    CTransaction txTmp;
    uint256 hashBlock;
    if(GetTransaction(txHash, txTmp, Params().GetConsensus(), hashBlock, true) && hashBlock != uint256()) {
        LogPrint("pos", "CPos::ResolveConflicts -- Done, %s is included in block %s\n", txHash.ToString(), hashBlock.ToString());
        return true;
    }
    BOOST_FOREACH(const CTxIn& txin, PosTxIn.posTx.vin) {
        CCoins coins;
        if(!pcoinsTip->GetCoins(txin.prevout.hash, coins) ||
           (unsigned int)txin.prevout.n>=coins.vout.size() ||
           coins.vout[txin.prevout.n].IsNull()) {
            LogPrintf("CPosTx::ResolveConflicts -- Failed to find UTXO %s - disconnecting tip...\n", txin.prevout.ToStringShort());
            if(!DisconnectBlocks(1)) {
                return false;
            }
            ResolveConflicts(PosTxIn, nMaxBlocks - 1);
            LogPrintf("CPosTx::ResolveConflicts -- Failed to find UTXO %s - activating best chain...\n", txin.prevout.ToStringShort());
            CValidationState state;
            if(!ActivateBestChain(state, Params()) || !state.IsValid()) {
                LogPrintf("CPosTx::ResolveConflicts -- ActivateBestChain failed, txid=%s\n", txin.prevout.ToStringShort());
                return false;
            }
            LogPrintf("CPosTx::ResolveConflicts -- Failed to find UTXO %s - fixed!\n", txin.prevout.ToStringShort());
        }
    }
    LogPrint("pos", "CPos::ResolveConflicts -- Done, txid=%s\n", txHash.ToString());

    return true;
}

int64_t CPos::GetAverageMasternodeOrphanVoteTime()
{
    LOCK(cs_pos);
    if(mapMasternodeOrphanVotes.empty()) return 0;

    std::map<COutPoint, int64_t>::iterator it = mapMasternodeOrphanVotes.begin();
    int64_t total = 0;

    while(it != mapMasternodeOrphanVotes.end()) {
        total+= it->second;
        ++it;
    }

    return total / mapMasternodeOrphanVotes.size();
}

void CPos::CheckAndRemove()
{
    if(!pCurrentBlockIndex) return;

    LOCK(cs_pos);

    std::map<uint256, CTxPMasternode>::iterator itPMasternode = mapTxPMasternodes.begin();

    while(itPMasternode != mapTxPMasternodes.end()) {
        CTxPMasternode &PosTxIn = itPMasternode->second;
        uint256 txHash = PosTxIn.GetHash();
        if(PosTxIn.IsExpired(pCurrentBlockIndex->nHeight)) {
            LogPrintf("CPos::CheckAndRemove -- Removing expired Transaction Lock Candidate: txid=%s\n", txHash.ToString());
            std::map<COutPoint, COutPointLock>::iterator itOutpointLock = PosTxIn.mapOutPointLocks.begin();
            while(itOutpointLock != PosTxIn.mapOutPointLocks.end()) {
                mapLockedOutpoints.erase(itOutpointLock->first);
                mapVotedOutpoints.erase(itOutpointLock->first);
                ++itOutpointLock;
            }
            mapLockRequestAccepted.erase(txHash);
            mapLockRequestRejected.erase(txHash);
            mapTxPMasternodes.erase(itPMasternode++);
        } else {
            ++itPMasternode;
        }
    }

    // remove expired votes
    std::map<uint256, CPosMn>::iterator itVote = mPosMnVotes.begin();
    while(itVote != mPosMnVotes.end()) {
        if(itVote->second.IsExpired(pCurrentBlockIndex->nHeight)) {
            LogPrint("pos", "CPos::CheckAndRemove -- Removing expired vote: txid=%s  masternode=%s\n",
                    itVote->second.GetTxHash().ToString(), itVote->second.GetMasternodeOutpoint().ToStringShort());
            mPosMnVotes.erase(itVote++);
        } else {
            ++itVote;
        }
    }

    // remove expired orphan votes
    std::map<uint256, CPosMn>::iterator itOrphanVote = mPosMnVotesOrphan.begin();
    while(itOrphanVote != mPosMnVotesOrphan.end()) {
        if(GetTime() - itOrphanVote->second.GetTimeCreated() > ORPHAN_VOTE_SECONDS) {
            LogPrint("pos", "CPos::CheckAndRemove -- Removing expired orphan vote: txid=%s  masternode=%s\n",
                    itOrphanVote->second.GetTxHash().ToString(), itOrphanVote->second.GetMasternodeOutpoint().ToStringShort());
            mPosMnVotes.erase(itOrphanVote->first);
            mPosMnVotesOrphan.erase(itOrphanVote++);
        } else {
            ++itOrphanVote;
        }
    }

    // remove expired masternode orphan votes (DOS protection)
    std::map<COutPoint, int64_t>::iterator itMasternodeOrphan = mapMasternodeOrphanVotes.begin();
    while(itMasternodeOrphan != mapMasternodeOrphanVotes.end()) {
        if(itMasternodeOrphan->second < GetTime()) {
            LogPrint("pos", "CPos::CheckAndRemove -- Removing expired orphan masternode vote: masternode=%s\n",
                    itMasternodeOrphan->first.ToStringShort());
            mapMasternodeOrphanVotes.erase(itMasternodeOrphan++);
        } else {
            ++itMasternodeOrphan;
        }
    }

    LogPrintf("CPos::CheckAndRemove -- %s\n", ToString());
}

bool CPos::AlreadyHave(const uint256& hash)
{
    LOCK(cs_pos);
    return mapLockRequestAccepted.count(hash) ||
            mapLockRequestRejected.count(hash) ||
            mPosMnVotes.count(hash);
}

void CPos::AcceptLockRequest(const CPosTx& posTx)
{
    LOCK(cs_pos);
    mapLockRequestAccepted.insert(make_pair(posTx.GetHash(), posTx));
}

void CPos::RejectLockRequest(const CPosTx& posTx)
{
    LOCK(cs_pos);
    mapLockRequestRejected.insert(make_pair(posTx.GetHash(), posTx));
}

bool CPos::HasPosTx(const uint256& txHash)
{
    CPosTx posTxTmp;
    return GetPosTx(txHash, posTxTmp);
}

bool CPos::GetPosTx(const uint256& txHash, CPosTx& posTxRet)
{
    LOCK(cs_pos);

    std::map<uint256, CTxPMasternode>::iterator it = mapTxPMasternodes.find(txHash);
    if(it == mapTxPMasternodes.end()) return false;
    posTxRet = it->second.posTx;

    return true;
}

bool CPos::GetTxLockVote(const uint256& hash, CPosMn& txLockVoteRet)
{
    LOCK(cs_pos);

    std::map<uint256, CPosMn>::iterator it = mPosMnVotes.find(hash);
    if(it == mPosMnVotes.end()) return false;
    txLockVoteRet = it->second;

    return true;
}

bool CPos::IsPoSReadyToLock(const uint256& txHash)
{
    if(!fEnablePoS || fLargeWorkForkFound || fLargeWorkInvalidChainFound ||
        !sporkManager.IsSporkActive(SPORK_2_POS_ENABLED)) return false;

    LOCK(cs_pos);
    // There must be a successfully verified lock request
    // and all outputs must be locked (i.e. have enough signatures)
    std::map<uint256, CTxPMasternode>::iterator it = mapTxPMasternodes.find(txHash);
    return it != mapTxPMasternodes.end() && it->second.IsAllOutPointsReady();
}

bool CPos::IsLockedPoSTransaction(const uint256& txHash)
{
    if(!fEnablePoS || fLargeWorkForkFound || fLargeWorkInvalidChainFound ||
        !sporkManager.IsSporkActive(SPORK_2_POS_ENABLED)) return false;

    LOCK(cs_pos);

    // there must be a lock candidate
    std::map<uint256, CTxPMasternode>::iterator itPMasternode = mapTxPMasternodes.find(txHash);
    if(itPMasternode == mapTxPMasternodes.end()) return false;

    // which should have outpoints
    if(itPMasternode->second.mapOutPointLocks.empty()) return false;

    // and all of these outputs must be included in mapLockedOutpoints with correct hash
    std::map<COutPoint, COutPointLock>::iterator itOutpointLock = itPMasternode->second.mapOutPointLocks.begin();
    while(itOutpointLock != itPMasternode->second.mapOutPointLocks.end()) {
        uint256 hashLocked;
        if(!GetLockedOutPointTxHash(itOutpointLock->first, hashLocked) || hashLocked != txHash) return false;
        ++itOutpointLock;
    }

    return true;
}

int CPos::GetTransactionLockSignatures(const uint256& txHash)
{
    if(!fEnablePoS) return -1;
    if(fLargeWorkForkFound || fLargeWorkInvalidChainFound) return -2;
    if(!sporkManager.IsSporkActive(SPORK_2_POS_ENABLED)) return -3;

    LOCK(cs_pos);

    std::map<uint256, CTxPMasternode>::iterator itPMasternode = mapTxPMasternodes.find(txHash);
    if(itPMasternode != mapTxPMasternodes.end()) {
        return itPMasternode->second.CountVotes();
    }

    return -1;
}

bool CPos::IsPosTxTimedOut(const uint256& txHash)
{
    if(!fEnablePoS) return false;

    LOCK(cs_pos);

    std::map<uint256, CTxPMasternode>::iterator itPMasternode = mapTxPMasternodes.find(txHash);
    if (itPMasternode != mapTxPMasternodes.end()) {
        return !itPMasternode->second.IsAllOutPointsReady() &&
                itPMasternode->second.posTx.IsTimedOut();
    }

    return false;
}

void CPos::Relay(const uint256& txHash)
{
    LOCK(cs_pos);

    std::map<uint256, CTxPMasternode>::const_iterator itPMasternode = mapTxPMasternodes.find(txHash);
    if (itPMasternode != mapTxPMasternodes.end()) {
        itPMasternode->second.Relay();
    }
}

void CPos::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
}

void CPos::SyncTransaction(const CTransaction& tx, const CBlock* pblock)
{
    // Update lock candidates and votes if corresponding tx confirmed
    // or went from confirmed to 0-confirmed or conflicted.

    if (tx.IsCoinBase()) return;

    LOCK2(cs_main, cs_pos);

    uint256 txHash = tx.GetHash();

    // When tx is 0-confirmed or conflicted, pblock is NULL and nHeightNew should be set to -1
    CBlockIndex* pblockindex = NULL;
        if(pblock) {
            uint256 blockHash = pblock->GetHash();
            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if(mi == mapBlockIndex.end() || !mi->second) {
                // shouldn't happen
                LogPrint("pos", "CPosTx::SyncTransaction -- Failed to find block %s\n", blockHash.ToString());
                return;
            }
            pblockindex = mi->second;
        }

    int nHeightNew = pblockindex ? pblockindex->nHeight : -1;

    LogPrint("pos", "CPos::SyncTransaction -- txid=%s nHeightNew=%d\n", txHash.ToString(), nHeightNew);

    // Check lock candidates
    std::map<uint256, CTxPMasternode>::iterator itPMasternode = mapTxPMasternodes.find(txHash);
    if(itPMasternode != mapTxPMasternodes.end()) {
        LogPrint("pos", "CPos::SyncTransaction -- txid=%s nHeightNew=%d lock candidate updated\n",
                txHash.ToString(), nHeightNew);
        itPMasternode->second.SetConfirmedHeight(nHeightNew);
        // Loop through outpoint locks
        std::map<COutPoint, COutPointLock>::iterator itOutpointLock = itPMasternode->second.mapOutPointLocks.begin();
        while(itOutpointLock != itPMasternode->second.mapOutPointLocks.end()) {
            // Check corresponding lock votes
            std::vector<CPosMn> vVotes = itOutpointLock->second.GetVotes();
            std::vector<CPosMn>::iterator itVote = vVotes.begin();
            std::map<uint256, CPosMn>::iterator it;
            while(itVote != vVotes.end()) {
                uint256 nVoteHash = itVote->GetHash();
                LogPrint("pos", "CPos::SyncTransaction -- txid=%s nHeightNew=%d vote %s updated\n",
                        txHash.ToString(), nHeightNew, nVoteHash.ToString());
                it = mPosMnVotes.find(nVoteHash);
                if(it != mPosMnVotes.end()) {
                    it->second.SetConfirmedHeight(nHeightNew);
                }
                ++itVote;
            }
            ++itOutpointLock;
        }
    }

    // check orphan votes
    std::map<uint256, CPosMn>::iterator itOrphanVote = mPosMnVotesOrphan.begin();
    while(itOrphanVote != mPosMnVotesOrphan.end()) {
        if(itOrphanVote->second.GetTxHash() == txHash) {
            LogPrint("pos", "CPos::SyncTransaction -- txid=%s nHeightNew=%d vote %s updated\n",
                    txHash.ToString(), nHeightNew, itOrphanVote->first.ToString());
            mPosMnVotes[itOrphanVote->first].SetConfirmedHeight(nHeightNew);
        }
        ++itOrphanVote;
    }
}

std::string CPos::ToString()
{
    LOCK(cs_pos);
    return strprintf("Lock Candidates: %llu, Votes %llu", mapTxPMasternodes.size(), mPosMnVotes.size());
}


//
// CPosTx
//

bool CPosTx::IsValid(bool fRequireUnspent) const
{
    if(vout.size() < 1) return false;

    if(vin.size() > WARN_MANY_INPUTS) {
        LogPrint("pos", "CPosTx::IsValid -- WARNING: Too many inputs: tx=%s", ToString());
    }

    LOCK(cs_main);
    if(!CheckFinalTx(*this)) {
        LogPrint("pos", "CPosTx::IsValid -- Transaction is not final: tx=%s", ToString());
        return false;
    }

    CAmount nValueIn = 0;
    CAmount nValueOut = 0;

    BOOST_FOREACH(const CTxOut& txout, vout) {
        // PoS supports normal scripts and unspendable (i.e. data) scripts.
        // TODO: Look into other script types that are normal and can be included
        if(!txout.scriptPubKey.IsNormalPaymentScript() && !txout.scriptPubKey.IsUnspendable()) {
            LogPrint("pos", "CPosTx::IsValid -- Invalid Script %s", ToString());
            return false;
        }
        nValueOut += txout.nValue;
    }

    BOOST_FOREACH(const CTxIn& txin, vin) {

        CCoins coins;
        int nPrevoutHeight = 0;
        CAmount nValue = 0;
        if(!pcoinsTip->GetCoins(txin.prevout.hash, coins) ||
           (unsigned int)txin.prevout.n>=coins.vout.size() ||
           coins.vout[txin.prevout.n].IsNull()) {
            LogPrint("pos", "CPosTx::IsValid -- Failed to find UTXO %s\n", txin.prevout.ToStringShort());
            // Normally above sould be enough, but in case we are reprocessing this because of
            // a lot of legit orphan votes we should also check already spent outpoints.
            if(fRequireUnspent) return false;
            CTransaction txOutpointCreated;
            uint256 nHashOutpointConfirmed;
            if(!GetTransaction(txin.prevout.hash, txOutpointCreated, Params().GetConsensus(), nHashOutpointConfirmed, true) || nHashOutpointConfirmed == uint256()) {
                LogPrint("pos", "CPosTx::IsValid -- Failed to find outpoint %s\n", txin.prevout.ToStringShort());
                return false;
            }
            if(txin.prevout.n >= txOutpointCreated.vout.size()) {
                LogPrint("pos", "CPosTx::IsValid -- Outpoint %s is out of bounds, size() = %lld\n",
                        txin.prevout.ToStringShort(), txOutpointCreated.vout.size());
                return false;
            }
            
            BlockMap::iterator mi = mapBlockIndex.find(nHashOutpointConfirmed);
            if(mi == mapBlockIndex.end() || !mi->second) {
                // shouldn't happen
                LogPrint("pos", "CPosTx::IsValid -- Failed to find block %s for outpoint %s\n",
                        nHashOutpointConfirmed.ToString(), txin.prevout.ToStringShort());
                return false;
            }
            nPrevoutHeight = mi->second->nHeight;
            nValue = txOutpointCreated.vout[txin.prevout.n].nValue;
        } else {
            nPrevoutHeight = coins.nHeight;
            nValue = coins.vout[txin.prevout.n].nValue;
        }

        int nTxAge = chainActive.Height() - nPrevoutHeight + 1;
        // 1 less than the "send IX" gui requires, in case of a block propagating the network at the time
        int nConfirmationsRequired = POS_CONFIRMATIONS_REQUIRED - 1;

        if(nTxAge < nConfirmationsRequired) {
            LogPrint("pos", "CPosTx::IsValid -- outpoint %s too new: nTxAge=%d, nConfirmationsRequired=%d, txid=%s\n",
                    txin.prevout.ToStringShort(), nTxAge, nConfirmationsRequired, GetHash().ToString());
            return false;
        }

        nValueIn += nValue;
    }

    if(nValueOut > sporkManager.GetSporkValue(SPORK_5_POS_MAX_VALUE)*COIN) {
        LogPrint("pos", "CPosTx::IsValid -- Transaction value too high: nValueOut=%d, tx=%s", nValueOut, ToString());
        return false;
    }

    if(nValueIn - nValueOut < GetMinFee()) {
        LogPrint("pos", "CPosTx::IsValid -- did not include enough fees in transaction: fees=%d, tx=%s", nValueOut - nValueIn, ToString());
        return false;
    }

    return true;
}

CAmount CPosTx::GetMinFee() const
{
    CAmount nMinFee = MIN_FEE;
    return std::max(nMinFee, CAmount(vin.size() * nMinFee));
}

int CPosTx::GetMaxSignatures() const
{
    return vin.size() * COutPointLock::SIGNATURES_TOTAL;
}

bool CPosTx::IsTimedOut() const
{
    return GetTime() - nTimeCreated > TIMEOUT_SECONDS;
}

//
// CPosMn
//

bool CPosMn::IsValid(CNode* pnode) const
{
    if(!mnodeman.Has(CTxIn(outpointMasternode))) {
        LogPrint("pos", "CPosMn::IsValid -- Unknown masternode %s\n", outpointMasternode.ToStringShort());
        mnodeman.AskForMN(pnode, CTxIn(outpointMasternode));
        return false;
    }

    int nPrevoutHeight = GetUTXOHeight(outpoint);
    if(nPrevoutHeight == -1) {
        LogPrint("pos", "CPosMn::IsValid -- Failed to find UTXO %s\n", outpoint.ToStringShort());
        // Validating utxo set is not enough, votes can arrive after outpoint was already spent,
        // if lock request was mined. We should process them too to count them later if they are legit.
        CTransaction txOutpointCreated;
        uint256 nHashOutpointConfirmed;
        if(!GetTransaction(outpoint.hash, txOutpointCreated, Params().GetConsensus(), nHashOutpointConfirmed, true) || nHashOutpointConfirmed == uint256()) {
            LogPrint("pos", "CPosMn::IsValid -- Failed to find outpoint %s\n", outpoint.ToStringShort());
            return false;
        }
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(nHashOutpointConfirmed);
        if(mi == mapBlockIndex.end() || !mi->second) {
            // not on this chain?
            LogPrint("pos", "CPosMn::IsValid -- Failed to find block %s for outpoint %s\n", nHashOutpointConfirmed.ToString(), outpoint.ToStringShort());
            return false;
        }
        nPrevoutHeight = mi->second->nHeight;
    }

    int nLockInputHeight = nPrevoutHeight + 4;

    int n = mnodeman.GetMasternodeRank(CTxIn(outpointMasternode), nLockInputHeight, MIN_POS_PROTO_VERSION);

    if(n == -1) {
        //can be caused by past versions trying to vote with an invalid protocol
        LogPrint("pos", "CPosMn::IsValid -- Can't calculate rank for masternode %s\n", outpointMasternode.ToStringShort());
        return false;
    }
    LogPrint("pos", "CPosMn::IsValid -- Masternode %s, rank=%d\n", outpointMasternode.ToStringShort(), n);

    int nSignaturesTotal = COutPointLock::SIGNATURES_TOTAL;
    if(n > nSignaturesTotal) {
        LogPrint("pos", "CPosMn::IsValid -- Masternode %s is not in the top %d (%d), vote hash=%s\n",
                outpointMasternode.ToStringShort(), nSignaturesTotal, n, GetHash().ToString());
        return false;
    }

    if(!CheckSignature()) {
        LogPrintf("CPosMn::IsValid -- Signature invalid\n");
        return false;
    }

    return true;
}

uint256 CPosMn::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << txHash;
    ss << outpoint;
    ss << outpointMasternode;
    return ss.GetHash();
}

bool CPosMn::CheckSignature() const
{
    std::string strError;
    std::string strMessage = txHash.ToString() + outpoint.ToStringShort();

    masternode_info_t infoMn = mnodeman.GetMasternodeInfo(CTxIn(outpointMasternode));

    if(!infoMn.fInfoValid) {
        LogPrintf("CPosMn::CheckSignature -- Unknown Masternode: masternode=%s\n", outpointMasternode.ToString());
        return false;
    }

    if(!CMessageSigner::VerifyMessage(infoMn.pubKeyMasternode, vchMasternodeSignature, strMessage, strError)) {
        LogPrintf("CPosMn::CheckSignature -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CPosMn::Sign()
{
    std::string strError;
    std::string strMessage = txHash.ToString() + outpoint.ToStringShort();

    if(!CMessageSigner::SignMessage(strMessage, vchMasternodeSignature, activeMasternode.keyMasternode)) {
        LogPrintf("CPosMn::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(activeMasternode.pubKeyMasternode, vchMasternodeSignature, strMessage, strError)) {
        LogPrintf("CPosMn::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

void CPosMn::Relay() const
{
    CInv inv(MSG_TXLOCK_VOTE, GetHash());
    RelayInv(inv);
}

bool CPosMn::IsExpired(int nHeight) const
{
    // Locks and votes expire nPoSKeepLock blocks after the block corresponding tx was included into.
    return (nConfirmedHeight != -1) && (nHeight - nConfirmedHeight > Params().GetConsensus().nPoSKeepLock);
}

//
// COutPointLock
//

bool COutPointLock::AddVote(const CPosMn& vote)
{
    if(mapMasternodeVotes.count(vote.GetMasternodeOutpoint()))
        return false;
    mapMasternodeVotes.insert(std::make_pair(vote.GetMasternodeOutpoint(), vote));
    return true;
}

std::vector<CPosMn> COutPointLock::GetVotes() const
{
    std::vector<CPosMn> vRet;
    std::map<COutPoint, CPosMn>::const_iterator itVote = mapMasternodeVotes.begin();
    while(itVote != mapMasternodeVotes.end()) {
        vRet.push_back(itVote->second);
        ++itVote;
    }
    return vRet;
}

bool COutPointLock::HasMasternodeVoted(const COutPoint& outpointMasternodeIn) const
{
    return mapMasternodeVotes.count(outpointMasternodeIn);
}

void COutPointLock::Relay() const
{
    std::map<COutPoint, CPosMn>::const_iterator itVote = mapMasternodeVotes.begin();
    while(itVote != mapMasternodeVotes.end()) {
        itVote->second.Relay();
        ++itVote;
    }
}

//
// CTxPMasternode
//

void CTxPMasternode::AddOutPointLock(const COutPoint& outpoint)
{
    mapOutPointLocks.insert(make_pair(outpoint, COutPointLock(outpoint)));
}


bool CTxPMasternode::AddVote(const CPosMn& vote)
{
    std::map<COutPoint, COutPointLock>::iterator it = mapOutPointLocks.find(vote.GetOutpoint());
    if(it == mapOutPointLocks.end()) return false;
    return it->second.AddVote(vote);
}

bool CTxPMasternode::IsAllOutPointsReady() const
{
    if(mapOutPointLocks.empty()) return false;

    std::map<COutPoint, COutPointLock>::const_iterator it = mapOutPointLocks.begin();
    while(it != mapOutPointLocks.end()) {
        if(!it->second.IsReady()) return false;
        ++it;
    }
    return true;
}

bool CTxPMasternode::HasMasternodeVoted(const COutPoint& outpointIn, const COutPoint& outpointMasternodeIn)
{
    std::map<COutPoint, COutPointLock>::iterator it = mapOutPointLocks.find(outpointIn);
    return it !=mapOutPointLocks.end() && it->second.HasMasternodeVoted(outpointMasternodeIn);
}

int CTxPMasternode::CountVotes() const
{
    // Note: do NOT use vote count to figure out if tx is locked, use IsAllOutPointsReady() instead
    int nCountVotes = 0;
    std::map<COutPoint, COutPointLock>::const_iterator it = mapOutPointLocks.begin();
    while(it != mapOutPointLocks.end()) {
        nCountVotes += it->second.CountVotes();
        ++it;
    }
    return nCountVotes;
}

bool CTxPMasternode::IsExpired(int nHeight) const
{
    // Locks and votes expire nPoSKeepLock blocks after the block corresponding tx was included into.
    return (nConfirmedHeight != -1) && (nHeight - nConfirmedHeight > Params().GetConsensus().nPoSKeepLock);
}

void CTxPMasternode::Relay() const
{
    RelayTransaction(posTx);
    std::map<COutPoint, COutPointLock>::const_iterator itOutpointLock = mapOutPointLocks.begin();
    while(itOutpointLock != mapOutPointLocks.end()) {
        itOutpointLock->second.Relay();
        ++itOutpointLock;
    }
}
