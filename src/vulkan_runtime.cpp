// vulkan_runtime.cpp
#include "vulkan_runtime.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "shaders_embedded.h" // provides kComputeShaderSpv and kComputeShaderSpvSize

#ifdef ENABLE_VULKAN_VALIDATION
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> instanceExtensions = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
#endif

const std::vector<const char*> deviceExtensions = {
    // Add required device extensions here if needed
};

// Small helpers
static bool pickComputeQueueFamily(VkPhysicalDevice phys, uint32_t& outFamily) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props.data());

    int fallbackIndex = -1;
    for (uint32_t i = 0; i < count; ++i) {
        // Prefer dedicated compute (has compute but not graphics)
        if ((props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            outFamily = i;
            return true;
        }
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            fallbackIndex = static_cast<int>(i);
        }
    }
    if (fallbackIndex >= 0) {
        outFamily = static_cast<uint32_t>(fallbackIndex);
        return true;
    }
    return false;
}

bool VulkanRuntime::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    for (const char* requiredExtension : deviceExtensions) {
        bool found = false;
        for (const auto& extension : availableExtensions) {
            if (strcmp(requiredExtension, extension.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

int VulkanRuntime::scorePhysicalDevice(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;

    // Score devices based on type, but don't heavily penalize iGPUs
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 800;  // iGPUs are still very capable, especially for compute
    } else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) {
        score += 600;
    } else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        score += 100;  // Software rendering fallback
    }

    // Maximum possible size of textures affects graphics quality
    score += deviceProperties.limits.maxImageDimension2D;

    // Check if device supports compute queues
    uint32_t queueFamily;
    if (!pickComputeQueueFamily(device, queueFamily)) {
        return 0; // Device unusable without compute queue
    }

    // Check extension support
    if (!checkDeviceExtensionSupport(device)) {
        return 0; // Device unusable without required extensions
    }

    // Score based on memory characteristics
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(device, &memProperties);

    bool hasDeviceLocalMemory = false;
    bool hasHostVisibleDeviceLocal = false;

    for (uint32_t i = 0; i < memProperties.memoryHeapCount; i++) {
        if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            hasDeviceLocalMemory = true;
            // For iGPUs, smaller memory pools are normal and shouldn't be heavily penalized
            score += static_cast<int>(memProperties.memoryHeaps[i].size / (1024 * 1024 * 10)); // Score in 10MB units
        }
    }

    // Check for unified memory architecture (common in iGPUs)
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        VkMemoryPropertyFlags props = memProperties.memoryTypes[i].propertyFlags;
        if ((props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            hasHostVisibleDeviceLocal = true;
            score += 200; // Bonus for unified memory architecture (great for iGPUs!)
            break;
        }
    }

    // iGPUs often have unified memory which is actually advantageous for many workloads
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && hasHostVisibleDeviceLocal) {
        score += 300; // Additional bonus for iGPU with unified memory
    }

    return score;
}

void VulkanRuntime::cacheMemoryTypeIndices() {
    if (!physicalDevice) return;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    hostVisibleMemoryTypeIndex = UINT32_MAX;
    deviceLocalMemoryTypeIndex = UINT32_MAX;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        VkMemoryPropertyFlags props = memProperties.memoryTypes[i].propertyFlags;

        // Find host visible and coherent memory
        if ((props & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            if (hostVisibleMemoryTypeIndex == UINT32_MAX) {
                hostVisibleMemoryTypeIndex = i;
            }
        }

        // Find device local memory
        if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            if (deviceLocalMemoryTypeIndex == UINT32_MAX) {
                deviceLocalMemoryTypeIndex = i;
            }
        }
    }
}

static uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VulkanRuntime::allocateBuffer(VkBuffer& buffer, VkDeviceMemory& memory, size_t size,
                                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    if (!device || !physicalDevice) return false;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    uint32_t memoryType = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);
    if (memoryType == UINT32_MAX) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryType;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }

    totalAllocatedMemory += memRequirements.size;
    return true;
}

void VulkanRuntime::deallocateBuffer(VkBuffer& buffer, VkDeviceMemory& memory, size_t size) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        if (totalAllocatedMemory >= size) {
            totalAllocatedMemory -= size;
        }
    }
}

bool VulkanRuntime::createStagingBuffer(size_t size) {
    if (stagingBuffer != VK_NULL_HANDLE && stagingBufferSize >= size) {
        return true; // Already have a suitable staging buffer
    }

    destroyStagingBuffer();

    if (allocateBuffer(stagingBuffer, stagingMemory, size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        stagingBufferSize = size;
        return true;
    }
    return false;
}

void VulkanRuntime::destroyStagingBuffer() {
    deallocateBuffer(stagingBuffer, stagingMemory, stagingBufferSize);
    stagingBufferSize = 0;
}

VulkanRuntime::~VulkanRuntime() { shutdown(); }

#ifdef ENABLE_VULKAN_VALIDATION
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRuntime::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    const char* severityStr = "UNKNOWN";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severityStr = "ERROR";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severityStr = "WARNING";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severityStr = "INFO";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        severityStr = "VERBOSE";
    }

    const char* typeStr = "UNKNOWN";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        typeStr = "GENERAL";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        typeStr = "VALIDATION";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        typeStr = "PERFORMANCE";
    }

    std::cerr << "[VULKAN " << severityStr << " " << typeStr << "] " << pCallbackData->pMessage << std::endl;

    // Return VK_TRUE to abort the call that triggered the validation layer message
    return VK_FALSE;
}

bool VulkanRuntime::setupDebugMessenger() {
    if (!instance) return false;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = this;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, &createInfo, nullptr, &debugMessenger) == VK_SUCCESS;
    }
    return false;
}

