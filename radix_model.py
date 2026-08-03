"""
Analytical finite-radix model calibrated on the measured ns-3 throughput.

Rather than recomputing from scratch, we take the OCS throughput measured in the
ideal-crossbar ns-3 runs (which represent an effectively high-radix switch) and
apply a radix-limitation factor on top. This stays fully consistent with the
ns-3 results.

The measured OCS time represents the high-radix case (the switch can hold all
the circuits a phase needs). For a finite radix R, any phase whose number of
concurrent circuits exceeds R suffers a throughput drop. Contention model: the
data-carrying part of the phase time is inflated by a factor max(1, N_conc / R).
The reconfiguration cost is not affected by the radix.
"""
import pandas as pd, numpy as np

df = pd.read_csv('results_stat.csv')

def get(pat, par, **kw):
    d = df[(df.pattern == pat) & (df.paradigm == par)]
    for k, v in kw.items():
        d = d[d[k] == v]
    return d

Trecfg = 200e-6

def apply_radix(ocs_measured_cct, N, pattern, R, Trecfg_us=200):
    """Apply a finite-radix limit on top of the ns-3-measured OCS time."""
    if pattern == 'all_to_all':
        nphases = N - 1 if N % 2 == 0 else N
        nconc = N
        nrecfg = nphases
    else:
        nphases = 2 * (N - 1)
        nconc = N
        nrecfg = 1
    recfg_total = nrecfg * (Trecfg_us * 1e-6)
    data_time = ocs_measured_cct - recfg_total          # data-carrying part
    if data_time < 0:
        data_time = ocs_measured_cct
        recfg_total = 0
    # inflation factor due to the limited radix
    infl = max(1.0, nconc / R) if R > 0 else 1.0
    return data_time * infl + recfg_total

# Table: All-to-All across sizes, per radix, speedup vs EPS
print("=== Realistic OCS speedup (EPS/OCS) across size and radix, spine=25 ===")
print(f"{'N':>4} {'EPS_s':>8} {'ideal':>7} {'R=32':>7} {'R=16':>7} {'R=8':>7}")
for N in [16, 24, 32, 48, 64]:
    eps = get('all_to_all', 'eps', N=N, spineRate=25, Trecfg_us=200).mean_CCT_s.iloc[0]
    ocs_meas = get('all_to_all', 'ocs', N=N, spineRate=25, Trecfg_us=200).mean_CCT_s.iloc[0]
    row = f"{N:>4} {eps:>8.3f} {eps/ocs_meas:>6.2f}x"
    for R in [32, 16, 8]:
        oc = apply_radix(ocs_meas, N, 'all_to_all', R)
        row += f" {eps/oc:>6.2f}x"
    print(row)

print("\n=== Summary at N=32 (reference point) ===")
eps32 = get('all_to_all', 'eps', N=32, spineRate=25, Trecfg_us=200).mean_CCT_s.iloc[0]
ocs32 = get('all_to_all', 'ocs', N=32, spineRate=25, Trecfg_us=200).mean_CCT_s.iloc[0]
print(f"EPS={eps32:.3f}s, OCS ideal(ns-3)={ocs32:.3f}s (speedup {eps32/ocs32:.2f}x)")
for R in [32, 16, 8]:
    oc = apply_radix(ocs32, 32, 'all_to_all', R)
    print(f"  radix={R}: OCS={oc:.3f}s, speedup={eps32/oc:.2f}x")

# Save results for the table / figure
rows = []
for N in [16, 24, 32, 48, 64]:
    eps = get('all_to_all', 'eps', N=N, spineRate=25, Trecfg_us=200).mean_CCT_s.iloc[0]
    ocs_meas = get('all_to_all', 'ocs', N=N, spineRate=25, Trecfg_us=200).mean_CCT_s.iloc[0]
    for R, lbl in [(0, 'ideal'), (32, '32'), (16, '16'), (8, '8')]:
        oc = apply_radix(ocs_meas, N, 'all_to_all', R)
        rows.append({'N': N, 'radix': lbl, 'OCS_s': round(oc, 4),
                     'EPS_s': round(eps, 4), 'speedup': round(eps / oc, 3)})
pd.DataFrame(rows).to_csv('radix_results.csv', index=False)
print("\nSaved radix_results.csv")
