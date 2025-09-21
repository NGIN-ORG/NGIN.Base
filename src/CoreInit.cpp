#include <NGIN/Defines.hpp>
#include <NGIN/NGIN.hpp>

namespace NGIN::detail
{
    // Linker anchor to force creation of a translation unit when building static/shared variants.
    NGIN_BASE_LOCAL void CoreLinkAnchor() noexcept {}
}// namespace NGIN::detail
