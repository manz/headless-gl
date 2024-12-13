#include <napi.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <map>

enum GLObjectType {
    GLOBJECT_TYPE_BUFFER,
    GLOBJECT_TYPE_FRAMEBUFFER,
    GLOBJECT_TYPE_PROGRAM,
    GLOBJECT_TYPE_RENDERBUFFER,
    GLOBJECT_TYPE_SHADER,
    GLOBJECT_TYPE_TEXTURE,
    GLOBJECT_TYPE_VERTEX_ARRAY,
};

enum GLContextState { GLCONTEXT_STATE_INIT, GLCONTEXT_STATE_OK, GLCONTEXT_STATE_DESTROY, GLCONTEXT_STATE_ERROR };

class WebGLRenderingContext : public Napi::ObjectWrap<WebGLRenderingContext> {
    static bool HAS_DISPLAY;
    static EGLDisplay DISPLAY;


    EGLContext context;
    EGLConfig config;
    EGLSurface surface;
    GLContextState state;

    // Pixel storage flags
    bool unpack_flip_y;
    bool unpack_premultiply_alpha;
    GLint unpack_colorspace_conversion;
    GLint unpack_alignment;

    // A list of object references, need do destroy them at program exit
    std::map<std::pair<GLuint, GLObjectType>, bool> objects;
    void registerGLObj(GLObjectType type, GLuint obj) { objects[std::make_pair(obj, type)] = true; }
    void unregisterGLObj(GLObjectType type, GLuint obj) { objects.erase(std::make_pair(obj, type)); }

    // Context list
    WebGLRenderingContext *next, *prev;
    static WebGLRenderingContext *CONTEXT_LIST_HEAD;
    void registerContext() {
        if (CONTEXT_LIST_HEAD) {
            CONTEXT_LIST_HEAD->prev = this;
        }
        next = CONTEXT_LIST_HEAD;
        prev = NULL;
        CONTEXT_LIST_HEAD = this;
    }
    void unregisterContext() {
        if (next) {
            next->prev = this->prev;
        }
        if (prev) {
            prev->next = this->next;
        }
        if (CONTEXT_LIST_HEAD == this) {
            CONTEXT_LIST_HEAD = this->next;
        }
        next = prev = NULL;
    }

    void _initialize(GLint width, GLint height);

    //    static void DefineEnumerableMethods(
    //        Napi::Object& prototype,
    //        const std::vector<std::pair<std::string, WebGLRenderingContext::InstanceMethodCallback>>& methods) {
    //        for (const auto& [name, callback] : methods) {
    //            prototype.DefineProperty(
    //                Napi::PropertyDescriptor::Function(name, callback, napi_enumerable)
    //            );
    //        }
    //    }
public:
    WebGLRenderingContext(const Napi::CallbackInfo &info);

    // Context validation
    static WebGLRenderingContext *ACTIVE;
    bool setActive();

    // Unpacks a buffer full of pixels into memory
    unsigned char *unpackPixels(GLenum type, GLenum format, GLint width, GLint height, unsigned char *pixels);

    // Error handling
    GLenum lastError;
    void setError(GLenum error);
    GLenum getError();

    static Napi::Object Init(Napi::Env env, Napi::Object exports);

    Napi::Value SetError(const Napi::CallbackInfo &info);
    Napi::Value GetError(const Napi::CallbackInfo &info);
    Napi::Value LastError(const Napi::CallbackInfo &info);
	Napi::Value GetGLError(const Napi::CallbackInfo &info);

    // Preferred depth format
    GLenum preferredDepth;

    // Destructors
    void dispose();
    Napi::Value Destroy(const Napi::CallbackInfo &info);

    static Napi::Value DisposeAll(const Napi::CallbackInfo &info);

    Napi::Value VertexAttribDivisor(const Napi::CallbackInfo &info);
    Napi::Value DrawArraysInstanced(const Napi::CallbackInfo &info);
    Napi::Value DrawElementsInstanced(const Napi::CallbackInfo &info);

    Napi::Value Uniform1f(const Napi::CallbackInfo &info);
    Napi::Value Uniform2f(const Napi::CallbackInfo &info);
    Napi::Value Uniform3f(const Napi::CallbackInfo &info);
    Napi::Value Uniform4f(const Napi::CallbackInfo &info);
    Napi::Value Uniform1i(const Napi::CallbackInfo &info);
    Napi::Value Uniform2i(const Napi::CallbackInfo &info);
    Napi::Value Uniform3i(const Napi::CallbackInfo &info);
    Napi::Value Uniform4i(const Napi::CallbackInfo &info);

