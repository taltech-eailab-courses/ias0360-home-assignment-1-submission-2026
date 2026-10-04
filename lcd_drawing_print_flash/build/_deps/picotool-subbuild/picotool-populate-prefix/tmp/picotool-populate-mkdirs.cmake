# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-src"
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-build"
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix"
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix/tmp"
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp"
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix/src"
  "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/student/Documents/ENSIBS/TALTECH/MACHINE_EMBEDDED/ias0360-home-assignment-1-submission-2026/lcd_drawing_print_flash/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
