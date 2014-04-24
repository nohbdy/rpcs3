#pragma once

#include "Utilities/Hasher.h"

namespace rpcs3 {
namespace rsx {

namespace FragmentShaderOpcode
{
	enum Type
	{
		NOP = 0x00, // No-Operation
		MOV = 0x01, // Move
		MUL = 0x02, // Multiply
		ADD = 0x03, // Add
		MAD = 0x04, // Multiply-Add
		DP3 = 0x05, // 3-component Dot Product
		DP4 = 0x06, // 4-component Dot Product
		DST = 0x07, // distance
		MIN = 0x08, // Minimum
		MAX = 0x09, // Maximum
		SLT = 0x0A, // Set-If-LessThan
		SGE = 0x0B, // Set-If-GreaterEqual
		SLE = 0x0C, // Set-If-LessEqual
		SGT = 0x0D, // Set-If-GreaterThan
		SNE = 0x0E, // Set-If-NotEqual
		SEQ = 0x0F, // Set-If-Equal
		FRC = 0x10, // fraction (fract)
		FLR = 0x11, // floor
		KIL = 0x12, // Kill fragment
		PK4 = 0x13,
		UP4 = 0x14,
		DDX = 0x15, // partial-derivative in x
		DDY = 0x16, // partial-derivative in y
		TEX = 0x17, // Texture Lookup
		TXP = 0x18, // texture sample with projection
		TXD = 0x19, // texture sample with partial differentiation
		RCP = 0x1A, // Reciprocal
		RSQ = 0x1B, // Reciprocal Square Root
		EX2 = 0x1C, // exp2
		LG2 = 0x1D, // log2
		LIT = 0x1E, // lighting coefficients
		LRP = 0x1F, // Linear-Interpolation
		STR = 0x20, // Set-If-True
		SFL = 0x21, // Set-If-False
		COS = 0x22, // Cosine
		SIN = 0x23, // Sine
		PK2 = 0x24,
		UP2 = 0x25,
		POW = 0x26, // Power
		PKB = 0x27,
		UPB = 0x28,
		PK16 = 0x29,
		UP16 = 0x2A,
		BEM = 0x2B, // bump-environment map aka 2d coordinate transform
		PKG = 0x2C,
		UPG = 0x2D,
		DP2A = 0x2E, // 2-component dot product with scalar addition
		TXL = 0x2F, // texture sample with LOD
		TXB = 0x31, // texture sample with bias
		TEXBEM = 0x33,
		TXPBEM = 0x34,
		BEMLUM = 0x35,
		REFL = 0x36, // Reflect
		TIMESWTEX = 0x37,
		DP2 = 0x38, // 2-component dot product
		NRM = 0x39, // normalize
		DIV = 0x3A, // Division
		DIVSQ = 0x3B, // Divide by Square Root
		LIF = 0x3C, // Final part of LIT
		FENCT = 0x3D, // Fence T?
		FENCB = 0x3E, // Fence B?
		UNUSED3F = 0x3F, // 0x3F isn't used, just here to fill in the gap between FENCB and BRK
		BRK = 0x40,  // Break
		CAL = 0x41,  // Subroutine call
		IFE = 0x42,  // If
		LOOP = 0x43, // Loop
		REP = 0x44,  // Repeat
		RET = 0x45,   // Return
		NUM_OPCODES = 0x46
	};
}

namespace FragmentShaderOperandType
{
	enum Type
	{
		Register = 0,	// Register/Temporary (r# or h#)
		Special = 1,	// Inputs, Index register
		Constant = 2	// Constant Register (c#)
	};
}

namespace FragmentShaderPrecision
{
	enum Type
	{
		Full = 0,	// 32-bit IEEE floating point
		Half = 1,	// 16-bit half precision floating point
		Fixed12 = 2,// 12-bit fixed point
		Fixed9 = 3	// 9-bit fixed point
	};
}

namespace FragmentShaderScale
{
	enum Type
	{
		None = 0,
		Times2 = 1,
		Times4 = 2,
		Times8 = 3,
		Unused = 4,
		Div2 = 5,
		Div4 = 6,
		Div8 = 7
	};
}

namespace FragmentShaderInputSemantic
{
	enum Type
	{
		WPOS = 0,
		COL0 = 1,
		COL1 = 2,
		FOGC = 3,
		TEX0 = 4,
		TEX1 = 5,
		TEX2 = 6,
		TEX3 = 7,
		TEX4 = 8,
		TEX5 = 9,
		TEX6 = 10,
		TEX7 = 11,
		TEX8 = 12,
		TEX9 = 13,
		SSA  = 14, // vFace, Sign of Signed Area
		NUM_SEMANTICS
	};
}

namespace FragmentShaderCondition
{
	enum Type
	{
		False = 0,
		LessThan = 1,
		Equal = 2,
		LessEqual = 3,
		GreaterThan = 4,
		NotEqual = 5,
		GreaterEqual = 6,
		True = 7
	};
}

/** Structure of the 32-bit value sent in a NV4097_SET_SHADER_CONTROL command*/
union FragmentProgramControl {
private:
	struct {
		u32: 1;					//     [0] unused
		u32 depthReplace : 3;	//   [1:3] Set to 0x7 if the shader program was compiled with depthReplace
		u32: 2;					//   [4:5] unused
		u32 outputFromR0 : 1;	//     [6] If 1, the color output is in R0, if 0 the color output is in H0
		u32 pixelKill : 1;		//     [7] If 1, uses KIL instruction?
		u32: 2;					//   [8:9] unused
		u32 on : 1;				//    [10] Always set to 1?
		u32: 4;					// [11:14] unused
		u32 txpConversion : 1;	//    [15] should convert txp instructions to tex instructions
		u32: 8;					// [17:23] unused
		u32 registerCount : 8;	// [24:31] How many registers this program uses (valid range: 2..48)
	};
public:
	u32 value;

