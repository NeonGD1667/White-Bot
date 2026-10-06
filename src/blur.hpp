// =============================================================================
//  src/blur.hpp  -  WhiteBot::Blur
//
//  Blur nền phía sau White Bot UI. Một file duy nhất, không dependency ngoài
//  Geode SDK + cocos2d-x/OpenGL của Geometry Dash.
//
//  Pipeline (mỗi frame khi blur đang active):
//
//     framebuffer đang bound (scene + pause layer đã vẽ xong)
//        -> glCopyTexSubImage2D vào `capture` (full-res, texture dùng lại)
//        -> blur H (capture -> A, đồng thời downsample)      } separable
//        -> blur V (A -> B)                                  } Gaussian
//        -> (passes-1) lần: H (B -> A), V (A -> B)           } ping-pong
//        -> vẽ B lên framebuffer ban đầu (alpha blend theo độ fade)
//        -> White Bot UI vẽ lên trên, vẫn sắc nét
//
//  Cách dùng:
//     - #include "blur.hpp" trong ĐÚNG MỘT file .cpp (header này có $modify hook).
//       Nếu cần include ở nhiều nơi, hãy define WHITEBOT_BLUR_NO_HOOK ở các
//       file còn lại (và chỉ để một file không define).
//     - WhiteBot::Blur::init();     // khi mod load
//     - WhiteBot::Blur::enable();   // khi mở White Bot UI
//     - WhiteBot::Blur::disable();  // khi đóng
//
//  Thứ tự render:
//     Mặc định blur chạy trong hook CCEGLView::swapBuffers với priority First,
//     tức là TRƯỚC các hook swapBuffers khác (ví dụ ImGui) -> UI vẽ sau blur.
//     Nếu White Bot UI là node Cocos nằm trong scene (hoặc thứ tự hook không
//     như ý), gọi WhiteBot::Blur::applyNow() ngay trước lúc UI vẽ
//     (ví dụ đầu CCNode::visit() của root UI). Khi đã gọi applyNow() trong
//     frame đó thì hook swapBuffers sẽ tự bỏ qua, không blur 2 lần.
//
//  Thread: mọi hàm phải gọi từ main/GL thread.
// =============================================================================
#pragma once

#include <Geode/Geode.hpp>

#ifndef WHITEBOT_BLUR_NO_HOOK
    #include <Geode/modify/CCEGLView.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace WhiteBot::Blur {

    // ------------------------------------------------------------------ config
    struct Config {
        int   passes      = 4;      // số vòng blur (mỗi vòng = 1 pass H + 1 pass V), 1..12
        float radius      = 1.0f;   // hệ số khoảng cách tap, 0.25..8 (to quá sẽ lộ banding)
        int   downscale   = 2;      // blur ở độ phân giải 1/downscale, 1..8
        float dim         = 0.12f;  // làm tối nền đã blur, 0..1
        float fadeSeconds = 0.12f;  // thời gian fade in/out, 0 = tức thì
    };

    namespace detail {
        using namespace geode::prelude;

        // Cùng index với cocos2d-x: kCCVertexAttrib_Position = 0, Color = 1, TexCoords = 2.
        constexpr GLuint kAttribPosition = 0;
        constexpr GLuint kAttribColor    = 1;
        constexpr GLuint kAttribTexCoord = 2;

        // ------------------------------------------------------------ shaders
        // Không có #version -> GLSL 1.10 (desktop compat) / GLSL ES 1.00, giống
        // các shader mặc định của cocos2d-x.
        constexpr const char* kVertexSrc = R"GLSL(
#ifdef GL_ES
precision highp float;
#endif
attribute vec2 a_position;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)GLSL";

        // Gaussian 9-tap tách trục, dùng linear sampling nên chỉ cần 5 lần fetch.
        constexpr const char* kBlurFragSrc = R"GLSL(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2 u_step;   // (direction * radius) / texture size
