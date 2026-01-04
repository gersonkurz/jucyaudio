// This file intentionally does NOT include any JUCE headers
// GLEW must be included before any OpenGL headers, and JUCE's OpenGL
// headers conflict with GLEW. By isolating GLEW in this compilation unit,
// we avoid the header conflicts.

#ifdef _WIN32
#include <GL/glew.h>
#endif

#include "GlewInitializer.h"
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        bool initializeGlew()
        {
#ifdef _WIN32
            GLenum err = glewInit();
            return (err == GLEW_OK);
#else
            // On macOS/Linux, OpenGL functions are loaded differently
            return true;
#endif
        }

        const char* getGlewVersionString()
        {
#ifdef _WIN32
            return reinterpret_cast<const char*>(glewGetString(GLEW_VERSION));
#else
            return "N/A";
#endif
        }

        void logOpenGLInfo()
        {
            const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
            
            spdlog::info("OpenGL Info: Version='{}', Vendor='{}', Renderer='{}', GLSL='{}'", 
                version ? version : "null", 
                vendor ? vendor : "null", 
                renderer ? renderer : "null",
                glsl ? glsl : "null");
        }

        bool checkGLError(const char* context)
        {
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                spdlog::error("OpenGL Error at {}: 0x{:x}", context, err);
                return true;
            }
            return false;
        }

        void setGLViewport(int x, int y, int width, int height)
        {
            glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
                       static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        }

        void glClearColor(float r, float g, float b, float a)
        {
            ::glClearColor(r, g, b, a);
        }

        void glClear()
        {
            ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        }
    }
}
