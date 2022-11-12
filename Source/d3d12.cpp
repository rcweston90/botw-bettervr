#include "layer.h"
#include "shader.h"

D3D_FEATURE_LEVEL d3d12SharedFeatureLevel;
LUID d3d12SharedAdapter;

ComPtr<IDXGIFactory6> dxgiFactory;
ComPtr<IDXGIAdapter1> dxgiAdapter;
ComPtr<ID3D12Device> d3d12Device;
ComPtr<ID3D12Fence> d3d12Fence;

struct D3D12RenderPass {
	ComPtr<ID3D12RootSignature> signature;
	ComPtr<ID3D12PipelineState> pipelineState;
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	ComPtr<ID3D12DescriptorHeap> colorTargetDescHeap;

	struct SharedTexture {
		uint32_t width;
		uint32_t height;
		ComPtr<ID3D12Resource> resource;
		HANDLE sharedHandle;
	};
	
	ComPtr<ID3D12DescriptorHeap> sharedTexturesHeap;
	std::vector<SharedTexture> sharedTextures;

	D3D12_INDEX_BUFFER_VIEW screenIndicesView;
	ComPtr<ID3D12Resource> screenIndicesBuffer;
};

D3D12RenderPass d3d12RenderPass;


HANDLE d3d12FenceHandle;

ComPtr<ID3D12CommandQueue> d3d12Queue;

template<bool blockTillExecuted>
class D3D12_CommandContext {
public:
	template <typename F>
	D3D12_CommandContext(F&& recordCallback) {
		// Create commands to upload buffers
		d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&this->commmandAllocator));
		d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, this->commmandAllocator.Get(), nullptr, IID_PPV_ARGS(&this->commandList));
		
		recordCallback(this->commandList.Get());
	}
	~D3D12_CommandContext() {
		// Close command list and then execute command list in queue
		checkHResult(this->commandList->Close(), "Failed to close D3D12_CommandContext's queue");
		ID3D12CommandList* collectedList[] = { this->commandList.Get() };
		d3d12Queue->ExecuteCommandLists((UINT)std::size(collectedList), collectedList);

		// If enabled, wait until the command list and the fence signal has been executed
		if constexpr (blockTillExecuted) {
			d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&this->blockFence));
			d3d12Queue->Signal(this->blockFence.Get(), 1);

			HANDLE waitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			checkAssert(waitEvent != NULL, "Failed to create upload event!");

			if (this->blockFence->GetCompletedValue() < 1) {
				this->blockFence->SetEventOnCompletion(1, waitEvent);
				WaitForSingleObject(waitEvent, INFINITE);
			}
			CloseHandle(waitEvent);
		}

		this->commmandAllocator.Reset();
	}
private:
	ComPtr<ID3D12CommandAllocator> commmandAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12Fence> blockFence;
};

void D3D12_CreateInstance(D3D_FEATURE_LEVEL minFeatureLevel, LUID adapterLUID) {
	logPrint("Creating OpenXR-compatible D3D12 instance for rendering VR frames...");
	
	UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debugController;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	debugController->EnableDebugLayer();
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	checkHResult(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)), "Failed to create the DXGIFactory!");

	for (UINT idx = 0; SUCCEEDED(dxgiFactory->EnumAdapterByGpuPreference(idx, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&dxgiAdapter))); idx++) {
		DXGI_ADAPTER_DESC1 adapterDesc;
		dxgiAdapter->GetDesc1(&adapterDesc);
		if (memcmp(&adapterDesc.AdapterLuid, &adapterLUID, sizeof(LUID)) == 0) {
			char descriptionStr[256+1];
			std::wcstombs(descriptionStr, adapterDesc.Description, 256);
			logPrint(std::format("Using {} as the VR GPU!", descriptionStr));
			break;
		}
	}
	checkAssert(dxgiAdapter, "Failed to find the VR GPU selected by OpenXR!");

	checkHResult(D3D12CreateDevice(dxgiAdapter.Get(), minFeatureLevel, IID_PPV_ARGS(&d3d12Device)), "Failed to create D3D12 device!");

	D3D12_COMMAND_QUEUE_DESC queueDesc = {
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
	};
	
	checkHResult(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d3d12Queue)), "Failed to create D3D12 command queue!");
	logPrint("Successfully created D3D12 instance!");
}

