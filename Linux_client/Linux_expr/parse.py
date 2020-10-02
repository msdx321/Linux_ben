import os
import sys
import numpy as np

#file = str(sys.argv[1])
s_file = 0
nb = 0
num = 0

def parse_file(head, number):
    res_avg = 0
    tail = 0
    tot = []
    length = 0
    res_min = float("inf")
    res_max = 0
    tot_sent = 0

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
	            tot.append(temp)
	            length += 1
                elif line.startswith("Requests sent"):
                    temp = int(line.split(None)[-1].strip())
                    tot_sent += temp

    tot.sort()
    tail = int(length*0.99)

    print "[hi]parsed file:                {} - {}".format("mcb_"+str(s_file), "mcb_"+str(s_file+nb))
    print "[hi]tail latecny:               {} us".format(tot[tail])
    print "[hi]avgerage latency:           {} us".format(res_avg/length)
    print "[hi]MIN latency:                {} us".format(res_min)
    print "[hi]MAX latency:                {} us".format(res_max)
    print "[hi]number of RTTs measured:    {}".format(length)
    print "[hi]number of requests dropped: {}".format(tot_sent-length)
    print "-----------------------------------------------------------"

if __name__ == "__main__":
    s_file = 11211
    nb = int(sys.argv[1])
    num = int(sys.argv[2])
    print "hi flow"
    parse_file(s_file, nb)
    print "lo_flow"
    parse_file(s_file+nb, num-nb)
