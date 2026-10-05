#pragma once

#include <filesystem>
#include <numbers>
#include <string>

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCLayerColor.hpp>
#include <Geode/modify/CCEGLViewProtocol.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/CCEGLView.hpp>

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif

#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif


namespace WhiteBot::Blur
{
    using namespace geode::prelude;
    using namespace cocos2d;


    // ============================================================
    // Settings
    // ============================================================

    inline bool enabled = true;
    inline int passes = 5;


    inline void initSettings()
    {
        listenForSettingChanges<bool>(
            "enabled",
            [](bool value)
            {
                enabled = value;
            }
        );

        listenForSettingChanges<int>(
            "passes",
            [](int value)
            {
                passes = value;
            }
        );

        enabled =
            Mod::get()->getSettingValue<bool>("enabled");

        passes =
            Mod::get()->getSettingValue<int>("passes");
    }


    // ============================================================
    // Blur Options
    // ============================================================

    inline constexpr char const* BLUR_TAG =
        "homeless.white-bot/blur-options";


    class BlurOptions : public CCObject
    {
    public:
        int apiVersion = 1;

        CCRenderTexture* rTex = nullptr;
        geode::Ref<CCClippingNode> clip = nullptr;

        bool forcePasses = false;
        int passes = 3;
        float alphaThreshold = 0.01f;

        bool init() 
        {
            return true;
        }

        CREATE_FUNC(BlurOptions);
    };


    inline BlurOptions* getOptions(CCNode* node)
    {
        return static_cast<BlurOptions*>(
            node->getUserObject(BLUR_TAG)
        );
    }


    inline void addBlur(CCNode* node)
    {
        if (getOptions(node))
            return;

        node->setUserObject(
            BLUR_TAG,
            BlurOptions::create()
        );
    }


    inline void removeBlur(CCNode* node)
    {
        node->setUserObject(
            BLUR_TAG,
            nullptr
        );
    }


    // ============================================================
    // Embedded Kawase Blur Shaders
    // ============================================================

    inline constexpr char const* KAWASE_VERT = R"GLSL(
attribute vec4 aPosition;
attribute vec2 aTexCoords;

varying vec2 TexCoords;

void main()
{
    TexCoords = aTexCoords;
    gl_Position = aPosition;
}
)GLSL";


    inline constexpr char const* KAWASE_FRAG = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif

varying vec2 TexCoords;

uniform sampler2D screen;
uniform vec2 screenSize;
uniform int pass;
uniform float radius;

