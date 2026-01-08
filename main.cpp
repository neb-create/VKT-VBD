
#define GLFW_INCLUDE_VULKAN // GLFW auto-loads Vulkan header
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

class Application {
public:
    void Run() {
        InitWindow();
        InitVulkan();
        MainLoop();
        Cleanup();
    }

private:
    GLFWwindow* window;

    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;

    void InitWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // GLFW was for OpenGL, tell it don't create OpenGL Context
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanTESTT", nullptr, nullptr); // 4th param is Monitor

    }

    void InitVulkan() {

    }

    void MainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents(); // Check for User Input Events

        }
    }

    void Cleanup() {
        // GLFW
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

int main() {
    Application app;

    try {
        app.Run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}