	bool OutputFromR0() { return value & 0x40; }
	bool PixelKill() { return value & 0x80; }
	bool TxpConversion() { return value & 0x8000; }
	bool DepthReplace() { return (depthReplace > 0); }
	u32 RegisterCount() { return registerCount; }
};

struct RSXShaderProgram
{
	u32 size;
	u32 addr;
	u32 offset;
	FragmentProgramControl ctrl;

	RSXShaderProgram()
		: size(0)
		, addr(0)
		, offset(0)
	{
		ctrl.value = 0;
	}
};

/** Utility to read from an in-memory RSX shader binary stream */
class FragmentShaderBinaryReader
{
	mem32_ptr_t m_data;
	u32 m_bytes_read;
	u32 m_line_number;

	/** Set to true when we read a Vec4 constant value */
	bool m_readConstant;

	bool m_inBeginEnd;

	const u32 kBytesPerLine = 16;
	u32 ReadDword(int i);
	float ReadFloat(int i);
public:
	FragmentShaderBinaryReader(u32 addr);
	void ReadVec4(float& x, float& y, float& z, float& w);
	void MoveToNextLine();
	u32 GetLineNumber() const { return m_line_number; }
	u32 GetBytesRead() const { return m_bytes_read; }

	/** Called at the start of instruction processing */
	void BeginInstruction(u32& dst, u32& src0, u32& src1, u32& src2);

