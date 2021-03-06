// RUN: %clangxx -fsanitize=vptr -g %s -O3 -o %t
// RUN: %run %t rT && %run %t mT && %run %t fT && %run %t cT
// RUN: %run %t rU && %run %t mU && %run %t fU && %run %t cU
// RUN: %run %t rS && %run %t rV && %run %t oV
// RUN: %run %t mS 2>&1 | FileCheck %s --check-prefix=CHECK-MEMBER --strict-whitespace
// RUN: %run %t fS 2>&1 | FileCheck %s --check-prefix=CHECK-MEMFUN --strict-whitespace
// RUN: %run %t cS 2>&1 | FileCheck %s --check-prefix=CHECK-DOWNCAST --strict-whitespace
// RUN: %run %t mV 2>&1 | FileCheck %s --check-prefix=CHECK-MEMBER --strict-whitespace
// RUN: %run %t fV 2>&1 | FileCheck %s --check-prefix=CHECK-MEMFUN --strict-whitespace
// RUN: %run %t cV 2>&1 | FileCheck %s --check-prefix=CHECK-DOWNCAST --strict-whitespace
// RUN: %run %t oU 2>&1 | FileCheck %s --check-prefix=CHECK-OFFSET --strict-whitespace
// RUN: %run %t m0 2>&1 | FileCheck %s --check-prefix=CHECK-NULL-MEMBER --strict-whitespace

// RUN: (echo "vptr_check:S"; echo "vptr_check:T"; echo "vptr_check:U") > %t.supp
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t mS 2>&1
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t fS 2>&1
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t cS 2>&1
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t mV 2>&1
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t fV 2>&1
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t cV 2>&1
// RUN: ASAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.supp':halt_on_error=1" %run %t oU 2>&1

// RUN: echo "vptr_check:S" > %t.loc-supp
// RUN: ASAN_OPTIONS="suppressions='%t.loc-supp':halt_on_error=1" UBSAN_OPTIONS="suppressions='%t.loc-supp':halt_on_error=1" not %run %t x- 2>&1 | FileCheck %s --check-prefix=CHECK-LOC-SUPPRESS

// FIXME: This test produces linker errors on Darwin.
// XFAIL: darwin
// REQUIRES: stable-runtime

extern "C" {
const char *__ubsan_default_options() {
  return "print_stacktrace=1";
}
}

struct S {
  S() : a(0) {}
  ~S() {}
  int a;
  int f() { return 0; }
  virtual int v() { return 0; }
};

struct T : S {
  T() : b(0) {}
  int b;
  int g() { return 0; }
  virtual int v() { return 1; }
};

struct X {};
struct U : S, T, virtual X { virtual int v() { return 2; } };

struct V : S {};

// Make p global so that lsan does not complain.
T *p = 0;

int access_p(T *p, char type);

int main(int, char **argv) {
  T t;
  (void)t.a;
  (void)t.b;
  (void)t.f();
  (void)t.g();
  (void)t.v();
  (void)t.S::v();

  U u;
  (void)u.T::a;
  (void)u.b;
  (void)u.T::f();
  (void)u.g();
  (void)u.v();
  (void)u.T::v();
  (void)((T&)u).S::v();

  char Buffer[sizeof(U)] = {};
  switch (argv[1][1]) {
  case '0':
    p = reinterpret_cast<T*>(Buffer);
    break;
  case 'S':
    p = reinterpret_cast<T*>(new S);
    break;
  case 'T':
    p = new T;
    break;
  case 'U':
    p = new U;
    break;
  case 'V':
    p = reinterpret_cast<T*>(new U);
    break;
  }

  access_p(p, argv[1][0]);
  return 0;
}

