import random, argparse, os

p = argparse.ArgumentParser()
p.add_argument("--n", type=int, default=200_000)
p.add_argument("--seed", type=int, default=42)
p.add_argument("--out", default="data/synth_orders.txt")
args = p.parse_args()

random.seed(args.seed)
os.makedirs(os.path.dirname(args.out), exist_ok=True)

px = 10000  # integer cents (100.00)
with open(args.out, "w") as f:
    for i in range(1, args.n+1):
        hh, mm, ss = 9, 30 + (i // 3600), (i % 60)
        ts = f"{hh:02d}:{mm:02d}:{ss:02d}"

        r = random.random()
        if i < 500:
            side = "BUY" if i % 2 else "SELL"
            pxt = px + (1 if side == "BUY" else 2)
            qty = random.randint(10, 200)
            f.write(f"{ts} LIMIT {side} {pxt/100:.2f} {qty} id={i}\n")
        else:
            if r < 0.08:
                rid = random.randint(max(1, i-2000), i-1)
                d = random.choice([-3, -2, -1, 1, 2, 3])
                qty = random.randint(10, 200)
                f.write(f"{ts} MODIFY id={rid} price={(px+d)/100:.2f} qty={qty}\n")
            elif r < 0.18:
                rid = random.randint(max(1, i-2000), i-1)
                f.write(f"{ts} CANCEL id={rid}\n")
            else:
                side = "BUY" if random.random() < 0.5 else "SELL"
                d = random.choice([-3,-2,-1,0,1,2,3])
                qty = random.randint(10, 200)
                f.write(f"{ts} LIMIT {side} {(px+d)/100:.2f} {qty} id={i}\n")
