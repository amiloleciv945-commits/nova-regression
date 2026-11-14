#ifndef NOVA_FMI_DLL_LOADER_HPP
#define NOVA_FMI_DLL_LOADER_HPP

#include <string>
#include <stdexcept>

namespace nova_fmi
{

class dll_loader
{
public:
    explicit dll_loader(const std::string& path);
    ~dll_loader();

    dll_loader(const dll_loader&) = delete;
    dll_loader& operator=(const dll_loader&) = delete;

    template <typename FuncType>
    FuncType get_function(const std::string& name) const
    {
        auto func = get_function_pointer(name);
        if (!func) {
            throw std::runtime_error("Failed to find function: " + name);
        }
        return reinterpret_cast<FuncType>(func);
    }

private:
    void* get_function_pointer(const std::string& name) const;
    void* handle_ = nullptr;
};

} // namespace nova_fmi

#endif // NOVA_FMI_DLL_LOADER_HPP
