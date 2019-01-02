struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 UV : TEXCOORD;
};

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

float4 Main(PixelInput Input) : SV_TARGET
{
	return Texture.Sample(Sampler, Input.UV);
}