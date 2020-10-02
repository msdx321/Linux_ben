#!/bin/bash

tot=$1
num=$2
s=$((tot-num))
e=$((tot-1))

echo "taskset -c $s-$e ./server"
taskset -c $s-$e ./server
