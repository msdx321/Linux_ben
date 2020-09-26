import os
import sys
import numpy as np

file = str(sys.argv[1])
rb_io = []
rb_ro = []
bm_io = []
bm_ro = []
fprr_io = []
fprr_ro = []
temp = 0
tail = 0

with open(file, "r") as f:
    for line in f:
        if line.startswith("rbtree:"):
            temp = int(line.split()[1].strip())
            rb_ro.append(temp)
            temp = int(line.split()[2].strip())
            rb_io.append(temp)
        if line.startswith("bitmap:"):
            temp = int(line.split()[1].strip())
            bm_ro.append(temp)
            temp = int(line.split()[2].strip())
            bm_io.append(temp)
        if line.startswith("fprr:"):
            temp = int(line.split()[1].strip())
            fprr_ro.append(temp)
            temp = int(line.split()[2].strip())
            fprr_io.append(temp)

tail = int(1000*0.9)

if len(rb_ro) > 0:
     print "rbtree res len:{};{}".format(len(rb_ro), len(rb_io))
     print "rbtree remove mean:{}".format(np.mean(rb_ro))
     print "rbtree insert mean:{}".format(np.mean(rb_io))
     print "rbtree remove std:{}".format(np.std(rb_ro, ddof=1))
     print "rbtree insert std:{}".format(np.std(rb_io, ddof=1))
     rb_ro.sort()
     rb_io.sort()
     print "rbtree 90% remove overhead:{}".format(rb_ro[tail])
     print "rbtree 90% insert overhead:{}".format(rb_io[tail])
     print "-----------------------------------"

if len(bm_ro) > 0:
     print "bitmap res len:{};{}".format(len(bm_ro), len(bm_io))
     print "bitmap remove mean:{}".format(np.mean(bm_ro))
     print "bitmap insert mean:{}".format(np.mean(bm_io))
     print "bitmap remove std:{}".format(np.std(bm_ro, ddof=1))
     print "bitmap insert std:{}".format(np.std(bm_io, ddof=1))
     bm_ro.sort()
     bm_io.sort()
     print "bitmap 90% remove overhead:{}".format(bm_ro[tail])
     print "bitmap 90% insert overhead:{}".format(bm_io[tail])
     print "-----------------------------------"

if len(fprr_ro) > 0:
     print "fprr res len:{};{}".format(len(fprr_ro), len(fprr_io))
     print "fprr remove mean:{}".format(np.mean(fprr_ro))
     print "fprr insert mean:{}".format(np.mean(fprr_io))
     print "fprr remove std:{}".format(np.std(fprr_ro, ddof=1))
     print "fprr insert std:{}".format(np.std(fprr_io, ddof=1))
     fprr_ro.sort()
     fprr_io.sort()
     print "fprr 90% remove overhead:{}".format(fprr_ro[tail])
     print "fprr 90% insert overhead:{}".format(fprr_io[tail])
     print "-----------------------------------"

