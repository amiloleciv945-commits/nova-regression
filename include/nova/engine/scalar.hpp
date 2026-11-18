
#ifndef LIBNOVA_SCALAR_HPP
#define LIBNOVA_SCALAR_HPP

#include <string>
#include <variant>

namespace nova
{

using scalar_value = std::variant<double, int, bool, std::string,std::vector<double>>;

}


#endif // LIBNOVA_SCALAR_HPP
