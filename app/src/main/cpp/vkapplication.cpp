#include <format>
#include <vkapplication.h>
//#define VK_NO_PROTOTYPES // for volk
//#define VOLK_IMPLEMENTATION
//#include "volk.h"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

void VkApplication::initVulkan() {
    LOGI("initVulkan");
    //VK_CHECK(volkInitialize());
    createInstance();
    createSurface();
    selectPhysicalDevice();
    queryPhysicalDeviceCaps();
    _initialized = true;
}

static VKAPI_ATTR VkBool32

VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void * /* pUserData */) {
    LOGE("MessageID: %s %d \nMessage: %s\n\n", pCallbackData->pMessageIdName,
         pCallbackData->messageIdNumber, pCallbackData->pMessage);
    return VK_FALSE;
}

void VkApplication::reset(ANativeWindow *osWindow, AAssetManager *assetManager) {
    _osWindow.reset(osWindow);
    _assetManager = assetManager;
    if (_initialized) {
        // window properties: size/format changed
        createSurface();
//        recreateSwapChain();
    }
}

void VkApplication::createInstance() {
    LOGI("createInstance");
    assert(!_enableValidationLayers ||
           checkValidationLayerSupport());  // validation layers requested, but
    // not available!
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                           extensions.data());
    LOGI("available extensions");
    for (const auto &extension: extensions) {
        LOGI("\t %s", extension.extensionName);
    }

    // 6. VkApplicationInfo
    VkApplicationInfo applicationInfo;
    {
        applicationInfo = {
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName = "TestVulkan",
                .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                .apiVersion = VK_API_VERSION_1_3,
        };
    }

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo;
    {
        messengerInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .flags = 0,
                .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = &debugCallback,
                .pUserData = nullptr,
        };
    }

//    // 4. VkLayerSettingsCreateInfoEXT - Specify layer capabilities for a Vulkan instance
//    VkLayerSettingsCreateInfoEXT layer_settings_create_info;
//    {
//        const std::string layer_name = "VK_LAYER_KHRONOS_validation";
//        const std::array<const char *, 1> setting_debug_action = {"VK_DBG_LAYER_ACTION_BREAK"};
//        const std::array<const char *, 1> setting_gpu_based_action = {
//                "GPU_BASED_DEBUG_PRINTF"};
//        const std::array<VkBool32, 1> setting_printf_to_stdout = {VK_TRUE};
//        const std::array<VkBool32, 1> setting_printf_verbose = {VK_TRUE};
//        const std::array<VkLayerSettingEXT, 4> settings = {
//                VkLayerSettingEXT{
//                        .pLayerName = layer_name.c_str(),
//                        .pSettingName = "debug_action",
//                        .type = VK_LAYER_SETTING_TYPE_STRING_EXT,
//                        .valueCount = 1,
//                        .pValues = setting_debug_action.data(),
//                },
//                VkLayerSettingEXT{
//                        .pLayerName = layer_name.c_str(),
//                        .pSettingName = "validate_gpu_based",
//                        .type = VK_LAYER_SETTING_TYPE_STRING_EXT,
//                        .valueCount = 1,
//                        .pValues = setting_gpu_based_action.data(),
//                },
//                VkLayerSettingEXT{
//                        .pLayerName = layer_name.c_str(),
//                        .pSettingName = "printf_to_stdout",
//                        .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
//                        .valueCount = 1,
//                        .pValues = setting_printf_to_stdout.data(),
//                },
//                VkLayerSettingEXT{
//                        .pLayerName = layer_name.c_str(),
//                        .pSettingName = "printf_verbose",
//                        .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
//                        .valueCount = 1,
//                        .pValues = setting_printf_verbose.data(),
//                },
//        };
//
//        layer_settings_create_info = {
//                .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
//                .pNext = &messengerInfo,
//                .settingCount = static_cast<uint32_t>(settings.size()),
//                .pSettings = settings.data(),
//        };
//    }

