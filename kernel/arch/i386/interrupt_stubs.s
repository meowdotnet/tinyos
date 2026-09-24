.code32
.extern interrupt_dispatch

.macro ISR_NOERR number
.global isr\number
isr\number:
    pushl $0
    pushl $\number
    jmp interrupt_common
.endm

.macro ISR_ERR number
.global isr\number
isr\number:
    pushl $\number
    jmp interrupt_common
.endm

.macro IRQ number
.global isr\number
isr\number:
    pushl $0
    pushl $\number
    jmp interrupt_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31
ISR_NOERR 128
IRQ 32
IRQ 33
IRQ 34
IRQ 35
IRQ 36
IRQ 37
IRQ 38
IRQ 39
IRQ 40
IRQ 41
IRQ 42
IRQ 43
IRQ 44
IRQ 45
IRQ 46
IRQ 47

interrupt_common:
    cld
    pusha
    mov 32(%esp), %eax
    pushl %esp
    pushl %eax
    call interrupt_dispatch
    add $8, %esp
    popa
    add $8, %esp
    iret

.global interrupt_stub_table
interrupt_stub_table:
    .long isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .long isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .long isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .long isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .long isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    .long isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47

.global syscall_stub
.set syscall_stub, isr128

.global idt_load
.type idt_load, @function
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret
