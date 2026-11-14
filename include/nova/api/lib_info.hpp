
#ifndef LIBNOVA_LIB_INFO_HPP
#define LIBNOVA_LIB_INFO_HPP

namespace nova
{

/// Software version
struct version
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/// Returns the version of the nova library.
version library_version();

} // namespace nova

#endif // LIBNOVA_LIB_INFO_HPP
