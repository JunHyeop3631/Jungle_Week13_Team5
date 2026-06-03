#include "DepthOfFieldPass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Command/DrawCommandList.h"

REGISTER_RENDER_PASS(FDepthOfFieldPass)

namespace
{
	void DrawFullscreenTriangle(ID3D11DeviceContext* DC)
	{
		DC->IASetInputLayout(nullptr);
		DC->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		DC->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DC->Draw(3, 0);
	}
}

FDepthOfFieldPass::FDepthOfFieldPass()
{
	PassType = ERenderPass::DepthOfField;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
					ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDepthOfFieldPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	const bool bDofEnabled = Frame.RenderOptions.ShowFlags.bDepthOfField;
	const bool bVisualizeDof = Frame.RenderOptions.ShowFlags.bVisualizeDepthOfField;
	const bool bNeedsDofLayers = bDofEnabled && !bVisualizeDof;

	return (bDofEnabled || bVisualizeDof)
		&& Frame.CameraDepthOfField.bEnabled
		&& Frame.SceneColorCopyTexture
		&& Frame.SceneColorCopySRV
		&& Frame.ViewportRenderTexture
		&& Frame.DepthTexture
		&& Frame.DepthCopyTexture
		&& Frame.DepthCopySRV
		&& (!bNeedsDofLayers
			|| (Frame.DofFarRTV
				&& Frame.DofFarSRV
				&& Frame.DofNearRTV
				&& Frame.DofNearSRV
				&& Frame.DofWidth > 0.0f
				&& Frame.DofHeight > 0.0f));
}

void FDepthOfFieldPass::Execute(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	if (!DC)
	{
		return;
	}

	FShader* FarLayerShader = FShaderManager::Get().GetOrCreate(
		FShaderKey(EShaderPath::DepthOfField, nullptr, "VS", "PS_FarLayer"));
	FShader* NearLayerShader = FShaderManager::Get().GetOrCreate(
		FShaderKey(EShaderPath::DepthOfField, nullptr, "VS", "PS_NearLayer"));
	FShader* CompositeShader = FShaderManager::Get().GetOrCreate(
		FShaderKey(EShaderPath::DepthOfField, nullptr, "VS", "PS_Composite"));
	FShader* VisualizeShader = FShaderManager::Get().GetOrCreate(
		FShaderKey(EShaderPath::DepthOfField, nullptr, "VS", "PS_Visualize"));
	const bool bVisualizeDof = Frame.RenderOptions.ShowFlags.bVisualizeDepthOfField;
	if (!VisualizeShader || (!bVisualizeDof && (!FarLayerShader || !NearLayerShader || !CompositeShader)))
	{
		return;
	}

	if (!DepthOfFieldCB.GetBuffer())
	{
		DepthOfFieldCB.Create(Ctx.Device.GetDevice(), sizeof(FDepthOfFieldConstants), "DepthOfFieldCB");
	}

	FDepthOfFieldConstants DofData = {};
	DofData.FocusDistance = Frame.CameraDepthOfField.FocusDistance;
	DofData.FocalLength = Frame.CameraDepthOfField.FocalLength;
	DofData.FStop = Frame.CameraDepthOfField.FStop;
	DofData.MaxBlurSize = Frame.CameraDepthOfField.MaxBlurSize;
	DofData.NearZ = Frame.NearClip;
	DofData.FarZ = Frame.FarClip;
	DepthOfFieldCB.Update(DC, &DofData, sizeof(DofData));

	Ctx.Resources.SetDepthStencilState(Ctx.Device, EDepthStencilState::NoDepth);
	Ctx.Resources.SetBlendState(Ctx.Device, EBlendState::Opaque);
	Ctx.Resources.SetRasterizerState(Ctx.Device, ERasterizerState::SolidNoCull);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11ShaderResourceView* NullSystemSRVs[12] = {};
	ID3D11Buffer* DofCBRaw = DepthOfFieldCB.GetBuffer();
	DC->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &DofCBRaw);
	DC->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &DofCBRaw);

	D3D11_VIEWPORT DofViewport = {};
	DofViewport.Width = Frame.DofWidth;
	DofViewport.Height = Frame.DofHeight;
	DofViewport.MinDepth = 0.0f;
	DofViewport.MaxDepth = 1.0f;

	D3D11_VIEWPORT SceneViewport = {};
	SceneViewport.Width = Frame.ViewportWidth;
	SceneViewport.Height = Frame.ViewportHeight;
	SceneViewport.MinDepth = 0.0f;
	SceneViewport.MaxDepth = 1.0f;

	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(Frame.DepthCopyTexture, Frame.DepthTexture);
	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);

	if (bVisualizeDof)
	{
		DC->RSSetViewports(1, &SceneViewport);
		DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);
		DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &Frame.SceneColorCopySRV);
		DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &Frame.DepthCopySRV);
		VisualizeShader->Bind(DC);
		DrawFullscreenTriangle(DC);

		DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
		DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
		DC->PSSetShaderResources(0, ARRAYSIZE(NullSystemSRVs), NullSystemSRVs);
		ID3D11Buffer* NullCB = nullptr;
		DC->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &NullCB);
		DC->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &NullCB);

		Ctx.Cache.bForceAll = true;
		return;
	}

	// Far layer: half-res blurred background into Bloom A.
	DC->RSSetViewports(1, &DofViewport);
	DC->OMSetRenderTargets(1, &Frame.DofFarRTV, nullptr);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &Frame.SceneColorCopySRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &Frame.DepthCopySRV);
	FarLayerShader->Bind(DC);
	DrawFullscreenTriangle(DC);

	// Near layer: half-res foreground veil into Bloom B.
	DC->OMSetRenderTargets(1, &Frame.DofNearRTV, nullptr);
	NearLayerShader->Bind(DC);
	DrawFullscreenTriangle(DC);

	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);

	// Composite full-res scene + far layer + near layer back into the viewport RT.
	DC->RSSetViewports(1, &SceneViewport);
	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &Frame.SceneColorCopySRV);
	DC->PSSetShaderResources(ESystemTexSlot::DofFar, 1, &Frame.DofFarSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DofNear, 1, &Frame.DofNearSRV);
	CompositeShader->Bind(DC);
	DrawFullscreenTriangle(DC);

	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DofFar, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DofNear, 1, &NullSRV);
	DC->PSSetShaderResources(0, ARRAYSIZE(NullSystemSRVs), NullSystemSRVs);
	ID3D11Buffer* NullCB = nullptr;
	DC->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &NullCB);
	DC->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &NullCB);

	Ctx.Cache.bForceAll = true;
}

void FDepthOfFieldPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DofFar, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DofNear, 1, &NullSRV);
}
