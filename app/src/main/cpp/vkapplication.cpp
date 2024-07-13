#include <format>
#include <vkapplication.h>
//#define VK_NO_PROTOTYPES // for volk
//#define VOLK_IMPLEMENTATION
//#include "volk.h"

#define VMA_IMPLEMENTATION

#include <vk_mem_alloc.h>

#include <misc.h>
#include <ktx.h>
#include <ktxvulkan.h>


// triple-buffer
static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
static constexpr int MAX_DESCRIPTOR_SETS = 1 * MAX_FRAMES_IN_FLIGHT;

// Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000

void VkApplication::initVulkan() {
    LOGI("initVulkan");
    //VK_CHECK(volkInitialize());
    // vulkan boilerplate code
    createInstance();
    createSurface();
    selectPhysicalDevice();
    queryPhysicalDeviceCaps();
    selectQueueFamily();
    selectFeatures();
    createLogicDevice();
    cacheCommandQueue();
    createVMA();
    prepareSwapChainCreation();
    createSwapChain();
    createSwapChainImageViews();
    createSwapChainRenderPass();
    createDescriptorSetLayout();
    createDescriptorPool();
    allocateDescriptorSets();
    createUniformBuffers();

    // application logic
    createGraphicsPipeline();
    createSwapChainFramebuffers();
    createCommandPool();
    createCommandBuffer();
    createPerFrameSyncObjects();
    loadVao();
    // must prior to bindResourceToDescriptorSets due to imageView
    loadTextures();
    bindResourceToDescriptorSets();

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
        recreateSwapChain();
    }
}

void VkApplication::teardown() {
    vkDeviceWaitIdle(_logicalDevice);
    deleteSwapChain();

    // texture
    vkDestroyImageView(_logicalDevice, _imageView, nullptr);
    vkDestroyImage(_logicalDevice, _image, nullptr);
    vkDestroySampler(_logicalDevice, _sampler, nullptr);
    vmaFreeMemory(_vmaAllocator, _vmaImageAllocation);

    // shader data
    vkDestroyDescriptorPool(_logicalDevice, _descriptorSetPool, nullptr);
    vkDestroyDescriptorSetLayout(_logicalDevice, _descriptorSetLayout, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // vma buffer
        // unmap a buffer not mapped will crash
        // vmaUnmapMemory(_vmaAllocator, _vmaAllocations[i]);
        vmaDestroyBuffer(_vmaAllocator, _uniformBuffers[i], _vmaAllocations[i]);
        // sync
        vkDestroySemaphore(_logicalDevice, _imageCanAcquireSemaphores[i], nullptr);
        vkDestroySemaphore(_logicalDevice, _imageRendereredSemaphores[i], nullptr);
        vkDestroyFence(_logicalDevice, _inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(_logicalDevice, _commandPool, nullptr);
    vkDestroyPipeline(_logicalDevice, _graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(_logicalDevice, _pipelineLayout, nullptr);
    vkDestroyRenderPass(_logicalDevice, _swapChainRenderPass, nullptr);

    vmaDestroyAllocator(_vmaAllocator);

    vkDestroyDevice(_logicalDevice, nullptr);
//    vkDestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
                _instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(_instance, _debugMessenger, nullptr);
        } else {
            ASSERT(false, "vkDestroyDebugUtilsMessengerEXT does not exist");
        }
    }
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vkDestroyInstance(_instance, nullptr);
    _initialized = false;
}

void VkApplication::renderPerFrame() {
    // no timeout set
    VK_CHECK(vkWaitForFences(_logicalDevice, 1, &_inFlightFences[_currentFrameId], VK_TRUE,
                             UINT64_MAX));
    //VK_CHECK(vkResetFences(device_, 1, &acquireFence_));
    uint32_t swapChainImageIndex;
    VkResult result = vkAcquireNextImageKHR(
            _logicalDevice, _swapChain, UINT64_MAX, _imageCanAcquireSemaphores[_currentFrameId],
            VK_NULL_HANDLE, &swapChainImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    assert(result == VK_SUCCESS ||
           result == VK_SUBOPTIMAL_KHR);  // failed to acquire swap chain image
    updateUniformBuffer(_currentFrameId);

    // vkWaitForFences and reset pattern
    VK_CHECK(vkResetFences(_logicalDevice, 1, &_inFlightFences[_currentFrameId]));
    // vkWaitForFences ensure the previous command is submitted from the host, now it can be modified.
    VK_CHECK(vkResetCommandBuffer(_commandBuffers[_currentFrameId], 0));

    recordCommandBuffer(_commandBuffers[_currentFrameId], swapChainImageIndex);
    // submit command
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {_imageCanAcquireSemaphores[_currentFrameId]};
    // pipeline stages
    // specifies the stage of the pipeline after blending where the final color values are output from the pipeline
    // basically wait for the previous rendering finished
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBuffers[_currentFrameId];
    // signal semaphore
    VkSemaphore signalRenderedSemaphores[] = {_imageRendereredSemaphores[_currentFrameId]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalRenderedSemaphores;
    // signal fence
    VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _inFlightFences[_currentFrameId]));

    // present after rendering is done
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalRenderedSemaphores;

    VkSwapchainKHR swapChains[] = {_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &swapChainImageIndex;
    presentInfo.pResults = nullptr;
    VK_CHECK(vkQueuePresentKHR(_presentationQueue, &presentInfo));

    _currentFrameId = (_currentFrameId + 1) % MAX_FRAMES_IN_FLIGHT;
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
                            _presentQueueFamilyIndex = familyIndex;
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
                            _presentQueueFamilyIndex = familyIndex;
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
        ASSERT(_presentQueueFamilyIndex != std::numeric_limits<uint32_t>::max(),
               "No Queue Family Index supporting surface found");
    }
}

void VkApplication::queryPhysicalDeviceCaps() {
    // 11. Query and Logging physical device (if some feature not supported by the physical device,
    // then we cannot enable them when we create the logic device later on)
    {
        // check if the descriptor index(bindless) is supported
        // Query bindless extension, called Descriptor Indexing (https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_EXT_descriptor_indexing.html)
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, nullptr};
        VkPhysicalDeviceFeatures2 deviceFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                                 &indexingFeatures};
        vkGetPhysicalDeviceFeatures2(_selectedPhysicalDevice, &deviceFeatures);
        _bindlessSupported = indexingFeatures.descriptorBindingPartiallyBound &&
                             indexingFeatures.runtimeDescriptorArray;
        ASSERT(_bindlessSupported, "Bindless is not supported");

        // Properties V1
        vkGetPhysicalDeviceProperties(_selectedPhysicalDevice, &_physicalDevicesProp1);
        // query device limits
        // timestampPeriod is the number of nanoseconds required for a timestamp query to be incremented by 1.
        // See Timestamp Queries.
        // auto gpu_timestamp_frequency = physicalDevicesProp1.limits.timestampPeriod / (1000 * 1000);
        // auto s_ubo_alignment = vulkan_physical_properties.limits.minUniformBufferOffsetAlignment;
        // auto s_ssbo_alignemnt = vulkan_physical_properties.limits.minStorageBufferOffsetAlignment;

        LOGI("GPU Used: %s, Vendor: %d, Device: %d, apiVersion:%d.%d.%d.%d",
             _physicalDevicesProp1.deviceName,
             _physicalDevicesProp1.vendorID,
             _physicalDevicesProp1.deviceID,
             VK_API_VERSION_MAJOR(_physicalDevicesProp1.apiVersion),
             VK_API_VERSION_MINOR(_physicalDevicesProp1.apiVersion),
             VK_API_VERSION_PATCH(_physicalDevicesProp1.apiVersion),
             VK_API_VERSION_VARIANT(_physicalDevicesProp1.apiVersion));

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
        for (const auto &ext: extensions) {
            LOGI("%s", ext.c_str());
        }
    }
}