// 5. VkValidationFeaturesEXT - Specify validation features to enable or disable for a Vulkan instance
    VkValidationFeaturesEXT validationFeatures;
    {
        // VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT: specifies that the layers will process debugPrintfEXT operations in shaders and send the resulting output to the debug callback.
        // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT: specifies that GPU-assisted validation is enabled. Activating this feature instruments shader programs to generate additional diagnostic data. This feature is disabled by default
        std::vector<VkValidationFeatureEnableEXT> validationFeaturesEnabled{
                VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT};
        // linked list
        validationFeatures = {
                .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                .pNext = &messengerInfo,
                .enabledValidationFeatureCount = static_cast<uint32_t>(validationFeaturesEnabled.size()),
                .pEnabledValidationFeatures = validationFeaturesEnabled.data(),
        };
    }

    // debug report extension is deprecated
    // The window must have been created with the SDL_WINDOW_VULKAN flag and instance must have been created
    // with extensions returned by SDL_Vulkan_GetInstanceExtensions() enabled.
    std::vector<const char *> instanceExtensions{
            VK_KHR_SURFACE_EXTENSION_NAME,
            "VK_KHR_android_surface",
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            // VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = (uint32_t) instanceExtensions.size();
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();

    if (_enableValidationLayers) {
        createInfo.enabledLayerCount =
                static_cast<uint32_t>(_validationLayers.size());
        createInfo.ppEnabledLayerNames = _validationLayers.data();
        createInfo.pNext = &validationFeatures;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &_instance));

    // 8. Create Debug Utils Messenger
//    {
//        VK_CHECK(vkCreateDebugUtilsMessengerEXT(_instance, &messengerInfo, nullptr,
//                                                &_debugMessenger));
//        ASSERT(_debugMessenger!= VK_NULL_HANDLE, "Error creating DebugUtilsMessenger");
//    }
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
                _instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            VK_CHECK(func(_instance, &messengerInfo, nullptr,
                          &_debugMessenger));
            ASSERT(_debugMessenger != VK_NULL_HANDLE, "Error creating DebugUtilsMessenger");
        } else {
            ASSERT(false, "vkCreateDebugUtilsMessengerEXT does not exist");
        }
    }
};

void VkApplication::createSurface() {
    ASSERT(_osWindow, "_osWindow is needed to create os surface");
    const VkAndroidSurfaceCreateInfoKHR create_info
            {
                    .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .window = _osWindow.get()};

    VK_CHECK(vkCreateAndroidSurfaceKHR(_instance, &create_info, nullptr, &_surface));
}

void VkApplication::selectPhysicalDevice() {
    // 10. Select Physical Device based on surface
    // family queue of discrete gpu support the surface of native window
    uint32_t familyIndexSupportSurface = std::numeric_limits<uint32_t>::max();
    {
        //  {VK_KHR_SWAPCHAIN_EXTENSION_NAME},  // physical device extensions
        uint32_t physicalDeviceCount{0};
        VK_CHECK(vkEnumeratePhysicalDevices(_instance, &physicalDeviceCount, nullptr));
        ASSERT(physicalDeviceCount > 0, "No Vulkan Physical Devices found");
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        VK_CHECK(vkEnumeratePhysicalDevices(_instance, &physicalDeviceCount,
                                            physicalDevices.data()));
        LOGI("Found %d  Vulkan capable device(s)", physicalDeviceCount);

        // select physical gpu
        VkPhysicalDevice discrete_gpu = VK_NULL_HANDLE;
        VkPhysicalDevice integrated_gpu = VK_NULL_HANDLE;

        VkPhysicalDeviceProperties prop;
        for (uint32_t i = 0; i < physicalDeviceCount; ++i) {
            VkPhysicalDevice physicalDevice = physicalDevices[i];
            vkGetPhysicalDeviceProperties(physicalDevice, &prop);

            if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                         nullptr);

                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                         queueFamilies.data());

                VkBool32 surfaceSupported;
                for (uint32_t familyIndex = 0; familyIndex < queueFamilyCount; ++familyIndex) {
                    const VkQueueFamilyProperties &queueFamilyProp = queueFamilies[familyIndex];
                    // graphics or GPGPU family queue
                    if (queueFamilyProp.queueCount > 0 && (queueFamilyProp.queueFlags &
                                                           (VK_QUEUE_GRAPHICS_BIT |
                                                            VK_QUEUE_COMPUTE_BIT))) {
                        vkGetPhysicalDeviceSurfaceSupportKHR(
                                physicalDevice,
                                familyIndex,
                                _surface,
                                &surfaceSupported);

                        if (surfaceSupported) {
                            familyIndexSupportSurface = familyIndex;
                            discrete_gpu = physicalDevice;
                            break;
                        }
                    }
                }
                continue;
            }

            if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                         nullptr);

                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                         queueFamilies.data());

                VkBool32 surfaceSupported;
                for (uint32_t familyIndex = 0; familyIndex < queueFamilyCount; ++familyIndex) {
                    const VkQueueFamilyProperties &queueFamilyProp = queueFamilies[familyIndex];
                    // graphics or GPGPU family queue
                    if (queueFamilyProp.queueCount > 0 && (queueFamilyProp.queueFlags &
                                                           (VK_QUEUE_GRAPHICS_BIT |
                                                            VK_QUEUE_COMPUTE_BIT))) {
                        vkGetPhysicalDeviceSurfaceSupportKHR(
                                physicalDevice,
                                familyIndex,
                                _surface,
                                &surfaceSupported);

                        if (surfaceSupported) {
                            familyIndexSupportSurface = familyIndex;
                            integrated_gpu = physicalDevice;

                            break;
                        }
                    }
                }
                continue;
            }
        }

        _selectedPhysicalDevice = discrete_gpu ? discrete_gpu : integrated_gpu;
        ASSERT(_selectedPhysicalDevice, "No Vulkan Physical Devices found");
        ASSERT(familyIndexSupportSurface != std::numeric_limits<uint32_t>::max(),
               "No Queue Family Index supporting surface found");
    }
}

