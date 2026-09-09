#include "present.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace screenfg {

namespace {

#define VK_CHECK(x)                                                        \
    do {                                                                   \
        VkResult _r = (x);                                                 \
        if (_r != VK_SUCCESS)                                              \
            throw std::runtime_error(std::string(#x) + " 失敗 (VkResult " + \
                                     std::to_string(_r) + ")");            \
    } while (0)

// ---------- 5x7 bitmap font ----------
struct FontChar {
    const char* rows[7];
};
const FontChar kFont[] = {
    // 0: ' '
    {".....", ".....", ".....", ".....", ".....", ".....", "....."},
    // 1: '0'
    {".###.", "#...#", "#..##", "#.#.#", "##...#", "#...#", ".###."},
    // 2: '1'
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."},
    // 3: '2'
    {".###.", "#...#", "....#", ".##..", "#....", "#....", "#####"},
    // 4: '3'
    {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."},
    // 5: '4'
    {"....#", "...##", "..###", ".####", "#####", "....#", "....#"},
    // 6: '5'
    {"#####", "#....", "#....", "####.", "....#", "....#", ".###."},
    // 7: '6'
    {"..##.", ".#...", "#....", "###..", "#...#", "#...#", ".###."},
    // 8: '7'
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
    // 9: '8'
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
    // 10: '9'
    {".###.", "#...#", "#...#", ".####", "....#", "#...#", ".###."},
    // 11: ':'
    {".....", ".....", "..#..", ".....", "..#..", ".....", "....."},
    // 12: '.'
    {".....", ".....", ".....", ".....", ".....", "..#..", "..#.."},
    // 13: 'A'
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
    // 14: 'B'
    {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."},
    // 15: 'D'
    {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."},
    // 16: 'E'
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"},
    // 17: 'F'
    {"#####", "#....", "#....", "####.", "#....", "#....", "#...."},
    // 18: 'H'
    {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
    // 19: 'I'
    {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."},
    // 20: 'J'
    {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."},
    // 21: 'K'
    {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"},
    // 22: 'L'
    {"#....", "#....", "#....", "#....", "#....", "#....", "#####"},
    // 23: 'M'
    {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"},
    // 24: 'N'
    {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"},
    // 25: 'O'
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."},
    // 26: 'P'
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."},
    // 27: 'R'
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"},
    // 28: 'S'
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."},
    // 29: 'T'
    {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."},
    // 30: 'U'
    {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."},
    // 31: 'V'
    {"#...#", "#...#", "#...#", "#...#", ".#.#.", "..#..", "..#.."},
    // 32: 'W'
    {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"},
    // 33: 'X' / 'x'
    {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"},
    // 34: 'Y'
    {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."},
    // 35: 'C'
    {".###.", "#....", "#....", "#....", "#....", "#....", ".###."},
    // 36: 'G'
    {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."},
    // 37: 'Q'
    {".###.", "#...#", "#...#", "#.#.#", "#..#.", "#...#", ".#.#."},
    // 38: 'Z'
    {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"},
    // 39: '%'
    {"##.##", "##.##", "....#", "...#.", "..#..", "#.##.", "#.##."},
};

int fontIndex(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char) (c - 'a' + 'A');
    if (c == ' ')
        return 0;
    if (c >= '0' && c <= '9')
        return 1 + (c - '0');
    switch (c) {
    case ':': return 11;
    case '.': return 12;
    case 'A': return 13;
    case 'B': return 14;
    case 'C': return 35;
    case 'D': return 15;
    case 'E': return 16;
    case 'F': return 17;
    case 'G': return 36;
    case 'H': return 18;
    case 'I': return 19;
    case 'J': return 20;
    case 'K': return 21;
    case 'L': return 22;
    case 'M': return 23;
    case 'N': return 24;
    case 'O': return 25;
    case 'P': return 26;
    case 'Q': return 37;
    case 'R': return 27;
    case 'S': return 28;
    case 'T': return 29;
    case 'U': return 30;
    case 'V': return 31;
    case 'W': return 32;
    case 'X': return 33;
    case 'Y': return 34;
    case 'Z': return 38;
    case '%': return 39;
    default: return 0;
    }
}

constexpr int kScale = 2;
constexpr int kHudW = 512;
constexpr int kHudH = 64;

// ---------- PCI bus ID → DRM card index ----------
std::optional<int> pciToCardIndex(const std::string& pciId) {
    std::string link = "/sys/bus/pci/devices/" + pciId + "/drm";
    char buf[4096];
    ssize_t n = readlink(link.c_str(), buf, sizeof buf - 1);
    if (n <= 0)
        return std::nullopt;
    buf[n] = 0;
    std::string s(buf);
    auto pos = s.rfind("card");
    if (pos == std::string::npos)
        return std::nullopt;
    int idx = atoi(s.c_str() + pos + 4);
    if (idx < 0)
        return std::nullopt;
    return idx;
}

int readIntFile(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const char* p = s.c_str();
    while (*p && !isdigit((unsigned char) *p))
        ++p; // 跳過 "0x" 前綴
    return (int) strtol(p, nullptr, 16);
}

uint32_t pickMemType(const VkMemoryRequirements& mr, const VkPhysicalDeviceMemoryProperties& dmr) {
    for (uint32_t i = 0; i < dmr.memoryTypeCount; ++i)
        if (mr.memoryTypeBits & (1u << i))
            return i;
    return 0;
}

} // namespace

struct Presenter::Impl {
    SDL_Window* window = nullptr;
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    VkFormat swapFmt = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    uint32_t w = 0, h = 0;
    VkSemaphore acquireSem = VK_NULL_HANDLE;
    VkSemaphore readySem = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    // 持久 command pool / buffer（每帧只 reset，不新建——新建 pool 每帧會讓 lsfg-vk layer 崩）
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    bool hudOn = true;
    VkImage hudImage = VK_NULL_HANDLE;
    VkImageView hudView = VK_NULL_HANDLE;
    VkDeviceMemory hudImageMem = VK_NULL_HANDLE;
    VkBuffer hudStaging = VK_NULL_HANDLE;
    VkDeviceMemory hudStagingMem = VK_NULL_HANDLE;
    std::vector<uint8_t> hudPixels;
    std::string lastHudText;
    bool hudDirty = false; // setHudText 寫入 staging buffer 後設 true → 下次 present 才上傳到 hudImage
    uint32_t displayHz_ = 0;
    // CPU 上傳用的持久 staging（frame → buffer → image）
    VkBuffer frameStageBuf = VK_NULL_HANDLE;
    VkDeviceMemory frameStageBufMem = VK_NULL_HANDLE;
    VkImage frameStageImg = VK_NULL_HANDLE;
    VkDeviceMemory frameStageImgMem = VK_NULL_HANDLE;
    uint32_t frameStageW = 0, frameStageH = 0;
    VkImageLayout frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
};

// 延遲建立 CPU 上傳用的持久 staging（buffer + image），尺寸 = swapchain（固定）
void Presenter::ensureFrameStaging(Impl& im) {
    uint32_t w = im.w, h = im.h;
    if (im.frameStageImg != VK_NULL_HANDLE && im.frameStageW == w && im.frameStageH == h)
        return;
    if (im.frameStageImg != VK_NULL_HANDLE) {
        vkDestroyImage(im.dev, im.frameStageImg, nullptr);
        im.frameStageImg = VK_NULL_HANDLE;
    }
    if (im.frameStageImgMem != VK_NULL_HANDLE) {
        vkFreeMemory(im.dev, im.frameStageImgMem, nullptr);
        im.frameStageImgMem = VK_NULL_HANDLE;
    }
    if (im.frameStageBuf != VK_NULL_HANDLE) {
        vkDestroyBuffer(im.dev, im.frameStageBuf, nullptr);
        im.frameStageBuf = VK_NULL_HANDLE;
    }
    if (im.frameStageBufMem != VK_NULL_HANDLE) {
        vkFreeMemory(im.dev, im.frameStageBufMem, nullptr);
        im.frameStageBufMem = VK_NULL_HANDLE;
    }
    size_t bytes = (size_t) w * h * 4;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(im.dev, &bci, nullptr, &im.frameStageBuf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(im.dev, im.frameStageBuf, &mr);
    VkPhysicalDeviceMemoryProperties dmr;
    vkGetPhysicalDeviceMemoryProperties(im.physDev, &dmr);
    uint32_t memType = 0;
    for (uint32_t i = 0; i < dmr.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (dmr.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            memType = i;
            break;
        }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;
    VK_CHECK(vkAllocateMemory(im.dev, &mai, nullptr, &im.frameStageBufMem));
    VK_CHECK(vkBindBufferMemory(im.dev, im.frameStageBuf, im.frameStageBufMem, 0));
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VK_CHECK(vkCreateImage(im.dev, &ici, nullptr, &im.frameStageImg));
    VkMemoryRequirements imr;
    vkGetImageMemoryRequirements(im.dev, im.frameStageImg, &imr);
    VkMemoryAllocateInfo imai{};
    imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imai.allocationSize = imr.size;
    imai.memoryTypeIndex = pickMemType(imr, dmr);
    VK_CHECK(vkAllocateMemory(im.dev, &imai, nullptr, &im.frameStageImgMem));
    VK_CHECK(vkBindImageMemory(im.dev, im.frameStageImg, im.frameStageImgMem, 0));
    im.frameStageW = w;
    im.frameStageH = h;
    im.frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

Presenter::Presenter() : impl_(new Impl) {}
Presenter::~Presenter() { shutdown(); }
uint32_t Presenter::displayHz() const { return impl_ ? impl_->displayHz_ : 0; }

void Presenter::init(const PresentParams& p) {
    auto& im = *impl_;
    im.hudOn = p.hud;

    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("SDL_Init 失敗: ") + SDL_GetError());
    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw std::runtime_error(std::string("SDL_Vulkan_LoadLibrary 失敗: ") + SDL_GetError());

    int nDisplays = 0;
    SDL_DisplayID* displayIds = SDL_GetDisplays(&nDisplays);
    if (!displayIds || nDisplays == 0)
        throw std::runtime_error("找不到任何 display");
    if (p.displayIndex < 0 || p.displayIndex >= nDisplays)
        throw std::runtime_error("display " + std::to_string(p.displayIndex) + " 不存在（共 " + std::to_string(nDisplays) + " 塊）");
    SDL_DisplayID did = displayIds[p.displayIndex];
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(did);
    im.displayHz_ = (uint32_t) mode->refresh_rate;
    SDL_free(displayIds);
    SDL_Rect bounds{};
    SDL_GetDisplayBounds(did, &bounds);
    im.w = (uint32_t) bounds.w;
    im.h = (uint32_t) bounds.h;

    im.window = SDL_CreateWindow("screen-fg", im.w, im.h,
                                SDL_WINDOW_VULKAN);
    if (!im.window)
        throw std::runtime_error(std::string("SDL_CreateWindow 失敗：") + SDL_GetError());

    Uint32 nExt = 0;
    const char* const* extArr = SDL_Vulkan_GetInstanceExtensions(&nExt);
    std::vector<const char*> instExts;
    if (nExt > 0 && extArr)
        instExts.assign(extArr, extArr + nExt);
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "screen-fg";
    ai.apiVersion = VK_MAKE_VERSION(1, 2, 0);
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = (uint32_t) instExts.size();
    ici.ppEnabledExtensionNames = instExts.empty() ? nullptr : instExts.data();
    VK_CHECK(vkCreateInstance(&ici, nullptr, &im.inst));

    if (!SDL_Vulkan_CreateSurface(im.window, im.inst, nullptr, &im.surface))
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface 失敗: ") + SDL_GetError());

    uint32_t nDev = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(im.inst, &nDev, nullptr));
    std::vector<VkPhysicalDevice> devs(nDev);
    VK_CHECK(vkEnumeratePhysicalDevices(im.inst, &nDev, devs.data()));

    int wantIdx = -1;
    if (p.gpuPciId) {
        auto ci = pciToCardIndex(p.gpuPciId);
        if (!ci)
            throw std::runtime_error(std::string("找不到 PCI ") + p.gpuPciId);
        wantIdx = *ci;
    }

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < nDev; ++i) {
        if (wantIdx >= 0 && (int) i != wantIdx)
            continue;
        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(devs[i], &prop);
        uint32_t nQ = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nQ, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nQ);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nQ, qf.data());
        bool hasGraphics = false;
        for (auto& q : qf)
            if (q.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                hasGraphics = true;
        if (!hasGraphics)
            continue;
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], 0, im.surface, &supported);
        if (!supported)
            continue;
        if (wantIdx >= 0 && p.gpuPciId) {
            std::string sysDir = "/sys/bus/pci/devices/" + std::string(p.gpuPciId);
            int sv = readIntFile(sysDir + "/vendor");
            int sd = readIntFile(sysDir + "/device");
            if ((sv & 0xffff) != (prop.vendorID & 0xffff) || (sd & 0xffff) != (prop.deviceID & 0xffff))
                throw std::runtime_error("PCI 對映不準：Vulkan device " + std::to_string(i) + " 的 vendor/device ≠ sysfs");
        }
        chosen = devs[i];
        break;
    }
    if (chosen == VK_NULL_HANDLE)
        throw std::runtime_error("找不到可用 GPU（surface + " + std::string(p.gpuPciId ? p.gpuPciId : "auto") + "）");

    const char* enable[] = {"VK_KHR_swapchain"};

    uint32_t nQ = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &nQ, nullptr);
    std::vector<VkQueueFamilyProperties> qf(nQ);
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &nQ, qf.data());
    uint32_t qIdx = 0;
    for (uint32_t i = 0; i < nQ; ++i)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            qIdx = i;
            break;
        }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = qIdx;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = enable;
    im.physDev = chosen;
    VK_CHECK(vkCreateDevice(chosen, &dci, nullptr, &im.dev));
    vkGetDeviceQueue(im.dev, qIdx, 0, &im.queue);

    uint32_t nFmt = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(chosen, im.surface, &nFmt, nullptr));
    if (nFmt == 0)
        throw std::runtime_error("沒有 surface format");
    std::vector<VkSurfaceFormatKHR> fmts(nFmt);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(chosen, im.surface, &nFmt, fmts.data()));
    static const VkFormat prefer[] = {VK_FORMAT_B8G8R8A8_UNORM,
                                     VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM};
    im.swapFmt = fmts[0].format;
    im.colorSpace = fmts[0].colorSpace;
    for (auto want : prefer)
        for (auto& f : fmts)
            if (f.format == want) {
                im.swapFmt = want;
                im.colorSpace = f.colorSpace;
                break;
            }

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(chosen, im.surface, &caps));
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = im.surface;
    sci.minImageCount = 3;
    sci.imageFormat = im.swapFmt;
    sci.imageColorSpace = im.colorSpace;
    sci.imageExtent = {im.w, im.h};
    sci.imageArrayLayers = 1;
    // 務必含 TRANSFER_DST（blit 寫入 swapchain image）+ TRANSFER_SRC（lsfg-vk layer 捕捉呈現幀要讀）
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(im.dev, &sci, nullptr, &im.swap));
    uint32_t nImg = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(im.dev, im.swap, &nImg, nullptr));
    im.swapImages.resize(nImg);
    VK_CHECK(vkGetSwapchainImagesKHR(im.dev, im.swap, &nImg, im.swapImages.data()));
    im.swapViews.resize(nImg);
    for (uint32_t i = 0; i < nImg; ++i) {
        VkImageViewCreateInfo ivi{};
        ivi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivi.image = im.swapImages[i];
        ivi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivi.format = im.swapFmt;
        ivi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(im.dev, &ivi, nullptr, &im.swapViews[i]));
    }

    VkSemaphoreCreateInfo sci2{};
    sci2.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(im.dev, &sci2, nullptr, &im.acquireSem));
    VK_CHECK(vkCreateSemaphore(im.dev, &sci2, nullptr, &im.readySem));
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(im.dev, &fci, nullptr, &im.fence));
    // 讓 fence 初始 signal（第一次 wait 不阻塞）
    vkQueueSubmit(im.queue, 0, nullptr, im.fence);
    VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
    // 持久 command pool + buffer（只建立一次；每帧 reset，不新建）
    VkCommandPoolCreateInfo cpic{};
    cpic.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    VK_CHECK(vkCreateCommandPool(im.dev, &cpic, nullptr, &im.cmdPool));
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = im.cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(im.dev, &cbai, &im.cmdBuf));

    { // HUD 資源常備（執行中 setHud(on/off) 即生效；init 時無條件建立）
        im.hudPixels.assign((size_t) kHudW * kHudH * 4, 0);
        VkPhysicalDeviceMemoryProperties dmr0;
        vkGetPhysicalDeviceMemoryProperties(im.physDev, &dmr0);
        VkImageCreateInfo hici{};
        hici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        hici.imageType = VK_IMAGE_TYPE_2D;
        hici.format = VK_FORMAT_R8G8B8A8_UNORM;
        hici.extent = {(uint32_t) kHudW, (uint32_t) kHudH, 1};
        hici.mipLevels = 1;
        hici.arrayLayers = 1;
        hici.samples = VK_SAMPLE_COUNT_1_BIT;
        hici.tiling = VK_IMAGE_TILING_OPTIMAL;
        hici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        hici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        VK_CHECK(vkCreateImage(im.dev, &hici, nullptr, &im.hudImage));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(im.dev, im.hudImage, &mr);
        uint32_t memType = pickMemType(mr, dmr0);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memType;
        VK_CHECK(vkAllocateMemory(im.dev, &mai, nullptr, &im.hudImageMem));
        VK_CHECK(vkBindImageMemory(im.dev, im.hudImage, im.hudImageMem, 0));
        VkImageViewCreateInfo hvi{};
        hvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        hvi.image = im.hudImage;
        hvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        hvi.format = VK_FORMAT_R8G8B8A8_UNORM;
        hvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(im.dev, &hvi, nullptr, &im.hudView));
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = (size_t) kHudW * kHudH * 4;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VK_CHECK(vkCreateBuffer(im.dev, &bci, nullptr, &im.hudStaging));
        vkGetBufferMemoryRequirements(im.dev, im.hudStaging, &mr);
        // staging 要能 map → HOST_VISIBLE
        for (uint32_t i = 0; i < dmr0.memoryTypeCount; ++i)
            if ((mr.memoryTypeBits & (1u << i)) && (dmr0.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                memType = i;
                break;
            }
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memType;
        VK_CHECK(vkAllocateMemory(im.dev, &mai, nullptr, &im.hudStagingMem));
        VK_CHECK(vkBindBufferMemory(im.dev, im.hudStaging, im.hudStagingMem, 0));
        // 初始Backdrop（半透明黑）填入 staging buffer + 標記 dirty，避免首帧上傳未初始化 garbage
        void* hm = nullptr;
        VK_CHECK(vkMapMemory(im.dev, im.hudStagingMem, 0, im.hudPixels.size(), 0, &hm));
        if (hm) {
            uint8_t* p = (uint8_t*) hm;
            for (size_t i = 0; i < im.hudPixels.size(); i += 4)
                p[i + 3] = 90;
            vkUnmapMemory(im.dev, im.hudStagingMem);
        }
        im.hudDirty = true;
    }

    fprintf(stderr, "[screen-fg] present 就緒（%ux%u @ %u Hz）\n", im.w, im.h, im.displayHz_);
}

void Presenter::presentFrame(const CapturedFrame& f) {
    auto& im = *impl_;
    VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(im.dev, 1, &im.fence));

