#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include "basic_transform.h"
#include "texture_atlas.h"
#include "vector_util.h"
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

#define GL_SILENCE_DEPRECATION  // silences macOS OpenGL deprecation warnings

#define glCheck(expr)                                                                              \
    expr;                                                                                          \
    if (int e = glGetError()) {                                                                    \
        std::cerr << "OpenGL error: " << e << " in file " __FILE__ << ", line number " << __LINE__ \
                  << '\n';                                                                         \
        abort();                                                                                   \
    }

using SpriteID = size_t;

class SpriteManager {
public:
    SpriteManager(const sf::Vector2u& atlasSize, unsigned padding)
        : mVertices(sf::PrimitiveType::Triangles),
          mNumSprites{0},
          mAtlas(atlasSize, padding),
          mShader(std::filesystem::path{"main.vert"}, std::filesystem::path{"main.frag"}) {
        // Look up the shader's attribute locations now -- they're fixed for the program's lifetime.
        GLuint programHandle = mShader.getNativeHandle();
        mPositionAttribLocation = glGetAttribLocation(programHandle, "position");
        mTexCoordsAttribLocation = glGetAttribLocation(programHandle, "texCoords");
        mSpriteNumberAttribLocation = glGetAttribLocation(programHandle, "spriteNumber");

        // Same thing for the shader's uniform locations.
        mTextureUniformLocation = glGetUniformLocation(programHandle, "texture");
        mTransformsUniformLocation = glGetUniformLocation(programHandle, "transforms");
        mNumTransformsUniformLocation = glGetUniformLocation(programHandle, "numTransforms");

        // Allocate the GPU buffers/texture this class re-fills every frame in drawGL().
        glGenBuffers(1, &mVerticesVBO);
        glGenBuffers(1, &mSpriteNumberVBO);
        glGenTextures(1, &mTransformsTexture);

        // One-time filtering setup: nearest + clamp-to-edge so a sample never blends two sprites'
        // transforms together.
        glBindTexture(GL_TEXTURE_1D, mTransformsTexture);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }

    // Free the GPU resources allocated above -- none of this is RAII-managed by SFML/OpenGL.
    ~SpriteManager() {
        glDeleteBuffers(1, &mVerticesVBO);
        glDeleteBuffers(1, &mSpriteNumberVBO);
        glDeleteTextures(1, &mTransformsTexture);
    }

    // Delegates straight to the atlas, which already handles packing/padding/UV math.
    TextureID registerTexture(const std::string& filepath) {
        return mAtlas.registerTexture(filepath);
    };

    SpriteID createSprite(TextureID textureID) {
        // Local quad: bottom-left pinned at (0,0), top-right at the texture's unpadded pixel size.
        sf::Vector2u sizePixels = mAtlas.getSizePixels(textureID);
        sf::Vector2f size = sf::Vector2f(sizePixels);
        // Already normalized to [0,1] -- the custom vertex shader reads this completely raw.
        sf::FloatRect uv = mAtlas.getBoundsUV(textureID);

        sf::Vector2f bl = {0.f, 0.f};
        sf::Vector2f br = {size.x, 0.f};
        sf::Vector2f tl = {0.f, size.y};
        sf::Vector2f tr = {size.x, size.y};

        sf::Vector2f uvBL = {uv.position.x, uv.position.y};
        sf::Vector2f uvBR = {uv.position.x + uv.size.x, uv.position.y};
        sf::Vector2f uvTL = {uv.position.x, uv.position.y + uv.size.y};
        sf::Vector2f uvTR = {uv.position.x + uv.size.x, uv.position.y + uv.size.y};

        // Two triangles, (BL,BR,TR) and (BL,TR,TL) -- setTexture must reuse this exact order.
        mVertices.append(sf::Vertex{bl, sf::Color::White, uvBL});
        mVertices.append(sf::Vertex{br, sf::Color::White, uvBR});
        mVertices.append(sf::Vertex{tr, sf::Color::White, uvTR});
        mVertices.append(sf::Vertex{bl, sf::Color::White, uvBL});
        mVertices.append(sf::Vertex{tr, sf::Color::White, uvTR});
        mVertices.append(sf::Vertex{tl, sf::Color::White, uvTL});

        // One entry per VERTEX, not per sprite -- the GPU-path attribute is per-vertex.
        float spriteIndex = static_cast<float>(mNumSprites);
        for (int i = 0; i < 6; ++i) {
            mSpriteNumbers.push_back(spriteIndex);
        }
        // Ids are dense/sequential, so "sprite * 6" always finds this sprite's vertex range later.
        SpriteID newID = mNumSprites;
        mTransforms.push_back(BasicTransform{});

        mNumSprites += 1;

        return newID;
    };

    void setTexture(SpriteID sprite, TextureID newTexture) {
        // Same corner/UV math as createSprite, but overwriting existing vertices, not appending.
        sf::Vector2u sizePixels = mAtlas.getSizePixels(newTexture);
        sf::Vector2f size = sf::Vector2f(sizePixels);
        sf::FloatRect uv = mAtlas.getBoundsUV(newTexture);

        sf::Vector2f bl = {0.f, 0.f};
        sf::Vector2f br = {size.x, 0.f};
        sf::Vector2f tl = {0.f, size.y};
        sf::Vector2f tr = {size.x, size.y};

        sf::Vector2f uvBL = {uv.position.x, uv.position.y};
        sf::Vector2f uvBR = {uv.position.x + uv.size.x, uv.position.y};
        sf::Vector2f uvTL = {uv.position.x, uv.position.y + uv.size.y};
        sf::Vector2f uvTR = {uv.position.x + uv.size.x, uv.position.y + uv.size.y};

        // This sprite's id maps directly to its 6-vertex range in mVertices.
        size_t base = sprite * 6;

        // Resize the quad, keeping its bottom-left corner pinned at local (0,0).
        mVertices[base + 0].position = bl;
        mVertices[base + 1].position = br;
        mVertices[base + 2].position = tr;
        mVertices[base + 3].position = bl;
        mVertices[base + 4].position = tr;
        mVertices[base + 5].position = tl;

        // Point at the new texture's UVs, same winding order as createSprite.
        mVertices[base + 0].texCoords = uvBL;
        mVertices[base + 1].texCoords = uvBR;
        mVertices[base + 2].texCoords = uvTR;
        mVertices[base + 3].texCoords = uvBL;
        mVertices[base + 4].texCoords = uvTR;
        mVertices[base + 5].texCoords = uvTL;
    };

