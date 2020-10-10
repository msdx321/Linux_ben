import subprocess
import argparse

nserver = 0
cur_core = 0
ncores = 1
filelen = 0

class SSL(object):
    def __init__(self, server_port, core):
        self.server_port = server_port

        self.args = ["taskset"]
        self.args.extend(["-c", str(core)])
        self.args.extend(["./server"])
        self.args.extend(["--server_port", str(server_port)])
        print self.args
        self.p = subprocess.Popen(self.args)

    def stop(self):
        self.p.terminate()

def increment_core_num(core):
    global cur_core
    p = core + cur_core
    cur_core = (cur_core + 1) % ncores
    return p

def start_server(start_port, nb_servers):
    ws_list = []

    for _ in range(nb_servers):
        nc = increment_core_num(0)
        ws_list.append(SSL(start_port, nc))
        start_port += 1
    return ws_list

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-s", help="https port number", type=int)
    parser.add_argument("-c", help="number of cores", type=int)
    parser.add_argument("--nb_servers", help="number of instances to create", type=int)
    return parser.parse_args()

if __name__ == '__main__':
    args = parse_args()
    start_port = 11211
    nb_servers = args.nb_servers if args.nb_servers else 1

    ws_list = start_server(start_port, nb_servers)