    uint32_t idx = 0;
    VK_CHECK(vkAcquireNextImageKHR(im.dev, im.swap, 100000000, im.acquireSem, VK_NULL_HANDLE, &idx));

    // 持久 pool / buffer（fence 已於函式頭 wait → 可安全 reset）
    VkCommandBuffer cmdb = im.cmdBuf;
    VK_CHECK(vkResetCommandBuffer(cmdb, 0));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmdb, &cbi));

    bool ok = false;
    if (f.data && f.width > 0 && f.height > 0) {
        ensureFrameStaging(im);
        // 上幀留下的 staging layout 是 SRC → 先切回 DST
        VkImageSubresourceRange rr{};
        rr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rr.levelCount = 1;
        rr.layerCount = 1;
        if (im.frameStageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            VkImageMemoryBarrier rb{};
            rb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            rb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            rb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            rb.image = im.frameStageImg;
            rb.subresourceRange = rr;
            vkCmdPipelineBarrier(cmdb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &rb);
            im.frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        // CPU 上傳：最近鄰把 f（f.width×f.height）縮放到 staging（im.w×im.h）
        size_t rowBytes = (size_t) im.w * 4;
        void* m = nullptr;
        VK_CHECK(vkMapMemory(im.dev, im.frameStageBufMem, 0, rowBytes * im.h, 0, &m));
        if (m) {
            uint8_t* dst = (uint8_t*) m;
            const uint32_t sw = f.width, sh = f.height;
            for (uint32_t y = 0; y < im.h; ++y) {
                uint32_t sy = (uint32_t)(((uint64_t) y * sh) / im.h);
                if (sy >= sh) sy = sh - 1;
                const uint8_t* srcRow = f.data + (size_t) sy * (size_t) sw * 4;
                uint8_t* drow = dst + (size_t) y * rowBytes;
                for (uint32_t x = 0; x < im.w; ++x) {
                    uint32_t sx = (uint32_t)(((uint64_t) x * sw) / im.w);
                    if (sx >= sw) sx = sw - 1;
                    memcpy(drow + (size_t) x * 4, srcRow + (size_t) sx * 4, 4);
                }
            }
            vkUnmapMemory(im.dev, im.frameStageBufMem);
            // buffer → staging image
            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0; // 0 = tightly packed
            copy.bufferImageHeight = im.h;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {0, 0, 0};
            copy.imageExtent = {im.w, im.h, 1};
            vkCmdCopyBufferToImage(cmdb, im.frameStageBuf, im.frameStageImg,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            // staging layout：DST → SRC（供 blit）
            VkImageMemoryBarrier ib{};
            ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            ib.image = im.frameStageImg;
            ib.subresourceRange = rr;
            vkCmdPipelineBarrier(cmdb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &ib);
            im.frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            // 1:1 blit staging → swapchain（兩者皆 im.w×im.h）
            VkImageSubresourceLayers sub{};
            sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            sub.baseArrayLayer = 0;
            sub.layerCount = 1;
            VkImageBlit blit{};
            blit.srcSubresource = sub;
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(int32_t) im.w, (int32_t) im.h, 1};
            blit.dstSubresource = sub;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(int32_t) im.w, (int32_t) im.h, 1};
            vkCmdBlitImage(cmdb, im.frameStageImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            im.swapImages[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, &blit, VK_FILTER_NEAREST);
            ok = true;
        }
    }

    if (ok && im.hudOn && im.hudImage != VK_NULL_HANDLE) {
        // setHudText 寫入 staging buffer 後，在此上傳到 GPU image（與後續 blit 同一 cmd buffer → 順序保證）
        if (im.hudDirty) {
            VkBufferImageCopy hcopy{};
            hcopy.bufferOffset = 0;
            hcopy.bufferRowLength = 0; // 0 = tightly packed
            hcopy.bufferImageHeight = kHudH;
            hcopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            hcopy.imageSubresource.mipLevel = 0;
            hcopy.imageSubresource.baseArrayLayer = 0;
            hcopy.imageSubresource.layerCount = 1;
            hcopy.imageOffset = {0, 0, 0};
            hcopy.imageExtent = {kHudW, kHudH, 1};
            vkCmdCopyBufferToImage(cmdb, im.hudStaging, im.hudImage,
                                   VK_IMAGE_LAYOUT_GENERAL, 1, &hcopy);
            im.hudDirty = false;
        }
        VkImageSubresourceLayers hs{};
        hs.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        hs.baseArrayLayer = 0;
        hs.layerCount = 1;
        VkImageBlit hblit{};
        hblit.srcSubresource = hs;
        hblit.srcOffsets[0] = {0, 0, 0};
        hblit.srcOffsets[1] = {kHudW, kHudH, 1};
        hblit.dstSubresource = hs;
        hblit.dstOffsets[0] = {(int32_t) im.w - kHudW - 20, (int32_t) im.h - kHudH - 20, 0};
        hblit.dstOffsets[1] = {(int32_t) im.w - 20, (int32_t) im.h - 20, 1};
        vkCmdBlitImage(cmdb, im.hudImage, VK_IMAGE_LAYOUT_GENERAL,
                        im.swapImages[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, &hblit, VK_FILTER_LINEAR);
    }

    VK_CHECK(vkEndCommandBuffer(cmdb));

    if (!ok) {
        // 無可呈現幀：空 submit（消耗 acquireSem 避免下帧 double-signal）讓 fence 照樣 signal
        VkPipelineStageFlags wst = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.pWaitSemaphores = &im.acquireSem;
        si.waitSemaphoreCount = 1;
        si.pWaitDstStageMask = &wst;
        VK_CHECK(vkQueueSubmit(im.queue, 1, &si, im.fence));
        VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
        return;
    }

    VkPipelineStageFlags wstage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pWaitSemaphores = &im.acquireSem;
    si.waitSemaphoreCount = 1;
    si.pWaitDstStageMask = &wstage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmdb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &im.readySem;
    VK_CHECK(vkQueueSubmit(im.queue, 1, &si, im.fence));

    // present（只 wait readySem；acquireSem 已被 submit 消耗，不可再 wait → 死鎖）
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &im.readySem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &im.swap;
    uint32_t i2 = idx;
    pi.pImageIndices = &i2;
    VK_CHECK(vkQueuePresentKHR(im.queue, &pi));

    // 等 submit 完成
    VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
}

void Presenter::setHudText(const std::string& text) {
    auto& im = *impl_;
    if (!im.hudOn || im.hudStaging == VK_NULL_HANDLE || text == im.lastHudText)
        return;
    im.lastHudText = text;
    std::fill(im.hudPixels.begin(), im.hudPixels.end(), 0);
    for (size_t i = 0; i < im.hudPixels.size(); i += 4)
        im.hudPixels[i + 3] = 90; // 半透明黑底
    int scale = kScale;
    int x0 = 6, y0 = 6;
    for (size_t i = 0; i < text.size(); ++i) {
        int fi = fontIndex(text[i]);
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c) {
                if (kFont[fi].rows[r][c] != '#')
                    continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = x0 + (int) i * 6 * scale + c * scale + sx;
                        int py = y0 + r * scale + sy;
                        if (px >= kHudW || py >= kHudH)
                            continue;
                        size_t off = ((size_t) py * kHudW + px) * 4;
                        im.hudPixels[off] = 255;
                        im.hudPixels[off + 1] = 255;
                        im.hudPixels[off + 2] = 255;
                        im.hudPixels[off + 3] = 255;
                    }
            }
    }
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(im.dev, im.hudStagingMem, 0, im.hudPixels.size(), 0, &mapped));
    memcpy(mapped, im.hudPixels.data(), im.hudPixels.size());
    vkUnmapMemory(im.dev, im.hudStagingMem);
    im.hudDirty = true; // 文字已寫入 staging buffer，等下次 present 上傳到 GPU image
}

