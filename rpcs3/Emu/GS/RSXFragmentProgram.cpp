#include "stdafx.h"
#include "RSXFragmentProgram.h"
#include <stack>
#include <map>

namespace rpcs3 {
namespace rsx {

namespace { // Internal Implementation Stuff
	union OPDEST
	{
		u32 HEX;

		struct
		{
			u32 end : 1; // Set to 1 if this is the last instruction
			u32 dest_reg : 6; // destination register index
			u32 fp16 : 1; // is the destination a half register
			u32 set_cond : 1; // does this instruction modify condition flags: see src0.condition.modified_index
			u32 write_mask : 4; // write mask, low bit = x, high bit = w, 1 = write it, 0 = no write
			u32 input_semantic : 4; // semantic of used input register
			u32 tex_num : 4; // sampler index
			u32 exp_tex : 1; // _bx2
			u32 precision : 2; // precision this instruction runs at
			u32 opcode : 6; // low 6 bits of the opcode
			u32 no_dest : 1; // if 0, destination is a normal register, if 1 the destination is the condition flags
			u32 saturate : 1; // _sat
		};
	};

	union GenericSrcOperand
	{
		u32 HEX;

		struct {
			u32 reg_type : 2;
			u32 tmp_reg_index : 6;
			u32 fp16 : 1;
			u32 swizzle_mask : 8;
			u32 neg : 1;
			u32 abs_1 : 1; // Stores abs for src1 and src2
			u32: 10;
			u32 abs_0 : 1; // Stores abs for src0
			u32:2;
		};
	};

	union SRC0
	{
		u32 HEX;

		struct
		{
			u32 reg_type : 2;
			u32 tmp_reg_index : 6;
			u32 fp16 : 1;
			u32 swizzle_x : 2;
			u32 swizzle_y : 2;
			u32 swizzle_z : 2;
			u32 swizzle_w : 2;
			u32 neg : 1;
			u32 exec_if_lt : 1;
			u32 exec_if_eq : 1;
			u32 exec_if_gr : 1;
			u32 cond_swizzle_x : 2;
			u32 cond_swizzle_y : 2;
			u32 cond_swizzle_z : 2;
			u32 cond_swizzle_w : 2;
			u32 abs : 1;
			u32 cond_mod_reg_index : 1;
			u32 cond_reg_index : 1;
		};

		struct {
			u32: 18;
			u32 value : 3;
			u32 swizzle : 8; // swizzle mask for condition register
			u32:1;
			u32 condition_index : 1; // if 1 use CC1, 0 is default
			u32 modified_index : 1; // if 1 modifies CC1, else modifies CC0 (if dst.set_cond is set)
		} condition;
	};

	union SRC1
	{
		u32 HEX;

		struct
		{
			u32 reg_type : 2;
			u32 tmp_reg_index : 6;
			u32 fp16 : 1;
			u32 swizzle_x : 2;
			u32 swizzle_y : 2;
			u32 swizzle_z : 2;
			u32 swizzle_w : 2;
			u32 neg : 1;
			u32 abs : 1;
			u32 input_mod_src0 : 3;
			u32: 6;
			u32 scale : 3;
			u32 opcode_is_branch : 1;
		};

		struct {
			u32 reg_type : 2;
			u32 end_counter : 8; // end counter for a LOOP, or rep count for a REP
			u32 init_counter : 8; // initial counter value for a LOOP
			u32 : 1;
			u32 increment : 8; // increment per loop for a LOOP
			u32 : 4;
			u32 opcode_is_branch : 1;
		} loop;

		struct {
			u32 reg_type : 2;
			u32 if_else_line : 17;	// for ifelse instructions, this is the position of the else
			u32 : 13;
		} ifelse;

		struct {
			u32 reg_type : 2;
			u32 target : 17;	// call target line for CAL instructions
			u32 : 13;
		} call;
	};

	union SRC2
	{
		u32 HEX;

		struct
		{
			u32 reg_type : 2;
			u32 tmp_reg_index : 6;
			u32 fp16 : 1;
			u32 swizzle_x : 2;
			u32 swizzle_y : 2;
			u32 swizzle_z : 2;
			u32 swizzle_w : 2;
			u32 neg : 1;
			u32 abs : 1;
			u32 addr_reg : 11;
			u32 use_index_reg : 1;
			u32 perspective_correction_disabled : 1; // if 1, pc is disabled (g[X]), else it's enabled (default, f[X]) - but apparently it does nothing on real hardware?
		};

