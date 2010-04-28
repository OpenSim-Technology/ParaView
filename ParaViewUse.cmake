IF(ParaView_FOUND)
  # Update CMAKE_MODULE_PATH so includes work.
  SET (CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${ParaView_CMAKE_DIR}
    ${VTKCS_CONFIG_DIR})

  SET(VTK_DIR ${PARAVIEW_VTK_DIR})
  FIND_PACKAGE(VTK)
  IF(VTK_FOUND)
    INCLUDE_DIRECTORIES(${PARAVIEW_INCLUDE_DIRS})
    LINK_DIRECTORIES(${PARAVIEW_LIBRARY_DIRS})
    INCLUDE(${VTK_USE_FILE})
  ENDIF(VTK_FOUND)

  MESSAGE(STATUS "Loading ParaView CMake commands")
  INCLUDE(vtkWrapClientServer)
  INCLUDE(ParaViewMacros)
  INCLUDE(ParaViewPlugins)
  INCLUDE(ParaViewBranding)
  INCLUDE(ParaViewBrandingCPack)
  MESSAGE(STATUS "Loading ParaView CMake commands - done")

  IF(PARAVIEW_BUILD_QT_GUI)
    SET(QT_QMAKE_EXECUTABLE ${PARAVIEW_QT_QMAKE_EXECUTABLE})
    FIND_PACKAGE(Qt4)
    IF(QT4_FOUND)
      INCLUDE(${QT_USE_FILE})
      INCLUDE_DIRECTORIES(${PARAVIEW_GUI_INCLUDE_DIRS})
    ENDIF(QT4_FOUND)
  ENDIF(PARAVIEW_BUILD_QT_GUI)

  IF(PARAVIEW_ENABLE_COPROCESSING)
    FIND_PACKAGE(CoProcessing)
    IF(CoProcessing_FOUND)
      INCLUDE(${CoProcessing_USE_FILE})
    ENDIF(CoProcessing_FOUND)
  ENDIF(PARAVIEW_ENABLE_COPROCESSING)

ENDIF(ParaView_FOUND)