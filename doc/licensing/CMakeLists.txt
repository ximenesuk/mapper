#
#    Copyright 2014-2017 Kai Pastor
#    
#    This file is part of OpenOrienteering.
# 
#    OpenOrienteering is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
# 
#    OpenOrienteering is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
# 
#    You should have received a copy of the GNU General Public License
#    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.


message(STATUS "Configuring ${PROJECT_NAME} licensing documentation")


find_package(Qt5Core REQUIRED)

# Like Qt5::moc, Qt5::qdoc should come from Qt5CoreConfigExtra.cmake
if (NOT TARGET Qt5::qdoc AND TARGET Qt5::moc)
    get_target_property(_qt5_moc_path Qt5::moc IMPORTED_LOCATION)
    get_filename_component(_qt5_bin_prefix "${_qt5_moc_path}" PATH)
    add_executable(Qt5::qdoc IMPORTED)
	set(imported_location ${_qt5_bin_prefix}/qdoc)
	if(NOT EXISTS "${imported_location}")
		message(FATAL_ERROR "qdoc executable does not exist: "
		                    "${imported_location}")
	endif()
	set_target_properties(Qt5::qdoc PROPERTIES
		IMPORTED_LOCATION ${imported_location}
	)
	if(TARGET Qt5)
		add_dependencies(Qt5::qdoc Qt5)
	endif()
endif()
set(Qt5Core_QDOC_EXECUTABLE Qt5::qdoc)


set(LICENSING_SRCS
  licensing.qdocconf
  licensing-html.qdocconf
  licensing.css
  src/licensing.qdoc
  src/gdal-licensing.qdocinc
  src/qt-licensing.qdocinc
  src/trademarks.qdocinc
  src/apache-2.0.qdoc
  src/gpl-3.0.qdoc
  src/lgpl-2.1.qdoc
  src/lgpl-3.0.qdoc
  src/gcc-runtime-library-exception.qdoc
)

add_custom_target(licensing-sources
  COMMENT   "This target makes Qt Creator show all sources in the project tree."
  SOURCES   licensing.pro licensing.qrc ${LICENSING_SRCS}
)

add_custom_command(
  OUTPUT    html/licensing.html
  COMMENT   "Running qdoc on licensing documentation"
  COMMAND   ${Qt5Core_QDOC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/licensing.qdocconf"
			  -outputdir "${CMAKE_CURRENT_BINARY_DIR}/html"
  DEPENDS   ${LICENSING_SRCS}
)

add_custom_command(
  OUTPUT    licensing.tmp.qrc
  COMMAND   ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/licensing.qrc" licensing.tmp.qrc
  DEPENDS   "${CMAKE_CURRENT_SOURCE_DIR}/licensing.qrc" html/licensing.html
)
qt5_add_resources(LICENSING_QRC_CPP
  "${CMAKE_CURRENT_BINARY_DIR}/licensing.tmp.qrc"
  OPTIONS -compress 9 -threshold 50
)

add_library(doc-licensing
  STATIC
  ${LICENSING_QRC_CPP}
)

target_link_libraries(doc-licensing
  PUBLIC Qt5::Core
)
