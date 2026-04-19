#include <ceryx/cpu/Idt.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>

namespace ceryx::cpu {

IdtGate Idt::s_idt[Idt::kIdtSize];
InterruptHandler Idt::s_handlers[Idt::kIdtSize];

extern "C" u64 isr_stub_table[];

void Idt::Initialize() {
    FoundationKitMemory::MemoryZero(s_idt, sizeof(s_idt));
    FoundationKitMemory::MemoryZero(s_handlers, sizeof(s_handlers));

    for (u16 i = 0; i < kIdtSize; ++i) {
        s_idt[i].SetOffset(isr_stub_table[i]);
        s_idt[i].selector = Gdt::kKernelCodeSelector;
        s_idt[i].ist = 0;
        s_idt[i].SetAttributes(0, IdtGateType::InterruptGate);
    }

    IdtPointer ptr;
    ptr.limit = sizeof(s_idt) - 1;
    ptr.base = reinterpret_cast<u64>(s_idt);

    __asm__ volatile ("lidt %0" : : "m"(ptr));
}

void Idt::RegisterHandler(u8 vector, InterruptHandler handler) {
    s_handlers[vector] = handler;
}

extern "C" void InterruptDispatch(InterruptFrame* frame) {
    if (frame->interrupt_number < Idt::kIdtSize && Idt::s_handlers[frame->interrupt_number]) {
        Idt::s_handlers[frame->interrupt_number](frame);
    } else {
        FK_LOG_ERR("Unhandled interrupt: {} at RIP {:#x}, error code {:#x}", 
                   frame->interrupt_number, frame->rip, frame->error_code);
        
        // Dump some registers
        FK_LOG_ERR("RAX: {:#x} RBX: {:#x} RCX: {:#x} RDX: {:#x}", frame->rax, frame->rbx, frame->rcx, frame->rdx);
        FK_LOG_ERR("RSI: {:#x} RDI: {:#x} RBP: {:#x} RSP: {:#x}", frame->rsi, frame->rdi, frame->rbp, frame->rsp);

        FK_BUG("Unhandled interrupt");
    }
}

} // namespace ceryx::cpu
