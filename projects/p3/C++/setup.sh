# Single time setup script for p3.
# Usage: ./setup.sh
# Author: Viranchee Lotia

# make 'build' directory, run 'cmake ..' and 'make' inside that directory
mkdir build
cd build
cmake ..
make
cd ..

# make 'p3-test' directory, configure it with 'wolfbench/configure'
mkdir p3-test
cd p3-test
/Users/v/ece566/wolfbench/configure --enable-customtool=/Users/v/ece566/projects/p3/C++/build/p3
