#include <doctest/doctest.h>
#if defined(_WIN32) && defined(FNK_RHI_WEBGPU)
#include <webgpu/webgpu_cpp.h>
#include "Graphics/SkinningCompute.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <iostream>
#include <Windows.h>

namespace {
    std::string ShaderSource(const char* name) {
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        std::ifstream stream(root / "Funke-Engine/src/Graphics/Shader.cpp");
        std::ostringstream text; text << stream.rdbuf();
        const std::string code = text.str();
        auto start = code.find(std::string("const char* ") + name);
        REQUIRE(start != std::string::npos);
        start = code.find("R\"WGSL(", start) + 7;
        const auto end = code.find(")WGSL\"", start);
        REQUIRE(end != std::string::npos);
        return code.substr(start, end - start);
    }
    void Wait(const wgpu::Instance& instance, wgpu::Future future) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (instance.WaitAny(future, 0) != wgpu::WaitStatus::Success) {
            REQUIRE(std::chrono::steady_clock::now() < end);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    struct MaterialGpu {
        std::vector<std::string> Errors;
        wgpu::Instance Instance;
        wgpu::Device Device;
        MaterialGpu() {
            SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            REQUIRE(LoadLibraryW(L"d3dcompiler_47.dll"));
            Instance = wgpu::CreateInstance();
            REQUIRE(Instance);
            wgpu::RequestAdapterOptions options{};
            options.backendType = wgpu::BackendType::D3D12;
            wgpu::Adapter adapter;
            Wait(Instance, Instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
                [&](wgpu::RequestAdapterStatus status, wgpu::Adapter value, wgpu::StringView) {
                    if (status == wgpu::RequestAdapterStatus::Success) adapter = std::move(value);
                }));
            REQUIRE(adapter);
            wgpu::AdapterInfo info{};
            REQUIRE(adapter.GetInfo(&info) == wgpu::Status::Success);
            REQUIRE((info.adapterType == wgpu::AdapterType::DiscreteGPU || info.adapterType == wgpu::AdapterType::IntegratedGPU));
            std::cout << "Material GPU: " << std::string(info.device.data, info.device.length) << '\n';
            wgpu::DeviceDescriptor descriptor{};
            descriptor.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message, MaterialGpu* self) {
                self->Errors.emplace_back(message.data, message.length);
            }, this);
            Wait(Instance, adapter.RequestDevice(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
                [&](wgpu::RequestDeviceStatus status, wgpu::Device value, wgpu::StringView) {
                    if (status == wgpu::RequestDeviceStatus::Success) Device = std::move(value);
                }));
            REQUIRE(Device);
        }
        wgpu::ShaderModule Compile(const std::string& code) {
            wgpu::ShaderSourceWGSL source{}; source.code = code.c_str();
            wgpu::ShaderModuleDescriptor descriptor{}; descriptor.nextInChain = &source;
            auto module = Device.CreateShaderModule(&descriptor);
            Wait(Instance, module.GetCompilationInfo(wgpu::CallbackMode::WaitAnyOnly,
                [&](wgpu::CompilationInfoRequestStatus status, const wgpu::CompilationInfo* info) {
                    CHECK(status == wgpu::CompilationInfoRequestStatus::Success);
                    if (!info) return;
                    for (size_t i = 0; i < info->messageCount; ++i) {
                        if (info->messages[i].type == wgpu::CompilationMessageType::Error)
                            Errors.emplace_back(info->messages[i].message.data, info->messages[i].message.length);
                    }
                }));
            for (const auto& error : Errors) CHECK_MESSAGE(false, error);
            REQUIRE(Errors.empty());
            return module;
        }
        wgpu::RenderPipeline Pipeline(const wgpu::ShaderModule& module, bool shadow) {
            wgpu::VertexAttribute vertex[4]{};
            const wgpu::VertexFormat formats[] = {wgpu::VertexFormat::Float32x3, wgpu::VertexFormat::Float32x3,
                wgpu::VertexFormat::Float32x2, wgpu::VertexFormat::Float32x4};
            const uint64_t offsets[] = {0,12,24,32};
            for (uint32_t i=0; i<4; ++i) { vertex[i].format=formats[i]; vertex[i].offset=offsets[i]; vertex[i].shaderLocation=i==3?14:i; }
            wgpu::VertexAttribute instance[11]{};
            for (uint32_t i=0; i<11; ++i) { instance[i].format=wgpu::VertexFormat::Float32x4; instance[i].offset=i*16; instance[i].shaderLocation=i+3; }
            wgpu::VertexBufferLayout streams[2]{};
            streams[0].arrayStride=sizeof(Funke::MeshVertex); streams[0].attributeCount=4; streams[0].attributes=vertex;
            streams[1].arrayStride=176; streams[1].stepMode=wgpu::VertexStepMode::Instance; streams[1].attributeCount=11; streams[1].attributes=instance;
            wgpu::ColorTargetState target{}; target.format=wgpu::TextureFormat::RGBA8Unorm;
            wgpu::FragmentState fragment{}; fragment.module=module; fragment.entryPoint="fs_main";
            fragment.targetCount=shadow?0:1; fragment.targets=shadow?nullptr:&target;
            wgpu::DepthStencilState depth{}; depth.format=wgpu::TextureFormat::Depth32Float; depth.depthWriteEnabled=true; depth.depthCompare=wgpu::CompareFunction::Less;
            wgpu::RenderPipelineDescriptor desc{};
            desc.vertex.module=module; desc.vertex.entryPoint="vs_main"; desc.vertex.bufferCount=2; desc.vertex.buffers=streams;
            desc.fragment=&fragment; desc.depthStencil=&depth;
            auto pipeline=Device.CreateRenderPipeline(&desc);
            for (const auto& error : Errors) CHECK_MESSAGE(false, error);
            REQUIRE(Errors.empty());
            REQUIRE(pipeline);
            return pipeline;
        }
        wgpu::Buffer Buffer(const void* bytes, uint64_t size, wgpu::BufferUsage usage) {
            wgpu::BufferDescriptor desc{}; desc.size=size; desc.usage=usage|wgpu::BufferUsage::CopyDst;
            auto result=Device.CreateBuffer(&desc);
            if (bytes) Device.GetQueue().WriteBuffer(result, 0, bytes, size);
            return result;
        }
        wgpu::Texture Texture(wgpu::TextureFormat format, wgpu::TextureUsage usage, uint32_t size=1) {
            wgpu::TextureDescriptor desc{}; desc.size={size,size,1}; desc.format=format; desc.usage=usage;
            return Device.CreateTexture(&desc);
        }
        // Exercise the actual built-in shader with a full-screen triangle and read back color/depth.
        std::pair<uint8_t,float> Draw(const wgpu::RenderPipeline& pipeline, bool shadow, uint8_t alpha,
                                    float surface, float tintAlpha=1, bool lit=false,
                                    std::array<uint8_t,4> normal={128,128,255,255}, float normalScale=0,
                                    std::array<uint8_t,4> mr={255,255,255,255}, float metallic=0, int tangentMode=1) {
            const float frame[76] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
                1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
                0,0,3,1, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
                1,1,0,-1, 0,0,0,1, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,3,0};
            float lights[1024]{}; lights[6]=-1; lights[8]=lights[9]=lights[10]=1;
            auto frameBuffer=Buffer(frame, shadow?64:sizeof(frame), wgpu::BufferUsage::Uniform);
            auto lightBuffer=Buffer(lights, sizeof(lights), wgpu::BufferUsage::Uniform);
            auto dummyShadow=Texture(wgpu::TextureFormat::Depth32Float, wgpu::TextureUsage::TextureBinding);
            wgpu::SamplerDescriptor comparison{}; comparison.compare=wgpu::CompareFunction::Less;
            wgpu::BindGroupEntry frameEntries[4]{};
            frameEntries[0].binding=0; frameEntries[0].buffer=frameBuffer; frameEntries[0].size=shadow?64:sizeof(frame);
            frameEntries[1].binding=1; frameEntries[1].buffer=lightBuffer; frameEntries[1].size=sizeof(lights);
            frameEntries[2].binding=2; frameEntries[2].textureView=dummyShadow.CreateView();
            frameEntries[3].binding=3; frameEntries[3].sampler=Device.CreateSampler(&comparison);
            wgpu::BindGroupDescriptor groupDesc{};
            groupDesc.layout=pipeline.GetBindGroupLayout(0); groupDesc.entryCount=shadow?1:4; groupDesc.entries=frameEntries;
            auto frameGroup=Device.CreateBindGroup(&groupDesc);
            wgpu::Texture maps[5];
            const std::array<uint8_t,4> pixels[5]={{255,255,255,alpha},normal,mr,{255,255,255,255},{255,255,255,255}};
            auto sampler=Device.CreateSampler();
            wgpu::BindGroupEntry entries[11]{};
            for (uint32_t i=0;i<5;++i) {
                maps[i]=Texture(wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopyDst);
                wgpu::TexelCopyTextureInfo destination{}; destination.texture=maps[i];
                wgpu::TexelCopyBufferLayout layout{}; layout.bytesPerRow=4; layout.rowsPerImage=1;
                const wgpu::Extent3D size{1,1,1};
                Device.GetQueue().WriteTexture(&destination,pixels[i].data(),4,&layout,&size);
                entries[2*i].binding=2*i; entries[2*i].textureView=maps[i].CreateView();
                entries[2*i+1].binding=2*i+1; entries[2*i+1].sampler=sampler;
            }
            const float parameters[8]={0.5f,normalScale,1,0, 0,0,0,0};
            auto parametersBuffer=Buffer(parameters,sizeof(parameters),wgpu::BufferUsage::Uniform);
            entries[10].binding=10; entries[10].buffer=parametersBuffer; entries[10].size=sizeof(parameters);
            const wgpu::BindGroupEntry shadowEntries[]={entries[0],entries[1],entries[10]};
            groupDesc.layout=pipeline.GetBindGroupLayout(1); groupDesc.entryCount=shadow?3:11; groupDesc.entries=shadow?shadowEntries:entries;
            auto materialGroup=Device.CreateBindGroup(&groupDesc);
            Funke::MeshVertex vertices[3]{};
            const float positions[3][2]={{-1,-1},{3,-1},{-1,3}};
            for (size_t i=0;i<3;++i) {
                vertices[i].Position[0]=positions[i][0]; vertices[i].Position[1]=positions[i][1]; vertices[i].Position[2]=0.5f;
                vertices[i].Normal[2]=1; vertices[i].Tangent[0]=vertices[i].Tangent[3]=1;
                vertices[i].UV[0]=vertices[i].UV[1]=0.5f;
                if(tangentMode!=1){
                    vertices[i].Tangent[0]=vertices[i].Tangent[3]=0;
                    if(tangentMode!=2){
                        vertices[i].UV[0]=(positions[i][0]+1)*0.5f*(tangentMode<0?-1.0f:1.0f);
                        vertices[i].UV[1]=(positions[i][1]+1)*0.5f;
                    }
                }
            }
            float instance[44]{};
            instance[0]=instance[5]=instance[10]=instance[15]=1;
            instance[16]=instance[21]=instance[26]=1;
            instance[28]=instance[29]=instance[30]=1; instance[31]=tintAlpha;
            instance[36]=metallic; instance[37]=1; instance[38]=lit?1.0f:0.0f; instance[39]=surface;
            instance[40]=instance[41]=1;
            auto vb=Buffer(vertices,sizeof(vertices),wgpu::BufferUsage::Vertex);
            auto ib=Buffer(instance,sizeof(instance),wgpu::BufferUsage::Vertex);
            auto color=Texture(wgpu::TextureFormat::RGBA8Unorm,wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::CopySrc,4);
            auto depth=Texture(wgpu::TextureFormat::Depth32Float,wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::CopySrc,4);
            auto colorRead=Buffer(nullptr,1024,wgpu::BufferUsage::MapRead);
            auto depthRead=Buffer(nullptr,1024,wgpu::BufferUsage::MapRead);
            wgpu::RenderPassColorAttachment attachment{};
            attachment.view=color.CreateView(); attachment.loadOp=wgpu::LoadOp::Clear; attachment.storeOp=wgpu::StoreOp::Store;
            wgpu::RenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view=depth.CreateView(); depthAttachment.depthLoadOp=wgpu::LoadOp::Clear;
            depthAttachment.depthStoreOp=wgpu::StoreOp::Store; depthAttachment.depthClearValue=1;
            wgpu::RenderPassDescriptor passDesc{};
            passDesc.colorAttachmentCount=shadow?0:1; passDesc.colorAttachments=shadow?nullptr:&attachment;
            passDesc.depthStencilAttachment=&depthAttachment;
            auto encoder=Device.CreateCommandEncoder(); auto pass=encoder.BeginRenderPass(&passDesc);
            pass.SetPipeline(pipeline); pass.SetBindGroup(0,frameGroup); pass.SetBindGroup(1,materialGroup);
            pass.SetVertexBuffer(0,vb); pass.SetVertexBuffer(1,ib); pass.Draw(3); pass.End();
            const wgpu::Extent3D extent{4,4,1};
            auto copy=[&](wgpu::Texture texture,wgpu::Buffer buffer,bool isDepth) {
                wgpu::TexelCopyTextureInfo source{}; source.texture=texture;
                source.aspect=isDepth?wgpu::TextureAspect::DepthOnly:wgpu::TextureAspect::All;
                wgpu::TexelCopyBufferInfo target{}; target.buffer=buffer; target.layout.bytesPerRow=256; target.layout.rowsPerImage=4;
                encoder.CopyTextureToBuffer(&source,&target,&extent);
            };
            if (!shadow) copy(color,colorRead,false);
            copy(depth,depthRead,true);
            auto command=encoder.Finish(); Device.GetQueue().Submit(1,&command);
            auto map=[&](wgpu::Buffer buffer) {
                wgpu::MapAsyncStatus result=wgpu::MapAsyncStatus::Error;
                Wait(Instance,buffer.MapAsync(wgpu::MapMode::Read,0,1024,wgpu::CallbackMode::WaitAnyOnly,
                    [&](wgpu::MapAsyncStatus status,wgpu::StringView) { result=status; }));
                REQUIRE(result==wgpu::MapAsyncStatus::Success);
            };
            uint8_t pixel=0;
            if (!shadow) { map(colorRead); pixel=static_cast<const uint8_t*>(colorRead.GetConstMappedRange())[0]; colorRead.Unmap(); }
            map(depthRead); const float z=static_cast<const float*>(depthRead.GetConstMappedRange())[0]; depthRead.Unmap();
            for (const auto& error:Errors) CHECK_MESSAGE(false,error);
            REQUIRE(Errors.empty());
            return {pixel,z};
        }
    };
}