void VkApplication::selectQueueFamily() {
    // 12. Query the selected device to cache the device queue family
    // 1th of main family or 0th of only compute family

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_selectedPhysicalDevice, &queueFamilyCount,
                                             nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(_selectedPhysicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        auto &queueFamily = queueFamilies[i];
        if (queueFamily.queueCount == 0) {
            continue;
        }

        LOGI("Queue Family Index %d, flags %d, queue count %d",
             i,
             queueFamily.queueFlags,
             queueFamily.queueCount);
        // |: means or, both graphics and compute
        if ((queueFamily.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            // Sparse memory bindings execute on a queue that includes the VK_QUEUE_SPARSE_BINDING_BIT bit
            // While some implementations may include VK_QUEUE_SPARSE_BINDING_BIT support in queue families that also include graphics and compute support
            ASSERT((queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) ==
                   VK_QUEUE_SPARSE_BINDING_BIT, "Sparse memory bindings is not supported");
            _graphicsComputeQueueFamilyIndex = i;
            _graphicsQueueIndex = 0;
            // separate graphics and compute queue
            if (queueFamily.queueCount > 1) {
                _computeQueueFamilyIndex = i;
                _computeQueueIndex = 1;
            }
            continue;
        }
        // compute only
        if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            _computeQueueIndex == std::numeric_limits<uint32_t>::max()) {
            _computeQueueFamilyIndex = i;
            _computeQueueIndex = 0;
        }
        if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0 &&
            (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)) {
            _transferQueueFamilyIndex = i;
            continue;
        }
    }
}

void VkApplication::selectFeatures() {
    // query all features through single linked list
    // physicalFeatures2 --> indexing_features --> dynamicRenderingFeatures --> nullptr;
    vkGetPhysicalDeviceFeatures2(_selectedPhysicalDevice, &_physicalFeatures2);

    // enable features
    VkPhysicalDeviceFeatures physicalDeviceFeatures{
            .independentBlend = VK_TRUE,
            .vertexPipelineStoresAndAtomics = VK_TRUE,
            .fragmentStoresAndAtomics = VK_TRUE,
    };
    VkPhysicalDeviceVulkan11Features enable11Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    };

    VkPhysicalDeviceVulkan12Features enable12Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    };

    VkPhysicalDeviceVulkan13Features enable13Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT fragmentDensityMapFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT,
    };

    VkPhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM fragmentDensityMapOffsetFeatures{
            .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_QCOM,
    };

    // for ray-tracing
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
    };

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
    };

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    };
    // enable features for logical device
    // default
    // do we need these for defaults?
//    enable12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
//    enable12Features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
////    enable12Features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE; // cautious
//    enable12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
//    enable12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
//    enable12Features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
//    enable12Features.descriptorBindingPartiallyBound = VK_TRUE;
//    enable12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
//    enable12Features.descriptorIndexing = VK_TRUE;
//    enable12Features.runtimeDescriptorArray = VK_TRUE;
    // enable indirect rendering
    //  enable11Features.shaderDrawParameters = VK_TRUE;
//    enable12Features.drawIndirectCount = VK_TRUE;
//    physicalDeviceFeatures.multiDrawIndirect = VK_TRUE;
//    physicalDeviceFeatures.drawIndirectFirstInstance = VK_TRUE;
    // enable independent blending
    physicalDeviceFeatures.independentBlend = VK_TRUE;
    // enable only if physical device support it
    if (_vk11features.multiview) {
        enable11Features.multiview = VK_TRUE;
    }
    // enable16bitFloatFeature
//   // enable11Features.storageBuffer16BitAccess = VK_TRUE;
//    enable12Features.shaderFloat16 = VK_TRUE;
//    // scalar layout
//    enable12Features.scalarBlockLayout = VK_TRUE;
    // buffer device address feature
//    enable12Features.bufferDeviceAddress = VK_TRUE;
//    enable12Features.bufferDeviceAddressCaptureReplay = VK_TRUE;
    // bindless
    if (_bindlessSupported) {
//        enable12Features.descriptorBindingPartiallyBound = VK_TRUE;
//        enable12Features.runtimeDescriptorArray = VK_TRUE;
    }
    // dynamic rendering
    enable13Features.dynamicRendering = VK_TRUE;
    // maintainance 4
    enable13Features.maintenance4 = VK_TRUE;
    // sync2
    enable13Features.synchronization2 = VK_TRUE;
    if (_fragmentDensityMapFeature.fragmentDensityMap) {
        // enableFragmentDensityMapFeatures
        fragmentDensityMapFeatures.fragmentDensityMap = VK_TRUE;
    }
    if (_fragmentDensityMapOffsetFeature.fragmentDensityMapOffset) {
        // enableFragmentDensityMapFeatures
        fragmentDensityMapOffsetFeatures.fragmentDensityMapOffset = VK_TRUE;
    }

    // create the linkedlist for features
    _enabledDeviceFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &enable12Features,
            .features = physicalDeviceFeatures,
    };
    //  enable11Features.pNext = &enable12Features;
    enable12Features.pNext = nullptr;
//    enable12Features.pNext = &enable13Features;

    // enable ray tracing features
    //if(isRayTracingSupported()) {
    accelStructFeatures.accelerationStructure = VK_TRUE;
    rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    rayQueryFeatures.rayQuery = VK_TRUE;
    accelStructFeatures.pNext = &rayTracingPipelineFeatures;
    rayTracingPipelineFeatures.pNext = &rayQueryFeatures;
    // enable13Features.pNext = &accelStructFeatures;
    //}
    if (_fragmentDensityMapFeature.fragmentDensityMap) {

    }
    if (_fragmentDensityMapOffsetFeature.fragmentDensityMapOffset) {

    }

//    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{
//            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
//    physicalFeatures2.pNext = &dynamicRenderingFeatures;
//
//    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
//            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
//            &dynamicRenderingFeatures};
//
//    if (_bindlessSupported) {
//        indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
//        indexingFeatures.runtimeDescriptorArray = VK_TRUE;
//        indexingFeatures.pNext = &dynamicRenderingFeatures;
//        physicalFeatures2.pNext = &indexingFeatures;
//    }

//    if (_protectedMemory) {
//        VkPhysicalDeviceProtectedMemoryFeatures protectedMemoryFeatures{
//                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,
//                &indexingFeatures};
//    }
}

