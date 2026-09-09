#include "test_framework.hpp"

#include <vector>

static std::vector<Test> tests;
static int checks = 0;
static int failures = 0;

std::vector<Test>& test_list() {
    return tests;
}
void test_check(bool ok, const char* file, int line, const char* expr) {
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL %s:%d  %s\n", file, line, expr);
    }
}

int main() {
    for (auto& t : tests) {
        printf("== %s\n", t.name);
        t.fn();
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
