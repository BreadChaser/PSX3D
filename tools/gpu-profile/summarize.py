#!/usr/bin/env python3
"""Summarize cumulative 300/600 simulator runs without rounded cprof output."""
import argparse
import json
from pathlib import Path


def load(path):
    return {tuple(row[:2]):list(map(int,row[2:])) for line in path.read_text().splitlines() if (row:=line.split())}


def totals(a,b):
    old,new=load(a),load(b);tot=[0]*4
    for key,value in new.items():
        delta=[v-o for v,o in zip(value,old.get(key,[0]*len(value)))]
        if delta[0]<=0:
            continue
        tot[0]+=delta[0]*int(key[1])
        for i in range(1,4):tot[i]+=delta[i]
    per=[n/300 for n in tot]
    return dict(zip(['instructions','i_misses','read_misses','store_misses'],per),cycles=per[0]+70*per[1]+70*per[2]+4*per[3])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('sim',type=Path);p.add_argument('--after-prefix',default='final');a=p.parse_args()
    out={}
    for state in ['sh','o3auto','ctr','gt2']:
        out[state]={label:totals(a.sim/f'cs_{prefix}_{state}_300.txt',a.sim/f'cs_{prefix}_{state}_600.txt') for label,prefix in [('before','before'),('after',a.after_prefix)]}
    for label in ['correct-base','correct-batch']:
        prefix='stp_'+label
        fa=a.sim/f'cs_{prefix}_300.txt';fb=a.sim/f'cs_{prefix}_600.txt'
        if fb.exists():out['sh'][label]=totals(fa,fb)
    print(json.dumps(out,indent=2))


if __name__=='__main__':
    main()
