# Build directory variable
BUILD_DIR=m1
P3_TEST_DIR=p3-test
# Go in build directory, and run 'make clean' and 'make'
cd $BUILD_DIR
make clean
make

# Go to p3-test directory and run 'make clean', 'make all test', 'make test compare'
cd ../$P3_TEST_DIR
make clean
make all test
make test compare