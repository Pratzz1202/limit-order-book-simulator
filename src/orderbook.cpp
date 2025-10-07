#include "orderbook.h"
#include <sstream>
#include <iomanip>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <cmath>

namespace {
inline bool safe_stoi(const std::string& s, int& out) {
    try { size_t i=0; int v = std::stoi(s, &i); if (i != s.size()) return false; out = v; return true; }
    catch (...) { return false; }
}
inline bool safe_stod(const std::string& s, double& out) {
    try { size_t i=0; double v = std::stod(s, &i); if (i != s.size()) return false; out = v; return true; }
    catch (...) { return false; }
}
}

OrderBook::OrderBook(int64_t tickScale) : tickScale_(tickScale) {}
OrderBook::~OrderBook() {
    if (tradesCsv_.is_open()) tradesCsv_.close();
    if (quotesCsv_.is_open()) quotesCsv_.close();
}

Price OrderBook::toTicks(double px) const {
    // round to nearest tick
    return static_cast<Price>(std::llround(px * static_cast<double>(tickScale_)));
}

void OrderBook::setTradesCsvPath(const std::string& path) {
    tradesCsvPath_ = path;
    if (!path.empty()) {
        tradesCsv_.open(path, std::ios::out);
        if (tradesCsv_.is_open()) {
            tradesCsv_ << "timestamp,price,qty,buy_id,sell_id\n";
        }
    }
}
void OrderBook::setQuotesCsvPath(const std::string& path) {
    quotesCsvPath_ = path;
    if (!path.empty()) {
        quotesCsv_.open(path, std::ios::out);
        if (quotesCsv_.is_open()) {
            quotesCsv_ << "timestamp,best_bid,bid_qty,best_ask,ask_qty,spread,mid\n";
        }
    }
}
void OrderBook::setSnapshotCadence(size_t everyN, const std::string& dir) {
    snapshotEvery_ = everyN;
    snapshotDir_   = dir;
    if (snapshotEvery_ > 0 && !snapshotDir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(snapshotDir_, ec);
    }
}

bool OrderBook::addFromLine(const std::string& line) {
    if (line.empty()) return false;
    // Skip blanks and comments starting with '#'
    size_t pos = line.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return false;
    if (line[pos] == '#') return false;

    Order o;
    bool isCancel=false, isModify=false;
    int modId=0, modQty=0;
    Price modPxTicks=0;

    if (!parseHumanLine(line, o, isCancel, isModify, modId, modPxTicks, modQty) &&
        !parseCompactCsvLine(line, o, isCancel, isModify, modId, modPxTicks, modQty)) {
        // Unrecognized or malformed line — ignore safely
        return false;
    }
    if (isCancel)  return cancelOrder(modId, o.timestamp);
    if (isModify)  return modifyOrder(modId, modPxTicks, modQty, o.timestamp);
    return addOrder(o);
}

bool OrderBook::addOrder(const Order& in) {
    Order o = in;
    if (o.id == 0) o.id = nextOrderId_++;

    if (o.type == OrderType::MARKET) {
        if (o.side == OrderSide::BUY) match<OrderSide::BUY>(o);
        else                          match<OrderSide::SELL>(o);
        emitQuoteIfChanged(o.timestamp);
        return true;
    }

    // LIMIT
    if (o.side == OrderSide::BUY) {
        match<OrderSide::BUY>(o);
        if (o.quantity > 0 && o.tif != TimeInForce::IOC && o.tif != TimeInForce::FOK) {
            restOrder(o);
        }
    } else {
        match<OrderSide::SELL>(o);
        if (o.quantity > 0 && o.tif != TimeInForce::IOC && o.tif != TimeInForce::FOK) {
            restOrder(o);
        }
    }
    emitQuoteIfChanged(o.timestamp);
    return true;
}

bool OrderBook::cancelOrder(int orderId, const std::string& ts) {
    auto it = idIndex_.find(orderId);
    if (it == idIndex_.end()) return false;
    auto [side, px, lit] = it->second;
    auto& book = (side == OrderSide::BUY) ? bids_ : asks_;
    auto b = book.find(px);
    if (b == book.end()) return false;
    b->second.totalQty -= lit->quantity;
    b->second.orders.erase(lit);
    idIndex_.erase(it);
    if (b->second.orders.empty()) eraseLevelIfEmpty(side, px);
    updateBestOnChange();
    emitQuoteIfChanged(ts);
    return true;
}

