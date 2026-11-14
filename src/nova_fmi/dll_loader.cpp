#include "dll_loader.hpp"
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace nova_fmi
{

dll_loader::dll_loader(const std::string& path)
{
#ifdef _WIN32
    handle_ = LoadLibraryA(path.c_str());
#else
    handle_ = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif

    if (!handle_) {
#ifdef _WIN32
        throw std::runtime_error("Failed to load DLL: " + path + " (Error code: " + std::to_string(GetLastError()) + ")");
#else
        throw std::runtime_error("Failed to load shared library: " + path + " (Error: " + dlerror() + ")");
#endif
    }
}

dll_loader::~dll_loader()
{
    if (handle_) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
}

void* dll_loader::get_function_pointer(const std::string& name) const
{
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name.c_str()));
#else
    return dlsym(handle_, name.c_str());
#endif
}

} // namespace nova_fmi
