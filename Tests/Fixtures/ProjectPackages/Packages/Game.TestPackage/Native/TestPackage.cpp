#if defined(_WIN32)
#define TEST_PACKAGE_API __declspec(dllexport)
#else
#define TEST_PACKAGE_API __attribute__((visibility("default")))
#endif

extern "C" TEST_PACKAGE_API int IndexTestPackage_Value() {
    return 42;
}