void VkApplication::queryPhysicalDeviceCaps() {
    // 11. Query and Logging physical device (if some feature not supported by the physical device,
    // then we cannot enable them when we create the logic device later on)
    bool bindlessSupported = false;
    {
        // check if the descriptor index(bindless) is supported
        // Query bindless extension, called Descriptor Indexing (https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_EXT_descriptor_indexing.html)
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, nullptr};
        VkPhysicalDeviceFeatures2 deviceFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                                 &indexingFeatures};
        vkGetPhysicalDeviceFeatures2(_selectedPhysicalDevice, &deviceFeatures);
        bindlessSupported = indexingFeatures.descriptorBindingPartiallyBound &&
                            indexingFeatures.runtimeDescriptorArray;
        ASSERT(bindlessSupported, "Bindless is not supported");

        // Properties V1
        VkPhysicalDeviceProperties physicalDevicesProp1;
        vkGetPhysicalDeviceProperties(_selectedPhysicalDevice, &physicalDevicesProp1);
        // query device limits
        // timestampPeriod is the number of nanoseconds required for a timestamp query to be incremented by 1.
        // See Timestamp Queries.
        // auto gpu_timestamp_frequency = physicalDevicesProp1.limits.timestampPeriod / (1000 * 1000);
        // auto s_ubo_alignment = vulkan_physical_properties.limits.minUniformBufferOffsetAlignment;
        // auto s_ssbo_alignemnt = vulkan_physical_properties.limits.minStorageBufferOffsetAlignment;

        LOGI("GPU Used: %s, Vendor: %d, Device: %d, apiVersion:%d.%d.%d.%d",
             physicalDevicesProp1.deviceName,
             physicalDevicesProp1.vendorID,
             physicalDevicesProp1.deviceID,
             VK_API_VERSION_MAJOR(physicalDevicesProp1.apiVersion),
             VK_API_VERSION_MINOR(physicalDevicesProp1.apiVersion),
             VK_API_VERSION_PATCH(physicalDevicesProp1.apiVersion),
             VK_API_VERSION_VARIANT(physicalDevicesProp1.apiVersion));

        // If the VkPhysicalDeviceSubgroupProperties structure is included in the pNext chain of the VkPhysicalDeviceProperties2 structure passed to vkGetPhysicalDeviceProperties2,
        // it is filled in with each corresponding implementation-dependent property.
        // linked-list
        VkPhysicalDeviceSubgroupProperties subgroupProp{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        subgroupProp.pNext = NULL;
        VkPhysicalDeviceProperties2 physicalDevicesProp{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        physicalDevicesProp.pNext = &subgroupProp;
        vkGetPhysicalDeviceProperties2(_selectedPhysicalDevice, &physicalDevicesProp);

        // Get memory properties
        VkPhysicalDeviceMemoryProperties2 memoryProperties_ = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        };
        vkGetPhysicalDeviceMemoryProperties2(_selectedPhysicalDevice, &memoryProperties_);

        // extensions for this selected device
        uint32_t extensionPropertyCount{0};
        VK_CHECK(vkEnumerateDeviceExtensionProperties(_selectedPhysicalDevice, nullptr,
                                                      &extensionPropertyCount, nullptr));
        std::vector<VkExtensionProperties> extensionProperties(extensionPropertyCount);
        VK_CHECK(vkEnumerateDeviceExtensionProperties(_selectedPhysicalDevice, nullptr,
                                                      &extensionPropertyCount,
                                                      extensionProperties.data()));
        // convert to c++ string
        std::vector<std::string> extensions;
        std::transform(extensionProperties.begin(), extensionProperties.end(),
                       std::back_inserter(extensions),
                       [](const VkExtensionProperties &property) {
                           return std::string(property.extensionName);
                       });
        LOGI("physical device extensions: ");
        for(const auto& ext : extensions) {
            LOGI("%s", ext.c_str());
        }
    }
}

bool VkApplication::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName: _validationLayers) {
        bool layerFound = false;
        for (const auto &layerProperties: availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }
    return true;
}