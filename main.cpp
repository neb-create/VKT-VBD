#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN // GLFW auto-loads Vulkan header
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdlib>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define U32T(v) (static_cast<uint32_t>(v))

using namespace std;
using namespace vk::raii;
using namespace glm;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

constexpr int MAX_FRAMES_IN_FLIGHT = 2; // Shouldn't be too many, don't want GPU to fall behind CPU

struct UniformBufferObject {
    alignas(4) float off;
    alignas(16) mat4 model;
    alignas(16) mat4 view;
    alignas(16) mat4 proj;
};

struct Vertex {
    vec2 pos;
    vec3 color;
    vec2 uv;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex }; // binding index, stride, load data per vertex
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() {
        // Location, Binding Index
        return {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv))
        };
    }
};

const vector<Vertex> vertices = {
    {vec2(-0.5f, -0.5f), vec3(1.0f, 1.0f, 0.0f), vec2(0,0)},
    {vec2(-0.5f, 0.5f), vec3(0.0f, 1.0f, 0.0f), vec2(0,1)},
    {vec2(0.5f, -0.5f), vec3(0.0f, 0.0f, 1.0f), vec2(1,0)},
    {vec2(0.5f, 0.5f), vec3(1.0f, 1.0f, 1.0f), vec2(1,1)}
};

// Need uint32_t for massive meshes
const vector<uint16_t> indices = {
    0, 1, 2,
    1, 3, 2
};

static vector<char> readFile(const std::string& fileName) {
    std::ifstream file(fileName,
        std::ios::ate | // start from end of file to easily get filesize
        std::ios::binary // read as binary
    );

    if (!file.is_open()) {
        throw std::runtime_error("Couldn't open file");
    }

    vector<char> buffer(file.tellg()); // cursor pos is size
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    file.close();

    return buffer;
}

class Application {
public:
    void Run() {
        InitWindow();
        InitVulkan();
        MainLoop();
        Cleanup();
    }

private:
    // raii library so it'll do the cleanup for us
    GLFWwindow* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::PhysicalDeviceProperties physicalDeviceProperties;

    const vector<const char*> desiredValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    Device device = nullptr;
    Queue presentQueue = nullptr;
    Queue graphicsQueue = nullptr;
    vector<const char*> desiredDeviceExtensions = {
        vk::KHRSwapchainExtensionName
    };

    SwapchainKHR swapChain = nullptr;
    vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapSurfaceFormat;
    vk::Extent2D swapChainExtent;

    // We own so raii (unlike swapChainImages)?
    vector<vk::raii::ImageView> swapChainImageViews;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

    vk::raii::CommandPool commandPool = nullptr;
    vector<vk::raii::CommandBuffer> commandBuffers;

    vector<vk::raii::Semaphore> presentCompleteSemaphores;
    vector<vk::raii::Semaphore> renderFinishedSemaphores;
    vector<vk::raii::Fence> drawFences;

    uint32_t currFrameIndex;

    // True when user resizes
    bool frameBufferResized = false;

    //
    Buffer vertexBuffer = nullptr;
    DeviceMemory vertexBufferMemory = nullptr;
    Buffer indexBuffer = nullptr;
    DeviceMemory indexBufferMemory = nullptr;

    vector<Buffer> uniformBuffers; // Memory for each frame in flight so each frame can have diff uniform vals
    vector<DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    DescriptorPool descriptorPool = nullptr;
    vector<DescriptorSet> descriptorSets;