    Napi::Value PixelStorei(const Napi::CallbackInfo &info);
    Napi::Value BindAttribLocation(const Napi::CallbackInfo &info);
    Napi::Value DrawArrays(const Napi::CallbackInfo &info);
    Napi::Value UniformMatrix2fv(const Napi::CallbackInfo &info);
    Napi::Value UniformMatrix3fv(const Napi::CallbackInfo &info);
    Napi::Value UniformMatrix4fv(const Napi::CallbackInfo &info);
    Napi::Value GenerateMipmap(const Napi::CallbackInfo &info);
    Napi::Value GetAttribLocation(const Napi::CallbackInfo &info);
    Napi::Value DepthFunc(const Napi::CallbackInfo &info);
    Napi::Value Viewport(const Napi::CallbackInfo &info);
    Napi::Value CreateShader(const Napi::CallbackInfo &info);
    Napi::Value ShaderSource(const Napi::CallbackInfo &info);
    Napi::Value CompileShader(const Napi::CallbackInfo &info);
    Napi::Value GetShaderParameter(const Napi::CallbackInfo &info);
    Napi::Value GetShaderInfoLog(const Napi::CallbackInfo &info);
    Napi::Value CreateProgram(const Napi::CallbackInfo &info);
    Napi::Value AttachShader(const Napi::CallbackInfo &info);
    Napi::Value LinkProgram(const Napi::CallbackInfo &info);
    Napi::Value GetProgramParameter(const Napi::CallbackInfo &info);
    Napi::Value GetUniformLocation(const Napi::CallbackInfo &info);
    Napi::Value ClearColor(const Napi::CallbackInfo &info);
    Napi::Value ClearDepth(const Napi::CallbackInfo &info);
    Napi::Value Disable(const Napi::CallbackInfo &info);
    Napi::Value Enable(const Napi::CallbackInfo &info);
    Napi::Value CreateTexture(const Napi::CallbackInfo &info);
    Napi::Value BindTexture(const Napi::CallbackInfo &info);
    Napi::Value TexImage2D(const Napi::CallbackInfo &info);
    Napi::Value TexParameteri(const Napi::CallbackInfo &info);
    Napi::Value TexParameterf(const Napi::CallbackInfo &info);
    Napi::Value Clear(const Napi::CallbackInfo &info);
    Napi::Value UseProgram(const Napi::CallbackInfo &info);
    Napi::Value CreateBuffer(const Napi::CallbackInfo &info);
    Napi::Value BindBuffer(const Napi::CallbackInfo &info);
    Napi::Value CreateFramebuffer(const Napi::CallbackInfo &info);
    Napi::Value BindFramebuffer(const Napi::CallbackInfo &info);
    Napi::Value FramebufferTexture2D(const Napi::CallbackInfo &info);
    Napi::Value BufferData(const Napi::CallbackInfo &info);
    Napi::Value BufferSubData(const Napi::CallbackInfo &info);
    Napi::Value BlendEquation(const Napi::CallbackInfo &info);
    Napi::Value BlendFunc(const Napi::CallbackInfo &info);
    Napi::Value EnableVertexAttribArray(const Napi::CallbackInfo &info);
    Napi::Value VertexAttribPointer(const Napi::CallbackInfo &info);
    Napi::Value ActiveTexture(const Napi::CallbackInfo &info);
    Napi::Value DrawElements(const Napi::CallbackInfo &info);
    Napi::Value Flush(const Napi::CallbackInfo &info);
    Napi::Value Finish(const Napi::CallbackInfo &info);

    Napi::Value VertexAttrib1f(const Napi::CallbackInfo &info);
    Napi::Value VertexAttrib2f(const Napi::CallbackInfo &info);
    Napi::Value VertexAttrib3f(const Napi::CallbackInfo &info);
    Napi::Value VertexAttrib4f(const Napi::CallbackInfo &info);

    Napi::Value BlendColor(const Napi::CallbackInfo &info);
    Napi::Value BlendEquationSeparate(const Napi::CallbackInfo &info);
    Napi::Value BlendFuncSeparate(const Napi::CallbackInfo &info);
    Napi::Value ClearStencil(const Napi::CallbackInfo &info);
    Napi::Value ColorMask(const Napi::CallbackInfo &info);
    Napi::Value CopyTexImage2D(const Napi::CallbackInfo &info);
    Napi::Value CopyTexSubImage2D(const Napi::CallbackInfo &info);
    Napi::Value CullFace(const Napi::CallbackInfo &info);
    Napi::Value DepthMask(const Napi::CallbackInfo &info);
    Napi::Value DepthRange(const Napi::CallbackInfo &info);
    Napi::Value Hint(const Napi::CallbackInfo &info);
    Napi::Value IsEnabled(const Napi::CallbackInfo &info);
    Napi::Value LineWidth(const Napi::CallbackInfo &info);
    Napi::Value PolygonOffset(const Napi::CallbackInfo &info);

