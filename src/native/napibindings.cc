#include <napi.h>
#include "webglnapi.hh"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    WebGLRenderingContext::Init(env, exports);
    return exports;
}

NODE_API_MODULE(addon, Init)
