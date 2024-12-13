#include <napi.h>
#include "webglnapi.h"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    WebGLRenderingContext::Init(env, exports);
    return exports;
}

NODE_API_MODULE(addon, Init)
