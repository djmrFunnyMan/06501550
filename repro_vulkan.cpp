#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t WINDOW_WIDTH = 1200;
constexpr uint32_t WINDOW_HEIGHT = 900;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const char* kTitle =
    "Case 06501550 repro | 1 = Left: perlinA (float 23.53) | "
    "2 = Right: perlinB (uniform 23.53)";

// Vulkan uses SPIR-V rather than OpenGL's runtime GLSL compilation.
// We keep the GLSL in the source and compile it to SPIR-V with shaderc at startup.
static const char* kVertexShader = R"glsl(
#version 450

layout(location = 0) out vec2 uv;

const vec2 verts[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 p = verts[gl_VertexIndex];
    uv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

static const char* kFragmentShader = R"glsl(
#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants {
    float value_U;
} pc;

const float pi = 3.14159265358979323846;
const float value = 23.53;

float value_F = value;

float sinM(float x)
{
    return sin(mod(x, 2.0 * pi));
}

float randA(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(value_F, 44.0))) * 42350.45);
}

float randB(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(pc.value_U, 44.0))) * 42350.45);
}

float perlinA(vec2 inCoord)
{
    vec2 i = floor(inCoord);
    vec2 j = fract(inCoord);
    vec2 coord = smoothstep(0.0, 1.0, j);

    float a = randA(i);
    float b = randA(i + vec2(1.0, 0.0));
    float c = randA(i + vec2(0.0, 1.0));
    float d = randA(i + vec2(1.0, 1.0));

    return mix(mix(a, b, coord.x), mix(c, d, coord.x), coord.y);
}

float perlinB(vec2 inCoord)
{
    vec2 i = floor(inCoord);
    vec2 j = fract(inCoord);
    vec2 coord = smoothstep(0.0, 1.0, j);

    float a = randB(i);
    float b = randB(i + vec2(1.0, 0.0));
    float c = randB(i + vec2(0.0, 1.0));
    float d = randB(i + vec2(1.0, 1.0));

    return mix(mix(a, b, coord.x), mix(c, d, coord.x), coord.y);
}

void main()
{
    vec2 gridUV = uv;

    if (uv.x < 0.5)
        gridUV.x = uv.x * 2.0;
    else
        gridUV.x = (uv.x - 0.5) * 2.0;

    vec2 inCoord = gridUV * 16.0;

    float v = (uv.x < 0.5) ? perlinA(inCoord) : perlinB(inCoord);

    vec3 color = vec3(v);

    float divider = 1.0 - smoothstep(0.0, 0.002, abs(uv.x - 0.5));
    color = mix(color, vec3(1.0), divider);

    FragColor = vec4(color, 1.0);
}
)glsl";

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool complete() const
    {
        return graphics.has_value() && present.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;
    size_t currentFrame = 0;
};

VkResult checkVk(VkResult result, const char* what)
{
    if (result != VK_SUCCESS)
        std::cerr << what << " failed with VkResult " << result << "\n";
    return result;
}

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphics = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport)
            indices.present = i;

        if (indices.complete())
            break;
    }

    return indices;
}

bool hasDeviceExtension(VkPhysicalDevice device, const char* extensionName)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());

    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, extensionName) == 0)
            return true;
    }
    return false;
}

bool suitableDevice(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    const QueueFamilyIndices indices = findQueueFamilies(device, surface);
    if (!indices.complete())
        return false;

    if (!hasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        return false;

    uint32_t formatCount = 0;
    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount, nullptr);
    return formatCount != 0 && presentCount != 0;
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount, nullptr);
    if (presentCount != 0) {
        details.presentModes.resize(presentCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes)
{
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D extent{
        static_cast<uint32_t>(std::max(width, 0)),
        static_cast<uint32_t>(std::max(height, 0))
    };

    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

std::vector<uint32_t> compileShader(const char* source, shaderc_shader_kind kind, const char* name)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

    const shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(source, kind, name, options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        fail(std::string("Shader compilation failed for ") + name + ":\n" + result.GetErrorMessage());
    }

    return {result.cbegin(), result.cend()};
}

VkShaderModule makeShaderModule(VkDevice device, const std::vector<uint32_t>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (checkVk(vkCreateShaderModule(device, &createInfo, nullptr, &module), "vkCreateShaderModule") != VK_SUCCESS)
        fail("Could not create shader module");
    return module;
}

void createInstance(VulkanContext& ctx)
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions)
        fail("GLFW did not report Vulkan instance extensions");

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = kTitle;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    if (checkVk(vkCreateInstance(&createInfo, nullptr, &ctx.instance), "vkCreateInstance") != VK_SUCCESS)
        fail("Could not create Vulkan instance");
}

