import argparse
import datetime
import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
from pathlib import Path

CASES = ["valid_i", "valid_u", "valid_l", "valid_ul", "valid_ll", "valid_ull", "ng_bencode", "invalid_i", "invalid_u"]
TARGET_NS = 200_000_000
PAIRS = 15
VARIANTS = ["baseline", "patched", "optimized"]

def run(variant, case, iterations):
    result = subprocess.run([f"bench/{variant}", case, str(iterations)], check=True, capture_output=True, text=True)
    return json.loads(result.stdout)

def smoke():
    for variant in VARIANTS:
        subprocess.run([f"bench/{variant}", "--verify"], check=True)
        for args in [["unknown", "16"], ["valid_i", "0"]]:
            result = subprocess.run([f"bench/{variant}", *args], capture_output=True)
            assert result.returncode == 2, (variant, args, result)
    for case in CASES:
        samples = [run(v, case, 160) for v in VARIANTS]
        if not case.startswith("invalid"):
            assert len({x["checksum"] for x in samples}) == 1, case
        else:
            assert samples[1]["checksum"] == samples[2]["checksum"], case
    print("PASS: harness checks both variants, valid checksums, and invalid arguments", flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--output", default="results.json")
    args = parser.parse_args()
    smoke()
    if args.smoke:
        return
    affinity = sorted(os.sched_getaffinity(0))
    os.sched_setaffinity(0, {affinity[0]})
    metadata = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "machine": platform.machine(), "kernel": platform.release(),
        "compiler": subprocess.check_output(["gcc", "--version"], text=True).splitlines()[0],
        "lscpu": json.loads(subprocess.check_output(["lscpu", "-J"], text=True)),
        "cpu_affinity": [affinity[0]], "available_cpus": affinity,
        "source_sha": subprocess.check_output(["git", "-C", "baseline", "rev-parse", "HEAD"], text=True).strip(),
        "patch_sha256": hashlib.sha256(Path("bench/integer-fix.patch").read_bytes()).hexdigest(),
        "optimized_patch_sha256": hashlib.sha256(Path("bench/integer-optimized.patch").read_bytes()).hexdigest(),
        "pairs": PAIRS, "target_sample_ns": TARGET_NS,
        "flags": "-O3 -flto=auto -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2 -pthread -std=c11",
    }
    expected = os.getenv("EXPECTED_ARCH")
    if expected:
        assert metadata["machine"] == expected, (metadata["machine"], expected)
    iterations = {}
    for case in CASES:
        pilots = [run(v, case, 100_000) for v in VARIANTS]
        ns = max(p["ns_per_op"] for p in pilots)
        iterations[case] = max(16, int(TARGET_NS / ns / 16) * 16)
    raw = []
    rng = random.Random(20261112)
    for pair in range(PAIRS):
        cases = CASES.copy()
        rng.shuffle(cases)
        for case in cases:
            offset = pair % 3
            variants = VARIANTS[offset:] + VARIANTS[:offset]
            if (pair // 3) % 2:
                variants.reverse()
            samples = {v: run(v, case, iterations[case]) for v in variants}
            if not case.startswith("invalid"):
                assert len({x["checksum"] for x in samples.values()}) == 1, case
            else:
                assert samples["patched"]["checksum"] == samples["optimized"]["checksum"], case
            raw.append({"pair": pair, "case": case, "order": variants, **samples})
        print(f"Completed pair {pair + 1}/{PAIRS}", flush=True)
    summary = []
    for case in CASES:
        rows = [r for r in raw if r["case"] == case]
        medians = {v: statistics.median(r[v]["ns_per_op"] for r in rows) for v in VARIANTS}
        comparisons = {}
        for old, new in [("baseline", "patched"), ("baseline", "optimized"), ("patched", "optimized")]:
            before = [r[old]["ns_per_op"] for r in rows]
            after = [r[new]["ns_per_op"] for r in rows]
            changes = [(b / a - 1) * 100 for a, b in zip(before, after)]
            deltas = [b - a for a, b in zip(before, after)]
            boot = sorted(statistics.median(rng.choices(changes, k=len(changes))) for _ in range(10_000))
            comparisons[f"{new}_vs_{old}"] = {
                "paired_change_pct": statistics.median(changes),
                "paired_delta_ns": statistics.median(deltas),
                "paired_change_bootstrap_95_pct": [boot[250], boot[9749]],
            }
        row = {"case": case, "median_ns": medians, "comparisons": comparisons,
            "iterations_per_sample": iterations[case]}
        summary.append(row)
        print(json.dumps(row), flush=True)
    Path(args.output).write_text(json.dumps({"metadata": metadata, "summary": summary, "raw": raw}, indent=2))

if __name__ == "__main__":
    main()
