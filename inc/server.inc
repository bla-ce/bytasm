section .bss

section .data
  ; server constants
  DEFAULT_MAX_CONNECTIONS equ 0x1

  PORT_MIN     equ 0x1
  DEFAULT_PORT equ 8080
  PORT_MAX     equ 0xffff

  server_struct:
    server_sockfd             dq 0  ; socket file descriptor
    server_routes             dq 0  ; pointer to linked list of routes
    server_sockaddr           dq 0  ; pointer to sockaddr struct
    server_middlewares        dq 0  ; pointer to middlewares linked list
    server_port               dw 0  ; port
  end_server_struct:

  SERVER_STRUCT_LEN equ end_server_struct - server_struct

  ; offsets
  SERVER_OFF_SOCKFD              equ 0x0
  SERVER_OFF_ROUTES              equ SERVER_OFF_SOCKFD + 0x8
  SERVER_OFF_SOCKADDR            equ SERVER_OFF_ROUTES + 0x8
  SERVER_OFF_MIDDLEWARES         equ SERVER_OFF_SOCKADDR + 0x8
  SERVER_OFF_PORT                equ SERVER_OFF_MIDDLEWARES + 0x8

section .text
; returns the socket fd associated to the server
; @param  rdi: pointer to server struct 
; @return rax: socket fd
get_server_sockfd:
  cmp   rdi, 0
  jle   .error

  mov   rax, qword [rdi+SERVER_OFF_SOCKFD]
  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ret

; sets the socket fd associated to the server
; @param  rdi: pointer to server struct 
; @param  rsi: sockfd
; @return rax: return code
set_server_sockfd:
  cmp   rdi, 0
  jle   .error

  cmp   rsi, 0
  jl    .error

  mov   qword [rdi+SERVER_OFF_SOCKFD], rsi

  mov   rax, SUCCESS_CODE
  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ret

; returns the port associated to the server
; @param  rdi: pointer to server struct 
; @return rax: port
get_server_port:
  cmp   rdi, 0
  jle   .error

  mov   rax, qword [rdi+SERVER_OFF_PORT]
  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ret

; sets the port the server will be listening to
; @param  rdi: pointer to server struct 
; @param  rsi: port
; @return rax: return code
set_server_port:
  cmp   rdi, 0
  jle   .error

  cmp   rsi, 0
  jl    .error

  mov   dword [rdi+SERVER_OFF_PORT], esi

  mov   rax, SUCCESS_CODE
  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ret

; returns the first route struct associated to the server
; @param  rdi: pointer to server struct 
; @return rax: pointer to the first route struct
get_server_routes:
  cmp   rdi, 0
  jle   .error

  mov   rax, [rdi+SERVER_OFF_ROUTES]
  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ret

; returns the first route struct associated to the server
; @param  rdi: pointer to server struct 
; @return rax: pointer to the first middleware struct
get_server_middlewares:
  cmp   rdi, 0
  jle   .error

  mov   rax, [rdi+SERVER_OFF_MIDDLEWARES]
  jmp   .return

.error:
  mov   rax, FAILURE_CODE

.return:
  ret

; converts a integer from host byte order to network byte order (big-endian).
; @param  rdi: integer
; @return rax: integer to network byte order
htons:
  mov   rax, rdi
  xchg  al, ah ; bswap 16-bit registers
  ret

; initialise server
; @param  rdi: port
; @return rax: pointer to server struct | error
server_init:
  sub   rsp, 0x18

  cmp   rdi, 0
  jg    .port_defined

  ; check if port is in environment variables
  lea   rdi, [PORT_ENV]
  call  getenv

  cmp   rax, 0
  jl    .default_port

  lea   rdi, [rax]
  call  stoi

  mov   rdi, rax

  cmp   rax, 0
  jg    .port_defined

.default_port:
  mov   rbx, DEFAULT_PORT
  cmp   rax, 0
  cmovl rax, rbx

  mov   rdi, rbx

