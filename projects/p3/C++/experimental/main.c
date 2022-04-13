
// function to return 8
int idempotentFunction(int lol) {
    return lol;
}

int main() {
    int a = 8;
    int x = 0;
    int y = 8;
    int idem = 0;
    int z = 1;
    int n = 100000;
    for (int i = n; i > 0; i = i - 1) {
        x = y + z;
        a = a + x * x;
        idem = idempotentFunction(8);
    }
}