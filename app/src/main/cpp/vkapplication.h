#pragma once

#include <android/asset_manager.h>
#include <android/log.h>
// os window, glfw, sdi, ...
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <assert.h>
//To use volk, you have to include volk.h instead of vulkan/vulkan.h;
//this is necessary to use function definitions from volk.
#include <vulkan/vulkan.h>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

//#define VK_NO_PROTOTYPES // for volk
//#define VOLK_IMPLEMENTATION

//#include "volk.h"
#include <assert.h>
#include <iostream>
#include <format>
#include <vector>
#include <algorithm>
#include <iterator>
#include <numeric>
#include <array>
#include <filesystem> // for shader




#define LOG_TAG "simpleandroidvk"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define ASSERT(expr, message) \
    {                         \
        void(message);        \
        assert(expr);         \
    }

#define VK_CHECK(x)                           \
  do {                                        \
    VkResult err = x;                         \
    if (err) {                                \
      LOGE("Detected Vulkan error: %d", err); \
      abort();                                \
    }                                         \
  } while (0)

// functor for custom deleter for unique_ptr
struct AndroidNativeWindowDeleter {
    void operator()(ANativeWindow *window) { ANativeWindow_release(window); }
};

class VkApplication {
public:
    void initVulkan();
    void reset(ANativeWindow *newWindow, AAssetManager *newManager);
    inline bool isInitialized() const {
        return _initialized;
    }

private:

    void createInstance();
    void createSurface();
    void selectPhysicalDevice();
    void queryPhysicalDeviceCaps();
    bool checkValidationLayerSupport();

    bool _initialized{false};
    bool _enableValidationLayers{true};
    const std::vector<const char *> _validationLayers = {
            "VK_LAYER_KHRONOS_validation"};

//    const std::vector<const char *> _deviceExtensions = {
//            VK_KHR_SWAPCHAIN_EXTENSION_NAME};
//
    // android specific
    std::unique_ptr<ANativeWindow, AndroidNativeWindowDeleter> _osWindow;
    AAssetManager *_assetManager;

    VkInstance _instance{VK_NULL_HANDLE};
    VkSurfaceKHR _surface{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkPhysicalDevice _selectedPhysicalDevice{VK_NULL_HANDLE};
};

