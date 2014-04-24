#pragma once

#include <sstream>
#include <set>
#include <map>
#include "Emu/GS/RSXFragmentProgram.h"

namespace rpcs3 {
namespace rsx {
namespace gl {

	class GLSLFragmentProgramWriter : ShaderVisitor
	{
		/** Accumulates our GLSL shader output */
		std::stringstream m_str;

		/** List of used input semantics */
		std::set<FragmentShaderInputSemantic::Type> m_usedInputs;

		/** List of register names we should declare at the top of the file */
		std::set<std::string> m_defaultRegisters;

		/** List of used condition registers */
		std::set<std::string> m_conditionRegisters;

		/** Maps output registers to locations */
		std::map<std::string, int> m_usedOutputs;

		/** Write mask for destination of current instruction */
		u32 m_writeMask;

		/** Set to false for instructions which shouldn't propagate the writemask to operands (e.g. dot3() writes a scalar but uses vector instructions) */
		bool m_useWriteMask;

		u32 m_indentationLevel;

		FragmentProgramControl m_control;

		const FragmentShaderInstructionList& m_instructions;

		/** Write a swizzle mask to the stringstream */
		void WriteSwizzleMask(u32 mask);

		void TrackRegister(const std::string& registerName, u32 index, bool isFp16);

		void PreInstruction(const FragmentShaderInstructionBase& insn);
		void PostInstruction(const FragmentShaderInstructionBase& insn);

		void PreOperand(const FragmentShaderOperandBase& op);
		void PostOperand(const FragmentShaderOperandBase& op);

		void Indent();
		void UnIndent();

	public:
		GLSLFragmentProgramWriter(const FragmentShaderInstructionList& instructions, FragmentProgramControl control);

		std::string Process();

		virtual void Visit(const FragmentShaderRegisterOperand& op) override;
		virtual void Visit(const FragmentShaderSpecialOperand& op) override;
		virtual void Visit(const FragmentShaderConstantOperand& op) override;

#define DECLARE_VISIT_FUNC(opcode, nargs, opname) virtual void Visit(const FragmentShader ## opname ## Instruction& insn) override;
		FOREACH_FRAGMENT_SHADER_INSTRUCTION(DECLARE_VISIT_FUNC)
		FOREACH_FRAGMENT_SHADER_SPECIAL_INSTRUCTION(DECLARE_VISIT_FUNC)
#undef DECLARE_VISIT_FUNC
	};
}
}
}