bool OrderBook::modifyOrder(int orderId, Price newPxTicks, int newQty, const std::string& ts) {
    if (newQty <= 0) return cancelOrder(orderId, ts);

    auto itIdx = idIndex_.find(orderId);
    if (itIdx == idIndex_.end()) return false;

    auto [side, oldPx, lit] = itIdx->second;
    auto& fromBook = (side == OrderSide::BUY) ? bids_ : asks_;
    auto fb = fromBook.find(oldPx);
    if (fb == fromBook.end()) return false;

    // 1) Copy the order (value type) out
    Order o = *lit;

    // 2) Drop the old index entry BEFORE we erase the list node
    idIndex_.erase(itIdx);

    // 3) Remove from old level
    fb->second.totalQty -= o.quantity;
    fb->second.orders.erase(lit);
    if (fb->second.orders.empty()) eraseLevelIfEmpty(side, oldPx);

    // 4) Apply new fields
    o.priceTicks = newPxTicks;
    o.quantity   = newQty;

    // 5) Try to match at the new price
    if (side == OrderSide::BUY) match<OrderSide::BUY>(o);
    else                        match<OrderSide::SELL>(o);

    // 6) If still has remainder, re-rest and re-index
    if (o.quantity > 0) restOrder(o);

    updateBestOnChange();
    emitQuoteIfChanged(ts);
    return true;
}


bool OrderBook::bestBidAsk(double& bid, int& bidQty, double& ask, int& askQty) const {
    if (bids_.empty() || asks_.empty()) return false;
    bid = fromTicks(bestBidPx_); bidQty = bestBidQty_;
    ask = fromTicks(bestAskPx_); askQty = bestAskQty_;
    return true;
}

double OrderBook::midPrice() const {
    if (bids_.empty() || asks_.empty()) return std::numeric_limits<double>::quiet_NaN();
    return (fromTicks(bestBidPx_) + fromTicks(bestAskPx_)) * 0.5;
}

double OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::numeric_limits<double>::quiet_NaN();
    return fromTicks(bestAskPx_ - bestBidPx_);
}

void OrderBook::printTrades(std::ostream& os) const {
    for (const auto& t : trades_) {
        os << t.timestamp << " - " << t.quantity << " @ " << std::fixed << std::setprecision(2)
           << t.price << " (BUY #" << t.buyId << " - SELL #" << t.sellId << ")\n";
    }
}

void OrderBook::dumpSnapshot(std::ostream& os, int depth) const {
    os << "=== SNAPSHOT ===\n";
    printBook(os, depth);
    os << "================\n";
}

void OrderBook::printBook(std::ostream& os, int depth) const {
    os << "----- ORDER BOOK -----\n";
    int printed = 0;
    for (auto it = asks_.begin(); it != asks_.end() && printed < depth; ++it, ++printed) {
        os << "ASK " << std::fixed << std::setprecision(2) << fromTicks(it->first)
           << " x " << it->second.totalQty << "\n";
    }
    printed = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && printed < depth; ++it, ++printed) {
        os << "BID " << std::fixed << std::setprecision(2) << fromTicks(it->first)
           << " x " << it->second.totalQty << "\n";
    }
    if (!bids_.empty() && !asks_.empty()) {
        os << "BestBid " << fromTicks(bestBidPx_) << " ("<< bestBidQty_ << "), "
           << "BestAsk " << fromTicks(bestAskPx_) << " ("<< bestAskQty_ << ")"
           << " | Spread " << spread() << " | Mid " << midPrice() << "\n";
    } else {
        os << "No full top-of-book.\n";
    }
}

void OrderBook::onTick(const std::string&) {
    ++tick_;
    if (snapshotEvery_ > 0 && tick_ % snapshotEvery_ == 0 && !snapshotDir_.empty()) {
        std::ostringstream fn;
        fn << snapshotDir_ << "/snapshot_" << std::setw(9) << std::setfill('0') << tick_ << ".txt";
        std::ofstream out(fn.str());
        if (out) dumpSnapshot(out);
    }
}

