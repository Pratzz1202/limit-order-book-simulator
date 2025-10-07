#include "orderbook.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct Args {
    std::string inputFile;
    std::string tradesCsv = "data/trades.csv";
    std::string quotesCsv = "data/quotes.csv";
    std::string latencyCsv= "data/latency.csv";
    std::string snapshotDir = "data/snapshots";
    size_t snapshotEvery = 0;
    int64_t tickScale = 100; // ticks per $1.00 (default: cents)
};

static Args parseArgs(int argc, char* argv[]) {
    Args a;
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_file> [--snapshot-every N|=N] [--snap-dir DIR|=DIR] "
                     "[--trades-csv PATH|=PATH] [--quotes-csv PATH|=PATH] [--latency-csv PATH|=PATH] "
                     "[--tick-scale N|=N]\n";
        std::exit(1);
    }
    a.inputFile = argv[1];

    auto next_is_value = [&](int i) {
        return (i+1 < argc) && argv[i+1][0] != '-';
    };

    for (int i=2; i<argc; ++i) {
        std::string s(argv[i]);
        std::string key = s, val;

        auto eq = s.find('=');
        if (eq != std::string::npos) {
            key = s.substr(0, eq);
            val = s.substr(eq + 1);
        } else if (next_is_value(i)) {
            key = s;
            val = argv[++i];
        }

        auto need = [&](const char* k){
            if (val.empty()) { std::cerr << "Missing value for " << k << "\n"; std::exit(2); }
        };

        if (key == "--snapshot-every") {
            need("--snapshot-every");
            try { a.snapshotEvery = static_cast<size_t>(std::stoul(val)); }
            catch (...) { std::cerr << "Invalid number for --snapshot-every: " << val << "\n"; std::exit(2); }
        } else if (key == "--snap-dir") {
            need("--snap-dir"); a.snapshotDir = val;
        } else if (key == "--trades-csv") {
            need("--trades-csv"); a.tradesCsv = val;
        } else if (key == "--quotes-csv") {
            need("--quotes-csv"); a.quotesCsv = val;
        } else if (key == "--latency-csv") {
            need("--latency-csv"); a.latencyCsv = val;
        } else if (key == "--tick-scale") {
            need("--tick-scale");
            try { a.tickScale = static_cast<int64_t>(std::stoll(val)); }
            catch (...) { std::cerr << "Invalid number for --tick-scale: " << val << "\n"; std::exit(2); }
        } else {
            std::cerr << "Unknown option: " << s << "\n";
            std::exit(2);
        }
    }
    return a;
}

int main(int argc, char* argv[]) {
    auto args = parseArgs(argc, argv);

    std::ifstream fin(args.inputFile);
    if (!fin) {
        std::cerr << "Failed to open input: " << args.inputFile << "\n";
        return 1;
    }

    OrderBook book(args.tickScale);
    if (!args.tradesCsv.empty()) book.setTradesCsvPath(args.tradesCsv);
    if (!args.quotesCsv.empty()) book.setQuotesCsvPath(args.quotesCsv);
    if (args.snapshotEvery > 0)  book.setSnapshotCadence(args.snapshotEvery, args.snapshotDir);

    std::vector<long long> latencies;
    latencies.reserve(200000);

    std::string line;
    while (std::getline(fin, line)) {
        auto t0 = std::chrono::high_resolution_clock::now();
        (void)book.addFromLine(line);
        auto t1 = std::chrono::high_resolution_clock::now();
        long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        latencies.push_back(ns);
        book.onTick();
    }

    double bid, ask; int bq, aq;
    if (book.bestBidAsk(bid,bq,ask,aq)) {
        std::cout << "Final BestBid " << bid << " ("<< bq << "), BestAsk " << ask << " ("<< aq << ")\n";
        std::cout << "Spread " << book.spread() << " Mid " << book.midPrice() << "\n";
    } else {
        std::cout << "No full top-of-book at end.\n";
    }

    if (!args.latencyCsv.empty()) {
        std::ofstream out(args.latencyCsv);
        out << "ns\n";
        for (auto ns : latencies) out << ns << "\n";
    }
    return 0;
}
