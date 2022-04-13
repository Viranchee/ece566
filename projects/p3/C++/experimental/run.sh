if [[ "$OSTYPE" != "darwin"* ]]; then
    extension='-13'
    docker='-docker'
fi
cd ../build$docker
make
cd -

../build$docker/p3 main.bc out.bc
llvm-dis$extension out.bc
rm out.bc

# diff main.ll out.ll
# rm *.bc.stats