#include "lazy100/gpu/present.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/console/window.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"

#include <vri/vri.h>
#include <vri/integration/vri_sdl3.h>

#include <algorithm>
#include <cstring>

#include "shaders/present_spv.h" // g_presentSpv (vertexMain + fragmentMain)

namespace lazy100
{
    namespace
    {
        constexpr u32 kStagingRing = 2; // index-texture staging buffers cycled across frames

        void VRI_CALL message_callback(void*, VriMessageSeverity severity, const char* message)
        {
            switch (severity)
            {
                case VriMessageSeverity_Error: LZ_ERROR("[VRI] %s", message); break;
                case VriMessageSeverity_Warning: LZ_WARN("[VRI] %s", message); break;
                default: LZ_INFO("[VRI] %s", message); break;
            }
        }

        // Integer-scaled, centered letterbox rect for a 320x240 image inside a w*h window.
        struct Rect
        {
            i32 x, y;
            u32 w, h;
        };
        Rect letterbox(u32 winW, u32 winH)
        {
            const u32 sx    = winW / kScreenW;
            const u32 sy    = winH / kScreenH;
            const u32 scale = std::max(1u, std::min(sx, sy));
            const u32 w     = kScreenW * scale;
            const u32 h     = kScreenH * scale;
            return {static_cast<i32>((winW - w) / 2), static_cast<i32>((winH - h) / 2), w, h};
        }
    } // namespace

    struct Present::Impl
    {
        Window*               window = nullptr;
        VriDevice*            dev    = nullptr;
        VriCoreInterface      c {};
        VriSwapChainInterface swap {};
        VriQueue*             queue     = nullptr;
        VriSwapChain*         swapchain = nullptr;
        VriCommandAllocator*  alloc     = nullptr;
        VriCommandBuffer*     cmd       = nullptr;
        VriFence*             fence     = nullptr;
        VriFormat             swapFormat  = VriFormat_BGRA8_UNORM;
        u32                   w = 0, h = 0;
        u64                   frameValue = 0;

        // Indexed framebuffer -> texture
        VriFormat      indexFormat = VriFormat_R8_UINT;
        VriTexture*    indexTex    = nullptr;
        VriDescriptor* indexView   = nullptr;
        bool           indexInit   = false;
        VriBuffer*     indexStaging[kStagingRing] = {};

        // Palette uniform (host-visible, written when dirty)
        VriBuffer*     paletteBuf  = nullptr;
        VriDescriptor* paletteView = nullptr;

        // Present pipeline + its descriptor set
        VriPipelineLayout* layout   = nullptr;
        VriPipeline*       pipeline = nullptr;
        VriDescriptorPool* descPool = nullptr;
        VriDescriptorSet*  descSet  = nullptr;

