section .bss
  heap_start  resq 1

section .data
  CHUNK_SIZE equ end_chunk-chunk

  CHUNK_OFFSET_MAGIC  equ 0
  CHUNK_OFFSET_SIZE   equ 0x8
  CHUNK_OFFSET_INUSE  equ 0x10
  CHUNK_OFFSET_NEXT   equ 0x18
  CHUNK_OFFSET_PREV   equ 0x20
  CHUNK_OFFSET_DATA   equ 0x28

  ; magic value on top of each header making sure the heap is not
  ; corrupted
  MAGIC_VAL_HEAP_CORRUPT equ 0xC0FFEE

  ; min size is 8 bytes which gives a result of 3 with 'bsr' 
  ; instruction.
  FIRST_BIN_BSR_INDEX equ 0x3
  N_BINS              equ 0xA

  chunk:
    magic_value dq 0
    size        dq 0
    inuse       dq 0
    next        dq 0
    prev        dq 0
  end_chunk:

  seg_free_list times N_BINS dq 0

section .text
; checks if the chunk can be splitted
; @param  rdi: chunk
; @param  rsi: size
; @return rax: bool
can_split:
  xor   rax, rax

  ; needs to remove header size from the chunk
  mov   rbx, qword [rdi+CHUNK_OFFSET_SIZE]
  sub   rbx, CHUNK_SIZE

  cmp   rbx, rsi
  setg  al
  movzx rax, al  

  ret

; splits the chunk of memory to allocate the smallest size
; @param  rdi: chunk
; @param  rsi: size
; @return rax: pointer to new chunk
_split:
  sub   rsp, 0x18

  mov   [rsp], rdi
  mov   qword [rsp+0x8], rsi

  ; check if it's a new chunk
  cmp   qword [rdi+CHUNK_OFFSET_INUSE], 1
  je    .new_chunk

  ; remove chunk from free list
  call  remove_from_free_list
  
.new_chunk:
  ; save size onto the stack
  mov   rdi, [rsp]
  mov   rsi, qword [rdi+CHUNK_OFFSET_SIZE]
  mov   qword [rsp+0x10], rsi

  ; update first chunk size and add magic value
  mov   rsi, qword [rsp+0x8]
  mov   qword [rdi+CHUNK_OFFSET_SIZE], rsi

  ; init second chunk
  add   rdi, qword [rdi+CHUNK_OFFSET_SIZE]
  mov   qword [rdi+CHUNK_OFFSET_MAGIC], MAGIC_VAL_HEAP_CORRUPT
  mov   qword [rdi+CHUNK_OFFSET_INUSE], 0

  ; get second chunk size
  mov   rbx, qword [rsp+0x10]
  sub   rbx, qword [rsp+0x8]

  mov   qword [rdi+CHUNK_OFFSET_SIZE], rbx

  ; set prev and next to 0 to avoid garbage data from prev malloc
  mov   qword [rdi+CHUNK_OFFSET_NEXT], 0
  mov   qword [rdi+CHUNK_OFFSET_PREV], 0

  ; add second chunk to the free list
  call  add_to_free_list

.return:
  mov   rax, [rsp]
  add   rsp, 0x18
  ret

; gets memory from the mmap syscall
; @param  rdi: size
; @return rax: returns a pointer to the allocated area
request_from_os:
  mov   rsi, PAGE_SIZE
  call  _align
  
  mov   rdi, rax
  call  mmap

  ret

; aligns the size in rdi by the value in rsi
; @param  rdi: size
; @param  rsi: alignment
; @return rax: aligned size
_align:
  ; alignment - 1
  dec   rsi

  ; (size + alignment - 1)
  mov   rax, rdi
  add   rax, rsi

  ; aligned = (size+alignment-1) & ~(alignment-1)
  not   rsi
  and   rax, rsi

  ret

; searches a valid previously allocated chunk
; @param  rdi: size
; @return rax: pointer to chunk, 0 if no valid chunk
search_chunk:
  sub   rsp, 0x10

  mov   qword [rsp], rdi
  mov   qword [rsp+0x8], 0

  ; get bin of data size
  sub   rdi, CHUNK_SIZE
  call  get_bin
  
.loop:
  mov   rdi, [seg_free_list+0x8*rax]
  cmp   rdi, 0
  jne   .first_fit

