// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2019 The 3DCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "v014/blocks.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"

uint256 CBlockv2Header::GetHash() const
{
    return HashKeccak(BEGIN(nVersion), END(nNonce));
}

std::string CBlockv2::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlockv014(hash=%s, ver=%d, hashPrevBlock=%s, hashTxRoot=%s,hashObjRoot=%s, hashDpsRoot=%s, minerPubKey=%s, minerSig=%u nTime=%u, nBits=%08x, vtx=%u, vobj=%u, vdps=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashTxRoot.ToString(),
        hashObjRoot.ToString(),
        hashDpsRoot.ToString(),
        minerPubKey.GetID().ToString(),
        minerSig.size(),
        nTime, nBits, nNonce,
        vtx.size(),
        vobj.size(),
        vdps.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vobj[i].ToString() << "\n";
    }
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vdps[i].ToString() << "\n";
    }
    return s.str();
}