        void barrier_tex(VriTexture*           tex,
                         VriAccessFlags        ba,
                         VriLayout             bl,
                         VriPipelineStageFlags bs,
                         VriAccessFlags        aa,
                         VriLayout             al,
                         VriPipelineStageFlags as)
        {
            VriTextureBarrierDesc b {};
            b.texture       = tex;
            b.before.access = ba;
            b.before.layout = bl;
            b.before.stages = bs;
            b.after.access  = aa;
            b.after.layout  = al;
            b.after.stages  = as;
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
        p_        = std::make_unique<Impl>();
        Impl& im  = *p_;
        im.window = &window;
        window.drawable_size(im.w, im.h);
        if (im.w == 0 || im.h == 0)
        {
            LZ_ERROR("window has zero drawable size at init");
            return false;
        }

        // ---- device + interfaces + swapchain ----
        VriDeviceCreationDesc dd {};
        dd.graphicsAPI      = VriGraphicsAPI_Auto;
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
        scd.format      = im.swapFormat;
        scd.width       = im.w;
        scd.height      = im.h;
        scd.textureNum  = 3;
        scd.presentMode = VriPresentMode_Fifo;
        if (im.swap.CreateSwapChain(im.dev, &scd, &im.swapchain) != VriResult_Success)
        {
            LZ_ERROR("CreateSwapChain failed");
            return false;
        }

        im.c.CreateCommandAllocator(im.dev, VriQueueType_Graphics, &im.alloc);
        im.c.CreateCommandBuffer(im.alloc, &im.cmd);
        im.c.CreateFence(im.dev, 0, &im.fence);

        // ---- index texture (R8_UINT, sampled) ----
        if (!(im.c.GetFormatSupport(im.dev, VriFormat_R8_UINT) & VriFormatSupport_Texture))
        {
            // Universally supported on desktop Vulkan; the UNORM fallback would need a
            // separate float-sampling shader, deferred until a backend actually needs it.
            LZ_WARN("R8_UINT not reported as a sampleable texture format; proceeding anyway");
        }
        VriTextureDesc itd {};
        itd.type           = VriTextureType_2D;
        itd.format         = im.indexFormat;
        itd.width          = kScreenW;
        itd.height         = kScreenH;
        itd.depth          = 1;
        itd.mipNum         = 1;
        itd.layerNum       = 1;
        itd.sampleNum      = 1;
        itd.usage          = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
        itd.memoryLocation = VriMemoryLocation_Device;
        if (im.c.CreateTexture(im.dev, &itd, &im.indexTex) != VriResult_Success)
        {
            LZ_ERROR("CreateTexture (index) failed");
            return false;
        }
        VriTextureViewDesc itv {};
        itv.texture  = im.indexTex;
        itv.viewType = VriTextureViewType_2D;
        itv.format   = VriFormat_Unknown;
        itv.aspect   = VriImageAspect_Color;
        im.c.CreateTextureView(im.dev, &itv, &im.indexView);

        for (u32 i = 0; i < kStagingRing; ++i)
        {
            VriBufferDesc sbd {};
            sbd.size           = static_cast<u64>(kScreenW) * kScreenH;
            sbd.usage          = VriBufferUsage_TransferSrc;
            sbd.memoryLocation = VriMemoryLocation_HostUpload;
            im.c.CreateBuffer(im.dev, &sbd, &im.indexStaging[i]);
        }

        // ---- palette uniform (host-visible) ----
        VriBufferDesc pbd {};
        pbd.size           = Palette::size() * sizeof(u32); // 256 entries -> 1024 bytes
        pbd.usage          = VriBufferUsage_ConstantBuffer;
        pbd.memoryLocation = VriMemoryLocation_HostUpload;
        if (im.c.CreateBuffer(im.dev, &pbd, &im.paletteBuf) != VriResult_Success)
        {
            LZ_ERROR("CreateBuffer (palette) failed");
            return false;
        }
        VriBufferViewDesc pbv {};
        pbv.buffer   = im.paletteBuf;
        pbv.viewType = VriDescriptorType_ConstantBuffer;
        pbv.offset   = 0;
        pbv.size     = pbd.size;
        im.c.CreateBufferView(im.dev, &pbv, &im.paletteView);

        // ---- pipeline layout (b0 palette, t1 index; both fragment) ----
        VriDescriptorRangeDesc ranges[2] {};
        ranges[0].baseRegister   = 0;
        ranges[0].descriptorNum  = 1;
        ranges[0].descriptorType = VriDescriptorType_ConstantBuffer;
        ranges[0].shaderStages   = VriShaderStage_Fragment;
        ranges[1].baseRegister   = 1;
        ranges[1].descriptorNum  = 1;
        ranges[1].descriptorType = VriDescriptorType_Texture;
        ranges[1].shaderStages   = VriShaderStage_Fragment;
        VriDescriptorSetDesc setDesc {};
        setDesc.registerSpace = 0;
        setDesc.ranges        = ranges;
        setDesc.rangeNum      = 2;
        VriPipelineLayoutDesc ld {};
        ld.descriptorSets   = &setDesc;
        ld.descriptorSetNum = 1;
        if (im.c.CreatePipelineLayout(im.dev, &ld, &im.layout) != VriResult_Success)
        {
            LZ_ERROR("CreatePipelineLayout failed");
            return false;
        }

        // ---- graphics pipeline (no vertex input; full-screen triangle) ----
        VriShaderDesc sh[2] {};
        sh[0].stage          = VriShaderStage_Vertex;
        sh[0].entryPointName = "vertexMain";
        sh[0].bytecode       = g_presentSpv;
        sh[0].bytecodeSize   = sizeof(g_presentSpv);
        sh[1].stage          = VriShaderStage_Fragment;
        sh[1].entryPointName = "fragmentMain";
        sh[1].bytecode       = g_presentSpv;
        sh[1].bytecodeSize   = sizeof(g_presentSpv);
        VriColorAttachmentDesc ca {};
        ca.format         = im.swapFormat;
        ca.colorWriteMask = VriColorWrite_RGBA;
        VriGraphicsPipelineDesc pd {};
        pd.pipelineLayout          = im.layout;
        pd.shaders                 = sh;
        pd.shaderNum               = 2;
        pd.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
        pd.rasterization.cullMode  = VriCullMode_None;
        pd.rasterization.lineWidth = 1.0f;
        pd.multisample.sampleNum   = 1;
        pd.outputMerger.colors     = &ca;
        pd.outputMerger.colorNum   = 1;
        if (im.c.CreateGraphicsPipeline(im.dev, &pd, &im.pipeline) != VriResult_Success)
        {
            LZ_ERROR("CreateGraphicsPipeline failed");
            return false;
        }

        // ---- descriptor pool + set (bound once; resources keep identity) ----
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum  = 1;
        pool.constantBufferMaxNum = 1;
        pool.textureMaxNum        = 1;
        im.c.CreateDescriptorPool(im.dev, &pool, &im.descPool);
        im.c.AllocateDescriptorSets(im.descPool, im.layout, 0, &im.descSet, 1);
        const VriDescriptor*         dPal[1] = {im.paletteView};
        const VriDescriptor*         dIdx[1] = {im.indexView};
        VriDescriptorRangeUpdateDesc upd[2] {};
        upd[0].descriptors   = dPal;
        upd[0].descriptorNum = 1;
        upd[1].descriptors   = dIdx;
        upd[1].descriptorNum = 1;
        im.c.UpdateDescriptorRanges(im.descSet, 0, 2, upd);

        const VriDeviceDesc* desc = im.c.GetDeviceDesc(im.dev);
        LZ_INFO("present ready (graphicsAPI=%d), %ux%u virtual -> %ux%u window",
                static_cast<int>(desc->graphicsAPI), kScreenW, kScreenH, im.w, im.h);
        return true;
    }

