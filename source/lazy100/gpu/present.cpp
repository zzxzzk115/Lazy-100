#include "lazy100/gpu/present.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/window.hpp"

#include <vri/vri.h>
#include <vri/integration/vri_sdl3.h>

namespace lazy100
{
    namespace
    {
        // Route VRI's unified diagnostics (its validation layer + each backend's native
        // debug output) through the kernel log.
        void VRI_CALL message_callback(void*, VriMessageSeverity severity, const char* message)
        {
            switch (severity)
            {
                case VriMessageSeverity_Error: LZ_ERROR("[VRI] %s", message); break;
                case VriMessageSeverity_Warning: LZ_WARN("[VRI] %s", message); break;
                default: LZ_INFO("[VRI] %s", message); break;
            }
        }
    } // namespace

    struct Present::Impl
    {
        Window*               window = nullptr;
        VriDevice*            dev     = nullptr;
        VriCoreInterface      c {};
        VriSwapChainInterface swap {};
        VriQueue*             queue     = nullptr;
        VriSwapChain*         swapchain = nullptr;
        VriCommandAllocator*  alloc     = nullptr;
        VriCommandBuffer*     cmd       = nullptr;
        VriFence*             fence     = nullptr;
        VriFormat             format     = VriFormat_BGRA8_UNORM;
        u32                   w = 0, h = 0;
        u64                   frame_value = 0;

        // Single-barrier helper for the swapchain backbuffer.
        void barrier(VriTexture*           tex,
                     VriAccessFlags        beforeAccess,
                     VriLayout             beforeLayout,
                     VriPipelineStageFlags beforeStages,
                     VriAccessFlags        afterAccess,
                     VriLayout             afterLayout,
                     VriPipelineStageFlags afterStages)
        {
            VriTextureBarrierDesc b {};
            b.texture       = tex;
            b.before.access = beforeAccess;
            b.before.layout = beforeLayout;
            b.before.stages = beforeStages;
            b.after.access  = afterAccess;
            b.after.layout  = afterLayout;
            b.after.stages  = afterStages;
            b.aspect        = VriImageAspect_Color;
            VriBarrierGroupDesc g {};
            g.textures   = &b;
            g.textureNum = 1;
            c.CmdBarrier(cmd, &g);
        }
    };

    Present::Present()  = default;
    Present::~Present() { shutdown(); }

    bool Present::init(Window& window)
    {
        p_       = std::make_unique<Impl>();
        Impl& im = *p_;
        im.window = &window;
        window.drawable_size(im.w, im.h);
        if (im.w == 0 || im.h == 0)
        {
            LZ_ERROR("window has zero drawable size at init");
            return false;
        }

        VriDeviceCreationDesc dd {};
        dd.graphicsAPI      = VriGraphicsAPI_Auto; // Vulkan on this Windows box
        dd.enableValidation = VRI_TRUE;
        dd.bestEffort       = VRI_TRUE;
        static VriCallbackInterface cb {};
        cb.MessageCallback   = message_callback;
        dd.callbackInterface = &cb;
        if (vriCreateDevice(&dd, &im.dev) != VriResult_Success)
        {
            LZ_ERROR("vriCreateDevice failed");
            return false;
        }
        if (vriGetInterface(im.dev, VRI_INTERFACE_CORE, sizeof(im.c), &im.c) != VriResult_Success ||
            vriGetInterface(im.dev, VRI_INTERFACE_SWAPCHAIN, sizeof(im.swap), &im.swap) != VriResult_Success)
        {
            LZ_ERROR("vriGetInterface failed");
            return false;
        }

        im.c.GetQueue(im.dev, VriQueueType_Graphics, 0, &im.queue);

        VriWindowHandle  wh = vriWindowHandleFromSDL3(window.handle());
        VriSwapChainDesc scd {};
        scd.window      = wh;
        scd.queue       = im.queue;
        scd.format      = im.format;
        scd.width       = im.w;
        scd.height      = im.h;
        scd.textureNum  = 3;
        scd.presentMode = VriPresentMode_Fifo; // vsync: universally supported, warning-free
        if (im.swap.CreateSwapChain(im.dev, &scd, &im.swapchain) != VriResult_Success)
        {
            LZ_ERROR("CreateSwapChain failed");
            return false;
        }

        im.c.CreateCommandAllocator(im.dev, VriQueueType_Graphics, &im.alloc);
        im.c.CreateCommandBuffer(im.alloc, &im.cmd);
        im.c.CreateFence(im.dev, 0, &im.fence);

        const VriDeviceDesc* desc = im.c.GetDeviceDesc(im.dev);
        LZ_INFO("VRI device up (graphicsAPI=%d), swapchain %ux%u", static_cast<int>(desc->graphicsAPI), im.w, im.h);
        return true;
    }

