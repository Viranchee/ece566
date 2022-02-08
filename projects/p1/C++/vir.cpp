#include <iostream>
#include <list>
#include <vector>
#include <map>
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>


#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace std;
using namespace llvm;

void lol() {
    cout << "lol" << endl;

};

struct BitField {
    bool value[32];

    void printLol() {
        cout << "LOL" << endl;
    }
};
void printBitfields (BitField bf) {
    cout << "BitField: " << bf.value[0] << bf.value[1] << bf.value[2] << bf.value[3] << bf.value[4] << bf.value[5] << bf.value[6] << bf.value[7] << bf.value[8] << bf.value[9] << bf.value[10] << bf.value[11] << bf.value[12] << bf.value[13] << bf.value[14] << bf.value[15] << bf.value[16] << bf.value[17] << bf.value[18] << bf.value[19] << bf.value[20] << bf.value[21] << bf.value[22] << bf.value[23] << bf.value[24] << bf.value[25] << bf.value[26] << bf.value[27] << bf.value[28] << bf.value[29] << bf.value[30] << bf.value[31] << endl;
};

BitField bitfield_initializer(int value) {
    BitField bf;
    for (int i = 0; i < 32; i++) {
        bf.value[i] = (value & (1 << i)) != 0;
    }
    printBitfields(bf);
    return bf;
}