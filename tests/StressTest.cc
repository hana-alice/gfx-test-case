#include "StressTest.h"
#include "gfx-vulkan/GFXVulkan.h"

#define VK_NO_PROTOTYPES
#include "gfx-vulkan/VKGPUObjects.h"

namespace cc {

/* *
constexpr uint PASS_COUNT = 1;
constexpr uint MODELS_PER_LINE[PASS_COUNT] = {10};
/* */
constexpr uint PASS_COUNT = 4;
//constexpr uint MODELS_PER_LINE[PASS_COUNT] = {50, 1, 5, 50};
//constexpr uint MODELS_PER_LINE[PASS_COUNT] = {100, 100, 100, 100};
constexpr uint MODELS_PER_LINE[PASS_COUNT] = {150, 1, 2, 3};
/* *
constexpr uint PASS_COUNT = 9;
constexpr uint MODELS_PER_LINE[PASS_COUNT] = {100, 2, 100, 100, 3, 4, 100, 5, 100};
/* */
constexpr uint MAIN_THREAD_SLEEP = 15;
constexpr float QUAD_SIZE = .01f;

const gfx::Color StressTest::clearColors[] = {
    {.2f, .2f, .2f, 1.f},
    {1.f, 0.f, 0.f, 1.f},
    {0.f, 1.f, 0.f, 1.f},
    {.4f, .4f, .4f, 1.f},
    {0.f, 0.f, 1.f, 1.f},
    {1.f, 1.f, 0.f, 1.f},
    {.8f, .8f, .8f, 1.f},
    {1.f, 0.f, 1.f, 1.f},
    {0.f, 1.f, 1.f, 1.f},
};

#define PARALLEL_STRATEGY_SEQUENTIAL         0 // complete sequential recording & submission
#define PARALLEL_STRATEGY_RP_BASED_PRIMARY   1 // render pass level concurrency
#define PARALLEL_STRATEGY_RP_BASED_SECONDARY 2 // render pass level concurrency with the use of secondary command buffers, which completely sequentializes the submission process
#define PARALLEL_STRATEGY_DC_BASED           3 // draw call level concurrency: current endgame milestone

#define PARALLEL_STRATEGY 2

#define USE_DYNAMIC_UNIFORM_BUFFER 1
#define USE_PARALLEL_RECORDING     1

uint8_t const taskCount = std::thread::hardware_concurrency() - 1;

void HSV2RGB(const float h, const float s, const float v, float &r, float &g, float &b) {
    int hi = (int)(h / 60.0f) % 6;
    float f = (h / 60.0f) - hi;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    switch (hi) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }
}

void StressTest::destroy() {
    CC_SAFE_DESTROY(_vertexBuffer);
    CC_SAFE_DESTROY(_inputAssembler);

#if USE_DYNAMIC_UNIFORM_BUFFER
    CC_SAFE_DESTROY(_uniDescriptorSet);
    CC_SAFE_DESTROY(_uniWorldBufferView);
    CC_SAFE_DESTROY(_uniWorldBuffer);
#else
    for (uint i = 0u; i < _descriptorSets.size(); i++) {
        CC_SAFE_DESTROY(_descriptorSets[i]);
    }
    _descriptorSets.clear();

    for (uint i = 0u; i < _worldBuffers.size(); i++) {
        CC_SAFE_DESTROY(_worldBuffers[i]);
    }
    _worldBuffers.clear();
#endif

    CC_SAFE_DESTROY(_uniformBufferVP);
    CC_SAFE_DESTROY(_shader);
    CC_SAFE_DESTROY(_descriptorSetLayout);
    CC_SAFE_DESTROY(_pipelineLayout);
    CC_SAFE_DESTROY(_pipelineState);

    size_t i = PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_PRIMARY ? 1u : 0u;
    for (; i < _parallelCBs.size(); ++i) {
        CC_SAFE_DESTROY(_parallelCBs[i]);
    }
    _parallelCBs.clear();
}

