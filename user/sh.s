.code32
.section .user_sh, "a"
.global user_sh_image
.global user_sh_image_end
.set BASE, 0x40000000
.set BUFBASE, 0x7ffff000     # bottom of this process's user stack page
.set LINE_MAX, 255           # leave the NUL below the scratch byte
.set SCRATCH, 0x7ffff100     # one-byte scratch for SYS_READ
.set PATHBUF, 0x7ffff200    # 128-byte path buffer
.set PATHBUF_END, 0x7ffff280
.set PATHBUF_LIMIT, PATHBUF_END - 1
.set DENTBUF, 0x7ffff300     # directory-entry buffer
.set DENTBUF_SIZE, 264        # 8-byte header + TFS_NAME_MAX
.set STATBUF, 0x7ffff480     # struct tfs_stat (7 words = 28 bytes)
.set FILEBUF, 0x7ffff4c0     # 512-byte file data buffer
.set SYS_WRITE, 1
.set SYS_READ, 2
.set SYS_EXIT, 3
.set SYS_CLEAR, 6
.set SYS_SHUTDOWN, 7
.set SYS_OPEN, 8
.set SYS_CLOSE, 9
.set SYS_FREAD, 10
.set SYS_FWRITE, 11
.set SYS_MKDIR, 12
.set SYS_READDIR, 13
.set SYS_STAT, 14
.set O_RDONLY, 0
.set O_WRONLY, 1
.set O_RDWR, 2
.set O_CREAT, 0x100
.set O_TRUNC, 0x200

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

    cmpl $LINE_MAX, %ebp
    jae 1b
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

# cmdeq: esi=line, edi=cmdword (NUL, no spaces) -> eax=0 if line's first
# word equals cmdword. Clobbers eax/ecx.
cmdeq:
1:  movb (%edi), %cl
    testb %cl, %cl
    jz 2f
    movb (%esi), %al
    cmpb %cl, %al
    jne 3f
    incl %esi
    incl %edi
    jmp 1b
2:  movb (%esi), %al          # line char at end of cmdword
    cmpb $32, %al              # a space follows
    je 4f
    testb %al, %al             # or end of line
    jz 4f
    jmp 3f
3:  movl $1, %eax
    ret
4:  xorl %eax, %eax
    ret

# getarg: ebp=command word ptr -> eax = ptr to first arg (spaces skipped) or 0.
getarg:
1:  cmpb $32, (%ebp)
    je 2f
    cmpb $0, (%ebp)
    je 4f
    incl %ebp
    jmp 1b
2:  incl %ebp
    cmpb $32, (%ebp)
    je 2b
    cmpb $0, (%ebp)
    je 4f
    movl %ebp, %eax
    ret
4:  xorl %eax, %eax
    ret

# strlen: eax=ptr -> eax=len. Clobbers ecx.
strlen:
    movl %eax, %ecx
1:  cmpb $0, (%ecx)
    je 2f
    incl %ecx
    jmp 1b
2:  subl %eax, %ecx
    movl %ecx, %eax
    ret

# abspath: eax=arg ptr -> copy one space-delimited token to PATHBUF,
# prefixing '/' if relative. Clobbers eax/ecx/esi/edi. eax = PATHBUF, or 0
# when the token is too long.
abspath:
    pushl %esi
    pushl %edi
    movl %eax, %esi
    movl $PATHBUF, %edi
    cmpb $47, (%esi)              # '/'
    je 1f
    movb $47, (%edi)              # prepend '/'
    incl %edi
1:  movb (%esi), %al
    testb %al, %al
    je 2f
    cmpb $32, %al                 # stop at space
    je 2f
    cmpl $PATHBUF_LIMIT, %edi
    jae 3f
    movb %al, (%edi)
    incl %esi
    incl %edi
    jmp 1b
2:  movb $0, (%edi)
    popl %edi
    popl %esi
    movl $PATHBUF, %eax
    ret
3:  popl %edi
    popl %esi
    xorl %eax, %eax
    ret

# putu: print unsigned decimal in eax. Clobbers eax/ecx/edx/esi/edi.
putu:
    pushl %esi
    pushl %edi
    movl $FILEBUF, %edi
    addl $63, %edi                # end of scratch area
    movb $0, (%edi)
    movl $10, %ecx
