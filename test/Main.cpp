#include <windows.h>

namespace Texpose::test::expose { auto runTests() -> void; }

int main() {
    SetConsoleOutputCP(CP_UTF8);

    Texpose::test::expose::runTests();

    return 0;
}