chmod +x setup.sh
chmod +x run.sh
filename='main'
# If OS is MacOS, use clang else use clang-13
if [[ "$OSTYPE" != "darwin"* ]]; then
    extension='-13'
fi
clang$extension -emit-llvm -O0 -Xclang -disable-O0-optnone -c $filename.c -o $filename.bc
llvm-dis$extension $filename.bc