    Napi::Value GetShaderPrecisionFormat(const Napi::CallbackInfo &info);

    Napi::Value StencilFunc(const Napi::CallbackInfo &info);
    Napi::Value StencilFuncSeparate(const Napi::CallbackInfo &info);
    Napi::Value StencilMask(const Napi::CallbackInfo &info);
    Napi::Value StencilMaskSeparate(const Napi::CallbackInfo &info);
    Napi::Value StencilOp(const Napi::CallbackInfo &info);
    Napi::Value StencilOpSeparate(const Napi::CallbackInfo &info);

    Napi::Value Scissor(const Napi::CallbackInfo &info);

    Napi::Value BindRenderbuffer(const Napi::CallbackInfo &info);
    Napi::Value CreateRenderbuffer(const Napi::CallbackInfo &info);
    Napi::Value FramebufferRenderbuffer(const Napi::CallbackInfo &info);

    Napi::Value DeleteBuffer(const Napi::CallbackInfo &info);
    Napi::Value DeleteFramebuffer(const Napi::CallbackInfo &info);
    Napi::Value DeleteProgram(const Napi::CallbackInfo &info);
    Napi::Value DeleteRenderbuffer(const Napi::CallbackInfo &info);
    Napi::Value DeleteShader(const Napi::CallbackInfo &info);
    Napi::Value DeleteTexture(const Napi::CallbackInfo &info);
    Napi::Value DetachShader(const Napi::CallbackInfo &info);

    Napi::Value GetVertexAttribOffset(const Napi::CallbackInfo &info);
    Napi::Value DisableVertexAttribArray(const Napi::CallbackInfo &info);

    Napi::Value IsBuffer(const Napi::CallbackInfo &info);
    Napi::Value IsFramebuffer(const Napi::CallbackInfo &info);
    Napi::Value IsProgram(const Napi::CallbackInfo &info);
    Napi::Value IsRenderbuffer(const Napi::CallbackInfo &info);
    Napi::Value IsShader(const Napi::CallbackInfo &info);
    Napi::Value IsTexture(const Napi::CallbackInfo &info);

    Napi::Value RenderbufferStorage(const Napi::CallbackInfo &info);
    Napi::Value GetShaderSource(const Napi::CallbackInfo &info);
    Napi::Value ValidateProgram(const Napi::CallbackInfo &info);

    Napi::Value TexSubImage2D(const Napi::CallbackInfo &info);
    Napi::Value ReadPixels(const Napi::CallbackInfo &info);
    Napi::Value GetTexParameter(const Napi::CallbackInfo &info);
    Napi::Value GetActiveAttrib(const Napi::CallbackInfo &info);
    Napi::Value GetActiveUniform(const Napi::CallbackInfo &info);
    Napi::Value GetAttachedShaders(const Napi::CallbackInfo &info);
    Napi::Value GetParameter(const Napi::CallbackInfo &info);
    Napi::Value GetBufferParameter(const Napi::CallbackInfo &info);
    Napi::Value GetFramebufferAttachmentParameter(const Napi::CallbackInfo &info);
    Napi::Value GetProgramInfoLog(const Napi::CallbackInfo &info);
    Napi::Value GetRenderbufferParameter(const Napi::CallbackInfo &info);
    Napi::Value GetVertexAttrib(const Napi::CallbackInfo &info);
    Napi::Value GetSupportedExtensions(const Napi::CallbackInfo &info);
    Napi::Value GetExtension(const Napi::CallbackInfo &info);
    Napi::Value CheckFramebufferStatus(const Napi::CallbackInfo &info);

    Napi::Value FrontFace(const Napi::CallbackInfo &info);
    Napi::Value SampleCoverage(const Napi::CallbackInfo &info);
    Napi::Value GetUniform(const Napi::CallbackInfo &info);

    Napi::Value DrawBuffersWEBGL(const Napi::CallbackInfo &info);
    Napi::Value EXTWEBGL_draw_buffers(const Napi::CallbackInfo &info);

    Napi::Value BindVertexArrayOES(const Napi::CallbackInfo &info);
    Napi::Value CreateVertexArrayOES(const Napi::CallbackInfo &info);
    Napi::Value DeleteVertexArrayOES(const Napi::CallbackInfo &info);
    Napi::Value IsVertexArrayOES(const Napi::CallbackInfo &info);


    void initPointers();
#include "procs.h"
};