    void draw(sf::RenderTarget& target, sf::RenderStates states) const {
        // Scratch copy -- mVertices itself stays untransformed and gets reused every frame.
        sf::VertexArray transformed =
            sf::VertexArray(sf::PrimitiveType::Triangles, mVertices.getVertexCount());

        // Apply each sprite's transform on the CPU, 6 vertices (one sprite) at a time.
        for (size_t spriteIdx = 0; spriteIdx < mNumSprites; ++spriteIdx) {
            const BasicTransform& transform = mTransforms[spriteIdx];
            size_t base = spriteIdx * 6;
            for (size_t v = 0; v < 6; ++v) {
                const sf::Vertex& src = mVertices[base + v];
                transformed[base + v].position = transform.applyPoint(src.position);
                transformed[base + v].texCoords = src.texCoords;
                transformed[base + v].color = src.color;
            }
        }

        // Bind the atlas texture and the CPU-path shader (main_sfml.vert + main.frag).
        states.texture = &mAtlas.getTexture();
        states.shader = &mShader;

        // One SFML draw call covering every sprite in the manager.
        target.draw(transformed, states);
    };

    void drawGL() {
        // Nothing to draw yet, and &mVertices[0] below would be UB on an empty VertexArray.
        size_t vertexCount = mVertices.getVertexCount();
        if (vertexCount == 0) {
            return;
        }
        // Activate the GPU-transform shader program (main.vert + main.frag).
        glUseProgram(mShader.getNativeHandle());

        // Upload this frame's vertex data (position + texCoords, interleaved as sf::Vertex).
        glBindBuffer(GL_ARRAY_BUFFER, mVerticesVBO);
        glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(sf::Vertex), &mVertices[0],
                     GL_DYNAMIC_DRAW);

        // Tell the GPU how to read "position" out of that buffer, then enable the attribute.
        glEnableVertexAttribArray(mPositionAttribLocation);
        glVertexAttribPointer(mPositionAttribLocation, 2, GL_FLOAT, GL_FALSE, sizeof(sf::Vertex),
                              (void*)offsetof(sf::Vertex, position));
        // Same thing for "texCoords" -- same buffer, different byte offset.
        glEnableVertexAttribArray(mTexCoordsAttribLocation);
        glVertexAttribPointer(mTexCoordsAttribLocation, 2, GL_FLOAT, GL_FALSE, sizeof(sf::Vertex),
                              (void*)offsetof(sf::Vertex, texCoords));

        // Upload this frame's per-vertex sprite indices into their own buffer.
        glBindBuffer(GL_ARRAY_BUFFER, mSpriteNumberVBO);
        glBufferData(GL_ARRAY_BUFFER, mSpriteNumbers.size() * sizeof(float), mSpriteNumbers.data(),
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(mSpriteNumberAttribLocation);
        glVertexAttribPointer(mSpriteNumberAttribLocation, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

        // Upload this frame's transforms as one RGBA32F texel per sprite -- BasicTransform IS a
        // vec4.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, mTransformsTexture);
        glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F_ARB, (GLsizei)mNumSprites, 0, GL_RGBA, GL_FLOAT,
                     mTransforms.data());

        // Bind the atlas texture on a separate texture unit from the transforms texture.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mAtlas.getTexture().getNativeHandle());

        // Tell the shader which texture unit each sampler uniform should read from.
        glUniform1i(mTextureUniformLocation, 0);
        glUniform1i(mTransformsUniformLocation, 1);
        glUniform1i(mNumTransformsUniformLocation, (GLint)mNumSprites);

        // One draw call for every sprite's geometry, all at once.
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertexCount);
    };

    // Mutable reference so main.cpp can poke scale/translation directly every frame.
    BasicTransform& getTransform(SpriteID id) { return mTransforms[id]; }

    // Plain accessor -- main.cpp uses this to read texture sizes for particle placement math.
    const TextureAtlas& getAtlas() const { return mAtlas; };

private:
    sf::VertexArray mVertices;                // untransformed quad geometry, 6 verts/sprite
    std::vector<BasicTransform> mTransforms;  // one transform per sprite
    size_t mNumSprites;
    TextureAtlas mAtlas;
    mutable sf::Shader mShader;

    // Use the below only in Batched rendering (part 2):
    std::vector<float> mSpriteNumbers;  // one entry per VERTEX, mirrors mVertices

    GLint mTextureUniformLocation;
    GLint mTransformsUniformLocation;
    GLint mNumTransformsUniformLocation;

    GLint mPositionAttribLocation;
    GLint mTexCoordsAttribLocation;
    GLint mSpriteNumberAttribLocation;

    GLuint mVerticesVBO;
    GLuint mSpriteNumberVBO;
    GLuint mTransformsTexture;
};

#endif  // SPRITE_MANAGER_H