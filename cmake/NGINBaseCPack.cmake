#-------------------------------------------------------------------------------
# CPack
#-------------------------------------------------------------------------------
include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME "NGIN.Base")
set(CPACK_PACKAGE_VENDOR "NGIN Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NGIN.Base - A lightweight C++ foundational library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "berggrenmille+NGIN@hotmail.se")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")

if(EXISTS "${NGIN_BASE_ROOT_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${NGIN_BASE_ROOT_DIR}/LICENSE")
endif()

include(CPack)