void VulkanRuntime::cleanupDebugMessenger() {
    if (debugMessenger != VK_NULL_HANDLE && instance) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, nullptr);
        }
        debugMessenger = VK_NULL_HANDLE;
    }
}
#endif



bool VulkanRuntime::initialize() {
    shutdown(); // safe reset if called twice

    // 1) Instance
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "RTerminal";
    appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0);
    appInfo.pEngineName = "RTerminalEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1,0,0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &appInfo;

#ifdef ENABLE_VULKAN_VALIDATION
    // Check validation layer support
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool validationSupported = true;
    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            validationSupported = false;
            std::cerr << "Vulkan: Validation layer " << layerName << " not available" << std::endl;
            break;
        }
    }

    if (validationSupported) {
        ici.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        ici.ppEnabledLayerNames = validationLayers.data();
        ici.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        ici.ppEnabledExtensionNames = instanceExtensions.data();
        std::cout << "Vulkan: Validation layers enabled" << std::endl;
    } else {
        std::cerr << "Vulkan: Validation layers requested but not available" << std::endl;
    }
#endif

    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create instance" << std::endl;
        return false;
    }

#ifdef ENABLE_VULKAN_VALIDATION
    if (validationSupported && !setupDebugMessenger()) {
        std::cerr << "Vulkan: Failed to set up debug messenger" << std::endl;
    }
#endif

    // 2) Physical device selection with scoring
    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(instance, &physCount, nullptr);
    if (physCount == 0) {
        std::cerr << "Vulkan: No physical devices found" << std::endl;
        shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> phys(physCount);
    vkEnumeratePhysicalDevices(instance, &physCount, phys.data());

    // Score all devices and pick the best one
    physicalDevice = VK_NULL_HANDLE;
    int bestScore = 0;
    for (auto pd : phys) {
        int score = scorePhysicalDevice(pd);
        if (score > bestScore) {
            bestScore = score;
            physicalDevice = pd;
            uint32_t qf;
            if (pickComputeQueueFamily(pd, qf)) {
                computeQueueFamily = qf;
            }
        }
    }

    if (physicalDevice == VK_NULL_HANDLE || bestScore == 0) {
        std::cerr << "Vulkan: No suitable device found" << std::endl;
        shutdown();
        return false;
    }

    // Cache device properties and memory types
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        deviceName = props.deviceName ? props.deviceName : std::string("Unknown");
        vendorId = props.vendorID;
        deviceId = props.deviceID;
        apiVersion = props.apiVersion;
        driverVersion = props.driverVersion;
    }
    cacheMemoryTypeIndices();

    // Cache compute limits for workgroup optimization
    VkPhysicalDeviceProperties computeProps{};
    vkGetPhysicalDeviceProperties(physicalDevice, &computeProps);
    maxWorkGroupSizeX = computeProps.limits.maxComputeWorkGroupSize[0];
    maxWorkGroupSizeY = computeProps.limits.maxComputeWorkGroupSize[1];
    maxWorkGroupSizeZ = computeProps.limits.maxComputeWorkGroupSize[2];
    maxWorkGroupInvocations = computeProps.limits.maxComputeWorkGroupInvocations;

    // Choose optimal workgroup size based on device capabilities
    // AMD Vega architecture uses wavefronts of 64, so optimize for that
    bool isAMD = (vendorId == 0x1002); // AMD vendor ID
    uint32_t wavefrontSize = isAMD ? 64 : 32; // NVIDIA uses 32 (warps)

    if (isAMD) {
        // For AMD Vega iGPUs, 8x8 (64 threads) is often optimal as it matches wavefront size
        optimalWorkGroupSizeX = std::min(8u, maxWorkGroupSizeX);
        optimalWorkGroupSizeY = std::min(8u, maxWorkGroupSizeY);
    } else {
        // For other vendors, use 16x16 or smaller
        optimalWorkGroupSizeX = std::min(16u, maxWorkGroupSizeX);
        optimalWorkGroupSizeY = std::min(16u, maxWorkGroupSizeY);
    }

    // Ensure total invocations don't exceed limit
    while (optimalWorkGroupSizeX * optimalWorkGroupSizeY > maxWorkGroupInvocations) {
        if (optimalWorkGroupSizeX > optimalWorkGroupSizeY) {
            optimalWorkGroupSizeX /= 2;
        } else {
            optimalWorkGroupSizeY /= 2;
        }
    }

    // Ensure we have at least one full wavefront/warp
    uint32_t totalThreads = optimalWorkGroupSizeX * optimalWorkGroupSizeY;
    if (totalThreads < wavefrontSize && maxWorkGroupInvocations >= wavefrontSize) {
        // Try to get closer to wavefront size
        if (maxWorkGroupSizeX >= 8 && maxWorkGroupSizeY >= 8) {
            optimalWorkGroupSizeX = 8;
            optimalWorkGroupSizeY = 8;
        }
    }

    std::cout << "Vulkan: Optimal workgroup size: " << optimalWorkGroupSizeX << "x" << optimalWorkGroupSizeY;
    if (isAMD) {
        std::cout << " (optimized for AMD Vega wavefronts)";
    }
    std::cout << std::endl;

    // Log device type and memory info
    VkPhysicalDeviceProperties logProps{};
    vkGetPhysicalDeviceProperties(physicalDevice, &logProps);

    const char* deviceTypeStr = "Unknown";
    switch (logProps.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceTypeStr = "Integrated GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: deviceTypeStr = "Discrete GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: deviceTypeStr = "Virtual GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: deviceTypeStr = "CPU"; break;
    }

    std::cout << "Vulkan: Using " << deviceTypeStr << " - " << deviceName << std::endl;
    if (logProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        std::cout << "Vulkan: iGPU detected - optimizations enabled for shared memory architecture" << std::endl;
    }

    // Check timestamp support (but don't create query pool yet - need device first)
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    timestampValidBits = props.limits.timestampComputeAndGraphics;
    timestampPeriod = props.limits.timestampPeriod;

    // 3) Logical device & queue
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = computeQueueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    // Enable device extensions
    if (!deviceExtensions.empty()) {
        dci.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        dci.ppEnabledExtensionNames = deviceExtensions.data();
    }

