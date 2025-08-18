#ifndef VULKAN_RUNTIME_H
#define VULKAN_RUNTIME_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include "vector.h"
#include "scene.h"

#ifdef _DEBUG
#define ENABLE_VULKAN_VALIDATION
#endif

class VulkanRuntime {
public:
    VulkanRuntime() = default;
    ~VulkanRuntime();

    bool initialize();
    void shutdown();
    void processFrame(int width, int height);
    void processFrameWithCamera(int width, int height, const Vec3& cameraPos, const Vec3& cameraTarget, const Vec3& cameraUp);
    void processFrameWithScene(int width, int height, const Vec3& cameraPos, const Vec3& cameraTarget, const Vec3& cameraUp, const Scene& scene);
    bool isFrameReady() const; // Check if current frame is ready without blocking

    bool getOutput(const uint32_t*& data, uint32_t& w, uint32_t& h) const;
    void getOutputDims(uint32_t& w, uint32_t& h) const;

    bool hasEmbeddedShader() const;
    bool isPipelineReady() const;
    uint32_t getLastDispatchMs() const;

    // Shader hot-reloading
    bool loadShaderFromFile(const std::string& shaderPath);
    bool recompileShaderFromSource(const std::string& glslSource);
    bool reloadEmbeddedShader();

    // Compute dispatch optimization
    void optimizeWorkGroupSize(uint32_t width, uint32_t height, uint32_t& groupsX, uint32_t& groupsY);
    void setCustomPushConstants(const void* data, uint32_t size);

    std::string getDeviceName() const;
    uint32_t getVendorId() const;
    uint32_t getDeviceId() const;
    uint32_t getApiVersion() const;
    uint32_t getDriverVersion() const;
    uint32_t getComputeQueueFamily() const;

    // Performance metrics
    size_t getTotalAllocatedMemory() const;
    float getGpuTimeMs() const;
    uint32_t getFramesPerSecond() const;

    // Error handling and recovery
    enum class VulkanError {
        None = 0,
        InitializationFailed,
        DeviceLost,
        OutOfMemory,
        ShaderCompilationFailed,
        BufferCreationFailed,
        CommandSubmissionFailed
    };

    VulkanError getLastError() const;
    const char* getErrorString(VulkanError error) const;
    bool recoverFromDeviceLost();

private:
    bool createComputePipelineFromEmbeddedShader();
    bool ensureOutputBuffer(uint32_t width, uint32_t height);

    // Debug and validation support
    bool setupDebugMessenger();
    void cleanupDebugMessenger();
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    // Device selection helpers
    int scorePhysicalDevice(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    // Memory management helpers
    void cacheMemoryTypeIndices();
    bool createStagingBuffer(size_t size);
    void destroyStagingBuffer();
    bool allocateBuffer(VkBuffer& buffer, VkDeviceMemory& memory, size_t size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void deallocateBuffer(VkBuffer& buffer, VkDeviceMemory& memory, size_t size);

    // Scene data management
    bool uploadSceneData(const Scene& scene);
    void cleanupSceneBuffers();

    // Shader compilation helpers
    bool compileGLSLToSPIRV(const std::string& glslSource, std::vector<uint32_t>& spirvCode);
    bool createComputePipelineFromSPIRV(const std::vector<uint32_t>& spirvCode);
    void destroyCurrentPipeline();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

#ifdef ENABLE_VULKAN_VALIDATION
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#endif

    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = UINT32_MAX;

    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Multiple command buffers for frame pipelining
    static const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkFence> fences;
    std::vector<VkSemaphore> computeFinishedSemaphores;
    uint32_t currentFrame = 0;

    // GPU timing queries
    VkQueryPool queryPool = VK_NULL_HANDLE;
    std::vector<uint64_t> timestampResults;
    bool timestampValidBits = false;
    float timestampPeriod = 1.0f;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkShaderModule computeShader = VK_NULL_HANDLE;

    VkBuffer outputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory = VK_NULL_HANDLE;
    size_t outputBufferSize = 0;

    // Scene data buffers
    VkBuffer triangleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory triangleMemory = VK_NULL_HANDLE;
    size_t triangleBufferSize = 0;
    uint32_t triangleCount = 0;

    // Staging buffer for device-local memory transfers
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    size_t stagingBufferSize = 0;

    // Memory usage tracking
    size_t totalAllocatedMemory = 0;
    uint32_t hostVisibleMemoryTypeIndex = UINT32_MAX;
    uint32_t deviceLocalMemoryTypeIndex = UINT32_MAX;

    // Compute dispatch optimization
    uint32_t maxWorkGroupSizeX = 256;
    uint32_t maxWorkGroupSizeY = 256;
    uint32_t maxWorkGroupSizeZ = 64;
    uint32_t maxWorkGroupInvocations = 1024;
    uint32_t optimalWorkGroupSizeX = 8;
    uint32_t optimalWorkGroupSizeY = 8;

    std::vector<uint32_t> hostPixelCache;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;

    std::string deviceName;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t apiVersion = 0;
    uint32_t driverVersion = 0;

    bool pipelineReady = false;
    uint32_t lastDispatchMs = 0;

    // Performance tracking
    std::vector<std::chrono::high_resolution_clock::time_point> frameTimes;
    float gpuTimeMs = 0.0f;
    uint32_t frameCount = 0;

    // Error tracking
    VulkanError lastError = VulkanError::None;
    uint32_t deviceLostCount = 0;
};

#endif // VULKAN_RUNTIME_H
