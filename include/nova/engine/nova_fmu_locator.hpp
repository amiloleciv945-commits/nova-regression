#ifndef LIBNOVA_NOVA_FMU_LOCATOR_HPP
#define LIBNOVA_NOVA_FMU_LOCATOR_HPP

#include "nova/engine/model.hpp"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <string>

namespace nova
{

class nova_fmu_locator
{
public:
    std::shared_ptr<model> locate(const std::filesystem::path& base, const std::string& uri);

private:
    std::unordered_map<std::string, std::shared_ptr<model>> cache_;
};

} // namespace nova

#endif // LIBNOVA_NOVA_FMU_LOCATOR_HPP
