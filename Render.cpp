#include "Render.h"

#include "Camera.h"




struct VertexType
{
	D3DXVECTOR3 position;
	D3DXVECTOR4 color;
	D3DXVECTOR4 uv;
};


struct tPixel
{
	float r;
	float g;
	float b;
	float a;
};

__declspec(align(16)) struct tResolutionCB
{
	int width;
	int height;
};

__declspec(align(16)) struct tCameraCB
{
	float cam_pos_x, cam_pos_y, cam_pos_z; //gCameraPosition
	float cam_orientation_00, cam_orientation_01, cam_orientation_02;
	float cam_orientation_10, cam_orientation_11, cam_orientation_12;
	float cam_orientation_20, cam_orientation_21, cam_orientation_22;
};






extern HWND gHWnd;
extern int gWidth;
extern int gHeight;


Render* Render::Instance = NULL;

Render::Render()
{
	Instance = this;

	InitDevice();
}


Render::~Render()
{
}



bool Render::InitDevice()
{

	HRESULT hr = S_OK;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif


	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 1;
	sd.BufferDesc.Width = gWidth;
	sd.BufferDesc.Height = gHeight;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = gHWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	D3D_DRIVER_TYPE         driverType = D3D_DRIVER_TYPE_HARDWARE;
	D3D_FEATURE_LEVEL       desired_featureLevel = D3D_FEATURE_LEVEL_11_0;
	D3D_FEATURE_LEVEL       featureLevel = D3D_FEATURE_LEVEL_11_0;
	hr = D3D11CreateDeviceAndSwapChain(NULL, driverType, NULL, createDeviceFlags, &desired_featureLevel, 1,
		D3D11_SDK_VERSION, &sd, &mSwapChain, &mD3DDevice, &featureLevel, &mImmediateContext);
	if (FAILED(hr))
		return false;


	// Get the pointer to the back buffer.
	ID3D11Texture2D* backBufferPtr;
	hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBufferPtr);
	if (FAILED(hr))
	{
		return false;
	}

	// Create the render target view with the back buffer pointer.
	hr = mD3DDevice->CreateRenderTargetView(backBufferPtr, NULL, &mRenderTargetView);
	if (FAILED(hr))
	{
		return false;
	}


	mImmediateContext->OMSetRenderTargets(1, &mRenderTargetView, NULL/*m_depthStencilView*/);


	////////////////////////////////////////
	// INPUT SCENE BUFFER
	////////////////////////////////////////

	tPixel colour;
	colour.r = 0.0f;
	colour.g = 0.0f;
	colour.b = 0.0f;

	// First we create a buffer in GPU memory
	D3D11_BUFFER_DESC input_descGPUBuffer;
	ZeroMemory(&input_descGPUBuffer, sizeof(input_descGPUBuffer));
	input_descGPUBuffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	input_descGPUBuffer.ByteWidth = sizeof(tPixel);
	input_descGPUBuffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	input_descGPUBuffer.StructureByteStride = sizeof(tPixel);

	D3D11_SUBRESOURCE_DATA InitData;
	InitData.pSysMem = &colour;//THIS IS WHERE THE SCENE INFORMATION GOES!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	hr = mD3DDevice->CreateBuffer(&input_descGPUBuffer, &InitData, &m_srcDataGPUBuffer);
	if (FAILED(hr))
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC input_descView;
	ZeroMemory(&input_descView, sizeof(input_descView));
	input_descView.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	input_descView.Format = DXGI_FORMAT_UNKNOWN;
	input_descView.BufferEx.FirstElement = 0;
	input_descView.BufferEx.NumElements = 1;

	if (FAILED(mD3DDevice->CreateShaderResourceView(m_srcDataGPUBuffer, &input_descView, &m_srcDataGPUBufferView)))
		return false;



	////////////////////////////////////////
	// OUTPUT BUFFER
	////////////////////////////////////////
	D3D11_BUFFER_DESC output_descGPUBuffer;
	ZeroMemory(&output_descGPUBuffer, sizeof(output_descGPUBuffer));
	output_descGPUBuffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	output_descGPUBuffer.ByteWidth = sizeof(tPixel) * gWidth * gHeight;
	output_descGPUBuffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	output_descGPUBuffer.StructureByteStride = sizeof(tPixel);
	if (FAILED(mD3DDevice->CreateBuffer(&output_descGPUBuffer, NULL, &mCSDestDataBuffer)))
		return false;

	// The view we need for the output is an unordered access view. This is to allow the compute shader to write anywhere in the buffer.
	D3D11_UNORDERED_ACCESS_VIEW_DESC output_descView;
	ZeroMemory(&output_descView, sizeof(output_descView));
	output_descView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	output_descView.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
	output_descView.Buffer.FirstElement = 0;
	output_descView.Buffer.NumElements = gWidth * gHeight;
	if (FAILED(mD3DDevice->CreateUnorderedAccessView(mCSDestDataBuffer, &output_descView, &mCSDestDataBufferView)))
		return false;

	// The SRV for reading the data back into the pixel shader
	D3D11_SHADER_RESOURCE_VIEW_DESC output_srv_descView;
	ZeroMemory(&output_srv_descView, sizeof(output_srv_descView));
	output_srv_descView.Format = DXGI_FORMAT_UNKNOWN;
	output_srv_descView.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	output_srv_descView.BufferEx.FirstElement = 0;
	output_srv_descView.BufferEx.NumElements = gWidth * gHeight;
	output_srv_descView.BufferEx.Flags = 0;
	if (FAILED(mD3DDevice->CreateShaderResourceView(mCSDestDataBuffer, &output_srv_descView, &mDestDataSRV)))
		return false;



	////////////////////////////////////////	
	// Load the compute shader now
	////////////////////////////////////////	
	// We load and compile the shader. If we fail, we bail out here.
	DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( _DEBUG )
	dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

	LPCSTR pProfile = (mD3DDevice->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";

	ID3DBlob* pErrorBlob = NULL;
	ID3DBlob* pBlob = NULL;
	hr = D3DX11CompileFromFile(L"RayCast.hlsl", NULL, NULL, "CSMain", pProfile, dwShaderFlags, NULL, NULL, &pBlob, &pErrorBlob, NULL);
	if (FAILED(hr))
	{
		if (pErrorBlob)
			OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
		if (pErrorBlob)
			pErrorBlob->Release();
		if (pBlob)
			pBlob->Release();

		return false;
	}
	else
	{
		hr = mD3DDevice->CreateComputeShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &mComputeShader);
		if (pErrorBlob)
			pErrorBlob->Release();
		if (pBlob)
			pBlob->Release();
	}



	////////////////////////////////////////	
	// Load the plain shader now
	////////////////////////////////////////
	ID3D10Blob* errorMessage;
	ID3D10Blob* vertexShaderBuffer;
	ID3D10Blob* pixelShaderBuffer;
	D3D11_INPUT_ELEMENT_DESC polygonLayout[3];
	unsigned int numElements;
	//D3D11_BUFFER_DESC matrixBufferDesc;

	//D3D11_SAMPLER_DESC samplerDesc;

	// Initialize the pointers this function will use to null.
	errorMessage = 0;
	vertexShaderBuffer = 0;
	pixelShaderBuffer = 0;

	// Compile the vertex shader code.
	hr = D3DX11CompileFromFile(L"Color.vs", NULL, NULL, "ColorVertexShader", "vs_5_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, NULL,
		&vertexShaderBuffer, &errorMessage, NULL);
	if (FAILED(hr))
	{
		// If the shader failed to compile it should have writen something to the error message.
		/*if(errorMessage)
		{
		//OutputShaderErrorMessage(errorMessage, hwnd, vsFilename);
		}
		// If there was  nothing in the error message then it simply could not find the shader file itself.
		else
		{
		//MessageBox(hwnd, vsFilename, L"Missing Shader File", MB_OK);
		}*/

		return false;
	}

	// Compile the pixel shader code.
	hr = D3DX11CompileFromFile(L"Color.ps", NULL, NULL, "ColorPixelShader", "ps_5_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, NULL,
		&pixelShaderBuffer, &errorMessage, NULL);
	if (FAILED(hr))
	{
		// If the shader failed to compile it should have writen something to the error message.
		/*if(errorMessage)
		{
		OutputShaderErrorMessage(errorMessage, hwnd, psFilename);
		}
		// If there was nothing in the error message then it simply could not find the file itself.
		else
		{
		MessageBox(hwnd, psFilename, L"Missing Shader File", MB_OK);
		}*/
		return false;
	}

	// Create the vertex shader from the buffer.
	hr = mD3DDevice->CreateVertexShader(vertexShaderBuffer->GetBufferPointer(), vertexShaderBuffer->GetBufferSize(), NULL, &mVertexShader);
	if (FAILED(hr))
	{
		return false;
	}

	// Create the pixel shader from the buffer.
	hr = mD3DDevice->CreatePixelShader(pixelShaderBuffer->GetBufferPointer(), pixelShaderBuffer->GetBufferSize(), NULL, &mPixelShader);
	if (FAILED(hr))
	{
		return false;
	}

	// Create the vertex input layout description.
	// This setup needs to match the VertexType stucture in the ModelClass and in the shader.
	polygonLayout[0].SemanticName = "POSITION";
	polygonLayout[0].SemanticIndex = 0;
	polygonLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout[0].InputSlot = 0;
	polygonLayout[0].AlignedByteOffset = 0;
	polygonLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[0].InstanceDataStepRate = 0;

	polygonLayout[1].SemanticName = "COLOR";
	polygonLayout[1].SemanticIndex = 0;
	polygonLayout[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	polygonLayout[1].InputSlot = 0;
	polygonLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[1].InstanceDataStepRate = 0;

	polygonLayout[2].SemanticName = "TEXCOORD";
	polygonLayout[2].SemanticIndex = 0;
	polygonLayout[2].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	polygonLayout[2].InputSlot = 0;
	polygonLayout[2].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout[2].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[2].InstanceDataStepRate = 0;

	// Get a count of the elements in the layout.
	numElements = sizeof(polygonLayout) / sizeof(polygonLayout[0]);

	// Create the vertex input layout.
	hr = mD3DDevice->CreateInputLayout(polygonLayout, numElements, vertexShaderBuffer->GetBufferPointer(),
		vertexShaderBuffer->GetBufferSize(), &mLayout);
	if (FAILED(hr))
	{
		return false;
	}

	// Release the vertex shader buffer and pixel shader buffer since they are no longer needed.
	vertexShaderBuffer->Release();
	vertexShaderBuffer = 0;

	pixelShaderBuffer->Release();
	pixelShaderBuffer = 0;


	// Create a texture sampler state description.
	/*samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.BorderColor[0] = 0;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	// Create the texture sampler state.
	hr = mD3DDevice->CreateSamplerState(&samplerDesc, &m_sampleState);
	if (FAILED(hr))
	{
		return false;
	}*/



	/////////////////Create vertex and index buffers
	VertexType* vertices;
	D3D11_BUFFER_DESC vertexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData;

	// Set the number of vertices in the vertex array.
	mVertexCount = 4;

	// Create the vertex array.
	vertices = new VertexType[mVertexCount];
	if (!vertices)
	{
		return false;
	}

	// Load the vertex array with data.
	vertices[0].position = D3DXVECTOR3(-1.0f, 1.0f, 0.5f);  // Bottom left.
	vertices[0].color = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 1.0f);
	vertices[0].uv = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	vertices[1].position = D3DXVECTOR3(1.0f, 1.0f, 0.5f);  // Bottom right.
	vertices[1].color = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 1.0f);
	vertices[1].uv = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);

	vertices[2].position = D3DXVECTOR3(-1.0f, -1.0f, 0.5f);  // Top left.
	vertices[2].color = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 1.0f);
	vertices[2].uv = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f);

	vertices[3].position = D3DXVECTOR3(1.0f, -1.0f, 0.5f);  // Top right.
	vertices[3].color = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 1.0f);
	vertices[3].uv = D3DXVECTOR4(1.0f, 1.0f, 0.0f, 0.0f);


	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertexBufferDesc.ByteWidth = sizeof(VertexType) * mVertexCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = 0;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	hr = mD3DDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &mVertexBuffer);
	if (FAILED(hr))
	{
		return false;
	}


	// Setup the raster description which will determine how and what polygons will be drawn.
	D3D11_RASTERIZER_DESC rasterDesc;
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = false;
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.MultisampleEnable = false;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.0f;

	// Create the rasterizer state from the description we just filled out.
	hr = mD3DDevice->CreateRasterizerState(&rasterDesc, &mRasterState);
	if (FAILED(hr))
	{
		return false;
	}

	// Now set the rasterizer state.
	mImmediateContext->RSSetState(mRasterState);

	// Setup the viewport for rendering.
	D3D11_VIEWPORT viewport;
	viewport.Width = (float)gWidth;
	viewport.Height = (float)gHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;

	// Create the viewport.
	mImmediateContext->RSSetViewports(1, &viewport);




	//////////////////////////////////////////
	//CONSTANT BUFFERS
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.ByteWidth = sizeof(tResolutionCB);
	bd.Usage = D3D11_USAGE_DEFAULT;
	hr = mD3DDevice->CreateBuffer(&bd, 0, &mResConstantBuffer);
	if (hr != S_OK)
	{
		return false;
	}
	tResolutionCB cb;
	cb.width = gWidth;
	cb.height = gHeight;

	mImmediateContext->UpdateSubresource(mResConstantBuffer, 0, 0, &cb, 0, 0);




	D3D11_BUFFER_DESC bd_camera;
	ZeroMemory(&bd_camera, sizeof(bd_camera));
	bd_camera.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd_camera.ByteWidth = sizeof(tCameraCB);
	bd_camera.Usage = D3D11_USAGE_DEFAULT;
	hr = mD3DDevice->CreateBuffer(&bd_camera, 0, &mCameraConstantBuffer);
	if (hr != S_OK)
	{
		return false;
	}


	return true;
}




