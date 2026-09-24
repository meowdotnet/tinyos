.code32
.section .user_sh, "a"
.global user_sh_image
.global user_sh_image_end
.set BASE, 0x40000000
.set BUFBASE, 0x7ffff000     # bottom of this process's user stack page
.set SCRATCH, 0x7ffff100     # one-byte scratch for SYS_READ
.set SYS_WRITE, 1
.set SYS_READ, 2
.set SYS_EXIT, 3
.set SYS_CLEAR, 6
.set SYS_SHUTDOWN, 7

/*
 * PID 2 (and every respawn) /bin/sh — a genuine ring-3 program.
 * It talks to the kernel only through int $0x80 (SYS_WRITE/SYS_READ/SYS_EXIT).
 * When it exits, PID 1 /init observes SYS_WAIT return and respawns it.
 *
 * All absolute string addresses are BASE + (label - user_sh_image): the image
 * is copied to 0x40000000 at load time.  The line buffer uses a FIXED address
 * (BUFBASE) rather than %esp, so it never collides with call frames.
 */

user_sh_image:
_start:
    movl $(BASE + banner - user_sh_image), %ebx
    movl $(banner_end - banner), %ecx
    call puts

prompt_loop:
    movl $(BASE + prompt - user_sh_image), %ebx
    movl $(prompt_end - prompt), %ecx
    call puts

    call read_line          # buffer at BUFBASE, nul-terminated
    call run_command        # ebp = command pointer (skips leading spaces)

    jmp prompt_loop

# ---------------------------------------------------------------- helpers

# puts: ebx=addr, ecx=len -> SYS_WRITE. Preserves esi/edi/ebp.
puts:
    pushl %ebx
    pushl %ecx
    movl $SYS_WRITE, %eax
    int $0x80
    popl %ecx
    popl %ebx
    ret

# putsz: esi=nul-terminated string -> SYS_WRITE. Preserves esi/ebp.
putsz:
    pushl %esi
    movl %esi, %ecx          # ecx = cursor
1:  cmpb $0, (%ecx)
    je 2f
    incl %ecx
    jmp 1b
2:  subl %esi, %ecx         # ecx = length (nul - start)
    movl %esi, %ebx
    call puts
    popl %esi
    ret

# read_line: read a line into BUFBASE, echo printable chars, handle backspace,
# stop on newline (nul-terminated). Uses ebp as length; leaves it in ebp.
read_line:
    xorl %ebp, %ebp
1:  # one char into SCRATCH
    movl $SYS_READ, %eax
    movl $SCRATCH, %ebx
    movl $1, %ecx
    int $0x80
    movzbl SCRATCH, %eax

    cmpb $10, %al
    je 5f
    cmpb $8, %al
    je 4f
    cmpb $32, %al
    jl 1b
    cmpb $127, %al
    jg 1b

    movb %al, BUFBASE(%ebp)
    incl %ebp
    # echo the char we just stored
    movl $SYS_WRITE, %eax
    leal BUFBASE-1(%ebp), %ebx
    movl $1, %ecx
    int $0x80
    jmp 1b

4:  # backspace: shrink the buffer, then erase the glyph on screen
    testl %ebp, %ebp
    jz 1b
    decl %ebp
    movl $SYS_WRITE, %eax
    movl $(BASE + bs_erase - user_sh_image), %ebx
    movl $3, %ecx
    int $0x80
    jmp 1b

5:  movb $0, BUFBASE(%ebp)
    movl $(BASE + nl - user_sh_image), %ebx
    movl $1, %ecx
    call puts
    ret

# strcmp: esi=a, edi=b -> eax = 0 if equal else nonzero. Preserves ebp.
strcmp:
1:  movb (%esi), %al
    movb (%edi), %cl
    cmpb %cl, %al
    jne 2f
    testb %al, %al
    jz 3f
    incl %esi
    incl %edi
    jmp 1b
2:  movl $1, %eax
    ret
3:  xorl %eax, %eax
    ret

