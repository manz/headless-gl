const { WebGLRenderingContext } = require('./webgl-rendering-context')

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
  }
  if (!ctx) {
    return null
  }

  return ctx
}

module.exports = createContext