		struct {
			u32 reg_type : 2;
			u32 loop_end : 17; // for this ifelse/loop/rep, this is the position of the endif/endloop/endrep
			u32 : 13;
		} loop;
	};

	//
	// Map operands to factory methods
	//

	template<typename T>
	FragmentShaderOperandBase* OperandFactory() { return new T(); }

	#define OPERAND(operandType, operandClass) { FragmentShaderOperandType::##operandType, OperandFactory<operandClass> }
	struct OperandSettings {
		FragmentShaderOperandType::Type operandType;
		std::function<FragmentShaderOperandBase*()> factory;
	} operand_settings[] {
		OPERAND(Register, FragmentShaderRegisterOperand),
		OPERAND(Special, FragmentShaderSpecialOperand),
		OPERAND(Constant, FragmentShaderConstantOperand)
	};
	#undef OPERAND

	//
	// Map instructions to factory methods
	//

	template<typename T>
	FragmentShaderInstructionBase* InstructionFactory(FragmentShaderOpcode::Type opcode) {
		return new T(opcode);
	}

	#define INSN(opcode, nargs, name) { FragmentShaderOpcode::##opcode, { FragmentShaderOpcode::##opcode, nargs, InstructionFactory<FragmentShader ## name ## Instruction> }},
	struct InsnSettings {
		FragmentShaderOpcode::Type opcode;
		u32 nArgs;
		std::function<FragmentShaderInstructionBase*(FragmentShaderOpcode::Type)> factory;
		InsnSettings() = default;
		InsnSettings(FragmentShaderOpcode::Type opc, u32 args, std::function<FragmentShaderInstructionBase*(FragmentShaderOpcode::Type)> f) : opcode(opc), nArgs(args), factory(f) {}
	};
	std::map<FragmentShaderOpcode::Type, InsnSettings> instruction_settings = {
		FOREACH_FRAGMENT_SHADER_INSTRUCTION(INSN)
		FOREACH_FRAGMENT_SHADER_SPECIAL_INSTRUCTION(INSN)
	};
	#undef INSN

	const u32 kInvalidLineNumber = 0xffffffff;

	struct FragmentShaderParserContext
	{
		FragmentShaderInstructionList* insn_list;
		FragmentShaderInstructionList* else_insn_list; // Store reference to second insn list if this context is an if
		FragmentShaderOpcode::Type opcode; // The opcode that spawned this context, or NOP for the base context
		u32 line_number_else; // Line number where this context ends, for an if
		u32 line_number_end; // Line number where this context ends

