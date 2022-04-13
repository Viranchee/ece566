cd ../build
make
cd -

../build/p3 main.bc out.bc
if [[ "$OSTYPE" != "darwin"* ]]; then
    extension='-13'
fi
llvm-dis$extension out.bc
rm out.bc

diff main.ll out.ll
rm *.bc.stats