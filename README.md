# Practical Artifacts: OCS vs EPS for Collective AI/ML Traffic

This folder contains the ns-3 simulation code, the driver scripts, the analytical
radix model, the raw statistical results, and the figures used in the paper.

## Contents
- `ocs-vs-eps.cc`        ns-3 program (measures one collective phase per run)
- `compute_cct_stat.sh`  driver: sums phase times into a CCT, over 5 placements
- `full_sweep_stat.sh`   runs all four experiments, writes results_stat.csv
- `radix_model.py`       analytical finite-radix model on top of measured throughput
- `results_statistical.csv`  raw mean/std results (the numbers in Table 2)
- `results_radix.csv`    radix extrapolation results (Figure 6)
- `fig*.png`             the six figures

## Environment
- ns-3 release 3.45, built with an optimized profile.
- A recent g++ (C++17). Python 3 for the driver arithmetic.
- Note: ns-3 3.45 needs Python <= 3.12 for its build tooling; if your system
  Python is newer, use pyenv to select 3.12 before building.

## Build
Copy `ocs-vs-eps.cc` into the ns-3 `scratch/` folder, then:
```
./ns3 build ocs-vs-eps
```
Run the executable directly so stdout is captured (do NOT use ./ns3 run):
```
./build/scratch/ns3.45-ocs-vs-eps-optimized --help
```

## Reproduce the paper results
```
# one operating point (N=32, spine 25 Gbps), 5 placements:
bash compute_cct_stat.sh all_to_all eps 32 4 200 1.0 25 5
bash compute_cct_stat.sh all_to_all ocs 32 4 200 1.0 25 5

# full sweep (all four experiments) -> results_stat.csv (takes a while):
bash full_sweep_stat.sh

# finite-radix extrapolation (Figure 6) -> results_radix.csv:
python3 radix_model.py
```

## Key parameters (see ocs-vs-eps.cc for all)
- `--pattern`   all_to_all | ring_allreduce
- `--paradigm`  eps | ocs
- `--N`         number of accelerators
- `--spineRateGbps`  spine link rate (sets oversubscription)
- `--TrecfgUs`  reconfiguration latency (microseconds)
- `--ocsRadix`  optical switch radix (0 = ideal crossbar)
- `--seed`      placement seed (statistical repetition)
- `--phaseId`   phase to measure; -1 prints the phase count

## Method summary
Each phase runs a short UDP measurement window at the host link rate. We read the
throughput sustained under contention, then compute phase_time = chunk / throughput
and sum over phases, adding reconfiguration cost. The full 1 GB transfer is not
simulated to completion; the completion time is derived from measured throughput.
