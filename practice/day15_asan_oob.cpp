#include <iostream>

int main() {
    int* data = new int[3]{10, 20, 30};

    for (int i = 0; i < 3; ++i) {
        data[i] += 10;
    }

    for (int i = 0; i < 3; ++i) {
        std::cout << data[i] << '\n';
    }

    delete[] data;
    return 0;
}