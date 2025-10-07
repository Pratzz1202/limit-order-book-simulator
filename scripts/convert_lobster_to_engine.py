# Minimal converter: LOBSTER 'Message' CSV -> your engine's human format
# Expected columns: time, type, order_id, size, price, direction
# Notes: This is a starter; LOBSTER semantics are richer (exec/cancel/delete nuances).

import csv, argparse, os

p = argparse.ArgumentParser()
p.add_argument("--messages", required=True, help="LOBSTER Message.csv")
p.add_argument("--out", default="data/lobster_converted.txt")
args = p.parse_args()

os.makedirs(os.path.dirname(args.out), exist_ok=True)

def ts_from_seconds(sec):
    # crude placeholder: convert to HH:MM:SS in a session window
    s = float(sec)
    h = 9 + int(s // 3600)
    m = 30 + int((s % 3600) // 60)
    s = int(s % 60)
    return f"{h:02d}:{m:02d}:{s:02d}"

with open(args.messages) as f, open(args.out, "w") as out:
    r = csv.reader(f)
    for row in r:
        t, typ, oid, qty, px, side = row[0], int(row[1]), int(row[2]), int(row[3]), float(row[4]), row[5]
        ts = ts_from_seconds(t)
        if typ in (1, 7):  # add / replace as add
            s = "BUY" if side in ("B","1") else "SELL"
            out.write(f"{ts} LIMIT {s} {px:.2f} {qty} id={oid}\n")
        elif typ in (3, 4):  # cancel/delete
            out.write(f"{ts} CANCEL id={oid}\n")
        elif typ == 5:       # modify reduce (approximate as new qty needed)
            # To use properly, you'd need the new remaining size; treat as cancel or ignore here
            pass
        else:
            # executions are implied by crossing orders; engine will generate trades
            pass
