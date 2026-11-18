
#ifndef NOVA_SSP_LOADER_HPP
#define NOVA_SSP_LOADER_HPP

#include "nova/components/structure/simulation_structure.hpp"

#include <filesystem>
#include <memory>

namespace nova
{

[[nodiscard]] std::unique_ptr<simulation_structure> load_ssp(const std::filesystem::path& path);

} // namespace nova

#endif // NOVA_SSP_LOADER_HPP
