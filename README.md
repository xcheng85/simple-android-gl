# simple-android-gl

## Android NDK + Graphics + zero java

1. SDK Manager --> ndk, cmake, etc.
2. Empty Activity
3. Add c/c++ module
4. Modify AndroidManifest.yaml



## Others

format: ctrl + alt + l

## Issue: There is no runner for android app
File-> Sync Project with Gradle Files

## Topic: Gamma correction & color space
leverages the ability to to apply gamma correction as the final step in your render pipeline
If your swapchain does the gamma correction, you do not need todo it in your shaders


sRGB Texture: sRGB image --> sRGB Texture, shader will convert linear

OpenGL 4.0 shading cookbook
OpenGL ES3.0 programming guide

## Topic: Physical features and logic device

### 1. linked list to query

### 2. same / different linked list to enable for the logic device

## Topic: Passing data to shader

### How many descriptor set layout needed ? depends on the shader

layout(set=0, binding = 0)
layout(set=1, binding = 0)

### Descriptor set pool to allocate descriptor sets

### Allocate ds from the ds pool, how many ? 
depends on the glsl

### Create Resource (UBO, TBO, etc)

### Bind Resource to the descriptor set (must match the shader code)
vkUpdateDescriptorSet
### Bind to the shaders in the pipeline
vkCmdBindDescriptorSets


## AOT Shader compilation in Android Studio

C:\Users\cheng\AppData\Local\Android\Sdk\ndk\27.0.11902837\build\cmake

Vendor (i.e. copy) the third-party source into your repository and use add_subdirectory to build it. 
This only works if the other library is also built with CMake.

1. Putting shaders into app/src/main/shaders/
2. Android Studio will do

Shaderc compile flags could be configured inside the gradle DSL shaders block (.kts) kotlin

## Assets folder for texture

project: Vulkan-Sample-Assets

## Barrier

### Image memory Barrier
1. could be used for ensure layout change (critical, only way to set image layout )
2. could be ensure read after write order correctness

### Image layout (intended usage of image)
1. transfer source/dst
a writeable image must be in layout: VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL

2. shader read
   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

## Vulkan device local memory
1. Images usually is device-local memory
2. copy the data to staging buffer first(map and unmap)
3. vkCmdCopy*: staging buffer -> device local

## Vulkan host-visible buffer
copy the data to staging buffer first(map and unmap)

## gltf binary loader
1. config cmake
2. ninja binary to system path (rapidJSON depends on ninja)

### interface to implement
1. IStreamReader: GetInputStream
2. GLTFResourceReader / GLBResourceReader

## Device Ext: BUFFER DEVICE ADDRESS
VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
Device extension VK_KHR_buffer_device_address allow to fetch raw GPU pointer to a buffer and pass it for usage in a shader code.
vk12features: bufferDeviceAddress

## Indirect draw
1. VkDrawIndexedIndirectCommand

2. Disable VAO: VkPipelineVertexInputStateCreateInfo

## Things to change when shader is updated (introduce new buffer/image/sampler)
1. DescriptorSetLayout: combo or per set (in shader)
2. Descriptor Pool update
3. Allocation of descriptorSet
4. Write to descriptors


### Things to change when integrate with Glb Textures (bindless Texture and Samplers)
1. Update descriptorSet layout to support "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE" and "VK_DESCRIPTOR_TYPE_SAMPLER"
2. Update descriptorSet Pool to support "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE" and "VK_DESCRIPTOR_TYPE_SAMPLER"
3. Allocate 1 descriptor set for Texture2D
4. Allocate 1 sampler for Sampler
5. bind imageview to ds
6. bind samplers to ds