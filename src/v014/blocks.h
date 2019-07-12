// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2019 The 3DCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_V014_BLOCKS_H
#define BITCOIN_V014_BLOCKS_H

#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "pubkey.h"


class CBlockv2Header
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashTxRoot;
    uint256 hashObjRoot;
    uint256 hashDpsRoot;
    CPubKey minerPubKey;
    std::vector<unsigned char> minerSig;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CBlockv2Header()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(hashPrevBlock);
        READWRITE(hashTxRoot);
        READWRITE(hashObjRoot);
        READWRITE(hashDpsRoot);
        READWRITE(minerPubKey);
        READWRITE(minerSig);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashTxRoot.SetNull();
        hashObjRoot.SetNull();
        hashDpsRoot.SetNull();
        minerSig.clear();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};


class CBlockv2 : public CBlockv2Header
{
public:
    // network and disk
    std::vector<CTransaction> vtx;
    std::vector<CTransaction> vobj; //Districts objects
    std::vector<CTransaction> vdps; // Dapps transactions
    // memory only
    mutable CTxOut txoutMasternode; // masternode payment
    mutable CTxOut txoutPrimenode; // primenode payment
    mutable CTxOut txoutPulsenode; // pulsenode payment
    mutable bool fChecked;

    CBlockv2()
    {
        SetNull();
    }

    CBlockv2(const CBlockv2Header &header)
    {
        SetNull();
        *((CBlockv2Header*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CBlockv2Header*)this);
        READWRITE(vtx);
        READWRITE(vobj);
        READWRITE(vdps);
    }

    void SetNull()
    {
        CBlockv2Header::SetNull();
        vtx.clear();
        vobj.clear();
        vdps.clear();
        txoutMasternode = CTxOut();
        txoutPrimenode = CTxOut();
        txoutPulsenode = CTxOut();
        fChecked = false;
    }

    CBlockv2Header GetBlocksHeader() const
    {
        CBlockv2Header blockv2;
        blockv2.nVersion       = nVersion;
        blockv2.hashPrevBlock  = hashPrevBlock;
        blockv2.hashTxRoot = hashTxRoot;
        blockv2.hashObjRoot = hashObjRoot;
        blockv2.hashDpsRoot = hashDpsRoot;
        blockv2.minerPubKey = minerPubKey;
        blockv2.minerSig    = minerSig;
        blockv2.nTime          = nTime;
        blockv2.nBits          = nBits;
        blockv2.nNonce         = nNonce;

        return blockv2;
    }

    std::string ToString() const;
};


struct CBlockv2Locator
{
    std::vector<uint256> vHave;

    CBlockv2Locator() {}

    CBlockv2Locator(const std::vector<uint256>& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_V014_BLOCKS_H
