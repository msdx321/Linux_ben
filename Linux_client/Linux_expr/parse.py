import os
import sys
import numpy as np

#file = str(sys.argv[1])
s_file = int(sys.argv[1])
nb = int(sys.argv[2])
res_avg = 0
tail = 0
tot = []
length = 0
res_min = float("inf")
res_max = 0
tot_sent = 0

for c in range(nb):
    idx = s_file + c
    filename = "logs/mcb_"+str(idx)
    print filename
    with open(filename, "r") as f:
        for line in f:
            if line.startswith("RTT:"):
                temp = int(line.split()[1].strip())
                res_min = min(res_min, temp);
                res_max = max(res_max, temp);
                res_avg += temp
	        tot.append(temp)
	        length += 1
            elif line.startswith("Requests sent"):
                temp = int(line.split(None)[-1].strip())
                tot_sent += temp

tot.sort()
tail = int(length*0.99)

print "parsed file:                {} - {}".format("mcb_"+str(s_file), "mcb_"+str(s_file+nb))
print "tail latecny:               {} us".format(tot[tail])
print "avgerage latency:           {} us".format(res_avg/length)
print "MIN latency:                {} us".format(res_min)
print "MAX latency:                {} us".format(res_max)
print "number of RTTs measured:    {}".format(length)
print "number of requests dropped: {}".format(tot_sent-length)

