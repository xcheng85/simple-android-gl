#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <android/sensor.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream>

#include <vkapplication.h>

struct VulkanEngine {
    struct android_app *androidApp;
    VkApplication *vkApp;
    bool canRender{false};
};

static void VulkanEngineHandleCmd(struct android_app *app, int32_t cmd) {
    auto *engine = (VulkanEngine *) app->userData;
    switch (cmd) {
        case APP_CMD_START:
            if (engine->androidApp->window != nullptr) {
                engine->vkApp->reset(app->window, app->activity->assetManager);
                engine->vkApp->initVulkan();
                engine->canRender = true;
            }
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            LOGI("Called - APP_CMD_INIT_WINDOW");
            if (engine->androidApp->window != nullptr) {
                LOGI("Setting a new surface");
                engine->vkApp->reset(app->window, app->activity->assetManager);
                if (!engine->vkApp->isInitialized()) {
                    LOGI("Starting application");
                    engine->vkApp->initVulkan();
                }
                engine->canRender = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being hidden or closed, clean it up.
            engine->canRender = false;
            break;
        case APP_CMD_DESTROY:
            // The window is being hidden or closed, clean it up.
            LOGI("Destroying");
            //engine->app_backend->cleanup();
        default:
            break;
    }
}

/*
 * Entry point required by the Android Glue library.
 * This can also be achieved more verbosely by manually declaring JNI functions
 * and calling them from the Android application layer.
 */
void android_main(struct android_app *state) {
    VulkanEngine engine{};
    VkApplication vkApp{};

    engine.androidApp = state;
    engine.vkApp = &vkApp;

    state->userData = &engine;
    state->onAppCmd = VulkanEngineHandleCmd;

//    android_app_set_key_event_filter(state, VulkanKeyEventFilter);
//    android_app_set_motion_event_filter(state, VulkanMotionEventFilter);

    while (true) {
        int ident;
        int events;
        android_poll_source *source;
        while ((ident = ALooper_pollAll(engine.canRender ? 0 : -1, nullptr, &events,
                                        (void **) &source)) >= 0) {
            if (source != nullptr) {
                source->process(state, source);
            }
        }

//        HandleInputEvents(state);
//
//        engine.app_backend->render();
    }
}