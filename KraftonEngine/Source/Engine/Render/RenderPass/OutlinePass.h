#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FOutlinePass final : public FRenderPassBase
{
public:
	FOutlinePass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
