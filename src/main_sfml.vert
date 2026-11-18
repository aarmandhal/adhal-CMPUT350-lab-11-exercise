#version 120

varying vec2 texCoordsOut;  // passed through to the fragment shader unchanged

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
    // output to gl_Position and to texCoordsOut.
    // In this case, using window.draw, SFML provides tex coords in gl_MultiTexCoord0.xy, and
    //  vertex positions in gl_Vertex.xy.
    // gl_Vertex is SFML's fixed-function feed for this vertex's position (window.draw supplies it,
    // not a custom attribute) -- only usable because no custom attributes are declared in this shader.
    gl_Position  = vec4(cameraPos(gl_Vertex.xy), 0.0, 1.0);
    // gl_MultiTexCoord0 is the matching fixed-function feed for this vertex's UV.
    texCoordsOut = gl_MultiTexCoord0.xy;
}