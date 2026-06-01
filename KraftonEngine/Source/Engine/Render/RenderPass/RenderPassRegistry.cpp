#include "RenderPassRegistry.h"

#include <algorithm>

FRenderPassRegistry& FRenderPassRegistry::Get()
{
	static FRenderPassRegistry Instance;
	return Instance;
}

void FRenderPassRegistry::Register(FRenderPassFactory Factory)
{
	Factories.push_back(std::move(Factory));
}

TArray<std::unique_ptr<FRenderPassBase>> FRenderPassRegistry::CreateAll() const
{
	TArray<std::unique_ptr<FRenderPassBase>> Passes;
	Passes.reserve(Factories.size());

	for (const auto& Factory : Factories)
	{
		Passes.push_back(Factory());
	}

	// Most passes follow enum order. Outline is a selection overlay, so keep its
	// serialized enum value stable while executing it after DOF.
	auto PassOrder = [](ERenderPass Pass) -> uint32
	{
		if (Pass == ERenderPass::Outline)
		{
			return static_cast<uint32>(ERenderPass::DepthOfField) + 1;
		}

		uint32 Order = static_cast<uint32>(Pass);
		if (Order > static_cast<uint32>(ERenderPass::DepthOfField)
			&& Order < static_cast<uint32>(ERenderPass::Outline))
		{
			++Order;
		}
		return Order;
	};

	std::sort(Passes.begin(), Passes.end(),
		[PassOrder](const std::unique_ptr<FRenderPassBase>& A, const std::unique_ptr<FRenderPassBase>& B)
		{
			return PassOrder(A->GetPassType()) < PassOrder(B->GetPassType());
		});

	return Passes;
}