bool StressTest::initialize() {

    createShader();
    createVertexBuffer();
    createInputAssembler();
    createPipeline();

    _threadCount = JobSystem::getInstance()->threadCount() + 1; // main thread counts too
    gfx::CommandBufferInfo info;
    info.queue = _device->getQueue();
    uint cbCount = 0u;

#if PARALLEL_STRATEGY == PARALLEL_STRATEGY_DC_BASED
    cbCount = _threadCount;
    info.type = gfx::CommandBufferType::SECONDARY;
#elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_SECONDARY
    cbCount = PASS_COUNT;
    info.type = gfx::CommandBufferType::SECONDARY;
#elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_PRIMARY
    cbCount = PASS_COUNT;
    info.type = gfx::CommandBufferType::PRIMARY;
    _parallelCBs.push_back(_commandBuffers[0]);
#endif

    for (uint i = _parallelCBs.size(); i < cbCount; ++i) {
        _parallelCBs.push_back(_device->createCommandBuffer(info));
    }

    return true;
}

void StressTest::createShader() {

    ShaderSources sources;
    sources.glsl4 = {
        R"(
            precision mediump float;
            layout(location = 0) in vec2 a_position;
            layout(set = 0, binding = 0) uniform ViewProj { mat4 u_viewProj; vec4 u_color; };
            layout(set = 0, binding = 1) uniform World { vec4 u_world; };

            void main() {
                gl_Position = u_viewProj * vec4(a_position + u_world.xy, 0.0, 1.0);
            }
        )",
        R"(
            precision mediump float;
            layout(set = 0, binding = 0) uniform ViewProj { mat4 u_viewProj; vec4 u_color; };
            layout(location = 0) out vec4 o_color;

            void main() {
                o_color = u_color;
            }
        )",
    };

    sources.glsl3 = {
        R"(
            precision mediump float;
            in vec2 a_position;
            layout(std140) uniform ViewProj { mat4 u_viewProj; vec4 u_color; };
            layout(std140) uniform World { vec4 u_world; };

            void main() {
                gl_Position = u_viewProj * vec4(a_position + u_world.xy, 0.0, 1.0);
            }
        )",
        R"(
            precision mediump float;
            layout(std140) uniform ViewProj { mat4 u_viewProj; vec4 u_color; };

            out vec4 o_color;
            void main() {
                o_color = u_color;
            }
        )",
    };

    sources.glsl1 = {
        R"(
            precision mediump float;
            attribute vec2 a_position;
            uniform mat4 u_viewProj;
            uniform vec4 u_world;

            void main() {
                gl_Position = u_viewProj * vec4(a_position + u_world.xy, 0.0, 1.0);
            }
        )",
        R"(
            precision mediump float;
            uniform vec4 u_color;

            void main() {
                gl_FragColor = u_color;
            }
        )",
    };

    ShaderSource &source = TestBaseI::getAppropriateShaderSource(sources);

    gfx::ShaderStageList shaderStageList;
    gfx::ShaderStage vertexShaderStage;
    vertexShaderStage.stage = gfx::ShaderStageFlagBit::VERTEX;
    vertexShaderStage.source = source.vert;
    shaderStageList.emplace_back(std::move(vertexShaderStage));

    gfx::ShaderStage fragmentShaderStage;
    fragmentShaderStage.stage = gfx::ShaderStageFlagBit::FRAGMENT;
    fragmentShaderStage.source = source.frag;
    shaderStageList.emplace_back(std::move(fragmentShaderStage));

    gfx::UniformBlockList uniformBlockList = {
        {0, 0, "ViewProj", {{"u_viewProj", gfx::Type::MAT4, 1}, {"u_color", gfx::Type::FLOAT4, 1}}, 1},
        {0, 1, "World", {{"u_world", gfx::Type::FLOAT4, 1}}, 1},
    };
    gfx::AttributeList attributeList = {{"a_position", gfx::Format::RG32F, false, 0, false, 0}};

    gfx::ShaderInfo shaderInfo;
    shaderInfo.name = "StressTest";
    shaderInfo.stages = std::move(shaderStageList);
    shaderInfo.attributes = std::move(attributeList);
    shaderInfo.blocks = std::move(uniformBlockList);
    _shader = _device->createShader(shaderInfo);
}

