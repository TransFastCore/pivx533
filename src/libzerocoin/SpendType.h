// Copyright (c) 2018 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TrumpCoin_SPENDTYPE_H
#define TrumpCoin_SPENDTYPE_H

#include <cstdint>

namespace libzerocoin {
    enum SpendType : uint8_t {
        SPEND, // Used for a typical spend transaction, zTRUMP should be unusable after
        STAKE, // Used for a spend that occurs as a stake
        PN_COLLATERAL, // Used when proving ownership of zTRUMP that will be used for patriotnodes (future)
        SIGN_MESSAGE // Used to sign messages that do not belong above (future)
    };
}

#endif //TrumpCoin_SPENDTYPE_H