void Render::UpdateBuffers()
{
	tCameraCB cb_camera;

	//Set camera constants to cb
	cb_camera.cam_pos_x = Camera::Instance->GetPosition()->mX;
	cb_camera.cam_pos_y = Camera::Instance->GetPosition()->mY;
	cb_camera.cam_pos_z = Camera::Instance->GetPosition()->mZ;

	Matrix3x3 view_mat = Camera::Instance->GetViewMatrix();
	cb_camera.cam_orientation_00 = view_mat.m00; cb_camera.cam_orientation_01 = view_mat.m01; cb_camera.cam_orientation_02 = view_mat.m02;
	cb_camera.cam_orientation_10 = view_mat.m10; cb_camera.cam_orientation_11 = view_mat.m11; cb_camera.cam_orientation_12 = view_mat.m12;
	cb_camera.cam_orientation_20 = view_mat.m20; cb_camera.cam_orientation_21 = view_mat.m21; cb_camera.cam_orientation_22 = view_mat.m22;

	mImmediateContext->UpdateSubresource(mCameraConstantBuffer, 0, 0, &cb_camera, 0, 0);
}



void Render::DoFrame()
{
	UpdateBuffers();




	/*float color[4];
	color[0] = (float)rand() / RAND_MAX;
	color[1] = 1.0f;
	color[2] = 0.0f;
	color[3] = 1.0f;*/

	// Clear the back buffer.
	//mImmediateContext->ClearRenderTargetView(mRenderTargetView, color);



	//Compute the Scene!

	mImmediateContext->CSSetConstantBuffers(0, 1, &mResConstantBuffer);
	mImmediateContext->CSSetConstantBuffers(1, 1, &mCameraConstantBuffer);

	//m_pImmediateContext->CSSetShaderResources( 0, 1, &m_srcDataGPUBufferView );
	mImmediateContext->CSSetUnorderedAccessViews(0, 1, &mCSDestDataBufferView, NULL);
	mImmediateContext->CSSetShader(mComputeShader, NULL, 0);

	const int threads_dim = 32;
	mImmediateContext->Dispatch((gWidth + (threads_dim - 1)) / threads_dim, (gHeight + (threads_dim - 1)) / threads_dim, 1);

	mImmediateContext->CSSetShader(NULL, NULL, 0);
	ID3D11UnorderedAccessView* ppUAViewNULL[1] = { NULL };
	ID3D11ShaderResourceView* ppSRVNULL[2] = { NULL, NULL };
	mImmediateContext->CSSetUnorderedAccessViews(0, 1, ppUAViewNULL, NULL);
	mImmediateContext->CSSetShaderResources(0, 2, ppSRVNULL);







	//render ray cast result to the back buffer
	unsigned int stride = sizeof(VertexType);
	unsigned int offset = 0;
	mImmediateContext->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
	mImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mImmediateContext->IASetInputLayout(mLayout);

	mImmediateContext->VSSetShader(mVertexShader, NULL, 0);
	mImmediateContext->PSSetShader(mPixelShader, NULL, 0);
	mImmediateContext->PSSetShaderResources(0, 1, &mDestDataSRV);
	mImmediateContext->PSSetConstantBuffers(0, 1, &mResConstantBuffer);


	mImmediateContext->Draw(mVertexCount, 0); 	// Render the triangle.

	mImmediateContext->PSSetShaderResources(0, 1, ppSRVNULL);

	mSwapChain->Present(0, 0);

}





/*  //Copy gpu buffer out to a cpu buffer

	ID3D11Buffer* debugbuf = NULL;

	D3D11_BUFFER_DESC desc;
	ZeroMemory( &desc, sizeof(desc) );
	m_destDataGPUBuffer->GetDesc( &desc );

	UINT byteSize = desc.ByteWidth;

	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.MiscFlags = 0;

	if ( SUCCEEDED(m_pd3dDevice->CreateBuffer(&desc, NULL, &debugbuf)) )
	{
		m_pImmediateContext->CopyResource( debugbuf, m_destDataGPUBuffer );

		D3D11_MAPPED_SUBRESOURCE mappedResource;
		if(m_pImmediateContext->Map(debugbuf, 0, D3D11_MAP_READ, 0, &mappedResource) != S_OK)
			return false;

		memcpy(image->GetBuffer(), mappedResource.pData, byteSize);

		m_pImmediateContext->Unmap(debugbuf, 0);

		debugbuf->Release();
	}
*/