void StressTest::createVertexBuffer() {
    float y = 1.f - QUAD_SIZE;
    float vertexData[] = {-1.f, -y,
                          -1.f, -1.f,
                          -y, -y,
                          -y, -1.f};

    gfx::BufferInfo vertexBufferInfo = {
        gfx::BufferUsage::VERTEX,
        gfx::MemoryUsage::DEVICE,
        sizeof(vertexData),
        2 * sizeof(float),
    };

    _vertexBuffer = _device->createBuffer(vertexBufferInfo);
    _vertexBuffer->update(vertexData, sizeof(vertexData));

#if USE_DYNAMIC_UNIFORM_BUFFER
    _worldBufferStride = TestBaseI::getAlignedUBOStride(_device, sizeof(Vec4));
    gfx::BufferInfo uniformBufferWInfo = {
        gfx::BufferUsage::UNIFORM,
        gfx::MemoryUsage::DEVICE | gfx::MemoryUsage::HOST,
        TestBaseI::getUBOSize(_worldBufferStride * MODELS_PER_LINE[0] * MODELS_PER_LINE[0]),
        _worldBufferStride,
    };
    _uniWorldBuffer = _device->createBuffer(uniformBufferWInfo);

    uint stride = _worldBufferStride / sizeof(float);
    vector<float> buffer(stride * MODELS_PER_LINE[0] * MODELS_PER_LINE[0]);
    for (uint i = 0u, idx = 0u; i < MODELS_PER_LINE[0]; i++) {
        for (uint j = 0u; j < MODELS_PER_LINE[0]; j++, idx++) {
            buffer[idx * stride] = 2.f * j / MODELS_PER_LINE[0];
            buffer[idx * stride + 1] = 2.f * i / MODELS_PER_LINE[0];
        }
    }
    _uniWorldBuffer->update(buffer.data(), buffer.size() * sizeof(float));

    gfx::BufferViewInfo worldBufferViewInfo = {
        _uniWorldBuffer,
        0,
        sizeof(Vec4),
    };
    _uniWorldBufferView = _device->createBuffer(worldBufferViewInfo);
#else
    uint size = TestBaseI::getUBOSize(sizeof(Vec4));
    gfx::BufferInfo uniformBufferWInfo = {
        gfx::BufferUsage::UNIFORM,
        gfx::MemoryUsage::DEVICE | gfx::MemoryUsage::HOST,
        size, size};

    _worldBuffers.resize(MODELS_PER_LINE[0] * MODELS_PER_LINE[0]);
    vector<float> buffer(size / sizeof(float));
    for (uint i = 0u, idx = 0u; i < MODELS_PER_LINE[0]; i++) {
        for (uint j = 0u; j < MODELS_PER_LINE[0]; j++, idx++) {
            _worldBuffers[idx] = _device->createBuffer(uniformBufferWInfo);

            buffer[0] = 2.f * j / MODELS_PER_LINE[0];
            buffer[1] = 2.f * i / MODELS_PER_LINE[0];

            _worldBuffers[idx]->update(buffer.data(), size);
        }
    }
#endif

    gfx::BufferInfo uniformBufferVPInfo = {
        gfx::BufferUsage::UNIFORM,
        gfx::MemoryUsage::DEVICE | gfx::MemoryUsage::HOST,
        TestBaseI::getUBOSize(sizeof(Mat4) + sizeof(Vec4)),
    };
    _uniformBufferVP = _device->createBuffer(uniformBufferVPInfo);

    TestBaseI::createOrthographic(-1, 1, -1, 1, -1, 1, &_uboVP.matViewProj);
}

void StressTest::createInputAssembler() {
    gfx::Attribute position = {"a_position", gfx::Format::RG32F, false, 0, false};
    gfx::InputAssemblerInfo inputAssemblerInfo;
    inputAssemblerInfo.attributes.emplace_back(std::move(position));
    inputAssemblerInfo.vertexBuffers.emplace_back(_vertexBuffer);
    _inputAssembler = _device->createInputAssembler(inputAssemblerInfo);
}