ComPtr<ID3D12Resource> D3D12Util_CreateConstantBuffer(D3D12_HEAP_TYPE heapType, uint32_t size) {
	auto findAlignedSize = [](uint32_t size) -> UINT {
		assert((D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT & (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) == 0, "The alignment must be power-of-two");
		return (size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
	};

	D3D12_RESOURCE_STATES bufferType = (heapType == D3D12_HEAP_TYPE_UPLOAD) ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heapProp = {
		.Type = heapType,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN
	};

	D3D12_RESOURCE_DESC buffDesc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Alignment = 0,
		.Width = (heapType == D3D12_HEAP_TYPE_UPLOAD) ? findAlignedSize(size) : size,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE
	};

	ComPtr<ID3D12Resource> buffer;
	checkHResult(d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &buffDesc, bufferType, nullptr, IID_PPV_ARGS(&buffer)));
	return buffer;
}


void D3D12_CreateShaderPipeline(DXGI_FORMAT swapchainFormat, uint32_t swapchainWidth, uint32_t swapchainHeight) {
	auto compileShader = [](const char* sourceHLSL, const char* entryPoint, const char* version) {
		DWORD shaderCompileFlags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef _DEBUG
		shaderCompileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
		shaderCompileFlags |= D3DCOMPILE_DEBUG;
#else
		shaderCompileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
		ComPtr<ID3DBlob> shaderBytes;
		ID3DBlob* hlslCompilationErrors;
		if (FAILED(D3DCompile(sourceHLSL, strlen(sourceHLSL), nullptr, nullptr, nullptr, entryPoint, version, shaderCompileFlags, 0, &shaderBytes, &hlslCompilationErrors))) {
			std::string errorMessage((const char*)hlslCompilationErrors->GetBufferPointer(), hlslCompilationErrors->GetBufferSize());
			logPrint("Vertex Shader Compilation Error:");
			logPrint(errorMessage);
			throw std::runtime_error("Error during the vertex shader compilation!");
		}
		return shaderBytes;
	};
	
	auto createDescriptorHeap = [](D3D12_DESCRIPTOR_HEAP_TYPE descType, bool shaderVisible, UINT numDescriptors) {
		ComPtr<ID3D12DescriptorHeap> descriptorHeap;
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
			.Type = descType,
			.NumDescriptors = numDescriptors,
			.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE
		};
		checkHResult(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap)), "Failed to create descriptor heap!");
		return descriptorHeap;
	};

	auto createSignature = []() {
		D3D12_DESCRIPTOR_RANGE pixelRange[] = {
			// Input texture
			{
				.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors = 1,
				.BaseShaderRegister = 0,
				.RegisterSpace = 0,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
			}
		};
		
		D3D12_ROOT_PARAMETER rootParams[] = {
			{
				.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
				.DescriptorTable = {
					.NumDescriptorRanges = 1,
					.pDescriptorRanges = pixelRange
				},
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
			},
			{
				.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
				.Descriptor = {
					.ShaderRegister = 1,
					.RegisterSpace = 0
				},
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
			}
		};

		D3D12_STATIC_SAMPLER_DESC textureSampler = {
			.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
			.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.MipLODBias = 0,
			.MaxAnisotropy = 0,
			.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
			.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			.MinLOD = 0.0f,
			.MaxLOD = D3D12_FLOAT32_MAX,
			.ShaderRegister = 0,
			.RegisterSpace = 0,
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
		};

		D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {
			.NumParameters = std::size(rootParams),
			.pParameters = rootParams,
			.NumStaticSamplers = 1,
			.pStaticSamplers = &textureSampler,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		};

		ComPtr<ID3DBlob> serializedBlob;
		ComPtr<ID3DBlob> error;
		if (HRESULT res = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedBlob, &error); FAILED(res)) {
			checkHResult(res, std::format("Failed to serialize root signature! {}", std::string((const char*)error->GetBufferPointer(), error->GetBufferSize())).c_str());
		}

		ComPtr<ID3D12RootSignature> rootSigBlob;
		checkHResult(d3d12Device->CreateRootSignature(0, serializedBlob->GetBufferPointer(), serializedBlob->GetBufferSize(), IID_PPV_ARGS(&rootSigBlob)), "Failed to create root signature!");
		return rootSigBlob;
	};

	logPrint("Compiling D3D12 shader pipeline...");

	// Create descriptor heaps for shader outputs
	d3d12RenderPass.vertexShader = compileShader(shaderHLSL, "VSMain", "vs_5_1");
	d3d12RenderPass.pixelShader = compileShader(shaderHLSL, "PSMain", "ps_5_1");

	// Create render view and heap for the render pass
	d3d12RenderPass.colorTargetDescHeap = createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false, 1);

	// Create shared texture and heap for the shared textures
	d3d12RenderPass.sharedTextures.clear();
	d3d12RenderPass.sharedTexturesHeap = createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, 2);
	
	d3d12RenderPass.signature = createSignature();

	// Upload constant buffers like screen indices
	ComPtr<ID3D12Resource> screenIndicesStaging;
	{
		D3D12_CommandContext<true> uploadBufferContext([&screenIndicesStaging](ID3D12GraphicsCommandList* cmdList) {
			d3d12RenderPass.screenIndicesBuffer = D3D12Util_CreateConstantBuffer(D3D12_HEAP_TYPE_DEFAULT, sizeof(screenIndices));

			screenIndicesStaging = D3D12Util_CreateConstantBuffer(D3D12_HEAP_TYPE_UPLOAD, sizeof(screenIndices));
			void* data;
			const D3D12_RANGE readRange = { .Begin = 0, .End = 0 };
			checkHResult(screenIndicesStaging->Map(0, &readRange, &data), "Failed to map memory for screen indices buffer!");
			memcpy(data, screenIndices, sizeof(screenIndices));
			screenIndicesStaging->Unmap(0, nullptr);

			cmdList->CopyBufferRegion(d3d12RenderPass.screenIndicesBuffer.Get(), 0, screenIndicesStaging.Get(), 0, sizeof(screenIndices));
			d3d12RenderPass.screenIndicesView = {
				.BufferLocation = d3d12RenderPass.screenIndicesBuffer->GetGPUVirtualAddress(),
				.SizeInBytes = sizeof(screenIndices),
				.Format = DXGI_FORMAT_R16_UINT
			};
		});
	}

	// Create pipeline stage
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "SV_InstanceID", 0, DXGI_FORMAT_R16_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SV_VertexID", 0, DXGI_FORMAT_R16_UINT, 0, 4, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, (UINT)std::size(inputElementDescs) };
	psoDesc.pRootSignature = d3d12RenderPass.signature.Get();
	psoDesc.VS = { d3d12RenderPass.vertexShader->GetBufferPointer(), d3d12RenderPass.vertexShader->GetBufferSize() };
	psoDesc.PS = { d3d12RenderPass.pixelShader->GetBufferPointer(), d3d12RenderPass.pixelShader->GetBufferSize() };
	psoDesc.BlendState = {
		.AlphaToCoverageEnable = false,
		.IndependentBlendEnable = false
	};
	for (uint32_t i=0; i<std::size(psoDesc.BlendState.RenderTarget); i++) {
		psoDesc.BlendState.RenderTarget[i] = {
			.BlendEnable = false,
			
			.SrcBlend = D3D12_BLEND_ONE,
			.DestBlend = D3D12_BLEND_ZERO,
			.BlendOp = D3D12_BLEND_OP_ADD,
			
			.SrcBlendAlpha = D3D12_BLEND_ONE,
			.DestBlendAlpha = D3D12_BLEND_ZERO,
			.BlendOpAlpha = D3D12_BLEND_OP_ADD,
			
			.LogicOp = D3D12_LOGIC_OP_NOOP,
			.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
		};
	}
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.RasterizerState = {
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = D3D12_CULL_MODE_BACK,
		.FrontCounterClockwise = false,
		.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
		.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
		.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
		.DepthClipEnable = true,
		.MultisampleEnable = false,
		.AntialiasedLineEnable = false,
		.ForcedSampleCount = 0,
		.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
	};
	psoDesc.DepthStencilState = {
		.DepthEnable = false,
		.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
		.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
		.StencilEnable = false,
		.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
		.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
		.FrontFace = {
			.StencilFailOp = D3D12_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
			.StencilPassOp = D3D12_STENCIL_OP_KEEP,
			.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
		}
	};
	psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = swapchainFormat;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.NodeMask = 0;
	psoDesc.CachedPSO = {nullptr, 0};
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	checkHResult(d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&d3d12RenderPass.pipelineState)), "Failed to create graphics pipeline state!");
}

