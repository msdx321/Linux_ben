import os
import sys
import numpy as np

#file = str(sys.argv[1])
s_file = 0
num = 0

def parse_file(head, number, bimodal):
    res_avg = 0
    tail = 0
    tot = []
    length = 0
    res_min = float("inf")
    res_max = 0
    tot_sent = 0
    tot_missed = 0
    tot_drop = 0
	tot_recv = 0

    for c in range(number):
        idx = head + c
        filename = "logs/mcb_"+str(idx)
        with open(filename, "r") as f:
            for line in f:
                if line.startswith("RTT:"):
                    temp = int(line.split()[1].strip())
                    res_min = min(res_min, temp);
                    res_max = max(res_max, temp);
                    res_avg += temp
                    length += 1
                    tot.append(temp)
                elif line.startswith("Requests sent"):
                    temp = int(line.split(None)[-1].strip())
                    tot_sent += temp
                elif line.startswith("Deadline missed:"):
                    temp = int(line.split(None)[-1].strip())
                    tot_missed += temp
				elif line.startswith("Measured RTTs"):
                    temp = int(line.split(None)[-1].strip())
                    tot_recv += temp

    tot.sort()
    print(length)
	assert(length > 0)
    tail = int(length*0.99)
    tot_drop = tot_sent-tot_recv

    print "[{}]parsed file:                {} - {}".format(bimodal, "mcb_"+str(s_file), "mcb_"+str(s_file+number-1))
    print "[{}]tail latecny:               {} us".format(bimodal, tot[tail])
    print "[{}]avgerage latency:           {} us".format(bimodal, res_avg/length)
    print "[{}]MIN latency:                {} us".format(bimodal, res_min)
    print "[{}]MAX latency:                {} us".format(bimodal, res_max)
    print "[{}]number of RTTs measured:    {}".format(bimodal, tot_recv)
    print "[{}]number of requests dropped: {}".format(bimodal, tot_drop)
    print "[{}]% of deadline miss:         {}%".format(bimodal, (tot_missed+tot_drop)*100/tot_sent)
    print "-----------------------------------------------------------"

if __name__ == "__main__":
    s_file = 11211
    nb = int(sys.argv[1])
    num = int(sys.argv[2])
    if nb > 0:
        print "hi flow"
        parse_file(s_file, nb, "hi")
    print "lo_flow"
    parse_file(s_file+nb, num-nb, "low")
