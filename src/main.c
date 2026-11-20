#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <webgpu/webgpu.h>

static SDL_Window *window = NULL;
static WGPUInstance instance = NULL;
static WGPUAdapter adapter = NULL;
static WGPUDevice device = NULL;
static WGPUQueue queue = NULL;
static WGPUSurface surface = NULL;
static WGPUSurfaceConfiguration config = { 0 };
static WGPURenderPipeline pipeline = NULL;

static bool is_configured = false;
static bool need_reconfigure = false;

static const int WIN_WIDTH = 280;
static const int WIN_HEIGHT = 280;

static const char *shader_code =
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {\n"
    "    var pos = array<vec2f, 3>(\n"
    "        vec2f(0.0, 0.5),\n"
    "        vec2f(-0.5, -0.5),\n"
    "        vec2f(0.5, -0.5)\n"
    "    );\n"
    "    return vec4f(pos[in_vertex_index], 0.0, 1.0);\n"
    "}\n\n"
    "@fragment\n"
    "fn fs_main() -> @location(0) vec4f {\n"
    "    return vec4f(1.0, 0.5, 0.0, 1.0); // Orange color\n"
    "}\n";

static inline WGPUStringView WGPU_STR(const char *s)
{
    WGPUStringView view = {
        .data = s,
        .length = (s != NULL) ? SDL_strlen(s) : WGPU_STRLEN
    };
    return view;
}

static WGPUSurface CreateWGPUSurface(WGPUInstance inst, SDL_Window *win)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    const char *driver = SDL_GetCurrentVideoDriver();

    if (driver && SDL_strcmp(driver, "wayland") == 0)
    {
        void *wlDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        void *wlSurface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);

        WGPUSurfaceSourceWaylandSurface waylandSource = {
            .chain = { .sType = WGPUSType_SurfaceSourceWaylandSurface },
            .display = wlDisplay,
            .surface = wlSurface
        };
        WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&waylandSource };
        return wgpuInstanceCreateSurface(inst, &desc);
    }
    else if (driver && SDL_strcmp(driver, "x11") == 0)
    {
        void *x11Display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        uint64_t x11Window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);

        WGPUSurfaceSourceXlibWindow x11Source = {
            .chain = { .sType = WGPUSType_SurfaceSourceXlibWindow },
            .display = x11Display,
            .window = x11Window
        };
        WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&x11Source };
        return wgpuInstanceCreateSurface(inst, &desc);
    }

    printf("Unsupported Linux SDL video driver: %s\n", driver ? driver : "(null)");
    return NULL;
}

static void handle_device_request(
    WGPURequestDeviceStatus status,
    WGPUDevice res,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata1;
    (void)userdata2;

    printf("DEVICE CALLBACK: status=%d\n", status);

    if (message.data && message.length > 0)
        printf("DEVICE MESSAGE: %.*s\n",
            (int)message.length,
            message.data);

    if (status == WGPURequestDeviceStatus_Success)
    {
        device = res;
        printf("DEVICE CREATED\n");
    }
    else
    {
        printf("DEVICE CREATION FAILED\n");
    }
}

