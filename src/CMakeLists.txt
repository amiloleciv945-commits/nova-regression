# ------------------------------------------------------------------
# Public headers
# ------------------------------------------------------------------
set(publicHeaders

    "nova/api/lib_info.hpp"
    "nova/engine/model.hpp"
    "nova/engine/model_instance.hpp"
    "nova/engine/nova_fmu_locator.hpp"
    "nova/engine/property.hpp"
    "nova/engine/scalar.hpp"
    "nova/engine/nova_engine.hpp"
    "nova/engine/engine_scheduler.hpp"
    "nova/engine/variable_identifier.hpp"

    "nova/components/algorithm/algorithm.hpp"
    "nova/components/algorithm/fixed_step_algorithm.hpp"
    "nova/components/algorithm/strong_coupling_algorithm.hpp"

    "nova/components/recorder/engine_observer.hpp"
    "nova/components/recorder/csv_recorder.hpp"

    "nova/components/logger/logger.hpp"

    "nova/components/ssp/ssp_loader.hpp"

    "nova/components/structure/simulation_structure.hpp"

    "nova/components/util/chart_plotter.hpp"
)

set(publicHeadersFull)
foreach (header IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${PROJECT_SOURCE_DIR}/include/${header}")
endforeach ()

# ------------------------------------------------------------------
# Install headers
# ------------------------------------------------------------------
install(
    FILES
      ${publicHeadersFull}
    DESTINATION
      ${CMAKE_INSTALL_INCLUDEDIR}/nova
)

set(privateHeaders

        "engine/fmi_model.hpp"
        "engine/fmi_model_instance.hpp"

        "engine/scenario/scenario.hpp"

        "components/ssp/ssp.hpp"

        "components/util/temp_dir.hpp"
        "components/util/unzipper.hpp"
        "components/util/uuid.hpp"

)

set(sources

        "engine/model_instance.cpp"
        "engine/nova_fmu_locator.cpp"
        "engine/nova_engine.cpp"
        "engine/engine_scheduler.cpp"

        "engine/scenario/scenario.cpp"

        "components/algorithm/fixed_step_algorithm.cpp"
        "components/algorithm/gauss_seidel_algorithm.cpp"
	"components/algorithm/strong_coupling_algorithm.cpp"

        "components/recorder/csv_recorder.cpp"
        "components/recorder/engine_observer.cpp"

        "components/ssp/ssp.cpp"
        "components/ssp/ssp_loader.cpp"

        "components/structure/simulation_structure.cpp"

        "components/logger/logger.cpp"

        "components/util/chart_plotter.cpp"

)

# Generate sources from .in files
set(generatedFiles "${GENERATED_SRC_DIR}/nova/lib_info.cpp")
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/api/lib_info.cpp.in"
        "${generatedFiles}"
        @ONLY
)

add_library(nova_fmi OBJECT

        "nova_fmi/dll_loader.hpp"
        "nova_fmi/buffered_slave.hpp"
        "nova_fmi/fmicontext.hpp"
        "nova_fmi/fmu.hpp"
        "nova_fmi/model_description.hpp"
        "nova_fmi/scalar_variable.hpp"
        "nova_fmi/slave.hpp"

        "nova_fmi/fmi1/fmi1_fmu.hpp"
        "nova_fmi/fmi1/fmi1_slave.hpp"
        "nova_fmi/fmi1/fmi1_model_description.hpp"

        "nova_fmi/fmi2/fmi2_fmu.hpp"
        "nova_fmi/fmi2/fmi2_slave.hpp"
        "nova_fmi/fmi2/fmi2_model_description.hpp"
        
        "nova_fmi/dll_loader.cpp"
        "nova_fmi/fmu.cpp"

        "nova_fmi/fmi1/fmi1_fmu.cpp"
        "nova_fmi/fmi1/fmi1_slave.cpp"
        "nova_fmi/fmi1/fmi1_model_description.cpp"

        "nova_fmi/fmi2/fmi2_fmu.cpp"
        "nova_fmi/fmi2/fmi2_slave.cpp"
        "nova_fmi/fmi2/fmi2_model_description.cpp"

        "nova_fmi/fmi3/fmi3_fmu.cpp"
        "nova_fmi/fmi3/fmi3_slave.cpp"
        "nova_fmi/fmi3/fmi3_model_description.cpp"
)
set_target_properties(nova_fmi PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(nova_fmi PRIVATE fmi4c)
target_include_directories(nova_fmi
        PUBLIC
        PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/src")



set(all_sources ${publicHeadersFull} ${privateHeaders} ${sources} ${generatedFiles} "$<TARGET_OBJECTS:fmi4c>" "$<TARGET_OBJECTS:nova_fmi>")

add_library(libnova ${all_sources})
set_target_properties(libnova PROPERTIES PREFIX "")
set_target_properties(libnova PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(libnova PUBLIC "cxx_std_20")
target_include_directories(libnova
        PUBLIC
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>"
        PRIVATE
        "${GENERATED_SRC_DIR}"
        "${THIRD_PARTY_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

if (UNIX)
    target_link_libraries(libnova INTERFACE stdc++fs "${NOVA_TBB_LIBRARY}" pthread dl)
endif ()



if (NOVA_BUILD_CLIB)
    add_library(libnovac SHARED "${CMAKE_CURRENT_SOURCE_DIR}/api/nova.cpp")
    set_target_properties(libnovac PROPERTIES PREFIX "")
    if (UNIX)
        set_target_properties(libnovac PROPERTIES
                BUILD_RPATH "$ORIGIN"
                INSTALL_RPATH "$ORIGIN")
    endif ()
    set_property(TARGET libnovac PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(libnovac PUBLIC libnova)

    add_custom_command(TARGET libnovac POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PY_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:libnovac>
            ${PY_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${NOVA_TBB_LIBRARY}
            ${CMAKE_BINARY_DIR}/bin/libtbb.so.12
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${NOVA_TBB_LIBRARY}
            ${PY_BINARY_DIR}/libtbb.so.12
            COMMENT "Copying libnovac to ${PY_BINARY_DIR}"
    )

endif ()