    void Present::present_clear(float r, float g, float b, float a)
    {
        Impl& im = *p_;

        // Keep the swapchain matched to the window.
        u32 cw = 0, ch = 0;
        im.window->drawable_size(cw, ch);
        if (cw == 0 || ch == 0)
            return; // minimized: nothing to present
        if (cw != im.w || ch != im.h)
        {
            im.swap.Resize(im.swapchain, cw, ch);
            im.w = cw;
            im.h = ch;
        }

        u32 index = 0;
        if (im.swap.AcquireNextTexture(im.swapchain, nullptr, 0, &index) == VriResult_OutOfDate)
        {
            im.swap.Resize(im.swapchain, im.w, im.h);
            return;
        }

        VriTexture* backbuffers[8] = {};
        u32         count          = 8;
        im.swap.GetSwapChainTextures(im.swapchain, backbuffers, &count);
        VriTexture* backbuffer = backbuffers[index];

        VriTextureViewDesc bvd {};
        bvd.texture           = backbuffer;
        bvd.viewType          = VriTextureViewType_2D;
        bvd.format            = VriFormat_Unknown;
        bvd.aspect            = VriImageAspect_Color;
        VriDescriptor* bbView = nullptr;
        im.c.CreateTextureView(im.dev, &bvd, &bbView);

        im.c.ResetCommandAllocator(im.alloc);
        im.c.BeginCommandBuffer(im.cmd);

        im.barrier(backbuffer,
                   VriAccess_None,
                   VriLayout_Undefined,
                   VriPipelineStage_None,
                   VriAccess_ColorAttachmentWrite,
                   VriLayout_ColorAttachment,
                   VriPipelineStage_ColorAttachmentOutput);

        VriAttachmentDesc colorRT {};
        colorRT.view                    = bbView;
        colorRT.loadOp                  = VriAttachmentLoadOp_Clear;
        colorRT.storeOp                 = VriAttachmentStoreOp_Store;
        colorRT.clearValue.color.f32[0] = r;
        colorRT.clearValue.color.f32[1] = g;
        colorRT.clearValue.color.f32[2] = b;
        colorRT.clearValue.color.f32[3] = a;
        VriAttachmentsDesc att {};
        att.colors            = &colorRT;
        att.colorNum          = 1;
        att.renderArea.width  = im.w;
        att.renderArea.height = im.h;
        att.layerNum          = 1;
        im.c.CmdBeginRendering(im.cmd, &att);
        VriViewport vp {0.0f, 0.0f, static_cast<float>(im.w), static_cast<float>(im.h), 0.0f, 1.0f};
        im.c.CmdSetViewports(im.cmd, &vp, 1);
        VriRect scis {0, 0, im.w, im.h};
        im.c.CmdSetScissors(im.cmd, &scis, 1);
        im.c.CmdEndRendering(im.cmd);

        im.barrier(backbuffer,
                   VriAccess_ColorAttachmentWrite,
                   VriLayout_ColorAttachment,
                   VriPipelineStage_ColorAttachmentOutput,
                   VriAccess_None,
                   VriLayout_Present,
                   VriPipelineStage_AllCommands);
        im.c.EndCommandBuffer(im.cmd);

        VriFenceSubmitDesc sig {};
        sig.fence  = im.fence;
        sig.value  = ++im.frame_value;
        sig.stages = VriPipelineStage_AllCommands;
        VriQueueSubmitDesc sub {};
        sub.commandBuffers   = &im.cmd;
        sub.commandBufferNum = 1;
        sub.signalFences     = &sig;
        sub.signalFenceNum   = 1;
        im.c.QueueSubmit(im.queue, &sub);
        im.c.Wait(im.fence, im.frame_value);
        im.swap.Present(im.swapchain, nullptr, 0);
        im.c.DestroyDescriptor(bbView);
    }

    void Present::shutdown()
    {
        if (!p_)
            return;
        Impl& im = *p_;
        if (im.dev)
            im.c.DeviceWaitIdle(im.dev);
        if (im.fence)
            im.c.DestroyFence(im.fence);
        if (im.alloc)
            im.c.DestroyCommandAllocator(im.alloc);
        if (im.swapchain)
            im.swap.DestroySwapChain(im.swapchain);
        if (im.dev)
            vriDestroyDevice(im.dev);
        p_.reset();
    }
} // namespace lazy100
