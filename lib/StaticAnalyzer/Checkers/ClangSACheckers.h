//===--- ClangSACheckers.h - Registration functions for Checkers *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares the registation functions for the checkers defined in
// libclangStaticAnalyzerCheckers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SA_LIB_CHECKERS_CLANGSACHECKERS_H
#define LLVM_CLANG_SA_LIB_CHECKERS_CLANGSACHECKERS_H

namespace clang {

namespace ento {
class ExprEngine;

#define GET_CHECKERS
#define CHECKER(FULLNAME,CLASS,CXXFILE,HELPTEXT,HIDDEN)    \
  void register##CLASS(ExprEngine &Eng);
#include "Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS

} // end ento namespace

} // end clang namespace

#endif