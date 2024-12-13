const bits = require('bit-twiddle')
const { WebGLContextAttributes } = require('./webgl-context-attributes')
const { WebGLRenderingContext, wrapContext } = require('./webgl-rendering-context')
const { WebGLTextureUnit } = require('./webgl-texture-unit')
const { WebGLVertexArrayObjectState, WebGLVertexArrayGlobalState } = require('./webgl-vertex-attribute')
const { gl, NativeWebGLRenderingContext, NativeWebGL } = require('./native-gl')



function createContext (width, height, options) {
  width = width | 0
  height = height | 0
  if (!(width > 0 && height > 0)) {
    return null
  }

  let ctx
  try {
    ctx = new WebGLRenderingContext(
      width,
      height,
      options)
  } catch (e) {
    debugger;
  }
  if (!ctx) {
    return null
  }

  return ctx
}

module.exports = createContext
