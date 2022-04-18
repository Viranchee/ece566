echo "Shortly describe the changes you made to the code in 8 letters or less"
desc="normal"
# Build directory variable
# If linux, then add variable docker
if [[ "$OSTYPE" != "darwin"* ]]; then
    BUILD_DIR="build-docker"
    P3_TEST_DIR="p3-test-docker"
    project="/ece566/"
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    BUILD_DIR="build"
    P3_TEST_DIR="p3-test"
    project="~/ece566/"
fi


# Go in build directory, and run 'make clean' and 'make'
cd $BUILD_DIR
make clean
make


# Go to p3-test directory and run 'make clean', 'make all test', 'make test compare'
cd ../$P3_TEST_DIR

make test EXTRA_SUFFIX=.normal
make EXTRA_SUFFIX=.normal-m2r-cse CUSTOMFLAGS="-verbose -mem2reg -cse" test
make EXTRA_SUFFIX=.normal-no-licm CUSTOMFLAGS=-no-licm test

make test compare
$project/wolfbench/timing.py