#ifdef ENABLE_VULKAN_VALIDATION
    // Enable validation layers for device (deprecated but some implementations still require it)
    dci.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    dci.ppEnabledLayerNames = validationLayers.data();
#endif

    if (vkCreateDevice(physicalDevice, &dci, nullptr, &device) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create logical device" << std::endl;
        shutdown();
        return false;
    }
    vkGetDeviceQueue(device, computeQueueFamily, 0, &computeQueue);

    // Create command pool for compute queue
    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = computeQueueFamily;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create command pool" << std::endl;
        shutdown();
        return false;
    }

    // Allocate multiple command buffers for frame pipelining
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cbAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbAlloc.commandPool = commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    if (vkAllocateCommandBuffers(device, &cbAlloc, commandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to allocate command buffers" << std::endl;
        shutdown();
        return false;
    }

    // Create fences for GPU completion (signaled initially so first frame doesn't wait)
    fences.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled
        if (vkCreateFence(device, &fci, nullptr, &fences[i]) != VK_SUCCESS) {
            std::cerr << "Vulkan: Failed to create fence " << i << std::endl;
            shutdown();
            return false;
        }
    }

    // Create semaphores for compute completion signaling
    computeFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(device, &sci, nullptr, &computeFinishedSemaphores[i]) != VK_SUCCESS) {
            std::cerr << "Vulkan: Failed to create semaphore " << i << std::endl;
            shutdown();
            return false;
        }
    }

    // Create query pool for GPU timing (now that device is created)
    if (timestampValidBits) {
        VkQueryPoolCreateInfo queryPoolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = MAX_FRAMES_IN_FLIGHT * 2; // Start and end timestamps per frame

        if (vkCreateQueryPool(device, &queryPoolInfo, nullptr, &queryPool) == VK_SUCCESS) {
            timestampResults.resize(MAX_FRAMES_IN_FLIGHT * 2);
            std::cout << "Vulkan: GPU timing enabled (period: " << timestampPeriod << " ns)" << std::endl;
        } else {
            std::cerr << "Vulkan: Failed to create query pool" << std::endl;
        }
    } else {
        std::cout << "Vulkan: GPU timing not supported on this device" << std::endl;
    }

    // Descriptor set layout: binding 0 = output buffer, binding 1 = triangle buffer
    VkDescriptorSetLayoutBinding bindings[2] = {};

    // Binding 0: Output buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Triangle buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 2;
    dslci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create descriptor set layout" << std::endl;
        shutdown();
        return false;
    }

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 2; // Output buffer + triangle buffer
    VkDescriptorPoolCreateInfo dpc{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpc.maxSets = 1;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &dpc, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create descriptor pool" << std::endl;
        shutdown();
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo dsa{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsa.descriptorPool = descriptorPool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &dsa, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to allocate descriptor set" << std::endl;
        shutdown();
        return false;
    }

    // Pipeline layout: include push constants for camera data
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = 128; // Enough space for camera data (will be defined in processFrame)

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptorSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create pipeline layout" << std::endl;
        shutdown();
        return false;
    }

    // If we have an embedded shader, create the shader module and pipeline now
    if (hasEmbeddedShader()) {
        if (!createComputePipelineFromEmbeddedShader()) {
            std::cerr << "Vulkan: Failed to create compute pipeline (embedded shader)" << std::endl;
            // Not fatal - we leave pipeline null and report it later
        } else {
            pipelineReady = true;
        }
    }

    return true;
}

