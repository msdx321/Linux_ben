#!/bin/bash

killall mcblaster
nodes=$1
rate=$2
core=0
total_core=32
fp=11211
dp=11211
idx=1

for n in `seq $nodes`
do
	taskset -c $((core % total_core)) ./mcblaster \
-t 1 -k 100 \
-z 135 -u $((dp)) \
-f $((fp)) -r $((rate)) \
-d 30 10.10.1.2 > logs/mcb_$((fp)) &

	fp=$((fp+1))
	dp=$((dp+1))
	core=$((core+1))
	rate=$((rate*idx))
	i=$((idx+1))
done
echo "started all client"
