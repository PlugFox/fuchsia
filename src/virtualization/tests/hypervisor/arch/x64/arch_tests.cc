// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vcpu.h>
#include <threads.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <string>

#include <gtest/gtest.h>

#include "src/virtualization/tests/hypervisor/hypervisor_tests.h"

namespace {

constexpr uint32_t kNmiVector = 2u;
constexpr uint32_t kGpFaultVector = 13u;
constexpr uint32_t kExceptionVector = 16u;

DECLARE_TEST_FUNCTION(vcpu_read_write_state)
DECLARE_TEST_FUNCTION(vcpu_interrupt)
DECLARE_TEST_FUNCTION(vcpu_hlt)
DECLARE_TEST_FUNCTION(vcpu_pause)
DECLARE_TEST_FUNCTION(vcpu_write_cr0)
DECLARE_TEST_FUNCTION(vcpu_write_invalid_cr0)
DECLARE_TEST_FUNCTION(vcpu_compat_mode)
DECLARE_TEST_FUNCTION(vcpu_syscall)
DECLARE_TEST_FUNCTION(vcpu_sysenter)
DECLARE_TEST_FUNCTION(vcpu_sysenter_compat)
DECLARE_TEST_FUNCTION(vcpu_vmcall_invalid_number)
DECLARE_TEST_FUNCTION(vcpu_vmcall_invalid_cpl)
DECLARE_TEST_FUNCTION(vcpu_extended_registers)
DECLARE_TEST_FUNCTION(guest_set_trap_with_io)

void SetupAndInterrupt(TestCase* test, const char* start, const char* end) {
  ASSERT_NO_FATAL_FAILURE(SetupGuest(test, start, end));
  test->interrupts_enabled = true;

  thrd_t thread;
  int ret = thrd_create(
      &thread,
      [](void* ctx) -> int {
        TestCase* test = static_cast<TestCase*>(ctx);
        return test->vcpu.interrupt(kInterruptVector) == ZX_OK ? thrd_success : thrd_error;
      },
      test);
  ASSERT_EQ(ret, thrd_success);
}

TEST(Guest, VcpuReadWriteState) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_read_write_state_start, vcpu_read_write_state_end));

  zx_vcpu_state_t vcpu_state = {
      .rax = 1u,
      .rcx = 2u,
      .rdx = 3u,
      .rbx = 4u,
      .rsp = 5u,
      .rbp = 6u,
      .rsi = 7u,
      .rdi = 8u,
      .r8 = 9u,
      .r9 = 10u,
      .r10 = 11u,
      .r11 = 12u,
      .r12 = 13u,
      .r13 = 14u,
      .r14 = 15u,
      .r15 = 16u,
      .rflags = 0,
  };

  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  EXPECT_EQ(vcpu_state.rax, 2u);
  EXPECT_EQ(vcpu_state.rcx, 4u);
  EXPECT_EQ(vcpu_state.rdx, 6u);
  EXPECT_EQ(vcpu_state.rbx, 8u);
  EXPECT_EQ(vcpu_state.rsp, 10u);
  EXPECT_EQ(vcpu_state.rbp, 12u);
  EXPECT_EQ(vcpu_state.rsi, 14u);
  EXPECT_EQ(vcpu_state.rdi, 16u);
  EXPECT_EQ(vcpu_state.r8, 18u);
  EXPECT_EQ(vcpu_state.r9, 20u);
  EXPECT_EQ(vcpu_state.r10, 22u);
  EXPECT_EQ(vcpu_state.r11, 24u);
  EXPECT_EQ(vcpu_state.r12, 26u);
  EXPECT_EQ(vcpu_state.r13, 28u);
  EXPECT_EQ(vcpu_state.r14, 30u);
  EXPECT_EQ(vcpu_state.r15, 32u);
  EXPECT_EQ(vcpu_state.rflags, (1u << 0) | (1u << 18));
}

TEST(Guest, VcpuInterruptPriority) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  // Check that interrupts have higher priority than exceptions.
  ASSERT_EQ(test.vcpu.interrupt(kExceptionVector), ZX_OK);
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kInterruptVector);

  // TODO(fxbug.dev/12585): Check that the exception is cleared.
}

TEST(Guest, VcpuNmi) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  // Check that NMIs are handled.
  ASSERT_EQ(test.vcpu.interrupt(kNmiVector), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kNmiVector);
}