TEST_CASE("Standard cutout agrees in color and shadow depth and normal maps change lighting"
    * doctest::skip(std::getenv("FUNKE_TEST_MATERIAL_GPU") == nullptr)) {
    MaterialGpu gpu;
    const auto mesh=gpu.Pipeline(gpu.Compile(ShaderSource("k_MeshWGSL")),false);
    const auto shadow=gpu.Pipeline(gpu.Compile(ShaderSource("k_Shadow3DWGSL")),true);
    for (uint8_t alpha : {uint8_t(64),uint8_t(192)}) {
        const auto color=gpu.Draw(mesh,false,alpha,2);
        const auto depth=gpu.Draw(shadow,true,alpha,2);
        CHECK(color.first==(alpha<128?0:255));
        CHECK(color.second==doctest::Approx(alpha<128?1.0f:0.5f));
        CHECK(depth.second==color.second);
    }
    CHECK(gpu.Draw(mesh,false,0,0).first==255); // Opaque still ignores texture alpha.
    CHECK(gpu.Draw(mesh,false,192,2,0.5f).second==1); // Renderer/material tint also controls coverage.
    CHECK(gpu.Draw(shadow,true,192,2,0.5f).second==1);
    const auto geometric=gpu.Draw(mesh,false,255,0,1,true);
    const auto neutral=gpu.Draw(mesh,false,255,0,1,true,{128,128,255,255},1);
    const auto tilted=gpu.Draw(mesh,false,255,0,1,true,{255,128,128,255},1);
    CHECK(std::abs(int(geometric.first)-int(neutral.first))<=2);
    CHECK(tilted.first+20<geometric.first);
    for(int mode:{0,-1}){
        const auto fallback=gpu.Draw(mesh,false,255,0,1,true,{255,128,128,255},1,{255,255,255,255},0,mode);
        CHECK(fallback.first+20<geometric.first);
    }
    const auto degenerate=gpu.Draw(mesh,false,255,0,1,true,{255,128,128,255},1,{255,255,255,255},0,2);
    CHECK(degenerate.first==geometric.first);
    const auto dielectric=gpu.Draw(mesh,false,255,0,1,true,{128,128,255,255},0,{255,255,0,255},1);
    const auto metal=gpu.Draw(mesh,false,255,0,1,true,{128,128,255,255},0,{255,255,255,255},1);
    CHECK(dielectric.first!=metal.first);
    const auto smooth=gpu.Draw(mesh,false,255,0,1,true,{128,128,255,255},0,{255,16,255,255},1);
    CHECK(smooth.first!=metal.first);
}