bool VulkanRuntime::createComputePipelineFromEmbeddedShader() {
    if (!hasEmbeddedShader() || !device) return false;
    // Shader module
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = kComputeShaderSpvSize;
    smci.pCode = reinterpret_cast<const uint32_t*>(kComputeShaderSpv);
    if (vkCreateShaderModule(device, &smci, nullptr, &computeShader) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create shader module from embedded SPIR-V\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = computeShader;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpci.stage = stage;
    cpci.layout = pipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &computePipeline) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create compute pipeline\n";
        vkDestroyShaderModule(device, computeShader, nullptr);
        computeShader = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanRuntime::shutdown() {
    // Wait device idle
    if (device) vkDeviceWaitIdle(device);

    // Destroy buffers and memory
    deallocateBuffer(outputBuffer, outputMemory, outputBufferSize);
    outputBufferSize = 0;
    destroyStagingBuffer();
    cleanupSceneBuffers();

    if (descriptorPool) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (computePipeline) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (computeShader) {
        vkDestroyShaderModule(device, computeShader, nullptr);
        computeShader = VK_NULL_HANDLE;
    }

    // Clean up synchronization objects
    for (auto& fence : fences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
    }
    fences.clear();

    for (auto& semaphore : computeFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    computeFinishedSemaphores.clear();
    commandBuffers.clear();

    if (queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
    }

    if (commandPool) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (instance) {
#ifdef ENABLE_VULKAN_VALIDATION
        cleanupDebugMessenger();
#endif
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    // Reset state
    computeQueue = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    computeQueueFamily = UINT32_MAX;
    pipelineReady = false;
    outputWidth = outputHeight = 0;
    hostPixelCache.clear();
    lastDispatchMs = 0;
}

// Internal helper to (re)create a host-visible output buffer sized for width*height RGBA8 (uint32_t per pixel)
bool VulkanRuntime::ensureOutputBuffer(uint32_t width, uint32_t height) {
    if (!device || !physicalDevice) return false;
    size_t requiredCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t requiredSize = requiredCount * sizeof(uint32_t);

    // If current buffer already fits, return true
    if (outputBuffer && outputMemory && requiredSize <= outputBufferSize) {
        outputWidth = width;
        outputHeight = height;
        hostPixelCache.resize(requiredCount);
        return true;
    }

    // Destroy old
    if (outputBuffer) {
        vkDestroyBuffer(device, outputBuffer, nullptr);
        outputBuffer = VK_NULL_HANDLE;
    }
    if (outputMemory) {
        vkFreeMemory(device, outputMemory, nullptr);
        outputMemory = VK_NULL_HANDLE;
    }

    // Create buffer
    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = requiredSize;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bci, nullptr, &outputBuffer) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create output buffer\n";
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outputBuffer, &memReq);

    // For iGPUs, try to find unified memory first (device local + host visible)
    uint32_t memType = findMemoryType(physicalDevice, memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (memType == UINT32_MAX) {
        // Fallback to host visible and coherent (standard approach)
        memType = findMemoryType(physicalDevice, memReq.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    if (memType == UINT32_MAX) {
        // Last resort: device local only (would need staging)
        memType = findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memType == UINT32_MAX) {
            std::cerr << "Vulkan: No suitable memory type found for output buffer\n";
            vkDestroyBuffer(device, outputBuffer, nullptr);
            outputBuffer = VK_NULL_HANDLE;
            return false;
        } else {
            std::cout << "Vulkan: Using device-local memory (may need staging for CPU access)\n";
        }
    } else {
        std::cout << "Vulkan: Using " << (memType == hostVisibleMemoryTypeIndex ? "host-visible" : "unified") << " memory\n";
    }

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = memReq.size;
    mai.memoryTypeIndex = memType;

    if (vkAllocateMemory(device, &mai, nullptr, &outputMemory) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to allocate output buffer memory\n";
        vkDestroyBuffer(device, outputBuffer, nullptr);
        outputBuffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(device, outputBuffer, outputMemory, 0) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to bind buffer memory\n";
        vkDestroyBuffer(device, outputBuffer, nullptr);
        vkFreeMemory(device, outputMemory, nullptr);
        outputBuffer = VK_NULL_HANDLE;
        outputMemory = VK_NULL_HANDLE;
        return false;
    }

    outputBufferSize = memReq.size;
    outputWidth = width;
    outputHeight = height;
    hostPixelCache.assign(requiredCount, 0u);

    // Update descriptor set to point to this buffer
    VkDescriptorBufferInfo dbi{};
    dbi.buffer = outputBuffer;
    dbi.offset = 0;
    dbi.range = requiredSize;

    VkWriteDescriptorSet wds{};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = descriptorSet;
    wds.dstBinding = 0;
    wds.dstArrayElement = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wds.pBufferInfo = &dbi;

    vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);

    return true;
}

// Process a frame: dispatch compute shader to produce width x height RGBA pixels into the storage buffer, then map it into hostPixelCache
void VulkanRuntime::processFrame(int width, int height) {
    if (!device || !computeQueue) return;
    if (width <= 0 || height <= 0) return;

    if (!pipelineReady) {
        // No pipeline: nothing to dispatch
        return;
    }

    // Ensure buffer is big enough and descriptor updated
    if (!ensureOutputBuffer(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        // Can't create buffer: abort
        return;
    }

    // Wait for the current frame's fence (non-blocking check first)
    VkResult fenceResult = vkGetFenceStatus(device, fences[currentFrame]);
    if (fenceResult == VK_NOT_READY) {
        // Previous frame still in flight, skip this frame or wait
        return; // Skip for now to maintain responsiveness
    } else if (fenceResult != VK_SUCCESS) {
        std::cerr << "Vulkan: Fence error" << std::endl;
        return;
    }

    // Reset fence and command buffer for current frame
    vkResetFences(device, 1, &fences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    // Record command buffer for current frame
    VkCommandBuffer currentCmdBuffer = commandBuffers[currentFrame];
    VkCommandBufferBeginInfo cbbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(currentCmdBuffer, &cbbi);

    // Reset query pool for this frame
    if (queryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(currentCmdBuffer, queryPool, currentFrame * 2, 2);
        vkCmdWriteTimestamp(currentCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, currentFrame * 2);
    }

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(currentCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(currentCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // Push constants: time, width, height, padding
    struct PushConstants {
        float time;
        uint32_t width;
        uint32_t height;
        float padding;
    } pc;

    // Calculate animation time
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float elapsedTime = std::chrono::duration<float>(currentTime - startTime).count();

    pc.time = elapsedTime;
    pc.width = static_cast<uint32_t>(width);
    pc.height = static_cast<uint32_t>(height);
    pc.padding = 0.0f;

    vkCmdPushConstants(currentCmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    // Optimize workgroup dispatch based on device capabilities
    uint32_t groupCountX, groupCountY;
    optimizeWorkGroupSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), groupCountX, groupCountY);

    vkCmdDispatch(currentCmdBuffer, groupCountX, groupCountY, 1);

    // Add memory barrier to ensure compute writes are visible
    VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(currentCmdBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    // End timestamp
    if (queryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(currentCmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, currentFrame * 2 + 1);
    }

    vkEndCommandBuffer(currentCmdBuffer);

    // Submit with semaphore signaling
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &currentCmdBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &computeFinishedSemaphores[currentFrame];

    auto t0 = std::chrono::high_resolution_clock::now();
    VkResult submitResult = vkQueueSubmit(computeQueue, 1, &si, fences[currentFrame]);
    if (submitResult != VK_SUCCESS) {
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            lastError = VulkanError::DeviceLost;
            std::cerr << "Vulkan: Device lost during command submission" << std::endl;
        } else {
            lastError = VulkanError::CommandSubmissionFailed;
            std::cerr << "Vulkan: Failed to submit compute work (error: " << submitResult << ")" << std::endl;
        }
        return;
    }

    // For immediate results, wait for this frame (can be made async later)
    VkResult waitResult = vkWaitForFences(device, 1, &fences[currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != VK_SUCCESS) {
        if (waitResult == VK_ERROR_DEVICE_LOST) {
            lastError = VulkanError::DeviceLost;
            std::cerr << "Vulkan: Device lost while waiting for fence" << std::endl;
        }
        return;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    lastDispatchMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Retrieve GPU timing results
    if (queryPool != VK_NULL_HANDLE) {
        uint64_t timestamps[2];
        VkResult result = vkGetQueryPoolResults(device, queryPool, currentFrame * 2, 2,
                                               sizeof(timestamps), timestamps, sizeof(uint64_t),
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result == VK_SUCCESS) {
            uint64_t gpuTimeNs = timestamps[1] - timestamps[0];
            gpuTimeMs = static_cast<float>(gpuTimeNs) * timestampPeriod / 1000000.0f;
        }
    }

    // Map output memory and copy to hostPixelCache
    if (outputMemory) {
        void* mapped = nullptr;
        VkResult r = vkMapMemory(device, outputMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (r == VK_SUCCESS && mapped) {
            size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            uint32_t* src = reinterpret_cast<uint32_t*>(mapped);
            // Copy into hostPixelCache (already sized to pixelCount by ensureOutputBuffer)
            std::memcpy(hostPixelCache.data(), src, pixelCount * sizeof(uint32_t));
            vkUnmapMemory(device, outputMemory);
        } else {
            std::cerr << "Vulkan: Failed to map output memory\n";
        }
    }

    // Track frame timing for FPS calculation
    auto now = std::chrono::high_resolution_clock::now();
    frameTimes.push_back(now);
    frameCount++;

    // Keep only last 60 frame times for FPS calculation
    if (frameTimes.size() > 60) {
        frameTimes.erase(frameTimes.begin());
    }

    // Move to next frame
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRuntime::processFrameWithCamera(int width, int height, const Vec3& cameraPos, const Vec3& cameraTarget, const Vec3& cameraUp) {
    if (!device || !computeQueue) return;
    if (width <= 0 || height <= 0) return;

    if (!pipelineReady) {
        // No pipeline: nothing to dispatch
        return;
    }

    // Ensure buffer is big enough and descriptor updated
    if (!ensureOutputBuffer(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        // Can't create buffer: abort
        return;
    }

    // Wait for the current frame's fence (non-blocking check first)
    VkResult fenceResult = vkGetFenceStatus(device, fences[currentFrame]);
    if (fenceResult == VK_NOT_READY) {
        // Previous frame still in flight, skip this frame or wait
        return; // Skip for now to maintain responsiveness
    } else if (fenceResult != VK_SUCCESS) {
        std::cerr << "Vulkan: Fence error" << std::endl;
        return;
    }

    // Reset fence and command buffer for current frame
    vkResetFences(device, 1, &fences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    // Record command buffer for current frame
    VkCommandBuffer currentCmdBuffer = commandBuffers[currentFrame];
    VkCommandBufferBeginInfo cbbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(currentCmdBuffer, &cbbi);

    // Reset query pool for this frame
    if (queryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(currentCmdBuffer, queryPool, currentFrame * 2, 2);
        vkCmdWriteTimestamp(currentCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, currentFrame * 2);
    }

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(currentCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(currentCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // Push constants: camera data
    struct PushConstants {
        float time;
        uint32_t width;
        uint32_t height;
        uint32_t triangleCount;

        // Camera position (16 bytes)
        float cameraPosX, cameraPosY, cameraPosZ, padding2;

        // Camera target (16 bytes)
        float cameraTargetX, cameraTargetY, cameraTargetZ, padding3;

        // Camera up (16 bytes)
        float cameraUpX, cameraUpY, cameraUpZ, padding4;
    } pc;

    // Calculate animation time
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float elapsedTime = std::chrono::duration<float>(currentTime - startTime).count();

    pc.time = elapsedTime;
    pc.width = static_cast<uint32_t>(width);
    pc.height = static_cast<uint32_t>(height);
    pc.triangleCount = triangleCount;

    pc.cameraPosX = static_cast<float>(cameraPos.x);
    pc.cameraPosY = static_cast<float>(cameraPos.y);
    pc.cameraPosZ = static_cast<float>(cameraPos.z);
    pc.padding2 = 0.0f;

    pc.cameraTargetX = static_cast<float>(cameraTarget.x);
    pc.cameraTargetY = static_cast<float>(cameraTarget.y);
    pc.cameraTargetZ = static_cast<float>(cameraTarget.z);
    pc.padding3 = 0.0f;

    pc.cameraUpX = static_cast<float>(cameraUp.x);
    pc.cameraUpY = static_cast<float>(cameraUp.y);
    pc.cameraUpZ = static_cast<float>(cameraUp.z);
    pc.padding4 = 0.0f;

    vkCmdPushConstants(currentCmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    // Optimize workgroup dispatch based on device capabilities
    uint32_t groupCountX, groupCountY;
    optimizeWorkGroupSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), groupCountX, groupCountY);

    vkCmdDispatch(currentCmdBuffer, groupCountX, groupCountY, 1);

    // Add memory barrier to ensure compute writes are visible
    VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(currentCmdBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    // End timestamp
    if (queryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(currentCmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, currentFrame * 2 + 1);
    }

    vkEndCommandBuffer(currentCmdBuffer);

    // Submit with semaphore signaling
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &currentCmdBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &computeFinishedSemaphores[currentFrame];

    auto t0 = std::chrono::high_resolution_clock::now();
    VkResult submitResult = vkQueueSubmit(computeQueue, 1, &si, fences[currentFrame]);
    if (submitResult != VK_SUCCESS) {
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            lastError = VulkanError::DeviceLost;
            std::cerr << "Vulkan: Device lost during command submission" << std::endl;
        } else {
            lastError = VulkanError::CommandSubmissionFailed;
            std::cerr << "Vulkan: Failed to submit compute work (error: " << submitResult << ")" << std::endl;
        }
        return;
    }

    // For immediate results, wait for this frame (can be made async later)
    VkResult waitResult = vkWaitForFences(device, 1, &fences[currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != VK_SUCCESS) {
        if (waitResult == VK_ERROR_DEVICE_LOST) {
            lastError = VulkanError::DeviceLost;
            std::cerr << "Vulkan: Device lost while waiting for fence" << std::endl;
        }
        return;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    lastDispatchMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Retrieve GPU timing results
    if (queryPool != VK_NULL_HANDLE) {
        uint64_t timestamps[2];
        VkResult result = vkGetQueryPoolResults(device, queryPool, currentFrame * 2, 2,
                                               sizeof(timestamps), timestamps, sizeof(uint64_t),
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result == VK_SUCCESS) {
            uint64_t gpuTimeNs = timestamps[1] - timestamps[0];
            gpuTimeMs = static_cast<float>(gpuTimeNs) * timestampPeriod / 1000000.0f;
        }
    }

    // Map output memory and copy to hostPixelCache
    if (outputMemory) {
        void* mapped = nullptr;
        VkResult r = vkMapMemory(device, outputMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (r == VK_SUCCESS && mapped) {
            size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            uint32_t* src = reinterpret_cast<uint32_t*>(mapped);
            // Copy into hostPixelCache (already sized to pixelCount by ensureOutputBuffer)
            std::memcpy(hostPixelCache.data(), src, pixelCount * sizeof(uint32_t));
            vkUnmapMemory(device, outputMemory);
        } else {
            std::cerr << "Vulkan: Failed to map output memory\n";
        }
    }

    // Track frame timing for FPS calculation
    auto now = std::chrono::high_resolution_clock::now();
    frameTimes.push_back(now);
    frameCount++;

    // Keep only last 60 frame times for FPS calculation
    if (frameTimes.size() > 60) {
        frameTimes.erase(frameTimes.begin());
    }

    // Move to next frame
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRuntime::processFrameWithScene(int width, int height, const Vec3& cameraPos, const Vec3& cameraTarget, const Vec3& cameraUp, const Scene& scene) {
    // Upload scene data if needed
    bool needDescriptorUpdate = false;
    if (triangleCount == 0 || triangleBuffer == VK_NULL_HANDLE) {
        if (!uploadSceneData(scene)) {
            std::cerr << "Vulkan: Failed to upload scene data" << std::endl;
            return;
        }
        needDescriptorUpdate = true;
    }

    // First ensure output buffer exists by calling the regular processing
    processFrameWithCamera(width, height, cameraPos, cameraTarget, cameraUp);

    // Update descriptor set to include triangle buffer after output buffer is created
    if (needDescriptorUpdate && triangleBuffer != VK_NULL_HANDLE && outputBuffer != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo bufferInfos[2];

        // Output buffer (binding 0)
        bufferInfos[0].buffer = outputBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = outputBufferSize;

        // Triangle buffer (binding 1)
        bufferInfos[1].buffer = triangleBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = triangleBufferSize;

        VkWriteDescriptorSet writes[2] = {};

        // Output buffer write
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &bufferInfos[0];

        // Triangle buffer write
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &bufferInfos[1];

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        std::cout << "Vulkan: Updated descriptor sets with triangle buffer" << std::endl;
    }
}

bool VulkanRuntime::isFrameReady() const {
    if (fences.empty()) return false;
    uint32_t prevFrame = (currentFrame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
    return vkGetFenceStatus(device, fences[prevFrame]) == VK_SUCCESS;
}

// Get pointer into pixel data produced by last dispatch (RGBA8 packed as 0xAABBGGRR in little-endian typical shader writes — here we assume RGBA8 little-endian packed into uint32_t)
bool VulkanRuntime::getOutput(const uint32_t*& data, uint32_t& w, uint32_t& h) const {
    if (hostPixelCache.empty() || outputWidth == 0 || outputHeight == 0) return false;
    data = hostPixelCache.data();
    w = outputWidth;
    h = outputHeight;
    return true;
}

// Additional getters used by renderer UI
void VulkanRuntime::getOutputDims(uint32_t& w, uint32_t& h) const {
    w = outputWidth; h = outputHeight;
}
bool VulkanRuntime::isPipelineReady() const { return pipelineReady; }
uint32_t VulkanRuntime::getLastDispatchMs() const { return lastDispatchMs; }
std::string VulkanRuntime::getDeviceName() const { return deviceName; }
uint32_t VulkanRuntime::getVendorId() const { return vendorId; }
uint32_t VulkanRuntime::getDeviceId() const { return deviceId; }
uint32_t VulkanRuntime::getApiVersion() const { return apiVersion; }
uint32_t VulkanRuntime::getDriverVersion() const { return driverVersion; }
uint32_t VulkanRuntime::getComputeQueueFamily() const { return computeQueueFamily; }

bool VulkanRuntime::hasEmbeddedShader() const { return kComputeShaderSpvSize > 0; }

size_t VulkanRuntime::getTotalAllocatedMemory() const {
    return totalAllocatedMemory;
}

float VulkanRuntime::getGpuTimeMs() const {
    return gpuTimeMs;
}

uint32_t VulkanRuntime::getFramesPerSecond() const {
    if (frameTimes.size() < 2) return 0;

    auto duration = frameTimes.back() - frameTimes.front();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    if (durationMs > 0) {
        return static_cast<uint32_t>((frameTimes.size() - 1) * 1000 / durationMs);
    }
    return 0;
}

VulkanRuntime::VulkanError VulkanRuntime::getLastError() const {
    return lastError;
}

const char* VulkanRuntime::getErrorString(VulkanError error) const {
    switch (error) {
        case VulkanError::None: return "No error";
        case VulkanError::InitializationFailed: return "Vulkan initialization failed";
        case VulkanError::DeviceLost: return "Vulkan device lost";
        case VulkanError::OutOfMemory: return "Out of GPU memory";
        case VulkanError::ShaderCompilationFailed: return "Shader compilation failed";
        case VulkanError::BufferCreationFailed: return "Buffer creation failed";
        case VulkanError::CommandSubmissionFailed: return "Command submission failed";
        default: return "Unknown error";
    }
}

bool VulkanRuntime::recoverFromDeviceLost() {
    std::cerr << "Vulkan: Attempting device recovery..." << std::endl;
    deviceLostCount++;

    // Wait for device to become idle
    if (device) {
        VkResult result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
            std::cerr << "Vulkan: Failed to wait for device idle during recovery" << std::endl;
            return false;
        }
    }

    // Reinitialize the entire Vulkan context
    shutdown();
    bool success = initialize();

    if (success) {
        lastError = VulkanError::None;
        std::cout << "Vulkan: Device recovery successful" << std::endl;
    } else {
        lastError = VulkanError::InitializationFailed;
        std::cerr << "Vulkan: Device recovery failed" << std::endl;
    }

    return success;
}

void VulkanRuntime::destroyCurrentPipeline() {
    if (device) {
        vkDeviceWaitIdle(device);
    }

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (computeShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, computeShader, nullptr);
        computeShader = VK_NULL_HANDLE;
    }

    pipelineReady = false;
}

bool VulkanRuntime::createComputePipelineFromSPIRV(const std::vector<uint32_t>& spirvCode) {
    if (!device || spirvCode.empty()) return false;

    // Destroy existing pipeline first
    destroyCurrentPipeline();

    // Create shader module
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = spirvCode.size() * sizeof(uint32_t);
    smci.pCode = spirvCode.data();

    if (vkCreateShaderModule(device, &smci, nullptr, &computeShader) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create shader module from SPIR-V" << std::endl;
        lastError = VulkanError::ShaderCompilationFailed;
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = computeShader;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = stage;
    cpci.layout = pipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &computePipeline) != VK_SUCCESS) {
        std::cerr << "Vulkan: Failed to create compute pipeline" << std::endl;
        vkDestroyShaderModule(device, computeShader, nullptr);
        computeShader = VK_NULL_HANDLE;
        lastError = VulkanError::ShaderCompilationFailed;
        return false;
    }

    pipelineReady = true;
    lastError = VulkanError::None;
    std::cout << "Vulkan: Compute pipeline created successfully" << std::endl;
    return true;
}

bool VulkanRuntime::loadShaderFromFile(const std::string& shaderPath) {
    std::ifstream file(shaderPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Vulkan: Failed to open shader file: " << shaderPath << std::endl;
        return false;
    }

    // Check if it's a SPIR-V file (.spv) or GLSL source
    bool isSpirv = shaderPath.ends_with(".spv");

    if (isSpirv) {
        // Read SPIR-V binary
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize % 4 != 0) {
            std::cerr << "Vulkan: Invalid SPIR-V file size" << std::endl;
            return false;
        }

        std::vector<uint32_t> spirvCode(fileSize / 4);
        file.read(reinterpret_cast<char*>(spirvCode.data()), fileSize);
        file.close();

        return createComputePipelineFromSPIRV(spirvCode);
    } else {
        // Read GLSL source and compile
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return recompileShaderFromSource(buffer.str());
    }
}

bool VulkanRuntime::recompileShaderFromSource(const std::string& glslSource) {
    std::vector<uint32_t> spirvCode;
    if (!compileGLSLToSPIRV(glslSource, spirvCode)) {
        return false;
    }

    return createComputePipelineFromSPIRV(spirvCode);
}

bool VulkanRuntime::compileGLSLToSPIRV(const std::string& glslSource, std::vector<uint32_t>& spirvCode) {
    // For now, this is a placeholder. In a real implementation, you would use:
    // - glslang library for runtime compilation
    // - shaderc library (Google's GLSL compiler)
    // - Or call external glslangValidator process

    std::cerr << "Vulkan: Runtime GLSL compilation not implemented. Use pre-compiled SPIR-V files." << std::endl;
    lastError = VulkanError::ShaderCompilationFailed;
    return false;
}

bool VulkanRuntime::reloadEmbeddedShader() {
    if (!hasEmbeddedShader()) {
        std::cerr << "Vulkan: No embedded shader available to reload" << std::endl;
        return false;
    }

    std::vector<uint32_t> spirvCode(
        reinterpret_cast<const uint32_t*>(kComputeShaderSpv),
        reinterpret_cast<const uint32_t*>(kComputeShaderSpv) + (kComputeShaderSpvSize / sizeof(uint32_t))
    );

    return createComputePipelineFromSPIRV(spirvCode);
}

void VulkanRuntime::optimizeWorkGroupSize(uint32_t width, uint32_t height, uint32_t& groupsX, uint32_t& groupsY) {
    // Use cached optimal workgroup sizes
    uint32_t wgX = optimalWorkGroupSizeX;
    uint32_t wgY = optimalWorkGroupSizeY;

    // Calculate number of workgroups needed
    groupsX = (width + wgX - 1) / wgX;
    groupsY = (height + wgY - 1) / wgY;

    // Ensure we don't exceed device limits
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

    groupsX = std::min(groupsX, props.limits.maxComputeWorkGroupCount[0]);
    groupsY = std::min(groupsY, props.limits.maxComputeWorkGroupCount[1]);
}

void VulkanRuntime::setCustomPushConstants(const void* data, uint32_t size) {
    // This would be used for custom push constant data beyond width/height
    // Implementation depends on specific shader requirements
    // For now, this is a placeholder for future extensibility
}

bool VulkanRuntime::uploadSceneData(const Scene& scene) {
    if (!device || !physicalDevice) return false;

    // Clean up existing triangle buffer
    cleanupSceneBuffers();

    // Collect triangle data from scene
    struct GPUTriangle {
        // Each vec3 in GLSL is 16-byte aligned (4 floats)
        float v0[4];        // vec3 + padding (16 bytes)
        float v1[4];        // vec3 + padding (16 bytes)
        float v2[4];        // vec3 + padding (16 bytes)
        float normal[4];    // vec3 + padding (16 bytes)
        float materialColor[4]; // vec3 + padding (16 bytes)
        float materialProps[4]; // vec4 (16 bytes)
        // Total: 96 bytes per triangle (properly aligned)
    };

    std::vector<GPUTriangle> triangles;
    triangles.reserve(scene.objects.size());

    for (const auto& obj : scene.objects) {
        Vec3 v0, v1, v2;
        if (obj->getTriangleVertices(v0, v1, v2)) {
            GPUTriangle tri;

            // Vertices (with padding for vec3 alignment)
            tri.v0[0] = static_cast<float>(v0.x); tri.v0[1] = static_cast<float>(v0.y); tri.v0[2] = static_cast<float>(v0.z); tri.v0[3] = 0.0f;
            tri.v1[0] = static_cast<float>(v1.x); tri.v1[1] = static_cast<float>(v1.y); tri.v1[2] = static_cast<float>(v1.z); tri.v1[3] = 0.0f;
            tri.v2[0] = static_cast<float>(v2.x); tri.v2[1] = static_cast<float>(v2.y); tri.v2[2] = static_cast<float>(v2.z); tri.v2[3] = 0.0f;

            // Calculate face normal (with padding)
            Vec3 edge1 = v1 - v0;
            Vec3 edge2 = v2 - v0;
            Vec3 normal = edge1.cross(edge2).normalize();
            tri.normal[0] = static_cast<float>(normal.x);
            tri.normal[1] = static_cast<float>(normal.y);
            tri.normal[2] = static_cast<float>(normal.z);
            tri.normal[3] = 0.0f;

            // Material properties (with padding)
            const Material& mat = obj->material;
            tri.materialColor[0] = static_cast<float>(mat.color.x);
            tri.materialColor[1] = static_cast<float>(mat.color.y);
            tri.materialColor[2] = static_cast<float>(mat.color.z);
            tri.materialColor[3] = 0.0f;

            // For now, set default material properties (can be extended later)
            tri.materialProps[0] = 0.1f; // reflectivity
            tri.materialProps[1] = 0.8f; // roughness
            tri.materialProps[2] = 0.0f; // transparency
            tri.materialProps[3] = 1.0f; // refractionIndex

            triangles.push_back(tri);
        }
    }

    if (triangles.empty()) {
        triangleCount = 0;
        return true; // No triangles to upload
    }

    triangleCount = static_cast<uint32_t>(triangles.size());
    size_t bufferSize = triangles.size() * sizeof(GPUTriangle);

    // Create triangle buffer
    if (!allocateBuffer(triangleBuffer, triangleMemory, bufferSize,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        std::cerr << "Vulkan: Failed to allocate triangle buffer" << std::endl;
        return false;
    }

    triangleBufferSize = bufferSize;

    // Upload triangle data
    void* mapped = nullptr;
    if (vkMapMemory(device, triangleMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS) {
        std::memcpy(mapped, triangles.data(), bufferSize);
        vkUnmapMemory(device, triangleMemory);

        std::cout << "Vulkan: Uploaded " << triangleCount << " triangles to GPU" << std::endl;
        return true;
    } else {
        std::cerr << "Vulkan: Failed to map triangle buffer memory" << std::endl;
        cleanupSceneBuffers();
        return false;
    }
}

void VulkanRuntime::cleanupSceneBuffers() {
    if (triangleBuffer != VK_NULL_HANDLE || triangleMemory != VK_NULL_HANDLE) {
        deallocateBuffer(triangleBuffer, triangleMemory, triangleBufferSize);
        triangleBufferSize = 0;
        triangleCount = 0;
    }
}