void createSurface(VulkanContext& ctx, GLFWwindow* window)
{
    if (glfwCreateWindowSurface(ctx.instance, window, nullptr, &ctx.surface) != VK_SUCCESS)
        fail("glfwCreateWindowSurface failed");
}

void pickPhysicalDevice(VulkanContext& ctx)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        fail("No Vulkan-capable physical devices found");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());

    for (VkPhysicalDevice device : devices) {
        if (suitableDevice(device, ctx.surface)) {
            ctx.physicalDevice = device;
            break;
        }
    }

    if (ctx.physicalDevice == VK_NULL_HANDLE)
        fail("No suitable Vulkan device found");

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &properties);
    std::cout << "Device   : " << properties.deviceName << "\n";
    std::cout << "Vulkan   : " << VK_VERSION_MAJOR(properties.apiVersion) << "."
              << VK_VERSION_MINOR(properties.apiVersion) << "."
              << VK_VERSION_PATCH(properties.apiVersion) << "\n";
}

void createLogicalDevice(VulkanContext& ctx)
{
    const QueueFamilyIndices indices = findQueueFamilies(ctx.physicalDevice, ctx.surface);

    std::set<uint32_t> uniqueFamilies = {*indices.graphics, *indices.present};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    const float priority = 1.0f;

    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures features{};

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    if (checkVk(vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device), "vkCreateDevice") != VK_SUCCESS)
        fail("Could not create logical device");

    vkGetDeviceQueue(ctx.device, *indices.graphics, 0, &ctx.graphicsQueue);
    vkGetDeviceQueue(ctx.device, *indices.present, 0, &ctx.presentQueue);
}

void createSwapchain(VulkanContext& ctx, GLFWwindow* window)
{
    const SwapChainSupportDetails support = querySwapChainSupport(ctx.physicalDevice, ctx.surface);
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    const VkExtent2D extent = chooseExtent(support.capabilities, window);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0)
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);

    const QueueFamilyIndices indices = findQueueFamilies(ctx.physicalDevice, ctx.surface);
    const uint32_t queueFamilyIndices[] = {*indices.graphics, *indices.present};

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = ctx.surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (*indices.graphics != *indices.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (checkVk(vkCreateSwapchainKHR(ctx.device, &createInfo, nullptr, &ctx.swapchain), "vkCreateSwapchainKHR") != VK_SUCCESS)
        fail("Could not create swapchain");

    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imageCount, nullptr);
    ctx.swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imageCount, ctx.swapchainImages.data());
    ctx.swapchainFormat = surfaceFormat.format;
    ctx.swapchainExtent = extent;

    ctx.swapchainImageViews.resize(ctx.swapchainImages.size());
    for (size_t i = 0; i < ctx.swapchainImages.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = ctx.swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = ctx.swapchainFormat;
        viewInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (checkVk(vkCreateImageView(ctx.device, &viewInfo, nullptr, &ctx.swapchainImageViews[i]), "vkCreateImageView") != VK_SUCCESS)
            fail("Could not create swapchain image view");
    }
}

void createRenderPass(VulkanContext& ctx)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = ctx.swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (checkVk(vkCreateRenderPass(ctx.device, &renderPassInfo, nullptr, &ctx.renderPass), "vkCreateRenderPass") != VK_SUCCESS)
        fail("Could not create render pass");
}

void createGraphicsPipeline(VulkanContext& ctx)
{
    const auto vertexSpv = compileShader(kVertexShader, shaderc_glsl_vertex_shader, "fullscreen_triangle.vert");
    const auto fragmentSpv = compileShader(kFragmentShader, shaderc_glsl_fragment_shader, "perlin.frag");

    VkShaderModule vertexModule = makeShaderModule(ctx.device, vertexSpv);
    VkShaderModule fragmentModule = makeShaderModule(ctx.device, fragmentSpv);

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexModule;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentModule;
    fragmentStage.pName = "main";

    const VkPipelineShaderStageCreateInfo stages[] = {vertexStage, fragmentStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const VkPushConstantRange pushConstantRange{
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(float)
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (checkVk(vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &ctx.pipelineLayout), "vkCreatePipelineLayout") != VK_SUCCESS)
        fail("Could not create pipeline layout");

    VkPipelineDynamicStateCreateInfo dynamicState{};
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = ctx.pipelineLayout;
    pipelineInfo.renderPass = ctx.renderPass;
    pipelineInfo.subpass = 0;

    if (checkVk(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &ctx.graphicsPipeline),
                "vkCreateGraphicsPipelines") != VK_SUCCESS)
        fail("Could not create graphics pipeline");

    vkDestroyShaderModule(ctx.device, fragmentModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertexModule, nullptr);
}