1:  xorl %edx, %edx
    divl %ecx
    addb $48, %dl
    decl %edi
    movb %dl, (%edi)
    testl %eax, %eax
    jnz 1b
    movl $FILEBUF, %eax
    addl $63, %eax
    subl %edi, %eax               # eax = length
    movl %eax, %ecx
    movl %edi, %ebx
    movl $SYS_WRITE, %eax
    int $0x80
    popl %edi
    popl %esi
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

    # --- TinyFS commands (word match, arguments allowed) ---
    movl %ebp, %esi
    movl $(BASE + cmd_ls - user_sh_image), %edi
    call cmdeq
    testl %eax, %eax
    jz do_ls

    movl %ebp, %esi
    movl $(BASE + cmd_mkdir - user_sh_image), %edi
    call cmdeq
    testl %eax, %eax
    jz do_mkdir

    movl %ebp, %esi
    movl $(BASE + cmd_cat - user_sh_image), %edi
    call cmdeq
    testl %eax, %eax
    jz do_cat

    movl %ebp, %esi
    movl $(BASE + cmd_put - user_sh_image), %edi
    call cmdeq
    testl %eax, %eax
    jz do_put

    movl %ebp, %esi
    movl $(BASE + cmd_stat - user_sh_image), %edi
    call cmdeq
    testl %eax, %eax
    jz do_stat

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
    movl $(BASE + msg_help2 - user_sh_image), %esi
    call putsz
    movl $(BASE + msg_help3 - user_sh_image), %esi
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

# ---- TinyFS command handlers ----

# print "<name>\n" for one dirent at DENTBUF; esi=dirent ptr
print_dent_name:
    movzbl 7(%esi), %edx        # namelen
    leal 8(%esi), %eax
    movl %eax, %ebx
    movl %edx, %ecx
    movl $SYS_WRITE, %eax
    int $0x80
    movl $(BASE + nl - user_sh_image), %esi
    movl %esi, %ebx
    movl $1, %ecx
    movl $SYS_WRITE, %eax
    int $0x80
    ret

do_ls:
    call getarg
    testl %eax, %eax
    jnz 1f
    movl $PATHBUF, %eax          # default path "/"
    movb $47, (%eax)
    movb $0, 1(%eax)
1:  call abspath                 # -> PATHBUF or 0
    testl %eax, %eax
    jz ls_err
    movl $PATHBUF, %ebx
    movl $O_RDONLY, %ecx
    movl $SYS_OPEN, %eax
    int $0x80
    testl %eax, %eax
    jns 2f
    jmp ls_err
2:  movl %eax, %edi              # edi = fd
3:  movl %edi, %ebx
    movl $DENTBUF, %ecx
    movl $DENTBUF_SIZE, %edx
    movl $SYS_READDIR, %eax
    int $0x80
    testl %eax, %eax
    jz 4f
    jle ls_read_err
    movl $DENTBUF, %esi
    call print_dent_name
    jmp 3b
4:  movl %edi, %ebx
    movl $SYS_CLOSE, %eax
    int $0x80
    ret
ls_read_err:
    movl %edi, %ebx
    movl $SYS_CLOSE, %eax
    int $0x80
ls_err:
    movl $(BASE + msg_fserr - user_sh_image), %esi
    call putsz
    ret

do_mkdir:
    call getarg
    testl %eax, %eax
    jnz 1f
    jmp mkdir_err
1:  call abspath
    testl %eax, %eax
    jz mkdir_err
    movl $PATHBUF, %ebx
    movl $0755, %ecx
    movl $SYS_MKDIR, %eax
    int $0x80
    testl %eax, %eax
    je 2f
    jmp mkdir_err
2:  movl $(BASE + msg_mkdirok - user_sh_image), %esi
    call putsz
    ret
mkdir_err:
    movl $(BASE + msg_fserr - user_sh_image), %esi
    call putsz
    ret

do_cat:
    call getarg
    testl %eax, %eax
    jz cat_err
    call abspath
    testl %eax, %eax
    jz cat_err
    movl $PATHBUF, %ebx
    movl $O_RDONLY, %ecx
    movl $SYS_OPEN, %eax
    int $0x80
    testl %eax, %eax
    jns 2f
    jmp cat_err
2:  movl %eax, %edi              # fd
3:  movl %edi, %ebx
    movl $FILEBUF, %ecx
    movl $512, %edx
    movl $SYS_FREAD, %eax
    int $0x80
    testl %eax, %eax
    jz 4f
    jle cat_read_err
    movl %eax, %edx
    movl $FILEBUF, %ebx
    movl %edx, %ecx
    movl $SYS_WRITE, %eax
    int $0x80
    jmp 3b
