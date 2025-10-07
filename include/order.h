#ifndef ORDER_H
#define ORDER_H

#include <string>
#include <cstdint>
#include <limits>

enum class OrderSide { BUY, SELL };
enum class OrderType { LIMIT, MARKET };
enum class TimeInForce { GTC, IOC, FOK, DAY };

using Price = int64_t; // integer price ticks

struct Order {
    int         id{0};
    std::string timestamp;           // free-form; used for logs
    OrderSide   side{OrderSide::BUY};
    OrderType   type{OrderType::LIMIT};
    TimeInForce tif{TimeInForce::GTC};
    Price       priceTicks{0};       // integer ticks (e.g., cents)
    int         quantity{0};

    Order() = default;
    Order(int id_,
          const std::string& ts_,
          OrderSide side_,
          OrderType type_,
          TimeInForce tif_,
          Price priceTicks_,
          int quantity_)
        : id(id_), timestamp(ts_), side(side_), type(type_), tif(tif_),
          priceTicks(priceTicks_), quantity(quantity_) {}
};

struct Trade {
    std::string timestamp;
    double      price{0.0}; // human-readable dollars (converted from ticks at log time)
    int         quantity{0};
    int         buyId{0};
    int         sellId{0};
};

#endif // ORDER_H
