// Auto-generated invoke_* shims for Emscripten -fexceptions side module.
// Compiled with -fno-exceptions to avoid infinite recursion.
// Since Godot uses -fno-exceptions (__cxa_throw aborts),
// these stubs never need JS-side exception propagation.
// Function pointer cast generates call_indirect in wasm.
#ifdef __EMSCRIPTEN__
#include <stdint.h>
extern "C" {

double invoke_ddiiiii(int fn, double a1, int a2, int a3, int a4, int a5, int a6) {
  return reinterpret_cast<double(*)(double, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

double invoke_di(int fn, int a1) {
  return reinterpret_cast<double(*)(int)>(static_cast<uintptr_t>(fn))(a1);
}

double invoke_did(int fn, int a1, double a2) {
  return reinterpret_cast<double(*)(int, double)>(static_cast<uintptr_t>(fn))(a1, a2);
}

double invoke_didii(int fn, int a1, double a2, int a3, int a4) {
  return reinterpret_cast<double(*)(int, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

double invoke_dii(int fn, int a1, int a2) {
  return reinterpret_cast<double(*)(int, int)>(static_cast<uintptr_t>(fn))(a1, a2);
}

double invoke_diid(int fn, int a1, int a2, double a3) {
  return reinterpret_cast<double(*)(int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

double invoke_diii(int fn, int a1, int a2, int a3) {
  return reinterpret_cast<double(*)(int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

float invoke_fi(int fn, int a1) {
  return reinterpret_cast<float(*)(int)>(static_cast<uintptr_t>(fn))(a1);
}

float invoke_fif(int fn, int a1, float a2) {
  return reinterpret_cast<float(*)(int, float)>(static_cast<uintptr_t>(fn))(a1, a2);
}

float invoke_fii(int fn, int a1, int a2) {
  return reinterpret_cast<float(*)(int, int)>(static_cast<uintptr_t>(fn))(a1, a2);
}

float invoke_fiii(int fn, int a1, int a2, int a3) {
  return reinterpret_cast<float(*)(int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

float invoke_fiiii(int fn, int a1, int a2, int a3, int a4) {
  return reinterpret_cast<float(*)(int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_i(int fn) {
  return reinterpret_cast<int(*)()>(static_cast<uintptr_t>(fn))();
}

int invoke_idddii(int fn, double a1, double a2, double a3, int a4, int a5) {
  return reinterpret_cast<int(*)(double, double, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

int invoke_idddiii(int fn, double a1, double a2, double a3, int a4, int a5, int a6) {
  return reinterpret_cast<int(*)(double, double, double, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

int invoke_iddii(int fn, double a1, double a2, int a3, int a4) {
  return reinterpret_cast<int(*)(double, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iddiii(int fn, double a1, double a2, int a3, int a4, int a5) {
  return reinterpret_cast<int(*)(double, double, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

int invoke_idii(int fn, double a1, int a2, int a3) {
  return reinterpret_cast<int(*)(double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_idiiiiii(int fn, double a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  return reinterpret_cast<int(*)(double, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_ii(int fn, int a1) {
  return reinterpret_cast<int(*)(int)>(static_cast<uintptr_t>(fn))(a1);
}

int invoke_iid(int fn, int a1, double a2) {
  return reinterpret_cast<int(*)(int, double)>(static_cast<uintptr_t>(fn))(a1, a2);
}

int invoke_iidii(int fn, int a1, double a2, int a3, int a4) {
  return reinterpret_cast<int(*)(int, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iidiii(int fn, int a1, double a2, int a3, int a4, int a5) {
  return reinterpret_cast<int(*)(int, double, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

int invoke_iif(int fn, int a1, float a2) {
  return reinterpret_cast<int(*)(int, float)>(static_cast<uintptr_t>(fn))(a1, a2);
}

int invoke_iii(int fn, int a1, int a2) {
  return reinterpret_cast<int(*)(int, int)>(static_cast<uintptr_t>(fn))(a1, a2);
}

int invoke_iiid(int fn, int a1, int a2, double a3) {
  return reinterpret_cast<int(*)(int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_iiidi(int fn, int a1, int a2, double a3, int a4) {
  return reinterpret_cast<int(*)(int, int, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iiidii(int fn, int a1, int a2, double a3, int a4, int a5) {
  return reinterpret_cast<int(*)(int, int, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

int invoke_iiif(int fn, int a1, int a2, float a3) {
  return reinterpret_cast<int(*)(int, int, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_iiiff(int fn, int a1, int a2, float a3, float a4) {
  return reinterpret_cast<int(*)(int, int, float, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iiifi(int fn, int a1, int a2, float a3, int a4) {
  return reinterpret_cast<int(*)(int, int, float, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iiii(int fn, int a1, int a2, int a3) {
  return reinterpret_cast<int(*)(int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_iiiid(int fn, int a1, int a2, int a3, double a4) {
  return reinterpret_cast<int(*)(int, int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iiiidddi(int fn, int a1, int a2, int a3, double a4, double a5, double a6, int a7) {
  return reinterpret_cast<int(*)(int, int, int, double, double, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_iiiiff(int fn, int a1, int a2, int a3, float a4, float a5) {
  return reinterpret_cast<int(*)(int, int, int, float, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

int invoke_iiiifffffiiff(int fn, int a1, int a2, int a3, float a4, float a5, float a6, float a7, float a8, int a9, int a10, float a11, float a12) {
  return reinterpret_cast<int(*)(int, int, int, float, float, float, float, float, int, int, float, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

int invoke_iiiii(int fn, int a1, int a2, int a3, int a4) {
  return reinterpret_cast<int(*)(int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iiiiidiii(int fn, int a1, int a2, int a3, int a4, double a5, int a6, int a7, int a8) {
  return reinterpret_cast<int(*)(int, int, int, int, double, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

int invoke_iiiiiffii(int fn, int a1, int a2, int a3, int a4, float a5, float a6, int a7, int a8) {
  return reinterpret_cast<int(*)(int, int, int, int, float, float, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

int invoke_iiiiifii(int fn, int a1, int a2, int a3, int a4, float a5, int a6, int a7) {
  return reinterpret_cast<int(*)(int, int, int, int, float, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_iiiiii(int fn, int a1, int a2, int a3, int a4, int a5) {
  return reinterpret_cast<int(*)(int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

int invoke_iiiiiidd(int fn, int a1, int a2, int a3, int a4, int a5, double a6, double a7) {
  return reinterpret_cast<int(*)(int, int, int, int, int, double, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_iiiiiiff(int fn, int a1, int a2, int a3, int a4, int a5, float a6, float a7) {
  return reinterpret_cast<int(*)(int, int, int, int, int, float, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_iiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

int invoke_iiiiiiid(int fn, int a1, int a2, int a3, int a4, int a5, int a6, double a7) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_iiiiiiidddi(int fn, int a1, int a2, int a3, int a4, int a5, int a6, double a7, double a8, double a9, int a10) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, double, double, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

int invoke_iiiiiiiff(int fn, int a1, int a2, int a3, int a4, int a5, int a6, float a7, float a8) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, float, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

int invoke_iiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

int invoke_iiiiiiiif(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, float a8) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

int invoke_iiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

int invoke_iiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

int invoke_iiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

int invoke_iiiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

int invoke_iiiiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

int invoke_iiiiiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

int invoke_iiiiiiiiiiiiiiif(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, float a15) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, int, int, int, int, int, int, int, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
}

int invoke_iiiiiiiijj(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, long long a8, long long a9) {
  return reinterpret_cast<int(*)(int, int, int, int, int, int, int, long long, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

int invoke_iiiij(int fn, int a1, int a2, int a3, long long a4) {
  return reinterpret_cast<int(*)(int, int, int, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iiij(int fn, int a1, int a2, long long a3) {
  return reinterpret_cast<int(*)(int, int, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_iiijj(int fn, int a1, int a2, long long a3, long long a4) {
  return reinterpret_cast<int(*)(int, int, long long, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

int invoke_iij(int fn, int a1, long long a2) {
  return reinterpret_cast<int(*)(int, long long)>(static_cast<uintptr_t>(fn))(a1, a2);
}

int invoke_iiji(int fn, int a1, long long a2, int a3) {
  return reinterpret_cast<int(*)(int, long long, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_iijj(int fn, int a1, long long a2, long long a3) {
  return reinterpret_cast<int(*)(int, long long, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

int invoke_ij(int fn, long long a1) {
  return reinterpret_cast<int(*)(long long)>(static_cast<uintptr_t>(fn))(a1);
}

int invoke_ijiiii(int fn, long long a1, int a2, int a3, int a4, int a5) {
  return reinterpret_cast<int(*)(long long, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

long long invoke_j(int fn) {
  return reinterpret_cast<long long(*)()>(static_cast<uintptr_t>(fn))();
}

long long invoke_ji(int fn, int a1) {
  return reinterpret_cast<long long(*)(int)>(static_cast<uintptr_t>(fn))(a1);
}

long long invoke_jii(int fn, int a1, int a2) {
  return reinterpret_cast<long long(*)(int, int)>(static_cast<uintptr_t>(fn))(a1, a2);
}

long long invoke_jiii(int fn, int a1, int a2, int a3) {
  return reinterpret_cast<long long(*)(int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

long long invoke_jiiij(int fn, int a1, int a2, int a3, long long a4) {
  return reinterpret_cast<long long(*)(int, int, int, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

long long invoke_jij(int fn, int a1, long long a2) {
  return reinterpret_cast<long long(*)(int, long long)>(static_cast<uintptr_t>(fn))(a1, a2);
}

long long invoke_jjiii(int fn, long long a1, int a2, int a3, int a4) {
  return reinterpret_cast<long long(*)(long long, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_v(int fn) {
  reinterpret_cast<void(*)()>(static_cast<uintptr_t>(fn))();
}

void invoke_vi(int fn, int a1) {
  reinterpret_cast<void(*)(int)>(static_cast<uintptr_t>(fn))(a1);
}

void invoke_vid(int fn, int a1, double a2) {
  reinterpret_cast<void(*)(int, double)>(static_cast<uintptr_t>(fn))(a1, a2);
}

void invoke_vidd(int fn, int a1, double a2, double a3) {
  reinterpret_cast<void(*)(int, double, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_viddddi(int fn, int a1, double a2, double a3, double a4, double a5, int a6) {
  reinterpret_cast<void(*)(int, double, double, double, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_vidii(int fn, int a1, double a2, int a3, int a4) {
  reinterpret_cast<void(*)(int, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_vif(int fn, int a1, float a2) {
  reinterpret_cast<void(*)(int, float)>(static_cast<uintptr_t>(fn))(a1, a2);
}

void invoke_vii(int fn, int a1, int a2) {
  reinterpret_cast<void(*)(int, int)>(static_cast<uintptr_t>(fn))(a1, a2);
}

void invoke_viid(int fn, int a1, int a2, double a3) {
  reinterpret_cast<void(*)(int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_viidi(int fn, int a1, int a2, double a3, int a4) {
  reinterpret_cast<void(*)(int, int, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_viidii(int fn, int a1, int a2, double a3, int a4, int a5) {
  reinterpret_cast<void(*)(int, int, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

void invoke_viidiiii(int fn, int a1, int a2, double a3, int a4, int a5, int a6, int a7) {
  reinterpret_cast<void(*)(int, int, double, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

void invoke_viif(int fn, int a1, int a2, float a3) {
  reinterpret_cast<void(*)(int, int, float)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_viifii(int fn, int a1, int a2, float a3, int a4, int a5) {
  reinterpret_cast<void(*)(int, int, float, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

void invoke_viifiiii(int fn, int a1, int a2, float a3, int a4, int a5, int a6, int a7) {
  reinterpret_cast<void(*)(int, int, float, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

void invoke_viii(int fn, int a1, int a2, int a3) {
  reinterpret_cast<void(*)(int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_viiid(int fn, int a1, int a2, int a3, double a4) {
  reinterpret_cast<void(*)(int, int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_viiidddi(int fn, int a1, int a2, int a3, double a4, double a5, double a6, int a7) {
  reinterpret_cast<void(*)(int, int, int, double, double, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

void invoke_viiiddi(int fn, int a1, int a2, int a3, double a4, double a5, int a6) {
  reinterpret_cast<void(*)(int, int, int, double, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiidfi(int fn, int a1, int a2, int a3, double a4, float a5, int a6) {
  reinterpret_cast<void(*)(int, int, int, double, float, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiidi(int fn, int a1, int a2, int a3, double a4, int a5) {
  reinterpret_cast<void(*)(int, int, int, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

void invoke_viiidii(int fn, int a1, int a2, int a3, double a4, int a5, int a6) {
  reinterpret_cast<void(*)(int, int, int, double, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiifiii(int fn, int a1, int a2, int a3, float a4, int a5, int a6, int a7) {
  reinterpret_cast<void(*)(int, int, int, float, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

void invoke_viiii(int fn, int a1, int a2, int a3, int a4) {
  reinterpret_cast<void(*)(int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_viiiid(int fn, int a1, int a2, int a3, int a4, double a5) {
  reinterpret_cast<void(*)(int, int, int, int, double)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

void invoke_viiiidiiiiii(int fn, int a1, int a2, int a3, int a4, double a5, int a6, int a7, int a8, int a9, int a10, int a11) {
  reinterpret_cast<void(*)(int, int, int, int, double, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

void invoke_viiiii(int fn, int a1, int a2, int a3, int a4, int a5) {
  reinterpret_cast<void(*)(int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

void invoke_viiiiidddi(int fn, int a1, int a2, int a3, int a4, int a5, double a6, double a7, double a8, int a9) {
  reinterpret_cast<void(*)(int, int, int, int, int, double, double, double, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

void invoke_viiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6) {
  reinterpret_cast<void(*)(int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7);
}

void invoke_viiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

void invoke_viiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

void invoke_viiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

void invoke_viiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

void invoke_viiiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

void invoke_viiiiiiiiiiiii(int fn, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13) {
  reinterpret_cast<void(*)(int, int, int, int, int, int, int, int, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

void invoke_viiiiij(int fn, int a1, int a2, int a3, int a4, int a5, long long a6) {
  reinterpret_cast<void(*)(int, int, int, int, int, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiiiji(int fn, int a1, int a2, int a3, int a4, long long a5, int a6) {
  reinterpret_cast<void(*)(int, int, int, int, long long, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiij(int fn, int a1, int a2, int a3, long long a4) {
  reinterpret_cast<void(*)(int, int, int, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_viiijii(int fn, int a1, int a2, int a3, long long a4, int a5, int a6) {
  reinterpret_cast<void(*)(int, int, int, long long, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viiijjiiiii(int fn, int a1, int a2, int a3, long long a4, long long a5, int a6, int a7, int a8, int a9, int a10) {
  reinterpret_cast<void(*)(int, int, int, long long, long long, int, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

void invoke_viij(int fn, int a1, int a2, long long a3) {
  reinterpret_cast<void(*)(int, int, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_viiji(int fn, int a1, int a2, long long a3, int a4) {
  reinterpret_cast<void(*)(int, int, long long, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_viijii(int fn, int a1, int a2, long long a3, int a4, int a5) {
  reinterpret_cast<void(*)(int, int, long long, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5);
}

void invoke_viijiji(int fn, int a1, int a2, long long a3, int a4, long long a5, int a6) {
  reinterpret_cast<void(*)(int, int, long long, int, long long, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6);
}

void invoke_viijjiiii(int fn, int a1, int a2, long long a3, long long a4, int a5, int a6, int a7, int a8) {
  reinterpret_cast<void(*)(int, int, long long, long long, int, int, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4, a5, a6, a7, a8);
}

void invoke_vij(int fn, int a1, long long a2) {
  reinterpret_cast<void(*)(int, long long)>(static_cast<uintptr_t>(fn))(a1, a2);
}

void invoke_viji(int fn, int a1, long long a2, int a3) {
  reinterpret_cast<void(*)(int, long long, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_vijii(int fn, int a1, long long a2, int a3, int a4) {
  reinterpret_cast<void(*)(int, long long, int, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

void invoke_vijj(int fn, int a1, long long a2, long long a3) {
  reinterpret_cast<void(*)(int, long long, long long)>(static_cast<uintptr_t>(fn))(a1, a2, a3);
}

void invoke_vijji(int fn, int a1, long long a2, long long a3, int a4) {
  reinterpret_cast<void(*)(int, long long, long long, int)>(static_cast<uintptr_t>(fn))(a1, a2, a3, a4);
}

} // extern "C"
#endif // __EMSCRIPTEN__