void VkApplication::createLogicDevice() {
    // enable 3 queue family for the logic device (compute/graphics/transfer)
    const float queuePriority[] = {1.0f, 1.0f};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;

    uint32_t queueCount = 0;
    VkDeviceQueueCreateInfo graphicsComputeQueue;
    graphicsComputeQueue.pNext = nullptr;
    graphicsComputeQueue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    // https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/protected.adoc
    //must enable physical device feature
    //graphicsComputeQueue.flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
    graphicsComputeQueue.flags = 0x0;
    graphicsComputeQueue.queueFamilyIndex = _graphicsComputeQueueFamilyIndex;
    // within that queuefamily:[0(graphics), 1(compute)];
    graphicsComputeQueue.queueCount = (_graphicsComputeQueueFamilyIndex == _computeQueueFamilyIndex
                                       ? 2 : 1);
    graphicsComputeQueue.pQueuePriorities = queuePriority;
    queueInfos.push_back(graphicsComputeQueue);
    // compute in different queueFamily
    if (_graphicsComputeQueueFamilyIndex != _computeQueueFamilyIndex) {
        VkDeviceQueueCreateInfo computeOnlyQueue;
        computeOnlyQueue.pNext = nullptr;
        computeOnlyQueue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        computeOnlyQueue.flags = 0x0;
        computeOnlyQueue.queueFamilyIndex = _computeQueueFamilyIndex;
        computeOnlyQueue.queueCount = 1;
        computeOnlyQueue.pQueuePriorities = queuePriority; // only the first float will be used (c-style)
        queueInfos.push_back(computeOnlyQueue);
    }

    if (_transferQueueFamilyIndex != std::numeric_limits<uint32_t>::max()) {
        VkDeviceQueueCreateInfo transferOnlyQueue;
        transferOnlyQueue.pNext = nullptr;
        transferOnlyQueue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        transferOnlyQueue.flags = 0x0;
        transferOnlyQueue.queueFamilyIndex = _transferQueueFamilyIndex;
        transferOnlyQueue.queueCount = 1;
        const float queuePriority[] = {1.0f};
        transferOnlyQueue.pQueuePriorities = queuePriority;
        // crash the logic device creation
        queueInfos.push_back(transferOnlyQueue);
    }


    // descriptor_indexing
    // Descriptor indexing is also known by the term "bindless",
    // https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html
    LOGI("%d", queueInfos[0].queueCount);
    //LOGI("%d", queueInfos[1].queueCount);
    VkDeviceCreateInfo logicDeviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    logicDeviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    logicDeviceCreateInfo.pQueueCreateInfos = queueInfos.data();
    logicDeviceCreateInfo.enabledExtensionCount = _deviceExtensions.size();
    logicDeviceCreateInfo.ppEnabledExtensionNames = _deviceExtensions.data();
    logicDeviceCreateInfo.enabledLayerCount =
            static_cast<uint32_t>(_validationLayers.size());
    logicDeviceCreateInfo.ppEnabledLayerNames = _validationLayers.data();
    logicDeviceCreateInfo.pNext = &_physicalFeatures2;

    VK_CHECK(
            vkCreateDevice(_selectedPhysicalDevice, &logicDeviceCreateInfo, nullptr,
                           &_logicalDevice));
    ASSERT(_logicalDevice, "Failed to create logic device");

    setCorrlationId(_instance, VK_OBJECT_TYPE_INSTANCE, "Instance: testVulkan");
    setCorrlationId(_logicalDevice, VK_OBJECT_TYPE_DEVICE, "Logic Device");
}

void VkApplication::cacheCommandQueue() {
    // 0th queue of that queue family is graphics
    vkGetDeviceQueue(_logicalDevice, _graphicsComputeQueueFamilyIndex, _graphicsQueueIndex,
                     &_graphicsQueue);
    vkGetDeviceQueue(_logicalDevice, _computeQueueFamilyIndex, _computeQueueIndex, &_computeQueue);

    // Get transfer queue if present
    if (_transferQueueFamilyIndex != std::numeric_limits<uint32_t>::max()) {
        vkGetDeviceQueue(_logicalDevice, _transferQueueFamilyIndex, 0, &_transferQueue);
    }

    // familyIndexSupportSurface
    vkGetDeviceQueue(_logicalDevice, _presentQueueFamilyIndex, 0, &_presentationQueue);
    vkGetDeviceQueue(_logicalDevice, _graphicsComputeQueueFamilyIndex, 0, &_sparseQueues);
    ASSERT(_graphicsQueue, "Failed to access graphics queue");
    ASSERT(_computeQueue, "Failed to access compute queue");
    ASSERT(_transferQueue, "Failed to access transfer queue");
    ASSERT(_presentationQueue, "Failed to access presentation queue");
    ASSERT(_sparseQueues, "Failed to access sparse queue");
}

void VkApplication::createVMA() {
    // https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator

    const VmaVulkanFunctions vulkanFunctions = {
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
            .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
            .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
            .vkAllocateMemory = vkAllocateMemory,
            .vkFreeMemory = vkFreeMemory,
            .vkMapMemory = vkMapMemory,
            .vkUnmapMemory = vkUnmapMemory,
            .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
            .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
            .vkBindBufferMemory = vkBindBufferMemory,
            .vkBindImageMemory = vkBindImageMemory,
            .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
            .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
            .vkCreateBuffer = vkCreateBuffer,
            .vkDestroyBuffer = vkDestroyBuffer,
            .vkCreateImage = vkCreateImage,
            .vkDestroyImage = vkDestroyImage,
            .vkCmdCopyBuffer = vkCmdCopyBuffer,
#if VMA_VULKAN_VERSION >= 1001000
            .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
            .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
            .vkBindBufferMemory2KHR = vkBindBufferMemory2,
            .vkBindImageMemory2KHR = vkBindImageMemory2,
            .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
#endif
#if VMA_VULKAN_VERSION >= 1003000
            .vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
            .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
#endif
    };

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = _selectedPhysicalDevice;
    allocatorInfo.device = _logicalDevice;
    allocatorInfo.instance = _instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    vmaCreateAllocator(&allocatorInfo, &_vmaAllocator);
    ASSERT(_vmaAllocator, "Failed to create vma allocator");
}

void VkApplication::prepareSwapChainCreation() {
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_selectedPhysicalDevice, _surface,
                                              &surfaceCapabilities);
    uint32_t width = surfaceCapabilities.currentExtent.width;
    uint32_t height = surfaceCapabilities.currentExtent.height;
    //surfaceCapabilities.supportedCompositeAlpha
    if (surfaceCapabilities.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
        surfaceCapabilities.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
        // Swap to get identity width and height
        surfaceCapabilities.currentExtent.height = width;
        surfaceCapabilities.currentExtent.width = height;
    }
    _swapChainExtent = surfaceCapabilities.currentExtent;
    _pretransformFlag = surfaceCapabilities.currentTransform;
    LOGI("Creating swapchain %d x %d, minImageCount: %d", _swapChainExtent.width,
         _swapChainExtent.height, surfaceCapabilities.minImageCount);

    uint32_t supportedSurfaceFormatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_selectedPhysicalDevice, _surface,
                                         &supportedSurfaceFormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> supportedFormats(supportedSurfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(_selectedPhysicalDevice, _surface,
                                         &supportedSurfaceFormatCount, supportedFormats.data());
    std::vector<VkFormat> formats;
    std::transform(std::begin(supportedFormats), std::end(supportedFormats),
                   std::back_inserter(formats),
                   [](const VkSurfaceFormatKHR &f) { return f.format; });


    LOGI("-->supported surface(swap chain) format");
//    for(const auto& format: supportedFormats) {
//        LOGI("%s %s", format.format, format.colorSpace);
//    }
    // std::copy(std::begin(formats), std::end(formats), std::ostream_iterator<VkFormat>(cout, "\n"));
    LOGI("<--supported surface(swap chain) format");



    // for gamma correction, non-linear color space
    ASSERT(supportedFormats.end() != std::find_if(supportedFormats.begin(), supportedFormats.end(),
                                                  [&](const VkSurfaceFormatKHR &format) {
                                                      return format.format == _swapChainFormat &&
                                                             format.colorSpace == _colorspace;
                                                  }), "swapChainFormat is not supported");
}