void createFramebuffers(VulkanContext& ctx)
{
    ctx.framebuffers.resize(ctx.swapchainImageViews.size());

    for (size_t i = 0; i < ctx.swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {ctx.swapchainImageViews[i]};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = ctx.renderPass;
        info.attachmentCount = 1;
        info.pAttachments = attachments;
        info.width = ctx.swapchainExtent.width;
        info.height = ctx.swapchainExtent.height;
        info.layers = 1;

        if (checkVk(vkCreateFramebuffer(ctx.device, &info, nullptr, &ctx.framebuffers[i]), "vkCreateFramebuffer") != VK_SUCCESS)
            fail("Could not create framebuffer");
    }
}

void createCommandPool(VulkanContext& ctx)
{
    const QueueFamilyIndices indices = findQueueFamilies(ctx.physicalDevice, ctx.surface);

    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = *indices.graphics;

    if (checkVk(vkCreateCommandPool(ctx.device, &info, nullptr, &ctx.commandPool), "vkCreateCommandPool") != VK_SUCCESS)
        fail("Could not create command pool");
}

void createCommandBuffers(VulkanContext& ctx)
{
    ctx.commandBuffers.resize(ctx.framebuffers.size());

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = ctx.commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<uint32_t>(ctx.commandBuffers.size());

    if (checkVk(vkAllocateCommandBuffers(ctx.device, &alloc, ctx.commandBuffers.data()), "vkAllocateCommandBuffers") != VK_SUCCESS)
        fail("Could not allocate command buffers");
}

void recordCommandBuffer(VulkanContext& ctx, uint32_t imageIndex)
{
    VkCommandBuffer cmd = ctx.commandBuffers[imageIndex];

    if (checkVk(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer") != VK_SUCCESS)
        fail("Could not reset command buffer");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (checkVk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer") != VK_SUCCESS)
        fail("Could not begin command buffer");

    VkClearValue clearValue{};
    clearValue.color = {{0.02f, 0.02f, 0.02f, 1.0f}};

    VkRenderPassBeginInfo renderPassBegin{};
    renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBegin.renderPass = ctx.renderPass;
    renderPassBegin.framebuffer = ctx.framebuffers[imageIndex];
    renderPassBegin.renderArea.offset = {0, 0};
    renderPassBegin.renderArea.extent = ctx.swapchainExtent;
    renderPassBegin.clearValueCount = 1;
    renderPassBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(ctx.swapchainExtent.width);
    viewport.height = static_cast<float>(ctx.swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = ctx.swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Equivalent to the original OpenGL "uniform float value_U = value".
    // Vulkan push constants are updated explicitly before the draw.
    const float valueU = 23.53f;
    vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(valueU), &valueU);

    // No vertex buffer is needed: the vertex shader generates the fullscreen triangle.
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    if (checkVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer") != VK_SUCCESS)
        fail("Could not end command buffer");
}

void createSyncObjects(VulkanContext& ctx)
{
    ctx.imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    ctx.renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    ctx.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(ctx.device, &semaphoreInfo, nullptr, &ctx.imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx.device, &semaphoreInfo, nullptr, &ctx.renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(ctx.device, &fenceInfo, nullptr, &ctx.inFlightFences[i]) != VK_SUCCESS) {
            fail("Could not create synchronization objects");
        }
    }

    ctx.imagesInFlight.assign(ctx.swapchainImages.size(), VK_NULL_HANDLE);
}

void destroySwapchainResources(VulkanContext& ctx)
{
    for (VkFramebuffer framebuffer : ctx.framebuffers)
        vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    ctx.framebuffers.clear();

    if (ctx.graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.device, ctx.graphicsPipeline, nullptr);
        ctx.graphicsPipeline = VK_NULL_HANDLE;
    }
    if (ctx.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx.device, ctx.pipelineLayout, nullptr);
        ctx.pipelineLayout = VK_NULL_HANDLE;
    }
    if (ctx.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx.device, ctx.renderPass, nullptr);
        ctx.renderPass = VK_NULL_HANDLE;
    }

    if (!ctx.commandBuffers.empty()) {
        vkFreeCommandBuffers(ctx.device, ctx.commandPool,
                             static_cast<uint32_t>(ctx.commandBuffers.size()), ctx.commandBuffers.data());
        ctx.commandBuffers.clear();
    }

    for (VkImageView view : ctx.swapchainImageViews)
        vkDestroyImageView(ctx.device, view, nullptr);
    ctx.swapchainImageViews.clear();

    if (ctx.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr);
        ctx.swapchain = VK_NULL_HANDLE;
    }
}

