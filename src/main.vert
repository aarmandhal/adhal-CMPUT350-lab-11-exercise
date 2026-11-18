#version 120

uniform sampler1D transforms;  // 1D texture: one texel per sprite, each a vec4 {scale, translation}
uniform int numTransforms;     // how many sprites/texels are packed into `transforms`

attribute vec2 position;       // this vertex's local (untransformed) quad position
attribute vec2 texCoords;      // this vertex's UV into the texture atlas
attribute float spriteNumber;  // which sprite (texel index) this vertex belongs to

varying vec2 texCoordsOut;     // passed through to the fragment shader unchanged

// Given a world position (for this lab, bottom-left is (0, 0), top-right of window is (1280, 720)),
// convert it to be in [-1, 1]^2, where y = -1 is the bottom and x = -1 is the left side.
vec2 cameraPos(vec2 worldPos) {
    // Map [0,1280]x[0,720] into [0,1]^2.
    vec2 normalized = worldPos / vec2(1280.0, 720.0);
    // Remap [0,1]^2 into [-1,1]^2 -- the clip-space range OpenGL expects.
    vec2 clip = normalized * 2.0 - 1.0;
    return clip;
}

void main() {
    // +0.5 lands the sample exactly on a texel center -- required for GL_NEAREST to return
    // this sprite's exact transform instead of blending with a neighboring texel.
    float u = (spriteNumber + 0.5) / float(numTransforms);
    vec4 transform = texture1D(transforms, u);

    // Unpack this sprite's transform: xy = scale, zw = translation.
    vec2 scale = transform.xy;
    vec2 translation = transform.zw;

    // Apply the transform on the GPU, scale first then translate (matches BasicTransform::applyPoint).
    vec2 worldPos = position * scale + translation;

    // Final clip-space position and pass-through UV for the fragment shader.
    gl_Position = vec4(cameraPos(worldPos), 0.0, 1.0);
    texCoordsOut = texCoords;
}