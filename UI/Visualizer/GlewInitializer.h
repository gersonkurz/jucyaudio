#pragma once

namespace jucyaudio
{
    namespace ui
    {
        // Initialize GLEW for OpenGL function loading
        // Must be called with a valid OpenGL context current
        // Returns true on success, false on failure
        bool initializeGlew();

        // Get GLEW version string (for logging)
        const char* getGlewVersionString();

        // Log OpenGL version, vendor, renderer, and GLSL version
        void logOpenGLInfo();

        // Check for OpenGL errors and log them. Returns true if error occurred.
        bool checkGLError(const char* context);

        // Set OpenGL viewport (wrapper to avoid header conflicts)
        void setGLViewport(int x, int y, int width, int height);

        // Clear the screen with a color (for debugging)
        void glClearColor(float r, float g, float b, float a);
        
        // Clear buffers (Color, Depth, Stencil)
        void glClear();
    }
}
