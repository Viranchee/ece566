#!/bin/bash
echo "File Prefix to store outputs in"
read filePrefix

# make clean
# make EXTRA_SUFFIX=.NOCSE CUSTOMFLAGS='-no-cse' test
# make EXTRA_SUFFIX=.M2RCSE CUSTOMFLAGS='-mem2reg -cse' test
file="../$filePrefix-stats.txt"
touch $file
echo "## Stats " > $file

appendStats() {
    # Print all arguments
    for i in "$@"
    do
        echo "Appending $i"
        echo "## $i" >> $file
        /ece566/wolfbench/fullstats.py $i >> $file
    done
}

appendStats NumLoops NumLoopsNoStore NumLoopsNoLoad NumLoopsNoStoreWithLoad NumLoopsWithCall LICMBasic LICMLoadHoist LICMNoPreheader LICMStoreSink Instructions Loads Stores Functions  CSEDead CSEElim CSESimplify CSELdElim CSEStore2Load CSEStElim

/ece566/wolfbench/timing.py >> $file
