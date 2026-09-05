// Standalone, bounded Vulkan/HIP shared-buffer correctness probe. No model needed.
#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime_api.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>

#define VK(call) do { VkResult r=(call); if(r!=VK_SUCCESS) { \
    fprintf(stderr,"%s: Vulkan %d\n",#call,r); return 2; } } while(0)
#define HIP(call) do { hipError_t r=(call); if(r!=hipSuccess) { \
    fprintf(stderr,"%s: %s\n",#call,hipGetErrorString(r)); return 2; } } while(0)

int main() {
    HIP(hipInit(0)); HIP(hipSetDevice(0));
    char pci[64]={}; HIP(hipDeviceGetPCIBusId(pci,sizeof(pci),0));
    unsigned domain,bus,slot,function;
    if(sscanf(pci,"%x:%x:%x.%x",&domain,&bus,&slot,&function)!=4) return 2;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo=&app;
    VkInstance instance; VK(vkCreateInstance(&ici,nullptr,&instance));
    uint32_t n=0; VK(vkEnumeratePhysicalDevices(instance,&n,nullptr));
    std::vector<VkPhysicalDevice> devices(n); VK(vkEnumeratePhysicalDevices(instance,&n,devices.data()));
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    for(auto candidate:devices) {
        uint32_t count=0; VK(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&count,nullptr));
        std::vector<VkExtensionProperties> extensions(count);
        VK(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&count,extensions.data()));
        bool has_pci=false,has_fd=false;
        for(auto &ext:extensions) {
            has_pci |= !strcmp(ext.extensionName,VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
            has_fd |= !strcmp(ext.extensionName,VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        }
        if(!has_pci || !has_fd) continue;
        VkPhysicalDevicePCIBusInfoPropertiesEXT address{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; properties.pNext=&address;
        vkGetPhysicalDeviceProperties2(candidate,&properties);
        if(address.pciDomain==domain && address.pciBus==bus && address.pciDevice==slot && address.pciFunction==function) {
            physical=candidate; printf("Matched Vulkan/HIP adapter: %s (%s)\n",properties.properties.deviceName,pci); break;
        }
    }
    if(!physical) { fprintf(stderr,"No matching external-memory Vulkan adapter\n"); return 2; }
    const auto handle_type=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkPhysicalDeviceExternalBufferInfo info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
    info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT; info.handleType=handle_type;
    VkExternalBufferProperties capabilities{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
    vkGetPhysicalDeviceExternalBufferProperties(physical,&info,&capabilities);
    if(!(capabilities.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT)) return 2;
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);
    std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());
    uint32_t family=0; while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
    if(family==n) return 2;
    float priority=1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex=family; qci.queueCount=1; qci.pQueuePriorities=&priority;
    const char *extension=VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&qci; dci.enabledExtensionCount=1; dci.ppEnabledExtensionNames=&extension;
    VkDevice device; VK(vkCreateDevice(physical,&dci,nullptr,&device));
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    auto get_fd=(PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(device,"vkGetMemoryFdKHR");
    if(!get_fd) return 2;
    constexpr size_t bytes=4096, words=bytes/sizeof(uint32_t);
    VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO}; external.handleTypes=handle_type;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size=bytes; bci.usage=info.usage; bci.sharingMode=VK_SHARING_MODE_EXCLUSIVE; bci.pNext=&external;
    VkBuffer shared,readback; VK(vkCreateBuffer(device,&bci,nullptr,&shared));
    bci.pNext=nullptr; VK(vkCreateBuffer(device,&bci,nullptr,&readback));
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    auto memory_type=[&](uint32_t mask,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)
            if((mask&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
        return UINT32_MAX;
    };
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,shared,&req);
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; dedicated.buffer=shared;
    VkExportMemoryAllocateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO}; export_info.handleTypes=handle_type; export_info.pNext=&dedicated;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.pNext=&export_info;
    alloc.allocationSize=req.size; alloc.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(alloc.memoryTypeIndex==UINT32_MAX) return 2;
    VkDeviceMemory shared_memory,readback_memory; VK(vkAllocateMemory(device,&alloc,nullptr,&shared_memory));
    VK(vkBindBufferMemory(device,shared,shared_memory,0));
    VkMemoryGetFdInfoKHR fd_info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR}; fd_info.memory=shared_memory; fd_info.handleType=handle_type;
    int fd=-1; VK(get_fd(device,&fd_info,&fd));
    hipExternalMemoryHandleDesc hd{}; hd.type=hipExternalMemoryHandleTypeOpaqueFd; hd.handle.fd=fd; hd.size=req.size; hd.flags=1;
    hipExternalMemory_t imported;
    hipError_t import_status=hipImportExternalMemory(&imported,&hd);
    if(import_status!=hipSuccess) { close(fd); fprintf(stderr,"hipImportExternalMemory: %s\n",hipGetErrorString(import_status)); return 2; }
    // The successful opaque-FD import transfers ownership to HIP.
    hipExternalMemoryBufferDesc bd{}; bd.size=bytes;
    void *mapped=nullptr; HIP(hipExternalMemoryGetMappedBuffer(&mapped,imported,&bd));
    vkGetBufferMemoryRequirements(device,readback,&req);
    alloc.pNext=nullptr; alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(alloc.memoryTypeIndex==UINT32_MAX) return 2;
    VK(vkAllocateMemory(device,&alloc,nullptr,&readback_memory)); VK(vkBindBufferMemory(device,readback,readback_memory,0));
    VkCommandPoolCreateInfo pci_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci_info.queueFamilyIndex=family; pci_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool; VK(vkCreateCommandPool(device,&pci_info,nullptr,&pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool=pool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1;
    VkCommandBuffer command; VK(vkAllocateCommandBuffers(device,&cai,&command));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; VK(vkCreateFence(device,&fci,nullptr,&fence));
    auto barrier=[&](bool release) {
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; b.buffer=shared; b.size=VK_WHOLE_SIZE;
        b.srcAccessMask=release?VK_ACCESS_TRANSFER_WRITE_BIT:0; b.dstAccessMask=release?0:VK_ACCESS_TRANSFER_READ_BIT;
        b.srcQueueFamilyIndex=release?family:VK_QUEUE_FAMILY_EXTERNAL; b.dstQueueFamilyIndex=release?VK_QUEUE_FAMILY_EXTERNAL:family;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,1,&b,0,nullptr);
    };
    auto submit=[&]() -> int {
        VK(vkEndCommandBuffer(command)); VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&command;
        VK(vkQueueSubmit(queue,1,&si,fence)); VK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
        VK(vkResetFences(device,1,&fence)); VK(vkResetCommandBuffer(command,0)); return 0;
    };
    uint32_t host[words]; void *cpu; VK(vkMapMemory(device,readback_memory,0,bytes,0,&cpu));
    for(unsigned iteration=0;iteration<16;++iteration) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK(vkBeginCommandBuffer(command,&begin));
        if(iteration) {
            VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            reuse.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; reuse.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&reuse,0,nullptr,0,nullptr);
        }
        const uint32_t pattern=0x12340000u+iteration;
        vkCmdFillBuffer(command,shared,0,bytes,pattern); barrier(true); if(submit()) return 2;
        HIP(hipMemcpy(host,mapped,bytes,hipMemcpyDeviceToHost));
        for(auto value:host) if(value!=pattern) { fprintf(stderr,"Vulkan -> HIP mismatch\n"); return 1; }
        HIP(hipMemset(mapped,0x50+iteration,bytes)); HIP(hipDeviceSynchronize());
        VK(vkBeginCommandBuffer(command,&begin)); barrier(false);
        VkBufferCopy copy{0,0,bytes}; vkCmdCopyBuffer(command,shared,readback,1,&copy);
        VkMemoryBarrier visible{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; visible.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; visible.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&visible,0,nullptr,0,nullptr);
        if(submit()) return 2;
        for(size_t i=0;i<words;++i) if(((uint32_t*)cpu)[i]!=(0x01010101u*(0x50+iteration))) {
            fprintf(stderr,"HIP -> Vulkan mismatch\n"); return 1;
        }
    }
    printf("{\"iterations\":16,\"words_per_direction\":1024,\"pass\":true}\n");
    vkUnmapMemory(device,readback_memory);
    HIP(hipFree(mapped)); HIP(hipDestroyExternalMemory(imported));
    vkDestroyFence(device,fence,nullptr); vkDestroyCommandPool(device,pool,nullptr);
    vkDestroyBuffer(device,shared,nullptr); vkDestroyBuffer(device,readback,nullptr);
    vkFreeMemory(device,shared_memory,nullptr); vkFreeMemory(device,readback_memory,nullptr);
    vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr); return 0;
}