// --------------- internals ---------------

void OrderBook::restOrder(const Order& o) {
    auto& book = (o.side == OrderSide::BUY) ? bids_ : asks_;
    auto& lvl  = book[o.priceTicks];
    lvl.orders.emplace_back(o);
    lvl.totalQty += o.quantity;
    auto it = std::prev(lvl.orders.end());
    idIndex_[o.id] = {o.side, o.priceTicks, it};
    updateBestOnAdd(o.side, o.priceTicks);
}

void OrderBook::eraseLevelIfEmpty(OrderSide side, Price px) {
    auto& book = (side == OrderSide::BUY) ? bids_ : asks_;
    auto it = book.find(px);
    if (it != book.end() && it->second.orders.empty()) {
        book.erase(it);
    }
}

void OrderBook::updateBestOnAdd(OrderSide, Price) { updateBestOnChange(); }

void OrderBook::updateBestOnChange() {
    if (bids_.empty()) { bestBidPx_ = std::numeric_limits<Price>::min(); bestBidQty_=0; }
    else { bestBidPx_ = bids_.rbegin()->first; bestBidQty_ = bids_.rbegin()->second.totalQty; }
    if (asks_.empty()) { bestAskPx_ = std::numeric_limits<Price>::max(); bestAskQty_=0; }
    else { bestAskPx_ = asks_.begin()->first; bestAskQty_ = asks_.begin()->second.totalQty; }
}

void OrderBook::emitQuoteIfChanged(const std::string& ts) {
    if (!quotesCsv_.is_open()) return;
    bool changed =
        ((bids_.empty()) != (lastQuotedBid_ == std::numeric_limits<Price>::min())) ||
        ((asks_.empty()) != (lastQuotedAsk_ == std::numeric_limits<Price>::max())) ||
        (!bids_.empty() && (bestBidPx_ != lastQuotedBid_ || bestBidQty_ != lastQuotedBidQty_)) ||
        (!asks_.empty() && (bestAskPx_ != lastQuotedAsk_ || bestAskQty_ != lastQuotedAskQty_));
    if (!changed) return;

    lastQuotedBid_ = bids_.empty() ? std::numeric_limits<Price>::min() : bestBidPx_;
    lastQuotedBidQty_ = bestBidQty_;
    lastQuotedAsk_ = asks_.empty() ? std::numeric_limits<Price>::max() : bestAskPx_;
    lastQuotedAskQty_ = bestAskQty_;

    double spr = spread();
    double mid = midPrice();
    quotesCsv_ << ts << ","
               << (bids_.empty() ? std::string() : std::to_string(fromTicks(bestBidPx_))) << ","
               << bestBidQty_ << ","
               << (asks_.empty() ? std::string() : std::to_string(fromTicks(bestAskPx_))) << ","
               << bestAskQty_ << ","
               << (std::isnan(spr) ? std::string() : std::to_string(spr)) << ","
               << (std::isnan(mid) ? std::string() : std::to_string(mid)) << "\n";
}

void OrderBook::logTrade(const std::string& ts, Price pxTicks, int qty, int buyId, int sellId) {
    double px = fromTicks(pxTicks);
    trades_.push_back(Trade{ts, px, qty, buyId, sellId});
    if (tradesCsv_.is_open()) {
        tradesCsv_ << ts << "," << px << "," << qty << "," << buyId << "," << sellId << "\n";
    }
}