void main()
{
    vec2 uv = TexCoords;
    vec2 res = screenSize;

    float i = float(pass) + 0.5;

    vec3 col =
        texture2D(
            screen,
            uv + vec2(i, i) / res
        ).rgb;

    col +=
        texture2D(
            screen,
            uv + vec2(i, -i) / res
        ).rgb;

    col +=
        texture2D(
            screen,
            uv + vec2(-i, i) / res
        ).rgb;

    col +=
        texture2D(
            screen,
            uv + vec2(-i, -i) / res
        ).rgb;

    col /= 4.0;

    gl_FragColor = vec4(col, 1.0);
}
)GLSL";


    // ============================================================
    // Shader
    // ============================================================

    struct Shader
    {
        GLuint vertex = 0;
        GLuint fragment = 0;
        GLuint program = 0;

        Result<std::string> compile(
            std::string const& vertexSource,
            std::string const& fragmentSource
        );

        Result<std::string> link();

        void cleanup();
    };


    // ============================================================
    // Render Texture
    // ============================================================

    struct RenderTexture
    {
        GLuint fbo = 0;
        GLuint tex = 0;

        void setup(
            GLsizei width,
            GLsizei height
        );

        void cleanup();
    };


    // ============================================================
    // Global Post Process State
    // ============================================================

    inline RenderTexture ppRt0;
    inline RenderTexture ppRt1;

    inline GLuint ppVao = 0;
    inline GLuint ppVbo = 0;

    inline Shader ppShader;

    inline GLint ppShaderFast = 0;
    inline GLint ppShaderFirst = 0;
    inline GLint ppShaderRadius = 0;
    inline GLint ppShaderPass = 0;


    // Forward declaration
    inline void drawBlurredNode(
        CCNode* node,
        BlurOptions* options
    );


    // ============================================================
    // Blur Render Texture
    // ============================================================

    class BlurRenderTex : public CCRenderTexture
    {
    public:
        BlurOptions* options = nullptr;

        static BlurRenderTex* create(
            int width,
            int height
        );

        void visit() override;
    };


    // ============================================================
    // Shader Compile
    // ============================================================

    inline Result<std::string> Shader::compile(
        std::string const& vertexSource,
        std::string const& fragmentSource
    )
    {
        auto getShaderLog = [](GLuint id) -> std::string
        {
            GLint length = 0;
            GLint written = 0;

            glGetShaderiv(
                id,
                GL_INFO_LOG_LENGTH,
                &length
            );

            if (length <= 0)
                return "";

            auto stuff = new char[length + 1];

            glGetShaderInfoLog(
                id,
                length,
                &written,
                stuff
            );

            std::string result(stuff);

            delete[] stuff;

            return result;
        };


        GLint res = 0;


        // Vertex shader
        vertex = glCreateShader(GL_VERTEX_SHADER);

        auto vertexPtr = vertexSource.c_str();

        glShaderSource(
            vertex,
            1,
            &vertexPtr,
            nullptr
        );

        glCompileShader(vertex);

        auto vertexLog = getShaderLog(vertex);

        glGetShaderiv(
            vertex,
            GL_COMPILE_STATUS,
            &res
        );

        if (!res)
        {
            glDeleteShader(vertex);

            vertex = 0;

            return Err(
                "vertex shader compilation failed:\n{}",
                vertexLog
            );
        }


        // Fragment shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);

        auto fragmentPtr = fragmentSource.c_str();

        glShaderSource(
            fragment,
            1,
            &fragmentPtr,
            nullptr
        );

        glCompileShader(fragment);

        auto fragmentLog = getShaderLog(fragment);

        glGetShaderiv(
            fragment,
            GL_COMPILE_STATUS,
            &res
        );

        if (!res)
        {
            glDeleteShader(vertex);
            glDeleteShader(fragment);

            vertex = 0;
            fragment = 0;

            return Err(
                "fragment shader compilation failed:\n{}",
                fragmentLog
            );
        }


        program = glCreateProgram();

        glAttachShader(
            program,
            vertex
        );

        glAttachShader(
            program,
            fragment
        );


        return Ok(fmt::format(
            "shader compilation successful. logs:\n"
            "vert:\n{}\n"
            "frag:\n{}",
            vertexLog,
            fragmentLog
        ));
    }


    // ============================================================
    // Shader Link
    // ============================================================

    inline Result<std::string> Shader::link()
    {
        if (!vertex)
            return Err("vertex shader not compiled");

        if (!fragment)
            return Err("fragment shader not compiled");


        auto getProgramLog = [](GLuint id) -> std::string
        {
            GLint length = 0;
            GLint written = 0;

            glGetProgramiv(
                id,
                GL_INFO_LOG_LENGTH,
                &length
            );

            if (length <= 0)
                return "";

            auto stuff = new char[length + 1];

            glGetProgramInfoLog(
                id,
                length,
                &written,
                stuff
            );

            std::string result(stuff);

            delete[] stuff;

            return result;
        };


        GLint res = 0;

        glLinkProgram(program);

        auto programLog =
            getProgramLog(program);


        glDeleteShader(vertex);
        glDeleteShader(fragment);

        vertex = 0;
        fragment = 0;


        glGetProgramiv(
            program,
            GL_LINK_STATUS,
            &res
        );

        if (!res)
        {
            glDeleteProgram(program);

            program = 0;

            return Err(
                "shader link failed:\n{}",
                programLog
            );
        }


        return Ok(fmt::format(
            "shader link successful. log:\n{}",
            programLog
        ));
    }


    // ============================================================
    // Shader Cleanup
    // ============================================================

    inline void Shader::cleanup()
    {
        if (program)
            glDeleteProgram(program);

        program = 0;
    }


    // ============================================================
    // Render Texture Setup
    // ============================================================

    inline void RenderTexture::setup(
        GLsizei width,
        GLsizei height
    )
    {
        GLint drawFbo = 0;
        GLint readFbo = 0;

        glGetIntegerv(
            GL_DRAW_FRAMEBUFFER_BINDING,
            &drawFbo
        );

        glGetIntegerv(
            GL_READ_FRAMEBUFFER_BINDING,
            &readFbo
        );


        glGenFramebuffers(
            1,
            &fbo
        );

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            fbo
        );


        glGenTextures(
            1,
            &tex
        );

        glBindTexture(
            GL_TEXTURE_2D,
            tex
        );


        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB,
            width,
            height,
            0,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            nullptr
        );


        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE
        );


        glBindTexture(
            GL_TEXTURE_2D,
            0
        );


        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            tex,
            0
        );


        if (
            glCheckFramebufferStatus(
                GL_FRAMEBUFFER
            ) != GL_FRAMEBUFFER_COMPLETE
        )
        {
            log::error(
                "White Bot blur FBO is not complete!"
            );
        }


        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            drawFbo
        );

        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            readFbo
        );
    }


    // ============================================================
    // Render Texture Cleanup
    // ============================================================

    inline void RenderTexture::cleanup()
    {
        if (fbo)
            glDeleteFramebuffers(
                1,
                &fbo
            );

        if (tex)
            glDeleteTextures(
                1,
                &tex
            );

        fbo = 0;
        tex = 0;
    }


    // ============================================================
    // Blur Render Texture
    // ============================================================

    inline BlurRenderTex* BlurRenderTex::create(
        int width,
        int height
    )
    {
        auto pRet = new BlurRenderTex();


        if (
            pRet &&
            pRet->initWithWidthAndHeight(
                width,
                height,
                kCCTexture2DPixelFormat_RGBA8888,
                GL_DEPTH24_STENCIL8
            )
        )
        {
            pRet->init();
            pRet->autorelease();

            return pRet;
        }


        CC_SAFE_DELETE(pRet);

        return nullptr;
    }


    inline void BlurRenderTex::visit()
    {
        kmGLPushMatrix();
        kmGLLoadIdentity();


        getSprite()->setPosition(
            CCDirector::get()->getWinSize() / 2
        );


        drawBlurredNode(
            getSprite(),
            options
        );


        kmGLPopMatrix();
    }


    // ============================================================
    // Setup Post Process
    // ============================================================

    inline void setupPostProcess()
    {
        auto size =
            CCDirector::get()
                ->getOpenGLView()
                ->getFrameSize();


        ppRt0.setup(
            static_cast<GLsizei>(size.width),
            static_cast<GLsizei>(size.height)
        );

        ppRt1.setup(
            static_cast<GLsizei>(size.width),
            static_cast<GLsizei>(size.height)
        );


        GLfloat ppVertices[] =
        {
            // position        // texCoords

            -1.0f,  1.0f,
             0.0f,  1.0f,

            -1.0f, -1.0f,
             0.0f,  0.0f,

             1.0f, -1.0f,
             1.0f,  0.0f,


            -1.0f,  1.0f,
             0.0f,  1.0f,

             1.0f, -1.0f,
             1.0f,  0.0f,

             1.0f,  1.0f,
             1.0f,  1.0f
        };


        glGenVertexArrays(
            1,
            &ppVao
        );

        glGenBuffers(
            1,
            &ppVbo
        );


        glBindVertexArray(ppVao);

        glBindBuffer(
            GL_ARRAY_BUFFER,
            ppVbo
        );


        glBufferData(
            GL_ARRAY_BUFFER,
            sizeof(ppVertices),
            ppVertices,
            GL_STATIC_DRAW
        );


        glEnableVertexAttribArray(0);

        glVertexAttribPointer(
            0,
            2,
            GL_FLOAT,
            GL_FALSE,
            4 * sizeof(GLfloat),
            nullptr
        );


        glEnableVertexAttribArray(1);

        glVertexAttribPointer(
            1,
            2,
            GL_FLOAT,
            GL_FALSE,
            4 * sizeof(GLfloat),
            reinterpret_cast<void*>(
                2 * sizeof(GLfloat)
            )
        );


        glBindVertexArray(0);
        glBindBuffer(
            GL_ARRAY_BUFFER,
            0
        );


        // Compile embedded shaders
        auto res =
            ppShader.compile(
                KAWASE_VERT,
                KAWASE_FRAG
            );


        if (!res)
        {
            log::error(
                "Failed to compile White Bot blur shader: {}",
                res.unwrapErr()
            );

            return;
        }


        log::info(
            "{}",
            res.unwrap()
        );


        glBindAttribLocation(
            ppShader.program,
            0,
            "aPosition"
        );

        glBindAttribLocation(
            ppShader.program,
            1,
            "aTexCoords"
        );


        res = ppShader.link();


        if (!res)
        {
            log::error(
                "Failed to link White Bot blur shader: {}",
                res.unwrapErr()
            );

            return;
        }


        log::info(
            "{}",
            res.unwrap()
        );


        ccGLUseProgram(
            ppShader.program
        );


        glUniform1i(
            glGetUniformLocation(
                ppShader.program,
                "screen"
            ),
            0
        );


        glUniform2f(
            glGetUniformLocation(
                ppShader.program,
                "screenSize"
            ),
            size.width,
            size.height
        );


        ppShaderFast =
            glGetUniformLocation(
                ppShader.program,
                "fast"
            );


        ppShaderFirst =
            glGetUniformLocation(
                ppShader.program,
                "first"
            );


        ppShaderRadius =
            glGetUniformLocation(
                ppShader.program,
                "radius"
            );


        ppShaderPass =
            glGetUniformLocation(
                ppShader.program,
                "pass"
            );
    }


    // ============================================================
    // Cleanup Post Process
    // ============================================================

    inline void cleanupPostProcess()
    {
        ppRt0.cleanup();
        ppRt1.cleanup();


        if (ppVao)
            glDeleteVertexArrays(
                1,
                &ppVao
            );


        if (ppVbo)
            glDeleteBuffers(
                1,
                &ppVbo
            );


        ppVao = 0;
        ppVbo = 0;


        ppShader.cleanup();


        ppShaderFast = 0;
        ppShaderFirst = 0;
        ppShaderRadius = 0;
        ppShaderPass = 0;
    }


    // ============================================================
    // Draw Blurred Node
    // ============================================================

    inline void drawBlurredNode(
        CCNode* node,
        BlurOptions* options
    )
    {
        int PASSES = passes;

        float blur = 1.0f;


        if (options->forcePasses)
            PASSES = options->passes;


        GLint drawFbo = 0;
        GLint readFbo = 0;


        glGetIntegerv(
            GL_DRAW_FRAMEBUFFER_BINDING,
            &drawFbo
        );

        glGetIntegerv(
            GL_READ_FRAMEBUFFER_BINDING,
            &readFbo
        );


        glBindFramebuffer(
            GL_FRAMEBUFFER,
            ppRt1.fbo
        );

        glClear(
            GL_COLOR_BUFFER_BIT
        );


        glBindFramebuffer(
            GL_FRAMEBUFFER,
            ppRt0.fbo
        );

        glClear(
            GL_COLOR_BUFFER_BIT
        );


        node->visit();


        glBindVertexArray(
            ppVao
        );

        ccGLUseProgram(
            ppShader.program
        );


        glUniform1f(
            ppShaderRadius,
            blur
        );


        bool ping = true;


        for (int i = 0; i < PASSES; ++i)
        {
            glBindFramebuffer(
                GL_FRAMEBUFFER,
                ping
                    ? ppRt1.fbo
                    : ppRt0.fbo
            );


            glBindTexture(
                GL_TEXTURE_2D,
                ping
                    ? ppRt0.tex
                    : ppRt1.tex
            );


            glUniform1i(
                ppShaderPass,
                i + 1
            );


            glDrawArrays(
                GL_TRIANGLES,
                0,
                6
            );


            ping = !ping;
        }


        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            drawFbo
        );

        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            readFbo
        );


        glBindTexture(
            GL_TEXTURE_2D,
            ping
                ? ppRt0.tex
                : ppRt1.tex
        );


        glDrawArrays(
            GL_TRIANGLES,
            0,
            6
        );


        glBindVertexArray(0);
    }
}


