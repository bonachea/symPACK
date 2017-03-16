file(INSTALL ${CMAKE_CURRENT_BINARY_DIR}/external/upcxx_install DESTINATION ${CMAKE_INSTALL_PREFIX})



file(GLOB files "${CMAKE_CURRENT_BINARY_DIR}/external/upcxx_install/include/*.mak")
foreach(file ${files})
  file(READ ${file} file_content)
  string(REGEX REPLACE "\n" ";" ContentsAsList "${file_content}")
  unset(ModifiedContents)
  foreach(Line ${ContentsAsList})
    if("${Line}" MATCHES "GASNET_PREFIX = ")
      message(${Line})
      string(REGEX REPLACE "= ${CMAKE_CURRENT_BINARY_DIR}/external/gasnet_install" "= ${CMAKE_INSTALL_PREFIX}/gasnet_install" Line ${Line})
      message(${Line})
    elseif("${Line}" MATCHES "${CMAKE_CURRENT_BINARY_DIR}/external/upcxx_install")
      string(REGEX REPLACE "${CMAKE_CURRENT_BINARY_DIR}/external/upcxx_install" "${CMAKE_INSTALL_PREFIX}/upcxx_install" Line ${Line})
    endif()

    set(ModifiedContents "${ModifiedContents}${Line}\n")
  endforeach()
  string(REGEX REPLACE "${CMAKE_CURRENT_BINARY_DIR}/external/upcxx_install" "${CMAKE_INSTALL_PREFIX}/upcxx_install" ofile ${file})
  message(${ofile})
  file(WRITE ${ofile} ${ModifiedContents})
endforeach()

