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
#include <numeric>

// #define VK_NO_PROTOTYPES // for volk
//#define VOLK_IMPLEMENTATION
//#include "volk.h"
//To do it properly:
//
//Include "vk_mem_alloc.h" file in each CPP file where you want to use the library. This includes declarations of all members of the library.
//In exactly one CPP file define following macro before this include. It enables also internal definitions.

#include <vk_mem_alloc.h>
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
    template<typename T>
    void setCorrlationId(T handle, VkObjectType type, const std::string &name) {
        const VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = type,
                .objectHandle = reinterpret_cast<uint64_t>(handle),
                .pObjectName = name.c_str(),
        };
        auto func = (PFN_vkSetDebugUtilsObjectNameEXT) vkGetInstanceProcAddr(
                _instance, "vkSetDebugUtilsObjectNameEXT");
        if (func != nullptr) {
            VK_CHECK(func(_logicalDevice, &objectNameInfo));
            ASSERT(_debugMessenger != VK_NULL_HANDLE, "Error creating DebugUtilsMessenger");
        } else {
            ASSERT(false, "vkSetDebugUtilsObjectNameEXT does not exist");
        }
    }

    void createInstance();

    void createSurface();

    void selectPhysicalDevice();

    void queryPhysicalDeviceCaps();

    void selectQueueFamily();

    void createLogicDevice();

    void cacheCommandQueue();

    void createVMA();

    // only depends on vk surface, one time deal
    void prepareSwapChainCreation();

    // called every resize();
    void createSwapChain();

    void createSwapChainImageViews();

    void createSwapChainRenderPass();

    bool checkValidationLayerSupport();

    bool _initialized{false};
    bool _enableValidationLayers{true};
    const std::vector<const char *> _validationLayers = {
            "VK_LAYER_KHRONOS_validation"};

    const std::vector<const char *> _deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
//            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
//            VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
//            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
//            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
//            VK_NV_MESH_SHADER_EXTENSION_NAME,            // mesh_shaders_extension_present
//            VK_KHR_MULTIVIEW_EXTENSION_NAME,             // multiview_extension_present
//            VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, // fragment_shading_rate_present
//            VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
//            VK_KHR_MAINTENANCE2_EXTENSION_NAME,
//            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, // ray_tracing_present
//            VK_KHR_SPIRV_1_4_EXTENSION_NAME,
//            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
//            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
//            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
//            VK_KHR_RAY_QUERY_EXTENSION_NAME, // ray query
//            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
//            VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
    };

    // android specific
    std::unique_ptr<ANativeWindow, AndroidNativeWindowDeleter> _osWindow;
    AAssetManager *_assetManager;

    VkInstance _instance{VK_NULL_HANDLE};
    VkSurfaceKHR _surface{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkPhysicalDevice _selectedPhysicalDevice{VK_NULL_HANDLE};
    VkDevice _logicalDevice{VK_NULL_HANDLE};
    bool _bindlessSupported{false};
    bool _protectedMemory{false};

    uint32_t _graphicsComputeQueueFamilyIndex{std::numeric_limits<uint32_t>::max()};
    uint32_t _computeQueueFamilyIndex{std::numeric_limits<uint32_t>::max()};
    uint32_t _transferQueueFamilyIndex{std::numeric_limits<uint32_t>::max()};
    // family queue of discrete gpu support the surface of native window
    uint32_t _presentQueueFamilyIndex{std::numeric_limits<uint32_t>::max()};

    // each queue family have many queues (16)
    // I choose 0 as graphics, 1 as compute
    uint32_t _graphicsQueueIndex{std::numeric_limits<uint32_t>::max()};
    uint32_t _computeQueueIndex{std::numeric_limits<uint32_t>::max()};

    VkQueue _graphicsQueue{VK_NULL_HANDLE};
    VkQueue _computeQueue{VK_NULL_HANDLE};
    VkQueue _transferQueue{VK_NULL_HANDLE};
    VkQueue _presentationQueue{VK_NULL_HANDLE};
    VkQueue _sparseQueues{VK_NULL_HANDLE};

//    std::vector<VkQueue> _graphicsQueues;
//    std::vector<VkQueue> _computeQueues;
//    std::vector<VkQueue> _transferQueues;
//    std::vector<VkQueue> _sparseQueues;

    VmaAllocator _vmaAllocator{VK_NULL_HANDLE};

    VkExtent2D _swapChainExtent;
    const uint32_t _swapChainImageCount{3};

    //constexpr VkFormat _swapChainFormat{VK_FORMAT_B8G8R8A8_UNORM};
    // gamma correction.
    // Using a swapchain with VK_FORMAT_B8G8R8A8_SRGB
    // leverages the ability to to apply gamma correction as the final step in your render pipeline
    // If your swapchain does the gamma correction, you do not need todo it in your shaders
    const VkFormat _swapChainFormat{VK_FORMAT_R8G8B8A8_SRGB};
    const VkColorSpaceKHR _colorspace{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkSurfaceTransformFlagBitsKHR _pretransformFlag;
    VkSwapchainKHR _swapChain{VK_NULL_HANDLE};
    std::vector<VkImageView> _swapChainImageViews;

    VkRenderPass _swapChainRenderPass{VK_NULL_HANDLE};
};

