%ifndef FILE_INC
%define FILE_INC

; extract extension of a file
; @param  rdi: file_path
; @return rax: content type
get_ext:
  sub   rsp, 0x8

  ; find next dot
  mov   rsi, DOT 
  call  find_next_char

  cmp   rax, 0
  jl    no_ext

  ; go to dot
  add   rdi, rax

  ; make sure there is no extra dot
  mov   rax, DOT
  call  find_next_char

  cmp   rax, 0
  jg    not_supported

  ; save . position
  mov   [rsp], rdi

  lea   rdi, [HTML_EXT]
  lea   rax, [rsp]
  mov   rsi, [rax]
  call  cmpstr

  cmp   rax, 1
  jne   check_js

  lea   rax, [CONTENT_HTML]
  jmp   exit_get_ext

check_js:
  lea   rdi, [JS_EXT]
  lea   rax, [rsp]
  mov   rsi, [rax]
  call  cmpstr

  cmp   rax, 1
  jne   check_css

  lea   rax, [CONTENT_JS]
  jmp   exit_get_ext
  
check_css:
  lea   rdi, [CSS_EXT]
  lea   rax, [rsp]
  mov   rsi, [rax]
  call  cmpstr

  cmp   rax, 1
  jne   check_plain

  lea   rax, [CONTENT_CSS]
  jmp   exit_get_ext
 
check_plain:
  lea   rdi, [TEXT_EXT]
  lea   rax, [rsp]
  mov   rsi, [rax]
  call  cmpstr

  cmp   rax, 1
  jne   not_supported

  lea   rax, [CONTENT_PLAIN]
  jmp   exit_get_ext

no_ext:
  mov   rax, 0
  jmp   exit_get_ext

not_supported:
  mov   rax, -1

exit_get_ext:
  add   rsp, 0x8
  ret

%endif
