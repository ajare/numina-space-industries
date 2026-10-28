#include <nsi/core/Core.h>

#include <concepts>

namespace core = nsi::core;
static_assert(std::same_as<int, int>); // C++20 is supplied by nsi::core.

int main()
{
    return 0;
}
