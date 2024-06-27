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