// Templated matcher (handles BUY or SELL)
template<OrderSide SIDE>
bool OrderBook::match(Order& incoming) {
    // FOK pre-check
    if (incoming.tif == TimeInForce::FOK) {
        std::optional<Price> limit = (incoming.type == OrderType::LIMIT)
            ? std::optional<Price>(incoming.priceTicks) : std::nullopt;
        if (!canFullyFill(SIDE, limit, incoming.quantity)) return true;
    }

    auto &opp = (SIDE == OrderSide::BUY) ? asks_ : bids_;
    auto crosses = [&](Price topPx) {
        if (incoming.type == OrderType::MARKET) return true;
        if constexpr (SIDE == OrderSide::BUY)  return topPx <= incoming.priceTicks;
        else                                    return topPx >= incoming.priceTicks;
    };

    while (incoming.quantity > 0 && !opp.empty()) {
        // Best opposite level
        auto it = (SIDE == OrderSide::BUY) ? opp.begin() : std::prev(opp.end());
        Price px = it->first;
        if (!crosses(px)) break;

        auto& lvl = it->second;
        while (incoming.quantity > 0 && !lvl.orders.empty()) {
            auto lit = lvl.orders.begin();
            Order& maker = *lit;
            int traded = std::min(incoming.quantity, maker.quantity);
            if constexpr (SIDE == OrderSide::BUY)
                logTrade(incoming.timestamp, px, traded, incoming.id, maker.id);
            else
                logTrade(incoming.timestamp, px, traded, maker.id, incoming.id);

            incoming.quantity -= traded;
            maker.quantity    -= traded;
            lvl.totalQty      -= traded;
            if (maker.quantity == 0) {
                idIndex_.erase(maker.id);
                lvl.orders.erase(lit);
            }
        }
        if (lvl.orders.empty()) opp.erase(it);
        updateBestOnChange();

        if (incoming.type == OrderType::MARKET) {
            if (opp.empty()) break;
        } else {
            if (opp.empty()) break;
            // re-check crossing for LIMIT after potential best changed
            auto nextTop = (SIDE == OrderSide::BUY) ? opp.begin()->first : std::prev(opp.end())->first;
            if (!crosses(nextTop)) break;
        }
    }
    return true;
}

bool OrderBook::canFullyFill(OrderSide side, std::optional<Price> limitPx, int qty) const {
    int need = qty;
    if (side == OrderSide::BUY) {
        for (auto it = asks_.begin(); it != asks_.end() && need > 0; ++it) {
            Price px = it->first;
            if (limitPx && px > *limitPx) break;
            need -= it->second.totalQty;
        }
    } else {
        for (auto it = bids_.rbegin(); it != bids_.rend() && need > 0; ++it) {
            Price px = it->first;
            if (limitPx && px < *limitPx) break;
            need -= it->second.totalQty;
        }
    }
    return need <= 0;
}

// -------- Parsing --------

std::optional<OrderSide> OrderBook::parseSide(const std::string& s) {
    if (s == "BUY") return OrderSide::BUY;
    if (s == "SELL") return OrderSide::SELL;
    return std::nullopt;
}
std::optional<OrderType> OrderBook::parseType(const std::string& s) {
    if (s == "LIMIT") return OrderType::LIMIT;
    if (s == "MARKET") return OrderType::MARKET;
    return std::nullopt;
}
std::optional<TimeInForce> OrderBook::parseTif(const std::string& s) {
    if (s == "GTC") return TimeInForce::GTC;
    if (s == "IOC") return TimeInForce::IOC;
    if (s == "FOK") return TimeInForce::FOK;
    if (s == "DAY") return TimeInForce::DAY;
    return std::nullopt;
}