		FragmentShaderParserContext()
			: opcode(FragmentShaderOpcode::NOP),
			line_number_else(kInvalidLineNumber),
			line_number_end(kInvalidLineNumber),
			insn_list(nullptr),
			else_insn_list(nullptr)
		{}
		FragmentShaderParserContext(const FragmentShaderOpcode::Type op, u32 line_end, FragmentShaderInstructionList* instructions)
			: opcode(op),
			line_number_else(kInvalidLineNumber),
			line_number_end(line_end),
			insn_list(instructions),
			else_insn_list(nullptr)
		{}
		FragmentShaderParserContext(const FragmentShaderOpcode::Type op, u32 line_else, u32 line_end, FragmentShaderInstructionList* instructions, FragmentShaderInstructionList* else_instructions)
			: opcode(op),
			line_number_else(line_else),
			line_number_end(line_end),
			insn_list(instructions),
			else_insn_list(else_instructions)
		{
		}
	};
}

/**
 * Reads a DWORD from the stream offset from the current read position
 * RSX shader values are always stored with the high and low words swapped for some reason
 * so we swap them back here before returning the value.
 */
u32 FragmentShaderBinaryReader::ReadDword(int i) {
	u32 result = m_data[i];
	result = ((result & 0xffff0000) >> 16) | ((result & 0xffff) << 16); // Swap high and low words
	return result;
}

/**
 * Reads a float from the stream at a given offset.
 * They're stored with the hi/lo word swap as well so just reuse ReadDword and reinterpret the result
 */
float FragmentShaderBinaryReader::ReadFloat(int i) {
	auto dword = ReadDword(i);
	return *(float*)&dword;
}

FragmentShaderBinaryReader::FragmentShaderBinaryReader(u32 addr)
	: m_data(addr),
	m_bytes_read(0),
	m_line_number(0),
	m_readConstant(false),
	m_inBeginEnd(false)
{ }

/**
 * Called when beginning the processing of a new instruction
 */
void FragmentShaderBinaryReader::BeginInstruction(u32& dst, u32& src0, u32& src1, u32& src2) {
	// If they didn't call EndInstruction() manually, we need to do it here
	if (m_inBeginEnd) { EndInstruction(); }

	m_inBeginEnd = true;
	dst = ReadDword(0);
	src0 = ReadDword(1);
	src1 = ReadDword(2);
	src2 = ReadDword(3);
}

/**
 * Assuming the data pointer always points to the start of an instruction, the
 * second 'line' (dwords 4-7) will be a 4-component floating point vector.
 * This constant data is not on every instruction, only on ones with constant operands.
 * Calling this function marks the instruction as having this constant data, and will
 * cause it to be skipped over when EndInstruction() is called, so only call this
 * if you are sure the instruction has valid constant data.
 */
void FragmentShaderBinaryReader::ReadVec4(float& x, float& y, float& z, float& w) {
	m_readConstant = true;
	x = ReadFloat(4);
	y = ReadFloat(5);
	z = ReadFloat(6);
	w = ReadFloat(7);
}

/**
 * Ends processing of the current instruction. Updates data pointer, bytes read,
 * and line number for the next instruction.
 */
void FragmentShaderBinaryReader::EndInstruction() {
	if (!m_inBeginEnd) return;

	// Skip Instruction Line
	m_data.Skip(kBytesPerLine);
	m_bytes_read += kBytesPerLine;
	m_line_number++;

	// Skip Constant Line, if exists
	if (m_readConstant) {
		m_data.Skip(kBytesPerLine);
		m_bytes_read += kBytesPerLine;
		m_line_number++;
		m_readConstant = false;
	}

	m_inBeginEnd = false;
}

FragmentShaderParser::FragmentShaderParser(const RSXShaderProgram& shader_binary)
	: m_size(0),
	m_hash(0)
{
	m_programAddr = shader_binary.addr;
}

FragmentShaderInstructionList FragmentShaderParser::Parse()
{
	FragmentShaderInstructionList result;
	std::stack<FragmentShaderParserContext> contextStack;
	contextStack.emplace(); // Create our default context and push it onto the stack

	FragmentShaderParserContext* context = &contextStack.top();
	context->insn_list = &result;

	FragmentShaderBinaryReader reader(m_programAddr);

	OPDEST dst;
	SRC0 src0;
	SRC1 src1;
	SRC2 src2;

	dst.HEX = 0;

	while (!dst.end)
	{
		const u32 line_number = reader.GetLineNumber();

		// Check if we need to adjust our context
		if (line_number == context->line_number_end)
		{
			// Destroy this context and return to the previous one on the stack
			// We can't leave the current context if we don't have another one to switch to
			assert(contextStack.size() > 1);

			contextStack.pop();
			context = &contextStack.top();
		}
		else if (line_number == context->line_number_else)
		{
			// Start placing parsed instructions into the else_insn_list
			context->insn_list = context->else_insn_list;
		}

		reader.BeginInstruction(dst.HEX, src0.HEX, src1.HEX, src2.HEX);

		const bool isBranch = src1.opcode_is_branch;
		const FragmentShaderOpcode::Type opcode = (FragmentShaderOpcode::Type)(dst.opcode | (src1.opcode_is_branch << 6));

		auto settings_iter = instruction_settings.find(opcode);
		if (settings_iter == instruction_settings.end()) {
			assert(false);
			break;
		}

		const InsnSettings& settings = settings_iter->second;

		// Create our instruction
		std::unique_ptr<FragmentShaderInstructionBase> insn(settings.factory(opcode));

		// Load data for the instruction
		insn->Load(dst.HEX, src0.HEX, src1.HEX, src2.HEX, reader);

		// If list instruction spawns a new context, create it at the top of our stack
		switch (opcode)
		{
		case FragmentShaderOpcode::IFE: {
			FragmentShaderIfElseInstruction* ifeInsn = dynamic_cast<FragmentShaderIfElseInstruction*>(insn.get());
			FragmentShaderParserContext nextContext(opcode, src1.ifelse.if_else_line, src2.loop.loop_end, &ifeInsn->GetIfInstructions(), &ifeInsn->GetElseInstructions());
			contextStack.push(nextContext);
			break;
		}
		case FragmentShaderOpcode::REP: {
			FragmentShaderRepInstruction* repInsn = dynamic_cast<FragmentShaderRepInstruction*>(insn.get());
			FragmentShaderParserContext nextContext(opcode, src2.loop.loop_end, &repInsn->GetInstructions());
			contextStack.push(nextContext);
			break;
		}
		case FragmentShaderOpcode::LOOP: {
			FragmentShaderLoopInstruction* loopInsn = dynamic_cast<FragmentShaderLoopInstruction*>(insn.get());
			FragmentShaderParserContext nextContext(opcode, src2.loop.loop_end, &loopInsn->GetInstructions());
			contextStack.push(nextContext);
			break;
		}
		}

		context->insn_list->push_back(std::move(insn));
		context = &contextStack.top();

		reader.EndInstruction();
	}

	m_size = reader.GetBytesRead();

	// Calculate a 32-bit hash of the program
	auto data_start = &Memory[m_programAddr];
	utility::hashing::Murmur3_32(data_start, m_size, 0, &m_hash);

	return result;
}

/**
 * Instruction Loaders
 */
void FragmentShaderInstructionBase::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	OPDEST dst;
	dst.HEX = dstHex;
	SRC0 src0; src0.HEX = src0Hex;
	SRC1 src1; src1.HEX = src1Hex;