.next_bin:
  inc   rax
  cmp   rax, N_BINS
  jge   .not_found

  jmp   .loop

.first_fit:
  mov   rbx, qword [rsp]
  cmp   qword [rdi+CHUNK_OFFSET_SIZE], rbx
  jge   .found

  cmp   qword [rdi+CHUNK_OFFSET_NEXT], 0
  je    .next_bin

  mov   rsi, qword [rdi+CHUNK_OFFSET_NEXT]
  mov   rdi, rsi
  jmp   .first_fit

.found:
  ; make sure the data is not corrupted
  cmp   qword [rdi+CHUNK_OFFSET_MAGIC], MAGIC_VAL_HEAP_CORRUPT
  jne   .error

  mov   [rsp+0x8], rdi
  ; check if this chunk can be splitted
  mov   rsi, qword [rsp]
  call  can_split

  cmp   rax, 0
  je    .free_chunk

  mov   rdi, [rsp+0x8]
  mov   rsi, qword [rsp]
  call  _split

  mov   rax, [rsp+0x8]

  jmp   .return

.free_chunk:
  ; use this chunk
  mov   rdi, [rsp+0x8]
  call  remove_from_free_list

  mov   rdi, [rsp+0x8]
  mov   qword [rdi+CHUNK_OFFSET_INUSE], 1
  
  ; return chunk
  mov   rax, rdi
  jmp   .return

.not_found:
  xor   rax, rax

.return:
  add   rsp, 0x10
  ret

.error:
  mov   rax, FAILURE_CODE
  jmp   .return

; allocates a chunk of memory of rsi bytes
; @param  rdi: size
; @return rax: pointer to chunk data
malloc:
  sub   rsp, 0x18

  ; make sure the size if greater than 0
  cmp   rdi, 0
  je    .null
  jl    .error 

  ; add chunk_size to the size
  add   rdi, CHUNK_SIZE

  ; align the size
  mov   rsi, 0x8
  call  _align

  ; save aligned size onto the stack
  mov   qword [rsp], rax

  ; search for an available chunk of data
  mov   rdi, rax
  call  search_chunk

  cmp   rax, 0
  je    .request
  jl    .error

  mov   [rsp+0x8], rax

  ; return the chunk
  jmp   .found

.request:
  ; call mmap
  mov   rdi, qword [rsp]
  call  request_from_os

  cmp   rax, 0
  jl    .error
  
  ; save chunk address onto the stack
  mov   [rsp+0x8], rax

  ; initialize chunk
  ; store size of the data
  mov   rdi, qword [rsp]

  ; if request from os, size needs to be page aligned
  mov   rsi, PAGE_SIZE
  call  _align 

  mov   rsi, [rsp+0x8]

  mov   qword [rsi+CHUNK_OFFSET_MAGIC], MAGIC_VAL_HEAP_CORRUPT
  mov   qword [rsi+CHUNK_OFFSET_SIZE],  rax
  mov   qword [rsi+CHUNK_OFFSET_NEXT],  0
  mov   qword [rsi+CHUNK_OFFSET_PREV],  0
  mov   qword [rsi+CHUNK_OFFSET_INUSE], 1

  ; check if this chunk can be splitted
  mov   rdi, rsi
  mov   rsi, qword [rsp]
  call  can_split

  cmp   rax, 0
  je    .found

  mov   rdi, [rsp+0x8]
  mov   rsi, qword [rsp]
  call  _split

  mov   rax, [rsp+0x8]
  jmp   .found

.error:
  mov   rax, FAILURE_CODE
  jmp   .return

.null:
  xor   rax, rax
  jmp   .return

.found:
  ; save chunk pointer onto the stack
  mov   rax, [rsp+0x8]

  ; make sure the heap is not corrupted
  cmp   qword [rax+CHUNK_OFFSET_MAGIC], MAGIC_VAL_HEAP_CORRUPT
  jne   .error

  ; reset next and prev pointers
  mov   qword [rax+CHUNK_OFFSET_NEXT],  0
  mov   qword [rax+CHUNK_OFFSET_PREV],  0
  mov   qword [rax+CHUNK_OFFSET_INUSE], 1

  add   rax, CHUNK_OFFSET_DATA

.return:
  add   rsp, 0x18
  ret