void StressTest::createPipeline() {
    gfx::DescriptorSetLayoutInfo dslInfo;
    dslInfo.bindings.push_back({0, gfx::DescriptorType::UNIFORM_BUFFER, 1,
                                gfx::ShaderStageFlagBit::VERTEX | gfx::ShaderStageFlagBit::FRAGMENT});
    dslInfo.bindings.push_back({1, USE_DYNAMIC_UNIFORM_BUFFER ? gfx::DescriptorType::DYNAMIC_UNIFORM_BUFFER : gfx::DescriptorType::UNIFORM_BUFFER,
                                1, gfx::ShaderStageFlagBit::VERTEX});
    _descriptorSetLayout = _device->createDescriptorSetLayout(dslInfo);

    _pipelineLayout = _device->createPipelineLayout({{_descriptorSetLayout}});

#if USE_DYNAMIC_UNIFORM_BUFFER
    _uniDescriptorSet = _device->createDescriptorSet({_pipelineLayout});
    _uniDescriptorSet->bindBuffer(0, _uniformBufferVP);
    _uniDescriptorSet->bindBuffer(1, _uniWorldBufferView);
    _uniDescriptorSet->update();
#else
    _descriptorSets.resize(_worldBuffers.size());
    for (uint i = 0u; i < _worldBuffers.size(); ++i) {
        _descriptorSets[i] = _device->createDescriptorSet({_pipelineLayout});
        _descriptorSets[i]->bindBuffer(0, _uniformBufferVP);
        _descriptorSets[i]->bindBuffer(1, _worldBuffers[i]);
        _descriptorSets[i]->update();
    }
#endif

    gfx::PipelineStateInfo pipelineInfo;
    pipelineInfo.primitive = gfx::PrimitiveMode::TRIANGLE_STRIP;
    pipelineInfo.shader = _shader;
    pipelineInfo.rasterizerState.cullMode = gfx::CullMode::NONE;
    pipelineInfo.inputState = {_inputAssembler->getAttributes()};
    pipelineInfo.renderPass = _fbo->getRenderPass();
    pipelineInfo.pipelineLayout = _pipelineLayout;

    _pipelineState = _device->createPipelineState(pipelineInfo);
}

