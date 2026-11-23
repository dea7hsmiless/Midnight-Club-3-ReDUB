# Cross-toolchain for iPhoneOS builds on Windows, with no Mac.
#
#   compiler   clang from the Swift Windows toolchain (targets arm64-apple-ios*)
#   linker     ld64.lld from LLVM 23 (the Swift toolchain's own lld is older
#              and rejects '-platform_version ios'); found via -fuse-ld=lld,
#              preferring IOS_LD64 on PATH (-B also adds llvm-ar)
#   sysroot     the iPhoneOS SDK extracted from Xcode (E:\AI\iosc\xcode\...)
#
# Override per machine with -DIOS_SDK=... -DIOS_LLVM=... -DIOS_LD64=..., or the
# matching environment variables. CMAKE_OSX_DEPLOYMENT_TARGET chooses the
# minimum iOS version.

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)

# --- sysroot --------------------------------------------------------------
if(NOT DEFINED IOS_SDK)
  if(DEFINED ENV{IOS_SDK})
    set(IOS_SDK "$ENV{IOS_SDK}")
  else()
    set(IOS_SDK "E:/AI/iosc/xcode/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk")
  endif()
endif()
set(IOS_SDK "${IOS_SDK}" CACHE PATH "iPhoneOS SDK")
set(CMAKE_OSX_SYSROOT "${IOS_SDK}")

# --- compiler -------------------------------------------------------------
if(NOT DEFINED IOS_LLVM OR IOS_LLVM STREQUAL "")
  if(DEFINED ENV{IOS_LLVM})
    set(IOS_LLVM "$ENV{IOS_LLVM}")
  else()
    set(IOS_LLVM "C:/Users/Donte/AppData/Local/Programs/Swift/Toolchains/6.4.1+Asserts/usr")
  endif()
endif()
if(NOT EXISTS "${IOS_LLVM}/bin/clang.exe")
  message(FATAL_ERROR
    "No clang in ${IOS_LLVM}: set IOS_LLVM=<Swift toolchain usr dir>")
endif()
set(IOS_LLVM "${IOS_LLVM}" CACHE PATH "Swift Windows toolchain usr directory")

set(CMAKE_C_COMPILER   "${IOS_LLVM}/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${IOS_LLVM}/bin/clang++.exe")
set(CMAKE_AR           "${IOS_LLVM}/bin/llvm-ar.exe")
set(CMAKE_RANLIB       "${IOS_LLVM}/bin/llvm-ranlib.exe")
set(CMAKE_STRIP        "${IOS_LLVM}/bin/llvm-strip.exe")
set(CMAKE_LINKER       "${IOS_LLVM}/bin/clang.exe")

# --- deployment target ----------------------------------------------------
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS version")
endif()

# --- linker ---------------------------------------------------------------
if(NOT DEFINED IOS_LD64)
  if(DEFINED ENV{IOS_LD64})
    set(IOS_LD64 "$ENV{IOS_LD64}")
  elseif(EXISTS "E:/AI/iosc/tools/ld64.lld.exe")
    set(IOS_LD64 "E:/AI/iosc/tools")
  else()
    set(IOS_LD64 "C:/Program Files/LLVM/bin")
  endif()
endif()
set(IOS_LD64 "${IOS_LD64}" CACHE PATH "LLVM 23 bin directory (ld64.lld)")
if(NOT EXISTS "${IOS_LD64}/ld64.lld.exe")
  message(FATAL_ERROR "No ld64.lld in ${IOS_LD64}: set IOS_LD64=<LLVM 23 bin dir>")
endif()

# The Swift toolchain's bundled lld cannot link for iOS; use the LLVM 23 one.
# -B also lets clang find llvm-ar for its internal ranlib invocations.
set(_ios_linker_prefix "-B${IOS_LD64}/ -fuse-ld=lld")
set(CMAKE_EXE_LINKER_FLAGS    "${_ios_linker_prefix}")
set(CMAKE_SHARED_LINKER_FLAGS "${_ios_linker_prefix}")
set(CMAKE_MODULE_LINKER_FLAGS "${_ios_linker_prefix}")

# CMake's compiler detection does not pass CMAKE_OSX_SYSROOT to its test
# compiles (AvailabilityMacros.h and friends were "not found" there), and its
# Apple platform modules add -miphoneos-version-min=..., a spelling a Windows
# default-triple clang rejects. So the target triple travels in the flags,
# alongside the sysroot, and the SDK stays a real path for iOS-Initialize.cmake.
set(CMAKE_C_FLAGS_INIT   "-target arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET} -isysroot ${IOS_SDK}")
set(CMAKE_CXX_FLAGS_INIT "-target arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET} -isysroot ${IOS_SDK}")
set(CMAKE_OBJC_FLAGS_INIT   "-target arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET} -isysroot ${IOS_SDK} -fobjc-arc")
set(CMAKE_OBJCXX_FLAGS_INIT "-target arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET} -isysroot ${IOS_SDK} -fobjc-arc")

# --- tool stand-ins --------------------------------------------------------
# CMake's Apple platform modules insist on install_name_tool (a macOS binary)
# when an OBJC compiler is enabled. Our links are done by ld64.lld, which
# handles install names itself, so a no-op stand-in is enough. Same for
# codesign: the sideload tool signs the IPA.
if(NOT DEFINED IOS_STUB_BIN)
  if(DEFINED ENV{IOS_STUB_BIN})
    set(IOS_STUB_BIN "$ENV{IOS_STUB_BIN}")
  else()
    set(IOS_STUB_BIN "$ENV{TEMP}/iosbin")
  endif()
endif()
if(EXISTS "${IOS_STUB_BIN}/install_name_tool.bat")
  set(CMAKE_INSTALL_NAME_TOOL "${IOS_STUB_BIN}/install_name_tool.bat")
endif()

message(STATUS "iOS toolchain: ${CMAKE_C_COMPILER}")
message(STATUS "iOS sysroot:  ${IOS_SDK}")
message(STATUS "iOS linker:    ${IOS_LD64}/ld64.lld.exe")
message(STATUS "iOS min OS:    ${CMAKE_OSX_DEPLOYMENT_TARGET}")