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


## AOT Shader compilation in Android Studio
1. Putting shaders into app/src/main/shaders/
2. Android Studio will do

Shaderc compile flags could be configured inside the gradle DSL shaders block (.kts) kotlin