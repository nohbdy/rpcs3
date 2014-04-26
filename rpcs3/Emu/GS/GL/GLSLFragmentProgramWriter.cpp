#include "stdafx.h"
#include "GLSLFragmentProgramWriter.h"

namespace rpcs3 {
namespace rsx {
namespace gl {

namespace {
	const u32 kMaximumIndentation = 6;
	const std::string kIndentation[kMaximumIndentation + 1] {
		"",
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t"
	};

	static const std::string kConditionMap[] {
		"false",
		"lessThan",
		"equal",
		"lessThanEqual",
		"greaterThan",
		"notEqual",
		"greaterThanEqual",
		"true"
	};

	static const std::string kIfConditionMap[] {
		"false",
		" < ",
		" == ",
		" <= ",
		" > ",
		" != ",
		" >= ",
		"true"
	};

	static const std::string kWriteMasks[] {
		"",     // 0000
		".x",   // 0001
		".y",   // 0010
		".xy",  // 0011
		".z",   // 0100
		".xz",  // 0101
		".yz",  // 0110
		".xyz", // 0111
		".w",   // 1000
		".xw",  // 1001
		".yw",  // 1010
		".xyw", // 1011
		".zw",  // 1100
		".xzw", // 1101
		".yzw", // 1110
		"",     // 1111
	};

	/** Array of masks used to limit the output to the number of values the destination uses */
	static const std::string kVectorCast[] {
		"",     // 0000
		".x",   // 0001
		".x",   // 0010
		".xy",  // 0011
		".x",   // 0100
		".xy",  // 0101
		".xy",  // 0110
		".xyz", // 0111
		".x",   // 1000
		".xy",  // 1001
		".xy",  // 1010
		".xyz", // 1011
		".xy",  // 1100
		".xyz", // 1101
		".xyz", // 1110
		"",     // 1111
	};

	// Map a destination write mask to the number of components written by that mask
	static const u32 kWriteComponents[] { 4, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

	static const std::string kInputRegisters[] =
	{
		"gl_Position",
		"col0", "col1",
		"fogc",
		"tc0", "tc1", "tc2", "tc3", "tc4", "tc5", "tc6", "tc7", "tc8", "tc9", "face_sign"
	};

	/** Return true if the condition mask uses the same condition component for each channel */
	inline bool SingleComponentCondition(u32 conditionMask)
	{
		const u32 allX = 0x00;  // binary: 00000000
		const u32 allY = 0x55;  // binary: 01010101
		const u32 allZ = 0xAA;	// binary: 10101010
		const u32 allW = 0xFF;	// binary: 11111111
		if ((conditionMask == allX) || (conditionMask == allY) || (conditionMask == allZ) || (conditionMask == allW)) {
			return true;
		}
		return false;
	}

	inline bool CanDoSingleInstruction(const FragmentShaderInstructionBase& insn)
	{
		if (insn.Condition() != FragmentShaderCondition::True) {
			auto conditionMask = insn.ConditionMask();
			// If we use the same condition for all channels, we can do it all in a single line..
			// If one or more of the components uses a different condition component, we need multiple instructions
			return SingleComponentCondition(insn.ConditionMask());
		} else {
			return true; // Condition == True, so there's no conditional writes
		}
	}

	inline u32 NumWrittenComponents(const FragmentShaderInstructionBase& insn)
	{
		return kWriteComponents[insn.GetDestinationWriteMask()];
	}
}

GLSLFragmentProgramWriter::GLSLFragmentProgramWriter(const FragmentShaderInstructionList& instructions, FragmentProgramControl control)
	: m_control(control),
	m_indentationLevel(1),
	m_currentComponent(0),
	m_singleComponent(false),
	m_useWriteMask(false),
	m_writeMask(0),
	m_instructions(instructions)
{
}

void GLSLFragmentProgramWriter::ProcessInstructionList(const FragmentShaderInstructionList& instructions) {
	for (const auto& insn_ptr : instructions) {
		auto opcode = insn_ptr->GetOpcode();
		const auto& insn = *insn_ptr;
		m_singleComponent = false;
		m_currentComponent = 0;

		switch (opcode)
		{
		case FragmentShaderOpcode::NOP:
		case FragmentShaderOpcode::FENCB:
		case FragmentShaderOpcode::FENCT:
			// do nothing for NOP, FENCT, FENCB
			break;
		case FragmentShaderOpcode::IFE:
		case FragmentShaderOpcode::LOOP:
		case FragmentShaderOpcode::REP:
			// Ifs and Loops handle their own beginning and ending
			insn_ptr->Accept(*this);
			break;
		default:
			if (CanDoSingleInstruction(insn)) {
				// Other instructions may be wrapped in stuff
				PreInstruction(insn);
				insn_ptr->Accept(*this);
				PostInstruction(insn);
			} else {
				auto nComponents = NumWrittenComponents(insn);
				m_singleComponent = true;
				for (u32 i = 0; i < nComponents; ++i) {
					m_currentComponent = i;
					PreInstruction(insn);
					insn_ptr->Accept(*this);
					PostInstruction(insn);
				}
			}
			break;
		}
	}
}

std::string GLSLFragmentProgramWriter::Process() {
	// Build our main code block by processing all the instructions in order
	ProcessInstructionList(m_instructions);

	// Storage for complete GLSL fragment shader
	std::string result("#version 330\n\n");

	// Write input parameters
	for (auto semantic : m_usedInputs) {
		result.append("in vec4 ");
		result.append(kInputRegisters[semantic]);
		result.append(";\n");
	}

	// Write defaults
	for (auto reg : m_defaultRegisters) {
		if (reg[0] == 'h') result.append("mediump ");
		result.append("vec4 ");
		result.append(reg);
		result.append(" = vec4(0, 0, 0, 0);\n");
	}

	for (auto reg : m_conditionRegisters) {
		if (reg[0] == 'h') result.append("mediump ");
		result.append("vec4 ");
		result.append(reg);
		result.append(" = vec4(0, 0, 0, 0);\n");
	}

	// Write outputs
	for (auto kvp : m_usedOutputs) {
		if (kvp.second >= 0) {
			result.append("layout(location = ");
			result.append(std::to_string(kvp.second));
			result.append(") ");
		}
		result.append("out vec4 ");
		result.append(kvp.first);
		result.append(";\n");
	}

	result.append("layout(location = 0) out vec4 ocol;\n\n");

	// Write main function
	result.append("void main()\n{\n");
	result.append(m_str.str());

	if (m_control.OutputFromR0()) {
		result.append("\tocol = r0;\n");
	} else {
		result.append("\tocol = h0;\n");
	}

	if (m_control.DepthReplace()) {
		result.append("\tgl_FragDepth = r1.z;\n");
	}

	result.append("}\n");

	return result;
}

void GLSLFragmentProgramWriter::Indent()
{
	// We cannot indent any more
	if (m_indentationLevel == kMaximumIndentation) {
		return;
	}

	m_indentationLevel++;
}

void GLSLFragmentProgramWriter::UnIndent()
{
	// We cannot indent any less
	if (m_indentationLevel == 0) {
		return;
	}

	m_indentationLevel--;
}

void GLSLFragmentProgramWriter::TrackRegister(const std::string& registerName, u32 index, bool isFp16)
{
	if (index >= 2 && index <= 4) {
		// Output register
		int location = -1;
		if (!isFp16) { location = index - 1; }
		m_usedOutputs.emplace(registerName, location);
	}
	else {
		m_defaultRegisters.emplace(registerName);
	}
}

void GLSLFragmentProgramWriter::WriteSwizzleMask(u32 mask)
{
	if (m_singleComponent) {
		WriteSwizzleMask(mask, m_currentComponent, 1 /* count */);
	} else {
		WriteSwizzleMask(mask, 0 /* skip */, 4 /* max count */);
	}
}

void GLSLFragmentProgramWriter::WriteSwizzleMask(u32 mask, u32 skip, u32 count)
{
	static const u32 kMask = 0x3; // 2-bit mask
	static const u32 kShiftX = 0;
	static const u32 kShiftY = 2;
	static const u32 kShiftZ = 4;
	static const u32 kShiftW = 6;

	/** If mask == kPassThroughMask then there is no swizzling being done */
	static const u32 kPassThroughMask = 0xE4;
	static const char kComponents[4] = { 'x', 'y', 'z', 'w' };

	// Swizzle Mask == '.xyzw' which is the same as adding nothing
	if ((mask == kPassThroughMask) && // mask is passthrough
		(!m_useWriteMask || (m_writeMask == 0xF)) && // and destination takes all components
		((skip == 0) && (count >= 4))) // and we're supposed to write all the components
	{ return; }

	u32 written = 0;
	u32 skipped = 0;

	m_str << '.';
	if (m_useWriteMask) {
		u32 writeMaskMask = 0x1;
		for (int i = 0; i < 4; i++) {
			if (m_writeMask & writeMaskMask) { // We only concern ourselves with components that are actually being written
				if (skipped < skip) { // Check if we've skipped enough
					++skipped;
					continue;
				}
				if (written < count) // Check if we've written enough
				{
					written++;
					m_str << kComponents[(mask >> (i * 2)) & kMask];
				}
			}
			writeMaskMask <<= 1; // shift writeMaskMask to the left by 1 bit
		}
	} else {
		for (int i = 0; i < 4; i++) {
			if (skipped < skip) { // Check if we've skipped enough
				++skipped;
				continue;
			}
			if (written < count) // Check if we've written enough
			{
				written++;
				m_str << kComponents[(mask >> (i * 2)) & kMask];
			}
		}
	}
	return;
}

void GLSLFragmentProgramWriter::PreInstruction(const FragmentShaderInstructionBase& insn)
{
	if (insn.HasDestination()) {
		m_useWriteMask = true;
		m_writeMask = insn.GetDestinationWriteMask();
	} else {
		m_useWriteMask = false;
	}

	m_str << kIndentation[m_indentationLevel];

	if (insn.Condition() != FragmentShaderCondition::True) {
		if (m_singleComponent || SingleComponentCondition(insn.ConditionMask())) {
			// Only use a single component of the condition for this if, use a float comparison
			// if (rc.x < 0.0) {
			m_str << "if (rc";
			// write the condition register index if it's > 0
			if (insn.ConditionRegisterRead() > 0) { m_str << insn.ConditionRegisterRead(); }
			// write component
			WriteSwizzleMask(insn.ConditionMask(), m_currentComponent, 1);
			m_str << kIfConditionMap[insn.Condition()];
			m_str << "0.0) {" << std::endl;
		} else {
			// if-statement block
			m_str << "if (all(";
			m_str << kConditionMap[insn.Condition()];
			m_str << "(rc";
			if (insn.ConditionRegisterRead() > 0) { m_str << insn.ConditionRegisterRead(); }
			WriteSwizzleMask(insn.ConditionMask());
			m_str << ", vec4(0.0)))) {" << std::endl;
		}

		Indent();
		m_str << kIndentation[m_indentationLevel];
	}

	if (insn.HasDestination()) {
		std::string destinationRegisterName(insn.GetIsDestFP16() ? "h" : "r");
		if (insn.TargetsConditionRegister()) {
			destinationRegisterName.append("c");
			if (insn.ConditionRegisterSet() != 0) {
				destinationRegisterName.append(std::to_string(insn.ConditionRegisterSet()));
			}
			m_conditionRegisters.emplace(destinationRegisterName);
		}
		else {
			destinationRegisterName.append(std::to_string(insn.GetDestinationRegisterIndex()));
			TrackRegister(destinationRegisterName, insn.GetDestinationRegisterIndex(), insn.GetIsDestFP16());
		}
		m_str << destinationRegisterName;
		// Write destination mask
		if (m_singleComponent) {
			if (m_writeMask == 0xF) {
				m_str << '.' << ('x' + m_currentComponent);
			} else {
				m_str << '.' << kWriteMasks[m_writeMask][m_currentComponent + 1];
			}
		} else {
			m_str << kWriteMasks[m_writeMask];
		}
		m_str << " = ";

		if (insn.IsBiased()) {
			m_str << "(";
		}

		if (insn.IsSaturated()) {
			m_str << "clamp(";
		} else {
			switch (insn.GetPrecision())
			{
			case FragmentShaderPrecision::Fixed12:
			case FragmentShaderPrecision::Fixed9:
				m_str << "clamp(";
				break;
			}
		}

		if (insn.Scale() != FragmentShaderScale::None)
		{
			m_str << "((";
		}
	}
}

void GLSLFragmentProgramWriter::PostInstruction(const FragmentShaderInstructionBase& insn)
{
	switch (insn.Scale())
	{
	case FragmentShaderScale::Div2: m_str << ") / 2.0)"; break;
	case FragmentShaderScale::Div4: m_str << ") / 4.0)"; break;
	case FragmentShaderScale::Div8: m_str << ") / 8.0)"; break;
	case FragmentShaderScale::Times2: m_str << ") * 2.0)"; break;
	case FragmentShaderScale::Times4: m_str << ") * 4.0)"; break;
	case FragmentShaderScale::Times8: m_str << ") * 8.0)"; break;
	}

	if (insn.HasDestination())
	{
		if (insn.IsSaturated()) {
			m_str << ", 0.0, 1.0)"; // clamp(xxx, 0.0, 1.0)
		} else {
			switch (insn.GetPrecision())
			{
			case FragmentShaderPrecision::Fixed12:
				m_str << ", -2.0, 2.0)";
				break;
			case FragmentShaderPrecision::Fixed9:
				m_str << ", -1.0, 1.0)";
				break;
			}
		}

		if (insn.IsBiased()) {
			m_str << " * 2 - 1)";
		}
	}

	m_str << ";" << std::endl;

	if (insn.Condition() != FragmentShaderCondition::True) {
		// close if-statement block
		UnIndent();
		m_str << kIndentation[m_indentationLevel] << "}" << std::endl;
	}

	m_useWriteMask = false;
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderNoOperationInstruction& insn) {
	// Do nothing
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderMoveInstruction& insn) {
	// Write first operand
	insn.Operand1().Accept(*this);
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderMultiplyInstruction& insn) {
	insn.Operand1().Accept(*this);
	m_str << " * ";
	insn.Operand2().Accept(*this);
}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderAddInstruction& insn) {
	insn.Operand1().Accept(*this);
	m_str << " + ";
	insn.Operand2().Accept(*this);
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderMultiplyAddInstruction& insn) {
	insn.Operand1().Accept(*this);
	m_str << " * ";
	insn.Operand2().Accept(*this);
	m_str << " + ";
	insn.Operand3().Accept(*this);
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDotProduct3Instruction& insn) {
	m_useWriteMask = false;

	m_str << "dot(";
	insn.Operand1().Accept(*this);
	m_str << ".xyz, ";
	insn.Operand2().Accept(*this);
	m_str << ".xyz)";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDotProduct4Instruction& insn) {
	m_useWriteMask = false;

	m_str << "dot(";
	insn.Operand1().Accept(*this);
	m_str << ", ";
	insn.Operand2().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDistanceInstruction& insn) {
	m_str << "distance(";
	insn.Operand1().Accept(*this);
	m_str << ", ";
	insn.Operand2().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderMinimumInstruction& insn) {
	m_str << "min(";
	insn.Operand1().Accept(*this);
	m_str << ", ";
	insn.Operand2().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderMaximumInstruction& insn) {
	m_str << "max(";
	insn.Operand1().Accept(*this);
	m_str << ", ";
	insn.Operand2().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnLessThanInstruction& insn) {
	// dst.xy = vecX(lessThan(op1, op2).dstMask)
	switch (kWriteComponents[m_writeMask]) {
	case 1:
		m_str << "float(";
		insn.Operand1().Accept(*this);
		m_str << " < ";
		insn.Operand2().Accept(*this);
		m_str << ")";
		break;
	default:
		m_str << "vec" << kWriteComponents[m_writeMask] << "(";
		m_str << "lessThan(";
		insn.Operand1().Accept(*this);
		m_str << ", ";
		insn.Operand2().Accept(*this);
		m_str << "))";
		break;
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnGreaterEqualInstruction& insn) {
	switch (kWriteComponents[m_writeMask]) {
	case 1:
		m_str << "float(";
		insn.Operand1().Accept(*this);
		m_str << " >= ";
		insn.Operand2().Accept(*this);
		m_str << ")";
		break;
	default:
		m_str << "vec" << kWriteComponents[m_writeMask] << "(";
		m_str << "greaterThanEqual(";
		insn.Operand1().Accept(*this);
		m_str << ", ";
		insn.Operand2().Accept(*this);
		m_str << "))";
		break;
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnLessEqualInstruction& insn) {
	switch (kWriteComponents[m_writeMask]) {
	case 1:
		m_str << "float(";
		insn.Operand1().Accept(*this);
		m_str << " <= ";
		insn.Operand2().Accept(*this);
		m_str << ")";
		break;
	default:
		m_str << "vec" << kWriteComponents[m_writeMask] << "(";
		m_str << "lessThanEqual(";
		insn.Operand1().Accept(*this);
		m_str << ", ";
		insn.Operand2().Accept(*this);
		m_str << "))";
		break;
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnGreaterThanInstruction& insn) {
	switch (kWriteComponents[m_writeMask]) {
	case 1:
		m_str << "float(";
		insn.Operand1().Accept(*this);
		m_str << " > ";
		insn.Operand2().Accept(*this);
		m_str << ")";
		break;
	default:
		m_str << "vec" << kWriteComponents[m_writeMask] << "(";
		m_str << "greaterThan(";
		insn.Operand1().Accept(*this);
		m_str << ", ";
		insn.Operand2().Accept(*this);
		m_str << "))";
		break;
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnNotEqualInstruction& insn) {
	switch (kWriteComponents[m_writeMask]) {
	case 1:
		m_str << "float(";
		insn.Operand1().Accept(*this);
		m_str << " != ";
		insn.Operand2().Accept(*this);
		m_str << ")";
		break;
	default:
		m_str << "vec" << kWriteComponents[m_writeMask] << "(";
		m_str << "notEqual(";
		insn.Operand1().Accept(*this);
		m_str << ", ";
		insn.Operand2().Accept(*this);
		m_str << "))";
		break;
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnEqualInstruction& insn) {
	switch (kWriteComponents[m_writeMask]) {
	case 1:
		m_str << "float(";
		insn.Operand1().Accept(*this);
		m_str << " == ";
		insn.Operand2().Accept(*this);
		m_str << ")";
		break;
	default:
		m_str << "vec" << kWriteComponents[m_writeMask] << "(";
		m_str << "equal(";
		insn.Operand1().Accept(*this);
		m_str << ", ";
		insn.Operand2().Accept(*this);
		m_str << "))";
		break;
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderFractionInstruction& insn) {
	m_str << "fract(";
	insn.Operand1().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderFloorInstruction& insn) {
	m_str << "floor(";
	insn.Operand1().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderKillInstruction& insn) {
	m_str << "discard";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderPack4Instruction& insn) {}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderUnpack4Instruction& insn) {}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDDXInstruction& insn) {
	m_str << "dFdx(";
	insn.Operand1().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDDYInstruction& insn) {
	m_str << "dFdy(";
	insn.Operand1().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureInstruction& insn) {
	m_str << "texture(tex" << insn.TextureSampler() << ", ";
	insn.Operand1().Accept(*this);
	m_str << ".xy)";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureProjectionInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureDerivativeInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderReciprocalInstruction& insn) {
	m_useWriteMask = false;

	m_str << "(1.0 / (";
	insn.Operand1().Accept(*this);
	m_str << "))";
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderReciprocalSquareRootInstruction& insn) {
	m_useWriteMask = false;

	m_str << "inversesqrt(";
	insn.Operand1().Accept(*this);
	m_str << ")";
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderExponential2Instruction& insn) {
	m_useWriteMask = false;

	m_str << "exp2(";
	insn.Operand1().Accept(*this);
	m_str << ")";
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderLog2Instruction& insn) {
	m_useWriteMask = false;

	m_str << "log2(";
	insn.Operand1().Accept(*this);
	m_str << ")";
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderLightingInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderLerpInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnTrueInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderSetOnFalseInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderCosineInstruction& insn) {
	m_useWriteMask = false;

	m_str << "cos(";
	insn.Operand1().Accept(*this);
	m_str << ")";
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSineInstruction& insn) {
	m_useWriteMask = false;

	m_str << "sin(";
	insn.Operand1().Accept(*this);
	m_str << ")";
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderPack2Instruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderUnpack2Instruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderPowInstruction& insn) {
	m_str << "pow(";
	insn.Operand1().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderPackBInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderUnpackBInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderPack16Instruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderUnpack16Instruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderBumpEnvInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderPackGInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderUnpackGInstruction& insn) {}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDotProduct2AddInstruction& insn) {
	m_useWriteMask = false;

	m_str << "(dot(";
	insn.Operand1().Accept(*this);
	m_str << ".xy, ";
	insn.Operand2().Accept(*this);
	m_str << ".xy) + ";

	m_useWriteMask = true;

	insn.Operand3().Accept(*this);
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureLodInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureBiasInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureBumpEnvInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureProjectionBumpEnvInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderBumpEnvLumInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderReflectInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderTextureTimesWInstruction& insn) {}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDotProduct2Instruction& insn) {
	m_useWriteMask = false;

	m_str << "dot(";
	insn.Operand1().Accept(*this);
	m_str << ".xy, ";
	insn.Operand2().Accept(*this);
	m_str << ".xy)";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderNormalizeInstruction& insn) {
	m_str << "normalize(";
	insn.Operand1().Accept(*this);
	m_str << ".xyz)";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDivideInstruction& insn) {
	insn.Operand1().Accept(*this);
	m_str << " / ";
	m_useWriteMask = false;
	insn.Operand2().Accept(*this);
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderDivideSquareRootInstruction& insn) {
	insn.Operand1().Accept(*this);
	m_str << " / sqrt(";

	m_useWriteMask = false;

	insn.Operand2().Accept(*this);
	if (!m_singleComponent) {
		m_str << kVectorCast[m_writeMask];
	}
	m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderLifInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderFenctInstruction& insn) {}
void GLSLFragmentProgramWriter::Visit(const FragmentShaderFencbInstruction& insn) {}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderBreakInstruction& insn) {
	m_str << "break";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderCallInstruction& insn) {}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderIfElseInstruction& insn) {
	m_str << kIndentation[m_indentationLevel];
	m_str << "if (";

	// Print condition
	switch (insn.Condition()) {
	case FragmentShaderCondition::False:
		m_str << "false";
		break;
	case FragmentShaderCondition::True:
		m_str << "true";
		break;
	case FragmentShaderCondition::GreaterThan:
		m_str << "all(greaterThan(rc";
		if (insn.ConditionRegisterRead() > 0) { m_str << "1"; }
		WriteSwizzleMask(insn.ConditionMask());
		m_str << ", vec4(0,0,0,0)))";
		break;
	case FragmentShaderCondition::Equal:
		m_str << "all(equal(rc";
		if (insn.ConditionRegisterRead() > 0) { m_str << "1"; }
		WriteSwizzleMask(insn.ConditionMask());
		m_str << ", vec4(0,0,0,0)))";
		break;
	default:
		m_str << "rc";
		if (insn.ConditionRegisterRead() > 0) { m_str << "1"; }
		WriteSwizzleMask(insn.ConditionMask());
		m_str << kIfConditionMap[insn.Condition()];
		m_str << "0";
		break;
	}

	m_str << ") {" << std::endl;
	Indent();
	ProcessInstructionList(insn.GetIfInstructions());
	UnIndent();

	if (insn.GetElseInstructions().size() > 0)
	{
		m_str << kIndentation[m_indentationLevel];
		m_str << "} else {" << std::endl;
		Indent();
		ProcessInstructionList(insn.GetElseInstructions());
		UnIndent();
	}
	m_str << kIndentation[m_indentationLevel];
	m_str << "}" << std::endl;
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderLoopInstruction& insn) {
	m_str << kIndentation[m_indentationLevel];
	m_str << "for (int loopCnt = " << insn.GetInitialValue();
	m_str << "; loopCnt < " << insn.GetEndValue();
	m_str << "; loopCnt += " << insn.GetIncrement() << ") {" << std::endl;

	Indent();
	ProcessInstructionList(insn.GetInstructions());
	UnIndent();

	m_str << kIndentation[m_indentationLevel];
	m_str << "}" << std::endl;
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderRepInstruction& insn) {
	m_str << kIndentation[m_indentationLevel];
	m_str << "for (int loopCnt = 0; loopCnt < " << insn.GetLoopCount();
	m_str << "; loopCnt++) {" << std::endl;

	Indent();
	ProcessInstructionList(insn.GetInstructions());
	UnIndent();

	m_str << kIndentation[m_indentationLevel];
	m_str << "}" << std::endl;
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderReturnInstruction& insn) {
	m_str << "return";
}

void GLSLFragmentProgramWriter::PreOperand(const FragmentShaderOperandBase& op) {
	// Could be negative
	if (op.UseNegative()) m_str << "-";

	// Could be the absolute value
	if (op.UseAbsoluteValue()) m_str << "abs(";
}

void GLSLFragmentProgramWriter::PostOperand(const FragmentShaderOperandBase& op) {
	// Swizzle mask
	WriteSwizzleMask(op.SwizzleMask());

	// Close absolute value
	if (op.UseAbsoluteValue()) m_str << ")";
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderRegisterOperand& op) {
	PreOperand(op);

	std::string registerName;
	auto index = op.RegisterIndex();

	// Write Register Name
	if (op.UseHalfPrecision()) {
		registerName = "h" + std::to_string(index);
	}
	else {
		registerName = "r" + std::to_string(index);
	}

	// Track register
	TrackRegister(registerName, index, op.UseHalfPrecision());

	m_str << registerName;

	PostOperand(op);
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderSpecialOperand& op) {
	PreOperand(op);

	if (op.IsInputRegister()) {
		assert(op.InputSemantic() < FragmentShaderInputSemantic::NUM_SEMANTICS);
		m_str << kInputRegisters[op.InputSemantic()];
		m_usedInputs.insert(op.InputSemantic());
	}
	else if (op.IsIndexRegister()) {
		// FIXME: this is wrong, but i don't have a sample that uses it
		m_str << "aL+" << op.RegisterIndex();
	}

	PostOperand(op);
}

void GLSLFragmentProgramWriter::Visit(const FragmentShaderConstantOperand& op) {
	PreOperand(op);

	if (op.UseHalfPrecision()) {
		m_str << "half4(";
	}
	else {
		m_str << "vec4(";
	}

	m_str << op.GetX() << ", ";
	m_str << op.GetY() << ", ";
	m_str << op.GetZ() << ", ";
	m_str << op.GetW() << ")";

	PostOperand(op);
}

}
}
}