#pragma once 

#include <string>
#include <vector>

namespace icarus::connectors::polymarket {
//minimal Market fields pulled from Polymarket API 
struct RawMarket {
    std::string id;
    std::string question;
    bool active;

};

//Raw Price Level before canonical probability conversion 
struct RawPriceLevel {
    

};

// Raw Polymarket Orderbook

struct RawOrderBook {

}

}
