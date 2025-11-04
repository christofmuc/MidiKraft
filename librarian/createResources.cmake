#
# Replace the JUCE BinaryBuilder with something more CMAKE
# This magic is from https://stackoverflow.com/questions/11813271/embed-resources-eg-shader-code-images-into-executable-library-with-cmake
#
# Creates C resources file from files in given directory
if(NOT DEFINED output)
    set(output "${CMAKE_CURRENT_SOURCE_DIR}/BinaryResources.h")
endif()
if(NOT DEFINED resource_dir)
    set(resource_dir "${CMAKE_CURRENT_SOURCE_DIR}/resources")
endif()

get_filename_component(output_dir "${output}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

# Create empty output file
file(WRITE "${output}" "")
# Collect input files
file(GLOB bins "${resource_dir}/*")
# Iterate through input files
foreach(bin ${bins})
	# Get short filename
	get_filename_component(filename "${bin}" NAME)
	# Replace filename spaces & extension separator for C compatibility
	string(REGEX REPLACE "\\.| |-" "_" filename "${filename}")
	# Read hex data from file
	file(READ "${bin}" filedata HEX)
	# Convert hex data for C compatibility
	string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata "${filedata}")
	# Append data to output file
	file(APPEND "${output}" "const unsigned char ${filename}[] = {${filedata}};\nconst unsigned ${filename}_size = sizeof(${filename});\n")
endforeach()
