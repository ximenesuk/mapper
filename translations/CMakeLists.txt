#
#    Copyright 2012, 2013 Thomas Schöps
#    Copyright 2012-2017 Kai Pastor
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
 
message(STATUS "Configuring ${PROJECT_NAME} translations")

find_package(Qt5LinguistTools REQUIRED)

set(Mapper_TRANS
  OpenOrienteering_cs.ts
  OpenOrienteering_da.ts
  OpenOrienteering_de.ts
  OpenOrienteering_en.ts
  OpenOrienteering_eo.ts
  OpenOrienteering_es.ts
  OpenOrienteering_fi.ts
  OpenOrienteering_fr.ts
  OpenOrienteering_hu.ts
  OpenOrienteering_id.ts
  OpenOrienteering_it.ts
  OpenOrienteering_ja.ts
  OpenOrienteering_lv.ts
  OpenOrienteering_nb.ts
  OpenOrienteering_nl.ts
  OpenOrienteering_pl.ts
  OpenOrienteering_pt_BR.ts
  OpenOrienteering_ru.ts
  OpenOrienteering_sv.ts
  OpenOrienteering_uk.ts
  CACHE INTERNAL "Translation files"
)
qt5_add_translation(Mapper_QM ${Mapper_TRANS})

set(Mapper_TRANS_TEMPLATE
  OpenOrienteering_et.ts
  OpenOrienteering_he.ts
  OpenOrienteering_zh_CN.ts
  OpenOrienteering_template.ts
  CACHE INTERNAL "Translation files which are not deployed"
)

set(Mapper_TRANS_DIR
 ${CMAKE_CURRENT_SOURCE_DIR}
 CACHE INTERNAL "Translations build directory"
)

