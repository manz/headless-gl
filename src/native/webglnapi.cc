#include "webglnapi.h"
#include <iostream>
#include <sstream>

bool WebGLRenderingContext::HAS_DISPLAY = false;
EGLDisplay WebGLRenderingContext::DISPLAY;
WebGLRenderingContext *WebGLRenderingContext::ACTIVE = NULL;
WebGLRenderingContext *WebGLRenderingContext::CONTEXT_LIST_HEAD = NULL;

const char *REQUIRED_EXTENSIONS[] = {"GL_OES_packed_depth_stencil", "GL_ANGLE_instanced_arrays", NULL};

Napi::Value WebGLRenderingContext::DisposeAll(const Napi::CallbackInfo &info) {
	  while(CONTEXT_LIST_HEAD) {
    WebGLRenderingContext::CONTEXT_LIST_HEAD->dispose();
  }

  if(WebGLRenderingContext::HAS_DISPLAY) {
    eglTerminate(WebGLRenderingContext::DISPLAY);
    WebGLRenderingContext::HAS_DISPLAY = false;
  }
    return info.Env().Undefined();
}



WebGLRenderingContext::WebGLRenderingContext(const Napi::CallbackInfo &info) :
    Napi::ObjectWrap<WebGLRenderingContext>(info), state(GLCONTEXT_STATE_INIT), unpack_flip_y(false),
    unpack_premultiply_alpha(false), unpack_colorspace_conversion(0x9244), unpack_alignment(4), next(NULL), prev(NULL),
    lastError(GL_NO_ERROR) {
    Napi::Env env = info.Env();

    int width = info[0].As<Napi::Number>().Int32Value();
    int height = info[1].As<Napi::Number>().Int32Value();
    bool alpha = info[2].As<Napi::Boolean>().Value();
    bool depth = info[3].As<Napi::Boolean>().Value();
    bool stencil = info[4].As<Napi::Boolean>().Value();
    bool antialias = info[5].As<Napi::Boolean>().Value();
    bool premultipliedAlpha = info[6].As<Napi::Boolean>().Value();
    bool preserveDrawingBuffer = info[7].As<Napi::Boolean>().Value();
    bool preferLowPowerToHighPerformance = info[8].As<Napi::Boolean>().Value();
    bool failIfMajorPerformanceCaveat = info[9].As<Napi::Boolean>().Value();


    this->_initialize(width, height);

    if (this->state != GLCONTEXT_STATE_OK) {
        throw std::invalid_argument("Unable to initialize context");
    }
}

void WebGLRenderingContext::_initialize(GLint width, GLint height) {
    // Get display
    if (!HAS_DISPLAY) {
        DISPLAY = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (DISPLAY == EGL_NO_DISPLAY) {
            state = GLCONTEXT_STATE_ERROR;
            return;
        }

        // Initialize EGL
        if (!eglInitialize(DISPLAY, NULL, NULL)) {
            state = GLCONTEXT_STATE_ERROR;
            return;
        }

        // Save display
        HAS_DISPLAY = true;
    }

    // Set up configuration
    EGLint attrib_list[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                            EGL_RED_SIZE,     8,
                            EGL_GREEN_SIZE,   8,
                            EGL_BLUE_SIZE,    8,
                            EGL_ALPHA_SIZE,   8,
                            EGL_DEPTH_SIZE,   24,
                            EGL_STENCIL_SIZE, 8,
                            EGL_NONE};
    EGLint num_config;
    if (!eglChooseConfig(DISPLAY, attrib_list, &config, 1, &num_config) || num_config != 1) {
        state = GLCONTEXT_STATE_ERROR;
        return;
    }

    // Create context
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context = eglCreateContext(DISPLAY, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        state = GLCONTEXT_STATE_ERROR;
        return;
    }

    EGLint surfaceAttribs[] = {EGL_WIDTH, (EGLint) width, EGL_HEIGHT, (EGLint) height, EGL_NONE};
    surface = eglCreatePbufferSurface(DISPLAY, config, surfaceAttribs);
    if (surface == EGL_NO_SURFACE) {
        state = GLCONTEXT_STATE_ERROR;
        return;
    }

    // Set active
    if (!eglMakeCurrent(DISPLAY, surface, surface, context)) {
        state = GLCONTEXT_STATE_ERROR;
        return;
    }

    // Success
    state = GLCONTEXT_STATE_OK;

    registerContext();
    ACTIVE = this;

    // Initialize function pointers
    initPointers();

    // Check extensions
    const char *extensionString = (const char *) ((glGetString) (GL_EXTENSIONS));
    // Load required extensions
    for (const char **rext = REQUIRED_EXTENSIONS; *rext; ++rext) {
        if (!strstr(extensionString, *rext)) {
            dispose();
            state = GLCONTEXT_STATE_ERROR;
            return;
        }
    }

    // Select best preferred depth
    preferredDepth = GL_DEPTH_COMPONENT16;
    if (strstr(extensionString, "GL_OES_depth32")) {
        preferredDepth = GL_DEPTH_COMPONENT32_OES;
    } else if (strstr(extensionString, "GL_OES_depth24")) {
        preferredDepth = GL_DEPTH_COMPONENT24_OES;
    }
}

bool WebGLRenderingContext::setActive() {
    if (state != GLCONTEXT_STATE_OK) {
        return false;
    }
    if (this == ACTIVE) {
        return true;
    }
    if (!eglMakeCurrent(DISPLAY, surface, surface, context)) {
        state = GLCONTEXT_STATE_ERROR;
        return false;
    }
    ACTIVE = this;
    return true;
}

void WebGLRenderingContext::dispose() {
    // Unregister context
    unregisterContext();

    if (!setActive()) {
        state = GLCONTEXT_STATE_ERROR;
        return;
    }

    // Update state
    state = GLCONTEXT_STATE_DESTROY;

    // Store this pointer
    WebGLRenderingContext *inst = this;

    // Destroy all object references
    for (std::map<std::pair<GLuint, GLObjectType>, bool>::iterator iter = objects.begin(); iter != objects.end();
         ++iter) {

        GLuint obj = iter->first.first;

        switch (iter->first.second) {
            case GLOBJECT_TYPE_PROGRAM:
                (inst->glDeleteProgram)(obj);
                break;
            case GLOBJECT_TYPE_BUFFER:
                (inst->glDeleteBuffers)(1, &obj);
                break;
            case GLOBJECT_TYPE_FRAMEBUFFER:
                (inst->glDeleteFramebuffers)(1, &obj);
                break;
            case GLOBJECT_TYPE_RENDERBUFFER:
                (inst->glDeleteRenderbuffers)(1, &obj);
                break;
            case GLOBJECT_TYPE_SHADER:
                (inst->glDeleteShader)(obj);
                break;
            case GLOBJECT_TYPE_TEXTURE:
                (inst->glDeleteTextures)(1, &obj);
                break;
            case GLOBJECT_TYPE_VERTEX_ARRAY:
                (inst->glDeleteVertexArraysOES)(1, &obj);
                break;
            default:
                break;
        }
    }
}

Napi::Value WebGLRenderingContext::SetError(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    Napi::Number value = info[0].As<Napi::Number>();
    int error = value.Uint32Value();

    if (error == GL_NO_ERROR || lastError != GL_NO_ERROR) {
        return env.Undefined();
    }
    GLenum prevError = (this->glGetError)();
    if (prevError == GL_NO_ERROR) {
        lastError = error;
    }

    return env.Undefined();
}

Napi::Value WebGLRenderingContext::LastError(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    return Napi::Number::New(env, lastError);
}

Napi::Value WebGLRenderingContext::GetGLError(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    return Napi::Number::New(env, this->glGetError());
}

Napi::Value WebGLRenderingContext::GetError(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    GLenum error = (this->glGetError)();

    if (lastError != GL_NO_ERROR) {
        error = lastError;
    }
    lastError = GL_NO_ERROR;

    return Napi::Number::New(env, error);
}


