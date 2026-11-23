#include <iostream>

struct Watch {
    const char* name;

    ~Watch() {
        std::cout << name << " destroyed\n";
    }
};

void show_scope() {
    Watch local{"local object"};
    std::cout << "inside function\n";
}

int main() {
    std::cout << "before function\n";
    show_scope();
    std::cout << "after function\n";

    int x = 10;
    int& ref = x;
    ref = 20;

    int* ptr = &x;
    *ptr = 30;

    std::cout << "x = " << x << '\n';
    return 0;
}