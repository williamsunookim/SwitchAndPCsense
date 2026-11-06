#!/usr/bin/env python3
"""
Tune how Switch 2 HD Rumble 2 is rendered on the DualSense, live.

The console's 10-bit frequency fields have no public Hz mapping, so the Pico
can interpret them three ways (RAM only; a reboot restores fixed/160):

  fixed   amplitude only; bands at 160 / 320 Hz (default)
  linear  code == Hz
  log     10 * 2^(code / 128) Hz (Switch 1 log scale at 10-bit resolution)

--full-scale sets the 10-bit amplitude that maps to full DualSense strength
(lower = stronger; default 160).

Requires: pip install hidapi
Run:      python switch2_rumble.py log --full-scale 160
"""
import argparse
import sys

try:
    import hid
except ImportError:
    sys.exit("Missing dependency. Install with:  pip install hidapi")

VID = 0x054C
PIDS = (0x0CE6, 0x0DF2)
REPORT_ID = 0xF6
FUNCID = 0x36
DATA_LEN = 63
MODES = {"fixed": 0, "linear": 1, "log": 2}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=MODES)
    ap.add_argument("--full-scale", type=int, default=0,
                    help="10-bit amplitude for full strength, 16..1023 (0 = keep current)")
    args = ap.parse_args()
    if args.full_scale and not 16 <= args.full_scale <= 1023:
        ap.error("--full-scale must be 16..1023")

    cand = [d for d in hid.enumerate(VID) if d["product_id"] in PIDS]
    if not cand:
        sys.exit("Pico not found (VID 054C, PID 0CE6/0DF2).")
    dev = hid.device()
    dev.open_path(cand[0]["path"])
    try:
        body = [FUNCID, MODES[args.mode], args.full_scale & 0xFF, args.full_scale >> 8]
        dev.send_feature_report(bytes([REPORT_ID] + body + [0] * (DATA_LEN - len(body))))
    finally:
        dev.close()
    scale = f", full scale {args.full_scale}" if args.full_scale else ""
    print(f"Switch 2 rumble: {args.mode}{scale}")


if __name__ == "__main__":
    main()
