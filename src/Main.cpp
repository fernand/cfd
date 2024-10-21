#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <glad/gl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#pragma comment(lib, "glfw3.lib")

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
    const char* glsl_version = "#version 460";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
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

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}