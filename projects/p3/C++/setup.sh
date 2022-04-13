# Single time setup script for p3.
# Usage: ./setup.sh
# Author: Viranchee Lotia
if [[ "$OSTYPE" != "darwin"* ]]; then
    docker="-docker"
    prefixPath=""
fi

# If else statement for linux vs mac
if [[ "$OSTYPE" == "darwin"* ]]; then
    docker=""
    prefixPath="~"
fi

project=$prefixPath/ece566/

# make 'build' directory, run 'cmake ..' and 'make' inside that directory
mkdir build$docker
cd build$docker
cmake ..
make
cd ..

## make 'p3-test' directory, configure it with 'wolfbench/configure'
chmod +x $project/wolfbench/configure
chmod +x $project/wolfbench/*.py
chmod +x $project/wolfbench/*.sh
chmod +x $project/wolfbench/*.sh
chmod +x *.sh

# If linux, then add variable docker

mkdir p3-test$docker
cd p3-test$docker
$project/wolfbench/configure --enable-customtool=$project/projects/p3/C++/build/p3
