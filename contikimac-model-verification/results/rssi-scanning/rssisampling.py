#!/usr/bin/python

import random
#import math
#import matplotlib.pyplot as plt
import sys

# CCA threshold.
thr = -77

f = open('rssi-log.txt', 'r')
fw = open('rssi-binary.txt', 'a')

for line in f:
    l = line.split(':')

    if len(l) != 2:
        continue

    try:
        x =  int(round(float(l[1].strip())/1))
        if int(l[0].strip()) < thr:
            for i in range (0,x):
                #print "write 0"
                fw.write("0\n")
        else:
           for i in range (0,x):
               #print "write 1"
               fw.write("1\n")
    except ValueError:
        print "ValueError"
        continue;

print "Processing completed."
fw.close()
