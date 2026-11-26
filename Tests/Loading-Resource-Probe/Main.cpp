#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <cstdlib>
#include <iostream>
extern bool InitializeLoadingResourceBackend();
extern void ShutdownLoadingResourceBackend();
int main(int argc, char** argv) {
    const bool gpu = std::getenv("FUNKE_LOADING_GPU_TEST") != nullptr;
    if (gpu && !InitializeLoadingResourceBackend()) return 77;
    doctest::Context context(argc, argv);
    if (!gpu) context.addFilter("test-case-exclude", "Large texture publication*,Font family selects*,Texture partial transfers*");
    const int result = context.run();
    if (gpu) ShutdownLoadingResourceBackend();
    return result;
}
