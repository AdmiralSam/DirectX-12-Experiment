struct VertexInput
{
	float4 Position : POSITION;
	float2 UV : TEXCOORD;
};

struct VertexOutput
{
	float4 Position : SV_POSITION;
	float2 UV : TEXCOORD;
};

VertexOutput Main(VertexInput Input)
{
	VertexOutput Output;
	Output.Position = Input.Position;
	Output.UV = Input.UV;
	return Output;
}