HANDLE D3D12_CreateSharedTexture(uint32_t width, uint32_t height, DXGI_FORMAT format) {
	uint32_t idx = d3d12RenderPass.sharedTextures.size();
	auto& sharedTexture = d3d12RenderPass.sharedTextures.emplace_back(D3D12RenderPass::SharedTexture{width, height});
	
	D3D12_RESOURCE_DESC textureDesc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Alignment = 0,
		.Width = width,
		.Height = height,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = format,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		.Flags = D3D12_RESOURCE_FLAG_NONE
	};

	D3D12_HEAP_PROPERTIES heapProp = {
		.Type = D3D12_HEAP_TYPE_DEFAULT,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1
	};

	checkHResult(d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_SHARED, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sharedTexture.resource)), "Failed to create texture!");

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = d3d12RenderPass.sharedTexturesHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (idx * d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	d3d12Device->CreateShaderResourceView(sharedTexture.resource.Get(), &srvDesc, srvHandle);
	
	checkHResult(d3d12Device->CreateSharedHandle(sharedTexture.resource.Get(), nullptr, GENERIC_ALL, nullptr, &sharedTexture.sharedHandle));
	
	return sharedTexture.sharedHandle;
}

void D3D12_RenderFrameToSwapchain(uint32_t textureIdx, ID3D12Resource* swapchain, uint32_t swapchainWidth, uint32_t swapchainHeight) {
	auto& texture = d3d12RenderPass.sharedTextures[textureIdx];

	D3D12_CommandContext<true> renderFrameContext([textureIdx, &texture, swapchain, swapchainWidth, swapchainHeight](ID3D12GraphicsCommandList* cmdList) {
		cmdList->SetPipelineState(d3d12RenderPass.pipelineState.Get());
		cmdList->SetGraphicsRootSignature(d3d12RenderPass.signature.Get());
		
		// Set framebuffer
		D3D12_VIEWPORT viewportSize = { 0.0f, 0.0f, (float)swapchainWidth, (float)swapchainHeight, 0.0f, 1.0f };
		cmdList->RSSetViewports(1, &viewportSize);

		D3D12_RECT scissorRect = { 0, 0, (LONG)swapchainWidth, (LONG)swapchainHeight };
		cmdList->RSSetScissorRects(1, &scissorRect);

		// Set shared texture with framebuffer
		ID3D12DescriptorHeap* heaps[] = { d3d12RenderPass.sharedTexturesHeap.Get() };
		cmdList->SetDescriptorHeaps(std::size(heaps), heaps);

		cmdList->SetGraphicsRootDescriptorTable(0, d3d12RenderPass.sharedTexturesHeap->GetGPUDescriptorHandleForHeapStart());

		// Set render target
		// todo: Do this once per swapchain since this might be costly?
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = d3d12RenderPass.colorTargetDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
		renderTargetViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		d3d12Device->CreateRenderTargetView(swapchain, &renderTargetViewDesc, renderTargetView);

		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = d3d12RenderPass.colorTargetDescHeap->GetCPUDescriptorHandleForHeapStart();
		cmdList->OMSetRenderTargets(1, &renderTargetViewHandle, true, nullptr);

		float clearColor[4] = { textureIdx == 0 ? 0.0f, 0.2f, 0.4f, 1.0f : 0.4f, 0.2f, 0.0f, 1.0f };
		cmdList->ClearRenderTargetView(renderTargetView, clearColor, 0, nullptr);

		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmdList->IASetIndexBuffer(&d3d12RenderPass.screenIndicesView);
		cmdList->DrawIndexedInstanced(std::size(screenIndices), 1, 0, 0, 0);

		logPrint(std::format("Rendered one frame for eye {}", textureIdx));
	});
}

XrGraphicsBindingD3D12KHR D3D12_GetGraphicsBinding() {
	return XrGraphicsBindingD3D12KHR{
		.type = XR_TYPE_GRAPHICS_BINDING_D3D12_KHR,
		.device = d3d12Device.Get(),
		.queue = d3d12Queue.Get()
	};
}

HANDLE D3D12_CreateSharedFence() {
	logPrint("Creating D3D12 fence and a shared handle for Vulkan...");
	checkHResult(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)), "Failed to create D3D12 fence!");
	checkHResult(d3d12Device->CreateSharedHandle(d3d12Fence.Get(), nullptr, GENERIC_ALL, nullptr, &d3d12FenceHandle), "Failed to create shared fence handle!");
	logPrint("Successfully created D3D12 instance!");
	return d3d12FenceHandle;
}

void D3D12_DestroyInstance() {
	logPrint("STUBBED: Trying to destroy D3D12 instance!");
	// todo: wait for and destroy queue
	// todo: destroy all handles
	d3d12Device->Release();
	dxgiFactory.Reset();
}