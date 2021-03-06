//===-- StreamChecker.cpp -----------------------------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines checkers that model and check stream handling functions.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/ImmutableMap.h"

using namespace clang;
using namespace ento;

namespace {

struct StreamState {
  enum Kind { Opened, Closed, OpenFailed, Escaped } K;
  const Stmt *S;

  StreamState(Kind k, const Stmt *s) : K(k), S(s) {}

  bool isOpened() const { return K == Opened; }
  bool isClosed() const { return K == Closed; }
  //bool isOpenFailed() const { return K == OpenFailed; }
  //bool isEscaped() const { return K == Escaped; }

  bool operator==(const StreamState &X) const {
    return K == X.K && S == X.S;
  }

  static StreamState getOpened(const Stmt *s) { return StreamState(Opened, s); }
  static StreamState getClosed(const Stmt *s) { return StreamState(Closed, s); }
  static StreamState getOpenFailed(const Stmt *s) { 
    return StreamState(OpenFailed, s); 
  }
  static StreamState getEscaped(const Stmt *s) {
    return StreamState(Escaped, s);
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(S);
  }
};

class StreamChecker : public Checker<eval::Call,
                                       check::DeadSymbols,
                                       check::EndPath,
                                       check::PreStmt<ReturnStmt> > {
  mutable IdentifierInfo *II_fopen, *II_tmpfile, *II_fclose, *II_fread,
                 *II_fwrite, 
                 *II_fseek, *II_ftell, *II_rewind, *II_fgetpos, *II_fsetpos,  
                 *II_clearerr, *II_feof, *II_ferror, *II_fileno;
  mutable llvm::OwningPtr<BuiltinBug> BT_nullfp, BT_illegalwhence,
                                      BT_doubleclose, BT_ResourceLeak;

public:
  StreamChecker() 
    : II_fopen(0), II_tmpfile(0) ,II_fclose(0), II_fread(0), II_fwrite(0), 
      II_fseek(0), II_ftell(0), II_rewind(0), II_fgetpos(0), II_fsetpos(0), 
      II_clearerr(0), II_feof(0), II_ferror(0), II_fileno(0) {}

  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  void checkEndPath(CheckerContext &Ctx) const;
  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const;

private:
  void Fopen(CheckerContext &C, const CallExpr *CE) const;
  void Tmpfile(CheckerContext &C, const CallExpr *CE) const;
  void Fclose(CheckerContext &C, const CallExpr *CE) const;
  void Fread(CheckerContext &C, const CallExpr *CE) const;
  void Fwrite(CheckerContext &C, const CallExpr *CE) const;
  void Fseek(CheckerContext &C, const CallExpr *CE) const;
  void Ftell(CheckerContext &C, const CallExpr *CE) const;
  void Rewind(CheckerContext &C, const CallExpr *CE) const;
  void Fgetpos(CheckerContext &C, const CallExpr *CE) const;
  void Fsetpos(CheckerContext &C, const CallExpr *CE) const;
  void Clearerr(CheckerContext &C, const CallExpr *CE) const;
  void Feof(CheckerContext &C, const CallExpr *CE) const;
  void Ferror(CheckerContext &C, const CallExpr *CE) const;
  void Fileno(CheckerContext &C, const CallExpr *CE) const;

  void OpenFileAux(CheckerContext &C, const CallExpr *CE) const;
  
  const ProgramState *CheckNullStream(SVal SV, const ProgramState *state, 
                                 CheckerContext &C) const;
  const ProgramState *CheckDoubleClose(const CallExpr *CE, const ProgramState *state, 
                                 CheckerContext &C) const;
};

} // end anonymous namespace

namespace clang {
namespace ento {
  template <>
  struct ProgramStateTrait<StreamState> 
    : public ProgramStatePartialTrait<llvm::ImmutableMap<SymbolRef, StreamState> > {
    static void *GDMIndex() { static int x; return &x; }
  };
}
}

