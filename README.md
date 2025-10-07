# Limit Order Book & Matching Engine (C++20)

**One-liner:** A production-style limit-order-book + matcher that replays order-level feeds, generates trades & top-of-book quotes, and benchmarks per-event latency for execution research and microstructure analysis.

---

## Highlights

- **Engine**: price–time priority, maker-price rule, partial fills, **cancel/modify**, **IOC/FOK**, cached top-of-book, **integer price ticks** (no FP drift).
- **Outputs**: `trades.csv`, `quotes.csv` (only on TOB changes), `latency.csv` (ns/event), optional snapshots every *N* ticks.
- **Analytics**: Python plots (price path, spread histogram, latency percentiles) + quick report for P50/P90/P99/P99.9.
- **Performance (example)**: processed **[N]** events with **P50 [P50] µs / P99 [P99] µs** (median-based throughput ~**[X] events/sec**) on **[machine]**.
- **Hygiene**: CMake build, GitHub Actions CI, ASan/UBSan debug config, dataset converters (LOBSTER/ITCH scaffold), synthetic generator.

---

## Architecture

```mermaid
flowchart LR
  FEED[Input stream/file<br/>LIMIT/MARKET/MODIFY/CANCEL] --> PARSER[Parser & validators]
  PARSER --> MATCHER[Matching Engine<br/>price–time, IOC/FOK]
  MATCHER -->|Fills| TRADES[(trades.csv)]
  MATCHER -->|TOB changes| QUOTES[(quotes.csv)]
  MATCHER -->|Every N ticks| SNAP[snapshots/]
  MATCHER -->|Per-event ns| LAT[(latency.csv)]