void Presenter::setHud(bool on) {
    auto& im = *impl_;
    im.hudOn = on;
}

void Presenter::shutdown() {
    auto& im = *impl_;
    if (im.dev) {
        vkDeviceWaitIdle(im.dev);
        if (im.frameStageImg)
            vkDestroyImage(im.dev, im.frameStageImg, nullptr);
        if (im.frameStageImgMem)
            vkFreeMemory(im.dev, im.frameStageImgMem, nullptr);
        if (im.frameStageBuf)
            vkDestroyBuffer(im.dev, im.frameStageBuf, nullptr);
        if (im.frameStageBufMem)
            vkFreeMemory(im.dev, im.frameStageBufMem, nullptr);
        if (im.hudStaging)
            vkDestroyBuffer(im.dev, im.hudStaging, nullptr);
        if (im.hudStagingMem)
            vkFreeMemory(im.dev, im.hudStagingMem, nullptr);
        if (im.hudImage)
            vkDestroyImage(im.dev, im.hudImage, nullptr);
        if (im.hudImageMem)
            vkFreeMemory(im.dev, im.hudImageMem, nullptr);
        if (im.hudView)
            vkDestroyImageView(im.dev, im.hudView, nullptr);
        for (auto v : im.swapViews)
            vkDestroyImageView(im.dev, v, nullptr);
        if (im.swap)
            vkDestroySwapchainKHR(im.dev, im.swap, nullptr);
        if (im.acquireSem)
            vkDestroySemaphore(im.dev, im.acquireSem, nullptr);
        if (im.readySem)
            vkDestroySemaphore(im.dev, im.readySem, nullptr);
        if (im.fence)
            vkDestroyFence(im.dev, im.fence, nullptr);
        if (im.cmdBuf)
            vkFreeCommandBuffers(im.dev, im.cmdPool, 1, &im.cmdBuf);
        if (im.cmdPool)
            vkDestroyCommandPool(im.dev, im.cmdPool, nullptr);
        vkDestroyDevice(im.dev, nullptr);
        im.dev = VK_NULL_HANDLE;
    }
    if (im.inst) {
        vkDestroyInstance(im.inst, nullptr);
        im.inst = VK_NULL_HANDLE;
    }
    if (im.window) {
        SDL_DestroyWindow(im.window);
        im.window = nullptr;
        im.window = nullptr;
    }
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

} // namespace screenfg
