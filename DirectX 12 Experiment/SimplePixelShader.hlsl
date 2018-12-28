struct PixelInput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};

float4 Main(PixelInput Input) : SV_TARGET
{
	return Input.Color;
}