void main() {
    vec2 o1 = u_step * 1.3846153846;
    vec2 o2 = u_step * 3.2307692308;
    vec3 c = texture2D(u_tex, v_uv).rgb * 0.2270270270;
    c += texture2D(u_tex, v_uv + o1).rgb * 0.3162162162;
    c += texture2D(u_tex, v_uv - o1).rgb * 0.3162162162;
    c += texture2D(u_tex, v_uv + o2).rgb * 0.0702702703;
    c += texture2D(u_tex, v_uv - o2).rgb * 0.0702702703;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

        // Composite: alpha = độ fade. Kết hợp với glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
        // thì framebuffer (vẫn đang chứa ảnh gốc) được trộn dần sang ảnh blur.
        constexpr const char* kCompositeFragSrc = R"GLSL(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_dim;
uniform float u_amount;
void main() {
    vec3 c = texture2D(u_tex, v_uv).rgb * (1.0 - u_dim);
    gl_FragColor = vec4(c, u_amount);
}
)GLSL";

        // -------------------------------------------------------------- state
        struct Gpu {
            void*  window = nullptr;  // cửa sổ lúc tạo resource (phát hiện context bị tạo lại)

            GLuint capture = 0;       // full-res, đích của glCopyTexSubImage2D
            GLuint texA = 0, texB = 0;
            GLuint fboA = 0, fboB = 0;
            GLuint vbo = 0;
            GLuint progBlur = 0, progComp = 0;

            GLint uBlurStep = -1;
            GLint uCompDim = -1, uCompAmount = -1;

            int capW = 0, capH = 0;   // kích thước capture (= viewport)
            int lowW = 0, lowH = 0;   // kích thước A/B
            int div = 0;              // downscale đang áp dụng

            bool ready = false;
            bool failed = false;      // lỗi shader/FBO -> không thử lại mỗi frame
            bool errorLogged = false;
        };

        struct State {
            Config cfg;
            Gpu gpu;
            bool initialized = false;
            bool target = false;            // enable()/disable()
            bool appliedThisFrame = false;  // applyNow() đã chạy trong frame này
            float amount = 0.0f;            // 0..1, độ fade hiện tại
            std::chrono::steady_clock::time_point lastTick{};
        };

        inline State& state() {
            static State s;
            return s;
        }

        // -------------------------------------------------------- GL helpers
        inline void drainGlErrors() {
            for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {}
        }

        inline void setCap(GLenum cap, GLboolean on) {
            if (on) glEnable(cap);
            else glDisable(cap);
        }

        // Lưu toàn bộ state GL mà pipeline đụng tới, restore trong destructor.
        // Vì ta lưu/restore bằng raw GL nên cache của cocos2d-x
        // (ccGLUseProgram, ccGLBindTexture2D, ccGLEnableVertexAttribs...) vẫn khớp
        // với GL thật sau khi blur xong.
        struct StateGuard {
            struct Attrib {
                GLint enabled = 0, size = 4, type = GL_FLOAT, normalized = 0, stride = 0, buffer = 0;
                GLvoid* pointer = nullptr;
            };

            GLint viewport[4] = {0, 0, 0, 0};
            GLint fbo = 0, program = 0, texture = 0, arrayBuffer = 0;
            GLint activeTexture = GL_TEXTURE0;
            GLint blendSrcRGB = GL_ONE, blendDstRGB = GL_ZERO, blendSrcA = GL_ONE, blendDstA = GL_ZERO;
            GLint blendEqRGB = GL_FUNC_ADD, blendEqA = GL_FUNC_ADD;
            GLboolean blend = GL_FALSE, depth = GL_FALSE, stencil = GL_FALSE, cull = GL_FALSE, scissor = GL_FALSE;
            GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
            Attrib attribs[3];

            StateGuard() {
                drainGlErrors();

                glGetIntegerv(GL_VIEWPORT, viewport);
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
                glGetIntegerv(GL_CURRENT_PROGRAM, &program);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);

                glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

                blend = glIsEnabled(GL_BLEND);
                depth = glIsEnabled(GL_DEPTH_TEST);
                stencil = glIsEnabled(GL_STENCIL_TEST);
                cull = glIsEnabled(GL_CULL_FACE);
                scissor = glIsEnabled(GL_SCISSOR_TEST);

                glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
                glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
                glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
                glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
                glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRGB);
                glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqA);
                glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);

                for (GLuint i = 0; i < 3; ++i) {
                    auto& a = attribs[i];
                    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
                    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
                    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
                    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.normalized);
                    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
                    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.buffer);
                    glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.pointer);
                }
            }

            ~StateGuard() {
                glUseProgram(static_cast<GLuint>(program));

                for (GLuint i = 0; i < 3; ++i) {
                    auto const& a = attribs[i];
                    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(a.buffer));
                    glVertexAttribPointer(i, a.size, static_cast<GLenum>(a.type),
                                          a.normalized ? GL_TRUE : GL_FALSE, a.stride, a.pointer);
                    if (a.enabled) glEnableVertexAttribArray(i);
                    else glDisableVertexAttribArray(i);
                }
                glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));

                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
                glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

                setCap(GL_BLEND, blend);
                setCap(GL_DEPTH_TEST, depth);
                setCap(GL_STENCIL_TEST, stencil);
                setCap(GL_CULL_FACE, cull);
                setCap(GL_SCISSOR_TEST, scissor);

                glBlendEquationSeparate(static_cast<GLenum>(blendEqRGB), static_cast<GLenum>(blendEqA));
                glBlendFuncSeparate(static_cast<GLenum>(blendSrcRGB), static_cast<GLenum>(blendDstRGB),
                                    static_cast<GLenum>(blendSrcA), static_cast<GLenum>(blendDstA));
                glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
                glActiveTexture(static_cast<GLenum>(activeTexture));
            }

            StateGuard(StateGuard const&) = delete;
            StateGuard& operator=(StateGuard const&) = delete;
        };

        // ------------------------------------------------------ shader build
        inline GLuint compileShader(GLenum type, const char* src, const char* label) {
            GLuint sh = glCreateShader(type);
            if (!sh) return 0;
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);

            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                GLint len = 0;
                glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
                std::string info(static_cast<size_t>(std::max(len, 1)), '\0');
                glGetShaderInfoLog(sh, static_cast<GLsizei>(info.size()), nullptr, info.data());
                log::error("[WhiteBot::Blur] compile {} failed: {}", label, info.c_str());
                glDeleteShader(sh);
                return 0;
            }
            return sh;
        }

        inline GLuint linkProgram(GLuint vs, GLuint fs, const char* label) {
            GLuint prog = glCreateProgram();
            if (!prog) return 0;
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glBindAttribLocation(prog, kAttribPosition, "a_position");
            glBindAttribLocation(prog, kAttribTexCoord, "a_uv");
            glLinkProgram(prog);

            glDetachShader(prog, vs);
            glDetachShader(prog, fs);

            GLint ok = 0;
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
            if (!ok) {
                GLint len = 0;
                glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
                std::string info(static_cast<size_t>(std::max(len, 1)), '\0');
                glGetProgramInfoLog(prog, static_cast<GLsizei>(info.size()), nullptr, info.data());
                log::error("[WhiteBot::Blur] link {} failed: {}", label, info.c_str());
                glDeleteProgram(prog);
                return 0;
            }

            glUseProgram(prog);
            glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
            return prog;
        }

        // --------------------------------------------------- resource handling
        inline void* currentWindow() {
#ifdef GEODE_IS_WINDOWS
            if (auto* view = CCEGLView::get()) {
                return static_cast<void*>(view->getWindow());
            }
#endif
            return nullptr;
        }

        // Xoá object GL (context hiện tại phải là context đã tạo ra chúng).
        inline void releaseObjects() {
            auto& g = state().gpu;
            if (g.progBlur) glDeleteProgram(g.progBlur);
            if (g.progComp) glDeleteProgram(g.progComp);
            if (g.fboA) glDeleteFramebuffers(1, &g.fboA);
            if (g.fboB) glDeleteFramebuffers(1, &g.fboB);
            if (g.capture) glDeleteTextures(1, &g.capture);
            if (g.texA) glDeleteTextures(1, &g.texA);
            if (g.texB) glDeleteTextures(1, &g.texB);
            if (g.vbo) glDeleteBuffers(1, &g.vbo);

            bool const failed = g.failed;
            g = Gpu{};
            g.failed = failed;
        }

        // Context cũ đã bị huỷ (ví dụ GD tạo lại window khi toggle fullscreen):
        // chỉ quên handle, KHÔNG glDelete vì tên object có thể trùng với object
        // của context mới.
        inline void forgetObjects() {
            auto& g = state().gpu;
            bool const failed = g.failed;
            g = Gpu{};
            g.failed = failed;
        }

        inline bool objectsStillValid() {
            auto const& g = state().gpu;
            return glIsTexture(g.capture) && glIsTexture(g.texA) && glIsTexture(g.texB) &&
                   glIsFramebuffer(g.fboA) && glIsFramebuffer(g.fboB) &&
                   glIsBuffer(g.vbo) && glIsProgram(g.progBlur) && glIsProgram(g.progComp);
        }

        inline bool createObjects() {
            auto& g = state().gpu;

            glGenTextures(1, &g.capture);
            glGenTextures(1, &g.texA);
            glGenTextures(1, &g.texB);
            glGenFramebuffers(1, &g.fboA);
            glGenFramebuffers(1, &g.fboB);
            glGenBuffers(1, &g.vbo);
            if (!g.capture || !g.texA || !g.texB || !g.fboA || !g.fboB || !g.vbo) {
                log::error("[WhiteBot::Blur] failed to generate GL objects");
                return false;
            }

            // Fullscreen quad (triangle strip): x, y, u, v
            static constexpr GLfloat kQuad[16] = {
                -1.0f, -1.0f, 0.0f, 0.0f,
                 1.0f, -1.0f, 1.0f, 0.0f,
                -1.0f,  1.0f, 0.0f, 1.0f,
                 1.0f,  1.0f, 1.0f, 1.0f,
            };
            glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);

            GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexSrc, "vertex");
            GLuint fsBlur = compileShader(GL_FRAGMENT_SHADER, kBlurFragSrc, "blur fragment");
            GLuint fsComp = compileShader(GL_FRAGMENT_SHADER, kCompositeFragSrc, "composite fragment");

            if (vs && fsBlur && fsComp) {
                g.progBlur = linkProgram(vs, fsBlur, "blur program");
                g.progComp = linkProgram(vs, fsComp, "composite program");
            }
            if (vs) glDeleteShader(vs);
            if (fsBlur) glDeleteShader(fsBlur);
            if (fsComp) glDeleteShader(fsComp);

            if (!g.progBlur || !g.progComp) return false;

            g.uBlurStep = glGetUniformLocation(g.progBlur, "u_step");
            g.uCompDim = glGetUniformLocation(g.progComp, "u_dim");
            g.uCompAmount = glGetUniformLocation(g.progComp, "u_amount");
            return true;
        }

        inline void allocTexture(GLuint tex, int w, int h) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        inline bool attachTexture(GLuint fbo, GLuint tex) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }

        // Cấp phát lại nội dung texture hiện có (cùng tên), KHÔNG tạo object mới.
        inline bool resizeTargets(int w, int h, int div) {
            auto& g = state().gpu;

            GLint maxSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
            if (w > maxSize || h > maxSize) {
                log::error("[WhiteBot::Blur] viewport {}x{} exceeds GL_MAX_TEXTURE_SIZE {}", w, h, maxSize);
                return false;
            }

            int const lw = std::max(1, w / div);
            int const lh = std::max(1, h / div);

            allocTexture(g.capture, w, h);
            allocTexture(g.texA, lw, lh);
            allocTexture(g.texB, lw, lh);

            if (!attachTexture(g.fboA, g.texA) || !attachTexture(g.fboB, g.texB)) {
                log::error("[WhiteBot::Blur] framebuffer incomplete at {}x{}", lw, lh);
                return false;
            }

            g.capW = w;
            g.capH = h;
            g.lowW = lw;
            g.lowH = lh;
            g.div = div;
            return true;
        }

        inline bool ensureResources(StateGuard const& st) {
            auto& s = state();
            auto& g = s.gpu;

            if (g.ready) {
                if (currentWindow() != g.window || !objectsStillValid()) {
                    log::warn("[WhiteBot::Blur] GL context changed, recreating resources");
                    forgetObjects();
                }
            }

            if (!g.ready) {
                if (!createObjects()) {
                    releaseObjects();
                    g.failed = true;
                    return false;
                }
                g.window = currentWindow();
                g.ready = true;
            }

            int const w = st.viewport[2];
            int const h = st.viewport[3];
            int const div = std::clamp(s.cfg.downscale, 1, 8);

            if (w != g.capW || h != g.capH || div != g.div) {
                if (!resizeTargets(w, h, div)) {
                    releaseObjects();
                    g.failed = true;
                    return false;
                }
            }
            return true;
        }

        // ------------------------------------------------------------- render
        inline void drawQuad(Gpu const& g) {
            glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
            glVertexAttribPointer(kAttribPosition, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                  reinterpret_cast<const void*>(0));
            glVertexAttribPointer(kAttribTexCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                  reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
            glEnableVertexAttribArray(kAttribPosition);
            glDisableVertexAttribArray(kAttribColor);
            glEnableVertexAttribArray(kAttribTexCoord);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        inline void blurPass(Gpu const& g, GLuint srcTex, int srcW, int srcH,
                             bool horizontal, GLuint dstFbo, float radius) {
            glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
            glViewport(0, 0, g.lowW, g.lowH);
            glBindTexture(GL_TEXTURE_2D, srcTex);
            glUniform2f(g.uBlurStep,
                        horizontal ? radius / static_cast<float>(srcW) : 0.0f,
                        horizontal ? 0.0f : radius / static_cast<float>(srcH));
            drawQuad(g);
        }

        inline void process(StateGuard const& st) {
            auto& s = state();
            auto const& g = s.gpu;

            int const passes = std::clamp(s.cfg.passes, 1, 12);
            float const radius = std::clamp(s.cfg.radius, 0.25f, 8.0f);
            float const dim = std::clamp(s.cfg.dim, 0.0f, 1.0f);
            float const a = std::clamp(s.amount, 0.0f, 1.0f);
            float const amount = a * a * (3.0f - 2.0f * a);  // smoothstep

            glActiveTexture(GL_TEXTURE0);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_BLEND);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // 1) Capture framebuffer đang bound (đúng vùng viewport).
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(st.fbo));
            glBindTexture(GL_TEXTURE_2D, g.capture);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, st.viewport[0], st.viewport[1], g.capW, g.capH);

            // 2) Blur: H (kèm downsample) -> V, rồi ping-pong thêm (passes-1) vòng.
            glUseProgram(g.progBlur);
            blurPass(g, g.capture, g.capW, g.capH, true, g.fboA, radius);
            blurPass(g, g.texA, g.lowW, g.lowH, false, g.fboB, radius);
            for (int i = 1; i < passes; ++i) {
                blurPass(g, g.texB, g.lowW, g.lowH, true, g.fboA, radius);
                blurPass(g, g.texA, g.lowW, g.lowH, false, g.fboB, radius);
            }

            // 3) Composite kết quả (texB) lên framebuffer gốc, giữ nguyên alpha của dest.
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(st.fbo));
            glViewport(st.viewport[0], st.viewport[1], st.viewport[2], st.viewport[3]);
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glUseProgram(g.progComp);
            glUniform1f(g.uCompDim, dim);
            glUniform1f(g.uCompAmount, amount);
            glBindTexture(GL_TEXTURE_2D, g.texB);
            drawQuad(g);

            auto& mg = state().gpu;
            if (!mg.errorLogged) {
                GLenum const err = glGetError();
                if (err != GL_NO_ERROR) {
                    mg.errorLogged = true;
                    log::warn("[WhiteBot::Blur] GL error 0x{:X} while rendering blur", static_cast<unsigned>(err));
                }
            }
        }

        inline bool advanceFade(State& s) {
            auto const now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - s.lastTick).count();
            s.lastTick = now;
            dt = std::clamp(dt, 0.0f, 0.1f);

            float const fade = std::clamp(s.cfg.fadeSeconds, 0.0f, 3.0f);
            if (fade <= 0.0001f) {
                s.amount = s.target ? 1.0f : 0.0f;
            } else {
                s.amount = std::clamp(s.amount + (s.target ? dt : -dt) / fade, 0.0f, 1.0f);
            }
            return s.amount > 0.0f;
        }

        // Trả về true nếu đã vẽ blur.
        inline bool render() {
            auto& s = state();
            if (!s.initialized) return false;
            if (!s.target && s.amount <= 0.0f) return false;  // idle: không tốn GL call nào
            if (!advanceFade(s)) return false;
            if (s.gpu.failed) return false;

            StateGuard guard;  // restore toàn bộ state GL khi ra khỏi scope
            if (guard.viewport[2] <= 0 || guard.viewport[3] <= 0) return false;
            if (!ensureResources(guard)) return false;

            process(guard);
            return true;
        }

        inline void onSwap() {
            auto& s = state();
            if (s.appliedThisFrame) {
                s.appliedThisFrame = false;
                return;
            }
            render();
        }
    } // namespace detail

    // =========================================================== public API
    // Gọi khi mod load. Không đụng tới GL (context có thể chưa sẵn sàng);
    // resource được tạo lazy ở frame đầu tiên cần blur.
    inline void init() {
        auto& s = detail::state();
        if (s.initialized) return;
        s.initialized = true;
        geode::log::info("[WhiteBot::Blur] initialized");
    }

    // Bật blur (fade in). Gọi nhiều lần an toàn.
    inline void enable() {
        auto& s = detail::state();
        init();
        if (!s.target) s.lastTick = std::chrono::steady_clock::now();
        s.target = true;
        s.gpu.failed = false;  // cho phép thử lại nếu lần trước lỗi shader/FBO
    }

    // Tắt blur (fade out rồi dừng hẳn, không còn tốn GPU). Gọi nhiều lần an toàn.
    inline void disable() {
        detail::state().target = false;
    }

    inline bool isEnabled() {
        return detail::state().target;
    }

    // ----------------------------------------------------------- extra / tuning
    inline void setPasses(int passes)      { detail::state().cfg.passes = std::clamp(passes, 1, 12); }
    inline void setRadius(float radius)    { detail::state().cfg.radius = std::clamp(radius, 0.25f, 8.0f); }
    inline void setDownscale(int divisor)  { detail::state().cfg.downscale = std::clamp(divisor, 1, 8); }
    inline void setDim(float dim)          { detail::state().cfg.dim = std::clamp(dim, 0.0f, 1.0f); }
    inline void setFadeDuration(float sec) { detail::state().cfg.fadeSeconds = std::clamp(sec, 0.0f, 3.0f); }
    inline Config getConfig()              { return detail::state().cfg; }

    // Blur ngay tại thời điểm này (framebuffer hiện tại = mọi thứ đã vẽ trước UI).
    // Dùng khi UI là node Cocos hoặc khi cần tự quyết thứ tự render.
    inline void applyNow() {
        auto& s = detail::state();
        s.appliedThisFrame = true;
        detail::render();
    }

    // Giải phóng toàn bộ GL object. Chỉ gọi trên GL thread khi context còn sống.
    inline void shutdown() {
        auto& s = detail::state();
        s.target = false;
        s.amount = 0.0f;
        s.appliedThisFrame = false;
        if (s.gpu.ready) detail::releaseObjects();
        s.gpu.failed = false;
    }

} // namespace WhiteBot::Blur

// =============================================================================
//  Hook: chạy blur ngay trước các hook swapBuffers khác (ImGui, ...), tức là
//  sau khi scene + pause layer đã vẽ xong và trước khi UI vẽ.
// =============================================================================
#ifndef WHITEBOT_BLUR_NO_HOOK
class $modify(WhiteBotBlurSwapHook, CCEGLView) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("CCEGLView::swapBuffers", geode::Priority::First);
    }

    void swapBuffers() {
        WhiteBot::Blur::detail::onSwap();
        CCEGLView::swapBuffers();
    }
};
#endif
