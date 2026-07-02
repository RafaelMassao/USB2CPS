# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/massao/joypadai/joypad-os/src/build/_deps/picotool-src"
  "/home/massao/joypadai/joypad-os/src/build/_deps/picotool-build"
  "/home/massao/joypadai/joypad-os/src/build/_deps"
  "/home/massao/joypadai/joypad-os/src/build/picotool/tmp"
  "/home/massao/joypadai/joypad-os/src/build/picotool/src/picotoolBuild-stamp"
  "/home/massao/joypadai/joypad-os/src/build/picotool/src"
  "/home/massao/joypadai/joypad-os/src/build/picotool/src/picotoolBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/massao/joypadai/joypad-os/src/build/picotool/src/picotoolBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/massao/joypadai/joypad-os/src/build/picotool/src/picotoolBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