void recreateSwapchain(VulkanContext& ctx, GLFWwindow* window)
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    vkDeviceWaitIdle(ctx.device);
    destroySwapchainResources(ctx);

    createSwapchain(ctx, window);
    createRenderPass(ctx);
    createGraphicsPipeline(ctx);
    createFramebuffers(ctx);
    createCommandBuffers(ctx);
    ctx.imagesInFlight.assign(ctx.swapchainImages.size(), VK_NULL_HANDLE);
}

void drawFrame(VulkanContext& ctx, GLFWwindow* window)
{
    VkFence currentFence = ctx.inFlightFences[ctx.currentFrame];
    vkWaitForFences(ctx.device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        ctx.device,
        ctx.swapchain,
        UINT64_MAX,
        ctx.imageAvailableSemaphores[ctx.currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(ctx, window);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        fail("vkAcquireNextImageKHR failed");

    if (ctx.imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(ctx.device, 1, &ctx.imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    ctx.imagesInFlight[imageIndex] = currentFence;

    vkResetFences(ctx.device, 1, &currentFence);
    recordCommandBuffer(ctx, imageIndex);

    VkSemaphore waitSemaphores[] = {ctx.imageAvailableSemaphores[ctx.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {ctx.renderFinishedSemaphores[ctx.currentFrame]};

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSemaphores;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &ctx.commandBuffers[imageIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSemaphores;

    if (checkVk(vkQueueSubmit(ctx.graphicsQueue, 1, &submit, currentFence), "vkQueueSubmit") != VK_SUCCESS)
        fail("Could not submit draw command buffer");

    VkSwapchainKHR swapchains[] = {ctx.swapchain};
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = signalSemaphores;
    present.swapchainCount = 1;
    present.pSwapchains = swapchains;
    present.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(ctx.presentQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        recreateSwapchain(ctx, window);
    else if (result != VK_SUCCESS)
        fail("vkQueuePresentKHR failed");

    ctx.currentFrame = (ctx.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void cleanup(VulkanContext& ctx)
{
    if (ctx.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(ctx.device);

    for (size_t i = 0; i < ctx.imageAvailableSemaphores.size(); ++i)
        vkDestroySemaphore(ctx.device, ctx.imageAvailableSemaphores[i], nullptr);
    for (size_t i = 0; i < ctx.renderFinishedSemaphores.size(); ++i)
        vkDestroySemaphore(ctx.device, ctx.renderFinishedSemaphores[i], nullptr);
    for (size_t i = 0; i < ctx.inFlightFences.size(); ++i)
        vkDestroyFence(ctx.device, ctx.inFlightFences[i], nullptr);

    ctx.imageAvailableSemaphores.clear();
    ctx.renderFinishedSemaphores.clear();
    ctx.inFlightFences.clear();

    if (ctx.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
        ctx.commandPool = VK_NULL_HANDLE;
    }

    destroySwapchainResources(ctx);

    if (ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx.device, nullptr);
        ctx.device = VK_NULL_HANDLE;
    }
    if (ctx.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
        ctx.surface = VK_NULL_HANDLE;
    }
    if (ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx.instance, nullptr);
        ctx.instance = VK_NULL_HANDLE;
    }
}

} // namespace

int main()
{
    try {
        if (!glfwInit())
            fail("glfwInit failed");

        if (!glfwVulkanSupported())
            fail("GLFW reports that Vulkan is not available");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, kTitle, nullptr, nullptr);
        if (!window)
            fail("glfwCreateWindow failed");

        VulkanContext ctx;
        createInstance(ctx);
        createSurface(ctx, window);
        pickPhysicalDevice(ctx);
        createLogicalDevice(ctx);
        createSwapchain(ctx, window);
        createRenderPass(ctx);
        createGraphicsPipeline(ctx);
        createFramebuffers(ctx);
        createCommandPool(ctx);
        createCommandBuffers(ctx);
        createSyncObjects(ctx);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);

            drawFrame(ctx, window);
        }

        cleanup(ctx);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }
}