bool OrderBook::parseHumanLine(const std::string& line, Order& out, bool& isCancel, bool& isModify,
                               int& modId, Price& modPxTicks, int& modQty) {
    std::istringstream iss(line);
    std::string ts;
    if (!(iss >> ts)) return false;
    std::string word;
    if (!(iss >> word)) return false;

    if (word == "CANCEL") {
        std::string tok;
        while (iss >> tok) {
            if (tok.rfind("id=",0)==0) {
                std::string v = tok.substr(3);
                int idv=0; if (!v.empty() && safe_stoi(v, idv)) { modId = idv; isCancel = true; out.timestamp = ts; return true; }
                else return false;
            }
        }
        return false;
    }
    if (word == "MODIFY") {
        std::string tok;
        bool haveId=false, havePx=false, haveQty=false;
        while (iss >> tok) {
            if (tok.rfind("id=",0)==0) {
                std::string v = tok.substr(3); int idv=0; if (!v.empty() && safe_stoi(v,idv)) { modId=idv; haveId=true; }
                else return false;
            } else if (tok.rfind("price=",0)==0) {
                std::string v = tok.substr(6); double px=0; if (!v.empty() && safe_stod(v,px)) { modPxTicks=toTicks(px); havePx=true; }
                else return false;
            } else if (tok.rfind("qty=",0)==0) {
                std::string v = tok.substr(4); int q=0; if (!v.empty() && safe_stoi(v,q) && q>0) { modQty=q; haveQty=true; }
                else return false;
            }
        }
        if (!(haveId && havePx && haveQty)) return false;
        isModify = true; out.timestamp = ts; return true;
    }

    // TYPE SIDE ...
    auto maybeType = parseType(word);
    if (!maybeType) return false;
    std::string sideStr; if (!(iss >> sideStr)) return false;
    auto maybeSide = parseSide(sideStr);
    if (!maybeSide) return false;

    out.timestamp = ts;
    out.type = *maybeType;
    out.side = *maybeSide;
    out.tif = TimeInForce::GTC;
    out.id = 0;

    if (out.type == OrderType::LIMIT) {
        std::string pxStr, qtyStr;
        if (!(iss >> pxStr >> qtyStr)) return false;
        double px=0; int q=0;
        if (!safe_stod(pxStr, px) || !safe_stoi(qtyStr, q)) return false;
        out.priceTicks = toTicks(px); out.quantity = q;
    } else { // MARKET
        std::string qtyStr; if (!(iss >> qtyStr)) return false;
        int q=0; if (!safe_stoi(qtyStr, q)) return false;
        out.quantity = q; out.priceTicks = 0;
    }
    // optional tokens
    std::string tok;
    while (iss >> tok) {
        if (tok.rfind("id=",0)==0) {
            std::string v = tok.substr(3); int idv=0; if (!v.empty() && safe_stoi(v,idv)) out.id = idv;
        } else if (tok.rfind("tif=",0)==0) {
            auto maybeT = parseTif(tok.substr(4)); if (maybeT) out.tif = *maybeT;
        }
    }
    return true;
}

bool OrderBook::parseCompactCsvLine(const std::string& line, Order& out, bool& isCancel, bool& isModify,
                                    int& modId, Price& modPxTicks, int& modQty) {
    // Compact:
    // A,ts,id,side,price,qty[,tif]
    // X,ts,id
    // M,ts,id,price,qty
    if (line.empty()) return false;
    char tag = line[0];
    if (!(tag=='A' || tag=='X' || tag=='M')) return false;

    std::vector<std::string> parts; parts.reserve(8);
    std::string cur; cur.reserve(line.size());
    for (char c : line) {
        if (c==',') { parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    parts.push_back(cur);

    if (parts.size() < 3) return false;
    const std::string& ts = parts[1];

    if (tag=='X') {
        if (parts.size()<3) return false;
        int idv=0; if (!safe_stoi(parts[2], idv)) return false;
        modId = idv; isCancel = true; out.timestamp = ts; return true;
    } else if (tag=='M') {
        if (parts.size()<5) return false;
        int idv=0, q=0; double px=0;
        if (!safe_stoi(parts[2], idv)) return false;
        if (!safe_stod(parts[3], px)) return false;
        if (!safe_stoi(parts[4], q) || q<=0) return false;
        modId=idv; modPxTicks=toTicks(px); modQty=q; isModify=true; out.timestamp = ts; return true;
    } else { // 'A'
        if (parts.size()<6) return false;
        int idv=0, q=0; double px=0;
        if (!safe_stoi(parts[2], idv)) return false;
        auto maybeSide = parseSide(parts[3]); if (!maybeSide) return false;
        if (!safe_stod(parts[4], px)) return false;
        if (!safe_stoi(parts[5], q)) return false;

        out.id = idv;
        out.timestamp = ts;
        out.side = *maybeSide;
        out.type = OrderType::LIMIT;
        out.priceTicks = toTicks(px);
        out.quantity = q;
        out.tif = TimeInForce::GTC;
        if (parts.size()>=7) {
            auto maybeT = parseTif(parts[6]); if (maybeT) out.tif = *maybeT;
        }
        return true;
    }
}
