#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include "order.h"
#include <map>
#include <list>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <iostream>
#include <optional>

class OrderBook {
public:
    explicit OrderBook(int64_t tickScale = 100); // e.g., 100 = cents
    ~OrderBook();

    // Ingest one line (human-readable or compact CSV). Returns true if processed.
    bool addFromLine(const std::string& line);

    // Direct API
    bool addOrder(const Order& o);
    bool cancelOrder(int orderId, const std::string& timestamp = "");
    bool modifyOrder(int orderId, Price newPxTicks, int newQty, const std::string& timestamp = "");

    // Queries (convert internal ticks to doubles)
    bool   bestBidAsk(double& bid, int& bidQty, double& ask, int& askQty) const;
    double midPrice() const;
    double spread() const;

    // Outputs
    void   printBook(std::ostream& os = std::cout, int depth = 10) const;
    void   printTrades(std::ostream& os = std::cout) const;
    void   dumpSnapshot(std::ostream& os, int depth = 10) const;

    // Logging configuration
    void   setTradesCsvPath(const std::string& path);
    void   setQuotesCsvPath(const std::string& path);
    void   setSnapshotCadence(size_t everyN, const std::string& dir);

    // Tick accounting (call after each processed input event)
    void   onTick(const std::string& timestamp = "");

private:
    struct LevelInfo {
        std::list<Order> orders; // FIFO
        int              totalQty{0};
    };

    using BookSide = std::map<Price, LevelInfo>; // asks ascending; bids ascending (best = rbegin)
    BookSide asks_;
    BookSide bids_;

    // id -> (side, price, iterator into Level)
    std::unordered_map<int, std::tuple<OrderSide,Price,std::list<Order>::iterator>> idIndex_;

    // Trades (also persisted to CSV)
    std::vector<Trade> trades_;

    // Cached top-of-book (ticks)
    Price bestBidPx_{std::numeric_limits<Price>::min()};
    int   bestBidQty_{0};
    Price bestAskPx_{std::numeric_limits<Price>::max()};
    int   bestAskQty_{0};

    // For quote-change detection
    Price lastQuotedBid_{std::numeric_limits<Price>::min()};
    int   lastQuotedBidQty_{0};
    Price lastQuotedAsk_{std::numeric_limits<Price>::max()};
    int   lastQuotedAskQty_{0};

    // CSV sinks (opened lazily)
    std::string tradesCsvPath_;
    std::string quotesCsvPath_;
    std::ofstream tradesCsv_;
    std::ofstream quotesCsv_;

    // Snapshots
    size_t  snapshotEvery_{0};
    size_t  tick_{0};
    std::string snapshotDir_;

    // Auto id if feed doesn't provide one
    int nextOrderId_{1};

    // Price tick scale (ticks per $1.0)
    int64_t tickScale_{100}; // e.g., cents

    // Matching (single templated engine)
    template<OrderSide SIDE>
    bool match(Order& incoming);
    bool canFullyFill(OrderSide side, std::optional<Price> limitPx, int qty) const;

    // Helpers
    void restOrder(const Order& o);
    void eraseLevelIfEmpty(OrderSide side, Price px);
    void updateBestOnAdd(OrderSide side, Price px);
    void updateBestOnChange();
    void emitQuoteIfChanged(const std::string& ts);
    void logTrade(const std::string& ts, Price pxTicks, int qty, int buyId, int sellId);

    // Parsing
    bool parseHumanLine(const std::string& line, Order& out, bool& isCancel, bool& isModify,
                        int& modId, Price& modPxTicks, int& modQty);
    bool parseCompactCsvLine(const std::string& line, Order& out, bool& isCancel, bool& isModify,
                             int& modId, Price& modPxTicks, int& modQty);
    static std::optional<OrderSide> parseSide(const std::string& s);
    static std::optional<OrderType> parseType(const std::string& s);
    static std::optional<TimeInForce> parseTif(const std::string& s);

    // Tick helpers
    inline double fromTicks(Price p) const { return static_cast<double>(p) / static_cast<double>(tickScale_); }
    Price toTicks(double px) const;
};

#endif // ORDERBOOK_H