    void Present::submit_frame(const Framebuffer& fb, Palette& palette)
    {
        Impl& im = *p_;

        // Keep the swapchain matched to the window.
        u32 cw = 0, ch = 0;
        im.window->drawable_size(cw, ch);
        if (cw == 0 || ch == 0)
            return; // minimized
        if (cw != im.w || ch != im.h)
        {
            im.swap.Resize(im.swapchain, cw, ch);
            im.w = cw;
            im.h = ch;
        }

        // Palette: write the host-visible uniform only when it changed.
        if (palette.dirty())
        {
            void* dst = im.c.MapBuffer(im.paletteBuf, 0, Palette::size() * sizeof(u32));
            std::memcpy(dst, palette.packed(), Palette::size() * sizeof(u32));
            im.c.UnmapBuffer(im.paletteBuf);
            palette.clear_dirty();
        }

        // Framebuffer -> staging (cycled).
        VriBuffer* staging = im.indexStaging[im.frameValue % kStagingRing];
        void*      sptr    = im.c.MapBuffer(staging, 0, Framebuffer::pixel_count());
        std::memcpy(sptr, fb.pixels(), Framebuffer::pixel_count());
        im.c.UnmapBuffer(staging);

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

        // Upload the index texture.
        im.barrier_tex(im.indexTex,
                       im.indexInit ? VriAccess_ShaderResourceRead : VriAccess_None,
                       im.indexInit ? VriLayout_ShaderResource : VriLayout_Undefined,
                       im.indexInit ? VriPipelineStage_FragmentShader : VriPipelineStage_None,
                       VriAccess_CopyDestinationWrite,
                       VriLayout_CopyDestination,
                       VriPipelineStage_Transfer);
        VriBufferTextureCopyDesc copy {};
        copy.texture.aspect   = VriImageAspect_Color;
        copy.texture.layerNum = 1;
        copy.texture.width    = kScreenW;
        copy.texture.height   = kScreenH;
        im.c.CmdUploadBufferToTexture(im.cmd, im.indexTex, staging, &copy);
        im.barrier_tex(im.indexTex,
                       VriAccess_CopyDestinationWrite,
                       VriLayout_CopyDestination,
                       VriPipelineStage_Transfer,
                       VriAccess_ShaderResourceRead,
                       VriLayout_ShaderResource,
                       VriPipelineStage_FragmentShader);
        im.indexInit = true;

        // Backbuffer -> color attachment.
        im.barrier_tex(backbuffer,
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
        colorRT.clearValue.color.f32[0] = 0.0f; // black letterbox bars
        colorRT.clearValue.color.f32[1] = 0.0f;
        colorRT.clearValue.color.f32[2] = 0.0f;
        colorRT.clearValue.color.f32[3] = 1.0f;
        VriAttachmentsDesc att {};
        att.colors            = &colorRT;
        att.colorNum          = 1;
        att.renderArea.width  = im.w;
        att.renderArea.height = im.h;
        att.layerNum          = 1;
        im.c.CmdBeginRendering(im.cmd, &att);

        const Rect  lb = letterbox(im.w, im.h);
        VriViewport vp {static_cast<float>(lb.x),
                        static_cast<float>(lb.y),
                        static_cast<float>(lb.w),
                        static_cast<float>(lb.h),
                        0.0f,
                        1.0f};
        im.c.CmdSetViewports(im.cmd, &vp, 1);
        VriRect scis {lb.x, lb.y, lb.w, lb.h};
        im.c.CmdSetScissors(im.cmd, &scis, 1);

        im.c.CmdSetPipelineLayout(im.cmd, im.layout);
        im.c.CmdSetPipeline(im.cmd, im.pipeline);
        im.c.CmdSetDescriptorSet(im.cmd, 0, im.descSet);
        VriDrawDesc draw {};
        draw.vertexNum   = 3;
        draw.instanceNum = 1;
        im.c.CmdDraw(im.cmd, &draw);

        im.c.CmdEndRendering(im.cmd);

        im.barrier_tex(backbuffer,
                       VriAccess_ColorAttachmentWrite,
                       VriLayout_ColorAttachment,
                       VriPipelineStage_ColorAttachmentOutput,
                       VriAccess_None,
                       VriLayout_Present,
                       VriPipelineStage_AllCommands);
        im.c.EndCommandBuffer(im.cmd);

        VriFenceSubmitDesc sig {};
        sig.fence  = im.fence;
        sig.value  = ++im.frameValue;
        sig.stages = VriPipelineStage_AllCommands;
        VriQueueSubmitDesc sub {};
        sub.commandBuffers   = &im.cmd;
        sub.commandBufferNum = 1;
        sub.signalFences     = &sig;
        sub.signalFenceNum   = 1;
        im.c.QueueSubmit(im.queue, &sub);
        im.c.Wait(im.fence, im.frameValue);
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
        if (im.pipeline)
            im.c.DestroyPipeline(im.pipeline);
        if (im.layout)
            im.c.DestroyPipelineLayout(im.layout);
        if (im.descPool)
            im.c.DestroyDescriptorPool(im.descPool);
        if (im.paletteView)
            im.c.DestroyDescriptor(im.paletteView);
        if (im.paletteBuf)
            im.c.DestroyBuffer(im.paletteBuf);
        for (u32 i = 0; i < kStagingRing; ++i)
            if (im.indexStaging[i])
                im.c.DestroyBuffer(im.indexStaging[i]);
        if (im.indexView)
            im.c.DestroyDescriptor(im.indexView);
        if (im.indexTex)
            im.c.DestroyTexture(im.indexTex);
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