void VkApplication::createSwapChain() {
    const bool presentationQueueIsShared =
            _graphicsComputeQueueFamilyIndex == _presentQueueFamilyIndex;
    std::array<uint32_t, 2> familyIndices{_graphicsComputeQueueFamilyIndex,
                                          _presentQueueFamilyIndex};

    VkSwapchainCreateInfoKHR swapchain = {};
    swapchain.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain.surface = _surface;
    swapchain.minImageCount = _swapChainImageCount;
    swapchain.imageFormat = _swapChainFormat;
    swapchain.imageColorSpace = _colorspace;
    swapchain.imageExtent = _swapChainExtent;
    swapchain.clipped = VK_TRUE;
    swapchain.imageArrayLayers = 1;
    swapchain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // VK_SHARING_MODE_EXCLUSIVE,
    // VK_SHARING_MODE_CONCURRENT: concurrent access to any range or image subresource from multiple queue families
    swapchain.imageSharingMode = presentationQueueIsShared ? VK_SHARING_MODE_EXCLUSIVE
                                                           : VK_SHARING_MODE_CONCURRENT;
    swapchain.queueFamilyIndexCount = presentationQueueIsShared ? 0u : 2u;
    swapchain.pQueueFamilyIndices = presentationQueueIsShared ? nullptr : familyIndices.data();
    swapchain.preTransform = _pretransformFlag;
    // ignore alpha completely
    // not supported: VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR for this default swapchain surface
    swapchain.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    // VK_PRESENT_MODE_FIFO_KHR always supported
    // VK_PRESENT_MODE_FIFO_KHR = Hard Vsync
    // This is always supported on Android phones
    swapchain.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain.oldSwapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(_logicalDevice, &swapchain, nullptr, &_swapChain));
    setCorrlationId(_swapChain, VK_OBJECT_TYPE_SWAPCHAIN_KHR, "Swapchain");
}

void VkApplication::createSwapChainImageViews() {
    uint32_t imageCount{0};
    VK_CHECK(vkGetSwapchainImagesKHR(_logicalDevice, _swapChain, &imageCount, nullptr));
    std::vector<VkImage> images(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(_logicalDevice, _swapChain, &imageCount, images.data()));
    _swapChainImageViews.resize(imageCount);

    VkImageViewCreateInfo imageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    // VK_IMAGE_VIEW_TYPE_2D_ARRAY for image array
    imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageView.format = _swapChainFormat;
    // no mipmap
    imageView.subresourceRange.baseMipLevel = 0;
    imageView.subresourceRange.levelCount = 1;
    // no image array
    imageView.subresourceRange.baseArrayLayer = 0;
    imageView.subresourceRange.layerCount = 1;
    imageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//    imageView.components.r = VK_COMPONENT_SWIZZLE_R;
//    imageView.components.g = VK_COMPONENT_SWIZZLE_G;
//    imageView.components.b = VK_COMPONENT_SWIZZLE_B;
//    imageView.components.a = VK_COMPONENT_SWIZZLE_A;
    imageView.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageView.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageView.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageView.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    for (size_t i = 0; i < imageCount; ++i) {
        imageView.image = images[i];
        VK_CHECK(vkCreateImageView(_logicalDevice, &imageView, nullptr, &_swapChainImageViews[i]));
        setCorrlationId(_swapChainImageViews[i], VK_OBJECT_TYPE_IMAGE_VIEW,
                        "Swap Chain Image view: " + std::to_string(i));
    }
}

void VkApplication::createSwapChainRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = _swapChainFormat;
    // multi-samples here
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // like tree traversal, enter/exit the node
    // enter the renderpass: clear
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // leave the renderpass: store
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // swap chain is not used for stencil, don't care
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // swap chain is for presentation
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // VkAttachmentReference is for subpass, how subpass could refer to the color attachment
    // here only 1 color attachement, index is 0;
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    // for graphics presentation
    // no depth, stencil and multi-sampling
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // subpass dependencies
    // VK_SUBPASS_EXTERNAL means anything outside of a given render pass scope.
    // When used for srcSubpass it specifies anything that happened before the render pass.
    // And when used for dstSubpass it specifies anything that happens after the render pass.
    // It means that synchronization mechanisms need to include operations that happen before
    // or after the render pass.
    // It may be another render pass, but it also may be some other operations,
    // not necessarily render pass-related.

    // dstSubpass: 0
    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0] = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            // specifies all operations performed by all commands
            .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            // stage of the pipeline after blending
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // dependencies: memory read may collide with renderpass write
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
    };

    dependencies[1] = {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
    };

    VkAttachmentDescription attachments[] = {colorAttachment};
    VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    // here could set multi-view VkRenderPassMultiviewCreateInfo
    renderPassInfo.pNext = nullptr;
    renderPassInfo.attachmentCount = sizeof(attachments) / sizeof(attachments[0]);
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies.data();

    VK_CHECK(vkCreateRenderPass(_logicalDevice, &renderPassInfo, nullptr, &_swapChainRenderPass));
    setCorrlationId(_swapChainRenderPass, VK_OBJECT_TYPE_RENDER_PASS, "Render pass: SwapChain");
}

void VkApplication::recreateSwapChain() {
    // wait on the host for the completion of outstanding queue operations for all queues
    vkDeviceWaitIdle(_logicalDevice);
    deleteSwapChain();
    createSwapChain();
    createSwapChainImageViews();
    createSwapChainFramebuffers();
}

void VkApplication::deleteSwapChain() {
    for (size_t i = 0; i < _swapChainFramebuffers.size(); ++i) {
        vkDestroyFramebuffer(_logicalDevice, _swapChainFramebuffers[i], nullptr);
    }

    for (size_t i = 0; i < _swapChainImageViews.size(); i++) {
        vkDestroyImageView(_logicalDevice, _swapChainImageViews[i], nullptr);
    }
    // image is owned by swap chain
    vkDestroySwapchainKHR(_logicalDevice, _swapChain, nullptr);
}

// depends on shader
void VkApplication::createDescriptorSetLayout() {
    // one ubo in vs + one sampler2D in fs
    std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings(2);
    dsLayoutBindings[0].binding = 0; //depends on the shader: set 0, binding = 0
    dsLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // array resource
    dsLayoutBindings[0].descriptorCount = 1;
    dsLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    dsLayoutBindings[0].pImmutableSamplers = nullptr;

    dsLayoutBindings[1].binding = 1; //depends on the shader: set 0, binding = 1
    dsLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // array resource
    dsLayoutBindings[1].descriptorCount = 1;
    dsLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    dsLayoutBindings[1].pImmutableSamplers = nullptr;

    // the pipeline only has one shader data (ubo)
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = dsLayoutBindings.size();
    layoutInfo.pBindings = dsLayoutBindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(_logicalDevice, &layoutInfo, nullptr,
                                         &_descriptorSetLayout));
}

