; RUN: not llvm-as --disable-output %s 2>&1 | FileCheck -DFILE=%s %s 

define void @f () {
; CHECK: [[FILE]]:[[@LINE+1]]:1: error: NUL character is not allowed in names
" ":
  ret void
}