	m_line = reader.GetLineNumber();
	m_destRegisterIndex = dst.dest_reg;
	m_destWriteMask = dst.write_mask;
	m_precision = (FragmentShaderPrecision::Type)dst.precision;
	m_isDestFP16 = (dst.fp16 == 1);
	m_setConditionFlags = dst.set_cond;
	m_conditionRegisterSet = src0.condition.modified_index;
	m_conditionRegisterRead = src0.condition.condition_index;
	m_condition = (FragmentShaderCondition::Type)src0.condition.value;
	m_conditionMask = src0.condition.swizzle;
	m_scale = (FragmentShaderScale::Type)src1.scale;
	m_biased = dst.exp_tex;
	m_saturated = dst.saturate;
	m_targetConditionRegister = dst.no_dest;
	m_sampler = dst.tex_num;
	m_hasDestination = (instruction_settings[m_opcode].nArgs > 0);
}

void FragmentShaderInstruction0::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderInstruction1::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstruction0::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);

	SRC0 src0;
	src0.HEX = src0Hex;

	// Load First Source
	m_operand1.reset(operand_settings[src0.reg_type].factory());
	m_operand1->Load(1, dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderInstruction2::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstruction1::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);

	SRC1 src1;
	src1.HEX = src1Hex;

	// Load Second Source
	m_operand2.reset(operand_settings[src1.reg_type].factory());
	m_operand2->Load(2, dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderInstruction3::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstruction2::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);

	SRC2 src2;
	src2.HEX = src2Hex;

	// Load Third Source
	m_operand3.reset(operand_settings[src2.reg_type].factory());
	m_operand3->Load(3, dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderCallInstruction::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);

	SRC1 src1;
	src1.HEX = src1Hex;

	m_targetLine = src1.call.target;
}

void FragmentShaderLoopInstruction::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	SRC1 src1;
	src1.HEX = src1Hex;

	m_initialValue = src1.loop.init_counter;
	m_endValue = src1.loop.end_counter;
	m_increment = src1.loop.increment;

	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderRepInstruction::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	SRC1 src1;
	src1.HEX = src1Hex;

	m_loopCount = src1.loop.end_counter;
	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderIfElseInstruction::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderBreakInstruction::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);
}

void FragmentShaderReturnInstruction::Load(u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader) {
	FragmentShaderInstructionBase::Load(dstHex, src0Hex, src1Hex, src2Hex, reader);
}

FragmentShaderInstructionBase::FragmentShaderInstructionBase(FragmentShaderOpcode::Type opcode)
	: m_opcode(opcode),
	m_destRegisterIndex(0),
	m_destWriteMask(0),
	m_precision(FragmentShaderPrecision::Full),
	m_isDestFP16(false),
	m_setConditionFlags(false),
	m_conditionRegisterSet(0),
	m_conditionRegisterRead(0),
	m_scale(FragmentShaderScale::None),
	m_biased(false),
	m_saturated(false),
	m_targetConditionRegister(false),
	m_line(0),
	m_sampler(0),
	m_condition(FragmentShaderCondition::True),
	m_conditionMask(0)
{
}