if(Mapper_TRANSLATIONS_EMBEDDED)
	set(Mapper_TRANS_QRC "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
	file(WRITE "${Mapper_TRANS_QRC}.tmp"
	  "<!DOCTYPE RCC><RCC version=\"1.0\">\n <qresource>\n")
	foreach(_qm_file ${Mapper_QM})
		get_filename_component(_qm_filename "${_qm_file}" NAME)
		file(APPEND "${Mapper_TRANS_QRC}.tmp"
		  "  <file alias=\"translations/${_qm_filename}\">${_qm_file}</file>\n")
	endforeach(_qm_file)
	file(APPEND "${Mapper_TRANS_QRC}.tmp"
	  " </qresource>\n</RCC>\n")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${Mapper_TRANS_QRC}.tmp" "${Mapper_TRANS_QRC}")
	qt5_add_resources(Mapper_TRANSLATIONS_RCC "${Mapper_TRANS_QRC}" OPTIONS -no-compress)

	# A library is a top-level target, the generated source file isn't.
	add_library(Mapper_translations STATIC ${Mapper_TRANSLATIONS_RCC})
	add_dependencies(Mapper_translations ${Mapper_QM})

	set(Mapper_LIBRARIES 
	  Mapper_translations 
	  ${Mapper_LIBRARIES}
	  PARENT_SCOPE)

else()
	# A target which builds all translations.
	add_custom_target(Mapper_translations ALL DEPENDS ${Mapper_QM})

	install(
	  FILES ${Mapper_QM}
	  DESTINATION "${MAPPER_DATA_DESTINATION}/translations")

endif(Mapper_TRANSLATIONS_EMBEDDED)

if(APPLE)
	# Localized resources just to enable localized native dialogs.
	foreach(_mapper_trans en ${Mapper_TRANS})
		get_filename_component(_mapper_lang ${_mapper_trans} NAME_WE)
		string(REPLACE OpenOrienteering_ "" LANGUAGE_CODE ${_mapper_lang})
		configure_file(locversion.plist.in ${LANGUAGE_CODE}.lproj/locversion.plist @ONLY)
		install(
		  DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${LANGUAGE_CODE}.lproj"
		  DESTINATION "${MAPPER_DATA_DESTINATION}")
	endforeach()
endif()

# For super-builds (e.g. cross-compilation), wait for the Qt build.
add_dependencies(Mapper_translations Mapper_prerequisites)


# A target which updates all translations.

add_custom_target(${PROJECT_NAME}_translations_update)


# A temporary file which lists the source files to be translated.

set(${PROJECT_NAME}_TRANSLATIONS_LISTFILE ${CMAKE_CURRENT_BINARY_DIR}/translations_sourcefiles.txt
	CACHE INTERNAL "A temporary file which lists the source files to be translated")
file(WRITE "${${PROJECT_NAME}_TRANSLATIONS_LISTFILE}"
  "${PROJECT_SOURCE_DIR}/translations/future_translations.cpp\n"
  "${PROJECT_SOURCE_DIR}/packaging/translations.cpp\n"
)

#
# A macro for registering translations sources and creating update targets.
#
# Synopsis:
#
# QT5_TRANSLATIONS_SOURCES(SOURCES a.cpp b.cpp)
#
macro(QT5_TRANSLATIONS_SOURCES)
	if(NOT Qt5_LUPDATE_EXECUTABLE)
		find_package(Qt5LinguistTools REQUIRED QUIET) # Provides Qt5_LUPDATE_EXECUTABLE
	endif()
	if(TARGET ${Qt5_LUPDATE_EXECUTABLE})
		get_property(Qt5_LUPDATE_EXECUTABLE TARGET ${Qt5_LUPDATE_EXECUTABLE} PROPERTY IMPORTED_LOCATION)
	endif()
	execute_process(
	  COMMAND "${Qt5_LUPDATE_EXECUTABLE}" -version
	  OUTPUT_VARIABLE lupdate_version
	)
	string(REGEX REPLACE "^.*([0-9]\\.[0-9]\\.[0-9]).*" "\\1" lupdate_version "${lupdate_version}")
	if(lupdate_version VERSION_LESS 5.6.1)
		message(AUTHOR_WARNING "lupdate version ${lupdate_version} is less than 5.6.1. Updates disabled.")
		# We can't return from a macro, so we skip the loop in this way
		set(loop_args )
	else()
		set(loop_args ${Mapper_TRANS} ${Mapper_TRANS_TEMPLATE} ${ARGN})
	endif()
	set(_DO_SOURCES FALSE)
	foreach(_arg ${loop_args})
		if("${_arg}" STREQUAL "SOURCES")
			set(_DO_SOURCES TRUE)
		elseif(_DO_SOURCES)
			get_source_file_property(_abs_path ${_arg} LOCATION)
			file(APPEND "${${PROJECT_NAME}_TRANSLATIONS_LISTFILE}" "${_abs_path}\n")
		else()
			get_filename_component(_ts_filename ${_arg} NAME_WE)
			set(LUPDATE_OPTIONS -locations absolute)
			if("${_ts_filename}" STREQUAL "OpenOrienteering_en")
				list(APPEND LUPDATE_OPTIONS -pluralonly)
			endif()
			if("${_ts_filename}" STREQUAL "OpenOrienteering_template")
				list(APPEND LUPDATE_OPTIONS -no-obsolete)
			endif()
			if("${_ts_filename}" MATCHES "OpenOrienteering_(et|id|lv|nb|nl)")
				# Qt translations which are not yet available from upstream, as of Qt 5.5.1.
				# Inserted from qt_template.ts into the OpenOrienteering .ts file by
				#   lconvert -sort-contexts -o OpenOrienteering_XX.ts 
				# Need to revert the "obsolete" and "vanished" marker created by lupdate,
				# but prevent deletion of empty "vanished" items.
				set(PRE_LUPDATE_ACTION
				  COMMAND sed -i -e "/<name>Q[A-Z]/,/<context>/s/\\(<translation[^>]*>\\)</\\1REMOVE_ME\\</" ${Mapper_TRANS_DIR}/${_arg}
				)
				set(POST_LUPDATE_ACTION
				  COMMAND sed -i -e "/<name>Q[A-Z]/,/<context>/s/<translation[^>]*>REMOVE_ME</<translation type=\"unfinished\"></" ${Mapper_TRANS_DIR}/${_arg}
				  COMMAND sed -i -e "s/>REMOVE_ME</></g" ${Mapper_TRANS_DIR}/${_arg}
				  COMMAND sed -i -e "/<name>Q[A-Z]/,/<context>/s/ type=.[ov][a-z]*.//g" ${Mapper_TRANS_DIR}/${_arg}
				  COMMAND sed -i -e "/<name>Q[A-Z]/,/<context>/s/\\(<translation\\)\\/>/\\1 type='unfinished'><\\/translation>/g" ${Mapper_TRANS_DIR}/${_arg}
				)
			else()
				set(PRE_LUPDATE_ACTION )
				set(POST_LUPDATE_ACTION
				  COMMAND sed -i -e "/<name>Q[A-Z]/,/<context>/d" ${Mapper_TRANS_DIR}/${_arg}
				)
			endif()
			add_custom_target(${PROJECT_NAME}_${_ts_filename}_update
			  # SOURCES are displayed in Qt Creator
			  SOURCES "${PROJECT_SOURCE_DIR}/translations/future_translations.cpp"
			          "${PROJECT_SOURCE_DIR}/packaging/translations.cpp"
			  ${PRE_LUPDATE_ACTION}
			  COMMAND "${Qt5_LUPDATE_EXECUTABLE}" ${LUPDATE_OPTIONS} @${${PROJECT_NAME}_TRANSLATIONS_LISTFILE} -ts ${Mapper_TRANS_DIR}/${_arg}
			  ${POST_LUPDATE_ACTION}
			  # Reformat for Weblate to minimize git noise
			  COMMAND sed -i -e "/<source\\|<translation\\|<[a-z]*comment/,/\\/source>\\|\\/translation>\\|\\/[a-z]*comment>/{s/&apos;/'/g;s/&quot;/\"/g}" ${Mapper_TRANS_DIR}/${_arg}
			  DEPENDS ${${PROJECT_NAME}_TRANSLATIONS_LISTFILE}
			  VERBATIM)
			add_dependencies(${PROJECT_NAME}_translations_update ${PROJECT_NAME}_${_ts_filename}_update)
		endif()
	endforeach()
endmacro()

if(UNIX AND NOT APPLE)
	set(update_source )
	if(Mapper_DEVELOPMENT_BUILD)
		# Only development builds are allowed to update the source directories.
		set(update_sources
		  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "../packaging/linux/Mapper.desktop" "${PROJECT_SOURCE_DIR}/packaging/linux/"
		  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "../packaging/linux/openorienteering-mapper.xml" "${PROJECT_SOURCE_DIR}/packaging/linux/"
		)
	endif()
	set(translations )
	foreach(translation ${Mapper_TRANS})
		list(APPEND translations "${CMAKE_CURRENT_SOURCE_DIR}/${translation}")
	endforeach()
	add_custom_command(OUTPUT text-files.stamp
	    desktop_file_comment.txt mime_type_comment.txt
	  COMMAND "${CMAKE_COMMAND}" -E make_directory "../packaging/linux"
	  COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/packaging/linux/Mapper.desktop" "../packaging/linux/"
	  COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/packaging/linux/openorienteering-mapper.xml" "../packaging/linux/"
	  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/translate_text_files.sh" ${translations}
	  ${update_sources}
	  COMMAND "${CMAKE_COMMAND}" -E touch text-files.stamp
	  DEPENDS translate_text_files.sh ${translations}
	  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	)
	add_custom_target(translate-text-files
	  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/text-files.stamp"
	)
	add_dependencies(Mapper_translations translate-text-files)
endif()

message(STATUS "Configuring ${PROJECT_NAME} translations - done")
