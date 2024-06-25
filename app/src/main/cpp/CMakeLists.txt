cmake_minimum_required(VERSION 3.22.1)

project("simpleandroidgl")

include(FetchContent)
FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG 1.3.270
)
FetchContent_MakeAvailable(volk)

FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.0.1
)
FetchContent_MakeAvailable(vma)
include_directories(${vma_SOURCE_DIR}/include)
include_directories(${volk_SOURCE_DIR})


# build native_app_glue as a static lib
set(${CMAKE_C_FLAGS}, "${CMAKE_C_FLAGS}")

## Include the GameActivity static lib to the project.
#find_package(game-activity REQUIRED CONFIG)
#set(CMAKE_SHARED_LINKER_FLAGS
#        "${CMAKE_SHARED_LINKER_FLAGS} -u \
#    Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

# now build app's shared lib
# -Wall -Werror
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 ")

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# VkAndroidSurfaceCreateInfoKHR need this
add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR=1)

file(GLOB_RECURSE SRC_FILES *.cpp CMAKE_CONFIGURE_DEPENDS)

#add_library(${CMAKE_PROJECT_NAME} SHARED
#        simpleandroidgl.cpp)

add_library(${CMAKE_PROJECT_NAME} SHARED
       simpleandroidvulkan.cpp vkapplication.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC .)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        native_app_glue
        vulkan
        EGL
        GLESv1_CM
        log)
        #volk_headers)