// ====================================================================
// CCLayerColor Hook
// ====================================================================

class $modify(WhiteBotBlurLayerColor, CCLayerColor)
{
    void draw()
    {
        using namespace WhiteBot::Blur;

        if (!getOptions(this))
        {
            CCLayerColor::draw();
            return;
        }


        auto c = getColor();
        auto o = getOpacity();


        ccBlendFunc(
            getBlendFunc()
        );


        ccDrawSolidRect(
            ccp(0, 0),
            getContentSize(),
            ccc4f(
                c.r / 255.0f,
                c.g / 255.0f,
                c.b / 255.0f,
                o / 255.0f
            )
        );
    }
};


// ====================================================================
// CCNode Hook
// ====================================================================

class $modify(WhiteBotBlurNode, CCNode)
{
    void visit()
    {
        using namespace WhiteBot::Blur;


        if (!enabled)
        {
            CCNode::visit();
            return;
        }


        static bool isCapturingScene = false;
        static bool shouldStopCapturing = false;
        static CCNode* capturingSceneStopAt = nullptr;


        if (isCapturingScene)
        {
            if (
                this == capturingSceneStopAt ||
                shouldStopCapturing
            )
            {
                shouldStopCapturing = true;
            }
            else
            {
                if (
                    typeinfo_cast<ShaderLayer*>(
                        this
                    )
                )
                {
                    return;
                }


                CCNode::visit();
            }


            return;
        }


        auto blur =
            typeinfo_cast<BlurOptions*>(
                getUserObject(BLUR_TAG)
            );


        if (blur)
        {
            isCapturingScene = true;

            capturingSceneStopAt = this;
            shouldStopCapturing = false;


            if (
                static_cast<CCNode*>(this) ==
                CCScene::get()
            )
            {
                capturingSceneStopAt = nullptr;
            }


            if (!blur->rTex)
            {
                auto size =
                    CCDirector::get()
                        ->getWinSize();


                blur->rTex =
                    BlurRenderTex::create(
                        static_cast<int>(size.width),
                        static_cast<int>(size.height)
                    );


                auto renderTex =
                    static_cast<BlurRenderTex*>(
                        blur->rTex
                    );


                renderTex->options = blur;


                blur->clip =
                    CCClippingNode::create(this);


                // Prevent the clipping node from retaining us.
                this->release();


                blur->clip->setAlphaThreshold(
                    blur->alphaThreshold
                );


                blur->clip->addChild(
                    blur->rTex
                );
            }


            auto rTex = blur->rTex;


            rTex->beginWithClear(
                0,
                0,
                0,
                0
            );


            CCScene::get()->visit();


            rTex->end();


            isCapturingScene = false;


            // ----------------------------------------------------
            // World-space bounding box
            // ----------------------------------------------------

            auto parent = getParent();

            auto boundingBox =
                this->boundingBox();


            CCPoint bbMin(
                boundingBox.getMinX(),
                boundingBox.getMinY()
            );


            CCPoint bbMax(
                boundingBox.getMaxX(),
                boundingBox.getMaxY()
            );


            auto min =
                parent
                    ? parent->convertToWorldSpace(bbMin)
                    : bbMin;


            auto max =
                parent
                    ? parent->convertToWorldSpace(bbMax)
                    : bbMax;


            CCRect rec = CCRectMake(
                min.x,
                min.y,
                max.x - min.x,
                max.y - min.y
            );


            if (rec.getMinX() < 0)
            {
                rec += CCRectMake(
                    -rec.getMinX(),
                    0,
                    0,
                    0
                );
            }


            if (rec.getMinY() < 0)
            {
                rec += CCRectMake(
                    0,
                    -rec.getMinY(),
                    0,
                    0
                );
            }


            auto winSize =
                CCDirector::get()
                    ->getWinSize();


            auto frameSize =
                CCEGLView::get()
                    ->getFrameSize();


            auto scX =
                (1.0f / winSize.width) *
                frameSize.width;


            auto scY =
                (1.0f / winSize.height) *
                frameSize.height;


            auto uiBL =
                ccp(
                    rec.getMinX() * scX,
                    rec.getMinY() * scY
                );


            auto uiTR =
                ccp(
                    rec.getMaxX() * scX,
                    rec.getMaxY() * scY
                );


            auto shadow =
                30 * passes;


            auto sciz =
                glIsEnabled(
                    GL_SCISSOR_TEST
                );


            glEnable(
                GL_SCISSOR_TEST
            );


            glScissor(
                static_cast<GLint>(
                    uiBL.x - shadow
                ),
                static_cast<GLint>(
                    uiBL.y - shadow
                ),
                static_cast<GLsizei>(
                    uiTR.x + shadow
                ),
                static_cast<GLsizei>(
                    uiTR.y + shadow
                )
            );


            blur->clip->visit();


            if (!sciz)
                glDisable(GL_SCISSOR_TEST);


            // If this is the scene itself,
            // don't render the original again.
            if (!getParent())
                return;
        }


        CCNode::visit();
    }
};