	/** Called after processing the current instruction */
	void EndInstruction();
};

#define FOREACH_FRAGMENT_SHADER_INSTRUCTION(macro) \
	macro(NOP, 0, NoOperation) \
	macro(MOV, 1, Move) \
	macro(MUL, 2, Multiply) \
	macro(ADD, 2, Add) \
	macro(MAD, 3, MultiplyAdd) \
	macro(DP3, 2, DotProduct3) \
	macro(DP4, 2, DotProduct4) \
	macro(DST, 2, Distance) \
	macro(MIN, 2, Minimum) \
	macro(MAX, 2, Maximum) \
	macro(SLT, 2, SetOnLessThan) \
	macro(SGE, 2, SetOnGreaterEqual) \
	macro(SLE, 2, SetOnLessEqual) \
	macro(SGT, 2, SetOnGreaterThan) \
	macro(SNE, 2, SetOnNotEqual) \
	macro(SEQ, 2, SetOnEqual) \
	macro(FRC, 1, Fraction) \
	macro(FLR, 1, Floor) \
	macro(KIL, 0, Kill) \
	macro(PK4, 1, Pack4) \
	macro(UP4, 1, Unpack4) \
	macro(DDX, 1, DDX) \
	macro(DDY, 1, DDY) \
	macro(TEX, 1, Texture) \
	macro(TXP, 1, TextureProjection) \
	macro(TXD, 1, TextureDerivative) \
	macro(RCP, 1, Reciprocal) \
	macro(RSQ, 1, ReciprocalSquareRoot) \
	macro(EX2, 1, Exponential2) \
	macro(LG2, 1, Log2) \
	macro(LIT, 1, Lighting) \
	macro(LRP, 3, Lerp) \
	macro(STR, 2, SetOnTrue) \
	macro(SFL, 2, SetOnFalse) \
	macro(COS, 1, Cosine) \
	macro(SIN, 1, Sine) \
	macro(PK2, 1, Pack2) \
	macro(UP2, 1, Unpack2) \
	macro(POW, 2, Pow) \
	macro(PKB, 1, PackB) \
	macro(UPB, 1, UnpackB) \
	macro(PK16, 1, Pack16) \
	macro(UP16, 1, Unpack16) \
	macro(BEM, 3, BumpEnv) \
	macro(PKG, 1, PackG) \
	macro(UPG, 1, UnpackG) \
	macro(DP2A, 3, DotProduct2Add) \
	macro(TXL, 3, TextureLod) \
	macro(TXB, 3, TextureBias) \
	macro(TEXBEM, 3, TextureBumpEnv) \
	macro(TXPBEM, 3, TextureProjectionBumpEnv) \
	macro(BEMLUM, 3, BumpEnvLum) \
	macro(REFL, 2, Reflect) \
	macro(TIMESWTEX, 1, TextureTimesW) \
	macro(DP2, 2, DotProduct2) \
	macro(NRM, 1, Normalize) \
	macro(DIV, 2, Divide) \
	macro(DIVSQ, 2, DivideSquareRoot) \
	macro(LIF, 1, Lif) \
	macro(FENCT, 0, Fenct) \
	macro(FENCB, 0, Fencb)
#define FOREACH_FRAGMENT_SHADER_SPECIAL_INSTRUCTION(macro) \
	macro(BRK, 0, Break) \
	macro(CAL, 0, Call) \
	macro(IFE, 0, IfElse) \
	macro(LOOP, 0, Loop) \
	macro(REP, 0, Rep) \
	macro(RET, 0, Return)

//
// Forward Declarations for Visitable Fragment Shader Instructions
//

class FragmentShaderRegisterOperand;
class FragmentShaderSpecialOperand;
class FragmentShaderConstantOperand;
#define FORWARD_DECLARE_INSTRUCTION_CLASS(opcode, nargs, opname) class FragmentShader ## opname ## Instruction;
FOREACH_FRAGMENT_SHADER_INSTRUCTION(FORWARD_DECLARE_INSTRUCTION_CLASS)
FOREACH_FRAGMENT_SHADER_SPECIAL_INSTRUCTION(FORWARD_DECLARE_INSTRUCTION_CLASS)
#undef FORWARD_DECLARE_INSTRUCTION_CLASS

//Macros for visitor pattern implementation
#define RSX_SHADER_VISITABLE() virtual void Accept(ShaderVisitor& visitor) const override { visitor.Visit(*this); }

/** Base class for visitors used to turn parsed RSX shader instructions into something we can actually use */
class ShaderVisitor
{
public:
	virtual void Visit(const FragmentShaderRegisterOperand& op) = 0;
	virtual void Visit(const FragmentShaderSpecialOperand& op) = 0;
	virtual void Visit(const FragmentShaderConstantOperand& op) = 0;
#define DEFINE_VISIT_FUNCTION(opcode, nargs, opname) virtual void Visit(const FragmentShader ## opname ## Instruction& insn) = 0;
	FOREACH_FRAGMENT_SHADER_INSTRUCTION(DEFINE_VISIT_FUNCTION)
	FOREACH_FRAGMENT_SHADER_SPECIAL_INSTRUCTION(DEFINE_VISIT_FUNCTION)
#undef DEFINE_VISIT_FUNCTION
};

/** Base class for registers */
class FragmentShaderOperandBase
{
	bool m_isAbs;
	bool m_isNegative;
	bool m_isFp16;
	u32 m_swizzleMask;
	u32 m_index;
public:
	FragmentShaderOperandBase();