Napi::Value WebGLRenderingContext::VertexAttribDivisor(const Napi::CallbackInfo &info) {
    GLuint index = info[0].As<Napi::Number>().Uint32Value();
    GLuint divisor = info[1].As<Napi::Number>().Uint32Value();

    (this->glVertexAttribDivisor)(index, divisor);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DrawArraysInstanced(const Napi::CallbackInfo &info) {
    GLenum mode = info[0].As<Napi::Number>().Uint32Value();
    GLint first = info[1].As<Napi::Number>().Uint32Value();
    GLuint count = info[2].As<Napi::Number>().Uint32Value();
    GLuint icount = info[3].As<Napi::Number>().Uint32Value();

    (this->glDrawArraysInstanced)(mode, first, count, icount);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DrawElementsInstanced(const Napi::CallbackInfo &info) {
    GLenum mode = info[0].As<Napi::Number>().Uint32Value();
    GLint count = info[1].As<Napi::Number>().Uint32Value();
    GLenum type = info[2].As<Napi::Number>().Uint32Value();
    GLint offset = info[3].As<Napi::Number>().Uint32Value();
    GLuint icount = info[4].As<Napi::Number>().Uint32Value();

    (this->glDrawElementsInstanced)(mode, count, type, reinterpret_cast<GLvoid *>(offset), icount);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DrawArrays(const Napi::CallbackInfo &info) {
    GLenum mode = info[0].As<Napi::Number>().Uint32Value();
    GLint first = info[1].As<Napi::Number>().Uint32Value();
    GLint count = info[2].As<Napi::Number>().Uint32Value();

    (this->glDrawArrays)(mode, first, count);
    return info.Env().Undefined();
}
/*

GL_METHOD(UniformMatrix2fv) {
  GL_BOILERPLATE;

  GLint location      = Nan::To<int32_t>(info[0]).ToChecked();
  GLboolean transpose = (Nan::To<bool>(info[1]).ToChecked());
  Nan::TypedArrayContents<GLfloat> data(info[2]);

  (inst->glUniformMatrix2fv)(location, data.length() / 4, transpose, *data);
}
 */

Napi::Value WebGLRenderingContext::UniformMatrix2fv(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Uint32Value();
    GLboolean transpose = info[1].As<Napi::Boolean>().Value();
    Napi::TypedArray typedArray = info[2].As<Napi::TypedArray>();

    if (typedArray.TypedArrayType() != napi_float32_array) {
        Napi::Error::New(info.Env(), "Expected an Float32Array").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
    Napi::Float32Array data = typedArray.As<Napi::Float32Array>();


    (this->glUniformMatrix2fv)(location, data.ByteLength() / 4, transpose, data.Data());

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::UniformMatrix3fv(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Uint32Value();
    GLboolean transpose = info[1].As<Napi::Boolean>().Value();
    Napi::TypedArray typedArray = info[2].As<Napi::TypedArray>();

    if (typedArray.TypedArrayType() != napi_float32_array) {
        Napi::Error::New(info.Env(), "Expected an Float32Array").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
    Napi::Float32Array data = typedArray.As<Napi::Float32Array>();


    (this->glUniformMatrix3fv)(location, data.ByteLength() / 9, transpose, data.Data());

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::UniformMatrix4fv(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Uint32Value();
    GLboolean transpose = info[1].As<Napi::Boolean>().Value();
    Napi::TypedArray typedArray = info[2].As<Napi::TypedArray>();

    if (typedArray.TypedArrayType() != napi_float32_array) {
        Napi::Error::New(info.Env(), "Expected an Float32Array").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
    Napi::Float32Array data = typedArray.As<Napi::Float32Array>();


    (this->glUniformMatrix4fv)(location, data.ByteLength() / 16, transpose, data.Data());

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetUniform(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Uint32Value();
    GLint location = info[1].As<Napi::Number>().Uint32Value();

    float data[16];
    (this->glGetUniformfv)(program, location, data);

    Napi::Array array = Napi::Array::New(info.Env());
    for (int i = 0; i < 16; i++) {
        array.Set(i, Napi::Number::From(info.Env(), data[i]));
    }

    return array;
}

Napi::Value WebGLRenderingContext::Uniform1f(const Napi::CallbackInfo &info) {

    int location = info[0].As<Napi::Number>().Int32Value();
    float x = info[1].As<Napi::Number>().FloatValue();

    (this->glUniform1f)(location, x);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform2f(const Napi::CallbackInfo &info) {
    int location = info[0].As<Napi::Number>().Int32Value();
    GLfloat x = info[1].As<Napi::Number>().FloatValue();
    GLfloat y = info[2].As<Napi::Number>().FloatValue();

    (this->glUniform2f)(location, x, y);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform3f(const Napi::CallbackInfo &info) {
    int location = info[0].As<Napi::Number>().Int32Value();
    GLfloat x = info[1].As<Napi::Number>().FloatValue();
    GLfloat y = info[2].As<Napi::Number>().FloatValue();
    GLfloat z = info[3].As<Napi::Number>().FloatValue();

    (this->glUniform3f)(location, x, y, z);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform4f(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Int32Value();
    GLfloat x = info[1].As<Napi::Number>().FloatValue();
    GLfloat y = info[2].As<Napi::Number>().FloatValue();
    GLfloat z = info[3].As<Napi::Number>().FloatValue();
    GLfloat w = info[4].As<Napi::Number>().FloatValue();

    (this->glUniform4f)(location, x, y, z, w);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform1i(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Int32Value();
    GLint x = info[1].As<Napi::Number>().Int32Value();

    (this->glUniform1i)(location, x);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform2i(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Int32Value();
    GLint x = info[1].As<Napi::Number>().Int32Value();
    GLint y = info[2].As<Napi::Number>().Int32Value();

    (this->glUniform2i)(location, x, y);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform3i(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Int32Value();
    GLint x = info[1].As<Napi::Number>().Int32Value();
    GLint y = info[2].As<Napi::Number>().Int32Value();
    GLint z = info[3].As<Napi::Number>().Int32Value();

    (this->glUniform3i)(location, x, y, z);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Uniform4i(const Napi::CallbackInfo &info) {
    GLint location = info[0].As<Napi::Number>().Int32Value();
    GLint x = info[1].As<Napi::Number>().Int32Value();
    GLint y = info[2].As<Napi::Number>().Int32Value();
    GLint z = info[3].As<Napi::Number>().Int32Value();
    GLint w = info[4].As<Napi::Number>().Int32Value();

    (this->glUniform4i)(location, x, y, z, w);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BindAttribLocation(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    GLint index = info[1].As<Napi::Number>().Int32Value();
    Napi::String name = info[2].As<Napi::String>();

    (this->glBindAttribLocation)(program, index, name.Utf8Value().c_str());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::PixelStorei(const Napi::CallbackInfo &info) {
    GLint pname = info[0].As<Napi::Number>().Int32Value();
    GLint param = info[1].As<Napi::Number>().Int32Value();
    // Handle WebGL specific extensions
    switch (pname) {
        case 0x9240:
            this->unpack_flip_y = param != 0;
            break;

        case 0x9241:
            this->unpack_premultiply_alpha = param != 0;
            break;

        case 0x9243:
            this->unpack_colorspace_conversion = param;
            break;

        case GL_UNPACK_ALIGNMENT:
            this->unpack_alignment = param;
            (this->glPixelStorei)(pname, param);
            break;

        case GL_MAX_DRAW_BUFFERS_EXT:
            (this->glPixelStorei)(pname, param);
            break;

        default:
            (this->glPixelStorei)(pname, param);
            break;
    }
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GenerateMipmap(const Napi::CallbackInfo &info) {
    GLint target = info[0].As<Napi::Number>().Int32Value();
    (this->glGenerateMipmap)(target);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetAttribLocation(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    Napi::String name = info[1].As<Napi::String>();

    GLint result = (this->glGetAttribLocation)(program, name.Utf8Value().c_str());

    return Napi::Number::From(info.Env(), result);
}

Napi::Value WebGLRenderingContext::DepthFunc(const Napi::CallbackInfo &info) {
    (this->glDepthFunc)(info[0].As<Napi::Number>().Int32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Viewport(const Napi::CallbackInfo &info) {
    GLint x = info[0].As<Napi::Number>().Int32Value();
    GLint y = info[1].As<Napi::Number>().Int32Value();
    GLsizei width = info[2].As<Napi::Number>().Int32Value();
    GLsizei height = info[3].As<Napi::Number>().Int32Value();

    (this->glViewport)(x, y, width, height);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CreateShader(const Napi::CallbackInfo &info) {
    GLuint shader = (this->glCreateShader)(info[0].As<Napi::Number>().Int32Value());
    this->registerGLObj(GLOBJECT_TYPE_SHADER, shader);

    return Napi::Number::From(info.Env(), shader);
}

Napi::Value WebGLRenderingContext::DeleteShader(const Napi::CallbackInfo &info) {
    GLuint shader = info[0].As<Napi::Number>().Int32Value();
    this->glDeleteShader(shader);
    this->unregisterGLObj(GLOBJECT_TYPE_SHADER, shader);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::ShaderSource(const Napi::CallbackInfo &info) {
    GLint id = info[0].As<Napi::Number>().Int32Value();
    std::string code = info[1].As<Napi::String>().Utf8Value();
    const char *codes[] = {code.c_str()};
    GLint length = code.length();

    (this->glShaderSource)(id, 1, codes, &length);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CompileShader(const Napi::CallbackInfo &info) {
    this->glCompileShader(info[0].As<Napi::Number>().Int32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetAttachedShaders(const Napi::CallbackInfo &info) {
    GLuint program = info[0].As<Napi::Number>().Uint32Value();
    GLint numAttachedShaders;
    (this->glGetProgramiv)(program, GL_ATTACHED_SHADERS, &numAttachedShaders);
    GLuint *shaders = new GLuint[numAttachedShaders];
    GLsizei count;

    (this->glGetAttachedShaders)(program, numAttachedShaders, &count, shaders);

    Napi::Array shadersArr = Napi::Array::New(info.Env());
    for (int i = 0; i < count; i++) {
        shadersArr.Set(i, Napi::Number::From(info.Env(), shaders[i]));
    }
    delete[] shaders;
    return shadersArr;
}

Napi::Value WebGLRenderingContext::FrontFace(const Napi::CallbackInfo &info) {
    this->glFrontFace(info[0].As<Napi::Number>().Int32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetShaderParameter(const Napi::CallbackInfo &info) {
    GLint shader = info[0].As<Napi::Number>().Int32Value();
    GLenum pname = info[1].As<Napi::Number>().Int32Value();

    GLint value;
    this->glGetShaderiv(shader, pname, &value);

    return Napi::Number::From(info.Env(), value);
}

Napi::Value WebGLRenderingContext::GetShaderInfoLog(const Napi::CallbackInfo &info) {
    GLint id = info[0].As<Napi::Number>().Int32Value();
    GLint infoLogLength;
    (this->glGetShaderiv)(id, GL_INFO_LOG_LENGTH, &infoLogLength);
    char *error = new char[infoLogLength + 1];
    (this->glGetShaderInfoLog)(id, infoLogLength + 1, &infoLogLength, error);

    return Napi::String::From(info.Env(), error);
    delete[] error;
}

Napi::Value WebGLRenderingContext::CreateProgram(const Napi::CallbackInfo &info) {
    GLuint program = (this->glCreateProgram)();
    this->registerGLObj(GLOBJECT_TYPE_PROGRAM, program);
    return Napi::Number::From(info.Env(), program);
}

Napi::Value WebGLRenderingContext::DeleteProgram(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    this->unregisterGLObj(GLOBJECT_TYPE_PROGRAM, program);
    this->glDeleteProgram(program);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DeleteFramebuffer(const Napi::CallbackInfo &info) {
    GLuint buffer = info[0].As<Napi::Number>().Uint32Value();

    this->unregisterGLObj(GLOBJECT_TYPE_FRAMEBUFFER, buffer);
    this->glDeleteFramebuffers(1, &buffer);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DeleteRenderbuffer(const Napi::CallbackInfo &info) {
    GLuint buffer = info[0].As<Napi::Number>().Uint32Value();

    this->unregisterGLObj(GLOBJECT_TYPE_RENDERBUFFER, buffer);
    this->glDeleteRenderbuffers(1, &buffer);

    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::AttachShader(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    GLint shader = info[1].As<Napi::Number>().Int32Value();
    (this->glAttachShader)(program, shader);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::ValidateProgram(const Napi::CallbackInfo &info) {
    this->glValidateProgram(info[0].As<Napi::Number>().Int32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::LinkProgram(const Napi::CallbackInfo &info) {
    this->glLinkProgram(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetProgramInfoLog(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    GLint infoLogLength;
    (this->glGetProgramiv)(program, GL_INFO_LOG_LENGTH, &infoLogLength);
    char *error = new char[infoLogLength + 1];
    (this->glGetProgramInfoLog)(program, infoLogLength + 1, &infoLogLength, error);

    return Napi::String::From(info.Env(), error);
}

Napi::Value WebGLRenderingContext::GetProgramParameter(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    GLenum pname = info[1].As<Napi::Number>().Int32Value();
    GLint value = 0;
    (this->glGetProgramiv)(program, pname, &value);

    return Napi::Number::From(info.Env(), value);
}

Napi::Value WebGLRenderingContext::GetUniformLocation(const Napi::CallbackInfo &info) {
    GLint program = info[0].As<Napi::Number>().Int32Value();
    std::string name = info[1].As<Napi::String>().Utf8Value();

    return Napi::Number::From(info.Env(), (this->glGetUniformLocation)(program, name.c_str()));
}

Napi::Value WebGLRenderingContext::ClearColor(const Napi::CallbackInfo &info) {
    GLclampf red = info[0].As<Napi::Number>().FloatValue();
    GLclampf green = info[1].As<Napi::Number>().FloatValue();
    GLclampf blue = info[2].As<Napi::Number>().FloatValue();
    GLclampf alpha = info[3].As<Napi::Number>().FloatValue();

    this->glClearColor(red, green, blue, alpha);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BlendColor(const Napi::CallbackInfo &info) {
    GLfloat red = info[0].As<Napi::Number>().FloatValue();
    GLfloat green = info[1].As<Napi::Number>().FloatValue();
    GLfloat blue = info[2].As<Napi::Number>().FloatValue();
    GLfloat alpha = info[3].As<Napi::Number>().FloatValue();

    this->glBlendColor(red, green, blue, alpha);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::ClearDepth(const Napi::CallbackInfo &info) {
    GLfloat depth = info[0].As<Napi::Number>().FloatValue();
    (this->glClearDepthf)(depth);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Disable(const Napi::CallbackInfo &info) {
    (this->glDisable)(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Enable(const Napi::CallbackInfo &info) {
    (this->glEnable)(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CreateTexture(const Napi::CallbackInfo &info) {
    GLuint texture;
    this->glGenTextures(1, &texture);
    this->registerGLObj(GLOBJECT_TYPE_TEXTURE, texture);
    return Napi::Number::From(info.Env(), texture);
}

Napi::Value WebGLRenderingContext::DeleteTexture(const Napi::CallbackInfo &info) {
    GLuint texture = info[0].As<Napi::Number>().Uint32Value();
    this->glDeleteTextures(1, &texture);
    this->unregisterGLObj(GLOBJECT_TYPE_TEXTURE, texture);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DetachShader(const Napi::CallbackInfo &info) {
    GLuint program = info[0].As<Napi::Number>().Int32Value();
    GLuint shader = info[1].As<Napi::Number>().Int32Value();

    this->glDetachShader(program, shader);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BindTexture(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLint texture = info[1].As<Napi::Number>().Uint32Value();
    this->glBindTexture(target, texture);
    return info.Env().Undefined();
}

unsigned char *WebGLRenderingContext::unpackPixels(GLenum type, GLenum format, GLint width, GLint height,
                                                   unsigned char *pixels) {

    // Compute pixel size
    GLint pixelSize = 1;
    if (type == GL_UNSIGNED_BYTE || type == GL_FLOAT) {
        if (type == GL_FLOAT) {
            pixelSize = 4;
        }
        switch (format) {
            case GL_ALPHA:
            case GL_LUMINANCE:
                break;
            case GL_LUMINANCE_ALPHA:
                pixelSize *= 2;
                break;
            case GL_RGB:
                pixelSize *= 3;
                break;
            case GL_RGBA:
                pixelSize *= 4;
                break;
        }
    } else {
        pixelSize = 2;
    }

    // Compute row stride
    GLint rowStride = pixelSize * width;
    if ((rowStride % unpack_alignment) != 0) {
        rowStride += unpack_alignment - (rowStride % unpack_alignment);
    }

    GLint imageSize = rowStride * height;
    unsigned char *unpacked = new unsigned char[imageSize];

    if (unpack_flip_y) {
        for (int i = 0, j = height - 1; j >= 0; ++i, --j) {
            memcpy(reinterpret_cast<void *>(unpacked + j * rowStride), reinterpret_cast<void *>(pixels + i * rowStride),
                   width * pixelSize);
        }
    } else {
        memcpy(reinterpret_cast<void *>(unpacked), reinterpret_cast<void *>(pixels), imageSize);
    }

    // Premultiply alpha unpacking
    if (unpack_premultiply_alpha && (format == GL_LUMINANCE_ALPHA || format == GL_RGBA)) {

        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                unsigned char *pixel = unpacked + (row * rowStride) + (col * pixelSize);
                if (format == GL_LUMINANCE_ALPHA) {
                    pixel[0] *= pixel[1] / 255.0;
                } else if (type == GL_UNSIGNED_BYTE) {
                    float scale = pixel[3] / 255.0;
                    pixel[0] *= scale;
                    pixel[1] *= scale;
                    pixel[2] *= scale;
                } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
                    int r = pixel[0] & 0x0f;
                    int g = pixel[0] >> 4;
                    int b = pixel[1] & 0x0f;
                    int a = pixel[1] >> 4;

                    float scale = a / 15.0;
                    r *= scale;
                    g *= scale;
                    b *= scale;

                    pixel[0] = r + (g << 4);
                    pixel[1] = b + (a << 4);
                } else if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
                    if ((pixel[0] & 1) == 0) {
                        pixel[0] = 1; // why does this get set to 1?!?!?!
                        pixel[1] = 0;
                    }
                }
            }
        }
    }

    return unpacked;
}

Napi::Value WebGLRenderingContext::TexImage2D(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Int32Value();
    GLint level = info[1].As<Napi::Number>().Int32Value();
    GLenum internalformat = info[2].As<Napi::Number>().Int32Value();
    GLsizei width = info[3].As<Napi::Number>().Int32Value();
    GLsizei height = info[4].As<Napi::Number>().Int32Value();

    GLint border = info[5].As<Napi::Number>().Int32Value();

    GLenum format = info[6].As<Napi::Number>().Int32Value();

    GLint type = info[7].As<Napi::Number>().Int32Value();

    if (info[8].IsObject()) {
        Napi::Uint8Array pixelsArray = info[8].As<Napi::Uint8Array>();
        unsigned char *pixels = pixelsArray.Data();
        if (this->unpack_flip_y || this->unpack_premultiply_alpha) {
            unsigned char *unpacked = this->unpackPixels(type, format, width, height, pixels);
            (this->glTexImage2D)(target, level, internalformat, width, height, border, format, type, unpacked);
            delete[] unpacked;
        } else {
            (this->glTexImage2D)(target, level, internalformat, width, height, border, format, type, pixels);
        }
    } else {
        size_t length = width * height * 4;
        if (type == GL_FLOAT) {
            length *= 4;
        }
        char *data = new char[length];
        memset(data, 0, length);
        (this->glTexImage2D)(target, level, internalformat, width, height, border, format, type, data);
        delete[] data;
    }
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::TexSubImage2D(const Napi::CallbackInfo &info) {

    GLenum target = info[0].As<Napi::Number>().Int32Value();
    GLint level = info[1].As<Napi::Number>().Int32Value();
    GLint xoffset = info[2].As<Napi::Number>().Int32Value();
    GLint yoffset = info[3].As<Napi::Number>().Int32Value();
    GLsizei width = info[4].As<Napi::Number>().Int32Value();
    GLsizei height = info[5].As<Napi::Number>().Int32Value();
    GLenum format = info[6].As<Napi::Number>().Int32Value();
    GLint type = info[7].As<Napi::Number>().Int32Value();
    info[2].IsTypedArray();
    Napi::Uint8Array pixelsArray = info[8].As<Napi::Uint8Array>();
    unsigned char *pixels = pixelsArray.Data();


    if (this->unpack_flip_y || this->unpack_premultiply_alpha) {
        unsigned char *unpacked = this->unpackPixels(type, format, width, height, pixels);
        (this->glTexSubImage2D)(target, level, xoffset, yoffset, width, height, format, type, unpacked);
        delete[] unpacked;
    } else {
        (this->glTexSubImage2D)(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::TexParameteri(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum pname = info[1].As<Napi::Number>().Uint32Value();
    GLint param = info[2].As<Napi::Number>().Uint32Value();

    (this->glTexParameteri)(target, pname, param);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::TexParameterf(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum pname = info[1].As<Napi::Number>().Uint32Value();
    GLfloat param = info[2].As<Napi::Number>().FloatValue();
    (this->glTexParameterf)(target, pname, param);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetTexParameter(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum pname = info[1].As<Napi::Number>().Uint32Value();

    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT) {
        GLfloat param_value = 0;
        (this->glGetTexParameterfv)(target, pname, &param_value);
        return Napi::Number::From(info.Env(), param_value);
    } else {
        GLint param_value = 0;
        (this->glGetTexParameteriv)(target, pname, &param_value);
        return Napi::Number::From(info.Env(), param_value);
    }
}


Napi::Value WebGLRenderingContext::Clear(const Napi::CallbackInfo &info) {
    this->glClear(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::UseProgram(const Napi::CallbackInfo &info) {
    this->glUseProgram(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CreateBuffer(const Napi::CallbackInfo &info) {
    GLuint buffer;
    this->glGenBuffers(1, &buffer);
    this->registerGLObj(GLOBJECT_TYPE_BUFFER, buffer);

    return Napi::Number::From(info.Env(), buffer);
}

Napi::Value WebGLRenderingContext::BindBuffer(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLuint buffer = info[1].As<Napi::Number>().Uint32Value();

    this->glBindBuffer(target, buffer);

    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::CreateFramebuffer(const Napi::CallbackInfo &info) {
    GLuint buffer;
    this->glGenFramebuffers(1, &buffer);
    this->registerGLObj(GLOBJECT_TYPE_FRAMEBUFFER, buffer);
    return Napi::Number::From(info.Env(), buffer);
}

Napi::Value WebGLRenderingContext::BindFramebuffer(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLuint buffer = info[1].As<Napi::Number>().Uint32Value();

    this->glBindFramebuffer(target, buffer);

    return info.Env().Undefined();
}
Napi::Value WebGLRenderingContext::FramebufferTexture2D(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum attachment = info[1].As<Napi::Number>().Uint32Value();
    GLint textarget = info[2].As<Napi::Number>().Uint32Value();
    GLint texture = info[3].As<Napi::Number>().Uint32Value();
    GLint level = info[4].As<Napi::Number>().Uint32Value();
    // Handle depth stencil case separately
    if (attachment == 0x821A) {
        (this->glFramebufferTexture2D)(target, GL_DEPTH_ATTACHMENT, textarget, texture, level);
        (this->glFramebufferTexture2D)(target, GL_STENCIL_ATTACHMENT, textarget, texture, level);
    } else {
        (this->glFramebufferTexture2D)(target, attachment, textarget, texture, level);
    }
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BufferData(const Napi::CallbackInfo &info) {
    GLint target = info[0].As<Napi::Number>().Int32Value();
    GLenum usage = info[2].As<Napi::Number>().Int32Value();

    if (info[1].IsObject()) {
        Napi::Uint8Array uintArray = info[1].As<Napi::Uint8Array>();
        unsigned char *array = uintArray.Data();
        (this->glBufferData)(target, uintArray.ByteLength(), static_cast<void *>(array), usage);

    } else if (info[1].IsNumber()) {
        (this->glBufferData)(target, info[1].As<Napi::Number>().Int32Value(), NULL, usage);
    }

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BufferSubData(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLint offset = info[1].As<Napi::Number>().Uint32Value();
    Napi::Uint8Array uintArray = info[2].As<Napi::Uint8Array>();
    unsigned char *array = uintArray.Data();
    this->glBufferSubData(target, offset, uintArray.ByteLength(), array);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BlendEquation(const Napi::CallbackInfo &info) {
    GLenum mode = info[0].As<Napi::Number>().Uint32Value();
    this->glBlendEquation(mode);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BlendFunc(const Napi::CallbackInfo &info) {
    GLenum sfactor = info[0].As<Napi::Number>().Uint32Value();
    GLenum dfactor = info[1].As<Napi::Number>().Uint32Value();

    this->glBlendFunc(sfactor, dfactor);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::EnableVertexAttribArray(const Napi::CallbackInfo &info) {
    this->glEnableVertexAttribArray(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::VertexAttribPointer(const Napi::CallbackInfo &info) {
    GLint index = info[0].As<Napi::Number>().Int32Value();
    GLint size = info[1].As<Napi::Number>().Int32Value();
    GLenum type = info[2].As<Napi::Number>().Int32Value();
    GLboolean normalized = info[3].As<Napi::Boolean>().Value();
    GLint stride = info[4].As<Napi::Number>().Int32Value();
    size_t offset = info[5].As<Napi::Number>().Uint32Value();

    this->glVertexAttribPointer(index, size, type, normalized, stride, reinterpret_cast<GLvoid *>(offset));
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::ActiveTexture(const Napi::CallbackInfo &info) {
    this->glActiveTexture(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DrawElements(const Napi::CallbackInfo &info) {
    GLenum mode = info[0].As<Napi::Number>().Uint32Value();
    GLint count = info[1].As<Napi::Number>().Uint32Value();
    GLenum type = info[2].As<Napi::Number>().Uint32Value();
    size_t offset = info[3].As<Napi::Number>().Uint32Value();

    (this->glDrawElements)(mode, count, type, reinterpret_cast<GLvoid *>(offset));
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Flush(const Napi::CallbackInfo &info) {
    this->glFlush();
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Finish(const Napi::CallbackInfo &info) {
    this->glFinish();
    return info.Env().Undefined();
}
Napi::Value WebGLRenderingContext::VertexAttrib1f(const Napi::CallbackInfo &info) {
    GLuint index = info[0].As<Napi::Number>().Uint32Value();
    GLfloat x = info[1].As<Napi::Number>().FloatValue();
    this->glVertexAttrib1f(index, x);
    return info.Env().Undefined();
}
Napi::Value WebGLRenderingContext::VertexAttrib2f(const Napi::CallbackInfo &info) {
    GLuint index = info[0].As<Napi::Number>().Uint32Value();
    GLfloat x = info[1].As<Napi::Number>().FloatValue();
    GLfloat y = info[2].As<Napi::Number>().FloatValue();

    this->glVertexAttrib2f(index, x, y);
    return info.Env().Undefined();
}
Napi::Value WebGLRenderingContext::VertexAttrib3f(const Napi::CallbackInfo &info) {
    GLuint index = info[0].As<Napi::Number>().Uint32Value();
    GLfloat x = info[1].As<Napi::Number>().FloatValue();
    GLfloat y = info[2].As<Napi::Number>().FloatValue();
    GLfloat z = info[3].As<Napi::Number>().FloatValue();

    this->glVertexAttrib3f(index, x, y, z);
    return info.Env().Undefined();
}
Napi::Value WebGLRenderingContext::VertexAttrib4f(const Napi::CallbackInfo &info) {
    GLuint index = info[0].As<Napi::Number>().Uint32Value();
    GLfloat x = info[1].As<Napi::Number>().DoubleValue();
    GLfloat y = info[2].As<Napi::Number>().DoubleValue();
    GLfloat z = info[3].As<Napi::Number>().DoubleValue();
    GLfloat w = info[4].As<Napi::Number>().DoubleValue();

    this->glVertexAttrib4f(index, x, y, z, w);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BlendEquationSeparate(const Napi::CallbackInfo &info) {
    GLenum mode_rgb = info[0].As<Napi::Number>().Uint32Value();
    GLenum mode_alpha = info[1].As<Napi::Number>().Uint32Value();
    this->glBlendEquationSeparate(mode_rgb, mode_alpha);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BlendFuncSeparate(const Napi::CallbackInfo &info) {
    GLenum src_rgb = info[0].As<Napi::Number>().Uint32Value();
    GLenum dst_rgb = info[1].As<Napi::Number>().Uint32Value();
    GLenum dst_alpha = info[2].As<Napi::Number>().Uint32Value();
    GLenum src_alpha = info[3].As<Napi::Number>().Uint32Value();
    this->glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::ClearStencil(const Napi::CallbackInfo &info) {
    this->glClearStencil(info[0].As<Napi::Number>().Uint32Value());
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::StencilFunc(const Napi::CallbackInfo &info) {
    GLenum func = info[0].As<Napi::Number>().Int32Value();
    GLint ref = info[1].As<Napi::Number>().Int32Value();
    GLuint mask = info[2].As<Napi::Number>().Uint32Value();
    this->glStencilFunc(func, ref, mask);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::StencilOp(const Napi::CallbackInfo &info) {
    GLenum fail = info[0].As<Napi::Number>().Int32Value();
    GLenum zfail = info[1].As<Napi::Number>().Int32Value();
    GLenum zpass = info[2].As<Napi::Number>().Int32Value();
    this->glStencilOp(fail, zfail, zpass);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::ColorMask(const Napi::CallbackInfo &info) {
    GLboolean r = info[0].As<Napi::Boolean>().Value();
    GLboolean g = info[1].As<Napi::Boolean>().Value();
    GLboolean b = info[2].As<Napi::Boolean>().Value();
    GLboolean a = info[3].As<Napi::Boolean>().Value();
    this->glColorMask(r, g, b, a);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CopyTexImage2D(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLint level = info[1].As<Napi::Number>().Uint32Value();
    GLenum internalFormat = info[2].As<Napi::Number>().Uint32Value();
    GLint x = info[3].As<Napi::Number>().Uint32Value();
    GLint y = info[4].As<Napi::Number>().Uint32Value();
    GLsizei width = info[5].As<Napi::Number>().Uint32Value();
    GLsizei height = info[6].As<Napi::Number>().Uint32Value();
    GLint border = info[7].As<Napi::Number>().Uint32Value();


    this->glCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);
    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::CopyTexSubImage2D(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLint level = info[1].As<Napi::Number>().Uint32Value();
    GLint xoffset = info[2].As<Napi::Number>().Uint32Value();
    GLint yoffset = info[3].As<Napi::Number>().Uint32Value();
    GLint x = info[4].As<Napi::Number>().Uint32Value();
    GLint y = info[5].As<Napi::Number>().Uint32Value();
    GLsizei width = info[6].As<Napi::Number>().Uint32Value();
    GLsizei height = info[7].As<Napi::Number>().Uint32Value();

    this->glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CullFace(const Napi::CallbackInfo &info) {
    GLenum mode = info[0].As<Napi::Number>().Uint32Value();

    this->glCullFace(mode);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DepthMask(const Napi::CallbackInfo &info) {
    GLboolean mode = info[0].As<Napi::Boolean>().Value();

    this->glDepthMask(mode);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DepthRange(const Napi::CallbackInfo &info) {
    GLclampf zNear = static_cast<GLclampf>(info[0].As<Napi::Number>().FloatValue());
    GLclampf zFar = static_cast<GLclampf>(info[0].As<Napi::Number>().FloatValue());

    (this->glDepthRangef)(zNear, zFar);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DisableVertexAttribArray(const Napi::CallbackInfo &info) {
    GLuint index = info[0].As<Napi::Number>().Uint32Value();

    this->glDisableVertexAttribArray(index);

    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::Hint(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Int32Value();
    GLenum mode = info[0].As<Napi::Number>().Int32Value();

    this->glHint(target, mode);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::IsEnabled(const Napi::CallbackInfo &info) {
    GLenum cap = info[0].As<Napi::Number>().Uint32Value();
    bool ret = this->glIsEnabled(cap) != 0;

    return Napi::Boolean::From(info.Env(), ret);
}

Napi::Value WebGLRenderingContext::LineWidth(const Napi::CallbackInfo &info) {
    GLfloat width = info[0].As<Napi::Number>().FloatValue();

    this->glLineWidth(width);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::PolygonOffset(const Napi::CallbackInfo &info) {
    GLclampf factor = static_cast<GLclampf>(info[0].As<Napi::Number>().FloatValue());
    GLclampf units = static_cast<GLclampf>(info[0].As<Napi::Number>().FloatValue());

    (this->glPolygonOffset)(factor, units);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::SampleCoverage(const Napi::CallbackInfo &info) {
    GLclampf value = static_cast<GLclampf>(info[0].As<Napi::Number>().DoubleValue());
    GLboolean invert = info[0].As<Napi::Boolean>();

    this->glSampleCoverage(value, invert);

    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::CreateRenderbuffer(const Napi::CallbackInfo &info) {
    GLuint renderbuffers;
    (this->glGenRenderbuffers)(1, &renderbuffers);
    this->registerGLObj(GLOBJECT_TYPE_RENDERBUFFER, renderbuffers);

    return Napi::Number::From(info.Env(), renderbuffers);
}


Napi::Value WebGLRenderingContext::GetParameter(const Napi::CallbackInfo &info) {
    GLenum name = info[0].As<Napi::Number>().Uint32Value();
    switch (name) {
        case 0x9240 /* UNPACK_FLIP_Y_WEBGL */:
            return Napi::Boolean::From(info.Env(), this->unpack_flip_y);

        case 0x9241 /* UNPACK_PREMULTIPLY_ALPHA_WEBGL*/:
            return Napi::Boolean::From(info.Env(), this->unpack_premultiply_alpha);


        case 0x9243 /* UNPACK_COLORSPACE_CONVERSION_WEBGL */:
            return Napi::Number::From(info.Env(), this->unpack_colorspace_conversion);

        case GL_BLEND:
        case GL_CULL_FACE:
        case GL_DEPTH_TEST:
        case GL_DEPTH_WRITEMASK:
        case GL_DITHER:
        case GL_POLYGON_OFFSET_FILL:
        case GL_SAMPLE_COVERAGE_INVERT:
        case GL_SCISSOR_TEST:
        case GL_STENCIL_TEST: {
            GLboolean params;
            (this->glGetBooleanv)(name, &params);
            return Napi::Boolean::From(info.Env(), params != 0);
        }

        case GL_DEPTH_CLEAR_VALUE:
        case GL_LINE_WIDTH:
        case GL_POLYGON_OFFSET_FACTOR:
        case GL_POLYGON_OFFSET_UNITS:
        case GL_SAMPLE_COVERAGE_VALUE:
        case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: {
            GLfloat params;
            (this->glGetFloatv)(name, &params);
            return Napi::Number::From(info.Env(), params);
        }

        case GL_RENDERER:
        case GL_SHADING_LANGUAGE_VERSION:
        case GL_VENDOR:
        case GL_VERSION:
        case GL_EXTENSIONS: {
            const char *params = reinterpret_cast<const char *>((this->glGetString)(name));
            if (params) {
                return Napi::String::From(info.Env(), params);
            }
            return info.Env().Undefined();
        }

        case GL_MAX_VIEWPORT_DIMS: {
            GLint params[2];
            (this->glGetIntegerv)(name, params);
            Napi::Array arr = Napi::Array::New(info.Env(), 2);
            arr.Set(uint32_t(0), Napi::Number::From(info.Env(), params[0]));
            arr.Set(1, Napi::Number::From(info.Env(), params[1]));

            return arr;
        }
        case GL_SCISSOR_BOX:
        case GL_VIEWPORT: {
            GLint params[4];
            (this->glGetIntegerv)(name, params);
            Napi::Array arr = Napi::Array::New(info.Env(), 4);
            arr.Set(uint32_t(0), Napi::Number::From(info.Env(), params[0]));
            arr.Set(1, Napi::Number::From(info.Env(), params[1]));
            arr.Set(2, Napi::Number::From(info.Env(), params[2]));
            arr.Set(3, Napi::Number::From(info.Env(), params[3]));
            return arr;
        }

        case GL_ALIASED_LINE_WIDTH_RANGE:
        case GL_ALIASED_POINT_SIZE_RANGE:
        case GL_DEPTH_RANGE: {
            GLfloat params[2];
            this->glGetFloatv(name, params);

            Napi::Array arr = Napi::Array::New(info.Env(), 2);
            arr.Set(uint32_t(0), Napi::Number::From(info.Env(), params[0]));
            arr.Set(1, Napi::Number::From(info.Env(), params[1]));
            return arr;
        }

        case GL_BLEND_COLOR:
        case GL_COLOR_CLEAR_VALUE: {
            GLfloat params[4];
            (this->glGetFloatv)(name, params);

            Napi::Array arr = Napi::Array::New(info.Env(), 4);
            arr.Set(uint32_t(0), Napi::Number::From(info.Env(), params[0]));
            arr.Set(1, Napi::Number::From(info.Env(), params[1]));
            arr.Set(2, Napi::Number::From(info.Env(), params[2]));
            arr.Set(3, Napi::Number::From(info.Env(), params[3]));
            return arr;
        }

        case GL_COLOR_WRITEMASK: {
            GLboolean params[4];
            (this->glGetBooleanv)(name, params);

            Napi::Array arr = Napi::Array::New(info.Env(), 4);
            arr.Set(uint32_t(0), Napi::Boolean::From(info.Env(), params[0] == GL_TRUE));
            arr.Set(1, Napi::Boolean::From(info.Env(), params[1] == GL_TRUE));
            arr.Set(2, Napi::Boolean::From(info.Env(), params[2] == GL_TRUE));
            arr.Set(3, Napi::Boolean::From(info.Env(), params[3] == GL_TRUE));
            return arr;
        }
        default: {
            GLint params;
            (this->glGetIntegerv)(name, &params);

            return Napi::Number::From(info.Env(), params);
        }
    }
}


Napi::Value WebGLRenderingContext::BindRenderbuffer(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLuint buffer = info[0].As<Napi::Number>().Uint32Value();

    this->glBindRenderbuffer(target, buffer);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetBufferParameter(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum pname = info[1].As<Napi::Number>().Uint32Value();

    GLint params;
    this->glGetBufferParameteriv(target, pname, &params);

    return Napi::Number::From(info.Env(), params);
}
Napi::Value WebGLRenderingContext::GetRenderbufferParameter(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum pname = info[1].As<Napi::Number>().Uint32Value();

    int value;
    (this->glGetRenderbufferParameteriv)(target, pname, &value);

    return Napi::Number::From(info.Env(), value);
}

Napi::Value WebGLRenderingContext::GetFramebufferAttachmentParameter(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Int32Value();
    GLenum attachement = info[1].As<Napi::Number>().Int32Value();
    GLenum pname = info[2].As<Napi::Number>().Int32Value();
    GLint params;

    this->glGetFramebufferAttachmentParameteriv(target, attachement, pname, &params);

    return Napi::Number::From(info.Env(), params);
}

Napi::Value WebGLRenderingContext::RenderbufferStorage(const Napi::CallbackInfo &info) {

    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    GLenum internalformat = info[1].As<Napi::Number>().Uint32Value();
    GLsizei width = info[2].As<Napi::Number>().Uint32Value();
    GLsizei height = info[3].As<Napi::Number>().Uint32Value();

    // In WebGL, we map GL_DEPTH_STENCIL to GL_DEPTH24_STENCIL8
    if (internalformat == GL_DEPTH_STENCIL_OES) {
        internalformat = GL_DEPTH24_STENCIL8_OES;
    } else if (internalformat == GL_DEPTH_COMPONENT32_OES) {
        internalformat = this->preferredDepth;
    }

    (this->glRenderbufferStorage)(target, internalformat, width, height);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetShaderSource(const Napi::CallbackInfo &info) {
    GLint shader = info[0].As<Napi::Number>().Int32Value();
    GLint len;
    (this->glGetShaderiv)(shader, GL_SHADER_SOURCE_LENGTH, &len);
    GLchar *source = new GLchar[len];
    (this->glGetShaderSource)(shader, len, NULL, source);

    Napi::String str = Napi::String::From(info.Env(), source);
    delete[] source;

    return str;
}

Napi::Value WebGLRenderingContext::DrawBuffersWEBGL(const Napi::CallbackInfo &info) {
    Napi::Array buffersArray = info[0].As<Napi::Array>();
    GLuint numBuffers = buffersArray.Length();
    GLenum *buffers = new GLenum[numBuffers];


    for (GLuint i = 0; i < numBuffers; i++) {
        buffers[i] = buffersArray.Get(i).As<Napi::Number>().Uint32Value();
    }

    this->glDrawBuffersEXT(numBuffers, buffers);

    delete[] buffers;

    return info.Env().Undefined();
}


Napi::Value WebGLRenderingContext::FramebufferRenderbuffer(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Int32Value();
    GLenum attachment = info[1].As<Napi::Number>().Uint32Value();
    GLenum renderbuffertarget = info[2].As<Napi::Number>().Int32Value();
    GLuint renderbuffer = info[3].As<Napi::Number>().Uint32Value();

    if (attachment == 0x821A) {
        (this->glFramebufferRenderbuffer)(target, GL_DEPTH_ATTACHMENT, renderbuffertarget, renderbuffer);
        (this->glFramebufferRenderbuffer)(target, GL_STENCIL_ATTACHMENT, renderbuffertarget, renderbuffer);
    } else {
        (this->glFramebufferRenderbuffer)(target, attachment, renderbuffertarget, renderbuffer);
    }
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::IsBuffer(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsBuffer(info[0].As<Napi::Number>().Uint32Value()));
}

Napi::Value WebGLRenderingContext::IsFramebuffer(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsFramebuffer(info[0].As<Napi::Number>().Uint32Value()));
}
Napi::Value WebGLRenderingContext::IsProgram(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsProgram(info[0].As<Napi::Number>().Uint32Value()));
}

Napi::Value WebGLRenderingContext::IsRenderbuffer(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsRenderbuffer(info[0].As<Napi::Number>().Uint32Value()));
}

Napi::Value WebGLRenderingContext::IsShader(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsShader(info[0].As<Napi::Number>().Uint32Value()));
}


Napi::Value WebGLRenderingContext::IsTexture(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsTexture(info[0].As<Napi::Number>().Uint32Value()));
}

Napi::Value WebGLRenderingContext::Scissor(const Napi::CallbackInfo &info) {
    GLint x = info[0].As<Napi::Number>().Uint32Value();
    GLint y = info[1].As<Napi::Number>().Uint32Value();
    GLsizei width = info[2].As<Napi::Number>().Uint32Value();
    GLsizei height = info[3].As<Napi::Number>().Uint32Value();

    (this->glScissor)(x, y, width, height);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::DeleteBuffer(const Napi::CallbackInfo &info) {
    GLuint buffer = info[0].As<Napi::Number>().Uint32Value();
    this->unregisterGLObj(GLOBJECT_TYPE_BUFFER, buffer);
    this->glDeleteBuffers(1, &buffer);
    return info.Env().Undefined();
}
Napi::Value WebGLRenderingContext::GetActiveAttrib(const Napi::CallbackInfo &info) {
    GLuint program = info[0].As<Napi::Number>().Uint32Value();
    GLuint index = info[1].As<Napi::Number>().Uint32Value();
    GLint maxLength;
    (this->glGetProgramiv)(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxLength);

    char *name = new char[maxLength];
    GLsizei length = 0;
    GLenum type;
    GLsizei size;

    (this->glGetActiveAttrib)(program, index, maxLength, &length, &size, &type, name);


    if (length > 0) {
        Napi::Object activeInfo = Napi::Object::New(info.Env());
        activeInfo.Set(Napi::String::From(info.Env(), "size"), Napi::Number::From(info.Env(), size));
        activeInfo.Set(Napi::String::From(info.Env(), "type"), Napi::Number::From(info.Env(), type));
        activeInfo.Set(Napi::String::From(info.Env(), "name"), Napi::String::From(info.Env(), name));
        return activeInfo;
    }
    return info.Env().Null();
}

Napi::Value WebGLRenderingContext::ReadPixels(const Napi::CallbackInfo &info) {
    GLint x = info[0].As<Napi::Number>().Int32Value();
    GLint y = info[1].As<Napi::Number>().Int32Value();
    GLsizei width = info[2].As<Napi::Number>().Int32Value();
    GLsizei height = info[3].As<Napi::Number>().Int32Value();
    GLenum format = info[4].As<Napi::Number>().Int32Value();
    GLenum type = info[5].As<Napi::Number>().Int32Value();
    auto pixels = info[6].As<Napi::Uint8Array>();
    uint8_t *pixelsData = pixels.Data();

    (this->glReadPixels)(x, y, width, height, format, type, pixelsData);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::Destroy(const Napi::CallbackInfo &info) {
    // FIXME: dispose crashes.
    // this->dispose();
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetActiveUniform(const Napi::CallbackInfo &info) {
    GLuint program = info[0].As<Napi::Number>().Uint32Value();
    GLuint index = info[1].As<Napi::Number>().Uint32Value();

    GLint maxLength;
    (this->glGetProgramiv)(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);


    char *name = new char[maxLength];
    GLsizei length = 0;
    GLenum type;
    GLsizei size;
    (this->glGetActiveUniform)(program, index, maxLength, &length, &size, &type, name);

    if (length > 0) {
        Napi::Object activeInfo = Napi::Object::New(info.Env());
        activeInfo.Set(Napi::String::From(info.Env(), "size"), Napi::Number::From(info.Env(), size));
        activeInfo.Set(Napi::String::From(info.Env(), "type"), Napi::Number::From(info.Env(), type));
        activeInfo.Set(Napi::String::From(info.Env(), "name"), Napi::String::From(info.Env(), name));
        return activeInfo;
    }
    return info.Env().Null();
}

Napi::Value WebGLRenderingContext::GetSupportedExtensions(const Napi::CallbackInfo &info) {
    const char *extensions = reinterpret_cast<const char *>((this->glGetString)(GL_EXTENSIONS));
    return Napi::String::From(info.Env(), extensions);
}


Napi::Value WebGLRenderingContext::GetExtension(const Napi::CallbackInfo &info) {
    // TODO: Unimplemented in the current v8 binding
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::CheckFramebufferStatus(const Napi::CallbackInfo &info) {
    GLenum target = info[0].As<Napi::Number>().Uint32Value();
    return Napi::Number::From(info.Env(), static_cast<int>(this->glCheckFramebufferStatus(target)));
}


Napi::Value WebGLRenderingContext::EXTWEBGL_draw_buffers(const Napi::CallbackInfo &info) {
    Napi::Object result = Napi::Object::New(info.Env());

    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT0_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT0_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT1_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT1_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT2_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT2_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT3_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT3_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT4_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT4_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT5_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT5_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT6_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT6_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT7_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT7_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT8_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT8_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT9_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT9_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT10_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT10_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT11_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT11_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT12_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT12_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT13_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT13_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT14_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT14_EXT));
    result.Set(Napi::String::From(info.Env(), "COLOR_ATTACHMENT15_WEBGL"),
               Napi::Number::From(info.Env(), GL_COLOR_ATTACHMENT15_EXT));

    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER0_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER0_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER1_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER1_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER2_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER2_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER3_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER3_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER4_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER4_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER5_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER5_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER6_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER6_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER7_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER7_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER8_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER8_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER9_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER9_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER10_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER10_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER11_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER11_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER12_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER12_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER13_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER13_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER14_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER14_EXT));
    result.Set(Napi::String::From(info.Env(), "DRAW_BUFFER15_WEBGL"),
               Napi::Number::From(info.Env(), GL_DRAW_BUFFER15_EXT));

    result.Set(Napi::String::From(info.Env(), "MAX_COLOR_ATTACHMENTS_WEBGL"),
               Napi::Number::From(info.Env(), GL_MAX_COLOR_ATTACHMENTS_EXT));
    result.Set(Napi::String::From(info.Env(), "MAX_DRAW_BUFFERS_WEBGL"),
               Napi::Number::From(info.Env(), GL_MAX_DRAW_BUFFERS_EXT));
    return result;
}

Napi::Value WebGLRenderingContext::CreateVertexArrayOES(const Napi::CallbackInfo &info) {
    GLuint array = 0;
    this->glGenVertexArraysOES(1, &array);
    this->registerGLObj(GLOBJECT_TYPE_VERTEX_ARRAY, array);

    return Napi::Number::From(info.Env(), array);
}

Napi::Value WebGLRenderingContext::DeleteVertexArrayOES(const Napi::CallbackInfo &info) {
    GLuint array = info[0].As<Napi::Number>().Uint32Value();
    this->unregisterGLObj(GLOBJECT_TYPE_VERTEX_ARRAY, array);
    this->glDeleteVertexArraysOES(1, &array);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::BindVertexArrayOES(const Napi::CallbackInfo &info) {
    GLuint array = 0;
    if (info[0].IsNumber()) {
        array = info[0].As<Napi::Number>().Uint32Value();
    }
    this->glBindVertexArrayOES(array);

    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::IsVertexArrayOES(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), this->glIsVertexArrayOES(info[0].As<Napi::Number>().Uint32Value()));
}


Napi::Value WebGLRenderingContext::GetShaderPrecisionFormat(const Napi::CallbackInfo &info) {
    GLenum shaderType = info[0].As<Napi::Number>().Int32Value();
    GLenum precisionType = info[0].As<Napi::Number>().Int32Value();
    GLint range[2];
    GLint precision;

    this->glGetShaderPrecisionFormat(shaderType, precisionType, range, &precision);
    Napi::Object result = Napi::Object::New(info.Env());
    result.Set("rangeMin", range[0]);
    result.Set("rangeMax", range[1]);
    result.Set("precision", precision);
    return result;
}

Napi::Value WebGLRenderingContext::StencilFuncSeparate(const Napi::CallbackInfo &info) {
    GLenum face = info[0].As<Napi::Number>().Int32Value();
    GLenum func = info[1].As<Napi::Number>().Int32Value();
    GLint ref = info[2].As<Napi::Number>().Int32Value();
    GLuint mask = info[3].As<Napi::Number>().Uint32Value();
    this->glStencilFuncSeparate(face, func, ref, mask);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::StencilMask(const Napi::CallbackInfo &info) {
    GLuint mask = info[0].As<Napi::Number>().Uint32Value();
    this->glStencilMask(mask);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::StencilMaskSeparate(const Napi::CallbackInfo &info) {
    GLenum face = info[0].As<Napi::Number>().Int32Value();
    GLuint mask = info[1].As<Napi::Number>().Uint32Value();
    this->glStencilMaskSeparate(face, mask);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::StencilOpSeparate(const Napi::CallbackInfo &info) {
    GLenum face = info[0].As<Napi::Number>().Int32Value();
    GLenum fail = info[1].As<Napi::Number>().Int32Value();
    GLenum zfail = info[2].As<Napi::Number>().Int32Value();
    GLenum zpass = info[3].As<Napi::Number>().Int32Value();
    this->glStencilOpSeparate(face, fail, zfail, zpass);
    return info.Env().Undefined();
}

Napi::Value WebGLRenderingContext::GetVertexAttrib(const Napi::CallbackInfo &info) {
    GLint index = info[0].As<Napi::Number>().Int32Value();
    GLenum pname = info[1].As<Napi::Number>().Int32Value();

    GLint value;

    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: {
            (this->glGetVertexAttribiv)(index, pname, &value);
            return Napi::Boolean::New(info.Env(), value != 0);
        }

        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
        case GL_VERTEX_ATTRIB_ARRAY_TYPE: {
            (this->glGetVertexAttribiv)(index, pname, &value);
            return Napi::Number::From(info.Env(), value);
        }

        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: {
            (this->glGetVertexAttribiv)(index, pname, &value);
            return Napi::Number::From(info.Env(), value);
        }

        case GL_CURRENT_VERTEX_ATTRIB: {
            float vextex_attribs[4];

            (this->glGetVertexAttribfv)(index, pname, vextex_attribs);

            Napi::Array arr = Napi::Array::New(info.Env());
            arr.Set(uint32_t(0), vextex_attribs[0]);
            arr.Set(1, vextex_attribs[1]);
            arr.Set(2, vextex_attribs[2]);
            arr.Set(3, vextex_attribs[3]);
        }

        default:
            this->setError(GL_INVALID_ENUM);
    }

    return info.Env().Null();
}

Napi::Value WebGLRenderingContext::GetVertexAttribOffset(const Napi::CallbackInfo &info) {
    GLint index = info[0].As<Napi::Number>().Int32Value();
    GLenum pname = info[1].As<Napi::Number>().Int32Value();
    void *ret = NULL;
    (this->glGetVertexAttribPointerv)(index, pname, &ret);

    GLuint offset = static_cast<GLuint>(reinterpret_cast<size_t>(ret));
    return Napi::Number::From(info.Env(), offset);
}

#define JS_METHOD(name, method)                                                                                        \
    InstanceMethod<method>(name,                                                                                       \
                           static_cast<napi_property_attributes>(napi_writable | napi_configurable | napi_enumerable))


#define JS_CONSTANT(x, v) InstanceValue(#x, Napi::Number::From(env, v), napi_enumerable)

#define JS_GL_CONSTANT(name) JS_CONSTANT(name, GL_##name)
using InstanceMethodCallback = Napi::Value (WebGLRenderingContext::*)(const Napi::CallbackInfo &);

Napi::Object WebGLRenderingContext::Init(Napi::Env env, Napi::Object exports) {

    std::map<const char *, InstanceMethodCallback> methods;
    methods["setError"] = &WebGLRenderingContext::SetError;
    methods["getError"] = &WebGLRenderingContext::GetError;
    methods["lastError"] = &WebGLRenderingContext::LastError;
    methods["getGLError"] = &WebGLRenderingContext::GetGLError;

    methods["_drawArraysInstanced"] = &WebGLRenderingContext::DrawArraysInstanced;
    methods["_drawElementsInstanced"] = &WebGLRenderingContext::DrawElementsInstanced;
    methods["_vertexAttribDivisor"] = &WebGLRenderingContext::VertexAttribDivisor;

    methods["getUniform"] = &WebGLRenderingContext::GetUniform;
    methods["uniform1f"] = &WebGLRenderingContext::Uniform1f;
    methods["uniform2f"] = &WebGLRenderingContext::Uniform2f;
    methods["uniform3f"] = &WebGLRenderingContext::Uniform3f;
    methods["uniform4f"] = &WebGLRenderingContext::Uniform4f;
    methods["uniform1i"] = &WebGLRenderingContext::Uniform1i;
    methods["uniform2i"] = &WebGLRenderingContext::Uniform2i;
    methods["uniform3i"] = &WebGLRenderingContext::Uniform3i;
    methods["uniform4i"] = &WebGLRenderingContext::Uniform4i;

    methods["pixelStorei"] = &WebGLRenderingContext::PixelStorei;
    methods["bindAttribLocation"] = &WebGLRenderingContext::BindAttribLocation;
    methods["getError"] = &WebGLRenderingContext::GetError;
    methods["drawArrays"] = &WebGLRenderingContext::DrawArrays;
    methods["uniformMatrix2fv"] = &WebGLRenderingContext::UniformMatrix2fv;
    methods["uniformMatrix3fv"] = &WebGLRenderingContext::UniformMatrix3fv;
    methods["uniformMatrix4fv"] = &WebGLRenderingContext::UniformMatrix4fv;
    methods["generateMipmap"] = &WebGLRenderingContext::GenerateMipmap;
    methods["getAttribLocation"] = &WebGLRenderingContext::GetAttribLocation;
    methods["depthFunc"] = &WebGLRenderingContext::DepthFunc;
    methods["viewport"] = &WebGLRenderingContext::Viewport;
    methods["createShader"] = &WebGLRenderingContext::CreateShader;
    methods["shaderSource"] = &WebGLRenderingContext::ShaderSource;
    methods["compileShader"] = &WebGLRenderingContext::CompileShader;
    methods["getShaderParameter"] = &WebGLRenderingContext::GetShaderParameter;
    methods["getShaderInfoLog"] = &WebGLRenderingContext::GetShaderInfoLog;
    methods["createProgram"] = &WebGLRenderingContext::CreateProgram;
    methods["attachShader"] = &WebGLRenderingContext::AttachShader;
    methods["linkProgram"] = &WebGLRenderingContext::LinkProgram;
    methods["getProgramParameter"] = &WebGLRenderingContext::GetProgramParameter;
    methods["getUniformLocation"] = &WebGLRenderingContext::GetUniformLocation;
    methods["clearColor"] = &WebGLRenderingContext::ClearColor;
    methods["clearDepth"] = &WebGLRenderingContext::ClearDepth;
    methods["disable"] = &WebGLRenderingContext::Disable;
    methods["createTexture"] = &WebGLRenderingContext::CreateTexture;
    methods["bindTexture"] = &WebGLRenderingContext::BindTexture;
    methods["texImage2D"] = &WebGLRenderingContext::TexImage2D;
    methods["texParameteri"] = &WebGLRenderingContext::TexParameteri;
    methods["texParameterf"] = &WebGLRenderingContext::TexParameterf;
    methods["clear"] = &WebGLRenderingContext::Clear;
    methods["useProgram"] = &WebGLRenderingContext::UseProgram;
    methods["createFramebuffer"] = &WebGLRenderingContext::CreateFramebuffer;
    methods["bindFramebuffer"] = &WebGLRenderingContext::BindFramebuffer;
    methods["framebufferTexture2D"] = &WebGLRenderingContext::FramebufferTexture2D;
    methods["createBuffer"] = &WebGLRenderingContext::CreateBuffer;
    methods["bindBuffer"] = &WebGLRenderingContext::BindBuffer;
    methods["bufferData"] = &WebGLRenderingContext::BufferData;
    methods["bufferSubData"] = &WebGLRenderingContext::BufferSubData;
    methods["enable"] = &WebGLRenderingContext::Enable;
    methods["blendEquation"] = &WebGLRenderingContext::BlendEquation;
    methods["blendFunc"] = &WebGLRenderingContext::BlendFunc;
    methods["enableVertexAttribArray"] = &WebGLRenderingContext::EnableVertexAttribArray;
    methods["vertexAttribPointer"] = &WebGLRenderingContext::VertexAttribPointer;
    methods["activeTexture"] = &WebGLRenderingContext::ActiveTexture;
    methods["drawElements"] = &WebGLRenderingContext::DrawElements;
    methods["flush"] = &WebGLRenderingContext::Flush;
    methods["finish"] = &WebGLRenderingContext::Finish;
    methods["vertexAttrib1f"] = &WebGLRenderingContext::VertexAttrib1f;
    methods["vertexAttrib2f"] = &WebGLRenderingContext::VertexAttrib2f;
    methods["vertexAttrib3f"] = &WebGLRenderingContext::VertexAttrib3f;
    methods["vertexAttrib4f"] = &WebGLRenderingContext::VertexAttrib4f;
    methods["blendColor"] = &WebGLRenderingContext::BlendColor;
    methods["blendEquationSeparate"] = &WebGLRenderingContext::BlendEquationSeparate;
    methods["blendFuncSeparate"] = &WebGLRenderingContext::BlendFuncSeparate;
    methods["clearStencil"] = &WebGLRenderingContext::ClearStencil;
    methods["colorMask"] = &WebGLRenderingContext::ColorMask;
    methods["copyTexImage2D"] = &WebGLRenderingContext::CopyTexImage2D;
    methods["copyTexSubImage2D"] = &WebGLRenderingContext::CopyTexSubImage2D;
    methods["cullFace"] = &WebGLRenderingContext::CullFace;
    methods["depthMask"] = &WebGLRenderingContext::DepthMask;
    methods["depthRange"] = &WebGLRenderingContext::DepthRange;
    methods["disableVertexAttribArray"] = &WebGLRenderingContext::DisableVertexAttribArray;
    methods["hint"] = &WebGLRenderingContext::Hint;
    methods["isEnabled"] = &WebGLRenderingContext::IsEnabled;
    methods["lineWidth"] = &WebGLRenderingContext::LineWidth;
    methods["polygonOffset"] = &WebGLRenderingContext::PolygonOffset;
    methods["scissor"] = &WebGLRenderingContext::Scissor;
    methods["stencilFunc"] = &WebGLRenderingContext::StencilFunc;
    methods["stencilFuncSeparate"] = &WebGLRenderingContext::StencilFuncSeparate;
    methods["stencilMask"] = &WebGLRenderingContext::StencilMask;
    methods["stencilMaskSeparate"] = &WebGLRenderingContext::StencilMaskSeparate;
    methods["stencilOp"] = &WebGLRenderingContext::StencilOp;
    methods["stencilOpSeparate"] = &WebGLRenderingContext::StencilOpSeparate;
    methods["bindRenderbuffer"] = &WebGLRenderingContext::BindRenderbuffer;
    methods["createRenderbuffer"] = &WebGLRenderingContext::CreateRenderbuffer;
    methods["deleteBuffer"] = &WebGLRenderingContext::DeleteBuffer;
    methods["deleteFramebuffer"] = &WebGLRenderingContext::DeleteFramebuffer;
    methods["deleteProgram"] = &WebGLRenderingContext::DeleteProgram;
    methods["deleteRenderbuffer"] = &WebGLRenderingContext::DeleteRenderbuffer;
    methods["deleteShader"] = &WebGLRenderingContext::DeleteShader;
    methods["deleteTexture"] = &WebGLRenderingContext::DeleteTexture;
    methods["detachShader"] = &WebGLRenderingContext::DetachShader;
    methods["framebufferRenderbuffer"] = &WebGLRenderingContext::FramebufferRenderbuffer;
    methods["getVertexAttribOffset"] = &WebGLRenderingContext::GetVertexAttribOffset;
    methods["isBuffer"] = &WebGLRenderingContext::IsBuffer;
    methods["isFramebuffer"] = &WebGLRenderingContext::IsFramebuffer;
    methods["isProgram"] = &WebGLRenderingContext::IsProgram;
    methods["isRenderbuffer"] = &WebGLRenderingContext::IsRenderbuffer;
    methods["isShader"] = &WebGLRenderingContext::IsShader;
    methods["isTexture"] = &WebGLRenderingContext::IsTexture;
    methods["renderbufferStorage"] = &WebGLRenderingContext::RenderbufferStorage;
    methods["getShaderSource"] = &WebGLRenderingContext::GetShaderSource;
    methods["validateProgram"] = &WebGLRenderingContext::ValidateProgram;
    methods["texSubImage2D"] = &WebGLRenderingContext::TexSubImage2D;
    methods["readPixels"] = &WebGLRenderingContext::ReadPixels;
    methods["getTexParameter"] = &WebGLRenderingContext::GetTexParameter;
    methods["getActiveAttrib"] = &WebGLRenderingContext::GetActiveAttrib;
    methods["getActiveUniform"] = &WebGLRenderingContext::GetActiveUniform;
    methods["getAttachedShaders"] = &WebGLRenderingContext::GetAttachedShaders;
    methods["getParameter"] = &WebGLRenderingContext::GetParameter;
    methods["getBufferParameter"] = &WebGLRenderingContext::GetBufferParameter;
    methods["getFramebufferAttachmentParameter"] = &WebGLRenderingContext::GetFramebufferAttachmentParameter;
    methods["getProgramInfoLog"] = &WebGLRenderingContext::GetProgramInfoLog;
    methods["getRenderbufferParameter"] = &WebGLRenderingContext::GetRenderbufferParameter;
    methods["getVertexAttrib"] = &WebGLRenderingContext::GetVertexAttrib;
    methods["getSupportedExtensions"] = &WebGLRenderingContext::GetSupportedExtensions;
    methods["getExtension"] = &WebGLRenderingContext::GetExtension;
    methods["checkFramebufferStatus"] = &WebGLRenderingContext::CheckFramebufferStatus;
    methods["getShaderPrecisionFormat"] = &WebGLRenderingContext::GetShaderPrecisionFormat;
    methods["frontFace"] = &WebGLRenderingContext::FrontFace;
    methods["sampleCoverage"] = &WebGLRenderingContext::SampleCoverage;
    methods["destroy"] = &WebGLRenderingContext::Destroy;
    methods["drawBuffersWEBGL"] = &WebGLRenderingContext::DrawBuffersWEBGL;
    methods["extWEBGL_draw_buffers"] = &WebGLRenderingContext::EXTWEBGL_draw_buffers;
    methods["createVertexArrayOES"] = &WebGLRenderingContext::CreateVertexArrayOES;
    methods["deleteVertexArrayOES"] = &WebGLRenderingContext::DeleteVertexArrayOES;
    methods["isVertexArrayOES"] = &WebGLRenderingContext::IsVertexArrayOES;
    methods["bindVertexArrayOES"] = &WebGLRenderingContext::BindVertexArrayOES;


    std::initializer_list<Napi::ClassPropertyDescriptor<WebGLRenderingContext>> properties = {
            InstanceValue("NO_ERROR", Napi::Number::From(env, GL_NO_ERROR), napi_enumerable),
            JS_GL_CONSTANT(INVALID_ENUM),
            JS_GL_CONSTANT(INVALID_VALUE),
            JS_GL_CONSTANT(INVALID_OPERATION),
            JS_GL_CONSTANT(OUT_OF_MEMORY),

            JS_CONSTANT(DEPTH_STENCIL, GL_DEPTH_STENCIL_OES),

            JS_CONSTANT(DEPTH_STENCIL_ATTACHMENT, 0x821A),
            JS_GL_CONSTANT(MAX_VERTEX_UNIFORM_VECTORS),
            JS_GL_CONSTANT(MAX_VARYING_VECTORS),
            JS_GL_CONSTANT(MAX_FRAGMENT_UNIFORM_VECTORS),
            JS_GL_CONSTANT(RGB565),
            JS_GL_CONSTANT(STENCIL_INDEX8),
            JS_GL_CONSTANT(FRAMEBUFFER_INCOMPLETE_DIMENSIONS),
            JS_GL_CONSTANT(DEPTH_BUFFER_BIT),
            JS_GL_CONSTANT(STENCIL_BUFFER_BIT),
            JS_GL_CONSTANT(COLOR_BUFFER_BIT),
            JS_GL_CONSTANT(POINTS),
            JS_GL_CONSTANT(LINES),
            JS_GL_CONSTANT(LINE_LOOP),
            JS_GL_CONSTANT(LINE_STRIP),
            JS_GL_CONSTANT(TRIANGLES),
            JS_GL_CONSTANT(TRIANGLE_STRIP),
            JS_GL_CONSTANT(TRIANGLE_FAN),
            JS_GL_CONSTANT(ZERO),
            JS_GL_CONSTANT(ONE),
            JS_GL_CONSTANT(SRC_COLOR),
            JS_GL_CONSTANT(ONE_MINUS_SRC_COLOR),
            JS_GL_CONSTANT(SRC_ALPHA),
            JS_GL_CONSTANT(ONE_MINUS_SRC_ALPHA),
            JS_GL_CONSTANT(DST_ALPHA),
            JS_GL_CONSTANT(ONE_MINUS_DST_ALPHA),
            JS_GL_CONSTANT(DST_COLOR),
            JS_GL_CONSTANT(ONE_MINUS_DST_COLOR),
            JS_GL_CONSTANT(SRC_ALPHA_SATURATE),
            JS_GL_CONSTANT(FUNC_ADD),
            JS_GL_CONSTANT(BLEND_EQUATION),
            JS_GL_CONSTANT(BLEND_EQUATION_RGB),
            JS_GL_CONSTANT(BLEND_EQUATION_ALPHA),
            JS_GL_CONSTANT(FUNC_SUBTRACT),
            JS_GL_CONSTANT(FUNC_REVERSE_SUBTRACT),
            JS_GL_CONSTANT(BLEND_DST_RGB),
            JS_GL_CONSTANT(BLEND_SRC_RGB),
            JS_GL_CONSTANT(BLEND_DST_ALPHA),
            JS_GL_CONSTANT(BLEND_SRC_ALPHA),
            JS_GL_CONSTANT(CONSTANT_COLOR),
            JS_GL_CONSTANT(ONE_MINUS_CONSTANT_COLOR),
            JS_GL_CONSTANT(CONSTANT_ALPHA),
            JS_GL_CONSTANT(ONE_MINUS_CONSTANT_ALPHA),
            JS_GL_CONSTANT(BLEND_COLOR),
            JS_GL_CONSTANT(ARRAY_BUFFER),
            JS_GL_CONSTANT(ELEMENT_ARRAY_BUFFER),
            JS_GL_CONSTANT(ARRAY_BUFFER_BINDING),
            JS_GL_CONSTANT(ELEMENT_ARRAY_BUFFER_BINDING),
            JS_GL_CONSTANT(STREAM_DRAW),
            JS_GL_CONSTANT(STATIC_DRAW),
            JS_GL_CONSTANT(DYNAMIC_DRAW),
            JS_GL_CONSTANT(BUFFER_SIZE),
            JS_GL_CONSTANT(BUFFER_USAGE),
            JS_GL_CONSTANT(CURRENT_VERTEX_ATTRIB),
            JS_GL_CONSTANT(FRONT),
            JS_GL_CONSTANT(BACK),
            JS_GL_CONSTANT(FRONT_AND_BACK),
            JS_GL_CONSTANT(TEXTURE_2D),
            JS_GL_CONSTANT(CULL_FACE),
            JS_GL_CONSTANT(BLEND),
            JS_GL_CONSTANT(DITHER),
            JS_GL_CONSTANT(STENCIL_TEST),
            JS_GL_CONSTANT(DEPTH_TEST),
            JS_GL_CONSTANT(SCISSOR_TEST),
            JS_GL_CONSTANT(POLYGON_OFFSET_FILL),
            JS_GL_CONSTANT(SAMPLE_ALPHA_TO_COVERAGE),
            JS_GL_CONSTANT(SAMPLE_COVERAGE),
            JS_GL_CONSTANT(CW),
            JS_GL_CONSTANT(CCW),
            JS_GL_CONSTANT(LINE_WIDTH),
            JS_GL_CONSTANT(ALIASED_POINT_SIZE_RANGE),
            JS_GL_CONSTANT(ALIASED_LINE_WIDTH_RANGE),
            JS_GL_CONSTANT(CULL_FACE_MODE),
            JS_GL_CONSTANT(FRONT_FACE),
            JS_GL_CONSTANT(DEPTH_RANGE),
            JS_GL_CONSTANT(DEPTH_WRITEMASK),
            JS_GL_CONSTANT(DEPTH_CLEAR_VALUE),
            JS_GL_CONSTANT(DEPTH_FUNC),
            JS_GL_CONSTANT(STENCIL_CLEAR_VALUE),
            JS_GL_CONSTANT(STENCIL_FUNC),
            JS_GL_CONSTANT(STENCIL_FAIL),
            JS_GL_CONSTANT(STENCIL_PASS_DEPTH_FAIL),
            JS_GL_CONSTANT(STENCIL_PASS_DEPTH_PASS),
            JS_GL_CONSTANT(STENCIL_REF),
            JS_GL_CONSTANT(STENCIL_VALUE_MASK),
            JS_GL_CONSTANT(STENCIL_WRITEMASK),
            JS_GL_CONSTANT(STENCIL_BACK_FUNC),
            JS_GL_CONSTANT(STENCIL_BACK_FAIL),
            JS_GL_CONSTANT(STENCIL_BACK_PASS_DEPTH_FAIL),
            JS_GL_CONSTANT(STENCIL_BACK_PASS_DEPTH_PASS),
            JS_GL_CONSTANT(STENCIL_BACK_REF),
            JS_GL_CONSTANT(STENCIL_BACK_VALUE_MASK),
            JS_GL_CONSTANT(STENCIL_BACK_WRITEMASK),
            JS_GL_CONSTANT(VIEWPORT),
            JS_GL_CONSTANT(SCISSOR_BOX),
            JS_GL_CONSTANT(COLOR_CLEAR_VALUE),
            JS_GL_CONSTANT(COLOR_WRITEMASK),
            JS_GL_CONSTANT(UNPACK_ALIGNMENT),
            JS_GL_CONSTANT(PACK_ALIGNMENT),
            JS_GL_CONSTANT(MAX_TEXTURE_SIZE),
            JS_GL_CONSTANT(MAX_VIEWPORT_DIMS),
            JS_GL_CONSTANT(SUBPIXEL_BITS),
            JS_GL_CONSTANT(RED_BITS),
            JS_GL_CONSTANT(GREEN_BITS),
            JS_GL_CONSTANT(BLUE_BITS),
            JS_GL_CONSTANT(ALPHA_BITS),
            JS_GL_CONSTANT(DEPTH_BITS),
            JS_GL_CONSTANT(STENCIL_BITS),
            JS_GL_CONSTANT(POLYGON_OFFSET_UNITS),
            JS_GL_CONSTANT(POLYGON_OFFSET_FACTOR),
            JS_GL_CONSTANT(TEXTURE_BINDING_2D),
            JS_GL_CONSTANT(SAMPLE_BUFFERS),
            JS_GL_CONSTANT(SAMPLES),
            JS_GL_CONSTANT(SAMPLE_COVERAGE_VALUE),
            JS_GL_CONSTANT(SAMPLE_COVERAGE_INVERT),
            JS_GL_CONSTANT(COMPRESSED_TEXTURE_FORMATS),
            JS_GL_CONSTANT(DONT_CARE),
            JS_GL_CONSTANT(FASTEST),
            JS_GL_CONSTANT(NICEST),
            JS_GL_CONSTANT(GENERATE_MIPMAP_HINT),
            JS_GL_CONSTANT(BYTE),
            JS_GL_CONSTANT(UNSIGNED_BYTE),
            JS_GL_CONSTANT(SHORT),
            JS_GL_CONSTANT(UNSIGNED_SHORT),
            JS_GL_CONSTANT(INT),
            JS_GL_CONSTANT(UNSIGNED_INT),
            JS_GL_CONSTANT(FLOAT),
            JS_GL_CONSTANT(DEPTH_COMPONENT),
            JS_GL_CONSTANT(ALPHA),
            JS_GL_CONSTANT(RGB),
            JS_GL_CONSTANT(RGBA),
            JS_GL_CONSTANT(LUMINANCE),
            JS_GL_CONSTANT(LUMINANCE_ALPHA),
            JS_GL_CONSTANT(UNSIGNED_SHORT_4_4_4_4),
            JS_GL_CONSTANT(UNSIGNED_SHORT_5_5_5_1),
            JS_GL_CONSTANT(UNSIGNED_SHORT_5_6_5),
            JS_GL_CONSTANT(FRAGMENT_SHADER),
            JS_GL_CONSTANT(VERTEX_SHADER),
            JS_GL_CONSTANT(MAX_VERTEX_ATTRIBS),
            JS_GL_CONSTANT(MAX_COMBINED_TEXTURE_IMAGE_UNITS),
            JS_GL_CONSTANT(MAX_VERTEX_TEXTURE_IMAGE_UNITS),
            JS_GL_CONSTANT(MAX_TEXTURE_IMAGE_UNITS),
            JS_GL_CONSTANT(SHADER_TYPE),
            JS_GL_CONSTANT(DELETE_STATUS),
            JS_GL_CONSTANT(LINK_STATUS),
            JS_GL_CONSTANT(VALIDATE_STATUS),
            JS_GL_CONSTANT(ATTACHED_SHADERS),
            JS_GL_CONSTANT(ACTIVE_UNIFORMS),
            JS_GL_CONSTANT(ACTIVE_ATTRIBUTES),
            JS_GL_CONSTANT(SHADING_LANGUAGE_VERSION),
            JS_GL_CONSTANT(CURRENT_PROGRAM),
            JS_GL_CONSTANT(NEVER),
            JS_GL_CONSTANT(LESS),
            JS_GL_CONSTANT(EQUAL),
            JS_GL_CONSTANT(LEQUAL),
            JS_GL_CONSTANT(GREATER),
            JS_GL_CONSTANT(NOTEQUAL),
            JS_GL_CONSTANT(GEQUAL),
            JS_GL_CONSTANT(ALWAYS),
            JS_GL_CONSTANT(KEEP),
            JS_GL_CONSTANT(REPLACE),
            JS_GL_CONSTANT(INCR),
            JS_GL_CONSTANT(DECR),
            JS_GL_CONSTANT(INVERT),
            JS_GL_CONSTANT(INCR_WRAP),
            JS_GL_CONSTANT(DECR_WRAP),
            JS_GL_CONSTANT(VENDOR),
            JS_GL_CONSTANT(RENDERER),
            JS_GL_CONSTANT(NEAREST),
            JS_GL_CONSTANT(LINEAR),
            JS_GL_CONSTANT(NEAREST_MIPMAP_NEAREST),
            JS_GL_CONSTANT(LINEAR_MIPMAP_NEAREST),
            JS_GL_CONSTANT(NEAREST_MIPMAP_LINEAR),
            JS_GL_CONSTANT(LINEAR_MIPMAP_LINEAR),
            JS_GL_CONSTANT(TEXTURE_MAG_FILTER),
            JS_GL_CONSTANT(TEXTURE_MIN_FILTER),
            JS_GL_CONSTANT(TEXTURE_WRAP_S),
            JS_GL_CONSTANT(TEXTURE_WRAP_T),
            JS_GL_CONSTANT(TEXTURE),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP),
            JS_GL_CONSTANT(TEXTURE_BINDING_CUBE_MAP),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP_POSITIVE_X),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP_NEGATIVE_X),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP_POSITIVE_Y),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP_NEGATIVE_Y),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP_POSITIVE_Z),
            JS_GL_CONSTANT(TEXTURE_CUBE_MAP_NEGATIVE_Z),
            JS_GL_CONSTANT(MAX_CUBE_MAP_TEXTURE_SIZE),
            JS_GL_CONSTANT(TEXTURE0),
            JS_GL_CONSTANT(TEXTURE1),
            JS_GL_CONSTANT(TEXTURE2),
            JS_GL_CONSTANT(TEXTURE3),
            JS_GL_CONSTANT(TEXTURE4),
            JS_GL_CONSTANT(TEXTURE5),
            JS_GL_CONSTANT(TEXTURE6),
            JS_GL_CONSTANT(TEXTURE7),
            JS_GL_CONSTANT(TEXTURE8),
            JS_GL_CONSTANT(TEXTURE9),
            JS_GL_CONSTANT(TEXTURE10),
            JS_GL_CONSTANT(TEXTURE11),
            JS_GL_CONSTANT(TEXTURE12),
            JS_GL_CONSTANT(TEXTURE13),
            JS_GL_CONSTANT(TEXTURE14),
            JS_GL_CONSTANT(TEXTURE15),
            JS_GL_CONSTANT(TEXTURE16),
            JS_GL_CONSTANT(TEXTURE17),
            JS_GL_CONSTANT(TEXTURE18),
            JS_GL_CONSTANT(TEXTURE19),
            JS_GL_CONSTANT(TEXTURE20),
            JS_GL_CONSTANT(TEXTURE21),
            JS_GL_CONSTANT(TEXTURE22),
            JS_GL_CONSTANT(TEXTURE23),
            JS_GL_CONSTANT(TEXTURE24),
            JS_GL_CONSTANT(TEXTURE25),
            JS_GL_CONSTANT(TEXTURE26),
            JS_GL_CONSTANT(TEXTURE27),
            JS_GL_CONSTANT(TEXTURE28),
            JS_GL_CONSTANT(TEXTURE29),
            JS_GL_CONSTANT(TEXTURE30),
            JS_GL_CONSTANT(TEXTURE31),
            JS_GL_CONSTANT(ACTIVE_TEXTURE),
            JS_GL_CONSTANT(REPEAT),
            JS_GL_CONSTANT(CLAMP_TO_EDGE),
            JS_GL_CONSTANT(MIRRORED_REPEAT),
            JS_GL_CONSTANT(FLOAT_VEC2),
            JS_GL_CONSTANT(FLOAT_VEC3),
            JS_GL_CONSTANT(FLOAT_VEC4),
            JS_GL_CONSTANT(INT_VEC2),
            JS_GL_CONSTANT(INT_VEC3),
            JS_GL_CONSTANT(INT_VEC4),
            JS_GL_CONSTANT(BOOL),
            JS_GL_CONSTANT(BOOL_VEC2),
            JS_GL_CONSTANT(BOOL_VEC3),
            JS_GL_CONSTANT(BOOL_VEC4),
            JS_GL_CONSTANT(FLOAT_MAT2),
            JS_GL_CONSTANT(FLOAT_MAT3),
            JS_GL_CONSTANT(FLOAT_MAT4),
            JS_GL_CONSTANT(SAMPLER_2D),
            JS_GL_CONSTANT(SAMPLER_CUBE),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_ENABLED),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_SIZE),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_STRIDE),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_TYPE),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_NORMALIZED),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_POINTER),
            JS_GL_CONSTANT(VERTEX_ATTRIB_ARRAY_BUFFER_BINDING),
            JS_GL_CONSTANT(COMPILE_STATUS),
            JS_GL_CONSTANT(LOW_FLOAT),
            JS_GL_CONSTANT(MEDIUM_FLOAT),
            JS_GL_CONSTANT(HIGH_FLOAT),
            JS_GL_CONSTANT(LOW_INT),
            JS_GL_CONSTANT(MEDIUM_INT),
            JS_GL_CONSTANT(HIGH_INT),
            JS_GL_CONSTANT(FRAMEBUFFER),
            JS_GL_CONSTANT(RENDERBUFFER),
            JS_GL_CONSTANT(RGBA4),
            JS_GL_CONSTANT(RGB5_A1),
            JS_GL_CONSTANT(DEPTH_COMPONENT16),
            JS_GL_CONSTANT(RENDERBUFFER_WIDTH),
            JS_GL_CONSTANT(RENDERBUFFER_HEIGHT),
            JS_GL_CONSTANT(RENDERBUFFER_INTERNAL_FORMAT),
            JS_GL_CONSTANT(RENDERBUFFER_RED_SIZE),
            JS_GL_CONSTANT(RENDERBUFFER_GREEN_SIZE),
            JS_GL_CONSTANT(RENDERBUFFER_BLUE_SIZE),
            JS_GL_CONSTANT(RENDERBUFFER_ALPHA_SIZE),
            JS_GL_CONSTANT(RENDERBUFFER_DEPTH_SIZE),
            JS_GL_CONSTANT(RENDERBUFFER_STENCIL_SIZE),
            JS_GL_CONSTANT(FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE),
            JS_GL_CONSTANT(FRAMEBUFFER_ATTACHMENT_OBJECT_NAME),
            JS_GL_CONSTANT(FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL),
            JS_GL_CONSTANT(FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE),
            JS_GL_CONSTANT(COLOR_ATTACHMENT0),
            JS_GL_CONSTANT(DEPTH_ATTACHMENT),
            JS_GL_CONSTANT(STENCIL_ATTACHMENT),
            JS_GL_CONSTANT(NONE),
            JS_GL_CONSTANT(FRAMEBUFFER_COMPLETE),
            JS_GL_CONSTANT(FRAMEBUFFER_INCOMPLETE_ATTACHMENT),
            JS_GL_CONSTANT(FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT),
            JS_GL_CONSTANT(FRAMEBUFFER_UNSUPPORTED),
            JS_GL_CONSTANT(FRAMEBUFFER_BINDING),
            JS_GL_CONSTANT(RENDERBUFFER_BINDING),
            JS_GL_CONSTANT(MAX_RENDERBUFFER_SIZE),
            JS_GL_CONSTANT(INVALID_FRAMEBUFFER_OPERATION),

            // WebGL-specific enums
            JS_CONSTANT(STENCIL_INDEX, 0x1901),
            JS_CONSTANT(UNPACK_FLIP_Y_WEBGL, 0x9240),
            JS_CONSTANT(UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0x9241),
            JS_CONSTANT(CONTEXT_LOST_WEBGL, 0x9242),
            JS_CONSTANT(UNPACK_COLORSPACE_CONVERSION_WEBGL, 0x9243),
            JS_CONSTANT(BROWSER_DEFAULT_WEBGL, 0x9244),
            JS_CONSTANT(VERSION, 0x1F02),
            JS_CONSTANT(IMPLEMENTATION_COLOR_READ_TYPE, 0x8B9A),
            JS_CONSTANT(IMPLEMENTATION_COLOR_READ_FORMAT, 0x8B9B),
    };

#define DEBUG_CALLS 0


    // This method is used to hook the accessor and method callbacks
    Napi::Function func = DefineClass(env, "WebGLRenderingContext", properties);


    auto prototype = func.Get("prototype").As<Napi::Object>();
    prototype.GetPropertyNames();
    for (auto const &[name, methodPointer]: methods) {
        prototype.DefineProperty(Napi::PropertyDescriptor::Function(
                name,
                [methodPointer, name](const Napi::CallbackInfo &info) -> Napi::Value {
                    WebGLRenderingContext *obj =
                            Napi::ObjectWrap<WebGLRenderingContext>::Unwrap(info.This().As<Napi::Object>());
                    obj->setActive();
#if DEBUG_CALLS
                    std::cout << "C++ invoke " << name;
                    std::stringstream os;
                    os << "(";
                    for (size_t k = 0; k < info.Length(); k++) {
                        os << info[k].ToString().Utf8Value() << ",";
                    }

                    os << ")";
                    std::cout << os.str() << std::endl;
#endif
                    auto retval = (obj->*methodPointer)(info);

#if DEBUG_CALLS
                    std::cout << "C++ " << "err: [" << obj->glGetError() << "]" << std::endl;
#endif
                    return retval;
                },
                static_cast<napi_property_attributes>(napi_enumerable), env));
    }

    Napi::FunctionReference *constructor = new Napi::FunctionReference();

    // Create a persistent reference to the class constructor. This will allow
    // a function called on a class prototype and a function
    // called on instance of a class to be distinguished from each other.
    *constructor = Napi::Persistent(func);
    exports.Set("WebGLRenderingContext", func);
    exports.Set("cleanup", Napi::Function::New(env, DisposeAll));
    // Store the constructor as the add-on instance data. This will allow this
    // add-on to support multiple instances of itself running on multiple worker
    // threads, as well as multiple instances of itself running in different
    // contexts on the same thread.
    //
    // By default, the value set on the environment here will be destroyed when
    // the add-on is unloaded using the `delete` operator, but it is also
    // possible to supply a custom deleter.
    env.SetInstanceData<Napi::FunctionReference>(constructor);

    return exports;
}


void WebGLRenderingContext::initPointers() {
    glDrawArraysInstanced =
            reinterpret_cast<PFNGLDRAWARRAYSINSTANCEDANGLEPROC>(eglGetProcAddress("glDrawArraysInstancedANGLE"));
    glDrawElementsInstanced =
            reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDANGLEPROC>(eglGetProcAddress("glDrawElementsInstancedANGLE"));
    glVertexAttribDivisor =
            reinterpret_cast<PFNGLVERTEXATTRIBDIVISORANGLEPROC>(eglGetProcAddress("glVertexAttribDivisorANGLE"));

    glUniform1f = reinterpret_cast<PFNGLUNIFORM1FPROC>(eglGetProcAddress("glUniform1f"));
    glUniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(eglGetProcAddress("glUniform2f"));
    glUniform3f = reinterpret_cast<PFNGLUNIFORM3FPROC>(eglGetProcAddress("glUniform3f"));
    glUniform4f = reinterpret_cast<PFNGLUNIFORM4FPROC>(eglGetProcAddress("glUniform4f"));
    glUniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(eglGetProcAddress("glUniform1i"));
    glUniform2i = reinterpret_cast<PFNGLUNIFORM2IPROC>(eglGetProcAddress("glUniform2i"));
    glUniform3i = reinterpret_cast<PFNGLUNIFORM3IPROC>(eglGetProcAddress("glUniform3i"));
    glUniform4i = reinterpret_cast<PFNGLUNIFORM4IPROC>(eglGetProcAddress("glUniform4i"));
    glPixelStorei = reinterpret_cast<PFNGLPIXELSTOREIPROC>(eglGetProcAddress("glPixelStorei"));
    glBindAttribLocation = reinterpret_cast<PFNGLBINDATTRIBLOCATIONPROC>(eglGetProcAddress("glBindAttribLocation"));
    glDrawArrays = reinterpret_cast<PFNGLDRAWARRAYSPROC>(eglGetProcAddress("glDrawArrays"));
    glUniformMatrix2fv = reinterpret_cast<PFNGLUNIFORMMATRIX2FVPROC>(eglGetProcAddress("glUniformMatrix2fv"));
    glUniformMatrix3fv = reinterpret_cast<PFNGLUNIFORMMATRIX3FVPROC>(eglGetProcAddress("glUniformMatrix3fv"));
    glUniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(eglGetProcAddress("glUniformMatrix4fv"));
    glGenerateMipmap = reinterpret_cast<PFNGLGENERATEMIPMAPPROC>(eglGetProcAddress("glGenerateMipmap"));
    glGetAttribLocation = reinterpret_cast<PFNGLGETATTRIBLOCATIONPROC>(eglGetProcAddress("glGetAttribLocation"));
    glDepthFunc = reinterpret_cast<PFNGLDEPTHFUNCPROC>(eglGetProcAddress("glDepthFunc"));
    glViewport = reinterpret_cast<PFNGLVIEWPORTPROC>(eglGetProcAddress("glViewport"));
    glCreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(eglGetProcAddress("glCreateShader"));
    glShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(eglGetProcAddress("glShaderSource"));
    glCompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(eglGetProcAddress("glCompileShader"));
    glGetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(eglGetProcAddress("glGetShaderInfoLog"));
    glCreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(eglGetProcAddress("glCreateProgram"));
    glAttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(eglGetProcAddress("glAttachShader"));
    glLinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(eglGetProcAddress("glLinkProgram"));
    glGetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(eglGetProcAddress("glGetUniformLocation"));
    glClearColor = reinterpret_cast<PFNGLCLEARCOLORPROC>(eglGetProcAddress("glClearColor"));
    glClearDepthf = reinterpret_cast<PFNGLCLEARDEPTHFPROC>(eglGetProcAddress("glClearDepthf"));
    glDisable = reinterpret_cast<PFNGLDISABLEPROC>(eglGetProcAddress("glDisable"));
    glEnable = reinterpret_cast<PFNGLENABLEPROC>(eglGetProcAddress("glEnable"));
    glGenTextures = reinterpret_cast<PFNGLGENTEXTURESPROC>(eglGetProcAddress("glGenTextures"));
    glBindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC>(eglGetProcAddress("glBindTexture"));
    glTexImage2D = reinterpret_cast<PFNGLTEXIMAGE2DPROC>(eglGetProcAddress("glTexImage2D"));
    glTexParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC>(eglGetProcAddress("glTexParameteri"));
    glTexParameterf = reinterpret_cast<PFNGLTEXPARAMETERFPROC>(eglGetProcAddress("glTexParameterf"));
    glClear = reinterpret_cast<PFNGLCLEARPROC>(eglGetProcAddress("glClear"));
    glUseProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(eglGetProcAddress("glUseProgram"));
    glGenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(eglGetProcAddress("glGenBuffers"));
    glBindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(eglGetProcAddress("glBindBuffer"));
    glGenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(eglGetProcAddress("glGenFramebuffers"));
    glBindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(eglGetProcAddress("glBindFramebuffer"));
    glFramebufferTexture2D =
            reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(eglGetProcAddress("glFramebufferTexture2D"));
    glBufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(eglGetProcAddress("glBufferData"));
    glBufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(eglGetProcAddress("glBufferSubData"));
    glBlendEquation = reinterpret_cast<PFNGLBLENDEQUATIONPROC>(eglGetProcAddress("glBlendEquation"));
    glBlendFunc = reinterpret_cast<PFNGLBLENDFUNCPROC>(eglGetProcAddress("glBlendFunc"));
    glEnableVertexAttribArray =
            reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(eglGetProcAddress("glEnableVertexAttribArray"));
    glVertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(eglGetProcAddress("glVertexAttribPointer"));
    glActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(eglGetProcAddress("glActiveTexture"));
    glDrawElements = reinterpret_cast<PFNGLDRAWELEMENTSPROC>(eglGetProcAddress("glDrawElements"));
    glFlush = reinterpret_cast<PFNGLFLUSHPROC>(eglGetProcAddress("glFlush"));
    glFinish = reinterpret_cast<PFNGLFINISHPROC>(eglGetProcAddress("glFinish"));
    glVertexAttrib1f = reinterpret_cast<PFNGLVERTEXATTRIB1FPROC>(eglGetProcAddress("glVertexAttrib1f"));
    glVertexAttrib2f = reinterpret_cast<PFNGLVERTEXATTRIB2FPROC>(eglGetProcAddress("glVertexAttrib2f"));
    glVertexAttrib3f = reinterpret_cast<PFNGLVERTEXATTRIB3FPROC>(eglGetProcAddress("glVertexAttrib3f"));
    glVertexAttrib4f = reinterpret_cast<PFNGLVERTEXATTRIB4FPROC>(eglGetProcAddress("glVertexAttrib4f"));
    glBlendColor = reinterpret_cast<PFNGLBLENDCOLORPROC>(eglGetProcAddress("glBlendColor"));
    glBlendEquationSeparate =
            reinterpret_cast<PFNGLBLENDEQUATIONSEPARATEPROC>(eglGetProcAddress("glBlendEquationSeparate"));
    glBlendFuncSeparate = reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(eglGetProcAddress("glBlendFuncSeparate"));
    glClearStencil = reinterpret_cast<PFNGLCLEARSTENCILPROC>(eglGetProcAddress("glClearStencil"));
    glColorMask = reinterpret_cast<PFNGLCOLORMASKPROC>(eglGetProcAddress("glColorMask"));
    glCopyTexImage2D = reinterpret_cast<PFNGLCOPYTEXIMAGE2DPROC>(eglGetProcAddress("glCopyTexImage2D"));
    glCopyTexSubImage2D = reinterpret_cast<PFNGLCOPYTEXSUBIMAGE2DPROC>(eglGetProcAddress("glCopyTexSubImage2D"));
    glCullFace = reinterpret_cast<PFNGLCULLFACEPROC>(eglGetProcAddress("glCullFace"));
    glDepthMask = reinterpret_cast<PFNGLDEPTHMASKPROC>(eglGetProcAddress("glDepthMask"));
    glDepthRangef = reinterpret_cast<PFNGLDEPTHRANGEFPROC>(eglGetProcAddress("glDepthRangef"));
    glHint = reinterpret_cast<PFNGLHINTPROC>(eglGetProcAddress("glHint"));
    glIsEnabled = reinterpret_cast<PFNGLISENABLEDPROC>(eglGetProcAddress("glIsEnabled"));
    glLineWidth = reinterpret_cast<PFNGLLINEWIDTHPROC>(eglGetProcAddress("glLineWidth"));
    glPolygonOffset = reinterpret_cast<PFNGLPOLYGONOFFSETPROC>(eglGetProcAddress("glPolygonOffset"));
    glGetShaderPrecisionFormat =
            reinterpret_cast<PFNGLGETSHADERPRECISIONFORMATPROC>(eglGetProcAddress("glGetShaderPrecisionFormat"));
    glStencilFunc = reinterpret_cast<PFNGLSTENCILFUNCPROC>(eglGetProcAddress("glStencilFunc"));
    glStencilFuncSeparate = reinterpret_cast<PFNGLSTENCILFUNCSEPARATEPROC>(eglGetProcAddress("glStencilFuncSeparate"));
    glStencilMask = reinterpret_cast<PFNGLSTENCILMASKPROC>(eglGetProcAddress("glStencilMask"));
    glStencilMaskSeparate = reinterpret_cast<PFNGLSTENCILMASKSEPARATEPROC>(eglGetProcAddress("glStencilMaskSeparate"));
    glStencilOp = reinterpret_cast<PFNGLSTENCILOPPROC>(eglGetProcAddress("glStencilOp"));
    glStencilOpSeparate = reinterpret_cast<PFNGLSTENCILOPSEPARATEPROC>(eglGetProcAddress("glStencilOpSeparate"));
    glScissor = reinterpret_cast<PFNGLSCISSORPROC>(eglGetProcAddress("glScissor"));
    glBindRenderbuffer = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(eglGetProcAddress("glBindRenderbuffer"));
    glGenRenderbuffers = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(eglGetProcAddress("glGenRenderbuffers"));
    glFramebufferRenderbuffer =
            reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(eglGetProcAddress("glFramebufferRenderbuffer"));
    glDeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(eglGetProcAddress("glDeleteBuffers"));
    glDeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(eglGetProcAddress("glDeleteFramebuffers"));
    glDeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(eglGetProcAddress("glDeleteProgram"));
    glDeleteRenderbuffers = reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(eglGetProcAddress("glDeleteRenderbuffers"));
    glDeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(eglGetProcAddress("glDeleteShader"));
    glDeleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC>(eglGetProcAddress("glDeleteTextures"));
    glDetachShader = reinterpret_cast<PFNGLDETACHSHADERPROC>(eglGetProcAddress("glDetachShader"));
    glDisableVertexAttribArray =
            reinterpret_cast<PFNGLDISABLEVERTEXATTRIBARRAYPROC>(eglGetProcAddress("glDisableVertexAttribArray"));
    glIsBuffer = reinterpret_cast<PFNGLISBUFFERPROC>(eglGetProcAddress("glIsBuffer"));
    glIsFramebuffer = reinterpret_cast<PFNGLISFRAMEBUFFERPROC>(eglGetProcAddress("glIsFramebuffer"));
    glIsProgram = reinterpret_cast<PFNGLISPROGRAMPROC>(eglGetProcAddress("glIsProgram"));
    glIsRenderbuffer = reinterpret_cast<PFNGLISRENDERBUFFERPROC>(eglGetProcAddress("glIsRenderbuffer"));
    glIsShader = reinterpret_cast<PFNGLISSHADERPROC>(eglGetProcAddress("glIsShader"));
    glIsTexture = reinterpret_cast<PFNGLISTEXTUREPROC>(eglGetProcAddress("glIsTexture"));
    glRenderbufferStorage = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(eglGetProcAddress("glRenderbufferStorage"));
    glGetShaderSource = reinterpret_cast<PFNGLGETSHADERSOURCEPROC>(eglGetProcAddress("glGetShaderSource"));
    glValidateProgram = reinterpret_cast<PFNGLVALIDATEPROGRAMPROC>(eglGetProcAddress("glValidateProgram"));
    glTexSubImage2D = reinterpret_cast<PFNGLTEXSUBIMAGE2DPROC>(eglGetProcAddress("glTexSubImage2D"));
    glReadPixels = reinterpret_cast<PFNGLREADPIXELSPROC>(eglGetProcAddress("glReadPixels"));
    glGetActiveAttrib = reinterpret_cast<PFNGLGETACTIVEATTRIBPROC>(eglGetProcAddress("glGetActiveAttrib"));
    glGetActiveUniform = reinterpret_cast<PFNGLGETACTIVEUNIFORMPROC>(eglGetProcAddress("glGetActiveUniform"));
    glGetAttachedShaders = reinterpret_cast<PFNGLGETATTACHEDSHADERSPROC>(eglGetProcAddress("glGetAttachedShaders"));
    glGetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(eglGetProcAddress("glGetProgramInfoLog"));
    glGetRenderbufferParameteriv =
            reinterpret_cast<PFNGLGETRENDERBUFFERPARAMETERIVPROC>(eglGetProcAddress("glGetRenderbufferParameteriv"));
    glCheckFramebufferStatus =
            reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(eglGetProcAddress("glCheckFramebufferStatus"));
    glFrontFace = reinterpret_cast<PFNGLFRONTFACEPROC>(eglGetProcAddress("glFrontFace"));
    glSampleCoverage = reinterpret_cast<PFNGLSAMPLECOVERAGEPROC>(eglGetProcAddress("glSampleCoverage"));
    glGetUniformiv = reinterpret_cast<PFNGLGETUNIFORMIVPROC>(eglGetProcAddress("glGetUniformiv"));
    glGetUniformfv = reinterpret_cast<PFNGLGETUNIFORMFVPROC>(eglGetProcAddress("glGetUniformfv"));
    glGetVertexAttribiv = reinterpret_cast<PFNGLGETVERTEXATTRIBIVPROC>(eglGetProcAddress("glGetVertexAttribiv"));
    glGetVertexAttribfv = reinterpret_cast<PFNGLGETVERTEXATTRIBFVPROC>(eglGetProcAddress("glGetVertexAttribfv"));
    glGetFramebufferAttachmentParameteriv = reinterpret_cast<PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC>(
            eglGetProcAddress("glGetFramebufferAttachmentParameteriv"));
    glGetBufferParameteriv =
            reinterpret_cast<PFNGLGETBUFFERPARAMETERIVPROC>(eglGetProcAddress("glGetBufferParameteriv"));
    glGetFloatv = reinterpret_cast<PFNGLGETFLOATVPROC>(eglGetProcAddress("glGetFloatv"));
    glGetIntegerv = reinterpret_cast<PFNGLGETINTEGERVPROC>(eglGetProcAddress("glGetIntegerv"));
    glGetBooleanv = reinterpret_cast<PFNGLGETBOOLEANVPROC>(eglGetProcAddress("glGetBooleanv"));
    glGetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(eglGetProcAddress("glGetProgramiv"));
    glGetTexParameterfv = reinterpret_cast<PFNGLGETTEXPARAMETERFVPROC>(eglGetProcAddress("glGetTexParameterfv"));
    glGetTexParameteriv = reinterpret_cast<PFNGLGETTEXPARAMETERIVPROC>(eglGetProcAddress("glGetTexParameteriv"));
    glGetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(eglGetProcAddress("glGetShaderiv"));
    glGetVertexAttribPointerv =
            reinterpret_cast<PFNGLGETVERTEXATTRIBPOINTERVPROC>(eglGetProcAddress("glGetVertexAttribPointerv"));
    glGetString = reinterpret_cast<PFNGLGETSTRINGPROC>(eglGetProcAddress("glGetString"));
    glGetError = reinterpret_cast<PFNGLGETERRORPROC>(eglGetProcAddress("glGetError"));
    glDrawBuffersEXT = reinterpret_cast<PFNGLDRAWBUFFERSEXTPROC>(eglGetProcAddress("glDrawBuffersEXT"));
    glGenVertexArraysOES = reinterpret_cast<PFNGLGENVERTEXARRAYSOESPROC>(eglGetProcAddress("glGenVertexArraysOES"));
    glDeleteVertexArraysOES =
            reinterpret_cast<PFNGLDELETEVERTEXARRAYSOESPROC>(eglGetProcAddress("glDeleteVertexArraysOES"));
    glIsVertexArrayOES = reinterpret_cast<PFNGLISVERTEXARRAYOESPROC>(eglGetProcAddress("glIsVertexArrayOES"));
    glBindVertexArrayOES = reinterpret_cast<PFNGLBINDVERTEXARRAYOESPROC>(eglGetProcAddress("glBindVertexArrayOES"));
}
