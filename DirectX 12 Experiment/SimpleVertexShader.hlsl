struct VertexInput
{
	float4 Position : POSITION;
	float4 Color : COLOR;
};

struct VertexOutput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};

VertexOutput Main(VertexInput Input)
{
	VertexOutput Output;
	Output.Position = Input.Position;
	Output.Color = Input.Color;
	return Output;
}