#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

int main() {
    std::vector<int> nums = {5, 2, 9, 1, 7};
    std::sort(nums.begin(), nums.end());

    printf("Hello from C++17! Sorted: ");
    for (int n : nums) printf("%d ", n);
    printf("\n");

#ifdef __clang__
    printf("Compiler: Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    printf("Compiler: MSVC %d\n", _MSC_VER);
#elif defined(__GNUC__)
    printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    printf("Compiler: Unknown\n");
#endif
    return 0;
}
