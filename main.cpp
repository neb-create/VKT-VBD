#include "defines.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdlib>

#include <chrono>

#include "scene/buffer.h"
#include "scene/texture.h"
#include "core/command-helper.h"
#include "core/memory-helper.h"
#include "scene/mesh.h"
#include "scene/pipeline.h"
#include "scene/material.h"
#include "scene/shader-parameter.h"
#include "scene/render-pass.h"
#include "scene/render-target.h"
#include "lighting/probe-creator.h"

#include "game/gui-manager.h"
#include "game/camera.h"
#include "game/input-manager.h"
#include "game/test-game.h"
#include "helper/math.h"

#include "lighting/gi-manager.h"

using namespace std;
using namespace vk::raii;
using namespace glm;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
const string MODEL_PATH = "models/viking_room.obj";
const string TEXTURE_PATH = "textures/viking_room.png";

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

    vk::PhysicalDeviceProperties physicalDeviceProperties;

    const vector<const char*> desiredValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    VulkanReferences coreReferences;
    Queue presentQueue = nullptr;
    // Queue graphicsQueue = nullptr;
    vector<const char*> desiredDeviceExtensions = {
        vk::KHRSwapchainExtensionName,
        // vk::EXTShaderAtomicFloatExtensionName, // ATOMIC
        vk::KHRComputeShaderDerivativesExtensionName
    };

    SwapchainKHR swapChain = nullptr;
    vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapSurfaceFormat;
    vk::Extent2D swapChainExtent;

    // We own so raii (unlike swapChainImages)?
    vector<vk::raii::ImageView> swapChainImageViews;

    ShaderPipeline shaderPipeline;

    ShaderPipeline skyboxShader;
    Material skyboxMaterial;

    ShaderPipeline reflectShader;
    Material reflectMaterial;

    vector<vk::raii::CommandBuffer> commandBuffers;

    vector<vk::raii::Semaphore> presentCompleteSemaphores;
    vector<vk::raii::Semaphore> renderFinishedSemaphores;
    vector<vk::raii::Fence> drawFences;

    // Only one image since only one draw op running at once
    WTexture depthTexture;

    uint32_t currFrameIndex;

    // True when user resizes
    bool frameBufferResized = false;

    Mesh blobMesh;
    Mesh chairMesh;
    Mesh cubeMesh;
    Mesh testRoom;

    vector<WBuffer> uniformBuffers; // Memory for each frame in flight so each frame can have diff uniform vals
    vector<WBuffer> uEntityBuffers;

    Material testMaterial;
    Material afterMaterial;
    Material blobMaterial;

    WTexture whiteTexture;

    WTexture testTexture;
    WTexture metallic;
    WTexture roughness;
    WTexture ao;
    WTexture testCubeMap;

    WTexture testRoomTexture;

    uPtr<GIManager> giManager;

    TestGame game;

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

        // Setup GLFW Input Callbacks
        glfwSetWindowUserPointer(window, this);
        // frame buffer size callback here
        glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
            auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
            vec2 pos = vec2(xpos, ypos) / vec2(WIDTH, HEIGHT);
            pos.y = 1.0f - pos.y;
            if (app) app->inputManager.OnMouseMove(pos);
            });
        glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int /*mods*/) {
            auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app) app->inputManager.OnMouseClick(button == 0, action == 1);
            });
        glfwSetScrollCallback(window, [](GLFWwindow* window, double /*xoffset*/, double yoffset) {
            auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app) app->inputManager.OnMouseScroll(static_cast<float>(yoffset));
            });
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
        isSuitable = isSuitable && deviceFeatures.geometryShader;

        // It must have a queue family that supports graphics calls (we assume it has presentation)
        auto queueFamilies = physicalDevice.getQueueFamilyProperties();

        bool foundGraphicsFamily = false;
        for (const auto& queueFamily : queueFamilies) {
            // Require Graphics & Compute
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics && queueFamily.queueFlags & vk::QueueFlagBits::eCompute) {
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
        auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT, vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR>();
        isSuitable = isSuitable &&
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
            // features.template get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>().shaderBufferFloat32AtomicAdd && // ATOMIC 
            features.template get<vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR>().computeDerivativeGroupQuads;

        return isSuitable;
    }

    void FindQueueFamilyIndices(const PhysicalDevice& physicalDevice, uint32_t* pGraphicsAndComputeIndex, uint32_t* pPresentIndex) {
        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        auto graphicsAndComputeIter = std::ranges::find_if(queueFamilies, [&](const auto& qFamily) {
            return (qFamily.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)) != static_cast<vk::QueueFlags>(0);
            });
        assert(graphicsAndComputeIter != queueFamilies.end());
        *pGraphicsAndComputeIndex = static_cast<uint32_t>(std::distance(queueFamilies.begin(), graphicsAndComputeIter));
        *pPresentIndex = physicalDevice.getSurfaceSupportKHR(*pGraphicsAndComputeIndex, *surface)
            ? *pGraphicsAndComputeIndex : U32T(queueFamilies.size());

        if (*pPresentIndex == queueFamilies.size()) {
            // GraphicsIndex != PresentIndex, find queue family supports both
            for (uint32_t i = 0; i < queueFamilies.size(); i++) {
                if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0)
                    && physicalDevice.getSurfaceSupportKHR(i, *surface)) {
                    *pGraphicsAndComputeIndex = i;
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
        coreReferences.physicalDevice = *devIter;
        coreReferences.minUniformBufferOffsetAlignment = coreReferences.physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
        physicalDeviceProperties = coreReferences.physicalDevice.getProperties();
        std::cout << "Max Compute Work Group Size: " << physicalDeviceProperties.limits.maxComputeWorkGroupSize[0] << std::endl;
    }

    uint32_t graphicsAndComputeIndex, presentIndex;
    void CreateLogicalDevice() {
        // uint32_t graphicsIndex, presentIndex; // Very likely same qFamily
        FindQueueFamilyIndices(coreReferences.physicalDevice, &graphicsAndComputeIndex, &presentIndex);
        float queuePriority = 0.5f; // [0,1] mandatory even if 1 queue
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = graphicsAndComputeIndex, // Index within physical device
            .queueCount = 1,
            .pQueuePriorities = &queuePriority }; // For creating the graphics queue
        // We can only have a few queues per queue family, and we only really need one per family.  We can create cmd buffers on multiple threads and submit all of them on main thread with low overhead

        // Modern simplification to auto chain structs with pNext
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2, // A
            vk::PhysicalDeviceVulkan11Features, // B
            vk::PhysicalDeviceVulkan13Features, // C 
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, // D
            // vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT, // ATOMIC 
            vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR
        >
            featureChain = {
                {.features = {.geometryShader = true, .samplerAnisotropy = true }}, // constructor for A,
                {.shaderDrawParameters = true}, // B
                {.synchronization2 = true, .dynamicRendering = true}, // constructor for C, dynamic rendering is a modern simplification
                {.extendedDynamicState = true},
                // {.shaderBufferFloat32AtomicAdd = true}, // ATOMIC 
                {.computeDerivativeGroupQuads = true}
        };

        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(), // The pointer to the first in the chain
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(desiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = desiredDeviceExtensions.data() // Device specific extensions
        }; // Device specific validation layers obsolete so not here

        coreReferences.device = Device(coreReferences.physicalDevice, deviceCreateInfo);
        coreReferences.graphicsQueue = Queue(coreReferences.device, graphicsAndComputeIndex, 0); // Get queue from our new device, 0 is the queue index within family, only 1 queue so we put 0
        coreReferences.computeQueue = Queue(coreReferences.device, graphicsAndComputeIndex, 0);
        presentQueue = Queue(coreReferences.device, presentIndex, 0);
        cout << "Graphics & Compute Index: " << graphicsAndComputeIndex << endl;
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
        vector<vk::SurfaceFormatKHR> availableFormats = coreReferences.physicalDevice.getSurfaceFormatsKHR(surface);
        vector<vk::PresentModeKHR> availablePresentModes = coreReferences.physicalDevice.getSurfacePresentModesKHR(surface);
        auto surfaceCapabilities = coreReferences.physicalDevice.getSurfaceCapabilitiesKHR(surface);

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

        array<uint32_t, 2> queueFamilyIndices = { graphicsAndComputeIndex, presentIndex };
        if (graphicsAndComputeIndex != presentIndex) {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent; // Queues don't hog images when they need to share to drawing and presenting
            swapChainCreateInfo.queueFamilyIndexCount = 2;
            swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data(); // The family that owns the images
        }
        else {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
            swapChainCreateInfo.queueFamilyIndexCount = 0;
            swapChainCreateInfo.pQueueFamilyIndices = nullptr;
        }

        swapChain = SwapchainKHR(coreReferences.device, swapChainCreateInfo);
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
            swapChainImageViews.emplace_back(coreReferences.device, imageViewCreateInfo); // constructs ImageView then pushes
        }

    }

    void CreateCommandPool() {
        vk::CommandPoolCreateInfo poolInfo = {
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, // cmd buffers in pool can be rerecorded individually instead of together
            .queueFamilyIndex = graphicsAndComputeIndex // all these cmd buffers are only for graphics and compute
        };
        coreReferences.commandPool = vk::raii::CommandPool(coreReferences.device, poolInfo);

    }

    void CreateCommandBuffers() {
        vk::CommandBufferAllocateInfo allocateInfo = {
            .commandPool = coreReferences.commandPool,
            .level = vk::CommandBufferLevel::ePrimary, // submitted to queue directly, not used by other cmd bufs, secondary helpful for reusing command ops from primary
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT // command makes multiple, we want one
        };
        commandBuffers = vk::raii::CommandBuffers(coreReferences.device, allocateInfo);
    }

    void RecordCommandBuffer(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex, uint32_t frameIndex) {
        // if cmd buffer already was recorded, beginning will reset it.  we can't append once we record a cmd buffer, only reset
        commandBuffer.begin({}); // could put flags in here

        // Transition to COLOR ATTACHMENT OPTIMAL
        WTexture::TransitionImageLayout(
            commandBuffer,
            swapChainImages[imageIndex],
            vk::ImageLayout::eUndefined, // From any?
            vk::ImageLayout::eColorAttachmentOptimal, // To this format
            {}, // What access to wait for?  We don't wanna wait for anything
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor, 1);

        // Transition the depth image to its optimal (from whatever it was we dont care)
        WTexture::TransitionImageLayout(
            commandBuffer,
            *depthTexture.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth, 1
        );

        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = swapChainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear, // what to do before rendering?
            .storeOp = vk::AttachmentStoreOp::eStore, // what to do after rendering?
            .clearValue = clearColor
        };

        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = depthTexture.view,
            .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare, // no need to keep depth
            .clearValue = clearDepth
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0,0}, .extent = swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo, // Which color attachments we rendering to?

            .pDepthAttachment = &depthAttachmentInfo,
        };

        // START RENDER
        // All record cmds return void so no error handling til we finished recording
        commandBuffer.beginRendering(renderingInfo);

        // For getting prim index
        commandBuffer.setPrimitiveRestartEnable(true);

        // remember in the pipeline we specified viewport and scissor state as dynamic, so we gotta specify them now
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

        // Skybox
        skyboxShader.Bind(commandBuffer);
        skyboxShader.BindMaterialDescriptorSets(coreReferences, commandBuffer, skyboxMaterial, frameIndex); 
        commandBuffer.bindVertexBuffers(0, *(cubeMesh.vertexBuffer.buffer), { 0 });
        commandBuffer.bindIndexBuffer(*cubeMesh.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        commandBuffer.drawIndexed(cubeMesh.indexCount, 1, 0, 0, 0);

        // Bind Pipeline & Material
        shaderPipeline.Bind(commandBuffer);
        shaderPipeline.BindMaterialDescriptorSets(coreReferences, commandBuffer, testMaterial, frameIndex, { 0 });
        
        // Bind Mesh
        commandBuffer.bindVertexBuffers(0, *(testRoom.vertexBuffer.buffer), { 0 }); // Bind buffer to our binding which has layout and stride stuff {0} is array of vertex buffers to bind
        commandBuffer.bindIndexBuffer(*(testRoom.indexBuffer.buffer), 0, vk::IndexType::eUint32);
        
        // IndexCount, InstanceCount, IndexBufferOffset, VertexBufferOffset, InstanceOffset
        commandBuffer.drawIndexed(testRoom.indexCount, 1, 0, 0, 0);

        // Draw orb
        // shaderPipeline.Bind(commandBuffer);
        shaderPipeline.BindMaterialDescriptorSets(coreReferences, commandBuffer, blobMaterial, frameIndex, { 1 });
        commandBuffer.bindVertexBuffers(0, *(blobMesh.vertexBuffer.buffer), { 0 });
        commandBuffer.bindIndexBuffer(*(blobMesh.indexBuffer.buffer), 0, vk::IndexType::eUint32);
        commandBuffer.drawIndexed(blobMesh.indexCount, 1, 0, 0, 0);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffer);

        commandBuffer.endRendering();
        // END RENDER

        // Have to transition image layout to presentable format
        WTexture::TransitionImageLayout(
            commandBuffer,
            swapChainImages[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, // We wanna wait for writing ops
            {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::ImageAspectFlagBits::eColor, 1);

        commandBuffer.end();

    }

    void CreateSyncObjects() {
        assert(
            presentCompleteSemaphores.empty() &&
            renderFinishedSemaphores.empty() &&
            drawFences.empty()
        );

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            renderFinishedSemaphores.emplace_back(coreReferences.device, vk::SemaphoreCreateInfo());
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            presentCompleteSemaphores.emplace_back(coreReferences.device, vk::SemaphoreCreateInfo());
            drawFences.emplace_back(coreReferences.device, vk::FenceCreateInfo{
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

        coreReferences.device.waitIdle();

        CleanupSwapChain();

        CreateSwapchain();
        CreateImageViews();

        CreateDepthResources();
    }

    void CreateUniformBuffers() {
        uniformBuffers.clear(); // In case this is used as 'recreate'
        uEntityBuffers.clear();

        // No staging buffer cuz we're updating this like every frame
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            uniformBuffers.emplace_back();
            uniformBuffers.back().Create(coreReferences, sizeof(UniformBufferObject), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            uniformBuffers.back().MapMemory();
            // Persistent mapping

            uEntityBuffers.emplace_back();
            uEntityBuffers.back().Create(coreReferences, 2 * ceilToNearest(sizeof(UEntity), coreReferences.minUniformBufferOffsetAlignment), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            uEntityBuffers.back().MapMemory();
        }
    }

    // Need to create a pool for creating descriptor sets
    void CreateDescriptorPool() {
        // Inadequate descriptor pools may not be caught by validation layers
        std::array poolSize = {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 100 * MAX_FRAMES_IN_FLIGHT),
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 25 * MAX_FRAMES_IN_FLIGHT),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 100 * MAX_FRAMES_IN_FLIGHT),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 5 * MAX_FRAMES_IN_FLIGHT)
        };

        // Pool Sizes denotes how many of each specific descriptor type we can allocate
        // Max Sets denotes how many descriptor sets total we can store
        // We make 1 big pool and use it to allocate for now

        vk::DescriptorPoolCreateInfo poolInfo = {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = MAX_FRAMES_IN_FLIGHT * 100,

            .poolSizeCount = poolSize.size(),
            .pPoolSizes = poolSize.data()
        };
        coreReferences.descriptorPool = DescriptorPool(coreReferences.device, poolInfo);
    }

    vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
        for (const auto format : candidates) {
            vk::FormatProperties props = coreReferences.physicalDevice.getFormatProperties(format);

            if (
                (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) ||
                (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
                ) {
                return format;
            }
        }

        throw std::runtime_error("failed to find format");
    }

    // These formats must have stencil component
    bool HasStencilComponent(vk::Format format) {
        return format == vk::Format::eD32SfloatS8Uint
            || format == vk::Format::eD24UnormS8Uint;
    }

    vk::Format GetDepthFormat() {
        return FindSupportedFormat(
            { vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint },

            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment
        );
    }

    void CreateDepthResources() {
        vk::Format depthFormat = GetDepthFormat();
        depthTexture.Create(coreReferences,
            swapChainExtent.width, swapChainExtent.height,
            depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::ImageAspectFlagBits::eDepth);
    }

    void testCompute() {
        uint32_t size = 551;
        vk::DeviceSize byteSize = sizeof(int) * size;
        vector<int> inData(size);
        for (int i = 0; i < size; i++) {
            inData[i] = i;
        }

        struct ComputeUniformData {
            alignas(16) uvec3 invocationCount;
        };
        ComputeUniformData uniformData = {
            .invocationCount = uvec3(size, 1, 1)
        };

        vector<WBuffer> uBuffer(1);
        WBuffer inBuffer;
        WBuffer outBuffer;

        uBuffer[0].Create(coreReferences, sizeof(ComputeUniformData), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
        uBuffer[0].MapMemory();
        memcpy(uBuffer[0].mappedMemory, &uniformData, sizeof(ComputeUniformData));
        uBuffer[0].UnmapMemory(); // One-time test

        inBuffer.CreateDeviceLocalFromData(coreReferences, byteSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, inData.data());
        outBuffer.Create(coreReferences, byteSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eDeviceLocal);

        vector shaderParams = {
            ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eCompute },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
        };

        vector materialParams = {
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &uBuffer}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &inBuffer}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &outBuffer}),
        };

        ComputePipeline computeShader;
        computeShader.Create(coreReferences, "shaders/test-compute.spv", shaderParams, materialParams, uvec3(256, 1, 1));

        ComputeDispatcher dispatcher;
        dispatcher.Create(coreReferences);

        dispatcher.StartRecord(coreReferences);
        computeShader.EnqueueDispatch(&dispatcher, uvec3(size, 1, 1));
        dispatcher.FinishRecordSubmit(coreReferences, true);

        // Test retrieve
        WBuffer receiveBuffer;
        receiveBuffer.Create(coreReferences, byteSize, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        receiveBuffer.CopyFrom(coreReferences, outBuffer, byteSize);

        /*int* data = (int*) receiveBuffer.MapMemory();
        for (int i = 0; i < size; i++) {
            std::cout << data[i] << std::endl;
        }*/

        // receiveBuffer.UnmapMemory();

    }

    void testGI() {
        giManager = mkU<GIManager>(&coreReferences);

        giManager->Test(&testCubeMap);
    }

    WTexture writtenToCubemap;
    void writeToCubemap() {

        UpdateUniformBuffer(0); // jank
        
        writtenToCubemap.CreateCubeMapFromFiles(coreReferences, {
            "textures/envmaps/storforsen/posx.jpg",
            "textures/envmaps/storforsen/negx.jpg",
            "textures/envmaps/storforsen/posy.jpg",
            "textures/envmaps/storforsen/negy.jpg",
            "textures/envmaps/storforsen/posz.jpg",
            "textures/envmaps/storforsen/negz.jpg"
            }, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal, 
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment, 
            vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eColorAttachmentOptimal); // drawing transitions to color attachment everytime right now anyways, later can optimize using a render graph typ eof thing.

        Material deferredMaterial;
        vector materialParams = {
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &uniformBuffers}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testTexture}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &metallic}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &roughness}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &ao}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &chairMesh.vertexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &chairMesh.indexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testCubeMap}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &giManager->shCoefficients})
        };
        deferredMaterial.Create(&shaderPipeline, coreReferences, materialParams);

        vk::Format depthFormat = GetDepthFormat();
        WTexture tempDepthTexture;
        tempDepthTexture.Create(coreReferences,
            writtenToCubemap.width, writtenToCubemap.height,
            depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::ImageAspectFlagBits::eDepth);

        // main shouldn't own it, i think the texture should own every possible image view or smth TODO, shouldn't be too many (but maybe too many when mips introduced, could just
        // pass entire image view to rendertarget and the rendertarget owns it, could use uptr std move
        ImageView colorView = writtenToCubemap.CreateImageView(coreReferences, 2);

        RenderTarget target;
        target.CreateFromTexture(&writtenToCubemap, &colorView, &tempDepthTexture, &tempDepthTexture.view);

        WRenderPass pass;
        pass.Create(coreReferences);

        CommandBuffer cmd = BeginOneTimeCommands(coreReferences);
        pass.Start(&target, &cmd);

        pass.EnqueueSetMaterial(deferredMaterial);
        pass.EnqueueDraw(chairMesh);
        
        pass.FinishExecute(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    void DebugUI() {
        // ImGui::ShowDemoWindow();
    }

    vector<WRenderPass> renderPasses;
    vector<RenderTarget> renderTargets;
    void CreateRenderPassesAndTargets() {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            renderPasses.emplace_back();
            renderPasses[i].Create(coreReferences);

            renderTargets.emplace_back();
            renderTargets[i].CreateFromTexture()
        }
    }

    WBuffer* skySh;
    uPtr<ProbeVolume> envSh;
    ProbeCreator pc;
    void InitVulkan() {
        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();

        CreateSwapchain();
        CreateImageViews();

        CreateCommandPool();
        CreateCommandBuffers();

        CreateSyncObjects();
        CreateUniformBuffers();

        CreateDepthResources();
        CreateDescriptorPool();

        CreateRenderPassesAndTargets();

        GUIManager::Initialize(
            window, *instance, 
            *coreReferences.physicalDevice, *coreReferences.device, 
            graphicsAndComputeIndex, *coreReferences.graphicsQueue, 
            *coreReferences.descriptorPool, MAX_FRAMES_IN_FLIGHT,
            static_cast<VkFormat>(swapSurfaceFormat.format), static_cast<VkFormat>(GetDepthFormat())
        );
        GUIManager::RegisterUIFunction(std::bind(&Application::DebugUI, this));
        GUIManager::RegisterUIFunction(std::bind(&TestGame::DrawUI, &game));

        // Other stuff
        blobMesh.CreateFromFile(coreReferences, "models/blob.obj", true);
        chairMesh.CreateFromFile(coreReferences, "models/morrisChair.obj", true);
        cubeMesh.CreateFromFile(coreReferences, "models/cube.obj");
        testRoom.CreateFromFile(coreReferences, "models/testRoom.obj", true);
        std::cout << "Test Room Index Count: " << testRoom.indexCount << std::endl;

        whiteTexture.CreateFromFile(coreReferences, "textures/white.png", vk::Format::eR8G8B8A8Srgb);
        testTexture.CreateFromFile(coreReferences, "textures/chair/morrisChair_bigChairMat_BaseColor.tga.png", vk::Format::eR8G8B8A8Srgb);
        metallic.CreateFromFile(coreReferences, "textures/chair/morrisChair_bigChairMat_Metallic.tga.png", vk::Format::eR8G8B8A8Srgb);
        roughness.CreateFromFile(coreReferences, "textures/chair/morrisChair_bigChairMat_Roughness.tga.png", vk::Format::eR8G8B8A8Srgb);
        ao.CreateFromFile(coreReferences, "textures/chair/morrisChair_bigChairMat_BaseColor.tga.png", vk::Format::eR8G8B8A8Srgb);
        testCubeMap.CreateCubeMapFromFiles(coreReferences, {
            "textures/envmaps/storforsen/posx.jpg",
            "textures/envmaps/storforsen/negx.jpg",
            "textures/envmaps/storforsen/posy.jpg",
            "textures/envmaps/storforsen/negy.jpg",
            "textures/envmaps/storforsen/posz.jpg",
            "textures/envmaps/storforsen/negz.jpg"
            /*"textures/envmaps/stormydays/stormydays_lf.tga",
            "textures/envmaps/stormydays/stormydays_rt.tga",
            "textures/envmaps/stormydays/stormydays_up.tga",
            "textures/envmaps/stormydays/stormydays_dn.tga",
            "textures/envmaps/stormydays/stormydays_ft.tga",
            "textures/envmaps/stormydays/stormydays_bk.tga"*/
            }, vk::Format::eR8G8B8A8Srgb);
        /*CreateTextureImage(testTexture, "textures/chair/morrisChair_bigChairMat_BaseColor.tga.png");
        CreateTextureImage(metallic, "textures/chair/morrisChair_bigChairMat_Metallic.tga.png");
        CreateTextureImage(roughness, "textures/chair/morrisChair_bigChairMat_Roughness.tga.png");
        CreateTextureImage(ao, "textures/chair/morrisChair_bigChairMat_BaseColor.tga.png");*/
        testRoomTexture.CreateFromFile(coreReferences, "textures/testGiPicture.png", vk::Format::eR8G8B8A8Srgb);

        vector skyboxShaderParams = {
            ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eAllGraphics },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
        };
        vector skyboxMaterialParams = {
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &uniformBuffers}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testCubeMap}),
        };
        skyboxShader.Create(coreReferences, "shaders/skybox.spv", &swapSurfaceFormat.format, GetDepthFormat(), skyboxShaderParams, false, true);
        skyboxMaterial.Create(&skyboxShader, coreReferences, skyboxMaterialParams);

        vector shaderParams = {
            ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eAllGraphics },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::DYNAMIC_UNIFORM, .visibility = vk::ShaderStageFlagBits::eAllGraphics },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eFragment },

        };
        shaderPipeline.Create(coreReferences, "shaders/pbr-test.spv", &swapSurfaceFormat.format, GetDepthFormat(), shaderParams);
        // testGI();
        UpdateUniformBuffer(0);
        pc.Create(&coreReferences, &testCubeMap, &uniformBuffers, &testCubeMap, &testRoom, &testRoomTexture, &metallic, &roughness, &ao);
        skySh = pc.BakeAndSetSkyboxProbe();
        envSh = std::move(pc.BakeEnvironmentProbes(uvec3(80, 40, 80), vec3(0), vec3(15.5,9,15.5)));
        //writeToCubemap();
        vector materialParams = {
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &uniformBuffers}),
            ShaderParameter::MParameter(ShaderParameter::UDynamicUniform {
                .buffers = &uEntityBuffers,
                .singleObjectSize =
                static_cast<vk::DeviceSize>(
                    ceilToNearest(
                        sizeof(UEntity), 
                        coreReferences.minUniformBufferOffsetAlignment
                    )
                )
            }),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testRoomTexture}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &metallic}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &roughness}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &ao}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom.vertexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom.indexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testCubeMap}),// &writtenCubemap }),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &envSh->shCoefficients}),
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &envSh->probeLayoutUBO}),
        };
        testMaterial.Create(&shaderPipeline, coreReferences, materialParams);
        vector blobMaterialParams = {
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &uniformBuffers}),
            ShaderParameter::MParameter(ShaderParameter::UDynamicUniform {
                .buffers = &uEntityBuffers,
                .singleObjectSize =
                static_cast<vk::DeviceSize>(
                    ceilToNearest(
                        sizeof(UEntity), 
                        coreReferences.minUniformBufferOffsetAlignment
                    )
                )
            }),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &whiteTexture}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &metallic}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &roughness}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &ao}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom.vertexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom.indexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testCubeMap}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &envSh->shCoefficients}),
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &envSh->probeLayoutUBO}),
        };
        blobMaterial.Create(&shaderPipeline, coreReferences, blobMaterialParams);
        vector reflectShaderParams = {
            ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eAllGraphics },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eFragment },
            ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eFragment },
        };
        vector reflectMaterialParams = {
            ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &uniformBuffers}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testCubeMap}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom.vertexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom.indexBuffer}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &testRoomTexture}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &metallic}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &roughness}),
            ShaderParameter::MParameter(ShaderParameter::USampler {.texture = &ao}),
            ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = skySh }) // TODO: modify samplepbr to interpolate between sh samples
        };
        reflectShader.Create(coreReferences, "shaders/reflect.spv", &swapSurfaceFormat.format, GetDepthFormat(), reflectShaderParams);
        reflectMaterial.Create(&reflectShader, coreReferences, reflectMaterialParams);

        // testCompute();
        
    }

    Camera camera = Camera(vec3(0), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), glm::radians(45.0f));
    InputManager inputManager = InputManager(uvec2(WIDTH, HEIGHT));

    float time;
    float dt;
    void UpdateTime() {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float prevTime = time;
        time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
        dt = time - prevTime;
    }

    void UpdateUniformBuffer(uint32_t currFrame) {
        vk::DeviceSize alignment = ceilToNearest(sizeof(UEntity), coreReferences.minUniformBufferOffsetAlignment);
        vector<UEntity> entities = {
            {.transform = game.roomTransform},
            {.transform = game.ballTransform}
        };
        for (int i = 0; i < entities.size(); i++) {
            memcpy(static_cast<char*>(uEntityBuffers[currFrame].mappedMemory) + i * alignment, &entities[i], sizeof(UEntity));
        }

        UniformBufferObject ubo = {
            .off = time,
            .model = glm::scale(mat4(1.0f), vec3(0.014f+0.5)) * glm::rotate(mat4(1.0f), 0*time, vec3(0.0f, 1.0f, 0.0f)),
            .view = camera.GetViewMatrix(),//glm::lookAt(mat3(glm::rotate(mat4(1.0f), time, vec3(0,1,0))) * vec3(0,2,5), vec3(0), vec3(0,1,0)),
            .proj = camera.GetProjectionMatrix(),//glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 1000.0f), // TODO: increase far
        };
        /*ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f) * 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = lookAt(glm::vec3(4.0f * cos(time), 1.2f, 4.0f * sin(time)), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 10.0f);
        */// glm::perspective outputs flipped y clip space, compensate
        ubo.proj[1][1] *= -1.0f;

        // TODO: how are we sure that the uniform buffer does get written to after this?  i think vulkan actually ensures the cpu data is same when mapped?
        memcpy(uniformBuffers[currFrame].mappedMemory, &ubo, sizeof(ubo));
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
        auto& renderPass = renderPasses[currFrameIndex];

        // Wait until prev frame finished
        // Takes in array of fences, true indicates we want to wait for all of em, UINT64_MAX is the timeout, effectively disabled
        /*auto fenceResult = coreReferences.device.waitForFences(*drawFence, vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw new std::runtime_error("Failed to wait for fence");
        }*/
        renderPass.WaitForFinish();

        UpdateUniformBuffer(currFrameIndex);

        // Grab img from framebuffer now that prev frame done
        // presentCompleteSemaphore is signaled when image is finished being used
        auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphore, nullptr); // Signal presentCompleteSemaphore
        if (result == vk::Result::eErrorOutOfDateKHR) { // frameBufferResized just in case app doesn't automatically do outOfDate
            RecreateSwapChain();
            return;
        }
        else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) { // suboptimal still ok
            throw new std::runtime_error("Failed to acquire swap chain img");
        }

        auto& renderFinishedSemaphore = renderFinishedSemaphores[imageIndex]; // This semaphore is per image because if it was per frame, a different frame in flight could be using a different semaphore on an image that hasn't finished being rendered to, since the semaphore would be different, it'd go through and we'd overwrite the image being drawn to

        renderPass.Start()

        // We know we're not returning early, so put the fence back up so it'll be signaled on draw
        coreReferences.device.resetFences(*drawFence); // Put fence back up


        // Record drawing cmds
        commandBuffer.reset();
        RecordCommandBuffer(commandBuffer, imageIndex, currFrameIndex);

        vk::PipelineStageFlags waitDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*presentCompleteSemaphore, // Wait for this semaphore to signal
            .pWaitDstStageMask = &waitDstStageMask, // Write colors

            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffer, // Submit this cmd buffer

            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSemaphore // Signal once done
        };

        coreReferences.graphicsQueue.submit(submitInfo, *drawFence); // Fence will be put down when done

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

            UpdateTime();
            GUIManager::MainLoop();
            inputManager.Update(window);
            camera.Update(inputManager, dt);
            game.Update(time, dt);

            DrawFrame();
        }

        // When we exit, wait for lingering commands to finish
        coreReferences.device.waitIdle();
    }

    void Cleanup() {
        GUIManager::Shutdown(coreReferences);

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