static void handle_adapter_request(
    WGPURequestAdapterStatus status,
    WGPUAdapter res,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata1;
    (void)userdata2;

    printf("ADAPTER CALLBACK: status=%d\n", status);

    if (message.data && message.length > 0)
        printf("ADAPTER MESSAGE: %.*s\n",
            (int)message.length,
            message.data);

    if (status == WGPURequestAdapterStatus_Success)
    {
        adapter = res;
        printf("ADAPTER CREATED\n");

        WGPURequestDeviceCallbackInfo deviceCallbackInfo = {
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = handle_device_request
        };

        wgpuAdapterRequestDevice(adapter, NULL, deviceCallbackInfo);
    }
    else
    {
        printf("ADAPTER CREATION FAILED\n");
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    (void)argc;
    (void)argv;

    if (SDL_getenv("WSL_DISTRO_NAME") != NULL)
    {
        if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
        {
            SDL_Log("Failed to set a frame rate: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
        return SDL_APP_FAILURE;

    window = SDL_CreateWindow("WebGPU Triangle", WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!window)
        return SDL_APP_FAILURE;

    instance = wgpuCreateInstance(NULL);
    if (!instance)
        return SDL_APP_FAILURE;

    surface = CreateWGPUSurface(instance, window);
    if (!surface)
        return SDL_APP_FAILURE;

    WGPURequestAdapterOptions opt = { .compatibleSurface = surface };

    WGPURequestAdapterCallbackInfo adapterCallbackInfo = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = handle_adapter_request
    };

    wgpuInstanceRequestAdapter(instance, &opt, adapterCallbackInfo);

    while (adapter == NULL || device == NULL)
    {
        SDL_Delay(1);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;

    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_RESTORED)
    {
        need_reconfigure = true;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    (void)appstate;

    if (!device)
    {
        return SDL_APP_CONTINUE;
    }

    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(16);
        return SDL_APP_CONTINUE;
    }

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0)
    {
        SDL_Delay(16);
        return SDL_APP_CONTINUE;
    }

    if (!is_configured)
    {
        queue = wgpuDeviceGetQueue(device);

        // Dynamically query supported surface capabilities
        WGPUSurfaceCapabilities caps = { 0 };
        wgpuSurfaceGetCapabilities(surface, adapter, &caps);

        WGPUTextureFormat surface_format = WGPUTextureFormat_Undefined;

        for (size_t i = 0; i < caps.formatCount; ++i)
        {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm)
            {
                surface_format = caps.formats[i];
                break;
            }

            if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)
            {
                surface_format = caps.formats[i];
            }
        }

        if (surface_format == WGPUTextureFormat_Undefined)
        {
            surface_format = (caps.formatCount > 0) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
        }

        config.device = device;
        config.format = surface_format;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = (uint32_t)w;
        config.height = (uint32_t)h;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &config);

        wgpuSurfaceCapabilitiesFreeMembers(caps);

        WGPUShaderSourceWGSL wgsl_desc = { 0 };
        wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_desc.code = WGPU_STR(shader_code);

        WGPUShaderModuleDescriptor shader_desc = { 0 };
        shader_desc.nextInChain = &wgsl_desc.chain;
        WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule(device, &shader_desc);

        WGPURenderPipelineDescriptor pipeline_desc = { 0 };
        pipeline_desc.vertex.module = shader_module;
        pipeline_desc.vertex.entryPoint = WGPU_STR("vs_main");

        WGPUColorTargetState color_target = { .format = config.format, .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState fragment_state = {
            .module = shader_module,
            .entryPoint = WGPU_STR("fs_main"),
            .targetCount = 1,
            .targets = &color_target
        };
        pipeline_desc.fragment = &fragment_state;
        pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeline_desc.multisample.count = 1;
        pipeline_desc.multisample.mask = 0xFFFFFFFF;

        pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);
        wgpuShaderModuleRelease(shader_module);

        is_configured = true;
        need_reconfigure = false;
        printf("WebGPU Device/Queue ready and Surface configured.\n");
    }
    else if (need_reconfigure || config.width != (uint32_t)w || config.height != (uint32_t)h)
    {
        config.width = (uint32_t)w;
        config.height = (uint32_t)h;
        wgpuSurfaceConfigure(surface, &config);
        need_reconfigure = false;
    }

    WGPUSurfaceTexture surfaceTexture = { 0 };
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

    if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
        surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Lost)
    {
        if (surfaceTexture.texture)
        {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        need_reconfigure = true;
        SDL_Delay(1);
        return SDL_APP_CONTINUE;
    }

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        if (surfaceTexture.texture)
        {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        SDL_Delay(1);
        return SDL_APP_CONTINUE;
    }

    if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        need_reconfigure = true;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);

    WGPURenderPassColorAttachment colorAttachment = {
        .nextInChain = NULL,
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .resolveTarget = NULL,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0.2, 0.2, 0.2, 1.0 }
    };

    WGPURenderPassDescriptor renderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuSurfacePresent(surface);

    if (commandBuffer)
    {
        wgpuCommandBufferRelease(commandBuffer);
    }
    if (pass)
    {
        wgpuRenderPassEncoderRelease(pass);
    }
    if (encoder)
    {
        wgpuCommandEncoderRelease(encoder);
    }
    if (view)
    {
        wgpuTextureViewRelease(view);
    }
    if (surfaceTexture.texture)
    {
        wgpuTextureRelease(surfaceTexture.texture);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;

    if (pipeline)
        wgpuRenderPipelineRelease(pipeline);
    if (surface)
        wgpuSurfaceUnconfigure(surface);
    if (queue)
        wgpuQueueRelease(queue);
    if (device)
        wgpuDeviceRelease(device);
    if (adapter)
        wgpuAdapterRelease(adapter);
    if (surface)
        wgpuSurfaceRelease(surface);
    if (instance)
        wgpuInstanceRelease(instance);
    if (window)
        SDL_DestroyWindow(window);

    pipeline = NULL;
    queue = NULL;
    device = NULL;
    adapter = NULL;
    surface = NULL;
    instance = NULL;
    window = NULL;
    config = (WGPUSurfaceConfiguration) { 0 };
    is_configured = false;
    need_reconfigure = false;

    SDL_Quit();
}