FragmentShaderBreakInstruction::FragmentShaderBreakInstruction(FragmentShaderOpcode::Type opcode)
	: FragmentShaderInstructionBase(opcode)
{
}

FragmentShaderCallInstruction::FragmentShaderCallInstruction(FragmentShaderOpcode::Type opcode)
	: FragmentShaderInstructionBase(opcode),
	m_targetLine(0)
{
}

FragmentShaderLoopInstruction::FragmentShaderLoopInstruction(FragmentShaderOpcode::Type opcode)
	: FragmentShaderInstructionBase(opcode),
	m_increment(0),
	m_initialValue(0),
	m_endValue(0)
{
}

FragmentShaderRepInstruction::FragmentShaderRepInstruction(FragmentShaderOpcode::Type opcode)
	: FragmentShaderInstructionBase(opcode),
	m_loopCount(0)
{
}

FragmentShaderReturnInstruction::FragmentShaderReturnInstruction(FragmentShaderOpcode::Type opcode)
	: FragmentShaderInstructionBase(opcode)
{
}

FragmentShaderOperandBase::FragmentShaderOperandBase()
	: m_isAbs(false),
	m_isNegative(false),
	m_isFp16(false),
	m_swizzleMask(0),
	m_index(0)
{}

void FragmentShaderOperandBase::Load(u32 operandIndex, u32 dstHex, u32 src0Hex, u32 src1Hex, u32 src2Hex, FragmentShaderBinaryReader& reader)
{
	GenericSrcOperand generic;
	switch (operandIndex)
	{
	case 1:
		generic.HEX = src0Hex;
		m_isAbs = (generic.abs_0 == 1);
		break;
	case 2:
		generic.HEX = src1Hex;
		m_isAbs = (generic.abs_1 == 1);
		break;
	case 3:
		generic.HEX = src2Hex;
		m_isAbs = (generic.abs_1 == 1);
		break;
	}
	m_isNegative = (generic.neg == 1);
	m_isFp16 = (generic.fp16 == 1);
	m_swizzleMask = generic.swizzle_mask;
	m_index = generic.tmp_reg_index;
}

FragmentShaderRegisterOperand::FragmentShaderRegisterOperand()
	: FragmentShaderOperandBase()
{}

void FragmentShaderRegisterOperand::Load(u32 operandIndex, u32 dstHex, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader)
{
	FragmentShaderOperandBase::Load(operandIndex, dstHex, src0, src1, src2, reader);
}

FragmentShaderSpecialOperand::FragmentShaderSpecialOperand()
	: FragmentShaderOperandBase(),
	m_inputSemantic(FragmentShaderInputSemantic::WPOS),
	m_perspectiveCorrection(false),
	m_useIndexRegister(false),
	m_loopRegisterValue(0)
{}

void FragmentShaderSpecialOperand::Load(u32 operandIndex, u32 dstHex, u32 src0, u32 src1, u32 src2Hex, FragmentShaderBinaryReader& reader)
{
	FragmentShaderOperandBase::Load(operandIndex, dstHex, src0, src1, src2Hex, reader);
	OPDEST dst;
	dst.HEX = dstHex;
	SRC2 src2;
	src2.HEX = src2Hex;

	m_inputSemantic = (FragmentShaderInputSemantic::Type)dst.input_semantic;
	m_perspectiveCorrection = (src2.perspective_correction_disabled == 0);
	m_useIndexRegister = (src2.use_index_reg == 1);
	m_loopRegisterValue = (src2.addr_reg);
}

FragmentShaderConstantOperand::FragmentShaderConstantOperand()
	: FragmentShaderOperandBase(),
	m_offset(0),
	m_x(0),
	m_y(0),
	m_z(0),
	m_w(0)
{
}

void FragmentShaderConstantOperand::Load(u32 operandIndex, u32 dst, u32 src0, u32 src1, u32 src2, FragmentShaderBinaryReader& reader)
{
	FragmentShaderOperandBase::Load(operandIndex, dst, src0, src1, src2, reader);
	m_offset = reader.GetBytesRead();
	reader.ReadVec4(m_x, m_y, m_z, m_w);
}

} // namespace rsx
} // namespace rpcs3