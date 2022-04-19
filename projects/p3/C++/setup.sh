# Single time setup script for p3.
# Usage: ./setup.sh
# Author: Viranchee Lotia

project=/ece566

# make 'build' directory, run 'cmake ..' and 'make' inside that directory
mkdir build
cd build
cmake ..
make
cd ..

## make 'p3-test' directory, configure it with 'wolfbench/configure'
chmod +x /ece566/wolfbench/configure
chmod +x /ece566/wolfbench/*.py
chmod +x /ece566/wolfbench/*.sh
chmod +x /ece566/wolfbench/*.sh
chmod +x *.sh

# If linux, then add variable docker

mkdir p3-test
cd p3-test
/ece566/wolfbench/configure --enable-customtool=/ece566/projects/p3/C++/build/p3
