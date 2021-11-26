// Copyright (c) 2021 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "test/test_trumpcoin.h"

#include "blockassembler.h"
#include "consensus/merkle.h"
#include "patriotnode-payments.h"
#include "patriotnode-sync.h"
#include "patriotnodeman.h"
#include "spork.h"
#include "primitives/transaction.h"
#include "utilmoneystr.h"
#include "util/blockstatecatcher.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(mnpayments_tests)

void enableMnSyncAndPNPayments()
{
    // force mnsync complete
    patriotnodeSync.RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FINISHED;

    // enable SPORK_13
    int64_t nTime = GetTime() - 10;
    CSporkMessage spork(SPORK_13_ENABLE_SUPERBLOCKS, nTime + 1, nTime);
    sporkManager.AddOrUpdateSporkMessage(spork);
    BOOST_CHECK(sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS));

    spork = CSporkMessage(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT, nTime + 1, nTime);
    sporkManager.AddOrUpdateSporkMessage(spork);
    BOOST_CHECK(sporkManager.IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT));
}

static bool CreatePNWinnerPayment(const CTxIn& mnVinVoter, int paymentBlockHeight, const CScript& payeeScript,
                                  const CKey& signerKey, const CPubKey& signerPubKey, CValidationState& state)
{
    CPatriotnodePaymentWinner mnWinner(mnVinVoter, paymentBlockHeight);
    mnWinner.AddPayee(payeeScript);
    BOOST_CHECK(mnWinner.Sign(signerKey, signerPubKey.GetID()));
    return patriotnodePayments.ProcessPNWinner(mnWinner, nullptr, state);
}

class PNdata
{
public:
    COutPoint collateralOut;
    CKey mnPrivKey;
    CPubKey mnPubKey;
    CPubKey collateralPubKey;
    CScript mnPayeeScript;

    PNdata(const COutPoint& collateralOut, const CKey& mnPrivKey, const CPubKey& mnPubKey,
           const CPubKey& collateralPubKey, const CScript& mnPayeeScript) :
           collateralOut(collateralOut), mnPrivKey(mnPrivKey), mnPubKey(mnPubKey),
           collateralPubKey(collateralPubKey), mnPayeeScript(mnPayeeScript) {}


};

CPatriotnode buildPN(const PNdata& data, const uint256& tipHash, uint64_t tipTime)
{
    CPatriotnode mn;
    mn.vin = CTxIn(data.collateralOut);
    mn.pubKeyCollateralAddress = data.mnPubKey;
    mn.pubKeyPatriotnode = data.collateralPubKey;
    mn.sigTime = GetTime() - 8000 - 1; // PN_WINNER_MINIMUM_AGE = 8000.
    mn.lastPing = CPatriotnodePing(mn.vin, tipHash, tipTime);
    return mn;
}

class FakePatriotnode {
public:
    explicit FakePatriotnode(CPatriotnode& mn, const PNdata& data) : mn(mn), data(data) {}
    CPatriotnode mn;
    PNdata data;
};

std::vector<FakePatriotnode> buildPNList(const uint256& tipHash, uint64_t tipTime, int size)
{
    std::vector<FakePatriotnode> ret;
    for (int i=0; i < size; i++) {
        CKey mnKey;
        mnKey.MakeNewKey(true);
        const CPubKey& mnPubKey = mnKey.GetPubKey();
        const CScript& mnPayeeScript = GetScriptForDestination(mnPubKey.GetID());
        // Fake collateral out and key for now
        COutPoint mnCollateral(GetRandHash(), 0);
        const CPubKey& collateralPubKey = mnPubKey;

        // Now add the PN
        PNdata mnData(mnCollateral, mnKey, mnPubKey, collateralPubKey, mnPayeeScript);
        CPatriotnode mn = buildPN(mnData, tipHash, tipTime);
        BOOST_CHECK(mnodeman.Add(mn));
        ret.emplace_back(mn, mnData);
    }
    return ret;
}

FakePatriotnode findPNData(std::vector<FakePatriotnode>& mnList, const PatriotnodeRef& ref)
{
    for (const auto& item : mnList) {
        if (item.data.mnPubKey == ref->pubKeyPatriotnode) {
            return item;
        }
    }
    throw std::runtime_error("PN not found");
}

bool findStrError(CValidationState& state, const std::string& str)
{
    return state.GetRejectReason().find(str) != std::string::npos;
}

