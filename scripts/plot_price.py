import argparse, os, sys
import pandas as pd
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument("--quotes", default="data/quotes.csv")
parser.add_argument("--trades", default="data/trades.csv")
parser.add_argument("--out",    default="data/plots/price_over_time.png")
parser.add_argument("--no-show", action="store_true")
args = parser.parse_args()

os.makedirs(os.path.dirname(args.out), exist_ok=True)

if not os.path.exists(args.quotes):
    print(f"[plot_price] quotes not found: {args.quotes}", file=sys.stderr); sys.exit(1)
if not os.path.exists(args.trades):
    print(f"[plot_price] trades not found: {args.trades}", file=sys.stderr); sys.exit(1)

q = pd.read_csv(args.quotes)
t = pd.read_csv(args.trades)

print(f"[plot_price] quotes={len(q)} rows, trades={len(t)} rows, non-NaN mids={(~q.get('mid', pd.Series([])).isna()).sum() if 'mid' in q else 0}")

q["i"] = range(len(q))
t["i"] = range(len(t))

plt.figure()
if "best_bid" in q.columns and len(q):
    plt.plot(q["i"], q["best_bid"], label="best_bid", linewidth=1)
if "best_ask" in q.columns and len(q):
    plt.plot(q["i"], q["best_ask"], label="best_ask", linewidth=1)
if "mid" in q.columns and (~q["mid"].isna()).any():
    q_mid = q.dropna(subset=["mid"])
    plt.plot(q_mid["i"], q_mid["mid"], label="mid", linewidth=1.5)
if "price" in t.columns and len(t):
    plt.plot(t["i"], t["price"], ".", label="trades", markersize=4)

plt.title("Price over time")
plt.xlabel("event index")
plt.ylabel("price")
plt.legend()
plt.tight_layout()
os.makedirs(os.path.dirname(args.out), exist_ok=True)
plt.savefig(args.out, dpi=160)
print(f"[plot_price] saved -> {args.out}")
if not args.no_show:
    plt.show()