    Image textureImage = nullptr;
    DeviceMemory textureImageMemory = nullptr;
    ImageView textureImageView = nullptr;
    Sampler textureSampler = nullptr;
    

#ifdef NDEBUG // Not Debug, Part of C++ Standard
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    static void FrameBufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->frameBufferResized = true;
    }

    void InitWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // GLFW was for OpenGL, tell it don't create OpenGL Context
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanTESTT", nullptr, nullptr); // 4th param is Monitor
        glfwSetWindowUserPointer(window, this); // Give window a pointer to app
        glfwSetFramebufferSizeCallback(window, FrameBufferResizeCallback);
    }

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
        // messageSeverity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
        std::cerr << "VALIDATION LAYER ERROR: validation layer: " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

        return vk::False;
    }

    vector<const char*> GetRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (enableValidationLayers) {
            // Need this additional extension for a callback
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }

        return extensions;
    }

    void SetupDebugMessenger() {
        // Debug callback for validation layers so they can tell us what went wrong
        if (!enableValidationLayers) return;

        // When should our callback be called?
        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError); // Only the bad ones
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation); // Want all message types
        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
            .messageSeverity = severityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = &debugCallback
        };
        // There are more ways to configure callback, look at Validation Layers article bottom
        debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
    }

    void CreateInstance() {
        // Required validation Layers
        std::vector<char const*> requiredLayers;
        if (enableValidationLayers) {
            // Copy from begin to end
            requiredLayers.assign(desiredValidationLayers.begin(), desiredValidationLayers.end());
        }

        // Check if required validation layers are supported
        auto layerProperties = context.enumerateInstanceLayerProperties();
        for (const auto& requiredLayer : requiredLayers) {
            bool found = false;
            for (const auto& supportedLayer : layerProperties) {
                std::cout << "Supported Layer: " << supportedLayer.layerName << std::endl;
                if (strcmp(supportedLayer.layerName, requiredLayer) == 0)
                    found = true;
            }
            if (!found) {
                throw std::runtime_error("Required validation layer not supported!");
            }
        }

        constexpr vk::ApplicationInfo appInfo{
            .pApplicationName = "Vulkan TEST APP",
            .applicationVersion = VK_MAKE_VERSION(1,0,0),
            .pEngineName = "No Engine",
            .apiVersion = vk::ApiVersion14
        };

        // Desired Extensions
        auto extensions = GetRequiredExtensions();

        // Supported Extensions
        auto supportedExtensions = context.enumerateInstanceExtensionProperties();
        cout << "Supported Extensions:\n";
        for (const auto& extension : supportedExtensions) {
            cout << "\t" << extension.extensionName << endl;
        }

        vk::InstanceCreateInfo createInfo{
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()
        };

        try {
            instance = vk::raii::Instance(context, createInfo);
        }
        catch (const vk::SystemError& err) {
            std::cerr << "Vulkan error: " << err.what() << std::endl;
            return;
        }
        catch (const std::exception& err) {
            std::cerr << "Error: " << err.what() << std::endl;
        }

    }

    bool IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice) {
        auto deviceProperties = physicalDevice.getProperties();
        auto deviceFeatures = physicalDevice.getFeatures(); // What optional features?

        // Must support API Version >= 1.3
        bool isSuitable = physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3;
        isSuitable = isSuitable && deviceFeatures.samplerAnisotropy;

        // It must have a queue family that supports graphics calls (we assume it has presentation)
        auto queueFamilies = physicalDevice.getQueueFamilyProperties();

        bool foundGraphicsFamily = false;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
                foundGraphicsFamily = true;
                break;
            }
        }
        isSuitable = isSuitable && foundGraphicsFamily;

        // Check if all required device extensions available
        auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        isSuitable = isSuitable && std::ranges::all_of(desiredDeviceExtensions, [&availableDeviceExtensions](const auto& requiredExtension) {
            return std::ranges::any_of(availableDeviceExtensions, [&](const auto& availableExtension) {
                return strcmp(availableExtension.extensionName, requiredExtension) == 0;
                });
            });

        // Check features (corresponds to logical device features we add)
        auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        isSuitable = isSuitable &&
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        return isSuitable;
    }

    void FindQueueFamilyIndices(const PhysicalDevice& physicalDevice, uint32_t* pGraphicsIndex, uint32_t* pPresentIndex) {
        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        auto graphicsIter = std::ranges::find_if(queueFamilies, [&](const auto& qFamily) {
            return (qFamily.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
            });
        assert(graphicsIter != queueFamilies.end());
        *pGraphicsIndex = static_cast<uint32_t>(std::distance(queueFamilies.begin(), graphicsIter));
        *pPresentIndex = physicalDevice.getSurfaceSupportKHR(*pGraphicsIndex, *surface)
            ? *pGraphicsIndex : U32T(queueFamilies.size());

        if (*pPresentIndex == queueFamilies.size()) {
            // GraphicsIndex != PresentIndex, find queue family supports both
            for (uint32_t i = 0; i < queueFamilies.size(); i++) {
                if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0)
                    && physicalDevice.getSurfaceSupportKHR(i, *surface)) {
                    *pGraphicsIndex = i;
                    *pPresentIndex = i;
                    return;
                }
            }
            throw std::runtime_error("Couldn't find single queue that supported both graphics and presentation, which we're requiring right now.");
        }
        else {
            return;
        }
    }

    void PickPhysicalDevice() {
        // List graphics cards
        vector<vk::raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
        if (devices.empty()) {
            throw std::runtime_error("No GPUs that support vulkan");
        }
        // We gotta manually go through each device whereas WebGPU we could just say what features we want mostly
        const auto devIter = std::ranges::find_if(devices, [&](const auto& physicalDevice) { return IsDeviceSuitable(physicalDevice); });
        if (devIter == devices.end()) {
            throw std::runtime_error("Couldn't find suitable GPU!");
        }
        physicalDevice = *devIter;
        physicalDeviceProperties = physicalDevice.getProperties();
    }

    uint32_t graphicsIndex, presentIndex;
    void CreateLogicalDevice() {
        // uint32_t graphicsIndex, presentIndex; // Very likely same qFamily
        FindQueueFamilyIndices(physicalDevice, &graphicsIndex, &presentIndex);
        float queuePriority = 0.5f; // [0,1] mandatory even if 1 queue
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = graphicsIndex, // Index within physical device
            .queueCount = 1,
            .pQueuePriorities = &queuePriority }; // For creating the graphics queue
        // We can only have a few queues per queue family, and we only really need one per family.  We can create cmd buffers on multiple threads and submit all of them on main thread with low overhead

        // Modern simplification to auto chain structs with pNext
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2, // A
            vk::PhysicalDeviceVulkan11Features, // B
            vk::PhysicalDeviceVulkan13Features, // C 
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT // D
        >
            featureChain = {
                {.features = {.samplerAnisotropy = true }}, // constructor for A,
                {.shaderDrawParameters = true}, // B
                {.synchronization2 = true, .dynamicRendering = true}, // constructor for C, dynamic rendering is a modern simplification
                {.extendedDynamicState = true}
        };

        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(), // The pointer to the first in the chain
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(desiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = desiredDeviceExtensions.data() // Device specific extensions
        }; // Device specific validation layers obsolete so not here

        device = Device(physicalDevice, deviceCreateInfo);
        graphicsQueue = Queue(device, graphicsIndex, 0); // Get queue from our new device, 0 is the queue index within family, only 1 queue so we put 0
        presentQueue = Queue(device, presentIndex, 0);
        cout << "Graphics Index: " << graphicsIndex << endl;
        cout << "Present Index: " << presentIndex << endl;
    }

    vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const vector<vk::SurfaceFormatKHR>& availableFormats) {
        // SurfaceFormatKHR includes surface format (RGBA32, R8..) and color space, if it supports srgb color space
        // We'll look for RGBA8, SRGB
        for (const auto& format : availableFormats) {
            if (format.format == vk::Format::eR8G8B8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return format;
            }
        }
        cerr << "Couldn't find RGBA8, SRGB format" << endl;
        return availableFormats[0];
    }

    vk::PresentModeKHR ChooseSwapPresentMode(const vector<vk::PresentModeKHR> availableModes) {
        // Nice explanation of different modes at https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html
        // Mailbox is generally best, but we'll return the best we can find
        // On mobile, regular FIFO is better
        for (const auto& mode : availableModes) {
            if (mode == vk::PresentModeKHR::eMailbox) {
                return mode;
            }
        }
        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
        // The resolution of the swapchain images in pixels
        // Usually equal to resolution of window we're drawing to, but
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        // vulkan tells us to differ when currentExtent at uint32 max, so we'll get best in range val
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }

    void CreateSwapchain() {
        vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(surface);
        vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(surface);
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);

        swapSurfaceFormat = ChooseSwapSurfaceFormat(availableFormats);
        auto presentMode = ChooseSwapPresentMode(availablePresentModes);
        swapChainExtent = ChooseSwapExtent(surfaceCapabilities);

        auto swapChainImageFormat = swapSurfaceFormat.format;

        uint32_t imageCount = surfaceCapabilities.minImageCount + 1; // How many imgs in swapchain you need to function plus one to avoid waiting
        if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount) {
            // maxImageCount == 0 implies no maximum
            imageCount = surfaceCapabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR swapChainCreateInfo = {
            .flags = vk::SwapchainCreateFlagsKHR(),
            .surface = *surface,
            .minImageCount = imageCount,
            .imageFormat = swapSurfaceFormat.format,
            .imageColorSpace = swapSurfaceFormat.colorSpace,
            .imageExtent = swapChainExtent,
            .imageArrayLayers = 1, // # layers in each image (for stereoscopic 3D apps)
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque, // how does the window blend with other windows?
            .presentMode = presentMode,
            .clipped = true, // don't care about pixels obscured by other windows
            .oldSwapchain = nullptr, // advanced, for when this chain becomes obselete (on resize) and we need a new one
        };

        array<uint32_t, 2> queueFamilyIndices = { graphicsIndex, presentIndex };
        if (graphicsIndex != presentIndex) {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent; // Queues don't hog images when they need to share to drawing and presenting
            swapChainCreateInfo.queueFamilyIndexCount = 2;
            swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data(); // The family that owns the images
        }
        else {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
            swapChainCreateInfo.queueFamilyIndexCount = 0;
            swapChainCreateInfo.pQueueFamilyIndices = nullptr;
        }

        swapChain = SwapchainKHR(device, swapChainCreateInfo);
        swapChainImages = swapChain.getImages();
    }

    void CreateSurface() {
        VkSurfaceKHR rawSurface;
        // Deref raii object acts as pointer
        if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface)) { // C API for this :(
            throw std::runtime_error("Failed to create window surface");
        }
        surface = vk::raii::SurfaceKHR(instance, rawSurface); // Nice auto cleanup version
    }

    void CreateImageViews() {
        swapChainImageViews.clear();

        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType = vk::ImageViewType::e2D,
            .format = swapSurfaceFormat.format,
            .subresourceRange = {
                vk::ImageAspectFlagBits::eColor,
                0, // BaseMip
                1, // LevelCount
                0, // baseArrayLayer
                1  // LayerCount (for stereographic)
            },
        };

        for (auto image : swapChainImages) {
            imageViewCreateInfo.image = image;
            // vk::raii::ImageView imgView = vk::raii::ImageView(device, imageViewCreateInfo); then pushing back Doesn't work without moving ig
            swapChainImageViews.emplace_back(device, imageViewCreateInfo); // constructs ImageView then pushes
        }

    }

    // [[nodiscard]] will make program throw error is programmer calls func without using return value
    [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(const vector<char>& compiledCode) const {
        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = compiledCode.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(compiledCode.data()) // compiledCode.data() is char* but we wanna read vector as if it has uint32_t (1 byte to 4 byte is dangerous for alignment but vector is aligned to 4 bytes so we good)
        };

        vk::raii::ShaderModule shaderModule(device, createInfo);

        return shaderModule;
    }

    void CreateGraphicsPipeline() {
        // Do programmable stages; Vertex, Fragment
        // Then fixed-function parameter setup for blending mode, viewport, rasterization

        // PROGRAMMABLE
        auto shaderModule = CreateShaderModule(readFile("shaders/slang.spv"));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shaderModule,
            .pName = "vertMain" // Name of main func in shader
        };
        // Optional pSpecializationInfo member you can specify values for compile time shader constants (more efficient than uniform)
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shaderModule,
            .pName = "fragMain" // Name of main func in shader
        };
        array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
            vertShaderStageInfo, fragShaderStageInfo
        };

        // FIXED

        // So much of pipeline state is immutable, baked, but
        // some of it can be changed without recreating pipeline at draw time (viewport size, line width, blend constants)
        vector dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = U32T(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        // Choosing to make this dynamic will make us HAVE TO specify it at drawing time and the baked config vals for it will be ignored

        // Format of vertex data = (bindings = spacing, attribute desc = types of attributes)
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,

            .vertexAttributeDescriptionCount = attributeDescriptions.size(),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {
            .topology = vk::PrimitiveTopology::eTriangleList
        };

        // Static examples below, but we're using dynamic so no need to specify
        //// May differ from WIDTH, HEIGHT of window
        //// Viewport defines transformation from image to framebuffer
        //vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height),
        //    0.0f, 1.0f); // Range of depth vals to use for frame buffer

        //// Scissor rectangle discards pixels out of the region
        //// While viewport rectangle stretches image into it
        //vk::Rect2D scissorRect(vk::Offset2D{ 0,0 }, swapChainExtent);

        vk::PipelineViewportStateCreateInfo viewportState = {
            .viewportCount = 1,
            .scissorCount = 1
        };

        // RASTERIZER
        vk::PipelineRasterizationStateCreateInfo rasterizer = {
            .depthClampEnable = vk::False, // frags beyond near far plane are clamped to them instead of discarded, using this requres enabling a gpu feature
            .rasterizerDiscardEnable = vk::False, // disables output to framebuffer
            .polygonMode = vk::PolygonMode::eFill, // could be used for lines or points (requires gpu feature for non fill)
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = vk::False, // could alter depth vals based on const or frags slope or wtver
            .depthBiasSlopeFactor = 1.0f,
            .lineWidth = 1.0f // > 1 requires gpu feature
        };

        // Disable multisampling (anti-aliasing, but better than rendering to high res => downsampling)
        vk::PipelineMultisampleStateCreateInfo multiSampling = {
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = vk::False
        };

        // we'll do depth and stencil testing later

        vk::PipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = vk::True,

            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vk::BlendOp::eAdd,

            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,

            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        // Pseudo Code below demonstrates blending
        /*if (blendEnable) {
            finalColor.rgb = (srcColorBlendFactor * newColor.rgb) COLORBLEND_OP (dstColorBlendFactor * oldColor.rgb);
            finalColor.a = (srcAlphaBlendFactor * newColor.a) ALPHABLEND_OP (dstAlphaBlendFactor * oldColor.a);
        }
        else {
            finalColor = newColor;
        }
        finalColor = finalColor & colorWriteMask;*/

        vk::PipelineColorBlendStateCreateInfo colorBlending = {
            .logicOpEnable = vk::False, // Alternate method of color blending we won't use since we're using the normal method above (will auto DISABLE FIRST METHOD)
            .logicOp = vk::LogicOp::eCopy, // That method is specifying Blending Logic Here
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {
            .setLayoutCount = 1,
            .pSetLayouts = &*descriptorSetLayout,
            .pushConstantRangeCount = 0 // different way of pushing dynamic vals to shaders
        };
        pipelineLayout = PipelineLayout(device, pipelineLayoutInfo);

        // Dynamic (simplified) rendering setup
        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo = {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapSurfaceFormat.format
        };

        vk::GraphicsPipelineCreateInfo pipelineInfo = {
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = 2, .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState, .pRasterizationState = &rasterizer,
            .pMultisampleState = &multiSampling, .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState, .layout = pipelineLayout,
            .renderPass = nullptr, // dynamic rendering removes need for render pass

            // OPTIONAL, you can make pipelines derive from a similar pipeline to simplify and speedup creation, we're not doing that here so it's optional
            // would also need VK_PIPELINE_CREATE_DERIVATIVE_BIT
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        };

        // nullptr is PipelineCache object which stores creation info across multiple calls to create pipeline, speed up pipeline creation significantly
        graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);

    }

    void CreateCommandPool() {
        vk::CommandPoolCreateInfo poolInfo = {
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, // cmd buffers in pool can be rerecorded individually instead of together
            .queueFamilyIndex = graphicsIndex // all these cmd buffers are only for graphics
        };
        commandPool = vk::raii::CommandPool(device, poolInfo);

    }

    void CreateCommandBuffers() {
        vk::CommandBufferAllocateInfo allocateInfo = {
            .commandPool = commandPool,
            .level = vk::CommandBufferLevel::ePrimary, // submitted to queue directly, not used by other cmd bufs, secondary helpful for reusing command ops from primary
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT // command makes multiple, we want one
        };
        commandBuffers = vk::raii::CommandBuffers(device, allocateInfo);
    }

    // images can be in different layouts at different times
    // depending on what we're using the img for
    // presenting has a diff layout than rendering (for optimization sake)
    void TransitionImageLayout(
        vk::raii::CommandBuffer& commandBuffer,
        uint32_t imageIndex,

        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask
    ) {
        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapChainImages[imageIndex],
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
            }
        };
        vk::DependencyInfo dependencyInfo = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    void RecordCommandBuffer(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex, uint32_t frameIndex) {
        // if cmd buffer already was recorded, beginning will reset it.  we can't append once we record a cmd buffer, only reset
        commandBuffer.begin({}); // could put flags in here

        // Transition to COLOR ATTACHMENT OPTIMAL
        TransitionImageLayout(
            commandBuffer,
            imageIndex,
            vk::ImageLayout::eUndefined, // From any?
            vk::ImageLayout::eColorAttachmentOptimal, // To this format
            {}, // What access to wait for?  We don't wanna wait for anything
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::ClearColorValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {
            .imageView = swapChainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear, // what to do before rendering?
            .storeOp = vk::AttachmentStoreOp::eStore, // what to do after rendering?
            .clearValue = clearColor
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0,0}, .extent = swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentInfo // Which color attachments we rendering to?
        };

        // START RENDER
        // All record cmds return void so no error handling til we finished recording
        commandBuffer.beginRendering(renderingInfo);

        // Bind Graphics Pipeline and Geo Data
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindVertexBuffers(0, *vertexBuffer, { 0 }); // Bind buffer to our binding which has layout and stride stuff {0} is array of vertex buffers to bind
        commandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint16);

        // remember in the pipeline we specified viewport and scissor state as dynamic, so we gotta specify them now
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height)));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

        // graphics pipeline, pipeline layout descriptors r based on, index of first descriptor, array of sets to bind
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSets[frameIndex], nullptr);
        // IndexCount, InstanceCount, IndexBufferOffset, VertexBufferOffset, InstanceOffset
        commandBuffer.drawIndexed(indices.size(), 1, 0, 0, 0);

        commandBuffer.endRendering();
        // END RENDER

        // Have to transition image layout to presentable format
        TransitionImageLayout(
            commandBuffer,
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, // We wanna wait for writing ops
            {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

        commandBuffer.end();

    }

    void CreateSyncObjects() {
        assert(
            presentCompleteSemaphores.empty() &&
            renderFinishedSemaphores.empty() &&
            drawFences.empty()
        );

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            drawFences.emplace_back(device, vk::FenceCreateInfo{
            .flags = vk::FenceCreateFlagBits::eSignaled
                });
        }
    }

    void CleanupSwapChain() {
        swapChainImageViews.clear();
        swapChain = nullptr;
    }

    void RecreateSwapChain() {
        // In case, user minimized, wait til it's not minimized
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        device.waitIdle();

        CleanupSwapChain();

        CreateSwapchain();
        CreateImageViews();
    }

    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
        // Find what memory vertex buffer shoul use
        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if (
                (typeFilter & (1 << i)) && // is in our type filter
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties // has at least all props we want
                ) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type");
    }

    void CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer* buffer, vk::raii::DeviceMemory* bufferMemory) {
        vk::BufferCreateInfo bufferInfo{ .size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive };
        *buffer = vk::raii::Buffer(device, bufferInfo);

        vk::MemoryRequirements memRequirements = buffer->getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size, .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties) };
        *bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
        buffer->bindMemory(*bufferMemory, 0); // 0 is offset within memory region, nonzero needs divisible by memRequirements.alignment
        // hostCoherence ensures CPU memory = GPU memory so dont need to explicitly time this
        // GPU data guaranteed to be there by next queueSubmit
    }

    // For practical applications, you should combine multiple operations into single command buffer instead of beginning and ending with like one command and then waiting on idle queue (CopyBuffer..)
    // Can try that
    CommandBuffer BeginOneTimeCommands() {
        // Create a temp command buf, could create a different one for this exclusive purpose
        vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
        vk::raii::CommandBuffer cmd = std::move(device.allocateCommandBuffers(allocInfo).front());
        // use temporary?
        cmd.begin(vk::CommandBufferBeginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            });
        return cmd;
    }

    void SubmitOneTimeCommands(CommandBuffer* cmd) {
        cmd->end();
        // Graphics queue MUST support TRANSFER BIT
        graphicsQueue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &**cmd }, nullptr);
        graphicsQueue.waitIdle(); // fence would be necessary with multiple transfers scheduled simultaneously
    }

    void CopyBuffer(vk::raii::Buffer& src, vk::raii::Buffer& dst, vk::DeviceSize size) {
        auto cmd = BeginOneTimeCommands();
        cmd.copyBuffer(src, dst, vk::BufferCopy(0, 0, size)); // From where to where
        SubmitOneTimeCommands(&cmd);
    }

    void CreateVertexBuffer() {
        vk::DeviceSize bufferSize = sizeof(Vertex) * vertices.size();

        vk::raii::Buffer stagingBuffer = nullptr;;
        vk::raii::DeviceMemory stagingMemory = nullptr;
        CreateBuffer(bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc, // Can be source of a transfer
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
            &stagingBuffer, &stagingMemory);

        // Fill Vertex Buffer w Data
        void* stagingMemoryData = stagingMemory.mapMemory(0, bufferSize); // (0, bufSize) are offset and size; Map vertex buffer data to cpu memory
        memcpy(stagingMemoryData, vertices.data(), bufferSize);
        stagingMemory.unmapMemory();

        CreateBuffer(bufferSize,
            vk::BufferUsageFlagBits::eVertexBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal, // Device local, can't map memory directly
            &vertexBuffer, &vertexBufferMemory);

        CopyBuffer(stagingBuffer, vertexBuffer, bufferSize);
        // Staging buffer will be cleaned up RAII
        // Staging allows us to use high performance memory for loading vertex data
        // In practice, not good to do a separate allocation for every object, better to do one big one and split it up (VulkanMemoryAllocator library)
        // You should even go a step further, allocate a single vertex and index buffer for lots of things and use offsets to bindvertexbuffers to store lots of 3D objects
    }

    void CreateIndexBuffer() {
        vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        Buffer stagingBuffer = nullptr;
        DeviceMemory stagingMemory = nullptr;
        CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            &stagingBuffer, &stagingMemory);
        void* data = stagingMemory.mapMemory(0, bufferSize);
        memcpy(data, indices.data(), bufferSize);
        stagingMemory.unmapMemory();

        CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal,
            &indexBuffer, &indexBufferMemory);

        CopyBuffer(stagingBuffer, indexBuffer, bufferSize);
    }

    void CreateUniformBuffers() {
        uniformBuffers.clear(); // In case this is used as 'recreate'
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();

        // No staging buffer cuz we're updating this like every frame
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
            Buffer buffer = nullptr;
            DeviceMemory bufferMemory = nullptr;
            CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                &buffer, &bufferMemory);
            uniformBuffers.emplace_back(std::move(buffer)); 
            uniformBuffersMemory.emplace_back(std::move(bufferMemory));
            uniformBuffersMapped.emplace_back(uniformBuffersMemory[i].mapMemory(0, bufferSize));
            // We don't unmap, it's persistent mapping
        }
    }

    void CreateDescriptorSetLayout() {
        // Layout of descriptor set (sorta pointers to uniforms)
        array bindings = {
            vk::DescriptorSetLayoutBinding(
                0, // Binding index used in shader
                vk::DescriptorType::eUniformBuffer, // Type 
                1, // How many objects?
                vk::ShaderStageFlagBits::eVertex, // Where can we reference 
                nullptr), // Image sampling (later)

            vk::DescriptorSetLayoutBinding(
                1,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                vk::ShaderStageFlagBits::eFragment,
                nullptr),
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo = {
            .bindingCount = bindings.size(),
            .pBindings = bindings.data()
        };
        descriptorSetLayout = DescriptorSetLayout(device, layoutInfo);
    }

    // Need to create a pool for creating descriptor sets
    void CreateDescriptorPool() {
        // Inadequate descriptor pools may not be caught by validation layers
        std::array poolSize = {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)
        };
  
        vk::DescriptorPoolCreateInfo poolInfo = {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = MAX_FRAMES_IN_FLIGHT,

            .poolSizeCount = poolSize.size(),
            .pPoolSizes = poolSize.data()
        };
        descriptorPool = DescriptorPool(device, poolInfo);
    }

    void CreateDescriptorSets() {
        vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocateInfo = {
            .descriptorPool = descriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts = layouts.data()
        };

        // Allocate
        descriptorSets.clear();
        descriptorSets = device.allocateDescriptorSets(allocateInfo);
        assert(descriptorSets.size() == MAX_FRAMES_IN_FLIGHT);

        // Configure descriptor sets
        for (size_t i = 0; i < layouts.size(); i++) {
            // Descriptors that use buffers are configured with DescriptorBufferInfo
            vk::DescriptorBufferInfo bufferInfo = {
                .buffer = uniformBuffers[i],
                .offset = 0,
                .range = sizeof(UniformBufferObject)
            };
            vk::DescriptorImageInfo imageInfo = { 
                .sampler = textureSampler, 
                .imageView = textureImageView, 
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal 
            };
            std::array descriptorWrites = {
                vk::WriteDescriptorSet {
                    .dstSet = descriptorSets[i], // descriptor set to update
                    .dstBinding = 0, // binding from beginning of CreateDescriptorSetLayout
                    .dstArrayElement = 0, // descriptors can be arrays
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfo // for buffer data, pImageInfo would be used for image data
                },

                vk::WriteDescriptorSet{
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo
                }
            };
            
            device.updateDescriptorSets(descriptorWrites, {});
        }
    }

    void CreateImage(uint32_t width, uint32_t height, 
        vk::Format format, 
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage, 
        vk::MemoryPropertyFlags properties, 
        vk::raii::Image* image, vk::raii::DeviceMemory* imageMemory) {

        vk::ImageCreateInfo imageInfo = {
            .imageType = vk::ImageType::e2D,
            .format = format, // Same format as pixels in staging buffer
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1, // could be used to store sparsely, useful for 3D textures of voxel terrain with lots of air
            .tiling = tiling, // how to arrange texels Optimal = Implementation Dependent, Efficient while Linear = Linearly laid out rows (limited)
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        *image = Image(device, imageInfo);

        vk::MemoryRequirements memRequirements = image->getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties) };
        *imageMemory = vk::raii::DeviceMemory(device, allocInfo);
        image->bindMemory(*imageMemory, 0);
    }

    void CopyBufferToImage(const Buffer& buffer, Image* image, uint32_t width, uint32_t height) {
        CommandBuffer cmd = BeginOneTimeCommands();

        vk::BufferImageCopy region = { 
            .bufferOffset = 0, 
            .bufferRowLength = 0, // 0 implies tightly packed
            .bufferImageHeight = 0,
            
            // where to copy the pixels to?
            .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, 
            .imageOffset = {0, 0, 0}, 
            .imageExtent = {width, height, 1} 
        };
        // Assume image has already been transitioned to optimal layout at this point
        cmd.copyBufferToImage(buffer, *image, vk::ImageLayout::eTransferDstOptimal, { region });

        SubmitOneTimeCommands(&cmd);
    }

    void TransitionImageLayoutBasic(const Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
        auto cmd = BeginOneTimeCommands();

        vk::ImageMemoryBarrier barrier = {
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .image = image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        };

        // SourceStage which pipeline stages must happen before barrier
        // DestinationStage which pipeline stage waits on barrier
        // eByRegion means barrier is a per region condition
        vk::PipelineStageFlags srcStage, dstStage;
        // Hardcode layout transitions
        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            // Undefined -> Transfer Destination
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            srcStage = vk::PipelineStageFlagBits::eTopOfPipe; // No waiting needed, earliest possible stage to wait on
            dstStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            // Transfer Destination -> Shader Reading
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            srcStage = vk::PipelineStageFlagBits::eTransfer;
            dstStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else {
            throw std::invalid_argument("unsupported layout transition");
        }


        cmd.pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
        SubmitOneTimeCommands(&cmd);
    }

    void CreateTextureImage() {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load("textures/vulkanTutorialImage.jpg", 
            &texWidth, &texHeight, &texChannels, 
            STBI_rgb_alpha); // Forces loading alpha channel even if one doesnt exist
        vk::DeviceSize imageByteSize = texWidth * texHeight * 4;

        if (!pixels) {
            throw std::runtime_error("failed to load texture image");
        }

        // Staging to get the actual data closer to GPU (which we cant directly write to ig)
        Buffer stagingBuffer = nullptr;
        DeviceMemory stagingBufferMemory = nullptr;
        CreateBuffer(imageByteSize, 
            vk::BufferUsageFlagBits::eTransferSrc, 
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
            &stagingBuffer, &stagingBufferMemory);
        void* data = stagingBufferMemory.mapMemory(0, imageByteSize);
        memcpy(data, pixels, imageByteSize);
        stagingBufferMemory.unmapMemory();

        stbi_image_free(pixels);

        // We want to copy from the image staging buffer to an image (not just a buffer)
        CreateImage(texWidth, texHeight,
            vk::Format::eR8G8B8A8Srgb,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            &textureImage, &textureImageMemory);
        
        // We need to transition this image through multiple layouts
        // Undefined -> Optimized for Receiving Data -> Optimized for Shader Reading
        TransitionImageLayoutBasic(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        CopyBufferToImage(stagingBuffer, &textureImage, 
            static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    
        TransitionImageLayoutBasic(textureImage, 
            vk::ImageLayout::eTransferDstOptimal, 
            vk::ImageLayout::eShaderReadOnlyOptimal);


    }

    ImageView CreateImageView(const Image& image, vk::Format format) {
        vk::ImageViewCreateInfo viewInfo = { 
            .image = textureImage, 
            .viewType = vk::ImageViewType::e2D, 
            .format = vk::Format::eR8G8B8A8Srgb,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } 
        };
        return ImageView(device, viewInfo);
    }

    void CreateTextureImageView() {
        textureImageView = CreateImageView(textureImage, vk::Format::eR8G8B8A8Srgb);
    }

    void CreateTextureSampler() {
        vk::SamplerCreateInfo samplerInfo = { 
            .magFilter = vk::Filter::eLinear, 
            .minFilter = vk::Filter::eLinear,  

            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            

            .addressModeU = vk::SamplerAddressMode::eRepeat, 
            .addressModeV = vk::SamplerAddressMode::eRepeat,

            .anisotropyEnable = vk::True, // One frag sampling from lots of texels
            .maxAnisotropy = physicalDeviceProperties.limits.maxSamplerAnisotropy,
        
            .compareEnable = vk::False, 
            .compareOp = vk::CompareOp::eAlways,

            .borderColor = vk::BorderColor::eIntOpaqueBlack, // Only matters with clamp to border addressing mode
            .unnormalizedCoordinates = vk::False, // Use texDim for sampling or (0,1)

            
        };
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        // Sampler can be applied to any image
        textureSampler = Sampler(device, samplerInfo);
    }

    void InitVulkan() {
        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();

        CreateSwapchain();
        CreateImageViews();

        CreateDescriptorSetLayout();
        CreateGraphicsPipeline();

        CreateCommandPool();
        CreateCommandBuffers();

        CreateSyncObjects();

        CreateVertexBuffer();
        CreateIndexBuffer();
        CreateUniformBuffers();

        CreateTextureImage();
        CreateTextureImageView();
        CreateTextureSampler();

        CreateDescriptorPool();
        CreateDescriptorSets();
    }

    void UpdateUniformBuffer(uint32_t currFrame) {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    
        UniformBufferObject ubo = {
            .off = time,
            .model = glm::rotate(mat4(1.0f), -glm::radians(45.0f), vec3(1.0f,0.0f,0.0f)) * glm::rotate(mat4(1.0f), time, vec3(0.0f, 0.0f, 1.0f)),
            .view = glm::lookAt(vec3(0,0,5), vec3(0), vec3(0,1,0)),
            .proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 10.0f), // TODO: increase far
        };
        // glm::perspective outputs flipped y clip space, compensate
        ubo.proj[1][1] *= -1.0f;
        
        memcpy(uniformBuffersMapped[currFrame], &ubo, sizeof(ubo));
    }

    void DrawFrame() {
        // Wait for prev frame to finish
        // Get image from swap chain
        // Record command buffer which renders onto that image
        // Submit recorded command buffer
        // Present swap chain image

        // Semaphore enforces synchronization btwn 2 queue ops
        // Forces one queue op (the wait) to wait for another queue op (the signal) to finish
        // Once (the wait) finishes, signal turns back off to allow this process to happen again
        // Semaphores enforce gpu waiting, but how to do CPU waiting?

        // Fence synchronizes execution on CPU, we can wait on GPU ops
        // Create a fence, make queue op signal fence, and call a wait for fence signal func on cpu
        // Unlike semaphores, fences must be reset manually

        // Semaphores preferably since we don't block host


        auto& drawFence = drawFences[currFrameIndex];
        auto& presentCompleteSemaphore = presentCompleteSemaphores[currFrameIndex];
        auto& commandBuffer = commandBuffers[currFrameIndex];

        // Wait until prev frame finished
        // Takes in array of fences, true indicates we want to wait for all of em, UINT64_MAX is the timeout, effectively disabled
        auto fenceResult = device.waitForFences(*drawFence, vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw new std::runtime_error("Failed to wait for fence");
        }

        UpdateUniformBuffer(currFrameIndex);

        // Grab img from framebuffer now that prev frame done
        // presentCompleteSemaphore is signaled when image is finished being used
        auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphore, nullptr);
        if (result == vk::Result::eErrorOutOfDateKHR) { // frameBufferResized just in case app doesn't automatically do outOfDate
            RecreateSwapChain();
            return;
        }
        else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) { // suboptimal still ok
            throw new std::runtime_error("Failed to acquire swap chain img");
        }

        // We know we're not returning early, so put the fence back up so it'll be signaled on draw
        device.resetFences(*drawFence); // Put fence back up

        auto& renderFinishedSemaphore = renderFinishedSemaphores[imageIndex]; // This semaphore is per image because if it was per frame, a different frame in flight could be using a different semaphore on an image that hasn't finished being rendered to, since the semaphore would be different, it'd go through and we'd overwrite the image being drawn to

        // Record drawing cmds
        commandBuffer.reset();
        RecordCommandBuffer(commandBuffer, imageIndex, currFrameIndex);

        vk::PipelineStageFlags waitDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*presentCompleteSemaphore, // Wait for this semaphore to
            .pWaitDstStageMask = &waitDstStageMask, // Write colors

            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffer, // Submit this cmd buffer

            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSemaphore // Signal once done
        };

        graphicsQueue.submit(submitInfo, *drawFence); // Fence will be put down when done

        const vk::PresentInfoKHR presentInfoKHR = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*renderFinishedSemaphore, // Wait for render

            .swapchainCount = 1,
            .pSwapchains = &*swapChain,
            .pImageIndices = &imageIndex
        };
        result = presentQueue.presentKHR(presentInfoKHR);
        if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || frameBufferResized) {
            frameBufferResized = false;
            RecreateSwapChain();
        }
        else {
            assert(result == vk::Result::eSuccess);
        }

        // Advance to next frame
        currFrameIndex = (currFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void MainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents(); // Check for User Input Events
            DrawFrame();
        }

        // When we exit, wait for lingering commands to finish
        device.waitIdle();
    }

    void Cleanup() {
        CleanupSwapChain();

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