// depends on your glsl
void VkApplication::createDescriptorPool() {
    // here I  need 2 type of descriptor per frame
    // layout (set = 0, binding = 0) uniform UBO
    // layout (set = 0, binding = 1) uniform sampler2D samplerColor;
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes(2);
    // poolSize.descriptorCount = static_cast<uint32_t>(MAX_DESCRIPTOR_SETS);
    descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorPoolSizes[0].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT;
    descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorPoolSizes[1].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = descriptorPoolSizes.size();
    poolInfo.pPoolSizes = descriptorPoolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_DESCRIPTOR_SETS);

    VK_CHECK(vkCreateDescriptorPool(_logicalDevice, &poolInfo, nullptr, &_descriptorSetPool));
}

void VkApplication::allocateDescriptorSets() {
    // how many ds to allocate ?
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, _descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorSetPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    _descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    // VK_ERROR_OUT_OF_POOL_MEMORY_KHR = VK_ERROR_OUT_OF_POOL_MEMORY = -1000069000
    VK_CHECK(vkAllocateDescriptorSets(_logicalDevice, &allocInfo, _descriptorSets.data()));
}

// vma
void VkApplication::createPersistentBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        const std::string &name,
        VkBuffer &buffer,
        VmaAllocation &vmaAllocation,
        VmaAllocationInfo &vmaAllocationInfo
) {
    VkBufferCreateInfo bufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo vmaAllocationCreateInfo{
            .flags = 0,
            .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
            .requiredFlags = properties,
    };
    VK_CHECK(vmaCreateBuffer(_vmaAllocator, &bufferCreateInfo, &vmaAllocationCreateInfo, &buffer,
                             &vmaAllocation,
                             nullptr));
    vmaGetAllocationInfo(_vmaAllocator, vmaAllocation, &vmaAllocationInfo);
    setCorrlationId(buffer, VK_OBJECT_TYPE_BUFFER, "Persistent Buffer: " + name);
}

void VkApplication::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformDataDef1);
    _uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    _vmaAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    _vmaAllocationInfos.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createPersistentBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                               "Uniform buffer " + std::to_string(i),
                               _uniformBuffers[i],
                               _vmaAllocations[i],
                               _vmaAllocationInfos[i]);
    }
}

/*
 * getPrerotationMatrix handles screen rotation with 3 hardcoded rotation
 * matrices (detailed below). We skip the 180 degrees rotation.
 */
void getPrerotationMatrix(const VkSurfaceTransformFlagBitsKHR &pretransformFlag,
                          std::array<float, 16> &mat) {
    // mat is initialized to the identity matrix
    mat = {1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
    if (pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
        // mat is set to a 90 deg rotation matrix
        mat = {0., 1., 0., 0., -1., 0, 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
    } else if (pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
        // mat is set to 270 deg rotation matrix
        mat = {0., -1., 0., 0., 1., 0, 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
    }
}


void VkApplication::updateUniformBuffer(int currentFrameId) {
//    UniformDataDef0 ubo{};
//    getPrerotationMatrix(_pretransformFlag, ubo.mvp);

    auto view = _camera.viewTransformLH();
    auto persPrj = PerspectiveProjectionTransformLH(0.1f, 100.0f, 0.5f,
                                                    (float) _swapChainExtent.width /
                                                    (float) _swapChainExtent.height);


    mat4x4f identity(1.0f);
    auto mv = MatrixMultiply4x4(identity, view);
    auto vp = MatrixMultiply4x4(view, persPrj);
    auto mvp = MatrixMultiply4x4(identity, vp);

    UniformDataDef1 ubo;
    ubo.viewPos = _camera.viewPos();
    ubo.modelView = mv;
    ubo.projection = persPrj;
    ubo.mvp = mvp;

    void *mappedMemory{nullptr};
    VK_CHECK(vmaMapMemory(_vmaAllocator, _vmaAllocations[currentFrameId], &mappedMemory));
    memcpy(mappedMemory, &ubo, sizeof(UniformDataDef1));
    // memcpy(mappedMemory, &ubo, sizeof(ubo));
    vmaUnmapMemory(_vmaAllocator, _vmaAllocations[currentFrameId]);
}

// 1 ubo + 1 texture sampler
void VkApplication::bindResourceToDescriptorSets() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // for ubo
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = _uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformDataDef1);

        // for texture + sampler
        VkDescriptorImageInfo imageInfo{};
        // images are never directly accessed by the shader
        imageInfo.imageView = _imageView;
        imageInfo.sampler = _sampler;
        // The current usage of image: shader read
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // 0: ubo, 1: texture sampler
        std::vector<VkWriteDescriptorSet> descriptorWrite(2);
        descriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        // bind resource to this descriptor set
        descriptorWrite[0].dstSet = _descriptorSets[i];
        // layout(set = 0, binding = 0) matching glsl
        descriptorWrite[0].dstBinding = 0;
        descriptorWrite[0].dstArrayElement = 0;
        // only bind resource to 1 set which is uniform buffer
        // layout(binding = 0) uniform UniformBufferObject
        descriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite[0].descriptorCount = 1;
        // bind texture/image resource
        descriptorWrite[0].pImageInfo = nullptr;
        // for buffer type resource
        descriptorWrite[0].pBufferInfo = &bufferInfo;

        descriptorWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        // bind resource to this descriptor set
        descriptorWrite[1].dstSet = _descriptorSets[i];
        // layout(set = 0, binding = 0) matching glsl
        descriptorWrite[1].dstBinding = 1;
        descriptorWrite[1].dstArrayElement = 0;
        // only bind resource to 1 set which is uniform buffer
        // layout(binding = 0) uniform UniformBufferObject
        descriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite[1].descriptorCount = 1;
        // bind texture/image resource
        descriptorWrite[1].pImageInfo = &imageInfo;
        // for buffer type resource
        descriptorWrite[1].pBufferInfo = nullptr;
        vkUpdateDescriptorSets(_logicalDevice, descriptorWrite.size(), descriptorWrite.data(), 0,
                               nullptr);
    }
}

// load shader spirv
std::vector<uint8_t> LoadBinaryFile(const char *file_path,
                                    AAssetManager *assetManager) {
    std::vector<uint8_t> file_content;
    assert(assetManager);
    AAsset *file =
            AAssetManager_open(assetManager, file_path, AASSET_MODE_BUFFER);
    size_t file_length = AAsset_getLength(file);

    file_content.resize(file_length);

    AAsset_read(file, file_content.data(), file_length);
    AAsset_close(file);
    return file_content;
}

VkShaderModule createShaderModule(VkDevice logicalDevice, const std::vector<uint8_t> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(logicalDevice, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

void VkApplication::createGraphicsPipeline() {
    auto vertShaderCode =
            LoadBinaryFile("shaders/texture.vert.spv", _assetManager);
    auto fragShaderCode =
            LoadBinaryFile("shaders/texture.frag.spv", _assetManager);

    VkShaderModule vertShaderModule = createShaderModule(_logicalDevice, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(_logicalDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
    vertShaderStageInfo.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    fragShaderStageInfo.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // vao in opengl, this settings means no vao (vbo), create data in the vs directly


//    // without vao rendering. ex: ssbo + vs.
//    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
//    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
//    vertexInputInfo.vertexBindingDescriptionCount = 0;
//    vertexInputInfo.pVertexBindingDescriptions = nullptr;
//    vertexInputInfo.vertexAttributeDescriptionCount = 0;
//    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

// match location with shader
//    layout (location = 0) in vec3 inPos;
//    layout (location = 1) in vec2 inUV;
//    layout (location = 2) in vec3 inNormal;

    VkPipelineVertexInputStateCreateInfo vao{};
    std::vector<VkVertexInputBindingDescription> vertexInputBindings(1);
    vertexInputBindings[0].binding = 0;
    vertexInputBindings[0].stride = sizeof(VertexDef1);
    vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes(3);
    // pos
    vertexInputAttributes[0].location = 0;
    vertexInputAttributes[0].binding = 0;
    vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; //std::array<float, 3> pos
    vertexInputAttributes[0].offset = 0;
    // uv
    vertexInputAttributes[1].location = 1;
    vertexInputAttributes[1].binding = 0;
    vertexInputAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; //std::array<float, 2> uv;
    vertexInputAttributes[1].offset = 3 * sizeof(float);
    // normal
    vertexInputAttributes[2].location = 2;
    vertexInputAttributes[2].binding = 0;
    vertexInputAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributes[2].offset = 5 * sizeof(float);

    vao.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vao.vertexBindingDescriptionCount = vertexInputBindings.size();
    vao.pVertexBindingDescriptions = vertexInputBindings.data();
    vao.vertexAttributeDescriptionCount = vertexInputAttributes.size();
    vao.pVertexAttributeDescriptions = vertexInputAttributes.data();

    // topology of input data
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    // Pipeline Dynamic State for viewport and scissor, not setting it here
    // no hardcode the viewport/scissor options,
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;         // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;   // Optional
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // disable alpha blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1; // for MRT
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // only one set layout
//    // example of two setlayout will be:
//    layout(set = 0, binding = 0) uniform Transforms
//    layout(set = 1, binding = 0) uniform ObjectProperties

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    VK_CHECK(vkCreatePipelineLayout(_logicalDevice, &pipelineLayoutInfo, nullptr,
                                    &_pipelineLayout));

    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT,
                                                       VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vao;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = VK_NULL_HANDLE; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateCI;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.renderPass = _swapChainRenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;  // Optional
    pipelineInfo.basePipelineIndex = -1;              // Optional

    VK_CHECK(vkCreateGraphicsPipelines(_logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo,
                                       nullptr, &_graphicsPipeline));
    vkDestroyShaderModule(_logicalDevice, fragShaderModule, nullptr);
    vkDestroyShaderModule(_logicalDevice, vertShaderModule, nullptr);
}

void VkApplication::createSwapChainFramebuffers() {
    _swapChainFramebuffers.resize(_swapChainImageViews.size());
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = _swapChainRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.width = _swapChainExtent.width;
    framebufferInfo.height = _swapChainExtent.height;
    framebufferInfo.layers = 1;
    for (size_t i = 0; i < _swapChainImageViews.size(); i++) {
        VkImageView attachments[] = {_swapChainImageViews[i]};
        framebufferInfo.pAttachments = attachments;
        VK_CHECK(vkCreateFramebuffer(_logicalDevice, &framebufferInfo, nullptr,
                                     &_swapChainFramebuffers[i]));
    }
}

void VkApplication::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    // since we want to
    // record a command
    // buffer every
    // frame, so we want
    // to be able to
    // reset and record
    // over it
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = _graphicsComputeQueueFamilyIndex;
    VK_CHECK(vkCreateCommandPool(_logicalDevice, &poolInfo, nullptr, &_commandPool));
}

void VkApplication::createCommandBuffer() {
    _commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = _commandBuffers.size();
    VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &allocInfo, _commandBuffers.data()));
}

void
VkApplication::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t swapChainImageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    //  command buffer will be reset and recorded again between each submissio
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    // Begin Render Pass, only 1 render pass
    constexpr VkClearValue clearColor{0.0f, 0.0f, 0.0f, 0.0f};
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _swapChainRenderPass;
    // fbo corresponding to the swapchain image index
    renderPassInfo.framebuffer = _swapChainFramebuffers[swapChainImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapChainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Dynamic States (when create the graphics pipeline, they are not specified)
    VkViewport viewport{};
    viewport.width = (float) _swapChainExtent.width;
    viewport.height = (float) _swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = _swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);

    // apply graphics pipeline to the cmd
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipeline);
    // resource and ds to the shaders of this pipeline
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipelineLayout, 0, 1, &_descriptorSets[_currentFrameId],
                            0, nullptr);
    // for vao driven draw
    VkDeviceSize offsets[1] = {0};
    // it could bind multiple VBs, here only need 1.
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &_deviceVb, offsets);
    // no offset, start from zero
    vkCmdBindIndexBuffer(commandBuffer, _deviceIb, 0, VK_INDEX_TYPE_UINT32);

//    // submit draw call, data is generated in the vs
//    // for ssbo + vs, gpu-driven rendering
//    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    const uint32_t instanceCt = 1;
    const uint32_t firstIndex = 0;
    const uint32_t vertexOffset = 0;
    const uint32_t firstInstance = 0;
    vkCmdDrawIndexed(commandBuffer, _indexCount, instanceCt, firstIndex, vertexOffset,
                     firstInstance);
    vkCmdEndRenderPass(commandBuffer);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void VkApplication::createPerFrameSyncObjects() {
    _imageCanAcquireSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    _imageRendereredSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(_logicalDevice, &semaphoreInfo, nullptr,
                                   &_imageCanAcquireSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(_logicalDevice, &semaphoreInfo, nullptr,
                                   &_imageRendereredSemaphores[i]));
        VK_CHECK(vkCreateFence(_logicalDevice, &fenceInfo, nullptr, &_inFlightFences[i]));
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

// cull face be careful
void VkApplication::loadVao() {
    std::vector<VertexDef1> vertices = {
            {{1.0f,  -1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f,  1.0f,  0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{-1.0f, 1.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
//    std::vector<VertexDef1> vertices = {
//            {{10.0f,  10.0f,  0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
//            {{-10.0f, 10.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
//            {{-10.0f, -10.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
//            {{10.0f,  -10.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}
//    };
    std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

    _indexCount = indices.size();

    // vao
    // staging buffer:
    // 1. usage: VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    // 2. VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    VkBuffer stagingVb, stagingIb;
    const auto vbByteSize = vertices.size() * sizeof(VertexDef1);
    const auto ebByteSize = indices.size() * sizeof(uint32_t);
    {
        // create stagingVb
        VmaAllocation vmaStagingBufferAllocation{nullptr};
        VkBufferCreateInfo bufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = vbByteSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo stagingAllocationCreateInfo = {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_CPU_ONLY,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(_vmaAllocator, &bufferCreateInfo, &stagingAllocationCreateInfo,
                                 &stagingVb,
                                 &vmaStagingBufferAllocation, nullptr));
        void *mappedMemory{nullptr};
        VK_CHECK(vmaMapMemory(_vmaAllocator, vmaStagingBufferAllocation, &mappedMemory));
        memcpy(mappedMemory, vertices.data(), vbByteSize);
        vmaUnmapMemory(_vmaAllocator, vmaStagingBufferAllocation);
    }

    {
        // create stagingEb
        VmaAllocation vmaStagingBufferAllocation{nullptr};
        VkBufferCreateInfo bufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = ebByteSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo stagingAllocationCreateInfo = {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_CPU_ONLY,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(_vmaAllocator, &bufferCreateInfo, &stagingAllocationCreateInfo,
                                 &stagingIb,
                                 &vmaStagingBufferAllocation, nullptr));
        void *mappedMemory{nullptr};
        VK_CHECK(vmaMapMemory(_vmaAllocator, vmaStagingBufferAllocation, &mappedMemory));
        memcpy(mappedMemory, indices.data(), ebByteSize);
        vmaUnmapMemory(_vmaAllocator, vmaStagingBufferAllocation);
    }

    // device buffer:
    // 1. usage: VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    // 2. VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    {
        // create vbo
        VmaAllocation vmaDeviceBufferAllocation{nullptr};
        VkBufferCreateInfo bufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = vbByteSize,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo bufferAllocationCreateInfo = {
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                .preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(_vmaAllocator, &bufferCreateInfo, &bufferAllocationCreateInfo,
                                 &_deviceVb,
                                 &vmaDeviceBufferAllocation, nullptr));

        VkCommandBuffer uploadCmd;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &allocInfo, &uploadCmd));

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(uploadCmd, &cmdBufferBeginInfo));

        // src: bytesOffset, dst: bytesOffset
        VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = vbByteSize};
        vkCmdCopyBuffer(uploadCmd, stagingVb, _deviceVb, 1, &region);
        VK_CHECK(vkEndCommandBuffer(uploadCmd));

        VkFenceCreateInfo fenceInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
        };
        VkFence fence;
        VK_CHECK(vkCreateFence(_logicalDevice, &fenceInfo, nullptr, &fence));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadCmd;
        VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK(vkWaitForFences(_logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
        vkDestroyFence(_logicalDevice, fence, nullptr);
        vkFreeCommandBuffers(_logicalDevice, _commandPool, 1, &uploadCmd);

        vkDestroyBuffer(_logicalDevice, stagingVb, nullptr);
    }

    {
        // create ebo
        VmaAllocation vmaDeviceBufferAllocation{nullptr};
        VkBufferCreateInfo bufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = ebByteSize,
                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo bufferAllocationCreateInfo = {
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                .preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(_vmaAllocator, &bufferCreateInfo, &bufferAllocationCreateInfo,
                                 &_deviceIb,
                                 &vmaDeviceBufferAllocation, nullptr));

        VkCommandBuffer uploadCmd;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &allocInfo, &uploadCmd));

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(uploadCmd, &cmdBufferBeginInfo));

        // src: bytesOffset, dst: bytesOffset
        VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = ebByteSize};
        vkCmdCopyBuffer(uploadCmd, stagingIb, _deviceIb, 1, &region);
        VK_CHECK(vkEndCommandBuffer(uploadCmd));

        VkFenceCreateInfo fenceInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
        };
        VkFence fence;
        VK_CHECK(vkCreateFence(_logicalDevice, &fenceInfo, nullptr, &fence));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadCmd;
        VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK(vkWaitForFences(_logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
        vkDestroyFence(_logicalDevice, fence, nullptr);
        vkFreeCommandBuffers(_logicalDevice, _commandPool, 1, &uploadCmd);

        vkDestroyBuffer(_logicalDevice, stagingIb, nullptr);
    }
}

void VkApplication::loadTextures() {
    // std::string filename = getAssetPath() + "metalplate01_rgba.ktx";
    std::string filename = getAssetPath() + "lavaplanet_color_rgba.ktx";
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    ktxResult result;
    ktxTexture *ktxTexture;

#if defined(__ANDROID__)
    AAsset *asset = AAssetManager_open(_assetManager, filename.c_str(), AASSET_MODE_STREAMING);
    if (!asset) {
        FATAL("Could not load texture from " + filename, -1);
    }
    size_t size = AAsset_getLength(asset);
    ASSERT(size > 0, "asset size should larger then 0");

    ktx_uint8_t *textureData = new ktx_uint8_t[size];
    AAsset_read(asset, textureData, size);
    AAsset_close(asset);
    result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                         &ktxTexture);
    delete[] textureData;
#else
    // To Do
#endif
    ASSERT(result == KTX_SUCCESS, "ktxTexture_CreateFromMemory failed");
    auto textureWidth = ktxTexture->baseWidth;
    auto textureHeight = ktxTexture->baseHeight;
    auto textureMipLevels = ktxTexture->numLevels;
    ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
    ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

    // Linear tiled images
    // Optimal tiled images: not accessible by the host, requires some sort of data copy,
    // either from a buffer or	a linear tiled image