// ====================================================================
// Frame Size Hook
// ====================================================================

class $modify(WhiteBotBlurEGLView, CCEGLView)
{
    void setFrameSize(
        float width,
        float height
    )
    {
        CCEGLViewProtocol::setFrameSize(
            width,
            height
        );


        if (
            !CCDirector::get()
                ->getOpenGLView()
        )
        {
            return;
        }


        WhiteBot::Blur::cleanupPostProcess();
        WhiteBot::Blur::setupPostProcess();
    }
};


// ====================================================================
// GameManager Hook
// ====================================================================

class $modify(WhiteBotBlurGameManager, GameManager)
{
    void reloadAllStep5()
    {
        GameManager::reloadAllStep5();


        WhiteBot::Blur::cleanupPostProcess();
        WhiteBot::Blur::setupPostProcess();
    }
};


// ====================================================================
// AppDelegate Hook
// ====================================================================

class $modify(WhiteBotBlurAppDelegate, AppDelegate)
{
    void applicationWillBecomeActive()
    {
        AppDelegate::applicationWillBecomeActive();


        WhiteBot::Blur::cleanupPostProcess();
        WhiteBot::Blur::setupPostProcess();
    }
};


// ====================================================================
// Mod Loaded
// ====================================================================

$on_mod(Loaded)
{
    using namespace WhiteBot::Blur;


    initSettings();


    Loader::get()->queueInMainThread(
        []
        {
            cleanupPostProcess();
            setupPostProcess();
        }
    );
}