int access_p(T *p, char type) {
  switch (type) {
  case 'r':
    // Binding a reference to storage of appropriate size and alignment is OK.
    {T &r = *p;}
    break;

  case 'x':
    for (int i = 0; i < 2; i++) {
      // Check that the first iteration ("S") succeeds, while the second ("V") fails.
      p = reinterpret_cast<T*>((i == 0) ? new S : new V);
      // CHECK-LOC-SUPPRESS: vptr.cpp:[[@LINE+5]]:7: runtime error: member call on address [[PTR:0x[0-9a-f]*]] which does not point to an object of type 'T'
      // CHECK-LOC-SUPPRESS-NEXT: [[PTR]]: note: object is of type 'V'
      // CHECK-LOC-SUPPRESS-NEXT: {{^ .. .. .. ..  .. .. .. .. .. .. .. ..  }}
      // CHECK-LOC-SUPPRESS-NEXT: {{^              \^~~~~~~~~~~(~~~~~~~~~~~~)? *$}}
      // CHECK-LOC-SUPPRESS-NEXT: {{^              vptr for 'V'}}
      p->g();
    }
    return 0;

  case 'm':
    // CHECK-MEMBER: vptr.cpp:[[@LINE+6]]:15: runtime error: member access within address [[PTR:0x[0-9a-f]*]] which does not point to an object of type 'T'
    // CHECK-MEMBER-NEXT: [[PTR]]: note: object is of type [[DYN_TYPE:'S'|'U']]
    // CHECK-MEMBER-NEXT: {{^ .. .. .. ..  .. .. .. .. .. .. .. ..  }}
    // CHECK-MEMBER-NEXT: {{^              \^~~~~~~~~~~(~~~~~~~~~~~~)? *$}}
    // CHECK-MEMBER-NEXT: {{^              vptr for}} [[DYN_TYPE]]
    // CHECK-MEMBER-NEXT: #0 {{.*}} in access_p{{.*}}vptr.cpp:[[@LINE+1]]
    return p->b;

    // CHECK-NULL-MEMBER: vptr.cpp:[[@LINE-2]]:15: runtime error: member access within address [[PTR:0x[0-9a-f]*]] which does not point to an object of type 'T'
    // CHECK-NULL-MEMBER-NEXT: [[PTR]]: note: object has invalid vptr
    // CHECK-NULL-MEMBER-NEXT: {{^  ?.. .. .. ..  ?00 00 00 00  ?00 00 00 00  ?}}
    // CHECK-NULL-MEMBER-NEXT: {{^              \^~~~~~~~~~~(~~~~~~~~~~~~)? *$}}
    // CHECK-NULL-MEMBER-NEXT: {{^              invalid vptr}}
    // CHECK-NULL-MEMBER-NEXT: #0 {{.*}} in access_p{{.*}}vptr.cpp:[[@LINE-7]]

  case 'f':
    // CHECK-MEMFUN: vptr.cpp:[[@LINE+6]]:12: runtime error: member call on address [[PTR:0x[0-9a-f]*]] which does not point to an object of type 'T'
    // CHECK-MEMFUN-NEXT: [[PTR]]: note: object is of type [[DYN_TYPE:'S'|'U']]
    // CHECK-MEMFUN-NEXT: {{^ .. .. .. ..  .. .. .. .. .. .. .. ..  }}
    // CHECK-MEMFUN-NEXT: {{^              \^~~~~~~~~~~(~~~~~~~~~~~~)? *$}}
    // CHECK-MEMFUN-NEXT: {{^              vptr for}} [[DYN_TYPE]]
    // TODO: Add check for stacktrace here.
    return p->g();

  case 'o':
    // CHECK-OFFSET: vptr.cpp:[[@LINE+6]]:12: runtime error: member call on address [[PTR:0x[0-9a-f]*]] which does not point to an object of type 'U'
    // CHECK-OFFSET-NEXT: 0x{{[0-9a-f]*}}: note: object is base class subobject at offset {{8|16}} within object of type [[DYN_TYPE:'U']]
    // CHECK-OFFSET-NEXT: {{^ .. .. .. ..  .. .. .. .. .. .. .. ..  .. .. .. .. .. .. .. ..  .. .. .. .. .. .. .. ..  }}
    // CHECK-OFFSET-NEXT: {{^              \^                        (                         ~~~~~~~~~~~~)?~~~~~~~~~~~ *$}}
    // CHECK-OFFSET-NEXT: {{^                                       (                         )?vptr for}} 'T' base class of [[DYN_TYPE]]
    // CHECK-OFFSET-NEXT: #0 {{.*}} in access_p{{.*}}vptr.cpp:[[@LINE+1]]
    return reinterpret_cast<U*>(p)->v() - 2;

  case 'c':
    // CHECK-DOWNCAST: vptr.cpp:[[@LINE+6]]:5: runtime error: downcast of address [[PTR:0x[0-9a-f]*]] which does not point to an object of type 'T'
    // CHECK-DOWNCAST-NEXT: [[PTR]]: note: object is of type [[DYN_TYPE:'S'|'U']]
    // CHECK-DOWNCAST-NEXT: {{^ .. .. .. ..  .. .. .. .. .. .. .. ..  }}
    // CHECK-DOWNCAST-NEXT: {{^              \^~~~~~~~~~~(~~~~~~~~~~~~)? *$}}
    // CHECK-DOWNCAST-NEXT: {{^              vptr for}} [[DYN_TYPE]]
    // CHECK-DOWNCAST-NEXT: #0 {{.*}} in access_p{{.*}}vptr.cpp:[[@LINE+1]]
    static_cast<T*>(reinterpret_cast<S*>(p));
    return 0;
  }
}
