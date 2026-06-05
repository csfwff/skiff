#ifndef SKIFF_PLATFORM_H_
#define SKIFF_PLATFORM_H_

#include <flutter_linux/flutter_linux.h>

G_DECLARE_FINAL_TYPE(SkiffNativePlugin,
                     skiff_native_plugin,
                     SKIFF,
                     NATIVE_PLUGIN,
                     GObject)

#ifdef __cplusplus
extern "C" {
#endif

// Register the Skiff native platform plugin with the given registrar.
// This should be called after fl_register_plugins().
void skiff_native_plugin_register_with_registrar(FlPluginRegistrar* registrar);

#ifdef __cplusplus
}
#endif

#endif  // SKIFF_PLATFORM_H_
