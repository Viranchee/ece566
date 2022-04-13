echo "Shortly describe the changes you made to the code in 8 letters or less"
read desc
# Build directory variable
BUILD_DIR=build
P3_TEST_DIR=p3-test
# Go in build directory, and run 'make clean' and 'make'
cd $BUILD_DIR
make clean
make

# Go to p3-test directory and run 'make clean', 'make all test', 'make test compare'
cd ../$P3_TEST_DIR

make EXTRA_SUFFIX=.$desc-no-licm CUSTOMFLAGS=-no-licm test
make test EXTRA_SUFFIX=.$desc
make EXTRA_SUFFIX=.$desc-m2r-cse CUSTOMFLAGS="-verbose -mem2reg -cse" test

make test compare