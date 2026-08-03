#!/usr/bin/env bash
# Full statistical sweep. Writes results_stat.csv.
# Reproduces the four experiments in the paper (5 placements each).
OUT=results_stat.csv
echo "pattern,paradigm,N,oversub,spineRate,Trecfg_us,mean_CCT_s,std_CCT_s,nseeds" > $OUT
SEEDS=5
run() {  # pattern paradigm N oversub spineRate Trecfg
  line=$(bash compute_cct_stat.sh $1 $2 $3 $4 $6 1.0 $5 $SEEDS 2>/dev/null)
  m=$(echo "$line" | sed -n 's/.*mean_CCT_s=\([0-9.eE+-]*\).*/\1/p')
  sd=$(echo "$line" | sed -n 's/.*std=\([0-9.eE+-]*\).*/\1/p')
  echo "$1,$2,$3,$4,$5,$6,$m,$sd,$SEEDS" >> $OUT
  echo "  $1 $2 N=$3 spine=$5 Trecfg=$6 -> mean=$m std=$sd"
}
echo "=== 1) Size sweep (spine=25) ==="
for N in 8 16 24 32 48 64; do run all_to_all eps $N 4 25 200; run all_to_all ocs $N 4 25 200; done
echo "=== 2) Oversubscription sweep (N=32) ==="
for SP in 100 50 25 12; do run all_to_all eps 32 4 $SP 200; run all_to_all ocs 32 4 $SP 200; done
echo "=== 3) Ring All-Reduce (spine=25) ==="
for N in 8 16 32 64; do run ring_allreduce eps $N 4 25 200; run ring_allreduce ocs $N 4 25 200; done
echo "=== 4) Reconfiguration-latency sweep (N=32) ==="
for TR in 50 100 200 500 1000 2000; do run all_to_all ocs 32 4 25 $TR; done
run all_to_all eps 32 4 25 200
echo "DONE -> $OUT"
