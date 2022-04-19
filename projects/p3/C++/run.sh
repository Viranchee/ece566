echo "Shortly describe the changes you made to the code in 8 letters or less"
desc="normal"
# Build directory variable
# If linux, then add variable docker



# Go in build directory, and run 'make clean' and 'make'
cd build
# make clean
make


# Go to p3-test directory and run 'make clean', 'make all test', 'make test compare'
cd ../p3-test

make test EXTRA_SUFFIX=.
make EXTRA_SUFFIX=.-m2r-cse CUSTOMFLAGS="-verbose -mem2reg -cse" test
make EXTRA_SUFFIX=.-no-licm CUSTOMFLAGS=-no-licm test

make test compare
/ece566/wolfbench/timing.py