#if PARALLEL_STRATEGY == PARALLEL_STRATEGY_SEQUENTIAL
void StressTest::recordRenderPass(uint passIndex) {
    gfx::Rect renderArea = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::CommandBuffer *commandBuffer = _commandBuffers[0];

    commandBuffer->begin();
    for (uint i = 0u; i < PASS_COUNT; ++i) {
        commandBuffer->beginRenderPass(_fbo->getRenderPass(), _fbo, renderArea,
                                       &clearColors[i], 1.0f, 0);
        commandBuffer->bindInputAssembler(_inputAssembler);
        commandBuffer->bindPipelineState(_pipelineState);

        uint dynamicOffset = 0u;
        for (uint t = 0; t < MODELS_PER_LINE[i] * MODELS_PER_LINE[i]; ++t) {
    #if USE_DYNAMIC_UNIFORM_BUFFER
            dynamicOffset = _worldBufferStride * t;
            commandBuffer->bindDescriptorSet(0, _uniDescriptorSet, 1, &dynamicOffset);
    #else
            commandBuffer->bindDescriptorSet(0, _descriptorSets[t]);
    #endif
            commandBuffer->draw(_inputAssembler);
        }

        commandBuffer->endRenderPass();
    }
    commandBuffer->end();
}
#elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_DC_BASED
void StressTest::recordRenderPass(uint threadIndex) {
    gfx::Rect scissor = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::Viewport vp = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::CommandBuffer *commandBuffer = _parallelCBs[threadIndex];

    for (uint i = 0u; i < PASS_COUNT; ++i) {
        uint dynamicOffset = 0u;
        uint totalCount = MODELS_PER_LINE[i] * MODELS_PER_LINE[i];
        uint perThreadCount = totalCount / _threadCount;
        uint dcCount = perThreadCount;
        if (threadIndex == _threadCount - 1) dcCount += totalCount % _threadCount;

        //CC_LOG_INFO("---- idx %x cb %x id %x dc %d", threadIndex, commandBuffer,
        //            std::hash<std::thread::id>()(std::this_thread::get_id()), dcCount);

        commandBuffer->begin(_renderPass, 0, _fbo);
        if (dcCount) {
            commandBuffer->bindInputAssembler(_inputAssembler);
            commandBuffer->bindPipelineState(_pipelineState);
            commandBuffer->setScissor(scissor);
            commandBuffer->setViewport(vp);

            for (uint j = 0, t = perThreadCount * threadIndex; j < dcCount; ++j, ++t) {
    #if USE_DYNAMIC_UNIFORM_BUFFER
                dynamicOffset = _worldBufferStride * t;
                commandBuffer->bindDescriptorSet(0, _uniDescriptorSet, 1, &dynamicOffset);
    #else
                commandBuffer->bindDescriptorSet(0, _descriptorSets[t]);
    #endif
                commandBuffer->draw(_inputAssembler);
            }
        }
        commandBuffer->end();
    }
}
#elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_SECONDARY
void StressTest::recordRenderPass(uint passIndex) {
    gfx::Rect scissor = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::Viewport vp = {0, 0, _device->getWidth(), _device->getHeight()};

    gfx::CommandBuffer *commandBuffer = _parallelCBs[passIndex];

    commandBuffer->begin(_renderPass, 0, _fbo);
    commandBuffer->bindInputAssembler(_inputAssembler);
    commandBuffer->bindPipelineState(_pipelineState);
    commandBuffer->setScissor(scissor);
    commandBuffer->setViewport(vp);

    uint dynamicOffset = 0u;
    for (uint t = 0; t < MODELS_PER_LINE[passIndex] * MODELS_PER_LINE[passIndex]; ++t) {
    #if USE_DYNAMIC_UNIFORM_BUFFER
        dynamicOffset = _worldBufferStride * t;
        commandBuffer->bindDescriptorSet(0, _uniDescriptorSet, 1, &dynamicOffset);
    #else
        commandBuffer->bindDescriptorSet(0, _descriptorSets[t]);
    #endif
        commandBuffer->draw(_inputAssembler);
    }

    commandBuffer->end();
}
#elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_PRIMARY
void StressTest::recordRenderPass(uint passIndex) {
    gfx::Rect renderArea = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::CommandBuffer *commandBuffer = _parallelCBs[passIndex];

    commandBuffer->begin(passIndex);
    commandBuffer->beginRenderPass(_fbo->getRenderPass(), _fbo, renderArea,
                                   &clearColors[passIndex], 1.0f, 0);
    commandBuffer->bindInputAssembler(_inputAssembler);
    commandBuffer->bindPipelineState(_pipelineState);

    uint dynamicOffset = 0u;
    for (uint t = 0; t < MODELS_PER_LINE[passIndex] * MODELS_PER_LINE[passIndex]; ++t) {
    #if USE_DYNAMIC_UNIFORM_BUFFER
        dynamicOffset = _worldBufferStride * t;
        commandBuffer->bindDescriptorSet(0, _uniDescriptorSet, 1, &dynamicOffset);
    #else
        commandBuffer->bindDescriptorSet(0, _descriptorSets[t]);
    #endif
        commandBuffer->draw(_inputAssembler);
    }

    commandBuffer->endRenderPass();
    commandBuffer->end();
}
#endif

