#pragma once

#include "../TestBase.h"
#include "base/Macros.h"
#include "gfx-base/GFXCommandBuffer.h"

namespace cc {

class FullscreenQuad : public cc::Object {
public:
    FullscreenQuad(gfx::Device *device, gfx::RenderPass *renderPass, gfx::Texture *texture);
    ~FullscreenQuad() override;
    DISABLE_COPY_SEMANTICS(FullscreenQuad)
    DISABLE_MOVE_SEMANTICS(FullscreenQuad)

    void draw(gfx::CommandBuffer *commandBuffer);

private:
    gfx::Shader *             _shader{nullptr};
    gfx::Buffer *             _vertexBuffer{nullptr};
    gfx::InputAssembler *     _inputAssembler{nullptr};
    gfx::DescriptorSetLayout *_descriptorSetLayout{nullptr};
    gfx::PipelineLayout *     _pipelineLayout{nullptr};
    gfx::PipelineState *      _pipelineState{nullptr};
    gfx::Sampler *            _sampler{nullptr};
    gfx::DescriptorSet *      _descriptorSet{nullptr};
};

} // namespace cc