	virtual void Load(u32 operandIndex, u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader);

	virtual void Accept(ShaderVisitor& visitor) const = 0;

	bool UseAbsoluteValue() const { return m_isAbs; }

	bool UseNegative() const { return m_isNegative; }

	bool UseHalfPrecision() const { return m_isFp16; }

	u32 SwizzleMask() const { return m_swizzleMask; }

	u32 RegisterIndex() const { return m_index; }
};

class FragmentShaderRegisterOperand : public FragmentShaderOperandBase
{
public:
	FragmentShaderRegisterOperand();

	virtual void Load(u32 operandIndex, u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

class FragmentShaderSpecialOperand : public FragmentShaderOperandBase
{
	FragmentShaderInputSemantic::Type m_inputSemantic;

	bool m_perspectiveCorrection;

	bool m_useIndexRegister;

	u32 m_loopRegisterValue; // aL+###

public:
	FragmentShaderSpecialOperand();

	virtual void Load(u32 operandIndex, u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	bool IsIndexRegister() const { return m_useIndexRegister; }

	u32 LoopRegisterValue() const { return m_loopRegisterValue; }

	bool IsInputRegister() const { return !m_useIndexRegister; }

	FragmentShaderInputSemantic::Type InputSemantic() const { return m_inputSemantic; }

	bool UsePerspectiveCorrection() const { return m_perspectiveCorrection; }

	RSX_SHADER_VISITABLE();
};

class FragmentShaderConstantOperand : public FragmentShaderOperandBase
{
	u32 m_offset; // Offset (in bytes) to the constant value from the start of the stream
	float m_x;
	float m_y;
	float m_z;
	float m_w;
public:
	FragmentShaderConstantOperand();

	virtual void Load(u32 operandIndex, u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	u32 GetOffset() const { return m_offset; }

	float GetX() const { return m_x; }

	float GetY() const { return m_y; }

	float GetZ() const { return m_z; }

	float GetW() const { return m_w; }

	RSX_SHADER_VISITABLE();
};

/** Base class for parsed RSX fragment shader instructions */
class FragmentShaderInstructionBase
{
	FragmentShaderOpcode::Type m_opcode;

	u32 m_line;

	u32 m_destRegisterIndex;

	u32 m_destWriteMask;

	/** The precision at which this instruction operates */
	FragmentShaderPrecision::Type m_precision;

	bool m_isDestFP16;

	bool m_setConditionFlags;

	u32 m_conditionRegisterSet;

	u32 m_conditionRegisterRead;

	FragmentShaderCondition::Type m_condition;

	u32 m_conditionMask;

	FragmentShaderScale::Type m_scale;

	bool m_biased;

	bool m_saturated;

	bool m_targetConditionRegister;

	u32 m_sampler;

	bool m_hasDestination;

public:
	FragmentShaderInstructionBase(FragmentShaderOpcode::Type opcode);

	virtual ~FragmentShaderInstructionBase() = default;

	/** Get the line number (beginning at 1) of this instruction in the stream */
	u32 GetLineNumber() const { return m_line; }

	/** Retrieve this instruction's opcode */
	FragmentShaderOpcode::Type GetOpcode() const { return m_opcode; }

	/** Retrieve this instruction's destination's register index */
	u32 GetDestinationRegisterIndex() const { return m_destRegisterIndex; }

	u32 GetDestinationWriteMask() const { return m_destWriteMask; }

	/** Is this instruction's destination fp16? */
	bool GetIsDestFP16() const { return m_isDestFP16; }

	/** Get the precision at which the instruction should operate */
	FragmentShaderPrecision::Type GetPrecision() const { return m_precision; }

	bool SetsConditionFlags() const { return m_setConditionFlags; }
	u32 ConditionRegisterSet() const { return m_conditionRegisterSet; }
	u32 ConditionRegisterRead() const { return m_conditionRegisterRead; }
	FragmentShaderCondition::Type Condition() const { return m_condition; }
	u32 ConditionMask() const { return m_conditionMask; }
	FragmentShaderScale::Type Scale() const { return m_scale; }
	bool IsBiased() const { return m_biased; }
	bool IsSaturated() const { return m_saturated; }
	bool TargetsConditionRegister() const { return m_targetConditionRegister; }
	u32 TextureSampler() const { return m_sampler; }
	bool HasDestination() const { return m_hasDestination; }

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader);

	virtual void Accept(ShaderVisitor& visitor) const = 0;
};

typedef std::vector<std::unique_ptr<FragmentShaderInstructionBase>> FragmentShaderInstructionList;

class FragmentShaderInstruction0 : public FragmentShaderInstructionBase
{
public:
	FragmentShaderInstruction0(FragmentShaderOpcode::Type opcode) : FragmentShaderInstructionBase(opcode) {}

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;
};

/** Shader Instruction with a single source register */
class FragmentShaderInstruction1 : public FragmentShaderInstruction0
{
	/** first operand */
	std::unique_ptr<FragmentShaderOperandBase> m_operand1;

public:
	FragmentShaderInstruction1(FragmentShaderOpcode::Type opcode) : FragmentShaderInstruction0(opcode), m_operand1(){}

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	const FragmentShaderOperandBase& Operand1() const { return *m_operand1; }
};

/** Shader Instruction with two used source registers */
class FragmentShaderInstruction2 : public FragmentShaderInstruction1
{
	/** second operand */
	std::unique_ptr<FragmentShaderOperandBase> m_operand2;

public:
	FragmentShaderInstruction2(FragmentShaderOpcode::Type opcode) : FragmentShaderInstruction1(opcode), m_operand2() {}

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	const FragmentShaderOperandBase& Operand2() const { return *m_operand2; }
};

/** Shader Instruction with three used source registers */
class FragmentShaderInstruction3 : public FragmentShaderInstruction2
{
	/** third operand */
	std::unique_ptr<FragmentShaderOperandBase> m_operand3;

public:
	FragmentShaderInstruction3(FragmentShaderOpcode::Type opcode) : FragmentShaderInstruction2(opcode), m_operand3() {}

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	const FragmentShaderOperandBase& Operand3() const { return *m_operand3; }
};

#define DEFINE_SHADER_INSTRUCTION_CLASS(opc, nargs, opname) \
class FragmentShader ## opname ## Instruction : public FragmentShaderInstruction ## nargs{ \
public: \
	FragmentShader ## opname ## Instruction(FragmentShaderOpcode::Type opcode) : FragmentShaderInstruction ## nargs(opcode) {} \
	virtual void Accept(ShaderVisitor& visitor) const override { visitor.Visit(*this); } \
};
FOREACH_FRAGMENT_SHADER_INSTRUCTION(DEFINE_SHADER_INSTRUCTION_CLASS)
#undef DEFINE_SHADER_INSTRUCTION_CLASS

/** Fragment Shader BRK Instruction */
class FragmentShaderBreakInstruction : public FragmentShaderInstructionBase
{
public:
	FragmentShaderBreakInstruction(FragmentShaderOpcode::Type opcode);

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

/** Fragment Shader CAL Instruction */
class FragmentShaderCallInstruction : public FragmentShaderInstructionBase
{
	u32 m_targetLine;

public:
	FragmentShaderCallInstruction(FragmentShaderOpcode::Type opcode);

	/** Call target line number */
	u32 GetTargetLine() const { return m_targetLine; }

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

/** Fragment Shader LOOP Instruction */
class FragmentShaderLoopInstruction : public FragmentShaderInstructionBase
{
	/** Instructions to execute in each iteration of the loop */
	FragmentShaderInstructionList m_instructions;

	u32 m_initialValue;
	u32 m_increment;
	u32 m_endValue;

public:
	FragmentShaderLoopInstruction(FragmentShaderOpcode::Type opcode);

	/** Initial value for loop counter */
	u32 GetInitialValue() const { return m_initialValue; }

	/** How much to increase the counter by each iteration */
	u32 GetIncrement() const { return m_increment; }

	/** Loop stops when the counter hits this value */
	u32 GetEndValue() const { return m_endValue; }

	FragmentShaderInstructionList& GetInstructions() { return m_instructions; }
	const FragmentShaderInstructionList& GetInstructions() const { return m_instructions; }

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

/** Fragment Shader REP Instruction */
class FragmentShaderRepInstruction : public FragmentShaderInstructionBase
{
	/** Instructions to execute each iteration */
	FragmentShaderInstructionList m_instructions;

	u32 m_loopCount;

public:
	FragmentShaderRepInstruction(FragmentShaderOpcode::Type opcode);

	/** How many times to repeat the instructions */
	u32 GetLoopCount() const { return m_loopCount; }

	FragmentShaderInstructionList& GetInstructions() { return m_instructions; }
	const FragmentShaderInstructionList& GetInstructions() const { return m_instructions; }

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

/** Fragment Shader RET Instruction */
class FragmentShaderReturnInstruction : public FragmentShaderInstructionBase
{
public:
	FragmentShaderReturnInstruction(FragmentShaderOpcode::Type opcode);

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

/** Fragment Shader IFE Instruction */
class FragmentShaderIfElseInstruction : public FragmentShaderInstructionBase
{
	/** Instructions to execute when the condition is true */
	FragmentShaderInstructionList m_if_instructions;

	/** Instructions to execute when the condition is false */
	FragmentShaderInstructionList m_else_instructions;

public:
	FragmentShaderIfElseInstruction(FragmentShaderOpcode::Type opcode) : FragmentShaderInstructionBase(opcode) {}

	FragmentShaderInstructionList& GetIfInstructions() { return m_if_instructions; }
	const FragmentShaderInstructionList& GetIfInstructions() const { return m_if_instructions; }

	FragmentShaderInstructionList& GetElseInstructions() { return m_else_instructions; }
	const FragmentShaderInstructionList& GetElseInstructions() const { return m_else_instructions; }

	virtual void Load(u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader) override;

	RSX_SHADER_VISITABLE();
};

/** Utility class for parsing in-memory RSX Fragment Shader binaries into an intermediate representation. */
class FragmentShaderParser
{
	/** Information about the location of the in-memory shader */
	u32 m_programAddr;

	/** Calculated size in bytes of the shader binary */
	u32 m_size;

	utility::hashing::HashValue32 m_hash;

public:
	FragmentShaderParser(const RSXShaderProgram& shader_binary);

	/** Parse the fragment shader into a list of instructions */
	FragmentShaderInstructionList Parse();

	u32 GetSize() const { return m_size; }

	utility::hashing::HashValue32 GetHash() const { return m_hash; }
};

#undef RSX_SHADER_VISITABLE

} // namespace rsx
} // namespace rpcs3