BOOST_FIXTURE_TEST_CASE(mnwinner_test, TestChain100Setup)
{
    CreateAndProcessBlock({}, coinbaseKey);
    CBlock tipBlock = CreateAndProcessBlock({}, coinbaseKey);
    enableMnSyncAndPNPayments();
    int nextBlockHeight = 103;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V5_3, nextBlockHeight - 1);

    // PN list.
    std::vector<FakePatriotnode> mnList = buildPNList(tipBlock.GetHash(), tipBlock.GetBlockTime(), 40);
    std::vector<std::pair<int64_t, PatriotnodeRef>> mnRank = mnodeman.GetPatriotnodeRanks(nextBlockHeight - 100);

    // Test mnwinner failure for non-existent PN voter.
    CTxIn dummyVoter;
    CScript dummyPayeeScript;
    CKey dummyKey;
    dummyKey.MakeNewKey(true);
    CValidationState state0;
    BOOST_CHECK(!CreatePNWinnerPayment(dummyVoter, nextBlockHeight, dummyPayeeScript,
                                       dummyKey, dummyKey.GetPubKey(), state0));
    BOOST_CHECK_MESSAGE(findStrError(state0, "Non-existent mnwinner voter"), state0.GetRejectReason());

    // Take the first PN
    auto firstPN = findPNData(mnList, mnRank[0].second);
    CTxIn mnVinVoter(firstPN.mn.vin);
    int paymentBlockHeight = nextBlockHeight;
    CScript payeeScript = firstPN.data.mnPayeeScript;
    CPatriotnode* pFirstPN = mnodeman.Find(firstPN.mn.vin.prevout);
    pFirstPN->sigTime += 8000 + 1; // PN_WINNER_MINIMUM_AGE = 8000.
    // Voter PN1, fail because the sigTime - GetAdjustedTime() is not greater than PN_WINNER_MINIMUM_AGE.
    CValidationState state1;
    BOOST_CHECK(!CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight, payeeScript,
                                       firstPN.data.mnPrivKey, firstPN.data.mnPubKey, state1));
    // future: add specific error cause
    BOOST_CHECK_MESSAGE(findStrError(state1, "Patriotnode not in the top"), state1.GetRejectReason());

    // Voter PN2, fail because PN2 doesn't match with the signing keys.
    auto secondMn = findPNData(mnList, mnRank[1].second);
    CPatriotnode* pSecondPN = mnodeman.Find(secondMn.mn.vin.prevout);
    mnVinVoter = CTxIn(pSecondPN->vin);
    payeeScript = secondMn.data.mnPayeeScript;
    CValidationState state2;
    BOOST_CHECK(!CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight, payeeScript,
                                       firstPN.data.mnPrivKey, firstPN.data.mnPubKey, state2));
    BOOST_CHECK_MESSAGE(findStrError(state2, "invalid voter mnwinner signature"), state2.GetRejectReason());

    // Voter PN2, fail because mnwinner height is too far in the future.
    mnVinVoter = CTxIn(pSecondPN->vin);
    CValidationState state2_5;
    BOOST_CHECK(!CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight + 20, payeeScript,
                                       secondMn.data.mnPrivKey, secondMn.data.mnPubKey, state2_5));
    BOOST_CHECK_MESSAGE(findStrError(state2_5, "block height out of range"), state2_5.GetRejectReason());


    // Voter PN2, fail because PN2 is not enabled
    pSecondPN->SetSpent();
    BOOST_CHECK(!pSecondPN->IsEnabled());
    CValidationState state3;
    BOOST_CHECK(!CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight, payeeScript,
                                       secondMn.data.mnPrivKey, secondMn.data.mnPubKey, state3));
    // future: could add specific error cause.
    BOOST_CHECK_MESSAGE(findStrError(state3, "Patriotnode not in the top"), state3.GetRejectReason());

    // Voter PN3, fail because the payeeScript is not a P2PKH
    auto thirdMn = findPNData(mnList, mnRank[2].second);
    CPatriotnode* pThirdPN = mnodeman.Find(thirdMn.mn.vin.prevout);
    mnVinVoter = CTxIn(pThirdPN->vin);
    CScript scriptDummy = CScript() << OP_TRUE;
    CValidationState state4;
    BOOST_CHECK(!CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight, scriptDummy,
                                       thirdMn.data.mnPrivKey, thirdMn.data.mnPubKey, state4));
    BOOST_CHECK_MESSAGE(findStrError(state4, "payee must be a P2PKH"), state4.GetRejectReason());

    // Voter PN15 pays to PN3, fail because the voter is not in the top ten.
    auto voterPos15 = findPNData(mnList, mnRank[14].second);
    CPatriotnode* p15dPN = mnodeman.Find(voterPos15.mn.vin.prevout);
    mnVinVoter = CTxIn(p15dPN->vin);
    payeeScript = thirdMn.data.mnPayeeScript;
    CValidationState state6;
    BOOST_CHECK(!CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight, payeeScript,
                                       voterPos15.data.mnPrivKey, voterPos15.data.mnPubKey, state6));
    BOOST_CHECK_MESSAGE(findStrError(state6, "Patriotnode not in the top"), state6.GetRejectReason());

    // Voter PN3, passes
    mnVinVoter = CTxIn(pThirdPN->vin);
    CValidationState state7;
    BOOST_CHECK(CreatePNWinnerPayment(mnVinVoter, paymentBlockHeight, payeeScript,
                                      thirdMn.data.mnPrivKey, thirdMn.data.mnPubKey, state7));
    BOOST_CHECK_MESSAGE(state7.IsValid(), state7.GetRejectReason());

    // Create block and check that is being paid properly.
    tipBlock = CreateAndProcessBlock({}, coinbaseKey);
    BOOST_CHECK_MESSAGE(tipBlock.vtx[0]->vout.back().scriptPubKey == payeeScript, "error: block not paying to proper PN");
    nextBlockHeight++;

    // Now let's push two valid winner payments and make every PN in the top ten vote for them (having more votes in mnwinnerA than in mnwinnerB).
    mnRank = mnodeman.GetPatriotnodeRanks(nextBlockHeight - 100);
    CScript firstRankedPayee = GetScriptForDestination(mnRank[0].second->pubKeyCollateralAddress.GetID());
    CScript secondRankedPayee = GetScriptForDestination(mnRank[1].second->pubKeyCollateralAddress.GetID());

    // Let's vote with the first 6 nodes for PN ranked 1
    // And with the last 4 nodes for PN ranked 2
    payeeScript = firstRankedPayee;
    for (int i=0; i<10; i++) {
        if (i > 5) {
            payeeScript = secondRankedPayee;
        }
        auto voterMn = findPNData(mnList, mnRank[i].second);
        CPatriotnode* pVoterPN = mnodeman.Find(voterMn.mn.vin.prevout);
        mnVinVoter = CTxIn(pVoterPN->vin);
        CValidationState stateInternal;
        BOOST_CHECK(CreatePNWinnerPayment(mnVinVoter, nextBlockHeight, payeeScript,
                                                             voterMn.data.mnPrivKey, voterMn.data.mnPubKey, stateInternal));
        BOOST_CHECK_MESSAGE(stateInternal.IsValid(), stateInternal.GetRejectReason());
    }

    // Check the votes count for each mnwinner.
    CPatriotnodeBlockPayees blockPayees = patriotnodePayments.mapPatriotnodeBlocks.at(nextBlockHeight);
    BOOST_CHECK_MESSAGE(blockPayees.HasPayeeWithVotes(firstRankedPayee, 6), "first ranked payee with no enough votes");
    BOOST_CHECK_MESSAGE(blockPayees.HasPayeeWithVotes(secondRankedPayee, 4), "second ranked payee with no enough votes");

    // let's try to create a bad block paying to the second most voted PN.
    CBlock badBlock = CreateBlock({}, coinbaseKey);
    CMutableTransaction coinbase(*badBlock.vtx[0]);
    coinbase.vout[coinbase.vout.size() - 1].scriptPubKey = secondRankedPayee;
    badBlock.vtx[0] = MakeTransactionRef(coinbase);
    badBlock.hashMerkleRoot = BlockMerkleRoot(badBlock);
    {
        auto pBadBlock = std::make_shared<CBlock>(badBlock);
        SolveBlock(pBadBlock, nextBlockHeight);
        BlockStateCatcher sc(pBadBlock->GetHash());
        sc.registerEvent();
        ProcessNewBlock(pBadBlock, nullptr);
        BOOST_CHECK(sc.found && !sc.state.IsValid());
        BOOST_CHECK_EQUAL(sc.state.GetRejectReason(), "bad-cb-payee");
    }
    BOOST_CHECK(WITH_LOCK(cs_main, return chainActive.Tip()->GetBlockHash();) != badBlock.GetHash());


    // And let's verify that the most voted one is the one being paid.
    tipBlock = CreateAndProcessBlock({}, coinbaseKey);
    BOOST_CHECK_MESSAGE(tipBlock.vtx[0]->vout.back().scriptPubKey == firstRankedPayee, "error: block not paying to first ranked PN");
    nextBlockHeight++;

    //
    // Generate 125 blocks paying to different PNs to load the payments cache.
    for (int i = 0; i < 125; i++) {
        mnRank = mnodeman.GetPatriotnodeRanks(nextBlockHeight - 100);
        payeeScript = GetScriptForDestination(mnRank[0].second->pubKeyCollateralAddress.GetID());
        for (int j=0; j<7; j++) { // votes
            auto voterMn = findPNData(mnList, mnRank[j].second);
            CPatriotnode* pVoterPN = mnodeman.Find(voterMn.mn.vin.prevout);
            mnVinVoter = CTxIn(pVoterPN->vin);
            CValidationState stateInternal;
            BOOST_CHECK(CreatePNWinnerPayment(mnVinVoter, nextBlockHeight, payeeScript,
                                              voterMn.data.mnPrivKey, voterMn.data.mnPubKey, stateInternal));
            BOOST_CHECK_MESSAGE(stateInternal.IsValid(), stateInternal.GetRejectReason());
        }
        // Create block and check that is being paid properly.
        tipBlock = CreateAndProcessBlock({}, coinbaseKey);
        BOOST_CHECK_MESSAGE(tipBlock.vtx[0]->vout.back().scriptPubKey == payeeScript, "error: block not paying to proper PN");
        nextBlockHeight++;
    }
    // Check chain height.
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return chainActive.Height();), nextBlockHeight - 1);

    // Let's now verify what happen if a previously paid PN goes offline but still have scheduled a payment in the future.
    // The current system allows it (up to a certain point) as payments are scheduled ahead of time and a PN can go down in the
    // [proposedWinnerHeightTime < currentHeight < currentHeight + 20] window.

    // 1) Schedule payment and vote for it with the first 6 PNs.
    mnRank = mnodeman.GetPatriotnodeRanks(nextBlockHeight - 100);
    PatriotnodeRef mnToPay = mnRank[0].second;
    payeeScript = GetScriptForDestination(mnToPay->pubKeyCollateralAddress.GetID());
    for (int i=0; i<6; i++) {
        auto voterMn = findPNData(mnList, mnRank[i].second);
        CPatriotnode* pVoterPN = mnodeman.Find(voterMn.mn.vin.prevout);
        mnVinVoter = CTxIn(pVoterPN->vin);
        CValidationState stateInternal;
        BOOST_CHECK(CreatePNWinnerPayment(mnVinVoter, nextBlockHeight, payeeScript,
                                          voterMn.data.mnPrivKey, voterMn.data.mnPubKey, stateInternal));
        BOOST_CHECK_MESSAGE(stateInternal.IsValid(), stateInternal.GetRejectReason());
    }

    // 2) Remove payee PN from the PN list and try to emit a vote from PN7 to the same payee.
    // it should still be accepted because the PN was scheduled when it was online.
    mnodeman.Remove(mnToPay->vin.prevout);
    BOOST_CHECK_MESSAGE(!mnodeman.Find(mnToPay->vin.prevout), "error: removed PN is still available");

    // Now emit the vote from PN7
    auto voterMn = findPNData(mnList, mnRank[7].second);
    CPatriotnode* pVoterPN = mnodeman.Find(voterMn.mn.vin.prevout);
    mnVinVoter = CTxIn(pVoterPN->vin);
    CValidationState stateInternal;
    BOOST_CHECK(CreatePNWinnerPayment(mnVinVoter, nextBlockHeight, payeeScript,
                                      voterMn.data.mnPrivKey, voterMn.data.mnPubKey, stateInternal));
    BOOST_CHECK_MESSAGE(stateInternal.IsValid(), stateInternal.GetRejectReason());
}

BOOST_AUTO_TEST_SUITE_END()