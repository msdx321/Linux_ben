#!/bin/bash

tot=$1
num=$2
len=$3
s=$((tot-num))
e=$((tot-1))

echo "taskset -c $s-$e ./server $len"
taskset -c $s-$e ./server --clen $len
#taskset -c $s-$e ./server