void StressTest::tick() {
    lookupTime();

    // simulate heavy logic operation
    std::this_thread::sleep_for(std::chrono::milliseconds(MAIN_THREAD_SLEEP));

    if (!hostThread.timeAcc) hostThread.timeAcc = hostThread.dt;
    else hostThread.timeAcc = hostThread.timeAcc * 0.95f + hostThread.dt * 0.05f;
    hostThread.frameAcc++;

//    if (hostThread.frameAcc % 6 == 0) {
        CC_LOG_INFO("Host thread n.%d avg: %.2fms (~%d FPS)", hostThread.frameAcc, hostThread.timeAcc * 1000.f, uint(1.f / hostThread.timeAcc + .5f));
//    }

    //gfx::CCVKDevice *actor = (gfx::CCVKDevice *)((gfx::DeviceAgent *)_device)->getRemote();
    //ENCODE_COMMAND_1(
    //    encoder,
    //    ReserveEvents,
    //    actor, actor,
    //    {
    //        for (int i = 0; i < PASS_COUNT; ++i)
    //            actor->gpuEventPool()->get(i);
    //    });

    _device->acquire();

    _uboVP.color.w = 1.f;
    HSV2RGB((hostThread.frameAcc * 20) % 360, .5f, 1.f, _uboVP.color.x, _uboVP.color.y, _uboVP.color.z);
    _uniformBufferVP->update(&_uboVP, sizeof(_uboVP));

    /* un-toggle this to support dynamic screen rotation *
    Mat4 VP;
    TestBaseI::createOrthographic(-1, 1, -1, 1, -1, 1, &VP);
    _uniformBufferVP->update(VP.m, 0, sizeof(Mat4));
    /* */

#if PARALLEL_STRATEGY == PARALLEL_STRATEGY_SEQUENTIAL
    recordRenderPass(0);
    _device->getQueue()->submit(_commandBuffers);
#else
    uint jobCount = PARALLEL_STRATEGY == PARALLEL_STRATEGY_DC_BASED ? _threadCount : PASS_COUNT;
    #if USE_PARALLEL_RECORDING
    {
        JobGraph g(JobSystem::getInstance());
        uint job = g.createForEachIndexJob(0u, jobCount - 1, 1u, [this](uint passIndex) {
            recordRenderPass(passIndex);
        });
        g.run(job);
        recordRenderPass(jobCount - 1);
        g.waitForAll();
    }
    #else
    for (uint t = 0u; t < jobCount; ++t) {
        recordRenderPass(t);
    }
    #endif

    #if PARALLEL_STRATEGY == PARALLEL_STRATEGY_DC_BASED
    gfx::Rect renderArea = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::CommandBuffer *commandBuffer = _commandBuffers[0];
    commandBuffer->begin();
    for (uint t = 0u; t < PASS_COUNT; ++t) {
        commandBuffer->beginRenderPass(_fbo->getRenderPass(), _fbo, renderArea,
                                       &clearColors[t], 1.0f, 0,
                                       _parallelCBs.data(), _threadCount);
        commandBuffer->execute(_parallelCBs.data(), _threadCount);
        commandBuffer->endRenderPass();
    }
    commandBuffer->end();
    _device->getQueue()->submit(_commandBuffers);
    #elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_SECONDARY
    gfx::Rect renderArea = {0, 0, _device->getWidth(), _device->getHeight()};
    gfx::CommandBuffer *commandBuffer = _commandBuffers[0];
    commandBuffer->begin();
    for (uint t = 0u; t < PASS_COUNT; ++t) {
        commandBuffer->beginRenderPass(_fbo->getRenderPass(), _fbo, renderArea,
                                       &clearColors[t], 1.0f, 0,
                                       &_parallelCBs[t], 1);
        commandBuffer->execute(&_parallelCBs[t], 1);
        commandBuffer->endRenderPass();
    }
    commandBuffer->end();
    _device->getQueue()->submit(_commandBuffers);
    #elif PARALLEL_STRATEGY == PARALLEL_STRATEGY_RP_BASED_PRIMARY
    _device->getQueue()->submit(_parallelCBs);
    #endif
#endif

    _device->present();
    
    CommandEncoder *encoder = ((gfx::DeviceAgent *)_device)->getMainEncoder();
    ENCODE_COMMAND_0(
        encoder,
        DeviceStatistics,
        {
            lookupTime(deviceThread);
            if (!deviceThread.timeAcc) deviceThread.timeAcc = deviceThread.dt;
            else deviceThread.timeAcc = deviceThread.timeAcc * 0.95f + deviceThread.dt * 0.05f;
            deviceThread.frameAcc++;
//            if (deviceThread.frameAcc % 6 == 0) {
                CC_LOG_INFO("Device thread n.%d avg: %.2fms (~%d FPS)", deviceThread.frameAcc, deviceThread.timeAcc * 1000.f, uint(1.f / deviceThread.timeAcc + .5f));
//            }
        });
}

} // namespace cc
