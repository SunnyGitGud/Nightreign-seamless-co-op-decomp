#include "nrsc/cs/auto_control_api.hpp"

#include <cstdio>

#include "nrsc/core/fatal_error.hpp"

namespace nrsc::cs {

std::string method_unavailable_message(std::string_view method) {
    char buffer[0x100] = {};
    const std::string name(method);
    std::snprintf(buffer, sizeof buffer,
                  "Method \"%s\" is not available. If you're using ModEngine, please make sure seamless co-op is "
                  "loaded early. The game will now exit.",
                  name.c_str());
    return buffer;
}

void invoke_method(const AutoControlMethods& methods, std::string_view method) {
    const auto it = methods.find(std::string(method));
    if (it == methods.end())
        throw FatalError(method_unavailable_message(method), "nrsc\\cs\\auto_control_api\\auto_control_api.cpp", 0x14);
    it->second(0);
}

bool should_sign_in(bool game_present, bool game_field_174e8_set, bool sign_in_present, int sign_in_state) {
    return game_present && game_field_174e8_set && sign_in_present && sign_in_state == 0;
}

}  // namespace nrsc::cs
