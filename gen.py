#!/usr/bin/env python3

Mb = 1024 * 1024

modes = {"intrasocket": "-i", "intersocket": "-I"}
temps = {"reuse": "-T", "noreuse": "-t"}
progs = ["cma", "shmem"]
sizes = {"1MB": 1 * Mb, "25MB": 25 * Mb, "200MB": 200 * Mb}

print("#!/bin/bash")
print("mkdir results")
print("make") 

for t in temps.keys():
    for p in progs:
        for m in modes.keys():
            for s in sizes.keys():
                reps = 100

                # increase reps to reduce noise
                reps = 100000 if sizes[s] < sizes["25MB"] and t != "noreuse" else reps

                # these scenarios won't fit in memory otherwise
                reps = 10 if sizes[s] > sizes["25MB"] else reps
                print("echo '{} {} {} {} ({})'".format(p, m, t, s, reps))
                print("./{} {} {} -s {} -r {} > ./results/{}-{}-{}-{}.log 2>&1".format(p, modes[m], temps[t], sizes[s], reps, p, m, t, s));


for s in sizes.keys():
    print("echo './cuda {}'".format(s))
    print("./cuda {} > ./results/cuda-{}.log 2>&1".format(sizes[s], s))

print("echo 'Caching perf'")
print("./cache > ./results/caches.log 2>&1")

