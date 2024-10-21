#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <glad/gl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#pragma comment(lib, "glfw3.lib")

#include <HandmadeMath.h>

#include <utility>

#include "OpenGLHelpers.h"
#include "Shader.h"

void setWindowPositionTopLeft(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    RECT windowRect, clientRect;
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    int frameHeight = (windowRect.bottom - windowRect.top) - (clientRect.bottom - clientRect.top);
    glfwSetWindowPos(window, 100, frameHeight);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_DEPTH_BITS, 32);
    const char* glsl_version = "#version 430";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow(1920 * 0.9, 1080 * 0.95, "CFD", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    setWindowPositionTopLeft(window);
    int kWindowWidth = 0;
    int kWindowHeight = 0;
    glfwGetWindowSize(window, &kWindowWidth, &kWindowHeight);
    glfwSwapInterval(1); // Enable vsync

    bool glad_err = gladLoadGL(glfwGetProcAddress) == 0;
    if (glad_err)
        return 1;

    const int width = 256;
    const int height = 256;
    const int num_velocities = 9; // D2Q9 model

    GLuint ssbo[2];
    glGenBuffers(2, ssbo);

    size_t bufferSize = width * height * num_velocities * sizeof(float);

    // SSBO for current distribution functions
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);

    // SSBO for updated distribution functions
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]);

    GLuint compute_shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute_shader, 1, &kComputeShader, nullptr);
    CompileShader(compute_shader);
    GLuint compute_program = glCreateProgram();
    glAttachShader(compute_program, compute_shader);
    LinkProgram(compute_program);

    while (!glfwWindowShouldClose(window))
    {
        glUseProgram(compute_program);

        glUniform1i(glGetUniformLocation(compute_program, "width"), width);
        glUniform1i(glGetUniformLocation(compute_program, "height"), height);
        glUniform1f(glGetUniformLocation(compute_program, "tau"), 0.6f);

        // Swap SSBOs for ping-pong buffering
        std::swap(ssbo[0], ssbo[1]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]);

        glUseProgram(compute_program);
        glDispatchCompute((GLuint)width / 16, (GLuint)height / 16, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}