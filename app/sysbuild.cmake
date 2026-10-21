# Sysbuild extensions of rt_daq, included after every image is added (MCUboot, the application)
# and before any of them is configured. Settings: sysbuild.conf. Plan:
# the gauge's dev_docs/plans/todo/FIRMWARE_UPDATE_PLAN.md (D1 mode, D2 partitions, D4 keys).

if(NOT SB_CONFIG_BOOTLOADER_MCUBOOT)
  return()
endif()

# The board's flash partition table, the one the application includes from its overlay
# (app/boards/<board>.overlay), also goes to MCUboot, which is built from the board devicetree and
# its own overlays only. MCUboot and the application then agree on where the slots are.
#
# The file is named like the board's overlay: the board and its qualifiers, or the qualifiers
# without the SoC (esp32s3_pm_hw_02_esp32s3_procpu, then esp32s3_pm_hw_02_procpu).
#
# This is an extra overlay, added to whatever MCUboot already uses. Do not add an
# app/sysbuild/mcuboot/ directory (sysbuild would take it as MCUboot's whole configuration
# directory and want its own prj.conf there), and no app/sysbuild/mcuboot.overlay (it would replace
# the overlays MCUboot finds for the board).
string(REGEX REPLACE "^/" "" board_qualifiers "${BOARD_QUALIFIERS}")
string(REPLACE "/" "_" board_target "${BOARD}_${board_qualifiers}")
# (Not a regex: CMake's REGEX REPLACE matches a leading ^ again on each remainder.)
string(REPLACE "/" ";" qualifier_list "${board_qualifiers}")
list(REMOVE_AT qualifier_list 0)
list(JOIN qualifier_list "_" board_qualifiers_no_soc)
set(board_target_no_soc "${BOARD}_${board_qualifiers_no_soc}")

set(partitions_dtsi "")
foreach(candidate ${board_target} ${board_target_no_soc})
  if(EXISTS ${APP_DIR}/boards/partitions/${candidate}.dtsi)
    set(partitions_dtsi ${APP_DIR}/boards/partitions/${candidate}.dtsi)
    break()
  endif()
endforeach()

if(partitions_dtsi)
  message(STATUS "MCUboot partition table: ${partitions_dtsi}")
  if(NOT partitions_dtsi IN_LIST mcuboot_EXTRA_DTC_OVERLAY_FILE)
    list(APPEND mcuboot_EXTRA_DTC_OVERLAY_FILE ${partitions_dtsi})
  endif()
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE ${mcuboot_EXTRA_DTC_OVERLAY_FILE}
      CACHE INTERNAL "Application extra DTC overlay file" FORCE
  )
else()
  message(WARNING "No partition table for ${board_target} in ${APP_DIR}/boards/partitions: "
                  "MCUboot builds with the board's own table, which the application may not match.")
endif()

# MCUboot's own SoC fragment (boot/zephyr/socs/esp32s3_procpu.conf: console, serial and log off) goes
# before the app's. MCUboot reads it itself ahead of the extra fragments, but Zephyr's sysbuild.cmake for the
# DevKitC appends it to mcuboot_EXTRA_CONF_FILE as well, after sysbuild/mcuboot.conf, and the later file wins:
# the bench block of mcuboot.conf (MCUboot's log on the console) would be switched off again. The relative
# order of the other fragments stays, so a list given on the command line keeps its meaning.
set(mcuboot_socs_conf ${mcuboot_EXTRA_CONF_FILE})
list(FILTER mcuboot_socs_conf INCLUDE REGEX "/boot/zephyr/socs/[^/]*\\.conf$")
if(mcuboot_socs_conf)
  set(mcuboot_other_conf ${mcuboot_EXTRA_CONF_FILE})
  list(FILTER mcuboot_other_conf EXCLUDE REGEX "/boot/zephyr/socs/[^/]*\\.conf$")
  set(mcuboot_EXTRA_CONF_FILE ${mcuboot_socs_conf} ${mcuboot_other_conf}
      CACHE INTERNAL "Application extra Kconfig fragments" FORCE
  )
endif()

# Size checks, run at the end of every sysbuild build: MCUboot has to fit its boot partition, and
# the signed application its slot minus the swap trailer. Nothing else checks the MCUboot size; the
# linker does not know the partition. Run it by hand on any build directory:
#   python3 scripts/check_image_sizes.py <build dir>
cmake_path(SET repo_root NORMALIZE "${APP_DIR}/..")
add_custom_target(check_image_sizes ALL
  COMMAND ${PYTHON_EXECUTABLE} ${repo_root}/scripts/check_image_sizes.py
          --mcuboot ${CMAKE_BINARY_DIR}/mcuboot --app ${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}
  COMMENT "Checking the image sizes against the partitions"
  USES_TERMINAL
)
add_dependencies(check_image_sizes mcuboot ${DEFAULT_IMAGE})
