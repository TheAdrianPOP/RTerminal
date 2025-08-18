{
    depfiles = "vulkan_runtime.o: src\\vulkan_runtime.cpp src\\vulkan_runtime.h  E:\\VulkanSDK\\Include/vulkan/vulkan.h  E:\\VulkanSDK\\Include/vulkan/vk_platform.h  E:\\VulkanSDK\\Include/vulkan/vulkan_core.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h264std.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codecs_common.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h264std_encode.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h264std.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h265std.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h265std_encode.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h265std.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h264std_decode.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_h265std_decode.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_av1std.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_av1std_decode.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_av1std.h  E:\\VulkanSDK\\Include/vk_video/vulkan_video_codec_av1std_encode.h  src\\vector.h src\\constants.h src\\scene.h src\\triangle.h src\\object.h  src\\ray.h src\\aabb.h src\\material.h src\\texture.h src\\ui.h src\\console.h  src\\light.h src\\bvh.h src\\shaders_embedded.h\
",
    depfiles_format = "gcc",
    values = {
        [[C:\msys64\mingw64\bin\x86_64-w64-mingw32-g++]],
        {
            "-Os",
            "-std=c++20",
            [[-IE:\VulkanSDK\Include]],
            "-DUSE_VULKAN",
            "-fexceptions"
        }
    },
    files = {
        [[src\vulkan_runtime.cpp]]
    }
}