.port_defined:
  mov   qword [rsp], rdi
  mov   qword [rsp+0x8], 0

  ; malloc server struct
  mov   rdi, SERVER_STRUCT_LEN
  call  malloc

  cmp   rax, 0
  jl    .error

  mov   [rsp+0x8], rax

  ; set default values
  mov   qword [rdi+SERVER_OFF_ROUTES], 0
  mov   qword [rdi+SERVER_OFF_MIDDLEWARES], 0

  mov   rdi, [rsp+0x8]

  ; check if port is in bounds
  cmp   qword [rsp], PORT_MIN
  jl    .error

  cmp   qword [rsp], PORT_MAX
  jg    .error

  ; add port to server struct
  mov   rdi, [rsp+0x8]
  mov   rsi, qword [rsp]
  call  set_server_port

  cmp   rax, 0
  jl    .error

  mov   rdi, AF_INET
  mov   rsi, SOCK_STREAM
  mov   rdx, 0
  call  create_socket

  cmp   rax, 0
  jl    .error

  mov   qword [rsp+0x10], rax

  mov   rdi, [rsp+0x8]
  mov   rsi, rax
  call  set_server_sockfd

  cmp   rax, 0
  jl    .error

  ; set socket options
  mov   rdi, qword [rsp+0x10]
  mov   rsi, SOL_SOCKET
  mov   rdx, SO_REUSEPORT
  call  set_socket_option

  cmp   rax, 0
  jl    .error

  mov   rdi, qword [rsp+0x10]
  mov   rsi, SOL_SOCKET
  mov   rdx, SO_REUSEADDR
  call  set_socket_option

  cmp   rax, 0
  jl    .error

  mov   rdi, [rsp+0x8]
  call  get_server_port

  cmp   rax, 0
  jl    .error

  mov   rsi, rax
  mov   rdi, qword [rsp+0x10]
  mov   rdx, AF_INET
  call  bind_socket

  cmp   rax, 0
  jl    .error

  ; listen socket
  mov   rdi, qword [rsp+0x10]
  xor   rsi, rsi
  call  listen_socket

  cmp   rax, 0
  jl    .error

  mov   rax, [rsp+0x8]
  jmp   .return

.error:
  ; check if socket have been opened
  cmp   qword [rsp+0x8], 0
  jle   .skip_close_socket

  ; if yes close it
  mov   rax, SYS_CLOSE
  mov   rdi, qword [rsp+0x10]
  syscall

  ; free server struct
  mov   rax, [rsp+0x8]
  test  rax, rax
  jz    .skip_close_socket

  mov   rdi, rax
  call  free

.skip_close_socket:
  mov   rax, FAILURE_CODE

.return:
  add   rsp, 0x18
  ret

; runs the server
; @param  rdi: pointer to server struct
; @param  rax: return value
run_server:
  sub   rsp, 0x28

  mov   [rsp], rdi
  mov   qword [rsp+0x8], 0
  mov   qword [rsp+0x10], 0

  call  log_server_init

.loop:
  mov   rdi, [rsp]
  call  ctx_init

  cmp   rax, 0
  jl    .close_server

  mov   [rsp+0x10], rax

  ; accept connection
  mov   rdi, [rsp+0x10]
  call  accept_connection

  cmp   rax, 0
  jl    .close_server

  ; set start timer -> TODO: malloc?
  mov   rax, SYS_CLOCK_GETTIME
  mov   rdi, CLOCK_REALTIME
  lea   rsi, [start_tv_sec]
  syscall

  cmp   rax, 0
  jl   .ctx_close 

  mov   rdi, [rsp+0x10]
  mov   rsi, qword [start_tv_nsec]
  call  set_ctx_start_time

  cmp   rax, 0
  jl   .ctx_close 

  mov   rdi, [rsp+0x10]
  call  receive_request

  cmp   rax, 0
  jl   .ctx_close 

  ; parse_request
  mov   rdi, [rsp+0x10]
  call  parse_request

  cmp   rax, 0
  jl    .ctx_close

; check if ctx has been aborted
  mov   rdi, [rsp+0x10]
  call  is_ctx_aborted

  cmp   rax, TRUE
  je    .ctx_close

  mov   rdi, [rsp]
  mov   rsi, [rsp+0x10]
  call  run_middlewares

; check if ctx has been aborted
  mov   rdi, [rsp+0x10]
  call  is_ctx_aborted

  cmp   rax, TRUE
  je    .ctx_close

  ; verify request
  mov   rdi, [rsp+0x10]
  call  verify_request
  cmp   rax, 0
  jl   .ctx_close

  mov   [rsp+0x18], rax

  ; check if ctx has been aborted
  mov   rdi, [rsp+0x10]
  call  is_ctx_aborted

  cmp   rax, TRUE
  je    .ctx_close

  ; check if route has a callback
  mov   rdi, [rsp+0x18]
  call  get_route_callback

  cmp   rax, 0
  je   .ctx_close 

  mov   rdi, [rsp+0x10]
  call  rax

.ctx_close:
  mov   rdi, [rsp+0x10]
  call  ctx_close

.log_request:
  ; TODO: update to use ctx
  ; set end timer
  mov   rax, SYS_CLOCK_GETTIME
  mov   rdi, CLOCK_REALTIME
  lea   rsi, [end_tv_sec]
  syscall

  ; if yes, compute the difference
  mov   rdi, [rsp+0x10]
  mov   rsi, qword [end_tv_nsec]
  call  set_ctx_end_time

  cmp   rax, 0
  jl    .error

  ; log previous request
  mov   rdi, [rsp+0x10]
  call  log_ctx

.free_ctx:
  mov   rdi, [rsp+0x10]
  call  free_ctx

  jmp   .loop

.close_server:
  ; shutdown server
  mov   rax, SYS_CLOSE
  mov   rdi, qword [rsp]
  syscall

.error:
  mov   rax, FAILURE_CODE

  add   rsp, 0x28
  ret


