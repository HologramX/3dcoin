// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2019 The 3DCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include <assert.h>
#include "arith_uint256.h"
#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
 *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
 *   vMerkleTree: e0028e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The most beloved of the people to Allaah are the most beneficial for the people";
    const CScript genesisOutputScript = CScript() << ParseHex("") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        //3DCoin V0.14
        consensus.nV014v1Start = 475000;
        consensus.nV014v2Start = 575000;
        consensus.nV014v3Start = 800000;
        consensus.nPrimenodePaymentsStartBlock = 820000; 
        consensus.nPulsenodePaymentsStartBlock = 850000;
        //3DCoin V0.14  
        consensus.nSubsidyHalvingInterval = 501750; // Note: actual number of blocks per calendar year with DGW v3 is 501750
        consensus.nMasternodePaymentsStartBlock = 500; // unused 
        consensus.nMasternodePaymentsIncreaseBlock = 41812; // 30days
        consensus.nMasternodePaymentsIncreasePeriod = 62718; // unused
        consensus.nInstantSendKeepLock = 32;
        consensus.nBudgetPaymentsStartBlock = 100000000; // Disabled
        consensus.nBudgetPaymentsCycleBlocks = 41812; // actual number of blocks per month is 501750 / 12 = 41812
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nBudgetProposalEstablishingTime = 60*60*24;
        consensus.nSuperblockStartBlock = 110000000; 
        consensus.nSuperblockCycle = 41812; // actual number of blocks per month is 501750 / 12 = 41812
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 15;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x00000dace491f29d20f137225c4db0342cb0a34ac96cb057ef03f00bd08ffaf1");
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; //  24 hours
        consensus.nPowTargetSpacing = 60; //  60 sec
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 1440; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1486252800; // Feb 5th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1517788800; // Feb 5th, 2018

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xd1;
        pchMessageStart[1] = 0xc0;
        pchMessageStart[2] = 0xa2;
        pchMessageStart[3] = 0x7d;
        nDefaultPort = 6695;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1521936000, 385071, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
      

        assert(consensus.hashGenesisBlock == uint256S("0x000009c68fa7a5604b26aae2330f87fb67248ebc79fd93648981da6e704ca901"));
        assert(genesis.hashMerkleRoot == uint256S("0x01cece0a7bcfdc2b9341c9ebf9f96b9f9746c57d2be182ebeada84ff21d855a8"));


        vSeeds.push_back(CDNSSeedData("seed1", "seed1.3dcstats.net"));
        vSeeds.push_back(CDNSSeedData("seed2", "seed2.3dcstats.net"));
        vSeeds.push_back(CDNSSeedData("seed3", "seed3.3dcstats.net"));
        vSeeds.push_back(CDNSSeedData("seed4", "seed4.3dcstats.net"));
        vSeeds.push_back(CDNSSeedData("mn1", "seed1.3dctalk.net"));
        vSeeds.push_back(CDNSSeedData("mn2", "seed2.3dctalk.net"));
        vSeeds.push_back(CDNSSeedData("mn3", "seed3.3dctalk.net"));
        vSeeds.push_back(CDNSSeedData("mn4", "seed4.3dctalk.net"));
        vSeeds.push_back(CDNSSeedData("s1", "seed1.3dcstats.info"));
        vSeeds.push_back(CDNSSeedData("s2", "seed2.3dcstats.info"));
        vSeeds.push_back(CDNSSeedData("s3", "seed3.3dcstats.info"));
        vSeeds.push_back(CDNSSeedData("s4", "seed4.3dcstats.info"));

       // 3DCoin addresses start with 'A'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 23);
        // 3DCoin script addresses start with '4'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 8);
        // 3DCoin private keys start with 'L'
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 130);
        // 3DCoin BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        // 3DCoin BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        // 3DCoin BIP44 coin type is '5'
        nExtCoinType = 5;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour

        strSporkPubKey = "0370d1a0a0db174bb3fa493f6f2773ec46eb5b784bb666882b3de00e5b5ffc38fb";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (  1, uint256S("0x00000dace491f29d20f137225c4db0342cb0a34ac96cb057ef03f00bd08ffaf1"))
            (  50, uint256S("0x000000d10472d9b2de295fef7bc7d6ce8c90fed450ae22e1e9a5731d5cd9ad05"))
            (  40805, uint256S("0x000000000a7c49002925d143b45ef89c1f47e6af9765c6ee0331af1530e70ebf"))
            (  170690, uint256S("0x0000000000115dd61216bb43ab76eb4921ffc33e47fd57e4c108c49371e5bd86"))
            (  409223, uint256S("0x000000000000033ac12f288d05943b22aa223de94b90c1c41594df08781d7528"))
            (  423781, uint256S("0x0000000000007a5f6302854a8f9a559ce640e82d503064ffe1316b076195ae6e"))
            (  471285, uint256S("0x00000000000c1e56df5f188d8776c5f268d218ae779eeabbe991eed0b9d58d38"))
            (  471285, uint256S("0x000000007048de8db28b27814fb768220be37a32f8f6839f9de88114fddfa281")),
            1562828734, // * UNIX timestamp of last checkpoint block
            441125,    // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0        // * estimated number of transactions per day after checkpoint
        };
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        //3DCoin V0.14
        consensus.nV014v1Start = 8450;
        consensus.nV014v2Start = 517000;
        consensus.nPrimenodePaymentsStartBlock = 600000; 
        consensus.nPulsenodePaymentsStartBlock = 750000;
        //3DCoin V0.14 
        consensus.nSubsidyHalvingInterval = 60000; 
        consensus.nMasternodePaymentsStartBlock = 500; 
        consensus.nMasternodePaymentsIncreaseBlock = 1000;
        consensus.nMasternodePaymentsIncreasePeriod = 300;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 10000000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 11000000; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x0000047d24635e347be3aaaeb66c26be94901a2f962feccd4f95090191f208c1");
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // 24 hours
        consensus.nPowTargetSpacing =  30; // 60 seconds
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1440; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        pchMessageStart[0] = 0xde;
        pchMessageStart[1] = 0xa2;
        pchMessageStart[2] = 0xc4;
        pchMessageStart[3] = 0xcf;
        nDefaultPort = 6696;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1521707748UL, 416965, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();



        assert(consensus.hashGenesisBlock == uint256S("0x000007c41652b32383518b0a5b6722c5a693c730c4be7f1abcb0f70e286a3edd"));
        assert(genesis.hashMerkleRoot == uint256S("0x01cece0a7bcfdc2b9341c9ebf9f96b9f9746c57d2be182ebeada84ff21d855a8"));

        
        
        vSeeds.push_back(CDNSSeedData("testseed1", "testseed1.3dcstats.net"));
        vSeeds.push_back(CDNSSeedData("testseed2", "testseed2.3dctalk.net"));
        vSeeds.push_back(CDNSSeedData("3dcstats1", "testseed1.3dcstats.info"));
        vSeeds.push_back(CDNSSeedData("3dcstats2", "testseed2.3dcstats.info"));

        // Testnet 3DCoin addresses start with 'T'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 66);
        // Testnet 3DCoin script addresses start with 't'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 128);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        // Testnet 3DCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Testnet 3DCoin BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Testnet 3DCoin BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        strSporkPubKey = "039e0a4361d749c97eb0bef5ec7686246f543e8da5f1772e4acae8b3d08eff835a";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (   0, uint256S("0x000007c41652b32383518b0a5b6722c5a693c730c4be7f1abcb0f70e286a3edd")),
            0, // * UNIX timestamp of last checkpoint block
            0,     // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0         // * estimated number of transactions per day after checkpoint
        };

    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        //3DCoin V0.14
        consensus.nV014v1Start = 380000;
        consensus.nV014v2Start = 517000;
        consensus.nPrimenodePaymentsStartBlock = 600000; 
        consensus.nPulsenodePaymentsStartBlock = 750000;
        //3DCoin V0.14 
        consensus.nSubsidyHalvingInterval = 150; 
        consensus.nMasternodePaymentsStartBlock = 240;
        consensus.nMasternodePaymentsIncreaseBlock = 350;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockCycle = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP34Hash = uint256();
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // 24 hours
        consensus.nPowTargetSpacing =  60; // 60 seconds
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;

        pchMessageStart[0] = 0xec;
        pchMessageStart[1] = 0xa1;
        pchMessageStart[2] = 0xc7;
        pchMessageStart[3] = 0xec;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDefaultPort = 6697;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1521707748, 0, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();






        assert(consensus.hashGenesisBlock == uint256S("0x0bc8fd0c140fe882fa62331845bb2cd92b030626376d0330d7fb542ddd4d5971"));
        assert(genesis.hashMerkleRoot == uint256S("0x01cece0a7bcfdc2b9341c9ebf9f96b9f9746c57d2be182ebeada84ff21d855a8"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0x0bc8fd0c140fe882fa62331845bb2cd92b030626376d0330d7fb542ddd4d5971")),
            1521707748,
            0,
            0
        };
        // Regtest 3DCoin addresses start with 'R'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 61);
        // Regtest 3DCoin script addresses start with 'r'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 123);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        // Regtest 3DCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Regtest 3DCoin BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Regtest 3DCoin BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;
   }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}
