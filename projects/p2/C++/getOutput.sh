#!/bin/bash

echo "Enter File Prefix: "
read prefix

make clean
make EXTRA_SUFFIX=.NOCSE CUSTOMFLAGS='-no-cse' test
make EXTRA_SUFFIX=.M2RCSE CUSTOMFLAGS='-mem2reg' test

echo "## Stats " > $prefix-Stats.txt
echo "# Instructions" > $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py Instructions >> $prefix-Stats.txt
echo "# Loads" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py Loads >> $prefix-Stats.txt
echo "# Stores" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py Stores >> $prefix-Stats.txt
echo "# Functions" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py Functions >> $prefix-Stats.txt
echo "# CSEDead" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py CSEDead >> $prefix-Stats.txt
echo "# CSEElim" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py CSEElim >> $prefix-Stats.txt
echo "# CSESimplify" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py CSESimplify >> $prefix-Stats.txt
echo "# CSELdElim" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py CSELdElim >> $prefix-Stats.txt
echo "# CSEStore2Load" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py CSEStore2Load >> $prefix-Stats.txt
echo "# CSEStElim" >> $prefix-Stats.txt
~/ece566/wolfbench/fullstats.py CSEStElim >> $prefix-Stats.txt
echo "# Timing" >> $prefix-Stats.txt
~/ece566/wolfbench/timing.py >> $prefix-Stats.txt