4:  movl %edi, %ebx
    movl $SYS_CLOSE, %eax
    int $0x80
    ret
cat_read_err:
    movl %edi, %ebx
    movl $SYS_CLOSE, %eax
    int $0x80
cat_err:
    movl $(BASE + msg_fserr - user_sh_image), %esi
    call putsz
    ret

do_put:
    call getarg
    testl %eax, %eax
    jz put_err
    call abspath
    testl %eax, %eax
    jz put_err
    # ebp now points at first arg; advance past the path token to the text
    movl %ebp, %eax
1:  cmpb $32, (%eax)
    je 2f
    cmpb $0, (%eax)
    je put_err
    incl %eax
    jmp 1b
2:  incl %eax
    cmpb $32, (%eax)
    je 2b
    cmpb $0, (%eax)
    je put_err
    movl %eax, %esi              # esi = text to write
    # open(path, O_WRONLY|O_CREAT|O_TRUNC)
    movl $PATHBUF, %ebx
    movl $(O_WRONLY | O_CREAT | O_TRUNC), %ecx
    movl $SYS_OPEN, %eax
    int $0x80
    testl %eax, %eax
    jns 2f
    jmp put_err
2:  movl %eax, %edi              # fd
    movl %esi, %eax
    call strlen                  # eax = text length
    movl %eax, %edx
    movl %edi, %ebx
    movl %esi, %ecx
    movl $SYS_FWRITE, %eax
    int $0x80
    cmpl %edx, %eax              # require the complete short write
    jne 4f
    jmp 3f
4:  movl %edi, %ebx
    movl $SYS_CLOSE, %eax
    int $0x80
    jmp put_err
3:  movl %edi, %ebx
    movl $SYS_CLOSE, %eax
    int $0x80
    ret
put_err:
    movl $(BASE + msg_fserr - user_sh_image), %esi
    call putsz
    ret

do_stat:
    call getarg
    testl %eax, %eax
    jnz 1f
    jmp stat_err
1:  call abspath
    testl %eax, %eax
    jz stat_err
    movl $PATHBUF, %ebx
    movl $STATBUF, %ecx
    movl $SYS_STAT, %eax
    int $0x80
    testl %eax, %eax
    je 2f
    jmp stat_err
2:  # "size=" <size> " ino=" <ino> " type=" <d|f> "\n"
    movl $(BASE + msg_size - user_sh_image), %esi
    call putsz
    movl $STATBUF, %ecx          # reload (putsz clobbers ecx)
    movl 16(%ecx), %eax          # STATBUF.size
    pushl %ecx
    call putu
    popl %ecx
    movl $(BASE + msg_ino - user_sh_image), %esi
    call putsz
    movl $STATBUF, %ecx
    movl (%ecx), %eax            # STATBUF.ino
    call putu
    movl $(BASE + msg_type - user_sh_image), %esi
    call putsz
    movl $STATBUF, %ecx
    movl 4(%ecx), %eax           # STATBUF.mode
    andl $0xf000, %eax
    cmpl $0x4000, %eax
    je 3f
    movl $(BASE + msg_file - user_sh_image), %esi
    call putsz
    ret
3:  movl $(BASE + msg_dir - user_sh_image), %esi
    call putsz
    ret
stat_err:
    movl $(BASE + msg_fserr - user_sh_image), %esi
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
cmd_ls:      .asciz "ls"
cmd_mkdir:   .asciz "mkdir"
cmd_cat:     .asciz "cat"
cmd_put:     .asciz "put"
cmd_stat:    .asciz "stat"
msg_help:    .asciz "commands: help uname whoami echo clear shutdown exit\n"
msg_help2:   .asciz "         ls [dir] cat <file> put <file> <text>\n"
msg_help3:   .asciz "         mkdir <dir> stat <path>\n"
msg_shutdown: .asciz "sh: shutdown was ignored by the emulator\n"
msg_uname:   .asciz "tinyos 0.2 pure-UNIX i386 (/bin/sh)\n"
msg_whoami:  .asciz "root\n"
msg_bye:     .asciz "sh exiting; init will respawn me\n"
msg_unknown: .asciz "sh: unknown command\n"
msg_fserr:   .asciz "fs: operation failed\n"
msg_mkdirok: .asciz "mkdir: ok\n"
msg_size:    .asciz "size="
msg_ino:     .asciz " ino="
msg_type:    .asciz " type="
msg_file:    .asciz "file\n"
msg_dir:     .asciz "dir\n"

user_sh_image_end:
