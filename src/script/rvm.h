#include <cstdint>
#include <cstring>
#include "../core/log.h"

namespace RVM {

// ── Instruction Set ──────────────────────────────────────────────────────────
enum OpCode : uint8_t {
    OP_HALT = 0,
    OP_PUSH_F,      // Followed by 4-byte float literal
    OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_LT,  OP_GT,  OP_EQ,
    OP_JMP,         // Followed by 2-byte signed offset (relative to opcode start)
    OP_JZ,          // Followed by 2-byte signed offset
    OP_CALL_NATIVE, // Followed by 1-byte index into native table
    OP_GET_GLOBAL,  // Followed by 1-byte index into globals array
    OP_SET_GLOBAL,  // Followed by 1-byte index into globals array
};

class VM;
typedef void (*NativeFn)(VM& vm);

// ── Virtual Machine ──────────────────────────────────────────────────────────
class VM {
public:
    static constexpr int STACK_MAX = 64;
    float stack[STACK_MAX];
    int top = 0;

    static constexpr int GLOBALS_MAX = 256;
    float globals[GLOBALS_MAX];

    VM() {
        memset(stack, 0, sizeof(stack));
        memset(globals, 0, sizeof(globals));
    }

    void push(float v) { if (top < STACK_MAX) stack[top++] = v; }
    float pop() { return (top > 0) ? stack[--top] : 0.0f; }

    void execute(const uint8_t* bytecode, int size, const NativeFn* natives) {
        int pc = 0;
        while (pc < size) {
            uint8_t op = bytecode[pc++];
            switch (op) {
                case OP_HALT: return;

                case OP_PUSH_F: {
                    float f;
                    memcpy(&f, &bytecode[pc], 4);
                    push(f);
                    pc += 4;
                } break;

                case OP_POP: pop(); break;

                case OP_ADD: { float b = pop(); float a = pop(); push(a + b); } break;
                case OP_SUB: { float b = pop(); float a = pop(); push(a - b); } break;
                case OP_MUL: { float b = pop(); float a = pop(); push(a * b); } break;
                case OP_DIV: { float b = pop(); float a = pop(); push(a / b); } break;

                case OP_LT:  { float b = pop(); float a = pop(); push(a < b ? 1.0f : 0.0f); } break;
                case OP_GT:  { float b = pop(); float a = pop(); push(a > b ? 1.0f : 0.0f); } break;
                case OP_EQ:  { float b = pop(); float a = pop(); push(a == b ? 1.0f : 0.0f); } break;

                case OP_JMP: {
                    int16_t offset;
                    memcpy(&offset, &bytecode[pc], 2);
                    pc += offset - 1; // -1 because pc already advanced past opcode
                } break;

                case OP_JZ: {
                    int16_t offset;
                    memcpy(&offset, &bytecode[pc], 2);
                    if (pop() == 0.0f) pc += offset - 1;
                    else pc += 2;
                } break;

                case OP_CALL_NATIVE: {
                    uint8_t idx = bytecode[pc++];
                    if (natives && natives[idx]) natives[idx](*this);
                } break;

                case OP_GET_GLOBAL: {
                    uint8_t idx = bytecode[pc++];
                    push(globals[idx]);
                } break;

                case OP_SET_GLOBAL: {
                    uint8_t idx = bytecode[pc++];
                    globals[idx] = pop();
                } break;

                default:
                    LOG_ERROR("RVM: Unknown opcode 0x%02X at PC %d", op, pc-1);
                    return;
            }
        }
    }
};

} // namespace RVM