# run_command: execute the line at BUFBASE. Never returns on "exit".
run_command:
    # ebp = command pointer, skipping leading spaces
    movl $BUFBASE, %ebp
1:  cmpb $32, (%ebp)
    jne 2f
    incl %ebp
    jmp 1b
2:  cmpb $0, (%ebp)
    je 9f

    movl %ebp, %esi
    movl $(BASE + cmd_help - user_sh_image), %edi
    call strcmp
    testl %eax, %eax
    jz do_help

    movl %ebp, %esi
    movl $(BASE + cmd_uname - user_sh_image), %edi
    call strcmp
    testl %eax, %eax
    jz do_uname

    movl %ebp, %esi
    movl $(BASE + cmd_whoami - user_sh_image), %edi
    call strcmp
    testl %eax, %eax
    jz do_whoami

    movl %ebp, %esi
    movl $(BASE + cmd_exit - user_sh_image), %edi
    call strcmp
    testl %eax, %eax
    jz do_exit

    movl %ebp, %esi
    movl $(BASE + cmd_clear - user_sh_image), %edi
    call strcmp
    testl %eax, %eax
    jz do_clear

    movl %ebp, %esi
    movl $(BASE + cmd_shutdown - user_sh_image), %edi
    call strcmp
    testl %eax, %eax
    jz do_shutdown

    # echo (prefix: "echo" then space or end)
    movl (%ebp), %eax
    cmpl $0x6f686365, %eax      # "echo" little-endian
    jne unknown
    cmpb $32, 4(%ebp)
    je do_echo
    cmpb $0, 4(%ebp)
    je do_echo
    jmp unknown

unknown:
    movl $(BASE + msg_unknown - user_sh_image), %esi
    call putsz
    ret

9:  ret

do_help:
    movl $(BASE + msg_help - user_sh_image), %esi
    call putsz
    ret

do_uname:
    movl $(BASE + msg_uname - user_sh_image), %esi
    call putsz
    ret

do_whoami:
    movl $(BASE + msg_whoami - user_sh_image), %esi
    call putsz
    ret

do_clear:
    movl $SYS_CLEAR, %eax
    xorl %ebx, %ebx
    int $0x80
    ret

do_shutdown:
    movl $SYS_SHUTDOWN, %eax
    xorl %ebx, %ebx
    int $0x80
    # only reached if the emulator ignored the poweroff request
    movl $(BASE + msg_shutdown - user_sh_image), %esi
    call putsz
    ret

do_echo:
    leal 4(%ebp), %esi
1:  cmpb $32, (%esi)
    jne 2f
    incl %esi
    jmp 1b
2:  call putsz
    movl $(BASE + nl - user_sh_image), %esi
    call putsz
    ret

do_exit:
    movl $(BASE + msg_bye - user_sh_image), %esi
    call putsz
    movl $SYS_EXIT, %eax
    xorl %ebx, %ebx
    int $0x80
1:  hlt
    jmp 1b

# ---------------------------------------------------------------- data

banner:      .ascii "tinyos userspace /bin/sh\n"
banner_end:
prompt:      .ascii "sh$ "
prompt_end:
nl:          .asciz "\n"      # nul-terminated: putsz() stops here
bs_erase:    .ascii "\b \b"    # backspace, space, backspace (erase 1 glyph)
cmd_help:    .asciz "help"
cmd_uname:   .asciz "uname"
cmd_whoami:  .asciz "whoami"
cmd_echo:    .asciz "echo"
cmd_exit:    .asciz "exit"
cmd_clear:   .asciz "clear"
cmd_shutdown: .asciz "shutdown"
msg_help:    .asciz "commands: help uname whoami echo <text> clear shutdown exit\n"
msg_shutdown: .asciz "sh: shutdown was ignored by the emulator\n"
msg_uname:   .asciz "tinyos 0.1 pure-UNIX i386 (pid2 /bin/sh)\n"
msg_whoami:  .asciz "root\n"
msg_bye:     .asciz "sh exiting; init will respawn me\n"
msg_unknown: .asciz "sh: unknown command\n"

user_sh_image_end:
