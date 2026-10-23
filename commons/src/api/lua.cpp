#include "api/lua.hpp"

namespace commons::api {

void setClasses(sol::state& lua) {
    lua.open_libraries(sol::lib::math);
}

void setNamespaces(sol::state& lua) {
    lua["bubble"].get_or_create<sol::table>();
}

}
