/*
 * BootShim.S
 * req build args:
 * FB_BASE
 *
 * fb:
 * 720x1600 32 bpp 2880 stride, grn=0x0000FF00
 *
 */

_Head:
    adr     x1, _Payload
    b       _Start

_StackBase:
    .quad   UEFI_BASE

_StackSize:
    .quad   UEFI_SIZE

_FbBase:
    .quad   FB_BASE

    .quad   0
    .quad   0
    .quad   0
    .ascii  "ARM\x64"
    .long   0

_Start:
    mov     x4, x1
    ldr     x5, _StackBase
    cmp     x4, x5
    b.eq    _Entry

    ldr     x6, _StackSize

_cheesepizza:
    ldp     x2, x3, [x4], #16
    stp     x2, x3, [x5], #16
    subs    x6, x6, #16
    b.ne    _cheesepizza

_Entry:
    ldr     x5, _StackBase
    ldr     x6, _StackSize

    mov     x7, x5
    add     x8, x5, x6
    bic     x7, x7, #63

_cleaner:
    dc      cvau, x7
    add     x7, x7, #64
    cmp     x7, x8
    b.lo    _cleaner

    dsb     sy
    ic      iallu
    dsb     sy
    isb

    ldr     x9, _FbBase
    mov     w10, #0xFF00
    mov     x11, #32

_strider:
    mov     x12, #720
_stridep:
    str     w10, [x9], #4
    subs    x12, x12, #1
    b.ne    _stridep

    subs    x11, x11, #1
    b.ne    _strider

    dsb     sy

    mrs     x12, cntfrq_el0
    lsr     x12, x12, #1
    mrs     x13, cntpct_el0
    add     x12, x12, x13

_delloop:
    mrs     x13, cntpct_el0
    cmp     x13, x12
    b.lo    _delloop

    isb
    br      x5

_Dead:
	wfe
    b       _Dead 

.text
.align 4

_Payload:


