#pragma once

#include <string>
#include <iostream>
#include <array>
#include <vulkan/vulkan.h>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/log.h>
#define LOG_TAG "simpleandroidvk"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

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


std::string getAssetPath();

void FATAL(const std::string &message, int32_t exitCode);

struct VertexDef1 {
    std::array<float, 3> pos;
    std::array<float, 2> uv;
    std::array<float, 3> normal;
};

//// mimic vao in opengl
//struct VAO {
//    VkBuffer vertexBuffer;
//    VkBuffer indexBuffer;
//};