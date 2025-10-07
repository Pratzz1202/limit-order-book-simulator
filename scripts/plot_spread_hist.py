import argparse, os, sys
import pandas as pd
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument("--quotes", default="data/quotes.csv")
parser.add_argument("--out",    default="data/plots/spread_hist.png")
parser.add_argument("--no-show", action="store_true")
args = parser.parse_args()

os.makedirs(os.path.dirname(args.out), exist_ok=True)

if not os.path.exists(args.quotes):
    print(f"[plot_spread] quotes not found: {args.quotes}", file=sys.stderr); sys.exit(1)

q = pd.read_csv(args.quotes)
q = q[pd.notna(q.get("spread"))]

plt.figure()
if len(q):
    plt.hist(q["spread"], bins=50)
plt.title("Spread histogram")
plt.xlabel("spread")
plt.ylabel("count")
plt.tight_layout()
plt.savefig(args.out, dpi=160)
print(f"[plot_spread] saved -> {args.out}")
if not args.no_show:
    plt.show()
