#pragma once
#include <cstdio>
#include <vector>

struct Test {
    const char* name;
    void (*fn)();
};
std::vector<Test>& test_list();
void test_check(bool ok, const char* file, int line, const char* expr);

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        test_list().push_back(Test{name, fn});
    }
};

#define CHECK(b) test_check((b), __FILE__, __LINE__, #b)
#define CHECK_EQ(a, b) test_check((a) == (b), __FILE__, __LINE__, #a " == " #b)
