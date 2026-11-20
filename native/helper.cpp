#include "include/capi/cef_app_capi.h"
#include "include/cef_sandbox_mac.h"
#include <dlfcn.h>
#include <filesystem>
#include <mach-o/dyld.h>

int main(int argc, char **argv) {
    void *sandbox = cef_scoped_sandbox_initialize(argc, argv);
    if (!sandbox)
        return 1;
    char exe[4096];
    uint32_t length = sizeof(exe);
    if (_NSGetExecutablePath(exe, &length))
        return 1;
    auto lib = std::filesystem::path(exe).parent_path() / "../../.." /
               "Chromium Embedded Framework.framework" / "Chromium Embedded Framework";
    void *framework = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!framework)
        return 1;
    auto execute = reinterpret_cast<decltype(&cef_execute_process)>(dlsym(framework, "cef_execute_process"));
    cef_main_args_t args{argc, argv};
    int result = execute ? execute(&args, nullptr, nullptr) : 1;
    cef_scoped_sandbox_destroy(sandbox);
    return result;
}
