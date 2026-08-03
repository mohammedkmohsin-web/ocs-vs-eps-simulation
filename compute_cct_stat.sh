#!/usr/bin/env bash
# Compute the collective completion time (CCT) with statistical repetition.
# Runs SEEDS placements, prints mean and standard deviation.
# Usage: compute_cct_stat.sh PATTERN PARADIGM N OVERSUB TRECFG GB SPINE SEEDS
EXE=./build/scratch/ns3.45-ocs-vs-eps-optimized
PATTERN=$1; PARADIGM=$2; N=$3; OVERSUB=$4; TRECFG=$5; GB=${6:-1.0}; SPINE=${7:-25}; SEEDS=${8:-5}

info=$($EXE --pattern=$PATTERN --N=$N --phaseId=-1)
NPHASES=$(echo $info | sed -n 's/.*NPHASES=\([0-9]*\).*/\1/p')
RECFG=$(echo $info | sed -n 's/.*RECONFIG_EVERY=\([0-9]*\).*/\1/p')
chunkBits=$(python3 -c "print(($GB*1e9/$N)*8)")

vals=()
for ((sd=1; sd<=SEEDS; sd++)); do
  cct=0.0
  for ((p=0; p<NPHASES; p++)); do
    out=$($EXE --pattern=$PATTERN --paradigm=$PARADIGM --N=$N --oversub=$OVERSUB \
          --totalPerNodeGB=$GB --spineRateGbps=$SPINE --phaseId=$p --seed=$sd)
    thr=$(echo $out | sed -n 's/.*PHASE_THR_BPS=\([0-9]*\).*/\1/p')
    cct=$(python3 -c "
thr=$thr; cb=$chunkBits; cct=$cct
pt=cb/thr if thr>0 else 1e9
recfg=0.0
if '$PARADIGM'=='ocs' and ($RECFG==1 or $p==0): recfg=$TRECFG*1e-6
print(cct+pt+recfg)")
  done
  vals+=($cct)
done
python3 -c "
import statistics as st
v=[float(x) for x in '${vals[*]}'.split()]
m=st.mean(v); sd=st.pstdev(v)
print(f'pattern=$PATTERN paradigm=$PARADIGM N=$N spine=$SPINE Trecfg=$TRECFG mean_CCT_s={m:.5f} std={sd:.5f} n={len(v)}')
"
