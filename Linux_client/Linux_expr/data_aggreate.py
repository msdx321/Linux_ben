import os
import sys

durtime = 100
lat_max = 10000
log_dir = sys.argv[1]
log_files = os.listdir(log_dir)
rlist = list()
wlist = list()

class stats(object):
    def __init__(self, request_type=None, throughput=None, avg=None, ld={}):
        # print len(ld)
        self.request_type = request_type
        self.throughput = throughput
        self.avg = avg
        self.nsent = 0
        self.timeout = 0
        self.err = 0
        self.ignore = 0
        self.invad = 0
        self.lat_distribution = ld


    def pretty_print(self):
        print "request type : {}".format(self.request_type)
        print "Requests sent: {}".format(self.nsent)
        print "throughput   : {} requests per second".format(self.throughput)
        print "average      : {} us".format(self.avg)
        print "Timeouts     : {}".format(self.timeout)
        print "Errors       : {} us".format(self.err)
        print "Invalid      : {} us".format(self.invad)
        print "Ignored      : {} us".format(self.ignore)
        for k in sorted(self.lat_distribution):
            print k, ':', self.lat_distribution[k]



def parse_stats(fn):
    read_stats = None
    write_stats = None
    curr_stats = None
    with open(fn, "r") as f:

        for line in f:
            if line.startswith("Request type"):
                request_type = line.split(None)[-1].strip()
                if request_type == "get":
                    curr_stats = read_stats = stats(request_type, 0, 0, {})
                else:
                    curr_stats = write_stats = stats(request_type)
            elif line.startswith("Requests sent"):
                curr_stats.nsent = int(line.split(None)[-1].strip())
            elif line.startswith("Timeouts"):
                curr_stats.timeout = int(line.split(None)[-1].strip())
            elif line.startswith("Errors"):
                curr_stats.err = int(line.split(None)[-1].strip())
            elif line.startswith("Invalid"):
                curr_stats.invad = int(line.split(None)[-1].strip())
            elif line.startswith("Ignored"):
                curr_stats.ignore = int(line.split(None)[-1].strip())
            elif line.startswith("Measured RTTs"):
                curr_stats.throughput = int(line.split(None)[-1].strip())
            elif line.startswith("RTT min"):
                curr_stats.avg = int(line.split('/')[3].strip())
                
            elif line.startswith("RTT distribution for 'get'"):
                curr_stats = read_stats
            elif line.startswith("RTT distribution for 'set'"):
                curr_stats = write_stats
                
            elif line.startswith("["):
                interval_start = int(line.split("-", 1)[0].strip()[1:])
                height = int(line.split(None)[-1].strip())
                curr_stats.lat_distribution[interval_start] = height
            elif line.startswith("Over"):
                curr_stats.lat_distribution[lat_max] = int(line.split(None)[-1].strip())

            elif line.startswith("Rate per "):
                pass
            elif line.startswith("-nan p"):
                pass
            elif not line.strip():
                pass
            else:
                print 'result parse error!!', line
    return (read_stats, write_stats)


def aggreate(sl, rtype):
    sllen = len(sl)
    print sllen, rtype
    total_stats = stats(rtype, throughput=0, avg=0, ld={})

    for sat in sl:
        total_stats.nsent += sat.nsent
        total_stats.timeout += sat.timeout
        total_stats.err += sat.err
        total_stats.invad += sat.invad
        total_stats.ignore += sat.ignore
        total_stats.throughput += sat.throughput
        total_stats.avg += sat.avg
        for key in sat.lat_distribution:
            if key in total_stats.lat_distribution:
                total_stats.lat_distribution[key] += sat.lat_distribution[key]
            else:
                total_stats.lat_distribution[key] = sat.lat_distribution[key]
            # print key, sat.lat_distribution[key]


    total_stats.throughput /= durtime
    total_stats.avg /= sllen
    total_stats.pretty_print()
    

for f in log_files:
    fn = log_dir + f;
    rs, ws = parse_stats(fn)
    # print fn
    # if rs != None: rs.pretty_print()
    if rs != None: rlist.append(rs)
    if ws != None: wlist.append(ws)
aggreate(rlist, "get")