#if defined(LINEAR_TILED_IMAGES)
    VmaAllocation vmaImageAllocation{nullptr};

    // linear texture
    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    // linear tiling
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    // if it is optim tiling, usage will be VK_IMAGE_USAGE_TRANSFER_SRC_BIT, a convert is needed
    // other flag: VK_IMAGE_USAGE_TRANSFER_DST_BIT
    // VK_IMAGE_USAGE_SAMPLED_BIT: directly used by shader
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // for optimal   .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    // VK_IMAGE_LAYOUT_PREINITIALIZED is only useful with linear images
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageCreateInfo.extent = {textureWidth, textureHeight, 1};

    // create a image, allocate memory for it, and bind them together, all in one call
    // memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT: use VMA_MEMORY_USAGE_AUTO_PREFER_HOST
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#ggaa5846affa1e9da3800e3e78fae2305cca9b422585242160b8ed3418310ee6664d
    // VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT is needed for VMA_MEMORY_USAGE_AUTO_PREFER_HOST
    // to map/unmap
    const VmaAllocationCreateInfo allocCreateInfo = {
            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            .priority = 1.0f,
    };

    VK_CHECK(vmaCreateImage(_vmaAllocator, &imageCreateInfo, &allocCreateInfo, &image,
                            &vmaImageAllocation, nullptr));

    if (vmaImageAllocation != nullptr) {
        VmaAllocationInfo imageAllocationInfo;
        vmaGetAllocationInfo(_vmaAllocator, vmaImageAllocation, &imageAllocationInfo);

//        // Map image memory
//        void *data;
//        VK_CHECK_RESULT(vkMapMemory(device, mappableMemory, 0, memReqs.size, 0, &data));
//        // Copy image data of the first mip level into memory
//        memcpy(data, ktxTextureData, memReqs.size);
//        vkUnmapMemory(device, mappableMemory);

        void *mappedMemory{nullptr};
        VK_CHECK(vmaMapMemory(_vmaAllocator, vmaImageAllocation, &mappedMemory));
        memcpy(mappedMemory, ktxTextureData, imageAllocationInfo.size);
        vmaUnmapMemory(_vmaAllocator, vmaImageAllocation);

        // commandbuffer to submit the texture data
        // image memory barrier transfer image to shader read layout
        // transition image layout
        VkCommandBuffer copyCmd;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &allocInfo, &copyCmd));
        // recording buffer
        VkCommandBufferBeginInfo cmdBufferBeginInfo {};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(copyCmd, &cmdBufferBeginInfo));

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;

        VkImageMemoryBarrier imageMemoryBarrier{};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        // from cpu write
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        // to gpu read
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        // match creation
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(
                copyCmd,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier);
        VK_CHECK(vkEndCommandBuffer(copyCmd));

        VkFenceCreateInfo fenceInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
        };
        VkFence fence;
        VK_CHECK(vkCreateFence(_logicalDevice, &fenceInfo, nullptr, &fence));
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCmd;
        VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK(vkWaitForFences(_logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
        vkDestroyFence(_logicalDevice, fence, nullptr);
        vkFreeCommandBuffers(_logicalDevice, _commandPool, 1, &copyCmd);
    }
#else
    // This buffer is used as a transfer source for the buffer copy
    VkBuffer stagingBuffer;
    VmaAllocation vmaStagingBufferAllocation{nullptr};
    VkBufferCreateInfo bufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // for staging buffer
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo stagingAllocationCreateInfo = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_CPU_ONLY,
    };
    VK_CHECK(vmaCreateBuffer(_vmaAllocator, &bufferCreateInfo, &stagingAllocationCreateInfo,
                             &stagingBuffer,
                             &vmaStagingBufferAllocation, nullptr));
    if (vmaStagingBufferAllocation != nullptr) {
        VmaAllocationInfo stagingBufferAllocationInfo;
        vmaGetAllocationInfo(_vmaAllocator, vmaStagingBufferAllocation,
                             &stagingBufferAllocationInfo);
        // copy to staging buffer (visible both host and device)
        void *mappedMemory{nullptr};
        VK_CHECK(vmaMapMemory(_vmaAllocator, vmaStagingBufferAllocation, &mappedMemory));
        memcpy(mappedMemory, ktxTextureData, ktxTextureSize);
        vmaUnmapMemory(_vmaAllocator, vmaStagingBufferAllocation);
        VK_CHECK(vmaFlushAllocation(_vmaAllocator, vmaStagingBufferAllocation, 0, ktxTextureSize));

        // for image
        // diff1: textureMipLevels,
        // diff2: VK_IMAGE_TILING_OPTIMAL,
        // diff3: usage has 1 more flag: VK_IMAGE_USAGE_TRANSFER_DST_BIT
        // diff4: initial layout: VK_IMAGE_LAYOUT_UNDEFINED

        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.mipLevels = textureMipLevels;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = {textureWidth, textureHeight, 1};
        // no need for VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, cpu does not need access
        const VmaAllocationCreateInfo allocCreateInfo = {
                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                .priority = 1.0f,
        };
        VK_CHECK(vmaCreateImage(_vmaAllocator, &imageCreateInfo, &allocCreateInfo, &_image,
                                &_vmaImageAllocation, nullptr));

        std::vector<VkBufferImageCopy> bufferCopyRegions;
        uint32_t offset = 0;

        for (uint32_t i = 0; i < textureMipLevels; ++i) {
            ktx_size_t offsetForMipMapLevel;
            auto ret = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offsetForMipMapLevel);
            ASSERT(ret == KTX_SUCCESS, "ktxTexture_GetImageOffset failed");
            // Setup a buffer image copy structure for the current mip level
            VkBufferImageCopy bufferCopyRegion = {};
            // regarding mipmap
            bufferCopyRegion.bufferOffset = offsetForMipMapLevel;
            // could be depth, stencil and color
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = i;
            bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            // primad mipmap hierachy
            bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> i;
            bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> i;
            bufferCopyRegion.imageExtent.depth = 1;
            bufferCopyRegions.push_back(bufferCopyRegion);
        }
        VkCommandBuffer copyCmd;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &allocInfo, &copyCmd));

        // recording buffer
        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(copyCmd, &cmdBufferBeginInfo));

        // now has mipmap
        // barrier based on mip level, array layers
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = textureMipLevels;
        subresourceRange.layerCount = 1;

        // transition layout
        VkImageMemoryBarrier imageMemoryBarrier{};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.image = _image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        imageMemoryBarrier.srcAccessMask = 0;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: written into
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        //it defines a memory dependency between commands that were
        // submitted to the same queue before it, and those submitted to the same queue after it.
        // ensure the image layout is VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, which could be write to.
        vkCmdPipelineBarrier(
                copyCmd,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier);
        // now image layout(usage) is writable
        // staging buffer to device-local(image is device local memory)
        vkCmdCopyBufferToImage(
                copyCmd,
                stagingBuffer,
                _image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(bufferCopyRegions.size()),
                bufferCopyRegions.data());

        // image layout(usage) from dst -> shader read
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(
                copyCmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier);
        VK_CHECK(vkEndCommandBuffer(copyCmd));

        VkFenceCreateInfo fenceInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
        };
        VkFence fence;
        VK_CHECK(vkCreateFence(_logicalDevice, &fenceInfo, nullptr, &fence));
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCmd;
        VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK(vkWaitForFences(_logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
        vkDestroyFence(_logicalDevice, fence, nullptr);
        vkFreeCommandBuffers(_logicalDevice, _commandPool, 1, &copyCmd);

        // Done with the staging buffer
        // vmaDestroyBuffer to replace the following
        // Clean up staging resources
//        vkFreeMemory(_logicalDevice, stagingBuffer, nullptr);
        //vmaFreeMemory(_vmaAllocator, vmaImageAllocation);

        vkDestroyBuffer(_logicalDevice, stagingBuffer, nullptr);
        // cannot free memory bounded to the image.
        // vmaDestroyBuffer(_vmaAllocator, stagingBuffer, vmaImageAllocation);
    }
#endif
    // done with the cpu texture
    ktxTexture_Destroy(ktxTexture);
    // image view

    // inteprete images's size, location and format except layout (image barrier)
    VkImageViewCreateInfo imageViewInfo = {};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = format;
    // subresource range could limit miplevel and layer ranges, here all are open to access
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = 1;
#if defined(LINEAR_TILED_IMAGES)
    imageViewInfo.subresourceRange.levelCount = 1;
#else
    imageViewInfo.subresourceRange.levelCount = textureMipLevels;
#endif
    imageViewInfo.image = _image;
    VK_CHECK(vkCreateImageView(_logicalDevice, &imageViewInfo, nullptr, &_imageView));

    // sampler
    VkSamplerCreateInfo samplerCreateInfo = {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerCreateInfo.minLod = 0.0f;
#if defined(LINEAR_TILED_IMAGES)
    samplerCreateInfo.maxLod = 0.0f;
#else
    samplerCreateInfo.maxLod = textureMipLevels;
#endif
    // Enable anisotropic filtering
    if (_enabledDeviceFeatures.features.samplerAnisotropy) {
        // Use max. level of anisotropy for this example
        samplerCreateInfo.maxAnisotropy = _physicalDevicesProp1.limits.maxSamplerAnisotropy;
        samplerCreateInfo.anisotropyEnable = VK_TRUE;
    } else {
        samplerCreateInfo.maxAnisotropy = 1.0;
        samplerCreateInfo.anisotropyEnable = VK_FALSE;
    }
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK(vkCreateSampler(_logicalDevice, &samplerCreateInfo, nullptr, &_sampler));
}