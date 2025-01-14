section .data
  STR_CONTENT_LENGTH_MAX_LENGTH equ 7 ; add one for null char

section .text
send_response:
  ; rdi -> status code str
  ; rsi -> content type
  ; rdx -> body
  ; rcx -> pointer to request struct
  sub   rsp, 0x30

  cmp   rdi, 0
  jle   .error

  cmp   rcx, 0
  jle   .error

  mov   r9, [rcx]
  add   r9, REQ_OFF_CLIENT
  mov   qword [rsp], r9
  mov   [rsp+0x8], rdi
  mov   [rsp+0x10], rsi
  mov   [rsp+0x18], rdx

  ; load protocol
  lea   rdi, [HTTP_1_1]
  call  strlen

  mov   rcx, rax
  lea   rdi, [response]
  lea   rsi, [HTTP_1_1]
  rep   movsb

  ; load space
  xor   rax, rax
  mov   al, SPACE
  stosb

  mov   [rsp+0x20], rdi

  ; load status code
  mov   rdi, [rsp+0x8]
  call  strlen

  mov   rcx, rax
  mov   rdi, [rsp+0x20]
  mov   rsi, [rsp+0x8]
  rep   movsb

  ; load new line
  xor   rax, rax
  mov   al, CARRIAGE_RETURN
  stosb

  mov   al, LINE_FEED
  stosb

  mov   [rsp+0x20], rdi

  ; set connection header
  lea   rdi, [response_headers]
  lea   rsi, [CONNECTION_HEADER]
  lea   rdx, [CONNECTION_CLOSE]
  call  set_header

  ; set content_length
  ; get body
  mov   rdi, [rsp+0x18]
  call  strlen
  cmp   rax, 0
  jle   .skip_body

  mov   qword [rsp+0x28], rax

  mov   rdi, STR_CONTENT_LENGTH_MAX_LENGTH
  call  malloc

  cmp   rax, 0
  jl    .error

  ; set content length header
  mov   rdi, qword [rsp+0x28]
  mov   rsi, rax
  mov   rdx, STR_CONTENT_LENGTH_MAX_LENGTH
  call  to_string

  cmp   rax, 0
  jl    .error

  lea   rdi, [response_headers]
  lea   rsi, [CONTENT_LENGTH_HEADER]
  mov   rdx, rax
  call  set_header

  ; set content type header
  lea   rdi, [response_headers]
  lea   rsi, [CONTENT_TYPE_HEADER]
  mov   rdx, [rsp+0x10]
  call  set_header

  ; load headers
  mov   rdi, [rsp+0x20]
  lea   rsi, [response_headers]
  call  load_headers

  mov   [rsp+0x20], rax

  ; load body
  mov   rdi, [rsp+0x18]
  call  strlen
  mov   rcx, rax
  lea   rsi, [rdi]
  mov   rdi, [rsp+0x20]
  rep   movsb

.skip_body:
  ; get response length
  lea   rdi, [response]
  call  strlen
  mov   rdx, rax

  ; send response
  mov   rax, SYS_WRITE
  mov   rdi, qword [rsp]
  lea   rsi, [response]
  syscall

  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ; clean stack
  add   rsp, 0x30

  ret

; TODO: pass response as arg
send_error_response:
  ; rdi -> sockfd
  ; rsi -> status code str
  sub   rsp, 0x18

  cmp   rdi, 0
  jle   .error

  cmp   rsi, 0
  jle   .error

  mov   qword [rsp], rdi
  mov   [rsp+0x8], rsi

  ; load protocol
  lea   rdi, [HTTP_1_1]
  call  strlen

  mov   rcx, rax
  lea   rdi, [response]
  lea   rsi, [HTTP_1_1]
  rep   movsb

  ; load space
  xor   rax, rax
  mov   al, SPACE
  stosb

  mov   [rsp+0x10], rdi

  ; load status code
  mov   rdi, [rsp+0x8]
  call  strlen

  mov   rcx, rax
  mov   rdi, [rsp+0x10]
  mov   rsi, [rsp+0x8]
  rep   movsb

  ; load new line
  xor   rax, rax
  mov   al, CARRIAGE_RETURN
  stosb

  mov   al, LINE_FEED
  stosb

  mov   [rsp+0x10], rdi

  ; load connection header
  lea   rdi, [response_headers]
  lea   rsi, [CONNECTION_HEADER]
  lea   rdx, [CONNECTION_CLOSE]
  call  set_header

  mov   rdi, [rsp+0x10]
  lea   rsi, [response_headers]
  call  load_headers
  
  lea   rdi, [response]
  call  strlen
  mov   rdx, rax

  ; send response
  mov   rax, SYS_WRITE
  mov   rdi, qword [rsp]
  lea   rsi, [response]
  syscall

  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ; clean stack
  add   rsp, 0x18
  ret

serve_string:
  ; rdi -> string
  ; rsi -> request pointer
  sub   rsp, 0x8 ; string

  cmp   rdi, 0
  jle   .error

  mov   [rsp], rdi

  mov   rcx, [rsi]
  add   rcx, REQ_OFF_CLIENT
  
  lea   rdi, [STR_OK]
  lea   rsi, [CONTENT_PLAIN]
  mov   rdx, [rsp]
  call  send_response

  mov   rax, SUCCESS_CODE

  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  add   rsp, 0x8
  ret

; TODO: make sure we have permissions
; serves a static file
; @param  rdi: file path
; @param  rsi: pointer to request struct
; @return rax: void
serve_static_file:
  sub   rsp, 0x28

  cmp   rdi, 0
  jle   .error

  mov   [rsp], rdi

  ; save request client fd
  mov   [rsp+0x20], rsi

  ; get file ext
  call  get_ext

  cmp   rax, 0
  jl    .error_open

  mov   [rsp+0x18], rax

  ; load file path
  mov   rdi, [rsp]

  ; open file
  mov   rsi, O_RDONLY
  mov   rdx, 0
  call  open_file

  cmp   rax, 0
  jl    .error_open

  mov   qword [rsp+0x10], rax

  ; read file
  mov   rdi, qword [rsp+0x10]
  call  read_file

  cmp   rax, 0
  jl    .error_read

  mov   [rsp+0x8], rax

  ; send response
  lea   rdi, [STR_OK]
  mov   rsi, [rsp+0x18]
  mov   rdx, [rsp+0x8]
  mov   rcx, qword [rsp+0x20]
  call  send_response

  mov   rax, SUCCESS_CODE

  ; free file content
  mov   rdi, [rsp+0x8]
  call  free

  ; clean stack
  add   rsp, 0x28
  ret

.error_open:
  ; load 404
  mov   qword [response_status_code], NOT_FOUND
  mov   rdi, qword [rsp+0x20]
  lea   rsi, [STR_NOT_FOUND]
  call  send_error_response
  jmp   .error

.error_read:
  ; load 500
  mov   qword [response_status_code], INTERNAL_SERVER_ERROR
  mov   rdi, qword [rsp+0x20]
  lea   rsi, [STR_INTERNAL_SERVER_ERROR]
  call  send_error_response

.error:
  ; clean stack
  add   rsp, 0x28

  ; return
  mov   rax, FAILURE_CODE
  ret

; serve file from directory
; @param  rdi: pointer to request struct
serve_dir:
  mov   rsi, rdi
  mov   rdx, rdi
  add   rdx, REQ_OFF_URL
  mov   rdi, [rdx]
  ; add one to remove '/'
  inc   rdi
  call  serve_static_file

  ret
