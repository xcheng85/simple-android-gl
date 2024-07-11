#include <misc.h>

std::string getAssetPath()
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
return "";
#else
return "./../assets/";
#endif
}

void FATAL(const std::string &message, int32_t exitCode)
{
#if defined(_WIN32)
if (!errorModeSilent) {
            MessageBox(NULL, message.c_str(), NULL, MB_OK | MB_ICONERROR);
        }
#elif defined(__ANDROID__)
LOGE("Fatal error: %s", message.c_str());
#endif
std::cerr << message << "\n";
#if !defined(__ANDROID__)
exit(exitCode);
#endif
}