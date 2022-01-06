include("${CMAKE_CURRENT_LIST_DIR}/Utilities.cmake")

check_generator("Xcode")

fix_xcode_try_compile()

set_common_xcode_attributes()

set(CMAKE_DEBUG_POSTFIX "-ios-dbg")
set(CMAKE_MINSIZEDEBUG_POSTFIX "-ios-dbg")
set(CMAKE_RELEASE_POSTFIX "-ios")

set(CMAKE_OSX_SYSROOT "iphoneos")

set(CMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator")
set(CMAKE_XCODE_EFFECTIVE_PLATFORMS "-iphoneos;-iphonesimulator")
set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "7.0")

set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=iphoneos*] "\$(ARCHS_iphoneos_\$(CONFIGURATION))")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS_iphoneos_Debug "armv7 arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS_iphoneos_MinSizeDebug "armv7 arm64")
# iOS release configurations need to build for armv7s for sake of CocoaPods.
set(CMAKE_XCODE_ATTRIBUTE_ARCHS_iphoneos_Release "armv7 armv7s arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS_iphoneos_RelWithDebInfo "armv7 armv7s arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS_iphoneos_MinSizeRel "armv7 armv7s arm64")

set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE[sdk=iphone*] "YES")

set_bitcode_attributes()