bool StreamChecker::evalCall(const CallExpr *CE, CheckerContext &C) const {
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD)
    return false;

  ASTContext &Ctx = C.getASTContext();
  if (!II_fopen)
    II_fopen = &Ctx.Idents.get("fopen");
  if (!II_tmpfile)
    II_tmpfile = &Ctx.Idents.get("tmpfile");
  if (!II_fclose)
    II_fclose = &Ctx.Idents.get("fclose");
  if (!II_fread)
    II_fread = &Ctx.Idents.get("fread");
  if (!II_fwrite)
    II_fwrite = &Ctx.Idents.get("fwrite");
  if (!II_fseek)
    II_fseek = &Ctx.Idents.get("fseek");
  if (!II_ftell)
    II_ftell = &Ctx.Idents.get("ftell");
  if (!II_rewind)
    II_rewind = &Ctx.Idents.get("rewind");
  if (!II_fgetpos)
    II_fgetpos = &Ctx.Idents.get("fgetpos");
  if (!II_fsetpos)
    II_fsetpos = &Ctx.Idents.get("fsetpos");
  if (!II_clearerr)
    II_clearerr = &Ctx.Idents.get("clearerr");
  if (!II_feof)
    II_feof = &Ctx.Idents.get("feof");
  if (!II_ferror)
    II_ferror = &Ctx.Idents.get("ferror");
  if (!II_fileno)
    II_fileno = &Ctx.Idents.get("fileno");

  if (FD->getIdentifier() == II_fopen) {
    Fopen(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_tmpfile) {
    Tmpfile(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fclose) {
    Fclose(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fread) {
    Fread(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fwrite) {
    Fwrite(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fseek) {
    Fseek(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_ftell) {
    Ftell(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_rewind) {
    Rewind(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fgetpos) {
    Fgetpos(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fsetpos) {
    Fsetpos(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_clearerr) {
    Clearerr(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_feof) {
    Feof(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_ferror) {
    Ferror(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fileno) {
    Fileno(C, CE);
    return true;
  }

  return false;
}

void StreamChecker::Fopen(CheckerContext &C, const CallExpr *CE) const {
  OpenFileAux(C, CE);
}

void StreamChecker::Tmpfile(CheckerContext &C, const CallExpr *CE) const {
  OpenFileAux(C, CE);
}

void StreamChecker::OpenFileAux(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  unsigned Count = C.getCurrentBlockCount();
  SValBuilder &svalBuilder = C.getSValBuilder();
  DefinedSVal RetVal =
    cast<DefinedSVal>(svalBuilder.getConjuredSymbolVal(0, CE, Count));
  state = state->BindExpr(CE, RetVal);
  
  ConstraintManager &CM = C.getConstraintManager();
  // Bifurcate the state into two: one with a valid FILE* pointer, the other
  // with a NULL.
  const ProgramState *stateNotNull, *stateNull;
  llvm::tie(stateNotNull, stateNull) = CM.assumeDual(state, RetVal);
  
  if (SymbolRef Sym = RetVal.getAsSymbol()) {
    // if RetVal is not NULL, set the symbol's state to Opened.
    stateNotNull =
      stateNotNull->set<StreamState>(Sym,StreamState::getOpened(CE));
    stateNull =
      stateNull->set<StreamState>(Sym, StreamState::getOpenFailed(CE));

    C.addTransition(stateNotNull);
    C.addTransition(stateNull);
  }
}

void StreamChecker::Fclose(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = CheckDoubleClose(CE, C.getState(), C);
  if (state)
    C.addTransition(state);
}

void StreamChecker::Fread(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(3)), state, C))
    return;
}

void StreamChecker::Fwrite(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(3)), state, C))
    return;
}

void StreamChecker::Fseek(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!(state = CheckNullStream(state->getSVal(CE->getArg(0)), state, C)))
    return;
  // Check the legality of the 'whence' argument of 'fseek'.
  SVal Whence = state->getSVal(CE->getArg(2));
  const nonloc::ConcreteInt *CI = dyn_cast<nonloc::ConcreteInt>(&Whence);

  if (!CI)
    return;

  int64_t x = CI->getValue().getSExtValue();
  if (x >= 0 && x <= 2)
    return;

  if (ExplodedNode *N = C.addTransition(state)) {
    if (!BT_illegalwhence)
      BT_illegalwhence.reset(new BuiltinBug("Illegal whence argument",
					"The whence argument to fseek() should be "
					"SEEK_SET, SEEK_END, or SEEK_CUR."));
    BugReport *R = new BugReport(*BT_illegalwhence, 
				 BT_illegalwhence->getDescription(), N);
    C.EmitReport(R);
  }
}

void StreamChecker::Ftell(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Rewind(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Fgetpos(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Fsetpos(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Clearerr(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Feof(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Ferror(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Fileno(CheckerContext &C, const CallExpr *CE) const {
  const ProgramState *state = C.getState();
  if (!CheckNullStream(state->getSVal(CE->getArg(0)), state, C))
    return;
}

const ProgramState *StreamChecker::CheckNullStream(SVal SV, const ProgramState *state,
                                    CheckerContext &C) const {
  const DefinedSVal *DV = dyn_cast<DefinedSVal>(&SV);
  if (!DV)
    return 0;

  ConstraintManager &CM = C.getConstraintManager();
  const ProgramState *stateNotNull, *stateNull;
  llvm::tie(stateNotNull, stateNull) = CM.assumeDual(state, *DV);

  if (!stateNotNull && stateNull) {
    if (ExplodedNode *N = C.generateSink(stateNull)) {
      if (!BT_nullfp)
        BT_nullfp.reset(new BuiltinBug("NULL stream pointer",
                                     "Stream pointer might be NULL."));
      BugReport *R =new BugReport(*BT_nullfp, BT_nullfp->getDescription(), N);
      C.EmitReport(R);
    }
    return 0;
  }
  return stateNotNull;
}

const ProgramState *StreamChecker::CheckDoubleClose(const CallExpr *CE,
                                               const ProgramState *state,
                                               CheckerContext &C) const {
  SymbolRef Sym = state->getSVal(CE->getArg(0)).getAsSymbol();
  if (!Sym)
    return state;
  
  const StreamState *SS = state->get<StreamState>(Sym);

  // If the file stream is not tracked, return.
  if (!SS)
    return state;
  
  // Check: Double close a File Descriptor could cause undefined behaviour.
  // Conforming to man-pages
  if (SS->isClosed()) {
    ExplodedNode *N = C.generateSink();
    if (N) {
      if (!BT_doubleclose)
        BT_doubleclose.reset(new BuiltinBug("Double fclose",
                                        "Try to close a file Descriptor already"
                                        " closed. Cause undefined behaviour."));
      BugReport *R = new BugReport(*BT_doubleclose,
                                   BT_doubleclose->getDescription(), N);
      C.EmitReport(R);
    }
    return NULL;
  }
  
  // Close the File Descriptor.
  return state->set<StreamState>(Sym, StreamState::getClosed(CE));
}

void StreamChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                     CheckerContext &C) const {
  for (SymbolReaper::dead_iterator I = SymReaper.dead_begin(),
         E = SymReaper.dead_end(); I != E; ++I) {
    SymbolRef Sym = *I;
    const ProgramState *state = C.getState();
    const StreamState *SS = state->get<StreamState>(Sym);
    if (!SS)
      return;

    if (SS->isOpened()) {
      ExplodedNode *N = C.generateSink();
      if (N) {
        if (!BT_ResourceLeak)
          BT_ResourceLeak.reset(new BuiltinBug("Resource Leak", 
                         "Opened File never closed. Potential Resource leak."));
        BugReport *R = new BugReport(*BT_ResourceLeak, 
                                     BT_ResourceLeak->getDescription(), N);
        C.EmitReport(R);
      }
    }
  }
}

void StreamChecker::checkEndPath(CheckerContext &Ctx) const {
  const ProgramState *state = Ctx.getState();
  typedef llvm::ImmutableMap<SymbolRef, StreamState> SymMap;
  SymMap M = state->get<StreamState>();
  
  for (SymMap::iterator I = M.begin(), E = M.end(); I != E; ++I) {
    StreamState SS = I->second;
    if (SS.isOpened()) {
      ExplodedNode *N = Ctx.addTransition(state);
      if (N) {
        if (!BT_ResourceLeak)
          BT_ResourceLeak.reset(new BuiltinBug("Resource Leak", 
                         "Opened File never closed. Potential Resource leak."));
        BugReport *R = new BugReport(*BT_ResourceLeak, 
                                     BT_ResourceLeak->getDescription(), N);
        Ctx.EmitReport(R);
      }
    }
  }
}

void StreamChecker::checkPreStmt(const ReturnStmt *S, CheckerContext &C) const {
  const Expr *RetE = S->getRetValue();
  if (!RetE)
    return;
  
  const ProgramState *state = C.getState();
  SymbolRef Sym = state->getSVal(RetE).getAsSymbol();
  
  if (!Sym)
    return;
  
  const StreamState *SS = state->get<StreamState>(Sym);
  if(!SS)
    return;

  if (SS->isOpened())
    state = state->set<StreamState>(Sym, StreamState::getEscaped(S));

  C.addTransition(state);
}

void ento::registerStreamChecker(CheckerManager &mgr) {
  mgr.registerChecker<StreamChecker>();
}
