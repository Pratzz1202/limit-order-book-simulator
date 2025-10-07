import argparse, os, sys
import pandas as pd
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument("--lat", default="data/latency.csv")
parser.add_argument("--out", default="data/plots/latency_hist.png")
parser.add_argument("--no-show", action="store_true")
args = parser.parse_args()

os.makedirs(os.path.dirname(args.out), exist_ok=True)

if not os.path.exists(args.lat):
    print(f"[lat_hist] latency file not found: {args.lat}", file=sys.stderr); sys.exit(1)

lat = pd.read_csv(args.lat)

plt.figure()
if len(lat):
    plt.hist(lat["ns"], bins=60)
plt.title("Per-event latency (nanoseconds)")
plt.xlabel("ns")
plt.ylabel("count")
plt.tight_layout()
plt.savefig(args.out, dpi=160)
print(f"[lat_hist] saved -> {args.out}")
if not args.no_show:
    plt.show()
