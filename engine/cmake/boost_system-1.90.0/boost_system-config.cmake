# Ubuntu 26.04 ships Boost 1.90 without a boost_system CMake config.
# Boost.System is header-only since 1.69, so provide interface targets for
# both the BoostConfig-style name (boost_system::system) and the
# FindBoost-style alias (Boost::system) that upstream v1.2 links against.
if(NOT TARGET boost_system::system)
  add_library(boost_system::system INTERFACE IMPORTED)
endif()
if(NOT TARGET Boost::system)
  add_library(Boost::system INTERFACE IMPORTED)
  set_target_properties(Boost::system PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "BOOST_SYSTEM_NO_DEPRECATED")
endif()
set(boost_system_FOUND TRUE)