TEST_CASE("Standard material WGSL and cutout shadow pipelines compile on D3D12"
    * doctest::skip(std::getenv("FUNKE_TEST_MATERIAL_GPU") == nullptr)) {
    MaterialGpu gpu;
    gpu.Pipeline(gpu.Compile(ShaderSource("k_MeshWGSL")), false);
    gpu.Pipeline(gpu.Compile(ShaderSource("k_Shadow3DWGSL")), true);
    auto skin = gpu.Compile(Funke::SkinningCompute::k_Shader);
    wgpu::ComputePipelineDescriptor descriptor{};
    descriptor.compute.module=skin; descriptor.compute.entryPoint="skin";
    auto pipeline=gpu.Device.CreateComputePipeline(&descriptor);
    REQUIRE(pipeline);
    Funke::SkinnedMeshData data;
    Funke::MeshVertex vertex{};
    vertex.Position[0]=1; vertex.Position[1]=2; vertex.Position[2]=-3;
    vertex.Normal[2]=1; vertex.Tangent[0]=1; vertex.Tangent[3]=-1;
    data.BindVertices.push_back(vertex); data.VertexJoints.push_back({0,0,0,0}); data.VertexWeights.push_back({1,0,0,0});
    std::vector<Funke::SkinningCompute::Vertex> packed;
    REQUIRE(Funke::SkinningCompute::PackVertices(data,packed));
    auto source=gpu.Buffer(packed.data(),sizeof(packed[0]),wgpu::BufferUsage::Storage);
    const auto matrix=glm::scale(glm::mat4(1),glm::vec3(-2,0.5f,3));
    auto joints=gpu.Buffer(&matrix,sizeof(matrix),wgpu::BufferUsage::Storage);
    auto output=gpu.Buffer(nullptr,sizeof(vertex),wgpu::BufferUsage::Storage|wgpu::BufferUsage::CopySrc);
    auto read=gpu.Buffer(nullptr,sizeof(vertex),wgpu::BufferUsage::MapRead);
    wgpu::BindGroupEntry entries[3]{};
    const wgpu::Buffer buffers[]={source,joints,output};
    const uint64_t sizes[]={sizeof(packed[0]),sizeof(matrix),sizeof(vertex)};
    for(uint32_t i=0;i<3;++i){entries[i].binding=i;entries[i].buffer=buffers[i];entries[i].size=sizes[i];}
    wgpu::BindGroupDescriptor group{};group.layout=pipeline.GetBindGroupLayout(0);group.entryCount=3;group.entries=entries;
    auto binding=gpu.Device.CreateBindGroup(&group);
    auto encoder=gpu.Device.CreateCommandEncoder();auto pass=encoder.BeginComputePass();
    pass.SetPipeline(pipeline);pass.SetBindGroup(0,binding);pass.DispatchWorkgroups(1);pass.End();
    encoder.CopyBufferToBuffer(output,0,read,0,sizeof(vertex));
    auto command=encoder.Finish();gpu.Device.GetQueue().Submit(1,&command);
    wgpu::MapAsyncStatus mapStatus=wgpu::MapAsyncStatus::Error;
    Wait(gpu.Instance,read.MapAsync(wgpu::MapMode::Read,0,sizeof(vertex),wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::MapAsyncStatus status,wgpu::StringView){mapStatus=status;}));
    REQUIRE(mapStatus==wgpu::MapAsyncStatus::Success);
    Funke::MeshVertex actual;std::memcpy(&actual,read.GetConstMappedRange(),sizeof(actual));read.Unmap();
    std::vector<Funke::MeshVertex> expected;
    Funke::SkeletalAnimation::SkinVertices(data,{matrix},expected);
    for(int axis=0;axis<3;++axis){
        CHECK(actual.Position[axis]==doctest::Approx(expected[0].Position[axis]));
        CHECK(actual.Normal[axis]==doctest::Approx(expected[0].Normal[axis]));
    }
    for(int axis=0;axis<4;++axis) CHECK(actual.Tangent[axis]==doctest::Approx(expected[0].Tangent[axis]));
    for (const auto& error : gpu.Errors) CHECK_MESSAGE(false, error);
    CHECK(gpu.Errors.empty());
}
#endif