TEST(Guest, VcpuNmiPriority) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  // Check that NMIs have higher priority than interrupts.
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);
  ASSERT_EQ(test.vcpu.interrupt(kNmiVector), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kNmiVector);

  // TODO(fxbug.dev/12585): Check that the interrupt is queued.
}

TEST(Guest, VcpuException) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  // Check that exceptions are handled.
  ASSERT_EQ(test.vcpu.interrupt(kExceptionVector), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kExceptionVector);
}

TEST(Guest, VcpuHlt) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupAndInterrupt(&test, vcpu_hlt_start, vcpu_hlt_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuPause) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_pause_start, vcpu_pause_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuWriteCr0) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_write_cr0_start, vcpu_write_cr0_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  // Check that the initial value of cr0, which was read into rbx, has the
  // correct initial values for the bits in the guest/host mask.
  EXPECT_EQ(vcpu_state.rbx & (X86_CR0_NE | X86_CR0_NW | X86_CR0_CD),
            static_cast<uint64_t>(X86_CR0_CD));

  // Check that the updated value of cr0, which was read into rax, correctly shadows the values in
  // the guest/host mask.
  EXPECT_EQ(vcpu_state.rax & (X86_CR0_NE | X86_CR0_CD), static_cast<uint64_t>(X86_CR0_NE));
}

TEST(Guest, VcpuWriteInvalidCr0) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_write_invalid_cr0_start, vcpu_write_invalid_cr0_end));

  test.interrupts_enabled = true;

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kGpFaultVector);
}

TEST(Guest, VcpuCompatMode) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_compat_mode_start, vcpu_compat_mode_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rbx, 1u);
  EXPECT_EQ(vcpu_state.rcx, 2u);
}

TEST(Guest, VcpuSyscall) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_syscall_start, vcpu_syscall_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuSysenter) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_sysenter_start, vcpu_sysenter_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuSysenterCompat) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_sysenter_compat_start, vcpu_sysenter_compat_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuVmcallInvalidNumber) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_vmcall_invalid_number_start, vcpu_vmcall_invalid_number_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  const uint64_t kUnknownHypercall = -1000;
  EXPECT_EQ(vcpu_state.rax, kUnknownHypercall);
}

TEST(Guest, VcpuVmcallInvalidCpl) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_vmcall_invalid_cpl_start, vcpu_vmcall_invalid_cpl_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  const uint64_t kNotPermitted = -1;
  EXPECT_EQ(vcpu_state.rax, kNotPermitted);
}

TEST(Guest, VcpuExtendedRegisters) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_extended_registers_start, vcpu_extended_registers_end));

  // Guest sets xmm0.
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  // Clear host xmm0.
  __asm__("xorps %%xmm0, %%xmm0" ::: "xmm0");

  // Guest reads xmm0 into rax:rbx.
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

  // Check that the host xmm0 is restored to zero.
  bool xmm0_is_zero;
  __asm__(
      "ptest %%xmm0, %%xmm0\n"
      "sete %0"
      : "=q"(xmm0_is_zero));
  EXPECT_TRUE(xmm0_is_zero);

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, 0x89abcdef01234567u);
  EXPECT_EQ(vcpu_state.rbx, 0x76543210fedcba98u);

  // Guest disables SSE
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
  // Guest successfully runs again
  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

// Verify that write_state with ZX_VCPU_IO only accepts valid access sizes.
TEST(Guest, VcpuWriteStateIoInvalidSize) {
  TestCase test;
  // Passing nullptr for start and end since we don't need to actually run the guest for this test.
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, nullptr, nullptr));

  // valid access sizes
  zx_vcpu_io_t io{};
  io.access_size = 1;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_OK);
  io.access_size = 2;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_OK);
  io.access_size = 4;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_OK);

  // invalid access sizes
  io.access_size = 0;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
  io.access_size = 3;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
  io.access_size = 5;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
  io.access_size = 255;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
}

TEST(Guest, GuestSetTrapWithIo) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, guest_set_trap_with_io_start, guest_set_trap_with_io_end));

  // Trap on writes to TRAP_PORT.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_IO, TRAP_PORT, 1, zx::port(), kTrapKey), ZX_OK);

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
  EXPECT_EQ(packet.guest_io.port, TRAP_PORT);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

}  // namespace