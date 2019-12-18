// Copyright (c) 2018-2019 The 3DCoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef POS_H
#define POS_H

#include "net.h"
#include "primitives/transaction.h"

class CPosMn;
class COutPointLock;
class CPosTx;
class CTxPMasternode;
class CPos;

extern CPos pos;

static const int POS_CONFIRMATIONS_REQUIRED = 95;
static const int DEFAULT_POS_DEPTH          = 25;

static const int MIN_POS_PROTO_VERSION      = 70219;

extern bool fEnablePoS;
extern int nPoSDepth;
extern int nCompleteTXLocks;

class CPos
{
private:
    static const int ORPHAN_VOTE_SECONDS            = 30;
    const CBlockIndex *pCurrentBlockIndex;
    std::map<uint256, CPosTx> mapLockRequestAccepted; 
    std::map<uint256, CPosTx> mapLockRequestRejected; 
    std::map<uint256, CPosMn> mPosMnVotes; 
    std::map<uint256, CPosMn> mPosMnVotesOrphan; 
    std::map<uint256, CTxPMasternode> mapTxPMasternodes; 
    std::map<COutPoint, std::set<uint256> > mapVotedOutpoints;
    std::map<COutPoint, uint256> mapLockedOutpoints;
    std::map<COutPoint, int64_t> mapMasternodeOrphanVotes;

    bool CreateTxPMasternode(const CPosTx& posTx);
    void Vote(CTxPMasternode& PosTxIn);

    //process consensus vote message
    bool ProcessTxLockVote(CNode* pfrom, CPosMn& vote);
    void ProcessOrphanTxLockVotes();
    bool IsEnoughOrphanVotesForTx(const CPosTx& posTx);
    bool IsEnoughOrphanVotesForTxAndOutPoint(const uint256& txHash, const COutPoint& outpoint);
    int64_t GetAverageMasternodeOrphanVoteTime();

    void LockPosTxIn(const CTxPMasternode& PosTxIn);
    void LockTransactionInputs(const CTxPMasternode& PosTxIn);
    void UpdateLockedTransaction(const CTxPMasternode& PosTxIn);
    bool ResolveConflicts(const CTxPMasternode& PosTxIn, int nMaxBlocks);

    bool IsPoSReadyToLock(const uint256 &txHash);

public:
    CCriticalSection cs_pos;

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    bool ProcessPosTx(const CPosTx& posTx);

    bool AlreadyHave(const uint256& hash);

    void AcceptLockRequest(const CPosTx& posTx);
    void RejectLockRequest(const CPosTx& posTx);
    bool HasPosTx(const uint256& txHash);
    bool GetPosTx(const uint256& txHash, CPosTx& posTxRet);

    bool GetTxLockVote(const uint256& hash, CPosMn& txLockVoteRet);

    bool GetLockedOutPointTxHash(const COutPoint& outpoint, uint256& hashRet);

    bool IsLockedPoSTransaction(const uint256& txHash);
    int GetTransactionLockSignatures(const uint256& txHash);

    void CheckAndRemove();
    bool IsPosTxTimedOut(const uint256& txHash);

    void Relay(const uint256& txHash);

    void UpdatedBlockTip(const CBlockIndex *pindex);
    void SyncTransaction(const CTransaction& tx, const CBlock* pblock);

    std::string ToString();
};

class CPosTx : public CTransaction
{
private:
    static const int TIMEOUT_SECONDS        = 20;
    static const CAmount MIN_FEE            = 0.0001 * COIN;

    int64_t nTimeCreated;

public:
    static const int WARN_MANY_INPUTS       = 100;

    CPosTx() :
        CTransaction(),
        nTimeCreated(GetTime())
        {}
    CPosTx(const CTransaction& tx) :
        CTransaction(tx),
        nTimeCreated(GetTime())
        {}

    bool IsValid(bool fRequireUnspent = true) const;
    CAmount GetMinFee() const;
    int GetMaxSignatures() const;
    bool IsTimedOut() const;
};

class CPosMn
{
private:
    uint256 txHash;
    COutPoint outpoint;
    COutPoint outpointMasternode;
    std::vector<unsigned char> vchMasternodeSignature;
    int nConfirmedHeight; 
    int64_t nTimeCreated;

public:
    CPosMn() :
        txHash(),
        outpoint(),
        outpointMasternode(),
        vchMasternodeSignature(),
        nConfirmedHeight(-1),
        nTimeCreated(GetTime())
        {}

    CPosMn(const uint256& txHashIn, const COutPoint& outpointIn, const COutPoint& outpointMasternodeIn) :
        txHash(txHashIn),
        outpoint(outpointIn),
        outpointMasternode(outpointMasternodeIn),
        vchMasternodeSignature(),
        nConfirmedHeight(-1),
        nTimeCreated(GetTime())
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(txHash);
        READWRITE(outpoint);
        READWRITE(outpointMasternode);
        READWRITE(vchMasternodeSignature);
    }

    uint256 GetHash() const;

    uint256 GetTxHash() const { return txHash; }
    COutPoint GetOutpoint() const { return outpoint; }
    COutPoint GetMasternodeOutpoint() const { return outpointMasternode; }
    int64_t GetTimeCreated() const { return nTimeCreated; }

    bool IsValid(CNode* pnode) const;
    void SetConfirmedHeight(int nConfirmedHeightIn) { nConfirmedHeight = nConfirmedHeightIn; }
    bool IsExpired(int nHeight) const;

    bool Sign();
    bool CheckSignature() const;

    void Relay() const;
};

class COutPointLock
{
private:
    COutPoint outpoint;
    std::map<COutPoint, CPosMn> mapMasternodeVotes;

public:
    static const int SIGNATURES_REQUIRED        = 6;
    static const int SIGNATURES_TOTAL           = 10;

    COutPointLock(const COutPoint& outpointIn) :
        outpoint(outpointIn),
        mapMasternodeVotes()
        {}

    COutPoint GetOutpoint() const { return outpoint; }

    bool AddVote(const CPosMn& vote);
    std::vector<CPosMn> GetVotes() const;
    bool HasMasternodeVoted(const COutPoint& outpointMasternodeIn) const;
    int CountVotes() const { return mapMasternodeVotes.size(); }
    bool IsReady() const { return CountVotes() >= SIGNATURES_REQUIRED; }

    void Relay() const;
};

class CTxPMasternode
{
private:
    int nConfirmedHeight; // when corresponding tx is 0-confirmed or conflicted, nConfirmedHeight is -1

public:
    CTxPMasternode(const CPosTx& posTxIn) :
        nConfirmedHeight(-1),
        posTx(posTxIn),
        mapOutPointLocks()
        {}

    CPosTx posTx;
    std::map<COutPoint, COutPointLock> mapOutPointLocks;

    uint256 GetHash() const { return posTx.GetHash(); }

    void AddOutPointLock(const COutPoint& outpoint);
    bool AddVote(const CPosMn& vote);
    bool IsAllOutPointsReady() const;

    bool HasMasternodeVoted(const COutPoint& outpointIn, const COutPoint& outpointMasternodeIn);
    int CountVotes() const;

    void SetConfirmedHeight(int nConfirmedHeightIn) { nConfirmedHeight = nConfirmedHeightIn; }
    bool IsExpired(int nHeight) const;

    void Relay() const;
};

#endif
